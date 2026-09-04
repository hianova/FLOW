#ifndef FLOW_LYAPUNOV_BACKPRESSURE_H
#define FLOW_LYAPUNOV_BACKPRESSURE_H

#include "smt.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW Lyapunov Backpressure & Banach Contraction Mapping
 * ============================================================================
 * Replaces empirical Exponential Backoff with Jitter and arbitrary timeouts
 * with a deterministic fluid dynamical system:
 *   dq/dt = lambda(t) - mu(t)
 *   Lyapunov Energy: V(q) = 0.5 * q^2
 *   Negative Drift Condition: dV/dt <= -alpha * V(q)
 *
 * Contraction Mapping: The queue state transition operator T(q) satisfies
 *   |T(q1) - T(q2)| <= L * |q1 - q2| with L < 1 (Banach Fixed Point Theorem),
 * guaranteeing asymptotic convergence to zero backlog without limit cycles.
 * ============================================================================
 */

typedef struct {
    double queue_depth;            /* Continuous fluid queue length q(t) */
    double max_queue_capacity;     /* Queue ceiling C */
    double arrival_rate_lambda;    /* Ingress arrival rate (pkts/sec) */
    double service_rate_mu;        /* Egress processing rate (pkts/sec) */
    double alpha_stability_gain;   /* Convergence rate alpha > 0 */
    double lipschitz_constant_l;   /* Contraction factor L < 1 */
    uint64_t total_steps;
    uint64_t total_backpressure_throttles;
} FlowLyapunovGovernor;

/* Initialize Lyapunov Governor */
int flow_lyapunov_init(FlowLyapunovGovernor *gov, double max_capacity, double alpha_gain);

/* Advance fluid queue dynamics by time step dt */
int flow_lyapunov_step(FlowLyapunovGovernor *gov, double arrival_rate, double service_rate, double dt_sec);

/* Query whether backpressure should be asserted based on Lyapunov drift dV/dt */
bool flow_lyapunov_should_throttle(const FlowLyapunovGovernor *gov);

/* Compute phase-space damped retry delay (nanoseconds) replacing heuristic jitter */
uint64_t flow_lyapunov_compute_retry_delay_ns(const FlowLyapunovGovernor *gov, uint64_t base_delay_ns);

/* SMT Formal Asymptotic Stability & Non-Overflow Theorem */
FlowSMTResult flow_lyapunov_verify_smt(const FlowLyapunovGovernor *gov, FlowSMTProofAttestation *proof_out);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_LYAPUNOV_BACKPRESSURE_H */
