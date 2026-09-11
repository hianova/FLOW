#ifndef FLOW_POLYHEDRAL_H
#define FLOW_POLYHEDRAL_H

#include "flow.h"
#include "plugin.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef FLOW_SMT_RESULT_DEFINED
#define FLOW_SMT_RESULT_DEFINED
typedef enum {
    FLOW_SMT_PROVEN_UNSAT = 0,    /* Negation is UNSAT -> Theorem holds universally */
    FLOW_SMT_VIOLATION_SAT = 1,   /* Counterexample found -> Invariant violated */
    FLOW_SMT_UNKNOWN = 2          /* Unconstrained or unbounded */
} FlowSMTResult;
#endif

struct FlowSMTProofAttestation;
typedef struct FlowSMTProofAttestation FlowSMTProofAttestation;

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

/* Quadratic convex inequality: 0.5 * (i - center)^T H (i - center) + gradient^T (i - center) <= bound */
typedef struct {
    double center[FLOW_POLY_MAX_DIM];
    double hessian[FLOW_POLY_MAX_DIM][FLOW_POLY_MAX_DIM];
    double gradient[FLOW_POLY_MAX_DIM];
    double bound;                        /* Capacity budget B */
    double semi_axes[FLOW_POLY_MAX_DIM]; /* Principal semi-axes r_j = sqrt(2*B / H_jj) */
    bool is_active;
} FlowQuadraticConstraint;

typedef struct {
    size_t dimension;
    size_t constraint_count;
    FlowAffineConstraint constraints[FLOW_POLY_MAX_CONSTRAINTS];
    int64_t lower_bounds[FLOW_POLY_MAX_DIM];
    int64_t upper_bounds[FLOW_POLY_MAX_DIM];
    size_t quad_constraint_count;
    FlowQuadraticConstraint quad_constraints[FLOW_POLY_MAX_CONSTRAINTS];
} FlowPolyhedron;

typedef struct {
    size_t optimal_tile_size;          /* T* provably maximizing L1/L2 data reuse */
    size_t optimal_simd_width;         /* V* provably hazard-free vector width */
    int64_t total_iterations;          /* Exact integer cardinality |D| */
    bool is_parallelizable;            /* 1 if Farkas dependence distance is 0 */
    bool is_bounded;                   /* 1 if polyhedron is compact */
    double quadratic_recovered_volume; /* Volume expanded through quadratic convexification */
    bool has_quadratic_curvature;      /* 1 if quadratic non-linear constraints are active */
} FlowPolyhedralSchedule;

/* Initialize Polyhedron with dimension n (e.g. 2 for nested loop) */
int flow_polyhedral_init(FlowPolyhedron *poly, size_t dimension);

/* Add affine inequality constraint A*i + b >= 0 */
int flow_polyhedral_add_constraint(FlowPolyhedron *poly, const int64_t *coeffs, int64_t constant);

/* Add quadratic Taylor convex constraint 0.5 * (i - center)^T H (i - center) + grad^T (i - center) <= bound */
int flow_polyhedral_add_quadratic_constraint(FlowPolyhedron *poly,
                                             const double center[],
                                             const double hessian[FLOW_POLY_MAX_DIM][FLOW_POLY_MAX_DIM],
                                             const double gradient[],
                                             double bound);

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

struct FlowJetPotentialLandscape;

/* Compiler-Guided Symplectic Shaping: Synthesize .fjet potential landscape from polyhedral bounds */
int flow_polyhedral_synthesize_jet_potential(size_t capacity,
                                            size_t threads,
                                            struct FlowJetPotentialLandscape *landscape_out);

struct FlowHardwiredPolyhedralTemplate;

/* Export to AOT Universal Hardwired Template (zero runtime compilation) */
int flow_polyhedral_export_template(const FlowPolyhedron *poly,
                                    struct FlowHardwiredPolyhedralTemplate *tpl_out);

/* Apply 64-bit hyperplane mask to active constraints */
int flow_polyhedral_apply_template_mask(FlowPolyhedron *poly, uint64_t mask);

/*
 * ============================================================================
 * Mathematical Polyhedral Constraint & Hypercube Projection Engine
 * Linear Inequality System: \mathcal{P} = { x \in R^D | A x <= b }
 * Orthogonal Projection Operator: \Pi_{\mathcal{P}} : R^D -> {0, 1}^N
 * ============================================================================
 */

#define FLOW_POLYTOPE_MAX_CONSTRAINTS 32
#define FLOW_POLYTOPE_MAX_DIMS 16

typedef enum {
    FLOW_CONSTRAINT_LEQ = 0, /* a^T x <= b */
    FLOW_CONSTRAINT_GEQ = 1, /* a^T x >= b */
    FLOW_CONSTRAINT_EQ  = 2, /* a^T x == b */
    FLOW_CONSTRAINT_INTERVAL = 3 /* b_min <= a^T x <= b_max */
} FlowConstraintOp;

typedef struct {
    double coefficients[FLOW_POLYTOPE_MAX_DIMS];
    FlowConstraintOp op;
    double rhs_min;
    double rhs_max;
    char symbolic_tag[64];
} FlowLinearConstraint;

typedef struct {
    FlowLinearConstraint constraints[FLOW_POLYTOPE_MAX_CONSTRAINTS];
    size_t constraint_count;
    size_t dimension_count;
    double lower_bounds[FLOW_POLYTOPE_MAX_DIMS];
    double upper_bounds[FLOW_POLYTOPE_MAX_DIMS];
} FlowPolyhedronSystem;

/* Polyhedron Lifecycle & Hypercube Projection */
void flow_polyhedron_init(FlowPolyhedronSystem *poly, size_t dim_count);
int flow_polyhedron_add_box_bounds(FlowPolyhedronSystem *poly, size_t dim_idx, double min_val, double max_val, const char *tag);
int flow_polyhedron_add_inequality(FlowPolyhedronSystem *poly, const double *coeffs, FlowConstraintOp op, double bound, const char *tag);
int flow_polyhedron_from_ir(const SemanticIR *ir, const Component *comp, const FlowPlanDimensionSet *dims, FlowPolyhedronSystem *poly);
uint64_t flow_polyhedron_project_mask(const FlowPolyhedronSystem *poly, const FlowPlanDimensionSet *dims, uint32_t total_bits);

/* Unified Mathematical Morphism: Affine Polyhedron \mathcal{P} -> BitSpace Hypercube Projection */
int flow_polyhedron_from_affine(const FlowPolyhedron *affine_poly, FlowPolyhedronSystem *poly_out);
uint64_t flow_polyhedral_project_to_bitspace(const FlowPolyhedron *poly, const FlowPlanDimensionSet *dims, uint32_t total_bits);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_POLYHEDRAL_H */
