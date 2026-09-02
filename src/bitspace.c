#include "bitspace.h"
#include "registry.h"
#include "security.h"
#include "verifier.h"
#include "adaptive.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FNV64_OFFSET UINT64_C(14695981039346656037)
#define FNV64_PRIME  UINT64_C(1099511628211)

static uint64_t hash_bytes(uint64_t hash, const void *ptr, size_t size) {
    const uint8_t *bytes = (const uint8_t *)ptr;
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= FNV64_PRIME;
    }
    return hash;
}

static uint64_t hash_str(uint64_t hash, const char *str) {
    if (str == NULL) return hash;
    return hash_bytes(hash, str, strlen(str));
}

uint64_t flow_compute_contract_hash(const SemanticIR *ir) {
    uint64_t h = FNV64_OFFSET;
    if (ir == NULL) return h;
    h = hash_str(h, ir->flow_name);
    h = hash_str(h, ir->domain_name);
    h = hash_str(h, ir->contract_name);
    h = hash_str(h, ir->output_name);
    h = hash_str(h, ir->resource_name);
    h = hash_str(h, ir->capability_name);
    h = hash_str(h, ir->fallback_policy);
    h = hash_bytes(h, &ir->input_max_count, sizeof(ir->input_max_count));
    h = hash_bytes(h, &ir->top_n, sizeof(ir->top_n));
    h = hash_bytes(h, &ir->memory_limit_mb, sizeof(ir->memory_limit_mb));
    h = hash_bytes(h, &ir->state_shared, sizeof(ir->state_shared));
    h = hash_bytes(h, &ir->state_read_heavy, sizeof(ir->state_read_heavy));
    h = hash_bytes(h, &ir->state_bounded, sizeof(ir->state_bounded));
    h = hash_bytes(h, &ir->flow_parallelizable, sizeof(ir->flow_parallelizable));
    h = hash_bytes(h, &ir->fact_ordered, sizeof(ir->fact_ordered));
    h = hash_bytes(h, &ir->fact_unordered, sizeof(ir->fact_unordered));
    h = hash_bytes(h, &ir->fact_deterministic, sizeof(ir->fact_deterministic));
    h = hash_bytes(h, &ir->fact_range_proven, sizeof(ir->fact_range_proven));
    h = hash_bytes(h, &ir->fact_size_preserved, sizeof(ir->fact_size_preserved));
    h = hash_bytes(h, &ir->fact_mutability_read_only, sizeof(ir->fact_mutability_read_only));
    h = hash_str(h, ir->project_name);
    for (size_t i = 0; i < ir->imported_module_count; ++i) {
        h = hash_str(h, ir->imported_modules[i]);
    }
    h = hash_bytes(h, &ir->declared_constraint_count, sizeof(ir->declared_constraint_count));
    for (size_t i = 0; i < ir->declared_constraint_count; ++i) {
        h = hash_str(h, ir->constraints[i].name);
        h = hash_str(h, ir->constraints[i].operator);
        h = hash_str(h, ir->constraints[i].value);
    }
    return h;
}

uint64_t flow_bitspace_compute_schema_hash(const SemanticIR *ir,
                                           const Component *comp,
                                           const FlowPlanDimensionSet *dims) {
    uint64_t h = flow_compute_contract_hash(ir);
    if (comp != NULL) {
        h = hash_str(h, comp->id);
        h = hash_str(h, comp->kind);
        h = hash_str(h, comp->resource);
        h = hash_str(h, comp->capability);
        h = hash_bytes(h, &comp->memory_fixed_bytes, sizeof(comp->memory_fixed_bytes));
        h = hash_bytes(h, &comp->memory_bytes_per_capacity, sizeof(comp->memory_bytes_per_capacity));
    }
    if (dims != NULL) {
        h = hash_bytes(h, &dims->count, sizeof(dims->count));
        for (size_t i = 0; i < dims->count; ++i) {
            h = hash_str(h, dims->dimensions[i].name);
            h = hash_bytes(h, &dims->dimensions[i].kind, sizeof(dims->dimensions[i].kind));
            h = hash_bytes(h, &dims->dimensions[i].min_val, sizeof(dims->dimensions[i].min_val));
            h = hash_bytes(h, &dims->dimensions[i].max_val, sizeof(dims->dimensions[i].max_val));
            h = hash_bytes(h, &dims->dimensions[i].step, sizeof(dims->dimensions[i].step));
        }
    }
    return h;
}

static unsigned dimension_bits(const FlowPlanDimension *dim) {
    if (dim->kind == FLOW_DIM_BOOLEAN) return 1;
    if (dim->kind == FLOW_DIM_EXPONENT) {
        uint64_t span = dim->max_val >= dim->min_val ? dim->max_val - dim->min_val : 0;
        unsigned b = 1;
        while (((uint64_t)1 << b) <= span && b < 64) ++b;
        return b;
    }
    {
        uint64_t step = dim->step == 0 ? 1 : dim->step;
        uint64_t span = dim->max_val >= dim->min_val ? (dim->max_val - dim->min_val) / step : 0;
        unsigned b = 1;
        while (((uint64_t)1 << b) <= span && b < 64) ++b;
        return b;
    }
}

static uint64_t decode_dim_val(const FlowPlanDimension *dim, uint64_t raw) {
    if (dim->kind == FLOW_DIM_EXPONENT) {
        uint64_t exp = dim->min_val + raw;
        if (exp > dim->max_val) exp = dim->max_val;
        return (uint64_t)1 << exp;
    }
    if (dim->kind == FLOW_DIM_BOOLEAN) {
        return (raw & 1) ? 1 : 0;
    }
    {
        uint64_t step = dim->step == 0 ? 1 : dim->step;
        uint64_t val = dim->min_val + raw * step;
        if (val > dim->max_val) val = dim->max_val;
        return val;
    }
}

static int hierarchical_decode(const FlowBitSpace *space, uint64_t genome, FlowPlan *plan) {
    if (space == NULL || plan == NULL || space->candidate_count == 0) return 0;
    memset(plan, 0, sizeof(*plan));

    size_t cand_idx = 0;
    uint64_t dim_genome = genome;

    if (space->selector_bits > 0 && space->candidate_count > 1) {
        uint64_t mask = (space->selector_bits >= 64) ? (uint64_t)-1 : (((uint64_t)1 << space->selector_bits) - 1);
        cand_idx = (size_t)(genome & mask) % space->candidate_count;
        dim_genome = genome >> space->selector_bits;
    }

    const Component *comp = space->candidates[cand_idx];
    const FlowPlanDimensionSet *dims = &space->candidate_dims[cand_idx];

    plan->component = comp;
    plan->dimension_set = *dims;
    plan->genome = genome;
    plan->bit_count = space->bit_count;
    plan->contract_hash = space->contract_hash;
    plan->schema_hash = flow_bitspace_compute_schema_hash(space->ir, comp, dims);
    plan->assignment.count = dims->count;

    unsigned shift = 0;
    for (size_t i = 0; i < dims->count; ++i) {
        unsigned bits = dimension_bits(&dims->dimensions[i]);
        uint64_t mask = (bits >= 64) ? (uint64_t)-1 : (((uint64_t)1 << bits) - 1);
        uint64_t raw = (dim_genome >> shift) & mask;
        plan->assignment.values[i] = decode_dim_val(&dims->dimensions[i], raw);
        shift += bits;
    }
    return 1;
}

static int hierarchical_evaluate(const FlowBitSpace *space, const FlowPlan *plan, FlowEvaluation *result) {
    FlowPlanMetrics metrics;
    if (space == NULL || plan == NULL || result == NULL || plan->component == NULL) return 0;
    memset(result, 0, sizeof(*result));
    memset(&metrics, 0, sizeof(metrics));

    if (!flow_component_evaluate(space->ir, plan->component, &plan->assignment, &metrics)) {
        return 0;
    }

    result->energy = metrics.energy;
    result->latency_score = metrics.latency_score;
    result->throughput_score = metrics.throughput_score;
    result->memory_bytes = metrics.memory_bytes;
    result->capacity = metrics.capacity;
    result->benchmark_ns = 0;
    result->hard_gate_passed = 1;
    return 1;
}

static int hierarchical_hard_gate_reason(const FlowBitSpace *space, const FlowPlan *plan,
                                         const FlowEvaluation *result, FlowGateFailureReason *reason_out) {
    VerificationReport v_report;
    if (space == NULL || plan == NULL || result == NULL || plan->component == NULL) {
        if (reason_out) *reason_out = FLOW_GATE_FAIL_MUTATION_INVALID;
        return 0;
    }
    if (result->capacity == 0) {
        if (reason_out) *reason_out = FLOW_GATE_FAIL_DIMENSION_BOUND;
        return 0;
    }
    if (space->ir != NULL) {
        if (!component_compatible(space->ir, plan->component)) {
            if (reason_out) *reason_out = FLOW_GATE_FAIL_THREAD_AFFINITY;
            return 0;
        }
        if (space->ir->top_n > 0 && result->capacity < (size_t)space->ir->top_n) {
            if (reason_out) *reason_out = FLOW_GATE_FAIL_DIMENSION_BOUND;
            return 0;
        }
        if (space->ir->memory_limit_mb > 0 &&
            result->memory_bytes > (size_t)space->ir->memory_limit_mb * 1024u * 1024u) {
            if (reason_out) *reason_out = FLOW_GATE_FAIL_MEMORY_LIMIT;
            return 0;
        }
    }
    if (space->ir != NULL && plan->component != NULL) {
        if (!flow_component_verify_plan(space->ir, plan->component, &plan->assignment, &v_report)) {
            if (reason_out) *reason_out = FLOW_GATE_FAIL_VERIFIER_UNPROVEN;
            return 0;
        }
        if (v_report.status == VERIFIER_COMPILE_ERROR) {
            if (reason_out) *reason_out = FLOW_GATE_FAIL_VERIFIER_UNPROVEN;
            return 0;
        }
    }
    if (reason_out) *reason_out = FLOW_GATE_PASS;
    return 1;
}

static int hierarchical_hard_gate(const FlowBitSpace *space, const FlowPlan *plan, const FlowEvaluation *result) {
    return hierarchical_hard_gate_reason(space, plan, result, NULL);
}

const char *flow_gate_failure_name(FlowGateFailureReason reason) {
    switch (reason) {
        case FLOW_GATE_PASS: return "passed";
        case FLOW_GATE_FAIL_MEMORY_LIMIT: return "memory_limit";
        case FLOW_GATE_FAIL_VERIFIER_UNPROVEN: return "verifier_unproven";
        case FLOW_GATE_FAIL_THREAD_AFFINITY: return "thread_affinity";
        case FLOW_GATE_FAIL_QUOTA_EXCEEDED: return "quota_exceeded";
        case FLOW_GATE_FAIL_DIMENSION_BOUND: return "dimension_bound";
        case FLOW_GATE_FAIL_REENTRANCY: return "reentrancy_contract";
        case FLOW_GATE_FAIL_MUTATION_INVALID: return "mutation_invalid";
        default: return "unknown_constraint";
    }
}

/* ========================================================================= */
/* Mathematical Polyhedral Constraint & Hypercube Projection Implementation  */
/* ========================================================================= */

void flow_polyhedron_init(FlowPolyhedronSystem *poly, size_t dim_count) {
    if (poly == NULL) return;
    memset(poly, 0, sizeof(*poly));
    poly->dimension_count = dim_count < FLOW_POLYTOPE_MAX_DIMS ? dim_count : FLOW_POLYTOPE_MAX_DIMS;
    for (size_t d = 0; d < poly->dimension_count; ++d) {
        poly->lower_bounds[d] = 0.0;
        poly->upper_bounds[d] = 1e12;
    }
}

int flow_polyhedron_add_box_bounds(FlowPolyhedronSystem *poly, size_t dim_idx, double min_val, double max_val, const char *tag) {
    if (poly == NULL || dim_idx >= poly->dimension_count) return 0;
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
    if (poly == NULL || coeffs == NULL || poly->constraint_count >= FLOW_POLYTOPE_MAX_CONSTRAINTS) return 0;
    FlowLinearConstraint *c = &poly->constraints[poly->constraint_count++];
    memset(c, 0, sizeof(*c));
    for (size_t d = 0; d < poly->dimension_count; ++d) {
        c->coefficients[d] = coeffs[d];
    }
    c->op = op;
    if (op == FLOW_CONSTRAINT_LEQ) c->rhs_max = bound;
    else if (op == FLOW_CONSTRAINT_GEQ) c->rhs_min = bound;
    else if (op == FLOW_CONSTRAINT_EQ) { c->rhs_min = bound; c->rhs_max = bound; }
    if (tag) snprintf(c->symbolic_tag, sizeof(c->symbolic_tag), "%s", tag);
    return 1;
}

int flow_polyhedron_from_ir(const SemanticIR *ir, const Component *comp, const FlowPlanDimensionSet *dims, FlowPolyhedronSystem *poly) {
    if (poly == NULL || dims == NULL) return 0;
    flow_polyhedron_init(poly, dims->count);

    for (size_t i = 0; i < dims->count; ++i) {
        double min_v = (double)dims->dimensions[i].min_val;
        double max_v = (double)dims->dimensions[i].max_val;

        if (dims->dimensions[i].kind == FLOW_DIM_EXPONENT) {
            min_v = (double)(1ULL << dims->dimensions[i].min_val);
            max_v = (double)(1ULL << dims->dimensions[i].max_val);
        }

        /* 1. Constraint: capacity >= top_n */
        if (strcmp(dims->dimensions[i].name, "capacity") == 0 && ir != NULL && ir->top_n > 0) {
            if ((double)ir->top_n > min_v) min_v = (double)ir->top_n;
        }

        /* 2. Constraint: capacity >= input_max_count */
        if (strcmp(dims->dimensions[i].name, "capacity") == 0 && ir != NULL && ir->input_max_count > 0 && ir->state_bounded) {
            if ((double)ir->input_max_count > min_v) min_v = (double)ir->input_max_count;
        }

        /* 3. Constraint: memory footprint <= memory_limit_mb */
        if (strcmp(dims->dimensions[i].name, "capacity") == 0 && ir != NULL && ir->memory_limit_mb > 0 && comp != NULL) {
            size_t bytes_per_elem = comp->memory_bytes_per_capacity > 0 ? comp->memory_bytes_per_capacity : 8;
            double max_cap_from_mem = (double)(ir->memory_limit_mb * 1024 * 1024 - (int)comp->memory_fixed_bytes) / (double)bytes_per_elem;
            if (max_cap_from_mem > 0.0 && max_cap_from_mem < max_v) max_v = max_cap_from_mem;
        }

        /* 4. Concurrency Constraint: threads == 1 if component does not support parallel/shared */
        if (strcmp(dims->dimensions[i].name, "threads") == 0 && comp != NULL && (!comp->supports_parallelizable && !comp->supports_shared)) {
            max_v = 1.0;
        }

        flow_polyhedron_add_box_bounds(poly, i, min_v, max_v, dims->dimensions[i].name);
    }
    return 1;
}

uint64_t flow_polyhedron_project_mask(const FlowPolyhedronSystem *poly, const FlowPlanDimensionSet *dims, uint32_t total_bits) {
    if (poly == NULL || dims == NULL) return (total_bits >= 64) ? (uint64_t)-1 : (((uint64_t)1 << total_bits) - 1);

    uint64_t projection_mask = 0;
    unsigned bit_offset = 0;

    for (size_t d = 0; d < dims->count && d < poly->dimension_count; ++d) {
        unsigned bits = dimension_bits(&dims->dimensions[d]);
        double upper = poly->upper_bounds[d];

        for (unsigned b = 0; b < bits; ++b) {
            unsigned global_bit = bit_offset + b;
            if (global_bit >= 64 || global_bit >= total_bits) break;

            int bit_feasible = 1;

            if (dims->dimensions[d].kind == FLOW_DIM_EXPONENT) {
                uint64_t bit_weight = (1ULL << b);
                if (dims->dimensions[d].min_val + bit_weight > dims->dimensions[d].max_val) {
                    bit_feasible = 0;
                }
            } else {
                uint64_t step = dims->dimensions[d].step > 0 ? dims->dimensions[d].step : 1;
                uint64_t bit_val = (1ULL << b) * step;
                if (dims->dimensions[d].min_val + bit_val > (uint64_t)upper && upper > 0) {
                    bit_feasible = 0;
                }
            }

            if (bit_feasible) {
                projection_mask |= (1ULL << global_bit);
            }
        }
        bit_offset += bits;
    }

    if (projection_mask == 0) projection_mask = (total_bits >= 64) ? (uint64_t)-1 : (((uint64_t)1 << total_bits) - 1);
    return projection_mask;
}

/* 3-Tier Dynamic Mask Canvas Composition & Polyhedral Superposition */
int flow_mask_canvas_compose(const SemanticIR *ir, const Component *comp,
                            const FlowPlanDimensionSet *dims,
                            const FlowPMUTelemetry *pmu,
                            FlowMaskCanvas *canvas_out) {
    if (canvas_out == NULL) return 0;
    memset(canvas_out, 0, sizeof(*canvas_out));

    /* 1. Hard Constraints (AND / Zero-Defect Polyhedral Manifold) */
    canvas_out->hard_safety_mask = flow_security_get_safety_mask(ir, comp, dims);
    canvas_out->hard_contract_mask = flow_verifier_get_contract_mask(ir, comp, dims);
    canvas_out->hard_resource_mask = flow_verifier_get_resource_mask(ir, comp, dims);
    canvas_out->hard_plugin_mask = flow_component_mutation_mask(ir, comp, dims);

    /* Mathematical Orthogonal Projection of Polyhedral Constraints on {0, 1}^N */
    FlowPolyhedronSystem poly;
    flow_polyhedron_from_ir(ir, comp, dims, &poly);
    canvas_out->hard_polytope_mask = flow_polyhedron_project_mask(&poly, dims, 64);

    canvas_out->hard_composite_mask = canvas_out->hard_safety_mask &
                                      canvas_out->hard_contract_mask &
                                      canvas_out->hard_resource_mask &
                                      canvas_out->hard_plugin_mask &
                                      canvas_out->hard_polytope_mask;

    /* 2. Soft Dynamic Telemetry & Domain Preferences */
    canvas_out->domain_preference_mask = flow_component_preference_mask(ir, comp, dims);
    canvas_out->dynamic_telemetry_bias = flow_adaptive_telemetry_bias_from_pmu(
        pmu, (ir != NULL && (ir->state_shared || ir->state_read_heavy)), dims);

    canvas_out->soft_composite_bias = canvas_out->domain_preference_mask |
                                      canvas_out->dynamic_telemetry_bias;

    /* Project soft bias onto hard manifold */
    if (canvas_out->soft_composite_bias != 0) {
        canvas_out->soft_composite_bias &= canvas_out->hard_composite_mask;
    }
    return 1;
}

void flow_mask_canvas_report(const FlowMaskCanvas *canvas, FILE *out) {
    if (canvas == NULL || out == NULL) return;
    fprintf(out, "Dynamic Mask Canvas Superposition:\n");
    fprintf(out, "  [Tier 1 Hard Safety]      0x%016llx (security gates / race / FD safety)\n",
            (unsigned long long)canvas->hard_safety_mask);
    fprintf(out, "  [Tier 1 Hard Contract]    0x%016llx (semantic IR / parallel contracts)\n",
            (unsigned long long)canvas->hard_contract_mask);
    fprintf(out, "  [Tier 1 Hard Resource]    0x%016llx (memory limits / quotas)\n",
            (unsigned long long)canvas->hard_resource_mask);
    fprintf(out, "  [Tier 1 Hard Plugin]      0x%016llx (plugin domain constraints)\n",
            (unsigned long long)canvas->hard_plugin_mask);
    fprintf(out, "  [Tier 1 Hard Polytope]    0x%016llx (orthogonal projection of {Ax <= b} on {0,1}^N)\n",
            (unsigned long long)canvas->hard_polytope_mask);
    fprintf(out, "  => COMPOSITE HARD MASK    0x%016llx (1-cycle physical early pruning)\n",
            (unsigned long long)canvas->hard_composite_mask);
    fprintf(out, "  [Tier 2 Dynamic PMU Bias] 0x%016llx (hardware telemetry feedback)\n",
            (unsigned long long)canvas->dynamic_telemetry_bias);
    fprintf(out, "  [Tier 3 Domain Preference]0x%016llx (plugin architecture preference)\n",
            (unsigned long long)canvas->domain_preference_mask);
    fprintf(out, "  => COMPOSITE SOFT BIAS    0x%016llx (probability-biasing manifold)\n",
            (unsigned long long)canvas->soft_composite_bias);
}

static uint32_t calc_bits_for_dims(const FlowPlanDimensionSet *dims) {
    uint32_t total = 0;
    for (size_t i = 0; i < dims->count; ++i) {
        total += dimension_bits(&dims->dimensions[i]);
    }
    return total > 0 ? total : 1;
}

int flow_bitspace_init_single(const SemanticIR *ir, const Component *comp, FlowBitSpace *space) {
    if (comp == NULL || space == NULL) return 0;
    memset(space, 0, sizeof(*space));
    space->ir = ir;
    space->contract_hash = flow_compute_contract_hash(ir);
    space->candidate_count = 1;
    space->candidates[0] = comp;
    if (!flow_component_dimensions(ir, comp, &space->candidate_dims[0])) {
        return 0;
    }
    space->candidate_bits[0] = calc_bits_for_dims(&space->candidate_dims[0]);
    flow_mask_canvas_compose(ir, comp, &space->candidate_dims[0], NULL, &space->candidate_canvases[0]);
    space->candidate_masks[0] = space->candidate_canvases[0].hard_composite_mask;
    space->global_canvas = space->candidate_canvases[0];

    space->selector_bits = 0;
    space->bit_count = space->candidate_bits[0];
    if (space->bit_count == 0) space->bit_count = 1;
    if (space->bit_count > 64) space->bit_count = 64;
    space->env_mask = (space->bit_count >= 64) ? (uint64_t)-1 : (((uint64_t)1 << space->bit_count) - 1);
    space->env_mask &= space->candidate_masks[0];

    space->schema_hash = flow_bitspace_compute_schema_hash(ir, comp, &space->candidate_dims[0]);
    space->decode = hierarchical_decode;
    space->evaluate = hierarchical_evaluate;
    space->hard_gate = hierarchical_hard_gate;
    return 1;
}

int flow_bitspace_init_for_ir(const SemanticIR *ir, FlowBitSpace *space) {
    if (ir == NULL || space == NULL) return 0;
    memset(space, 0, sizeof(*space));
    space->ir = ir;
    space->contract_hash = flow_compute_contract_hash(ir);

    size_t comp_count = compatible_component_count(ir);
    if (comp_count == 0) return 0;
    if (comp_count > FLOW_BITSPACE_MAX_CANDIDATES) comp_count = FLOW_BITSPACE_MAX_CANDIDATES;

    space->candidate_count = comp_count;
    uint32_t max_dim_bits = 0;

    for (size_t i = 0; i < comp_count; ++i) {
        const Component *c = compatible_component_at(ir, i);
        space->candidates[i] = c;
        if (!flow_component_dimensions(ir, c, &space->candidate_dims[i])) {
            return 0;
        }
        space->candidate_bits[i] = calc_bits_for_dims(&space->candidate_dims[i]);
        if (space->candidate_bits[i] > max_dim_bits) {
            max_dim_bits = space->candidate_bits[i];
        }
        /* 3-Tier Mask Superposition per candidate */
        flow_mask_canvas_compose(ir, c, &space->candidate_dims[i], NULL, &space->candidate_canvases[i]);
        space->candidate_masks[i] = space->candidate_canvases[i].hard_composite_mask;
    }

    if (comp_count > 1) {
        space->selector_bits = 1;
        while (((size_t)1 << space->selector_bits) < comp_count && space->selector_bits < 16) {
            space->selector_bits++;
        }
    } else {
        space->selector_bits = 0;
    }

    space->bit_count = space->selector_bits + max_dim_bits;
    if (space->bit_count == 0) space->bit_count = 1;
    if (space->bit_count > 64) space->bit_count = 64;

    /* Global Environment Mask & Composite Canvas covering all active bits */
    uint64_t full_mask = (space->bit_count >= 64) ? (uint64_t)-1 : (((uint64_t)1 << space->bit_count) - 1);
    space->env_mask = full_mask;

    if (comp_count == 1) {
        space->global_canvas = space->candidate_canvases[0];
        space->env_mask &= space->candidate_masks[0];
    } else {
        uint64_t sel_mask = (space->selector_bits >= 64) ? (uint64_t)-1 : (((uint64_t)1 << space->selector_bits) - 1);
        uint64_t union_hard = sel_mask;
        uint64_t union_soft = 0;
        for (size_t i = 0; i < comp_count; ++i) {
            union_hard |= (space->candidate_canvases[i].hard_composite_mask << space->selector_bits);
            union_soft |= (space->candidate_canvases[i].soft_composite_bias << space->selector_bits);
        }
        space->global_canvas.hard_composite_mask = union_hard & full_mask;
        space->global_canvas.soft_composite_bias = union_soft & full_mask;
        space->env_mask &= space->global_canvas.hard_composite_mask;
    }

    space->schema_hash = flow_bitspace_compute_schema_hash(ir, space->candidates[0], &space->candidate_dims[0]);
    space->decode = hierarchical_decode;
    space->evaluate = hierarchical_evaluate;
    space->hard_gate = hierarchical_hard_gate;
    return 1;
}

static uint64_t xorshift64(uint64_t *state) {
    uint64_t x = *state;
    if (x == 0) x = UINT64_C(0x9e3779b97f4a7c15);
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

/* ========================================================================= */
/* 1024-Bit Bitset Array Genome Operations (O(1) 1-Bit Chaotic Mutation)     */
/* ========================================================================= */

void flow_genome_init(FlowGenome *g, uint32_t total_bits) {
    if (g == NULL) return;
    memset(g, 0, sizeof(*g));
    g->total_bits = (total_bits > 0 && total_bits <= (FLOW_GENOME_MAX_WORDS * 64)) ? total_bits : 64;
    g->active_words = (g->total_bits + 63) / 64;
    if (g->active_words == 0) g->active_words = 1;
    if (g->active_words > FLOW_GENOME_MAX_WORDS) g->active_words = FLOW_GENOME_MAX_WORDS;
}

void flow_genome_from_u64(FlowGenome *g, uint64_t val, uint32_t total_bits) {
    flow_genome_init(g, total_bits);
    g->words[0] = val;
}

uint64_t flow_genome_to_u64(const FlowGenome *g) {
    if (g == NULL) return 0;
    return g->words[0];
}

int flow_genome_get_bit(const FlowGenome *g, uint32_t bit_idx) {
    if (g == NULL || bit_idx >= g->total_bits) return 0;
    uint32_t word_idx = bit_idx / 64;
    uint32_t offset = bit_idx % 64;
    if (word_idx >= FLOW_GENOME_MAX_WORDS) return 0;
    return (int)((g->words[word_idx] >> offset) & 1);
}

void flow_genome_set_bit(FlowGenome *g, uint32_t bit_idx, int val) {
    if (g == NULL || bit_idx >= g->total_bits) return;
    uint32_t word_idx = bit_idx / 64;
    uint32_t offset = bit_idx % 64;
    if (word_idx >= FLOW_GENOME_MAX_WORDS) return;
    if (val) {
        g->words[word_idx] |= (UINT64_C(1) << offset);
    } else {
        g->words[word_idx] &= ~(UINT64_C(1) << offset);
    }
}

void flow_genome_flip_bit(FlowGenome *g, uint32_t bit_idx) {
    if (g == NULL || bit_idx >= g->total_bits) return;
    uint32_t word_idx = bit_idx / 64;
    uint32_t offset = bit_idx % 64;
    if (word_idx >= FLOW_GENOME_MAX_WORDS) return;
    g->words[word_idx] ^= (UINT64_C(1) << offset);
}

void flow_genome_mutate_1bit(FlowGenome *g, uint64_t *rng_state, uint32_t *mutated_bit_out) {
    if (g == NULL || g->total_bits == 0) return;
    uint64_t rnd = xorshift64(rng_state);
    uint32_t bit_idx = (uint32_t)(rnd % (uint64_t)g->total_bits);
    flow_genome_flip_bit(g, bit_idx);
    if (mutated_bit_out != NULL) *mutated_bit_out = bit_idx;
}

void flow_linkage_map_init(FlowGeneLinkageMap *map) {
    if (map == NULL) return;
    memset(map, 0, sizeof(*map));
}

int flow_linkage_map_add_group(FlowGeneLinkageMap *map, const uint32_t *bits, size_t count, const char *rationale) {
    if (map == NULL || bits == NULL || count == 0 || count > FLOW_MAX_LINKED_BITS) return 0;
    if (map->group_count >= FLOW_MAX_LINKAGE_GROUPS) return 0;
    FlowGeneLinkageGroup *grp = &map->groups[map->group_count++];
    grp->bit_count = count;
    for (size_t i = 0; i < count; ++i) {
        grp->bit_indices[i] = bits[i];
    }
    if (rationale) strncpy(grp->rationale, rationale, sizeof(grp->rationale) - 1);
    return 1;
}

void flow_genome_mutate_with_linkage(FlowGenome *g, const FlowGeneLinkageMap *linkage,
                                    uint64_t *rng_state, uint32_t *primary_bit_out, size_t *linked_flips_out) {
    if (g == NULL || g->total_bits == 0) return;
    uint64_t rnd = xorshift64(rng_state);
    uint32_t bit_idx = (uint32_t)(rnd % (uint64_t)g->total_bits);
    flow_genome_flip_bit(g, bit_idx);
    if (primary_bit_out != NULL) *primary_bit_out = bit_idx;

    size_t flips = 1;
    if (linkage != NULL) {
        for (size_t grp_idx = 0; grp_idx < linkage->group_count; ++grp_idx) {
            const FlowGeneLinkageGroup *grp = &linkage->groups[grp_idx];
            int is_member = 0;
            for (size_t i = 0; i < grp->bit_count; ++i) {
                if (grp->bit_indices[i] == bit_idx) {
                    is_member = 1;
                    break;
                }
            }
            if (is_member) {
                for (size_t i = 0; i < grp->bit_count; ++i) {
                    if (grp->bit_indices[i] != bit_idx) {
                        flow_genome_flip_bit(g, grp->bit_indices[i]);
                        flips++;
                    }
                }
                break;
            }
        }
    }
    if (linked_flips_out != NULL) *linked_flips_out = flips;
}

int flow_genome_equals(const FlowGenome *a, const FlowGenome *b) {
    if (a == NULL || b == NULL) return 0;
    if (a->total_bits != b->total_bits) return 0;
    uint32_t words = a->active_words;
    for (uint32_t i = 0; i < words; ++i) {
        if (a->words[i] != b->words[i]) return 0;
    }
    return 1;
}

uint64_t flow_bitspace_mutate_1bit_superposed(const FlowBitSpace *space, uint64_t genome,
                                             const FlowMaskCanvas *canvas, double bias_weight,
                                             uint64_t *rng_state, uint32_t *mutated_bit_out) {
    uint32_t bits = (space != NULL && space->bit_count > 0) ? space->bit_count : 16;
    if (bits > 64) bits = 64;

    uint64_t hard_mask = (canvas != NULL && canvas->hard_composite_mask != 0) ?
                         canvas->hard_composite_mask :
                         ((space != NULL && space->env_mask != 0) ? space->env_mask : (uint64_t)-1);
    if (space != NULL && space->env_mask != 0) {
        hard_mask &= space->env_mask;
    }

    uint64_t soft_bias = (canvas != NULL) ? canvas->soft_composite_bias : 0;
    soft_bias &= hard_mask;

    uint64_t rnd = xorshift64(rng_state);

    if (bias_weight > 0.0 && soft_bias != 0) {
        uint32_t soft_indices[64];
        size_t soft_count = 0;
        for (uint32_t b = 0; b < bits; ++b) {
            if (soft_bias & (UINT64_C(1) << b)) {
                soft_indices[soft_count++] = b;
            }
        }
        uint32_t p_val = (uint32_t)(rnd & 0xFFFF);
        uint32_t threshold = (uint32_t)(bias_weight * 65536.0);
        if (soft_count > 0 && p_val < threshold) {
            uint32_t chosen_bit = soft_indices[(rnd >> 16) % soft_count];
            if (mutated_bit_out != NULL) *mutated_bit_out = chosen_bit;
            return genome ^ (UINT64_C(1) << chosen_bit);
        }
    }

    /* Standard Hard-Masked 1-Bit Mutation (Zero-Overhead Early Pruning) */
    uint32_t bit = (uint32_t)((rnd >> 16) % bits);
    uint64_t mutation_bit = (UINT64_C(1) << bit);

    if (mutation_bit & hard_mask) {
        if (mutated_bit_out != NULL) *mutated_bit_out = bit;
        return genome ^ mutation_bit;
    }

    if (mutated_bit_out != NULL) *mutated_bit_out = 0xFFFFFFFF;
    return genome;
}

uint64_t flow_bitspace_mutate_1bit_masked(const FlowBitSpace *space, uint64_t genome,
                                          uint64_t env_mask, uint64_t *rng_state,
                                          uint32_t *mutated_bit_out) {
    FlowMaskCanvas canvas;
    memset(&canvas, 0, sizeof(canvas));
    canvas.hard_composite_mask = env_mask;
    return flow_bitspace_mutate_1bit_superposed(space, genome, &canvas, 0.0, rng_state, mutated_bit_out);
}

uint64_t flow_bitspace_mutate_1bit(const FlowBitSpace *space, uint64_t genome,
                                   uint64_t *rng_state, uint32_t *mutated_bit_out) {
    return flow_bitspace_mutate_1bit_masked(space, genome, (uint64_t)-1, rng_state, mutated_bit_out);
}

static void pareto_update_bitspace(FlowBitSearchResult *res, const FlowPlan *plan) {
    if (res == NULL || plan == NULL || !plan->eval.hard_gate_passed) return;
    size_t i, j;
    double lat = plan->eval.latency_score;
    double mem = (double)plan->eval.memory_bytes;

    if (res->pareto_count == 0) {
        res->pareto_points[0] = *plan;
        res->pareto_count = 1;
        res->best_latency = lat;
        res->best_memory = mem;
        return;
    }
    if (lat < res->best_latency) res->best_latency = lat;
    if (mem < res->best_memory) res->best_memory = mem;

    for (i = 0; i < res->pareto_count; ++i) {
        double pi_lat = res->pareto_points[i].eval.latency_score;
        double pi_mem = (double)res->pareto_points[i].eval.memory_bytes;
        if (pi_lat <= lat && pi_mem <= mem && (pi_lat < lat || pi_mem < mem)) {
            return; /* dominated */
        }
    }

    /* remove points dominated by new plan */
    j = 0;
    for (i = 0; i < res->pareto_count; ++i) {
        double pi_lat = res->pareto_points[i].eval.latency_score;
        double pi_mem = (double)res->pareto_points[i].eval.memory_bytes;
        if (!(lat <= pi_lat && mem <= pi_mem && (lat < pi_lat || mem < pi_mem))) {
            res->pareto_points[j++] = res->pareto_points[i];
        }
    }
    res->pareto_count = j;
    if (res->pareto_count < FLOW_PARETO_MAX) {
        res->pareto_points[res->pareto_count++] = *plan;
    }
}

uint64_t flow_bitspace_encode(const FlowBitSpace *space, size_t cand_idx, const FlowPlanAssignment *plan) {
    if (space == NULL || plan == NULL || cand_idx >= space->candidate_count) return 0;
    uint64_t genome = (uint64_t)cand_idx;
    const FlowPlanDimensionSet *dims = &space->candidate_dims[cand_idx];
    unsigned shift = space->selector_bits;

    for (size_t i = 0; i < dims->count && i < plan->count; ++i) {
        const FlowPlanDimension *d = &dims->dimensions[i];
        unsigned bits = dimension_bits(d);
        uint64_t val = plan->values[i];
        uint64_t raw = 0;

        if (d->kind == FLOW_DIM_EXPONENT) {
            uint64_t exp = d->min_val;
            while (((uint64_t)1 << exp) < val && exp < d->max_val) exp++;
            raw = exp >= d->min_val ? exp - d->min_val : 0;
        } else if (d->kind == FLOW_DIM_BOOLEAN) {
            raw = val & 1;
        } else {
            uint64_t step = d->step == 0 ? 1 : d->step;
            raw = val >= d->min_val ? (val - d->min_val) / step : 0;
        }

        uint64_t mask = (bits >= 64) ? (uint64_t)-1 : (((uint64_t)1 << bits) - 1);
        genome |= (raw & mask) << shift;
        shift += bits;
    }
    return genome;
}

uint64_t flow_bitspace_default_genome(const FlowBitSpace *space) {
    if (space == NULL || space->candidate_count == 0) return 0;
    size_t cand_idx = 0;
    if (space->ir != NULL) {
        const Component *heur = select_component(space->ir);
        if (heur != NULL) {
            for (size_t c = 0; c < space->candidate_count; ++c) {
                if (space->candidates[c] == heur) {
                    cand_idx = c;
                    break;
                }
            }
        }
    }

    uint64_t genome = (uint64_t)cand_idx;
    const FlowPlanDimensionSet *dims = &space->candidate_dims[cand_idx];
    unsigned shift = space->selector_bits;

    for (size_t i = 0; i < dims->count; ++i) {
        const FlowPlanDimension *d = &dims->dimensions[i];
        unsigned bits = dimension_bits(d);
        uint64_t val = d->default_val;

        if (space->ir != NULL) {
            if (strcmp(d->name, "capacity") == 0) {
                size_t req_cap = (size_t)space->ir->input_max_count;
                if (req_cap > 4096) req_cap = 4096;
                if (space->ir->top_n > 0 && req_cap < (size_t)space->ir->top_n) {
                    req_cap = (size_t)space->ir->top_n;
                }
                if (d->kind == FLOW_DIM_EXPONENT) {
                    uint64_t exp = d->min_val;
                    while (((uint64_t)1 << exp) < req_cap && exp < d->max_val) exp++;
                    val = exp;
                } else {
                    val = req_cap;
                }
            } else if (strcmp(d->name, "threads") == 0) {
                if (d->kind == FLOW_DIM_EXPONENT) {
                    uint64_t exp = 0;
                    if (space->ir->flow_parallelizable) exp = 2;
                    else if (space->ir->state_shared) exp = 4;
                    if (exp < d->min_val) exp = d->min_val;
                    if (exp > d->max_val) exp = d->max_val;
                    val = exp;
                }
            } else if (strcmp(d->name, "shards") == 0) {
                if (d->kind == FLOW_DIM_EXPONENT) {
                    uint64_t exp = space->ir->state_shared ? 4 : 0;
                    if (exp < d->min_val) exp = d->min_val;
                    if (exp > d->max_val) exp = d->max_val;
                    val = exp;
                }
            }
        }

        uint64_t raw = 0;
        if (d->kind == FLOW_DIM_EXPONENT) {
            raw = val >= d->min_val ? val - d->min_val : 0;
        } else if (d->kind == FLOW_DIM_BOOLEAN) {
            raw = val & 1;
        } else {
            uint64_t step = d->step == 0 ? 1 : d->step;
            raw = val >= d->min_val ? (val - d->min_val) / step : 0;
        }

        uint64_t mask = (bits >= 64) ? (uint64_t)-1 : (((uint64_t)1 << bits) - 1);
        genome |= (raw & mask) << shift;
        shift += bits;
    }
    return genome;
}

double flow_bitspace_calculate_transition_penalty(const FlowTransitionCostModel *model,
                                                  const FlowPlan *candidate,
                                                  int *is_structural_out) {
    if (model == NULL || !model->has_active_baseline || model->baseline_plan == NULL || candidate == NULL) {
        if (is_structural_out) *is_structural_out = 0;
        return 0.0;
    }

    int is_structural = 0;
    double dimension_migration_cost = 0.0;

    /* Check 1: Component changed (Macro Structural Transition) */
    if (model->baseline_plan->component != candidate->component) {
        is_structural = 1;
        dimension_migration_cost += 500.0;
    } else {
        /* Check 2: Inspect individual dimensions for Plugin-declared semantics */
        for (size_t i = 0; i < candidate->dimension_set.count && i < candidate->assignment.count; ++i) {
            uint64_t cand_val = candidate->assignment.values[i];
            uint64_t base_val = 0;
            int found_base = 0;

            for (size_t j = 0; j < model->baseline_plan->dimension_set.count; ++j) {
                if (strcmp(model->baseline_plan->dimension_set.dimensions[j].name,
                           candidate->dimension_set.dimensions[i].name) == 0) {
                    base_val = model->baseline_plan->assignment.values[j];
                    found_base = 1;
                    break;
                }
            }

            if (found_base && base_val != cand_val) {
                const FlowPlanDimension *d = &candidate->dimension_set.dimensions[i];
                if (d->dim_class == FLOW_DIM_CLASS_STRUCTURAL_JIT) {
                    is_structural = 1;
                    dimension_migration_cost += (d->base_migration_cost_ns > 0) ? (double)d->base_migration_cost_ns : 200.0;
                }
            }
        }
    }

    if (is_structural_out) *is_structural_out = is_structural;

    if (!is_structural) {
        /* Pure tactile parameter mutation: 0 transition penalty */
        return 0.0;
    }

    double jit_penalty = (model->jit_penalty_energy > 0.0) ? model->jit_penalty_energy : (50.0 + dimension_migration_cost);
    double bw_cost_per_byte = model->bandwidth_cost_per_byte > 0.0 ? model->bandwidth_cost_per_byte : 0.0001;
    double total_migration_cost = jit_penalty + (double)model->live_state_bytes * bw_cost_per_byte;

    size_t horizon = model->horizon_calls > 0 ? model->horizon_calls : 1000;
    return total_migration_cost / (double)horizon;
}

int flow_bitspace_search_two_tier(const FlowBitSpace *space,
                                  const FlowTwoTierChaosConfig *config,
                                  uint32_t seed, int measured,
                                  const FlowTransitionCostModel *transition_model,
                                  FlowBitSearchResult *result_out) {
    if (space == NULL || result_out == NULL || space->candidate_count == 0) return 0;
    memset(result_out, 0, sizeof(*result_out));

    size_t macro_cycles = (config != NULL && config->macro_cycles > 0) ? config->macro_cycles : 5;
    size_t micro_steps = (config != NULL && config->micro_steps_per_cycle > 0) ? config->micro_steps_per_cycle : 20;
    double macro_prob = (config != NULL && config->macro_tunneling_prob > 0.0) ? config->macro_tunneling_prob : 0.15;
    size_t stagnation_limit = (config != NULL && config->plateau_stagnation_limit > 0) ? config->plateau_stagnation_limit : 8;

    result_out->iterations = macro_cycles * micro_steps;
    result_out->seed = seed;
    result_out->measured = measured;

    uint64_t rng = seed == 0 ? UINT64_C(0x123456789abcdef0) : (uint64_t)seed;
    uint64_t current_genome = (transition_model != NULL && transition_model->has_active_baseline && transition_model->baseline_plan != NULL)
                              ? transition_model->baseline_plan->genome
                              : flow_bitspace_default_genome(space);
    FlowPlan current_plan;

    space->decode(space, current_genome, &current_plan);
    space->evaluate(space, &current_plan, &current_plan.eval);
    FlowGateFailureReason seed_reason = FLOW_GATE_PASS;
    current_plan.eval.hard_gate_passed = hierarchical_hard_gate_reason(space, &current_plan, &current_plan.eval, &seed_reason);
    result_out->heatmap.total_mutations++;
    if (!current_plan.eval.hard_gate_passed) {
        current_plan.eval.energy += 1.0e12;
        result_out->heatmap.total_failures++;
        result_out->heatmap.failure_counts[seed_reason]++;
    } else {
        if (measured && space->ir != NULL && current_plan.component != NULL) {
            current_plan.eval.benchmark_ns = flow_component_benchmark(space->ir, current_plan.component, &current_plan.assignment);
            if (current_plan.eval.benchmark_ns > 0) {
                current_plan.eval.energy += (double)current_plan.eval.benchmark_ns / 1000.0;
            }
        }
        if (transition_model != NULL && transition_model->has_active_baseline) {
            current_plan.eval.energy += flow_bitspace_calculate_transition_penalty(transition_model, &current_plan, NULL);
        }
    }

    FlowPlan best_plan = current_plan;
    if (best_plan.eval.hard_gate_passed) {
        pareto_update_bitspace(result_out, &best_plan);
    }

    /* Two-Tier Nested Chaos Engine Execution */
    for (size_t macro = 0; macro < macro_cycles; ++macro) {
        /* Outer Tier: Macro Phase Jump / Correlated Multi-Bit Tunneling */
        if (macro > 0) {
            uint64_t base_genome = best_plan.eval.hard_gate_passed ? best_plan.genome : current_genome;
            double p = (double)(xorshift64(&rng) % 10000) / 10000.0;
            if (p < macro_prob) {
                /* 2-Bit Correlated Quantum Leap (Escaping Epistasis / Hamming-2 Saddle Barrier) */
                uint32_t b1 = 0, b2 = 0;
                uint64_t leaped_genome = flow_bitspace_mutate_1bit(space, base_genome, &rng, &b1);
                leaped_genome = flow_bitspace_mutate_1bit(space, leaped_genome, &rng, &b2);
                current_genome = leaped_genome;
            } else if (space->selector_bits > 0 && space->candidate_count > 1) {
                /* Subspace Phase Shift (Jump across component manifolds) */
                uint64_t mask = (space->selector_bits >= 64) ? (uint64_t)-1 : (((uint64_t)1 << space->selector_bits) - 1);
                uint64_t next_cand = (xorshift64(&rng) % space->candidate_count);
                current_genome = (base_genome & ~mask) | (next_cand & mask);
            } else {
                current_genome = base_genome;
            }
            space->decode(space, current_genome, &current_plan);
            space->evaluate(space, &current_plan, &current_plan.eval);
            FlowGateFailureReason jump_reason = FLOW_GATE_PASS;
            current_plan.eval.hard_gate_passed = hierarchical_hard_gate_reason(space, &current_plan, &current_plan.eval, &jump_reason);
            if (!current_plan.eval.hard_gate_passed) {
                current_plan.eval.energy += 1.0e12;
            } else {
                if (measured && space->ir != NULL && current_plan.component != NULL) {
                    current_plan.eval.benchmark_ns = flow_component_benchmark(space->ir, current_plan.component, &current_plan.assignment);
                    if (current_plan.eval.benchmark_ns > 0) {
                        current_plan.eval.energy += (double)current_plan.eval.benchmark_ns / 1000.0;
                    }
                }
                if (transition_model != NULL && transition_model->has_active_baseline) {
                    current_plan.eval.energy += flow_bitspace_calculate_transition_penalty(transition_model, &current_plan, NULL);
                }
                if (current_plan.eval.energy < best_plan.eval.energy) {
                    best_plan = current_plan;
                    pareto_update_bitspace(result_out, &best_plan);
                }
            }
        }

        double temp_start = 100.0 / (1.0 + 0.8 * (double)macro);
        double temp_decay = 0.98;
        double temp = temp_start;
        size_t stagnation_count = 0;

        /* Inner Tier: Micro 1-Bit Markovian Relaxation */
        for (size_t micro = 0; micro < micro_steps; ++micro) {
            uint64_t cand_genome = flow_bitspace_mutate_1bit(space, current_genome, &rng, NULL);
            FlowPlan cand_plan;
            space->decode(space, cand_genome, &cand_plan);
            space->evaluate(space, &cand_plan, &cand_plan.eval);
            FlowGateFailureReason cand_reason = FLOW_GATE_PASS;
            cand_plan.eval.hard_gate_passed = hierarchical_hard_gate_reason(space, &cand_plan, &cand_plan.eval, &cand_reason);
            result_out->heatmap.total_mutations++;

            if (!cand_plan.eval.hard_gate_passed) {
                cand_plan.eval.energy += 1.0e12;
                result_out->heatmap.total_failures++;
                result_out->heatmap.failure_counts[cand_reason]++;
            } else {
                if (measured && space->ir != NULL && cand_plan.component != NULL) {
                    cand_plan.eval.benchmark_ns = flow_component_benchmark(space->ir, cand_plan.component, &cand_plan.assignment);
                    if (cand_plan.eval.benchmark_ns > 0) {
                        cand_plan.eval.energy += (double)cand_plan.eval.benchmark_ns / 1000.0;
                    }
                }
                if (transition_model != NULL && transition_model->has_active_baseline) {
                    cand_plan.eval.energy += flow_bitspace_calculate_transition_penalty(transition_model, &cand_plan, NULL);
                }
                pareto_update_bitspace(result_out, &cand_plan);
            }

            double delta = cand_plan.eval.energy - current_plan.eval.energy;
            double r = (double)(xorshift64(&rng) % 10000) / 10000.0;
            if (delta < 0.0 || (temp > 0.001 && r < exp(-delta / temp))) {
                current_genome = cand_genome;
                current_plan = cand_plan;
                if (cand_plan.eval.hard_gate_passed &&
                    (!best_plan.eval.hard_gate_passed || cand_plan.eval.energy < best_plan.eval.energy)) {
                    best_plan = cand_plan;
                    stagnation_count = 0;
                }
            } else {
                stagnation_count++;
                if (stagnation_count >= stagnation_limit) {
                    /* Plateau detected: trigger inner thermal burst */
                    temp = temp_start * 0.5;
                    stagnation_count = 0;
                }
            }
            temp *= temp_decay;
        }
    }

    /* Search Protocol Step 2: Measured Recheck & Repeated Benchmark */
    if (measured && best_plan.component != NULL && best_plan.eval.hard_gate_passed && space->ir != NULL) {
        uint64_t bench_sum = 0;
        const int bench_runs = 3;
        for (int b = 0; b < bench_runs; ++b) {
            bench_sum += flow_component_benchmark(space->ir, best_plan.component, &best_plan.assignment);
        }
        best_plan.eval.benchmark_ns = bench_sum / bench_runs;
    }

    /* Search Protocol Step 3: Held-out workload boundary verification */
    if (space->ir != NULL && best_plan.component != NULL && best_plan.eval.hard_gate_passed) {
        SemanticIR held_out_ir = *space->ir;
        held_out_ir.input_max_count = (int)((double)held_out_ir.input_max_count * 1.25);
        if (held_out_ir.input_max_count < space->ir->input_max_count) {
            held_out_ir.input_max_count = space->ir->input_max_count + 100;
        }
        VerificationReport held_out_report;
        if (!flow_component_verify_plan(&held_out_ir, best_plan.component, &best_plan.assignment, &held_out_report) ||
            held_out_report.status == VERIFIER_COMPILE_ERROR) {
            snprintf(best_plan.eval.message, sizeof(best_plan.eval.message),
                     "held-out check: %s", held_out_report.message);
        }
    }

    result_out->best_plan = best_plan;
    if (result_out->best_latency > 0.0) {
        result_out->latency_regret_percent =
            ((best_plan.eval.latency_score - result_out->best_latency) / result_out->best_latency) * 100.0;
    }
    if (result_out->best_memory > 0.0) {
        result_out->memory_regret_percent =
            (((double)best_plan.eval.memory_bytes - result_out->best_memory) / result_out->best_memory) * 100.0;
    }
    return best_plan.eval.hard_gate_passed ? 1 : 0;
}

int flow_bitspace_search_single_tier(const FlowBitSpace *space, size_t iterations, uint32_t seed,
                                     int measured, const FlowTransitionCostModel *transition_model,
                                     FlowBitSearchResult *result_out) {
    if (space == NULL || result_out == NULL || space->candidate_count == 0) return 0;
    memset(result_out, 0, sizeof(*result_out));
    result_out->iterations = iterations == 0 ? 1 : iterations;
    result_out->seed = seed;
    result_out->measured = measured;

    uint64_t rng = seed == 0 ? UINT64_C(0x123456789abcdef0) : (uint64_t)seed;
    uint64_t current_genome = (transition_model != NULL && transition_model->has_active_baseline && transition_model->baseline_plan != NULL)
                              ? transition_model->baseline_plan->genome
                              : flow_bitspace_default_genome(space);
    FlowPlan current_plan;

    space->decode(space, current_genome, &current_plan);
    space->evaluate(space, &current_plan, &current_plan.eval);
    FlowGateFailureReason seed_reason = FLOW_GATE_PASS;
    current_plan.eval.hard_gate_passed = hierarchical_hard_gate_reason(space, &current_plan, &current_plan.eval, &seed_reason);
    result_out->heatmap.total_mutations++;
    if (!current_plan.eval.hard_gate_passed) {
        current_plan.eval.energy += 1.0e12;
        result_out->heatmap.total_failures++;
        result_out->heatmap.failure_counts[seed_reason]++;
    } else {
        if (measured && space->ir != NULL && current_plan.component != NULL) {
            current_plan.eval.benchmark_ns = flow_component_benchmark(space->ir, current_plan.component, &current_plan.assignment);
            if (current_plan.eval.benchmark_ns > 0) {
                current_plan.eval.energy += (double)current_plan.eval.benchmark_ns / 1000.0;
            }
        }
    }

    FlowPlan best_plan = current_plan;
    if (best_plan.eval.hard_gate_passed) {
        pareto_update_bitspace(result_out, &best_plan);
    }

    double temp_start = 100.0;
    double temp_decay = 0.995;
    double temp = temp_start;

    for (size_t iter = 0; iter < result_out->iterations; ++iter) {
        uint64_t cand_genome = flow_bitspace_mutate_1bit(space, current_genome, &rng, NULL);
        FlowPlan cand_plan;
        space->decode(space, cand_genome, &cand_plan);
        space->evaluate(space, &cand_plan, &cand_plan.eval);
        FlowGateFailureReason cand_reason = FLOW_GATE_PASS;
        cand_plan.eval.hard_gate_passed = hierarchical_hard_gate_reason(space, &cand_plan, &cand_plan.eval, &cand_reason);
        result_out->heatmap.total_mutations++;

        if (!cand_plan.eval.hard_gate_passed) {
            cand_plan.eval.energy += 1.0e12;
            result_out->heatmap.total_failures++;
            result_out->heatmap.failure_counts[cand_reason]++;
        } else {
            if (measured && space->ir != NULL && cand_plan.component != NULL) {
                cand_plan.eval.benchmark_ns = flow_component_benchmark(space->ir, cand_plan.component, &cand_plan.assignment);
                if (cand_plan.eval.benchmark_ns > 0) {
                    cand_plan.eval.energy += (double)cand_plan.eval.benchmark_ns / 1000.0;
                }
            }
            if (transition_model != NULL && transition_model->has_active_baseline) {
                cand_plan.eval.energy += flow_bitspace_calculate_transition_penalty(transition_model, &cand_plan, NULL);
            }
            pareto_update_bitspace(result_out, &cand_plan);
        }

        double delta = cand_plan.eval.energy - current_plan.eval.energy;
        double r = (double)(xorshift64(&rng) % 10000) / 10000.0;
        if (delta < 0.0 || (temp > 0.001 && r < exp(-delta / temp))) {
            current_genome = cand_genome;
            current_plan = cand_plan;
            if (cand_plan.eval.hard_gate_passed &&
                (!best_plan.eval.hard_gate_passed || cand_plan.eval.energy < best_plan.eval.energy)) {
                best_plan = cand_plan;
            }
        }
        temp *= temp_decay;
    }

    result_out->best_plan = best_plan;
    if (result_out->best_latency > 0.0) {
        result_out->latency_regret_percent =
            ((best_plan.eval.latency_score - result_out->best_latency) / result_out->best_latency) * 100.0;
    }
    if (result_out->best_memory > 0.0) {
        result_out->memory_regret_percent =
            (((double)best_plan.eval.memory_bytes - result_out->best_memory) / result_out->best_memory) * 100.0;
    }
    return best_plan.eval.hard_gate_passed ? 1 : 0;
}

int flow_bitspace_search_configured(const FlowBitSpace *space, size_t iterations, uint32_t seed,
                                    int measured, const FlowTransitionCostModel *transition_model,
                                    const FlowChaosAnnealConfig *anneal_config,
                                    FlowBitSearchResult *result_out) {
    if (space == NULL || result_out == NULL || space->candidate_count == 0) return 0;
    memset(result_out, 0, sizeof(*result_out));
    result_out->iterations = iterations == 0 ? 1 : iterations;
    result_out->seed = seed;
    result_out->measured = measured;

    double temp_start = (anneal_config != NULL && anneal_config->initial_temperature > 0.0) ?
                        anneal_config->initial_temperature : 80.0;
    double temp_decay = (anneal_config != NULL && anneal_config->cooling_decay > 0.0 && anneal_config->cooling_decay < 1.0) ?
                        anneal_config->cooling_decay : 0.98;
    size_t stag_limit = (anneal_config != NULL && anneal_config->plateau_stagnation_limit > 0) ?
                        anneal_config->plateau_stagnation_limit : 6;
    double reheat_ratio = (anneal_config != NULL && anneal_config->reheat_ratio > 0.0) ?
                          anneal_config->reheat_ratio : 0.6;

    uint64_t rng = seed == 0 ? UINT64_C(0x123456789abcdef0) : (uint64_t)seed;
    uint64_t current_genome = (transition_model != NULL && transition_model->has_active_baseline && transition_model->baseline_plan != NULL)
                              ? transition_model->baseline_plan->genome
                              : flow_bitspace_default_genome(space);
    FlowPlan current_plan;

    space->decode(space, current_genome, &current_plan);
    space->evaluate(space, &current_plan, &current_plan.eval);
    FlowGateFailureReason seed_reason = FLOW_GATE_PASS;
    current_plan.eval.hard_gate_passed = hierarchical_hard_gate_reason(space, &current_plan, &current_plan.eval, &seed_reason);
    result_out->heatmap.total_mutations++;
    if (!current_plan.eval.hard_gate_passed) {
        current_plan.eval.energy += 1.0e12;
        result_out->heatmap.total_failures++;
        result_out->heatmap.failure_counts[seed_reason]++;
    } else {
        if (measured && space->ir != NULL && current_plan.component != NULL) {
            current_plan.eval.benchmark_ns = flow_component_benchmark(space->ir, current_plan.component, &current_plan.assignment);
            if (current_plan.eval.benchmark_ns > 0) {
                current_plan.eval.energy += (double)current_plan.eval.benchmark_ns / 1000.0;
            }
        }
        if (transition_model != NULL && transition_model->has_active_baseline) {
            current_plan.eval.energy += flow_bitspace_calculate_transition_penalty(transition_model, &current_plan, NULL);
        }
    }

    FlowPlan best_plan = current_plan;
    if (best_plan.eval.hard_gate_passed) {
        pareto_update_bitspace(result_out, &best_plan);
    }

    double temp = temp_start;
    size_t stagnation_count = 0;
    FlowMaskCanvas active_canvas;
    if (anneal_config != NULL && anneal_config->use_mask_canvas) {
        active_canvas = anneal_config->mask_canvas;
    } else {
        active_canvas = space->global_canvas;
        if (anneal_config != NULL && anneal_config->env_mask != 0) {
            active_canvas.hard_composite_mask &= anneal_config->env_mask;
        }
    }
    double soft_weight = (anneal_config != NULL && anneal_config->soft_bias_weight > 0.0) ?
                         anneal_config->soft_bias_weight : 0.50;

    /* Continuous Probability-Biased Single 1-Bit Mutation Loop with Epigenetic Early Pruning */
    for (size_t iter = 0; iter < result_out->iterations; ++iter) {
        uint32_t flipped_bit = 0;
        double cur_bias_weight = current_plan.eval.hard_gate_passed ? soft_weight : 0.0;
        uint64_t cand_genome = flow_bitspace_mutate_1bit_superposed(space, current_genome, &active_canvas, cur_bias_weight, &rng, &flipped_bit);
        if (flipped_bit == 0xFFFFFFFF) {
            /* 1-cycle Bitwise Epigenetic Early Pruning */
            result_out->heatmap.total_mutations++;
            continue;
        }

        FlowPlan cand_plan;
        space->decode(space, cand_genome, &cand_plan);
        space->evaluate(space, &cand_plan, &cand_plan.eval);
        FlowGateFailureReason cand_reason = FLOW_GATE_PASS;
        cand_plan.eval.hard_gate_passed = hierarchical_hard_gate_reason(space, &cand_plan, &cand_plan.eval, &cand_reason);
        result_out->heatmap.total_mutations++;

        if (!cand_plan.eval.hard_gate_passed) {
            cand_plan.eval.energy += 1.0e12;
            result_out->heatmap.total_failures++;
            result_out->heatmap.failure_counts[cand_reason]++;
        } else {
            if (measured && space->ir != NULL && cand_plan.component != NULL) {
                cand_plan.eval.benchmark_ns = flow_component_benchmark(space->ir, cand_plan.component, &cand_plan.assignment);
                if (cand_plan.eval.benchmark_ns > 0) {
                    cand_plan.eval.energy += (double)cand_plan.eval.benchmark_ns / 1000.0;
                }
            }
            if (transition_model != NULL && transition_model->has_active_baseline) {
                cand_plan.eval.energy += flow_bitspace_calculate_transition_penalty(transition_model, &cand_plan, NULL);
            }
            pareto_update_bitspace(result_out, &cand_plan);
        }

        double delta = cand_plan.eval.energy - current_plan.eval.energy;
        double r = (double)(xorshift64(&rng) % 10000) / 10000.0;
        if (delta < 0.0 || (temp > 0.001 && r < exp(-delta / temp))) {
            current_genome = cand_genome;
            current_plan = cand_plan;
            if (cand_plan.eval.hard_gate_passed &&
                (!best_plan.eval.hard_gate_passed || cand_plan.eval.energy < best_plan.eval.energy)) {
                best_plan = cand_plan;
                stagnation_count = 0;
            }
        } else {
            stagnation_count++;
            if (stagnation_count >= stag_limit) {
                /* Thermodynamic Reheating on Plateau */
                temp = temp_start * reheat_ratio;
                stagnation_count = 0;
            }
        }
        temp *= temp_decay;
    }

    /* Search Protocol Step 2: Measured Recheck & Repeated Benchmark */
    if (measured && best_plan.component != NULL && best_plan.eval.hard_gate_passed && space->ir != NULL) {
        uint64_t bench_sum = 0;
        const int bench_runs = 3;
        for (int b = 0; b < bench_runs; ++b) {
            bench_sum += flow_component_benchmark(space->ir, best_plan.component, &best_plan.assignment);
        }
        best_plan.eval.benchmark_ns = bench_sum / bench_runs;
    }

    /* Search Protocol Step 3: Held-out workload boundary verification */
    if (space->ir != NULL && best_plan.component != NULL && best_plan.eval.hard_gate_passed) {
        SemanticIR held_out_ir = *space->ir;
        held_out_ir.input_max_count = (int)((double)held_out_ir.input_max_count * 1.25);
        if (held_out_ir.input_max_count < space->ir->input_max_count) {
            held_out_ir.input_max_count = space->ir->input_max_count + 100;
        }
        VerificationReport held_out_report;
        if (!flow_component_verify_plan(&held_out_ir, best_plan.component, &best_plan.assignment, &held_out_report) ||
            held_out_report.status == VERIFIER_COMPILE_ERROR) {
            snprintf(best_plan.eval.message, sizeof(best_plan.eval.message),
                     "held-out check: %s", held_out_report.message);
        }
    }

    result_out->best_plan = best_plan;
    result_out->mask_canvas = active_canvas;
    if (result_out->best_latency > 0.0) {
        result_out->latency_regret_percent =
            ((best_plan.eval.latency_score - result_out->best_latency) / result_out->best_latency) * 100.0;
    }
    if (result_out->best_memory > 0.0) {
        result_out->memory_regret_percent =
            (((double)best_plan.eval.memory_bytes - result_out->best_memory) / result_out->best_memory) * 100.0;
    }
    return best_plan.eval.hard_gate_passed ? 1 : 0;
}

int flow_bitspace_search_adaptive(const FlowBitSpace *space, size_t iterations, uint32_t seed,
                                  int measured, const FlowTransitionCostModel *transition_model,
                                  FlowBitSearchResult *result_out) {
    FlowChaosAnnealConfig config = {
        .initial_temperature = 80.0,
        .cooling_decay = 0.98,
        .plateau_stagnation_limit = 6,
        .reheat_ratio = 0.6
    };
    return flow_bitspace_search_configured(space, iterations, seed, measured, transition_model, &config, result_out);
}

int flow_bitspace_search(const FlowBitSpace *space, size_t iterations, uint32_t seed,
                         int measured, const FlowPlan *seed_plan, FlowBitSearchResult *result_out) {
    FlowTransitionCostModel model = {0};
    if (seed_plan != NULL) {
        model.has_active_baseline = 1;
        model.baseline_plan = seed_plan;
        model.live_state_bytes = 0;
        model.horizon_calls = 1000;
        model.jit_penalty_energy = 0.0;
    }
    return flow_bitspace_search_adaptive(space, iterations, seed, measured, &model, result_out);
}

int flow_bitspace_explain_seed(const FlowBitSpace *space, size_t iterations, uint32_t seed,
                               int measured, const FlowPlan *seed_plan, FILE *out) {
    if (space == NULL || out == NULL || space->candidate_count == 0) return 0;
    size_t iters = iterations == 0 ? 1 : iterations;
    uint64_t rng = seed == 0 ? UINT64_C(0x123456789abcdef0) : (uint64_t)seed;
    uint64_t current_genome = seed_plan != NULL ? seed_plan->genome : flow_bitspace_default_genome(space);
    FlowPlan current_plan;

    fprintf(out, "=== FLOW Search Deterministic Replay Diagnostics (Seed: %u, Iterations: %zu, Measured: %d) ===\n",
            seed, iters, measured);

    space->decode(space, current_genome, &current_plan);
    space->evaluate(space, &current_plan, &current_plan.eval);
    FlowGateFailureReason seed_reason = FLOW_GATE_PASS;
    current_plan.eval.hard_gate_passed = hierarchical_hard_gate_reason(space, &current_plan, &current_plan.eval, &seed_reason);

    fprintf(out, "[Step 0 / Initial Genome] 0x%016llx Comp=%s Status=%s (Energy=%.2f, Mem=%zu, Lat=%.2f)\n",
            (unsigned long long)current_genome,
            current_plan.component ? current_plan.component->id : "none",
            current_plan.eval.hard_gate_passed ? "PASS" : flow_gate_failure_name(seed_reason),
            current_plan.eval.energy, current_plan.eval.memory_bytes, current_plan.eval.latency_score);

    double temp = 100.0;
    double temp_decay = 0.995;

    for (size_t iter = 0; iter < iters; ++iter) {
        uint32_t flipped_bit = 0;
        uint64_t cand_genome = flow_bitspace_mutate_1bit(space, current_genome, &rng, &flipped_bit);
        FlowPlan cand_plan;
        space->decode(space, cand_genome, &cand_plan);
        space->evaluate(space, &cand_plan, &cand_plan.eval);
        FlowGateFailureReason cand_reason = FLOW_GATE_PASS;
        cand_plan.eval.hard_gate_passed = hierarchical_hard_gate_reason(space, &cand_plan, &cand_plan.eval, &cand_reason);

        if (!cand_plan.eval.hard_gate_passed) {
            cand_plan.eval.energy += 1.0e12;
            fprintf(out, "  [Step %zu] Flipped bit %u -> Genome=0x%016llx Comp=%s -> REJECTED (%s)\n",
                    iter + 1, flipped_bit, (unsigned long long)cand_genome,
                    cand_plan.component ? cand_plan.component->id : "none",
                    flow_gate_failure_name(cand_reason));
        } else {
            double delta = cand_plan.eval.energy - current_plan.eval.energy;
            double r = (double)(xorshift64(&rng) % 10000) / 10000.0;
            int accepted = (delta < 0.0 || (temp > 0.001 && r < exp(-delta / temp)));
            fprintf(out, "  [Step %zu] Flipped bit %u -> Genome=0x%016llx Comp=%s -> PASS (Energy=%.2f, Delta=%.2f) [%s]\n",
                    iter + 1, flipped_bit, (unsigned long long)cand_genome,
                    cand_plan.component ? cand_plan.component->id : "none",
                    cand_plan.eval.energy, delta, accepted ? "ACCEPTED" : "ANNEAL_DISCARD");
            if (accepted) {
                current_genome = cand_genome;
                current_plan = cand_plan;
            }
        }
        temp *= temp_decay;
    }
    fprintf(out, "=== End of Replay Diagnostics ===\n");
    return 1;
}

int flow_plan_to_artifact(const FlowPlan *plan, const SemanticIR *ir, uint32_t seed, FlowPlanArtifact *art) {
    if (plan == NULL || art == NULL) return 0;
    memset(art, 0, sizeof(*art));
    if (ir != NULL) {
        strncpy(art->flow_name, ir->flow_name, sizeof(art->flow_name) - 1);
    }
    if (plan->component != NULL) {
        strncpy(art->component_id, plan->component->id, sizeof(art->component_id) - 1);
        const FlowPlugin *plugin = flow_component_plugin(plan->component);
        if (plugin != NULL) {
            strncpy(art->module_name, plugin->name, sizeof(art->module_name) - 1);
            strncpy(art->module_version, plugin->version, sizeof(art->module_version) - 1);
        }
    }
    art->contract_hash = plan->contract_hash;
    art->plan_schema_hash = plan->schema_hash;
    art->seed = seed;
    art->bit_count = plan->bit_count;
    art->genome = plan->genome;
    art->dimensions = plan->dimension_set;
    art->plan = plan->assignment;
    art->metrics = plan->eval;
    strncpy(art->verification_status, plan->eval.hard_gate_passed ? "verified" : "rejected",
            sizeof(art->verification_status) - 1);
    snprintf(art->attestation_msg, sizeof(art->attestation_msg),
             "schema_hash=%llu genome=0x%016llx energy=%.4f",
             (unsigned long long)art->plan_schema_hash, (unsigned long long)art->genome,
             art->metrics.energy);
    return 1;
}

int flow_artifact_to_plan(const FlowPlanArtifact *art, const FlowBitSpace *space, FlowPlan *plan) {
    if (art == NULL || plan == NULL) return 0;
    memset(plan, 0, sizeof(*plan));
    if (space != NULL) {
        if (space->decode(space, art->genome, plan)) {
            return 1;
        }
    }
    /* Fallback direct assignment from artifact */
    plan->genome = art->genome;
    plan->bit_count = art->bit_count;
    plan->schema_hash = art->plan_schema_hash;
    plan->contract_hash = art->contract_hash;
    plan->dimension_set = art->dimensions;
    plan->assignment = art->plan;
    plan->eval = art->metrics;
    if (space != NULL) {
        for (size_t i = 0; i < space->candidate_count; ++i) {
            if (strcmp(space->candidates[i]->id, art->component_id) == 0) {
                plan->component = space->candidates[i];
                break;
            }
        }
    }
    return 1;
}

int flow_plan_artifact_save(FILE *output, const FlowPlanArtifact *art) {
    if (output == NULL || art == NULL) return 0;
    fprintf(output, "# Flow Plan Artifact (FLOW_PLAN_V1)\n");
    fprintf(output, "flow=%s\n", art->flow_name);
    fprintf(output, "module=%s\n", art->module_name);
    fprintf(output, "module_version=%s\n", art->module_version);
    fprintf(output, "component=%s\n", art->component_id);
    fprintf(output, "contract_hash=%llu\n", (unsigned long long)art->contract_hash);
    fprintf(output, "plan_schema_hash=%llu\n", (unsigned long long)art->plan_schema_hash);
    fprintf(output, "seed=%u\n", art->seed);
    fprintf(output, "bit_count=%u\n", art->bit_count);
    fprintf(output, "genome=0x%016llx\n", (unsigned long long)art->genome);
    fprintf(output, "dimension_count=%zu\n", art->dimensions.count);
    for (size_t i = 0; i < art->dimensions.count && i < art->plan.count; ++i) {
        const FlowPlanDimension *d = &art->dimensions.dimensions[i];
        fprintf(output, "dim.%s=%llu\n", d->name, (unsigned long long)art->plan.values[i]);
        fprintf(output, "dim_kind.%s=%d\n", d->name, (int)d->kind);
        fprintf(output, "dim_min.%s=%llu\n", d->name, (unsigned long long)d->min_val);
        fprintf(output, "dim_max.%s=%llu\n", d->name, (unsigned long long)d->max_val);
        fprintf(output, "dim_step.%s=%llu\n", d->name, (unsigned long long)d->step);
    }
    fprintf(output, "metric.energy=%.6f\n", art->metrics.energy);
    fprintf(output, "metric.latency=%.6f\n", art->metrics.latency_score);
    fprintf(output, "metric.throughput=%.6f\n", art->metrics.throughput_score);
    fprintf(output, "metric.memory_bytes=%zu\n", art->metrics.memory_bytes);
    fprintf(output, "metric.capacity=%zu\n", art->metrics.capacity);
    fprintf(output, "metric.benchmark_ns=%llu\n", (unsigned long long)art->metrics.benchmark_ns);
    fprintf(output, "verification_status=%s\n", art->verification_status);
    fprintf(output, "attestation=%s\n", art->attestation_msg);
    return ferror(output) == 0;
}

int flow_plan_artifact_load(FILE *input, FlowPlanArtifact *art) {
    char line[256];
    if (input == NULL || art == NULL) return 0;
    memset(art, 0, sizeof(*art));

    while (fgets(line, sizeof(line), input) != NULL) {
        char *eq = strchr(line, '=');
        if (eq == NULL) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        size_t vlen = strlen(val);
        if (vlen > 0 && val[vlen - 1] == '\n') val[vlen - 1] = '\0';
        if (vlen > 1 && val[vlen - 2] == '\r') val[vlen - 2] = '\0';

        if (strcmp(key, "flow") == 0) strncpy(art->flow_name, val, sizeof(art->flow_name) - 1);
        else if (strcmp(key, "module") == 0) strncpy(art->module_name, val, sizeof(art->module_name) - 1);
        else if (strcmp(key, "module_version") == 0) strncpy(art->module_version, val, sizeof(art->module_version) - 1);
        else if (strcmp(key, "component") == 0) strncpy(art->component_id, val, sizeof(art->component_id) - 1);
        else if (strcmp(key, "contract_hash") == 0) art->contract_hash = strtoull(val, NULL, 10);
        else if (strcmp(key, "plan_schema_hash") == 0) art->plan_schema_hash = strtoull(val, NULL, 10);
        else if (strcmp(key, "seed") == 0) art->seed = (uint32_t)strtoul(val, NULL, 10);
        else if (strcmp(key, "bit_count") == 0) art->bit_count = (uint32_t)strtoul(val, NULL, 10);
        else if (strcmp(key, "genome") == 0) art->genome = strtoull(val, NULL, 16);
        else if (strcmp(key, "dimension_count") == 0) {
            (void)val;
        }
        else if (strncmp(key, "dim.", 4) == 0) {
            const char *dname = key + 4;
            uint64_t dval = strtoull(val, NULL, 10);
            size_t idx = 0;
            int found = 0;
            for (size_t i = 0; i < art->dimensions.count; ++i) {
                if (strcmp(art->dimensions.dimensions[i].name, dname) == 0) {
                    idx = i;
                    found = 1;
                    break;
                }
            }
            if (!found && art->dimensions.count < 16) {
                idx = art->dimensions.count++;
                art->plan.count = art->dimensions.count;
                strncpy(art->dimensions.dimensions[idx].name, dname, sizeof(art->dimensions.dimensions[idx].name) - 1);
            }
            art->plan.values[idx] = dval;
        }
        else if (strncmp(key, "dim_kind.", 9) == 0) {
            const char *dname = key + 9;
            int kind = atoi(val);
            for (size_t i = 0; i < art->dimensions.count; ++i) {
                if (strcmp(art->dimensions.dimensions[i].name, dname) == 0) {
                    art->dimensions.dimensions[i].kind = (FlowDimensionKind)kind;
                    break;
                }
            }
        }
        else if (strncmp(key, "dim_min.", 8) == 0) {
            const char *dname = key + 8;
            uint64_t mval = strtoull(val, NULL, 10);
            for (size_t i = 0; i < art->dimensions.count; ++i) {
                if (strcmp(art->dimensions.dimensions[i].name, dname) == 0) {
                    art->dimensions.dimensions[i].min_val = mval;
                    break;
                }
            }
        }
        else if (strncmp(key, "dim_max.", 8) == 0) {
            const char *dname = key + 8;
            uint64_t mval = strtoull(val, NULL, 10);
            for (size_t i = 0; i < art->dimensions.count; ++i) {
                if (strcmp(art->dimensions.dimensions[i].name, dname) == 0) {
                    art->dimensions.dimensions[i].max_val = mval;
                    break;
                }
            }
        }
        else if (strncmp(key, "dim_step.", 9) == 0) {
            const char *dname = key + 9;
            uint64_t sval = strtoull(val, NULL, 10);
            for (size_t i = 0; i < art->dimensions.count; ++i) {
                if (strcmp(art->dimensions.dimensions[i].name, dname) == 0) {
                    art->dimensions.dimensions[i].step = sval;
                    break;
                }
            }
        }
        else if (strcmp(key, "metric.energy") == 0) art->metrics.energy = strtod(val, NULL);
        else if (strcmp(key, "metric.latency") == 0) art->metrics.latency_score = strtod(val, NULL);
        else if (strcmp(key, "metric.throughput") == 0) art->metrics.throughput_score = strtod(val, NULL);
        else if (strcmp(key, "metric.memory_bytes") == 0) art->metrics.memory_bytes = (size_t)strtoull(val, NULL, 10);
        else if (strcmp(key, "metric.capacity") == 0) art->metrics.capacity = (size_t)strtoull(val, NULL, 10);
        else if (strcmp(key, "metric.benchmark_ns") == 0) art->metrics.benchmark_ns = strtoull(val, NULL, 10);
        else if (strcmp(key, "verification_status") == 0) strncpy(art->verification_status, val, sizeof(art->verification_status) - 1);
        else if (strcmp(key, "attestation") == 0) strncpy(art->attestation_msg, val, sizeof(art->attestation_msg) - 1);
    }
    return 1;
}

int flow_artifact_validate(const FlowPlanArtifact *art, const SemanticIR *ir,
                           const FlowBitSpace *space, char *err_msg, size_t err_size) {
    if (err_msg != NULL && err_size != 0) err_msg[0] = '\0';
    if (art == NULL || ir == NULL) {
        if (err_msg && err_size) snprintf(err_msg, err_size, "null artifact or IR");
        return 0;
    }

    /* 1. Contract Hash Validation */
    uint64_t current_contract_hash = flow_compute_contract_hash(ir);
    if (art->contract_hash != 0 && art->contract_hash != current_contract_hash) {
        if (err_msg && err_size) {
            snprintf(err_msg, err_size, "contract hash mismatch (expected %llu, got %llu)",
                     (unsigned long long)current_contract_hash, (unsigned long long)art->contract_hash);
        }
        return 0;
    }

    /* 2. Component lookup and compatibility */
    const Component *comp = NULL;
    if (space != NULL) {
        for (size_t i = 0; i < space->candidate_count; ++i) {
            if (strcmp(space->candidates[i]->id, art->component_id) == 0) {
                comp = space->candidates[i];
                break;
            }
        }
    }
    if (comp == NULL) {
        for (size_t i = 0; i < component_count(); ++i) {
            const Component *c = component_at(i);
            if (c != NULL && strcmp(c->id, art->component_id) == 0) {
                comp = c;
                break;
            }
        }
    }
    if (comp == NULL) {
        if (err_msg && err_size) snprintf(err_msg, err_size, "component '%s' not registered", art->component_id);
        return 0;
    }
    if (!component_compatible(ir, comp)) {
        if (err_msg && err_size) snprintf(err_msg, err_size, "component '%s' is incompatible with contract", comp->id);
        return 0;
    }

    /* 3. Module Version Validation */
    const FlowPlugin *plugin = flow_component_plugin(comp);
    if (plugin != NULL && art->module_version[0] != '\0') {
        if (strcmp(plugin->version, art->module_version) != 0) {
            if (err_msg && err_size) {
                snprintf(err_msg, err_size, "module '%s' version mismatch (expected %s, got %s)",
                         plugin->name, plugin->version, art->module_version);
            }
            return 0;
        }
    }

    /* 4. Plan Schema Hash Validation */
    if (art->plan_schema_hash != 0) {
        uint64_t expected_schema = flow_bitspace_compute_schema_hash(ir, comp, &art->dimensions);
        if (expected_schema != art->plan_schema_hash) {
            if (err_msg && err_size) {
                snprintf(err_msg, err_size, "plan schema hash mismatch (expected %llu, got %llu)",
                         (unsigned long long)expected_schema, (unsigned long long)art->plan_schema_hash);
            }
            return 0;
        }
    }

    /* 5. Hard Gate & Verifier Validation */
    VerificationReport report;
    if (!flow_component_verify_plan(ir, comp, &art->plan, &report) || report.status == VERIFIER_COMPILE_ERROR) {
        if (err_msg && err_size) {
            snprintf(err_msg, err_size, "hard gate verification failed: %s", report.message);
        }
        return 0;
    }

    return 1;
}

const char *flow_plan_tactic_name(FlowPlanTactic tactic) {
    switch (tactic) {
        case FLOW_TACTIC_SPEED: return "speed";
        case FLOW_TACTIC_BALANCED: return "balanced";
        case FLOW_TACTIC_MEMORY: return "memory";
        default: return "unknown";
    }
}

int flow_bitspace_extract_ensemble(const FlowBitSearchResult *search_res,
                                   FlowPlanEnsemble *ensemble_out) {
    if (search_res == NULL || ensemble_out == NULL) return 0;
    memset(ensemble_out, 0, sizeof(*ensemble_out));

    if (search_res->best_plan.component == NULL) return 0;

    /* Start with best_plan as baseline for all 3 tactics */
    ensemble_out->tactics[FLOW_TACTIC_SPEED] = search_res->best_plan;
    ensemble_out->tactics[FLOW_TACTIC_BALANCED] = search_res->best_plan;
    ensemble_out->tactics[FLOW_TACTIC_MEMORY] = search_res->best_plan;
    ensemble_out->available[FLOW_TACTIC_SPEED] = 1;
    ensemble_out->available[FLOW_TACTIC_BALANCED] = 1;
    ensemble_out->available[FLOW_TACTIC_MEMORY] = 1;
    ensemble_out->count = 3;

    if (search_res->pareto_count > 0) {
        double min_lat = search_res->pareto_points[0].eval.latency_score;
        double min_energy = search_res->pareto_points[0].eval.energy;
        size_t min_mem = search_res->pareto_points[0].eval.memory_bytes;
        size_t speed_idx = 0;
        size_t balanced_idx = 0;
        size_t mem_idx = 0;

        for (size_t i = 1; i < search_res->pareto_count; ++i) {
            const FlowPlan *p = &search_res->pareto_points[i];
            if (p->eval.latency_score < min_lat) {
                min_lat = p->eval.latency_score;
                speed_idx = i;
            }
            if (p->eval.energy < min_energy) {
                min_energy = p->eval.energy;
                balanced_idx = i;
            }
            if (p->eval.memory_bytes < min_mem) {
                min_mem = p->eval.memory_bytes;
                mem_idx = i;
            }
        }
        ensemble_out->tactics[FLOW_TACTIC_SPEED] = search_res->pareto_points[speed_idx];
        ensemble_out->tactics[FLOW_TACTIC_BALANCED] = search_res->pareto_points[balanced_idx];
        ensemble_out->tactics[FLOW_TACTIC_MEMORY] = search_res->pareto_points[mem_idx];
    }
    return 1;
}
