#include "flow_test_kit.h"
#include "flow.h"
#include "registry.h"
#include "smt.h"
#include "flowy.h"
#include "abi.h"
#include "backend.h"
#include "reload.h"
#include "flow_jet.h"
#include "flow_spsc.h"
#include "cubical_hott.h"
#include "polyhedral.h"

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
    FLOW_STAGE_BEGIN(5, "Pure .lean Embodied Reflex (plugins/embodied.lean) & AWS Cedar Authorization in C17");
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

        /* 5b. Real-World Acceptance: AWS Cedar Authorization Engine (tests/cedar_authorization.lean) in C17 */
        FILE *cedar_fp = fopen("tests/cedar_authorization.lean", "r");
        FLOW_ASSERT_TRUE(cedar_fp != NULL);
        static FlowSpec cedar_spec;
        memset(&cedar_spec, 0, sizeof(cedar_spec));
        FLOW_ASSERT_TRUE(parse_spec(cedar_fp, &cedar_spec));
        fclose(cedar_fp);

        FLOW_ASSERT_EQ(cedar_spec.lean_certified, 1);
        FLOW_ASSERT_EQ(cedar_spec.state_field_count, 7);
        FLOW_ASSERT_EQ(cedar_spec.input_stride_bytes, 56);
        FLOW_ASSERT_TRUE(cedar_spec.lean_enum_count >= 1);
        FLOW_ASSERT_TRUE(cedar_spec.lean_struct_count >= 2);

        static SemanticIR cedar_ir;
        memset(&cedar_ir, 0, sizeof(cedar_ir));
        lower_to_ir(&cedar_spec, &cedar_ir);
        const Component *cedar_comp = select_component(&cedar_ir);
        FLOW_ASSERT_TRUE(cedar_comp != NULL);
        VerificationReport cedar_vrep;
        FLOW_ASSERT_TRUE(verify_candidate(&cedar_ir, cedar_comp, NULL, &cedar_vrep));

        const char *cedar_c = "/tmp/flow_cedar_dso.c";
        const char *cedar_so = "/tmp/libflow_cedar_dso.so";
        FILE *cc_fp = fopen(cedar_c, "w");
        FLOW_ASSERT_TRUE(cc_fp != NULL);
        FLOW_ASSERT_TRUE(emit_c(cc_fp, &cedar_ir, cedar_comp, NULL, &cedar_vrep, 0));
        fclose(cc_fp);

        FLOW_ASSERT_EQ(system("cc -std=c17 -Wall -Wextra -Werror -O2 -shared -fPIC -DFLOW_SHARED_LIB /tmp/flow_cedar_dso.c -o /tmp/libflow_cedar_dso.so -lpthread"), 0);
        void *cdl = dlopen(cedar_so, RTLD_NOW);
        FLOW_ASSERT_TRUE(cdl != NULL);
        typedef void *(*create_fn_t)(void);
        typedef void (*destroy_fn_t)(void *);
        typedef int32_t (*proc_desc_fn_t)(void *, const FlowExtentDescriptor *, FlowExtentDescriptor *);
        create_fn_t fn_cr = NULL;
        destroy_fn_t fn_de = NULL;
        proc_desc_fn_t fn_pd = NULL;
        void *s_cr = dlsym(cdl, "flow_linear_array_create");
        void *s_de = dlsym(cdl, "flow_linear_array_destroy");
        void *s_pd = dlsym(cdl, "flow_linear_array_process_descriptor");
        FLOW_ASSERT_TRUE(s_cr != NULL && s_de != NULL && s_pd != NULL);
        memcpy(&fn_cr, &s_cr, sizeof(fn_cr));
        memcpy(&fn_de, &s_de, sizeof(fn_de));
        memcpy(&fn_pd, &s_pd, sizeof(fn_pd));

        void *ch = fn_cr();
        FLOW_ASSERT_TRUE(ch != NULL);
        FlowExtentDescriptor in_ext = { .base_offset = (1ULL << 50), .byte_length = 5600, .stride_bytes = 56, .align_bytes = 8 };
        FlowExtentDescriptor out_ext = {0};
        FLOW_ASSERT_EQ(fn_pd(ch, &in_ext, &out_ext), 0);
        FLOW_ASSERT_EQ(out_ext.base_offset, (1ULL << 50) + 5600ULL);
        FLOW_ASSERT_TRUE(out_ext.qsbr_epoch >= 2);
        fn_de(ch);
        dlclose(cdl);

        remove(cedar_c);
        remove(cedar_so);
        flow_ir_cleanup(&cedar_ir);

        FlowJet jet;
        FLOW_ASSERT_TRUE(flow_jet_init(&jet, "jet_embodied_test", "Embodied Named View Jet"));
        TestEmbodiedJetView view = TestEmbodiedJetView_bind(&jet);
        view.q->hipPitch = 0.25;
        view.q->hipRoll = NAN;
        FLOW_ASSERT_TRUE(flow_jet_sanitize_finite(&jet));
        FLOW_ASSERT_TRUE(isfinite(view.q->hipRoll) && view.q->hipRoll == 0.0);

        printf("  ✓ Stage 5 Passed: Pure .lean (plugins/embodied.lean + AWS Cedar 427-line Authorization.lean) verified in C17.\n\n");
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
            "#include \"smt.h\"\n"
            "#include \"%s\"\n"
            "typedef double (*exec_quote_fn)(double, double, double, double, double *, double *);\n"
            "typedef double (*quote_spread_fn)(double, double, double, double);\n"
            "typedef flow_fx64_t (*exec_quote_fx_fn)(flow_fx64_t, flow_fx64_t, flow_fx64_t, flow_fx64_t, flow_fx64_t *, flow_fx64_t *);\n"
            "typedef flow_fx64_t (*quote_spread_fx_fn)(flow_fx64_t, flow_fx64_t, flow_fx64_t, flow_fx64_t);\n"
            "typedef void (*ab_rec_fn)(uint32_t, double, double, double);\n"
            "typedef uint64_t (*ab_get_u64_fn)(void);\n"
            "typedef uint64_t (*ab_get_fn_u64_fn)(uint32_t);\n"
            "typedef void (*ab_reset_fn)(void);\n"
            "int main(void) {\n"
            "    flow_fx64_t fx_bid = 0, fx_ask = 0;\n"
            "    exec_bilateral_quote_tuple_t tup_check = { 0.0, 0.0 };\n"
            "    (void)tup_check;\n"
            "    flow_fx64_t fx_ret0 = flowc_exec_bilateral_quote_fx(100LL * FLOW_FX_ONE, 4LL * FLOW_FX_ONE, -2LL * FLOW_FX_ONE, FLOW_FX_ONE >> 2, &fx_bid, &fx_ask);\n"
            "    assert(fx_ret0 == fx_bid);\n"
            "    assert(fx_bid == (99LL * FLOW_FX_ONE + (FLOW_FX_ONE >> 2)));\n"
            "    assert(fx_ask == (101LL * FLOW_FX_ONE + (FLOW_FX_ONE >> 2)));\n"
            "    flow_fx64_t fx_sprd = flowc_quote_spread_fx(100LL * FLOW_FX_ONE, 4LL * FLOW_FX_ONE, -2LL * FLOW_FX_ONE, FLOW_FX_ONE >> 2);\n"
            "    assert(fx_sprd == 2LL * FLOW_FX_ONE);\n"
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
            "    exec_quote_fx_fn dso_quote_fx = (exec_quote_fx_fn)dlsym(h, \"exec_bilateral_quote_fx\");\n"
            "    quote_spread_fx_fn dso_spread_fx = (quote_spread_fx_fn)dlsym(h, \"quote_spread_fx\");\n"
            "    ab_rec_fn dso_ab_rec = (ab_rec_fn)dlsym(h, \"flow_ab_shadow_record\");\n"
            "    ab_get_u64_fn dso_ab_calls = (ab_get_u64_fn)dlsym(h, \"flow_ab_shadow_get_calls\");\n"
            "    ab_get_u64_fn dso_ab_mis = (ab_get_u64_fn)dlsym(h, \"flow_ab_shadow_get_mismatches\");\n"
            "    ab_get_fn_u64_fn dso_ab_fn = (ab_get_fn_u64_fn)dlsym(h, \"flow_ab_shadow_get_fn_calls\");\n"
            "    ab_reset_fn dso_ab_reset = (ab_reset_fn)dlsym(h, \"flow_ab_shadow_reset\");\n"
            "    assert(dso_quote && dso_spread && dso_quote_fx && dso_spread_fx && dso_ab_rec && dso_ab_calls && dso_ab_mis && dso_ab_fn && dso_ab_reset);\n"
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
    /* STAGE 9: FlowExactNumerics — Fixed-Point Q16.16/Q32.32, U256 & Linalg     */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(9, "FlowExactNumerics: Lossless Fixed-Point Q16.16/Q32.32, 256-Bit BigInt & Fixed-Point Linalg");
    {
        int32_t exp_0 = 0;
        FLOW_ASSERT_EQ(flow_exp_approx_q16(0, &exp_0), 1);
        FLOW_ASSERT_EQ(exp_0, 65536);

        uint32_t rsqrt_16 = 0;
        FLOW_ASSERT_EQ(flow_rsqrt_approx_i32(16U, &rsqrt_16), 1);
        FLOW_ASSERT_EQ(rsqrt_16, 16384U);

        FLOW_ASSERT_EQ(flow_fx64_from_double(1.0), FLOW_FX_ONE);
        FLOW_ASSERT_EQ(flow_fx64_from_double(NAN), FLOW_FX64_NONE);
        FLOW_ASSERT_EQ(flow_fx64_sqrt(9LL * FLOW_FX_ONE), 3LL * FLOW_FX_ONE);

        static const uint64_t secp_bases[] = { 2ULL, 3ULL, 5ULL, 7ULL };
        FlowU256 p_secp;
        FLOW_ASSERT_TRUE(flow_u256_from_hex_str("0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F", &p_secp));
        FLOW_ASSERT_EQ(flow_u256_is_probably_prime(&p_secp, secp_bases, 4), 1);

        FlowU256 big_base;
        FLOW_ASSERT_TRUE(flow_u256_from_hex_str("0x8000000000000000000000000000000000000000000000000000000000000005", &big_base));
        FlowU256 even_res = flow_u256_mod_pow(big_base, flow_u256_from_u64(2ULL), flow_u256_from_u64(1000ULL));
        FLOW_ASSERT_EQ(even_res.w[0], 729ULL);

        flow_fx64_t x_in[2]    = { 1LL * FLOW_FX_ONE, 2LL * FLOW_FX_ONE };
        flow_fx64_t w_param[2] = { 0LL, 0LL };
        flow_fx64_t preds[1]   = { 0LL };
        flow_fx64_t tgts[1]    = { 5LL * FLOW_FX_ONE };
        flow_fx_mat_mul_ikj(1, 2, 1, x_in, w_param, preds);
        flow_fx64_t loss_before = flow_fx_mse_loss(1, 1, preds, tgts);
        flow_fx_gradient_descent_step(1, 2, 1, x_in, w_param, preds, tgts, FLOW_FX_ONE / 10);
        flow_fx_mat_mul_ikj(1, 2, 1, x_in, w_param, preds);
        flow_fx64_t loss_after = flow_fx_mse_loss(1, 1, preds, tgts);
        FLOW_ASSERT_TRUE(loss_after < loss_before);

        printf("  ✓ Stage 9 Passed: FlowExactNumerics (Q16.16/Q32.32, 256-bit secp256k1 BigInt & fixed-point linalg) verified.\n\n");
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
        fputs("#include \"flow_spsc.h\"\n#include \"flow_jet.h\"\n#include \"reload.h\"\n#include \"cubical_hott.h\"\n#include \"polyhedral.h\"\n#include <assert.h>\n"
              "FLOW_SPSC_DEFINE(TickRing, double, 16)\n"
              "int main(void) {\n"
              "    TickRing ring;\n"
              "    TickRing_init(&ring);\n"
              "    assert(TickRing_push(&ring, 101.25) == 1);\n"
              "    double out_px = 0.0;\n"
              "    assert(TickRing_pop(&ring, &out_px) == 1 && out_px == 101.25);\n"
              "    FlowJet jet;\n"
              "    assert(flow_jet_init(&jet, \"rt_jet\", \"Minimal RT Jet\") == 1);\n"
              "    flow_fx64_t a = flow_fx64_from_double(3.5), b = flow_fx64_from_double(2.0);\n"
              "    assert(flow_fx64_to_double(flow_fx64_mul(a, b)) == 7.0);\n"
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
              "    /* Pure double mode (float_abi_ieee64) emits zero _fx symbols */\n"
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

    FLOW_TEST_SUITE_END();
    return 0;
}
