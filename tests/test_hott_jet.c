#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "hott_test_kit.h"
#include "bitspace.h"
#include "cubical_hott.h"
#include "flow_jet.h"
#include "flow_bridge.h"
#include "flow_gpu.h"
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

        /* Moreau envelope hysteresis & non-Zeno SMT verification */
        FlowMoreauHysteresis moreau;
        FLOW_ASSERT_EQ(flow_moreau_init(&moreau, -0.5, 0.5, 0), 1);
        FLOW_ASSERT_EQ(flow_moreau_step(&moreau, 0.0), 0);  /* dead-zone -> stays 0 */
        FLOW_ASSERT_EQ(flow_moreau_step(&moreau, 0.8), 1);  /* >= 0.5 -> flips to 1 */
        FLOW_ASSERT_EQ(flow_moreau_step(&moreau, 0.0), 1);  /* dead-zone -> retains state 1 */
        FLOW_ASSERT_EQ(flow_moreau_step(&moreau, -0.8), 0); /* <= -0.5 -> flips to 0 */
        FLOW_ASSERT_EQ(moreau.flutters_suppressed, 2ULL);
        FlowSMTProofAttestation moreau_proof = {0};
        FLOW_ASSERT_EQ(flow_moreau_verify_smt(&moreau, &moreau_proof), FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_SMT_SOUND(moreau_proof);

        /* Phase-space hyperplane attention router */
        FlowPhaseAttentionRouter router;
        FLOW_ASSERT_EQ(flow_phase_attention_init(&router, 4U, 0U), 1);
        const double coeffs[4] = {1.0, 0.0, 0.0, 0.0};
        FLOW_ASSERT_EQ(flow_phase_attention_set_plane(&router, 0U, coeffs, 1.0, true, 0.8, 1.2), 1);
        FlowPhaseAttentionHead h0 = {.name = "nominal_regime",
                                     .key_mask = 1ULL,
                                     .key_polarity = 1ULL,
                                     .state_sieve = FLOW_SIEVE_SECURITY_CANONICAL_MASK,
                                     .kan_boundary_filter = 0ULL,
                                     .priority = 10U,
                                     .kernel = NULL,
                                     .user_unit = NULL};
        uint32_t h0_idx = 99U;
        FLOW_ASSERT_EQ(flow_phase_attention_register_head(&router, &h0, &h0_idx), 1);
        FLOW_ASSERT_EQ(h0_idx, 0U);
        const double q_vec[FLOW_PHASE_ATTENTION_DIM] = {0.5, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
        FLOW_ASSERT_EQ(flow_phase_attention_project_query(&router, q_vec), 1ULL);
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

        /* IEEE-754 Quiet NaN double round-trip & stamping helpers */
        double b_nan = flow_hott_make_boundary_nan(0x0FFFU, 0U, FLOW_BOUNDARY_PRECOND, 2U, 42U);
        FLOW_ASSERT_EQ(flow_hott_is_boundary_nan(b_nan), 1);
        FLOW_ASSERT_EQ(flow_hott_is_boundary_nan(3.14159), 0);
        uint64_t extracted_tag = flow_hott_extract_boundary_tag(b_nan);
        FLOW_ASSERT_TRUE(flow_hott_is_boundary_tag(extracted_tag));
        FLOW_ASSERT_EQ(flowc_boundary_code(b_nan), 42U);
        double stamped_nan = flowc_stamp_boundary_fn_id(b_nan, 99U);
        uint64_t stamped_tag = flow_hott_extract_boundary_tag(stamped_nan);
        FLOW_ASSERT_EQ((uint32_t)((stamped_tag >> 32U) & 0x0FFFULL), 99U);
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

        /* Discrete Schmitt trigger hysteresis debounce */
        FlowSchmittTrigger st;
        int st_changed = 0;
        flow_schmitt_trigger_init(&st, 100.0, 1000ULL);
        /* drop_threshold = 80.0, recovery_threshold = 150.0, dwell = 1000ns */
        FLOW_ASSERT_EQ(flow_schmitt_trigger_update(&st, 90.0, 100ULL, &st_changed), 0);
        FLOW_ASSERT_EQ(st_changed, 0);
        FLOW_ASSERT_EQ(flow_schmitt_trigger_update(&st, 70.0, 200ULL, &st_changed), 1);
        FLOW_ASSERT_EQ(st_changed, 1);
        FLOW_ASSERT_EQ(flow_schmitt_trigger_update(&st, 160.0, 500ULL, &st_changed), 1);
        FLOW_ASSERT_EQ(st_changed, 0); /* dwell start at 500ns */
        FLOW_ASSERT_EQ(flow_schmitt_trigger_update(&st, 160.0, 1600ULL, &st_changed), 0);
        FLOW_ASSERT_EQ(st_changed, 1); /* dwell >= 1000ns -> recovers to 0 */
    }

    /* =====================================================================
     * Stage 5: GPU-Native BMF Meshlet, 1.58-Bit/2-Bit Codec, 2-Jet Cone & Ballot
     * ===================================================================== */
    FLOW_STAGE_BEGIN(5, "GPU-Native BMF Meshlet, 1.58-Bit/2-Bit Codec, 2-Jet Cone & Ballot");
    {
        /* 5a. 1.58-Bit Ternary Bit-Manifold ({-1, 0, +1}) & POPCNT Dot Product */
        int8_t vec_a[64], vec_b[64], decoded_a[64];
        int32_t expected_dot = 0;
        for (uint32_t i = 0; i < 64U; ++i) {
            vec_a[i] = (i % 3U == 0U) ? 1 : ((i % 3U == 1U) ? -1 : 0);
            vec_b[i] = (i % 2U == 0U) ? 1 : -1;
            expected_dot += (int32_t)vec_a[i] * (int32_t)vec_b[i];
        }
        FlowBmfTernary64 ta = flow_bmf_ternary64_encode(vec_a);
        FlowBmfTernary64 tb = flow_bmf_ternary64_encode(vec_b);
        FLOW_ASSERT_TRUE(flow_bmf_ternary64_is_canonical(ta));
        FLOW_ASSERT_TRUE(flow_bmf_ternary64_is_canonical(tb));
        flow_bmf_ternary64_decode_all(ta, decoded_a);
        for (uint32_t i = 0; i < 64U; ++i) {
            FLOW_ASSERT_EQ(decoded_a[i], vec_a[i]);
        }
        FLOW_ASSERT_EQ(flow_bmf_ternary64_dot(ta, tb), expected_dot);

        /* 5b. 2-Bit Block Interpolation (BC1 / DXT1 Isomorphic LUT) */
        int16_t tile_in[16] = {300, 260, 220, 180, 300, 260, 220, 180,
                               300, 260, 220, 180, 300, 260, 220, 180};
        int16_t tile_out[16] = {0};
        FlowBmfBlock2Bit blk = flow_bmf_block2bit_encode(tile_in);
        FLOW_ASSERT_EQ(blk.c0, 300);
        FLOW_ASSERT_EQ(blk.c1, 180);
        flow_bmf_block2bit_decode_all(blk, tile_out);
        for (uint32_t i = 0; i < 16U; ++i) {
            FLOW_ASSERT_EQ(tile_out[i], tile_in[i]);
        }

        /* 5c. Pointerless Flat Index-Based Scene Hierarchy (0-Pointer Chasing) */
        FlowBmfFlatNode flat_nodes[3];
        memset(flat_nodes, 0, sizeof(flat_nodes));
        flat_nodes[0].parent_idx = FLOW_BMF_INVALID_HANDLE;
        flat_nodes[0].local_pos[0] = 10.0f;
        flat_nodes[0].local_pos[1] = 20.0f;
        flat_nodes[0].local_pos[2] = 30.0f;
        flat_nodes[0].local_scale = 2.0f;

        flat_nodes[1].parent_idx = 0U; /* child of root 0 */
        flat_nodes[1].local_pos[0] = 1.5f;
        flat_nodes[1].local_pos[1] = -2.0f;
        flat_nodes[1].local_pos[2] = 0.5f;
        flat_nodes[1].local_scale = 0.5f;

        flat_nodes[2].parent_idx = 1U; /* child of node 1 */
        flat_nodes[2].local_pos[0] = 4.0f;
        flat_nodes[2].local_pos[1] = 0.0f;
        flat_nodes[2].local_pos[2] = 0.0f;
        flat_nodes[2].local_scale = 1.0f;

        FLOW_ASSERT_EQ(flow_gpu_propagate_flat_nodes(flat_nodes, 3U), 0);
        FLOW_ASSERT_FLOAT_EQ(flat_nodes[1].world_pos[0], 13.0, 1e-6); /* 10 + 2 * 1.5 */
        FLOW_ASSERT_FLOAT_EQ(flat_nodes[1].world_scale, 1.0, 1e-6);   /* 2 * 0.5 */
        FLOW_ASSERT_FLOAT_EQ(flat_nodes[2].world_pos[0], 17.0, 1e-6); /* 13 + 1.0 * 4 */
        FLOW_ASSERT_TRUE((flat_nodes[2].node_sieve_mask & FLOW_GPU_SIEVE_FLAT_DAG_VALID) != 0ULL);

        /* Reject cyclic / forward parent reference */
        flat_nodes[0].parent_idx = 2U;
        FLOW_ASSERT_EQ(flow_gpu_propagate_flat_nodes(flat_nodes, 3U), -1);

        /* 5d. GPU-Native BMF Meshlets (10-bit XYZ + 6-bit Micro-Indices) & Watertight Seam */
        const float left_verts[4][3] = {
            {0.0f, 0.0f, 0.0f},
            {1.0f, 0.0f, 0.0f}, /* Shared seam vertex 0 */
            {1.0f, 1.0f, 0.0f}, /* Shared seam vertex 1 */
            {0.0f, 1.0f, 0.0f},
        };
        const uint8_t left_flags[4] = {FLOW_BMF_VERT_INTERIOR, FLOW_BMF_VERT_SEAM_LOCKED,
                                       FLOW_BMF_VERT_SEAM_LOCKED, FLOW_BMF_VERT_INTERIOR};
        const uint8_t left_tris[2][3] = {{0, 1, 2}, {0, 2, 3}};

        const float right_verts[4][3] = {
            {1.0f, 0.0f, 0.0f}, /* Shared seam vertex 0 */
            {2.0f, 0.0f, 0.0f},
            {2.0f, 1.0f, 0.0f},
            {1.0f, 1.0f, 0.0f}, /* Shared seam vertex 1 */
        };
        const uint8_t right_flags[4] = {FLOW_BMF_VERT_SEAM_LOCKED, FLOW_BMF_VERT_INTERIOR,
                                        FLOW_BMF_VERT_INTERIOR, FLOW_BMF_VERT_SEAM_LOCKED};
        const uint8_t right_tris[2][3] = {{0, 1, 2}, {0, 2, 3}};

        FlowBmfMeshlet meshlet_l, meshlet_r;
        FLOW_ASSERT_EQ(flow_gpu_meshlet_encode(&meshlet_l, left_verts, left_flags, 4U, left_tris,
                                               2U, 0.001f, 0U),
                       0);
        FLOW_ASSERT_EQ(flow_gpu_meshlet_encode(&meshlet_r, right_verts, right_flags, 4U, right_tris,
                                               2U, 0.001f, 0U),
                       0);

        FlowSMTProofAttestation mesh_proof = {0};
        FLOW_ASSERT_EQ(flow_gpu_meshlet_verify_smt(&meshlet_l, &mesh_proof), FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_EQ(flow_gpu_meshlet_verify_smt(&meshlet_r, &mesh_proof), FLOW_SMT_PROVEN_UNSAT);

        /* Prove Watertight Topological Fidelity (Zero T-Junctions / Cracks across seam) */
        size_t shared_seam_verts = 0;
        FLOW_ASSERT_EQ(flow_gpu_verify_watertight_seam(&meshlet_l, &meshlet_r, &shared_seam_verts),
                       0);
        FLOW_ASSERT_EQ(shared_seam_verts, 2U);

        /* Verify 54-bit 3-triangle packing in a single 64-bit register word */
        uint32_t t0 = flow_bmf_pack_tri_6bit(0, 1, 2);
        uint32_t t1 = flow_bmf_pack_tri_6bit(2, 3, 0);
        uint32_t t2 = flow_bmf_pack_tri_6bit(1, 4, 5);
        uint64_t w54 = flow_bmf_pack_3tris_54bit(t0, t1, t2);
        uint32_t u0 = 0, u1 = 0, u2 = 0;
        flow_bmf_unpack_3tris_54bit(w54, &u0, &u1, &u2);
        FLOW_ASSERT_EQ(u0, t0);
        FLOW_ASSERT_EQ(u1, t1);
        FLOW_ASSERT_EQ(u2, t2);

        /* 5e. 2-Jet Contact Geometry Cone Derivation + 64-Lane Subgroup Ballot Compaction */
        FlowJetScalar2 patch_u[3] = {flow_jet2_make(0.5, 1.0, 0.05), flow_jet2_make(0.5, 0.0, 0.0),
                                     flow_jet2_make(0.0, 0.0, -0.1)};
        FlowJetScalar2 patch_v[3] = {flow_jet2_make(0.5, 0.0, 0.0), flow_jet2_make(0.5, 1.0, 0.05),
                                     flow_jet2_make(0.0, 0.0, -0.1)};
        FLOW_ASSERT_EQ(flow_gpu_meshlet_derive_jet2_cone(&meshlet_l, patch_u, patch_v, 0.1, 0.1),
                       0);
        FLOW_ASSERT_FLOAT_EQ(meshlet_l.cone_axis[2], 1.0, 1e-6);
        FLOW_ASSERT_TRUE(meshlet_l.cone_cutoff < 0.0f);

        /* Construct a 4-meshlet wave:
         *   lane 0: front-facing, inside frustum -> VISIBLE
         *   lane 1: back-facing (cone axis points away from camera along view ray) -> CULLED
         *   lane 2: front-facing, outside frustum -> CULLED
         *   lane 3: front-facing, inside frustum -> VISIBLE
         */
        FlowBmfMeshlet wave[4];
        wave[0] = meshlet_l;
        wave[1] = meshlet_l;
        wave[1].cone_axis[0] = 0.0f;
        wave[1].cone_axis[1] = 0.0f;
        wave[1].cone_axis[2] = -1.0f; /* points away from camera at +Z -> back-facing */
        wave[1].cone_cutoff = -0.25f;

        wave[2] = meshlet_r;
        wave[2].aabb_min[0] = 500.0f;
        wave[2].aabb_max[0] = 501.0f; /* outside frustum [-10, 10] */

        wave[3] = meshlet_r;

        FlowGpuCullCamera cam = {.view_pos = {0.5f, 0.5f, 5.0f},
                                 .view_dir = {0.0f, 0.0f, -1.0f},
                                 .frustum_min = {-10.0f, -10.0f, -10.0f},
                                 .frustum_max = {10.0f, 10.0f, 10.0f},
                                 .required_sieve_mask = 0ULL};

        uint64_t lane_sieves[FLOW_BMF_WAVE_SIZE] = {0};
        uint64_t ballot = flow_gpu_ballot_cull_wave64(wave, 4U, &cam, lane_sieves);
        /* Only lanes 0 and 3 are visible -> ballot == (1 << 0) | (1 << 3) == 0x9 */
        FLOW_ASSERT_EQ(ballot, 0x9ULL);
        FLOW_ASSERT_EQ(flow_gpu_ballot_visible_count(ballot), 2U);
        FLOW_ASSERT_EQ(flow_gpu_ballot_lane_offset(ballot, 0U), 0U);
        FLOW_ASSERT_EQ(flow_gpu_ballot_lane_offset(ballot, 3U), 1U);

        uint32_t visible_handles[FLOW_BMF_WAVE_SIZE] = {0};
        uint32_t written = flow_gpu_ballot_compact_wave64(ballot, 100U, visible_handles);
        FLOW_ASSERT_EQ(written, 2U);
        FLOW_ASSERT_EQ(visible_handles[0], 100U); /* base 100 + lane 0 */
        FLOW_ASSERT_EQ(visible_handles[1], 103U); /* base 100 + lane 3 */
    }

    FLOW_TEST_SUITE_END();
}
