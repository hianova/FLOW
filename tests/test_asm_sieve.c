#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "hott_test_kit.h"

#include <limits.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

FLOW_QPOLY_DEFINE(FlowQPoly4D_S16, 4, 16)

static __attribute__((noinline)) uint64_t asm_hott_jet2_bootstrap_step(
    const int64_t a_q16[4], int64_t b_q16, const int64_t x_q16[4], FlowJet2 prev, FlowJet2 next,
    int64_t dt_q16, int64_t a, int64_t b, const uint64_t *arena, uint64_t raw_idx, uint64_t mask,
    uint64_t *out_sieve) {
    uint64_t sieve = FLOW_ASM_SIEVE_ALL_SOUND;

    FlowResultU64 r_add = flow_asm_qadd_i64(a, b, &sieve);
    uint64_t loaded = flow_asm_arena_load_u64(arena, raw_idx, mask, &sieve);
    int64_t slack = flow_asm_qpoly4d_eval_plane(a_q16, b_q16, x_q16, &sieve);
    uint64_t final_sieve = flow_asm_jet2_self_check_step(prev, next, dt_q16, 32, 32, &sieve);

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
        "FLOW v4: Total ASM Substrate, Hardware Sieve & Self-Bootstrapping Suite");

    /* =====================================================================
     * Stage 1: Zero-UB ASM Signed Q-Arithmetic (adds/subs/smull/smulh)
     * ===================================================================== */
    FLOW_STAGE_BEGIN(1, "Zero-UB ASM Signed Q-Arithmetic (adds/subs/smull/smulh -> HoTT Sieve)");
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

        /* 1e. D-dimensional ASM inner product (flow_asm_qdot_i32): interior vs saturation */
        const int32_t a_vec[4] = {100, -200, 300, -400};
        const int32_t x_vec[4] = {10, 20, 30, 40};
        uint64_t sieve_dot_ok = FLOW_ASM_SIEVE_ALL_SOUND;
        FlowResultU64 r_dot_ok = flow_asm_qdot_i32(a_vec, x_vec, 4U, &sieve_dot_ok);
        FLOW_ASSERT_FALSE(flow_result_u64_is_boundary(r_dot_ok));
        FLOW_ASSERT_EQ((int64_t)r_dot_ok.value, -10000LL);
        FLOW_ASSERT_EQ(sieve_dot_ok, FLOW_ASM_SIEVE_ALL_SOUND);

        const int32_t a_max[4] = {INT32_MAX, INT32_MAX, INT32_MAX, INT32_MAX};
        const int32_t x_max[4] = {INT32_MAX, INT32_MAX, INT32_MAX, INT32_MAX};
        uint64_t sieve_dot_ovf = FLOW_ASM_SIEVE_ALL_SOUND;
        FlowResultU64 r_dot_ovf = flow_asm_qdot_i32(a_max, x_max, 4U, &sieve_dot_ovf);
        FLOW_ASSERT_TRUE(flow_result_u64_is_boundary(r_dot_ovf));
        FLOW_ASSERT_EQ((int64_t)r_dot_ovf.value, INT64_MAX);
        FLOW_ASSERT_EQ(sieve_dot_ovf & FLOW_ASM_SIEVE_INT_NO_OVERFLOW, 0ULL);

        /* 1f. Edge cases: INT32_MIN*INT32_MIN (+2^62 pair overflow), negative underflow, dim=0 */
        const int32_t a_min[4] = {INT32_MIN, INT32_MIN, INT32_MIN, INT32_MIN};
        uint64_t sieve_min_pos = FLOW_ASM_SIEVE_ALL_SOUND;
        FlowResultU64 r_min_pos = flow_asm_qdot_i32(a_min, a_min, 4U, &sieve_min_pos);
        FLOW_ASSERT_TRUE(flow_result_u64_is_boundary(r_min_pos));
        FLOW_ASSERT_EQ((int64_t)r_min_pos.value, INT64_MAX);
        FLOW_ASSERT_EQ(sieve_min_pos & FLOW_ASM_SIEVE_INT_NO_OVERFLOW, 0ULL);

        uint64_t sieve_min_neg = FLOW_ASM_SIEVE_ALL_SOUND;
        FlowResultU64 r_min_neg = flow_asm_qdot_i32(a_min, x_max, 4U, &sieve_min_neg);
        FLOW_ASSERT_TRUE(flow_result_u64_is_boundary(r_min_neg));
        FLOW_ASSERT_EQ((int64_t)r_min_neg.value, INT64_MIN);
        FLOW_ASSERT_EQ(sieve_min_neg & FLOW_ASM_SIEVE_INT_NO_OVERFLOW, 0ULL);

        /* 1g. 128-bit exact cancellation when pair p0+p1 overflows int64_t (+2^63) and p2+p3
         * cancels */
        const int32_t x_cancel4[4] = {INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX};
        uint64_t sieve_cancel4 = FLOW_ASM_SIEVE_ALL_SOUND;
        FlowResultU64 r_cancel4 = flow_asm_qdot_i32(a_min, x_cancel4, 4U, &sieve_cancel4);
        FLOW_ASSERT_FALSE(flow_result_u64_is_boundary(r_cancel4));
        FLOW_ASSERT_EQ((int64_t)r_cancel4.value, 4294967296LL);
        FLOW_ASSERT_EQ(sieve_cancel4, FLOW_ASM_SIEVE_ALL_SOUND);

        /* 1h. dim != 4 (dim=3, dim=5) intermediate overflow saturation & 128-bit cancellation */
        const int32_t a_dim3[3] = {INT32_MIN, INT32_MIN, -100};
        const int32_t x_dim3[3] = {INT32_MIN, INT32_MIN, 1};
        uint64_t sieve_dim3 = FLOW_ASM_SIEVE_ALL_SOUND;
        FlowResultU64 r_dim3 = flow_asm_qdot_i32(a_dim3, x_dim3, 3U, &sieve_dim3);
        FLOW_ASSERT_FALSE(flow_result_u64_is_boundary(r_dim3));
        FLOW_ASSERT_EQ((int64_t)r_dim3.value, INT64_MAX - 99LL);
        FLOW_ASSERT_EQ(sieve_dim3, FLOW_ASM_SIEVE_ALL_SOUND);

        const int32_t a_dim5_ovf[5] = {INT32_MIN, INT32_MIN, INT32_MIN, -100, 0};
        const int32_t x_dim5_ovf[5] = {INT32_MIN, INT32_MIN, INT32_MIN, 1, 0};
        uint64_t sieve_dim5_ovf = FLOW_ASM_SIEVE_ALL_SOUND;
        FlowResultU64 r_dim5_ovf = flow_asm_qdot_i32(a_dim5_ovf, x_dim5_ovf, 5U, &sieve_dim5_ovf);
        FLOW_ASSERT_TRUE(flow_result_u64_is_boundary(r_dim5_ovf));
        FLOW_ASSERT_EQ((int64_t)r_dim5_ovf.value, INT64_MAX);
        FLOW_ASSERT_EQ(sieve_dim5_ovf & FLOW_ASM_SIEVE_INT_NO_OVERFLOW, 0ULL);

        const int32_t a_dim3_c[3] = {INT32_MIN, INT32_MIN, INT32_MIN};
        const int32_t x_dim3_c[3] = {INT32_MIN, INT32_MIN, INT32_MAX};
        uint64_t sieve_dim3_ok = FLOW_ASM_SIEVE_ALL_SOUND;
        FlowResultU64 r_dim3_ok = flow_asm_qdot_i32(a_dim3_c, x_dim3_c, 3U, &sieve_dim3_ok);
        FLOW_ASSERT_FALSE(flow_result_u64_is_boundary(r_dim3_ok));
        FLOW_ASSERT_EQ((int64_t)r_dim3_ok.value, (1LL << 62) + (1LL << 31));
        FLOW_ASSERT_EQ(sieve_dim3_ok, FLOW_ASM_SIEVE_ALL_SOUND);

        uint64_t sieve_dim0 = FLOW_ASM_SIEVE_ALL_SOUND;
        FlowResultU64 r_dim0 = flow_asm_qdot_i32(a_vec, x_vec, 0U, &sieve_dim0);
        FLOW_ASSERT_FALSE(flow_result_u64_is_boundary(r_dim0));
        FLOW_ASSERT_EQ(r_dim0.value, 0ULL);
        FLOW_ASSERT_EQ(sieve_dim0, FLOW_ASM_SIEVE_ALL_SOUND);

        FLOW_ASSERT_EQ(flow_asm_qsub_sat_pos_i64(100LL, -50LL), 150LL);
        FLOW_ASSERT_EQ(flow_asm_qsub_sat_pos_i64(INT64_MAX, -1LL), INT64_MAX);
        FLOW_ASSERT_EQ(flow_asm_qsub_sat_pos_i64(0LL, INT64_MIN), INT64_MAX);

        /* 1i. flow_asm_qpoly4d_eval_plane slack saturation when b_q16 - dot_q16 overflows */
        const int64_t a_plane[4] = {1LL << 48, 0, 0, 0};
        const int64_t x_plane_pos[4] = {1LL << 30, 0, 0, 0}; /* dot_q16 = +2^62 */
        uint64_t sv_plane_viol = FLOW_ASM_SIEVE_ALL_SOUND;
        int64_t sl_viol =
            flow_asm_qpoly4d_eval_plane(a_plane, -(1LL << 62) - 100LL, x_plane_pos, &sv_plane_viol);
        FLOW_ASSERT_EQ(sl_viol, INT64_MIN);
        FLOW_ASSERT_EQ(sv_plane_viol & FLOW_ASM_SIEVE_QPOLY_INTERIOR, 0ULL);

        const int64_t x_plane_neg[4] = {-(1LL << 30), 0, 0, 0}; /* dot_q16 = -2^62 */
        uint64_t sv_plane_pos = FLOW_ASM_SIEVE_ALL_SOUND;
        int64_t sl_pos =
            flow_asm_qpoly4d_eval_plane(a_plane, (1LL << 62) + 100LL, x_plane_neg, &sv_plane_pos);
        FLOW_ASSERT_EQ(sl_pos, INT64_MAX);

        /* 1j. Pure Q16 ISQRT, QSQRT, QEMA (Zero Mantissa Absorption) & Solvency Check */
        FLOW_ASSERT_EQ(flow_asm_isqrt_u64(0ULL), 0ULL);
        FLOW_ASSERT_EQ(flow_asm_isqrt_u64(1ULL), 1ULL);
        FLOW_ASSERT_EQ(flow_asm_isqrt_u64(UINT64_MAX), 4294967295ULL);
        __extension__ typedef unsigned __int128 u128;
        FLOW_ASSERT_EQ(flow_asm_isqrt_u128(~(u128)0U), UINT64_MAX);
        FLOW_ASSERT_EQ(flow_asm_isqrt_u128((u128)UINT64_MAX * (u128)UINT64_MAX), UINT64_MAX);
        FLOW_ASSERT_EQ(flow_asm_isqrt_u128((u128)UINT64_MAX * (u128)UINT64_MAX - 1U),
                       UINT64_MAX - 1ULL);

        uint64_t sv_sqrt = FLOW_ASM_SIEVE_ALL_SOUND;
        FlowResultU64 r_sqrt9 = flow_asm_qsqrt_shift16_i64(9LL << 16, &sv_sqrt);
        FLOW_ASSERT_FALSE(flow_result_u64_is_boundary(r_sqrt9));
        FLOW_ASSERT_EQ((int64_t)r_sqrt9.value, 3LL << 16);
        FLOW_ASSERT_EQ(sv_sqrt, FLOW_ASM_SIEVE_ALL_SOUND);

        /* Verify zero mantissa absorption on Q16 EMA step across huge scale difference */
        uint64_t sv_ema = FLOW_ASM_SIEVE_ALL_SOUND;
        int64_t big_ema_q16 = 1000000000LL << 16;
        int64_t sample_q16 = big_ema_q16 + (1LL << 16);
        FlowResultU64 r_ema = flow_asm_qema_step_q16(big_ema_q16, sample_q16, 1LL, &sv_ema);
        FLOW_ASSERT_FALSE(flow_result_u64_is_boundary(r_ema));
        FLOW_ASSERT_EQ((int64_t)r_ema.value, big_ema_q16 + 1LL);
        FLOW_ASSERT_EQ(sv_ema, FLOW_ASM_SIEVE_ALL_SOUND);

        /* Solvency check: interior vs insolvent vs overflow */
        uint64_t sv_solv_ok = FLOW_ASM_SIEVE_ALL_SOUND;
        FlowResultU64 r_solv_ok =
            flow_asm_solvency_check_q16(500LL << 16, 800LL << 16, 400LL << 16, &sv_solv_ok);
        FLOW_ASSERT_FALSE(flow_result_u64_is_boundary(r_solv_ok));
        FLOW_ASSERT_EQ((int64_t)r_solv_ok.value, 100LL << 16);
        FLOW_ASSERT_EQ(sv_solv_ok, FLOW_ASM_SIEVE_ALL_SOUND);

        uint64_t sv_solv_bad = FLOW_ASM_SIEVE_ALL_SOUND;
        FlowResultU64 r_solv_bad =
            flow_asm_solvency_check_q16(500LL << 16, 1200LL << 16, 400LL << 16, &sv_solv_bad);
        FLOW_ASSERT_EQ((int64_t)r_solv_bad.value, -(300LL << 16));
        FLOW_ASSERT_EQ(sv_solv_bad & FLOW_ASM_SIEVE_QPOLY_INTERIOR, 0ULL);
    }

    /* =====================================================================
     * Stage 2: Pure Q16 Domain, DivZero, Saturation & Boundary Tag Sieve
     * ===================================================================== */
    FLOW_STAGE_BEGIN(2, "Pure Q16 Domain, DivZero, Saturation & HoTT Boundary Tag Sieve");
    {
        uint64_t sieve_clean = FLOW_ASM_SIEVE_ALL_SOUND;
        FlowJet2 valid_jet = {81920LL, -32768LL, 131072LL};
        uint64_t res_clean = flow_asm_check_jet2(valid_jet, &sieve_clean);
        FLOW_ASSERT_EQ(res_clean, FLOW_ASM_SIEVE_ALL_SOUND);

        /* Q16 1.0 / 0 triggers INT_NO_DIVZERO and Q_NO_SATURATION */
        uint64_t sieve_div0 = FLOW_ASM_SIEVE_ALL_SOUND;
        FlowResultU64 div0_res = flow_asm_qdiv_shift16_i64(FLOW_Q16_ONE, 0LL, &sieve_div0);
        FLOW_ASSERT_TRUE(flow_result_u64_is_boundary(div0_res));
        FlowJet2 div0_jet = {(int64_t)div0_res.value, 0LL, 0LL};
        (void)flow_asm_check_jet2(div0_jet, &sieve_div0);
        FLOW_ASSERT_EQ(sieve_div0 & FLOW_ASM_SIEVE_INT_NO_DIVZERO, 0ULL);
        FLOW_ASSERT_EQ(sieve_div0 & FLOW_ASM_SIEVE_Q_NO_SATURATION, 0ULL);

        /* Q16 0 / 0 and sqrt(-1) trigger DOMAIN_VALID */
        uint64_t sieve_dom = FLOW_ASM_SIEVE_ALL_SOUND;
        FlowResultU64 neg_sqrt = flow_asm_qsqrt_shift16_i64(-FLOW_Q16_ONE, &sieve_dom);
        FLOW_ASSERT_TRUE(flow_result_u64_is_boundary(neg_sqrt));
        FlowJet2 dom_jet = {(int64_t)neg_sqrt.value, FLOW_Q16_ONE, 0LL};
        (void)flow_asm_check_jet2(dom_jet, &sieve_dom);
        FLOW_ASSERT_EQ(sieve_dom & FLOW_ASM_SIEVE_DOMAIN_VALID, 0ULL);

        /* HoTT boundary tag payload injection */
        uint64_t sieve_hott_tag = FLOW_ASM_SIEVE_ALL_SOUND;
        uint64_t btag = flow_hott_make_boundary_tag(
            FLOW_HOTT_BOUNDARY_OVERFLOW, 2U, (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY, 0U, 0xCAFU);
        FLOW_ASSERT_TRUE(flow_hott_is_boundary_tag(btag));
        FLOW_ASSERT_EQ((uint64_t)flow_hott_boundary_code(btag), 0xCAFULL);
        FlowJet2 hott_tag_jet = {(int64_t)btag, 0LL, 0LL};
        (void)flow_asm_check_jet2(hott_tag_jet, &sieve_hott_tag);
        FLOW_ASSERT_EQ(sieve_hott_tag & FLOW_ASM_SIEVE_DOMAIN_VALID, 0ULL);
    }

    /* =====================================================================
     * Stage 3: Branchless ASM Masked Cyclic Arena Memory Load/Store
     * ===================================================================== */
    FLOW_STAGE_BEGIN(3, "Branchless ASM Masked Cyclic Arena Memory Load/Store (Zero Segfault)");
    {
        FlowUmaBuffer uma;
        memset(&uma, 0, sizeof(uma));
        int rc = flow_uma_buffer_alloc(&uma, FLOW_HW_PAGE_BYTES);
        FLOW_ASSERT_EQ(rc, 0);
        FLOW_ASSERT_EQ((uintptr_t)uma.cpu_ptr & (FLOW_HW_CACHE_LINE_BYTES - 1U), 0ULL);

        uint64_t *arena = (uint64_t *)uma.cpu_ptr;
        const uint64_t mask = 0xFFULL;

        uint64_t sieve_mem = FLOW_ASM_SIEVE_ALL_SOUND;
        for (uint64_t idx = 0; idx <= mask; ++idx) {
            uint64_t val = 0x9E3779B97F4A7C15ULL ^ (idx * 0x10001ULL);
            flow_asm_arena_store_u64(arena, idx, mask, val, &sieve_mem);
            uint64_t ld = flow_asm_arena_load_u64(arena, idx, mask, &sieve_mem);
            FLOW_ASSERT_EQ(ld, val);
        }
        FLOW_ASSERT_EQ(sieve_mem, FLOW_ASM_SIEVE_ALL_SOUND);

        /* Wild out-of-bounds indices wrap into [0, mask] and clear INBOUNDS sieve bit */
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

        /* Unaligned base pointer clears FLOW_ASM_SIEVE_MEM_ALIGNED */
        uint64_t sv_unaligned = FLOW_ASM_SIEVE_ALL_SOUND;
        (void)flow_asm_arena_load_u64(arena + 1, 0ULL, 0x7FULL, &sv_unaligned);
        FLOW_ASSERT_EQ(sv_unaligned & FLOW_ASM_SIEVE_MEM_ALIGNED, 0ULL);

        flow_uma_buffer_free(&uma);
    }

    /* =====================================================================
     * Stage 4: Wave64 Subgroup Ballot & 1.58-Bit Ternary Lattice (POPCNT/CTZ)
     * ===================================================================== */
    FLOW_STAGE_BEGIN(4, "Wave64 Subgroup Ballot & 1.58-Bit Ternary Lattice (POPCNT / CTZ)");
    {
        int8_t vec_a[64], vec_b[64], decoded_a[64];
        int32_t expected_dot = 0;
        for (uint32_t i = 0; i < 64U; ++i) {
            vec_a[i] = (int8_t)((int32_t)(i % 3U) - 1);
            vec_b[i] = (int8_t)(1 - (int32_t)((i * 7U) % 3U));
            expected_dot += (int32_t)vec_a[i] * (int32_t)vec_b[i];
        }
        FlowBmfTernary64 ta = flow_bmf_ternary64_encode(vec_a);
        FlowBmfTernary64 tb = flow_bmf_ternary64_encode(vec_b);
        FLOW_ASSERT_TRUE(flow_bmf_ternary64_is_canonical(ta));
        FLOW_ASSERT_TRUE(flow_bmf_ternary64_is_canonical(tb));
        FLOW_ASSERT_EQ(flow_bmf_ternary64_dot(ta, tb), expected_dot);

        flow_bmf_ternary64_decode_all(ta, decoded_a);
        for (uint32_t i = 0; i < 64U; ++i) {
            FLOW_ASSERT_EQ(decoded_a[i], vec_a[i]);
        }

        uint64_t ballot = (1ULL << 2) | (1ULL << 19) | (1ULL << 41) | (1ULL << 63);
        FLOW_ASSERT_EQ(flow_ballot_visible_count(ballot), 4U);
        FLOW_ASSERT_EQ(flow_ballot_lane_offset(ballot, 0U), 0U);
        FLOW_ASSERT_EQ(flow_ballot_lane_offset(ballot, 19U), 1U);
        FLOW_ASSERT_EQ(flow_ballot_lane_offset(ballot, 41U), 2U);
        FLOW_ASSERT_EQ(flow_ballot_lane_offset(ballot, 63U), 3U);

        uint32_t handles[64] = {0};
        uint32_t cnt = flow_ballot_compact_wave64(ballot, 100U, handles);
        FLOW_ASSERT_EQ(cnt, 4U);
        FLOW_ASSERT_EQ(handles[0], 102U);
        FLOW_ASSERT_EQ(handles[1], 119U);
        FLOW_ASSERT_EQ(handles[2], 141U);
        FLOW_ASSERT_EQ(handles[3], 163U);

        /* 4b. Goldilocks Prime Field F_p (p = 2^64 - 2^32 + 1) division-free arithmetic */
        const uint64_t p_gl = FLOW_GOLDILOCKS_PRIME;
        FLOW_ASSERT_EQ(flow_asm_fp64_reduce(p_gl), 0ULL);
        FLOW_ASSERT_EQ(flow_asm_fp64_add(p_gl - 1ULL, 1ULL), 0ULL);
        FLOW_ASSERT_EQ(flow_asm_fp64_add(p_gl - 1ULL, 2ULL), 1ULL);
        FLOW_ASSERT_EQ(flow_asm_fp64_sub(0ULL, 1ULL), p_gl - 1ULL);
        /* (-1) * (-1) == +1 mod p */
        FLOW_ASSERT_EQ(flow_asm_fp64_mul(p_gl - 1ULL, p_gl - 1ULL), 1ULL);
        /* 2^32 * (2^32 - 1) = 2^64 - 2^32 = p - 1 */
        FLOW_ASSERT_EQ(flow_asm_fp64_mul(1ULL << 32U, FLOW_GOLDILOCKS_EPS), p_gl - 1ULL);

        uint64_t fp_seed = 0x123456789ABCDEF0ULL;
        for (uint32_t i = 0; i < 256U; ++i) {
            uint64_t a = flow_asm_fp64_reduce(flow_prng_next(&fp_seed));
            uint64_t b = flow_asm_fp64_reduce(flow_prng_next(&fp_seed));
            uint64_t c = flow_asm_fp64_reduce(flow_prng_next(&fp_seed));
            uint64_t lhs = flow_asm_fp64_mul(flow_asm_fp64_add(a, b), c);
            uint64_t rhs = flow_asm_fp64_add(flow_asm_fp64_mul(a, c), flow_asm_fp64_mul(b, c));
            FLOW_ASSERT_EQ(lhs, rhs);
        }

        /* 4c. 1-Cycle Hardware Bit-Reversal (rbit) & QMC Radical Inverse */
        FLOW_ASSERT_EQ(flow_asm_rbit64(1ULL), 1ULL << 63U);
        FLOW_ASSERT_EQ(flow_asm_rbit64(flow_asm_rbit64(0xCAFEBABE13572468ULL)),
                       0xCAFEBABE13572468ULL);
        FLOW_ASSERT_EQ(flow_asm_radical_inverse_q16(0ULL), 0LL);
        FLOW_ASSERT_EQ(flow_asm_radical_inverse_q16(1ULL), 32768LL); /* 1/2 in Q16 */
        FLOW_ASSERT_EQ(flow_asm_radical_inverse_q16(2ULL), 16384LL); /* 1/4 in Q16 */
        FLOW_ASSERT_EQ(flow_asm_radical_inverse_q16(3ULL), 49152LL); /* 3/4 in Q16 */
        FLOW_ASSERT_EQ(flow_asm_radical_inverse_q16(4ULL), 8192LL);  /* 1/8 in Q16 */

        /* 4d. Bijective F_2 Tempering Involution & GF(2) 64x64 Matrix-Vector Multiply */
        FLOW_ASSERT_EQ(flow_asm_biject_untemper32(flow_asm_biject_temper32(0U)), 0U);
        FLOW_ASSERT_EQ(flow_asm_biject_untemper32(flow_asm_biject_temper32(UINT32_MAX)),
                       UINT32_MAX);
        for (uint32_t i = 0; i < 256U; ++i) {
            uint32_t x32 = (uint32_t)flow_prng_next(&fp_seed);
            uint32_t y32 = flow_asm_biject_temper32(x32);
            FLOW_ASSERT_EQ(flow_asm_biject_untemper32(y32), x32);
            FLOW_ASSERT_EQ(flow_asm_biject_temper32(flow_asm_biject_untemper32(x32)), x32);
        }

        uint64_t gf2_ident[64];
        for (uint32_t i = 0; i < 64U; ++i) {
            gf2_ident[i] = 1ULL << i;
        }
        FLOW_ASSERT_EQ(flow_asm_gf2_matvec64(gf2_ident, 0xFEDCBA9876543210ULL),
                       0xFEDCBA9876543210ULL);

        /* 4e. Closed-Loop 2-Jet Impedance (Dual of EMA) & 2D Cross Product (ZMP) */
        FlowJet2 cur_j = flow_jet2_make(2LL << 16, 1LL << 16, 1LL << 16);
        FlowJet2 ref_j = flow_jet2_make(1LL << 16, 0LL, 0LL);
        uint64_t sv_imp = FLOW_ASM_SIEVE_ALL_SOUND;
        int64_t u_imp = flow_jet2_impedance_step_q16(cur_j, ref_j, 10LL << 16, 4LL << 16, 2LL << 16,
                                                     100LL << 16, &sv_imp);
        /* -(10*(2-1) + 4*(1-0) + 2*1) = -16.0 in Q16 */
        FLOW_ASSERT_EQ(u_imp, -(16LL << 16));
        FLOW_ASSERT_EQ(sv_imp, FLOW_ASM_SIEVE_ALL_SOUND);

        uint64_t sv_imp_sat = FLOW_ASM_SIEVE_ALL_SOUND;
        int64_t u_sat = flow_jet2_impedance_step_q16(cur_j, ref_j, 10LL << 16, 4LL << 16, 2LL << 16,
                                                     5LL << 16, &sv_imp_sat);
        FLOW_ASSERT_EQ(u_sat, -(5LL << 16));
        FLOW_ASSERT_EQ(sv_imp_sat & FLOW_ASM_SIEVE_Q_NO_SATURATION, 0ULL);

        FLOW_ASSERT_EQ(flow_q16_cross2d(0, 0, 1LL << 16, 0, 2LL << 16, 0), 0LL);
        FLOW_ASSERT_EQ(flow_q16_cross2d(0, 0, 2LL << 16, 0, 0, 3LL << 16), 6LL << 16);
        FLOW_ASSERT_EQ(flow_q16_cross2d(0, 0, 0, 3LL << 16, 2LL << 16, 0), -(6LL << 16));
    }

    /* =====================================================================
     * Stage 5: Self-Bootstrapping 2-Jet Taylor Consistency & QPoly Equivalence
     * ===================================================================== */
    FLOW_STAGE_BEGIN(5, "Self-Bootstrapping 2-Jet Taylor Consistency & QPoly Equivalence");
    {
        /*
         * Exact Q48.16 quadratic trajectory:
         *   dt_q16 = 1024 (1/64 in Q16), d2_q16 = -2 * 65536 = -131072
         *   Every step has exact 0-ULP integer Taylor residual!
         */
        const int64_t dt_q16 = 1024LL;
        const int64_t accel_q16 = -(2LL << 16);
        FlowJet2 prev = {1000LL << 16, 100LL << 16, accel_q16};
        uint64_t running_sieve = FLOW_ASM_SIEVE_ALL_SOUND;

        for (int step = 1; step <= 100; ++step) {
            int64_t v_next = flow_jet2_eval_taylor(prev, dt_q16);
            int64_t d1_next = prev.d1 + ((accel_q16 * dt_q16) >> 16);
            FlowJet2 next = {v_next, d1_next, accel_q16};
            running_sieve = flow_asm_jet2_self_check_step(prev, next, dt_q16, 0, 0, &running_sieve);
            prev = next;
        }
        FLOW_ASSERT_EQ(running_sieve, FLOW_ASM_SIEVE_ALL_SOUND);

        /* Fault Injection 1: Silent Memory Bit-Flip in next.v (bit 12) */
        FlowJet2 corrupted = {
            flow_jet2_eval_taylor(prev, dt_q16) ^ (1LL << 12),
            prev.d1 + ((accel_q16 * dt_q16) >> 16),
            accel_q16,
        };

        uint64_t fault1_sieve = FLOW_ASM_SIEVE_ALL_SOUND;
        (void)flow_asm_jet2_self_check_step(prev, corrupted, dt_q16, 2, 2, &fault1_sieve);
        FLOW_ASSERT_EQ(fault1_sieve & FLOW_ASM_SIEVE_JET2_TAYLOR_0, 0ULL);

        uint32_t broken_bit = (uint32_t)__builtin_ctzll(~fault1_sieve & FLOW_ASM_SIEVE_ALL_SOUND);
        FLOW_ASSERT_EQ(1ULL << broken_bit, (uint64_t)FLOW_ASM_SIEVE_JET2_TAYLOR_0);
        uint64_t next_mask = 0ULL;
        FlowKanStatus kan = flow_cubical_step_neighbor(fault1_sieve, broken_bit, 0ULL, &next_mask);
        FLOW_ASSERT_EQ((int64_t)kan, (int64_t)FLOW_KAN_FILLED_HOMOTOPIC);
        FLOW_ASSERT_EQ(next_mask, FLOW_ASM_SIEVE_ALL_SOUND);

        /* Fault Injection 2: Discretization / Tunneling Step Breakdown */
        int64_t huge_dt_q16 = 5LL << 15; /* 2.5 in Q16 */
        FlowJet2 jump_next = {0LL, -(500LL << 16), accel_q16};
        int64_t barrier_a[4] = {1LL << 16, 0, 0, 0};
        int64_t barrier_b = -50000LL;
        int64_t jump_x[4] = {0, 0, 0, 0};
        uint64_t tunnel_sieve = FLOW_ASM_SIEVE_ALL_SOUND;
        int64_t jump_slack =
            flow_asm_qpoly4d_eval_plane(barrier_a, barrier_b, jump_x, &tunnel_sieve);
        FLOW_ASSERT_TRUE(jump_slack < 0);
        (void)flow_asm_jet2_self_check_step(prev, jump_next, huge_dt_q16, 2, 2, &tunnel_sieve);
        FLOW_ASSERT_EQ(tunnel_sieve & FLOW_ASM_SIEVE_QPOLY_INTERIOR, 0ULL);
        FLOW_ASSERT_EQ(tunnel_sieve & FLOW_ASM_SIEVE_JET2_TAYLOR_0, 0ULL);
        FLOW_ASSERT_EQ(tunnel_sieve & FLOW_ASM_SIEVE_JET2_TAYLOR_1, 0ULL);

        /* Fault Injection 3: FlowQPoly4D_S16_eval vs flow_asm_qpoly4d_eval_plane (10,000 pts) */
        FlowQPoly4D_S16 poly;
        FlowQPoly4D_S16_init(&poly);
        poly.A[0][0] = 12345;
        poly.A[0][1] = -23456;
        poly.A[0][2] = 34567;
        poly.A[0][3] = -45678;
        atomic_store_explicit(&poly.b[0], 50000000LL, memory_order_relaxed);
        atomic_store_explicit(&poly.active_mask, 1ULL, memory_order_release);

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

        /* 1,000,000-iteration self-bootstrapping benchmark */
        FlowUmaBuffer uma_bench;
        memset(&uma_bench, 0, sizeof(uma_bench));
        FLOW_ASSERT_EQ(flow_uma_buffer_alloc(&uma_bench, FLOW_HW_PAGE_BYTES), 0);
        uint64_t *arena = (uint64_t *)uma_bench.cpu_ptr;
        for (uint64_t i = 0; i < 256; ++i)
            arena[i] = i * 7ULL + 1ULL;

        int64_t x_bench[4] = {10, 20, 30, 40};
        FlowJet2 b_prev = {FLOW_Q16_ONE, 0LL, -FLOW_Q16_ONE};
        FlowJet2 b_next = {FLOW_Q16_ONE, -64LL, -FLOW_Q16_ONE};
        uint64_t out_sieve = FLOW_ASM_SIEVE_ALL_SOUND;
        uint64_t asm_sink = 0ULL;
        const uint64_t iters = 1000000ULL;

        uint64_t t0 = flow_clock_monotonic_ns();
        for (uint64_t i = 0; i < iters; ++i) {
            asm_sink ^= asm_hott_jet2_bootstrap_step(a_q16, b_q16, x_bench, b_prev, b_next, 64LL,
                                                     (int64_t)i, 42LL, arena, i & 0xFFULL, 0xFFULL,
                                                     &out_sieve);
        }
        uint64_t t1 = flow_clock_monotonic_ns();
        flow_uma_buffer_free(&uma_bench);

        FLOW_ASSERT_NE(asm_sink, 0ULL);
        FLOW_ASSERT_EQ(out_sieve, FLOW_ASM_SIEVE_ALL_SOUND);
        if (report_mode) {
            uint64_t elapsed_ns = t1 - t0;
            uint64_t ns_int = elapsed_ns / iters;
            uint64_t ns_frac = ((elapsed_ns % iters) * 100ULL) / iters;
            printf(
                "  [ASM Bootstrap] 1,000,000 steps | %llu.%02llu ns/step | Sieve=0x%03llx (8 B)\n",
                (unsigned long long)ns_int, (unsigned long long)ns_frac,
                (unsigned long long)out_sieve);
        }
    }

    FLOW_TEST_SUITE_END();
}
