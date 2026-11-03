#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "hott_test_kit.h"

#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* =========================================================================
 * Compile-Time Univalence Verification (0 Runtime Bytes)
 * ========================================================================= */
typedef struct __attribute__((aligned(64))) {
    int64_t q0[2];
    int64_t q1[2];
    int64_t q2[2];
    uint64_t external_sieve;
    uint64_t epoch;
} ExternalObservationABI;

typedef struct __attribute__((aligned(64))) {
    FlowQJet2 axis[2];
    uint64_t syndrome_mask;
    uint64_t generation;
} InternalStateRegisterFile;

FLOW_STATIC_ASSERT_UNIVALENT(ExternalObservationABI, InternalStateRegisterFile);

/* Universal External-Constraint Polyhedron (3D Q16) */
FLOW_QPOLY_DEFINE(FlowExtPoly3D, 3, 16)

/* External Constraint Packet for Lock-Free SPSC Streaming */
typedef struct {
    uint32_t plane_idx;
    int64_t b_enter_ext;
    int64_t b_exit_ext;
} ExternalConstraintMsg;

FLOW_SPSC_DEFINE(ExtConstraintRing, ExternalConstraintMsg, 64)

typedef struct {
    FlowQsbrEpoch *qsbr;
    _Atomic uint32_t readers_ready;
    _Atomic int stop;
    _Atomic uint64_t reads_completed;
    _Atomic uint64_t errors;
} QsbrStressArgs;

static void *qsbr_reader_thread_fn(void *arg) {
    QsbrStressArgs *sa = (QsbrStressArgs *)arg;
    FlowReloadReader reader;
    if (flow_reload_reader_register(sa->qsbr, &reader) != FLOW_RELOAD_OK) {
        atomic_fetch_add_explicit(&sa->errors, 1ULL, memory_order_relaxed);
        atomic_fetch_add_explicit(&sa->readers_ready, 1U, memory_order_release);
        return NULL;
    }
    atomic_fetch_add_explicit(&sa->readers_ready, 1U, memory_order_release);
    const int32_t u_obs[3] = {300, 0, 0};
    while (!atomic_load_explicit(&sa->stop, memory_order_acquire)) {
        const FlowExtPoly3D *bank = (const FlowExtPoly3D *)flow_qsbr_load_ptr(sa->qsbr, &reader);
        if (!bank) {
            atomic_fetch_add_explicit(&sa->errors, 1ULL, memory_order_relaxed);
            break;
        }
        FlowQPolyEval ev = FlowExtPoly3D_eval(bank, u_obs);
        if (ev.violated_mask != 0ULL && ev.violated_mask != 1ULL) {
            atomic_fetch_add_explicit(&sa->errors, 1ULL, memory_order_relaxed);
        }
        flow_qsbr_checkpoint(&reader);
        atomic_fetch_add_explicit(&sa->reads_completed, 1ULL, memory_order_relaxed);
    }
    if (flow_reload_reader_unregister(&reader) != FLOW_RELOAD_OK) {
        atomic_fetch_add_explicit(&sa->errors, 1ULL, memory_order_relaxed);
    }
    return NULL;
}

static _Atomic uint32_t g_unit_drops = 0U;

static int test_unit_init_1(void *host, void **state_out) {
    (void)host;
    *state_out = (void *)(uintptr_t)0x1111ULL;
    return 0;
}

static int test_unit_init_2(void *host, void **state_out) {
    (void)host;
    *state_out = (void *)(uintptr_t)0x2222ULL;
    return 0;
}

static void test_unit_drop(void *host, void *state) {
    (void)host;
    if (state) {
        atomic_fetch_add_explicit(&g_unit_drops, 1U, memory_order_relaxed);
    }
}

static int test_unit_run(void *host, void *state, const void *in, void *out) {
    (void)host;
    (void)in;
    *(uint64_t *)out = (uint64_t)(uintptr_t)state;
    return 0;
}

int main(void) {
    FLOW_TEST_SUITE_BEGIN(
        "FLOW v4: Universal External-Constraint ABI (b_ext, u_ext), Hysteresis & Anti-Tunneling");

    /* =====================================================================
     * Stage 1: Compile-Time Univalence & Bitwise HIT Exception Quotient
     * ===================================================================== */
    FLOW_STAGE_BEGIN(1, "Compile-Time Univalence & Bitwise HIT Exception Quotient (0 Structs)");
    {
        FLOW_ASSERT_EQ(sizeof(ExternalObservationABI), 64U);
        FLOW_ASSERT_EQ(sizeof(InternalStateRegisterFile), 64U);
        FLOW_ASSERT_EQ(_Alignof(ExternalObservationABI), 64U);

        uint64_t ambient = 0xFFFFFFFFFFFFFFFFULL;
        uint64_t exc_mask = (1ULL << 3) | (1ULL << 17) | (1ULL << 42) | (1ULL << 61);
        uint64_t interior = flow_hott_quotient_sieve(ambient, exc_mask);
        uint32_t dof = flow_hott_quotient_dof(ambient, exc_mask);
        FLOW_ASSERT_EQ(interior, ambient & ~exc_mask);
        FLOW_ASSERT_EQ(dof, 60U);
        FLOW_ASSERT_EQ(flow_topos_classify_subobject(interior, ambient), 1U);
        FLOW_ASSERT_EQ(flow_topos_classify_subobject(ambient, interior), 0U);
    }

    /* =====================================================================
     * Stage 2: Domain-Agnostic External Constraints (Lean / SDF / Quota)
     * ===================================================================== */
    FLOW_STAGE_BEGIN(2, "Universal External Constraint Evaluation Across Application Domains");
    {
        FlowExtPoly3D poly;
        FlowExtPoly3D_init(&poly);

        /*
         * Define 6 static normals (octahedral + box planes):
         *   Plane 0: +x <= b_0 (e.g., Lean formal safety bound)
         *   Plane 1: -x <= b_1
         *   Plane 2: +y <= b_2 (e.g., 3D Frustum / SDF boundary)
         *   Plane 3: -y <= b_3
         *   Plane 4: +z <= b_4 (e.g., Network / DMA bandwidth quota)
         *   Plane 5:  x + y + z <= b_5 (coupled multi-axis constraint)
         */
        poly.A[0][0] = 1;
        poly.A[1][0] = -1;
        poly.A[2][1] = 1;
        poly.A[3][1] = -1;
        poly.A[4][2] = 1;
        poly.A[5][0] = 1;
        poly.A[5][1] = 1;
        poly.A[5][2] = 1;

        const int64_t b_ext_initial[6] = {1000, 1000, 800, 800, 500, 1500};
        FlowExtPoly3D_update_external_bounds(&poly, b_ext_initial, 0x3FULL);
        FLOW_ASSERT_EQ(atomic_load_explicit(&poly.generation, memory_order_acquire), 2ULL);

        /* Interior external observation u_ext = (400, 400, 400) -> x+y+z = 1200 <= 1500 */
        const int32_t u_in[3] = {400, 400, 400};
        FlowQPolyEval ev_in = FlowExtPoly3D_eval(&poly, u_in);
        FLOW_ASSERT_EQ(ev_in.violated_mask, 0ULL);
        FLOW_ASSERT_EQ(ev_in.min_slack, 100LL); /* Plane 4: 500 - 400 = 100 */
        FLOW_ASSERT_EQ(ev_in.tightest_plane, 4U);

        /* Tighten external constraint b_ext[5] dynamically at runtime from 1500 -> 1100 */
        int64_t b_ext_tight[6] = {1000, 1000, 800, 800, 500, 1100};
        FlowExtPoly3D_update_external_bounds(&poly, b_ext_tight, 0x3FULL);

        /* Same observation u_ext = (400, 400, 400) now violates Plane 5 by 100 */
        FlowQPolyEval ev_out = FlowExtPoly3D_eval(&poly, u_in);
        FLOW_ASSERT_EQ(ev_out.violated_mask, (1ULL << 5));
        FLOW_ASSERT_EQ(ev_out.worst_plane, 5U);
        FLOW_ASSERT_EQ(ev_out.worst_violation, 100LL);

        /* Verify single active plane with INT64_MAX slack sets tightest_plane to that plane */
        FlowExtPoly3D single_plane;
        FlowExtPoly3D_init(&single_plane);
        single_plane.A[5][0] = 1;
        atomic_store_explicit(&single_plane.b[5], INT64_MAX, memory_order_relaxed);
        atomic_store_explicit(&single_plane.active_mask, (1ULL << 5), memory_order_release);
        const int32_t z_pt[3] = {0, 0, 0};
        FlowQPolyEval ev_sp = FlowExtPoly3D_eval(&single_plane, z_pt);
        FLOW_ASSERT_EQ(ev_sp.violated_mask, 0ULL);
        FLOW_ASSERT_EQ(ev_sp.tightest_plane, 5U);
        FLOW_ASSERT_EQ(ev_sp.min_slack, INT64_MAX);

        /* Verify dim=3 intermediate overflow (2^62 + 2^62 - 100 = INT64_MAX - 99) and true overflow
         */
        FlowExtPoly3D ovf3_poly;
        FlowExtPoly3D_init(&ovf3_poly);
        ovf3_poly.A[0][0] = INT32_MIN;
        ovf3_poly.A[0][1] = INT32_MIN;
        ovf3_poly.A[0][2] = -100;
        ovf3_poly.A[1][0] = INT32_MIN;
        ovf3_poly.A[1][1] = INT32_MIN;
        ovf3_poly.A[1][2] = INT32_MIN;
        atomic_store_explicit(&ovf3_poly.b[0], INT64_MAX - 100LL, memory_order_relaxed);
        atomic_store_explicit(&ovf3_poly.b[1], INT64_MAX - 50LL, memory_order_relaxed);
        atomic_store_explicit(&ovf3_poly.active_mask, 0x3ULL, memory_order_release);
        const int32_t x_ovf3[3] = {INT32_MIN, INT32_MIN, 1};
        FlowQPolyEval ev_ovf3 = FlowExtPoly3D_eval(&ovf3_poly, x_ovf3);
        /* Plane 0: dot = INT64_MAX - 99 > b[0] (INT64_MAX - 100), so violated by 1! */
        FLOW_ASSERT_EQ(ev_ovf3.violated_mask, 1ULL);
        FLOW_ASSERT_EQ(ev_ovf3.worst_violation, 1LL);

        const int32_t x_ovf3_all[3] = {INT32_MIN, INT32_MIN, INT32_MIN};
        FlowQPolyEval ev_ovf3_all = FlowExtPoly3D_eval(&ovf3_poly, x_ovf3_all);
        /* Both Plane 0 (2^63 + 100*2^31) and Plane 1 (3*2^62) overflow INT64_MAX */
        FLOW_ASSERT_EQ(ev_ovf3_all.violated_mask, 0x3ULL);
        FLOW_ASSERT_EQ(ev_ovf3_all.worst_violation, INT64_MAX);
    }

    /* =====================================================================
     * Stage 3: Branchless 1-Cycle Dual-Threshold Hysteresis (b_enter/b_exit)
     * ===================================================================== */
    FLOW_STAGE_BEGIN(3, "Branchless 1-Cycle Dual-Threshold Hysteresis (b_enter vs b_exit)");
    {
        FlowExtPoly3D hpoly;
        FlowExtPoly3D_init(&hpoly);
        hpoly.A[0][0] = 1; /* Plane 0: x_0 <= b_active */

        /*
         * Configure hysteresis on Plane 0:
         *   - When interior (not violated), violation triggers at x_0 > b_enter = 1000.
         *   - Once violated, recovery requires dropping back to x_0 <= b_exit = 850.
         */
        FlowExtPoly3D_set_hysteresis(&hpoly, 0U, 1000LL, 850LL);

        /* Trajectory chattering around [880, 1020] */
        const int32_t x_seq[6] = {950, 1020, 960, 900, 850, 920};
        const uint64_t expected_viol[6] = {
            0ULL, /* 950 <= 1000 (enter bound): interior */
            1ULL, /* 1020 > 1000 (enter bound): enters violation! */
            1ULL, /* 960 > 850 (exit bound active!): stays violated despite <= 1000 */
            1ULL, /* 900 > 850 (exit bound active!): stays violated */
            0ULL, /* 850 <= 850 (exit bound satisfied!): recovers to interior! */
            0ULL  /* 920 <= 1000 (enter bound restored!): remains interior! */
        };

        uint64_t mask = 0ULL;
        for (size_t step = 0; step < 6U; ++step) {
            int32_t u_ext[3] = {x_seq[step], 0, 0};
            FlowQPolyEval ev = FlowExtPoly3D_eval_hysteresis(&hpoly, u_ext, mask);
            mask = ev.violated_mask;
            FLOW_ASSERT_EQ(mask, expected_viol[step]);
        }
    }

    /* =====================================================================
     * Stage 4: 2-Jet Quadratic Root Solver & Worldline Anti-Tunneling
     * ===================================================================== */
    FLOW_STAGE_BEGIN(4, "2-Jet Quadratic Root Solver & Worldline Anti-Tunneling");
    {
        /*
         * Suppose an external trajectory u(t) starts at x(0) = 0.8 (inside x <= 1.0)
         * and ends at x(1.0) = 0.8 (still inside x <= 1.0!), but has initial velocity
         * v_1 = +1.0 and acceleration a_1 = -2.0:
         *   x(t) = 0.8 + 1.0*t - 1.0*t^2
         * At t = 0.5, x(0.5) = 1.05 > 1.0 (tunnels through the wall x = 1.0 at t ≈ 0.2764!).
         * Discrete endpoint sampling at t=0 and t=1 misses the collision completely,
         * whereas 2-Jet slack root-solving catches the exact crossing time!
         */
        FlowJet2 slack_worldline = flow_jet2_make(1.0 - 0.8, -1.0, 2.0);
        FLOW_ASSERT_FLOAT_EQ(flow_jet2_eval_taylor(slack_worldline, 0.0), 0.2, 1e-9);
        FLOW_ASSERT_FLOAT_EQ(flow_jet2_eval_taylor(slack_worldline, 1.0), 0.2, 1e-9);
        FLOW_ASSERT_FLOAT_EQ(flow_jet2_eval_taylor(slack_worldline, 0.5), -0.05, 1e-9);

        double t_hit = 0.0;
        bool tunneled = flow_jet2_solve_root(slack_worldline, 1.0, &t_hit);
        FLOW_ASSERT_TRUE(tunneled);
        /* Exact smallest root of 0.2 - t + t^2 = 0 is (1 - sqrt(1 - 0.8)) / 2 ≈ 0.2763932 */
        FLOW_ASSERT_FLOAT_EQ(t_hit, (1.0 - sqrt(0.2)) * 0.5, 1e-6);

        /* Also verify via integer Q-Polyhedron 2-Jet face predictor */
        const int32_t A_wall[1][3] = {{1, 0, 0}};
        const int64_t b_wall[1] = {1000};
        const int32_t x0[3] = {800, 0, 0};
        const int32_t dx0[3] = {1000, 0, 0};
        const int32_t ddx0[3] = {-2000, 0, 0};
        FlowQPolyForecast fc =
            flow_qpoly_predict_next_face(&A_wall[0][0], b_wall, 1ULL, 3U, x0, dx0, ddx0);
        FLOW_ASSERT_TRUE(fc.will_exit);
        FLOW_ASSERT_EQ(fc.plane, 0U);
        FLOW_ASSERT_FLOAT_EQ((double)fc.t_hit_q / 65536.0, (1.0 - sqrt(0.2)) * 0.5, 1e-4);

        /* Boundary re-hit: starting at x=1000 (s=0), moving inward dx=-100, accelerating outward
         * ddx=+200 */
        const int32_t x_on_wall[3] = {1000, 0, 0};
        const int32_t dx_inward[3] = {-100, 0, 0};
        const int32_t ddx_outward[3] = {200, 0, 0};
        FlowQPolyForecast fc_rehit = flow_qpoly_predict_next_face(
            &A_wall[0][0], b_wall, 1ULL, 3U, x_on_wall, dx_inward, ddx_outward);
        FLOW_ASSERT_TRUE(fc_rehit.will_exit);
        FLOW_ASSERT_EQ(fc_rehit.plane, 0U);
        FLOW_ASSERT_FLOAT_EQ((double)fc_rehit.t_hit_q / 65536.0, 1.0, 1e-4);
    }

    /* =====================================================================
     * Stage 5: Lock-Free SPSC + UMA + QSBR External Constraint Hot-Swap
     * ===================================================================== */
    FLOW_STAGE_BEGIN(5, "Lock-Free SPSC + UMA + QSBR External Constraint Bank Hot-Swap");
    {
        /* Allocate two FlowExtPoly3D banks inside a 16KB UMA shared page */
        FlowUmaBuffer uma;
        FLOW_ASSERT_EQ(flow_uma_buffer_alloc(&uma, FLOW_HW_PAGE_BYTES * 2U), 0);
        FLOW_ASSERT_EQ(uma.is_uma_coherent, 1);

        FlowExtPoly3D *bank_a = (FlowExtPoly3D *)uma.cpu_ptr;
        FlowExtPoly3D *bank_b = bank_a + 1;
        FlowExtPoly3D_init(bank_a);
        FlowExtPoly3D_init(bank_b);
        bank_a->A[0][0] = 1;
        bank_b->A[0][0] = 1;

        /* Stream external constraint updates via Lock-Free SPSC Ring */
        ExtConstraintRing ring;
        ExtConstraintRing_init(&ring);
        ExternalConstraintMsg msg_a = {0U, 500LL, 450LL};
        ExternalConstraintMsg msg_b = {0U, 200LL, 180LL};
        FLOW_ASSERT_EQ(ExtConstraintRing_push(&ring, msg_a), 1);
        FLOW_ASSERT_EQ(ExtConstraintRing_push(&ring, msg_b), 1);
        FLOW_ASSERT_EQ(ExtConstraintRing_size(&ring), 2U);

        ExternalConstraintMsg popped;
        FLOW_ASSERT_EQ(ExtConstraintRing_pop(&ring, &popped), 1);
        FlowExtPoly3D_set_hysteresis(bank_a, popped.plane_idx, popped.b_enter_ext,
                                     popped.b_exit_ext);
        FLOW_ASSERT_EQ(ExtConstraintRing_pop(&ring, &popped), 1);
        FlowExtPoly3D_set_hysteresis(bank_b, popped.plane_idx, popped.b_enter_ext,
                                     popped.b_exit_ext);

        /* Publish bank_a via lock-free QSBR epoch context */
        FlowQsbrEpoch *qsbr = flow_reload_create(NULL);
        FLOW_ASSERT_TRUE(qsbr != NULL);
        FlowReloadReader reader;
        FLOW_ASSERT_EQ(flow_reload_reader_register(qsbr, &reader), FLOW_RELOAD_OK);
        FLOW_ASSERT_EQ(flow_qsbr_publish_ptr(qsbr, bank_a), FLOW_RELOAD_OK);
        FLOW_ASSERT_EQ(flow_reload_resource_sieve(qsbr, &reader), (uint64_t)FLOW_RES_CANONICAL_ALL);

        const int32_t u_obs[3] = {300, 0, 0};
        const FlowExtPoly3D *active = (const FlowExtPoly3D *)flow_qsbr_load_ptr(qsbr, &reader);
        FLOW_ASSERT_TRUE(active == bank_a);
        FLOW_ASSERT_EQ(FlowExtPoly3D_eval(active, u_obs).violated_mask, 0ULL);

        /* Atomically hot-swap to bank_b (where x_0 <= 200) without locks */
        FLOW_ASSERT_EQ(flow_qsbr_publish_ptr(qsbr, bank_b), FLOW_RELOAD_OK);
        active = (const FlowExtPoly3D *)flow_qsbr_load_ptr(qsbr, &reader);
        FLOW_ASSERT_TRUE(active == bank_b);
        FLOW_ASSERT_EQ(FlowExtPoly3D_eval(active, u_obs).violated_mask, 1ULL);
        FLOW_ASSERT_EQ(flow_qsbr_synchronize(qsbr, 1000000ULL), FLOW_RELOAD_OK);

        /* Verify FlowUnit ABI version check and atomic (unit, state) slot lifecycle */
        FlowUnit bad_unit = {.abi_version = 999U, .run = test_unit_run};
        FLOW_ASSERT_EQ(flow_reload_activate(qsbr, &bad_unit), FLOW_RELOAD_INCOMPATIBLE);

        FlowUnit unit1 = {.abi_version = FLOW_RELOAD_ABI_VERSION,
                          .init = test_unit_init_1,
                          .run = test_unit_run,
                          .drop = test_unit_drop};
        FlowUnit unit2 = {.abi_version = FLOW_RELOAD_ABI_VERSION,
                          .init = test_unit_init_2,
                          .run = test_unit_run,
                          .drop = test_unit_drop};
        FLOW_ASSERT_EQ(flow_reload_activate(qsbr, &unit1), FLOW_RELOAD_OK);
        uint64_t state_val = 0ULL;
        FLOW_ASSERT_EQ(flow_reload_call(qsbr, &reader, NULL, &state_val), 0);
        FLOW_ASSERT_EQ(state_val, 0x1111ULL);
        FLOW_ASSERT_EQ(flow_reload_activate(qsbr, &unit2), FLOW_RELOAD_OK);
        FLOW_ASSERT_EQ(atomic_load_explicit(&g_unit_drops, memory_order_relaxed), 1U);
        FLOW_ASSERT_EQ(flow_reload_call(qsbr, &reader, NULL, &state_val), 0);
        FLOW_ASSERT_EQ(state_val, 0x2222ULL);

        /* Multi-threaded QSBR reader/writer hot-swap stress test */
        QsbrStressArgs sa;
        sa.qsbr = qsbr;
        atomic_init(&sa.readers_ready, 0U);
        atomic_init(&sa.stop, 0);
        atomic_init(&sa.reads_completed, 0ULL);
        atomic_init(&sa.errors, 0ULL);
        pthread_t threads[2];
        FLOW_ASSERT_EQ(pthread_create(&threads[0], NULL, qsbr_reader_thread_fn, &sa), 0);
        FLOW_ASSERT_EQ(pthread_create(&threads[1], NULL, qsbr_reader_thread_fn, &sa), 0);
        while (atomic_load_explicit(&sa.readers_ready, memory_order_acquire) < 2U ||
               atomic_load_explicit(&sa.reads_completed, memory_order_acquire) < 2ULL) {
            /* Wait for both reader threads to enter hot loop */
        }
        for (uint32_t swap = 0; swap < 200U; ++swap) {
            const FlowExtPoly3D *target = (swap & 1U) ? bank_a : bank_b;
            FLOW_ASSERT_EQ(flow_qsbr_publish_ptr(qsbr, target), FLOW_RELOAD_OK);
            flow_qsbr_checkpoint(&reader);
            (void)flow_qsbr_synchronize(qsbr, 500000ULL);
        }
        atomic_store_explicit(&sa.stop, 1, memory_order_release);
        FLOW_ASSERT_EQ(pthread_join(threads[0], NULL), 0);
        FLOW_ASSERT_EQ(pthread_join(threads[1], NULL), 0);
        FLOW_ASSERT_EQ(atomic_load_explicit(&sa.errors, memory_order_relaxed), 0ULL);
        FLOW_ASSERT_TRUE(atomic_load_explicit(&sa.reads_completed, memory_order_relaxed) >= 2ULL);

        /* Verify 64-lane Wave64 non-degenerate 2-Jet ballot compaction */
        FlowJet2 wave_jets[64];
        for (uint32_t i = 0; i < 64U; ++i) {
            wave_jets[i] = flow_jet2_make((double)i, 0.0, 0.0);
        }
        wave_jets[7] = flow_jet2_make(1.0, 0.5, 0.0);
        wave_jets[31] = flow_jet2_make(2.0, 0.0, -0.8);
        wave_jets[60] = flow_jet2_make(3.0, 1.2, 0.4);
        uint64_t nd_ballot = flow_jet2_nondegenerate_ballot64(wave_jets, 64U, 1e-4, 1e-4);
        FLOW_ASSERT_EQ(nd_ballot, (1ULL << 7) | (1ULL << 31) | (1ULL << 60));

        uint32_t compact_lanes[64] = {0};
        uint32_t compact_cnt = flow_ballot_compact_wave64(nd_ballot, 0U, compact_lanes);
        FLOW_ASSERT_EQ(compact_cnt, 3U);
        FLOW_ASSERT_EQ(compact_lanes[0], 7U);
        FLOW_ASSERT_EQ(compact_lanes[1], 31U);
        FLOW_ASSERT_EQ(compact_lanes[2], 60U);

        FLOW_ASSERT_EQ(flow_reload_reader_unregister(&reader), FLOW_RELOAD_OK);
        FLOW_ASSERT_EQ(flow_reload_destroy(qsbr), FLOW_RELOAD_OK);
        FLOW_ASSERT_EQ(atomic_load_explicit(&g_unit_drops, memory_order_relaxed), 2U);
        flow_uma_buffer_free(&uma);
    }

    FLOW_TEST_SUITE_END();
}
