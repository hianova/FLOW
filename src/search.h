#ifndef SEARCH_H
#define SEARCH_H

#include <stddef.h>
#include <stdint.h>

#include "flow.h"
#include "registry.h"
#include "bitspace.h"

#define FLOW_PARETO_MAX 16

typedef struct {
    size_t buffer_bytes;
    size_t initial_capacity;
    unsigned growth_percent;
    size_t batch_size;
    size_t arena_bytes;
} FlowTuning;

typedef struct {
    const Component *component;
    FlowPlanAssignment assignment;
    FlowPlanMetrics metrics;
    double energy;
} FlowParetoPoint;

typedef struct {
    size_t count;
    FlowParetoPoint points[FLOW_PARETO_MAX];
    double best_latency;
    double best_memory;
    double latency_regret_percent;
    double memory_regret_percent;
} FlowParetoSummary;

typedef struct FlowSearchResult {
    const Component *component;
    uint64_t genome;
    FlowPlanDimensionSet dimension_set;
    FlowPlanAssignment assignment;
    FlowPlanMetrics metrics;
    double capacity;
    double threads;
    double shards;
    double energy;
    uint64_t benchmark_ns;
    int measured;
    size_t iterations;
    uint32_t seed;
    FlowTuning tuning;
    FlowParetoSummary pareto;
    FlowSearchHeatmap heatmap;
} SearchResult;

typedef struct {
    int available;
    size_t component;
    size_t capacity;
    size_t threads;
    size_t shards;
    uint64_t benchmark_ns;
    size_t workload_bytes;
    FlowTuning tuning;
    FlowPlanAssignment assignment;
} ProfileSeed;

uint64_t flow_plan_get_value(const FlowPlanDimensionSet *dims,
                             const FlowPlanAssignment *plan,
                             const char *name, uint64_t default_val);

FlowTuning flow_default_tuning(const SemanticIR *ir,
                               const Component *component);

SearchResult search_best(const SemanticIR *ir, size_t iterations, uint32_t seed,
                         int measured, const ProfileSeed *profile);

void flow_plan_to_search_result(const FlowPlan *plan, const SemanticIR *ir,
                                uint32_t seed, SearchResult *out);

int flow_search_result_to_artifact(const SemanticIR *ir, const SearchResult *result,
                                  FlowPlanArtifact *art);

int flow_artifact_to_profile_seed(const FlowPlanArtifact *art, ProfileSeed *seed_out);

#endif
