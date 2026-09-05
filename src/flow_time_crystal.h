#ifndef FLOW_TIME_CRYSTAL_H
#define FLOW_TIME_CRYSTAL_H

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
 * FLOW Discrete Time Crystal (DTC) Simulation Engine (flow_time_crystal.h)
 * ============================================================================
 * Physical Foundation:
 * Discrete Time Crystals spontaneously break Discrete Time Translation Symmetry (DTTS).
 * Under periodic Floquet driving of period T, the macroscopic observable exhibits
 * robust subharmonic oscillation of period 2T (or nT):
 *   O(t + T) = -O(t),   O(t + 2T) = O(t)
 *
 * Protection Mechanism:
 * Protected against thermal heating / thermalization via Many-Body Localization (MBL)
 * or Prethermalization driven by non-linear phase-space geodesic confinement on .fjet.
 *
 * Strategic Application in FLOW:
 * Provides a zero-dissipation cyclic computational memory substrate where logic states
 * are stored in the robust topological phase of eternal limit-cycle trajectories.
 * ============================================================================
 */

#define FLOW_DTC_MAX_HISTORY 256

typedef struct {
    FlowJet *jet;                               /* Underlying symplectic Jet manifold */
    double period_T;                            /* Floquet drive period T (seconds) */
    double kick_strength;                       /* Parametric kick rotation angle / strength (pi * (1 - epsilon)) */
    double disorder_strength;                   /* Quasi-random disorder / MBL localization field W */
    uint32_t floquet_cycles_total;              /* Total Floquet driving periods elapsed */
    double current_order_param;                 /* Macroscopic subharmonic order Z(t) */
    double history_order[FLOW_DTC_MAX_HISTORY]; /* Historical trajectory of order parameter */
    size_t history_count;
    double initial_energy;                      /* Base Hamiltonian energy H_0 */
    double current_energy;                      /* Current Hamiltonian energy H(t) */
    double max_energy_drift;                    /* Max observed energy deviation |H(t) - H_0| */
    int is_subharmonic_locked;                  /* 1 if rigid 2T subharmonic oscillation verified */
    int encoded_bit;                            /* Stored bit (0 or 1) in topological limit cycle phase */
} FlowTimeCrystal;

/* Lifecycle */
int flow_dtc_init(FlowTimeCrystal *dtc, FlowJet *jet, double period_T,
                  double kick_strength, double disorder_strength);

/* Execute Floquet driving: continuous symplectic evolution over (T - kick) + instantaneous parametric kick */
int flow_dtc_step_floquet(FlowTimeCrystal *dtc, uint32_t cycles, double dt);

/* Compute macroscopic spatial order parameter Z = (1/dim) * sum(sgn(q_i)) */
double flow_dtc_compute_subharmonic_order(const FlowTimeCrystal *dtc);

/* Calculate Discrete Fourier Transform peak ratio at subharmonic frequency omega = pi/T */
double flow_dtc_get_fourier_subharmonic_ratio(const FlowTimeCrystal *dtc);

/* Encode / decode computational bit in the topological phase of the limit cycle */
int flow_dtc_encode_bit(FlowTimeCrystal *dtc, int bit_val);
int flow_dtc_decode_bit(const FlowTimeCrystal *dtc);

/* Formal SMT Supreme Court verification of Time-Translation Symmetry Breaking & Non-Thermalization */
FlowSMTResult flow_dtc_verify_soundness_smt(const FlowTimeCrystal *dtc,
                                            FlowSMTProofAttestation *proof_out);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_TIME_CRYSTAL_H */
