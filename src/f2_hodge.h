#ifndef FLOW_F2_HODGE_H
#define FLOW_F2_HODGE_H

#include "smt.h"
#include "polyhedral.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW Discrete F2-Hodge Orthogonal Projector on Cubical Complexes (f2_hodge.h)
 * ============================================================================
 * Implements Discrete Exterior Calculus (DEC) over the finite field F_2 = {0, 1}
 * on the 1-bit BitManifold (BMF) hypercube {0, 1}^N:
 *
 * Boundary nilpotency:
 *   partial_1 o partial_2 = 0 (mod 2)
 *
 * Unique Hodge-Helmholtz orthogonal decomposition of any 1-form vector field V:
 *   V = d_0(Phi) [Exact Gradient Flow]
 *     ^ H_1      [Harmonic Non-Contractible Loop]
 *     ^ delta_2(Psi) [Coexact Vortex / Chattering]
 *
 * Key Properties:
 * 1. Exact flow d_0(Phi) is topologically strictly acyclic (no limit cycles).
 * 2. Coexact flow delta_2(Psi) represents high-frequency 2-cube chattering.
 * 3. Harmonic flow H_1 represents global topological ergodic chaos.
 * 4. Exact projector P_exact annihilates vortices and harmonic loops in 1 cycle.
 * ============================================================================
 */

typedef struct {
    uint64_t exact_flow;      /* d_0 Phi: Monotonic acyclic gradient flow */
    uint64_t harmonic_loop;   /* H_1: Non-contractible ergodic loop / chaos source */
    uint64_t coexact_vortex;  /* delta_2 Psi: Local 2-cube chattering circulation */
    uint64_t total_field;     /* V = exact ^ harmonic ^ coexact */
} FlowF2HodgeDecomposition;

/*
 * Discrete F2-Hodge Orthogonal Decomposition:
 * Decomposes arbitrary state transition field V against boundary sieve S:
 *   V = exact ^ harmonic ^ coexact
 * Guarantees pairwise disjoint orthogonality in F_2:
 *   exact & coexact == 0, exact & harmonic == 0, harmonic & coexact == 0.
 */
int flow_f2_hodge_decompose(uint64_t v_field,
                            uint64_t boundary_sieve,
                            FlowF2HodgeDecomposition *decomp_out);

/*
 * 2-Cube Circulation (Curl) Operator:
 * Computes the circulation parity over local 2-cubes (squares).
 * Non-zero curl indicates high-frequency flipping (chattering).
 */
uint64_t flow_f2_hodge_curl(uint64_t v_field, uint64_t surface_mask);

/*
 * Exact Gradient Flow Projector P_exact:
 * Filters out coexact vortices (curl) and harmonic loops, leaving
 * only the strictly monotonic potential descent field:
 *   P_exact(V) = V ^ curl(V) ^ harmonic(V)
 */
uint64_t flow_f2_hodge_project_exact(uint64_t v_field, uint64_t boundary_sieve);

/*
 * Scenario 1: Zero-Latency Anti-Chattering Filter
 * Eliminates 2-cycle or 4-cycle alternating oscillations across non-smooth
 * sliding mode surfaces (Moreau impulse switching) in a single clock cycle,
 * without low-pass delay or boundary layer softening.
 */
uint64_t flow_f2_hodge_anti_chattering(uint64_t current_state,
                                       uint64_t transition_intent,
                                       uint64_t sliding_surface_mask);

/*
 * Scenario 2: Combinational Deadlock Killer
 * Resolves circular wait / resource cycle deadlocks by projecting cyclic
 * dependency graphs to exact acyclic DAG schedules in 1 cycle without timeout.
 */
int flow_f2_hodge_kill_deadlock(const uint64_t dependency_matrix[FLOW_POLY_MAX_DIM],
                                uint64_t *acyclic_schedule_out);

/*
 * Scenario 3: Chaos-to-Quench Single-Bit Throttle
 * Quench flag = 0: releases H_1 harmonic loops for ergodic chaotic exploration.
 * Quench flag = 1: applies P_exact, instantaneously extinguishing circulation
 *                  and forcing monotonic settling onto the target attractor.
 */
uint64_t flow_f2_hodge_throttle(uint64_t current_state,
                                uint64_t chaos_step,
                                uint8_t quench_active,
                                uint64_t target_attractor);

/*
 * SMT Supreme Court Formal Verification of F2-Hodge Orthogonality:
 * Theorem 1: Nilpotent Boundary (partial_1 o partial_2 = 0 mod 2)
 * Theorem 2: F_2 Orthogonality (exact & coexact == 0)
 * Theorem 3: Acyclic Monotonicity (curl(P_exact(V)) == 0)
 * Theorem 4: 64-Byte Cache-Line Confinement
 */
FlowSMTResult flow_f2_hodge_verify_smt(const FlowF2HodgeDecomposition *decomp,
                                       FlowSMTProofAttestation *proof_out);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_F2_HODGE_H */
