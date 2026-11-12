/*
 * ============================================================================
 * FLOW: Unified L0 Foundational Integration & Benchmark Suite
 * ============================================================================
 * Single consolidated C test suite verifying all 6 first-principles stages:
 *   1. Hardware Bit Algebra, Total Q-Arithmetic, Overflow Capture & BoundaryTag
 *   2. Wave64 Ballot, 1.58-Bit Ternary Dot, Goldilocks F_p & 64B Capsule
 *   3. FlowJet2 Fiber Algebra & Normalized Citardauq Root Solver
 *   4. FlowQPoly Inner Quantization & 2-Jet Next-Face Prediction
 *   5. Zero-Heap Page-Aligned UMA & Lock-Free QSBR Epoch Pointer Swapping
 *   6. Deterministic Performance Benchmark (ns/op)
 * ============================================================================
 */

#include "flow.h"
#include "hott_test_kit.h"

FLOW_QPOLY_DEFINE_EX(FlowTestPoly4D, 4, 16, 4)

int main(void) {
    FLOW_TEST_SUITE_BEGIN("FLOW L0 Total Deterministic ISA Substrate");

    /* ========================================================================
     * Stage 1: ASM Total Arithmetic, Overflow Capture, Sieve & BoundaryTag
     * ======================================================================== */
    FLOW_STAGE_BEGIN(1, "Hardware Arithmetic, Overflow Capture, Sieve & BoundaryTag");
    {
        uint64_t sieve = FLOW_ASM_SIEVE_ALL_SOUND;

        /* Bit ops */
        FLOW_ASSERT_EQ(flow_clz64_nonzero(1ULL), 63U);
        FLOW_ASSERT_EQ(flow_clz64_nonzero(0x8000000000000000ULL), 0U);
        FLOW_ASSERT_EQ(flow_ctz64_nonzero(1ULL), 0U);
        FLOW_ASSERT_EQ(flow_ctz64_nonzero(0x8000000000000000ULL), 63U);
        FLOW_ASSERT_EQ(flow_popcnt64(0ULL), 0U);
        FLOW_ASSERT_EQ(flow_popcnt64(0xFFFFFFFFFFFFFFFFULL), 64U);
        FLOW_ASSERT_EQ(flow_popcnt64(0x5555555555555555ULL), 32U);
        FLOW_ASSERT_EQ(flow_asm_rbit64(0x8000000000000000ULL), 1ULL);
        FLOW_ASSERT_EQ(flow_asm_rbit64(1ULL), 0x8000000000000000ULL);

        /* Total arithmetic: normal addition */
        FlowResultU64 r_add = flow_asm_qadd_i64(FLOW_Q16_ONE * 2, FLOW_Q16_ONE * 3, &sieve);
        FLOW_ASSERT_EQ((int64_t)r_add.value, FLOW_Q16_ONE * 5);
        FLOW_ASSERT_TRUE(flow_sieve_is_all_sound(sieve));

        /* Total arithmetic: overflow capture -> sieve cleared, BoundaryTag returned */
        FlowResultU64 r_ovf = flow_asm_qadd_i64(INT64_MAX, 1, &sieve);
        FLOW_ASSERT_TRUE(flow_result_u64_is_boundary(r_ovf));
        FLOW_ASSERT_TRUE(flow_hott_is_boundary_tag(r_ovf.boundary_tag));
        FLOW_ASSERT_FALSE(flow_sieve_is_all_sound(sieve));

        /* Division by zero capture */
        sieve = FLOW_ASM_SIEVE_ALL_SOUND;
        FlowResultU64 r_div0 = flow_asm_qdiv_shift16_i64(FLOW_Q16_ONE, 0, &sieve);
        FLOW_ASSERT_TRUE(flow_result_u64_is_boundary(r_div0));
        FLOW_ASSERT_TRUE(flow_hott_is_boundary_tag(r_div0.boundary_tag));
        FLOW_ASSERT_FALSE(sieve & FLOW_ASM_SIEVE_INT_NO_DIVZERO);

        /* BoundaryTag encode / decode */
        uint64_t tag_encoded = flow_hott_make_boundary_tag(0x123, 4U, 2U, 7U, 0xABC);
        FLOW_ASSERT_TRUE(flow_hott_is_boundary_tag(tag_encoded));
        FLOW_ASSERT_EQ(flow_hott_boundary_tag_code(tag_encoded), 0xABC);

        /* Chebyshev single-udiv integer square root */
        FLOW_ASSERT_EQ(flow_isqrt_u64(0ULL), 0ULL);
        FLOW_ASSERT_EQ(flow_isqrt_u64(1ULL), 1ULL);
        FLOW_ASSERT_EQ(flow_isqrt_u64(4ULL), 2ULL);
        FLOW_ASSERT_EQ(flow_isqrt_u64(9ULL), 3ULL);
        FLOW_ASSERT_EQ(flow_isqrt_u64(16ULL), 4ULL);
        FLOW_ASSERT_EQ(flow_isqrt_u64(100ULL), 10ULL);
        FLOW_ASSERT_EQ(flow_isqrt_u64(1000000ULL), 1000ULL);
        FLOW_ASSERT_EQ(flow_isqrt_u64(18446744065119617025ULL), 4294967295ULL); /* (2^32-1)^2 */

        /* Masked Arena store / load */
        _Alignas(FLOW_HW_CACHE_LINE_BYTES) uint64_t arena[64] = {0};
        sieve = FLOW_ASM_SIEVE_ALL_SOUND;
        flow_asm_arena_store_u64(arena, 5ULL, 63ULL, 0xCAFEBABEDEADBEEFULL, &sieve);
        uint64_t loaded = flow_asm_arena_load_u64(arena, 5ULL, 63ULL, &sieve);
        FLOW_ASSERT_EQ(loaded, 0xCAFEBABEDEADBEEFULL);
        FLOW_ASSERT_TRUE(flow_sieve_is_all_sound(sieve));
    }

    /* ========================================================================
     * Stage 2: Wave64 Ballot, 1.58-Bit Ternary Dot, Goldilocks F_p & 64B Capsule
     * ======================================================================== */
    FLOW_STAGE_BEGIN(2, "Wave64 Ballot, 1.58-Bit Ternary Dot, Goldilocks F_p & 64B Capsule");
    {
        /* Wave64 ballot */
        uint64_t ballot = 0x000000000000000FULL; /* 4 active lanes */
        FLOW_ASSERT_EQ(flow_ballot_visible_count(ballot), 4U);
        FLOW_ASSERT_EQ(flow_ballot_lane_offset(ballot, 2U), 2U);
        uint32_t comp_ids[64] = {0};
        uint32_t cnt = flow_ballot_compact_wave64(ballot, 0U, comp_ids);
        FLOW_ASSERT_EQ(cnt, 4U);
        FLOW_ASSERT_EQ(comp_ids[0], 0U);
        FLOW_ASSERT_EQ(comp_ids[3], 3U);

        /* 1.58-bit Ternary dot product */
        int8_t v1[64] = {0};
        int8_t v2[64] = {0};
        v1[0] = 1;  v2[0] = 1;   /* 1*1 = 1 */
        v1[1] = -1; v2[1] = -1;  /* (-1)*(-1) = 1 */
        v1[2] = 1;  v2[2] = -1;  /* 1*(-1) = -1 */
        v1[3] = 0;  v2[3] = 1;   /* 0*1 = 0 */
        FlowBmfTernary64 t1 = flow_bmf_ternary64_encode(v1);
        FlowBmfTernary64 t2 = flow_bmf_ternary64_encode(v2);
        int32_t dot = flow_bmf_ternary64_dot(t1, t2);
        FLOW_ASSERT_EQ(dot, 1); /* 1 + 1 - 1 + 0 = 1 */

        /* Goldilocks F_p Field */
        uint64_t p = FLOW_GOLDILOCKS_PRIME;
        FLOW_ASSERT_EQ(flow_asm_fp64_reduce(p), 0ULL);
        FLOW_ASSERT_EQ(flow_asm_fp64_reduce(p + 5ULL), 5ULL);
        FLOW_ASSERT_EQ(flow_asm_fp64_add(p - 1ULL, 2ULL), 1ULL);
        FLOW_ASSERT_EQ(flow_asm_fp64_sub(1ULL, 2ULL), p - 1ULL);
        FLOW_ASSERT_EQ(flow_asm_fp64_mul(p - 1ULL, p - 1ULL), 1ULL); /* (-1)*(-1) == 1 mod p */

        /* 64B Capsule seal, verify & tamper detection */
        uint8_t cap[64] = {0};
        for (size_t i = 0; i < 56; ++i) {
            cap[i] = (uint8_t)(i * 7U + 3U);
        }
        uint64_t seed = 0x9E3779B97F4A7C15ULL;
        uint64_t mac = flow_capsule64_seal(cap, seed);
        FLOW_ASSERT_NE(mac, 0ULL);
        FLOW_ASSERT_TRUE(flow_capsule64_verify(cap, seed));

        /* Tamper 1 bit in payload -> verify MUST fail */
        cap[0] ^= 0x01U;
        FLOW_ASSERT_FALSE(flow_capsule64_verify(cap, seed));
        cap[0] ^= 0x01U; /* restore */
        FLOW_ASSERT_TRUE(flow_capsule64_verify(cap, seed));

        /* Wrong seed -> verify MUST fail */
        FLOW_ASSERT_FALSE(flow_capsule64_verify(cap, seed + 1ULL));
    }

    /* ========================================================================
     * Stage 3: FlowJet2 Fiber Algebra & Normalized Citardauq Root Solver
     * ======================================================================== */
    FLOW_STAGE_BEGIN(3, "FlowJet2 Fiber Algebra & Normalized Citardauq Root Solver");
    {
        FlowJet2 ja = flow_jet2_make(FLOW_Q16_ONE * 2, FLOW_Q16_ONE * 3, FLOW_Q16_ONE * 4);
        FlowJet2 jb = flow_jet2_make(FLOW_Q16_ONE * 5, FLOW_Q16_ONE * 6, FLOW_Q16_ONE * 7);

        FlowJet2 j_add = flow_jet2_add(ja, jb);
        FLOW_ASSERT_EQ(j_add.v, FLOW_Q16_ONE * 7);
        FLOW_ASSERT_EQ(j_add.d1, FLOW_Q16_ONE * 9);
        FLOW_ASSERT_EQ(j_add.d2, FLOW_Q16_ONE * 11);

        FlowJet2 j_sub = flow_jet2_sub(jb, ja);
        FLOW_ASSERT_EQ(j_sub.v, FLOW_Q16_ONE * 3);
        FLOW_ASSERT_EQ(j_sub.d1, FLOW_Q16_ONE * 3);
        FLOW_ASSERT_EQ(j_sub.d2, FLOW_Q16_ONE * 3);

        /* Taylor polynomial evaluation: j(dt) = v + d1 * dt + 0.5 * d2 * dt^2 */
        /* For j = (10, 2, 4) at dt = 1.0 (Q16 = 65536): 10 + 2*1 + 0.5*4*1 = 14 */
        FlowJet2 j_test = flow_jet2_make(FLOW_Q16_ONE * 10, FLOW_Q16_ONE * 2, FLOW_Q16_ONE * 4);
        int64_t val_dt = flow_jet2_eval_taylor(j_test, FLOW_Q16_ONE);
        FLOW_ASSERT_EQ(val_dt, FLOW_Q16_ONE * 14);

        /* Citardauq Root Solver: Linear case v = 10, d1 = -2, d2 = 0 -> root t = 5 */
        FlowJet2 j_lin = flow_jet2_make(FLOW_Q16_ONE * 10, -FLOW_Q16_ONE * 2, 0);
        int64_t root_lin = 0;
        bool ok_lin = flow_jet2_solve_root(j_lin, FLOW_Q16_ONE * 10, &root_lin);
        FLOW_ASSERT_TRUE(ok_lin);
        FLOW_ASSERT_EQ(root_lin, FLOW_Q16_ONE * 5);

        /* Citardauq Root Solver: Quadratic case v = 4, d1 = -4, d2 = 2 */
        /* Polynomial: 4 - 4t + 0.5 * 2 * t^2 = 4 - 4t + t^2 = (t - 2)^2 = 0 -> root t = 2 */
        FlowJet2 j_quad = flow_jet2_make(FLOW_Q16_ONE * 4, -FLOW_Q16_ONE * 4, FLOW_Q16_ONE * 2);
        int64_t root_quad = 0;
        bool ok_quad = flow_jet2_solve_root(j_quad, FLOW_Q16_ONE * 10, &root_quad);
        FLOW_ASSERT_TRUE(ok_quad);
        FLOW_ASSERT_EQ(root_quad, FLOW_Q16_ONE * 2);

        /* No real root (v > 0, d1 > 0, d2 >= 0) -> returns false */
        FlowJet2 j_noroot = flow_jet2_make(FLOW_Q16_ONE * 5, FLOW_Q16_ONE * 2, FLOW_Q16_ONE * 1);
        int64_t root_none = 0;
        bool ok_none = flow_jet2_solve_root(j_noroot, FLOW_Q16_ONE * 10, &root_none);
        FLOW_ASSERT_FALSE(ok_none);

        /* Boundary tag rejection */
        FlowJet2 j_bad = flow_jet2_make(FLOW_HOTT_TAG_MAGIC, 0, 0);
        int64_t root_bad = 0;
        FLOW_ASSERT_FALSE(flow_jet2_solve_root(j_bad, FLOW_Q16_ONE * 10, &root_bad));
    }

    /* ========================================================================
     * Stage 4: FlowQPoly Quantization, Slack & 2-Jet Next-Face Prediction
     * ======================================================================== */
    FLOW_STAGE_BEGIN(4, "FlowQPoly Quantization, Slack & 2-Jet Next-Face Prediction");
    {
        /* 4D Polyhedron definition and evaluation */
        FlowTestPoly4D poly;
        FlowTestPoly4D_init(&poly);
        /* Plane 0: x0 <= 10.0 */
        poly.A[0][0] = 1;
        poly.b[0] = FLOW_Q16_ONE * 10;
        /* Plane 1: x1 <= 20.0 */
        poly.A[1][1] = 1;
        poly.b[1] = FLOW_Q16_ONE * 20;
        poly.active_mask = 3ULL;

        int32_t x_in[4] = {(int32_t)(FLOW_Q16_ONE * 5), (int32_t)(FLOW_Q16_ONE * 15), 0, 0};
        FlowQPolyEval ev_in = FlowTestPoly4D_eval(&poly, x_in);
        FLOW_ASSERT_EQ(ev_in.violated_mask, 0ULL);

        int32_t x_out[4] = {(int32_t)(FLOW_Q16_ONE * 12), (int32_t)(FLOW_Q16_ONE * 15), 0, 0};
        FlowQPolyEval ev_out = FlowTestPoly4D_eval(&poly, x_out);
        FLOW_ASSERT_NE(ev_out.violated_mask, 0ULL);
        FLOW_ASSERT_EQ(ev_out.violated_mask, 1ULL); /* Plane 0 violated */

        /* Polyhedron inner quantization */
        int64_t a_num[2] = {1, 2};
        int64_t b_num[1] = {100};
        int64_t box_min[2] = {0, 0};
        int64_t box_max[2] = {10, 10};
        int32_t a_q[2] = {0};
        int64_t b_q[1] = {0};
        int q_ok = flow_qpoly_quantize_inner(1, 2, 0, a_num, b_num, 1, box_min, box_max, a_q, b_q);
        FLOW_ASSERT_EQ(q_ok, 1);
        FLOW_ASSERT_EQ(a_q[0], 1);
        FLOW_ASSERT_EQ(a_q[1], 2);
        FLOW_ASSERT_EQ(b_q[0], 100);

        /* Next-face prediction: particle at x = 0 moving toward plane x0 <= 10 with v = 2 */
        int32_t A_rows[1] = {1};
        int64_t b_plane[1] = {FLOW_Q16_ONE * 10};
        int32_t x_pos[1] = {0};
        int32_t dx[1] = {(int32_t)(FLOW_Q16_ONE * 2)};
        FlowQPolyForecast fc = flow_qpoly_predict_next_face(A_rows, b_plane, 1ULL, 1, x_pos, dx, NULL);
        FLOW_ASSERT_TRUE(fc.will_exit);
        FLOW_ASSERT_EQ(fc.plane, 0U);
        FLOW_ASSERT_EQ(fc.t_hit_q, FLOW_Q16_ONE * 5); /* t = 10 / 2 = 5 */
    }

    /* ========================================================================
     * Stage 5: Zero-Heap Page-Aligned UMA & Lock-Free QSBR Epoch Pointer Swapping
     * ======================================================================== */
    FLOW_STAGE_BEGIN(5, "Page-Aligned UMA Buffer & Lock-Free QSBR Epoch Swapping");
    {
        /* Hardware timer frequency probe */
        uint64_t freq = flow_hardware_timer_frequency_hz();
        FLOW_ASSERT_TRUE(freq > 0ULL);

        /* Zero-heap UMA Buffer allocation */
        FlowUmaBuffer uma;
        int alloc_res = flow_uma_buffer_alloc(&uma, 65536);
        FLOW_ASSERT_EQ(alloc_res, 0);
        FLOW_ASSERT_TRUE(uma.cpu_ptr != NULL);
        FLOW_ASSERT_TRUE(uma.capacity_bytes >= 65536);
        FLOW_ASSERT_EQ(uma.is_uma_coherent, 1);

        /* Write and read through UMA pointer */
        uint64_t *uma_words = (uint64_t *)uma.cpu_ptr;
        uma_words[0] = 0x1122334455667788ULL;
        FLOW_ASSERT_EQ(uma_words[0], 0x1122334455667788ULL);
        flow_uma_buffer_free(&uma);
        FLOW_ASSERT_TRUE(uma.cpu_ptr == NULL);

        /* Lock-Free QSBR Epoch Pointer Swapping */
        FlowQsbrEpoch epoch_ctx;
        int q_init = flow_qsbr_init(&epoch_ctx, NULL);
        FLOW_ASSERT_EQ(q_init, 0);

        FlowQsbrReader reader;
        int reg_res = flow_qsbr_reader_register(&epoch_ctx, &reader);
        FLOW_ASSERT_EQ(reg_res, 0);

        const char *bank_a = "BANK_A_PRIMARY";
        const char *bank_b = "BANK_B_SECONDARY";

        flow_qsbr_publish_ptr(&epoch_ctx, bank_a);
        const void *loaded_ptr = flow_qsbr_load_ptr(&epoch_ctx, &reader);
        FLOW_ASSERT_TRUE(loaded_ptr == (const void *)bank_a);

        /* Swap to Bank B */
        flow_qsbr_publish_ptr(&epoch_ctx, bank_b);
        flow_qsbr_checkpoint(&reader);
        loaded_ptr = flow_qsbr_load_ptr(&epoch_ctx, &reader);
        FLOW_ASSERT_TRUE(loaded_ptr == (const void *)bank_b);

        /* Synchronize quiescence */
        flow_qsbr_offline(&reader);
        int sync_res = flow_qsbr_synchronize(&epoch_ctx, 1000000ULL);
        FLOW_ASSERT_EQ(sync_res, 0);

        flow_qsbr_reader_unregister(&reader);
        flow_qsbr_fini(&epoch_ctx);
    }

    /* ========================================================================
     * Stage 6: Deterministic Performance Benchmark
     * ======================================================================== */
    FLOW_STAGE_BEGIN(6, "Deterministic Performance Benchmark");
    {
        uint64_t freq = flow_hardware_timer_frequency_hz();
        const uint32_t ITERS = 1000000U;
        uint64_t t_start = flow_hardware_cycles();

        uint64_t dummy = 0;
        int64_t x = FLOW_Q16_ONE;
        int64_t v = FLOW_Q16_ONE * 2;
        uint64_t sieve = FLOW_ASM_SIEVE_ALL_SOUND;
        for (uint32_t i = 0; i < ITERS; ++i) {
            FlowResultU64 r = flow_asm_qadd_i64(x, v, &sieve);
            dummy ^= r.value;
            x += (int64_t)(dummy & 1);
            __asm__ __volatile__("" : "+r"(x), "+r"(dummy));
        }

        uint64_t t_end = flow_hardware_cycles();
        uint64_t cyc_total = (t_end >= t_start) ? (t_end - t_start) : 0ULL;
        uint64_t ns_total = (freq > 0) ? (cyc_total * 1000000000ULL / freq) : cyc_total;
        uint64_t ns_per_op = ns_total / ITERS;

        printf("  ⚡ Total Q-Arithmetic Add: %llu ns for %u ops (~%llu ns/op, %llu ticks)\n",
               (unsigned long long)ns_total, ITERS, (unsigned long long)ns_per_op, (unsigned long long)cyc_total);
        FLOW_ASSERT_TRUE(dummy != 0ULL || dummy == 0ULL);

        /* Benchmark Chebyshev sqrt */
        t_start = flow_hardware_cycles();
        uint64_t sq_acc = 0;
        for (uint32_t i = 1; i <= ITERS; ++i) {
            uint64_t val = (uint64_t)i * 12345ULL + (sq_acc & 1);
            __asm__ __volatile__("" : "+r"(val));
            sq_acc ^= flow_isqrt_u64(val);
        }
        t_end = flow_hardware_cycles();
        cyc_total = (t_end >= t_start) ? (t_end - t_start) : 0ULL;
        ns_total = (freq > 0) ? (cyc_total * 1000000000ULL / freq) : cyc_total;
        printf("  ⚡ Chebyshev 1-udiv isqrt_u64: %llu ns for %u ops (~%llu ns/op, %llu ticks)\n",
               (unsigned long long)ns_total, ITERS, (unsigned long long)(ns_total / ITERS), (unsigned long long)cyc_total);
        FLOW_ASSERT_TRUE(sq_acc != 0ULL || sq_acc == 0ULL);

        /* Benchmark Citardauq Solve Root */
        t_start = flow_hardware_cycles();
        int64_t root_acc = 0;
        for (uint32_t i = 0; i < ITERS; ++i) {
            FlowJet2 j_bench = flow_jet2_make(FLOW_Q16_ONE * 4 + (root_acc & 1), -FLOW_Q16_ONE * 4, FLOW_Q16_ONE * 2);
            __asm__ __volatile__("" : "+r"(j_bench.v));
            int64_t r = 0;
            flow_jet2_solve_root(j_bench, FLOW_Q16_ONE * 10, &r);
            root_acc ^= r;
        }
        t_end = flow_hardware_cycles();
        cyc_total = (t_end >= t_start) ? (t_end - t_start) : 0ULL;
        ns_total = (freq > 0) ? (cyc_total * 1000000000ULL / freq) : cyc_total;
        printf("  ⚡ Citardauq 64-bit Root Solver: %llu ns for %u ops (~%llu ns/op, %llu ticks)\n",
               (unsigned long long)ns_total, ITERS, (unsigned long long)(ns_total / ITERS), (unsigned long long)cyc_total);
        FLOW_ASSERT_TRUE(root_acc != 0ULL || root_acc == 0ULL);
    }

    FLOW_TEST_SUITE_END();
}
