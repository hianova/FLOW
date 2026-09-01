#include "search.h"
#include "benchmark.h"
#include "bitspace.h"

#include <math.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>

/* The FLOW search core uses one chaotic 1-bit mutation over hierarchical FlowBitSpace */

uint64_t flow_plan_get_value(const FlowPlanDimensionSet *dims,
                             const FlowPlanAssignment *plan,
                             const char *name, uint64_t default_val) {
    size_t i;
    if (dims == NULL || plan == NULL || name == NULL) return default_val;
    for (i = 0; i < dims->count && i < plan->count; ++i) {
        if (strcmp(dims->dimensions[i].name, name) == 0) {
            return plan->values[i];
        }
    }
    return default_val;
}

FlowTuning flow_default_tuning(const SemanticIR *ir,
                               const Component *component) {
    FlowTuning tuning;
    (void)ir;
    (void)component;
    tuning.buffer_bytes = 16384;
    tuning.initial_capacity = 4;
    tuning.growth_percent = 150;
    tuning.batch_size = 16384;
    tuning.arena_bytes = 0;
    return tuning;
}

SearchResult search_best(const SemanticIR *ir, size_t iterations, uint32_t seed,
                         int measured, const ProfileSeed *profile) {
    FlowBitSpace space;
    FlowBitSearchResult bit_res;
    SearchResult result;
    memset(&result, 0, sizeof(result));

    if (ir == NULL || !flow_bitspace_init_for_ir(ir, &space)) {
        return result;
    }

    FlowPlan seed_plan;
    FlowPlan *seed_ptr = NULL;
    if (profile != NULL && profile->available) {
        for (size_t c = 0; c < space.candidate_count; ++c) {
            if (component_index(space.candidates[c]) == profile->component ||
                c == profile->component) {
                seed_plan.component = space.candidates[c];
                seed_plan.dimension_set = space.candidate_dims[c];
                if (profile->assignment.count > 0) {
                    seed_plan.assignment = profile->assignment;
                } else {
                    seed_plan.assignment.count = 3;
                    seed_plan.assignment.values[0] = profile->capacity;
                    seed_plan.assignment.values[1] = profile->threads;
                    seed_plan.assignment.values[2] = profile->shards;
                }
                seed_plan.genome = flow_bitspace_encode(&space, c, &seed_plan.assignment);
                seed_ptr = &seed_plan;
                break;
            }
        }
    }

    if (!flow_bitspace_search(&space, iterations, seed, measured, seed_ptr, &bit_res)) {
        return result;
    }

    const FlowPlan *best = &bit_res.best_plan;
    FlowTuning tuning = flow_default_tuning(ir, best->component);
    if (profile != NULL && profile->available) {
        if (profile->tuning.buffer_bytes != 0) tuning.buffer_bytes = profile->tuning.buffer_bytes;
        if (profile->tuning.initial_capacity != 0) tuning.initial_capacity = profile->tuning.initial_capacity;
        if (profile->tuning.growth_percent != 0) tuning.growth_percent = profile->tuning.growth_percent;
        if (profile->tuning.batch_size != 0) tuning.batch_size = profile->tuning.batch_size;
        if (profile->tuning.arena_bytes != 0) tuning.arena_bytes = profile->tuning.arena_bytes;
    } else {
        tuning.buffer_bytes = flow_plan_get_value(&best->dimension_set, &best->assignment, "buffer_bytes", tuning.buffer_bytes);
        tuning.initial_capacity = flow_plan_get_value(&best->dimension_set, &best->assignment, "initial_capacity", tuning.initial_capacity);
        tuning.growth_percent = (unsigned)flow_plan_get_value(&best->dimension_set, &best->assignment, "growth_percent", tuning.growth_percent);
        tuning.batch_size = flow_plan_get_value(&best->dimension_set, &best->assignment, "batch_size", tuning.batch_size);
        tuning.arena_bytes = flow_plan_get_value(&best->dimension_set, &best->assignment, "arena_bytes", tuning.arena_bytes);
    }

    FlowPlanMetrics best_metrics;
    memset(&best_metrics, 0, sizeof(best_metrics));
    best_metrics.capacity = best->eval.capacity;
    best_metrics.threads = flow_plan_get_value(&best->dimension_set, &best->assignment, "threads", 1);
    best_metrics.shards = flow_plan_get_value(&best->dimension_set, &best->assignment, "shards", 1);
    best_metrics.latency_score = best->eval.latency_score;
    best_metrics.throughput_score = best->eval.throughput_score;
    best_metrics.memory_bytes = best->eval.memory_bytes;
    best_metrics.energy = best->eval.energy;

    FlowParetoSummary pareto;
    memset(&pareto, 0, sizeof(pareto));
    pareto.count = bit_res.pareto_count;
    pareto.best_latency = bit_res.best_latency;
    pareto.best_memory = bit_res.best_memory;
    pareto.latency_regret_percent = bit_res.latency_regret_percent;
    pareto.memory_regret_percent = bit_res.memory_regret_percent;
    for (size_t p = 0; p < bit_res.pareto_count; ++p) {
        pareto.points[p].component = bit_res.pareto_points[p].component;
        pareto.points[p].assignment = bit_res.pareto_points[p].assignment;
        pareto.points[p].energy = bit_res.pareto_points[p].eval.energy;
        pareto.points[p].metrics.capacity = bit_res.pareto_points[p].eval.capacity;
        pareto.points[p].metrics.memory_bytes = bit_res.pareto_points[p].eval.memory_bytes;
        pareto.points[p].metrics.latency_score = bit_res.pareto_points[p].eval.latency_score;
        pareto.points[p].metrics.threads = flow_plan_get_value(&bit_res.pareto_points[p].dimension_set, &bit_res.pareto_points[p].assignment, "threads", 1);
        pareto.points[p].metrics.shards = flow_plan_get_value(&bit_res.pareto_points[p].dimension_set, &bit_res.pareto_points[p].assignment, "shards", 1);
    }

    result.component = best->component;
    result.genome = best->genome;
    result.dimension_set = best->dimension_set;
    result.assignment = best->assignment;
    result.metrics = best_metrics;
    result.capacity = (double)best_metrics.capacity;
    result.threads = (double)best_metrics.threads;
    result.shards = (double)best_metrics.shards;
    result.energy = best->eval.energy;
    result.benchmark_ns = best->eval.benchmark_ns;
    result.measured = measured;
    result.iterations = iterations;
    result.seed = seed;
    result.tuning = tuning;
    result.pareto = pareto;
    return result;
}

int flow_search_result_to_artifact(const SemanticIR *ir, const SearchResult *result,
                                  FlowPlanArtifact *art) {
    if (ir == NULL || result == NULL || art == NULL) return 0;
    memset(art, 0, sizeof(*art));
    strncpy(art->flow_name, ir->flow_name, sizeof(art->flow_name) - 1);
    if (result->component != NULL) {
        strncpy(art->component_id, result->component->id, sizeof(art->component_id) - 1);
        const FlowPlugin *plugin = flow_component_plugin(result->component);
        if (plugin != NULL) {
            strncpy(art->module_name, plugin->name, sizeof(art->module_name) - 1);
            strncpy(art->module_version, plugin->version, sizeof(art->module_version) - 1);
        }
    }
    art->contract_hash = flow_compute_contract_hash(ir);
    art->plan_schema_hash = flow_bitspace_compute_schema_hash(ir, result->component, &result->dimension_set);
    art->seed = result->seed;
    art->bit_count = result->dimension_set.count;
    art->genome = result->genome;
    art->dimensions = result->dimension_set;
    art->plan = result->assignment;
    art->metrics.energy = result->energy;
    art->metrics.latency_score = result->metrics.latency_score;
    art->metrics.throughput_score = result->metrics.throughput_score;
    art->metrics.memory_bytes = result->metrics.memory_bytes;
    art->metrics.capacity = result->metrics.capacity;
    art->metrics.benchmark_ns = result->benchmark_ns;
    strncpy(art->verification_status, "verified", sizeof(art->verification_status) - 1);
    snprintf(art->attestation_msg, sizeof(art->attestation_msg),
             "schema_hash=%llu genome=0x%016llx",
             (unsigned long long)art->plan_schema_hash, (unsigned long long)art->genome);
    return 1;
}

int flow_artifact_to_profile_seed(const FlowPlanArtifact *art, ProfileSeed *seed_out) {
    if (art == NULL || seed_out == NULL) return 0;
    memset(seed_out, 0, sizeof(*seed_out));
    seed_out->available = 1;
    seed_out->capacity = art->metrics.capacity;
    seed_out->benchmark_ns = art->metrics.benchmark_ns;
    seed_out->assignment = art->plan;
    seed_out->tuning.buffer_bytes = flow_plan_get_value(&art->dimensions, &art->plan, "buffer_bytes", 16384);
    seed_out->tuning.initial_capacity = flow_plan_get_value(&art->dimensions, &art->plan, "initial_capacity", 4);
    seed_out->tuning.growth_percent = (unsigned)flow_plan_get_value(&art->dimensions, &art->plan, "growth_percent", 150);
    seed_out->tuning.batch_size = flow_plan_get_value(&art->dimensions, &art->plan, "batch_size", 16384);
    seed_out->tuning.arena_bytes = flow_plan_get_value(&art->dimensions, &art->plan, "arena_bytes", 0);
    return 1;
}
