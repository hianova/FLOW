#ifndef FLOW_ADAPTIVE_H
#define FLOW_ADAPTIVE_H
#include "reload.h"
#include "plugin.h"
#include <stddef.h>
#include <stdint.h>
typedef struct FlowAdaptiveController FlowAdaptiveController;
typedef struct { const char *flow_name, *domain_contract, *resource, *capability; size_t input_capacity, memory_limit_bytes; int require_parallelizable; } FlowAdaptivePolicy;
typedef struct { size_t sample_window, cooldown_calls, journal_capacity; double min_improvement_percent; FlowAdaptivePolicy policy; } FlowAdaptiveConfig;
typedef struct { uint64_t calls, failures, total_ns; } FlowAdaptiveMetrics;
typedef struct { const char *name, *flow_binding, *domain_contract, *resource, *capability; const FlowUnit *unit; int latency_score, memory_score, supports_parallelizable;
    size_t memory_fixed_bytes, memory_bytes_per_capacity; uint64_t plan_schema_hash; } FlowAdaptiveCandidate;
typedef int (*FlowAdaptiveProbe)(void *host_context, const FlowAdaptiveCandidate *candidate, const FlowAdaptiveMetrics *metrics, double *score_out);
typedef enum { FLOW_ADAPTIVE_OK = 0, FLOW_ADAPTIVE_NOT_READY = 1, FLOW_ADAPTIVE_NO_CHANGE = 2, FLOW_ADAPTIVE_INVALID = 3, FLOW_ADAPTIVE_RELOAD_FAILED = 4 } FlowAdaptiveStatus;
typedef struct { uint64_t l3_cache_misses, l3_cache_references, branch_mispredictions, page_faults, memory_bandwidth_bytes, cpu_cycles, instructions; double cache_miss_rate, ipc; } FlowPMUTelemetry;
FlowAdaptiveController *flow_adaptive_create(FlowReloadContext *context, void *host_context, const FlowAdaptiveConfig *config, const FlowAdaptiveCandidate *candidates, size_t candidate_count, size_t current_index, FlowAdaptiveProbe probe);
int flow_adaptive_destroy(FlowAdaptiveController *controller);
int flow_adaptive_call(FlowAdaptiveController *controller, FlowReloadReader *reader, const void *input, void *output);
size_t flow_adaptive_current_index(const FlowAdaptiveController *controller);
uint64_t flow_adaptive_telemetry_bias_from_pmu(const FlowPMUTelemetry *pmu, int write_heavy_state, const FlowPlanDimensionSet *dims);
FlowAdaptiveStatus flow_adaptive_handle_pressure_event(FlowAdaptiveController *controller, const FlowEnvironmentState *env, size_t *morphed_candidate_index_out);
typedef struct { double drop_threshold, recovery_threshold; uint64_t dwell_time_required_ns, stable_since_ns; int current_state; } FlowSchmittTrigger;
void flow_schmitt_trigger_init(FlowSchmittTrigger *st, double base_min, uint64_t dwell_ns);
int flow_schmitt_trigger_update(FlowSchmittTrigger *st, double current_val, uint64_t current_time_ns, int *state_changed_out);
#endif
