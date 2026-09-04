#ifndef FLOW_POLYHEDRAL_H
#define FLOW_POLYHEDRAL_H

#include "smt.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW Polyhedral Model: Presburger Affine Integer Optimization
 * ============================================================================
 * Replaces empirical compiler loop unroll and pass heuristics with
 * affine-constrained Integer Linear Programming (ILP) on multi-dimensional
 * iteration polyhedra D = { i in Z^n | A*i + b >= 0 }.
 *
 * Provably determines:
 * 1. Optimal loop tiling factor T* minimizing cache misses.
 * 2. Exact SIMD vectorization width V* guaranteeing zero cross-iteration hazard.
 * ============================================================================
 */

#define FLOW_POLY_MAX_DIM 4
#define FLOW_POLY_MAX_CONSTRAINTS 16

/* Affine inequality: sum_{j=0}^{dim-1} a_j * i_j + c >= 0 */
typedef struct {
    int64_t coeffs[FLOW_POLY_MAX_DIM];
    int64_t constant;
} FlowAffineConstraint;

typedef struct {
    size_t dimension;
    size_t constraint_count;
    FlowAffineConstraint constraints[FLOW_POLY_MAX_CONSTRAINTS];
    int64_t lower_bounds[FLOW_POLY_MAX_DIM];
    int64_t upper_bounds[FLOW_POLY_MAX_DIM];
} FlowPolyhedron;

typedef struct {
    size_t optimal_tile_size;       /* T* provably maximizing L1/L2 data reuse */
    size_t optimal_simd_width;      /* V* provably hazard-free vector width */
    int64_t total_iterations;       /* Exact integer cardinality |D| */
    bool is_parallelizable;         /* 1 if Farkas dependence distance is 0 */
    bool is_bounded;                /* 1 if polyhedron is compact */
} FlowPolyhedralSchedule;

/* Initialize Polyhedron with dimension n (e.g. 2 for nested loop) */
int flow_polyhedral_init(FlowPolyhedron *poly, size_t dimension);

/* Add affine inequality constraint A*i + b >= 0 */
int flow_polyhedral_add_constraint(FlowPolyhedron *poly, const int64_t *coeffs, int64_t constant);

/* Set box bounds for a specific dimension [lower, upper] */
int flow_polyhedral_set_box_bounds(FlowPolyhedron *poly, size_t dim_idx, int64_t lower, int64_t upper);

/* Solve optimal schedule via Fourier-Motzkin elimination and Farkas Lemma */
int flow_polyhedral_solve_schedule(const FlowPolyhedron *poly,
                                  size_t cache_line_bytes,
                                  size_t vector_register_bytes,
                                  FlowPolyhedralSchedule *schedule_out);

/* SMT Formal Polytope Verification (Bounds safety, non-emptiness, determinism) */
FlowSMTResult flow_polyhedral_verify_smt(const FlowPolyhedron *poly,
                                        const FlowPolyhedralSchedule *sched,
                                        FlowSMTProofAttestation *proof_out);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_POLYHEDRAL_H */
