#include "flowy.h"
#include "topology.h"
#include "token_ring.h"
#include "cubical_hott.h"
#include <stdio.h>
#include <string.h>

void flowy_explain_decision(const FlowDecisionEvent *ev, char *buf_out, size_t max_len) {
    if (!ev || !buf_out || !max_len) return;
    FlowCubicalTransitionReport rep = {0};
    flow_cubical_decode_transition(ev->pre_state_mask, ev->post_state_mask, ev->flipped_genome_bit, &rep);
    snprintf(buf_out, max_len,
             "[FLOW Token-Ring & Lean4 Decision Audit]\nTrigger: %s (t=%.2fms) | Metric: %.1f/%.1f %s | Invariant: %s\n"
             "Cubical 1-Cell: Bit#%u [%s] | Kan Homotopy: %s | Morph: %s -> %s (%lluns QSBR)\nRationale: %s\n",
             ev->trigger_source, (double)ev->timestamp_ns / 1e6, ev->observed_metric_value, ev->threshold_limit_value, ev->metric_unit, ev->violated_constraint,
             rep.flipped_axis, (rep.axis_info && rep.axis_info->axis_name) ? rep.axis_info->axis_name : "axis",
             rep.is_kan_homotopic ? "KAN_FILLED (p ~ q)" : "OBSTRUCTED", ev->pre_topology, ev->post_topology, (unsigned long long)ev->hot_swap_grace_ns,
             ev->causal_rationale[0] ? ev->causal_rationale : rep.explanation);
}

int flowy_explain_bottleneck(const FlowTopologyGraph *graph, char *buf_out, size_t max_len) {
    if (!buf_out || !max_len) return 0;
    FlowTopologyGraph local;
    const FlowTopologyGraph *g = (graph && graph->node_count) ? graph : (flow_topology_build_codebase_graph(&local), &local);
    const FlowTopologyNode *peak = NULL;
    for (size_t i = 0; i < g->node_count; ++i)
        if (!peak || (g->nodes[i].loc * (g->nodes[i].fan_in + 1)) > (peak->loc * (peak->fan_in + 1))) peak = &g->nodes[i];
    if (!peak) return 0;
    SemanticIR ir = { .input_max_count = 4096, .state_bounded = 1, .state_shared = 1, .state_read_heavy = 1, .memory_limit_mb = 64, .fact_deterministic = 1, .fact_lean_certified = 1 };
    strncpy(ir.flow_name, peak->name, sizeof(ir.flow_name) - 1);
    FlowTokenRing ring;
    flow_token_ring_setup_canonical(&ring, &ir, 64, 42);
    flow_token_ring_run_to_attractor(&ring, 8);
    snprintf(buf_out, max_len,
             "[FLOW Live Topology & Token-Ring Bottleneck Analysis]\nPeak Node: %s (Layer %u, LOC=%u, FanIn=%u, FanOut=%u, Cubical=0x%016llx)\n"
             "Token Ring Attractor: state=%d, genome=0x%016llx, E=%.2f, DeltaE=%.6f | SMT: %s\n",
             peak->name, peak->layer, peak->loc, peak->fan_in, peak->fan_out, (unsigned long long)peak->capability_mask,
             (int)ring.state, (unsigned long long)ring.active_genome, ring.lyapunov_energy, ring.lyapunov_delta_e, ring.smt_proof.proof_summary);
    return 1;
}

int flowy_query_codebase(const FlowTopologyGraph *graph, const char *query_text, FlowyIntrospectiveAnswer *ans) {
    if (!query_text || !ans) return 0;
    memset(ans, 0, sizeof(*ans));
    strncpy(ans->query, query_text, sizeof(ans->query) - 1);
    FlowTopologyGraph local;
    const FlowTopologyGraph *g = (graph && graph->node_count) ? graph : (flow_topology_build_codebase_graph(&local), &local);
    if (!g->node_count) return 0;
    if (strstr(query_text, "bottleneck") || strstr(query_text, "瓶頸")) {
        ans->primary_node = g->nodes[0]; ans->matched_score = 100;
        return flowy_explain_bottleneck(g, ans->explanation, sizeof(ans->explanation));
    }
    if (strstr(query_text, "why") || strstr(query_text, "為什麼")) {
        ans->primary_node = g->nodes[0]; ans->matched_score = 100;
        flowy_explain_decision(flow_decision_logger_latest(NULL), ans->explanation, sizeof(ans->explanation));
        return 1;
    }
    uint64_t q_mask = flow_cubical_project_text_intent(query_text);
    const FlowTopologyNode *best_node = &g->nodes[0];
    uint32_t best_score = 0;
    for (size_t i = 0; i < g->node_count; ++i) {
        const FlowTopologyNode *n = &g->nodes[i];
        int match = (n->name[0] && (strcasestr(query_text, n->name) != NULL || strcasestr(n->name, query_text) != NULL));
        uint32_t s = (match ? 150 : 0) + (uint32_t)__builtin_popcountll(q_mask & n->capability_mask) * 30;
        if (s > best_score) { best_score = s; best_node = n; }
    }
    ans->primary_node = *best_node;
    ans->matched_score = best_score ? best_score : 10;
    uint32_t axis = best_node->capability_mask ? (uint32_t)__builtin_ctzll(best_node->capability_mask) : 0;
    const FlowCubicalAxisInfo *ax = flow_cubical_get_axis_info(axis);
    SemanticIR ir = { .input_max_count = 4096, .state_bounded = 1, .state_shared = 1, .memory_limit_mb = 64, .fact_deterministic = 1, .fact_lean_certified = 1 };
    strncpy(ir.flow_name, best_node->name, sizeof(ir.flow_name) - 1);
    FlowTokenRing ring;
    flow_token_ring_setup_canonical(&ring, &ir, 64, 42);
    flow_token_ring_run_to_attractor(&ring, 8);
    snprintf(ans->explanation, sizeof(ans->explanation),
             "[FLOW Canonical Agent: Live AST Topology + Token Ring + Lean 4]\n"
             "Node: %s (Layer %u, Role=%s, LOC=%u, FanIn=%u, FanOut=%u, Sieve=0x%016llx)\n"
             "Subspace: %s | Proof Obligation: %s\n"
             "Token Ring Convergence: Component=%s, Genome=0x%016llx, Energy=%.2f, SMT=%s\n",
             best_node->name, best_node->layer, best_node->role, best_node->loc, best_node->fan_in, best_node->fan_out,
             (unsigned long long)best_node->capability_mask, ax->subspace_name, ax->policy_contract,
             ring.best_search.component ? ring.best_search.component->id : "sharded_hash",
             (unsigned long long)ring.active_genome, ring.lyapunov_energy, ring.smt_proof.proof_summary);
    return 1;
}
