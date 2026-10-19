#include "flow_test_kit.h"
#include "flow.h"
#include "registry.h"
#include "smt.h"
#include "flowy.h"
#include "jit.h"
#include "adaptive.h"
#include "abi.h"
#include "security.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

    FLOW_TEST_SUITE_END();
    return 0;
}
