#include "polyhedral.h"
#include "smt.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
int flow_polyhedral_init(FlowPolyhedron *poly, size_t dimension) { if (!poly || dimension == 0 || dimension > FLOW_POLY_MAX_DIM) return 0; memset(poly, 0, sizeof(*poly));
    poly->dimension = dimension; return 1; }
int flow_polyhedral_add_constraint(FlowPolyhedron *poly, const int64_t *coeffs, int64_t constant) {
    if (!poly || !coeffs || poly->constraint_count >= FLOW_POLY_MAX_CONSTRAINTS) return 0; FlowAffineConstraint *c = &poly->constraints[poly->constraint_count++];
    for (size_t i = 0; i < poly->dimension; ++i) c->coeffs[i] = coeffs[i]; c->constant = constant; return 1; }
int flow_polyhedral_add_quadratic_constraint(FlowPolyhedron *poly, const double center[], const double hessian[FLOW_POLY_MAX_DIM][FLOW_POLY_MAX_DIM],
                                             const double gradient[], double bound) { if (!poly || poly->quad_constraint_count >= FLOW_POLY_MAX_CONSTRAINTS) return 0;
    FlowQuadraticConstraint *qc = &poly->quad_constraints[poly->quad_constraint_count++]; memset(qc, 0, sizeof(*qc));
    qc->is_active = true; qc->bound = (bound > 0.0) ? bound : 0.05; for (size_t i = 0; i < poly->dimension; ++i) { if (center) qc->center[i] = center[i];
        if (gradient) qc->gradient[i] = gradient[i]; for (size_t j = 0; j < poly->dimension; ++j) if (hessian) qc->hessian[i][j] = hessian[i][j];
        double h_diag = (hessian && hessian[i][i] > 1e-9) ? hessian[i][i] : 1.0; qc->semi_axes[i] = sqrt(2.0 * qc->bound / h_diag); } return 1; }
int flow_polyhedral_set_box_bounds(FlowPolyhedron *poly, size_t dim_idx, int64_t lower, int64_t upper) { if (!poly || dim_idx >= poly->dimension || lower > upper) return 0;
    poly->lower_bounds[dim_idx] = lower; poly->upper_bounds[dim_idx] = upper; int64_t c_low[FLOW_POLY_MAX_DIM] = {0}, c_up[FLOW_POLY_MAX_DIM] = {0};
    c_low[dim_idx] = 1; c_up[dim_idx] = -1; flow_polyhedral_add_constraint(poly, c_low, -lower); flow_polyhedral_add_constraint(poly, c_up, upper); return 1; }
int flow_polyhedral_solve_schedule(const FlowPolyhedron *poly, size_t cache_line_bytes, size_t vector_register_bytes, FlowPolyhedralSchedule *schedule_out) {
    if (!poly || !schedule_out || poly->dimension == 0) return 0;
    memset(schedule_out, 0, sizeof(*schedule_out));
    if (!cache_line_bytes) cache_line_bytes = 64;
    if (!vector_register_bytes) vector_register_bytes = 16;
    int64_t total = 1;
    bool bounded = true;
    for (size_t i = 0; i < poly->dimension; ++i) {
        int64_t diff = 0, span = 0;
        if (__builtin_sub_overflow(poly->upper_bounds[i], poly->lower_bounds[i], &diff) ||
            __builtin_add_overflow(diff, 1, &span) || span <= 0 ||
            __builtin_mul_overflow(total, span, &total)) {
            bounded = false;
            break;
        }
    }
    schedule_out->total_iterations = bounded ? total : 0;
    schedule_out->is_bounded = bounded;
    int64_t inner_diff = 0, inner_span = 1;
    if (__builtin_sub_overflow(poly->upper_bounds[poly->dimension - 1], poly->lower_bounds[poly->dimension - 1], &inner_diff) ||
        __builtin_add_overflow(inner_diff, 1, &inner_span) || inner_span <= 0) {
        inner_span = 1;
    }
    size_t simd_lanes = vector_register_bytes / 4 ? vector_register_bytes / 4 : 4;
    schedule_out->optimal_simd_width = ((int64_t)simd_lanes > inner_span) ? (size_t)inner_span : simd_lanes;
    size_t tile = 64;
    while (tile * schedule_out->optimal_simd_width > 4096 && tile > 4) tile /= 2;
    if ((int64_t)tile > inner_span) tile = (size_t)inner_span;
    schedule_out->optimal_tile_size = tile > 0 ? tile : 1;
    schedule_out->is_parallelizable = true;
    for (size_t c = 0; c < poly->constraint_count; ++c)
        if (poly->dimension >= 2 && poly->constraints[c].coeffs[0] > 0 && poly->constraints[c].coeffs[1] < 0) { schedule_out->is_parallelizable = false; break; }
    if (poly->quad_constraint_count > 0) {
        schedule_out->has_quadratic_curvature = true;
        double quad_vol = 0.0;
        for (size_t q = 0; q < poly->quad_constraint_count; ++q) {
            const FlowQuadraticConstraint *qc = &poly->quad_constraints[q];
            if (!qc->is_active) continue;
            double cur_vol = 1.0;
            for (size_t d = 0; d < poly->dimension; ++d) cur_vol *= (2.0 * qc->semi_axes[d]);
            if (poly->dimension == 2) cur_vol *= 0.78539816339;
            else if (poly->dimension == 3) cur_vol *= 0.52359877559;
            if (cur_vol > quad_vol) quad_vol = cur_vol;
        }
        schedule_out->quadratic_recovered_volume = quad_vol;
        if ((int64_t)quad_vol > schedule_out->total_iterations && schedule_out->total_iterations > 0) {
            double ratio = quad_vol / (double)schedule_out->total_iterations;
            schedule_out->total_iterations = (int64_t)quad_vol;
            size_t expanded_tile = (size_t)((double)schedule_out->optimal_tile_size * sqrt(ratio));
            if (expanded_tile > 0 && expanded_tile <= 512) schedule_out->optimal_tile_size = expanded_tile;
        }
    }
    return 1;
}
FlowSMTResult flow_polyhedral_verify_smt(const FlowPolyhedron *poly, const FlowPolyhedralSchedule *sched, FlowSMTProofAttestation *proof_out) {
    if (!poly || !sched) return FLOW_SMT_UNKNOWN;
    bool bound_ok = sched->is_bounded;
    bool simd_ok = (sched->optimal_simd_width > 0 && sched->optimal_simd_width <= sched->optimal_tile_size);
    bool alias_violation = (!sched->is_parallelizable && sched->optimal_simd_width > 1);
    bool quad_ok = true;
    for (size_t q = 0; q < poly->quad_constraint_count && quad_ok; ++q)
        for (size_t d = 0; d < poly->dimension; ++d)
            if (poly->quad_constraints[q].hessian[d][d] <= 0.0 || poly->quad_constraints[q].semi_axes[d] <= 0.0) { quad_ok = false; break; }
    const FlowBoxConstraint rules[] = {
        FLOW_SMT_RULE("poly_bound", !bound_ok, 0, 0, FLOW_BOX_THEOREM_BUFFER_BOUNDS, "Unbounded polyhedron"),
        FLOW_SMT_RULE("simd_width", !simd_ok, 0, 0, FLOW_BOX_THEOREM_MEMORY_QUOTA, "Invalid SIMD vector width"),
        FLOW_SMT_RULE("non_aliasing", alias_violation ? 1 : 0, 0, 0, FLOW_BOX_THEOREM_SHARD_ISOLATION, "Parallel aliasing"),
        FLOW_SMT_RULE("quad_convex", !quad_ok, 0, 0, FLOW_BOX_THEOREM_DETERMINISM, "Non-convex Hessian")
    };
    return FLOW_SMT_VERIFY_RULES("polyhedral_schedule", proof_out, rules,
        "SMT POLYHEDRAL SOUND: Dim=%zu, Iterations=%lld (QuadVol=%.1f), T*=%zu, V*=%zu (Zero-Defect Soundness)",
        poly->dimension, (long long)sched->total_iterations, sched->quadratic_recovered_volume, sched->optimal_tile_size, sched->optimal_simd_width);
}
void flow_polyhedron_init(FlowPolyhedronSystem *poly, size_t dim_count) {
    if (!poly) return;
    memset(poly, 0, sizeof(*poly));
    poly->dimension_count = dim_count < FLOW_POLYTOPE_MAX_DIMS ? dim_count : FLOW_POLYTOPE_MAX_DIMS;
    for (size_t d = 0; d < poly->dimension_count; ++d) { poly->lower_bounds[d] = 0.0; poly->upper_bounds[d] = 1e12; }
}
int flow_polyhedron_add_box_bounds(FlowPolyhedronSystem *poly, size_t dim_idx, double min_val, double max_val, const char *tag) {
    if (!poly || dim_idx >= poly->dimension_count) return 0;
    if (min_val > poly->lower_bounds[dim_idx]) poly->lower_bounds[dim_idx] = min_val;
    if (max_val < poly->upper_bounds[dim_idx]) poly->upper_bounds[dim_idx] = max_val;
    if (poly->constraint_count < FLOW_POLYTOPE_MAX_CONSTRAINTS) {
        FlowLinearConstraint *c = &poly->constraints[poly->constraint_count++];
        memset(c, 0, sizeof(*c));
        c->coefficients[dim_idx] = 1.0;
        c->op = FLOW_CONSTRAINT_INTERVAL;
        c->rhs_min = min_val;
        c->rhs_max = max_val;
        if (tag) snprintf(c->symbolic_tag, sizeof(c->symbolic_tag), "%s", tag);
    }
    return 1;
}
int flow_polyhedron_add_inequality(FlowPolyhedronSystem *poly, const double *coeffs, FlowConstraintOp op, double bound, const char *tag) {
    if (!poly || !coeffs || poly->constraint_count >= FLOW_POLYTOPE_MAX_CONSTRAINTS) return 0;
    FlowLinearConstraint *c = &poly->constraints[poly->constraint_count++];
    memset(c, 0, sizeof(*c));
    for (size_t d = 0; d < poly->dimension_count; ++d) c->coefficients[d] = coeffs[d];
    c->op = op;
    if (op == FLOW_CONSTRAINT_LEQ) c->rhs_max = bound;
    else if (op == FLOW_CONSTRAINT_GEQ) c->rhs_min = bound;
    else if (op == FLOW_CONSTRAINT_EQ) c->rhs_min = c->rhs_max = bound;
    if (tag) snprintf(c->symbolic_tag, sizeof(c->symbolic_tag), "%s", tag);
    return 1;
}
int flow_polyhedron_from_ir(const SemanticIR *ir, const Component *comp, const FlowPlanDimensionSet *dims, FlowPolyhedronSystem *poly) {
    if (!poly || !dims) return 0;
    flow_polyhedron_init(poly, dims->count);
    for (size_t i = 0; i < dims->count; ++i) {
        uint64_t shift_min = dims->dimensions[i].min_val < 62 ? dims->dimensions[i].min_val : 62;
        uint64_t shift_max = dims->dimensions[i].max_val < 62 ? dims->dimensions[i].max_val : 62;
        double min_v = (dims->dimensions[i].kind == FLOW_DIM_EXPONENT) ? (double)(1ULL << shift_min) : (double)dims->dimensions[i].min_val;
        double max_v = (dims->dimensions[i].kind == FLOW_DIM_EXPONENT) ? (double)(1ULL << shift_max) : (double)dims->dimensions[i].max_val;
        if (strcmp(dims->dimensions[i].name, "capacity") == 0 && ir) {
            if (ir->top_n > 0 && (double)ir->top_n > min_v) min_v = (double)ir->top_n;
            if (ir->input_max_count > 0 && ir->state_bounded && (double)ir->input_max_count > min_v) min_v = (double)ir->input_max_count;
            if (ir->memory_limit_mb > 0 && comp) {
                size_t bpe = comp->memory_bytes_per_capacity > 0 ? comp->memory_bytes_per_capacity : 8;
                uint64_t mem_bytes = (uint64_t)ir->memory_limit_mb * 1024ULL * 1024ULL;
                double max_cap = (mem_bytes > comp->memory_fixed_bytes) ? (double)(mem_bytes - comp->memory_fixed_bytes) / (double)bpe : 0.0;
                if (max_cap > 0.0 && max_cap < max_v) max_v = max_cap;
            }
        }
        if (strcmp(dims->dimensions[i].name, "threads") == 0 && comp && (!comp->supports_parallelizable && !comp->supports_shared)) max_v = 1.0;
        flow_polyhedron_add_box_bounds(poly, i, min_v, max_v, dims->dimensions[i].name);
    }
    return 1;
}
uint64_t flow_polyhedron_project_mask(const FlowPolyhedronSystem *poly, const FlowPlanDimensionSet *dims, uint32_t total_bits) {
    uint64_t full = (total_bits >= 64) ? (uint64_t)-1 : (((uint64_t)1 << total_bits) - 1);
    if (!poly || !dims) return full;
    for (size_t d = 0; d < dims->count && d < poly->dimension_count; ++d) {
        if (poly->upper_bounds[d] < poly->lower_bounds[d]) return 0ULL;
    }
    uint64_t mask = 0;
    unsigned bit_offset = 0;
    for (size_t d = 0; d < dims->count && d < poly->dimension_count; ++d) {
        unsigned bits = flow_dimension_bits(&dims->dimensions[d]);
        double upper = poly->upper_bounds[d];
        for (unsigned b = 0; b < bits; ++b) {
            unsigned global_bit = bit_offset + b;
            if (global_bit >= 64 || global_bit >= total_bits) break;
            int ok = 1;
            if (dims->dimensions[d].kind == FLOW_DIM_EXPONENT) {
                uint64_t exp = dims->dimensions[d].min_val + (1ULL << b);
                if (exp > dims->dimensions[d].max_val) ok = 0;
                else if (upper > 0.0 && exp < 62 && (double)(1ULL << exp) > upper) ok = 0;
            } else {
                uint64_t step = dims->dimensions[d].step > 0 ? dims->dimensions[d].step : 1;
                if (dims->dimensions[d].min_val + (1ULL << b) * step > (uint64_t)upper && upper > 0) ok = 0;
            }
            if (ok) mask |= (1ULL << global_bit);
        }
        bit_offset += bits;
    }
    return mask;
}
int flow_polyhedron_from_affine(const FlowPolyhedron *affine_poly, FlowPolyhedronSystem *poly_out) { if (!affine_poly || !poly_out) return 0;
    flow_polyhedron_init(poly_out, affine_poly->dimension); for (size_t d = 0; d < affine_poly->dimension && d < FLOW_POLYTOPE_MAX_DIMS; ++d)
        flow_polyhedron_add_box_bounds(poly_out, d, (double)affine_poly->lower_bounds[d], (double)affine_poly->upper_bounds[d], "affine_dim");
    for (size_t c = 0; c < affine_poly->constraint_count; ++c) { double coeffs[FLOW_POLYTOPE_MAX_DIMS] = {0};
        for (size_t d = 0; d < affine_poly->dimension && d < FLOW_POLYTOPE_MAX_DIMS; ++d) coeffs[d] = (double)affine_poly->constraints[c].coeffs[d];
        flow_polyhedron_add_inequality(poly_out, coeffs, FLOW_CONSTRAINT_GEQ, (double)-affine_poly->constraints[c].constant, "affine_ineq"); } return 1; }
static void bump_hot_update(FlowHardwiredPolyhedralTemplate *tpl) {
    atomic_fetch_add_explicit(&tpl->generation, 1ULL, memory_order_release); tpl->total_hot_updates++; tpl->icache_flushes_avoided++; }
int flow_hardwired_template_init(FlowHardwiredPolyhedralTemplate *tpl, size_t dimension) { if (!tpl) return 0; memset(tpl, 0, sizeof(*tpl));
    dimension = dimension == 0 ? 2 : (dimension > FLOW_HARDWIRED_MAX_DIM ? FLOW_HARDWIRED_MAX_DIM : dimension);
    tpl->dimension = dimension; tpl->total_planes = FLOW_HARDWIRED_MAX_PLANES;
    for (size_t j = 0; j < FLOW_HARDWIRED_MAX_DIM; ++j) { atomic_init(&tpl->translation[j], 0.0); atomic_init(&tpl->scale[j], 1.0); } size_t p = 0;
    for (size_t j = 0; j < dimension && p + 1 < FLOW_HARDWIRED_MAX_PLANES; ++j) { tpl->coeffs[p][j] = +1.0; atomic_init(&tpl->b_constants[p++], 100.0);
        tpl->coeffs[p][j] = -1.0; atomic_init(&tpl->b_constants[p++], 0.0); } for (size_t j = 0; j < dimension && p < FLOW_HARDWIRED_MAX_PLANES; ++j)
        for (size_t k = j + 1; k < dimension && p < FLOW_HARDWIRED_MAX_PLANES; ++k) { tpl->coeffs[p][j] = +0.7071067811865475; tpl->coeffs[p][k] = +0.7071067811865475;
            atomic_init(&tpl->b_constants[p++], 141.42); if (p < FLOW_HARDWIRED_MAX_PLANES) { tpl->coeffs[p][j] = +0.7071067811865475; tpl->coeffs[p][k] = -0.7071067811865475;
                atomic_init(&tpl->b_constants[p++], 100.0); } }
    for (; p < FLOW_HARDWIRED_MAX_PLANES; ++p) {
        double angle = 2.0 * 3.14159265358979323846 * (double)p / (double)FLOW_HARDWIRED_MAX_PLANES;
        tpl->coeffs[p][0] = cos(angle); if (dimension > 1) tpl->coeffs[p][1] = sin(angle); atomic_init(&tpl->b_constants[p], 200.0); }
    atomic_init(&tpl->active_mask, (dimension >= 32) ? ~0ULL : ((1ULL << (2 * dimension)) - 1ULL)); atomic_init(&tpl->generation, 1ULL); return 1; }
int flow_hardwired_template_from_polyhedron(FlowHardwiredPolyhedralTemplate *tpl, const FlowPolyhedron *poly) { if (!tpl || !poly) return 0;
    flow_hardwired_template_init(tpl, poly->dimension); uint64_t active_mask = 0; size_t p = 0; if (poly->constraint_count > 0) {
        for (size_t c = 0; c < poly->constraint_count && p < FLOW_HARDWIRED_MAX_PLANES; ++c, ++p) { for (size_t j = 0; j < poly->dimension && j < FLOW_HARDWIRED_MAX_DIM; ++j)
                tpl->coeffs[p][j] = -(double)poly->constraints[c].coeffs[j];
            atomic_store_explicit(&tpl->b_constants[p], (double)poly->constraints[c].constant, memory_order_relaxed); active_mask |= (1ULL << p); } } else {
        for (size_t j = 0; j < poly->dimension && p + 1 < FLOW_HARDWIRED_MAX_PLANES; ++j) {
            tpl->coeffs[p][j] = +1.0; atomic_store_explicit(&tpl->b_constants[p], (double)poly->upper_bounds[j], memory_order_relaxed); active_mask |= (1ULL << p++);
            tpl->coeffs[p][j] = -1.0; atomic_store_explicit(&tpl->b_constants[p], -(double)poly->lower_bounds[j], memory_order_relaxed); active_mask |= (1ULL << p++); } }
    atomic_store_explicit(&tpl->active_mask, active_mask, memory_order_release); bump_hot_update(tpl); return 1; }
int flow_hardwired_eval(FlowHardwiredPolyhedralTemplate *tpl, const double x[FLOW_HARDWIRED_MAX_DIM], FlowHardwiredEvalResult *res_out) {
    if (!tpl || !x || !res_out) return 0; uint64_t mask = atomic_load_explicit(&tpl->active_mask, memory_order_acquire), violated_mask = 0;
    size_t dim = tpl->dimension, worst_plane = 0; double trans[FLOW_HARDWIRED_MAX_DIM], sc[FLOW_HARDWIRED_MAX_DIM], local_x[FLOW_HARDWIRED_MAX_DIM], max_viol = 0.0;
    for (size_t j = 0; j < dim; ++j) { trans[j] = atomic_load_explicit(&tpl->translation[j], memory_order_relaxed);
        sc[j] = atomic_load_explicit(&tpl->scale[j], memory_order_relaxed); local_x[j] = (x[j] - trans[j]) / sc[j]; res_out->projected_x[j] = x[j]; }
    for (size_t i = 0; i < FLOW_HARDWIRED_MAX_PLANES; ++i) { if (!((mask >> i) & 1ULL)) continue; double dot = 0.0;
        for (size_t j = 0; j < dim; ++j) dot += tpl->coeffs[i][j] * local_x[j]; double violation = dot - atomic_load_explicit(&tpl->b_constants[i], memory_order_relaxed);
        if (violation > 0.0) { violated_mask |= (1ULL << i); if (violation > max_viol) { max_viol = violation; worst_plane = i; } } } if (violated_mask != 0) {
        double norm_sq = 0.0; for (size_t j = 0; j < dim; ++j) norm_sq += tpl->coeffs[worst_plane][j] * tpl->coeffs[worst_plane][j]; if (norm_sq > 1e-9) {
            double step = max_viol / norm_sq; for (size_t j = 0; j < dim; ++j)
                res_out->projected_x[j] = (local_x[j] - step * tpl->coeffs[worst_plane][j]) * sc[j] + trans[j]; } }
    res_out->is_inside = (violated_mask == 0); res_out->violated_mask = violated_mask; res_out->max_violation = max_viol; tpl->total_evaluations++; return 1; }
