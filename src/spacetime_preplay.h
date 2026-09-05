#ifndef FLOW_SPACETIME_PREPLAY_H
#define FLOW_SPACETIME_PREPLAY_H

#include "flow.h"
#include "smt.h"
#include "flow_smt_dsl.h"
#include "simd_manifold.h"
#include "hardware_telemetry.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW Spacetime Pre-Play Engine (Counterfactual Spacetime Cone)
 * ============================================================================
 * Upgrades SMT from "Now-Safe" (single-step verification) to "Spacetime-Safe":
 * Pre-plays the Hamiltonian phase-space light cone 3.0 seconds into the future
 * using 512-bit SIMD registers before issuing physical commands.
 *
 * If a black swan (e.g. ice-patch spin-out, queue collapse) is detected at
 * t = 2.8s, 1-bit chaotic annealing computes phase-space steering bias in
 * < 200 microseconds, eliminating the failure 2.8 seconds BEFORE it happens.
 * ============================================================================
 */

#define FLOW_PREPLAY_DIM 8
#define FLOW_PREPLAY_HORIZON_STEPS 60  /* 60 x 0.05s = 3.0 seconds */
#define FLOW_PREPLAY_DT 0.05           /* 50 ms simulation delta */

typedef struct {
    double q[FLOW_PREPLAY_DIM];        /* Generalized coordinates (pos, yaw, roll, joints) */
    double p[FLOW_PREPLAY_DIM];        /* Generalized conjugate momenta */
    double timestamp_s;
} FlowPhaseState;

typedef struct {
    double mass_kg;
    double inertia_kg_m2;
    double nominal_friction_mu;        /* e.g. 0.85 (dry asphalt) */
    double black_swan_friction_mu;     /* e.g. 0.05 (black ice) */
    double black_swan_start_time_s;    /* e.g. 2.5s */
    double black_swan_end_time_s;      /* e.g. 3.0s */
    double critical_roll_angle_rad;    /* e.g. 0.25 rad (~14.3 deg, tip-over threshold) */
    double nominal_control[FLOW_PREPLAY_DIM]; /* Desired control inputs */
} FlowPrePlayEnvironment;

typedef struct {
    FlowPhaseState trajectory[FLOW_PREPLAY_HORIZON_STEPS];
    size_t step_count;
    bool violation_detected;
    double violation_time_s;
    size_t violation_step;
    char violation_reason[128];
    double max_roll_observed;
    double peak_lateral_force_n;
} FlowSpacetimeConeResult;

typedef struct {
    FlowPrePlayEnvironment env;
    FlowVector512 q_simd;              /* 512-bit SIMD lane for Q */
    FlowVector512 p_simd;              /* 512-bit SIMD lane for P */
    double pre_emptive_bias[FLOW_PREPLAY_DIM]; /* Annealed compensation bias */
    uint64_t preplay_cycles;
    double preplay_duration_us;
    uint64_t total_preplays;
    uint64_t black_swans_averted;
} FlowSpacetimeEngine;

/* Initialize Spacetime Pre-Play Engine with physical parameters */
int flow_spacetime_init(FlowSpacetimeEngine *engine, double mass_kg, double nominal_mu);

/* Configure Black Swan scenario (e.g. ice patch at t=2.5s..3.0s, mu=0.05) */
int flow_spacetime_set_black_swan(FlowSpacetimeEngine *engine,
                                  double ice_mu,
                                  double start_s,
                                  double end_s,
                                  double critical_roll_rad);

/*
 * Forward Spacetime Cone Simulation:
 * Simulates future 3.0 seconds trajectory under given control input and bias.
 */
int flow_spacetime_simulate(const FlowSpacetimeEngine *engine,
                            const FlowPhaseState *initial_state,
                            const double *control_bias,
                            FlowSpacetimeConeResult *result_out);

/*
 * Counterfactual Pre-Play & 1-Bit Chaotic Annealing:
 * 1. Simulates future 3.0s under nominal control.
 * 2. If a violation is predicted (e.g. at t=2.8s on ice), executes fast
 *    annealing in < 200 microseconds.
 * 3. Derives optimal pre-emptive bias delta_U, re-verifies safety,
 *    and writes compensated bias into engine->pre_emptive_bias.
 */
int flow_spacetime_preplay_and_anneal(FlowSpacetimeEngine *engine,
                                     const FlowPhaseState *initial_state,
                                     FlowSpacetimeConeResult *final_result_out);

/*
 * SMT Formal Verification of Spacetime Light Cone:
 * Proves:
 * 1. Post-compensation roll stability: max_roll <= critical_roll
 * 2. Bounded Annealing Latency: preplay_duration_us < 200.0 us
 * 3. Friction Cone Feasibility: lateral_force <= mu * normal_force
 * 4. Symplectic Energy Conservation: finite bounded Hamiltonian
 */
FlowSMTResult flow_spacetime_preplay_verify_smt(const FlowSpacetimeEngine *engine,
                                               const FlowSpacetimeConeResult *result,
                                               FlowSMTProofAttestation *proof_out);

/*
 * Spacetime Pre-Play Cache Warmup:
 * Speculative L1I and L1D prefetch of emergency braking routines and CAN DMA buffers.
 */
int flow_spacetime_warmup_emergency_cache(const FlowSpacetimeEngine *engine,
                                         const FlowSpacetimeConeResult *result,
                                         const void *emergency_code_ptr,
                                         const void *emergency_data_ptr,
                                         uint32_t *lines_warmed_out);

FlowSMTResult flow_spacetime_verify_prefetch_soundness_smt(const FlowSpacetimeEngine *engine,
                                                          uint32_t lines_warmed,
                                                          double warmup_latency_ns,
                                                          FlowSMTProofAttestation *proof_out);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_SPACETIME_PREPLAY_H */
