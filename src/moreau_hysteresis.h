#ifndef FLOW_MOREAU_HYSTERESIS_H
#define FLOW_MOREAU_HYSTERESIS_H

#include "smt.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW Moreau Sweeping Process & Geometric Hysteresis Operator
 * ============================================================================
 * Replaces empirical debounce timers (now - last_switch > 500ms) and streak
 * counters with Non-smooth Mechanics and Moreau's Sweeping Process:
 *
 *   -dx/dt in N_C(x)
 *
 * where N_C(x) is the normal cone to the closed convex set C = [x_low, x_high].
 * The discrete decision state s in {0, 1} changes only when the input strictly
 * penetrates the boundary of C:
 *   s = 1  if x >= x_high
 *   s = 0  if x <= x_low
 *   s = s_prev if x in (x_low, x_high)
 *
 * Mathematically eliminates flapping for any perturbation with amplitude
 * delta < (x_high - x_low).
 * ============================================================================
 */

typedef struct {
    double threshold_low;          /* x_low */
    double threshold_high;         /* x_high (strictly > x_low) */
    double current_value;          /* Internal smooth state x(t) */
    int discrete_state;            /* 0 (nominal) or 1 (throttled / alert) */
    uint64_t total_updates;
    uint64_t state_transitions;
    uint64_t flutters_suppressed;  /* Noise fluctuations within [x_low, x_high] absorbed */
} FlowMoreauHysteresis;

/* Initialize Moreau Hysteresis with convex boundary [x_low, x_high] */
int flow_moreau_init(FlowMoreauHysteresis *hys, double x_low, double x_high, int initial_state);

/* Evaluate input signal via normal cone projection and return active discrete state */
int flow_moreau_step(FlowMoreauHysteresis *hys, double input_signal);

/* SMT Formal Non-Smooth Anti-Flapping Soundness Proof */
FlowSMTResult flow_moreau_verify_smt(const FlowMoreauHysteresis *hys, FlowSMTProofAttestation *proof_out);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_MOREAU_HYSTERESIS_H */
