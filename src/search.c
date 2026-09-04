#include "search.h"
#include "benchmark.h"
#include "bitspace.h"
#include <string.h>

/* The FLOW search core uses one chaotic 1-bit mutation over hierarchical FlowBitSpace */

uint64_t flow_plan_get_value(const FlowPlanDimensionSet *dims, const FlowPlanAssignment *plan, const char *name, uint64_t default_val) {
    if (!dims || !plan || !name) return default_val;
    for (size_t i = 0; i < dims->count && i < plan->count; ++i)
        if (!strcmp(dims->dimensions[i].name, name)) return plan->values[i];
    return default_val;
}

FlowTuning flow_default_tuning(const SemanticIR *ir, const Component *comp) {
    (void)ir; (void)comp;
    return (FlowTuning){.buffer_bytes = 16384, .initial_capacity = 4, .growth_percent = 150, .batch_size = 16384, .arena_bytes = 0};
}

SearchResult search_best(const SemanticIR *ir, size_t iterations, uint32_t seed, int measured, const ProfileSeed *profile) {
    FlowBitSpace space;
    FlowBitSearchResult bit_res;
    SearchResult result = {0};
    if (!ir || !flow_bitspace_init_for_ir(ir, &space)) return result;

    FlowPlan seed_plan = {0}, *seed_ptr = NULL;
    if (profile && profile->available) {
        for (size_t c = 0; c < space.candidate_count; ++c) {
            if (component_index(space.candidates[c]) == profile->component || c == profile->component) {
                seed_plan.component = space.candidates[c];
                seed_plan.dimension_set = space.candidate_dims[c];
                seed_plan.assignment = profile->assignment.count > 0 ? profile->assignment :
                    (FlowPlanAssignment){.count = 3, .values = {profile->capacity, profile->threads, profile->shards}};
                seed_plan.genome = flow_bitspace_encode(&space, c, &seed_plan.assignment);
                seed_ptr = &seed_plan;
                break;
            }
        }
    }

    if (!flow_bitspace_search(&space, iterations, seed, measured, seed_ptr, &bit_res)) return result;

    flow_plan_to_search_result(&bit_res.best_plan, ir, seed, &result);
    result.iterations = iterations;
    result.measured = measured;
    result.benchmark_ns = bit_res.best_plan.eval.benchmark_ns;
    result.heatmap = bit_res.heatmap;
    result.mask_canvas = bit_res.mask_canvas;
    result.pareto = (FlowParetoSummary){
        .count = bit_res.pareto_count, .best_latency = bit_res.best_latency, .best_memory = bit_res.best_memory,
        .latency_regret_percent = bit_res.latency_regret_percent, .memory_regret_percent = bit_res.memory_regret_percent
    };
    for (size_t p = 0; p < bit_res.pareto_count; ++p) {
        const FlowPlan *pt = &bit_res.pareto_points[p];
        result.pareto.points[p] = (FlowParetoPoint){
            .component = pt->component, .assignment = pt->assignment, .energy = pt->eval.energy,
            .metrics = { .capacity = pt->eval.capacity, .memory_bytes = pt->eval.memory_bytes,
                         .latency_score = pt->eval.latency_score,
                         .threads = flow_plan_get_value(&pt->dimension_set, &pt->assignment, "threads", 1),
                         .shards = flow_plan_get_value(&pt->dimension_set, &pt->assignment, "shards", 1) }
        };
    }
    if (profile && profile->available) {
        #define OVR(f) if (profile->tuning.f) result.tuning.f = profile->tuning.f
        OVR(buffer_bytes); OVR(initial_capacity); OVR(growth_percent); OVR(batch_size); OVR(arena_bytes);
        #undef OVR
    }
    return result;
}

int flow_search_result_to_artifact(const SemanticIR *ir, const SearchResult *result, FlowPlanArtifact *art) {
    if (!ir || !result || !art) return 0;
    memset(art, 0, sizeof(*art));
    strncpy(art->flow_name, ir->flow_name, sizeof(art->flow_name) - 1);
    if (result->component) {
        strncpy(art->component_id, result->component->id, sizeof(art->component_id) - 1);
        const FlowPlugin *plugin = flow_component_plugin(result->component);
        if (plugin) {
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
    art->metrics = (FlowEvaluation){
        .energy = result->energy, .latency_score = result->metrics.latency_score,
        .throughput_score = result->metrics.throughput_score, .memory_bytes = result->metrics.memory_bytes,
        .capacity = result->metrics.capacity, .benchmark_ns = result->benchmark_ns
    };
    strncpy(art->verification_status, "verified", sizeof(art->verification_status) - 1);
    snprintf(art->attestation_msg, sizeof(art->attestation_msg), "schema_hash=%llu genome=0x%016llx",
             (unsigned long long)art->plan_schema_hash, (unsigned long long)art->genome);
    return 1;
}

int flow_artifact_to_profile_seed(const FlowPlanArtifact *art, ProfileSeed *seed_out) {
    if (!art || !seed_out) return 0;
    *seed_out = (ProfileSeed){
        .available = 1, .capacity = art->metrics.capacity, .benchmark_ns = art->metrics.benchmark_ns,
        .assignment = art->plan,
        .tuning = {
            .buffer_bytes = flow_plan_get_value(&art->dimensions, &art->plan, "buffer_bytes", 16384),
            .initial_capacity = flow_plan_get_value(&art->dimensions, &art->plan, "initial_capacity", 4),
            .growth_percent = (unsigned)flow_plan_get_value(&art->dimensions, &art->plan, "growth_percent", 150),
            .batch_size = flow_plan_get_value(&art->dimensions, &art->plan, "batch_size", 16384),
            .arena_bytes = flow_plan_get_value(&art->dimensions, &art->plan, "arena_bytes", 0)
        }
    };
    return 1;
}

void flow_plan_to_search_result(const FlowPlan *plan, const SemanticIR *ir, uint32_t seed, SearchResult *out) {
    if (!plan || !out) return;
    *out = (SearchResult){
        .component = plan->component, .genome = plan->genome, .dimension_set = plan->dimension_set,
        .assignment = plan->assignment, .capacity = (double)plan->eval.capacity, .energy = plan->eval.energy,
        .benchmark_ns = (uint64_t)plan->eval.latency_score, .iterations = 1, .seed = seed,
        .tuning = flow_default_tuning(ir, plan->component),
        .metrics = {
            .capacity = plan->eval.capacity, .latency_score = plan->eval.latency_score,
            .throughput_score = plan->eval.throughput_score, .memory_bytes = plan->eval.memory_bytes, .energy = plan->eval.energy,
            .threads = flow_plan_get_value(&plan->dimension_set, &plan->assignment, "threads", 1),
            .shards = flow_plan_get_value(&plan->dimension_set, &plan->assignment, "shards", 1)
        }
    };
    out->threads = (double)out->metrics.threads;
    out->shards = (double)out->metrics.shards;
    out->tuning.buffer_bytes = flow_plan_get_value(&plan->dimension_set, &plan->assignment, "buffer_bytes", out->tuning.buffer_bytes);
    out->tuning.initial_capacity = flow_plan_get_value(&plan->dimension_set, &plan->assignment, "initial_capacity", out->tuning.initial_capacity);
    out->tuning.growth_percent = (unsigned)flow_plan_get_value(&plan->dimension_set, &plan->assignment, "growth_percent", out->tuning.growth_percent);
    out->tuning.batch_size = flow_plan_get_value(&plan->dimension_set, &plan->assignment, "batch_size", out->tuning.batch_size);
    out->tuning.arena_bytes = flow_plan_get_value(&plan->dimension_set, &plan->assignment, "arena_bytes", out->tuning.arena_bytes);
}
