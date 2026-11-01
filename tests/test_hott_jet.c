#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "hott_test_kit.h"
#include "bitspace.h"
#include "cubical_hott.h"
#include "flow_jet.h"
#include "flow_bridge.h"
#include "flow_spsc.h"
#include "reload.h"
#include <pthread.h>

FLOW_SPSC_DEFINE(TestSpscRing, uint64_t, 16)

/* Helper operators for Stage 3 (HoTT x 2-Jet Bridge) */
typedef struct {
    double pos;
    double vel;
    double acc;
} TestSensorObs;

typedef struct {
    int64_t q_mm;
    int64_t v_mms;
} TestCertInput;

typedef struct {
    int64_t cmd_force_mn;
} TestCertOutput;

static int test_quantize(const void *raw_obs, void *cert_in) {
    const TestSensorObs *obs = (const TestSensorObs *)raw_obs;
    TestCertInput *in = (TestCertInput *)cert_in;
    if (!obs || !in || !isfinite(obs->pos) || !isfinite(obs->vel))
        return -1;
    in->q_mm = (int64_t)llround(obs->pos * 1000.0);
    in->v_mms = (int64_t)llround(obs->vel * 1000.0);
    return 0;
}

static int test_pre_guard(const void *cert_in, uint64_t *violation_tag_out) {
    const TestCertInput *in = (const TestCertInput *)cert_in;
    if (!in || in->q_mm < -5000 || in->q_mm > 5000) {
        if (violation_tag_out)
            *violation_tag_out =
                flow_hott_make_boundary_tag(1U, 0U, FLOW_BOUNDARY_PRECOND, 0U, 101U);
        return -1;
    }
    return 0;
}

static int test_primary_op(const void *cert_in, void *cert_out) {
    const TestCertInput *in = (const TestCertInput *)cert_in;
    TestCertOutput *out = (TestCertOutput *)cert_out;
    out->cmd_force_mn = -(in->q_mm * 2 + in->v_mms);
    return 0;
}

static int test_bad_post_op(const void *cert_in, void *cert_out) {
    (void)cert_in;
    TestCertOutput *out = (TestCertOutput *)cert_out;
    out->cmd_force_mn = 999999; /* violates postcondition */
    return 0;
}

static int test_post_guard(const void *cert_in, const void *cert_out, uint64_t *violation_tag_out) {
    (void)cert_in;
    const TestCertOutput *out = (const TestCertOutput *)cert_out;
    if (!out || out->cmd_force_mn < -20000 || out->cmd_force_mn > 20000) {
        if (violation_tag_out)
            *violation_tag_out =
                flow_hott_make_boundary_tag(1U, 1U, FLOW_BOUNDARY_POSTCOND, 0U, 202U);
        return -1;
    }
    return 0;
}

static int test_fallback_op(const void *cert_in, void *cert_out) {
    (void)cert_in;
    TestCertOutput *out = (TestCertOutput *)cert_out;
    if (out)
        out->cmd_force_mn = 0; /* safe zero-energy damping */
    return 0;
}

static int test_dequantize(const void *cert_out, void *act_cmd) {
    const TestCertOutput *out = (const TestCertOutput *)cert_out;
    double *cmd = (double *)act_cmd;
    if (!out || !cmd)
        return -1;
    *cmd = (double)out->cmd_force_mn / 1000.0;
    return 0;
}

/* Helper units for Stage 4 (QSBR Hot-Swap) */
static int unit_v1_init(void *host, void **state_out) {
    (void)host;
    int64_t *s = (int64_t *)calloc(1, sizeof(int64_t));
    if (!s)
        return -1;
    *s = 100;
    *state_out = s;
    return 0;
}

static int unit_v1_run(void *host, void *state, const void *in, void *out) {
    (void)host;
    int64_t base = state ? *(int64_t *)state : 0;
    int64_t x = in ? *(const int64_t *)in : 0;
    *(int64_t *)out = base + x;
    return 0;
}

static int unit_v2_migrate(void *host, const void *old_state, void *new_state) {
    (void)host;
    if (old_state && new_state)
        *(int64_t *)new_state = (*(const int64_t *)old_state) * 10;
    return 0;
}

static int unit_v2_run(void *host, void *state, const void *in, void *out) {
    (void)host;
    int64_t base = state ? *(int64_t *)state : 0;
    int64_t x = in ? *(const int64_t *)in : 0;
    *(int64_t *)out = base + x * 2;
    return 0;
}

static void unit_drop(void *host, void *state) {
    (void)host;
    free(state);
}

int main(void) {
    FLOW_TEST_SUITE_BEGIN("HoTT x 2-Jet x Univalence Mathematical Core (In-Memory)");

    /* =====================================================================
     * Stage 1: 2-Jet Differential Calculus & Symplectic Phase Space
     * ===================================================================== */
    FLOW_STAGE_BEGIN(1, "2-Jet Forward AD Algebra & Symplectic Phase Space");
    {
        /* f(x) = x^3 + 2x at x = 3: f(3)=33, f'(3)=29, f''(3)=18 */
        FlowJetScalar2 x = flow_jet2_var(3.0);
        FlowJetScalar2 c2 = flow_jet2_const(2.0);
        FlowJetScalar2 x2 = flow_jet2_mul(x, x);
        FlowJetScalar2 x3 = flow_jet2_mul(x2, x);
        FlowJetScalar2 fx = flow_jet2_add(x3, flow_jet2_mul(c2, x));
        FLOW_ASSERT_FLOAT_EQ(fx.v, 33.0, 1e-12);
        FLOW_ASSERT_FLOAT_EQ(fx.d1, 29.0, 1e-12);
        FLOW_ASSERT_FLOAT_EQ(fx.d2, 18.0, 1e-12);

        /* Chain rule composition: h(t) = exp(sin(t)) at t = 0.5 */
        double t0 = 0.5;
        FlowJetScalar2 t = flow_jet2_var(t0);
        FlowJetScalar2 h = flow_jet2_exp(flow_jet2_sin(t));
        double expected_v = exp(sin(t0));
        double expected_d1 = cos(t0) * expected_v;
        double expected_d2 = (cos(t0) * cos(t0) - sin(t0)) * expected_v;
        FLOW_ASSERT_FLOAT_EQ(h.v, expected_v, 1e-12);
        FLOW_ASSERT_FLOAT_EQ(h.d1, expected_d1, 1e-12);
        FLOW_ASSERT_FLOAT_EQ(h.d2, expected_d2, 1e-12);

        /* Transcendental identity: log(exp(x)) == x in 2-Jet space */
        FlowJetScalar2 id_jet = flow_jet2_log(flow_jet2_exp(x));
        FLOW_ASSERT_FLOAT_EQ(id_jet.v, 3.0, 1e-12);
        FLOW_ASSERT_FLOAT_EQ(id_jet.d1, 1.0, 1e-12);
        FLOW_ASSERT_FLOAT_EQ(id_jet.d2, 0.0, 1e-12);

        /* Quotient & sqrt: g(x) = sqrt(x) / x = x^(-1/2) at x = 4.0 */
        FlowJetScalar2 x4 = flow_jet2_var(4.0);
        FlowJetScalar2 gx = flow_jet2_div(flow_jet2_sqrt(x4), x4);
        FLOW_ASSERT_FLOAT_EQ(gx.v, 0.5, 1e-12);
        FLOW_ASSERT_FLOAT_EQ(gx.d1, -0.0625, 1e-12);   /* -0.5 * 4^(-1.5) = -1/16 */
        FLOW_ASSERT_FLOAT_EQ(gx.d2, 0.0234375, 1e-12); /* 0.75 * 4^(-2.5) = 3/128 */

        /* Symplectic phase-space integration & energy boundedness */
        FlowJet jet;
        FLOW_ASSERT_EQ(flow_jet_init(&jet, "jet_harmonic", "2-Jet Symplectic Oscillator"), 1);
        jet.payload.q[0] = 1.0;
        jet.payload.p[0] = 0.0;
        double e0 = flow_jet_hamiltonian(&jet);
        FLOW_ASSERT_FLOAT_EQ(e0, 0.5, 1e-12);
        for (int step = 0; step < 100; ++step) {
            FLOW_ASSERT_EQ(flow_jet_symplectic_step(&jet, 0.01), 1);
        }
        double e1 = flow_jet_hamiltonian(&jet);
        FLOW_ASSERT_FLOAT_EQ(e0, e1, 1e-4);

        FlowSMTProofAttestation jet_proof = {0};
        FLOW_ASSERT_EQ(flow_jet_verify_symplectic_soundness_smt(&jet, &jet_proof),
                       FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_SMT_SOUND(jet_proof);
    }

    /* =====================================================================
     * Stage 2: Cubical HoTT, Topos Sieve, Univalence U_0 & HIT Pullback
     * ===================================================================== */
    FLOW_STAGE_BEGIN(2, "Cubical HoTT, Topos Subobject Classifier & Univalence U_0");
    {
        /* De Morgan interval algebra */
        FLOW_ASSERT_EQ(flow_demorgan_not(FLOW_DEMORGAN_ZERO), FLOW_DEMORGAN_ONE);
        FLOW_ASSERT_EQ(flow_demorgan_not(FLOW_DEMORGAN_ONE), FLOW_DEMORGAN_ZERO);
        uint64_t a = 0xAA55AA5500FF00FFULL, b = 0x0F0F0F0FF0F0F0F0ULL;
        FLOW_ASSERT_EQ(flow_demorgan_word_not(flow_demorgan_word_and(a, b)),
                       flow_demorgan_word_or(flow_demorgan_word_not(a), flow_demorgan_word_not(b)));

        /* Cubical face & degeneracy operators */
        uint64_t s0 = flow_cubical_face_proj(a, 5, 0);
        uint64_t s1 = flow_cubical_face_proj(a, 5, 1);
        FLOW_ASSERT_EQ((s0 >> 5) & 1ULL, 0ULL);
        FLOW_ASSERT_EQ((s1 >> 5) & 1ULL, 1ULL);

        /* Kan Open-Box homotopy & Topos subobject classifier */
        uint64_t mismatch = 0ULL;
        FLOW_ASSERT_EQ(flow_kan_check_homotopy(0xFF0FULL, 0xFF00ULL, 0xFF00ULL, &mismatch),
                       FLOW_KAN_FILLED_HOMOTOPIC);
        FLOW_ASSERT_EQ(mismatch, 0ULL);
        FLOW_ASSERT_EQ(flow_kan_check_homotopy(0xFF0FULL, 0xFE0FULL, 0xFF00ULL, &mismatch),
                       FLOW_KAN_OBSTRUCTED_SINGULARITY);
        FLOW_ASSERT_EQ(mismatch, 0x0100ULL);
        FLOW_ASSERT_EQ(flow_topos_classify_subobject(0x00F0ULL, 0x0FF0ULL), 1);
        FLOW_ASSERT_EQ(flow_topos_classify_subobject(0x10F0ULL, 0x0FF0ULL), 0);

        /* Univalence Universe U_0 & AoS ~= SOA64 isomorphism */
        FlowHoTTUniverse u0;
        flow_hott_universe_reset(&u0);
        int id_lean = flow_hott_register_type(&u0, "PhaseVec", FLOW_HOTT_LANG_LEAN, 3, 24, 8, 0,
                                              FLOW_SIEVE_SECURITY_CANONICAL_MASK);
        int id_c17 = flow_hott_register_type(&u0, "PhaseVec", FLOW_HOTT_LANG_C17, 3, 24, 8, 0,
                                             FLOW_SIEVE_SECURITY_CANONICAL_MASK);
        FLOW_ASSERT_TRUE(id_lean >= 0 && id_c17 >= 0);
        uint64_t ua_hash = 0ULL;
        FLOW_ASSERT_EQ(flow_hott_verify_univalence(&u0, "PhaseVec", &ua_hash),
                       FLOW_KAN_FILLED_HOMOTOPIC);
        FLOW_ASSERT_NE(ua_hash, 0ULL);

        int id_soa = flow_hott_register_soa_equivalence(&u0, "PhaseVec", "PhaseVec_SOA64", 3, 24,
                                                        FLOW_SIMD_SOA_LANES, 0,
                                                        FLOW_SIEVE_SECURITY_CANONICAL_MASK);
        FLOW_ASSERT_TRUE(id_soa >= 0);
        FLOW_ASSERT_EQ(flow_hott_verify_soa_univalence(&u0, "PhaseVec", "PhaseVec_SOA64",
                                                       FLOW_SIMD_SOA_LANES, &ua_hash),
                       FLOW_KAN_FILLED_HOMOTOPIC);

        /* HIT SpaceWithException quotient & IEEE-754 Quiet NaN boundary pullback */
        FlowHITExceptionQuotient hit_q;
        FLOW_ASSERT_EQ(flow_hott_quotient_exception_zone(0xFFFFULL, 0x00F0ULL, 7U, &hit_q), 1);
        FLOW_ASSERT_EQ(hit_q.collapsed_boundary_points, 1U);
        FLOW_ASSERT_TRUE(flow_hott_is_boundary_tag(hit_q.canonical_boundary_tag));

        FlowBoundaryCertificateReport b_rep;
        FLOW_ASSERT_EQ(
            flow_hott_decode_boundary_exception(hit_q.canonical_boundary_tag, NULL, &b_rep), 1);
        FLOW_ASSERT_EQ(b_rep.is_on_boundary, 1);
        FLOW_ASSERT_EQ(b_rep.fn_id, 7U);
    }

    /* =====================================================================
     * Stage 3: HoTT x 2-Jet Grounding Bridge (flow_bridge.c)
     * ===================================================================== */
    FLOW_STAGE_BEGIN(3, "HoTT x 2-Jet Manifold Sieve Projection & 1-Cycle Homotopy Fallback");
    {
        FlowJetManifoldEnvelope jet_env = {.v_min = -2.0,
                                           .v_max = 2.0,
                                           .max_lipschitz_d1 = 5.0,
                                           .max_curvature_d2 = 20.0,
                                           .lyapunov_decay_rate = 0.5,
                                           .transversality_floor = 1e-4,
                                           .required_kan_sieve = FLOW_JET_SIEVE_CANONICAL_ALL,
                                           .kan_boundary_filter = FLOW_JET_SIEVE_CANONICAL_ALL};

        /* 1. Interior dissipative 2-Jet state: v = 1.0, d1 = -2.0, d2 = 4.0
         *    L_f V = v * d1 = -2.0 <= -0.5 * 0.5 * 1.0^2 = -0.25 -> FILLED_HOMOTOPIC */
        FlowJetScalar2 j_ok = {.v = 1.0, .d1 = -2.0, .d2 = 4.0};
        FlowKanStatus kan_status = FLOW_KAN_OBSTRUCTED_SINGULARITY;
        uint64_t kan_mismatch = ~0ULL;
        uint64_t sieve_ok =
            flow_bridge_project_jet2_sieve(j_ok, &jet_env, &kan_status, &kan_mismatch);
        FLOW_ASSERT_EQ(kan_status, FLOW_KAN_FILLED_HOMOTOPIC);
        FLOW_ASSERT_EQ(kan_mismatch, 0ULL);
        FLOW_ASSERT_EQ(sieve_ok, FLOW_JET_SIEVE_CANONICAL_ALL);

        /* 2. 2nd-order Hessian curvature fracture: |d2| = 50.0 > 20.0 -> OBSTRUCTED_SINGULARITY */
        FlowJetScalar2 j_curv_bad = {.v = 1.0, .d1 = -2.0, .d2 = 50.0};
        uint64_t sieve_bad =
            flow_bridge_project_jet2_sieve(j_curv_bad, &jet_env, &kan_status, &kan_mismatch);
        FLOW_ASSERT_EQ(kan_status, FLOW_KAN_OBSTRUCTED_SINGULARITY);
        FLOW_ASSERT_EQ(sieve_bad & FLOW_JET_SIEVE_SECOND_ORDER_CURVATURE, 0ULL);
        FLOW_ASSERT_EQ(kan_mismatch, FLOW_JET_SIEVE_SECOND_ORDER_CURVATURE);

        /* 3. Full HoTT x 2-Jet execution loop with automatic 1-cycle Kan fallback */
        FlowPhysicalBridge bridge = {
            .transducer = {.transducer_name = "test_transducer",
                           .raw_obs_bytes = sizeof(TestSensorObs),
                           .discrete_in_bytes = sizeof(TestCertInput),
                           .discrete_out_bytes = sizeof(TestCertOutput),
                           .raw_act_bytes = sizeof(double),
                           .quantize_observation = test_quantize,
                           .dequantize_actuation = test_dequantize},
            .contract = {.contract_name = "test_contract",
                         .schema_hash = 0x1234ULL,
                         .check_precondition = test_pre_guard,
                         .check_postcondition = test_post_guard,
                         .check_relational_invariant = NULL},
            .envelope = {.max_latency_cycles = 0ULL,
                         .ambient_sieve_mask = FLOW_JET_SIEVE_CANONICAL_ALL,
                         .fallback_operator = test_fallback_op},
            .engineering_operator = test_primary_op,
            .jet_envelope = jet_env,
            .univalence_ctx = NULL};

        TestSensorObs obs = {.pos = 1.0, .vel = -1.5, .acc = 4.0};
        double act_cmd = -999.0;
        uint64_t btag = 0ULL;

        /* 3a. Nominal interior execution: -(1000*2 - 1500)/1000 = -0.5 */
        int rc = flow_bridge_execute_hott_jet_step(&bridge, &j_ok, 1, &obs, &act_cmd,
                                                   FLOW_JET_SIEVE_CANONICAL_ALL, &btag);
        FLOW_ASSERT_EQ(rc, FLOW_BRIDGE_SUCCESS);
        FLOW_ASSERT_EQ(btag, 0ULL);
        FLOW_ASSERT_FLOAT_EQ(act_cmd, -0.5, 1e-9);

        /* 3b. 2-Jet curvature fracture triggers 1-cycle Kan fallback operator (0.0) */
        rc = flow_bridge_execute_hott_jet_step(&bridge, &j_curv_bad, 1, &obs, &act_cmd,
                                               FLOW_JET_SIEVE_CANONICAL_ALL, &btag);
        FLOW_ASSERT_EQ(rc, FLOW_BRIDGE_ERR_JET_SINGULAR);
        FLOW_ASSERT_TRUE(flow_hott_is_boundary_tag(btag));
        FLOW_ASSERT_FLOAT_EQ(act_cmd, 0.0, 1e-9);

        /* 3c. Precondition breach triggers fallback */
        TestSensorObs obs_oob = {.pos = 9.5, .vel = -1.0, .acc = 0.0};
        rc = flow_bridge_execute_hott_jet_step(&bridge, &j_ok, 1, &obs_oob, &act_cmd,
                                               FLOW_JET_SIEVE_CANONICAL_ALL, &btag);
        FLOW_ASSERT_EQ(rc, FLOW_BRIDGE_ERR_PRECONDITION);
        FLOW_ASSERT_TRUE(flow_hott_is_boundary_tag(btag));
        FLOW_ASSERT_FLOAT_EQ(act_cmd, 0.0, 1e-9);

        /* 3d. Postcondition breach triggers fallback */
        bridge.engineering_operator = test_bad_post_op;
        rc = flow_bridge_execute_hott_jet_step(&bridge, &j_ok, 1, &obs, &act_cmd,
                                               FLOW_JET_SIEVE_CANONICAL_ALL, &btag);
        FLOW_ASSERT_EQ(rc, FLOW_BRIDGE_ERR_POSTCONDITION);
        FLOW_ASSERT_TRUE(flow_hott_is_boundary_tag(btag));
        FLOW_ASSERT_FLOAT_EQ(act_cmd, 0.0, 1e-9);
    }

    /* =====================================================================
     * Stage 4: Univalence Dual-Mapped mmap & Lock-Free QSBR Hot-Swap
     * ===================================================================== */
    FLOW_STAGE_BEGIN(4, "Univalence Dual-Mapped Virtual Memory & Lock-Free QSBR");
    {
        /* Dual-mapped POSIX virtual memory pool */
        FlowJITConfig cfg = {.enable_lto = 0, .opt_level = 2, .initial_code_heap_bytes = 65536};
        FlowJITEngine *jit = flow_jit_create(&cfg);
        FLOW_ASSERT_TRUE(jit != NULL);
        FlowJITPoolStats stats = {0};
        FLOW_ASSERT_EQ(flow_jit_get_pool_stats(jit, &stats), 1);
        FLOW_ASSERT_TRUE(stats.pool_size >= 65536);
        flow_jit_destroy(jit);

        /* Lock-free SPSC ring buffer */
        TestSpscRing ring;
        TestSpscRing_init(&ring);
        for (uint64_t v = 1; v <= 10; ++v) {
            FLOW_ASSERT_EQ(TestSpscRing_push(&ring, v), 1);
        }
        FLOW_ASSERT_EQ(TestSpscRing_size(&ring), 10U);
        for (uint64_t v = 1; v <= 10; ++v) {
            uint64_t out_v = 0;
            FLOW_ASSERT_EQ(TestSpscRing_pop(&ring, &out_v), 1);
            FLOW_ASSERT_EQ(out_v, v);
        }

        /* Lock-free QSBR state migration & hot-swap */
        FlowReloadContext *ctx = flow_reload_create(NULL);
        FLOW_ASSERT_TRUE(ctx != NULL);
        FlowReloadReader reader;
        memset(&reader, 0, sizeof(reader));
        FLOW_ASSERT_EQ(flow_reload_reader_register(ctx, &reader), FLOW_RELOAD_OK);

        FlowUnit u1 = {.abi_version = FLOW_RELOAD_ABI_VERSION,
                       .semantic_schema_hash = 0x1111ULL,
                       .constraint_hash = 0x2222ULL,
                       .capability_hash = 0x3333ULL,
                       .name = "unit_v1",
                       .layout = FLOW_LAYOUT_AOS,
                       .init = unit_v1_init,
                       .run = unit_v1_run,
                       .drop = unit_drop};
        FlowUnit u2 = u1;
        u2.name = "unit_v2";
        u2.run = unit_v2_run;
        u2.migrate = unit_v2_migrate;

        FLOW_ASSERT_EQ(flow_reload_activate(ctx, &u1), FLOW_RELOAD_OK);
        int64_t in_val = 5, out_val = 0;
        FLOW_ASSERT_EQ(flow_reload_call(ctx, &reader, &in_val, &out_val), FLOW_RELOAD_OK);
        FLOW_ASSERT_EQ(out_val, 105); /* 100 + 5 */

        FLOW_ASSERT_EQ(flow_reload_activate(ctx, &u2), FLOW_RELOAD_OK);
        flow_qsbr_checkpoint(&reader);
        (void)flow_reload_reclaim(ctx);
        FLOW_ASSERT_EQ(flow_reload_call(ctx, &reader, &in_val, &out_val), FLOW_RELOAD_OK);
        FLOW_ASSERT_EQ(out_val, 1010); /* migrated 100*10 + 5*2 = 1010 */

        FLOW_ASSERT_EQ(flow_reload_reader_unregister(&reader), FLOW_RELOAD_OK);
        FLOW_ASSERT_EQ(flow_reload_destroy(ctx), FLOW_RELOAD_OK);
    }

    FLOW_TEST_SUITE_END();
}
