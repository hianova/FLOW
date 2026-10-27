#include "backend.h"
#include "flow.h"
#include "registry.h"
#include "abi.h"
#include "induce.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void flowc_print_usage(FILE *out) {
    fprintf(out,
        "FLOW Lean 4 -> C17 Formal Synthesizer (flowc) v2.5.0 | Lean-RT & Cubical HoTT\n"
        "Usage: flowc <input.lean | --induce data.csv> [-o <output.c>] [options]\n"
        "Options:\n"
        "  -o <output.c>                 Target C17 source path (default: <input>.c)\n"
        "  --induce <samples.csv>        Induce Lean 4 spec & formal certificate from data trajectory\n"
        "  --induce-mode <mode>          Induction mode: composite (default), poly, grid, smm-cir, smm-merton, prng\n"
        "  --emit-induced-lean <f.lean>  Write synthesized Lean 4 specification to <f.lean>\n"
        "  --target-c-header <file.h>    Emit static inline double/fx64 C17 header & FlowStatusCode\n"
        "  --target-mex                  Append MATLAB MEX C17 mexFunction dispatcher to <output.c>\n"
        "  --target-lean <file.lean>     Emit & verify Lean 4 formal soundness certificate\n"
        "  --reload-adapter              Generate live reload QSBR wrapper adapter\n"
        "  --ieee64                      Force IEEE 754 64-bit floating point ABI\n"
        "  --fx64                        Force Q32.32 fixed point ABI\n"
        "  -v, --version                 Show version information\n"
        "  -h, --help                    Show this help message\n");
}

static int validate_and_lower_module(const char *mod_name, const FlowSpec *spec, SemanticIR *ir) {
    flow_registry_resolve_plugin(mod_name);
    const FlowPlugin *module = flow_registry_lookup(mod_name);
    char err_msg[128] = {0};

    if (!module) {
        fprintf(stderr, "flowc: imported domain module '%s' not registered\n", mod_name);
        return 0;
    }
    flow_plugin_lower_semantics(spec, ir, module);
    if (!flow_component_validate_contract(ir, module, err_msg, sizeof(err_msg))) {
        fprintf(stderr, "flowc: module '%s' contract validation failed: %s\n",
                mod_name, err_msg[0] ? err_msg : "invalid domain contract");
        return 0;
    }
    return 1;
}

int flowc_main(int argc, char **argv) {
    const char *input_path = NULL;
    const char *induce_csv_path = NULL;
    const char *induce_mode = "composite";
    const char *emit_induced_lean = NULL;
    const char *output_path = NULL;
    const char *target_c_header = NULL;
    const char *target_lean = NULL;
    char auto_output_buf[512] = {0};

    static FlowSpec spec;
    static SemanticIR ir;
    static FlowComponentABI abi;
    memset(&spec, 0, sizeof(spec));
    memset(&ir, 0, sizeof(ir));
    memset(&abi, 0, sizeof(abi));

    const Component *component = NULL;
    VerificationReport verification;
    memset(&verification, 0, sizeof(verification));

    int use_reload_adapter = 0;
    int force_ieee64 = 0;
    int target_mex = 0;

    if (!flow_registry_init()) {
        fprintf(stderr, "flowc: failed to initialize plugin registry\n");
        return EXIT_FAILURE;
    }

    if (argc < 2) {
        flowc_print_usage(stderr);
        return EXIT_FAILURE;
    }

    if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help") || !strcmp(argv[1], "help")) {
        flowc_print_usage(stdout);
        return EXIT_SUCCESS;
    }

    if (!strcmp(argv[1], "-v") || !strcmp(argv[1], "--version") || !strcmp(argv[1], "version")) {
        printf("FLOW Modern Declarative Compiler (flowc) v2.5.0 | Lean-RT & Cubical HoTT\n");
        return EXIT_SUCCESS;
    }

    for (int arg = 1; arg < argc; ++arg) {
        if (!strcmp(argv[arg], "-o") && arg + 1 < argc) {
            output_path = argv[++arg];
        } else if (!strcmp(argv[arg], "--induce") && arg + 1 < argc) {
            induce_csv_path = argv[++arg];
        } else if (!strcmp(argv[arg], "--induce-mode") && arg + 1 < argc) {
            induce_mode = argv[++arg];
        } else if (!strcmp(argv[arg], "--emit-induced-lean") && arg + 1 < argc) {
            emit_induced_lean = argv[++arg];
        } else if (!strcmp(argv[arg], "--reload-adapter")) {
            use_reload_adapter = 1;
        } else if (!strcmp(argv[arg], "--target-c-header") && arg + 1 < argc) {
            target_c_header = argv[++arg];
        } else if (!strcmp(argv[arg], "--target-mex")) {
            target_mex = 1;
        } else if (!strcmp(argv[arg], "--target-lean") && arg + 1 < argc) {
            target_lean = argv[++arg];
        } else if (!strcmp(argv[arg], "--ieee64") || !strcmp(argv[arg], "--float-abi=ieee64")) {
            force_ieee64 = 1;
        } else if (!strcmp(argv[arg], "--fx64") || !strcmp(argv[arg], "--float-abi=fx64")) {
            force_ieee64 = -1;
        } else if (!strcmp(argv[arg], "-h") || !strcmp(argv[arg], "--help")) {
            flowc_print_usage(stdout);
            return EXIT_SUCCESS;
        } else if (argv[arg][0] != '-' && !input_path) {
            input_path = argv[arg];
        } else {
            fprintf(stderr, "flowc: unknown option: %s\n", argv[arg]);
            return EXIT_FAILURE;
        }
    }

    if (!input_path && !induce_csv_path) {
        fprintf(stderr, "flowc: missing input .lean specification or --induce <samples.csv>\n");
        return EXIT_FAILURE;
    }

    const char *primary_path = input_path ? input_path : induce_csv_path;
    const char *dot = strrchr(primary_path, '.');
    size_t base_len = (dot && (!strcmp(dot, ".lean") || !strcmp(dot, ".csv"))) ? (size_t)(dot - primary_path) : 0;
    if (!output_path) {
        if (base_len > 0 && base_len + 3 < sizeof(auto_output_buf)) {
            memcpy(auto_output_buf, primary_path, base_len);
            strcpy(auto_output_buf + base_len, ".c");
        } else {
            snprintf(auto_output_buf, sizeof(auto_output_buf), "%s.c", primary_path);
        }
        output_path = auto_output_buf;
    }

    FILE *input = NULL;
    if (induce_csv_path) {
        double *xs = NULL, *ys = NULL;
        size_t sample_cnt = 0;
        int is_biv = 0;
        if (!flow_induce_load_csv_samples(induce_csv_path, &xs, &ys, &sample_cnt, &is_biv)) {
            fprintf(stderr, "flowc: --induce cannot load samples from %s\n", induce_csv_path);
            return EXIT_FAILURE;
        }
        FlowInducedPolyModel poly = {0};
        FlowGridEnvelope grid = {0};
        FlowSMMResult smm = {0};
        FlowPRNGAuditResult prng = {0};

        int want_poly = (!strcmp(induce_mode, "composite") || !strcmp(induce_mode, "poly"));
        int want_grid = (!strcmp(induce_mode, "composite") || !strcmp(induce_mode, "grid"));
        int want_cir  = (!strcmp(induce_mode, "composite") || !strcmp(induce_mode, "smm-cir"));
        int want_mert = (!strcmp(induce_mode, "smm-merton"));
        int want_prng = (!strcmp(induce_mode, "composite") || !strcmp(induce_mode, "prng"));

        if (want_poly) flow_induce_fit_rational_poly(xs, ys, sample_cnt, 2, 1024, &poly);
        if (want_grid) flow_induce_build_grid_envelope(xs, ys, sample_cnt, 4, 100, &grid);
        if (want_cir)  flow_induce_solve_smm(ys, sample_cnt, FLOW_SMM_CIR_REFLECTING, 0.05, 42ULL, &smm);
        if (want_mert) flow_induce_solve_smm(ys, sample_cnt, FLOW_SMM_MERTON_JUMP, 0.01, 42ULL, &smm);
        if (want_prng) flow_induce_audit_prng_floats(ys, sample_cnt < 64 ? sample_cnt : 64, 5000, &prng);

        free(xs);
        free(ys);

        input = tmpfile();
        if (!input) {
            fprintf(stderr, "flowc: failed to create temporary buffer for induced Lean 4 spec\n");
            return EXIT_FAILURE;
        }
        flow_induce_emit_lean_composite(
            input, "Flow.InducedKnowledge",
            want_poly ? &poly : NULL,
            want_grid ? &grid : NULL,
            (want_cir || want_mert) ? &smm : NULL,
            (want_prng && prng.detected) ? &prng : NULL);

        if (emit_induced_lean) {
            FILE *lout = fopen(emit_induced_lean, "w");
            if (lout) {
                flow_induce_emit_lean_composite(
                    lout, "Flow.InducedKnowledge",
                    want_poly ? &poly : NULL,
                    want_grid ? &grid : NULL,
                    (want_cir || want_mert) ? &smm : NULL,
                    (want_prng && prng.detected) ? &prng : NULL);
                fclose(lout);
                printf("  induce: synthesized Lean 4 spec -> %s\n", emit_induced_lean);
            }
        }
        rewind(input);
        input_path = induce_csv_path;
    } else {
        input = fopen(input_path, "r");
        if (!input) {
            fprintf(stderr, "flowc: cannot open %s: %s\n", input_path, strerror(errno));
            return EXIT_FAILURE;
        }
    }

    if (!parse_spec(input, &spec)) {
        fclose(input);
        return EXIT_FAILURE;
    }
    fclose(input);

    if (force_ieee64 != 0) {
        spec.float_abi_ieee64 = (force_ieee64 > 0) ? 1 : 0;
    }
    if (target_c_header != NULL || target_mex != 0) {
        spec.omit_dummy_main = 1;
    }

    lower_to_ir(&spec, &ir);
    if (target_lean) {
        ir.fact_lean_certified = 1;
    }

    if (ir.imported_module_count > 0) {
        for (size_t m = 0; m < ir.imported_module_count; ++m) {
            if (!validate_and_lower_module(ir.imported_modules[m], &spec, &ir)) {
                flow_ir_cleanup(&ir);
                return EXIT_FAILURE;
            }
        }
    } else {
        const char *def_mod = spec.plugin_name[0] ? spec.plugin_name : "builtin";
        if (!validate_and_lower_module(def_mod, &spec, &ir)) {
            flow_ir_cleanup(&ir);
            return EXIT_FAILURE;
        }
    }

    component = select_component(&ir);
    if (!component) {
        fprintf(stderr, "flowc: no registered plugin candidate satisfies the spec\n");
        flow_ir_cleanup(&ir);
        return EXIT_FAILURE;
    }

    if (use_reload_adapter && !flow_component_supports_reload(component)) {
        fprintf(stderr, "flowc: --reload-adapter is not supported by component '%s'\n", component->id);
        flow_ir_cleanup(&ir);
        return EXIT_FAILURE;
    }

    if (!verify_candidate(&ir, component, NULL, &verification)) {
        fprintf(stderr, "flowc: verifier: %s\n", verification.message);
        flow_ir_cleanup(&ir);
        return EXIT_FAILURE;
    }

    FlowSMTProofAttestation proof = {0};
    FlowPlanMetrics smt_m = {0};
    smt_m.capacity = verification.capacity > 0 ? verification.capacity : (size_t)(ir.input_max_count > 0 ? ir.input_max_count : 4096);
    smt_m.shards = 1;
    smt_m.threads = 1;
    smt_m.memory_bytes = verification.estimated_bytes;

    int smt_ok = flow_smt_verify(&ir, component, NULL, &smt_m, &proof);
    FlowSMTResult status = (!smt_ok ||
                            proof.buffer_bounds_safety == FLOW_SMT_VIOLATION_SAT ||
                            proof.memory_quota_bound == FLOW_SMT_VIOLATION_SAT ||
                            proof.shard_non_aliasing == FLOW_SMT_VIOLATION_SAT ||
                            proof.determinism_invariant == FLOW_SMT_VIOLATION_SAT)
                           ? FLOW_SMT_VIOLATION_SAT
                           : ((proof.buffer_bounds_safety != FLOW_SMT_PROVEN_UNSAT ||
                               proof.memory_quota_bound != FLOW_SMT_PROVEN_UNSAT ||
                               proof.shard_non_aliasing != FLOW_SMT_PROVEN_UNSAT ||
                               proof.determinism_invariant != FLOW_SMT_PROVEN_UNSAT)
                              ? FLOW_SMT_UNKNOWN : FLOW_SMT_PROVEN_UNSAT);

    if (status != FLOW_SMT_PROVEN_UNSAT) {
        fprintf(stderr, "flowc: SMT proof failed: status=%s (%s)\n",
                flow_smt_result_name(status), proof.proof_summary);
        flow_ir_cleanup(&ir);
        return EXIT_FAILURE;
    }

    FILE *output = fopen(output_path, "w");
    if (!output) {
        fprintf(stderr, "flowc: cannot write %s: %s\n", output_path, strerror(errno));
        flow_ir_cleanup(&ir);
        return EXIT_FAILURE;
    }

    int generated_ok = emit_c(output, &ir, component, NULL, &verification, use_reload_adapter);
    if (generated_ok && target_mex) {
        generated_ok = flow_abi_emit_mex_dispatcher(output, &ir);
    }
    if (fclose(output) != 0 || !generated_ok) {
        fprintf(stderr, "flowc: failed to generate %s\n", output_path);
        flow_ir_cleanup(&ir);
        return EXIT_FAILURE;
    }

    printf("flowc: %s -> %s\n", input_path, output_path);
    if (target_mex) {
        printf("  target-mex: appended mexFunction dispatcher to %s\n", output_path);
    }
    if (ir.project_name[0]) {
        printf("  project: %s\n", ir.project_name);
    }

    printf("  IR: output=%s shared=%d read_heavy=%d bounded=%d parallelizable=%d ordered=%d unordered=%d "
           "deterministic=%d range=%d size_preserved=%d mutability_read_only=%d ensures=%d "
           "declared_constraints=%zu resource=%s capability=%s domain=%s contract=%s fallback=%s "
           "graph_nodes=%zu facts=%zu constraints=%zu holes=%zu\n"
           "  selected: %s (%s)\n  candidates:",
           ir.output_name, ir.state_shared, ir.state_read_heavy, ir.state_bounded,
           ir.flow_parallelizable, ir.fact_ordered, ir.fact_unordered, ir.fact_deterministic,
           ir.fact_range_proven, ir.fact_size_preserved, ir.fact_mutability_read_only,
           ir.ensure_count, ir.declared_constraint_count, ir.resource_name, ir.capability_name,
           ir.domain_name, ir.contract_name, ir.fallback_policy, ir.flow_node_count,
           ir.fact_count, ir.constraint_count, ir.hole_count,
           component->id, component->kind);

    for (size_t ci = 0; ci < compatible_component_count(&ir); ++ci) {
        const Component *cand = compatible_component_at(&ir, ci);
        if (cand) printf(" %s", cand->id);
    }
    printf("\n  verifier: status=%s capacity=%zu estimated_bytes=%zu (%s)\n",
           verification_status_name(verification.status),
           verification.capacity, verification.estimated_bytes, verification.message);

    size_t cap = ir.input_max_count > 0 ? (size_t)ir.input_max_count : (verification.capacity > 0 ? verification.capacity : 64);
    printf("  symplectic: omega=%.3f mu=%.6f q_sat=%.1f (polyhedral shaped)\n",
           fmax(0.1, sqrt((double)cap) / 16.0), 1.0 / (double)cap, 1.25 * (double)cap);

    if (target_c_header || target_lean) {
        flow_abi_build_for_component(&ir, component, &abi);
        if (target_c_header) {
            FILE *fp = fopen(target_c_header, "w");
            if (fp) {
                flow_abi_emit_c_header(fp, &abi);
                fclose(fp);
                printf("  target-c-header: wrote %s\n", target_c_header);
            }
        }
        if (target_lean) {
            FILE *fp = fopen(target_lean, "w");
            if (fp) {
                flow_abi_emit_lean_adapter(fp, &ir, &abi, 0, verification.capacity, 1);
                fclose(fp);
                printf("  target-lean: wrote %s\n", target_lean);
            }
            char lean_err[256] = {0};
            if (!flow_abi_verify_lean_certificate(target_lean, lean_err, sizeof(lean_err))) {
                fprintf(stderr, "flowc: lean certificate verification failed: %s\n", lean_err);
                flow_ir_cleanup(&ir);
                return EXIT_FAILURE;
            }
        }
    }

    printf("  SMT proof: status=%s (%s)\n", flow_smt_result_name(status), proof.proof_summary);
    flow_ir_cleanup(&ir);
    return EXIT_SUCCESS;
}

#ifndef FLOWC_NO_MAIN
int main(int argc, char **argv) {
    return flowc_main(argc, argv);
}
#endif
