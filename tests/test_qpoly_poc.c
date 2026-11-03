#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "hott_test_kit.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>

/* =========================================================================
 * Compile-time Q-Polyhedron Instantiations for PoC A, B, C, E
 * ========================================================================= */
FLOW_QPOLY_DEFINE(FlowQPoly2D_S8, 2, 8)
FLOW_QPOLY_DEFINE(FlowQPoly2D_S12, 2, 12)
FLOW_QPOLY_DEFINE(FlowQPoly2D_S16, 2, 16)
FLOW_QPOLY_DEFINE(FlowQPoly2D_S20, 2, 20)

FLOW_QPOLY_DEFINE(FlowQPoly3D_S8, 3, 8)
FLOW_QPOLY_DEFINE(FlowQPoly3D_S12, 3, 12)
FLOW_QPOLY_DEFINE(FlowQPoly3D_S16, 3, 16)
FLOW_QPOLY_DEFINE(FlowQPoly3D_S20, 3, 20)

FLOW_QPOLY_DEFINE(FlowQPoly4D_S16, 4, 16)

FLOW_SPSC_DEFINE(PocHotRing, uint64_t, 64)

typedef struct __attribute__((aligned(FLOW_HW_CACHE_LINE_BYTES))) {
    int64_t coeffs[FLOW_QPOLY_PLANES][4];
    _Atomic int64_t b_constants[FLOW_QPOLY_PLANES];
    _Atomic uint64_t active_mask;
    _Atomic int64_t translation[4];
    _Atomic int64_t scale[4];
} NaiveUnpackedPoly4D;

static __attribute__((noinline)) uint64_t naive_unpacked_eval_4d(const NaiveUnpackedPoly4D *tpl,
                                                                 const int64_t x[4],
                                                                 int64_t *max_viol_out) {
    uint64_t mask = atomic_load_explicit(&tpl->active_mask, memory_order_acquire);
    uint64_t violated = 0ULL;
    int64_t local_x[4];
    int64_t max_viol = 0;
    for (size_t j = 0; j < 4; ++j) {
        int64_t tr = atomic_load_explicit(&tpl->translation[j], memory_order_relaxed);
        int64_t sc = atomic_load_explicit(&tpl->scale[j], memory_order_relaxed);
        local_x[j] = (sc != 0) ? ((x[j] - tr) / sc) : (x[j] - tr);
    }
    for (uint32_t i = 0; i < FLOW_QPOLY_PLANES; ++i) {
        if (!((mask >> i) & 1ULL))
            continue;
        int64_t dot = 0;
        for (size_t j = 0; j < 4; ++j)
            dot += tpl->coeffs[i][j] * local_x[j];
        int64_t viol = dot - atomic_load_explicit(&tpl->b_constants[i], memory_order_relaxed);
        if (viol > 0) {
            violated |= (1ULL << i);
            if (viol > max_viol)
                max_viol = viol;
        }
    }
    if (max_viol_out)
        *max_viol_out = max_viol;
    return violated;
}

static __attribute__((noinline)) FlowQPolyEval qpoly_eval_4d_noinline(const FlowQPoly4D_S16 *p,
                                                                      const int32_t x[4]) {
    return FlowQPoly4D_S16_eval(p, x);
}

static int poc_hot_unit_run(void *host, void *state, const void *in, void *out) {
    (void)host;
    (void)state;
    const uint64_t *v_in = (const uint64_t *)in;
    uint64_t *v_out = (uint64_t *)out;
    *v_out = (*v_in) ^ 0x5A5A5A5A5A5A5A5AULL;
    return 0;
}

static bool exact_rational_inside(size_t planes, size_t dim, const int64_t *a_num,
                                  const int64_t *b_num, const int32_t *x) {
    for (size_t i = 0; i < planes; ++i) {
        int64_t dot = 0;
        for (size_t j = 0; j < dim; ++j)
            dot += a_num[i * dim + j] * (int64_t)x[j];
        if (dot > b_num[i])
            return false;
    }
    return true;
}

int main(int argc, char **argv) {
    bool report_mode = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--report") == 0)
            report_mode = true;
    }

    FLOW_TEST_SUITE_BEGIN(
        "FLOW v4: Q-Polyhedron Quantization, 2-Jet Forecast & Zero-Fault Hot Path (PoC A-E)");

    if (report_mode) {
        printf("  [HW Spec] PAGE=%u B, CACHE_LINE=%u B, L1D=%u B, VECTOR=%u B, WAVE=%u lanes\n",
               FLOW_HW_PAGE_BYTES, FLOW_HW_CACHE_LINE_BYTES, FLOW_HW_L1D_BYTES,
               FLOW_HW_VECTOR_BYTES, FLOW_HW_WAVE_LANES);
    }

    /* =====================================================================
     * PoC A: Soundness of Inner Quantization (2D & 3D Lattice Exhaustive)
     * ===================================================================== */
    FLOW_STAGE_BEGIN(1, "PoC A: Conservative Inner Quantization Soundness (2D & 3D)");
    {
        const int64_t denom_2d = 997;
        const int64_t a2d[8][2] = {
            {997, 0},   {-997, 0},   {0, 997},    {0, -997},
            {705, 709}, {-705, 709}, {705, -709}, {-705, -709},
        };
        const int64_t b2d[8] = {
            60 * 997, 60 * 997, 60 * 997, 60 * 997, 56530, 56530, 56530, 56530,
        };
        const int64_t box2d_min[2] = {-80, -80};
        const int64_t box2d_max[2] = {80, 80};

        FlowQPoly2D_S8 p2_8;
        FlowQPoly2D_S12 p2_12;
        FlowQPoly2D_S16 p2_16;
        FlowQPoly2D_S20 p2_20;
        FLOW_ASSERT_EQ(FlowQPoly2D_S8_quantize_inner(&p2_8, 8, &a2d[0][0], b2d, denom_2d, box2d_min,
                                                     box2d_max),
                       1);
        FLOW_ASSERT_EQ(FlowQPoly2D_S12_quantize_inner(&p2_12, 8, &a2d[0][0], b2d, denom_2d,
                                                      box2d_min, box2d_max),
                       1);
        FLOW_ASSERT_EQ(FlowQPoly2D_S16_quantize_inner(&p2_16, 8, &a2d[0][0], b2d, denom_2d,
                                                      box2d_min, box2d_max),
                       1);
        FLOW_ASSERT_EQ(FlowQPoly2D_S20_quantize_inner(&p2_20, 8, &a2d[0][0], b2d, denom_2d,
                                                      box2d_min, box2d_max),
                       1);

        uint64_t exact_2d = 0;
        uint64_t q2_in[4] = {0, 0, 0, 0};
        uint64_t q2_unsound[4] = {0, 0, 0, 0};

        for (int32_t x0 = -80; x0 <= 80; ++x0) {
            for (int32_t x1 = -80; x1 <= 80; ++x1) {
                int32_t pt[2] = {x0, x1};
                bool ex = exact_rational_inside(8, 2, &a2d[0][0], b2d, pt);
                if (ex)
                    exact_2d++;
                bool in8 = (FlowQPoly2D_S8_eval(&p2_8, pt).violated_mask == 0ULL);
                bool in12 = (FlowQPoly2D_S12_eval(&p2_12, pt).violated_mask == 0ULL);
                bool in16 = (FlowQPoly2D_S16_eval(&p2_16, pt).violated_mask == 0ULL);
                bool in20 = (FlowQPoly2D_S20_eval(&p2_20, pt).violated_mask == 0ULL);
                if (in8) {
                    q2_in[0]++;
                    if (!ex)
                        q2_unsound[0]++;
                }
                if (in12) {
                    q2_in[1]++;
                    if (!ex)
                        q2_unsound[1]++;
                }
                if (in16) {
                    q2_in[2]++;
                    if (!ex)
                        q2_unsound[2]++;
                }
                if (in20) {
                    q2_in[3]++;
                    if (!ex)
                        q2_unsound[3]++;
                }
            }
        }

        for (int s = 0; s < 4; ++s) {
            FLOW_ASSERT_EQ(q2_unsound[s], 0ULL);
            FLOW_ASSERT_TRUE(q2_in[s] <= exact_2d);
        }

        const int64_t denom_3d = 997;
        const int64_t a3d[10][3] = {
            {997, 0, 0},  {-997, 0, 0},    {0, 997, 0},      {0, -997, 0},     {0, 0, 997},
            {0, 0, -997}, {577, 579, 581}, {-577, 579, 581}, {577, -579, 581}, {577, 579, -581},
        };
        const int64_t b3d[10] = {
            20 * 997, 20 * 997, 20 * 997, 20 * 997, 20 * 997, 20 * 997, 17348, 17348, 17348, 17348,
        };
        const int64_t box3d_min[3] = {-25, -25, -25};
        const int64_t box3d_max[3] = {25, 25, 25};

        FlowQPoly3D_S8 p3_8;
        FlowQPoly3D_S12 p3_12;
        FlowQPoly3D_S16 p3_16;
        FlowQPoly3D_S20 p3_20;
        FLOW_ASSERT_EQ(FlowQPoly3D_S8_quantize_inner(&p3_8, 10, &a3d[0][0], b3d, denom_3d,
                                                     box3d_min, box3d_max),
                       1);
        FLOW_ASSERT_EQ(FlowQPoly3D_S12_quantize_inner(&p3_12, 10, &a3d[0][0], b3d, denom_3d,
                                                      box3d_min, box3d_max),
                       1);
        FLOW_ASSERT_EQ(FlowQPoly3D_S16_quantize_inner(&p3_16, 10, &a3d[0][0], b3d, denom_3d,
                                                      box3d_min, box3d_max),
                       1);
        FLOW_ASSERT_EQ(FlowQPoly3D_S20_quantize_inner(&p3_20, 10, &a3d[0][0], b3d, denom_3d,
                                                      box3d_min, box3d_max),
                       1);

        uint64_t exact_3d = 0;
        uint64_t q3_in[4] = {0, 0, 0, 0};
        uint64_t q3_unsound[4] = {0, 0, 0, 0};

        for (int32_t x0 = -25; x0 <= 25; ++x0) {
            for (int32_t x1 = -25; x1 <= 25; ++x1) {
                for (int32_t x2 = -25; x2 <= 25; ++x2) {
                    int32_t pt[3] = {x0, x1, x2};
                    bool ex = exact_rational_inside(10, 3, &a3d[0][0], b3d, pt);
                    if (ex)
                        exact_3d++;
                    bool in8 = (FlowQPoly3D_S8_eval(&p3_8, pt).violated_mask == 0ULL);
                    bool in12 = (FlowQPoly3D_S12_eval(&p3_12, pt).violated_mask == 0ULL);
                    bool in16 = (FlowQPoly3D_S16_eval(&p3_16, pt).violated_mask == 0ULL);
                    bool in20 = (FlowQPoly3D_S20_eval(&p3_20, pt).violated_mask == 0ULL);
                    if (in8) {
                        q3_in[0]++;
                        if (!ex)
                            q3_unsound[0]++;
                    }
                    if (in12) {
                        q3_in[1]++;
                        if (!ex)
                            q3_unsound[1]++;
                    }
                    if (in16) {
                        q3_in[2]++;
                        if (!ex)
                            q3_unsound[2]++;
                    }
                    if (in20) {
                        q3_in[3]++;
                        if (!ex)
                            q3_unsound[3]++;
                    }
                }
            }
        }

        for (int s = 0; s < 4; ++s) {
            FLOW_ASSERT_EQ(q3_unsound[s], 0ULL);
            FLOW_ASSERT_TRUE(q3_in[s] <= exact_3d);
        }

        if (report_mode) {
            const int shifts[4] = {8, 12, 16, 20};
            for (int s = 0; s < 4; ++s) {
                uint64_t loss2_bps = ((exact_2d - q2_in[s]) * 10000ULL) / exact_2d;
                uint64_t loss3_bps = ((exact_3d - q3_in[s]) * 10000ULL) / exact_3d;
                printf(
                    "  [PoC A] shift=%2d | 2D unsound=%llu, inside=%llu/%llu (loss=%llu.%02llu%%)"
                    " | 3D unsound=%llu, inside=%llu/%llu (loss=%llu.%02llu%%)\n",
                    shifts[s], (unsigned long long)q2_unsound[s], (unsigned long long)q2_in[s],
                    (unsigned long long)exact_2d, (unsigned long long)(loss2_bps / 100ULL),
                    (unsigned long long)(loss2_bps % 100ULL), (unsigned long long)q3_unsound[s],
                    (unsigned long long)q3_in[s], (unsigned long long)exact_3d,
                    (unsigned long long)(loss3_bps / 100ULL),
                    (unsigned long long)(loss3_bps % 100ULL));
            }
        }
    }

    /* =====================================================================
     * PoC B: Evaluation Cost (10^7 runs) & Int64 Overflow Guard
     * ===================================================================== */
    FLOW_STAGE_BEGIN(2, "PoC B: FlowQPoly_eval vs Unpacked 64-Plane Eval (10^7 Iterations)");
    {
        FlowQPoly4D_S16 qpoly4;
        FlowQPoly4D_S16_init(&qpoly4);
        NaiveUnpackedPoly4D npoly4;
        memset(&npoly4, 0, sizeof(npoly4));
        for (size_t j = 0; j < 4; ++j) {
            atomic_init(&npoly4.translation[j], 0LL);
            atomic_init(&npoly4.scale[j], 1LL);
        }

        for (uint32_t i = 0; i < 8; ++i) {
            uint32_t axis = i / 2U;
            int32_t sign = (i % 2U == 0U) ? 1 : -1;
            qpoly4.A[i][axis] = sign * (1 << 16);
            atomic_store_explicit(&qpoly4.b[i], 100LL << 16, memory_order_relaxed);
            npoly4.coeffs[i][axis] = (int64_t)sign;
            atomic_store_explicit(&npoly4.b_constants[i], 100LL, memory_order_relaxed);
        }
        atomic_store_explicit(&qpoly4.active_mask, 0xFFULL, memory_order_release);
        atomic_store_explicit(&npoly4.active_mask, 0xFFULL, memory_order_release);

        const uint64_t iters = 10000000ULL;
        uint64_t sink_mask = 0ULL;

        uint64_t t0_q = flow_clock_monotonic_ns();
        for (uint64_t k = 0; k < iters; ++k) {
            int32_t x[4] = {(int32_t)(k & 63U), (int32_t)((k >> 2U) & 63U),
                            (int32_t)((k >> 4U) & 63U), (int32_t)((k >> 6U) & 63U)};
            FlowQPolyEval ev = qpoly_eval_4d_noinline(&qpoly4, x);
            sink_mask ^= ev.violated_mask ^ (uint64_t)ev.min_slack;
        }
        uint64_t t1_q = flow_clock_monotonic_ns();

        int64_t sink_viol = 0;
        uint64_t t0_n = flow_clock_monotonic_ns();
        for (uint64_t k = 0; k < iters; ++k) {
            int64_t x[4] = {(int64_t)(k & 63U), (int64_t)((k >> 2U) & 63U),
                            (int64_t)((k >> 4U) & 63U), (int64_t)((k >> 6U) & 63U)};
            int64_t mv = 0;
            sink_mask ^= naive_unpacked_eval_4d(&npoly4, x, &mv);
            sink_viol += mv;
        }
        uint64_t t1_n = flow_clock_monotonic_ns();

        uint64_t dt_q = (t1_q > t0_q) ? (t1_q - t0_q) : 1ULL;
        uint64_t dt_n = (t1_n > t0_n) ? (t1_n - t0_n) : 1ULL;
        FLOW_ASSERT_TRUE(dt_q > 0ULL && dt_n > 0ULL);
        (void)sink_mask;
        (void)sink_viol;

        FlowQPoly4D_S16 q_ovf;
        FlowQPoly4D_S16_init(&q_ovf);
        for (size_t j = 0; j < 4; ++j)
            q_ovf.A[0][j] = INT32_MAX;
        atomic_store_explicit(&q_ovf.b[0], INT64_MAX - 10, memory_order_relaxed);
        atomic_store_explicit(&q_ovf.active_mask, 1ULL, memory_order_release);
        const int32_t x_extreme[4] = {INT32_MAX, INT32_MAX, INT32_MAX, INT32_MAX};
        FlowQPolyEval ovf_ev = FlowQPoly4D_S16_eval(&q_ovf, x_extreme);
        FLOW_ASSERT_EQ(ovf_ev.violated_mask, 1ULL);
        FLOW_ASSERT_EQ(ovf_ev.worst_plane, 0U);
        FLOW_ASSERT_EQ(ovf_ev.worst_violation, INT64_MAX);

        if (report_mode) {
            uint64_t q_ps = (dt_q * 1000ULL) / iters;
            uint64_t n_ps = (dt_n * 1000ULL) / iters;
            uint64_t speedup_x100 = (dt_n * 100ULL) / dt_q;
            printf(
                "  [PoC B] 10^7 evals | FlowQPoly_eval=%llu.%03llu ns/call | Unpacked=%llu.%03llu"
                " ns/call | speedup=%llu.%02llux | ASM overflow safe=YES\n",
                (unsigned long long)(q_ps / 1000ULL), (unsigned long long)(q_ps % 1000ULL),
                (unsigned long long)(n_ps / 1000ULL), (unsigned long long)(n_ps % 1000ULL),
                (unsigned long long)(speedup_x100 / 100ULL),
                (unsigned long long)(speedup_x100 % 100ULL));
        }
    }

    /* =====================================================================
     * PoC C: 2-Jet Next-Face Forecast (2D Square + 1000 Quadratic Trajectories)
     * ===================================================================== */
    FLOW_STAGE_BEGIN(3, "PoC C: Pure Q16 2-Jet Quadratic Next-Face Forecast (1000 Trajectories)");
    {
        const int32_t A_sq[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        const int64_t b_sq[4] = {1000, 1000, 1000, 1000};
        const uint64_t active_sq = 0x0FULL;

        uint64_t seed = 0xC0FFEE123456789ULL;
        uint32_t matched_faces = 0U;
        uint64_t sum_t_err_q16 = 0ULL;
        uint64_t max_t_err_q16 = 0ULL;
        const uint32_t total_traj = 1000U;

        for (uint32_t k = 0; k < total_traj; ++k) {
            int32_t x[2], dx[2], ddx[2];
            x[0] = (int32_t)(flow_prng_next(&seed) % 801ULL) - 400;
            x[1] = (int32_t)(flow_prng_next(&seed) % 801ULL) - 400;
            dx[0] = (int32_t)(flow_prng_next(&seed) % 401ULL) - 200;
            dx[1] = (int32_t)(flow_prng_next(&seed) % 401ULL) - 200;
            ddx[0] = (int32_t)(flow_prng_next(&seed) % 101ULL) - 50;
            ddx[1] = (int32_t)(flow_prng_next(&seed) % 101ULL) - 50;
            if (dx[0] == 0 && dx[1] == 0 && ddx[0] == 0 && ddx[1] == 0)
                dx[0] = 50;
            if (ddx[0] == 0 && ddx[1] == 0 && dx[0] == 0)
                dx[0] = 40;
            if (ddx[0] == 0 && ddx[1] == 0)
                ddx[0] = 15;

            FlowQPolyForecast fc =
                flow_qpoly_predict_next_face(&A_sq[0][0], b_sq, active_sq, 2, x, dx, ddx);
            FLOW_ASSERT_TRUE(fc.will_exit);

            int64_t best_true_t_q16 = INT64_MAX;
            uint32_t true_plane = 0U;
            for (uint32_t i = 0; i < 4U; ++i) {
                int64_t s = b_sq[i] - ((int64_t)A_sq[i][0] * x[0] + (int64_t)A_sq[i][1] * x[1]);
                int64_t r = (int64_t)A_sq[i][0] * dx[0] + (int64_t)A_sq[i][1] * dx[1];
                int64_t c = (int64_t)A_sq[i][0] * ddx[0] + (int64_t)A_sq[i][1] * ddx[1];
                FlowJet2 sj = flow_jet2_make(s, -r, -c);
                int64_t t_i_q16 = 0;
                if (flow_jet2_solve_root(sj, INT64_MAX, &t_i_q16) && t_i_q16 > 0 &&
                    t_i_q16 < best_true_t_q16) {
                    best_true_t_q16 = t_i_q16;
                    true_plane = i;
                }
            }

            if (fc.plane == true_plane)
                matched_faces++;
            uint64_t err_q16 = (fc.t_hit_q >= best_true_t_q16)
                                   ? (uint64_t)(fc.t_hit_q - best_true_t_q16)
                                   : (uint64_t)(best_true_t_q16 - fc.t_hit_q);
            sum_t_err_q16 += err_q16;
            if (err_q16 > max_t_err_q16)
                max_t_err_q16 = err_q16;
        }

        FLOW_ASSERT_EQ(matched_faces, total_traj);
        FLOW_ASSERT_EQ(max_t_err_q16, 0ULL);

        if (report_mode) {
            printf("  [PoC C] 1000 trajectories | face accuracy=%u/%u (100.00%%) | "
                   "mean |dt_q16|=%llu LSB | max |dt_q16|=%llu LSB\n",
                   matched_faces, total_traj, (unsigned long long)(sum_t_err_q16 / total_traj),
                   (unsigned long long)max_t_err_q16);
        }
    }

    /* =====================================================================
     * PoC D: Cubical HoTT Single-Bit Neighbor Transition vs Brute-Force
     * ===================================================================== */
    FLOW_STAGE_BEGIN(4, "PoC D: Single-Bit Neighbor Transition vs Brute-Force Kan Check");
    {
        uint64_t seed = 0xCAFEBABE98765432ULL;
        const uint32_t trials = 10000U;
        uint32_t agreements = 0U;

        for (uint32_t t = 0; t < trials; ++t) {
            uint64_t mask = flow_prng_next(&seed);
            uint64_t filter = flow_prng_next(&seed);
            uint32_t bit = (uint32_t)(flow_prng_next(&seed) % 64ULL);

            uint64_t next_mask = 0ULL;
            FlowKanStatus st = flow_cubical_step_neighbor(mask, bit, filter, &next_mask);

            uint64_t target = mask ^ (1ULL << bit);
            bool brute_legal = true;
            for (uint32_t k = 0; k < 64U; ++k) {
                uint64_t bit_k = 1ULL << k;
                if ((filter & bit_k) != 0ULL) {
                    if ((mask & bit_k) != (target & bit_k)) {
                        brute_legal = false;
                        break;
                    }
                }
            }
            FlowKanStatus expected_st =
                brute_legal ? FLOW_KAN_FILLED_HOMOTOPIC : FLOW_KAN_OBSTRUCTED_SINGULARITY;
            uint64_t expected_next = brute_legal ? target : mask;

            if (st == expected_st && next_mask == expected_next)
                agreements++;
        }

        FLOW_ASSERT_EQ(agreements, trials);

        if (report_mode) {
            printf("  [PoC D] %u random neighbor transitions | Kan agreement=%u/%u (100.00%%)\n",
                   trials, agreements, trials);
        }
    }

    /* =====================================================================
     * PoC E: Resource Sieve & Hot-Path Zero Kernel Overhead (10^6 Iterations)
     * ===================================================================== */
    FLOW_STAGE_BEGIN(5, "PoC E: Hot-Path Kernel Overhead (10^6 Iterations via getrusage)");
    {
        FlowQPoly4D_S16 hot_poly;
        FlowQPoly4D_S16_init(&hot_poly);
        for (uint32_t i = 0; i < 8U; ++i) {
            hot_poly.A[i][i % 4U] = (i % 2U == 0U) ? (1 << 16) : -(1 << 16);
            atomic_store_explicit(&hot_poly.b[i], 1000LL << 16, memory_order_relaxed);
        }
        atomic_store_explicit(&hot_poly.active_mask, 0xFFULL, memory_order_release);

        PocHotRing ring;
        PocHotRing_init(&ring);

        FlowReloadContext *ctx = flow_reload_create(NULL);
        FLOW_ASSERT_TRUE(ctx != NULL);
        FlowReloadReader reader;
        memset(&reader, 0, sizeof(reader));
        FLOW_ASSERT_EQ(flow_reload_reader_register(ctx, &reader), FLOW_RELOAD_OK);

        FlowUnit hot_unit = {.abi_version = FLOW_RELOAD_ABI_VERSION,
                             .semantic_schema_hash = 0xAA01ULL,
                             .constraint_hash = 0xBB02ULL,
                             .capability_hash = 0xCC03ULL,
                             .name = "poc_hot_unit",
                             .layout = FLOW_LAYOUT_SOA,
                             .init = NULL,
                             .run = poc_hot_unit_run,
                             .drop = NULL};
        FLOW_ASSERT_EQ(flow_reload_activate(ctx, &hot_unit), FLOW_RELOAD_OK);
        flow_qsbr_checkpoint(&reader);
        FLOW_ASSERT_EQ(flow_qsbr_synchronize(ctx, 1000000ULL), FLOW_RELOAD_OK);

        uint64_t res_sieve = flow_reload_resource_sieve(ctx, &reader);
        if (!PocHotRing_has_space(&ring))
            res_sieve &= ~(uint64_t)FLOW_RES_RING_HAS_SPACE;
        FLOW_ASSERT_EQ(res_sieve, (uint64_t)FLOW_RES_CANONICAL_ALL);

        /* Warm up one pass before measuring rusage */
        {
            int32_t wx[4] = {10, 20, 30, 40};
            FlowQPolyEval wev = FlowQPoly4D_S16_eval(&hot_poly, wx);
            uint64_t wn = 0ULL;
            (void)flow_cubical_step_neighbor(wev.violated_mask, wev.tightest_plane, 0ULL, &wn);
            (void)PocHotRing_push(&ring, wn);
            uint64_t popped = 0ULL, out_v = 0ULL;
            (void)PocHotRing_pop(&ring, &popped);
            (void)flow_reload_call(ctx, &reader, &popped, &out_v);
        }

        struct rusage ru_before, ru_after;
        FLOW_ASSERT_EQ(getrusage(RUSAGE_SELF, &ru_before), 0);
        uint64_t t0_ns = flow_clock_monotonic_ns();

        const uint64_t hot_iters = 1000000ULL;
        uint64_t acc = 0ULL;
        for (uint64_t k = 0; k < hot_iters; ++k) {
            int32_t x[4] = {(int32_t)(k & 255U), (int32_t)((k >> 3U) & 255U),
                            (int32_t)((k >> 6U) & 255U), (int32_t)((k >> 9U) & 255U)};
            FlowQPolyEval ev = FlowQPoly4D_S16_eval(&hot_poly, x);
            uint64_t r_mask = flow_reload_resource_sieve(ctx, &reader);
            uint64_t next_mask = 0ULL;
            FlowKanStatus kst =
                flow_cubical_step_neighbor(ev.violated_mask, ev.tightest_plane, 0ULL, &next_mask);
            if (kst == FLOW_KAN_FILLED_HOMOTOPIC &&
                (r_mask & (uint64_t)FLOW_RES_CANONICAL_ALL) == (uint64_t)FLOW_RES_CANONICAL_ALL) {
                (void)PocHotRing_push(&ring, next_mask ^ k);
                uint64_t msg = 0ULL, out = 0ULL;
                (void)PocHotRing_pop(&ring, &msg);
                (void)flow_reload_call(ctx, &reader, &msg, &out);
                acc += out;
            }
        }

        uint64_t t1_ns = flow_clock_monotonic_ns();
        FLOW_ASSERT_EQ(getrusage(RUSAGE_SELF, &ru_after), 0);
        FLOW_ASSERT_NE(acc, 0ULL);

        long d_minflt = ru_after.ru_minflt - ru_before.ru_minflt;
        long d_majflt = ru_after.ru_majflt - ru_before.ru_majflt;
        long d_nvcsw = ru_after.ru_nvcsw - ru_before.ru_nvcsw;
        long d_nivcsw = ru_after.ru_nivcsw - ru_before.ru_nivcsw;

        FLOW_ASSERT_EQ(d_minflt, 0L);
        FLOW_ASSERT_EQ(d_majflt, 0L);
        FLOW_ASSERT_EQ(d_nvcsw, 0L);

        if (report_mode) {
            uint64_t elapsed_ns = t1_ns - t0_ns;
            uint64_t ns_int = elapsed_ns / hot_iters;
            uint64_t ns_frac = ((elapsed_ns % hot_iters) * 100ULL) / hot_iters;
            printf("  [PoC E] 10^6 hot-path steps (%llu.%02llu ns/step) | ru_minflt=+%ld | "
                   "ru_majflt=+%ld | ru_nvcsw=+%ld | ru_nivcsw=+%ld\n",
                   (unsigned long long)ns_int, (unsigned long long)ns_frac, d_minflt, d_majflt,
                   d_nvcsw, d_nivcsw);
        }

        FLOW_ASSERT_EQ(flow_reload_reader_unregister(&reader), FLOW_RELOAD_OK);
        FLOW_ASSERT_EQ(flow_reload_destroy(ctx), FLOW_RELOAD_OK);
    }

    FLOW_TEST_SUITE_END();
}
