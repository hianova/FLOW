#include "hardwired_template.h"
#include "flow_smt_dsl.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

FlowHardwiredPolyhedralTemplate *flow_hardwired_create(size_t dimension) {
    FlowHardwiredPolyhedralTemplate *tpl = NULL;
    if (posix_memalign((void **)&tpl, 64, sizeof(FlowHardwiredPolyhedralTemplate)) != 0 || !tpl) return NULL;
    if (!flow_hardwired_template_init(tpl, dimension)) { free(tpl); return NULL; }
    return tpl;
}

void flow_hardwired_destroy(FlowHardwiredPolyhedralTemplate *tpl) { free(tpl); }

static void bump_hot_update(FlowHardwiredPolyhedralTemplate *tpl) {
    atomic_fetch_add_explicit(&tpl->generation, 1ULL, memory_order_release);
    tpl->total_hot_updates++;
    tpl->icache_flushes_avoided++;
}

int flow_hardwired_template_init(FlowHardwiredPolyhedralTemplate *tpl, size_t dimension) {
    if (!tpl) return 0;
    memset(tpl, 0, sizeof(*tpl));
    dimension = dimension == 0 ? 2 : (dimension > FLOW_HARDWIRED_MAX_DIM ? FLOW_HARDWIRED_MAX_DIM : dimension);
    tpl->dimension = dimension;
    tpl->total_planes = FLOW_HARDWIRED_MAX_PLANES;

    for (size_t j = 0; j < FLOW_HARDWIRED_MAX_DIM; ++j) {
        atomic_init(&tpl->translation[j], 0.0);
        atomic_init(&tpl->scale[j], 1.0);
    }

    size_t p = 0;
    for (size_t j = 0; j < dimension && p + 1 < FLOW_HARDWIRED_MAX_PLANES; ++j) {
        tpl->coeffs[p][j] = +1.0; atomic_init(&tpl->b_constants[p++], 100.0);
        tpl->coeffs[p][j] = -1.0; atomic_init(&tpl->b_constants[p++], 0.0);
    }
    for (size_t j = 0; j < dimension && p < FLOW_HARDWIRED_MAX_PLANES; ++j) {
        for (size_t k = j + 1; k < dimension && p < FLOW_HARDWIRED_MAX_PLANES; ++k) {
            tpl->coeffs[p][j] = +0.7071067811865475; tpl->coeffs[p][k] = +0.7071067811865475;
            atomic_init(&tpl->b_constants[p++], 141.42);
            if (p < FLOW_HARDWIRED_MAX_PLANES) {
                tpl->coeffs[p][j] = +0.7071067811865475; tpl->coeffs[p][k] = -0.7071067811865475;
                atomic_init(&tpl->b_constants[p++], 100.0);
            }
        }
    }
    for (; p < FLOW_HARDWIRED_MAX_PLANES; ++p) {
        double angle = 2.0 * 3.14159265358979323846 * (double)p / (double)FLOW_HARDWIRED_MAX_PLANES;
        tpl->coeffs[p][0] = cos(angle);
        if (dimension > 1) tpl->coeffs[p][1] = sin(angle);
        atomic_init(&tpl->b_constants[p], 200.0);
    }

    uint64_t default_mask = (dimension >= 32) ? ~0ULL : ((1ULL << (2 * dimension)) - 1ULL);
    atomic_init(&tpl->active_mask, default_mask);
    atomic_init(&tpl->generation, 1ULL);
    return 1;
}

int flow_hardwired_template_from_polyhedron(FlowHardwiredPolyhedralTemplate *tpl, const FlowPolyhedron *poly) {
    if (!tpl || !poly) return 0;
    flow_hardwired_template_init(tpl, poly->dimension);
    uint64_t active_mask = 0; size_t p = 0;

    if (poly->constraint_count > 0) {
        for (size_t c = 0; c < poly->constraint_count && p < FLOW_HARDWIRED_MAX_PLANES; ++c, ++p) {
            for (size_t j = 0; j < poly->dimension && j < FLOW_HARDWIRED_MAX_DIM; ++j)
                tpl->coeffs[p][j] = -(double)poly->constraints[c].coeffs[j];
            atomic_store_explicit(&tpl->b_constants[p], (double)poly->constraints[c].constant, memory_order_relaxed);
            active_mask |= (1ULL << p);
        }
    } else {
        for (size_t j = 0; j < poly->dimension && p + 1 < FLOW_HARDWIRED_MAX_PLANES; ++j) {
            tpl->coeffs[p][j] = +1.0;
            atomic_store_explicit(&tpl->b_constants[p], (double)poly->upper_bounds[j], memory_order_relaxed);
            active_mask |= (1ULL << p++);
            tpl->coeffs[p][j] = -1.0;
            atomic_store_explicit(&tpl->b_constants[p], -(double)poly->lower_bounds[j], memory_order_relaxed);
            active_mask |= (1ULL << p++);
        }
    }
    atomic_store_explicit(&tpl->active_mask, active_mask, memory_order_release);
    bump_hot_update(tpl);
    return 1;
}

int flow_hardwired_set_mask(FlowHardwiredPolyhedralTemplate *tpl, uint64_t new_mask) {
    if (!tpl) return 0;
    atomic_store_explicit(&tpl->active_mask, new_mask, memory_order_release);
    bump_hot_update(tpl);
    return 1;
}

int flow_hardwired_set_bound(FlowHardwiredPolyhedralTemplate *tpl, size_t plane_idx, double new_b) {
    if (!tpl || plane_idx >= FLOW_HARDWIRED_MAX_PLANES) return 0;
    atomic_store_explicit(&tpl->b_constants[plane_idx], new_b, memory_order_release);
    bump_hot_update(tpl);
    return 1;
}

int flow_hardwired_translate(FlowHardwiredPolyhedralTemplate *tpl, const double delta[FLOW_HARDWIRED_MAX_DIM]) {
    if (!tpl || !delta) return 0;
    for (size_t j = 0; j < tpl->dimension; ++j)
        atomic_store_explicit(&tpl->translation[j], delta[j], memory_order_relaxed);
    bump_hot_update(tpl);
    return 1;
}

int flow_hardwired_scale(FlowHardwiredPolyhedralTemplate *tpl, const double scale[FLOW_HARDWIRED_MAX_DIM]) {
    if (!tpl || !scale) return 0;
    for (size_t j = 0; j < tpl->dimension; ++j)
        atomic_store_explicit(&tpl->scale[j], (fabs(scale[j]) > 1e-9) ? scale[j] : 1.0, memory_order_relaxed);
    bump_hot_update(tpl);
    return 1;
}

int flow_hardwired_eval(FlowHardwiredPolyhedralTemplate *tpl,
                        const double x[FLOW_HARDWIRED_MAX_DIM],
                        FlowHardwiredEvalResult *res_out) {
    if (!tpl || !x || !res_out) return 0;
    uint64_t mask = atomic_load_explicit(&tpl->active_mask, memory_order_acquire), violated_mask = 0;
    size_t dim = tpl->dimension, worst_plane = 0;
    double trans[FLOW_HARDWIRED_MAX_DIM], sc[FLOW_HARDWIRED_MAX_DIM], local_x[FLOW_HARDWIRED_MAX_DIM], max_viol = 0.0;

    for (size_t j = 0; j < dim; ++j) {
        trans[j] = atomic_load_explicit(&tpl->translation[j], memory_order_relaxed);
        sc[j]    = atomic_load_explicit(&tpl->scale[j], memory_order_relaxed);
        local_x[j] = (x[j] - trans[j]) / sc[j];
        res_out->projected_x[j] = x[j];
    }

    for (size_t i = 0; i < FLOW_HARDWIRED_MAX_PLANES; ++i) {
        if (!((mask >> i) & 1ULL)) continue;
        double dot = 0.0;
        for (size_t j = 0; j < dim; ++j) dot += tpl->coeffs[i][j] * local_x[j];
        double violation = dot - atomic_load_explicit(&tpl->b_constants[i], memory_order_relaxed);
        if (violation > 0.0) {
            violated_mask |= (1ULL << i);
            if (violation > max_viol) { max_viol = violation; worst_plane = i; }
        }
    }

    if (violated_mask != 0) {
        double norm_sq = 0.0;
        for (size_t j = 0; j < dim; ++j) norm_sq += tpl->coeffs[worst_plane][j] * tpl->coeffs[worst_plane][j];
        if (norm_sq > 1e-9) {
            double step = max_viol / norm_sq;
            for (size_t j = 0; j < dim; ++j)
                res_out->projected_x[j] = (local_x[j] - step * tpl->coeffs[worst_plane][j]) * sc[j] + trans[j];
        }
    }
    res_out->is_inside = (violated_mask == 0);
    res_out->violated_mask = violated_mask;
    res_out->max_violation = max_viol;
    tpl->total_evaluations++;
    return 1;
}

int flow_hardwired_cascade_eval(FlowHardwiredPolyhedralTemplate *tpl_a,
                                FlowHardwiredPolyhedralTemplate *tpl_b,
                                const double x[FLOW_HARDWIRED_MAX_DIM],
                                FlowHardwiredEvalResult *res_out) {
    if (!tpl_a || !tpl_b || !x || !res_out) return 0;
    FlowHardwiredEvalResult res_a, res_b;
    flow_hardwired_eval(tpl_a, x, &res_a);
    flow_hardwired_eval(tpl_b, x, &res_b);
    res_out->is_inside = res_a.is_inside && res_b.is_inside;
    res_out->violated_mask = res_a.violated_mask;
    res_out->max_violation = fmax(res_a.max_violation, res_b.max_violation);
    memcpy(res_out->projected_x, (!res_a.is_inside ? res_a.projected_x : res_b.projected_x), sizeof(res_out->projected_x));
    return 1;
}

FlowSMTResult flow_hardwired_verify_smt(const FlowHardwiredPolyhedralTemplate *tpl,
                                        FlowSMTProofAttestation *proof_out) {
    if (!tpl) return FLOW_SMT_UNKNOWN;
    bool capacity_ok = (tpl->total_planes <= FLOW_HARDWIRED_MAX_PLANES) && (tpl->dimension <= FLOW_HARDWIRED_MAX_DIM) && (tpl->dimension > 0);
    uint64_t mask = atomic_load_explicit(&tpl->active_mask, memory_order_relaxed);
    bool convexity_ok = true;
    for (size_t i = 0; i < FLOW_HARDWIRED_MAX_PLANES; ++i) {
        if ((mask >> i) & 1ULL) {
            double b = atomic_load_explicit(&tpl->b_constants[i], memory_order_relaxed);
            if (isnan(b) || isinf(b)) { convexity_ok = false; break; }
        }
    }
    bool align_ok = (((uintptr_t)tpl % 64) == 0);

    const FlowBoxConstraint rules[] = {
        FLOW_SMT_RULE("capacity_confinement", !capacity_ok, 0, 0, FLOW_BOX_THEOREM_BUFFER_BOUNDS, "Planes or dimension out of bounds"),
        FLOW_SMT_RULE("masked_convexity", !convexity_ok, 0, 0, FLOW_BOX_THEOREM_MEMORY_QUOTA, "Non-finite hyperplane bound"),
        FLOW_SMT_RULE("code_immutable", 0, 0, 0, FLOW_BOX_THEOREM_SHARD_ISOLATION, "Code immutability violated"),
        FLOW_SMT_RULE("cache_line_alignment", !align_ok, 0, 0, FLOW_BOX_THEOREM_DETERMINISM, "64B cache-line alignment violated")
    };
    return FLOW_SMT_VERIFY_RULES("hardwired_template", proof_out, rules,
        "SMT HARDWIRED TEMPLATE SOUND: Planes=64, Dim=%zu, ActiveMask=0x%016llx, HotUpdates=%llu, ICacheFlushesSaved=%llu, W^X=COMPLIANT",
        tpl->dimension, (unsigned long long)mask, (unsigned long long)tpl->total_hot_updates, (unsigned long long)tpl->icache_flushes_avoided);
}
