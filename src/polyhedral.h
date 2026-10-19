#ifndef FLOW_POLYHEDRAL_H
#define FLOW_POLYHEDRAL_H
#include "flow.h"
#include "plugin.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#ifndef FLOW_SMT_RESULT_DEFINED
#define FLOW_SMT_RESULT_DEFINED
typedef enum { FLOW_SMT_PROVEN_UNSAT = 0, FLOW_SMT_VIOLATION_SAT = 1, FLOW_SMT_UNKNOWN = 2
} FlowSMTResult;
#endif
struct FlowSMTProofAttestation; typedef struct FlowSMTProofAttestation FlowSMTProofAttestation;
#ifdef __cplusplus
extern "C" {
#endif
#define FLOW_POLY_MAX_DIM 4
#define FLOW_POLY_MAX_CONSTRAINTS 16
typedef struct { int64_t coeffs[FLOW_POLY_MAX_DIM]; int64_t constant; } FlowAffineConstraint; typedef struct { double center[FLOW_POLY_MAX_DIM];
    double hessian[FLOW_POLY_MAX_DIM][FLOW_POLY_MAX_DIM]; double gradient[FLOW_POLY_MAX_DIM]; double bound; double semi_axes[FLOW_POLY_MAX_DIM]; bool is_active;
} FlowQuadraticConstraint; typedef struct { size_t dimension, constraint_count; FlowAffineConstraint constraints[FLOW_POLY_MAX_CONSTRAINTS];
    int64_t lower_bounds[FLOW_POLY_MAX_DIM], upper_bounds[FLOW_POLY_MAX_DIM]; size_t quad_constraint_count;
    FlowQuadraticConstraint quad_constraints[FLOW_POLY_MAX_CONSTRAINTS]; } FlowPolyhedron; typedef struct { size_t optimal_tile_size, optimal_simd_width;
    int64_t total_iterations; bool is_parallelizable, is_bounded; double quadratic_recovered_volume; bool has_quadratic_curvature; } FlowPolyhedralSchedule;
int flow_polyhedral_init(FlowPolyhedron *poly, size_t dimension); int flow_polyhedral_add_constraint(FlowPolyhedron *poly, const int64_t *coeffs, int64_t constant);
int flow_polyhedral_add_quadratic_constraint(FlowPolyhedron *poly, const double center[], const double hessian[FLOW_POLY_MAX_DIM][FLOW_POLY_MAX_DIM], const double gradient[], double bound);
int flow_polyhedral_set_box_bounds(FlowPolyhedron *poly, size_t dim_idx, int64_t lower, int64_t upper);
int flow_polyhedral_solve_schedule(const FlowPolyhedron *poly, size_t cache_line_bytes, size_t vector_register_bytes, FlowPolyhedralSchedule *schedule_out);
FlowSMTResult flow_polyhedral_verify_smt(const FlowPolyhedron *poly, const FlowPolyhedralSchedule *sched, FlowSMTProofAttestation *proof_out);
struct FlowJetPotentialLandscape; int flow_polyhedral_synthesize_jet_potential(size_t capacity, size_t threads, struct FlowJetPotentialLandscape *landscape_out);
struct FlowHardwiredPolyhedralTemplate; int flow_polyhedral_export_template(const FlowPolyhedron *poly, struct FlowHardwiredPolyhedralTemplate *tpl_out);
int flow_polyhedral_apply_template_mask(FlowPolyhedron *poly, uint64_t mask);
#define FLOW_POLYTOPE_MAX_CONSTRAINTS 32
#define FLOW_POLYTOPE_MAX_DIMS 16
typedef enum { FLOW_CONSTRAINT_LEQ = 0, FLOW_CONSTRAINT_GEQ = 1, FLOW_CONSTRAINT_EQ  = 2, FLOW_CONSTRAINT_INTERVAL = 3
} FlowConstraintOp; typedef struct { double coefficients[FLOW_POLYTOPE_MAX_DIMS]; FlowConstraintOp op; double rhs_min, rhs_max; char symbolic_tag[64]; } FlowLinearConstraint;
typedef struct { FlowLinearConstraint constraints[FLOW_POLYTOPE_MAX_CONSTRAINTS]; size_t constraint_count, dimension_count;
    double lower_bounds[FLOW_POLYTOPE_MAX_DIMS], upper_bounds[FLOW_POLYTOPE_MAX_DIMS]; } FlowPolyhedronSystem;
void flow_polyhedron_init(FlowPolyhedronSystem *poly, size_t dim_count);
int flow_polyhedron_add_box_bounds(FlowPolyhedronSystem *poly, size_t dim_idx, double min_val, double max_val, const char *tag);
int flow_polyhedron_add_inequality(FlowPolyhedronSystem *poly, const double *coeffs, FlowConstraintOp op, double bound, const char *tag);
int flow_polyhedron_from_ir(const SemanticIR *ir, const Component *comp, const FlowPlanDimensionSet *dims, FlowPolyhedronSystem *poly);
uint64_t flow_polyhedron_project_mask(const FlowPolyhedronSystem *poly, const FlowPlanDimensionSet *dims, uint32_t total_bits);
int flow_polyhedron_from_affine(const FlowPolyhedron *affine_poly, FlowPolyhedronSystem *poly_out);
uint64_t flow_polyhedral_project_to_bitspace(const FlowPolyhedron *poly, const FlowPlanDimensionSet *dims, uint32_t total_bits);
#ifdef __cplusplus
}
#endif
#endif /* FLOW_POLYHEDRAL_H */
