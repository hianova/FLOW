#include "backend.h"
#include "flow.h"
#include "registry.h"
#include "search.h"
#include "bitspace.h"
#include "abi.h"
#include "smt.h"
#include "topology.h"
#include "security.h"
#include "swarm.h"
#include "genetic.h"
#include "orchestrator.h"
#include "flowy.h"
#include "benchmark.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int flowc_main(int argc, char **argv) {
    /* Flowy: 1-Bit Chaos Conversational Assistant (Interactive Mode) */
    if (argc >= 2 && (strcmp(argv[1], "flowy") == 0 || strcmp(argv[1], "--flowy") == 0)) {
        flow_registry_init();
        FlowOrchestrator *orch = flow_orchestrator_create(".");
        char diag[256] = {0};
        flow_orchestrator_absorb(orch, "examples/compiler.flow", diag, sizeof(diag));
        flow_orchestrator_absorb(orch, "examples/project.flow", diag, sizeof(diag));
        int res = flowy_interactive_loop(orch, stdin, stdout);
        flow_orchestrator_destroy(orch);
        return res ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    /* Flowy: Single-Shot Introspective Codebase Query (flowc ask "...") */
    if (argc >= 2 && (strcmp(argv[1], "ask") == 0 || strcmp(argv[1], "--ask") == 0)) {
        if (argc < 3) {
            fprintf(stderr, "usage: flowc ask \"<query about architecture, algorithms, or invariants>\"\n");
            return EXIT_FAILURE;
        }
        FlowTopologyGraph graph;
        flow_topology_build_codebase_graph(&graph);

        FlowyIntrospectiveAnswer ans;
        flowy_query_codebase(&graph, argv[2], &ans);
        flowy_print_answer(&ans, stdout);
        return EXIT_SUCCESS;
    }

    /* Flowy: Explain Real-Time Decision (flowc why) */
    if (argc >= 2 && (strcmp(argv[1], "why") == 0 || strcmp(argv[1], "--why") == 0)) {
        const FlowDecisionEvent *ev = flow_decision_logger_latest(NULL);
        flowy_print_decision_explanation(ev, stdout);
        return EXIT_SUCCESS;
    }

    /* Flowy: Real-Time Decision Timeline (flowc timeline / flowc explain-decisions) */
    if (argc >= 2 && (strcmp(argv[1], "timeline") == 0 || strcmp(argv[1], "--timeline") == 0 ||
                      strcmp(argv[1], "explain-decisions") == 0)) {
        flowy_print_decision_timeline(NULL, stdout);
        return EXIT_SUCCESS;
    }

    /* Quantitative Mechanism Efficiency Audit (flowc audit-mechanisms) */
    if (argc >= 2 && (strcmp(argv[1], "audit-mechanisms") == 0 || strcmp(argv[1], "--audit-mechanisms") == 0)) {
        FlowMechanismAuditReport rep;
        flow_benchmark_run_mechanism_audit(&rep);
        flow_benchmark_print_mechanism_audit(&rep, stdout);
        return EXIT_SUCCESS;
    }

    /* Living Topology Orchestrator Background Continuous Evolution Daemon */
    if (argc >= 2 && (strcmp(argv[1], "daemon") == 0 || strcmp(argv[1], "--daemon") == 0)) {
        size_t interval_ms = 100;
        size_t max_cycles = 3;
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "--interval-ms") == 0 && i + 1 < argc) {
                interval_ms = (size_t)strtoul(argv[++i], NULL, 10);
            } else if (strcmp(argv[i], "--cycles") == 0 && i + 1 < argc) {
                max_cycles = (size_t)strtoul(argv[++i], NULL, 10);
            }
        }
        flow_registry_init();
        FlowOrchestrator *orch = flow_orchestrator_create(".");
        char diag[256] = {0};
        flow_orchestrator_absorb(orch, "examples/compiler.flow", diag, sizeof(diag));
        flow_orchestrator_absorb(orch, "examples/project.flow", diag, sizeof(diag));
        printf("flow-daemon: [started] Living Topology Orchestrator daemon active (interval=%zums, cycles=%zu)\n", interval_ms, max_cycles);
        for (size_t c = 0; c < max_cycles; ++c) {
            double delta = 0.0;
            flow_orchestrator_refactor_entropy(orch, &delta);
            FlowOrchestratorEpoch ep;
            flow_orchestrator_anneal(orch, 50, 42 + (uint32_t)c, &ep);
            printf("flow-daemon: [cycle #%zu] Entropy=%.4f (Delta=%.4f) GlobalEnergy=%.4f ActiveEpoch=#%llu PrimaryComponent=%s\n",
                   c + 1, ep.entropy_score, delta, ep.global_energy, (unsigned long long)ep.epoch_id, ep.primary_component);
        }
        printf("flow-daemon: [quiesced] Background continuous annealing completed.\n");
        flow_orchestrator_destroy(orch);
        return EXIT_SUCCESS;
    }

    /* State / Topology Orchestrator CLI Suite */
    if (argc >= 2 && (strcmp(argv[1], "absorb") == 0 || strcmp(argv[1], "--absorb") == 0)) {
        if (argc < 3) {
            fprintf(stderr, "usage: flowc absorb <file.flow>\n");
            return EXIT_FAILURE;
        }
        flow_registry_init();
        FlowOrchestrator *orch = flow_orchestrator_create(".");
        char diag[256] = {0};
        FlowAbsorbStatus st = flow_orchestrator_absorb(orch, argv[2], diag, sizeof(diag));
        printf("flow-orchestrator: [%s] %s\n", flow_absorb_status_name(st), diag);
        flow_orchestrator_destroy(orch);
        return (st == FLOW_ABSORB_OK || st == FLOW_ABSORB_ALREADY_ABSORBED) ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (argc >= 2 && (strcmp(argv[1], "anneal") == 0 || strcmp(argv[1], "--anneal") == 0)) {
        flow_registry_init();
        FlowOrchestrator *orch = flow_orchestrator_create(".");
        /* If specific files passed, absorb them first */
        for (int i = 2; i < argc; ++i) {
            if (argv[i][0] != '-') {
                char diag[256] = {0};
                flow_orchestrator_absorb(orch, argv[i], diag, sizeof(diag));
            }
        }
        if (flow_orchestrator_intent_count(orch) == 0) {
            /* Try default examples/project.flow if no files */
            char diag[256] = {0};
            flow_orchestrator_absorb(orch, "examples/project.flow", diag, sizeof(diag));
        }
        FlowOrchestratorEpoch epoch;
        if (!flow_orchestrator_anneal(orch, 200, 42, &epoch)) {
            fprintf(stderr, "flowc anneal: failed to solidify global constraints into a sound epoch\n");
            flow_orchestrator_destroy(orch);
            return EXIT_FAILURE;
        }
        printf("flow-orchestrator: [epoch_solidified] Epoch=#%llu GlobalEnergy=%.4f Entropy=%.4f PrimaryComponent=%s\n",
               (unsigned long long)epoch.epoch_id, epoch.global_energy, epoch.entropy_score, epoch.primary_component);
        flow_orchestrator_landscape(orch, stdout);
        flow_orchestrator_destroy(orch);
        return EXIT_SUCCESS;
    }

    if (argc >= 2 && (strcmp(argv[1], "landscape") == 0 || strcmp(argv[1], "--landscape") == 0)) {
        flow_registry_init();
        FlowOrchestrator *orch = flow_orchestrator_create(".");
        for (int i = 2; i < argc; ++i) {
            if (argv[i][0] != '-') {
                char diag[256] = {0};
                flow_orchestrator_absorb(orch, argv[i], diag, sizeof(diag));
            }
        }
        if (flow_orchestrator_intent_count(orch) == 0) {
            char diag[256] = {0};
            flow_orchestrator_absorb(orch, "examples/project.flow", diag, sizeof(diag));
        }
        FlowOrchestratorEpoch epoch;
        flow_orchestrator_anneal(orch, 100, 42, &epoch);
        flow_orchestrator_landscape(orch, stdout);
        flow_orchestrator_destroy(orch);
        return EXIT_SUCCESS;
    }

    if (argc >= 2 && (strcmp(argv[1], "refactor") == 0 || strcmp(argv[1], "--refactor") == 0)) {
        flow_registry_init();
        FlowOrchestrator *orch = flow_orchestrator_create(".");
        char diag[256] = {0};
        flow_orchestrator_absorb(orch, "examples/project.flow", diag, sizeof(diag));
        FlowOrchestratorEpoch epoch;
        flow_orchestrator_anneal(orch, 100, 42, &epoch);
        double delta = 0.0;
        flow_orchestrator_refactor_entropy(orch, &delta);
        printf("flow-orchestrator: [entropy_reduction] Codebase Entropy Delta=%.4f (Refactored Epoch Solidified)\n", delta);
        flow_orchestrator_destroy(orch);
        return EXIT_SUCCESS;
    }

    if (argc >= 2 && (strcmp(argv[1], "morph") == 0 || strcmp(argv[1], "--morph") == 0)) {
        const char *tactic_str = argc >= 3 ? argv[2] : "speed";
        FlowPlanTactic tactic = FLOW_TACTIC_SPEED;
        if (strcmp(tactic_str, "memory") == 0) tactic = FLOW_TACTIC_MEMORY;
        else if (strcmp(tactic_str, "balanced") == 0) tactic = FLOW_TACTIC_BALANCED;

        flow_registry_init();
        FlowOrchestrator *orch = flow_orchestrator_create(".");
        char diag[256] = {0};
        flow_orchestrator_absorb(orch, "examples/project.flow", diag, sizeof(diag));
        FlowOrchestratorEpoch epoch;
        flow_orchestrator_anneal(orch, 100, 42, &epoch);

        FlowPlan target_plan;
        if (flow_orchestrator_time_travel(orch, tactic, &target_plan)) {
            printf("flow-orchestrator: [state_time_travel] Morphed to Tactic '%s' (Component=%s, LatencyScore=%.1f, MemBytes=%zu)\n",
                   flow_plan_tactic_name(tactic),
                   target_plan.component ? target_plan.component->id : "unknown",
                   target_plan.eval.latency_score,
                   target_plan.eval.memory_bytes);
        }
        flow_orchestrator_destroy(orch);
        return EXIT_SUCCESS;
    }

    const char *input_path;
    const char *output_path;
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
    const char *ensemble_prefix = NULL;
    const char *smt_proof_path = NULL;
    const char *topology_out = NULL;
    int run_topology_audit = 0;
    size_t workload_bytes = 0;
    ProfileSeed profile = {0};
    int show_heatmap = 0;
    int show_masks = 0;
    int use_mtd = 0;
    int use_swarm = 0;
    size_t swarm_particles = 8;
    int use_synth_kernel = 0;
    int use_explain_seed = 0;
    uint32_t explain_seed = 0;

    if (!flow_registry_init()) {
        fprintf(stderr, "flowc: failed to initialize plugin registry\n");
        return EXIT_FAILURE;
    }

    if (argc < 3) {
        fprintf(stderr, "usage: flowc <input.flow> -o <output.c> [--search] [--iterations <N>] [--seed <N>] [--benchmark] [--heatmap] [--show-masks] [--mtd] [--swarm [N]] [--synth-kernel] [--explain-seed <N>] [--reload-adapter] [--profile <file>] [--profile-out <file>] [--component <id>] [--workload-bytes <N>] [--target-c-header <file.h>] [--target-rust <file.rs>] [--target-python <file.py>]\n");
        return EXIT_FAILURE;
    }
    input_path = argv[1];
    output_path = NULL;
    for (int arg = 2; arg < argc; ++arg) {
        if (strcmp(argv[arg], "-o") == 0 && arg + 1 < argc) {
            output_path = argv[++arg];
        } else if (strcmp(argv[arg], "--search") == 0) {
            use_search = 1;
        } else if (strcmp(argv[arg], "--heatmap") == 0) {
            show_heatmap = 1;
        } else if (strcmp(argv[arg], "--show-masks") == 0 || strcmp(argv[arg], "--mask-canvas") == 0) {
            show_masks = 1;
        } else if (strcmp(argv[arg], "--mtd") == 0) {
            use_mtd = 1;
        } else if (strcmp(argv[arg], "--swarm") == 0) {
            use_swarm = 1;
            use_search = 1;
            if (arg + 1 < argc && argv[arg + 1][0] >= '0' && argv[arg + 1][0] <= '9') {
                swarm_particles = (size_t)strtoul(argv[++arg], NULL, 10);
            }
        } else if (strcmp(argv[arg], "--synth-kernel") == 0 || strcmp(argv[arg], "--genetic") == 0) {
            use_synth_kernel = 1;
        } else if (strcmp(argv[arg], "--explain-seed") == 0 && arg + 1 < argc) {
            explain_seed = (uint32_t)strtoul(argv[++arg], NULL, 10);
            use_explain_seed = 1;
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
        } else if (strcmp(argv[arg], "--ensemble") == 0 && arg + 1 < argc) {
            ensemble_prefix = argv[++arg];
            use_search = 1;
        } else if (strcmp(argv[arg], "--smt-proof") == 0 && arg + 1 < argc) {
            smt_proof_path = argv[++arg];
        } else if (strcmp(argv[arg], "--topology") == 0 && arg + 1 < argc) {
            topology_out = argv[++arg];
        } else if (strcmp(argv[arg], "--topology-audit") == 0) {
            run_topology_audit = 1;
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
    if (ir.imported_module_count > 0) {
        for (size_t m = 0; m < ir.imported_module_count; ++m) {
            const char *mod_name = ir.imported_modules[m];
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
    if (use_search) {
        if (use_swarm) {
            FlowBitSpace space;
            if (flow_bitspace_init_for_ir(&ir, &space)) {
                FlowBitSearchResult swarm_res;
                size_t swarm_cycles = (iterations / (swarm_particles * 10)) > 0 ? (iterations / (swarm_particles * 10)) : 6;
                if (flow_swarm_search(&space, swarm_particles, swarm_cycles, seed, use_benchmark, &swarm_res)) {
                    flow_plan_to_search_result(&swarm_res.best_plan, &ir, seed, &search);
                    search.mask_canvas = swarm_res.mask_canvas;
                    search.iterations = swarm_res.iterations;
                    search.seed = seed;
                } else {
                    search = search_best(&ir, iterations, seed, use_benchmark,
                                         profile.available ? &profile : NULL);
                }
            } else {
                search = search_best(&ir, iterations, seed, use_benchmark,
                                     profile.available ? &profile : NULL);
            }
        } else {
            search = search_best(&ir, iterations, seed, use_benchmark,
                                 profile.available ? &profile : NULL);
        }
        component = search.component;
        if (use_explain_seed) {
            FlowBitSpace space;
            if (flow_bitspace_init_for_ir(&ir, &space)) {
                flow_bitspace_explain_seed(&space, iterations, explain_seed, use_benchmark, NULL, stdout);
            }
        }
        if (show_heatmap) {
            flow_search_heatmap_report(&search.heatmap, stdout);
        }
        if (show_masks) {
            flow_mask_canvas_report(&search.mask_canvas, stdout);
        }
        if (use_mtd) {
            size_t sample_sizes[6] = {8, 4, 8, 1, 4, 16};
            size_t sample_aligns[6] = {8, 4, 8, 1, 4, 8};
            FlowMTDLayout mtd_layout;
            if (flow_security_mtd_generate_layout(seed ? (uint64_t)seed : UINT64_C(0x12345678), 6, sample_sizes, sample_aligns, 32, &mtd_layout)) {
                flow_security_mtd_report(&mtd_layout, stdout);
            }
        }
        if (use_synth_kernel) {
            FlowGeneticEngine genetic_engine;
            flow_genetic_init(&genetic_engine, &ir);
            FlowKernelGenome synth_kernel;
            flow_genetic_evolve(&genetic_engine, iterations > 0 ? iterations : 200, seed, &synth_kernel);
            flow_genetic_report(&genetic_engine, stdout);
        }
        if (component == NULL) {
            fprintf(stderr, "flowc: search could not find a viable implementation plan satisfying all hard gates\n");
            flow_search_heatmap_report(&search.heatmap, stderr);
            fprintf(stderr, "Hint: Run with --explain-seed %u for step-by-step constraint rejection diagnostics.\n", seed);
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
        if (strstr(profile_out, ".flowplan") != NULL || strstr(profile_out, ".lock") != NULL) {
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
    if (target_c_header != NULL || target_rust != NULL || target_python != NULL) {
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

    /* SMT Formal Verification & Script Emission */
    {
        FlowSMTProofAttestation proof_attestation;
        flow_smt_verify(&ir, component, &search.assignment, &search.metrics, &proof_attestation);
        if (smt_proof_path != NULL) {
            FILE *smt_fp = fopen(smt_proof_path, "w");
            if (smt_fp != NULL) {
                flow_smt_generate_proof_script(&ir, component, &search.assignment, &search.metrics, smt_fp);
                fclose(smt_fp);
                printf("  smt-proof: wrote %s\n", smt_proof_path);
            }
        }
        printf("  SMT proof: status=%s (%s)\n",
               flow_smt_result_name(proof_attestation.buffer_bounds_safety),
               proof_attestation.proof_summary);
    }

    /* Plan Ensemble / Tactical Bundle Generation */
    if (ensemble_prefix != NULL && use_search) {
        FlowBitSpace space;
        if (flow_bitspace_init_for_ir(&ir, &space)) {
            FlowBitSearchResult bit_res;
            if (flow_bitspace_search(&space, iterations, seed, use_benchmark, NULL, &bit_res)) {
                FlowPlanEnsemble ensemble;
                flow_bitspace_extract_ensemble(&bit_res, &ensemble);

                for (int t = 0; t < FLOW_TACTIC_COUNT; ++t) {
                    char tac_c_path[256];
                    snprintf(tac_c_path, sizeof(tac_c_path), "%s_%s.c",
                             ensemble_prefix, flow_plan_tactic_name((FlowPlanTactic)t));
                    FILE *tac_out = fopen(tac_c_path, "w");
                    if (tac_out != NULL) {
                        SearchResult tac_search;
                        VerificationReport tac_v_rep;
                        const Component *tac_comp =
                            ensemble.tactics[t].component != NULL
                                ? ensemble.tactics[t].component
                                : component;
                        flow_plan_to_search_result(&ensemble.tactics[t], &ir, seed, &tac_search);
                        verify_candidate(&ir, tac_comp, &tac_search, &tac_v_rep);
                        emit_c(tac_out, &ir, tac_comp, &tac_search, &tac_v_rep, use_reload_adapter);
                        fclose(tac_out);
                        printf("  ensemble: wrote %s (tactic=%s)\n",
                               tac_c_path, flow_plan_tactic_name((FlowPlanTactic)t));
                    }
                }

                /* Write ensemble dispatch header */
                char ens_h_path[256];
                snprintf(ens_h_path, sizeof(ens_h_path), "%s_ensemble.h", ensemble_prefix);
                FILE *ens_h = fopen(ens_h_path, "w");
                if (ens_h != NULL) {
                    fprintf(ens_h, "/* FLOW Plan Ensemble Bundle Dispatcher */\n");
                    fprintf(ens_h, "#ifndef FLOW_ENSEMBLE_BUNDLE_H\n#define FLOW_ENSEMBLE_BUNDLE_H\n\n");
                    fprintf(ens_h, "#include <stddef.h>\n#include <stdint.h>\n\n");
                    fprintf(ens_h, "typedef enum {\n");
                    fprintf(ens_h, "    FLOW_TACTIC_SPEED = 0,\n");
                    fprintf(ens_h, "    FLOW_TACTIC_BALANCED = 1,\n");
                    fprintf(ens_h, "    FLOW_TACTIC_MEMORY = 2,\n");
                    fprintf(ens_h, "    FLOW_TACTIC_COUNT = 3\n");
                    fprintf(ens_h, "} FlowPlanTactic;\n\n");
                    fprintf(ens_h, "typedef struct {\n");
                    fprintf(ens_h, "    const char *tactic_name;\n");
                    fprintf(ens_h, "    const char *component_id;\n");
                    fprintf(ens_h, "    size_t capacity;\n");
                    fprintf(ens_h, "    size_t threads;\n");
                    fprintf(ens_h, "    size_t memory_bytes;\n");
                    fprintf(ens_h, "    double latency_score;\n");
                    fprintf(ens_h, "} FlowPlanTacticInfo;\n\n");
                    fprintf(ens_h, "static const FlowPlanTacticInfo FLOW_PLAN_TACTICS[FLOW_TACTIC_COUNT] = {\n");
                    for (int t = 0; t < FLOW_TACTIC_COUNT; ++t) {
                        const FlowPlan *p = &ensemble.tactics[t];
                        size_t th = (size_t)flow_plan_get_value(&p->dimension_set, &p->assignment, "threads", 1);
                        fprintf(ens_h, "    {\"%s\", \"%s\", %zu, %zu, %zu, %.2f},\n",
                                flow_plan_tactic_name((FlowPlanTactic)t),
                                p->component ? p->component->id : "default",
                                p->eval.capacity, th, p->eval.memory_bytes,
                                p->eval.latency_score);
                    }
                    fprintf(ens_h, "};\n\n");
                    fprintf(ens_h, "static inline const FlowPlanTacticInfo *flow_ensemble_get_tactic(FlowPlanTactic tactic) {\n");
                    fprintf(ens_h, "    if ((size_t)tactic >= FLOW_TACTIC_COUNT) return &FLOW_PLAN_TACTICS[FLOW_TACTIC_BALANCED];\n");
                    fprintf(ens_h, "    return &FLOW_PLAN_TACTICS[tactic];\n");
                    fprintf(ens_h, "}\n\n");
                    fprintf(ens_h, "#endif\n");
                    fclose(ens_h);
                    printf("  ensemble: wrote %s\n", ens_h_path);
                }

                /* Write ensemble bundle lock */
                char ens_lock_path[256];
                snprintf(ens_lock_path, sizeof(ens_lock_path), "%s_bundle.lock", ensemble_prefix);
                FILE *ens_lock = fopen(ens_lock_path, "w");
                if (ens_lock != NULL) {
                    for (int t = 0; t < FLOW_TACTIC_COUNT; ++t) {
                        FlowPlanArtifact art;
                        flow_plan_to_artifact(&ensemble.tactics[t], &ir, seed, &art);
                        fprintf(ens_lock, "# Tactic: %s\n", flow_plan_tactic_name((FlowPlanTactic)t));
                        flow_plan_artifact_save(ens_lock, &art);
                        fprintf(ens_lock, "\n");
                    }
                    fclose(ens_lock);
                    printf("  ensemble: wrote %s\n", ens_lock_path);
                }
            }
        }
    }

    /* Codebase & Intent Topology Analysis */
    {
        FlowTopologyGraph topology_graph;
        flow_topology_build_intent_graph(&topology_graph, &ir, component, NULL);

        if (topology_out != NULL) {
            FILE *top_fp = fopen(topology_out, "w");
            if (top_fp != NULL) {
                if (strstr(topology_out, ".dot") != NULL) {
                    flow_topology_export_dot(&topology_graph, top_fp);
                } else {
                    flow_topology_export_json(&topology_graph, top_fp);
                }
                fclose(top_fp);
                printf("  topology: wrote %s\n", topology_out);
            }
        }

        if (run_topology_audit) {
            FlowTopologyGraph codebase_graph;
            flow_topology_build_codebase_graph(&codebase_graph);
            FlowTopologyAuditReport audit_report;
            flow_topology_audit(&codebase_graph, &audit_report);
            printf("  topology audit: nodes=%zu core=%zu plugins=%zu modularity=%.2f leaks=%zu %s\n",
                   audit_report.total_nodes, audit_report.core_nodes, audit_report.plugin_nodes,
                   audit_report.modularity_score, audit_report.cross_layer_leaks,
                   audit_report.cross_layer_leaks == 0 ? "(Clean Interface Firewall)" : audit_report.leak_details);
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
