#include "bitspace.h"
#include "registry.h"

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

static int hierarchical_hard_gate(const FlowBitSpace *space, const FlowPlan *plan, const FlowEvaluation *result) {
    VerificationReport v_report;
    if (space == NULL || plan == NULL || result == NULL || plan->component == NULL) return 0;
    if (result->capacity == 0) return 0;
    if (space->ir != NULL) {
        if (!component_compatible(space->ir, plan->component)) return 0;
        if (space->ir->top_n > 0 && result->capacity < (size_t)space->ir->top_n) return 0;
        if (space->ir->memory_limit_mb > 0 &&
            result->memory_bytes > (size_t)space->ir->memory_limit_mb * 1024u * 1024u)
            return 0;
    }
    if (space->ir != NULL && plan->component != NULL) {
        if (!flow_component_verify_plan(space->ir, plan->component, &plan->assignment, &v_report))
            return 0;
        if (v_report.status == VERIFIER_COMPILE_ERROR) return 0;
    }
    return 1;
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
    space->selector_bits = 0;
    space->bit_count = space->candidate_bits[0];
    if (space->bit_count > 64) space->bit_count = 64;

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

uint64_t flow_bitspace_mutate_1bit(const FlowBitSpace *space, uint64_t genome,
                                   uint64_t *rng_state, uint32_t *mutated_bit_out) {
    uint32_t bits = (space != NULL && space->bit_count > 0) ? space->bit_count : 16;
    if (bits > 64) bits = 64;
    uint32_t bit = (uint32_t)(xorshift64(rng_state) % bits);
    if (mutated_bit_out != NULL) *mutated_bit_out = bit;
    return genome ^ (UINT64_C(1) << bit);
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

static uint64_t flow_bitspace_default_genome(const FlowBitSpace *space) {
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

int flow_bitspace_search(const FlowBitSpace *space, size_t iterations, uint32_t seed,
                         int measured, const FlowPlan *seed_plan, FlowBitSearchResult *result_out) {
    if (space == NULL || result_out == NULL || space->candidate_count == 0) return 0;
    memset(result_out, 0, sizeof(*result_out));
    result_out->iterations = iterations == 0 ? 1 : iterations;
    result_out->seed = seed;
    result_out->measured = measured;

    uint64_t rng = seed == 0 ? UINT64_C(0x123456789abcdef0) : (uint64_t)seed;
    uint64_t current_genome = seed_plan != NULL ? seed_plan->genome : flow_bitspace_default_genome(space);
    FlowPlan current_plan;

    space->decode(space, current_genome, &current_plan);
    space->evaluate(space, &current_plan, &current_plan.eval);
    current_plan.eval.hard_gate_passed = space->hard_gate(space, &current_plan, &current_plan.eval);
    if (!current_plan.eval.hard_gate_passed) {
        current_plan.eval.energy += 1.0e12;
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
        cand_plan.eval.hard_gate_passed = space->hard_gate(space, &cand_plan, &cand_plan.eval);

        if (!cand_plan.eval.hard_gate_passed) {
            cand_plan.eval.energy += 1.0e12;
        } else {
            if (measured && space->ir != NULL && cand_plan.component != NULL) {
                cand_plan.eval.benchmark_ns = flow_component_benchmark(space->ir, cand_plan.component, &cand_plan.assignment);
                if (cand_plan.eval.benchmark_ns > 0) {
                    cand_plan.eval.energy += (double)cand_plan.eval.benchmark_ns / 1000.0;
                }
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
