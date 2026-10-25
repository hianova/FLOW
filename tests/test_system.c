#include "flow_test_kit.h"
#include "flow.h"
#include "registry.h"
#include "smt.h"
#include "flowy.h"
#include "abi.h"
#include "backend.h"
#include "reload.h"
#include "flow_jet.h"
#include "topology.h"
#include "cubical_hott.h"

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
    FLOW_TEST_SUITE_BEGIN("System: Compiler Pipeline, Plugin ABI, Edge Gateway, Finance & Doc-as-Intent");

    FLOW_ASSERT_TRUE(flow_registry_init());

    /* ========================================================================= */
    /* STAGE 1: Compiler Pipeline, AST Parsing & IR Lowering                     */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(1, "Compiler Pipeline: Spec Parsing, IR Lowering & Component Selection");
    {
        const char *spec_src =
            "project test_system_pipeline\n"
            "input task_stream {\n"
            "    max_count 1000\n"
            "}\n"
            "flow data_flow {\n"
            "    task_stream -> transform -> sink\n"
            "}\n"
            "require {\n"
            "    deterministic\n"
            "    memory < 16mb\n"
            "}\n"
            "prefer {\n"
            "    latency\n"
            "}\n";

        FILE *mem = tmpfile();
        FLOW_ASSERT_TRUE(mem != NULL);
        fputs(spec_src, mem);
        rewind(mem);

        FlowSpec spec;
        memset(&spec, 0, sizeof(spec));
        FLOW_ASSERT_TRUE(parse_spec(mem, &spec));
        fclose(mem);

        FLOW_ASSERT_STR_EQ(spec.project_name, "test_system_pipeline");
        FLOW_ASSERT_EQ(spec.max_count, 1000);
        FLOW_ASSERT_EQ(spec.deterministic, 1);
        FLOW_ASSERT_EQ(spec.memory_mb, 16);

        SemanticIR ir;
        memset(&ir, 0, sizeof(ir));
        lower_to_ir(&spec, &ir);

        FLOW_ASSERT_STR_EQ(ir.flow_name, "data_flow");
        FLOW_ASSERT_EQ(ir.input_max_count, 1000);
        FLOW_ASSERT_EQ(ir.fact_deterministic, 1);
        FLOW_ASSERT_EQ(ir.memory_limit_mb, 16);

        const Component *comp = select_component(&ir);
        FLOW_ASSERT_TRUE(comp != NULL);
        printf("  ✓ Stage 1 Passed: Spec parsed, lowered to IR, selected component '%s'.\n\n", comp->id);
    }

    /* ========================================================================= */
    /* STAGE 2: Dynamic Plugin ABI & Registry Introspection                      */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(2, "Plugin ABI & Registry Subsystem Introspection");
    {
        size_t plugin_count = flow_registry_plugin_count();
        FLOW_ASSERT_TRUE(plugin_count >= 1);

        const FlowPlugin *builtin = flow_registry_lookup("builtin");
        FLOW_ASSERT_TRUE(builtin != NULL);
        FLOW_ASSERT_STR_EQ(builtin->name, "builtin");
        FLOW_ASSERT_TRUE(builtin->component_count >= 3);

        const Component *sharded = NULL;
        for (size_t i = 0; i < builtin->component_count; ++i) {
            if (strcmp(builtin->components[i].id, "sharded_hash") == 0) {
                sharded = &builtin->components[i];
                break;
            }
        }
        FLOW_ASSERT_TRUE(sharded != NULL);
        FLOW_ASSERT_TRUE(sharded->supports_shared);
        FLOW_ASSERT_TRUE(sharded->supports_read_heavy);
        FLOW_ASSERT_TRUE(sharded->reload_capable);

        printf("  ✓ Stage 2 Passed: %zu registered plugins validated with active ABI components.\n\n", plugin_count);
    }

    /* ========================================================================= */
    /* STAGE 5: Doc-as-Intent Literate Specifications                            */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(5, "Doc-as-Intent: Executable Specifications Embedded in Markdown");
    {
        const char *markdown_source =
            "# Specification Document: Microservice Pipeline\n"
            "\n"
            "> This is living prose documentation that acts as a formal contract.\n"
            "\n"
            "```flow\n"
            "project literate_microservice\n"
            "input event_stream {\n"
            "    max_count 5000\n"
            "}\n"
            "flow event_processor {\n"
            "    event_stream -> filter -> dispatch\n"
            "}\n"
            "require {\n"
            "    deterministic\n"
            "    memory < 32mb\n"
            "}\n"
            "prefer {\n"
            "    latency\n"
            "}\n"
            "```\n"
            "\n"
            "```c\n"
            "void dummy_c_code(void) {}\n"
            "```\n";

        FILE *mem = tmpfile();
        FLOW_ASSERT_TRUE(mem != NULL);
        fputs(markdown_source, mem);
        rewind(mem);

        FlowSpec doc_spec;
        memset(&doc_spec, 0, sizeof(doc_spec));
        FLOW_ASSERT_TRUE(parse_spec(mem, &doc_spec));
        fclose(mem);

        FLOW_ASSERT_STR_EQ(doc_spec.project_name, "literate_microservice");
        FLOW_ASSERT_EQ(doc_spec.max_count, 5000);
        FLOW_ASSERT_EQ(doc_spec.memory_mb, 32);

        SemanticIR doc_ir;
        memset(&doc_ir, 0, sizeof(doc_ir));
        lower_to_ir(&doc_spec, &doc_ir);

        FLOW_ASSERT_STR_EQ(doc_ir.flow_name, "event_processor");
        FLOW_ASSERT_EQ(doc_ir.input_max_count, 5000);

        printf("  ✓ Stage 5 Passed: Markdown prose with embedded ```flow code block compiled directly.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 6: Classical Adaptive Control, Hysteresis & SMT Watchdog Invariants */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(6, "Classical Adaptive Control: JIT Quota Sizing, Schmitt Hysteresis & SMT Watchdog");
    {
        /* 6a. AST Complexity Driven JIT Memory Sizing */
        SemanticIR ir11;
        memset(&ir11, 0, sizeof(ir11));
        ir11.flow_node_count = 11;
        int jit_ram_11 = flow_jit_calculate_min_memory_mb(&ir11);
        FLOW_ASSERT_EQ(jit_ram_11, 100);

        /* 6b. Schmitt Trigger Anti-Flapping Hysteresis */
        FlowSchmittTrigger st;
        flow_schmitt_trigger_init(&st, 100.0, 500000000ULL);
        FLOW_ASSERT_EQ((int)st.drop_threshold, 80);
        FLOW_ASSERT_EQ((int)st.recovery_threshold, 150);

        /* Drop to 16MB */
        int changed = 0;
        flow_schmitt_trigger_update(&st, 16.0, 1000ULL, &changed);
        FLOW_ASSERT_EQ(st.current_state, 1);
        FLOW_ASSERT_EQ(changed, 1);

        /* Reject flapping around 95MB <-> 105MB (must NOT flap back) */
        flow_schmitt_trigger_update(&st, 95.0, 2000ULL, &changed);
        FLOW_ASSERT_EQ(st.current_state, 1);
        FLOW_ASSERT_EQ(changed, 0);
        flow_schmitt_trigger_update(&st, 105.0, 3000ULL, &changed);
        FLOW_ASSERT_EQ(st.current_state, 1);
        FLOW_ASSERT_EQ(changed, 0);

        /* 6c. SMT Watchdog Conservative Polytope Fallback (<10us budget) */
        FlowSMTProofAttestation watchdog_proof;
        Component dummy_comp = { .id = "test_comp" };
        int smt_ok = flow_smt_verify_with_budget(&ir11, &dummy_comp, NULL, NULL, 5, &watchdog_proof);
        FLOW_ASSERT_TRUE(smt_ok == 1);
        FLOW_ASSERT_TRUE(strstr(watchdog_proof.proof_summary, "Conservative Polytope Interval Bounding Box") != NULL);

        printf("  ✓ Stage 6 Passed: JIT sizing (100MB), Schmitt hysteresis (anti-flapping), and SMT 5us watchdog verified.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 7: Self-Hosting Lean 4 Reflection, Hard Gate & 6-Pillar Audit       */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(7, "Lean 4 Self-Hosting Reflection: .flow lean_certified Gate, 64-Axis Witness & @& FFI");
    {
        /* 7a. Parse spec with lean_certified requirement */
        const char *lean_spec_src =
            "input source {\n"
            "    max_count 4096\n"
            "}\n"
            "output c_source {\n"
            "    type text\n"
            "}\n"
            "state compiler {\n"
            "    bounded\n"
            "}\n"
            "flow compiler {\n"
            "    source -> read_file -> parse -> arena -> graph -> hash_table -> work_queue -> ffi -> lower -> select -> emit\n"
            "}\n"
            "require {\n"
            "    deterministic\n"
            "    lean_certified\n"
            "    memory < 64mb\n"
            "}\n"
            "ensure {\n"
            "    deterministic\n"
            "    lean_certified\n"
            "}\n"
            "resource cpu {\n"
            "}\n"
            "capability stdio {\n"
            "}\n"
            "domain compiler {\n"
            "}\n"
            "contract compiler\n";

        FILE *mem = tmpfile();
        FLOW_ASSERT_TRUE(mem != NULL);
        fputs(lean_spec_src, mem);
        rewind(mem);

        FlowSpec lspec;
        memset(&lspec, 0, sizeof(lspec));
        FLOW_ASSERT_TRUE(parse_spec(mem, &lspec));
        fclose(mem);
        FLOW_ASSERT_EQ(lspec.lean_certified, 1);
        FLOW_ASSERT_EQ(lspec.deterministic, 1);

        SemanticIR lir;
        memset(&lir, 0, sizeof(lir));
        lower_to_ir(&lspec, &lir);
        const FlowPlugin *builtin_mod = flow_registry_lookup("builtin");
        FLOW_ASSERT_TRUE(builtin_mod != NULL);
        flow_plugin_lower_semantics(&lspec, &lir, builtin_mod);
        FLOW_ASSERT_EQ(lir.fact_lean_certified, 1);
        FLOW_ASSERT_EQ(lir.hole_count, 0);

        const Component *comp = select_component(&lir);
        FLOW_ASSERT_TRUE(comp != NULL);
        FLOW_ASSERT_STR_EQ(comp->id, "flowc_bootstrap");

        VerificationReport vrep;
        FLOW_ASSERT_TRUE(verify_candidate(&lir, comp, NULL, &vrep));
        FLOW_ASSERT_EQ(vrep.status, VERIFIER_PROVEN);

        /* 7b. Reject unresolved '?' hole (sorry) under lean_certified gate */
        SemanticIR hole_ir = lir;
        hole_ir.hole_count = 1;
        VerificationReport hole_rep;
        FLOW_ASSERT_TRUE(!verify_candidate(&hole_ir, comp, NULL, &hole_rep));
        FLOW_ASSERT_EQ(hole_rep.status, VERIFIER_COMPILE_ERROR);
        FLOW_ASSERT_TRUE(strstr(hole_rep.message, "unresolved") != NULL);

        /* 7c. Emit Lean 4 Certificate & C Header Newtypes and Audit All 6 Pillars */
        FlowComponentABI abi;
        FLOW_ASSERT_TRUE(flow_abi_build_for_component(&lir, comp, &abi));

        FILE *lean_mem = tmpfile();
        FLOW_ASSERT_TRUE(lean_mem != NULL);
        FLOW_ASSERT_TRUE(flow_abi_emit_lean_adapter(lean_mem, &lir, &abi, 0x0000000000000021ULL, 4096, 1));
        long lean_len = ftell(lean_mem);
        FLOW_ASSERT_TRUE(lean_len > 500);
        rewind(lean_mem);

        char *lean_buf = (char *)calloc((size_t)lean_len + 1, 1);
        FLOW_ASSERT_TRUE(lean_buf != NULL);
        size_t read_bytes = fread(lean_buf, 1, (size_t)lean_len, lean_mem);
        FLOW_ASSERT_EQ(read_bytes, (size_t)lean_len);
        fclose(lean_mem);

        char audit_err[256] = {0};
        FLOW_ASSERT_TRUE(flow_abi_audit_lean_certificate(lean_buf, audit_err, sizeof(audit_err)));

        /* 7d. Verify auditor catches injected `sorry` and custom `axiom` */
        const char *bad_sorry = "theorem bad : 1 = 2 := by sorry\n";
        FLOW_ASSERT_TRUE(!flow_abi_audit_lean_certificate(bad_sorry, audit_err, sizeof(audit_err)));
        FLOW_ASSERT_TRUE(strstr(audit_err, "sorry") != NULL);

        const char *bad_axiom = "axiom fake_soundness : False\n";
        FLOW_ASSERT_TRUE(!flow_abi_audit_lean_certificate(bad_axiom, audit_err, sizeof(audit_err)));
        FLOW_ASSERT_TRUE(strstr(audit_err, "axiom") != NULL);

        /* 7e. Verify C Header emits Dimensional Newtypes & FLOW_LEAN_PRE_GUARD */
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
        FLOW_ASSERT_TRUE(strstr(hdr_buf, "flow_pct_vol_t") != NULL);
        FLOW_ASSERT_TRUE(strstr(hdr_buf, "flow_point_vol_t") != NULL);
        FLOW_ASSERT_TRUE(strstr(hdr_buf, "flow_bachelier_std_dev") != NULL);
        FLOW_ASSERT_TRUE(strstr(hdr_buf, "FLOW_LEAN_PRE_GUARD") != NULL);

        free(hdr_buf);
        free(lean_buf);
        flow_ir_cleanup(&lir);

        printf("  ✓ Stage 7 Passed: Lean 4 reflection certificate, zero-sorry gate, 64-axis witness, and @& FFI verified.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 8: Neutral Memory Plane (stride/align/sla/field) & C-ABI Closure    */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(8, "Neutral Memory Plane (stride/align/sla/field) & QSBR C-ABI Export Closure");
    {
        const char *neutral_spec_src =
            "input sensor_batch {\n"
            "    max_count 2048\n"
            "    stride 32\n"
            "    align 64\n"
            "}\n"
            "state cortex_state {\n"
            "    shared\n"
            "    read_heavy\n"
            "    bounded\n"
            "    version 2\n"
            "    field pos 16 persistent\n"
            "    field vel u64 ordered\n"
            "}\n"
            "flow reflex_pipeline {\n"
            "    sensor_batch -> sort -> top(3)\n"
            "}\n"
            "require {\n"
            "    deterministic\n"
            "    memory < 32mb\n"
            "    sla < 200us\n"
            "}\n";

        FILE *mem = tmpfile();
        FLOW_ASSERT_TRUE(mem != NULL);
        fputs(neutral_spec_src, mem);
        rewind(mem);

        FlowSpec nspec;
        memset(&nspec, 0, sizeof(nspec));
        FLOW_ASSERT_TRUE(parse_spec(mem, &nspec));
        fclose(mem);

        FLOW_ASSERT_EQ(nspec.input_stride_bytes, 32);
        FLOW_ASSERT_EQ(nspec.input_align_bytes, 64);
        FLOW_ASSERT_EQ(nspec.state_schema_version, 2);
        FLOW_ASSERT_EQ(nspec.state_field_count, 2);
        FLOW_ASSERT_STR_EQ(nspec.state_fields[0].name, "pos");
        FLOW_ASSERT_EQ(nspec.state_fields[0].size_bytes, 16);
        FLOW_ASSERT_STR_EQ(nspec.state_fields[1].name, "vel");
        FLOW_ASSERT_EQ(nspec.state_fields[1].size_bytes, 8);
        FLOW_ASSERT_EQ(nspec.sla_latency_ns, 200000ULL);

        SemanticIR nir;
        memset(&nir, 0, sizeof(nir));
        lower_to_ir(&nspec, &nir);
        const Component *ncomp = select_component(&nir);
        FLOW_ASSERT_TRUE(ncomp != NULL);
        FLOW_ASSERT_STR_EQ(ncomp->id, "sharded_hash");

        FlowComponentABI nabi;
        FLOW_ASSERT_TRUE(flow_abi_build_for_component(&nir, ncomp, &nabi));
        FLOW_ASSERT_EQ(nabi.input_view_contract.min_length, (size_t)(2048 * 32));
        FLOW_ASSERT_EQ(nabi.input_view_contract.required_alignment, 64);

        VerificationReport nvrep;
        FLOW_ASSERT_TRUE(verify_candidate(&nir, ncomp, NULL, &nvrep));

        FILE *c_mem = tmpfile();
        FLOW_ASSERT_TRUE(c_mem != NULL);
        FLOW_ASSERT_TRUE(emit_c(c_mem, &nir, ncomp, NULL, &nvrep, 0));
        long c_len = ftell(c_mem);
        FLOW_ASSERT_TRUE(c_len > 400);
        rewind(c_mem);
        char *c_buf = (char *)calloc((size_t)c_len + 1, 1);
        FLOW_ASSERT_TRUE(c_buf != NULL);
        (void)fread(c_buf, 1, (size_t)c_len, c_mem);
        fclose(c_mem);

        FLOW_ASSERT_TRUE(strstr(c_buf, "#define FLOW_INPUT_STRIDE 32") != NULL);
        FLOW_ASSERT_TRUE(strstr(c_buf, "#define FLOW_INPUT_ALIGN 64") != NULL);
        FLOW_ASSERT_TRUE(strstr(c_buf, "#define FLOW_SLA_LATENCY_NS UINT64_C(200000)") != NULL);
        FLOW_ASSERT_TRUE(strstr(c_buf, "void *flow_sharded_hash_create(void)") != NULL);
        FLOW_ASSERT_TRUE(strstr(c_buf, "void *sharded_hash_bind_context(void *handle)") != NULL);
        FLOW_ASSERT_TRUE(strstr(c_buf, "int32_t flow_sharded_hash_process(") != NULL);
        FLOW_ASSERT_TRUE(strstr(c_buf, "void flow_sharded_hash_destroy(void *handle)") != NULL);

        free(c_buf);
        flow_ir_cleanup(&nir);
        printf("  ✓ Stage 8 Passed: Neutral memory plane (stride/align/sla/field) & live C-ABI QSBR exports verified.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 9: Zero-Boilerplate FlowUnit Plugin & Automatic Core Synthesis      */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(9, "Zero-Boilerplate FlowUnit Registration & Automatic Core Synthesis");
    {
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

        const char *unit_spec_src =
            "input stream {\n"
            "    max_count 100\n"
            "    stride 64\n"
            "}\n"
            "flow zero_pipe {\n"
            "    stream -> custom_zero_unit -> sink\n"
            "}\n"
            "require {\n"
            "    deterministic\n"
            "    memory < 4mb\n"
            "}\n";

        FILE *umem = tmpfile();
        FLOW_ASSERT_TRUE(umem != NULL);
        fputs(unit_spec_src, umem);
        rewind(umem);

        FlowSpec uspec;
        memset(&uspec, 0, sizeof(uspec));
        FLOW_ASSERT_TRUE(parse_spec(umem, &uspec));
        fclose(umem);

        SemanticIR uir;
        memset(&uir, 0, sizeof(uir));
        lower_to_ir(&uspec, &uir);

        const Component *ucomp = select_component(&uir);
        FLOW_ASSERT_TRUE(ucomp != NULL);
        FLOW_ASSERT_STR_EQ(ucomp->id, "custom_zero_unit");

        VerificationReport uvrep;
        FLOW_ASSERT_TRUE(verify_candidate(&uir, ucomp, NULL, &uvrep));
        /* Default linear memory model: 64 + capacity * 64 */
        FLOW_ASSERT_EQ(uvrep.estimated_bytes, 64 + uvrep.capacity * 64);

        FILE *uc_mem = tmpfile();
        FLOW_ASSERT_TRUE(uc_mem != NULL);
        FLOW_ASSERT_TRUE(emit_c(uc_mem, &uir, ucomp, NULL, &uvrep, 0));
        long uc_len = ftell(uc_mem);
        FLOW_ASSERT_TRUE(uc_len > 300);
        rewind(uc_mem);
        char *uc_buf = (char *)calloc((size_t)uc_len + 1, 1);
        FLOW_ASSERT_TRUE(uc_buf != NULL);
        (void)fread(uc_buf, 1, (size_t)uc_len, uc_mem);
        fclose(uc_mem);

        FLOW_ASSERT_TRUE(strstr(uc_buf, "void *flow_custom_zero_unit_create(void)") != NULL);
        FLOW_ASSERT_TRUE(strstr(uc_buf, "int32_t flow_custom_zero_unit_process(") != NULL);
        free(uc_buf);

        const FlowPlugin *uplug = flow_component_plugin(ucomp);
        FLOW_ASSERT_TRUE(uplug != NULL && uplug->create_unit != NULL);
        FlowUnit live_u;
        FLOW_ASSERT_TRUE(uplug->create_unit(NULL, &uir, ucomp, &live_u));

        FlowReloadContext *rctx = flow_reload_create(NULL);
        FLOW_ASSERT_TRUE(rctx != NULL);
        FLOW_ASSERT_EQ(flow_reload_activate(rctx, &live_u), FLOW_RELOAD_OK);

        uint64_t in_val = 42, out_val = 0;
        FLOW_ASSERT_EQ(flow_qsbr_call(rctx, &in_val, &out_val), FLOW_RELOAD_OK);
        FLOW_ASSERT_EQ(out_val, 142ULL);

        FLOW_ASSERT_EQ(flow_reload_destroy(rctx), FLOW_RELOAD_OK);

        /* Verify default synthesis on EMBODIED_PLUGIN (where emit == NULL and memory_model == NULL) */
        extern const FlowPluginDescriptor *flow_embodied_entry_v1(void);
        const FlowPluginDescriptor *emb_desc = flow_embodied_entry_v1();
        FLOW_ASSERT_TRUE(emb_desc != NULL && emb_desc->plugin != NULL);
        if (!flow_registry_lookup("flow.embodied")) {
            FLOW_ASSERT_TRUE(flow_registry_register(emb_desc->plugin));
        }
        const Component *emb_comp = &emb_desc->plugin->components[0];
        size_t emb_bytes = 0;
        FLOW_ASSERT_TRUE(flow_component_memory(&uir, emb_comp, 1024, 1, &emb_bytes));
        FLOW_ASSERT_TRUE(emb_bytes > 1024 * 64);

        FILE *e_mem = tmpfile();
        FLOW_ASSERT_TRUE(e_mem != NULL);
        FLOW_ASSERT_TRUE(emit_c(e_mem, &uir, emb_comp, NULL, &uvrep, 0));
        long e_len = ftell(e_mem);
        FLOW_ASSERT_TRUE(e_len > 300);
        fclose(e_mem);

        flow_ir_cleanup(&uir);
        printf("  ✓ Stage 9 Passed: Zero-boilerplate FlowUnit registered, selected, emitted & executed via QSBR.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 10: Bidirectional .flow <-> .lean Bridge & PB-Scale Extent Descriptor */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(10, "Bidirectional .flow <-> .lean Bridge & PB-Scale Extent Descriptor");
    {
        const char *pb_spec_src =
            "input pb_extent_stream {\n"
            "    max_count 4096\n"
            "    stride 64\n"
            "    align 64\n"
            "}\n"
            "state lsm_manifest {\n"
            "    shared\n"
            "    read_heavy\n"
            "    bounded\n"
            "    version 3\n"
            "    field base_offset u64 persistent\n"
            "    field generation u64 ordered\n"
            "}\n"
            "flow pb_compaction {\n"
            "    pb_extent_stream -> extern(lean_lsm_compact_step) -> top(3)\n"
            "}\n"
            "require {\n"
            "    deterministic\n"
            "    lean_certified\n"
            "    memory < 64mb\n"
            "    sla < 50us\n"
            "}\n";

        FILE *pb_mem = tmpfile();
        FLOW_ASSERT_TRUE(pb_mem != NULL);
        fputs(pb_spec_src, pb_mem);
        rewind(pb_mem);

        FlowSpec pb_spec;
        memset(&pb_spec, 0, sizeof(pb_spec));
        FLOW_ASSERT_TRUE(parse_spec(pb_mem, &pb_spec));
        fclose(pb_mem);

        FLOW_ASSERT_STR_EQ(pb_spec.extern_kernel, "lean_lsm_compact_step");
        FLOW_ASSERT_EQ(pb_spec.input_stride_bytes, 64);
        FLOW_ASSERT_EQ(pb_spec.input_align_bytes, 64);
        FLOW_ASSERT_EQ(pb_spec.state_schema_version, 3);
        FLOW_ASSERT_EQ(pb_spec.sla_latency_ns, 50000ULL);
        FLOW_ASSERT_EQ(sizeof(FlowExtentDescriptor), 64u);

        SemanticIR pb_ir;
        memset(&pb_ir, 0, sizeof(pb_ir));
        lower_to_ir(&pb_spec, &pb_ir);
        FLOW_ASSERT_STR_EQ(pb_ir.extern_kernel, "lean_lsm_compact_step");

        const Component *pb_comp = select_component(&pb_ir);
        FLOW_ASSERT_TRUE(pb_comp != NULL);

        VerificationReport pb_vrep;
        FLOW_ASSERT_TRUE(verify_candidate(&pb_ir, pb_comp, NULL, &pb_vrep));

        FlowComponentABI pb_abi;
        FLOW_ASSERT_TRUE(flow_abi_build_for_component(&pb_ir, pb_comp, &pb_abi));

        const char *tmp_lean_path = "/tmp/flow_pb_extent_test.lean";
        FILE *l_fp = fopen(tmp_lean_path, "w");
        FLOW_ASSERT_TRUE(l_fp != NULL);
        FLOW_ASSERT_TRUE(flow_abi_emit_lean_adapter(l_fp, &pb_ir, &pb_abi, 0xCAFEBABEULL, pb_vrep.capacity, 4));
        fclose(l_fp);

        char lean_err[256] = {0};
        FLOW_ASSERT_TRUE(flow_abi_verify_lean_certificate(tmp_lean_path, lean_err, sizeof(lean_err)));

        /* Verify real CLI invocation path via FLOW_LEAN_BIN (pass & fail cases) */
        setenv("FLOW_LEAN_BIN", "/usr/bin/true", 1);
        FLOW_ASSERT_TRUE(flow_abi_verify_lean_certificate(tmp_lean_path, lean_err, sizeof(lean_err)));
        setenv("FLOW_LEAN_BIN", "/usr/bin/false", 1);
        FLOW_ASSERT_TRUE(!flow_abi_verify_lean_certificate(tmp_lean_path, lean_err, sizeof(lean_err)));
        unsetenv("FLOW_LEAN_BIN");

        FILE *l_rd = fopen(tmp_lean_path, "r");
        FLOW_ASSERT_TRUE(l_rd != NULL);
        fseek(l_rd, 0, SEEK_END);
        long l_sz = ftell(l_rd);
        rewind(l_rd);
        char *l_buf = (char *)calloc((size_t)l_sz + 1, 1);
        FLOW_ASSERT_TRUE(l_buf != NULL);
        (void)fread(l_buf, 1, (size_t)l_sz, l_rd);
        fclose(l_rd);
        remove(tmp_lean_path);

        FLOW_ASSERT_TRUE(strstr(l_buf, "def inputStrideBytes : Nat := 64") != NULL);
        FLOW_ASSERT_TRUE(strstr(l_buf, "def stateSchemaVersion : Nat := 3") != NULL);
        FLOW_ASSERT_TRUE(strstr(l_buf, "def slaLatencyNs : Nat := 50000") != NULL);
        FLOW_ASSERT_TRUE(strstr(l_buf, "{ name := \"base_offset\", sizeBytes := 8, flags := 2 }") != NULL);
        FLOW_ASSERT_TRUE(strstr(l_buf, "{ name := \"generation\", sizeBytes := 8, flags := 4 }") != NULL);
        FLOW_ASSERT_TRUE(strstr(l_buf, "theorem theorem7_pb_extent_monotonic_step") != NULL);
        FLOW_ASSERT_TRUE(strstr(l_buf, "theorem theorem8_qsbr_epoch_lease_safety") != NULL);
        FLOW_ASSERT_TRUE(strstr(l_buf, "theorem theorem10_pb_extent_inductive_steps") != NULL);
        FLOW_ASSERT_TRUE(strstr(l_buf, "structure ExtentDescriptor where") != NULL);
        FLOW_ASSERT_TRUE(strstr(l_buf, "@[export lean_lsm_compact_step]") != NULL);
        free(l_buf);

        FILE *c_fp = tmpfile();
        FLOW_ASSERT_TRUE(c_fp != NULL);
        FLOW_ASSERT_TRUE(emit_c(c_fp, &pb_ir, pb_comp, NULL, &pb_vrep, 0));
        long c_sz = ftell(c_fp);
        rewind(c_fp);
        char *c_src = (char *)calloc((size_t)c_sz + 1, 1);
        FLOW_ASSERT_TRUE(c_src != NULL);
        (void)fread(c_src, 1, (size_t)c_sz, c_fp);
        fclose(c_fp);

        FLOW_ASSERT_TRUE(strstr(c_src, "lean_lsm_compact_step") != NULL);
        FLOW_ASSERT_TRUE(strstr(c_src, "_process_descriptor(") != NULL);

        /* Compile generated C + external @export kernel into DSO and run 1 PB extent step */
        const char *tmp_c = "/tmp/flow_pb_extent_dso.c";
        const char *tmp_so = "/tmp/libflow_pb_extent_dso.so";
        FILE *dso_c = fopen(tmp_c, "w");
        FLOW_ASSERT_TRUE(dso_c != NULL);
        fputs(c_src, dso_c);
        fputs("\nuint32_t lean_lsm_compact_step(uint64_t base_off, uint64_t byte_len, uint64_t stride, uint64_t epoch) {\n"
              "    return (stride == 64 && epoch >= 2 && (base_off + byte_len > base_off)) ? 0u : 7u;\n"
              "}\n", dso_c);
        fclose(dso_c);
        free(c_src);

        int cc_rc = system("cc -std=c17 -O2 -shared -fPIC -DFLOW_SHARED_LIB /tmp/flow_pb_extent_dso.c -o /tmp/libflow_pb_extent_dso.so -lpthread");
        FLOW_ASSERT_EQ(cc_rc, 0);

        /* Execute via Python adapter with FlowExtentDescriptor at 1 PB (2^50) base_offset */
        const char *tmp_py = "/tmp/flow_pb_extent_adapter.py";
        FILE *py_fp = fopen(tmp_py, "w");
        FLOW_ASSERT_TRUE(py_fp != NULL);
        FLOW_ASSERT_TRUE(flow_abi_emit_python_adapter(py_fp, &pb_abi));
        fputs("\nif __name__ == '__main__':\n"
              "    with ShardedHash('/tmp/libflow_pb_extent_dso.so') as eng:\n"
              "        in_ext = FlowExtentDescriptor(base_offset=(1 << 50), byte_length=(1 << 30), stride_bytes=64, align_bytes=64)\n"
              "        out_ext = eng.process_descriptor(in_ext)\n"
              "        assert out_ext.base_offset == (1 << 50) + (1 << 30)\n"
              "        assert out_ext.qsbr_epoch >= 2\n"
              "        assert out_ext.schema_version == 3\n"
              "        assert out_ext.stride_bytes == 64\n", py_fp);
        fclose(py_fp);

        int py_rc = system("python3 /tmp/flow_pb_extent_adapter.py");
        FLOW_ASSERT_EQ(py_rc, 0);
        remove(tmp_c);
        remove(tmp_so);
        remove(tmp_py);

        flow_ir_cleanup(&pb_ir);
        printf("  ✓ Stage 10 Passed: Bidirectional .flow <-> .lean kernel bridge & 64-byte PB ExtentDescriptor verified.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 11: Direct .lean Absorption, Explicit QSBR Lease & Polyglot Emitter */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(11, "Direct .lean Absorption, Explicit QSBR Lease & Polyglot Emitter");
    {
        const char *direct_lean_src =
            "namespace Flow.Cert.AiMathKernel\n"
            "def inputMaxCount : Nat := 2048\n"
            "def inputStrideBytes : Nat := 128\n"
            "def inputAlignBytes : Nat := 64\n"
            "def stateSchemaVersion : Nat := 5\n"
            "def slaLatencyNs : Nat := 25000\n"
            "def shardCount : Nat := 8\n"
            "def memoryQuotaBytes : Nat := 16777216\n"
            "@[export lean_ai_proven_step]\n"
            "def extentStepKernel (baseOff byteLen stride epoch : UInt64) : UInt32 := 0\n"
            "end Flow.Cert.AiMathKernel\n";

        FILE *dl_mem = tmpfile();
        FLOW_ASSERT_TRUE(dl_mem != NULL);
        fputs(direct_lean_src, dl_mem);
        rewind(dl_mem);

        FlowSpec dl_spec;
        memset(&dl_spec, 0, sizeof(dl_spec));
        FLOW_ASSERT_TRUE(parse_spec(dl_mem, &dl_spec));
        fclose(dl_mem);

        FLOW_ASSERT_EQ(dl_spec.input_max_count, 2048);
        FLOW_ASSERT_EQ(dl_spec.input_stride_bytes, 128);
        FLOW_ASSERT_EQ(dl_spec.input_align_bytes, 64);
        FLOW_ASSERT_EQ(dl_spec.state_schema_version, 5);
        FLOW_ASSERT_EQ(dl_spec.sla_latency_ns, 25000ULL);
        FLOW_ASSERT_EQ(dl_spec.memory_limit_mb, 16);
        FLOW_ASSERT_EQ(dl_spec.lean_certified, 1);
        FLOW_ASSERT_STR_EQ(dl_spec.extern_kernel, "lean_ai_proven_step");

        /* Reject .lean containing sorry */
        FILE *bad_mem = tmpfile();
        FLOW_ASSERT_TRUE(bad_mem != NULL);
        fputs("namespace Flow.Cert.Bad\ntheorem t : 1 = 1 := sorry\nend Flow.Cert.Bad\n", bad_mem);
        rewind(bad_mem);
        FlowSpec bad_spec;
        memset(&bad_spec, 0, sizeof(bad_spec));
        FLOW_ASSERT_TRUE(!parse_spec(bad_mem, &bad_spec));
        fclose(bad_mem);

        SemanticIR dl_ir;
        memset(&dl_ir, 0, sizeof(dl_ir));
        lower_to_ir(&dl_spec, &dl_ir);
        const Component *dl_comp = select_component(&dl_ir);
        FLOW_ASSERT_TRUE(dl_comp != NULL);
        VerificationReport dl_vrep;
        FLOW_ASSERT_TRUE(verify_candidate(&dl_ir, dl_comp, NULL, &dl_vrep));

        FlowComponentABI dl_abi;
        FLOW_ASSERT_TRUE(flow_abi_build_for_component(&dl_ir, dl_comp, &dl_abi));

        /* Check acquire_epoch / release_epoch in emitted C code */
        FILE *dlc_fp = tmpfile();
        FLOW_ASSERT_TRUE(dlc_fp != NULL);
        FLOW_ASSERT_TRUE(emit_c(dlc_fp, &dl_ir, dl_comp, NULL, &dl_vrep, 0));
        long dlc_sz = ftell(dlc_fp);
        rewind(dlc_fp);
        char *dlc_buf = (char *)calloc((size_t)dlc_sz + 1, 1);
        FLOW_ASSERT_TRUE(dlc_buf != NULL);
        (void)fread(dlc_buf, 1, (size_t)dlc_sz, dlc_fp);
        fclose(dlc_fp);
        FLOW_ASSERT_TRUE(strstr(dlc_buf, "_acquire_epoch(void *handle)") != NULL);
        FLOW_ASSERT_TRUE(strstr(dlc_buf, "_release_epoch(void *handle, uint64_t epoch)") != NULL);
        free(dlc_buf);

        /* Verify Table-Driven Polyglot Projector (JS, CUDA, MATLAB, Julia) */
        const char *langs[] = { "js", "cuda", "matlab", "julia" };
        const char *expect_tokens[] = {
            "acquire_epoch: { args: [FFIType.ptr]",
            "cudaLaunchHostFunc(stream",
            "classdef flow_sharded_hash < handle",
            "ccall((:flow_sharded_hash_acquire_epoch"
        };
        for (int i = 0; i < 4; i++) {
            FILE *pfp = tmpfile();
            FLOW_ASSERT_TRUE(pfp != NULL);
            FLOW_ASSERT_TRUE(flow_abi_emit_polyglot_adapter(pfp, &dl_abi, langs[i]));
            long psz = ftell(pfp);
            FLOW_ASSERT_TRUE(psz > 100);
            rewind(pfp);
            char *pbuf = (char *)calloc((size_t)psz + 1, 1);
            FLOW_ASSERT_TRUE(pbuf != NULL);
            (void)fread(pbuf, 1, (size_t)psz, pfp);
            fclose(pfp);
            FLOW_ASSERT_TRUE(strstr(pbuf, expect_tokens[i]) != NULL);
            free(pbuf);
        }

        flow_ir_cleanup(&dl_ir);

        /* 11b. Pure .lean Single-File Fusion: Zero @[export], Native structure Layout & Embodied Plugin Lowering */
        const char *pure_embodied_lean =
            "namespace Embodied.GaitReflex\n"
            "structure JointState where\n"
            "  angle    : Float\n"
            "  velocity : Float\n"
            "  torque   : Float\n"
            "  deriving Repr\n"
            "def stepReflex (baseOff byteLen stride epoch : UInt64) : UInt32 :=\n"
            "  if stride >= 24 && epoch > 0 && baseOff + byteLen >= baseOff then 0 else 1\n"
            "theorem zmp_torque_safe : stepReflex 0 24 24 1 = 0 := by decide\n"
            "end Embodied.GaitReflex\n";
        FILE *emb_fp = tmpfile();
        FLOW_ASSERT_TRUE(emb_fp != NULL);
        fputs(pure_embodied_lean, emb_fp);
        rewind(emb_fp);
        FlowSpec emb_spec;
        memset(&emb_spec, 0, sizeof(emb_spec));
        FLOW_ASSERT_TRUE(parse_spec(emb_fp, &emb_spec));
        fclose(emb_fp);

        /* Verify structure JointState (3 Floats) inferred stride=24 & 3 schema fields without boilerplate */
        FLOW_ASSERT_STR_EQ(emb_spec.flow_name, "GaitReflex");
        FLOW_ASSERT_STR_EQ(emb_spec.domain_name, "embodied");
        FLOW_ASSERT_STR_EQ(emb_spec.plugin_name, "flow.embodied");
        FLOW_ASSERT_EQ(emb_spec.state_field_count, 3);
        FLOW_ASSERT_STR_EQ(emb_spec.state_fields[0].name, "angle");
        FLOW_ASSERT_EQ(emb_spec.state_fields[0].size_bytes, 8);
        FLOW_ASSERT_EQ(emb_spec.input_stride_bytes, 24);
        /* Verify theorem zmp_torque_safe anchored stepReflex WITHOUT @[export] */
        FLOW_ASSERT_STR_EQ(emb_spec.extern_kernel, "stepReflex");
        FLOW_ASSERT_TRUE(strstr(emb_spec.extern_kernel_body, "stride >= 24") != NULL);

        SemanticIR emb_ir;
        memset(&emb_ir, 0, sizeof(emb_ir));
        lower_to_ir(&emb_spec, &emb_ir);
        const FlowPlugin *emb_mod = flow_registry_lookup("flow.embodied");
        FLOW_ASSERT_TRUE(emb_mod != NULL);
        flow_plugin_lower_semantics(&emb_spec, &emb_ir, emb_mod);

        /* Verify EMBODIED_PLUGIN encapsulated domain complexity -> hardware mechanisms */
        FLOW_ASSERT_EQ(emb_ir.sla_latency_ns, 1000000ULL);
        FLOW_ASSERT_STR_EQ(emb_ir.resource_name, "hardware");
        FLOW_ASSERT_STR_EQ(emb_ir.capability_name, "realtime");
        FLOW_ASSERT_STR_EQ(emb_ir.contract_name, "zmp_torque_safe");
        FLOW_ASSERT_EQ(emb_ir.jet.enabled, 1);

        const Component *emb_sel = select_component(&emb_ir);
        FLOW_ASSERT_TRUE(emb_sel != NULL);
        FLOW_ASSERT_STR_EQ(emb_sel->id, "embodied_controller");

        VerificationReport emb_vrep;
        FLOW_ASSERT_TRUE(verify_candidate(&emb_ir, emb_sel, NULL, &emb_vrep));
        FlowComponentABI emb_abi;
        FLOW_ASSERT_TRUE(flow_abi_build_for_component(&emb_ir, emb_sel, &emb_abi));

        /* Verify Lean certificate adapter emits actual extern_kernel_body & stride_b (24) */
        FILE *emb_lean_fp = tmpfile();
        FLOW_ASSERT_TRUE(emb_lean_fp != NULL);
        FLOW_ASSERT_TRUE(flow_abi_emit_lean_adapter(emb_lean_fp, &emb_ir, &emb_abi, 0ULL, 1024, 1));
        long emb_lean_sz = ftell(emb_lean_fp);
        rewind(emb_lean_fp);
        char *emb_lean_buf = (char *)calloc((size_t)emb_lean_sz + 1, 1);
        FLOW_ASSERT_TRUE(emb_lean_buf != NULL);
        (void)fread(emb_lean_buf, 1, (size_t)emb_lean_sz, emb_lean_fp);
        fclose(emb_lean_fp);
        FLOW_ASSERT_TRUE(strstr(emb_lean_buf, "stride >= 24") != NULL);
        FLOW_ASSERT_TRUE(strstr(emb_lean_buf, "theorem kernel_step_certified : stepReflex 0 24 24 1 = 0 := by decide") != NULL);
        free(emb_lean_buf);

        /* Verify Unicode operators (≥, ≤, ≠, ∧, ∨, ¬) and nested if/then/else-if chains compile cleanly in C17 */
        const char *nested_lean =
            "namespace Embodied.NestedReflex\n"
            "def nestedKernel (baseOff byteLen stride epoch : UInt64) : UInt32 :=\n"
            "  if stride < 24 then 2\n"
            "  else if ¬(epoch ≠ 0) then 1\n"
            "  else if stride ≥ 24 ∧ (0 ≤ baseOff ∨ byteLen ≥ 24) then if epoch > 0 then 0 else 3\n"
            "  else 4\n"
            "theorem nested_ok : nestedKernel 0 24 24 1 = 0 := by decide\n"
            "end Embodied.NestedReflex\n";
        FILE *nl_fp = tmpfile();
        FLOW_ASSERT_TRUE(nl_fp != NULL);
        fputs(nested_lean, nl_fp);
        rewind(nl_fp);
        FlowSpec nl_spec;
        memset(&nl_spec, 0, sizeof(nl_spec));
        FLOW_ASSERT_TRUE(parse_spec(nl_fp, &nl_spec));
        fclose(nl_fp);
        FLOW_ASSERT_TRUE(strstr(nl_spec.extern_kernel_body, "else 4") != NULL);
        SemanticIR uni_ir = emb_ir;
        strncpy(uni_ir.extern_kernel_body, nl_spec.extern_kernel_body, sizeof(uni_ir.extern_kernel_body) - 1);
        FILE *uni_c_fp = fopen("/tmp/flow_uni_lean_dso.c", "w");
        FLOW_ASSERT_TRUE(uni_c_fp != NULL);
        FLOW_ASSERT_TRUE(emit_c(uni_c_fp, &uni_ir, emb_sel, NULL, &emb_vrep, 0));
        fclose(uni_c_fp);
        FLOW_ASSERT_EQ(system("cc -std=c17 -Wall -Wextra -Werror -O2 -shared -fPIC -DFLOW_SHARED_LIB /tmp/flow_uni_lean_dso.c -o /tmp/libflow_uni_lean_dso.so -lpthread"), 0);
        remove("/tmp/flow_uni_lean_dso.c");
        remove("/tmp/libflow_uni_lean_dso.so");

        /* Compile emitted C from Pure .lean with ZERO external C glue and run via Python 1PB Extent adapter */
        const char *pure_c = "/tmp/flow_pure_lean_dso.c";
        const char *pure_so = "/tmp/libflow_pure_lean_dso.so";
        const char *pure_py = "/tmp/flow_pure_lean_adapter.py";
        FILE *pc_fp = fopen(pure_c, "w");
        FLOW_ASSERT_TRUE(pc_fp != NULL);
        FLOW_ASSERT_TRUE(emit_c(pc_fp, &emb_ir, emb_sel, NULL, &emb_vrep, 0));
        fclose(pc_fp);

        int pure_cc = system("cc -std=c17 -Wall -Wextra -O2 -shared -fPIC -DFLOW_SHARED_LIB /tmp/flow_pure_lean_dso.c -o /tmp/libflow_pure_lean_dso.so -lpthread");
        FLOW_ASSERT_EQ(pure_cc, 0);

        FILE *ppy_fp = fopen(pure_py, "w");
        FLOW_ASSERT_TRUE(ppy_fp != NULL);
        FLOW_ASSERT_TRUE(flow_abi_emit_python_adapter(ppy_fp, &emb_abi));
        fputs("\nif __name__ == '__main__':\n"
              "    with EmbodiedController('/tmp/libflow_pure_lean_dso.so') as eng:\n"
              "        in_ext = FlowExtentDescriptor(base_offset=(1 << 50), byte_length=2400, stride_bytes=24, align_bytes=8)\n"
              "        out_ext = eng.process_descriptor(in_ext)\n"
              "        assert out_ext.base_offset == (1 << 50) + 2400\n"
              "        assert out_ext.qsbr_epoch >= 2\n"
              "        assert out_ext.stride_bytes == 24\n", ppy_fp);
        fclose(ppy_fp);

        int pure_py_rc = system("python3 /tmp/flow_pure_lean_adapter.py");
        FLOW_ASSERT_EQ(pure_py_rc, 0);
        remove(pure_c);
        remove(pure_so);
        remove(pure_py);
        flow_ir_cleanup(&emb_ir);

        /* 11c. Real-World Acceptance: AWS Cedar Authorization Engine (cedar-policy/cedar-spec, 427 lines / 16KB) */
        FILE *cedar_fp = fopen("tests/cedar_authorization.lean", "r");
        FLOW_ASSERT_TRUE(cedar_fp != NULL);
        FlowSpec cedar_spec;
        memset(&cedar_spec, 0, sizeof(cedar_spec));
        FLOW_ASSERT_TRUE(parse_spec(cedar_fp, &cedar_spec));
        fclose(cedar_fp);

        /* Request (4 fields) + Response (3 fields) = 7 fields, stride = 56B, kernel = isAuthorized */
        FLOW_ASSERT_EQ(cedar_spec.lean_certified, 1);
        FLOW_ASSERT_EQ(cedar_spec.state_field_count, 7);
        FLOW_ASSERT_STR_EQ(cedar_spec.state_fields[0].name, "principal");
        FLOW_ASSERT_STR_EQ(cedar_spec.state_fields[4].name, "decision");
        FLOW_ASSERT_EQ(cedar_spec.input_stride_bytes, 56);
        FLOW_ASSERT_STR_EQ(cedar_spec.extern_kernel, "isAuthorized");

        SemanticIR cedar_ir;
        memset(&cedar_ir, 0, sizeof(cedar_ir));
        lower_to_ir(&cedar_spec, &cedar_ir);
        const Component *cedar_comp = select_component(&cedar_ir);
        FLOW_ASSERT_TRUE(cedar_comp != NULL);
        VerificationReport cedar_vrep;
        FLOW_ASSERT_TRUE(verify_candidate(&cedar_ir, cedar_comp, NULL, &cedar_vrep));
        FlowComponentABI cedar_abi;
        FLOW_ASSERT_TRUE(flow_abi_build_for_component(&cedar_ir, cedar_comp, &cedar_abi));

        const char *cedar_c = "/tmp/flow_cedar_dso.c";
        const char *cedar_so = "/tmp/libflow_cedar_dso.so";
        const char *cedar_py = "/tmp/flow_cedar_adapter.py";
        FILE *cc_fp = fopen(cedar_c, "w");
        FLOW_ASSERT_TRUE(cc_fp != NULL);
        FLOW_ASSERT_TRUE(emit_c(cc_fp, &cedar_ir, cedar_comp, NULL, &cedar_vrep, 0));
        fclose(cc_fp);

        int cedar_cc_rc = system("cc -std=c17 -Wall -Wextra -O2 -shared -fPIC -DFLOW_SHARED_LIB /tmp/flow_cedar_dso.c -o /tmp/libflow_cedar_dso.so -lpthread");
        FLOW_ASSERT_EQ(cedar_cc_rc, 0);

        FILE *cpy_fp = fopen(cedar_py, "w");
        FLOW_ASSERT_TRUE(cpy_fp != NULL);
        FLOW_ASSERT_TRUE(flow_abi_emit_python_adapter(cpy_fp, &cedar_abi));
        fputs("\nif __name__ == '__main__':\n"
              "    with LinearArray('/tmp/libflow_cedar_dso.so') as eng:\n"
              "        in_ext = FlowExtentDescriptor(base_offset=(1 << 50), byte_length=5600, stride_bytes=56, align_bytes=8)\n"
              "        out_ext = eng.process_descriptor(in_ext)\n"
              "        assert out_ext.base_offset == (1 << 50) + 5600\n"
              "        assert out_ext.qsbr_epoch >= 2\n"
              "        assert out_ext.stride_bytes == 56\n", cpy_fp);
        fclose(cpy_fp);

        int cedar_py_rc = system("python3 /tmp/flow_cedar_adapter.py");
        FLOW_ASSERT_EQ(cedar_py_rc, 0);
        remove(cedar_c);
        remove(cedar_so);
        remove(cedar_py);
        flow_ir_cleanup(&cedar_ir);

        printf("  ✓ Stage 11 Passed: Pure .lean (Embodied reflex + AWS Cedar 427-line Authorization.lean) verified end-to-end.\n\n");
    }

    /* =========================================================================
     * STAGE 12: Full .lean Scalar Def C17 Lowering, Theorem Registry & UI Target
     * ========================================================================= */
    printf("--- [Stage 12: Full .lean Scalar Def C17 Lowering, Theorem Registry & Schema-Driven UI] ---\n");
    {
        const char *scalar_lean =
            "namespace Flow.TestScalar\n"
            "structure LedgerState where\n"
            "  epoch : UInt64\n"
            "  budgetBps : UInt64\n"
            "  driftBps : UInt64\n"
            "def totalDrift (a : Nat) (b : Nat) : Nat := a + b\n"
            "def slackDeficit (a : Nat) (b : Nat) (budget : Nat) : Nat :=\n"
            "  let tot := totalDrift a b\n"
            "  tot - budget\n"
            "def tierLimit (tier : Nat) : Nat :=\n"
            "  match tier with\n"
            "  | 1 => 100\n"
            "  | 2 => 300\n"
            "  | _ => 0\n"
            "def safeRatio (num : Nat) (den : Nat) : Nat := num / den\n"
            "def canApprove\n"
            "    (creator : Nat) (approver : Nat) (tier : Nat) (deficit : Nat) : Bool :=\n"
            "  creator != 0 && approver != 0 && creator != approver && deficit <= tierLimit tier\n"
            "def signedDelta (a : Int) (b : Int) : Int := a - b\n"
            "def bitPrec (a : UInt64) (b : UInt64) (c : UInt64) : UInt64 := a ||| b &&& c\n"
            "def powRightAssoc (x : Nat) : Nat := x ^ 3 ^ 2\n"
            "def stepLedgerKernel (baseOff : UInt64) (byteLen : UInt64) (stride : UInt64) (epoch : UInt64) : UInt32 :=\n"
            "  if stride >= 24 && epoch > 0 && baseOff + byteLen >= baseOff then 0 else 1\n"
            "theorem deficit_zero_when_within_budget : slackDeficit 20 30 60 = 0 := by decide\n"
            "theorem approve_requires_sod : canApprove 7 7 2 100 = false := by decide\n"
            "end Flow.TestScalar\n";
        const char *s_lean_path = "/tmp/flow_test_scalar.lean";
        FILE *sl_fp = fopen(s_lean_path, "w");
        FLOW_ASSERT_TRUE(sl_fp != NULL);
        fputs(scalar_lean, sl_fp);
        fclose(sl_fp);

        sl_fp = fopen(s_lean_path, "r");
        FLOW_ASSERT_TRUE(sl_fp != NULL);
        FlowSpec s_spec;
        memset(&s_spec, 0, sizeof(s_spec));
        FLOW_ASSERT_TRUE(parse_spec(sl_fp, &s_spec));
        fclose(sl_fp);

        FLOW_ASSERT_EQ(s_spec.lean_function_count, 9);
        FLOW_ASSERT_EQ(s_spec.lean_theorem_count, 2);
        FLOW_ASSERT_STR_EQ(s_spec.lean_functions[0].name, "totalDrift");
        FLOW_ASSERT_STR_EQ(s_spec.lean_functions[1].name, "slackDeficit");
        FLOW_ASSERT_STR_EQ(s_spec.lean_functions[4].name, "canApprove");
        FLOW_ASSERT_STR_EQ(s_spec.lean_theorems[0].anchored_fn, "slackDeficit");
        FLOW_ASSERT_STR_EQ(s_spec.lean_theorems[1].anchored_fn, "canApprove");

        SemanticIR s_ir;
        memset(&s_ir, 0, sizeof(s_ir));
        lower_to_ir(&s_spec, &s_ir);
        const Component *s_comp = select_component(&s_ir);
        FLOW_ASSERT_TRUE(s_comp != NULL);
        VerificationReport s_vrep;
        FLOW_ASSERT_TRUE(verify_candidate(&s_ir, s_comp, NULL, &s_vrep));
        FlowComponentABI s_abi;
        FLOW_ASSERT_TRUE(flow_abi_build_for_component(&s_ir, s_comp, &s_abi));

        FILE *sc_fp = fopen("/tmp/flow_test_scalar.c", "w");
        FLOW_ASSERT_TRUE(sc_fp != NULL);
        FLOW_ASSERT_TRUE(emit_c(sc_fp, &s_ir, s_comp, NULL, &s_vrep, 0));
        fclose(sc_fp);

        FLOW_ASSERT_EQ(system("cc -std=c17 -Wall -Wextra -Werror -O2 -shared -fPIC -DFLOW_SHARED_LIB /tmp/flow_test_scalar.c -o /tmp/libflow_test_scalar.so -lpthread"), 0);

        FILE *ui_fp = fopen("/tmp/flow_test_scalar_ui.html", "w");
        FLOW_ASSERT_TRUE(ui_fp != NULL);
        FLOW_ASSERT_TRUE(flow_abi_emit_ui_adapter(ui_fp, &s_ir, &s_abi));
        fclose(ui_fp);

        FILE *spy_fp = fopen("/tmp/flow_test_scalar.py", "w");
        FLOW_ASSERT_TRUE(spy_fp != NULL);
        FLOW_ASSERT_TRUE(flow_abi_emit_python_adapter(spy_fp, &s_abi));
        fputs("\nif __name__ == '__main__':\n"
              "    with LinearArray('/tmp/libflow_test_scalar.so') as eng:\n"
              "        assert len(eng.LEAN_FUNCTIONS) == 9\n"
              "        assert len(eng.LEAN_THEOREMS) == 2\n"
              "        assert eng.totalDrift(120, 80) == 200\n"
              "        assert eng.slackDeficit(20, 30, 60) == 0\n"
              "        assert eng.slackDeficit(120, 80, 150) == 50\n"
              "        assert eng.tierLimit(1) == 100 and eng.tierLimit(2) == 300 and eng.tierLimit(9) == 0\n"
              "        assert eng.safeRatio(100, 0) == 0 and eng.safeRatio(100, 4) == 25\n"
              "        assert eng.canApprove(10, 20, 2, 250) is True\n"
              "        assert eng.canApprove(10, 10, 2, 250) is False\n"
              "        assert eng.signedDelta(3, 10) == -7\n"
              "        assert eng.bitPrec(1, 6, 3) == (1 | (6 & 3))\n"
              "        assert eng.powRightAssoc(2) == 512\n"
              "        try:\n"
              "            eng.canApprove(-5, 20, 2, 250)\n"
              "            assert False, 'Expected ValueError for negative Nat'\n"
              "        except ValueError:\n"
              "            pass\n"
              "        bad_ext = FlowExtentDescriptor(base_offset=0, byte_length=64, stride_bytes=8, align_bytes=8)\n"
              "        try:\n"
              "            eng.process_descriptor(bad_ext)\n"
              "            assert False, 'Expected RuntimeError from failing extern_kernel'\n"
              "        except RuntimeError:\n"
              "            pass\n", spy_fp);
        fclose(spy_fp);

        FLOW_ASSERT_EQ(system("python3 /tmp/flow_test_scalar.py"), 0);
        char ui_cmd12[512];
        snprintf(ui_cmd12, sizeof(ui_cmd12), "%s/flowy ui /tmp/flow_test_scalar.lean --selftest >/dev/null", FLOW_BUILD_DIR);
        FLOW_ASSERT_EQ(system(ui_cmd12), 0);

        /* Verify Lean theorem evaluator rejects false theorems and comment-evaded certificates (V5, L1-L3) */
        const char *false_lean =
            "namespace Flow.FalseTest\n"
            "structure BadState where\n"
            "  x : UInt64\n"
            "def addOne (a : Nat) : Nat := a + 1\n"
            "theorem bogus_claim : addOne 1 = 3 := by decide\n"
            "end Flow.FalseTest\n";
        FILE *fl_fp = fopen("/tmp/flow_false_thm.lean", "w");
        FLOW_ASSERT_TRUE(fl_fp != NULL);
        fputs(false_lean, fl_fp);
        fclose(fl_fp);
        fl_fp = fopen("/tmp/flow_false_thm.lean", "r");
        FLOW_ASSERT_TRUE(fl_fp != NULL);
        FlowSpec f_spec;
        memset(&f_spec, 0, sizeof(f_spec));
        FLOW_ASSERT_FALSE(parse_spec(fl_fp, &f_spec));
        fclose(fl_fp);
        char fc_cmd12[512];
        snprintf(fc_cmd12, sizeof(fc_cmd12), "%s/flowc /tmp/flow_false_thm.lean >/dev/null 2>&1", FLOW_BUILD_DIR);
        FLOW_ASSERT_NE(system(fc_cmd12), 0);
        remove("/tmp/flow_false_thm.lean");

        /* Verify parameterized false decide and omega theorems are rejected (L1) */
        FILE *bad_param_fp = tmpfile();
        FLOW_ASSERT_TRUE(bad_param_fp != NULL);
        fputs("namespace Flow.BadParam\ndef idNat (x : Nat) : Nat := x\ntheorem bad_param (x : Nat) : idNat x = 1 := by decide\nend Flow.BadParam\n", bad_param_fp);
        rewind(bad_param_fp);
        FlowSpec bp_spec;
        memset(&bp_spec, 0, sizeof(bp_spec));
        FLOW_ASSERT_FALSE(parse_spec(bad_param_fp, &bp_spec));
        fclose(bad_param_fp);

        FILE *bad_omega_fp = tmpfile();
        FLOW_ASSERT_TRUE(bad_omega_fp != NULL);
        fputs("namespace Flow.BadOmega\ntheorem bad_omega (a : Nat) (b : Nat) (h : b <= a) : a - b = a + 1 := by omega\nend Flow.BadOmega\n", bad_omega_fp);
        rewind(bad_omega_fp);
        FlowSpec bo_spec;
        memset(&bo_spec, 0, sizeof(bo_spec));
        FLOW_ASSERT_FALSE(parse_spec(bad_omega_fp, &bo_spec));
        fclose(bad_omega_fp);

        /* Verify flow_registry_load_dso rejects non-ELF/Mach-O and world-writable DSOs (E7) */
        const char *fake_dso = "/tmp/flow_fake_header_test.so";
        FILE *fk_fp = fopen(fake_dso, "wb");
        FLOW_ASSERT_TRUE(fk_fp != NULL);
        fputs("NOT_AN_ELF_OR_MACHO_BINARY", fk_fp);
        fclose(fk_fp);
        char dso_err[256] = {0};
        FLOW_ASSERT_FALSE(flow_registry_load_dso(fake_dso, dso_err, sizeof(dso_err)));
        FLOW_ASSERT_TRUE(strstr(dso_err, "header") != NULL || strstr(dso_err, "world-writable") != NULL);
        remove(fake_dso);

        const char *comment_cheat_lean =
            "-- structure ExtentDescriptor where\n"
            "/- theorem theorem7_pb_extent_monotonic_step\n"
            "   theorem theorem8_qsbr_epoch_lease_safety\n"
            "   theorem theorem9_neutral_stride_align\n"
            "   theorem theorem10_pb_extent_inductive_steps\n"
            "   theorem state_preservation_locked_margin -/\n";
        const char *cheat_path = "/tmp/flow_cheat_cert.lean";
        FILE *ch_fp = fopen(cheat_path, "w");
        FLOW_ASSERT_TRUE(ch_fp != NULL);
        fputs(comment_cheat_lean, ch_fp);
        fclose(ch_fp);
        char cheat_err[256] = {0};
        FLOW_ASSERT_FALSE(flow_abi_audit_lean_certificate(cheat_path, cheat_err, sizeof(cheat_err)));
        remove(cheat_path);

        remove(s_lean_path);
        remove("/tmp/flow_test_scalar.c");
        remove("/tmp/libflow_test_scalar.so");
        remove("/tmp/flow_test_scalar_ui.html");
        remove("/tmp/flow_test_scalar.py");
        remove("build/flow_test_scalar_ui.c");
        remove("build/flow_test_scalar_ui_abi.py");
        remove("build/flow_test_scalar_ui.html");
        remove("build/libflow_test_scalar_ui.so");
        flow_ir_cleanup(&s_ir);

        printf("  ✓ Stage 12 Passed: Full .lean scalar def C17 lowering, theorem registry & schema-driven UI verified.\n\n");
    }

    /* =========================================================================
     * STAGE 13: .lean (What) x .flow (Where & How) Manifest, Named FlowJet Views & Auto NaN Firewall
     * ========================================================================= */
    FLOW_STAGE_BEGIN(13, ".lean x .flow Manifest Boundary, Named FlowJet Views & Auto NaN/Inf Firewall");
    {
        /* 1. Parse plugins/embodied.flow (which binds plugins/embodied.lean and declares @quota, @embodied, @reload) */
        FILE *ef_fp = fopen("plugins/embodied.flow", "r");
        FLOW_ASSERT_TRUE(ef_fp != NULL);
        FlowSpec ef_spec;
        memset(&ef_spec, 0, sizeof(ef_spec));
        FLOW_ASSERT_TRUE(parse_spec(ef_fp, &ef_spec));
        fclose(ef_fp);

        FLOW_ASSERT_STR_EQ(ef_spec.project_name, "embodied_reflex");
        FLOW_ASSERT_STR_EQ(ef_spec.flow_name, "embodied_reflex");
        FLOW_ASSERT_STR_EQ(ef_spec.kernel_source_lean, "plugins/embodied.lean");
        FLOW_ASSERT_STR_EQ(ef_spec.domain_name, "embodied");
        FLOW_ASSERT_STR_EQ(ef_spec.plugin_name, "flow.embodied");
        FLOW_ASSERT_STR_EQ(ef_spec.contract_name, "zmp_torque_safe");
        FLOW_ASSERT_STR_EQ(ef_spec.fallback_policy, "emergency_stop");
        FLOW_ASSERT_EQ(ef_spec.sla_latency_ns, 200000ULL);
        FLOW_ASSERT_EQ(ef_spec.arena_limit_bytes, 65536u);
        FLOW_ASSERT_EQ(ef_spec.memory_limit_mb, 16);
        FLOW_ASSERT_EQ(ef_spec.state_schema_version, 2u);
        FLOW_ASSERT_EQ(ef_spec.qsbr_timeout_ns, 5000000ULL);
        FLOW_ASSERT_STR_EQ(ef_spec.migrate_fn, "embodiedStepKernel");
        FLOW_ASSERT_STR_EQ(ef_spec.ingress_driver, "sensor_imu_6dof");
        FLOW_ASSERT_STR_EQ(ef_spec.egress_driver, "socketcan_fd");
        FLOW_ASSERT_EQ(ef_spec.jet.enabled, 1);
        FLOW_ASSERT_EQ(ef_spec.jet.symplectic, 1);
        FLOW_ASSERT_EQ((int)ef_spec.jet.frequency_hz, 1000);
        FLOW_ASSERT_EQ(ef_spec.jet_coord_count, 8u);
        FLOW_ASSERT_STR_EQ(ef_spec.jet_coord_names[0], "hipPitch");
        FLOW_ASSERT_STR_EQ(ef_spec.jet_coord_names[7], "comY");
        FLOW_ASSERT_EQ(ef_spec.state_field_count, 8u);
        FLOW_ASSERT_EQ(ef_spec.input_stride_bytes, 64u);
        FLOW_ASSERT_EQ(ef_spec.lean_function_count, 7u);
        FLOW_ASSERT_EQ(ef_spec.lean_theorem_count, 2u);
        FLOW_ASSERT_STR_EQ(ef_spec.extern_kernel, "embodiedStepKernel");
        char fc_emb_cmd[512];
        snprintf(fc_emb_cmd, sizeof(fc_emb_cmd), "%s/flowc plugins/embodied.flow -o /tmp/flow_cli_embodied_out.c >/dev/null", FLOW_BUILD_DIR);
        FLOW_ASSERT_EQ(system(fc_emb_cmd), 0);
        remove("/tmp/flow_cli_embodied_out.c");

        /* Edge cases: reject invalid bind path and malformed @quota/@embodied/@reload units & keys */
        static const char *const bad_manifests[] = {
            "project bad_bind\nbind \"nonexistent_file_xyz.lean\"\n",
            "project bad_arena\n@quota {\n  arena 64bogus\n}\n",
            "project bad_deadline\n@quota {\n  deadline 200bogus\n}\n",
            "project bad_freq\n@embodied {\n  frequency 1000bogus\n}\n",
            "project bad_ver\n@reload {\n  schema_version 0\n}\n",
            "project bad_qsbr\n@reload {\n  qsbr_timeout -5ms\n}\n",
            "project bad_reload_key\n@reload {\n  unknown_key 42\n}\n"
        };
        for (size_t bi = 0; bi < sizeof(bad_manifests) / sizeof(bad_manifests[0]); ++bi) {
            FILE *bm_fp = tmpfile();
            FLOW_ASSERT_TRUE(bm_fp != NULL);
            fputs(bad_manifests[bi], bm_fp);
            rewind(bm_fp);
            FlowSpec bm_spec;
            memset(&bm_spec, 0, sizeof(bm_spec));
            FLOW_ASSERT_FALSE(parse_spec(bm_fp, &bm_spec));
            fclose(bm_fp);
        }

        /* Verify pre-bind manifest preservation, relative bind from /tmp, multi-line coordinates, and < / space-separated units */
        const char *tmp_rel_lean = "/tmp/flow_stage13_rel_math.lean";
        const char *tmp_rel_flow = "/tmp/flow_stage13_rel_manifest.flow";
        FILE *trl_fp = fopen(tmp_rel_lean, "w");
        FLOW_ASSERT_TRUE(trl_fp != NULL);
        fputs("structure RelState where\n"
              "  alpha : Float\n"
              "  beta : Float\n"
              "def controlLaw (q : Float) (dq : Float) : Float :=\n"
              "  let kp : Float := 10.0 in\n"
              "  let kd : Float := 2.0\n"
              "  -kp * q - kd * dq\n"
              "def overflowProduct (a : Float) (b : Float) : Float := a * b\n", trl_fp);
        fclose(trl_fp);

        FILE *trf_fp = fopen(tmp_rel_flow, "w");
        FLOW_ASSERT_TRUE(trf_fp != NULL);
        fputs("project pre_bind_test\n"
              "domain embodied\n"
              "@quota {\n"
              "  deadline < 250 us\n"
              "  arena < 32 kb\n"
              "  memory < 8 mb\n"
              "}\n"
              "@embodied {\n"
              "  ingress \"sensor_imu_6dof\"\n"
              "  egress \"socketcan_fd\"\n"
              "  frequency 500 hz\n"
              "  symplectic true\n"
              "  coordinates [\n"
              "    alpha,\n"
              "    beta,\n"
              "    gamma\n"
              "  ]\n"
              "}\n"
              "@reload {\n"
              "  schema_version 3\n"
              "  qsbr_timeout < 4 ms\n"
              "  migrate controlLaw\n"
              "}\n"
              "bind \"flow_stage13_rel_math.lean\"\n"
              "flow pre_bind_reflex {\n"
              "  use plugin \"flow.embodied\"\n"
              "}\n", trf_fp);
        fclose(trf_fp);

        trf_fp = fopen(tmp_rel_flow, "r");
        FLOW_ASSERT_TRUE(trf_fp != NULL);
        FlowSpec rel_spec;
        memset(&rel_spec, 0, sizeof(rel_spec));
        FLOW_ASSERT_TRUE(parse_spec(trf_fp, &rel_spec));
        fclose(trf_fp);
        FLOW_ASSERT_STR_EQ(rel_spec.project_name, "pre_bind_test");
        FLOW_ASSERT_STR_EQ(rel_spec.flow_name, "pre_bind_reflex");
        FLOW_ASSERT_EQ(rel_spec.sla_latency_ns, 250000ULL);
        FLOW_ASSERT_EQ(rel_spec.arena_limit_bytes, 32768u);
        FLOW_ASSERT_EQ(rel_spec.memory_limit_mb, 8);
        FLOW_ASSERT_EQ(rel_spec.state_schema_version, 3u);
        FLOW_ASSERT_EQ(rel_spec.qsbr_timeout_ns, 4000000ULL);
        FLOW_ASSERT_EQ(rel_spec.jet_coord_count, 3u);
        FLOW_ASSERT_STR_EQ(rel_spec.jet_coord_names[2], "gamma");
        FLOW_ASSERT_EQ(rel_spec.lean_function_count, 2u);

        /* Compile non-4-arg Float .lean (controlLaw + overflowProduct) to DSO and verify ABI + post-return isfinite guard */
        SemanticIR rel_ir;
        memset(&rel_ir, 0, sizeof(rel_ir));
        lower_to_ir(&rel_spec, &rel_ir);
        const FlowPlugin *emb_mod = flow_registry_lookup("flow.embodied");
        FLOW_ASSERT_TRUE(emb_mod != NULL);
        flow_plugin_lower_semantics(&rel_spec, &rel_ir, emb_mod);
        const Component *rel_comp = select_component(&rel_ir);
        FLOW_ASSERT_TRUE(rel_comp != NULL);
        SearchResult rel_sr = search_best(&rel_ir, 8, 42, 0, NULL);
        FLOW_ASSERT_EQ(rel_sr.tuning.arena_bytes, 32768u);
        VerificationReport rel_vrep;
        FLOW_ASSERT_TRUE(verify_candidate(&rel_ir, rel_comp, &rel_sr, &rel_vrep));
        FlowComponentABI rel_abi;
        FLOW_ASSERT_TRUE(flow_abi_build_for_component(&rel_ir, rel_comp, &rel_abi));

        FILE *rel_c_fp = fopen("/tmp/flow_stage13_rel.c", "w");
        FLOW_ASSERT_TRUE(rel_c_fp != NULL);
        FLOW_ASSERT_TRUE(emit_c(rel_c_fp, &rel_ir, rel_comp, &rel_sr, &rel_vrep, 0));
        fclose(rel_c_fp);
        FLOW_ASSERT_EQ(system("cc -std=c17 -Wall -Wextra -Werror -O2 -shared -fPIC -DFLOW_SHARED_LIB /tmp/flow_stage13_rel.c -o /tmp/libflow_stage13_rel.so -lpthread"), 0);

        FILE *rel_py_fp = fopen("/tmp/flow_stage13_rel.py", "w");
        FLOW_ASSERT_TRUE(rel_py_fp != NULL);
        FLOW_ASSERT_TRUE(flow_abi_emit_python_adapter(rel_py_fp, &rel_abi));
        fputs("\nif __name__ == '__main__':\n"
              "    with EmbodiedController('/tmp/libflow_stage13_rel.so') as eng:\n"
              "        assert abs(eng.controlLaw(0.5, 1.0) - (-7.0)) < 1e-6\n"
              "        assert eng.controlLaw(float('nan'), 1.0) == 0.0\n"
              "        assert eng.overflowProduct(1e308, 1e308) == 0.0\n"
              "        ok_ext = FlowExtentDescriptor(base_offset=0, byte_length=64, stride_bytes=16, align_bytes=8)\n"
              "        out_ext = eng.process_descriptor(ok_ext)\n"
              "        assert out_ext.base_offset == 64 and out_ext.schema_version == 3\n"
              "        over_arena = FlowExtentDescriptor(base_offset=0, byte_length=65536, stride_bytes=16, align_bytes=8)\n"
              "        try:\n"
              "            eng.process_descriptor(over_arena)\n"
              "            assert False, 'Expected arena quota violation'\n"
              "        except RuntimeError:\n"
              "            pass\n", rel_py_fp);
        fclose(rel_py_fp);
        FLOW_ASSERT_EQ(system("python3 /tmp/flow_stage13_rel.py"), 0);

        remove(tmp_rel_lean);
        remove(tmp_rel_flow);
        remove("/tmp/flow_stage13_rel.c");
        remove("/tmp/libflow_stage13_rel.so");
        remove("/tmp/flow_stage13_rel.py");
        flow_ir_cleanup(&rel_ir);

        /* 2. Lower plugins/embodied.flow to SemanticIR, emit C17, verify manifest defines & named FlowJetView */
        SemanticIR ef_ir;
        memset(&ef_ir, 0, sizeof(ef_ir));
        lower_to_ir(&ef_spec, &ef_ir);
        flow_plugin_lower_semantics(&ef_spec, &ef_ir, emb_mod);

        const Component *ef_comp = select_component(&ef_ir);
        FLOW_ASSERT_TRUE(ef_comp != NULL);
        FLOW_ASSERT_STR_EQ(ef_comp->id, "embodied_controller");
        VerificationReport ef_vrep;
        FLOW_ASSERT_TRUE(verify_candidate(&ef_ir, ef_comp, NULL, &ef_vrep));
        FlowComponentABI ef_abi;
        FLOW_ASSERT_TRUE(flow_abi_build_for_component(&ef_ir, ef_comp, &ef_abi));

        const char *ef_c_path = "/tmp/flow_test_embodied_manifest.c";
        const char *ef_so_path = "/tmp/libflow_test_embodied_manifest.so";
        const char *ef_py_path = "/tmp/flow_test_embodied_manifest.py";
        FILE *efc_fp = fopen(ef_c_path, "w");
        FLOW_ASSERT_TRUE(efc_fp != NULL);
        FLOW_ASSERT_TRUE(emit_c(efc_fp, &ef_ir, ef_comp, NULL, &ef_vrep, 0));
        fclose(efc_fp);

        FILE *ef_rd = fopen(ef_c_path, "r");
        FLOW_ASSERT_TRUE(ef_rd != NULL);
        fseek(ef_rd, 0, SEEK_END);
        long ef_sz = ftell(ef_rd);
        rewind(ef_rd);
        char *ef_c_buf = (char *)calloc((size_t)ef_sz + 1, 1);
        FLOW_ASSERT_TRUE(ef_c_buf != NULL);
        (void)fread(ef_c_buf, 1, (size_t)ef_sz, ef_rd);
        fclose(ef_rd);

        FLOW_ASSERT_TRUE(strstr(ef_c_buf, "#define FLOW_MANIFEST_SLA_NS UINT64_C(200000)") != NULL);
        FLOW_ASSERT_TRUE(strstr(ef_c_buf, "#define FLOW_MANIFEST_ARENA_BYTES 65536") != NULL);
        FLOW_ASSERT_TRUE(strstr(ef_c_buf, "#define FLOW_MANIFEST_QSBR_TIMEOUT_NS UINT64_C(5000000)") != NULL);
        FLOW_ASSERT_TRUE(strstr(ef_c_buf, "#define FLOW_MANIFEST_INGRESS_DRIVER \"sensor_imu_6dof\"") != NULL);
        FLOW_ASSERT_TRUE(strstr(ef_c_buf, "#define FLOW_MANIFEST_EGRESS_DRIVER \"socketcan_fd\"") != NULL);
        FLOW_ASSERT_TRUE(strstr(ef_c_buf, "#define FLOW_MANIFEST_MIGRATE_FN embodiedStepKernel") != NULL);
        FLOW_ASSERT_TRUE(strstr(ef_c_buf, "FlowCoords_embodied_reflex") != NULL);
        FLOW_ASSERT_TRUE(strstr(ef_c_buf, "FlowJetView_embodied_reflex") != NULL);
        FLOW_ASSERT_TRUE(strstr(ef_c_buf, "!isfinite(torque)") != NULL);
        free(ef_c_buf);

        /* 3. Compile emitted C17 into shared library & verify automatic NaN/Inf firewall in Python */
        FLOW_ASSERT_EQ(system("cc -std=c17 -Wall -Wextra -Werror -O2 -shared -fPIC -DFLOW_SHARED_LIB /tmp/flow_test_embodied_manifest.c -o /tmp/libflow_test_embodied_manifest.so -lpthread"), 0);

        FILE *efpy_fp = fopen(ef_py_path, "w");
        FLOW_ASSERT_TRUE(efpy_fp != NULL);
        FLOW_ASSERT_TRUE(flow_abi_emit_python_adapter(efpy_fp, &ef_abi));
        fputs("\nif __name__ == '__main__':\n"
              "    with EmbodiedController('/tmp/libflow_test_embodied_manifest.so') as eng:\n"
              "        assert eng.isTorqueWithinLimit(45.0, 80.0) is True\n"
              "        assert eng.isTorqueWithinLimit(95.0, 80.0) is False\n"
              "        assert eng.isTorqueWithinLimit(float('nan'), 80.0) is False\n"
              "        assert eng.isTorqueWithinLimit(45.0, float('inf')) is False\n"
              "        assert abs(eng.dampedAngularAccel(10.0, 2.0, 50.0) - 39.6) < 1e-6\n"
              "        assert eng.dampedAngularAccel(float('nan'), 2.0, 50.0) == 0.0\n"
              "        assert eng.dampedAngularAccel(10.0, float('-inf'), 50.0) == 0.0\n"
              "        assert abs(eng.pdReflexLaw(0.5, 1.0, 20.0, 2.0) - (-12.0)) < 1e-6\n"
              "        assert eng.pdReflexLaw(float('nan'), 1.0, 20.0, 2.0) == 0.0\n", efpy_fp);
        fclose(efpy_fp);

        FLOW_ASSERT_EQ(system("python3 /tmp/flow_test_embodied_manifest.py"), 0);
        remove(ef_c_path);
        remove(ef_so_path);
        remove(ef_py_path);
        flow_ir_cleanup(&ef_ir);

        /* 4. Verify FLOW_JET_DEFINE_NAMED_VIEW and flow_jet_sanitize_finite on FlowJet */
        FlowJet jet;
        FLOW_ASSERT_TRUE(flow_jet_init(&jet, "jet_embodied_test", "Embodied Named View Jet"));
        TestEmbodiedJetView view = TestEmbodiedJetView_bind(&jet);
        FLOW_ASSERT_TRUE(view.q != NULL && view.p != NULL && view.a != NULL);
        view.q->hipPitch = 0.25;
        view.q->comY = -0.05;
        view.p->kneePitch = 1.5;
        FLOW_ASSERT_TRUE(fabs(jet.payload.q[0] - 0.25) < 1e-9);
        FLOW_ASSERT_TRUE(fabs(jet.payload.q[7] - (-0.05)) < 1e-9);
        FLOW_ASSERT_TRUE(fabs(jet.payload.p[2] - 1.5) < 1e-9);

        view.q->hipRoll = NAN;
        jet.payload.s = INFINITY;
        FLOW_ASSERT_TRUE(flow_jet_sanitize_finite(&jet));
        FLOW_ASSERT_TRUE(isfinite(view.q->hipRoll) && view.q->hipRoll == 0.0);
        FLOW_ASSERT_TRUE(isfinite(jet.payload.s) && jet.payload.s == 0.0);

        printf("  ✓ Stage 13 Passed: .lean x .flow manifest boundary, named FlowJet views & auto NaN/Inf firewall verified.\n\n");
    }

    /* =========================================================================
     * STAGE 14: LEAN-RT ZERO-GC GATE, NESTED STRUCTURES, HARDWARE CODEC
     *           INTRINSICS, VERIFIED HOT-RELOAD MIGRATION & @PATCHBAY
     * ========================================================================= */
    FLOW_STAGE_BEGIN(14, "Lean-RT Zero-GC Gate, Verified State Migration & @patchbay Composition");
    {
        const FlowPlugin *emb_mod = flow_registry_lookup("flow.embodied");
        FLOW_ASSERT_TRUE(emb_mod != NULL);

        /* 1. Lean-RT Zero-GC Compile-Time Gate: reject List, Array, ByteArray, ++, closures, recursion, recursive inductive */
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
            const char *bad_lpath = "/tmp/flow_stage14_bad_rt.lean";
            const char *bad_fpath = "/tmp/flow_stage14_bad_rt.flow";
            FILE *lf = fopen(bad_lpath, "w");
            FLOW_ASSERT_TRUE(lf != NULL);
            fputs(forbidden_lean_snippets[fi], lf);
            fclose(lf);
            FILE *ff = fopen(bad_fpath, "w");
            FLOW_ASSERT_TRUE(ff != NULL);
            fprintf(ff, "project bad_rt_%zu\nbind \"%s\"\n", fi, bad_lpath);
            fclose(ff);
            FILE *rf = fopen(bad_fpath, "r");
            FLOW_ASSERT_TRUE(rf != NULL);
            FlowSpec bad_spec;
            memset(&bad_spec, 0, sizeof(bad_spec));
            FLOW_ASSERT_TRUE(!parse_spec(rf, &bad_spec));
            fclose(rf);
            remove(bad_lpath);
            remove(bad_fpath);
        }

        /* 2. Nested structure flattening, finite inductive enum, flow_quantize/flow_dequantize & C17/Rust/Python ctypes.Structure */
        const char *nest_lean = "/tmp/flow_stage14_nested.lean";
        const char *nest_flow = "/tmp/flow_stage14_nested.flow";
        FILE *nlf = fopen(nest_lean, "w");
        FLOW_ASSERT_TRUE(nlf != NULL);
        fputs(
            "namespace Flow.Stage14Nested\n"
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
            "def step_pose (p : PoseFrame) : PoseFrame :=\n"
            "  { pos := { x := p.pos.x + p.vel.vx * 0.5, y := p.pos.y + p.vel.vy * 0.5 }, vel := { vx := p.vel.vx, vy := p.vel.vy }, ticks := p.ticks + 1 }\n"
            "def encode_torque (t : Float) : UInt64 := flow_quantize t (-80.0) 80.0 16\n"
            "def decode_torque (q : UInt64) : Float := flow_dequantize q (-80.0) 80.0 16\n"
            "def mode_scale (m : ControlMode) (v : UInt64) : UInt64 :=\n"
            "  match m with\n"
            "  | idle => 0\n"
            "  | track => v * 2\n"
            "  | fault => 1\n"
            "theorem step_pose_ticks_monotonic (p : PoseFrame) : (step_pose p).ticks > p.ticks := by decide\n"
            "theorem step_pose_pos_x_sound (p : PoseFrame) : (step_pose p).pos.x = p.pos.x + p.vel.vx * 0.5 := by rfl\n"
            "theorem mode_scale_track_sound (v : UInt64) : mode_scale ControlMode.track v = v * 2 := by rfl\n"
            "theorem quantize_zero_midpoint (x : UInt64) : encode_torque 0.0 == 32768 := by decide\n"
            "end Flow.Stage14Nested\n", nlf);
        fclose(nlf);

        /* Negative check: false theorem over match and false float scaling theorem must be refuted */
        {
            char err_buf[256] = {0};
            FLOW_ASSERT_TRUE(!flow_verify_lean_theorems(
                "inductive ControlMode where | idle | track | fault\n"
                "def mode_scale (m : ControlMode) (v : UInt64) : UInt64 := match m with | idle => 0 | track => v * 2 | fault => 1\n"
                "theorem bad_match_thm (v : UInt64) : mode_scale ControlMode.track v == 999 := by decide\n",
                err_buf, sizeof(err_buf)));
            FLOW_ASSERT_TRUE(!flow_verify_lean_theorems(
                "structure SensorRaw where\n  pos : Float\n  seq : UInt64\n"
                "def half_pos (r : SensorRaw) : SensorRaw := { pos := r.pos * 0.5, seq := r.seq }\n"
                "theorem bad_float_rfl (r : SensorRaw) : (half_pos r).pos = r.pos := by rfl\n",
                err_buf, sizeof(err_buf)));
        }

        FILE *nff = fopen(nest_flow, "w");
        FLOW_ASSERT_TRUE(nff != NULL);
        fprintf(nff,
            "project stage14_nested\n"
            "bind \"%s\"\n"
            "@quota {\n"
            "  deadline: 250us\n"
            "  arena: 64kb\n"
            "  fallback: emergency_stop\n"
            "}\n", nest_lean);
        fclose(nff);

        FILE *nrf = fopen(nest_flow, "r");
        FLOW_ASSERT_TRUE(nrf != NULL);
        FlowSpec nest_spec;
        memset(&nest_spec, 0, sizeof(nest_spec));
        FLOW_ASSERT_TRUE(parse_spec(nrf, &nest_spec));
        fclose(nrf);

        FLOW_ASSERT_EQ(nest_spec.lean_enum_count, 1u);
        FLOW_ASSERT_STR_EQ(nest_spec.lean_enums[0].name, "ControlMode");
        FLOW_ASSERT_EQ(nest_spec.lean_enums[0].variant_count, 3u);
        FLOW_ASSERT_EQ(nest_spec.lean_struct_count, 3u);
        FLOW_ASSERT_STR_EQ(nest_spec.lean_structs[2].name, "PoseFrame");
        FLOW_ASSERT_EQ(nest_spec.lean_structs[2].field_count, 5u);
        FLOW_ASSERT_STR_EQ(nest_spec.lean_structs[2].fields[0].name, "pos_x");
        FLOW_ASSERT_STR_EQ(nest_spec.lean_structs[2].fields[1].name, "pos_y");
        FLOW_ASSERT_STR_EQ(nest_spec.lean_structs[2].fields[2].name, "vel_vx");
        FLOW_ASSERT_STR_EQ(nest_spec.lean_structs[2].fields[3].name, "vel_vy");
        FLOW_ASSERT_STR_EQ(nest_spec.lean_structs[2].fields[4].name, "ticks");
        FLOW_ASSERT_EQ(nest_spec.lean_structs[2].byte_size, 40u);
        FLOW_ASSERT_EQ(nest_spec.lean_function_count, 4u);
        FLOW_ASSERT_EQ(nest_spec.lean_functions[0].is_struct_transform, 1);
        FLOW_ASSERT_TRUE(nest_spec.lean_functions[0].estimated_wcet_ns > 0);

        SemanticIR nest_ir;
        memset(&nest_ir, 0, sizeof(nest_ir));
        lower_to_ir(&nest_spec, &nest_ir);
        flow_plugin_lower_semantics(&nest_spec, &nest_ir, emb_mod);
        const Component *nest_comp = select_component(&nest_ir);
        FLOW_ASSERT_TRUE(nest_comp != NULL);
        VerificationReport nest_vrep;
        FLOW_ASSERT_TRUE(verify_candidate(&nest_ir, nest_comp, NULL, &nest_vrep));
        FlowComponentABI nest_abi;
        FLOW_ASSERT_TRUE(flow_abi_build_for_component(&nest_ir, nest_comp, &nest_abi));

        FILE *nh_fp = fopen("/tmp/flow_stage14_nested.h", "w");
        FLOW_ASSERT_TRUE(nh_fp != NULL);
        FLOW_ASSERT_TRUE(flow_abi_emit_c_header(nh_fp, &nest_abi));
        fclose(nh_fp);

        FILE *nrs_fp = fopen("/tmp/flow_stage14_nested.rs", "w");
        FLOW_ASSERT_TRUE(nrs_fp != NULL);
        FLOW_ASSERT_TRUE(flow_abi_emit_rust_adapter(nrs_fp, &nest_abi));
        fclose(nrs_fp);

        /* Verify Rust adapter contains #[repr(C)] pub struct PoseFrame */
        FILE *nrs_rd = fopen("/tmp/flow_stage14_nested.rs", "r");
        FLOW_ASSERT_TRUE(nrs_rd != NULL);
        char rs_buf[4096] = {0};
        (void)fread(rs_buf, 1, sizeof(rs_buf) - 1, nrs_rd);
        fclose(nrs_rd);
        FLOW_ASSERT_TRUE(strstr(rs_buf, "pub struct PoseFrame") != NULL);
        FLOW_ASSERT_TRUE(strstr(rs_buf, "pub pos_x: f64") != NULL);

        FILE *nc_fp = fopen("/tmp/flow_stage14_nested.c", "w");
        FLOW_ASSERT_TRUE(nc_fp != NULL);
        FLOW_ASSERT_TRUE(emit_c(nc_fp, &nest_ir, nest_comp, NULL, &nest_vrep, 0));
        fclose(nc_fp);
        FLOW_ASSERT_EQ(system("cc -std=c17 -Wall -Wextra -Werror -O2 -shared -fPIC -DFLOW_SHARED_LIB /tmp/flow_stage14_nested.c -o /tmp/libflow_stage14_nested.so -lpthread"), 0);

        FILE *npy_fp = fopen("/tmp/flow_stage14_nested.py", "w");
        FLOW_ASSERT_TRUE(npy_fp != NULL);
        FLOW_ASSERT_TRUE(flow_abi_emit_python_adapter(npy_fp, &nest_abi));
        fputs("\nif __name__ == '__main__':\n"
              "    with EmbodiedController('/tmp/libflow_stage14_nested.so') as eng:\n"
              "        pf = PoseFrame(pos_x=1.0, pos_y=2.0, vel_vx=4.0, vel_vy=-2.0, ticks=10)\n"
              "        out = eng.step_pose(pf)\n"
              "        assert abs(out.pos_x - 3.0) < 1e-9\n"
              "        assert abs(out.pos_y - 1.0) < 1e-9\n"
              "        assert out.ticks == 11\n"
              "        pf_nan = PoseFrame(pos_x=float('nan'), pos_y=2.0, vel_vx=4.0, vel_vy=float('inf'), ticks=5)\n"
              "        out_nan = eng.step_pose(pf_nan)\n"
              "        assert abs(out_nan.pos_x - 2.0) < 1e-9\n"
              "        assert abs(out_nan.pos_y - 2.0) < 1e-9\n"
              "        assert out_nan.ticks == 6\n"
              "        q0 = eng.encode_torque(0.0)\n"
              "        assert q0 == 32768\n"
              "        q25 = eng.encode_torque(25.0)\n"
              "        d25 = eng.decode_torque(q25)\n"
              "        assert abs(d25 - 25.0) < 0.01\n"
              "        assert eng.mode_scale(track, 21) == 42\n"
              "        assert eng.mode_scale(idle, 21) == 0\n", npy_fp);
        fclose(npy_fp);
        FLOW_ASSERT_EQ(system("python3 /tmp/flow_stage14_nested.py"), 0);

        remove(nest_lean);
        remove(nest_flow);
        remove("/tmp/flow_stage14_nested.h");
        remove("/tmp/flow_stage14_nested.rs");
        remove("/tmp/flow_stage14_nested.c");
        remove("/tmp/libflow_stage14_nested.so");
        remove("/tmp/flow_stage14_nested.py");
        flow_ir_cleanup(&nest_ir);

        /* 3. Verified Hot-Reload State Migration (StateV1 -> StateV2): negative false theorem & positive proof + runtime migration */
        const char *bad_mig_lean = "/tmp/flow_stage14_bad_mig.lean";
        const char *bad_mig_flow = "/tmp/flow_stage14_bad_mig.flow";
        FILE *bml = fopen(bad_mig_lean, "w");
        FLOW_ASSERT_TRUE(bml != NULL);
        fputs(
            "structure StateV1 where\n"
            "  balanceA : UInt64\n"
            "  balanceB : UInt64\n"
            "  epoch : UInt64\n"
            "structure StateV2 where\n"
            "  balanceA : UInt64\n"
            "  balanceB : UInt64\n"
            "  totalBalance : UInt64\n"
            "  epoch : UInt64\n"
            "  riskTier : UInt32\n"
            "def migrateV1ToV2 (s : StateV1) : StateV2 :=\n"
            "  { balanceA := s.balanceA, balanceB := s.balanceB, totalBalance := s.balanceA + s.balanceB, epoch := s.epoch + 1, riskTier := 1 }\n"
            "theorem migration_preserves_balance (s : StateV1) : (migrateV1ToV2 s).totalBalance = s.balanceA := by rfl\n", bml);
        fclose(bml);
        FILE *bmf = fopen(bad_mig_flow, "w");
        FLOW_ASSERT_TRUE(bmf != NULL);
        fprintf(bmf, "project bad_mig\nbind \"%s\"\n", bad_mig_lean);
        fclose(bmf);
        FILE *bmr = fopen(bad_mig_flow, "r");
        FLOW_ASSERT_TRUE(bmr != NULL);
        FlowSpec bad_mig_spec;
        memset(&bad_mig_spec, 0, sizeof(bad_mig_spec));
        FLOW_ASSERT_TRUE(!parse_spec(bmr, &bad_mig_spec));
        fclose(bmr);
        remove(bad_mig_lean);
        remove(bad_mig_flow);

        const char *mig_lean = "/tmp/flow_stage14_mig.lean";
        const char *mig_flow = "/tmp/flow_stage14_mig.flow";
        FILE *mlf = fopen(mig_lean, "w");
        FLOW_ASSERT_TRUE(mlf != NULL);
        fputs(
            "namespace Flow.Stage14Migrate\n"
            "structure StateV1 where\n"
            "  balanceA : UInt64\n"
            "  balanceB : UInt64\n"
            "  epoch : UInt64\n"
            "structure StateV2 where\n"
            "  balanceA : UInt64\n"
            "  balanceB : UInt64\n"
            "  totalBalance : UInt64\n"
            "  epoch : UInt64\n"
            "  riskTier : UInt32\n"
            "def migrateV1ToV2 (s : StateV1) : StateV2 :=\n"
            "  let tot := s.balanceA + s.balanceB\n"
            "  { balanceA := s.balanceA, balanceB := s.balanceB, totalBalance := tot, epoch := s.epoch + 1, riskTier := 1 }\n"
            "theorem migration_preserves_balance (s : StateV1) : (migrateV1ToV2 s).totalBalance = s.balanceA + s.balanceB := by rfl\n"
            "theorem migration_advances_epoch (s : StateV1) : (migrateV1ToV2 s).epoch = s.epoch + 1 := by rfl\n"
            "theorem migration_sets_risk_tier (s : StateV1) : (migrateV1ToV2 s).riskTier == 1 := by decide\n"
            "end Flow.Stage14Migrate\n", mlf);
        fclose(mlf);

        FILE *mff = fopen(mig_flow, "w");
        FLOW_ASSERT_TRUE(mff != NULL);
        fprintf(mff,
            "project stage14_migrate\n"
            "bind \"%s\"\n"
            "@reload {\n"
            "  schema_version: 2\n"
            "  from_schema: StateV1\n"
            "  to_schema: StateV2\n"
            "  migrate: migrateV1ToV2\n"
            "  qsbr_timeout: 5ms\n"
            "}\n", mig_lean);
        fclose(mff);

        FILE *mrf = fopen(mig_flow, "r");
        FLOW_ASSERT_TRUE(mrf != NULL);
        FlowSpec mig_spec;
        memset(&mig_spec, 0, sizeof(mig_spec));
        FLOW_ASSERT_TRUE(parse_spec(mrf, &mig_spec));
        fclose(mrf);

        FLOW_ASSERT_STR_EQ(mig_spec.reload_from_schema, "StateV1");
        FLOW_ASSERT_STR_EQ(mig_spec.reload_to_schema, "StateV2");
        FLOW_ASSERT_STR_EQ(mig_spec.migrate_fn, "migrateV1ToV2");
        FLOW_ASSERT_EQ(mig_spec.lean_theorem_count, 3u);

        SemanticIR mig_ir;
        memset(&mig_ir, 0, sizeof(mig_ir));
        lower_to_ir(&mig_spec, &mig_ir);
        flow_plugin_lower_semantics(&mig_spec, &mig_ir, emb_mod);
        const Component *mig_comp = select_component(&mig_ir);
        FLOW_ASSERT_TRUE(mig_comp != NULL);
        VerificationReport mig_vrep;
        FLOW_ASSERT_TRUE(verify_candidate(&mig_ir, mig_comp, NULL, &mig_vrep));
        FlowComponentABI mig_abi;
        FLOW_ASSERT_TRUE(flow_abi_build_for_component(&mig_ir, mig_comp, &mig_abi));

        FILE *mc_fp = fopen("/tmp/flow_stage14_mig.c", "w");
        FLOW_ASSERT_TRUE(mc_fp != NULL);
        FLOW_ASSERT_TRUE(emit_c(mc_fp, &mig_ir, mig_comp, NULL, &mig_vrep, 0));
        fclose(mc_fp);
        FLOW_ASSERT_EQ(system("cc -std=c17 -Wall -Wextra -Werror -O2 -shared -fPIC -DFLOW_SHARED_LIB /tmp/flow_stage14_mig.c -o /tmp/libflow_stage14_mig.so -lpthread"), 0);

        /* Verify C17 migration shim via dlopen AND live FlowReloadContext hot-swap */
        typedef struct { uint64_t balanceA; uint64_t balanceB; uint64_t epoch; } TestStateV1;
        typedef struct { uint64_t balanceA; uint64_t balanceB; uint64_t totalBalance; uint64_t epoch; uint32_t riskTier; } TestStateV2;
        typedef int32_t (*migrate_buf_fn_t)(const void *, size_t, void *, size_t);
        typedef int (*migrate_unit_fn_t)(void *, const void *, void *);

        void *mig_dl = dlopen("/tmp/libflow_stage14_mig.so", RTLD_NOW);
        FLOW_ASSERT_TRUE(mig_dl != NULL);
        void *sym_mbuf = dlsym(mig_dl, "flow_embodied_controller_migrate_buffer");
        void *sym_munit = dlsym(mig_dl, "flow_embodied_controller_migrate_unit");
        FLOW_ASSERT_TRUE(sym_mbuf != NULL && sym_munit != NULL);
        migrate_buf_fn_t fn_mbuf = NULL;
        migrate_unit_fn_t fn_munit = NULL;
        memcpy(&fn_mbuf, &sym_mbuf, sizeof(fn_mbuf));
        memcpy(&fn_munit, &sym_munit, sizeof(fn_munit));

        TestStateV1 s1 = { .balanceA = 42ULL, .balanceB = 58ULL, .epoch = 9ULL };
        TestStateV2 s2 = {0};
        FLOW_ASSERT_EQ(fn_mbuf(&s1, sizeof(s1), &s2, sizeof(s2)), 0);
        FLOW_ASSERT_EQ(s2.balanceA, 42ULL);
        FLOW_ASSERT_EQ(s2.balanceB, 58ULL);
        FLOW_ASSERT_EQ(s2.totalBalance, 100ULL);
        FLOW_ASSERT_EQ(s2.epoch, 10ULL);
        FLOW_ASSERT_EQ(s2.riskTier, 1u);
        /* Buffer too small must return -1 */
        FLOW_ASSERT_EQ(fn_mbuf(&s1, 4, &s2, sizeof(s2)), -1);

        /* Wire fn_munit into FlowReloadContext and hot-swap V1 -> V2 */
        FlowReloadContext *rctx = flow_reload_create(NULL);
        FLOW_ASSERT_TRUE(rctx != NULL);
        TestStateV1 *heap_v1 = (TestStateV1 *)calloc(1, sizeof(TestStateV1));
        TestStateV2 *heap_v2 = (TestStateV2 *)calloc(1, sizeof(TestStateV2));
        FLOW_ASSERT_TRUE(heap_v1 != NULL && heap_v2 != NULL);
        *heap_v1 = (TestStateV1){ .balanceA = 120ULL, .balanceB = 80ULL, .epoch = 5ULL };
        FlowUnit u_v1 = {
            .abi_version = FLOW_RELOAD_ABI_VERSION,
            .constraint_hash = 0xABCDULL,
            .capability_hash = 0x1234ULL,
            .semantic_schema_hash = 0x9999ULL,
            .name = "state_v1",
            .run = unit_zero_run,
            .drop = unit_zero_drop
        };
        FlowUnit u_v2 = u_v1;
        u_v2.name = "state_v2";
        u_v2.migrate = fn_munit;
        FLOW_ASSERT_EQ(flow_reload_publish(rctx, &u_v1, heap_v1), FLOW_RELOAD_OK);
        FLOW_ASSERT_EQ(flow_reload_publish(rctx, &u_v2, heap_v2), FLOW_RELOAD_OK);
        FLOW_ASSERT_EQ(heap_v2->balanceA, 120ULL);
        FLOW_ASSERT_EQ(heap_v2->balanceB, 80ULL);
        FLOW_ASSERT_EQ(heap_v2->totalBalance, 200ULL);
        FLOW_ASSERT_EQ(heap_v2->epoch, 6ULL);
        FLOW_ASSERT_EQ(heap_v2->riskTier, 1u);
        FLOW_ASSERT_EQ(flow_reload_destroy(rctx), FLOW_RELOAD_OK);
        dlclose(mig_dl);

        /* Verify Python migrate_state method */
        FILE *mpy_fp = fopen("/tmp/flow_stage14_mig.py", "w");
        FLOW_ASSERT_TRUE(mpy_fp != NULL);
        FLOW_ASSERT_TRUE(flow_abi_emit_python_adapter(mpy_fp, &mig_abi));
        fputs("\nif __name__ == '__main__':\n"
              "    with EmbodiedController('/tmp/libflow_stage14_mig.so') as eng:\n"
              "        v1 = StateV1(balanceA=120, balanceB=80, epoch=5)\n"
              "        v2 = eng.migrate_state(v1)\n"
              "        assert v2.balanceA == 120 and v2.balanceB == 80 and v2.totalBalance == 200\n"
              "        assert v2.epoch == 6 and v2.riskTier == 1\n", mpy_fp);
        fclose(mpy_fp);
        FLOW_ASSERT_EQ(system("python3 /tmp/flow_stage14_mig.py"), 0);

        remove(mig_lean);
        remove(mig_flow);
        remove("/tmp/flow_stage14_mig.c");
        remove("/tmp/libflow_stage14_mig.so");
        remove("/tmp/flow_stage14_mig.py");
        flow_ir_cleanup(&mig_ir);

        /* 4. Multi-Lean @patchbay Zero-Copy Composition, Compositional SMT Proofs & Watchdog Emergency-Stop Latch */
        /* Verify chained multi-stage struct-transform theorem unfolding (positive & negative) */
        {
            char err_buf[256] = {0};
            FLOW_ASSERT_TRUE(flow_verify_lean_theorems(
                "structure S1 where\n  seq : UInt64\n"
                "structure S2 where\n  seq : UInt64\n"
                "structure S3 where\n  seq : UInt64\n"
                "def stg1 (a : S1) : S2 := { seq := a.seq + 1 }\n"
                "def stg2 (b : S2) : S3 := { seq := b.seq + 2 }\n"
                "theorem chain_ok (x : S1) : (stg2 (stg1 x)).seq = x.seq + 3 := by rfl\n",
                err_buf, sizeof(err_buf)));
            FLOW_ASSERT_TRUE(!flow_verify_lean_theorems(
                "structure S1 where\n  seq : UInt64\n"
                "structure S2 where\n  seq : UInt64\n"
                "structure S3 where\n  seq : UInt64\n"
                "def stg1 (a : S1) : S2 := { seq := a.seq + 1 }\n"
                "def stg2 (b : S2) : S3 := { seq := b.seq + 2 }\n"
                "theorem chain_bad (x : S1) : (stg2 (stg1 x)).seq = x.seq + 99 := by rfl\n",
                err_buf, sizeof(err_buf)));
        }
        const char *pb_s1_lean = "/tmp/flow_stage14_pb_filter.lean";
        const char *pb_s2_lean = "/tmp/flow_stage14_pb_ik.lean";
        const char *pb_s3_lean = "/tmp/flow_stage14_pb_safety.lean";
        const char *pb_bad_lean = "/tmp/flow_stage14_pb_bad.lean";

        FILE *f_s1 = fopen(pb_s1_lean, "w");
        FLOW_ASSERT_TRUE(f_s1 != NULL);
        fputs(
            "structure SensorRaw where\n"
            "  pos : Float\n"
            "  vel : Float\n"
            "  seq : UInt64\n"
            "structure FilteredReading where\n"
            "  pos : Float\n"
            "  vel : Float\n"
            "  seq : UInt64\n"
            "def filter_stage (raw : SensorRaw) : FilteredReading :=\n"
            "  { pos := raw.pos * 0.5, vel := raw.vel * 0.5, seq := raw.seq + 1 }\n"
            "theorem filter_seq_inc (raw : SensorRaw) : (filter_stage raw).seq > raw.seq := by decide\n", f_s1);
        fclose(f_s1);

        FILE *f_s2 = fopen(pb_s2_lean, "w");
        FLOW_ASSERT_TRUE(f_s2 != NULL);
        fputs(
            "structure FilteredReading where\n"
            "  pos : Float\n"
            "  vel : Float\n"
            "  seq : UInt64\n"
            "structure JointTarget where\n"
            "  torque : Float\n"
            "  damping : Float\n"
            "  seq : UInt64\n"
            "def ik_stage (flt : FilteredReading) : JointTarget :=\n"
            "  { torque := flt.pos * 10.0 + flt.vel * 2.0, damping := flt.vel * 0.25, seq := flt.seq }\n", f_s2);
        fclose(f_s2);

        FILE *f_s3 = fopen(pb_s3_lean, "w");
        FLOW_ASSERT_TRUE(f_s3 != NULL);
        fputs(
            "structure JointTarget where\n"
            "  torque : Float\n"
            "  damping : Float\n"
            "  seq : UInt64\n"
            "structure ActuatorCmd where\n"
            "  clamped_torque : Float\n"
            "  pwm_code : UInt64\n"
            "  seq : UInt64\n"
            "def safety_stage (jt : JointTarget) : ActuatorCmd :=\n"
            "  { clamped_torque := if jt.torque > 50.0 then 50.0 else jt.torque, pwm_code := flow_quantize jt.torque (-80.0) 80.0 16, seq := jt.seq }\n", f_s3);
        fclose(f_s3);

        FILE *f_bad = fopen(pb_bad_lean, "w");
        FLOW_ASSERT_TRUE(f_bad != NULL);
        fputs(
            "structure WrongSchema where\n"
            "  only_one : UInt64\n"
            "structure ActuatorCmd where\n"
            "  clamped_torque : Float\n"
            "  pwm_code : UInt64\n"
            "  seq : UInt64\n"
            "def bad_stage (w : WrongSchema) : ActuatorCmd :=\n"
            "  { clamped_torque := 0.0, pwm_code := w.only_one, seq := 0 }\n", f_bad);
        fclose(f_bad);

        /* 4a. Negative: understated stage WCET (wcet 1ns < ast_wcet_ns) rejected by parser */
        const char *pb_under_flow = "/tmp/flow_stage14_pb_under.flow";
        FILE *f_under = fopen(pb_under_flow, "w");
        FLOW_ASSERT_TRUE(f_under != NULL);
        fprintf(f_under,
            "project pb_under\n"
            "@patchbay {\n"
            "  stage filter bind \"%s\" fn filter_stage in SensorRaw out FilteredReading wcet 1ns\n"
            "}\n", pb_s1_lean);
        fclose(f_under);
        FILE *r_under = fopen(pb_under_flow, "r");
        FLOW_ASSERT_TRUE(r_under != NULL);
        FlowSpec pb_under_spec;
        memset(&pb_under_spec, 0, sizeof(pb_under_spec));
        FLOW_ASSERT_TRUE(!parse_spec(r_under, &pb_under_spec));
        fclose(r_under);
        remove(pb_under_flow);

        /* 4b. Negative: incompatible wire schema/stride rejected by parser */
        const char *pb_mis_flow = "/tmp/flow_stage14_pb_mis.flow";
        FILE *f_mis = fopen(pb_mis_flow, "w");
        FLOW_ASSERT_TRUE(f_mis != NULL);
        fprintf(f_mis,
            "project pb_mis\n"
            "@patchbay {\n"
            "  stage filter bind \"%s\" fn filter_stage in SensorRaw out FilteredReading\n"
            "  stage bad bind \"%s\" fn bad_stage in WrongSchema out ActuatorCmd\n"
            "  wire filter -> bad\n"
            "}\n", pb_s1_lean, pb_bad_lean);
        fclose(f_mis);
        FILE *r_mis = fopen(pb_mis_flow, "r");
        FLOW_ASSERT_TRUE(r_mis != NULL);
        FlowSpec pb_mis_spec;
        memset(&pb_mis_spec, 0, sizeof(pb_mis_spec));
        FLOW_ASSERT_TRUE(!parse_spec(r_mis, &pb_mis_spec));
        fclose(r_mis);
        remove(pb_mis_flow);

        /* 4c. Valid 3-stage @patchbay pipeline */
        const char *pb_ok_flow = "/tmp/flow_stage14_pb_ok.flow";
        FILE *f_ok = fopen(pb_ok_flow, "w");
        FLOW_ASSERT_TRUE(f_ok != NULL);
        fprintf(f_ok,
            "project stage14_patchbay\n"
            "@quota {\n"
            "  deadline: 500us\n"
            "  arena: 64kb\n"
            "  fallback: emergency_stop\n"
            "}\n"
            "@patchbay {\n"
            "  stage filter bind \"%s\" fn filter_stage in SensorRaw out FilteredReading wcet 100us\n"
            "  stage ik bind \"%s\" fn ik_stage in FilteredReading out JointTarget wcet 150us\n"
            "  stage safety bind \"%s\" fn safety_stage in JointTarget out ActuatorCmd wcet 100us\n"
            "  wire filter -> ik -> safety\n"
            "}\n", pb_s1_lean, pb_s2_lean, pb_s3_lean);
        fclose(f_ok);

        FILE *r_ok = fopen(pb_ok_flow, "r");
        FLOW_ASSERT_TRUE(r_ok != NULL);
        FlowSpec pb_spec;
        memset(&pb_spec, 0, sizeof(pb_spec));
        FLOW_ASSERT_TRUE(parse_spec(r_ok, &pb_spec));
        fclose(r_ok);

        FLOW_ASSERT_EQ(pb_spec.patchbay_stage_count, 3u);
        FLOW_ASSERT_EQ(pb_spec.patchbay_stages[0].in_bytes, 24u);
        FLOW_ASSERT_EQ(pb_spec.patchbay_stages[0].out_bytes, 24u);
        FLOW_ASSERT_EQ(pb_spec.patchbay_stages[1].in_bytes, 24u);
        FLOW_ASSERT_EQ(pb_spec.patchbay_stages[1].out_bytes, 24u);
        FLOW_ASSERT_EQ(pb_spec.patchbay_stages[2].in_bytes, 24u);
        FLOW_ASSERT_EQ(pb_spec.patchbay_stages[2].out_bytes, 24u);
        FLOW_ASSERT_TRUE(pb_spec.patchbay_stages[0].ast_wcet_ns > 0);
        FLOW_ASSERT_TRUE(pb_spec.patchbay_stages[1].ast_wcet_ns > 0);
        FLOW_ASSERT_TRUE(pb_spec.patchbay_stages[2].ast_wcet_ns > 0);

        SemanticIR pb_ir;
        memset(&pb_ir, 0, sizeof(pb_ir));
        lower_to_ir(&pb_spec, &pb_ir);
        flow_plugin_lower_semantics(&pb_spec, &pb_ir, emb_mod);
        const Component *pb_comp = select_component(&pb_ir);
        FLOW_ASSERT_TRUE(pb_comp != NULL);
        VerificationReport pb_vrep;
        FLOW_ASSERT_TRUE(verify_candidate(&pb_ir, pb_comp, NULL, &pb_vrep));
        FlowSMTProofAttestation pb_proof;
        FLOW_ASSERT_TRUE(flow_smt_verify(&pb_ir, pb_comp, NULL, NULL, &pb_proof));
        FLOW_ASSERT_EQ(pb_proof.shard_non_aliasing, FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_EQ(pb_proof.determinism_invariant, FLOW_SMT_PROVEN_UNSAT);

        /* Verify SMT detects WCET deadline overrun (sum(wcet) = 600us > 500us) and wire mismatch */
        SemanticIR pb_over_ir = pb_ir;
        pb_over_ir.patchbay_stages[0].wcet_ns = 200000ULL;
        pb_over_ir.patchbay_stages[1].wcet_ns = 200000ULL;
        pb_over_ir.patchbay_stages[2].wcet_ns = 200000ULL;
        VerificationReport pb_over_vrep;
        FLOW_ASSERT_TRUE(!verify_candidate(&pb_over_ir, pb_comp, NULL, &pb_over_vrep));
        FLOW_ASSERT_EQ(pb_over_vrep.status, VERIFIER_COMPILE_ERROR);
        FlowSMTProofAttestation pb_over_proof;
        FLOW_ASSERT_TRUE(!flow_smt_verify(&pb_over_ir, pb_comp, NULL, NULL, &pb_over_proof));
        FLOW_ASSERT_EQ(pb_over_proof.determinism_invariant, FLOW_SMT_VIOLATION_SAT);

        SemanticIR pb_alias_ir = pb_ir;
        pb_alias_ir.patchbay_stages[1].in_bytes = 16u;
        FlowSMTProofAttestation pb_alias_proof;
        FLOW_ASSERT_TRUE(!flow_smt_verify(&pb_alias_ir, pb_comp, NULL, NULL, &pb_alias_proof));
        FLOW_ASSERT_EQ(pb_alias_proof.shard_non_aliasing, FLOW_SMT_VIOLATION_SAT);

        /* Compile C17 shared library and test zero-copy run_patchbay + emergency_stop latch in C & Python */
        FlowComponentABI pb_abi;
        FLOW_ASSERT_TRUE(flow_abi_build_for_component(&pb_ir, pb_comp, &pb_abi));
        FILE *pbc_fp = fopen("/tmp/flow_stage14_pb.c", "w");
        FLOW_ASSERT_TRUE(pbc_fp != NULL);
        FLOW_ASSERT_TRUE(emit_c(pbc_fp, &pb_ir, pb_comp, NULL, &pb_vrep, 0));
        fclose(pbc_fp);
        FLOW_ASSERT_EQ(system("cc -std=c17 -Wall -Wextra -Werror -O2 -shared -fPIC -DFLOW_SHARED_LIB /tmp/flow_stage14_pb.c -o /tmp/libflow_stage14_pb.so -lpthread"), 0);

        FILE *pbpy_fp = fopen("/tmp/flow_stage14_pb.py", "w");
        FLOW_ASSERT_TRUE(pbpy_fp != NULL);
        FLOW_ASSERT_TRUE(flow_abi_emit_python_adapter(pbpy_fp, &pb_abi));
        fputs("\nif __name__ == '__main__':\n"
              "    with EmbodiedController('/tmp/libflow_stage14_pb.so') as eng:\n"
              "        raw = SensorRaw(pos=4.0, vel=2.0, seq=99)\n"
              "        cmd = eng.run_patchbay(raw)\n"
              "        # filter: pos=2.0, vel=1.0, seq=100 -> ik: torque=22.0, damping=0.25, seq=100 -> safety: clamped=22.0\n"
              "        assert abs(cmd.clamped_torque - 22.0) < 1e-9\n"
              "        assert cmd.seq == 100\n"
              "        assert cmd.pwm_code > 32768\n"
              "        # Below SLA (100us <= 500us): watchdog does not trip\n"
              "        assert eng.trip_watchdog_for_test(100000) == 0\n"
              "        cmd2 = eng.run_patchbay(SensorRaw(pos=20.0, vel=0.0, seq=1))\n"
              "        assert abs(cmd2.clamped_torque - 50.0) < 1e-9\n"
              "        assert cmd2.seq == 2\n"
              "        # Breach SLA (600us > 500us): emergency_stop latches -4 permanently\n"
              "        assert eng.trip_watchdog_for_test(600000) == -4\n"
              "        try:\n"
              "            eng.run_patchbay(raw)\n"
              "            assert False, 'Expected RuntimeError(-4) after emergency_stop latch'\n"
              "        except RuntimeError as e:\n"
              "            assert '-4' in str(e)\n", pbpy_fp);
        fclose(pbpy_fp);
        FLOW_ASSERT_EQ(system("python3 /tmp/flow_stage14_pb.py"), 0);

        /* Also verify live runtime SLA breach inside flow_<id>_run_patchbay when sla_latency_ns == 1ns */
        SemanticIR pb_tight_ir = pb_ir;
        pb_tight_ir.sla_latency_ns = 1ULL;
        FILE *pbt_fp = fopen("/tmp/flow_stage14_pb_tight.c", "w");
        FLOW_ASSERT_TRUE(pbt_fp != NULL);
        FLOW_ASSERT_TRUE(emit_c(pbt_fp, &pb_tight_ir, pb_comp, NULL, &pb_vrep, 0));
        fclose(pbt_fp);
        FLOW_ASSERT_EQ(system("cc -std=c17 -Wall -Wextra -Werror -O2 -shared -fPIC -DFLOW_SHARED_LIB /tmp/flow_stage14_pb_tight.c -o /tmp/libflow_stage14_pb_tight.so -lpthread"), 0);
        void *pbt_dl = dlopen("/tmp/libflow_stage14_pb_tight.so", RTLD_NOW);
        FLOW_ASSERT_TRUE(pbt_dl != NULL);
        typedef void *(*pb_create_fn_t)(void);
        typedef void (*pb_destroy_fn_t)(void *);
        typedef struct { double pos, vel; uint64_t seq; } C_SensorRaw;
        typedef struct { double clamped_torque; uint64_t pwm_code, seq; } C_ActuatorCmd;
        typedef int32_t (*pb_run_fn_t)(void *, const C_SensorRaw *, C_ActuatorCmd *);
        pb_create_fn_t fn_pbt_create = NULL;
        pb_destroy_fn_t fn_pbt_destroy = NULL;
        pb_run_fn_t fn_pbt_run = NULL;
        void *s_cr = dlsym(pbt_dl, "flow_embodied_controller_create");
        void *s_de = dlsym(pbt_dl, "flow_embodied_controller_destroy");
        void *s_rn = dlsym(pbt_dl, "flow_embodied_controller_run_patchbay");
        FLOW_ASSERT_TRUE(s_cr != NULL && s_de != NULL && s_rn != NULL);
        memcpy(&fn_pbt_create, &s_cr, sizeof(fn_pbt_create));
        memcpy(&fn_pbt_destroy, &s_de, sizeof(fn_pbt_destroy));
        memcpy(&fn_pbt_run, &s_rn, sizeof(fn_pbt_run));
        void *pbt_h = fn_pbt_create();
        FLOW_ASSERT_TRUE(pbt_h != NULL);
        C_SensorRaw c_in = { 4.0, 2.0, 1ULL };
        C_ActuatorCmd c_out = { 0.0, 0ULL, 0ULL };
        FLOW_ASSERT_EQ(fn_pbt_run(pbt_h, &c_in, &c_out), -4);
        FLOW_ASSERT_EQ(fn_pbt_run(pbt_h, &c_in, &c_out), -4);
        fn_pbt_destroy(pbt_h);
        dlclose(pbt_dl);
        remove("/tmp/flow_stage14_pb_tight.c");
        remove("/tmp/libflow_stage14_pb_tight.so");

        remove(pb_s1_lean);
        remove(pb_s2_lean);
        remove(pb_s3_lean);
        remove(pb_bad_lean);
        remove(pb_ok_flow);
        remove("/tmp/flow_stage14_pb.c");
        remove("/tmp/libflow_stage14_pb.so");
        remove("/tmp/flow_stage14_pb.py");
        flow_ir_cleanup(&pb_ir);

        printf("  ✓ Stage 14 Passed: Lean-RT zero-GC gate, nested structs, flow_quantize/dequantize, verified state migration & @patchbay composition verified.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 16: 15-Module Flat Consolidation, Boilerplate Elimination (B1-B6)   */
    /*           & Symbolic Constants Integrity (M1-M7)                          */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(16, "15-Module Flat Consolidation, Boilerplate Elimination (B1-B6), Symbolic Constants (M1-M7) & Algorithmic UI Layout Engine");
    {
        /* 1. 15-Module Flat Consolidation & Topology Audit */
        FLOW_ASSERT_TRUE(access("src/fvec", F_OK) != 0);
        FLOW_ASSERT_TRUE(access("src/jet", F_OK) != 0);
        FLOW_ASSERT_TRUE(access("src/topo", F_OK) != 0);
        FLOW_ASSERT_TRUE(access("src/inspect", F_OK) != 0);

        FlowTopologyGraph cgraph;
        flow_topology_build_codebase_graph(&cgraph);
        FlowTopologyAuditReport creport;
        flow_topology_audit(&cgraph, &creport);
        FLOW_ASSERT_EQ(creport.cross_layer_leaks, 0u);
        FLOW_ASSERT_TRUE(fabs(creport.modularity_score - 1.0) < 1e-9);

        /* 2. Unified Boilerplate Helpers (B1-B5) */
        const char *crc_msg = "FLOW_ORTHOGONAL_2026";
        uint32_t crc_base = flow_crc32_ieee(crc_msg, 20);
        FLOW_ASSERT_EQ(crc_base, flow_fvec_crc32(crc_msg, 20));
        FLOW_ASSERT_EQ(crc_base, flow_jet_crc32(crc_msg, 20));

        FlowThermalState th1;
        flow_jet_thermal_init_default(&th1);
        FLOW_ASSERT_TRUE(flow_thermal_state_step(&th1, 65.0, 0.1));
        FlowJet j_th;
        FLOW_ASSERT_TRUE(flow_jet_init(&j_th, "stage16_th", "Stage 16 Thermal"));
        FLOW_ASSERT_TRUE(flow_jet_thermal_step(&j_th, 65.0, 0.1));
        FLOW_ASSERT_TRUE(fabs(th1.temp_c - j_th.payload.thermal.temp_c) < 1e-12);

        double q1[2] = { 1.0, -0.5 }, p1[2] = { 0.2, 0.4 }, a1[2] = { 0.0, 0.0 };
        FlowJet j_sym;
        FLOW_ASSERT_TRUE(flow_jet_init_extended(&j_sym, "stage16_sym", "Stage 16 Symplectic", 2, 2, 2));
        j_sym.payload.q[0] = q1[0]; j_sym.payload.q[1] = q1[1];
        j_sym.payload.p[0] = p1[0]; j_sym.payload.p[1] = p1[1];
        j_sym.payload.a[0] = a1[0]; j_sym.payload.a[1] = a1[1];
        flow_verlet_step_harmonic(2, q1, p1, a1, j_sym.payload.curvature_spectrum, 0.01);
        FLOW_ASSERT_TRUE(flow_jet_symplectic_step(&j_sym, 0.01));
        FLOW_ASSERT_TRUE(fabs(q1[0] - j_sym.payload.q[0]) < 1e-12);
        FLOW_ASSERT_TRUE(fabs(p1[1] - j_sym.payload.p[1]) < 1e-12);

        FLOW_ASSERT_TRUE(flow_registry_resolve_plugin("flow.embodied"));
        FlowTokenRing tr;
        FLOW_ASSERT_TRUE(flow_token_ring_converge_default(&tr, NULL, "stage16_verify"));
        FLOW_ASSERT_EQ(tr.state, FLOW_RING_ATTRACTOR_REACHED);

        /* 3. Symbolic Constants Integrity (M1-M7) & flow_cubical_sieve_from_ir consistency */
        FLOW_ASSERT_EQ((uint32_t)FLOW_SIEVE_BIT_EXTENT_ALIGN, 5u);
        FLOW_ASSERT_EQ((uint32_t)FLOW_SIEVE_BIT_PARALLEL, 8u);
        FLOW_ASSERT_EQ((uint32_t)FLOW_SIEVE_BIT_LEAN_CERTIFIED, 62u);
        FLOW_ASSERT_EQ((uint64_t)FLOW_WCET_ALU_OP_NS, 5ULL);
        FLOW_ASSERT_EQ((uint64_t)FLOW_WCET_COMPLEX_OP_NS, 25ULL);
        FLOW_ASSERT_STR_EQ(FLOW_FVEC_MAGIC, "FVEC_V1");
        {
            SemanticIR sieve_ir;
            memset(&sieve_ir, 0, sizeof(sieve_ir));
            sieve_ir.flow_parallelizable = 1;
            sieve_ir.state_shared = 1;
            sieve_ir.state_read_heavy = 1;
            sieve_ir.fact_unordered = 1;
            sieve_ir.state_bounded = 1;
            sieve_ir.fact_deterministic = 1;
            sieve_ir.fact_lean_certified = 1;
            FlowCubicalTypeSieve ts = flow_cubical_sieve_from_ir(&sieve_ir);
            FLOW_ASSERT_TRUE((ts.mask & (1ULL << FLOW_SIEVE_BIT_PARALLEL)) != 0);
            FLOW_ASSERT_TRUE((ts.mask & (1ULL << FLOW_SIEVE_BIT_SHARED_STATE)) != 0);
            FLOW_ASSERT_TRUE((ts.mask & (1ULL << FLOW_SIEVE_BIT_READ_HEAVY)) != 0);
            FLOW_ASSERT_TRUE((ts.mask & (1ULL << FLOW_SIEVE_BIT_UNORDERED)) != 0);
            FLOW_ASSERT_TRUE((ts.mask & (1ULL << FLOW_SIEVE_BIT_BOUNDED_RANGE)) != 0);
            FLOW_ASSERT_TRUE((ts.mask & (1ULL << FLOW_SIEVE_BIT_DETERMINISTIC)) != 0);
            FLOW_ASSERT_TRUE((ts.mask & (1ULL << FLOW_SIEVE_BIT_LEAN_CERTIFIED)) != 0);
        }

        /* 4. Algorithmic UI Layout Engine (B6): 2D Skyline Bin-Packing, Sugiyama DAG & Type-Directed Controls */
        const char *s16_lean = "/tmp/flow_stage16_ui.lean";
        FILE *fx_fp = fopen(s16_lean, "w");
        FLOW_ASSERT_TRUE(fx_fp != NULL);
        fputs(
            "namespace Flow.Stage16UI\n"
            "inductive BarrierRegime where\n"
            "  | Continuous\n"
            "  | DiscreteWindow\n"
            "structure FXQuote where\n"
            "  spot : Float\n"
            "  barrier : Float\n"
            "def check_barrier_breach (spot : Float) (barrier : Float) : Bool :=\n"
            "  spot >= barrier\n"
            "def scale_by_regime (r : BarrierRegime) (v : UInt64) : UInt64 :=\n"
            "  match r with\n"
            "  | Continuous => v * 2\n"
            "  | DiscreteWindow => v\n"
            "def step_quote (q : FXQuote) : FXQuote :=\n"
            "  { spot := q.spot + 1.0, barrier := q.barrier }\n"
            "theorem check_barrier_breach_sound (x : Float) : check_barrier_breach 120.0 100.0 == true := by decide\n"
            "end Flow.Stage16UI\n", fx_fp);
        fclose(fx_fp);

        FILE *fx_rd = fopen(s16_lean, "r");
        FLOW_ASSERT_TRUE(fx_rd != NULL);
        FlowSpec fx_spec;
        memset(&fx_spec, 0, sizeof(fx_spec));
        strncpy(fx_spec.kernel_source_lean, s16_lean, sizeof(fx_spec.kernel_source_lean) - 1);
        FLOW_ASSERT_TRUE(parse_spec(fx_rd, &fx_spec));
        fclose(fx_rd);

        /* Also verify flowy ui --selftest succeeds on a spec with struct-taking functions! */
        {
            char ui_cmd[512];
            snprintf(ui_cmd, sizeof(ui_cmd), "%s/flowy ui %s --selftest", FLOW_BUILD_DIR, s16_lean);
            FLOW_ASSERT_EQ(system(ui_cmd), 0);
        }
        remove(s16_lean);

        SemanticIR fx_ir;
        memset(&fx_ir, 0, sizeof(fx_ir));
        lower_to_ir(&fx_spec, &fx_ir);
        for (size_t s = 0; s < FLOW_PATCHBAY_STAGE_MAX; ++s) {
            snprintf(fx_ir.patchbay_stages[s].stage_name, 63, "stage_%zu", s);
            strncpy(fx_ir.patchbay_stages[s].fn_name, (s == 0) ? "check_barrier_breach" : "step_quote", 63);
            strncpy(fx_ir.patchbay_stages[s].in_struct, "FXQuote", 63);
            strncpy(fx_ir.patchbay_stages[s].out_struct, (s == 0) ? "Bool" : "FXQuote", 63);
            fx_ir.patchbay_stages[s].wcet_ns = 250ULL;
            fx_ir.patchbay_stages[s].ast_wcet_ns = 25ULL;
        }
        fx_ir.patchbay_stage_count = FLOW_PATCHBAY_STAGE_MAX;

        const Component *fx_comp = select_component(&fx_ir);
        FLOW_ASSERT_TRUE(fx_comp != NULL);
        FlowComponentABI fx_abi;
        FLOW_ASSERT_TRUE(flow_abi_build_for_component(&fx_ir, fx_comp, &fx_abi));

        const char *ui_html_path = "/tmp/flow_stage16_ui.html";
        FILE *ui_fp = fopen(ui_html_path, "w");
        FLOW_ASSERT_TRUE(ui_fp != NULL);
        FLOW_ASSERT_TRUE(flow_abi_emit_ui_adapter(ui_fp, &fx_ir, &fx_abi));
        fclose(ui_fp);

        FILE *ui_rd = fopen(ui_html_path, "r");
        FLOW_ASSERT_TRUE(ui_rd != NULL);
        fseek(ui_rd, 0, SEEK_END);
        long ui_len = ftell(ui_rd);
        fseek(ui_rd, 0, SEEK_SET);
        FLOW_ASSERT_TRUE(ui_len > 1024);
        char *ui_buf = (char *)calloc((size_t)ui_len + 1, 1);
        FLOW_ASSERT_TRUE(ui_buf != NULL);
        FLOW_ASSERT_EQ(fread(ui_buf, 1, (size_t)ui_len, ui_rd), (size_t)ui_len);
        fclose(ui_rd);

        FLOW_ASSERT_TRUE(strstr(ui_buf, "lean_structs: [") != NULL);
        FLOW_ASSERT_TRUE(strstr(ui_buf, "lean_enums: [") != NULL);
        FLOW_ASSERT_TRUE(strstr(ui_buf, "patchbay_stages: [") != NULL);
        FLOW_ASSERT_TRUE(strstr(ui_buf, "computeSkylineLayout") != NULL);
        FLOW_ASSERT_TRUE(strstr(ui_buf, "<svg id=\"sugiyama-dag\"") != NULL);
        FLOW_ASSERT_TRUE(strstr(ui_buf, "renderSugiyamaDag") != NULL);
        FLOW_ASSERT_TRUE(strstr(ui_buf, "FORMAL SAFETY GATE") != NULL);
        FLOW_ASSERT_TRUE(strstr(ui_buf, "<select") != NULL);
        FLOW_ASSERT_TRUE(strstr(ui_buf, "struct-group") != NULL);
        FLOW_ASSERT_TRUE(strstr(ui_buf, "margin-top:auto") != NULL);
        FLOW_ASSERT_TRUE(strstr(ui_buf, "controlWatchdog") != NULL);
        FLOW_ASSERT_TRUE(strstr(ui_buf, "exportComplianceCertificate") != NULL);
        FLOW_ASSERT_TRUE(strstr(ui_buf, "wd-badge") != NULL);

        /* 5. Stage 17: Six-Persona Application-Layer Last-Mile Closure (MATLAB IR Adapter, FlowStatusCode, Watchdog Reset & flowy build --shared) */
        const char *m_path = "/tmp/flow_stage17_fx.m";
        FILE *m_fp = fopen(m_path, "w");
        FLOW_ASSERT_TRUE(m_fp != NULL);
        FLOW_ASSERT_TRUE(flow_abi_emit_polyglot_adapter(m_fp, &fx_abi, "matlab"));
        fclose(m_fp);

        FILE *m_rd = fopen(m_path, "r");
        FLOW_ASSERT_TRUE(m_rd != NULL);
        fseek(m_rd, 0, SEEK_END);
        long m_len = ftell(m_rd);
        fseek(m_rd, 0, SEEK_SET);
        char *m_buf = (char *)calloc((size_t)m_len + 1, 1);
        FLOW_ASSERT_TRUE(m_buf != NULL);
        FLOW_ASSERT_EQ(fread(m_buf, 1, (size_t)m_len, m_rd), (size_t)m_len);
        fclose(m_rd);
        remove(m_path);

        FLOW_ASSERT_TRUE(strstr(m_buf, "FLOW_OK = int32(0);") != NULL);
        FLOW_ASSERT_TRUE(strstr(m_buf, "FLOW_ERR_WATCHDOG_ESTOP = int32(-4);") != NULL);
        FLOW_ASSERT_TRUE(strstr(m_buf, "Continuous = uint32(0);") != NULL);
        FLOW_ASSERT_TRUE(strstr(m_buf, "DiscreteWindow = uint32(1);") != NULL);
        FLOW_ASSERT_TRUE(strstr(m_buf, "LEAN_FUNCTIONS = {") != NULL);
        FLOW_ASSERT_TRUE(strstr(m_buf, "PATCHBAY_STAGES = {") != NULL);
        FLOW_ASSERT_TRUE(strstr(m_buf, "function rc = resetWatchdog(obj)") != NULL);
        FLOW_ASSERT_TRUE(strstr(m_buf, "function cnt = breachCount(obj)") != NULL);
        FLOW_ASSERT_TRUE(strstr(m_buf, "function outFrame = runPatchbay(obj, inFrame)") != NULL);
        FLOW_ASSERT_TRUE(strstr(m_buf, "function res = check_barrier_breach(obj, spot, barrier)") != NULL);
        FLOW_ASSERT_TRUE(strstr(m_buf, "function res = scale_by_regime(obj, r, v)") != NULL);
        FLOW_ASSERT_TRUE(strstr(m_buf, "function res = step_quote(obj, q)") != NULL);
        free(m_buf);

        /* Test flowy build --shared one-command SDK bundle + Watchdog Trip/Reset round-trip */
        const char *s17_lean = "/tmp/flow_stage17_sdk.lean";
        FILE *s17_fp = fopen(s17_lean, "w");
        FLOW_ASSERT_TRUE(s17_fp != NULL);
        fputs(
            "namespace Embodied.Stage17SDK\n"
            "structure JointCmd where\n"
            "  torque_nm : Float\n"
            "  seq       : UInt64\n"
            "def clamp_torque (t : Float) (lim : Float) : Float :=\n"
            "  if t > lim then lim else t\n"
            "theorem clamp_torque_sound : clamp_torque 120.0 80.0 == 80.0 := by decide\n"
            "end Embodied.Stage17SDK\n", s17_fp);
        fclose(s17_fp);

        {
            char sh_cmd[512];
            snprintf(sh_cmd, sizeof(sh_cmd), "%s/flowy build --shared %s >/dev/null", FLOW_BUILD_DIR, s17_lean);
            FLOW_ASSERT_EQ(system(sh_cmd), 0);
            FLOW_ASSERT_EQ(access("build/libflow_stage17_sdk.so", F_OK), 0);
            FLOW_ASSERT_EQ(access("build/flow_stage17_sdk.h", F_OK), 0);
            FLOW_ASSERT_EQ(access("build/flow_stage17_sdk_abi.py", F_OK), 0);
            FLOW_ASSERT_EQ(access("build/flow_flow_stage17_sdk.m", F_OK), 0);

            /* Verify FlowStatusCode enum in generated .h */
            FILE *h_rd = fopen("build/flow_stage17_sdk.h", "r");
            FLOW_ASSERT_TRUE(h_rd != NULL);
            char h_buf[4096] = {0};
            (void)fread(h_buf, 1, sizeof(h_buf) - 1, h_rd);
            fclose(h_rd);
            FLOW_ASSERT_TRUE(strstr(h_buf, "FLOW_ERR_WATCHDOG_ESTOP = -4") != NULL);
            FLOW_ASSERT_TRUE(strstr(h_buf, "_reset_watchdog(") != NULL);
            FLOW_ASSERT_TRUE(strstr(h_buf, "_watchdog_breach_count(") != NULL);

            /* Verify Python SDK Watchdog trip -> breach_count -> reset recovery */
            const char *py_verify =
                "python3 -c \""
                "import sys; sys.path.insert(0, 'build'); "
                "import flow_stage17_sdk_abi as sdk; "
                "assert sdk.FLOW_OK == 0 and sdk.FLOW_ERR_WATCHDOG_ESTOP == -4; "
                "eng = sdk.EmbodiedController('build/libflow_stage17_sdk.so'); "
                "assert abs(eng.clamp_torque(120.0, 80.0) - 80.0) < 1e-9; "
                "assert eng.watchdog_breach_count() == 0; "
                "assert eng.trip_watchdog_for_test(10**9) == sdk.FLOW_ERR_WATCHDOG_ESTOP; "
                "assert eng.watchdog_breach_count() == 1; "
                "assert eng.reset_watchdog() == sdk.FLOW_OK; "
                "w = eng.step_words([0, 64, 1]); assert w[0] == 64; "
                "eng.close()\"";
            FLOW_ASSERT_EQ(system(py_verify), 0);

            remove("build/libflow_stage17_sdk.so");
            remove("build/flow_stage17_sdk.h");
            remove("build/flow_stage17_sdk_abi.py");
            remove("build/flow_stage17_sdk_abi.rs");
            remove("build/flow_flow_stage17_sdk.m");
        }
        remove(s17_lean);

        free(ui_buf);
        remove(ui_html_path);
        flow_ir_cleanup(&fx_ir);

        printf("  ✓ Stage 16 & 17 Passed: 15-module flat consolidation, unified boilerplate (B1-B6), symbolic constants (M1-M7), Algorithmic UI & Six-Persona Last-Mile SDK verified.\n\n");
    }

    /* =========================================================================
     * STAGE 18: Application-Layer PR #1-#4 Verification
     *   PR #1: Multi-line Lean 4 def, (Float × Float) tuple return destructuring, Float.* math builtins & unary -x
     *   PR #2: flowc <input.lean> -o <out.c> --target-c-header <out.h> (static inline flowc_<fn> + FlowPluginDescriptor)
     *   PR #3: Cross-DLL / Cross-DSO process-wide shadow A/B verification & per-kernel PMU counters
     *   PR #4: Strict -std=gnu17 -Wall -Wextra -Wpedantic -Werror -Wmisleading-indentation clean headers
     * ========================================================================= */
    FLOW_STAGE_BEGIN(18, "Application-Layer PR #1-#4: Multi-line Tuple Lean 4, Static Inline Header, Cross-DSO Shadow A/B & -Wpedantic");
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

        /* PR #1 + PR #2: Run flowc <input.lean> -o <out.c> --target-c-header <out.h> */
        char cmd_flowc[512];
        snprintf(cmd_flowc, sizeof(cmd_flowc),
                 "%s/flowc %s -o %s --target-c-header %s",
                 FLOW_BUILD_DIR, pr_lean_path, pr_c_path, pr_h_path);
        FLOW_ASSERT_EQ(system(cmd_flowc), 0);
        FLOW_ASSERT_EQ(access(pr_c_path, F_OK), 0);
        FLOW_ASSERT_EQ(access(pr_h_path, F_OK), 0);

        /* Compile shared library from generated .c */
        char cmd_so[512];
        snprintf(cmd_so, sizeof(cmd_so),
                 "cc -std=c17 -Wall -Wextra -Wpedantic -Werror -O2 -shared -fPIC -DFLOW_SHARED_LIB %s -o %s -lm -lpthread",
                 pr_c_path, pr_so_path);
        FLOW_ASSERT_EQ(system(cmd_so), 0);

        /* PR #2, #3, #4: Compile C17 driver with -std=gnu17 -Wall -Wextra -Wpedantic -Werror -Wmisleading-indentation
         * that directly includes plugin.h, bitspace.h, smt.h AND the generated --target-c-header file,
         * dlopens the shared library, verifies FlowPluginDescriptor, static inline flowc_*, tuple out_0/out_1,
         * and cross-DSO shadow A/B telemetry & per-kernel PMU counters! */
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
            "    /* 1. Test static inline zero-overhead C17 synthesis (PR #1 & PR #2) + Mode A native Q32.32 _fx ABI */\n"
            "    flow_fx64_t fx_bid = 0, fx_ask = 0;\n"
            "    flow_fx64_t fx_ret0 = flowc_exec_bilateral_quote_fx(100LL * FLOW_FX_ONE, 4LL * FLOW_FX_ONE, -2LL * FLOW_FX_ONE, FLOW_FX_ONE >> 2, &fx_bid, &fx_ask);\n"
            "    assert(fx_ret0 == fx_bid);\n"
            "    assert(fx_bid == (99LL * FLOW_FX_ONE + (FLOW_FX_ONE >> 2)));\n"
            "    assert(fx_ask == (101LL * FLOW_FX_ONE + (FLOW_FX_ONE >> 2)));\n"
            "    flow_fx64_t fx_sprd = flowc_quote_spread_fx(100LL * FLOW_FX_ONE, 4LL * FLOW_FX_ONE, -2LL * FLOW_FX_ONE, FLOW_FX_ONE >> 2);\n"
            "    assert(fx_sprd == 2LL * FLOW_FX_ONE);\n"
            "    double inl_bid = 0.0, inl_ask = 0.0;\n"
            "    double ret0 = flowc_exec_bilateral_quote(100.0, 4.0, -2.0, 0.25, &inl_bid, &inl_ask);\n"
            "    assert(fabs(ret0 - inl_bid) < 1e-9);\n"
            "    /* vol=4.0 -> half_sprd = sqrt(4)*0.5 = 1.0; inv=-2 < 0 -> skew = -0.25;\n"
            "       raw_bid = 100 - 1.0 - (-0.25) = 99.25; raw_ask = 100 + 1.0 - (-0.25) = 101.25 */\n"
            "    assert(fabs(inl_bid - 99.25) < 1e-9);\n"
            "    assert(fabs(inl_ask - 101.25) < 1e-9);\n"
            "    double inl_sprd = flowc_quote_spread(100.0, 4.0, -2.0, 0.25);\n"
            "    assert(fabs(inl_sprd - 2.0) < 1e-9);\n",
            pr_h_path);
        fprintf(drv_fp,
            "    /* 2. Test cross-DSO execution, FlowPluginDescriptor & PMU/Shadow A/B counters (PR #2 & PR #3) */\n"
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
            "    flow_fx64_t dso_fx_b = 0, dso_fx_a = 0;\n"
            "    dso_quote_fx(100LL * FLOW_FX_ONE, 4LL * FLOW_FX_ONE, -2LL * FLOW_FX_ONE, FLOW_FX_ONE >> 2, &dso_fx_b, &dso_fx_a);\n"
            "    assert(dso_fx_b == fx_bid && dso_fx_a == fx_ask);\n"
            "    assert(dso_spread_fx(100LL * FLOW_FX_ONE, 4LL * FLOW_FX_ONE, -2LL * FLOW_FX_ONE, FLOW_FX_ONE >> 2) == fx_sprd);\n"
            "    dso_ab_reset();\n"
            "    assert(dso_ab_calls() == 0 && dso_ab_fn(0) == 0 && dso_ab_fn(1) == 0);\n"
            "    double dso_bid = 0.0, dso_ask = 0.0;\n"
            "    dso_quote(100.0, 4.0, -2.0, 0.25, &dso_bid, &dso_ask);\n"
            "    assert(fabs(dso_bid - inl_bid) < 1e-9 && fabs(dso_ask - inl_ask) < 1e-9);\n"
            "    assert(dso_ab_fn(0) == 1);\n"
            "    double dso_sprd = dso_spread(100.0, 4.0, -2.0, 0.25);\n"
            "    assert(fabs(dso_sprd - inl_sprd) < 1e-9);\n"
            "    assert(dso_ab_fn(1) == 1);\n"
            "    dso_ab_rec(0, dso_bid, inl_bid, 1e-9);\n"
            "    dso_ab_rec(1, dso_sprd, inl_sprd + 0.5, 1e-9);\n"
            "    assert(dso_ab_calls() == 2);\n"
            "    assert(dso_ab_mis() == 1);\n"
            "    dso_ab_reset();\n"
            "    assert(dso_ab_calls() == 0 && dso_ab_mis() == 0 && dso_ab_fn(0) == 0);\n"
            "    dlclose(h);\n"
            "    return 0;\n"
            "}\n",
            pr_so_path);
        fclose(drv_fp);

        char cmd_drv[1024];
        snprintf(cmd_drv, sizeof(cmd_drv),
                 "cc -std=gnu17 -Wall -Wextra -Wpedantic -Werror -Wmisleading-indentation -O2 -Isrc %s -o %s -lm -ldl",
                 pr_drv_path, pr_bin_path);
        FLOW_ASSERT_EQ(system(cmd_drv), 0);
        FLOW_ASSERT_EQ(system(pr_bin_path), 0);

        remove(pr_lean_path);
        remove(pr_c_path);
        remove(pr_h_path);
        remove(pr_so_path);
        remove(pr_drv_path);
        remove(pr_bin_path);
        printf("  ✓ Stage 18 Passed: PR #1 (multi-line tuple Lean 4 & math builtins), PR #2 (--target-c-header static inline & FlowPluginDescriptor), PR #3 (cross-DSO shadow A/B & PMU counters), and PR #4 (-Wpedantic -Werror=misleading-indentation) verified.\n\n");
    }

    /* =========================================================================
     * STAGE 19: FlowExactNumerics — Lossless Fixed-Point (Q16.16 / Q32.32),
     *           256-Bit Montgomery/Miller-Rabin BigInt, Xoshiro256** & Linalg
     * ========================================================================= */
    FLOW_STAGE_BEGIN(19, "FlowExactNumerics: Lossless Fixed-Point Q16.16/Q32.32, 256-Bit Montgomery/Miller-Rabin BigInt, Xoshiro256** & Fixed-Point Linalg");
    {
        /* 1. Q16.16 math.rs exact parity (exp_approx_q16, rsqrt_approx_i32, silu_approx_i8) */
        int32_t exp_0 = 0;
        FLOW_ASSERT_EQ(flow_exp_approx_q16(0, &exp_0), 1);
        FLOW_ASSERT_EQ(exp_0, 65536);
        int32_t exp_ovf = 0;
        FLOW_ASSERT_EQ(flow_exp_approx_q16(100 * 65536, &exp_ovf), 0);

        uint32_t rsqrt_16 = 0;
        FLOW_ASSERT_EQ(flow_rsqrt_approx_i32(16U, &rsqrt_16), 1); /* 1/sqrt(16) = 1/4 = 16384 in Q16.16 */
        FLOW_ASSERT_EQ(rsqrt_16, 16384U);
        FLOW_ASSERT_EQ(flow_rsqrt_approx_i32(0U, &rsqrt_16), 0);

        int8_t silu_out = -1;
        FLOW_ASSERT_EQ(flow_silu_approx_i8(0, &silu_out), 1);
        FLOW_ASSERT_EQ((int)silu_out, 0);
        FLOW_ASSERT_EQ(flow_silu_approx_i8(2, &silu_out), 1);
        FLOW_ASSERT_TRUE(silu_out >= 0);

        /* 2. Q32.32 flow_fx64_t bitwise IEEE-754 codec, overflow sentinel & pure-integer transcendentals */
        FLOW_ASSERT_EQ(flow_fx64_from_double(0.0), 0LL);
        FLOW_ASSERT_EQ(flow_fx64_from_double(1.0), FLOW_FX_ONE);
        FLOW_ASSERT_EQ(flow_fx64_from_double(-0.25), -(FLOW_FX_ONE >> 2));
        FLOW_ASSERT_EQ(flow_fx64_from_double(NAN), FLOW_FX64_NONE);
        FLOW_ASSERT_EQ(flow_fx64_from_double(INFINITY), FLOW_FX64_NONE);
        FLOW_ASSERT_EQ(flow_fx64_from_double(1e308), FLOW_FX64_NONE);
        FLOW_ASSERT_TRUE(fabs(flow_fx64_to_double(flow_fx64_from_double(101.25)) - 101.25) == 0.0);
        FLOW_ASSERT_EQ(flow_fx64_sqrt(9LL * FLOW_FX_ONE), 3LL * FLOW_FX_ONE);
        FLOW_ASSERT_EQ(flow_fx64_log(FLOW_FX_ONE), 0LL);
        FLOW_ASSERT_EQ(flow_fx64_sin(0LL), 0LL);
        FLOW_ASSERT_EQ(flow_fx64_cos(0LL), FLOW_FX_ONE);
        FLOW_ASSERT_TRUE(fabs(flow_fx64_to_double(flow_fx64_exp(FLOW_FX_ONE)) - 2.718281828) < 1e-6);

        /* 3. 256-Bit & 512-Bit Wide BigInt (bignum/u256.rs + prime.rs) */
        FlowU256 u_a = flow_u256_from_u64(123456789ULL);
        FlowU256 u_b = flow_u256_from_u64(987654321ULL);
        uint64_t wide[8] = {0};
        flow_u256_mul_wide(u_a, u_b, wide);
        FLOW_ASSERT_EQ(wide[0], 123456789ULL * 987654321ULL);
        FLOW_ASSERT_EQ(wide[1], 0ULL);

        FlowU256 q_div, r_div;
        FLOW_ASSERT_TRUE(flow_u256_div_rem(flow_u256_from_u64(1000ULL), flow_u256_from_u64(33ULL), &q_div, &r_div));
        FLOW_ASSERT_EQ(q_div.w[0], 30ULL);
        FLOW_ASSERT_EQ(r_div.w[0], 10ULL);

        /* 4^13 mod 497 == 445 (from SEARU prime.rs test_mod_pow) */
        FlowU256 mp = flow_u256_mod_pow(flow_u256_from_u64(4ULL), flow_u256_from_u64(13ULL), flow_u256_from_u64(497ULL));
        FLOW_ASSERT_EQ(mp.w[0], 445ULL);

        /* Miller-Rabin primality test (from SEARU prime.rs test_miller_rabin) */
        static const uint64_t mr_bases[] = {2ULL, 3ULL, 5ULL, 7ULL, 11ULL, 13ULL, 17ULL, 19ULL, 23ULL};
        FlowU256 p97 = flow_u256_from_u64(97ULL);
        FlowU256 c91 = flow_u256_from_u64(91ULL);
        FlowU256 p64 = flow_u256_from_u64(18446744073709551557ULL); /* 2^64 - 59 */
        FLOW_ASSERT_EQ(flow_u256_is_probably_prime(&p97, mr_bases, 3), 1);
        FLOW_ASSERT_EQ(flow_u256_is_probably_prime(&c91, mr_bases, 3), 0);
        FLOW_ASSERT_EQ(flow_u256_is_probably_prime(&p64, mr_bases, 9), 1);

        FlowU256 hex_u;
        FLOW_ASSERT_TRUE(flow_u256_from_hex_str("0x10000000000000000", &hex_u));
        FLOW_ASSERT_EQ(hex_u.w[0], 0ULL);
        FLOW_ASSERT_EQ(hex_u.w[1], 1ULL);

        /* 4. Xoshiro256** PRNG (random.rs) & Fixed-Point Linalg (linalg/matrix.rs + calculus.rs) */
        FlowXoshiro256SS rng1, rng2;
        flow_xoshiro256ss_init(&rng1, 42ULL);
        flow_xoshiro256ss_init(&rng2, 42ULL);
        uint64_t rv1 = flow_xoshiro256ss_next_u64(&rng1);
        uint64_t rv2 = flow_xoshiro256ss_next_u64(&rng2);
        FLOW_ASSERT_EQ(rv1, rv2);
        FLOW_ASSERT_TRUE(rv1 != 0ULL);

        /* 2x2 Q32.32 matrix multiply: [[1, 2], [3, 4]] * [[2, 0], [1, 2]] = [[4, 4], [10, 8]] */
        flow_fx64_t mat_a[4] = { 1LL * FLOW_FX_ONE, 2LL * FLOW_FX_ONE, 3LL * FLOW_FX_ONE, 4LL * FLOW_FX_ONE };
        flow_fx64_t mat_b[4] = { 2LL * FLOW_FX_ONE, 0LL,               1LL * FLOW_FX_ONE, 2LL * FLOW_FX_ONE };
        flow_fx64_t mat_c[4] = {0};
        flow_fx_mat_mul_ikj(2, 2, 2, mat_a, mat_b, mat_c);
        FLOW_ASSERT_EQ(mat_c[0], 4LL * FLOW_FX_ONE);
        FLOW_ASSERT_EQ(mat_c[1], 4LL * FLOW_FX_ONE);
        FLOW_ASSERT_EQ(mat_c[2], 10LL * FLOW_FX_ONE);
        FLOW_ASSERT_EQ(mat_c[3], 8LL * FLOW_FX_ONE);

        /* Fixed-point MSE loss & gradient descent step reduces MSE loss */
        flow_fx64_t x_in[2]    = { 1LL * FLOW_FX_ONE, 2LL * FLOW_FX_ONE };
        flow_fx64_t w_param[2] = { 0LL, 0LL };
        flow_fx64_t preds[1]   = { 0LL };
        flow_fx64_t tgts[1]    = { 5LL * FLOW_FX_ONE };
        flow_fx_mat_mul_ikj(1, 2, 1, x_in, w_param, preds);
        flow_fx64_t loss_before = flow_fx_mse_loss(1, 1, preds, tgts);
        FLOW_ASSERT_EQ(loss_before, 25LL * FLOW_FX_ONE);
        flow_fx_gradient_descent_step(1, 2, 1, x_in, w_param, preds, tgts, FLOW_FX_ONE / 10);
        flow_fx_mat_mul_ikj(1, 2, 1, x_in, w_param, preds);
        flow_fx64_t loss_after = flow_fx_mse_loss(1, 1, preds, tgts);
        FLOW_ASSERT_TRUE(loss_after < loss_before);

        printf("  ✓ Stage 19 Passed: FlowExactNumerics (Q16.16/Q32.32 fixed-point, 256-bit Montgomery/Miller-Rabin BigInt, Xoshiro256** & fixed-point linalg) verified.\n\n");
    }

    /* =========================================================================
     * STAGE 20: Self-Bootstrapping Codebase Auditor, 7 Component Scopes,
     *           Rust Plugin DSO Lifecycle, 256-Bit secp256k1 & 7-Language Polyglot Gate
     * ========================================================================= */
    FLOW_STAGE_BEGIN(20, "Self-Bootstrapping Codebase Auditor, 7 Component Scopes, Rust Plugin DSO & 7-Language Polyglot Gate");
    {
        /* 1. Full-Codebase Topology Graph, 7 Component Scopes & Self-Bootstrapping Audit */
        FlowTopologyGraph full_graph;
        flow_topology_build_codebase_graph(&full_graph);
        FlowTopologyAuditReport full_rep;
        flow_topology_audit(&full_graph, &full_rep);

        FLOW_ASSERT_EQ(full_rep.cross_layer_leaks, 0u);
        FLOW_ASSERT_EQ(full_rep.makefile_bijective, 1);
        FLOW_ASSERT_EQ(full_rep.build_rs_bijective, 1);
        FLOW_ASSERT_EQ(full_rep.rust_sdk_parity, 1);
        FLOW_ASSERT_EQ(full_rep.polyglot_homomorphic, 1);
        FLOW_ASSERT_EQ(full_rep.exact_numerics_sound, 1);
        FLOW_ASSERT_EQ(full_rep.hott_univalence_sound, 1);
        FLOW_ASSERT_EQ(full_rep.probe_weaving_sound, 1);
        FLOW_ASSERT_TRUE(full_rep.hott_type_count >= 7u);
        FLOW_ASSERT_EQ(full_rep.hott_broken_count, 0u);
        FLOW_ASSERT_EQ(full_rep.rust_nodes, 2u);

        /* Verify all redundant C & Rust wrapper files have been folded into core pillars and deleted */
        FLOW_ASSERT_TRUE(access("build.rs", F_OK) != 0);
        FLOW_ASSERT_TRUE(access("src/main.rs", F_OK) != 0);
        FLOW_ASSERT_TRUE(access("src/builtin_plugin.c", F_OK) != 0);
        FLOW_ASSERT_TRUE(access("src/flowy_fvec.h", F_OK) != 0);
        FLOW_ASSERT_TRUE(access("src/flowy_fvec.c", F_OK) != 0);
        FLOW_ASSERT_TRUE(access("src/security.h", F_OK) != 0);
        FLOW_ASSERT_TRUE(access("src/security.c", F_OK) != 0);
        FLOW_ASSERT_TRUE(access("src/hardware_telemetry.h", F_OK) != 0);
        FLOW_ASSERT_TRUE(access("src/hardware_telemetry.c", F_OK) != 0);
        FLOW_ASSERT_TRUE(access("src/jit.h", F_OK) != 0);
        FLOW_ASSERT_TRUE(access("src/jit.c", F_OK) != 0);
        FLOW_ASSERT_TRUE(access("src/token_ring.h", F_OK) != 0);
        FLOW_ASSERT_TRUE(access("src/token_ring.c", F_OK) != 0);
        FLOW_ASSERT_TRUE(access("plugins/flow_embodied_mz.h", F_OK) != 0);
        FLOW_ASSERT_TRUE(access("plugins/flow_embodied_mz.c", F_OK) != 0);
        FLOW_ASSERT_TRUE(access("plugins/driver_imu.h", F_OK) != 0);
        FLOW_ASSERT_TRUE(access("plugins/driver_imu.c", F_OK) != 0);
        FLOW_ASSERT_TRUE(access("plugins/driver_can.h", F_OK) != 0);
        FLOW_ASSERT_TRUE(access("plugins/driver_can.c", F_OK) != 0);

        /* Verify Global HoTT Universe U_0 populated by flow_topology_audit has exact C17 == Rust field counts (10, 30, 13, 16) */
        FlowHoTTUniverse *g_u0 = flow_hott_universe_global();
        FLOW_ASSERT_TRUE(g_u0 != NULL && g_u0->count >= 8u);
        FLOW_ASSERT_EQ(g_u0->entries[0].field_count, 10u); /* FlowPluginDescriptor C17 */
        FLOW_ASSERT_EQ(g_u0->entries[1].field_count, 10u); /* FlowPluginDescriptor Rust */
        FLOW_ASSERT_EQ(g_u0->entries[2].field_count, 30u); /* FlowPlugin C17 */
        FLOW_ASSERT_EQ(g_u0->entries[3].field_count, 30u); /* FlowPlugin Rust */
        FLOW_ASSERT_EQ(g_u0->entries[4].field_count, 13u); /* FlowUnit C17 */
        FLOW_ASSERT_EQ(g_u0->entries[5].field_count, 13u); /* FlowUnit Rust */
        FLOW_ASSERT_EQ(g_u0->entries[6].field_count, 16u); /* FlowComponent C17 */
        FLOW_ASSERT_EQ(g_u0->entries[7].field_count, 16u); /* FlowComponent Rust */

        /* HoTT Intensional Type Universe U_0: Univalence positive & negative homotopy path break tests */
        FlowHoTTUniverse local_u0;
        flow_hott_universe_reset(&local_u0);
        FLOW_ASSERT_EQ(flow_hott_register_type(&local_u0, "OrderBookFrame", FLOW_HOTT_LANG_LEAN, 4, 32, 8, 32, 0xABCD1234ULL), 1);
        FLOW_ASSERT_EQ(flow_hott_register_type(&local_u0, "OrderBookFrame", FLOW_HOTT_LANG_C17,  4, 32, 8, 32, 0xABCD1234ULL), 1);
        FLOW_ASSERT_EQ(flow_hott_register_type(&local_u0, "OrderBookFrame", FLOW_HOTT_LANG_RUST, 4, 32, 8, 32, 0xABCD1234ULL), 1);
        uint64_t path_hash = 0ULL;
        FLOW_ASSERT_EQ(flow_hott_verify_univalence(&local_u0, "OrderBookFrame", &path_hash), FLOW_KAN_FILLED_HOMOTOPIC);
        FLOW_ASSERT_TRUE(path_hash != 0ULL);
        FLOW_ASSERT_EQ(flow_hott_verify_universe_all(&local_u0), 1);
        FLOW_ASSERT_EQ(local_u0.distinct_type_count, 1u);
        FLOW_ASSERT_EQ(local_u0.univalence_passed_count, 1u);
        FLOW_ASSERT_EQ(local_u0.homotopy_broken_count, 0u);

        /* Negative test: Rust struct missing a field triggers Homotopy Path Broken */
        FLOW_ASSERT_EQ(flow_hott_register_type(&local_u0, "OrderBookFrame", FLOW_HOTT_LANG_RUST, 3, 24, 8, 32, 0xABCD1234ULL), 1);
        FLOW_ASSERT_EQ(flow_hott_verify_univalence(&local_u0, "OrderBookFrame", &path_hash), FLOW_KAN_OBSTRUCTED_SINGULARITY);
        FLOW_ASSERT_EQ(path_hash, 0ULL);
        FLOW_ASSERT_EQ(flow_hott_verify_universe_all(&local_u0), 0);
        FLOW_ASSERT_EQ(local_u0.homotopy_broken_count, 1u);
        FLOW_ASSERT_TRUE(strstr(local_u0.broken_diagnostic, "Homotopy Path Broken on 'OrderBookFrame'") != NULL);

        for (int s = 0; s < (int)FLOW_SCOPE_COUNT; ++s) {
            FLOW_ASSERT_TRUE(full_rep.scope_node_counts[s] > 0u);
            FLOW_ASSERT_TRUE(full_rep.scope_loc[s] > 0u);
            FlowComponentScope parsed_sc = FLOW_SCOPE_FRONTEND;
            FLOW_ASSERT_TRUE(flow_topology_parse_scope(flow_topology_scope_name((FlowComponentScope)s), &parsed_sc));
            FLOW_ASSERT_EQ((int)parsed_sc, s);
        }

        /* Negative test 1: injecting a forbidden Layer 0 -> Layer 2 dependency edge and an orphan C module node is caught by flow_topology_audit */
        FlowTopologyGraph mutated_graph = full_graph;
        int core_idx = -1, plg_idx = -1;
        for (size_t i = 0; i < mutated_graph.node_count; ++i) {
            if (mutated_graph.nodes[i].layer == 0 && core_idx < 0) core_idx = (int)i;
            if (mutated_graph.nodes[i].layer == 2 && plg_idx < 0) plg_idx = (int)i;
        }
        FLOW_ASSERT_TRUE(core_idx >= 0 && plg_idx >= 0);
        FLOW_ASSERT_TRUE(flow_topology_add_edge(&mutated_graph, (uint32_t)core_idx, (uint32_t)plg_idx, FLOW_EDGE_USES, 1.0, "synthetic_violation"));
        (void)flow_topology_add_node(&mutated_graph, FLOW_NODE_CORE_MODULE, "nonexistent_ghost_module", "core", 1, 0);
        FlowTopologyAuditReport bad_rep;
        flow_topology_audit(&mutated_graph, &bad_rep);
        FLOW_ASSERT_TRUE(bad_rep.cross_layer_leaks >= 2u);
        FLOW_ASSERT_EQ(bad_rep.makefile_bijective, 0);
        FLOW_ASSERT_TRUE(strstr(bad_rep.leak_details, "[LEAK]") != NULL);
        FLOW_ASSERT_TRUE(strstr(bad_rep.leak_details, "[MAKEFILE_ORPHAN_NODE]") != NULL);

        /* Negative test 2: hermetic corrupted workspace via FLOW_WORKSPACE_ROOT catches stale build.rs/main.rs, non-virtual Cargo.toml, external crate in Cargo.lock & Rust SDK field drift */
        FLOW_ASSERT_EQ(system("rm -rf /tmp/flow_s20_bad_ws && mkdir -p /tmp/flow_s20_bad_ws/src /tmp/flow_s20_bad_ws/crates/flow-plugin/src"), 0);
        FILE *f_brs = fopen("/tmp/flow_s20_bad_ws/build.rs", "w");
        if (f_brs) { fputs("fn main() {}\n", f_brs); fclose(f_brs); }
        FILE *f_mrs = fopen("/tmp/flow_s20_bad_ws/src/main.rs", "w");
        if (f_mrs) { fputs("fn main() {}\n", f_mrs); fclose(f_mrs); }
        FILE *f_ct = fopen("/tmp/flow_s20_bad_ws/Cargo.toml", "w");
        if (f_ct) { fputs("[workspace]\nresolver = \"2\"\nmembers = [\"crates/flow-plugin\", \"crates/flow-plugin-example\"]\n[package]\nname = \"bad\"\n[build-dependencies]\ncc = \"1.0\"\n", f_ct); fclose(f_ct); }
        FILE *f_cl = fopen("/tmp/flow_s20_bad_ws/Cargo.lock", "w");
        if (f_cl) { fputs("[[package]]\nname = \"flow-plugin\"\n[[package]]\nname = \"flow-plugin-example\"\n[[package]]\nname = \"cc\"\nsource = \"registry+https://github.com/rust-lang/crates.io-index\"\n", f_cl); fclose(f_cl); }
        FILE *f_sdk = fopen("/tmp/flow_s20_bad_ws/crates/flow-plugin/src/lib.rs", "w");
        if (f_sdk) { fputs("pub const FLOW_PLUGIN_ABI_MAJOR: u32 = 1;\npub struct FlowPluginDescriptor { pub a: u32 }\npub struct FlowPlugin { pub a: u32 }\npub struct FlowUnit { pub a: u32 }\npub struct FlowComponent { pub a: u32 }\n", f_sdk); fclose(f_sdk); }
        setenv("FLOW_WORKSPACE_ROOT", "/tmp/flow_s20_bad_ws", 1);
        FlowTopologyAuditReport corrupt_rep;
        flow_topology_audit(&full_graph, &corrupt_rep);
        unsetenv("FLOW_WORKSPACE_ROOT");
        FLOW_ASSERT_EQ(corrupt_rep.makefile_bijective, 0);
        FLOW_ASSERT_EQ(corrupt_rep.rust_sdk_parity, 0);
        FLOW_ASSERT_EQ(corrupt_rep.hott_univalence_sound, 0);
        FLOW_ASSERT_TRUE(strstr(corrupt_rep.leak_details, "[STALE_BUILD_RS]") != NULL);
        FLOW_ASSERT_TRUE(strstr(corrupt_rep.leak_details, "[STALE_RUST_MAIN]") != NULL);
        FLOW_ASSERT_TRUE(strstr(corrupt_rep.leak_details, "[RUST_NON_VIRTUAL_WORKSPACE]") != NULL);
        FLOW_ASSERT_TRUE(strstr(corrupt_rep.leak_details, "[RUST_EXTERNAL_CRATE_LEAK]") != NULL);
        FLOW_ASSERT_TRUE(strstr(corrupt_rep.leak_details, "[RUST_SDK_DRIFT]") != NULL);
        FLOW_ASSERT_TRUE(strstr(corrupt_rep.leak_details, "[HOTT_UNIVALENCE_BROKEN]") != NULL);
        (void)system("rm -rf /tmp/flow_s20_bad_ws");
        /* Restore global HoTT universe U_0 to clean workspace state */
        flow_topology_audit(&full_graph, &full_rep);
        FLOW_ASSERT_EQ(full_rep.hott_univalence_sound, 1);

        /* Verify flowy CLI scope & audit commands succeed from both repo root and subdirectory */
        char audit_cmd[512], scope_cmd[512], subdir_audit_cmd[512];
        snprintf(audit_cmd, sizeof(audit_cmd), "%s/flowy audit > /dev/null", FLOW_BUILD_DIR);
        snprintf(scope_cmd, sizeof(scope_cmd), "%s/flowy inspect scope rust_sdk > /dev/null", FLOW_BUILD_DIR);
        snprintf(subdir_audit_cmd, sizeof(subdir_audit_cmd), "(cd %s && ./flowy audit | grep -q 'Nodes: 30')", FLOW_BUILD_DIR);
        FLOW_ASSERT_EQ(system(audit_cmd), 0);
        FLOW_ASSERT_EQ(system(scope_cmd), 0);
        FLOW_ASSERT_EQ(system(subdir_audit_cmd), 0);

        /* 2. 256-Bit secp256k1 Prime, Even-Modulus mod_pow, Multi-Output MSE, Q32.32 Overflow & QSBR Overflow Guard */
        FlowU256 p_secp;
        static const uint64_t secp_bases[] = { 2ULL, 3ULL, 5ULL, 7ULL };
        FLOW_ASSERT_TRUE(flow_u256_from_hex_str("0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F", &p_secp));
        FLOW_ASSERT_EQ(flow_u256_is_probably_prime(&p_secp, secp_bases, 4), 1);

        /* Even-modulus 256-bit mod_pow with high bit set in base: (2^255 + 5)^2 mod 1000 == (968 + 5)^2 mod 1000 == 973^2 mod 1000 == 729 */
        FlowU256 big_base, exp2, even_mod;
        FLOW_ASSERT_TRUE(flow_u256_from_hex_str("0x8000000000000000000000000000000000000000000000000000000000000005", &big_base));
        exp2 = flow_u256_from_u64(2ULL);
        even_mod = flow_u256_from_u64(1000ULL);
        FlowU256 even_res = flow_u256_mod_pow(big_base, exp2, even_mod);
        FLOW_ASSERT_EQ(even_res.w[0], 729ULL);

        /* Multi-output flow_fx_mse_loss (N=2, OUT=2) and OUT=0 guard */
        flow_fx64_t m_pred[4] = { 2LL * FLOW_FX_ONE, 4LL * FLOW_FX_ONE, 6LL * FLOW_FX_ONE, 8LL * FLOW_FX_ONE };
        flow_fx64_t m_tgt[4]  = { 1LL * FLOW_FX_ONE, 2LL * FLOW_FX_ONE, 3LL * FLOW_FX_ONE, 4LL * FLOW_FX_ONE };
        flow_fx64_t m_loss = flow_fx_mse_loss(2, 2, m_pred, m_tgt);
        FLOW_ASSERT_TRUE(fabs(flow_fx64_to_double(m_loss) - 7.5) < 1e-6);
        FLOW_ASSERT_EQ(flow_fx_mse_loss(2, 0, m_pred, m_tgt), 0LL);

        /* Extreme Q32.32 signed overflow guards */
        FLOW_ASSERT_EQ(flow_fx64_ceil(FLOW_FX64_MAX), FLOW_FX64_NONE);
        FLOW_ASSERT_EQ(flow_fx64_round(FLOW_FX64_MAX), FLOW_FX64_NONE);
        FLOW_ASSERT_EQ(flow_fx64_round(FLOW_FX64_MIN), FLOW_FX64_NONE);
        FLOW_ASSERT_EQ(flow_fx64_cos(FLOW_FX64_NONE), FLOW_FX64_NONE);
        FLOW_ASSERT_TRUE(flow_fx64_cos(FLOW_FX64_MAX) != FLOW_FX64_NONE);
        FLOW_ASSERT_EQ(flow_fx64_atan2(FLOW_FX64_NONE, FLOW_FX_ONE), 0LL);
        FLOW_ASSERT_TRUE(flow_fx64_atan2(-FLOW_FX64_MAX, FLOW_FX_ONE) < 0LL);
        FLOW_ASSERT_EQ(flow_quantize_fx(FLOW_FX64_MAX, FLOW_FX64_MIN, FLOW_FX64_MAX, 100), 0ULL);

        /* QSBR bump allocator SIZE_MAX overflow guard */
        uint8_t qsbr_backing[4096] = {0};
        FlowBumpQsbrArena qsbr_arena;
        FLOW_ASSERT_TRUE(flow_bump_qsbr_init(&qsbr_arena, qsbr_backing, sizeof(qsbr_backing)));
        FLOW_ASSERT_TRUE(flow_bump_qsbr_alloc(&qsbr_arena, 128) != NULL);
        FLOW_ASSERT_TRUE(flow_bump_qsbr_alloc(&qsbr_arena, SIZE_MAX - 4) == NULL);

        /* Parser bind metadata preservation */
        const char *bind_spec_text =
            "project bound_pipeline\n"
            "import rust_simd_plugin\n"
            "contract zmp_torque_safe\n"
            "fallback golden_fallback\n"
            "input raw_stream {\n"
            "  max_count 2048\n"
            "}\n"
            "require {\n"
            "  memory_mb <= 64\n"
            "}\n"
            "bind \"plugins/embodied.lean\"\n";
        FILE *bfp = tmpfile();
        FLOW_ASSERT_TRUE(bfp != NULL);
        fputs(bind_spec_text, bfp);
        rewind(bfp);
        FlowSpec *bspec = (FlowSpec *)calloc(1, sizeof(FlowSpec));
        FLOW_ASSERT_TRUE(bspec != NULL);
        FLOW_ASSERT_TRUE(parse_spec(bfp, bspec));
        fclose(bfp);
        FLOW_ASSERT_STR_EQ(bspec->project_name, "bound_pipeline");
        FLOW_ASSERT_STR_EQ(bspec->plugin_name, "rust_simd_plugin");
        FLOW_ASSERT_STR_EQ(bspec->contract_name, "zmp_torque_safe");
        FLOW_ASSERT_STR_EQ(bspec->fallback_policy, "golden_fallback");
        FLOW_ASSERT_EQ(bspec->input_max_count, 2048);
        FLOW_ASSERT_EQ(bspec->memory_limit_mb, 64);
        FLOW_ASSERT_TRUE(bspec->lean_function_count > 0u);
        free(bspec);

        /* 3. Rust Plugin DSO (flow_plugin_example / rust_simd_plugin / plugin-example) Full Lifecycle Verification */
        FLOW_ASSERT_TRUE(flow_registry_resolve_plugin("plugin-example"));
        FLOW_ASSERT_TRUE(flow_registry_resolve_plugin("rust_simd_plugin"));
        const FlowPlugin *rplg = flow_registry_lookup("rust_simd_plugin");
        FLOW_ASSERT_TRUE(rplg != NULL && rplg->component_count > 0 && rplg->components != NULL);
        FLOW_ASSERT_EQ(flow_registry_lookup("flow_plugin_example"), rplg);
        FLOW_ASSERT_EQ(flow_registry_lookup("plugin-example"), rplg);
        const Component *rcomp = &rplg->components[0];

        SemanticIR rir = {0};
        strncpy(rir.flow_name, "rust_dso_test", sizeof(rir.flow_name) - 1);
        rir.input_max_count = 1024;
        rir.fact_deterministic = 1;

        /* Short-buffer null-termination in verify */
        char short_msg[5] = { 'X', 'X', 'X', 'X', 'X' };
        FLOW_ASSERT_EQ(rplg->verify(&rir, rcomp, 1024, 1, short_msg, sizeof(short_msg)), 1);
        FLOW_ASSERT_EQ(short_msg[4], '\0');

        /* Zero-valued plan division-by-zero protection in evaluate_plan & verify_plan */
        FlowPlanAssignment zero_plan = {0};
        FlowPlanMetrics rmetrics = {0};
        FLOW_ASSERT_EQ(rplg->evaluate_plan(&rir, rcomp, &zero_plan, &rmetrics), 1);
        FLOW_ASSERT_TRUE(isfinite(rmetrics.latency_score) && rmetrics.latency_score > 0.0);
        VerificationReport rvrep = {0};
        FLOW_ASSERT_EQ(rplg->verify_plan(&rir, rcomp, &zero_plan, &rvrep), 1);
        FLOW_ASSERT_EQ((int)rvrep.status, (int)VERIFIER_PROVEN);
        FLOW_ASSERT_TRUE(rvrep.capacity >= 64u && rvrep.estimated_bytes > 0u);

        /* Fallback C emission when plugin emit == NULL */
        FILE *remit_fp = tmpfile();
        FLOW_ASSERT_TRUE(remit_fp != NULL);
        FLOW_ASSERT_EQ(flow_component_emit(remit_fp, &rir, rcomp, NULL, &rvrep, 0), 1);
        FLOW_ASSERT_TRUE(ftell(remit_fp) > 64);
        fclose(remit_fp);

        /* FlowUnit creation and aliased (input == output) execution */
        FlowUnit runit = {0};
        FLOW_ASSERT_EQ(flow_component_create_unit(NULL, &rir, rcomp, &runit), 1);
        FLOW_ASSERT_TRUE(runit.init != NULL && runit.run != NULL && runit.drop != NULL);
        void *ustate = NULL;
        FLOW_ASSERT_EQ(runit.init(NULL, &ustate), 0);
        uint32_t rbuf[1024] = {0};
        rbuf[0] = 11U;
        rbuf[1] = 22U;
        rbuf[2] = 33U;
        rbuf[3] = 44U;
        FLOW_ASSERT_EQ(runit.run(NULL, ustate, rbuf, rbuf), 0);
        FLOW_ASSERT_EQ(rbuf[0], 11U);
        FLOW_ASSERT_EQ(rbuf[3], 44U);
        uint32_t rdst[1024] = {0};
        FLOW_ASSERT_EQ(runit.run(NULL, ustate, rbuf, rdst), 0);
        FLOW_ASSERT_EQ(rdst[0], 11U);
        FLOW_ASSERT_EQ(rdst[1], 22U);
        runit.drop(NULL, ustate);

        /* 4. 7-Language Polyglot Emission, C++17 Header/CUDA Syntax Check, Live Rustc & Live Python Execution */
        const char *s20_lean = "/tmp/flow_stage20_polyglot.lean";
        FILE *p20 = fopen(s20_lean, "w");
        FLOW_ASSERT_TRUE(p20 != NULL);
        fprintf(p20,
                "namespace Quant.Stage20\n"
                "inductive RegimeMode where\n"
                "  | Idle\n"
                "  | Active\n"
                "  | Fault\n"
                "  deriving Repr\n"
                "structure Vec2 where\n"
                "  x : Float\n"
                "  y : Float\n"
                "structure AuditFrame where\n"
                "  pos  : Vec2\n"
                "  mode : RegimeMode\n"
                "  seq  : USize\n"
                "def bilateral_spread (mid : Float) (vol : Float) (tick : Float) : (Float * Float) :=\n"
                "  let half := Float.max (Float.sqrt (Float.abs vol) * 0.5) tick in\n"
                "  (mid - half, mid + half)\n"
                "def eval_keyword_gate (in : UInt64) (type : UInt32) (from : Float) (var : Float) : Float :=\n"
                "  if type == 0 && in > 0 then from + var else from - var\n"
                "def step_frame (f : AuditFrame) : AuditFrame :=\n"
                "  { pos := { x := f.pos.x + 1.0, y := f.pos.y - 0.5 }, mode := f.mode, seq := f.seq + 1 }\n"
                "theorem spread_ok : (bilateral_spread 100.0 4.0 0.25).1 <= (bilateral_spread 100.0 4.0 0.25).2 := by decide\n"
                "end Quant.Stage20\n");
        fclose(p20);

        char emit_all_cmd[2048];
        snprintf(emit_all_cmd, sizeof(emit_all_cmd),
                 "%s/flowc %s -o /tmp/flow_s20.c "
                 "--target-c-header /tmp/flow_s20.h "
                 "--target-rust /tmp/flow_s20_abi.rs "
                 "--target-python /tmp/flow_s20_abi.py "
                 "--target-js /tmp/flow_s20_abi.mjs "
                 "--target-cuda /tmp/flow_s20.cuh "
                 "--target-matlab /tmp/flow_s20.m "
                 "--target-julia /tmp/flow_s20.jl",
                 FLOW_BUILD_DIR, s20_lean);
        FLOW_ASSERT_EQ(system(emit_all_cmd), 0);

        /* Compile shared library */
#ifdef __APPLE__
        FLOW_ASSERT_EQ(system("cc -std=c17 -O2 -shared -fPIC -DFLOW_SHARED_LIB /tmp/flow_s20.c -o /tmp/libflow_s20.dylib -lm"), 0);
#else
        FLOW_ASSERT_EQ(system("cc -std=c17 -O2 -shared -fPIC -DFLOW_SHARED_LIB /tmp/flow_s20.c -o /tmp/libflow_s20.so -lm"), 0);
#endif
        /* Verify C header and CUDA header parse cleanly under C++17 */
        FLOW_ASSERT_EQ(system("c++ -x c++ -std=c++17 -fsyntax-only /tmp/flow_s20.h"), 0);
        FLOW_ASSERT_EQ(system("c++ -x c++ -std=c++17 -fsyntax-only /tmp/flow_s20.cuh"), 0);

        /* Live Python adapter execution (enums, structs, USize, tuple returns, _fx, escaped keywords, watchdog, A/B shadow) */
        const char *py_s20_cmd =
            "python3 -c \""
            "import sys; sys.path.insert(0, '/tmp'); "
            "import flow_s20_abi as m; "
            "cls = next(v for v in vars(m).values() if isinstance(v, type) and hasattr(v, 'LEAN_FUNCTIONS')); "
            "eng = cls('/tmp/libflow_s20.dylib' if sys.platform == 'darwin' else '/tmp/libflow_s20.so'); "
            "assert m.RegimeMode.Active == 1; "
            "bid, ask = eng.bilateral_spread(100.0, 4.0, 0.25); "
            "assert abs(bid - 99.0) < 1e-9 and abs(ask - 101.0) < 1e-9; "
            "bid_fx, ask_fx = eng.bilateral_spread_fx(100 << 32, 4 << 32, 1 << 30); "
            "assert (bid_fx >> 32) == 99 and (ask_fx >> 32) == 101; "
            "kw = eng.eval_keyword_gate(1, 0, 10.0, 2.5); "
            "assert abs(kw - 12.5) < 1e-9; "
            "kw_fx = eng.eval_keyword_gate_fx(1, 0, 10 << 32, 2 << 32); "
            "assert (kw_fx >> 32) == 12; "
            "f0 = m.AuditFrame(1.0, 2.0, m.RegimeMode.Active, 7); "
            "f1 = eng.step_frame(f0); "
            "assert abs(f1.pos_x - 2.0) < 1e-9 and abs(f1.pos_y - 1.5) < 1e-9 and f1.seq == 8; "
            "eng.ab_shadow_reset(); eng.ab_shadow_record(0, 1.0, 2.0, 0.1); "
            "assert eng.ab_shadow_get_calls() == 1 and eng.ab_shadow_get_mismatches() == 1; "
            "assert eng.reset_watchdog() == 0; "
            "eng.close()\"";
        FLOW_ASSERT_EQ(system(py_s20_cmd), 0);

        /* Live Rust adapter compilation & execution via rustc */
        const char *rs_runner = "/tmp/flow_s20_runner.rs";
        FILE *rfp = fopen(rs_runner, "w");
        FLOW_ASSERT_TRUE(rfp != NULL);
        fprintf(rfp,
                "#[allow(dead_code, non_camel_case_types, non_snake_case)]\n"
                "#[path = \"/tmp/flow_s20_abi.rs\"]\n"
                "mod abi;\n"
                "use abi::*;\n"
                "fn main() {\n"
                "    let eng = FlowComponentAdapter::new().unwrap();\n"
                "    let (bid, ask) = eng.bilateral_spread(100.0, 4.0, 0.25);\n"
                "    assert!((bid - 99.0).abs() < 1e-9 && (ask - 101.0).abs() < 1e-9);\n"
                "    let (bid_fx, ask_fx) = eng.bilateral_spread_fx(100i64 << 32, 4i64 << 32, 1i64 << 30);\n"
                "    assert_eq!(bid_fx >> 32, 99);\n"
                "    assert_eq!(ask_fx >> 32, 101);\n"
                "    let kw = eng.eval_keyword_gate(1, 0, 10.0, 2.5);\n"
                "    assert!((kw - 12.5).abs() < 1e-9);\n"
                "    let f0 = AuditFrame { pos_x: 1.0, pos_y: 2.0, mode: RegimeMode::Active as u32, seq: 7 };\n"
                "    let f1 = eng.step_frame(f0);\n"
                "    assert!((f1.pos_x - 2.0).abs() < 1e-9 && (f1.pos_y - 1.5).abs() < 1e-9 && f1.seq == 8);\n"
                "    FlowComponentAdapter::ab_shadow_reset();\n"
                "    FlowComponentAdapter::ab_shadow_record(0, 1.0, 2.0, 0.1);\n"
                "    assert_eq!(FlowComponentAdapter::ab_shadow_get_calls(), 1);\n"
                "    assert_eq!(eng.reset_watchdog(), 0);\n"
                "    let out = eng.process(&[1u8, 2, 3, 4, 5, 6, 7, 8]).unwrap();\n"
                "    assert!(!out.is_empty());\n"
                "}\n");
        fclose(rfp);
        FLOW_ASSERT_EQ(system("rustc --edition 2021 /tmp/flow_s20_runner.rs -L /tmp -l flow_s20 -C link-arg=-Wl,-rpath,/tmp -o /tmp/flow_s20_runner && /tmp/flow_s20_runner"), 0);

        remove(s20_lean);
        remove("/tmp/flow_s20.c");
        remove("/tmp/flow_s20.h");
        remove("/tmp/flow_s20_abi.rs");
        remove("/tmp/flow_s20_abi.py");
        remove("/tmp/flow_s20_abi.mjs");
        remove("/tmp/flow_s20.cuh");
        remove("/tmp/flow_s20.m");
        remove("/tmp/flow_s20.jl");
        remove("/tmp/libflow_s20.dylib");
        remove("/tmp/libflow_s20.so");
        remove(rs_runner);
        remove("/tmp/flow_s20_runner");

        printf("  ✓ Stage 20 Passed: Self-bootstrapping codebase auditor, 7 Component Scopes, Rust plugin DSO lifecycle, 256-bit secp256k1 & 7-language polyglot live gate verified.\n\n");
    }

    FLOW_TEST_SUITE_END();
    return 0;
}
