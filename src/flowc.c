#include "backend.h"
#include "flow.h"
#include "abi.h"
#include "cubical_hott.h"
#include "llvm_lifter.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

int flowc_main(int argc, char **argv);

static void flowc_print_usage(FILE *out) {
    fprintf(
        out,
        "FLOW Lean 4 -> C17 Formal Synthesizer (flowc) v" FLOW_VERSION_STRING
        " (git: " FLOW_GIT_PROVENANCE ") | Lean-RT & Cubical HoTT\n"
        "Usage: flowc <input.lean> [-o <output.c>] [options]\n"
        "Options:\n"
        "  -o <output.c>                 Target C17 source path (default: <input>.c)\n"
        "  --target-c-header <file.h>    Emit static inline IEEE-754 C17 header & FlowStatusCode\n"
        "  --target-mex                  Append MATLAB MEX C17 mexFunction dispatcher to <output.c>\n"
        "  --target-lean <file.lean>     Emit & verify Lean 4 formal soundness certificate\n"
        "  --lift-llvm <source.c|.ll>    Reverse lift C/LLVM IR SSA into Lean 4 model & boundary proof\n"
        "  --reload-adapter              Generate live reload QSBR wrapper adapter\n"
        "  --ieee64                      IEEE 754 64-bit floating point ABI (default)\n"
        "  -q, --quiet                   Suppress compilation summary output\n"
        "  -v, --version                 Show version information\n"
        "  -h, --help                    Show this help message\n");
}

static int flowc_run_lift_llvm_pipeline(const char *in_path, const char *out_path, int quiet) {
    FlowLLVMLiftedFunction fns[16];
    size_t fn_count = 0;
    int rc = 0;

    const char *dot = strrchr(in_path, '.');
    if (dot && (!strcmp(dot, ".c") || !strcmp(dot, ".h"))) {
        rc = flow_llvm_lift_c_source(in_path, fns, 16, &fn_count);
    } else {
        FILE *fp = fopen(in_path, "rb");
        if (!fp) {
            fprintf(stderr, "flowc: cannot open LLVM IR '%s': %s\n", in_path, strerror(errno));
            return EXIT_FAILURE;
        }
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (sz <= 0) {
            fclose(fp);
            fprintf(stderr, "flowc: empty LLVM IR file '%s'\n", in_path);
            return EXIT_FAILURE;
        }
        char *ir_text = (char *)calloc((size_t)sz + 1, 1);
        if (!ir_text) {
            fclose(fp);
            fprintf(stderr, "flowc: out of memory reading '%s'\n", in_path);
            return EXIT_FAILURE;
        }
        size_t nread = fread(ir_text, 1, (size_t)sz, fp);
        ir_text[nread] = '\0';
        fclose(fp);
        rc = flow_llvm_lift_ir_text(ir_text, fns, 16, &fn_count);
        free(ir_text);
    }

    if (rc != 0 || fn_count == 0) {
        fprintf(stderr, "flowc: reverse lifting failed for '%s'\n", in_path);
        return EXIT_FAILURE;
    }

    for (size_t i = 0; i < fn_count; ++i) {
        flow_llvm_infer_bounds(&fns[i], NULL);
    }

    char auto_cert_buf[512] = {0};
    if (!out_path) {
        size_t base_len = dot ? (size_t)(dot - in_path) : strlen(in_path);
        if (base_len + 18 < sizeof(auto_cert_buf)) {
            memcpy(auto_cert_buf, in_path, base_len);
            strcpy(auto_cert_buf + base_len, "_lifted_cert.lean");
            out_path = auto_cert_buf;
        } else {
            out_path = "lifted_cert.lean";
        }
    }

    FILE *out_fp = fopen(out_path, "w");
    if (!out_fp) {
        fprintf(stderr, "flowc: cannot write '%s': %s\n", out_path, strerror(errno));
        return EXIT_FAILURE;
    }

    int emit_rc = flow_llvm_emit_lean_boundary_proof(out_fp, NULL, fns, fn_count);
    fclose(out_fp);
    if (emit_rc != 0) {
        fprintf(stderr, "flowc: failed to emit Lean boundary certificate\n");
        return EXIT_FAILURE;
    }

    char verify_err[256] = {0};
    if (!flow_abi_verify_lean_certificate(out_path, verify_err, sizeof(verify_err))) {
        fprintf(stderr, "flowc: lean verification failed: %s\n", verify_err);
        return EXIT_FAILURE;
    }

    if (!quiet) {
        printf("flowc: --lift-llvm %s -> %s (lifted %zu functions, Lean 4 verified)\n", in_path,
               out_path, fn_count);
    }
    return EXIT_SUCCESS;
}

int flowc_main(int argc, char **argv) {
    const char *input_path = NULL;
    const char *output_path = NULL;
    const char *target_c_header = NULL;
    const char *target_lean = NULL;
    char auto_output_buf[512] = {0};

    static FlowSpec spec;
    static SemanticIR ir;
    memset(&spec, 0, sizeof(spec));
    memset(&ir, 0, sizeof(ir));

    int force_ieee64 = 0;
    int target_mex = 0;
    int quiet_mode = 0;
    int lift_llvm_mode = 0;
    const char *lift_llvm_file = NULL;

    if (argc < 2) {
        flowc_print_usage(stderr);
        return EXIT_FAILURE;
    }

    if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help") || !strcmp(argv[1], "help")) {
        flowc_print_usage(stdout);
        return EXIT_SUCCESS;
    }

    if (!strcmp(argv[1], "-v") || !strcmp(argv[1], "--version") || !strcmp(argv[1], "version")) {
        printf("FLOW Modern Declarative Compiler (flowc) v" FLOW_VERSION_STRING
               " (git: " FLOW_GIT_PROVENANCE ") | Lean-RT & Cubical HoTT\n");
        return EXIT_SUCCESS;
    }

    for (int arg = 1; arg < argc; ++arg) {
        if (!strcmp(argv[arg], "-o") && arg + 1 < argc) {
            output_path = argv[++arg];
        } else if (!strcmp(argv[arg], "--reload-adapter")) {
            /* retained for CLI flag compatibility */
        } else if (!strcmp(argv[arg], "--target-c-header") && arg + 1 < argc) {
            target_c_header = argv[++arg];
        } else if (!strcmp(argv[arg], "--target-mex")) {
            target_mex = 1;
        } else if (!strcmp(argv[arg], "--target-lean") && arg + 1 < argc) {
            target_lean = argv[++arg];
        } else if (!strcmp(argv[arg], "--lift-llvm")) {
            lift_llvm_mode = 1;
            if (arg + 1 < argc && argv[arg + 1][0] != '-') {
                lift_llvm_file = argv[++arg];
            }
        } else if (!strcmp(argv[arg], "--ieee64") || !strcmp(argv[arg], "--float-abi=ieee64")) {
            force_ieee64 = 1;
        } else if (!strcmp(argv[arg], "-q") || !strcmp(argv[arg], "--quiet")) {
            quiet_mode = 1;
            flow_parser_set_quiet(1);
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

    if (lift_llvm_mode) {
        if (!lift_llvm_file && input_path) {
            lift_llvm_file = input_path;
        }
        if (!lift_llvm_file) {
            fprintf(stderr, "flowc: --lift-llvm requires an input file (.c or .ll)\n");
            return EXIT_FAILURE;
        }
        return flowc_run_lift_llvm_pipeline(lift_llvm_file, target_lean ? target_lean : output_path,
                                            quiet_mode);
    }

    if (!input_path) {
        fprintf(stderr, "flowc: missing input .lean specification\n");
        return EXIT_FAILURE;
    }

    const char *dot = strrchr(input_path, '.');
    size_t base_len = (dot && !strcmp(dot, ".lean")) ? (size_t)(dot - input_path) : 0;
    if (!output_path) {
        if (base_len > 0 && base_len + 3 < sizeof(auto_output_buf)) {
            memcpy(auto_output_buf, input_path, base_len);
            strcpy(auto_output_buf + base_len, ".c");
        } else {
            snprintf(auto_output_buf, sizeof(auto_output_buf), "%s.c", input_path);
        }
        output_path = auto_output_buf;
    }

    FILE *input = fopen(input_path, "r");
    if (!input) {
        fprintf(stderr, "flowc: cannot open %s: %s\n", input_path, strerror(errno));
        return EXIT_FAILURE;
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

    FILE *output = fopen(output_path, "w");
    if (!output) {
        fprintf(stderr, "flowc: cannot write %s: %s\n", output_path, strerror(errno));
        flow_ir_cleanup(&ir);
        return EXIT_FAILURE;
    }

    int generated_ok = emit_c(output, &ir);
    if (generated_ok && target_mex) {
        generated_ok = flow_abi_emit_mex_dispatcher(output, &ir);
    }
    if (fclose(output) != 0 || !generated_ok) {
        fprintf(stderr, "flowc: failed to generate %s\n", output_path);
        flow_ir_cleanup(&ir);
        return EXIT_FAILURE;
    }

    size_t total_fields = 0, total_invariants = 0, total_variants = 0;
    size_t aos_total_bytes = 0, soa64_total_bytes = 0;
    FlowHoTTUniverse local_u0;
    flow_hott_universe_reset(&local_u0);
    uint64_t base_sieve = flow_cubical_sieve_from_ir(&ir).mask;
    for (size_t si = 0; si < ir.lean_struct_count; ++si) {
        const FlowLeanStruct *st = &ir.lean_structs[si];
        total_fields += st->field_count;
        total_invariants += st->invariant_count;
        aos_total_bytes += st->byte_size;
        soa64_total_bytes += st->byte_size * FLOW_SIMD_SOA_LANES;
        flow_hott_register_type(&local_u0, st->name, FLOW_HOTT_LANG_LEAN, st->field_count,
                                st->byte_size, st->align_bytes, 0U, base_sieve);
        flow_hott_register_type(&local_u0, st->name, FLOW_HOTT_LANG_C17, st->field_count,
                                st->byte_size, st->align_bytes, 0U, base_sieve);
        if (st->field_count > 0) {
            char soa_name[128] = {0};
            snprintf(soa_name, sizeof(soa_name), "%s_SOA64", st->name);
            flow_hott_register_soa_equivalence(&local_u0, st->name, soa_name, st->field_count,
                                               st->byte_size, FLOW_SIMD_SOA_LANES, 0U, base_sieve);
        }
    }
    int u0_univalent = flow_hott_verify_universe_all(&local_u0);
    for (size_t ei = 0; ei < ir.lean_enum_count; ++ei) {
        total_variants += ir.lean_enums[ei].variant_count;
    }

    size_t fn_soa64 = 0, fn_jet2 = 0, fn_certified = 0, fn_try = 0;
    size_t pre_guards = 0, post_guards = 0, thm_implies_guards = 0;
    uint64_t wcet_min = UINT64_MAX, wcet_max = 0;
    for (size_t fi = 0; fi < ir.lean_function_count; ++fi) {
        const FlowLeanFunction *fn = &ir.lean_functions[fi];
        if (fn->variant_mask & FLOW_FN_VAR_SOA64)
            fn_soa64++;
        if (fn->variant_mask & FLOW_FN_VAR_JET2)
            fn_jet2++;
        if (fn->variant_mask & FLOW_FN_VAR_CERTIFIED)
            fn_certified++;
        if (fn->variant_mask & FLOW_FN_VAR_TRY)
            fn_try++;
        if (fn->precond_expr[0])
            pre_guards++;
        if (fn->postcond_expr[0])
            post_guards++;
        thm_implies_guards += fn->thm_guard_count;
        if (fn->estimated_wcet_ns < wcet_min)
            wcet_min = fn->estimated_wcet_ns;
        if (fn->estimated_wcet_ns > wcet_max)
            wcet_max = fn->estimated_wcet_ns;
    }
    if (wcet_min == UINT64_MAX)
        wcet_min = 0;

    if (!quiet_mode) {
        printf("flowc: %s -> %s\n", input_path, output_path);
        if (target_mex) {
            printf("  target-mex: appended mexFunction dispatcher to %s\n", output_path);
        }
        printf("  lean-ir: module=%s | structs=%zu (fields=%zu, invariants=%zu) | enums=%zu "
               "(variants=%zu) | fns=%zu (soa64=%zu, jet2=%zu, certified=%zu, try=%zu) | "
               "theorems=%zu\n",
               ir.flow_name, ir.lean_struct_count, total_fields, total_invariants,
               ir.lean_enum_count, total_variants, ir.lean_function_count, fn_soa64, fn_jet2,
               fn_certified, fn_try, ir.lean_theorem_count);
        printf("  u0-layout: AoS=%zuB | SOA64=%zuB (%u-lane SIMD) | Univalence=%s (%zu types)\n",
               aos_total_bytes, soa64_total_bytes, (unsigned)FLOW_SIMD_SOA_LANES,
               u0_univalent ? (local_u0.distinct_type_count == 0 ? "VACUOUS" : "UNIVALENT")
                            : "BROKEN",
               local_u0.distinct_type_count);
        printf("  contracts: struct_invariants=%zu | pre_guards=%zu | thm_implies_guards=%zu | "
               "post_guards=%zu | wcet_ns=[%llu..%llu]\n",
               total_invariants, pre_guards, thm_implies_guards, post_guards,
               (unsigned long long)wcet_min, (unsigned long long)wcet_max);
    }

    if (target_c_header || target_lean) {
        if (target_c_header) {
            FILE *fp = fopen(target_c_header, "w");
            if (fp) {
                flow_abi_emit_c_header(fp, &ir);
                fclose(fp);
                if (!quiet_mode)
                    printf("  target-c-header: wrote %s\n", target_c_header);
            }
        }
        if (target_lean) {
            FILE *fp = fopen(target_lean, "w");
            if (fp) {
                flow_abi_emit_lean_adapter(fp, &ir);
                fclose(fp);
                if (!quiet_mode)
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

    flow_ir_cleanup(&ir);
    return EXIT_SUCCESS;
}

#ifndef FLOWC_NO_MAIN
int main(int argc, char **argv) {
    return flowc_main(argc, argv);
}
#endif
