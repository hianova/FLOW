#ifndef FLOW_ADAPTIVE_H
#define FLOW_ADAPTIVE_H

#include "reload.h"

#include <stddef.h>
#include <stdint.h>

typedef struct FlowAdaptiveController FlowAdaptiveController;

typedef struct {
    const char *flow_name;
    const char *domain_contract;
    size_t input_capacity;
    size_t memory_limit_bytes;
    const char *resource;
    const char *capability;
    int require_parallelizable;
} FlowAdaptivePolicy;

typedef struct {
    size_t sample_window;
    size_t cooldown_calls;
    size_t journal_capacity;
    double min_improvement_percent;
    FlowAdaptivePolicy policy;
} FlowAdaptiveConfig;

typedef struct {
    uint64_t calls;
    uint64_t failures;
    uint64_t total_ns;
} FlowAdaptiveMetrics;

typedef struct {
    const char *name;
    const FlowUnit *unit;
    const char *flow_binding;
    const char *domain_contract;
    int latency_score;
    int memory_score;
    size_t memory_fixed_bytes;
    size_t memory_bytes_per_capacity;
    const char *resource;
    const char *capability;
    int supports_parallelizable;
    uint64_t plan_schema_hash;
} FlowAdaptiveCandidate;

typedef int (*FlowAdaptiveProbe)(void *host_context,
                                 const FlowAdaptiveCandidate *candidate,
                                 const FlowAdaptiveMetrics *metrics,
                                 double *score_out);

typedef enum {
    FLOW_ADAPTIVE_OK = 0,
    FLOW_ADAPTIVE_NOT_READY = 1,
    FLOW_ADAPTIVE_NO_CHANGE = 2,
    FLOW_ADAPTIVE_INVALID = 3,
    FLOW_ADAPTIVE_RELOAD_FAILED = 4
} FlowAdaptiveStatus;

typedef struct {
    uint64_t l3_cache_misses;
    uint64_t l3_cache_references;
    uint64_t branch_mispredictions;
    uint64_t page_faults;
    uint64_t memory_bandwidth_bytes;
    uint64_t cpu_cycles;
    uint64_t instructions;
    double cache_miss_rate;
    double ipc;
} FlowPMUTelemetry;

typedef struct {
    double cache_miss_rate_threshold;
    double min_ipc_threshold;
    uint64_t max_memory_bandwidth_bytes;
} FlowPMUThresholds;

FlowAdaptiveController *flow_adaptive_create(
    FlowReloadContext *context, void *host_context,
    const FlowAdaptiveConfig *config,
    const FlowAdaptiveCandidate *candidates, size_t candidate_count,
    size_t current_index, FlowAdaptiveProbe probe);
int flow_adaptive_destroy(FlowAdaptiveController *controller);

int flow_adaptive_call(FlowAdaptiveController *controller,
                       FlowReloadReader *reader, const void *input,
                       void *output);
FlowAdaptiveStatus flow_adaptive_tick(FlowAdaptiveController *controller);
FlowAdaptiveStatus flow_adaptive_tick_pmu(FlowAdaptiveController *controller,
                                         const FlowPMUThresholds *thresholds);
int flow_adaptive_feed_pmu(FlowAdaptiveController *controller,
                           const FlowPMUTelemetry *pmu);
int flow_adaptive_pmu_metrics(const FlowAdaptiveController *controller,
                              FlowPMUTelemetry *pmu_out);
size_t flow_adaptive_current_index(const FlowAdaptiveController *controller);
int flow_adaptive_metrics(const FlowAdaptiveController *controller,
                          FlowAdaptiveMetrics *metrics_out);
const char *flow_adaptive_status_name(FlowAdaptiveStatus status);

#endif
