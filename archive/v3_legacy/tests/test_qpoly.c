#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "hott_test_kit.h"
#include "cubical_hott.h"
#include "flow_jet.h"
#include "flow_spsc.h"
#include "reload.h"
#include "flow_hw_probe.h"

#include <math.h>
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

/* =========================================================================
 * Legacy double polyhedral baseline for PoC B comparison
 * ========================================================================= */
typedef struct __attribute__((aligned(FLOW_HW_CACHE_LINE_BYTES))) {
    double coeffs[FLOW_QPOLY_PLANES][4];
    _Atomic double b_constants[FLOW_QPOLY_PLANES];
    _Atomic uint64_t active_mask;
    _Atomic double translation[4];
    _Atomic double scale[4];
} LegacyDoublePoly4D;

static __attribute__((noinline)) uint64_t legacy_double_eval_4d(const LegacyDoublePoly4D *tpl,
                                                                const double x[4],
                                                                double *max_viol_out) {
    uint64_t mask = atomic_load_explicit(&tpl->active_mask, memory_order_acquire);
    uint64_t violated = 0ULL;
    double local_x[4];
    double max_viol = 0.0;
    for (size_t j = 0; j < 4; ++j) {
        double tr = atomic_load_explicit(&tpl->translation[j], memory_order_relaxed);
        double sc = atomic_load_explicit(&tpl->scale[j], memory_order_relaxed);
        local_x[j] = (x[j] - tr) / sc;
    }
    for (uint32_t i = 0; i < FLOW_QPOLY_PLANES; ++i) {
        if (!((mask >> i) & 1ULL))
            continue;
        double dot = 0.0;
        for (size_t j = 0; j < 4; ++j)
            dot += tpl->coeffs[i][j] * local_x[j];
        double viol = dot - atomic_load_explicit(&tpl->b_constants[i], memory_order_relaxed);
        if (viol > 0.0) {
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

/* =========================================================================
 * Helper unit for PoC E (Zero-Kernel-Overhead Hot Path)
 * ========================================================================= */
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
        "Lean Polyhedron -> HoTT Neighbor Search -> Hardware Spec -> C17 PoC (A-E)");

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
        /* 2D 8-plane non-axis-aligned octagon with prime denominator D = 997 */
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

        /* 3D 10-plane polyhedron with prime denominator D = 997 */
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
                double loss2 = (double)(exact_2d - q2_in[s]) * 100.0 / (double)exact_2d;
                double loss3 = (double)(exact_3d - q3_in[s]) * 100.0 / (double)exact_3d;
                printf("  [PoC A] shift=%2d | 2D unsound=%llu, inside=%llu/%llu (loss=%.4f%%) | "
                       "3D unsound=%llu, inside=%llu/%llu (loss=%.4f%%)\n",
                       shifts[s], (unsigned long long)q2_unsound[s], (unsigned long long)q2_in[s],
                       (unsigned long long)exact_2d, loss2, (unsigned long long)q3_unsound[s],
                       (unsigned long long)q3_in[s], (unsigned long long)exact_3d, loss3);
            }
        }
    }

    /* =====================================================================
     * PoC B: Evaluation Cost (10^7 runs) & Int64 Overflow Guard
     * ===================================================================== */
    FLOW_STAGE_BEGIN(2, "PoC B: FlowQPoly_eval vs Legacy double Eval (10^7 Iterations)");
    {
        FlowQPoly4D_S16 qpoly4;
        FlowQPoly4D_S16_init(&qpoly4);
        LegacyDoublePoly4D dpoly4;
        memset(&dpoly4, 0, sizeof(dpoly4));
        for (size_t j = 0; j < 4; ++j) {
            atomic_init(&dpoly4.translation[j], 0.0);
            atomic_init(&dpoly4.scale[j], 1.0);
        }

        for (uint32_t i = 0; i < 8; ++i) {
            uint32_t axis = i / 2U;
            int32_t sign = (i % 2U == 0U) ? 1 : -1;
            qpoly4.A[i][axis] = sign * (1 << 16);
            atomic_store_explicit(&qpoly4.b[i], 100LL << 16, memory_order_relaxed);
            dpoly4.coeffs[i][axis] = (double)sign;
            atomic_store_explicit(&dpoly4.b_constants[i], 100.0, memory_order_relaxed);
        }
        atomic_store_explicit(&qpoly4.active_mask, 0xFFULL, memory_order_release);
        atomic_store_explicit(&dpoly4.active_mask, 0xFFULL, memory_order_release);

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

        double sink_viol = 0.0;
        uint64_t t0_d = flow_clock_monotonic_ns();
        for (uint64_t k = 0; k < iters; ++k) {
            double x[4] = {(double)(k & 63U), (double)((k >> 2U) & 63U), (double)((k >> 4U) & 63U),
                           (double)((k >> 6U) & 63U)};
            double mv = 0.0;
            sink_mask ^= legacy_double_eval_4d(&dpoly4, x, &mv);
            sink_viol += mv;
        }
        uint64_t t1_d = flow_clock_monotonic_ns();

        double ns_per_q = (double)(t1_q - t0_q) / (double)iters;
        double ns_per_d = (double)(t1_d - t0_d) / (double)iters;
        FLOW_ASSERT_TRUE(ns_per_q > 0.0 && ns_per_d > 0.0);
        (void)sink_mask;
        (void)sink_viol;

        /* Verify saturation / overflow protection on extreme INT32_MAX coefficients & inputs */
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

        /* Verify FlowResultU64 & flow_hott_boundary_tag_code alongside FlowQPoly evaluation */
        const int32_t x_in4[4] = {0, 0, 0, 0};
        FlowQPolyEval in_ev = FlowQPoly4D_S16_eval(&qpoly4, x_in4);
        FlowResultU64 r_in = (in_ev.violated_mask == 0ULL)
                                 ? flow_result_u64_interior((uint64_t)in_ev.min_slack)
                                 : flow_result_u64_boundary(2U, 0U, (uint8_t)FLOW_BOUNDARY_PRECOND,
                                                            (uint8_t)in_ev.worst_plane, 404U);
        FLOW_ASSERT_FALSE(flow_result_u64_is_boundary(r_in));
        FLOW_ASSERT_EQ(r_in.value, (uint64_t)(100LL << 16));

        FlowResultU64 r_out = (ovf_ev.violated_mask == 0ULL)
                                  ? flow_result_u64_interior((uint64_t)ovf_ev.min_slack)
                                  : flow_result_u64_boundary(2U, 0U, (uint8_t)FLOW_BOUNDARY_PRECOND,
                                                             (uint8_t)ovf_ev.worst_plane, 404U);
        FLOW_ASSERT_TRUE(flow_result_u64_is_boundary(r_out));
        FLOW_ASSERT_EQ(flow_hott_boundary_tag_code(r_out.boundary_tag), 404U);

        if (report_mode) {
            printf(
                "  [PoC B] 10^7 evals | FlowQPoly_eval=%.3f ns/call | Legacy double=%.3f ns/call "
                "| speedup=%.2fx | int64 overflow safe=YES\n",
                ns_per_q, ns_per_d, ns_per_d / ns_per_q);
        }
    }

    /* =====================================================================
     * PoC C: 2-Jet Next-Face Forecast (2D Square + 1000 Quadratic Trajectories)
     * ===================================================================== */
    FLOW_STAGE_BEGIN(3, "PoC C: 2-Jet Quadratic Next-Face Forecast (1000 Trajectories)");
    {
        /* 2D Square [-1000, 1000]^2:
         * Plane 0: +x0 <= 1000
         * Plane 1: -x0 <= 1000
         * Plane 2: +x1 <= 1000
         * Plane 3: -x1 <= 1000
         */
        const int32_t A_sq[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        const int64_t b_sq[4] = {1000, 1000, 1000, 1000};
        const uint64_t active_sq = 0x0FULL;

        uint64_t seed = 0xC0FFEE123456789ULL;
        uint32_t matched_faces = 0U;
        double sum_t_err = 0.0;
        double max_t_err = 0.0;
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
            /* Ensure trajectory exits the square by guaranteeing non-zero acceleration if needed */
            if (ddx[0] == 0 && ddx[1] == 0 && dx[0] == 0)
                dx[0] = 40;
            if (ddx[0] == 0 && (dx[0] * ddx[0] < 0))
                ddx[0] = (dx[0] >= 0) ? 10 : -10;
            if (ddx[0] == 0 && ddx[1] == 0)
                ddx[0] = 15;

            FlowQPolyForecast fc =
                flow_qpoly_predict_next_face(&A_sq[0][0], b_sq, active_sq, 2, x, dx, ddx);
            FLOW_ASSERT_TRUE(fc.will_exit);

            /* Ground-truth analytical crossing of s_i(t) = b_i - a_i.(x + dx*t + 0.5*ddx*t^2) = 0
             */
            double best_true_t = 1e30;
            uint32_t true_plane = 0U;
            for (uint32_t i = 0; i < 4U; ++i) {
                double s = (double)(b_sq[i] - (A_sq[i][0] * x[0] + A_sq[i][1] * x[1]));
                double r = (double)(A_sq[i][0] * dx[0] + A_sq[i][1] * dx[1]);
                double c = (double)(A_sq[i][0] * ddx[0] + A_sq[i][1] * ddx[1]);
                double t_i = -1.0;
                if (c == 0.0) {
                    if (r > 0.0)
                        t_i = s / r;
                } else {
                    double disc = r * r + 2.0 * c * s;
                    if (disc >= 0.0 && (c > 0.0 || r > 0.0))
                        t_i = (-r + sqrt(disc)) / c;
                }
                if (t_i > 0.0 && t_i < best_true_t) {
                    best_true_t = t_i;
                    true_plane = i;
                }
            }

            if (fc.plane == true_plane)
                matched_faces++;
            double pred_t = (double)fc.t_hit_q / 65536.0;
            double err = fabs(pred_t - best_true_t);
            sum_t_err += err;
            if (err > max_t_err)
                max_t_err = err;
        }

        FLOW_ASSERT_EQ(matched_faces, total_traj);
        FLOW_ASSERT_TRUE(max_t_err < 1e-4);

        if (report_mode) {
            printf("  [PoC C] 1000 trajectories | face accuracy=%u/%u (%.2f%%) | mean |dt|=%.6e | "
                   "max |dt|=%.6e\n",
                   matched_faces, total_traj, (double)matched_faces * 100.0 / (double)total_traj,
                   sum_t_err / (double)total_traj, max_t_err);
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

            /* Brute-force enumeration over all 64 axes to check if any protected axis changed */
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
            printf("  [PoC E] 10^6 hot-path steps (%.2f ns/step) | ru_minflt=+%ld | ru_majflt=+%ld "
                   "| ru_nvcsw=+%ld | ru_nivcsw=+%ld\n",
                   (double)(t1_ns - t0_ns) / (double)hot_iters, d_minflt, d_majflt, d_nvcsw,
                   d_nivcsw);
        }

        FLOW_ASSERT_EQ(flow_reload_reader_unregister(&reader), FLOW_RELOAD_OK);
        FLOW_ASSERT_EQ(flow_reload_destroy(ctx), FLOW_RELOAD_OK);
    }

    FLOW_TEST_SUITE_END();
}
