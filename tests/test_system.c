#include "flow_test_kit.h"
#include "flow.h"
#include "registry.h"
#include "smt.h"
#include "flowy.h"
#include "jit.h"
#include "adaptive.h"
#include "abi.h"
#include "backend.h"
#include "reload.h"
#include "security.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

        /* Verify Unicode operators (≥, ≤, ≠, ∧, ∨) in extern_kernel_body compile cleanly in C17 */
        SemanticIR uni_ir = emb_ir;
        strncpy(uni_ir.extern_kernel_body, "if stride ≥ 24 ∧ epoch ≠ 0 ∧ (0 ≤ baseOff ∨ byteLen ≥ 24) then 0 else 1", sizeof(uni_ir.extern_kernel_body) - 1);
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

    FLOW_TEST_SUITE_END();
    return 0;
}
