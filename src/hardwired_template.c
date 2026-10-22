#include "hardwired_template.h"
#include <math.h>
#include <string.h>
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
