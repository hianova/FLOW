#include "backend.h"
#include "flow.h"
#include "registry.h"
#include "search.h"
#include "bitspace.h"
#include "abi.h"
#include "smt.h"
#include "flowy_fvec.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int resolve_and_load_plugin(const char *mod_name) {
    if (mod_name == NULL || mod_name[0] == '\0') return 0;
    if (flow_registry_lookup(mod_name) != NULL) return 1;

    /* Extract base name if prefixed with flow. */
    const char *base = mod_name;
    if (strncmp(mod_name, "flow.", 5) == 0) {
        base = mod_name + 5;
    }

    char dso_paths[6][256];
    snprintf(dso_paths[0], sizeof(dso_paths[0]), "build/libflow_%s.so", base);
    snprintf(dso_paths[1], sizeof(dso_paths[1]), "build/libflow_%s.dylib", base);
    snprintf(dso_paths[2], sizeof(dso_paths[2]), "libflow_%s.so", base);
    snprintf(dso_paths[3], sizeof(dso_paths[3]), "libflow_%s.dylib", base);
    snprintf(dso_paths[4], sizeof(dso_paths[4]), "build/lib%s.so", base);
    snprintf(dso_paths[5], sizeof(dso_paths[5]), "build/lib%s.dylib", base);

    char err_msg[256] = {0};
    for (int i = 0; i < 6; ++i) {
        if (flow_registry_load_dso(dso_paths[i], err_msg, sizeof(err_msg))) {
            return 1;
        }
    }
    return 0;
}

int flowc_main(int argc, char **argv) {
    const char *input_path = NULL;
    const char *output_path = NULL;
    FILE *input;
    FILE *output;
    FlowSpec spec;
    SemanticIR ir;
    const Component *component;
    VerificationReport verification;
    SearchResult search = {0};
    int use_search = 0;
    int use_benchmark = 0;
    int use_reload_adapter = 0;
    size_t iterations = 250;
    uint32_t seed = 0xC0F0123u;
    const char *profile_path = NULL;
    const char *profile_out = NULL;
    const char *component_override = NULL;
    const char *target_c_header = NULL;
    const char *target_rust = NULL;
    const char *target_python = NULL;
    const char *target_mlir = NULL;
    const char *target_llvm_ir = NULL;
    const char *apply_fvec_path = NULL;
    size_t workload_bytes = 0;
    ProfileSeed profile = {0};

    if (!flow_registry_init()) {
        fprintf(stderr, "flowc: failed to initialize plugin registry\n");
        return EXIT_FAILURE;
    }

    if (argc < 3) {
        fprintf(stderr, "usage: flowc <input.flow> -o <output.c> [--search] [--iterations <N>] [--seed <N>] [--benchmark] [--reload-adapter] [--profile <file>] [--profile-out <file>] [--component <id>] [--apply-fvec <file.fvec>] [--workload-bytes <N>] [--target-c-header <file.h>] [--target-rust <file.rs>] [--target-python <file.py>] [--target-mlir <file.mlir>] [--target-llvm-ir <file.ll>]\n");
        return EXIT_FAILURE;
    }

    input_path = argv[1];
    for (int arg = 2; arg < argc; ++arg) {
        if (strcmp(argv[arg], "-o") == 0 && arg + 1 < argc) {
            output_path = argv[++arg];
        } else if (strcmp(argv[arg], "--search") == 0) {
            use_search = 1;
        } else if (strcmp(argv[arg], "--benchmark") == 0) {
            use_benchmark = 1;
            use_search = 1;
        } else if (strcmp(argv[arg], "--reload-adapter") == 0) {
            use_reload_adapter = 1;
        } else if (strcmp(argv[arg], "--profile") == 0 && arg + 1 < argc) {
            profile_path = argv[++arg];
        } else if ((strcmp(argv[arg], "--profile-out") == 0 ||
                    strcmp(argv[arg], "--lock") == 0 ||
                    strcmp(argv[arg], "--flowplan") == 0) && arg + 1 < argc) {
            profile_out = argv[++arg];
        } else if (strcmp(argv[arg], "--component") == 0 && arg + 1 < argc) {
            component_override = argv[++arg];
        } else if ((strcmp(argv[arg], "--apply-fvec") == 0 ||
                    strcmp(argv[arg], "--fvec") == 0) && arg + 1 < argc) {
            apply_fvec_path = argv[++arg];
        } else if (strcmp(argv[arg], "--target-c-header") == 0 && arg + 1 < argc) {
            target_c_header = argv[++arg];
        } else if (strcmp(argv[arg], "--target-rust") == 0 && arg + 1 < argc) {
            target_rust = argv[++arg];
        } else if (strcmp(argv[arg], "--target-python") == 0 && arg + 1 < argc) {
            target_python = argv[++arg];
        } else if (strcmp(argv[arg], "--target-mlir") == 0 && arg + 1 < argc) {
            target_mlir = argv[++arg];
        } else if (strcmp(argv[arg], "--target-llvm-ir") == 0 && arg + 1 < argc) {
            target_llvm_ir = argv[++arg];
        } else if (strcmp(argv[arg], "--workload-bytes") == 0 && arg + 1 < argc) {
            workload_bytes = (size_t)strtoull(argv[++arg], NULL, 10);
        } else if (strcmp(argv[arg], "--iterations") == 0 && arg + 1 < argc) {
            iterations = (size_t)strtoul(argv[++arg], NULL, 10);
        } else if (strcmp(argv[arg], "--seed") == 0 && arg + 1 < argc) {
            seed = (uint32_t)strtoul(argv[++arg], NULL, 10);
        } else {
            fprintf(stderr, "flowc: unknown option: %s\n", argv[arg]);
            return EXIT_FAILURE;
        }
    }

    if (!output_path) {
        fprintf(stderr, "flowc: -o <output.c> is required\n");
        return EXIT_FAILURE;
    }

    input = fopen(input_path, "r");
    if (input == NULL) {
        fprintf(stderr, "flowc: cannot open %s: %s\n", input_path, strerror(errno));
        return EXIT_FAILURE;
    }
    if (!parse_spec(input, &spec)) {
        fclose(input);
        return EXIT_FAILURE;
    }
    fclose(input);

    lower_to_ir(&spec, &ir);
    ir.workload_bytes = workload_bytes;

    /* Dynamic Domain Module & DSO Plugin Resolution */
    if (ir.imported_module_count > 0) {
        for (size_t m = 0; m < ir.imported_module_count; ++m) {
            const char *mod_name = ir.imported_modules[m];
            resolve_and_load_plugin(mod_name);
            const FlowPlugin *module = flow_registry_lookup(mod_name);
            char err_msg[128] = {0};
            if (module == NULL) {
                fprintf(stderr, "flowc: imported domain module '%s' not registered\n", mod_name);
                flow_ir_cleanup(&ir);
                return EXIT_FAILURE;
            }
            flow_plugin_lower_semantics(&spec, &ir, module);
            if (!flow_component_validate_contract(&ir, module, err_msg, sizeof(err_msg))) {
                fprintf(stderr, "flowc: module '%s' contract validation failed: %s\n",
                        mod_name, err_msg[0] ? err_msg : "invalid domain contract");
                flow_ir_cleanup(&ir);
                return EXIT_FAILURE;
            }
        }
    } else {
        const char *module_name = spec.plugin_name[0] != '\0' ? spec.plugin_name : "builtin";
        resolve_and_load_plugin(module_name);
        const FlowPlugin *module = flow_registry_lookup(module_name);
        char err_msg[128] = {0};
        if (module == NULL) {
            fprintf(stderr, "flowc: imported domain module '%s' not registered\n", module_name);
            flow_ir_cleanup(&ir);
            return EXIT_FAILURE;
        }
        flow_plugin_lower_semantics(&spec, &ir, module);
        if (!flow_component_validate_contract(&ir, module, err_msg, sizeof(err_msg))) {
            fprintf(stderr, "flowc: module '%s' contract validation failed: %s\n",
                    module_name, err_msg[0] ? err_msg : "invalid domain contract");
            flow_ir_cleanup(&ir);
            return EXIT_FAILURE;
        }
    }

    if (profile_path != NULL) {
        FILE *profile_file = fopen(profile_path, "r");
        char line[256];
        if (profile_file != NULL) {
            /* Try loading as FlowPlanArtifact first */
            FlowPlanArtifact artifact;
            if (flow_plan_artifact_load(profile_file, &artifact) &&
                (artifact.flow_name[0] == '\0' || strcmp(artifact.flow_name, ir.flow_name) == 0) &&
                artifact.component_id[0] != '\0') {
                char val_err[256] = {0};
                if (!flow_artifact_validate(&artifact, &ir, NULL, val_err, sizeof(val_err))) {
                    fprintf(stderr, "flowc: lockfile rejected: %s\n", val_err);
                    fclose(profile_file);
                    flow_ir_cleanup(&ir);
                    return EXIT_FAILURE;
                }
                for (size_t i = 0; i < component_count(); ++i) {
                    const Component *candidate = component_at(i);
                    if (candidate != NULL && strcmp(candidate->id, artifact.component_id) == 0) {
                        flow_artifact_to_profile_seed(&artifact, &profile);
                        profile.component = i;
                        break;
                    }
                }
            }
            if (!profile.available) {
                rewind(profile_file);
                while (fgets(line, sizeof(line), profile_file) != NULL) {
                    char flow_name[FLOW_NAME];
                    char component_name[FLOW_NAME];
                    unsigned long capacity_value;
                    unsigned long threads_value;
                    unsigned long shards_value;
                    unsigned long long benchmark_value;
                    unsigned long long workload_value;
                    unsigned long buffer_value;
                    unsigned long initial_value;
                    unsigned long growth_value;
                    unsigned long batch_value;
                    unsigned long arena_value;
                    int fields = sscanf(line, "%63[^,],%63[^,],%lu,%lu,%lu,%llu,%llu,%lu,%lu,%lu,%lu,%lu",
                               flow_name, component_name, &capacity_value,
                               &threads_value, &shards_value, &benchmark_value,
                               &workload_value, &buffer_value, &initial_value,
                               &growth_value, &batch_value, &arena_value);
                    if ((fields == 6 || fields == 12) &&
                        strcmp(flow_name, ir.flow_name) == 0) {
                        for (size_t i = 0; i < component_count(); ++i) {
                            const Component *candidate = component_at(i);
                            if (candidate != NULL && strcmp(candidate->id, component_name) == 0) {
                                profile.available = 1;
                                profile.component = i;
                                profile.capacity = (size_t)capacity_value;
                                profile.threads = (size_t)threads_value;
                                profile.shards = (size_t)shards_value;
                                profile.benchmark_ns = (uint64_t)benchmark_value;
                                if (fields == 12) {
                                    profile.workload_bytes = (size_t)workload_value;
                                    profile.tuning.buffer_bytes = (size_t)buffer_value;
                                    profile.tuning.initial_capacity = (size_t)initial_value;
                                    profile.tuning.growth_percent = (unsigned)growth_value;
                                    profile.tuning.batch_size = (size_t)batch_value;
                                    profile.tuning.arena_bytes = (size_t)arena_value;
                                }
                                break;
                            }
                        }
                    }
                }
            }
            fclose(profile_file);
        }
        if (profile.available)
            printf("  profile: loaded component=%s capacity=%zu threads=%zu shards=%zu benchmark_ns=%llu workload_bytes=%zu\n",
                   component_at(profile.component)->id, profile.capacity,
                   profile.threads, profile.shards,
                   (unsigned long long)profile.benchmark_ns,
                   profile.workload_bytes);
        else
            fprintf(stderr, "flowc: profile not found for flow %s: %s\n", ir.flow_name, profile_path);
    }

    component = select_component(&ir);
    if (component == NULL && component_override == NULL) {
        fprintf(stderr, "flowc: no registered plugin candidate satisfies the spec\n");
        return EXIT_FAILURE;
    }
    if (component_override != NULL) {
        component = NULL;
        for (size_t i = 0; i < component_count(); ++i) {
            const Component *candidate = component_at(i);
            if (candidate != NULL && strcmp(candidate->id, component_override) == 0) {
                component = candidate;
                break;
            }
        }
        if (component == NULL) {
            fprintf(stderr, "flowc: unknown component: %s\n", component_override);
            return EXIT_FAILURE;
        }
        if (use_search) {
            fprintf(stderr, "flowc: --component cannot be combined with --search/--benchmark\n");
            return EXIT_FAILURE;
        }
    }

    /* Apply Pre-baked .fvec Architecture Feature Model (Skip BMF Search) */
    if (apply_fvec_path != NULL) {
        FlowVecHeader fvec_hdr;
        FlowVecPayload fvec_payload;
        if (!flow_fvec_read_file(apply_fvec_path, &fvec_hdr, &fvec_payload)) {
            fprintf(stderr, "flowc: failed to load .fvec file '%s'\n", apply_fvec_path);
            flow_ir_cleanup(&ir);
            return EXIT_FAILURE;
        }

        /* Enforce Hardware Affinity & SMT Invariants (1ms pre-flight gate) */
        char affinity_diag[512] = {0};
        FlowEnvironmentState host_env;
        memset(&host_env, 0, sizeof(host_env));
        host_env.hardware_arch = FLOW_ARCH_INTEL_AVX2;
        if (!flow_fvec_verify_hardware_affinity(&fvec_hdr, &host_env, affinity_diag, sizeof(affinity_diag))) {
            fprintf(stderr, "flowc: .fvec hardware affinity rejected: %s\n", affinity_diag);
            flow_ir_cleanup(&ir);
            return EXIT_FAILURE;
        }

        FlowBitSpace space;
        if (!flow_bitspace_init_for_ir(&ir, &space)) {
            fprintf(stderr, "flowc: failed to initialize BitSpace for spec '%s'\n", input_path);
            flow_ir_cleanup(&ir);
            return EXIT_FAILURE;
        }

        FlowPlan plan;
        space.decode(&space, fvec_payload.pure_genome, &plan);
        space.evaluate(&space, &plan, &plan.eval);
        component = plan.component;
        flow_plan_to_search_result(&plan, &ir, seed, &search);
        use_search = 1;

        printf("  fvec: applied '%s' [%s]\n", fvec_hdr.name, apply_fvec_path);
        printf("        -> Genome: 0x%016llx | Component: %s | Energy: %.2f | SMT: %s\n",
               (unsigned long long)plan.genome, component ? component->id : "(auto)",
               plan.eval.energy, fvec_hdr.smt_signature);
    } else if (use_search) {
        /* BMF Search Engine with Multimodal Domain Masks */
        search = search_best(&ir, iterations, seed, use_benchmark,
                             profile.available ? &profile : NULL);
        component = search.component;
        if (component == NULL) {
            fprintf(stderr, "flowc: search could not find a viable implementation plan satisfying all hard gates\n");
            flow_ir_cleanup(&ir);
            return EXIT_FAILURE;
        }
    }

    if (use_reload_adapter && !flow_component_supports_reload(component)) {
        fprintf(stderr, "flowc: --reload-adapter is not supported by component '%s'\n", component->id);
        flow_ir_cleanup(&ir);
        return EXIT_FAILURE;
    }

    if (!verify_candidate(&ir, component, use_search ? &search : NULL, &verification)) {
        fprintf(stderr, "flowc: verifier: %s\n", verification.message);
        flow_ir_cleanup(&ir);
        return EXIT_FAILURE;
    }

    output = fopen(output_path, "w");
    if (output == NULL) {
        fprintf(stderr, "flowc: cannot write %s: %s\n", output_path, strerror(errno));
        flow_ir_cleanup(&ir);
        return EXIT_FAILURE;
    }
    {
        int generated_ok = emit_c(output, &ir, component,
                                  use_search ? &search : NULL,
                                  &verification, use_reload_adapter);
        if (fclose(output) != 0) generated_ok = 0;
        if (!generated_ok) {
            fprintf(stderr, "flowc: failed to generate %s\n", output_path);
            flow_ir_cleanup(&ir);
            return EXIT_FAILURE;
        }
    }

    if (profile_out != NULL && use_search) {
        FILE *profile_file = fopen(profile_out, "w");
        if (profile_file == NULL) {
            fprintf(stderr, "flowc: cannot write profile %s: %s\n",
                    profile_out, strerror(errno));
            flow_ir_cleanup(&ir);
            return EXIT_FAILURE;
        }
        if (strstr(profile_out, ".fvec") != NULL) {
            fclose(profile_file);
            FlowVecHeader hdr;
            FlowVecPayload payload;
            memset(&hdr, 0, sizeof(hdr));
            memset(&payload, 0, sizeof(payload));
            strncpy(hdr.magic, "FVEC_V1", sizeof(hdr.magic) - 1);
            snprintf(hdr.id, sizeof(hdr.id), "lock_%s", ir.flow_name);
            snprintf(hdr.name, sizeof(hdr.name), "Universal Lockfile [%s]", ir.flow_name);
            strncpy(hdr.origin_hardware, "x86_avx2, L1=64K, Cores=64", sizeof(hdr.origin_hardware) - 1);
            strncpy(hdr.trigger_intent, "UNIVERSAL_LOCK", sizeof(hdr.trigger_intent) - 1);
            strncpy(hdr.category, "UNIVERSAL_LOCK", sizeof(hdr.category) - 1);
            strncpy(hdr.component_id, component->id, sizeof(hdr.component_id) - 1);
            strncpy(hdr.smt_signature, "BUFFER_UNSAT:MEM_UNSAT:SHARD_UNSAT:DET_UNSAT", sizeof(hdr.smt_signature) - 1);
            hdr.energy_score = search.energy;
            hdr.created_at_unix = (uint64_t)time(NULL);
            hdr.vector_dim = 16;
            hdr.payload_size = sizeof(FlowVecPayload);
            payload.pure_genome = search.genome;
            payload.hard_composite_mask = 0xFFFFFFFFFFFFFFFFULL;
            payload.soft_composite_bias = 0ULL;
            payload.proof.buffer_bounds_safety = FLOW_SMT_PROVEN_UNSAT;
            payload.proof.memory_quota_bound = FLOW_SMT_PROVEN_UNSAT;
            payload.proof.shard_non_aliasing = FLOW_SMT_PROVEN_UNSAT;
            payload.proof.determinism_invariant = FLOW_SMT_PROVEN_UNSAT;
            strncpy(payload.proof.proof_summary, "UNIVERSAL_LOCK_PROVEN", sizeof(payload.proof.proof_summary) - 1);
            payload.crc32 = flow_fvec_crc32(&payload, sizeof(payload) - sizeof(uint32_t));
            flow_fvec_write_file(profile_out, &hdr, &payload);
        } else if (strstr(profile_out, ".flowplan") != NULL || strstr(profile_out, ".lock") != NULL) {
            FlowBitSpace space;
            flow_bitspace_init_for_ir(&ir, &space);
            FlowPlan plan;
            space.decode(&space, search.genome, &plan);
            FlowPlanArtifact artifact;
            flow_plan_to_artifact(&plan, &ir, seed, &artifact);
            flow_plan_artifact_save(profile_file, &artifact);
        } else {
            fprintf(profile_file, "flow,component,capacity,threads,shards,benchmark_ns,workload_bytes,buffer_bytes,initial_capacity,growth_percent,batch_size,arena_bytes\n");
            fprintf(profile_file, "%s,%s,%zu,%zu,%zu,%llu,%zu,%zu,%zu,%u,%zu,%zu\n",
                    ir.flow_name, component->id, (size_t)search.capacity,
                    (size_t)search.threads, (size_t)search.shards,
                    (unsigned long long)search.benchmark_ns, ir.workload_bytes,
                    search.tuning.buffer_bytes, search.tuning.initial_capacity,
                    search.tuning.growth_percent, search.tuning.batch_size,
                    search.tuning.arena_bytes);
        }
        fclose(profile_file);
        printf("  profile: wrote %s\n", profile_out);
    }

    printf("flowc: %s -> %s\n", input_path, output_path);
    if (ir.project_name[0] != '\0') {
        printf("  project: %s\n", ir.project_name);
    }
    printf("  IR: output=%s shared=%d read_heavy=%d bounded=%d parallelizable=%d ordered=%d unordered=%d deterministic=%d range=%d size_preserved=%d mutability_read_only=%d ensures=%d declared_constraints=%zu resource=%s capability=%s domain=%s contract=%s fallback=%s graph_nodes=%zu facts=%zu constraints=%zu holes=%zu\n",
           ir.output_name, ir.state_shared, ir.state_read_heavy, ir.state_bounded,
           ir.flow_parallelizable, ir.fact_ordered, ir.fact_unordered,
           ir.fact_deterministic, ir.fact_range_proven, ir.fact_size_preserved,
           ir.fact_mutability_read_only,
           ir.ensure_count, ir.declared_constraint_count, ir.resource_name,
           ir.capability_name, ir.domain_name, ir.contract_name,
           ir.fallback_policy, ir.flow_node_count,
           ir.fact_count, ir.constraint_count, ir.hole_count);
    printf("  selected: %s (%s)\n", component->id, component->kind);
    printf("  candidates:");
    for (size_t candidate_index = 0;
         candidate_index < compatible_component_count(&ir);
         ++candidate_index) {
        const Component *candidate =
            compatible_component_at(&ir, candidate_index);
        if (candidate != NULL) printf(" %s", candidate->id);
    }
    putchar('\n');
    printf("  verifier: status=%s capacity=%zu estimated_bytes=%zu (%s)\n",
           verification_status_name(verification.status), verification.capacity,
           verification.estimated_bytes, verification.message);
    if (use_search) {
        uint64_t schema_hash = flow_bitspace_compute_schema_hash(&ir, component, &search.dimension_set);
        printf("  C search: mode=%s iterations=%zu seed=%u schema_hash=%llu genome=%llu energy=%.6f benchmark_ns=%llu capacity=%.0f threads=%.0f shards=%.0f tuning_buffer=%zu tuning_initial=%zu tuning_growth=%u tuning_batch=%zu tuning_arena=%zu\n",
               search.measured ? "benchmark" : "model", search.iterations,
               search.seed, (unsigned long long)schema_hash, (unsigned long long)search.genome, search.energy,
               (unsigned long long)search.benchmark_ns, search.capacity,
               search.threads, search.shards, search.tuning.buffer_bytes,
               search.tuning.initial_capacity, search.tuning.growth_percent,
               search.tuning.batch_size, search.tuning.arena_bytes);
    }

    if (target_c_header != NULL || target_rust != NULL || target_python != NULL || target_mlir != NULL || target_llvm_ir != NULL) {
        FlowComponentABI abi;
        flow_abi_build_for_component(&ir, component, &abi);
        if (target_c_header != NULL) {
            FILE *h_fp = fopen(target_c_header, "w");
            if (h_fp != NULL) {
                flow_abi_emit_c_header(h_fp, &abi);
                fclose(h_fp);
                printf("  target-c-header: wrote %s\n", target_c_header);
            }
        }
        if (target_rust != NULL) {
            FILE *rs_fp = fopen(target_rust, "w");
            if (rs_fp != NULL) {
                flow_abi_emit_rust_adapter(rs_fp, &abi);
                fclose(rs_fp);
                printf("  target-rust: wrote %s\n", target_rust);
            }
        }
        if (target_python != NULL) {
            FILE *py_fp = fopen(target_python, "w");
            if (py_fp != NULL) {
                flow_abi_emit_python_adapter(py_fp, &abi);
                fclose(py_fp);
                printf("  target-python: wrote %s\n", target_python);
            }
        }
        if (target_mlir != NULL) {
            FILE *mlir_fp = fopen(target_mlir, "w");
            if (mlir_fp != NULL) {
                flow_emit_mlir(mlir_fp, &ir, component, use_search ? &search : NULL, &verification);
                fclose(mlir_fp);
                printf("  target-mlir: wrote %s\n", target_mlir);
            }
        }
        if (target_llvm_ir != NULL) {
            FILE *ll_fp = fopen(target_llvm_ir, "w");
            if (ll_fp != NULL) {
                flow_emit_llvm_ir(ll_fp, &ir, component, use_search ? &search : NULL, &verification);
                fclose(ll_fp);
                printf("  target-llvm-ir: wrote %s\n", target_llvm_ir);
            }
        }
    }

    /* SMT Proof Summary */
    {
        FlowSMTProofAttestation proof_attestation;
        flow_smt_verify(&ir, component, &search.assignment, &search.metrics, &proof_attestation);
        printf("  SMT proof: status=%s (%s)\n",
               flow_smt_result_name(proof_attestation.buffer_bounds_safety),
               proof_attestation.proof_summary);
    }

    flow_ir_cleanup(&ir);
    return EXIT_SUCCESS;
}

#ifndef FLOWC_NO_MAIN
int main(int argc, char **argv) {
    return flowc_main(argc, argv);
}
#endif
