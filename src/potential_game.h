#ifndef FLOW_POTENTIAL_GAME_H
#define FLOW_POTENTIAL_GAME_H

#include "smt.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW Potential Game & Wardrop Equilibrium Routing
 * ============================================================================
 * Replaces empirical load balancing heuristics (round-robin, work-stealing)
 * with Non-cooperative Potential Game Theory and Wardrop's First Equilibrium:
 *
 * Cost Function: c_i(x_i) = l_{0, i} * (1 + alpha * (x_i / C_i)^beta)
 * Beckmann Potential: Phi(x) = sum_i integral_0^{x_i} c_i(s) ds
 *
 * Wardrop Principle: At equilibrium, all utilized nodes have identical minimal
 * latency mu*, and all unutilized nodes have latency >= mu*.
 * Routing follows the gradient flow dx/dt = -grad Phi(x).
 * ============================================================================
 */

#define FLOW_POTENTIAL_MAX_NODES 16

typedef struct {
    uint8_t node_id;
    double capacity;             /* C_i in requests/sec */
    double base_latency_us;      /* l_{0, i} in microseconds */
    double current_flow;         /* x_i in active requests */
    double current_cost;         /* c_i(x_i) */
    uint64_t total_routed;
    bool is_active;
} FlowPotentialNode;

typedef struct {
    FlowPotentialNode nodes[FLOW_POTENTIAL_MAX_NODES];
    size_t node_count;
    double alpha;                /* Congestion parameter alpha (e.g. 0.15) */
    double beta;                 /* Congestion exponent beta (e.g. 2.0) */
    double global_potential_phi; /* Beckmann potential Phi(x) */
    uint64_t total_routing_decisions;
} FlowPotentialRouter;

/* Initialize Potential Router */
int flow_potential_router_init(FlowPotentialRouter *router, double alpha, double beta);

/* Register node */
int flow_potential_register_node(FlowPotentialRouter *router, uint8_t node_id, double capacity, double base_latency_us);

/* Update load flow and calculate current costs & global Beckmann potential */
int flow_potential_update_load(FlowPotentialRouter *router, uint8_t node_id, double flow_delta);

/* Route next request following negative gradient -grad Phi(x) to preserve Wardrop equilibrium */
int flow_potential_route_next(FlowPotentialRouter *router, uint8_t *selected_node_id_out);

/* SMT Formal Wardrop Equilibrium & Bounded Invariant Proof */
FlowSMTResult flow_potential_verify_smt(const FlowPotentialRouter *router, FlowSMTProofAttestation *proof_out);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_POTENTIAL_GAME_H */
