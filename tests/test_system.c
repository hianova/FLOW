#include "hott_test_kit.h"
#include "flow.h"
#include "registry.h"
#include "flowy.h"
#include "abi.h"
#include "backend.h"
#include "reload.h"
#include "flow_jet.h"
#include "flow_spsc.h"
#include "cubical_hott.h"
#include "induce.h"

#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct { double hipPitch, hipRoll, kneePitch, anklePitch, ankleRoll, torsoYaw, comX, comY; } TestEmbodiedCoords;
FLOW_JET_DEFINE_NAMED_VIEW(TestEmbodiedJetView, TestEmbodiedCoords)

static int unit_zero_init(void *host, void **state_out) {
    (void)host;
    uint64_t *st = (uint64_t *)calloc(1, sizeof(uint64_t));
    if (!st) return -1;
    *st = 100;
    *state_out = st;
    return 0;
}

static int unit_zero_run(void *host, void *state, const void *input, void *output) {
    (void)host;
    uint64_t *st = (uint64_t *)state;
    const uint64_t *in_val = (const uint64_t *)input;
    uint64_t *out_val = (uint64_t *)output;
    if (!st || !in_val || !out_val) return -1;
    *st += *in_val;
    *out_val = *st;
    return 0;
}

static void unit_zero_drop(void *host, void *state) {
    (void)host;
    free(state);
}

int main(void) {
    FLOW_TEST_SUITE_BEGIN("System: Pure Lean 4 -> C17 Compiler, Explicit C17 Runtime & Theorem Contract Synthesis");

    FLOW_ASSERT_TRUE(flow_registry_init());

    /* ========================================================================= */
    /* STAGE 1: Pure Lean 4 Spec Parsing, Zero Config Injection & IR Lowering    */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(1, "Pure Lean 4 Spec Parsing, Zero Config Injection & IR Lowering");
    {
        const char *lean_src =
            "namespace Flow.SystemPipeline\n"
            "structure TaskFrame where\n"
            "  taskId : UInt64\n"
            "  w      : UInt64\n"
            "  h_w    : w > 0\n"
            "def slaLatencyNs : Nat := 25000\n"
            "def memoryQuotaBytes : Nat := 16777216\n"
            "def inputMaxCount : Nat := 2048\n"
            "private def internal_scale (w : UInt64) : UInt64 := w * 2\n"
            "def process_weight (w : UInt64) : UInt64 := internal_scale w + 1\n"
            "theorem process_weight_positive (w : UInt64) (h : w > 0) : process_weight w > w := by omega\n"
            "theorem process_weight_bounded (w : UInt64) : w < 1000 → process_weight w < 2001 := by omega\n"
            "end Flow.SystemPipeline\n";

        FILE *mem = tmpfile();
        FLOW_ASSERT_TRUE(mem != NULL);
        fputs(lean_src, mem);
        rewind(mem);

        static FlowSpec spec;
        memset(&spec, 0, sizeof(spec));
        FLOW_ASSERT_TRUE(parse_spec(mem, &spec));
        fclose(mem);

        /* Verify Zero Config Injection: def slaLatencyNs / memoryQuotaBytes / inputMaxCount
         * are parsed as ordinary Lean functions, NOT injected into FlowSpec hardware config */
        FLOW_ASSERT_STR_EQ(spec.flow_name, "SystemPipeline");
        FLOW_ASSERT_EQ(spec.lean_certified, 1);
        FLOW_ASSERT_EQ(spec.sla_latency_ns, 0ULL);
        FLOW_ASSERT_EQ(spec.max_count, 1024);
        FLOW_ASSERT_EQ(spec.memory_mb, 64);

        /* Verify private vs non-private def, structural domain precond (from structure Prop field)
         * and theorem material implication guards P(x) => Q(x, _ret) */
        const FlowLeanFunction *fn_priv = NULL;
        const FlowLeanFunction *fn_pub = NULL;
        for (size_t i = 0; i < spec.lean_function_count; ++i) {
            if (strcmp(spec.lean_functions[i].name, "internal_scale") == 0) fn_priv = &spec.lean_functions[i];
            if (strcmp(spec.lean_functions[i].name, "process_weight") == 0) fn_pub = &spec.lean_functions[i];
        }
        FLOW_ASSERT_TRUE(fn_priv != NULL && fn_priv->is_private == 1);
        FLOW_ASSERT_TRUE(fn_pub != NULL && fn_pub->is_private == 0);
        FLOW_ASSERT_TRUE(strstr(fn_pub->precond_expr, "w > 0") != NULL);
        FLOW_ASSERT_EQ(fn_pub->thm_guard_count, 2u);
        FLOW_ASSERT_TRUE(strstr(fn_pub->postcond_expr, "w > 0") != NULL);
        FLOW_ASSERT_TRUE(strstr(fn_pub->postcond_expr, "_ret > w") != NULL);
        FLOW_ASSERT_TRUE(strstr(fn_pub->postcond_expr, "w < 1000") != NULL);
        FLOW_ASSERT_TRUE(strstr(fn_pub->postcond_expr, "_ret < 2001") != NULL);

        /* Verify retired @quota / @embodied / @reload / @patchbay annotations are rejected */
        static const char *const retired_annotations[] = {
            "namespace Bad1\n@quota { deadline: 200us }\ndef f (x : UInt64) : UInt64 := x\nend Bad1\n",
            "namespace Bad2\n@embodied { frequency 1000hz }\ndef f (x : UInt64) : UInt64 := x\nend Bad2\n",
            "namespace Bad3\n@reload { version: 2 }\ndef f (x : UInt64) : UInt64 := x\nend Bad3\n",
            "namespace Bad4\n@patchbay { stage s1 fn f in S out S }\ndef f (x : UInt64) : UInt64 := x\nend Bad4\n"
        };
        for (size_t i = 0; i < sizeof(retired_annotations) / sizeof(retired_annotations[0]); ++i) {
            FILE *bmem = tmpfile();
            FLOW_ASSERT_TRUE(bmem != NULL);
            fputs(retired_annotations[i], bmem);
            rewind(bmem);
            static FlowSpec bspec;
            memset(&bspec, 0, sizeof(bspec));
            FLOW_ASSERT_FALSE(parse_spec(bmem, &bspec));
            fclose(bmem);
        }

        static SemanticIR ir;
        memset(&ir, 0, sizeof(ir));
        lower_to_ir(&spec, &ir);
        const Component *comp = select_component(&ir);
        FLOW_ASSERT_TRUE(comp != NULL);
        flow_ir_cleanup(&ir);
        printf("  ✓ Stage 1 Passed: Pure .lean parsed, Zero Config Injection & Theorem Pre/Post extraction verified.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 2: Registry Subsystem & Explicit C17 FlowUnit QSBR Lifecycle        */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(2, "Registry Subsystem, Explicit FlowUnit Registration & QSBR Execution");
    {
        size_t plugin_count = flow_registry_plugin_count();
        FLOW_ASSERT_TRUE(plugin_count >= 1);

        const FlowPlugin *builtin = flow_registry_lookup("builtin");
        FLOW_ASSERT_TRUE(builtin != NULL);
        FLOW_ASSERT_STR_EQ(builtin->name, "builtin");
        FLOW_ASSERT_TRUE(builtin->component_count >= 3);

        static const FlowUnit zero_unit = {
            .abi_version = FLOW_RELOAD_ABI_VERSION,
            .semantic_schema_hash = 0x11223344ULL,
            .constraint_hash = 0,
            .capability_hash = 0,
            .name = "custom_zero_unit",
            .layout = FLOW_LAYOUT_DEFAULT,
            .supports_snapshot_cow = 1,
            .init = unit_zero_init,
            .run = unit_zero_run,
            .apply = NULL,
            .migrate = NULL,
            .drop = unit_zero_drop,
            .schema = NULL
        };
        FLOW_ASSERT_TRUE(flow_registry_register_unit(&zero_unit, "cpu", "qsbr", 0, 64));

        FlowReloadContext *rctx = flow_reload_create(NULL);
        FLOW_ASSERT_TRUE(rctx != NULL);
        FLOW_ASSERT_EQ(flow_reload_activate(rctx, &zero_unit), FLOW_RELOAD_OK);

        uint64_t in_val = 42, out_val = 0;
        FLOW_ASSERT_EQ(flow_qsbr_call(rctx, &in_val, &out_val), FLOW_RELOAD_OK);
        FLOW_ASSERT_EQ(out_val, 142ULL);
        FLOW_ASSERT_EQ(flow_reload_destroy(rctx), FLOW_RELOAD_OK);

        printf("  ✓ Stage 2 Passed: Registry introspection & explicit C17 FlowUnit QSBR execution verified.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 3: Classical Adaptive Control, Hysteresis & SMT Watchdog Invariants */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(3, "Classical Adaptive Control: JIT Quota Sizing, Schmitt Hysteresis & SMT Watchdog");
    {
        static SemanticIR ir11;
        memset(&ir11, 0, sizeof(ir11));
        ir11.flow_node_count = 11;
        int jit_ram_11 = flow_jit_calculate_min_memory_mb(&ir11);
        FLOW_ASSERT_EQ(jit_ram_11, 100);

        FlowSchmittTrigger st;
        flow_schmitt_trigger_init(&st, 100.0, 500000000ULL);
        FLOW_ASSERT_EQ((int)st.drop_threshold, 80);
        FLOW_ASSERT_EQ((int)st.recovery_threshold, 150);

        int changed = 0;
        flow_schmitt_trigger_update(&st, 16.0, 1000ULL, &changed);
        FLOW_ASSERT_EQ(st.current_state, 1);
        FLOW_ASSERT_EQ(changed, 1);

        flow_schmitt_trigger_update(&st, 95.0, 2000ULL, &changed);
        FLOW_ASSERT_EQ(st.current_state, 1);
        FLOW_ASSERT_EQ(changed, 0);
        flow_schmitt_trigger_update(&st, 105.0, 3000ULL, &changed);
        FLOW_ASSERT_EQ(st.current_state, 1);
        FLOW_ASSERT_EQ(changed, 0);

        FlowSMTProofAttestation watchdog_proof;
        Component dummy_comp = { .id = "test_comp" };
        int smt_ok = flow_smt_verify_with_budget(&ir11, &dummy_comp, NULL, NULL, 5, &watchdog_proof);
        FLOW_ASSERT_TRUE(smt_ok == 1);
        FLOW_ASSERT_TRUE(strstr(watchdog_proof.proof_summary, "Conservative Polytope Interval Bounding Box") != NULL);

        printf("  ✓ Stage 3 Passed: JIT sizing (100MB), Schmitt hysteresis, and SMT 5us watchdog verified.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 4: Lean 4 Certificate Auditor, Zero-Sorry Gate & C17 Header         */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(4, "Lean 4 Certificate Auditor, Zero-Sorry Gate & Dimensional C17 Header");
    {
        const char *cert_lean_src =
            "namespace Flow.CertTest\n"
            "structure ExtentState where\n"
            "  baseOff : UInt64\n"
            "  epoch   : UInt64\n"
            "def compactStep (baseOff byteLen stride epoch : UInt64) : UInt32 :=\n"
            "  if stride >= 16 && epoch > 0 && baseOff + byteLen >= baseOff then 0 else 1\n"
            "theorem compact_ok : compactStep 0 64 16 1 = 0 := by decide\n"
            "end Flow.CertTest\n";

        FILE *mem = tmpfile();
        FLOW_ASSERT_TRUE(mem != NULL);
        fputs(cert_lean_src, mem);
        rewind(mem);

        static FlowSpec lspec;
        memset(&lspec, 0, sizeof(lspec));
        FLOW_ASSERT_TRUE(parse_spec(mem, &lspec));
        fclose(mem);
        FLOW_ASSERT_EQ(lspec.lean_certified, 1);

        static SemanticIR lir;
        memset(&lir, 0, sizeof(lir));
        lower_to_ir(&lspec, &lir);
        const Component *comp = select_component(&lir);
        FLOW_ASSERT_TRUE(comp != NULL);

        VerificationReport vrep;
        FLOW_ASSERT_TRUE(verify_candidate(&lir, comp, NULL, &vrep));
        FLOW_ASSERT_EQ(vrep.status, VERIFIER_PROVEN);

        /* Reject unresolved '?' hole (sorry) under lean_certified gate */
        static SemanticIR hole_ir;
        hole_ir = lir;
        hole_ir.hole_count = 1;
        VerificationReport hole_rep;
        FLOW_ASSERT_TRUE(!verify_candidate(&hole_ir, comp, NULL, &hole_rep));
        FLOW_ASSERT_EQ(hole_rep.status, VERIFIER_COMPILE_ERROR);

        FlowComponentABI abi;
        FLOW_ASSERT_TRUE(flow_abi_build_for_component(&lir, comp, &abi));

        const char *tmp_lean_path = "/tmp/flow_cert_audit_test.lean";
        FILE *lean_fp = fopen(tmp_lean_path, "w");
        FLOW_ASSERT_TRUE(lean_fp != NULL);
        FLOW_ASSERT_TRUE(flow_abi_emit_lean_adapter(lean_fp, &lir, &abi, 0x0000000000000021ULL, 1024, 1));
        fclose(lean_fp);

        char audit_err[256] = {0};
        FLOW_ASSERT_TRUE(flow_abi_verify_lean_certificate(tmp_lean_path, audit_err, sizeof(audit_err)));
        remove(tmp_lean_path);

        /* Verify auditor catches injected `sorry` and custom `axiom` */
        const char *bad_sorry = "theorem bad : 1 = 2 := by sorry\n";
        FLOW_ASSERT_TRUE(!flow_abi_audit_lean_certificate(bad_sorry, audit_err, sizeof(audit_err)));
        FLOW_ASSERT_TRUE(strstr(audit_err, "sorry") != NULL);

        const char *bad_axiom = "axiom fake_soundness : False\n";
        FLOW_ASSERT_TRUE(!flow_abi_audit_lean_certificate(bad_axiom, audit_err, sizeof(audit_err)));
        FLOW_ASSERT_TRUE(strstr(audit_err, "axiom") != NULL);

        /* Verify C17 Header emits Dimensional Newtypes & FLOW_LEAN_PRE_GUARD */
        FILE *hdr_mem = tmpfile();
        FLOW_ASSERT_TRUE(hdr_mem != NULL);
        FLOW_ASSERT_TRUE(flow_abi_emit_c_header(hdr_mem, &abi));
        long hdr_len = ftell(hdr_mem);
        FLOW_ASSERT_TRUE(hdr_len > 200);
        rewind(hdr_mem);
        char *hdr_buf = (char *)calloc((size_t)hdr_len + 1, 1);
        FLOW_ASSERT_TRUE(hdr_buf != NULL);
        (void)fread(hdr_buf, 1, (size_t)hdr_len, hdr_mem);
        fclose(hdr_mem);
        FLOW_ASSERT_TRUE(strstr(hdr_buf, "ExtentState") != NULL);
        FLOW_ASSERT_TRUE(strstr(hdr_buf, "compactStep") != NULL);
        FLOW_ASSERT_TRUE(strstr(hdr_buf, "lean_compactStep") != NULL);
        FLOW_ASSERT_TRUE(strstr(hdr_buf, "FLOW_LEAN_PRE_GUARD") != NULL);
        free(hdr_buf);

        flow_ir_cleanup(&lir);
        printf("  ✓ Stage 4 Passed: Lean 4 certificate audit, zero-sorry gate, and C17 header verified.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 5: Pure .lean Embodied Reflex & AWS Cedar Authorization in C17      */
    /* ========================================================================= */
        FLOW_STAGE_BEGIN(5, "Pure .lean Embodied Reflex (plugins/embodied.lean) in C17");
        {
            /* 5a. Compile plugins/embodied.lean directly to C17 + C17 header and execute in C17 */
            char cmd_emb[1024];
            snprintf(cmd_emb, sizeof(cmd_emb),
                     "%s/flowc plugins/embodied.lean -o /tmp/flow_emb_pure.c --target-c-header /tmp/flow_emb_pure.h",
                     FLOW_BUILD_DIR);
            FLOW_ASSERT_EQ(system(cmd_emb), 0);

            FILE *emb_drv = fopen("/tmp/flow_emb_pure_drv.c", "w");
            FLOW_ASSERT_TRUE(emb_drv != NULL);
            fputs("#include \"/tmp/flow_emb_pure.h\"\n"
                  "#include <assert.h>\n"
                  "#include <math.h>\n"
                  "int main(void) {\n"
                  "    assert(flowc_isTorqueWithinLimit(45.0, 80.0) == 1);\n"
                  "    assert(flowc_isTorqueWithinLimit(95.0, 80.0) == 0);\n"
                  "    assert(flowc_isTorqueWithinLimit(NAN, 80.0) == 0);\n"
                  "    assert(fabs(flowc_dampedAngularAccel(10.0, 2.0, 50.0) - 39.6) < 1e-6);\n"
                  "    assert(flowc_dampedAngularAccel(NAN, 2.0, 50.0) == 0.0);\n"
                  "    assert(fabs(flowc_pdReflexLaw(0.5, 1.0, 20.0, 2.0) - (-12.0)) < 1e-6);\n"
                  "    assert(flowc_embodiedStepKernel(0, 64, 64, 1) == 0);\n"
                  "    return 0;\n"
                  "}\n", emb_drv);
            fclose(emb_drv);
            FLOW_ASSERT_EQ(system("cc -std=c17 -Wall -Wextra -Werror -O2 /tmp/flow_emb_pure_drv.c -lm -o /tmp/flow_emb_pure_drv && /tmp/flow_emb_pure_drv"), 0);
            remove("/tmp/flow_emb_pure.c");
            remove("/tmp/flow_emb_pure.h");
            remove("/tmp/flow_emb_pure_drv.c");
            remove("/tmp/flow_emb_pure_drv");

            FlowJet jet;
            FLOW_ASSERT_TRUE(flow_jet_init(&jet, "jet_embodied_test", "Embodied Named View Jet"));
            TestEmbodiedJetView view = TestEmbodiedJetView_bind(&jet);
            view.q->hipPitch = 0.25;
            view.q->hipRoll = NAN;
            FLOW_ASSERT_TRUE(flow_jet_sanitize_finite(&jet));
            FLOW_ASSERT_TRUE(isfinite(view.q->hipRoll) && view.q->hipRoll == 0.0);

            printf("  ✓ Stage 5 Passed: Pure .lean (plugins/embodied.lean) verified in C17.\n\n");
        }

    /* ========================================================================= */
    /* STAGE 6: Full .lean Scalar/Struct Defs, Theorem Contract Guards & Zero-GC */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(6, "Full .lean Scalar/Struct C17 Synthesis, Automatic Theorem Pre/Post Guards & Zero-GC Gate");
    {
        /* 6a. Reject cheat tokens (sorry, admit, sorryAx, native_decide, axiom) & unclosed block comments */
        static const char *const cheat_lean_snippets[] = {
            "namespace Flow.Cheat1\ntheorem t1 : 1 = 2 := by sorry\nend Flow.Cheat1\n",
            "namespace Flow.Cheat2\ntheorem t2 : 1 = 2 := by admit\nend Flow.Cheat2\n",
            "namespace Flow.Cheat3\ntheorem t3 : 1 = 2 := by native_decide\nend Flow.Cheat3\n",
            "namespace Flow.Cheat4\naxiom bogus_ax : False\nend Flow.Cheat4\n",
            "namespace Flow.Cheat5\n/- unclosed block comment\ndef f (x : UInt64) : UInt64 := x\n"
        };
        for (size_t ci = 0; ci < sizeof(cheat_lean_snippets) / sizeof(cheat_lean_snippets[0]); ++ci) {
            FILE *fl_fp = tmpfile();
            FLOW_ASSERT_TRUE(fl_fp != NULL);
            fputs(cheat_lean_snippets[ci], fl_fp);
            rewind(fl_fp);
            static FlowSpec f_spec;
            memset(&f_spec, 0, sizeof(f_spec));
            FLOW_ASSERT_FALSE(parse_spec(fl_fp, &f_spec));
            fclose(fl_fp);
        }

        /* 6b. Lean-RT Zero-GC Compile-Time Gate: reject List, Array, ByteArray, ++, closures, recursion, recursive inductive */
        static const char *const forbidden_lean_snippets[] = {
            "def bad_list (x : List UInt64) : UInt64 := 0\n",
            "structure BadArr where\n  xs : Array Float\ndef f (x : UInt64) : UInt64 := x\n",
            "def bad_ba (x : ByteArray) : UInt64 := 0\n",
            "def bad_cat (a : UInt64) (b : UInt64) : UInt64 := a ++ b\n",
            "def bad_closure (y : UInt64) : UInt64 := (fun x => x + 1) y\n",
            "def bad_rec (n : UInt64) : UInt64 := if n == 0 then 0 else bad_rec (n - 1)\n",
            "inductive BadTree where\n  | leaf : BadTree\n  | node : BadTree -> BadTree\ndef f (x : UInt64) : UInt64 := x\n"
        };
        for (size_t fi = 0; fi < sizeof(forbidden_lean_snippets) / sizeof(forbidden_lean_snippets[0]); ++fi) {
            FILE *bfp = tmpfile();
            FLOW_ASSERT_TRUE(bfp != NULL);
            fputs(forbidden_lean_snippets[fi], bfp);
            rewind(bfp);
            static FlowSpec bad_spec;
            memset(&bad_spec, 0, sizeof(bad_spec));
            FLOW_ASSERT_FALSE(parse_spec(bfp, &bad_spec));
            fclose(bfp);
        }

        /* 6c. Nested structures, inductive enums, private def hiding & automatic theorem contract synthesis in C17 */
        const char *nest_lean = "/tmp/flow_stage6_nested.lean";
        FILE *nlf = fopen(nest_lean, "w");
        FLOW_ASSERT_TRUE(nlf != NULL);
        fputs(
            "namespace Flow.Stage6Nested\n"
            "inductive ControlMode where\n"
            "  | idle\n"
            "  | track\n"
            "  | fault\n"
            "structure Vec2 where\n"
            "  x : Float\n"
            "  y : Float\n"
            "structure Vel2 where\n"
            "  vx : Float\n"
            "  vy : Float\n"
            "structure PoseFrame where\n"
            "  pos : Vec2\n"
            "  vel : Vel2\n"
            "  ticks : UInt64\n"
            "structure GainDomain where\n"
            "  limit : Float\n"
            "  h_lim : limit > 0.0\n"
            "private def half_dt : Float := 0.5\n"
            "def step_pose (p : PoseFrame) : PoseFrame :=\n"
            "  { pos := { x := p.pos.x + p.vel.vx * half_dt, y := p.pos.y + p.vel.vy * half_dt }, vel := { vx := p.vel.vx, vy := p.vel.vy }, ticks := p.ticks + 1 }\n"
            "def encode_torque (t : Float) : UInt64 := flow_quantize t (-80.0) 80.0 16\n"
            "def decode_torque (q : UInt64) : Float := flow_dequantize q (-80.0) 80.0 16\n"
            "def bounded_gain (x : Float) (limit : Float) : Float :=\n"
            "  if x > limit then limit else x\n"
            "theorem step_pose_ticks_monotonic (p : PoseFrame) : (step_pose p).ticks > p.ticks := by decide\n"
            "theorem bounded_gain_contract (x : Float) (limit : Float) (h_lim : limit > 0.0) : bounded_gain x limit <= limit := by decide\n"
            "theorem bounded_gain_nonneg (x : Float) (limit : Float) : x >= 0.0 → bounded_gain x limit >= 0.0 := by decide\n"
            "theorem bounded_gain_neg (x : Float) (limit : Float) (h_lim : limit > 0.0) : x < 0.0 → bounded_gain x limit < 0.0 := by decide\n"
            "end Flow.Stage6Nested\n", nlf);
        fclose(nlf);

        char cmd6[1024];
        snprintf(cmd6, sizeof(cmd6),
                 "%s/flowc %s -o /tmp/flow_stage6_nested.c --target-c-header /tmp/flow_stage6_nested.h",
                 FLOW_BUILD_DIR, nest_lean);
        FLOW_ASSERT_EQ(system(cmd6), 0);

        /* Verify private def half_dt is NOT exported in the public C header, while flowc_lean_* contract wrappers are */
        FILE *h6_fp = fopen("/tmp/flow_stage6_nested.h", "r");
        FLOW_ASSERT_TRUE(h6_fp != NULL);
        fseek(h6_fp, 0, SEEK_END);
        long h6_sz = ftell(h6_fp);
        rewind(h6_fp);
        char *h6_buf = (char *)calloc((size_t)h6_sz + 1, 1);
        FLOW_ASSERT_TRUE(h6_buf != NULL);
        (void)fread(h6_buf, 1, (size_t)h6_sz, h6_fp);
        fclose(h6_fp);
        FLOW_ASSERT_TRUE(strstr(h6_buf, "half_dt(") == NULL);
        FLOW_ASSERT_TRUE(strstr(h6_buf, "flowc_lean_bounded_gain(") != NULL);
        FLOW_ASSERT_TRUE(strstr(h6_buf, "flowc_lean_step_pose(") != NULL);
        free(h6_buf);

        FLOW_ASSERT_EQ(system("cc -std=c17 -Wall -Wextra -Werror -O2 -shared -fPIC -DFLOW_SHARED_LIB /tmp/flow_stage6_nested.c -o /tmp/libflow_stage6_nested.so -lm -lpthread"), 0);

        FILE *drv6 = fopen("/tmp/flow_stage6_drv.c", "w");
        FLOW_ASSERT_TRUE(drv6 != NULL);
        fputs("#include \"/tmp/flow_stage6_nested.h\"\n"
              "#include <assert.h>\n"
              "#include <dlfcn.h>\n"
              "#include <math.h>\n"
              "typedef double (*bg_fn_t)(double, double);\n"
              "typedef PoseFrame (*sp_fn_t)(PoseFrame);\n"
              "int main(void) {\n"
              "    void *dl = dlopen(\"/tmp/libflow_stage6_nested.so\", RTLD_NOW);\n"
              "    assert(dl != NULL);\n"
              "    /* private def half_dt must NOT be exported as lean_half_dt */\n"
              "    assert(dlsym(dl, \"lean_half_dt\") == NULL);\n"
              "    bg_fn_t fn_lean_bg = (bg_fn_t)dlsym(dl, \"lean_bounded_gain\");\n"
              "    sp_fn_t fn_lean_sp = (sp_fn_t)dlsym(dl, \"lean_step_pose\");\n"
              "    assert(fn_lean_bg != NULL && fn_lean_sp != NULL);\n"
              "    /* Valid domain (limit > 0.0) & positive x: satisfies x >= 0.0 => _ret >= 0.0 */\n"
              "    assert(fabs(fn_lean_bg(120.0, 80.0) - 80.0) < 1e-9);\n"
              "    /* Violating structural domain precondition (limit <= 0.0) triggers domain pre-guard -> 0.0 */\n"
              "    assert(fn_lean_bg(50.0, -10.0) == 0.0);\n"
              "    /* Valid negative x = -5.0: material implication guards (x >= 0 => _ret >= 0) and (x < 0 => _ret < 0) both pass! */\n"
              "    assert(fabs(fn_lean_bg(-5.0, 80.0) - (-5.0)) < 1e-9);\n"
              "    PoseFrame pf = { 1.0, 2.0, 4.0, -2.0, 10ULL };\n"
              "    PoseFrame out = fn_lean_sp(pf);\n"
              "    assert(fabs(out.pos_x - 3.0) < 1e-9 && fabs(out.pos_y - 1.0) < 1e-9 && out.ticks == 11ULL);\n"
              "    dlclose(dl);\n"
              "    return 0;\n"
              "}\n", drv6);
        fclose(drv6);
        FLOW_ASSERT_EQ(system("cc -std=c17 -Wall -Wextra -Werror -O2 /tmp/flow_stage6_drv.c -ldl -lm -o /tmp/flow_stage6_drv && /tmp/flow_stage6_drv"), 0);

        remove(nest_lean);
        remove("/tmp/flow_stage6_nested.c");
        remove("/tmp/flow_stage6_nested.h");
        remove("/tmp/libflow_stage6_nested.so");
        remove("/tmp/flow_stage6_drv.c");
        remove("/tmp/flow_stage6_drv");
        printf("  ✓ Stage 6 Passed: Private def hiding, theorem pre/post C17 contract guards & Zero-GC gate verified.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 7: Flat Consolidation, Symbolic Constants & flowy build --shared    */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(7, "Flat Consolidation, Symbolic Constants & Pure C17 flowy build --shared");
    {
        FLOW_ASSERT_TRUE(access("src/fvec", F_OK) != 0);
        FLOW_ASSERT_TRUE(access("src/jet", F_OK) != 0);
        FLOW_ASSERT_TRUE(access("src/topo", F_OK) != 0);
        FLOW_ASSERT_TRUE(access("src/inspect", F_OK) != 0);
        FLOW_ASSERT_TRUE(access("src/topology.h", F_OK) != 0);
        FLOW_ASSERT_TRUE(access("src/topology.c", F_OK) != 0);
        FLOW_ASSERT_TRUE(access("crates/flow-plugin", F_OK) != 0);
        FLOW_ASSERT_TRUE(access("plugins/embodied.flow", F_OK) != 0);

        const char *crc_msg = "FLOW_ORTHOGONAL_2026";
        uint32_t crc_base = flow_crc32_ieee(crc_msg, 20);
        FLOW_ASSERT_EQ(crc_base, flow_fvec_crc32(crc_msg, 20));
        FLOW_ASSERT_EQ(crc_base, flow_jet_crc32(crc_msg, 20));

        const char *s7_lean = "/tmp/flow_stage7_sdk.lean";
        FILE *s7_fp = fopen(s7_lean, "w");
        FLOW_ASSERT_TRUE(s7_fp != NULL);
        fputs(
            "namespace Embodied.Stage7SDK\n"
            "structure JointCmd where\n"
            "  torque_nm : Float\n"
            "  seq       : UInt64\n"
            "def clamp_torque (t : Float) (lim : Float) : Float :=\n"
            "  if t > lim then lim else t\n"
            "theorem clamp_torque_sound : clamp_torque 120.0 80.0 == 80.0 := by decide\n"
            "end Embodied.Stage7SDK\n", s7_fp);
        fclose(s7_fp);

        char sh_cmd[512];
        snprintf(sh_cmd, sizeof(sh_cmd), "%s/flowy build --shared %s >/dev/null", FLOW_BUILD_DIR, s7_lean);
        FLOW_ASSERT_EQ(system(sh_cmd), 0);
        FLOW_ASSERT_EQ(access("build/libflow_stage7_sdk.so", F_OK), 0);
        FLOW_ASSERT_EQ(access("build/flow_stage7_sdk.h", F_OK), 0);
        /* Verify no polyglot files are emitted */
        FLOW_ASSERT_NE(access("build/flow_stage7_sdk_abi.py", F_OK), 0);
        FLOW_ASSERT_NE(access("build/flow_stage7_sdk_abi.rs", F_OK), 0);
        FLOW_ASSERT_NE(access("build/flow_flow_stage7_sdk.m", F_OK), 0);

        remove("build/libflow_stage7_sdk.so");
        remove("build/flow_stage7_sdk.h");
        remove("build/flow_stage7_sdk.c");
        remove(s7_lean);

        printf("  ✓ Stage 7 Passed: Flat consolidation & pure C17 flowy build --shared (.so + .h only) verified.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 8: Multi-line Tuple Lean 4, Static Inline Header & Shadow A/B       */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(8, "Multi-line Tuple Lean 4, --target-c-header Static Inline & Cross-DSO Shadow A/B");
    {
        const char *pr_lean_path = "/tmp/flow_stage18_quant.lean";
        const char *pr_c_path    = "/tmp/flow_stage18_quant.c";
        const char *pr_h_path    = "/tmp/flow_stage18_quant.h";
        const char *pr_so_path   = "/tmp/libflow_stage18_quant.so";
        const char *pr_drv_path  = "/tmp/flow_stage18_driver.c";
        const char *pr_bin_path  = "/tmp/flow_stage18_driver";

        FILE *pr_fp = fopen(pr_lean_path, "w");
        FLOW_ASSERT_TRUE(pr_fp != NULL);
        fputs(
            "namespace Quant.BilateralQuote\n"
            "structure QuoteBook where\n"
            "  mid_px : Float\n"
            "  inv_qty : Float\n"
            "def exec_bilateral_quote\n"
            "    (mid : Float)\n"
            "    (vol : Float)\n"
            "    (inv : Float)\n"
            "    (tick : Float) : (Float × Float) :=\n"
            "  let safe_tick := Float.max tick 1.0e-6 in\n"
            "  let half_sprd := Float.max (Float.sqrt (Float.abs vol) * 0.5) safe_tick in\n"
            "  let skew := if inv == 0.0 then 0.0 else (if inv < 0.0 then -half_sprd * 0.25 else half_sprd * 0.25) in\n"
            "  let raw_bid := mid - half_sprd - skew in\n"
            "  let raw_ask := mid + half_sprd - skew in\n"
            "  let bid_px := Float.floor (raw_bid / safe_tick) * safe_tick in\n"
            "  let ask_px := Float.ceil (raw_ask / safe_tick) * safe_tick in\n"
            "  (bid_px, ask_px)\n"
            "def quote_spread\n"
            "    (mid : Float)\n"
            "    (vol : Float)\n"
            "    (inv : Float)\n"
            "    (tick : Float) : Float :=\n"
            "  let (b, a) := exec_bilateral_quote mid vol inv tick in\n"
            "  Float.max (a - b) (Float.min tick 1.0)\n"
            "end Quant.BilateralQuote\n",
            pr_fp);
        fclose(pr_fp);

        char cmd_flowc[512];
        snprintf(cmd_flowc, sizeof(cmd_flowc),
                 "%s/flowc %s -o %s --target-c-header %s",
                 FLOW_BUILD_DIR, pr_lean_path, pr_c_path, pr_h_path);
        FLOW_ASSERT_EQ(system(cmd_flowc), 0);

        char cmd_so[512];
        snprintf(cmd_so, sizeof(cmd_so),
                 "cc -std=c17 -Wall -Wextra -Wpedantic -Werror -O2 -shared -fPIC -DFLOW_SHARED_LIB %s -o %s -lm -lpthread",
                 pr_c_path, pr_so_path);
        FLOW_ASSERT_EQ(system(cmd_so), 0);

        FILE *drv_fp = fopen(pr_drv_path, "w");
        FLOW_ASSERT_TRUE(drv_fp != NULL);
        fprintf(drv_fp,
            "#include <assert.h>\n"
            "#include <dlfcn.h>\n"
            "#include <math.h>\n"
            "#include <stdio.h>\n"
            "#include \"plugin.h\"\n"
            "#include \"bitspace.h\"\n"
            "#include \"%s\"\n"
            "typedef double (*exec_quote_fn)(double, double, double, double, double *, double *);\n"
            "typedef double (*quote_spread_fn)(double, double, double, double);\n"
            "typedef void (*ab_rec_fn)(uint32_t, double, double, double);\n"
            "typedef uint64_t (*ab_get_u64_fn)(void);\n"
            "typedef uint64_t (*ab_get_fn_u64_fn)(uint32_t);\n"
            "typedef void (*ab_reset_fn)(void);\n"
            "int main(void) {\n"
            "    exec_bilateral_quote_tuple_t tup_check = { 0.0, 0.0 };\n"
            "    (void)tup_check;\n"
            "    double inl_bid = 0.0, inl_ask = 0.0;\n"
            "    double ret0 = flowc_exec_bilateral_quote(100.0, 4.0, -2.0, 0.25, &inl_bid, &inl_ask);\n"
            "    assert(fabs(ret0 - inl_bid) < 1e-9);\n"
            "    assert(fabs(inl_bid - 99.25) < 1e-9);\n"
            "    assert(fabs(inl_ask - 101.25) < 1e-9);\n"
            "    double inl_sprd = flowc_quote_spread(100.0, 4.0, -2.0, 0.25);\n"
            "    assert(fabs(inl_sprd - 2.0) < 1e-9);\n",
            pr_h_path);
        fprintf(drv_fp,
            "    void *h = dlopen(\"%s\", RTLD_NOW | RTLD_LOCAL);\n"
            "    assert(h != NULL);\n"
            "    FlowPluginEntryFn entry = (FlowPluginEntryFn)dlsym(h, \"flow_plugin_entry_v1\");\n"
            "    assert(entry != NULL);\n"
            "    const FlowPluginDescriptor *desc = entry();\n"
            "    assert(desc != NULL && desc->abi_major == 1 && desc->descriptor_size >= 64);\n"
            "    exec_quote_fn dso_quote = (exec_quote_fn)dlsym(h, \"exec_bilateral_quote\");\n"
            "    quote_spread_fn dso_spread = (quote_spread_fn)dlsym(h, \"quote_spread\");\n"
            "    ab_rec_fn dso_ab_rec = (ab_rec_fn)dlsym(h, \"flow_ab_shadow_record\");\n"
            "    ab_get_u64_fn dso_ab_calls = (ab_get_u64_fn)dlsym(h, \"flow_ab_shadow_get_calls\");\n"
            "    ab_get_u64_fn dso_ab_mis = (ab_get_u64_fn)dlsym(h, \"flow_ab_shadow_get_mismatches\");\n"
            "    ab_get_fn_u64_fn dso_ab_fn = (ab_get_fn_u64_fn)dlsym(h, \"flow_ab_shadow_get_fn_calls\");\n"
            "    ab_reset_fn dso_ab_reset = (ab_reset_fn)dlsym(h, \"flow_ab_shadow_reset\");\n"
            "    assert(dso_quote && dso_spread && dso_ab_rec && dso_ab_calls && dso_ab_mis && dso_ab_fn && dso_ab_reset);\n"
            "    dso_ab_reset();\n"
            "    double dso_bid = 0.0, dso_ask = 0.0;\n"
            "    dso_quote(100.0, 4.0, -2.0, 0.25, &dso_bid, &dso_ask);\n"
            "    assert(fabs(dso_bid - inl_bid) < 1e-9 && fabs(dso_ask - inl_ask) < 1e-9);\n"
            "    assert(dso_ab_fn(0) == 1);\n"
            "    dso_ab_rec(0, dso_bid, inl_bid, 1e-9);\n"
            "    dso_ab_rec(1, 2.0, 2.5, 1e-9);\n"
            "    assert(dso_ab_calls() == 2 && dso_ab_mis() == 1);\n"
            "    dlclose(h);\n"
            "    return 0;\n"
            "}\n",
            pr_so_path);
        fclose(drv_fp);

        char cmd_drv[1024];
        snprintf(cmd_drv, sizeof(cmd_drv),
                 "cc -std=gnu17 -Wall -Wextra -Wpedantic -Werror -Wmisleading-indentation -O2 -Isrc %s -o %s -lm -ldl && %s",
                 pr_drv_path, pr_bin_path, pr_bin_path);
        FLOW_ASSERT_EQ(system(cmd_drv), 0);

        remove(pr_lean_path);
        remove(pr_c_path);
        remove(pr_h_path);
        remove(pr_so_path);
        remove(pr_drv_path);
        remove(pr_bin_path);
        printf("  ✓ Stage 8 Passed: Multi-line tuple Lean 4, static inline header, and cross-DSO shadow A/B verified.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 9: FlowExactNumerics — Q16.16 Approximators, U256 & IEEE-754 Linalg */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(9, "FlowExactNumerics: Q16.16 Approximators, 256-Bit BigInt & IEEE-754 Linalg");
    {
        int32_t exp_0 = 0;
        FLOW_ASSERT_EQ(flow_exp_approx_q16(0, &exp_0), 1);
        FLOW_ASSERT_EQ(exp_0, 65536);

        uint32_t rsqrt_16 = 0;
        FLOW_ASSERT_EQ(flow_rsqrt_approx_i32(16U, &rsqrt_16), 1);
        FLOW_ASSERT_EQ(rsqrt_16, 16384U);

        FLOW_ASSERT_EQ(flow_quantize(0.5, 0.0, 1.0, 16U), 32768ULL);
        FLOW_ASSERT_TRUE(fabs(flow_dequantize(32768ULL, 0.0, 1.0, 16U) - 0.5) < 1e-4);

        static const uint64_t secp_bases[] = { 2ULL, 3ULL, 5ULL, 7ULL };
        FlowU256 p_secp;
        FLOW_ASSERT_TRUE(flow_u256_from_hex_str("0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F", &p_secp));
        FLOW_ASSERT_EQ(flow_u256_is_probably_prime(&p_secp, secp_bases, 4), 1);

        FlowU256 big_base;
        FLOW_ASSERT_TRUE(flow_u256_from_hex_str("0x8000000000000000000000000000000000000000000000000000000000000005", &big_base));
        FlowU256 even_res = flow_u256_mod_pow(big_base, flow_u256_from_u64(2ULL), flow_u256_from_u64(1000ULL));
        FLOW_ASSERT_EQ(even_res.w[0], 729ULL);

        double x_in[2]    = { 1.0, 2.0 };
        double w_param[2] = { 0.0, 0.0 };
        double preds[1]   = { 0.0 };
        double tgts[1]    = { 5.0 };
        flow_mat_mul_ikj(1, 2, 1, x_in, w_param, preds);
        double loss_before = flow_mse_loss(1, 1, preds, tgts);
        flow_gradient_descent_step(1, 2, 1, x_in, w_param, preds, tgts, 0.1);
        flow_mat_mul_ikj(1, 2, 1, x_in, w_param, preds);
        double loss_after = flow_mse_loss(1, 1, preds, tgts);
        FLOW_ASSERT_TRUE(loss_after < loss_before);

        printf("  ✓ Stage 9 Passed: FlowExactNumerics (Q16.16, 256-bit secp256k1 BigInt & IEEE-754 linalg) verified.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 10: Pure C17 Runtime (libflow_rt.a), HoTT U_0 & FlowFinance C17     */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(10, "Pure C17 Runtime (libflow_rt.a), Explicit C17 Pipeline, HoTT U_0 & FlowFinance C17");
    {
        /* 1. Verify HoTT Intensional Type Universe U_0 Univalence */
        FlowHoTTUniverse local_u0;
        flow_hott_universe_reset(&local_u0);
        FLOW_ASSERT_EQ(flow_hott_register_type(&local_u0, "OrderBookFrame", FLOW_HOTT_LANG_LEAN, 4, 32, 8, 32, 0xABCD1234ULL), 1);
        FLOW_ASSERT_EQ(flow_hott_register_type(&local_u0, "OrderBookFrame", FLOW_HOTT_LANG_C17,  4, 32, 8, 32, 0xABCD1234ULL), 1);
        uint64_t path_hash = 0ULL;
        FLOW_ASSERT_EQ(flow_hott_verify_univalence(&local_u0, "OrderBookFrame", &path_hash), FLOW_KAN_FILLED_HOMOTOPIC);
        FLOW_ASSERT_TRUE(path_hash != 0ULL);

        /* 2. Verify Minimal Production Runtime (libflow_rt.a) links standalone with explicit C17 SPSC/Jet/Reload/HoTT primitives */
        char rt_path[256], full_lib_path[256], rt_cmd[1024];
        snprintf(rt_path, sizeof(rt_path), "%s/libflow_rt.a", FLOW_BUILD_DIR);
        snprintf(full_lib_path, sizeof(full_lib_path), "%s/libflow.a", FLOW_BUILD_DIR);
        FILE *fp_rt = fopen(rt_path, "rb"), *fp_full = fopen(full_lib_path, "rb");
        FLOW_ASSERT_TRUE(fp_rt != NULL && fp_full != NULL);
        fseek(fp_rt, 0, SEEK_END); long sz_rt = ftell(fp_rt); fclose(fp_rt);
        fseek(fp_full, 0, SEEK_END); long sz_full = ftell(fp_full); fclose(fp_full);
        FLOW_ASSERT_TRUE(sz_rt > 0 && sz_rt * 3 < sz_full);

        FILE *f_rt_c = fopen("/tmp/flow_s10_rt_only.c", "w");
        FLOW_ASSERT_TRUE(f_rt_c != NULL);
        fputs("#include \"flow_spsc.h\"\n#include \"flow_jet.h\"\n#include \"reload.h\"\n#include \"cubical_hott.h\"\n#include <assert.h>\n"
              "FLOW_SPSC_DEFINE(TickRing, double, 16)\n"
              "int main(void) {\n"
              "    TickRing ring;\n"
              "    TickRing_init(&ring);\n"
              "    assert(TickRing_push(&ring, 101.25) == 1);\n"
              "    double out_px = 0.0;\n"
              "    assert(TickRing_pop(&ring, &out_px) == 1 && out_px == 101.25);\n"
              "    FlowJet jet;\n"
              "    assert(flow_jet_init(&jet, \"rt_jet\", \"Minimal RT Jet\") == 1);\n"
              "    FlowJetScalar2 a = flow_jet2_var(3.5), b = flow_jet2_const(2.0);\n"
              "    assert(flow_jet2_mul(a, b).v == 7.0 && flow_jet2_mul(a, b).d1 == 2.0);\n"
              "    FlowReloadContext *rctx = flow_reload_create(NULL);\n"
              "    assert(rctx != NULL);\n"
              "    flow_reload_destroy(rctx);\n"
              "    return 0;\n}\n", f_rt_c);
        fclose(f_rt_c);
        snprintf(rt_cmd, sizeof(rt_cmd), "cc -std=c17 -Wall -Wextra -Werror -O2 -Isrc /tmp/flow_s10_rt_only.c %s -lm -lpthread -o /tmp/flow_s10_rt_only && /tmp/flow_s10_rt_only", rt_path);
        FLOW_ASSERT_EQ(system(rt_cmd), 0);
        remove("/tmp/flow_s10_rt_only.c");
        remove("/tmp/flow_s10_rt_only");

        /* 3. Verify flowy CLI audit command succeeds */
        char audit_cmd[512];
        snprintf(audit_cmd, sizeof(audit_cmd), "%s/flowy audit > /dev/null", FLOW_BUILD_DIR);
        FLOW_ASSERT_EQ(system(audit_cmd), 0);

        /* 4. FlowFinance Pure .lean -> C17 Regression: noncomputable section, ℝ -> double, Proof Erasure, >8 Params, >32 Theorems & 0-ULP Extreme Powers */
        const char *ff_lean = "/tmp/flow_finance_reg.lean";
        FILE *ff = fopen(ff_lean, "w");
        FLOW_ASSERT_TRUE(ff != NULL);
        fputs("namespace FlowFinance.Pricing\n"
              "noncomputable section\n"
              "structure MarketQuote where\n"
              "  spot   : ℝ\n"
              "  strike : ℝ\n"
              "  vol    : ℝ\n"
              "  h_spot : spot > 0\n"
              "  h_vol  : vol ≥ 0\n"
              "  h_prop : Prop\n"
              "noncomputable def clamp_spread (x : ℝ) (lo : ℝ) (hi : ℝ) : ℝ :=\n"
              "  f_min (f_max (f_abs x) lo) hi\n"
              "noncomputable def extreme_compound (base : ℝ) (years : ℝ) (notional : ℝ) : ℝ :=\n"
              "  notional + (base ^ years)\n"
              "noncomputable def multi_factor_12\n"
              "    (p1 p2 p3 p4 p5 p6 p7 p8 p9 p10 p11 p12 : ℝ)\n"
              "    (h_pos : p1 > 0) : ℝ :=\n"
              "  clamp_spread p1 p2 p3 + p4 + p5 + p6 + p7 + p8 + p9 + p10 + p11 + p12\n", ff);
        for (int ti = 0; ti < 38; ++ti) {
            fprintf(ff, "theorem fin_thm_%d : clamp_spread (-2.5) 1.0 5.0 = 2.5 := by decide\n", ti);
        }
        fputs("theorem frac_exact_thm : 1.5 + 2.25 = 3.75 := by rfl\n"
              "theorem extreme_pow_thm : extreme_compound 2.0 10.0 1000.0 = 2024.0 := by decide\n"
              "end FlowFinance.Pricing\n", ff);
        fclose(ff);

        FILE *ff_in = fopen(ff_lean, "r");
        FLOW_ASSERT_TRUE(ff_in != NULL);
        FlowSpec *ff_spec = (FlowSpec *)calloc(1, sizeof(FlowSpec));
        FLOW_ASSERT_TRUE(ff_spec != NULL);
        FLOW_ASSERT_TRUE(parse_spec(ff_in, ff_spec));
        fclose(ff_in);

        FLOW_ASSERT_EQ(ff_spec->lean_struct_count, 1);
        FLOW_ASSERT_EQ(ff_spec->lean_structs[0].field_count, 3);
        FLOW_ASSERT_EQ(ff_spec->lean_structs[0].byte_size, 24);
        FLOW_ASSERT_EQ(ff_spec->lean_function_count, 3);
        FLOW_ASSERT_EQ(ff_spec->lean_functions[2].param_count, 12);
        FLOW_ASSERT_EQ(ff_spec->float_abi_ieee64, 1);
        FLOW_ASSERT_STR_EQ(ff_spec->lean_functions[2].precond_expr, "(p1 > 0)");
        FLOW_ASSERT_EQ(ff_spec->lean_theorem_count, 40);
        free(ff_spec);

        char ff_cmd[1024];
        snprintf(ff_cmd, sizeof(ff_cmd),
                 "%s/flowc %s -o /tmp/flow_ff.c --target-c-header /tmp/flow_ff.h",
                 FLOW_BUILD_DIR, ff_lean);
        FLOW_ASSERT_EQ(system(ff_cmd), 0);
        FLOW_ASSERT_EQ(system("cc -std=c17 -Wall -Wextra -Werror -O2 -shared -fPIC -DFLOW_SHARED_LIB /tmp/flow_ff.c -o /tmp/libflow_ff.so -lm"), 0);

        FILE *ff_drv = fopen("/tmp/flow_ff_drv.c", "w");
        FLOW_ASSERT_TRUE(ff_drv != NULL);
        fputs("#include \"/tmp/flow_ff.h\"\n"
              "#include <assert.h>\n"
              "#include <dlfcn.h>\n"
              "#include <math.h>\n"
              "typedef double (*mf12_fn_t)(double,double,double,double,double,double,double,double,double,double,double,double);\n"
              "typedef double (*ec_fn_t)(double,double,double);\n"
              "int main(void) {\n"
              "    void *dl = dlopen(\"/tmp/libflow_ff.so\", RTLD_NOW);\n"
              "    assert(dl != NULL);\n"
              "    /* Unified IEEE-754 double ABI emits zero legacy _fx symbols */\n"
              "    assert(dlsym(dl, \"clamp_spread_fx\") == NULL);\n"
              "    ec_fn_t fn_ec = (ec_fn_t)dlsym(dl, \"extreme_compound\");\n"
              "    mf12_fn_t fn_mf = (mf12_fn_t)dlsym(dl, \"multi_factor_12\");\n"
              "    mf12_fn_t fn_lean_mf = (mf12_fn_t)dlsym(dl, \"lean_multi_factor_12\");\n"
              "    assert(fn_ec != NULL && fn_mf != NULL && fn_lean_mf != NULL);\n"
              "    double ref_pow = 1e12 + pow(1.10, 250.0);\n"
              "    assert(fn_ec(1.10, 250.0, 1e12) == ref_pow);\n"
              "    assert(fn_mf(-4.0, 1.0, 10.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0) == 58.0);\n"
              "    assert(fn_lean_mf(4.0, 1.0, 10.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0) == 58.0);\n"
              "    /* Precondition violation (p1 = -4.0 <= 0) returns 0.0 */\n"
              "    assert(fn_lean_mf(-4.0, 1.0, 10.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0) == 0.0);\n"
              "    dlclose(dl);\n"
              "    return 0;\n"
              "}\n", ff_drv);
        fclose(ff_drv);
        FLOW_ASSERT_EQ(system("cc -std=c17 -Wall -Wextra -Werror -O2 /tmp/flow_ff_drv.c -ldl -lm -o /tmp/flow_ff_drv && /tmp/flow_ff_drv"), 0);

        /* Verify FLOW_LEAN_FN_MAX == 128 (>64 functions in a single .lean file) */
        FLOW_ASSERT_EQ(FLOW_LEAN_FN_MAX, 128);
        const char *many_lean = "/tmp/flow_many_fns.lean";
        FILE *mf = fopen(many_lean, "w");
        FLOW_ASSERT_TRUE(mf != NULL);
        fputs("namespace Quant.ManyFns\n", mf);
        for (int fi = 0; fi < 72; ++fi) {
            fprintf(mf, "def fn_op_%d (x : Float) : Float := x + %d.0\n", fi, fi + 1);
        }
        fputs("theorem many_ok : fn_op_0 1.0 = 2.0 := by decide\nend Quant.ManyFns\n", mf);
        fclose(mf);
        FILE *mf_in = fopen(many_lean, "r");
        FLOW_ASSERT_TRUE(mf_in != NULL);
        FlowSpec *mf_spec = (FlowSpec *)calloc(1, sizeof(FlowSpec));
        FLOW_ASSERT_TRUE(mf_spec != NULL);
        FLOW_ASSERT_TRUE(parse_spec(mf_in, mf_spec));
        fclose(mf_in);
        FLOW_ASSERT_EQ(mf_spec->lean_function_count, 72u);
        free(mf_spec);

        remove(many_lean);
        remove(ff_lean);
        remove("/tmp/flow_ff.c");
        remove("/tmp/flow_ff.h");
        remove("/tmp/libflow_ff.so");
        remove("/tmp/flow_ff_drv.c");
        remove("/tmp/flow_ff_drv");

        /* 5. MATLAB --target-mex, Material Implication Guards, 2-Jet Forward AD & Zero-Branch FSM LUT Regression:
         *    (1) Mathlib |v| and |a - b| syntax sugar
         *    (2) Indentation-aware multi-line let bindings
         *    (3) Structural domain precondition (ReynoldsDomain Prop fields) vs Theorem Material Implication Guards (P => Q)
         *    (4) namespace Spec + namespace Exec computable mirror override inheriting Spec theorem contract
         *    (5) 2-Jet Forward AD synthesis (flowc_jet2_<fn>, jet2_<fn>, and MEX "jet2_<fn>" returning [v, d1, d2])
         *    (6) Zero-branch C17 LUT synthesis (static const uint8_t step_order_fsm_transition_table[6][6]) for inductive FSM */
        const char *mex_lean = "/tmp/flow_mex_frictions.lean";
        FILE *mxf = fopen(mex_lean, "w");
        FLOW_ASSERT_TRUE(mxf != NULL);
        fputs("namespace FlowFinance.MexBridge\n"
              "noncomputable section\n"
              "structure ReynoldsDomain where\n"
              "  w_v  : ℝ\n"
              "  w_a  : ℝ\n"
              "  h_wv : w_v ≥ 0\n"
              "  h_wa : w_a > 0\n"
              "inductive OrderState\n"
              "  | Idle\n"
              "  | PendingNew\n"
              "  | Working\n"
              "  | PartialFill\n"
              "  | PendingCancel\n"
              "  | Terminal\n"
              "inductive ExecEvent\n"
              "  | EvSubmit\n"
              "  | EvAck\n"
              "  | EvPartial\n"
              "  | EvFill\n"
              "  | EvCancelReq\n"
              "  | EvCanceled\n"
              "def step_order_fsm (s : OrderState) (ev : ExecEvent) : OrderState :=\n"
              "  match s, ev with\n"
              "  | OrderState.Idle, ExecEvent.EvSubmit => OrderState.PendingNew\n"
              "  | OrderState.PendingNew, ExecEvent.EvAck => OrderState.Working\n"
              "  | OrderState.PendingNew, ExecEvent.EvFill => OrderState.Terminal\n"
              "  | OrderState.PendingNew, ExecEvent.EvCanceled => OrderState.Terminal\n"
              "  | OrderState.Working, ExecEvent.EvPartial => OrderState.PartialFill\n"
              "  | OrderState.Working, ExecEvent.EvFill => OrderState.Terminal\n"
              "  | OrderState.Working, ExecEvent.EvCancelReq => OrderState.PendingCancel\n"
              "  | OrderState.Working, ExecEvent.EvCanceled => OrderState.Terminal\n"
              "  | OrderState.PartialFill, ExecEvent.EvPartial => OrderState.PartialFill\n"
              "  | OrderState.PartialFill, ExecEvent.EvFill => OrderState.Terminal\n"
              "  | OrderState.PartialFill, ExecEvent.EvCancelReq => OrderState.PendingCancel\n"
              "  | OrderState.PartialFill, ExecEvent.EvCanceled => OrderState.Terminal\n"
              "  | OrderState.PendingCancel, ExecEvent.EvPartial => OrderState.PendingCancel\n"
              "  | OrderState.PendingCancel, ExecEvent.EvFill => OrderState.Terminal\n"
              "  | OrderState.PendingCancel, ExecEvent.EvCanceled => OrderState.Terminal\n"
              "  | OrderState.Terminal, _ => OrderState.Terminal\n"
              "  | _, _ => s\n"
              "theorem no_zombie_order (s : OrderState) (ev : ExecEvent)\n"
              "    (h_term : s = OrderState.Terminal) :\n"
              "    step_order_fsm s ev = OrderState.Terminal := by decide\n"
              "noncomputable def eval_reynolds_domain (dom : ReynoldsDomain) (v : ℝ) : ℝ :=\n"
              "  dom.w_v * |v| + dom.w_a\n"
              "noncomputable def option_payoff_smooth (S : ℝ) (K : ℝ) : ℝ :=\n"
              "  let d := S - K\n"
              "  0.5 * d + 0.5 * Real.sqrt (d * d + 4.0)\n"
              "noncomputable def portfolio_utility (S : ℝ) (K : ℝ) : ℝ :=\n"
              "  let v := option_payoff_smooth S K\n"
              "  v * v + 0.1 * (S ^ 3.0)\n"
              "namespace Spec\n"
              "noncomputable def scale_term (w : ℝ) (x : ℝ) : ℝ := w * |x|\n"
              "noncomputable def hamiltonian_reynolds (w_v : ℝ) (w_a : ℝ) (v : ℝ) (a : ℝ) : ℝ :=\n"
              "  w_v * |v| + w_a * |a|\n"
              "theorem hamiltonian_reynolds_nonneg (w_v : ℝ) (w_a : ℝ) (v : ℝ) (a : ℝ)\n"
              "    (h_wv : w_v ≥ 0) (h_wa : w_a > 0) :\n"
              "    hamiltonian_reynolds w_v w_a v a ≥ 0 := by decide\n"
              "theorem hamiltonian_reynolds_bullish_branch (w_v : ℝ) (w_a : ℝ) (v : ℝ) (a : ℝ)\n"
              "    (h_wv : w_v ≥ 0) (h_wa : w_a > 0) (h_v_pos : v > 0) :\n"
              "    hamiltonian_reynolds w_v w_a v a ≥ w_v * v := by decide\n"
              "theorem hamiltonian_reynolds_bearish_branch (w_v : ℝ) (w_a : ℝ) (v : ℝ) (a : ℝ)\n"
              "    (h_wv : w_v ≥ 0) (h_wa : w_a > 0) (h_v_neg : v < 0) :\n"
              "    hamiltonian_reynolds w_v w_a v a ≥ w_v * (-v) := by decide\n"
              "theorem hamiltonian_reynolds_iff (w_v : ℝ) (w_a : ℝ) (v : ℝ) (a : ℝ) :\n"
              "    hamiltonian_reynolds w_v w_a v a = 0 ↔ v = 0 := by decide\n"
              "end Spec\n"
              "namespace Exec\n"
              "def hamiltonian_reynolds (w_v : Float) (w_a : Float) (v : Float) (a : Float) : Float :=\n"
              "  let ke :=\n"
              "    scale_term w_v\n"
              "      v\n"
              "  let pe := f_max\n"
              "    (w_a * |a - 0.0|)\n"
              "    1.0\n"
              "  ke +\n"
              "    pe\n"
              "end Exec\n"
              "end FlowFinance.MexBridge\n", mxf);
        fclose(mxf);

        char mex_cmd[1024];
        snprintf(mex_cmd, sizeof(mex_cmd),
                 "%s/flowc %s -o /tmp/flow_mex_gen.c --target-c-header /tmp/flow_mex_gen.h --target-mex",
                 FLOW_BUILD_DIR, mex_lean);
        FLOW_ASSERT_EQ(system(mex_cmd), 0);

        /* Verify that zero-branch FSM transition table and 2-Jet AD operators are synthesized in .h and .c */
        FILE *chk_h = fopen("/tmp/flow_mex_gen.h", "r");
        FLOW_ASSERT_TRUE(chk_h != NULL);
        char *h_text = (char *)calloc(1, 65536);
        FLOW_ASSERT_TRUE(h_text != NULL);
        (void)fread(h_text, 1, 65535, chk_h);
        fclose(chk_h);
        FLOW_ASSERT_TRUE(strstr(h_text, "static const uint8_t step_order_fsm_transition_table[6][6] = {") != NULL);
        FLOW_ASSERT_TRUE(strstr(h_text, "FlowJetScalar2 flowc_jet2_portfolio_utility(FlowJetScalar2 S, FlowJetScalar2 K)") != NULL);
        FLOW_ASSERT_TRUE(strstr(h_text, "FlowJetScalar2 jet2_portfolio_utility(FlowJetScalar2 S, FlowJetScalar2 K);") != NULL);
        FLOW_ASSERT_TRUE(strstr(h_text, "FLOW_LEAN_IMPLIES_GUARD(") != NULL);
        free(h_text);

        FILE *mex_drv = fopen("/tmp/flow_mex_drv.c", "w");
        FLOW_ASSERT_TRUE(mex_drv != NULL);
        fputs("#include \"bitspace.h\"\n"
              "#include \"/tmp/flow_mex_gen.h\"\n"
              "#include <assert.h>\n"
              "#include <math.h>\n"
              "#include <string.h>\n"
              "typedef struct { int is_str; char str[64]; double val; } MockMx;\n"
              "static MockMx s_scalars[8];\n"
              "static int s_scalar_idx = 0;\n"
              "static int s_mex_err_triggered = 0;\n"
              "int mxGetString(const void *pm, char *str, size_t strlen) {\n"
              "    const MockMx *m = (const MockMx *)pm;\n"
              "    if (!m || !m->is_str) return 1;\n"
              "    strncpy(str, m->str, strlen - 1);\n"
              "    str[strlen - 1] = '\\0';\n"
              "    return 0;\n"
              "}\n"
              "double mxGetScalar(const void *pm) {\n"
              "    const MockMx *m = (const MockMx *)pm;\n"
              "    return m ? m->val : 0.0;\n"
              "}\n"
              "void *mxCreateDoubleScalar(double value) {\n"
              "    MockMx *m = &s_scalars[s_scalar_idx++ & 7];\n"
              "    m->is_str = 0;\n"
              "    m->val = value;\n"
              "    return m;\n"
              "}\n"
              "void mexErrMsgIdAndTxt(const char *id, const char *msg, ...) {\n"
              "    (void)id; (void)msg;\n"
              "    s_mex_err_triggered = 1;\n"
              "}\n"
              "extern void mexFunction(int nlhs, void *plhs[], int nrhs, const void *prhs[]);\n"
              "int main(void) {\n"
              "    /* 1. Coexisting bullish (v > 0) and bearish (v < 0) Material Implication Guards */\n"
              "    MockMx cmd_lean = { .is_str = 1, .str = \"lean_hamiltonian_reynolds\" };\n"
              "    MockMx a_wv = { .val = 2.0 }, a_wa = { .val = 4.0 }, a_v = { .val = -3.0 }, a_a = { .val = -0.5 };\n"
              "    const void *prhs[5] = { &cmd_lean, &a_wv, &a_wa, &a_v, &a_a };\n"
              "    void *plhs[3] = { NULL, NULL, NULL };\n"
              "    mexFunction(1, plhs, 5, prhs);\n"
              "    assert(!s_mex_err_triggered && plhs[0] != NULL);\n"
              "    assert(fabs(mxGetScalar(plhs[0]) - 8.0) < 1e-9);\n"
              "    MockMx a_v_pos = { .val = 3.0 };\n"
              "    const void *prhs_pos[5] = { &cmd_lean, &a_wv, &a_wa, &a_v_pos, &a_a };\n"
              "    mexFunction(1, plhs, 5, prhs_pos);\n"
              "    assert(fabs(mxGetScalar(plhs[0]) - 8.0) < 1e-9);\n"
              "    /* 2. Structural domain precondition from ReynoldsDomain Prop fields */\n"
              "    ReynoldsDomain ok_dom = { .w_v = 2.0, .w_a = 4.0 };\n"
              "    ReynoldsDomain bad_dom = { .w_v = 2.0, .w_a = -1.0 };\n"
              "    assert(fabs(lean_eval_reynolds_domain(ok_dom, -3.0) - 10.0) < 1e-9);\n"
              "    assert(lean_eval_reynolds_domain(bad_dom, -3.0) == 0.0);\n"
              "    /* 3. 2-Jet Forward AD: verify Value (0-jet), Delta (1-jet), and Gamma (2-jet) */\n"
              "    double S = 102.0, K = 100.0;\n"
              "    double d = S - K;\n"
              "    double r = sqrt(d * d + 4.0);\n"
              "    double v_ref = 0.5 * d + 0.5 * r;\n"
              "    double vp_ref = 0.5 + 0.5 * (d / r);\n"
              "    double vpp_ref = 2.0 / (r * r * r);\n"
              "    double u_ref = v_ref * v_ref + 0.1 * S * S * S;\n"
              "    double up_ref = 2.0 * v_ref * vp_ref + 0.3 * S * S;\n"
              "    double upp_ref = 2.0 * (vp_ref * vp_ref + v_ref * vpp_ref) + 0.6 * S;\n"
              "    FlowJetScalar2 j_inl = flowc_jet2_portfolio_utility(flow_jet2_var(S), flow_jet2_const(K));\n"
              "    FlowJetScalar2 j_abi = jet2_portfolio_utility(flow_jet2_var(S), flow_jet2_const(K));\n"
              "    assert(fabs(j_inl.v - u_ref) < 1e-10 && fabs(j_abi.v - u_ref) < 1e-10);\n"
              "    assert(fabs(j_inl.d1 - up_ref) < 1e-10 && fabs(j_abi.d1 - up_ref) < 1e-10);\n"
              "    assert(fabs(j_inl.d2 - upp_ref) < 1e-10 && fabs(j_abi.d2 - upp_ref) < 1e-10);\n"
              "    /* Verify MEX dispatcher for jet2_portfolio_utility returning [v, d1, d2] */\n"
              "    MockMx cmd_jet = { .is_str = 1, .str = \"jet2_portfolio_utility\" };\n"
              "    MockMx mx_S = { .val = S }, mx_K = { .val = K };\n"
              "    const void *prhs_jet[3] = { &cmd_jet, &mx_S, &mx_K };\n"
              "    mexFunction(3, plhs, 3, prhs_jet);\n"
              "    assert(fabs(mxGetScalar(plhs[0]) - u_ref) < 1e-10);\n"
              "    assert(fabs(mxGetScalar(plhs[1]) - up_ref) < 1e-10);\n"
              "    assert(fabs(mxGetScalar(plhs[2]) - upp_ref) < 1e-10);\n"
              "    /* 4. Zero-branch C17 lookup table FSM transitions + no_zombie_order theorem guard */\n"
              "    OrderState st = Idle;\n"
              "    st = lean_step_order_fsm(st, EvSubmit);\n"
              "    assert(st == PendingNew);\n"
              "    st = lean_step_order_fsm(st, EvAck);\n"
              "    assert(st == Working);\n"
              "    st = lean_step_order_fsm(st, EvPartial);\n"
              "    assert(st == PartialFill);\n"
              "    st = lean_step_order_fsm(st, EvCancelReq);\n"
              "    assert(st == PendingCancel);\n"
              "    st = lean_step_order_fsm(st, EvFill);\n"
              "    assert(st == Terminal);\n"
              "    assert(lean_step_order_fsm(Terminal, EvSubmit) == Terminal);\n"
              "    assert(flowc_step_order_fsm(Working, EvCanceled) == Terminal);\n"
              "    return 0;\n"
              "}\n", mex_drv);
        fclose(mex_drv);

        /* Compile & link /tmp/flow_mex_gen.c directly with /tmp/flow_mex_drv.c WITHOUT -DFLOW_SHARED_LIB! */
        FLOW_ASSERT_EQ(system("cc -std=c17 -Wall -Wextra -Wpedantic -Werror -Wmisleading-indentation -O2 -Isrc /tmp/flow_mex_drv.c /tmp/flow_mex_gen.c -lm -lpthread -o /tmp/flow_mex_drv && /tmp/flow_mex_drv"), 0);

        remove(mex_lean);
        remove("/tmp/flow_mex_gen.c");
        remove("/tmp/flow_mex_gen.h");
        remove("/tmp/flow_mex_drv.c");
        remove("/tmp/flow_mex_drv");

        printf("  ✓ Stage 10 Passed: Pure C17 runtime (libflow_rt.a), HoTT U_0, FlowFinance C17 & MATLAB --target-mex verified.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 11: Inductive Knowledge Synthesis Engine (Data -> Solver -> Lean)   */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(11, "Inductive Knowledge Synthesis: Rational Poly, Grid Reflection, SMM & PRNG Seed=42 Audit");
    {
        const size_t N_SAMPLES = 10000;
        double *xs = (double *)malloc(N_SAMPLES * sizeof(double));
        double *ys = (double *)malloc(N_SAMPLES * sizeof(double));
        FLOW_ASSERT_TRUE(xs != NULL && ys != NULL);

        /* 1. Fit 10,000 samples of exact rational polynomial:
         *    f(x) = (3/8)*x^2 - (1/4)*x + (5/16) = 0.375*x^2 - 0.25*x + 0.3125
         */
        for (size_t i = 0; i < N_SAMPLES; ++i) {
            double x = (double)i / (double)(N_SAMPLES - 1);
            xs[i] = x;
            ys[i] = 0.375 * x * x - 0.25 * x + 0.3125;
        }

        FlowInducedPolyModel poly = {0};
        FLOW_ASSERT_TRUE(flow_induce_fit_rational_poly(xs, ys, N_SAMPLES, 2, 1024, &poly));
        FLOW_ASSERT_EQ(poly.coeffs[0].num, 5);
        FLOW_ASSERT_EQ(poly.coeffs[0].den, 16);
        FLOW_ASSERT_EQ(poly.coeffs[1].num, -1);
        FLOW_ASSERT_EQ(poly.coeffs[1].den, 4);
        FLOW_ASSERT_EQ(poly.coeffs[2].num, 3);
        FLOW_ASSERT_EQ(poly.coeffs[2].den, 8);
        FLOW_ASSERT_TRUE(poly.r_squared > 0.9999);
        FLOW_ASSERT_TRUE(poly.epsilon_bound.approx_val >= poly.max_empirical_residual);

        /* 2. Build finite Grid Envelope for computational reflection (by decide) */
        FlowGridEnvelope grid = {0};
        FLOW_ASSERT_TRUE(flow_induce_build_grid_envelope(xs, ys, N_SAMPLES, 4, 100, &grid));
        FLOW_ASSERT_EQ(grid.bucket_count, 4u);
        FLOW_ASSERT_EQ(grid.total_samples, N_SAMPLES);
        FLOW_ASSERT_TRUE(grid.certified_upper_bound > grid.global_y_max);
        FLOW_ASSERT_TRUE(grid.certified_lower_bound < grid.global_y_min);

        /* 3. SMM Inverse Discovery: Cox-Ingersoll-Ross (CIR) & Feller Condition 2ab >= sigma^2 */
        double *cir_samples = (double *)malloc(N_SAMPLES * sizeof(double));
        FLOW_ASSERT_TRUE(cir_samples != NULL);
        /* True parameters: a = 1.8, b = 3.0, sigma = 0.9 => 2ab = 10.8 >= sigma^2 = 0.81 */
        FLOW_ASSERT_TRUE(flow_induce_simulate_cir(1.8, 3.0, 0.9, 3.0, 0.02, 42ULL, cir_samples, N_SAMPLES));

        FlowSMMResult smm_cir = {0};
        FLOW_ASSERT_TRUE(flow_induce_solve_smm(cir_samples, N_SAMPLES, FLOW_SMM_CIR_REFLECTING, 0.02, 42ULL, &smm_cir));
        FLOW_ASSERT_EQ(smm_cir.feller_condition_holds, 1);
        FLOW_ASSERT_TRUE(smm_cir.feller_margin > 0.0);
        FLOW_ASSERT_TRUE(fabs(smm_cir.param_b_or_sigma - 3.0) < 0.35);

        /* 4. SMM Inverse Discovery: Merton Jump-Diffusion on non-Gaussian skewed/fat-tailed returns */
        double *merton_returns = (double *)malloc(N_SAMPLES * sizeof(double));
        FLOW_ASSERT_TRUE(merton_returns != NULL);
        FLOW_ASSERT_TRUE(flow_induce_simulate_merton_jump(0.05, 0.20, 3.0, -0.04, 0.06, 0.01, 42ULL, merton_returns, N_SAMPLES));

        FlowEmpiricalMoments merton_mom = {0};
        FLOW_ASSERT_TRUE(flow_induce_compute_moments(merton_returns, N_SAMPLES, &merton_mom));
        /* Verify non-Gaussian fat tails (excess kurtosis > 0) and negative skewness */
        FLOW_ASSERT_TRUE(merton_mom.excess_kurtosis > 0.5);
        FLOW_ASSERT_TRUE(merton_mom.skewness < -0.1);

        FlowSMMResult smm_merton = {0};
        FLOW_ASSERT_TRUE(flow_induce_solve_smm(merton_returns, N_SAMPLES, FLOW_SMM_MERTON_JUMP, 0.01, 42ULL, &smm_merton));
        FLOW_ASSERT_TRUE(smm_merton.param_b_or_sigma > 0.0);

        /* Verify Integral Probability Metrics (IPM) distances */
        double w1_self = flow_induce_ipm_distance(merton_returns, 1000, merton_returns, 1000, FLOW_IPM_WASSERSTEIN_1);
        double ks_self = flow_induce_ipm_distance(merton_returns, 1000, merton_returns, 1000, FLOW_IPM_KOLMOGOROV_SMIRNOV);
        FLOW_ASSERT_TRUE(w1_self < 1e-12 && ks_self < 1e-12);

        /* 5. Algebraic Cryptanalysis: PRNG Seed=42 Inversion & State Recovery */
        uint64_t prng_words[32];
        uint64_t rng_st = 42ULL;
        /* Advance 7 steps first to test multi-step F_2 backtracking to seed=42! */
        for (int pre = 0; pre < 7; ++pre) {
            (void)flow_prng_next(&rng_st);
        }
        for (size_t i = 0; i < 32; ++i) {
            prng_words[i] = flow_prng_next(&rng_st);
        }
        FlowPRNGAuditResult prng_audit = {0};
        FLOW_ASSERT_TRUE(flow_induce_audit_prng_u64(prng_words, 32, 64, &prng_audit));
        FLOW_ASSERT_EQ(prng_audit.detected, 1);
        FLOW_ASSERT_EQ(prng_audit.family, FLOW_PRNG_XORSHIFT64);
        FLOW_ASSERT_EQ(prng_audit.recovered_initial_seed, 42ULL);
        FLOW_ASSERT_EQ(prng_audit.recovered_step_offset, 7ULL);

        /* Reset to step 0 from seed=42 so theorem witness step1 matches seed=42 directly */
        rng_st = 42ULL;
        for (size_t i = 0; i < 16; ++i) {
            prng_words[i] = flow_prng_next(&rng_st);
        }
        FLOW_ASSERT_TRUE(flow_induce_audit_prng_u64(prng_words, 16, 64, &prng_audit));
        FLOW_ASSERT_EQ(prng_audit.recovered_initial_seed, 42ULL);
        FLOW_ASSERT_EQ(prng_audit.recovered_step_offset, 0ULL);

        /* 6. Synthesize composite Zero-GC Lean 4 specification and verify parse_spec + C17 codegen */
        FILE *lean_mem = tmpfile();
        FLOW_ASSERT_TRUE(lean_mem != NULL);
        FLOW_ASSERT_TRUE(flow_induce_emit_lean_composite(lean_mem, "Flow.InducedKnowledge",
                                                         &poly, &grid, &smm_cir, &prng_audit));
        rewind(lean_mem);

        static FlowSpec ind_spec;
        static SemanticIR ind_ir;
        memset(&ind_spec, 0, sizeof(ind_spec));
        memset(&ind_ir, 0, sizeof(ind_ir));
        FLOW_ASSERT_TRUE(parse_spec(lean_mem, &ind_spec));
        fclose(lean_mem);

        FLOW_ASSERT_STR_EQ(ind_spec.flow_name, "InducedKnowledge");
        FLOW_ASSERT_TRUE(ind_spec.lean_function_count >= 6);
        FLOW_ASSERT_TRUE(ind_spec.lean_theorem_count >= 6);

        /* Verify fitted_poly extracted formal theorem guards from fitted_poly_upper_bound / lower_bound */
        const FlowLeanFunction *fn_poly = NULL;
        for (size_t i = 0; i < ind_spec.lean_function_count; ++i) {
            if (!strcmp(ind_spec.lean_functions[i].name, "fitted_poly")) {
                fn_poly = &ind_spec.lean_functions[i];
                break;
            }
        }
        FLOW_ASSERT_TRUE(fn_poly != NULL);
        FLOW_ASSERT_TRUE(fn_poly->thm_guard_count >= 2);

        /* Verify end-to-end CLI pipeline: flowc --induce <csv> -> C17 + Header */
        const char *csv_tmp = "/tmp/flow_induce_test.csv";
        FILE *cfp = fopen(csv_tmp, "w");
        FLOW_ASSERT_TRUE(cfp != NULL);
        for (size_t i = 0; i < 200; ++i) {
            fprintf(cfp, "%.6f,%.6f\n", xs[i * 50], ys[i * 50]);
        }
        fclose(cfp);

        FLOW_ASSERT_EQ(system("./build/flowc --induce /tmp/flow_induce_test.csv -o /tmp/flow_induce_out.c --target-c-header /tmp/flow_induce_out.h --emit-induced-lean /tmp/flow_induced.lean >/dev/null"), 0);
        FLOW_ASSERT_EQ(system("cc -std=c17 -Wall -Wextra -Wpedantic -Werror -O2 -Isrc -c /tmp/flow_induce_out.c -o /tmp/flow_induce_out.o"), 0);

        remove(csv_tmp);
        remove("/tmp/flow_induce_out.c");
        remove("/tmp/flow_induce_out.h");
        remove("/tmp/flow_induce_out.o");
        remove("/tmp/flow_induced.lean");

        free(xs);
        free(ys);
        free(cir_samples);
        free(merton_returns);
        printf("  ✓ Stage 11 Passed: 10,000-sample Rational Poly (3/8 x^2 - 1/4 x + 5/16), Grid Reflection, SMM CIR/Merton & Seed=42 Audit verified.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 12: Cryptanalysis Plugin (MT19937 Seed=42, PCG32, QMC & PUF Cert)   */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(12, "Cryptanalysis Plugin: MT19937 Untwist Seed=42, PCG32 State Recovery, GF(2), QMC & PUF");
    {
        /* 12a. Resolve & dynamically load build/libflow_cryptanalysis.so in plugin registry */
        FLOW_ASSERT_TRUE(flow_registry_resolve_plugin("flow.cryptanalysis"));
        const FlowPlugin *crypto_plug = flow_registry_lookup("flow.cryptanalysis");
        FLOW_ASSERT_TRUE(crypto_plug != NULL);
        FLOW_ASSERT_STR_EQ(crypto_plug->name, "flow.cryptanalysis");
        FLOW_ASSERT_EQ(crypto_plug->component_count, 1u);
        FLOW_ASSERT_STR_EQ(crypto_plug->components[0].id, "cryptanalysis_engine");

        /* 12b. Compile plugins/cryptanalysis.lean to pure Zero-GC C17 + static inline header */
        char cmd_crypto_lean[1024];
        snprintf(cmd_crypto_lean, sizeof(cmd_crypto_lean),
                 "%s/flowc plugins/cryptanalysis.lean -o /tmp/flow_crypto_pure.c --target-c-header /tmp/flow_crypto_pure.h",
                 FLOW_BUILD_DIR);
        FLOW_ASSERT_EQ(system(cmd_crypto_lean), 0);

        /* 12c. Build & run a full C17 verification driver testing both plugins/cryptanalysis.lean
         *      and plugins/cryptanalysis.c (MT19937 624-word untwist -> seed=42, PCG32 64-bit state
         *      reconstruction, 64x64 GF(2) ANF solver, QMC star-discrepancy, PUF Min-Entropy & Lean cert)
         */
        FILE *c_drv = fopen("/tmp/flow_crypto_drv.c", "w");
        FLOW_ASSERT_TRUE(c_drv != NULL);
        fputs(
            "#include \"cryptanalysis.h\"\n"
            "#include \"/tmp/flow_crypto_pure.h\"\n"
            "#include <assert.h>\n"
            "#include <math.h>\n"
            "#include <stdio.h>\n"
            "#include <string.h>\n"
            "int main(void) {\n"
            "    /* 1. Verify Lean-synthesized static inline MT19937 untemper & init step inversion */\n"
            "    uint64_t t_val = flowc_mt19937_temper(0x12345678ULL);\n"
            "    assert(flowc_mt19937_untemper(t_val) == 0x12345678ULL);\n"
            "    uint64_t s1_from_42 = flowc_mt19937_init_step(42ULL, 1ULL);\n"
            "    assert(flowc_mt19937_invert_init_step(s1_from_42, 1ULL) == 42ULL);\n"
            "    assert(flowc_puf_min_entropy_gate(920ULL, 498ULL, 12ULL, 1100ULL) == 0U);\n"
            "    assert(flowc_puf_min_entropy_gate(350ULL, 498ULL, 12ULL, 1100ULL) == 1U);\n"
            "\n"
            "    /* 2. Full 624-word MT19937 state cloning, untwisting & exact seed=42 recovery */\n"
            "    FlowMT19937State mt_orig;\n"
            "    flow_mt19937_seed(&mt_orig, 42U);\n"
            "    uint32_t obs624[FLOW_MT19937_N];\n"
            "    for (size_t i = 0; i < FLOW_MT19937_N; ++i) {\n"
            "        obs624[i] = flow_mt19937_next(&mt_orig);\n"
            "    }\n"
            "    FlowMT19937State mt_cloned;\n"
            "    assert(flow_mt19937_clone_state(obs624, &mt_cloned) == 1);\n"
            "    for (size_t i = 0; i < 64; ++i) {\n"
            "        assert(flow_mt19937_next(&mt_orig) == flow_mt19937_next(&mt_cloned));\n"
            "    }\n"
            "    FlowMT19937AuditReport mt_rep;\n"
            "    assert(flow_mt19937_recover_seed(obs624, FLOW_MT19937_N, &mt_rep) == 1);\n"
            "    assert(mt_rep.seed_recovered == 1 && mt_rep.recovered_seed == 42U);\n"
            "\n"
            "    /* Also test micro-sample MT19937 seed=42 recovery from just 8 outputs */\n"
            "    FlowMT19937AuditReport mt_short;\n"
            "    assert(flow_mt19937_recover_seed(obs624, 8, &mt_short) == 1);\n"
            "    assert(mt_short.seed_recovered == 1 && mt_short.recovered_seed == 42U);\n"
            "\n"
            "    /* 3. PCG32 (PCG-XSH-RR) 64-bit hidden state recovery from 4 32-bit outputs */\n"
            "    uint64_t pcg_true_state = 0xB5AD4ECEDA1CE2A9ULL & ~0x07FFFFFFULL;\n"
            "    pcg_true_state |= 0x0000BEEFULL; /* 64-bit state with 16-bit low entropy */\n"
            "    uint64_t pcg_sim = pcg_true_state;\n"
            "    uint32_t pcg_obs[6];\n"
            "    for (int i = 0; i < 6; ++i) {\n"
            "        pcg_obs[i] = flow_pcg32_step(&pcg_sim, FLOW_PCG32_DEFAULT_INC);\n"
            "    }\n"
            "    uint64_t pcg_recovered = 0ULL;\n"
            "    assert(flow_pcg32_recover_state(pcg_obs, 6, FLOW_PCG32_DEFAULT_INC, &pcg_recovered) == 1);\n"
            "    assert(pcg_recovered == pcg_true_state);\n"
            "\n"
            "    /* 4. 64x64 GF(2) Gaussian Elimination Solver */\n"
            "    uint64_t gf2_mat[64];\n"
            "    uint64_t secret_vec = 0xCAFEBABE1337BEEFULL;\n"
            "    uint64_t rhs = 0ULL;\n"
            "    for (unsigned r = 0; r < 64; ++r) {\n"
            "        gf2_mat[r] = (1ULL << r) ^ ((r + 1 < 64) ? (1ULL << (r + 1)) : 0ULL) ^ ((r + 7 < 64) ? (1ULL << (r + 7)) : 0ULL);\n"
            "        uint64_t dot = (uint64_t)__builtin_parityll(gf2_mat[r] & secret_vec);\n"
            "        rhs |= (dot << r);\n"
            "    }\n"
            "    uint64_t solved_vec = 0ULL;\n"
            "    size_t rank = 0;\n"
            "    assert(flow_gf2_solve_64x64(gf2_mat, rhs, &solved_vec, &rank) == 1);\n"
            "    assert(rank == 64 && solved_vec == secret_vec);\n", c_drv);
        fputs(
            "    /* 5. Certified QMC Derandomization (Halton 2D Star-Discrepancy < PRNG Discrepancy) */\n"
            "    double qmc_x[256], qmc_y[256];\n"
            "    FlowQMCCertificate qmc_cert;\n"
            "    assert(flow_qmc_generate_2d(256, qmc_x, qmc_y, &qmc_cert) == 1);\n"
            "    assert(qmc_cert.bound_certified == 1);\n"
            "    assert(qmc_cert.empirical_star_discrepancy < qmc_cert.pseudo_random_discrepancy);\n"
            "    assert(qmc_cert.derandomization_gain_ratio > 1.2);\n"
            "\n"
            "    /* 6. Hardware PUF Min-Entropy & Lipschitz SMT Certification */\n"
            "    FlowPUFDevice puf_a, puf_b;\n"
            "    assert(flow_puf_device_init(&puf_a, 101U, 0x1122334455667788ULL, 0.05) == 1);\n"
            "    assert(flow_puf_device_init(&puf_b, 102U, 0x8877665544332211ULL, 0.05) == 1);\n"
            "    uint64_t challenges[512];\n"
            "    uint64_t ch_rng = 0x9E3779B97F4A7C15ULL;\n"
            "    for (size_t i = 0; i < 512; ++i) challenges[i] = flow_prng_next(&ch_rng);\n"
            "    uint8_t puf_resp[512];\n"
            "    FlowPUFCertificate puf_cert;\n"
            "    assert(flow_puf_certify_device(&puf_a, &puf_b, challenges, 512, puf_resp, &puf_cert) == 1);\n"
            "    assert(puf_cert.is_cryptographically_sound == 1);\n"
            "    assert(puf_cert.min_entropy_bits >= 0.70);\n"
            "    FlowSMTProofAttestation puf_smt;\n"
            "    assert(flow_puf_verify_smt(&puf_cert, &puf_smt) == FLOW_SMT_PROVEN_UNSAT);\n"
            "\n"
            "    /* 7. Emit composite Lean 4 Cryptanalysis Certificate */\n"
            "    FILE *lcert = fopen(\"/tmp/flow_crypto_cert.lean\", \"w\");\n"
            "    assert(lcert != NULL);\n"
            "    assert(flow_cryptanalysis_emit_lean_certificate(lcert, \"Flow.CryptoAuditCert\", &mt_rep, &qmc_cert, &puf_cert) == 1);\n"
            "    fclose(lcert);\n"
            "    return 0;\n"
            "}\n", c_drv);
        fclose(c_drv);

        FLOW_ASSERT_EQ(system("cc -std=c17 -Wall -Wextra -Wpedantic -Werror -O2 -Isrc -Iplugins /tmp/flow_crypto_drv.c plugins/cryptanalysis.c build/libflow.a -lm -lpthread -o /tmp/flow_crypto_drv && /tmp/flow_crypto_drv"), 0);

        /* Verify the synthesized /tmp/flow_crypto_cert.lean compiles cleanly through flowc! */
        char cmd_cert_compile[1024];
        snprintf(cmd_cert_compile, sizeof(cmd_cert_compile),
                 "%s/flowc /tmp/flow_crypto_cert.lean -o /tmp/flow_crypto_cert.c --target-c-header /tmp/flow_crypto_cert.h >/dev/null",
                 FLOW_BUILD_DIR);
        FLOW_ASSERT_EQ(system(cmd_cert_compile), 0);

        remove("/tmp/flow_crypto_pure.c");
        remove("/tmp/flow_crypto_pure.h");
        remove("/tmp/flow_crypto_drv.c");
        remove("/tmp/flow_crypto_drv");
        remove("/tmp/flow_crypto_cert.lean");
        remove("/tmp/flow_crypto_cert.c");
        remove("/tmp/flow_crypto_cert.h");

        printf("  ✓ Stage 12 Passed: Cryptanalysis plugin (.so + .lean), MT19937 624-word Untwist -> seed=42, PCG32 64-bit recovery, GF(2), QMC & PUF verified.\n\n");
    }

    /* ======================================================================
     * STAGE 13: HoTT Witness Types, Univalence SIMD SOA & Phase Attention
     * ====================================================================== */
    {
        printf("--- [Stage 13: HoTT Witness Types (Omega), Univalence SIMD SOA64 & Phase-Space Attention] ---\n");
        const char *lean_hott_att =
            "-- Pure Lean 4 Quant Strategy with Subobject Classifier Invariants & SOA Lifting\n"
            "structure OrderSlot where\n"
            "  order_id : UInt64\n"
            "  price : Float\n"
            "  qty : Float\n"
            "  solvency_headroom : Float\n"
            "  h_solvency : solvency_headroom >= 0.20\n"
            "  h_qty : qty > 0.0\n"
            "\n"
            "structure JetState where\n"
            "  strike : Float\n"
            "  iv : Float\n"
            "  tte : Float\n"
            "  solvency : Float\n"
            "\n"
            "def build_order (oid : UInt64) (p : Float) (q : Float) (solv : Float) (h_solv : solv >= 0.20) (h_q : q > 0.0) : OrderSlot :=\n"
            "  { order_id := oid, price := p, qty := q, solvency_headroom := solv }\n"
            "\n"
            "def vega_snipe (s : JetState) (vol_floor : Float) : Float :=\n"
            "  let eff_iv := if s.iv > vol_floor then s.iv else vol_floor\n"
            "  in s.strike * eff_iv * Float.sqrt s.tte\n"
            "\n"
            "theorem vega_snipe_nonneg (s : JetState) (vol_floor : Float) (h_s : s.strike >= 0.0) (h_v : vol_floor >= 0.0) (h_t : s.tte >= 0.0) :\n"
            "  vega_snipe s vol_floor >= 0.0 := by\n"
            "  trivial\n";

        FILE *lf = fopen("/tmp/flow_hott_attention.lean", "w");
        FLOW_ASSERT_TRUE(lf != NULL);
        fputs(lean_hott_att, lf);
        fclose(lf);

        char cmd_compile[1024];
        snprintf(cmd_compile, sizeof(cmd_compile),
                 "%s/flowc /tmp/flow_hott_attention.lean -o /tmp/flow_hott_attention.c --target-c-header /tmp/flow_hott_attention.h >/dev/null",
                 FLOW_BUILD_DIR);
        FLOW_ASSERT_EQ(system(cmd_compile), 0);

        /* Verify generated header contains Witness Types and SIMD SOA64 constructs */
        FILE *hf = fopen("/tmp/flow_hott_attention.h", "r");
        FLOW_ASSERT_TRUE(hf != NULL);
        char hbuf[65536];
        memset(hbuf, 0, sizeof(hbuf));
        (void)fread(hbuf, 1, sizeof(hbuf) - 1, hf);
        fclose(hf);

        FLOW_ASSERT_TRUE(strstr(hbuf, "lean_certified_OrderSlot") != NULL);
        FLOW_ASSERT_TRUE(strstr(hbuf, "flowc_certify_OrderSlot") != NULL);
        FLOW_ASSERT_TRUE(strstr(hbuf, "lean_certified_build_order") != NULL);
        FLOW_ASSERT_TRUE(strstr(hbuf, "JetState_SOA64") != NULL);
        FLOW_ASSERT_TRUE(strstr(hbuf, "flowc_ua_lift_to_soa_JetState") != NULL);
        FLOW_ASSERT_TRUE(strstr(hbuf, "flowc_soa_vega_snipe") != NULL);
        FLOW_ASSERT_TRUE(strstr(hbuf, "soa_vega_snipe") != NULL);

        /* Write C17 driver testing Witness Types, SOA64 batch execution, and Phase-Space Attention */
        FILE *drv = fopen("/tmp/flow_hott_att_drv.c", "w");
        FLOW_ASSERT_TRUE(drv != NULL);
        fputs(
            "#include \"/tmp/flow_hott_attention.h\"\n"
            "#include \"flow_jet.h\"\n"
            "#include <assert.h>\n"
            "#include <math.h>\n"
            "#include <stdio.h>\n"
            "\n"
            "/* Hardware Gateway that ONLY accepts compile-time unforgeable lean_certified_OrderSlot */\n"
            "static uint64_t g_gateway_orders_sent = 0;\n"
            "static int flow_kgi_gateway_send_order(lean_certified_OrderSlot certified_token) {\n"
            "    OrderSlot slot = flowc_uncertify_OrderSlot(certified_token);\n"
            "    assert(slot.solvency_headroom >= 0.20);\n"
            "    assert(slot.qty > 0.0);\n"
            "    g_gateway_orders_sent++;\n"
            "    return 1;\n"
            "}\n"
            "\n"
            "static int regime_calm_mm(FlowJet *jet, const void *in, void *out) {\n"
            "    (void)jet; (void)in;\n"
            "    if (out) *(int *)out = 10;\n"
            "    return 10;\n"
            "}\n"
            "static int regime_tmf_waterfall(FlowJet *jet, const void *in, void *out) {\n"
            "    (void)jet; (void)in;\n"
            "    if (out) *(int *)out = 20;\n"
            "    return 20;\n"
            "}\n"
            "static int regime_emergency_fold(FlowJet *jet, const void *in, void *out) {\n"
            "    (void)jet; (void)in;\n"
            "    if (out) *(int *)out = 99;\n"
            "    return 99;\n"
            "}\n", drv);
        fputs(
            "int main(void) {\n"
            "    /* 1. Subobject Classifier (Omega) -> Witness Type (Sigma x:A, P(x)) Verification */\n"
            "    lean_certified_OrderSlot witness_slot;\n"
            "    /* Solvency 0.12 < 0.20 must be rejected by Subobject Classifier */\n"
            "    assert(lean_certified_build_order(1001ULL, 500.0, 2.0, 0.12, &witness_slot) == false);\n"
            "    OrderSlot raw_bad = { .order_id = 1001ULL, .price = 500.0, .qty = 2.0, .solvency_headroom = 0.15 };\n"
            "    assert(lean_certify_OrderSlot(raw_bad, &witness_slot) == false);\n"
            "    /* Solvency 0.35 >= 0.20 and qty 2.0 > 0.0 succeeds and yields unforgeable token */\n"
            "    assert(lean_certified_build_order(1002ULL, 500.0, 2.0, 0.35, &witness_slot) == true);\n"
            "    assert(flow_kgi_gateway_send_order(witness_slot) == 1);\n"
            "    assert(g_gateway_orders_sent == 1ULL);\n"
            "\n"
            "    /* 2. HoTT Univalence AOS[64] <-> SIMD SOA64 Automatic Transport & Vectorized Kernel */\n"
            "    FlowHoTTUniverse *u0 = flow_hott_universe_global();\n"
            "    assert(flow_hott_register_soa_equivalence(u0, \"JetState\", \"JetState_SOA64\", 4, sizeof(JetState), FLOW_SIMD_SOA_LANES, 0U, FLOW_SIEVE_SECURITY_CANONICAL_MASK) == 1);\n"
            "    uint64_t ua_path = 0ULL;\n"
            "    assert(flow_hott_verify_soa_univalence(u0, \"JetState\", \"JetState_SOA64\", FLOW_SIMD_SOA_LANES, &ua_path) == FLOW_KAN_FILLED_HOMOTOPIC);\n"
            "    assert(ua_path != 0ULL);\n"
            "\n"
            "    JetState aos_chain[64];\n"
            "    for (size_t i = 0; i < 64; ++i) {\n"
            "        aos_chain[i].strike = 18000.0 + (double)i * 100.0;\n"
            "        aos_chain[i].iv = 0.10 + (double)(i % 10) * 0.03;\n"
            "        aos_chain[i].tte = 0.25;\n"
            "        aos_chain[i].solvency = 0.40;\n"
            "    }\n"
            "    JetState_SOA64 soa_chain;\n"
            "    flowc_ua_lift_to_soa_JetState(aos_chain, 64, &soa_chain);\n"
            "    double simd_out[64] = {0};\n"
            "    soa_vega_snipe(&soa_chain, 0.18, simd_out);\n"
            "    for (size_t i = 0; i < 64; ++i) {\n"
            "        double scalar_ref = vega_snipe(aos_chain[i], 0.18);\n"
            "        assert(fabs(simd_out[i] - scalar_ref) < 1e-12);\n"
            "    }\n"
            "    JetState roundtrip[64];\n"
            "    flowc_ua_lower_from_soa_JetState(&soa_chain, 64, roundtrip);\n"
            "    for (size_t i = 0; i < 64; ++i) {\n"
            "        assert(fabs(roundtrip[i].strike - aos_chain[i].strike) < 1e-12);\n"
            "        assert(fabs(roundtrip[i].iv - aos_chain[i].iv) < 1e-12);\n"
            "    }\n", drv);
        fputs(
            "    /* 3. Phase-Space Attention Router (Q, K_i, V_i) + Moreau Hysteresis + Kan Handover */\n"
            "    FlowPhaseAttentionRouter router;\n"
            "    assert(flow_phase_attention_init(&router, 2, 2U) == 1);\n"
            "    /* Plane 0: H_reynolds <= 40.0 with Moreau Hysteresis [38.0, 42.0] */\n"
            "    double c_p0[2] = { 1.0, 0.0 };\n"
            "    assert(flow_phase_attention_set_plane(&router, 0, c_p0, 40.0, true, 38.0, 42.0) == 1);\n"
            "    /* Plane 1: -Solvency <= -0.20 (i.e. Solvency >= 0.20) */\n"
            "    double c_p1[2] = { 0.0, -1.0 };\n"
            "    assert(flow_phase_attention_set_plane(&router, 1, c_p1, -0.20, false, 0.0, 0.0) == 1);\n"
            "\n"
            "    FlowPhaseAttentionHead h_calm = {\n"
            "        .name = \"calm_mm\", .key_mask = 0x3ULL, .key_polarity = 0x3ULL,\n"
            "        .state_sieve = 0x11ULL, .kan_boundary_filter = 0x01ULL, .priority = 10, .kernel = regime_calm_mm\n"
            "    };\n"
            "    FlowPhaseAttentionHead h_tmf = {\n"
            "        .name = \"tmf_waterfall_ioc\", .key_mask = 0x3ULL, .key_polarity = 0x2ULL,\n"
            "        .state_sieve = 0x13ULL, .kan_boundary_filter = 0x01ULL, .priority = 20, .kernel = regime_tmf_waterfall\n"
            "    };\n"
            "    FlowPhaseAttentionHead h_emerg = {\n"
            "        .name = \"emergency_fold\", .key_mask = 0x2ULL, .key_polarity = 0x0ULL,\n"
            "        .state_sieve = 0x01ULL, .kan_boundary_filter = 0x01ULL, .priority = 100, .kernel = regime_emergency_fold\n"
            "    };\n"
            "    uint32_t idx_calm = 0, idx_tmf = 0, idx_emerg = 0;\n"
            "    assert(flow_phase_attention_register_head(&router, &h_calm, &idx_calm) == 1);\n"
            "    assert(flow_phase_attention_register_head(&router, &h_tmf, &idx_tmf) == 1);\n"
            "    assert(flow_phase_attention_register_head(&router, &h_emerg, &idx_emerg) == 1);\n"
            "\n"
            "    FlowJet jet;\n"
            "    flow_jet_init(&jet, \"quant_jet\", \"PhaseAttentionTest\");\n"
            "    int action = 0; uint32_t sel = 99;\n"
            "    /* Step A: Calm regime (H=25.0 <= 38.0, Solvency=0.35 >= 0.20) -> calm_mm */\n"
            "    double q1[8] = { 25.0, 0.35, 0 };\n"
            "    assert(flow_phase_attention_route_and_step(&router, &jet, q1, NULL, &action, &sel) == 10);\n"
            "    assert(sel == idx_calm && action == 10);\n"
            "    /* Step B: Flutter inside Moreau deadband (H=41.0 in [38, 42]) -> stays in calm_mm! */\n"
            "    double q2[8] = { 41.0, 0.35, 0 };\n"
            "    assert(flow_phase_attention_route_and_step(&router, &jet, q2, NULL, &action, &sel) == 10);\n"
            "    assert(sel == idx_calm && router.hysteresis_flutters_suppressed == 1ULL);\n"
            "    /* Step C: Breakout above deadband (H=48.0 >= 42.0) -> homotopic switch to tmf_waterfall_ioc */\n"
            "    double q3[8] = { 48.0, 0.35, 0 };\n"
            "    assert(flow_phase_attention_route_and_step(&router, &jet, q3, NULL, &action, &sel) == 20);\n"
            "    assert(sel == idx_tmf && router.context_switches == 1ULL);\n"
            "    /* Step D: Solvency breach (Solvency=0.10 < 0.20) -> O(1) switch to emergency_fold */\n"
            "    double q4[8] = { 48.0, 0.10, 0 };\n"
            "    assert(flow_phase_attention_route_and_step(&router, &jet, q4, NULL, &action, &sel) == 99);\n"
            "    assert(sel == idx_emerg);\n"
            "    /* Step E: Kan Open-Box Singularity Obstruction -> blocked & diverted to emergency_fold */\n"
            "    FlowPhaseAttentionHead h_bad_kan = {\n"
            "        .name = \"rogue_non_homotopic\", .key_mask = 0x3ULL, .key_polarity = 0x3ULL,\n"
            "        .state_sieve = 0x00ULL, .kan_boundary_filter = 0x01ULL, .priority = 50, .kernel = regime_calm_mm\n"
            "    };\n"
            "    uint32_t idx_bad = 0;\n"
            "    assert(flow_phase_attention_register_head(&router, &h_bad_kan, &idx_bad) == 1);\n"
            "    double q5[8] = { 20.0, 0.40, 0 };\n"
            "    assert(flow_phase_attention_route_and_step(&router, &jet, q5, NULL, &action, &sel) == 99);\n"
            "    assert(sel == idx_emerg && router.kan_singularity_blocks == 1ULL);\n"
            "    return 0;\n"
            "}\n", drv);
        fclose(drv);

        FLOW_ASSERT_EQ(system("cc -std=c17 -Wall -Wextra -Wpedantic -Werror -O2 -Isrc /tmp/flow_hott_att_drv.c /tmp/flow_hott_attention.c build/libflow.a -lm -lpthread -o /tmp/flow_hott_att_drv && /tmp/flow_hott_att_drv"), 0);

        remove("/tmp/flow_hott_attention.lean");
        remove("/tmp/flow_hott_attention.c");
        remove("/tmp/flow_hott_attention.h");
        remove("/tmp/flow_hott_att_drv.c");
        remove("/tmp/flow_hott_att_drv");

        printf("  ✓ Stage 13 Passed: Witness Types (lean_certified_<T>), Univalence SOA64 (flowc_soa_<fn>) & Phase-Space Attention Router verified.\n\n");
    }

    /* ======================================================================
     * STAGE 14: Differential Cohesive HoTT, Discrete 4-Jet (J^4 E) & Homotopy Fiber
     * ====================================================================== */
    {
        printf("--- [Stage 14: Differential Cohesive HoTT, Discrete 4-Jet (J^4 E) & Homotopy Fiber Synthesis] ---\n");
        double bmf_section[32];
        for (size_t i = 0; i < 32; ++i) {
            bmf_section[i] = 1.0 + 0.25 * cos((double)i * 0.19634954084936207);
        }
        double rk_min[5] = { -10.0, -5.0, -5.0, -5.0, -5.0 };
        double rk_max[5] = {  10.0,  5.0,  5.0,  5.0,  5.0 };

        FlowDiscreteJetBundle jbundle;
        FLOW_ASSERT_EQ(flow_jet_bundle_prolong_bmf(bmf_section, 32, rk_min, rk_max, &jbundle), 1);
        FLOW_ASSERT_EQ(jbundle.is_holonomic_section, 1);
        FLOW_ASSERT_TRUE(jbundle.cartan_kahler_defect <= 1e-12);

        /* Diagnose 0-Type multi-solution fiber and collapse to (-2)-Type Contractible */
        FlowHomotopyFiberReport hfiber;
        FLOW_ASSERT_EQ(flow_hott_analyze_homotopy_fiber(0x07ULL, 0x3FULL, 0x07ULL, 0x10ULL, &hfiber), 1);
        FLOW_ASSERT_EQ(hfiber.h_level, FLOW_HLEVEL_GROUPOID_SYMMETRY);
        uint64_t w_sieve = 0ULL, pinned = 0ULL;
        FLOW_ASSERT_EQ(flow_hott_collapse_fiber_contractible(&hfiber, &w_sieve, &pinned), 1);
        FLOW_ASSERT_EQ(flow_hott_analyze_homotopy_fiber(0x07ULL, 0x3FULL, pinned, 0x10ULL, &hfiber), 1);
        FLOW_ASSERT_EQ(hfiber.h_level, FLOW_HLEVEL_CONTRACTIBLE);
        FLOW_ASSERT_EQ(hfiber.pi0_components, 1U);

        /* Emit Commutative Cube & Bundle Projection Diagram */
        FILE *md_fp = fopen("/tmp/flow_cohesive_cube.md", "w");
        FLOW_ASSERT_TRUE(md_fp != NULL);
        FLOW_ASSERT_EQ(flow_hott_emit_commutative_diagram(md_fp, &jbundle, &hfiber, "FlowCohesiveJet"), 1);
        fclose(md_fp);

        /* Emit Zero-Sorry Lean 4 specification and compile via flowc with --fiber-diagram */
        FILE *lean_fp = fopen("/tmp/flow_cohesive_jet.lean", "w");
        FLOW_ASSERT_TRUE(lean_fp != NULL);
        FLOW_ASSERT_EQ(flow_hott_emit_lean_fiber_jet_spec(lean_fp, &jbundle, &hfiber, "FlowCohesiveJet"), 1);
        fclose(lean_fp);

        char cmd_cohesive[1024];
        snprintf(cmd_cohesive, sizeof(cmd_cohesive),
                 "%s/flowc /tmp/flow_cohesive_jet.lean -o /tmp/flow_cohesive_jet.c --target-c-header /tmp/flow_cohesive_jet.h --fiber-diagram /tmp/flow_cli_cube.md >/dev/null",
                 FLOW_BUILD_DIR);
        FLOW_ASSERT_EQ(system(cmd_cohesive), 0);

        /* Compile and run a C17 test verifying the synthesized DiscreteJet4, SOA64 & HomotopyFiberWitness */
        FILE *cj_drv = fopen("/tmp/flow_cohesive_drv.c", "w");
        FLOW_ASSERT_TRUE(cj_drv != NULL);
        fputs(
            "#include \"/tmp/flow_cohesive_jet.h\"\n"
            "#include <assert.h>\n"
            "#include <math.h>\n"
            "int main(void) {\n"
            "    DiscreteJet4 j = { .j0_val = 1.0, .j1_grad = 0.2, .j2_curv = -0.1, .j3_skew = 0.05, .j4_kurt = 0.02 };\n"
            "    double rk_e = lean_eval_rk_subbundle(j);\n"
            "    assert(rk_e >= 0.0);\n"
            "    HomotopyFiberWitness w_contr = { .witness_sieve = 7.0, .pi0_count = 1.0, .pi1_rank = 0.0, .j4_energy = rk_e };\n"
            "    assert(lean_fiber_is_contractible(w_contr) == true);\n"
            "    DiscreteJet4 jets[64];\n"
            "    for (int i = 0; i < 64; ++i) jets[i] = j;\n"
            "    DiscreteJet4_SOA64 soa_jets;\n"
            "    flowc_ua_lift_to_soa_DiscreteJet4(jets, 64, &soa_jets);\n"
            "    double out_rk[64] = {0};\n"
            "    soa_eval_rk_subbundle(&soa_jets, out_rk);\n"
            "    for (int i = 0; i < 64; ++i) assert(fabs(out_rk[i] - rk_e) < 1e-12);\n"
            "    return 0;\n"
            "}\n", cj_drv);
        fclose(cj_drv);

        FLOW_ASSERT_EQ(system("cc -std=c17 -Wall -Wextra -Wpedantic -Werror -O2 -Isrc /tmp/flow_cohesive_drv.c /tmp/flow_cohesive_jet.c build/libflow.a -lm -lpthread -o /tmp/flow_cohesive_drv && /tmp/flow_cohesive_drv"), 0);

        remove("/tmp/flow_cohesive_cube.md");
        remove("/tmp/flow_cli_cube.md");
        remove("/tmp/flow_cohesive_jet.lean");
        remove("/tmp/flow_cohesive_jet.c");
        remove("/tmp/flow_cohesive_jet.h");
        remove("/tmp/flow_cohesive_drv.c");
        remove("/tmp/flow_cohesive_drv");

        printf("  ✓ Stage 14 Passed: Differential Cohesive HoTT, Discrete 4-Jet (J^4 E) & Zero-Sorry Homotopy Fiber verified.\n\n");
    }

    /* ---------------------------------------------------------------------- */
    /* Stage 15: HoTT Boundary Pullback (dM), SpaceWithException & flowy why  */
    /* ---------------------------------------------------------------------- */
    FLOW_STAGE_BEGIN(15, "HoTT Boundary Pullback (dM), Option/FlowResult/SpaceWithException & flowy why");
    {
        const char *hott_err_lean = "/tmp/flow_hott_boundary.lean";
        FILE *hb_fp = fopen(hott_err_lean, "w");
        FLOW_ASSERT_TRUE(hb_fp != NULL);
        fputs(
            "namespace FlowHoTTBoundary\n\n"
            "inductive FlowResult (α : Type) where\n"
            "  | interior : α → FlowResult α\n"
            "  | boundary : Nat → FlowResult α\n\n"
            "inductive SpaceWithException (M : Type) (Exc : Prop) where\n"
            "  | point    : M → SpaceWithException M Exc\n"
            "  | collapse : SpaceWithException M Exc\n\n"
            "def safe_reciprocal (x : Float) (h_pos : x > 0.0) : FlowResult Float :=\n"
            "  if x > 0.001 then interior (1.0 / x) else boundary 77\n\n"
            "def recover_or_default (r : FlowResult Float) (fallback : Float) : Float :=\n"
            "  match r with\n"
            "  | interior v => v + 1.0\n"
            "  | boundary b => fallback\n\n"
            "def option_scale (opt_x : Option Float) (gain : Float) : Option Float :=\n"
            "  match opt_x with\n"
            "  | some v => if v >= 0.0 then some (v * gain) else none\n"
            "  | none => none\n\n"
            "theorem safe_reciprocal_bounded (x : Float) :\n"
            "    x >= 0.01 → safe_reciprocal x <= 100.0 := by\n"
            "  decide\n\n"
            "end FlowHoTTBoundary\n",
            hb_fp);
        fclose(hb_fp);

        char cmd_hb[1024];
        snprintf(cmd_hb, sizeof(cmd_hb),
                 "%s/flowc %s -o /tmp/flow_hott_boundary.c --target-c-header /tmp/flow_hott_boundary.h",
                 FLOW_BUILD_DIR, hott_err_lean);
        FLOW_ASSERT_EQ(system(cmd_hb), 0);

        FILE *hb_drv = fopen("/tmp/flow_hott_boundary_drv.c", "w");
        FLOW_ASSERT_TRUE(hb_drv != NULL);
        fputs(
            "#include \"/tmp/flow_hott_boundary.h\"\n"
            "#include <assert.h>\n"
            "#include <math.h>\n"
            "#include <stdio.h>\n"
            "#include <string.h>\n"
            "int main(void) {\n"
            "    /* 1. Interior evaluation (x = 0.5 -> 1/0.5 = 2.0 <= 100.0) */\n"
            "    double v_ok = lean_boundary_safe_reciprocal(0.5);\n"
            "    assert(!flowc_is_boundary_nan(v_ok));\n"
            "    assert(fabs(v_ok - 2.0) < 1e-12);\n"
            "    assert(fabs(recover_or_default(v_ok, -99.0) - 3.0) < 1e-12);\n\n"
            "    /* 2. Explicit boundary 77 (0 < x <= 0.001) & branchless FPU propagation */\n"
            "    double v_b77 = lean_boundary_safe_reciprocal(0.0005);\n"
            "    assert(flowc_is_boundary_nan(v_b77));\n"
            "    double v_prop = (v_b77 * 8.0 - 3.0) / 2.0;\n"
            "    assert(flowc_is_boundary_nan(v_prop));\n"
            "    assert(flowc_boundary_code(v_prop) == 77U);\n"
            "    assert(fabs(recover_or_default(v_prop, 42.0) - 42.0) < 1e-12);\n\n"
            "    /* 3. Precondition violation (x = -1.0 violates h_pos : x > 0.0) */\n"
            "    lean_result_double r_pre = lean_try_safe_reciprocal(-1.0);\n"
            "    assert(r_pre.boundary_tag != 0ULL);\n"
            "    const char *fn_n = NULL, *thm_n = NULL, *grd_e = NULL;\n"
            "    uint8_t k = 0, ax = 0; uint16_t code = 0;\n"
            "    assert(flowc_decode_boundary_tag(r_pre.boundary_tag, &fn_n, &thm_n, &grd_e, &k, &ax, &code) == true);\n"
            "    assert(strcmp(fn_n, \"safe_reciprocal\") == 0);\n"
            "    assert(strcmp(thm_n, \"safe_reciprocal_precond\") == 0);\n"
            "    assert(k == (uint8_t)FLOW_BOUNDARY_PRECOND && ax == 51U);\n\n"
            "    /* 4. Option Float pattern matching (some / none) */\n"
            "    double opt_ok = option_scale(3.5, 2.0);\n"
            "    assert(!flowc_is_boundary_nan(opt_ok) && fabs(opt_ok - 7.0) < 1e-12);\n"
            "    double opt_neg = option_scale(-2.0, 2.0);\n"
            "    assert(flowc_is_boundary_nan(opt_neg));\n"
            "    double opt_chain = option_scale(opt_neg, 5.0);\n"
            "    assert(flowc_is_boundary_nan(opt_chain));\n\n"
            "    /* Write precondition boundary tag hex to file for flowy why CLI verification */\n"
            "    FILE *tf = fopen(\"/tmp/flow_btag.txt\", \"w\");\n"
            "    assert(tf != NULL);\n"
            "    fprintf(tf, \"0x%016llx\", (unsigned long long)r_pre.boundary_tag);\n"
            "    fclose(tf);\n"
            "    return 0;\n"
            "}\n",
            hb_drv);
        fclose(hb_drv);

        FLOW_ASSERT_EQ(system("cc -std=c17 -Wall -Wextra -Wpedantic -Werror -O2 -Isrc /tmp/flow_hott_boundary_drv.c /tmp/flow_hott_boundary.c build/libflow.a -lm -lpthread -o /tmp/flow_hott_boundary_drv && /tmp/flow_hott_boundary_drv"), 0);

        /* Verify flowy why <hex> <spec.lean> decodes the exact function and guard */
        char hex_tag[64] = {0};
        FILE *tf = fopen("/tmp/flow_btag.txt", "r");
        FLOW_ASSERT_TRUE(tf != NULL);
        FLOW_ASSERT_TRUE(fgets(hex_tag, sizeof(hex_tag), tf) != NULL);
        fclose(tf);

        char why_cmd[1024];
        snprintf(why_cmd, sizeof(why_cmd),
                 "%s/flowy why %s %s > /tmp/flow_why_out.txt",
                 FLOW_BUILD_DIR, hex_tag, hott_err_lean);
        FLOW_ASSERT_EQ(system(why_cmd), 0);

        FILE *wf = fopen("/tmp/flow_why_out.txt", "r");
        FLOW_ASSERT_TRUE(wf != NULL);
        char why_buf[2048] = {0};
        size_t nread = fread(why_buf, 1, sizeof(why_buf) - 1, wf);
        fclose(wf);
        FLOW_ASSERT_TRUE(nread > 0);
        FLOW_ASSERT_TRUE(strstr(why_buf, "safe_reciprocal") != NULL);
        FLOW_ASSERT_TRUE(strstr(why_buf, "PRECONDITION_VIOLATION") != NULL);

        /* Verify flowy patch synthesizes a formally verified white-box .lean patch from dM boundary tag */
        char patch_cmd[1024];
        snprintf(patch_cmd, sizeof(patch_cmd),
                 "%s/flowy patch %s -b %s -o /tmp/flow_hott_patched.lean > /dev/null && "
                 "%s/flowc /tmp/flow_hott_patched.lean -o /tmp/flow_hott_patched.c --target-c-header /tmp/flow_hott_patched.h > /dev/null",
                 FLOW_BUILD_DIR, hott_err_lean, hex_tag, FLOW_BUILD_DIR);
        FLOW_ASSERT_EQ(system(patch_cmd), 0);

        FILE *pf = fopen("/tmp/flow_hott_patched.lean", "r");
        FLOW_ASSERT_TRUE(pf != NULL);
        char p_buf[4096] = {0};
        (void)fread(p_buf, 1, sizeof(p_buf) - 1, pf);
        fclose(pf);
        FLOW_ASSERT_TRUE(strstr(p_buf, "-- Provenance           : flowy v") != NULL);

        FILE *phf = fopen("/tmp/flow_hott_patched.h", "r");
        FLOW_ASSERT_TRUE(phf != NULL);
        char ph_buf[4096] = {0};
        (void)fread(ph_buf, 1, sizeof(ph_buf) - 1, phf);
        fclose(phf);
        FLOW_ASSERT_TRUE(strstr(ph_buf, "FLOW_GENERATED_BY_VERSION") != NULL);
        FLOW_ASSERT_TRUE(strstr(ph_buf, "FLOW_GENERATED_BY_GIT_REV") != NULL);

        remove(hott_err_lean);
        remove("/tmp/flow_hott_boundary.c");
        remove("/tmp/flow_hott_boundary.h");
        remove("/tmp/flow_hott_boundary_drv.c");
        remove("/tmp/flow_hott_boundary_drv");
        remove("/tmp/flow_btag.txt");
        remove("/tmp/flow_why_out.txt");
        remove("/tmp/flow_hott_patched.lean");
        remove("/tmp/flow_hott_patched.c");
        remove("/tmp/flow_hott_patched.h");

        printf("  ✓ Stage 15 Passed: HoTT Boundary Pullback (dM), SpaceWithException, IEEE-754 Quiet NaN & flowy why verified.\n\n");
    }

    FLOW_TEST_SUITE_END();
    return 0;
}
