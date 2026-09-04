#ifndef FLOW_MANIFOLD_ALGEBRA_H
#define FLOW_MANIFOLD_ALGEBRA_H

#include "flow.h"
#include "bitspace.h"
#include "smt.h"
#include "flow_smt_dsl.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW Manifold Algebra: Constraint Convergence is Correlation
 * ============================================================================
 * In FLOW, relationships between dimensions are not captured by empirical,
 * fragile O(N^2) covariance matrices. Instead:
 *
 *     "Constraint Convergence IS Correlation (約束收斂即相關)"
 *
 * When constraints are inactive (slack s > 0), dimensions are orthogonal and
 * independent (Lagrangian shadow price lambda = 0).
 * Once a physical constraint activates (lambda > 0), the boundary normal cone
 * collapses degrees of freedom, locking dimensions onto an epistatic geodesic.
 *
 * This algebra formalizes:
 * 1. Manifold Intersection: M_A \cap M_B (Pareto Consensus & Feasible Fusion)
 * 2. Manifold Direct Sum:   M_A \oplus M_B (Orthogonal Subspace Composition)
 * 3. Moreau Projection:     \Pi_M(x) (Non-smooth Boundary Clamping)
 * 4. Epistatic Linkage:     Extracts coupled dimensions from active constraints
 * 5. SMT Formal Proof:      Zero-Defect guarantee of non-empty intersection
 * ============================================================================
 */

#define FLOW_MANIFOLD_DIM 16
#define FLOW_MANIFOLD_MAX_CONSTRAINTS 8

typedef struct {
    double normal[FLOW_MANIFOLD_DIM]; /* Hyperplane normal vector a */
    double bound;                     /* Hyperplane bound b in a^T x <= b */
    bool is_active;                   /* True if boundary is currently touched */
    double shadow_price;              /* Dual multiplier lambda >= 0 */
} FlowManifoldConstraint;

typedef struct {
    uint64_t subspace_mask;                   /* 64-bit BitManifold subspace */
    double center[FLOW_MANIFOLD_DIM];         /* Pareto optimal attractor coordinates */
    double lower_bounds[FLOW_MANIFOLD_DIM];   /* Feasible box lower bounds */
    double upper_bounds[FLOW_MANIFOLD_DIM];   /* Feasible box upper bounds */
    double dual_multipliers[FLOW_MANIFOLD_DIM];/* Cumulative shadow prices */
    uint64_t epistatic_linkage_mask;          /* Bitmask of mutually coupled dimensions */
    FlowManifoldConstraint constraints[FLOW_MANIFOLD_MAX_CONSTRAINTS];
    size_t constraint_count;
    double volume_proxy;                      /* Product of bounds spans */
    bool is_compact;
} FlowManifold;

/* Initialize an empty manifold on a given 64-bit subspace */
int flow_manifold_init(FlowManifold *m, uint64_t subspace_mask);

/* Configure box interval bounds for a specific dimension [lower, upper] */
int flow_manifold_set_bounds(FlowManifold *m, size_t dim, double lower, double upper);

/* Add an affine inequality constraint a^T x <= b */
int flow_manifold_add_constraint(FlowManifold *m, const double *normal, double bound, double shadow_price);

/*
 * Epistatic Linkage Derivation:
 * Identifies coupled dimensions where active constraints (shadow_price > 0)
 * force simultaneous co-adaptation.
 */
int flow_manifold_derive_epistatic_linkage(FlowManifold *m);

/*
 * Manifold Intersection (Meet Operator):
 * M_inter = M_A \cap M_B
 * Computes joint feasible space, Pareto consensus center, merged constraints,
 * and unified epistatic linkage groups. Returns 1 on non-empty intersection, 0 on empty.
 */
int flow_manifold_intersect(const FlowManifold *a, const FlowManifold *b, FlowManifold *out_intersection);

/*
 * Manifold Direct Sum (Orthogonal Join Operator):
 * M_sum = M_A \oplus M_B
 * Combines two disjoint subspaces (subspace_mask_A & subspace_mask_B == 0).
 */
int flow_manifold_direct_sum(const FlowManifold *a, const FlowManifold *b, FlowManifold *out_sum);

/*
 * Moreau Boundary Projection Operator:
 * \Pi_M(x) = argmin_{y \in M} 1/2 || y - x ||^2
 * Projects an arbitrary point onto the closed convex manifold boundary.
 */
int flow_manifold_boundary_project(const FlowManifold *m, const double *input_pt, double *projected_pt);

/*
 * SMT Formal Proof of Manifold Invariants:
 * 1. Box Invariant: lower <= upper for all dimensions
 * 2. Non-Emptiness: Feasible space volume > 0
 * 3. Shadow Price Non-Negativity: lambda_i >= 0 (KKT dual feasibility)
 * 4. Epistatic Consistency: coupled bits match active constraint gradients
 */
FlowSMTResult flow_manifold_verify_smt(const FlowManifold *m, FlowSMTProofAttestation *proof_out);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_MANIFOLD_ALGEBRA_H */
