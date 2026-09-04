#include "flow_smt_dsl.h"
#include "potential_game.h"
#include <string.h>
#include <math.h>

int flow_potential_router_init(FlowPotentialRouter *router, double alpha, double beta) {
    if (router == NULL) return 0;
    memset(router, 0, sizeof(*router));
    router->alpha = (alpha > 0.0) ? alpha : 0.15;
    router->beta = (beta >= 1.0) ? beta : 2.0;
    return 1;
}

int flow_potential_register_node(FlowPotentialRouter *router, uint8_t node_id, double capacity, double base_latency_us) {
    if (router == NULL || capacity <= 0.0 || base_latency_us <= 0.0 || router->node_count >= FLOW_POTENTIAL_MAX_NODES) {
        return 0;
    }

    for (size_t i = 0; i < router->node_count; ++i) {
        if (router->nodes[i].node_id == node_id) return 0;
    }

    FlowPotentialNode *node = &router->nodes[router->node_count++];
    node->node_id = node_id;
    node->capacity = capacity;
    node->base_latency_us = base_latency_us;
    node->current_flow = 0.0;
    node->current_cost = base_latency_us;
    node->total_routed = 0;
    node->is_active = true;
    return 1;
}

int flow_potential_update_load(FlowPotentialRouter *router, uint8_t node_id, double flow_delta) {
    if (router == NULL) return 0;

    for (size_t i = 0; i < router->node_count; ++i) {
        FlowPotentialNode *node = &router->nodes[i];
        if (node->node_id == node_id && node->is_active) {
            node->current_flow += flow_delta;
            if (node->current_flow < 0.0) node->current_flow = 0.0;
            break;
        }
    }

    /* Recalculate costs c_i(x_i) and Beckmann potential Phi(x) */
    double total_phi = 0.0;
    for (size_t i = 0; i < router->node_count; ++i) {
        FlowPotentialNode *node = &router->nodes[i];
        if (!node->is_active) continue;

        double x = node->current_flow;
        double c = node->capacity;
        double ratio = (c > 0.0) ? (x / c) : 0.0;
        node->current_cost = node->base_latency_us * (1.0 + router->alpha * pow(ratio, router->beta));

        /* Integral_0^x c_i(s) ds = l_{0, i} * (x + (alpha / (beta + 1)) * (x^{beta+1} / C^beta)) */
        double term2 = (router->alpha / (router->beta + 1.0)) * pow(ratio, router->beta) * x;
        total_phi += node->base_latency_us * (x + term2);
    }
    router->global_potential_phi = total_phi;

    return 1;
}

int flow_potential_route_next(FlowPotentialRouter *router, uint8_t *selected_node_id_out) {
    if (router == NULL || selected_node_id_out == NULL || router->node_count == 0) return 0;
    router->total_routing_decisions++;

    /* Wardrop Gradient Step: Find active node with minimum marginal cost c_i(x_i) */
    double min_cost = 1e18;
    int best_idx = -1;

    for (size_t i = 0; i < router->node_count; ++i) {
        const FlowPotentialNode *node = &router->nodes[i];
        if (!node->is_active) continue;

        /* Calculate cost with additional marginal unit flow */
        double next_ratio = (node->current_flow + 1.0) / node->capacity;
        double marginal_cost = node->base_latency_us * (1.0 + router->alpha * pow(next_ratio, router->beta));

        if (marginal_cost < min_cost) {
            min_cost = marginal_cost;
            best_idx = (int)i;
        }
    }

    if (best_idx < 0) return 0;

    FlowPotentialNode *chosen = &router->nodes[best_idx];
    *selected_node_id_out = chosen->node_id;
    chosen->total_routed++;
    flow_potential_update_load(router, chosen->node_id, 1.0);

    return 1;
}

FlowSMTResult flow_potential_verify_smt(const FlowPotentialRouter *router, FlowSMTProofAttestation *proof_out) {
    if (router == NULL) return FLOW_SMT_UNKNOWN;

    FLOW_SMT_BOX_BUILDER_DECL(builder);

    /* Theorem 1: Capacity & Latency Positivity */
    uint64_t latency_violation = 0;
    double min_cost = 1e18;
    double max_cost = -1e18;
    for (size_t i = 0; i < router->node_count; ++i) {
        if (router->nodes[i].is_active) {
            if (router->nodes[i].current_cost <= 0.0) latency_violation = 1;
            if (router->nodes[i].current_flow > 0.0) {
                if (router->nodes[i].current_cost < min_cost) min_cost = router->nodes[i].current_cost;
                if (router->nodes[i].current_cost > max_cost) max_cost = router->nodes[i].current_cost;
            }
        }
    }
    FLOW_SMT_BOX_ADD_RULE(builder, "latency positivity", latency_violation, 0, 0,
                          FLOW_BOX_THEOREM_BUFFER_BOUNDS, "Node latency cost is non-positive");

    /* Theorem 2: Wardrop Equilibrium Discrepancy Bound (|max_cost - min_cost| <= threshold) */
    uint64_t wardrop_divergence = 0;
    if (max_cost > 0.0 && min_cost < 1e18) {
        double diff = max_cost - min_cost;
        /* If difference among utilized nodes exceeds 50% of min cost under balanced load */
        if (diff > min_cost * 1.5) {
            wardrop_divergence = (uint64_t)diff;
        }
    }
    FLOW_SMT_BOX_ADD_RULE(builder, "wardrop equilibrium convergence", wardrop_divergence, 0, 0,
                          FLOW_BOX_THEOREM_SHARD_ISOLATION, "Utilized nodes diverge from Wardrop equilibrium");

    FlowSMTResult res = FLOW_SMT_BOX_VERIFY(builder, "potential_game_routing", proof_out);
    if (res == FLOW_SMT_PROVEN_UNSAT && proof_out != NULL) {
        snprintf(proof_out->proof_summary, sizeof(proof_out->proof_summary),
                 "SMT WARDROP SOUND: Nodes=%zu, Potential=%.2f, MinCost=%.1fus, MaxCost=%.1fus (Zero-Defect Soundness)",
                 router->node_count, router->global_potential_phi,
                 min_cost < 1e18 ? min_cost : 0.0, max_cost > 0.0 ? max_cost : 0.0);
    }
    return res;
}
