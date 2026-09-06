#ifndef FLOW_JET_IMPACT_H
#define FLOW_JET_IMPACT_H

#include "flow_embodied_mz.h"
#include "flow_jet.h"
#include "smt.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW Non-Smooth Symplectic Impact Manifold (flow_jet_impact.h)
 * ============================================================================
 * Physics Foundation:
 * Rigid impact events in robotics (e.g. bipedal foot strike, peg-in-hole collision)
 * introduce discontinuous velocity jumps (\dot{q}^+ \ne \dot{q}^-).
 * Standard Runge-Kutta / Euler integrators violate energy conservation during impact,
 * blowing up numerical Hamiltonian energy or inducing high-frequency chattering.
 *
 * Solution:
 * Combines Moreau's Sweeping Process with Symplectic Submanifold Refraction:
 * 1. Symplectic Momentum Jump Mapping:
 *      p^+ = -e * p^- + \Delta p_contact
 *    preserves the symplectic 2-form \omega = \sum dq_i \wedge dp_i on the contact
 *    hypersurface \Sigma = { q | \phi(q) = 0 }.
 * 2. Mori-Zwanzig Viscoelastic Absorption:
 *    The non-Markovian memory kernel dissipates impact shocks without artificial damping
 *    during free flight.
 * 3. Passivity Guarantee:
 *    Proves H(q^+, p^+) <= H(q^-, p^-) (Lyapunov passivity, zero chatter).
 * ============================================================================
 */

typedef struct {
    size_t joint_count;
    double restitution_coeff;               /* Coefficient of restitution e in [0.0, 1.0] */
    double surface_height;                  /* Contact plane boundary q_crit */
    double max_allowed_torque;              /* Motor rated torque limit */
    FlowMoriZwanzigImpedanceController *mz_controller; /* Bound Mori-Zwanzig controller */

    /* Impact statistics */
    uint64_t total_impact_events;
    uint64_t total_ticks_evaluated;
    double peak_impact_impulse;
    double total_dissipated_energy;
    bool is_contact_active;
    bool passivity_maintained;
} FlowSymplecticImpactManifold;

/* Initialize symplectic impact manifold with restitution coefficient and ground plane */
int flow_symplectic_impact_init(FlowSymplecticImpactManifold *manifold,
                               FlowMoriZwanzigImpedanceController *mz_ctrl,
                               double restitution_coeff,
                               double surface_height,
                               double max_torque);

/* Advance 10kHz reflex step with Moreau normal cone projection & symplectic momentum jump */
int flow_symplectic_impact_step_10khz(FlowSymplecticImpactManifold *manifold,
                                      FlowJet *jet,
                                      const double target_q[],
                                      const double target_v[],
                                      double torques_out[],
                                      double dt);

/* Detect if any joint/end-effector is in contact with the impact boundary */
bool flow_symplectic_impact_is_in_contact(const FlowSymplecticImpactManifold *manifold,
                                          const FlowJet *jet);

/* Formal SMT Supreme Court verification of symplectic impact passivity and non-explosive torque */
FlowSMTResult flow_symplectic_impact_verify_smt(const FlowSymplecticImpactManifold *manifold,
                                                const FlowJet *jet,
                                                FlowSMTProofAttestation *proof_out);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_JET_IMPACT_H */
