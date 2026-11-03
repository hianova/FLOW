#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "bitspace.h"
#include "cubical_hott.h"
#include "flow.h"
#include "flow_asm_sieve.h"
#include "flow_bridge.h"
#include "flow_gpu.h"
#include "flow_hw_probe.h"
#include "flow_jet.h"
#include "hott_test_kit.h"

#include <limits.h>
#include <math.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static inline uint64_t test_clock_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* Instantiate 4D Q16 Polyhedron for PoC 4 & PoC 5 comparisons */
FLOW_QPOLY_DEFINE(FlowQPoly4D_S16, 4, 16)

/* =========================================================================
 * Legacy C Baseline Helper for PoC 5 (FlowBoxConstraint + FLOW_VERIFY_INVARIANTS)
 * ========================================================================= */
static __attribute__((noinline)) uint64_t legacy_c_invariant_step(
    const int64_t a_q16[4], int64_t b_q16, const int64_t x_q16[4], FlowJet2 prev, FlowJet2 next,
    double dt, int64_t a, int64_t b, const uint64_t *arena, uint64_t raw_idx, uint64_t mask,
    FlowInvariantAttestation *att) {
    /* 1. C integer check with explicit branch to avoid UB */
    int64_t ovf_flag = 0;
    int64_t sum = 0;
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) {
        ovf_flag = 1;
    } else {
        sum = a + b;
    }

    /* 2. C bounds check with explicit branch */
    uint64_t loaded = (raw_idx <= mask) ? arena[raw_idx] : arena[raw_idx & mask];

    /* 3. C 4D plane dot product */
    int64_t dot = ((a_q16[0] * x_q16[0]) + (a_q16[1] * x_q16[1]) + (a_q16[2] * x_q16[2]) +
                   (a_q16[3] * x_q16[3])) >>
                  16;
    int64_t slack = b_q16 - dot;

    /* 4. C Taylor residual check */
    double v_pred = prev.v + prev.d1 * dt + 0.5 * prev.d2 * dt * dt;
    double d1_pred = prev.d1 + 0.5 * (prev.d2 + next.d2) * dt;
    int64_t r0_q16 = (int64_t)(fabs(next.v - v_pred) * 65536.0);
    int64_t r1_q16 = (int64_t)(fabs(next.d1 - d1_pred) * 65536.0);

    /* 5. Legacy FlowBoxConstraint table + FLOW_VERIFY_INVARIANTS string formatting */
    const FlowBoxConstraint rules[4] = {
        FLOW_INVARIANT_RULE("alu_no_overflow", ovf_flag, 0, 0, FLOW_BOX_THEOREM_DETERMINISM,
                            "signed overflow"),
        FLOW_INVARIANT_RULE("arena_bounds", (int64_t)raw_idx, 0, (int64_t)mask,
                            FLOW_BOX_THEOREM_BUFFER_BOUNDS, "index out of bounds"),
        FLOW_INVARIANT_RULE("qpoly_slack", slack, 0, INT64_MAX, FLOW_BOX_THEOREM_MEMORY_QUOTA,
                            "hyperplane violated"),
        FLOW_INVARIANT_RULE("jet2_taylor", r0_q16 + r1_q16, 0, 64, FLOW_BOX_THEOREM_DETERMINISM,
                            "Taylor residual exceeded")};

    FlowInvariantResult res =
        FLOW_VERIFY_INVARIANTS("legacy_step", att, rules, "Verified (sum=%lld, slack=%lld)",
                               (long long)sum, (long long)slack);

    return (res == FLOW_INVARIANT_VERIFIED) ? ((uint64_t)sum ^ loaded ^ (uint64_t)slack) : 0ULL;
}

/* =========================================================================
 * ASM + HoTT + 2-Jet Self-Bootstrapping Pipeline Helper for PoC 5
 * ========================================================================= */
static __attribute__((noinline)) uint64_t asm_hott_jet2_bootstrap_step(
    const int64_t a_q16[4], int64_t b_q16, const int64_t x_q16[4], FlowJet2 prev, FlowJet2 next,
    double dt, int64_t a, int64_t b, const uint64_t *arena, uint64_t raw_idx, uint64_t mask,
    uint64_t *out_sieve) {
    uint64_t sieve = FLOW_ASM_SIEVE_ALL_SOUND;

    FlowResultU64 r_add = flow_asm_qadd_i64(a, b, &sieve);
    uint64_t loaded = flow_asm_arena_load_u64(arena, raw_idx, mask, &sieve);
    int64_t slack = flow_asm_qpoly4d_eval_plane(a_q16, b_q16, x_q16, &sieve);
    uint64_t final_sieve = flow_asm_jet2_self_check_step(prev, next, dt, 32, 32, &sieve);

    *out_sieve = final_sieve;
    return r_add.value ^ loaded ^ (uint64_t)slack ^ final_sieve;
}

int main(int argc, char **argv) {
    bool report_mode = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--report") == 0)
            report_mode = true;
    }

    FLOW_TEST_SUITE_BEGIN(
        "90% ASM + Only HoTT & 2-Jet Self-Checking & Self-Bootstrapping PoC (1-5)");

    /* =====================================================================
     * PoC 1: Zero-UB ASM Integer Arithmetic vs C Signed Overflow
     * ===================================================================== */
    FLOW_STAGE_BEGIN(1, "PoC 1: Zero-UB ASM Integer Arithmetic (adds/subs/smulh -> HoTT Sieve)");
    {
        /* 1a. Interior addition */
        uint64_t sieve = FLOW_ASM_SIEVE_ALL_SOUND;
        FlowResultU64 r_ok = flow_asm_qadd_i64(1000000LL, 2000000LL, &sieve);
        FLOW_ASSERT_FALSE(flow_result_u64_is_boundary(r_ok));
        FLOW_ASSERT_EQ(r_ok.value, 3000000ULL);
        FLOW_ASSERT_EQ(sieve, FLOW_ASM_SIEVE_ALL_SOUND);

        /* 1b. Positive signed overflow: INT64_MAX + 1 (Zero C UB!) */
        uint64_t sieve_ovf = FLOW_ASM_SIEVE_ALL_SOUND;
        FlowResultU64 r_ovf = flow_asm_qadd_i64(INT64_MAX, 1LL, &sieve_ovf);
        FLOW_ASSERT_TRUE(flow_result_u64_is_boundary(r_ovf));
        FLOW_ASSERT_EQ(r_ovf.value, (uint64_t)INT64_MIN);
        FLOW_ASSERT_EQ(sieve_ovf & FLOW_ASM_SIEVE_INT_NO_OVERFLOW, 0ULL);
        FLOW_ASSERT_EQ((uint64_t)flow_hott_boundary_tag_code(r_ovf.boundary_tag), 1ULL);

        /* 1c. Negative signed underflow: INT64_MIN - 1 (Zero C UB!) */
        uint64_t sieve_udf = FLOW_ASM_SIEVE_ALL_SOUND;
        FlowResultU64 r_udf = flow_asm_qsub_i64(INT64_MIN, 1LL, &sieve_udf);
        FLOW_ASSERT_TRUE(flow_result_u64_is_boundary(r_udf));
        FLOW_ASSERT_EQ(r_udf.value, (uint64_t)INT64_MAX);
        FLOW_ASSERT_EQ(sieve_udf & FLOW_ASM_SIEVE_INT_NO_OVERFLOW, 0ULL);

        /* 1d. 128-bit Q48.16 fixed-point multiply: interior vs 64-bit overflow */
        uint64_t sieve_mul_ok = FLOW_ASM_SIEVE_ALL_SOUND;
        int64_t three_q16 = 3LL << 16;
        int64_t seven_q16 = 7LL << 16;
        FlowResultU64 r_mul_ok = flow_asm_qmul_shift16_i64(three_q16, seven_q16, &sieve_mul_ok);
        FLOW_ASSERT_FALSE(flow_result_u64_is_boundary(r_mul_ok));
        FLOW_ASSERT_EQ((int64_t)r_mul_ok.value, 21LL << 16);
        FLOW_ASSERT_EQ(sieve_mul_ok, FLOW_ASM_SIEVE_ALL_SOUND);

        uint64_t sieve_mul_ovf = FLOW_ASM_SIEVE_ALL_SOUND;
        FlowResultU64 r_mul_ovf =
            flow_asm_qmul_shift16_i64(INT64_MAX >> 4, INT64_MAX >> 4, &sieve_mul_ovf);
        FLOW_ASSERT_TRUE(flow_result_u64_is_boundary(r_mul_ovf));
        FLOW_ASSERT_EQ(sieve_mul_ovf & FLOW_ASM_SIEVE_INT_NO_OVERFLOW, 0ULL);
    }

    /* =====================================================================
     * PoC 2: Hardware FPU Exception Status Register (FPSR/MXCSR) -> HoTT
     * ===================================================================== */
    FLOW_STAGE_BEGIN(2, "PoC 2: Hardware FPU Exception Status Register (FPSR/MXCSR -> HoTT Sieve)");
    {
        /* 2a. Valid 2-Jet maintains all FPU sieve bits */
        flow_asm_fpsr_clear();
        uint64_t sieve_clean = FLOW_ASM_SIEVE_ALL_SOUND;
        FlowJet2 valid_jet = {1.25, -0.5, 2.0};
        uint64_t res_clean = flow_asm_fpu_check_jet2(valid_jet, &sieve_clean);
        FLOW_ASSERT_EQ(res_clean, FLOW_ASM_SIEVE_ALL_SOUND);

        /* 2b. Hardware 1.0 / 0.0 triggers DZC / INF and clears FPU sieve bits */
        flow_asm_fpsr_clear();
        uint64_t sieve_div0 = FLOW_ASM_SIEVE_ALL_SOUND;
        double inf_val = flow_asm_fdiv_f64(1.0, 0.0);
        FlowJet2 div0_jet = {inf_val, 0.0, 0.0};
        (void)flow_asm_fpu_check_jet2(div0_jet, &sieve_div0);
        FLOW_ASSERT_EQ(sieve_div0 & FLOW_ASM_SIEVE_FPU_NO_DIVZERO, 0ULL);
        FLOW_ASSERT_EQ(sieve_div0 & FLOW_ASM_SIEVE_FPU_NO_OVERFLOW, 0ULL);

        /* 2c. Hardware 0.0 / 0.0 triggers IOC / NaN and clears FLOW_ASM_SIEVE_FPU_NO_INVALID */
        flow_asm_fpsr_clear();
        uint64_t sieve_nan = FLOW_ASM_SIEVE_ALL_SOUND;
        double nan_val = flow_asm_fdiv_f64(0.0, 0.0);
        FlowJet2 nan_jet = {nan_val, 1.0, 0.0};
        (void)flow_asm_fpu_check_jet2(nan_jet, &sieve_nan);
        FLOW_ASSERT_EQ(sieve_nan & FLOW_ASM_SIEVE_FPU_NO_INVALID, 0ULL);

        /* 2d. HoTT Quiet NaN payload injection clears FLOW_ASM_SIEVE_FPU_NO_INVALID */
        flow_asm_fpsr_clear();
        uint64_t sieve_hott_nan = FLOW_ASM_SIEVE_ALL_SOUND;
        double qnan_v = flow_hott_make_boundary_nan(
            FLOW_HOTT_BOUNDARY_OVERFLOW, 2U, (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY, 0U, 0xCAFU);
        FlowJet2 hott_nan_jet = {qnan_v, 0.0, 0.0};
        (void)flow_asm_fpu_check_jet2(hott_nan_jet, &sieve_hott_nan);
        FLOW_ASSERT_EQ(sieve_hott_nan & FLOW_ASM_SIEVE_FPU_NO_INVALID, 0ULL);
        flow_asm_fpsr_clear();
    }

    /* =====================================================================
     * PoC 3: Branchless ASM Masked Arena Memory Load/Store (Zero Segfault)
     * ===================================================================== */
    FLOW_STAGE_BEGIN(3, "PoC 3: Branchless ASM Masked Arena Memory Load/Store (Zero Segfault)");
    {
        FlowUmaBuffer uma;
        memset(&uma, 0, sizeof(uma));
        int rc = flow_uma_buffer_alloc(&uma, FLOW_HW_PAGE_BYTES);
        FLOW_ASSERT_EQ(rc, 0);
        FLOW_ASSERT_EQ((uintptr_t)uma.cpu_ptr & (FLOW_HW_CACHE_LINE_BYTES - 1U), 0ULL);

        uint64_t *arena = (uint64_t *)uma.cpu_ptr;
        const uint64_t mask = 0xFFULL; /* 256 uint64_t slots */

        /* 3a. Valid in-bounds stores and loads across all 256 slots */
        uint64_t sieve_mem = FLOW_ASM_SIEVE_ALL_SOUND;
        for (uint64_t idx = 0; idx <= mask; ++idx) {
            uint64_t val = 0x9E3779B97F4A7C15ULL ^ (idx * 0x10001ULL);
            flow_asm_arena_store_u64(arena, idx, mask, val, &sieve_mem);
            uint64_t ld = flow_asm_arena_load_u64(arena, idx, mask, &sieve_mem);
            FLOW_ASSERT_EQ(ld, val);
        }
        FLOW_ASSERT_EQ(sieve_mem, FLOW_ASM_SIEVE_ALL_SOUND);

        /* 3b. Out-of-bounds wild index injection: zero segfault, wrapped access, sieve cleared */
        const uint64_t wild_indices[3] = {256ULL, 0xDEADBEEFULL, UINT64_MAX};
        for (size_t k = 0; k < 3; ++k) {
            uint64_t wild = wild_indices[k];
            uint64_t expected_slot = wild & mask;
            uint64_t expected_val = 0x9E3779B97F4A7C15ULL ^ (expected_slot * 0x10001ULL);

            uint64_t sv_load = FLOW_ASM_SIEVE_ALL_SOUND;
            uint64_t r_ld = flow_asm_arena_load_u64(arena, wild, mask, &sv_load);
            FLOW_ASSERT_EQ(r_ld, expected_val);
            FLOW_ASSERT_EQ(sv_load & FLOW_ASM_SIEVE_MEM_MASKED_INBOUNDS, 0ULL);
            FLOW_ASSERT_NE(sv_load & FLOW_ASM_SIEVE_MEM_ALIGNED, 0ULL);

            uint64_t sv_store = FLOW_ASM_SIEVE_ALL_SOUND;
            flow_asm_arena_store_u64(arena, wild, mask, expected_val, &sv_store);
            FLOW_ASSERT_EQ(sv_store & FLOW_ASM_SIEVE_MEM_MASKED_INBOUNDS, 0ULL);
        }

        flow_uma_buffer_free(&uma);
    }

    /* =====================================================================
     * PoC 4: Self-Bootstrapping 2-Jet Taylor Consistency & QPoly equivalence
     * ===================================================================== */
    FLOW_STAGE_BEGIN(4, "PoC 4: Self-Bootstrapping 2-Jet Taylor Consistency & QPoly Equivalence");
    {
        /* 4a. Analytical harmonic oscillator x(t) = cos(w*t), w = 1.0, dt = 0.01 for 100 steps */
        const double omega = 1.0;
        const double dt = 0.01;
        flow_asm_fpsr_clear();

        FlowJet2 prev = {1.0, 0.0, -omega * omega};
        uint64_t running_sieve = FLOW_ASM_SIEVE_ALL_SOUND;

        for (int step = 1; step <= 100; ++step) {
            double t = (double)step * dt;
            FlowJet2 next = {cos(omega * t), -omega * sin(omega * t),
                             -omega * omega * cos(omega * t)};
            running_sieve = flow_asm_jet2_self_check_step(prev, next, dt, 2, 2, &running_sieve);
            prev = next;
        }
        FLOW_ASSERT_EQ(running_sieve, FLOW_ASM_SIEVE_ALL_SOUND);

        /* 4b. Fault Injection 1: Silent Memory Bit-Flip in next.v (bit 50) */
        double t_101 = 101.0 * dt;
        FlowJet2 corrupted = {cos(omega * t_101), -omega * sin(omega * t_101),
                              -omega * omega * cos(omega * t_101)};
        uint64_t v_bits = 0;
        memcpy(&v_bits, &corrupted.v, sizeof(v_bits));
        v_bits ^= (1ULL << 50);
        memcpy(&corrupted.v, &v_bits, sizeof(corrupted.v));

        uint64_t fault1_sieve = FLOW_ASM_SIEVE_ALL_SOUND;
        (void)flow_asm_jet2_self_check_step(prev, corrupted, dt, 2, 2, &fault1_sieve);
        FLOW_ASSERT_EQ(fault1_sieve & FLOW_ASM_SIEVE_JET2_TAYLOR_0, 0ULL);

        /* Verify cubical neighbor fallback finds the first sound recovery axis via CTZ */
        uint32_t broken_bit = (uint32_t)__builtin_ctzll(~fault1_sieve & FLOW_ASM_SIEVE_ALL_SOUND);
        FLOW_ASSERT_EQ(1ULL << broken_bit, (uint64_t)FLOW_ASM_SIEVE_JET2_TAYLOR_0);
        uint64_t next_mask = 0ULL;
        FlowKanStatus kan = flow_cubical_step_neighbor(fault1_sieve, broken_bit, 0ULL, &next_mask);
        FLOW_ASSERT_EQ((int64_t)kan, (int64_t)FLOW_KAN_FILLED_HOMOTOPIC);
        FLOW_ASSERT_EQ(next_mask, FLOW_ASM_SIEVE_ALL_SOUND);

        /* 4c. Fault Injection 2: Discretization / Tunneling Step Breakdown (dt = 2.5) */
        double huge_dt = 2.5;
        FlowJet2 jump_next = {cos(omega * huge_dt), -omega * sin(omega * huge_dt),
                              -omega * omega * cos(omega * huge_dt)};
        int64_t barrier_a[4] = {1LL << 16, 0, 0, 0};
        int64_t barrier_b = -50000LL; /* x_0 <= -0.7629 */
        int64_t jump_x[4] = {0, 0, 0, 0};
        uint64_t tunnel_sieve = FLOW_ASM_SIEVE_ALL_SOUND;
        int64_t jump_slack =
            flow_asm_qpoly4d_eval_plane(barrier_a, barrier_b, jump_x, &tunnel_sieve);
        FLOW_ASSERT_TRUE(jump_slack < 0);
        (void)flow_asm_jet2_self_check_step(prev, jump_next, huge_dt, 2, 2, &tunnel_sieve);
        FLOW_ASSERT_EQ(tunnel_sieve & FLOW_ASM_SIEVE_QPOLY_INTERIOR, 0ULL);
        FLOW_ASSERT_EQ(tunnel_sieve & FLOW_ASM_SIEVE_JET2_TAYLOR_0, 0ULL);
        FLOW_ASSERT_EQ(tunnel_sieve & FLOW_ASM_SIEVE_JET2_TAYLOR_1, 0ULL);

        /* 4d. Fault Injection 3: ASM vs C FlowQPoly4D_S16_eval Bit-Exact Equivalence (10,000 pts)
         */
        FlowQPoly4D_S16 poly;
        FlowQPoly4D_S16_init(&poly);
        poly.A[0][0] = 12345;
        poly.A[0][1] = -23456;
        poly.A[0][2] = 34567;
        poly.A[0][3] = -45678;
        atomic_store_explicit(&poly.b[0], 50000000LL, memory_order_relaxed);
        atomic_store_explicit(&poly.active_mask, 1ULL, memory_order_release);

        /* Pre-scale a_q16 by 65536 so ((a_q16 . x) >> 16) matches FlowQPoly4D_S16_eval bit-for-bit
         */
        int64_t a_q16[4] = {(int64_t)poly.A[0][0] * 65536LL, (int64_t)poly.A[0][1] * 65536LL,
                            (int64_t)poly.A[0][2] * 65536LL, (int64_t)poly.A[0][3] * 65536LL};
        int64_t b_q16 = 50000000LL;

        uint64_t prng = 0xCAFEBABE12345678ULL;
        uint64_t interior_count = 0;
        uint64_t boundary_count = 0;
        for (uint32_t iter = 0; iter < 10000; ++iter) {
            int32_t x_i32[4];
            int64_t x_i64[4];
            for (int d = 0; d < 4; ++d) {
                prng = flow_prng_next(&prng);
                x_i32[d] = (int32_t)(prng & 0xFFFFULL) - 32768;
                x_i64[d] = (int64_t)x_i32[d];
            }

            FlowQPolyEval c_eval = FlowQPoly4D_S16_eval(&poly, x_i32);
            uint64_t asm_sieve = FLOW_ASM_SIEVE_ALL_SOUND;
            int64_t asm_slack = flow_asm_qpoly4d_eval_plane(a_q16, b_q16, x_i64, &asm_sieve);

            bool c_violated = (c_eval.violated_mask & 1ULL) != 0ULL;
            bool asm_violated = (asm_slack < 0);
            bool sieve_cleared = (asm_sieve & FLOW_ASM_SIEVE_QPOLY_INTERIOR) == 0ULL;

            FLOW_ASSERT_TRUE(c_violated == asm_violated);
            FLOW_ASSERT_TRUE(c_violated == sieve_cleared);
            if (c_violated) {
                FLOW_ASSERT_EQ(-asm_slack, c_eval.worst_violation);
                boundary_count++;
            } else {
                FLOW_ASSERT_EQ(asm_slack, c_eval.min_slack);
                interior_count++;
            }
        }
        FLOW_ASSERT_TRUE(interior_count > 1000 && boundary_count > 1000);
    }

    /* =====================================================================
     * PoC 5: Performance & Footprint Benchmark (1,000,000 Iterations)
     * ===================================================================== */
    FLOW_STAGE_BEGIN(5, "PoC 5: Performance & Footprint Benchmark (Legacy C vs ASM+HoTT+2-Jet)");
    {
        const uint64_t iters = 1000000ULL;
        FlowInvariantAttestation att;
        memset(&att, 0, sizeof(att));

        FlowUmaBuffer uma_bench;
        memset(&uma_bench, 0, sizeof(uma_bench));
        int rc = flow_uma_buffer_alloc(&uma_bench, FLOW_HW_PAGE_BYTES);
        FLOW_ASSERT_EQ(rc, 0);
        uint64_t *arena = (uint64_t *)uma_bench.cpu_ptr;
        for (uint64_t i = 0; i < 256; ++i)
            arena[i] = i * 7ULL + 1ULL;

        int64_t a_q16[4] = {1024LL * 65536LL, -512LL * 65536LL, 256LL * 65536LL, -128LL * 65536LL};
        int64_t b_q16 = 100000000LL;
        int64_t x_q16[4] = {10, 20, 30, 40};

        const double dt = 0.001;
        FlowJet2 prev = {1.0, 0.0, -1.0};
        FlowJet2 next = {cos(dt), -sin(dt), -cos(dt)};

        /* Benchmark 1: Legacy C FlowBoxConstraint + FLOW_VERIFY_INVARIANTS */
        uint64_t legacy_sink = 0ULL;
        uint64_t t0_legacy = test_clock_ns();
        for (uint64_t i = 0; i < iters; ++i) {
            legacy_sink ^= legacy_c_invariant_step(a_q16, b_q16, x_q16, prev, next, dt, (int64_t)i,
                                                   42LL, arena, i & 0xFFULL, 0xFFULL, &att);
        }
        uint64_t t1_legacy = test_clock_ns();
        double legacy_ns_per_op = (double)(t1_legacy - t0_legacy) / (double)iters;

        /* Benchmark 2: Pure ASM + HoTT + 2-Jet Self-Bootstrapping Pipeline */
        flow_asm_fpsr_clear();
        uint64_t asm_sink = 0ULL;
        uint64_t out_sieve = FLOW_ASM_SIEVE_ALL_SOUND;
        uint64_t t0_asm = test_clock_ns();
        for (uint64_t i = 0; i < iters; ++i) {
            asm_sink ^=
                asm_hott_jet2_bootstrap_step(a_q16, b_q16, x_q16, prev, next, dt, (int64_t)i, 42LL,
                                             arena, i & 0xFFULL, 0xFFULL, &out_sieve);
        }
        uint64_t t1_asm = test_clock_ns();
        double asm_ns_per_op = (double)(t1_asm - t0_asm) / (double)iters;

        flow_uma_buffer_free(&uma_bench);

        FLOW_ASSERT_NE(legacy_sink, 0ULL);
        FLOW_ASSERT_NE(asm_sink, 0ULL);
        FLOW_ASSERT_EQ(out_sieve, FLOW_ASM_SIEVE_ALL_SOUND);

        size_t legacy_footprint = sizeof(FlowInvariantAttestation) + 4U * sizeof(FlowBoxConstraint);
        size_t asm_footprint = sizeof(uint64_t); /* Single 64-bit sieve register */
        double speedup = (asm_ns_per_op > 0.0) ? (legacy_ns_per_op / asm_ns_per_op) : 1.0;

        if (report_mode) {
            printf("\n");
            printf(
                "  +-----------------------------------------------------------------------------+\n");
            printf(
                "  | PoC 5: Legacy C Attestation vs 90%% ASM + HoTT + 2-Jet Self-Check (1M iters) |\n");
            printf(
                "  +--------------------------------------+------------------+-------------------+\n");
            printf(
                "  | Metric                               | Legacy C String  | ASM + HoTT + 2Jet |\n");
            printf(
                "  +--------------------------------------+------------------+-------------------+\n");
            printf("  | Latency per Full Verification Step   | %11.2f ns/op | %12.2f ns/op |\n",
                   legacy_ns_per_op, asm_ns_per_op);
            printf("  | Relative Throughput Speedup          |            1.00x | %16.2fx |\n",
                   speedup);
            printf("  | Verification State Footprint         | %14zu B | %15zu B |\n",
                   legacy_footprint, asm_footprint);
            printf(
                "  | C Signed Overflow / Shift UB Risk    |      Non-Zero UB |     Zero (ISA Z/V)|\n");
            printf(
                "  +--------------------------------------+------------------+-------------------+\n");
        }
    }

    FLOW_TEST_SUITE_END();
}
