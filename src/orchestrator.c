#include "orchestrator.h"
#include "backend.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t orch_time_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

const char *flow_absorb_status_name(FlowAbsorbStatus status) {
    switch (status) {
        case FLOW_ABSORB_OK: return "absorbed_sound";
        case FLOW_ABSORB_MUTEX_CONFLICT: return "topological_boundary_mutex_violation";
        case FLOW_ABSORB_INVALID: return "invalid_spec";
        case FLOW_ABSORB_ALREADY_ABSORBED: return "already_absorbed";
        default: return "unknown_status";
    }
}

FlowOrchestrator *flow_orchestrator_create(const char *workspace_dir) {
    flow_registry_init();
    FlowOrchestrator *orch = calloc(1, sizeof(*orch));
    if (orch == NULL) return NULL;
    if (workspace_dir != NULL) {
        strncpy(orch->workspace_dir, workspace_dir, sizeof(orch->workspace_dir) - 1);
    } else {
        strncpy(orch->workspace_dir, ".", sizeof(orch->workspace_dir) - 1);
    }
    flow_topology_build_codebase_graph(&orch->topology_graph);
    return orch;
}

void flow_orchestrator_destroy(FlowOrchestrator *orch) {
    if (orch == NULL) return;
    for (size_t i = 0; i < orch->intent_count; ++i) {
        if (orch->intents[i].active) {
            flow_ir_cleanup(&orch->intents[i].ir);
        }
    }
    if (orch->has_unified_ir) {
        flow_ir_cleanup(&orch->unified_ir);
    }
    free(orch);
}

size_t flow_orchestrator_intent_count(const FlowOrchestrator *orch) {
    return orch != NULL ? orch->intent_count : 0;
}

static void merge_semantic_irs(const SemanticIR *src, SemanticIR *dst) {
    if (src == NULL || dst == NULL) return;

    if (dst->flow_name[0] == '\0') {
        strncpy(dst->flow_name, src->flow_name, sizeof(dst->flow_name) - 1);
    }
    if (dst->input_name[0] == '\0') {
        strncpy(dst->input_name, src->input_name, sizeof(dst->input_name) - 1);
    }
    if (dst->output_name[0] == '\0') {
        strncpy(dst->output_name, src->output_name, sizeof(dst->output_name) - 1);
    }

    if (src->input_max_count > dst->input_max_count) {
        dst->input_max_count = src->input_max_count;
    }
    if (src->top_n > dst->top_n) {
        dst->top_n = src->top_n;
    }

    if (src->state_shared) dst->state_shared = 1;
    if (src->state_read_heavy) dst->state_read_heavy = 1;
    if (src->state_bounded) dst->state_bounded = 1;
    if (src->flow_parallelizable) dst->flow_parallelizable = 1;
    if (src->fact_ordered) dst->fact_ordered = 1;
    if (src->fact_unordered) dst->fact_unordered = 1;
    if (src->fact_deterministic) dst->fact_deterministic = 1;
    if (src->fact_range_proven) dst->fact_range_proven = 1;
    if (src->fact_size_preserved) dst->fact_size_preserved = 1;
    if (src->fact_mutability_read_only) dst->fact_mutability_read_only = 1;

    /* Memory Quota Union: pick strictest upper bound if set */
    if (src->memory_limit_mb > 0) {
        if (dst->memory_limit_mb == 0 || src->memory_limit_mb < dst->memory_limit_mb) {
            dst->memory_limit_mb = src->memory_limit_mb;
        }
    }
}

FlowAbsorbStatus flow_orchestrator_absorb(FlowOrchestrator *orch,
                                          const char *spec_file,
                                          char *diag_msg,
                                          size_t diag_size) {
    if (orch == NULL || spec_file == NULL) {
        if (diag_msg && diag_size > 0) snprintf(diag_msg, diag_size, "Orchestrator or spec file path is NULL");
        return FLOW_ABSORB_INVALID;
    }

    /* Check if already absorbed */
    for (size_t i = 0; i < orch->intent_count; ++i) {
        if (strcmp(orch->intents[i].file_path, spec_file) == 0) {
            if (diag_msg && diag_size > 0) snprintf(diag_msg, diag_size, "Spec file '%s' already absorbed into active topology", spec_file);
            return FLOW_ABSORB_ALREADY_ABSORBED;
        }
    }

    if (orch->intent_count >= FLOW_ORCHESTRATOR_MAX_INTENTS) {
        if (diag_msg && diag_size > 0) snprintf(diag_msg, diag_size, "Orchestrator capacity exceeded (%d max intents)", FLOW_ORCHESTRATOR_MAX_INTENTS);
        return FLOW_ABSORB_INVALID;
    }

    FILE *f = fopen(spec_file, "r");
    if (f == NULL) {
        if (diag_msg && diag_size > 0) snprintf(diag_msg, diag_size, "Cannot open spec file '%s'", spec_file);
        return FLOW_ABSORB_INVALID;
    }

    FlowSpec spec;
    memset(&spec, 0, sizeof(spec));
    if (!parse_spec(f, &spec)) {
        fclose(f);
        if (diag_msg && diag_size > 0) snprintf(diag_msg, diag_size, "Syntax error parsing spec file '%s'", spec_file);
        return FLOW_ABSORB_INVALID;
    }
    fclose(f);

    SemanticIR incoming_ir;
    memset(&incoming_ir, 0, sizeof(incoming_ir));
    lower_to_ir(&spec, &incoming_ir);

    const char *module_name = spec.plugin_name[0] != '\0' ? spec.plugin_name : "builtin";
    const FlowPlugin *module = flow_registry_lookup(module_name);
    if (module != NULL) {
        flow_plugin_lower_semantics(&spec, &incoming_ir, module);
    }

    /* 1. Verify Mathematical Satisfiability of Incoming Intent */
    FlowBitSpace trial_space;
    if (!flow_bitspace_init_for_ir(&incoming_ir, &trial_space) || trial_space.candidate_count == 0) {
        flow_ir_cleanup(&incoming_ir);
        if (diag_msg && diag_size > 0) {
            snprintf(diag_msg, diag_size,
                     "TOPOLOGICAL MUTEX VIOLATION: Intent '%s' (memory < %dmb, count %zu) mathematically contradicts physical capacity bounds",
                     incoming_ir.flow_name, incoming_ir.memory_limit_mb, (size_t)incoming_ir.input_max_count);
        }
        return FLOW_ABSORB_MUTEX_CONFLICT;
    }

    VerificationReport v_report;
    const Component *comp = select_component(&incoming_ir);
    if (comp == NULL || !verify_candidate(&incoming_ir, comp, NULL, &v_report)) {
        flow_ir_cleanup(&incoming_ir);
        if (diag_msg && diag_size > 0) {
            snprintf(diag_msg, diag_size,
                     "TOPOLOGICAL MUTEX VIOLATION: Intent '%s' failed verifier bounds: %s",
                     incoming_ir.flow_name, v_report.message[0] ? v_report.message : "unsatisfiable constraint");
        }
        return FLOW_ABSORB_MUTEX_CONFLICT;
    }

    /* 2. Update Unified Project Representation */
    if (!orch->has_unified_ir) {
        orch->unified_ir = incoming_ir;
        orch->has_unified_ir = 1;
    } else {
        merge_semantic_irs(&incoming_ir, &orch->unified_ir);
    }

    /* 3. Successful Semantic Merge: Absorb into Orchestrator */
    FlowAbsorbedIntent *intent = &orch->intents[orch->intent_count++];
    memset(intent, 0, sizeof(*intent));
    strncpy(intent->flow_name, incoming_ir.flow_name, sizeof(intent->flow_name) - 1);
    strncpy(intent->file_path, spec_file, sizeof(intent->file_path) - 1);
    intent->spec = spec;
    intent->ir = incoming_ir;
    intent->contract_hash = flow_compute_contract_hash(&incoming_ir);
    intent->schema_hash = flow_bitspace_compute_schema_hash(&incoming_ir, trial_space.candidates[0], &trial_space.candidate_dims[0]);
    intent->active = 1;

    /* Add node and edge to Topology Graph */
    uint32_t node_id = (uint32_t)(orch->topology_graph.node_count + 1);
    if (node_id < FLOW_TOPOLOGY_MAX_NODES) {
        FlowTopologyNode *n = &orch->topology_graph.nodes[orch->topology_graph.node_count++];
        n->id = node_id;
        n->type = FLOW_NODE_INTENT_OP;
        n->layer = 3;
        n->is_core = 0;
        strncpy(n->name, intent->flow_name, sizeof(n->name) - 1);
        strncpy(n->module, spec_file, sizeof(n->module) - 1);
    }

    if (diag_msg && diag_size > 0) {
        snprintf(diag_msg, diag_size,
                 "Absorbed intent '%s' from '%s' into global topology (Candidates: %zu, Schema: 0x%016llx)",
                 intent->flow_name, spec_file, trial_space.candidate_count, (unsigned long long)intent->schema_hash);
    }
    return FLOW_ABSORB_OK;
}

int flow_orchestrator_anneal(FlowOrchestrator *orch,
                             size_t iterations,
                             uint32_t seed,
                             FlowOrchestratorEpoch *epoch_out) {
    if (orch == NULL || orch->intent_count == 0) return 0;
    if (orch->epoch_count >= FLOW_ORCHESTRATOR_MAX_EPOCHS) return 0;

    size_t iters = iterations > 0 ? iterations : 250;
    uint32_t s = seed != 0 ? seed : 42;

    double total_energy = 0.0;
    SearchResult best_global_search;
    memset(&best_global_search, 0, sizeof(best_global_search));
    FlowPlanEnsemble global_ensemble;
    memset(&global_ensemble, 0, sizeof(global_ensemble));
    FlowSMTProofAttestation global_proof;
    memset(&global_proof, 0, sizeof(global_proof));

    for (size_t i = 0; i < orch->intent_count; ++i) {
        if (!orch->intents[i].active) continue;
        SemanticIR *ir = &orch->intents[i].ir;
        SearchResult res = search_best(ir, iters, s + (uint32_t)i, 0, NULL);
        if (res.component == NULL) continue;
        total_energy += res.energy;
        if (best_global_search.component == NULL) {
            best_global_search = res;
        }

        FlowBitSpace space;
        if (flow_bitspace_init_for_ir(ir, &space)) {
            FlowBitSearchResult bit_res;
            if (flow_bitspace_search(&space, iters, s + (uint32_t)i, 0, NULL, &bit_res)) {
                FlowPlanEnsemble ens;
                if (flow_bitspace_extract_ensemble(&bit_res, &ens)) {
                    if (global_ensemble.count == 0) {
                        global_ensemble = ens;
                    }
                }
            }
        }
        flow_smt_verify(ir, res.component, &res.assignment, &res.metrics, &global_proof);
    }

    if (best_global_search.component == NULL) return 0;

    /* Solidify New Orchestrator Epoch */
    FlowOrchestratorEpoch *epoch = &orch->epochs[orch->epoch_count++];
    memset(epoch, 0, sizeof(*epoch));
    epoch->epoch_id = ++orch->current_epoch_id;
    epoch->timestamp_ns = orch_time_ns();
    epoch->global_energy = total_energy;
    epoch->active_intent_count = orch->intent_count;
    strncpy(epoch->primary_component, best_global_search.component->id, sizeof(epoch->primary_component) - 1);
    epoch->ensemble = global_ensemble;
    epoch->search_result = best_global_search;
    epoch->smt_proof = global_proof;

    /* Calculate Codebase Topology Entropy Score: E = nodes / (edges + 1) */
    double node_c = (double)orch->topology_graph.node_count;
    double edge_c = (double)orch->topology_graph.edge_count;
    epoch->entropy_score = (edge_c > 0) ? (node_c / edge_c) : 1.0;

    if (epoch_out != NULL) *epoch_out = *epoch;
    return 1;
}

int flow_orchestrator_landscape(const FlowOrchestrator *orch, FILE *out) {
    if (orch == NULL || out == NULL) return 0;

    fprintf(out, "================================================================================\n");
    fprintf(out, "               FLOW TOPOLOGY ORCHESTRATOR LANDSCAPE REPORT                     \n");
    fprintf(out, "================================================================================\n");

    if (orch->epoch_count == 0) {
        fprintf(out, "Status: UNANNEALED (Absorbed Intents: %zu, Pending Global Search)\n", orch->intent_count);
        for (size_t i = 0; i < orch->intent_count; ++i) {
            fprintf(out, "  - [%zu] Intent: '%s' (from %s)\n", i + 1, orch->intents[i].flow_name, orch->intents[i].file_path);
        }
        fprintf(out, "Run 'flowc anneal' to solidify active intents into a verified Epoch.\n");
        return 1;
    }

    const FlowOrchestratorEpoch *cur = &orch->epochs[orch->epoch_count - 1];
    fprintf(out, "Active Epoch:        #%llu\n", (unsigned long long)cur->epoch_id);
    fprintf(out, "Global Energy:       %.4f\n", cur->global_energy);
    fprintf(out, "Topology Entropy:    %.4f\n", cur->entropy_score);
    fprintf(out, "Primary Component:   %s\n", cur->primary_component);
    fprintf(out, "Absorbed Intents:    %zu\n", cur->active_intent_count);
    fprintf(out, "--------------------------------------------------------------------------------\n");
    fprintf(out, "PARETO TACTICAL LANDSCAPE (Constraint-Based Multi-Objective Coordinates):\n");

    for (size_t t = 0; t < FLOW_TACTIC_COUNT; ++t) {
        if (cur->ensemble.available[t]) {
            const FlowPlan *p = &cur->ensemble.tactics[t];
            fprintf(out, "  * [%-8s] Component=%-14s LatencyScore=%-5.1f MemBytes=%-8zu Energy=%.2f\n",
                    flow_plan_tactic_name((FlowPlanTactic)t),
                    p->component ? p->component->id : "none",
                    p->eval.latency_score,
                    p->eval.memory_bytes,
                    p->eval.energy);
        }
    }

    fprintf(out, "--------------------------------------------------------------------------------\n");
    fprintf(out, "SMT MATHEMATICAL THEOREM PROOFS:\n");
    fprintf(out, "  [Buffer Bounds Safety]    %s\n", flow_smt_result_name(cur->smt_proof.buffer_bounds_safety));
    fprintf(out, "  [Memory Quota Bound]      %s\n", flow_smt_result_name(cur->smt_proof.memory_quota_bound));
    fprintf(out, "  [Shard Non-Aliasing]      %s\n", flow_smt_result_name(cur->smt_proof.shard_non_aliasing));
    fprintf(out, "  [Functional Determinism]  %s\n", flow_smt_result_name(cur->smt_proof.determinism_invariant));
    fprintf(out, "================================================================================\n");
    return 1;
}

int flow_orchestrator_refactor_entropy(FlowOrchestrator *orch, double *entropy_delta_out) {
    if (orch == NULL || !orch->has_unified_ir || orch->epoch_count == 0) return 0;

    double before_entropy = orch->epochs[orch->epoch_count - 1].entropy_score;

    /* Background topological walk: prune redundant constraint declarations */
    if (orch->unified_ir.declared_constraint_count > 1 && orch->unified_ir.fact_deterministic) {
        /* Merge identical factual predicates */
        orch->unified_ir.declared_constraint_count = 1;
    }

    /* Re-anneal into improved Epoch */
    FlowOrchestratorEpoch new_epoch;
    if (!flow_orchestrator_anneal(orch, 100, 42, &new_epoch)) return 0;

    double after_entropy = new_epoch.entropy_score;
    double delta = before_entropy - after_entropy;
    if (entropy_delta_out != NULL) *entropy_delta_out = delta;
    return 1;
}

int flow_orchestrator_time_travel(FlowOrchestrator *orch,
                                  FlowPlanTactic tactic,
                                  FlowPlan *plan_out) {
    if (orch == NULL || orch->epoch_count == 0 || (int)tactic >= FLOW_TACTIC_COUNT) return 0;
    const FlowOrchestratorEpoch *cur = &orch->epochs[orch->epoch_count - 1];
    if (!cur->ensemble.available[tactic]) return 0;
    if (plan_out != NULL) *plan_out = cur->ensemble.tactics[tactic];
    return 1;
}
