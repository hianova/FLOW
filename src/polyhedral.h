#ifndef FLOW_POLYHEDRAL_H
#define FLOW_POLYHEDRAL_H
#include "flow.h"
#include "plugin.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#ifndef FLOW_SMT_RESULT_DEFINED
#define FLOW_SMT_RESULT_DEFINED
typedef enum { FLOW_SMT_UNINITIALIZED = 0, FLOW_SMT_PROVEN_UNSAT = 1, FLOW_SMT_VIOLATION_SAT = 2, FLOW_SMT_UNKNOWN = 3 } FlowSMTResult;
#endif
struct FlowSMTProofAttestation; typedef struct FlowSMTProofAttestation FlowSMTProofAttestation;
#ifdef __cplusplus
extern "C" {
#endif
#define FLOW_POLY_MAX_DIM 4
#define FLOW_POLY_MAX_CONSTRAINTS 16
typedef struct { int64_t coeffs[FLOW_POLY_MAX_DIM]; int64_t constant; } FlowAffineConstraint;
typedef struct { double center[FLOW_POLY_MAX_DIM], hessian[FLOW_POLY_MAX_DIM][FLOW_POLY_MAX_DIM], gradient[FLOW_POLY_MAX_DIM], bound, semi_axes[FLOW_POLY_MAX_DIM]; bool is_active;
} FlowQuadraticConstraint;
typedef struct { size_t dimension, constraint_count; FlowAffineConstraint constraints[FLOW_POLY_MAX_CONSTRAINTS];
    int64_t lower_bounds[FLOW_POLY_MAX_DIM], upper_bounds[FLOW_POLY_MAX_DIM]; size_t quad_constraint_count; FlowQuadraticConstraint quad_constraints[FLOW_POLY_MAX_CONSTRAINTS]; } FlowPolyhedron;
typedef struct { size_t optimal_tile_size, optimal_simd_width; int64_t total_iterations; bool is_parallelizable, is_bounded; double quadratic_recovered_volume; bool has_quadratic_curvature; } FlowPolyhedralSchedule;
int flow_polyhedral_init(FlowPolyhedron *poly, size_t dimension);
int flow_polyhedral_add_constraint(FlowPolyhedron *poly, const int64_t *coeffs, int64_t constant);
int flow_polyhedral_add_quadratic_constraint(FlowPolyhedron *poly, const double center[], const double hessian[FLOW_POLY_MAX_DIM][FLOW_POLY_MAX_DIM], const double gradient[], double bound);
int flow_polyhedral_set_box_bounds(FlowPolyhedron *poly, size_t dim_idx, int64_t lower, int64_t upper);
int flow_polyhedral_solve_schedule(const FlowPolyhedron *poly, size_t cache_line_bytes, size_t vector_register_bytes, FlowPolyhedralSchedule *schedule_out);
FlowSMTResult flow_polyhedral_verify_smt(const FlowPolyhedron *poly, const FlowPolyhedralSchedule *sched, FlowSMTProofAttestation *proof_out);
#define FLOW_POLYTOPE_MAX_CONSTRAINTS 32
#define FLOW_POLYTOPE_MAX_DIMS 16
typedef enum { FLOW_CONSTRAINT_LEQ = 0, FLOW_CONSTRAINT_GEQ = 1, FLOW_CONSTRAINT_EQ = 2, FLOW_CONSTRAINT_INTERVAL = 3 } FlowConstraintOp;
typedef struct { double coefficients[FLOW_POLYTOPE_MAX_DIMS]; FlowConstraintOp op; double rhs_min, rhs_max; char symbolic_tag[64]; } FlowLinearConstraint;
typedef struct { FlowLinearConstraint constraints[FLOW_POLYTOPE_MAX_CONSTRAINTS]; size_t constraint_count, dimension_count;
    double lower_bounds[FLOW_POLYTOPE_MAX_DIMS], upper_bounds[FLOW_POLYTOPE_MAX_DIMS]; } FlowPolyhedronSystem;
void flow_polyhedron_init(FlowPolyhedronSystem *poly, size_t dim_count);
int flow_polyhedron_add_box_bounds(FlowPolyhedronSystem *poly, size_t dim_idx, double min_val, double max_val, const char *tag);
int flow_polyhedron_add_inequality(FlowPolyhedronSystem *poly, const double *coeffs, FlowConstraintOp op, double bound, const char *tag);
int flow_polyhedron_from_ir(const SemanticIR *ir, const Component *comp, const FlowPlanDimensionSet *dims, FlowPolyhedronSystem *poly);
uint64_t flow_polyhedron_project_mask(const FlowPolyhedronSystem *poly, const FlowPlanDimensionSet *dims, uint32_t total_bits);
int flow_polyhedron_from_affine(const FlowPolyhedron *affine_poly, FlowPolyhedronSystem *poly_out);
#define FLOW_HARDWIRED_MAX_PLANES 64
#define FLOW_HARDWIRED_MAX_DIM    8
typedef struct __attribute__((aligned(64))) FlowHardwiredPolyhedralTemplate {
    double coeffs[FLOW_HARDWIRED_MAX_PLANES][FLOW_HARDWIRED_MAX_DIM]; _Atomic double b_constants[FLOW_HARDWIRED_MAX_PLANES]; _Atomic uint64_t active_mask;
    _Atomic double translation[FLOW_HARDWIRED_MAX_DIM]; _Atomic double scale[FLOW_HARDWIRED_MAX_DIM]; size_t dimension, total_planes; _Atomic uint64_t generation;
    uint64_t total_evaluations, total_hot_updates, icache_flushes_avoided; } FlowHardwiredPolyhedralTemplate;
typedef struct { bool is_inside; uint64_t violated_mask; double max_violation, projected_x[FLOW_HARDWIRED_MAX_DIM]; } FlowHardwiredEvalResult;
int flow_hardwired_template_init(FlowHardwiredPolyhedralTemplate *tpl, size_t dimension);
int flow_hardwired_template_from_polyhedron(FlowHardwiredPolyhedralTemplate *tpl, const FlowPolyhedron *poly);
int flow_hardwired_eval(FlowHardwiredPolyhedralTemplate *tpl, const double x[FLOW_HARDWIRED_MAX_DIM], FlowHardwiredEvalResult *res_out);
#ifdef __cplusplus
}
#endif
#endif /* FLOW_POLYHEDRAL_H */
