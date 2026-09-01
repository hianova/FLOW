#ifndef FLOW_ADAPTIVE_H
#define FLOW_ADAPTIVE_H

#include "reload.h"
#include "plugin.h"

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

#define FLOW_MAX_IP_RANGES 16

typedef struct {
    uintptr_t start_ip;
    uintptr_t end_ip;
    char name[64];
    uint32_t candidate_index;
} FlowIPRange;

typedef struct {
    FlowIPRange ranges[FLOW_MAX_IP_RANGES];
    size_t range_count;
} FlowIPRangeTracker;

typedef struct {
    double ema_alpha;              /* Smoothing factor: default 0.25 */
    size_t anomaly_streak_required;/* Must persist across N sample windows: default 3 */
    size_t cooldown_ticks;         /* Ticks between hot-swaps: default 5 */
    double backoff_multiplier;     /* Backoff multiplier on rapid switches: default 1.5 */
} FlowAntiThrashingConfig;

typedef struct {
    double smoothed_miss_rate;
    double smoothed_ipc;
    size_t current_anomaly_streak;
    size_t ticks_since_last_swap;
    size_t effective_cooldown_ticks;
    size_t swap_count;
} FlowDebounceState;

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

/* Silicon-Grade Attribution & Anti-Thrashing */
int flow_adaptive_register_ip_range(FlowAdaptiveController *controller,
                                   uintptr_t start_ip, uintptr_t end_ip,
                                   const char *name, uint32_t candidate_index);
int flow_adaptive_is_ip_attributed(const FlowAdaptiveController *controller,
                                  uintptr_t ip, uint32_t *candidate_index_out);
int flow_adaptive_feed_attributed_pmu(FlowAdaptiveController *controller,
                                      uintptr_t ip, const FlowPMUTelemetry *pmu);
void flow_adaptive_set_anti_thrashing(FlowAdaptiveController *controller,
                                      const FlowAntiThrashingConfig *config);
int flow_adaptive_get_debounce_state(const FlowAdaptiveController *controller,
                                     FlowDebounceState *state_out);

size_t flow_adaptive_current_index(const FlowAdaptiveController *controller);
int flow_adaptive_metrics(const FlowAdaptiveController *controller,
                          FlowAdaptiveMetrics *metrics_out);
const char *flow_adaptive_status_name(FlowAdaptiveStatus status);

/* Dynamic Telemetry Bias Generator (Soft/Dynamic Chaos Biasing) */
uint64_t flow_adaptive_telemetry_bias_from_pmu(const FlowPMUTelemetry *pmu,
                                               int write_heavy_state,
                                               const FlowPlanDimensionSet *dims);

uint64_t flow_adaptive_get_telemetry_bias(const FlowAdaptiveController *controller,
                                          const Component *comp,
                                          const FlowPlanDimensionSet *dims);

/* Dynamic Environment Pressure & Instant Morphing API */
uint64_t flow_adaptive_synthesize_env_mask(const FlowAdaptiveController *controller,
                                           const FlowEnvironmentState *env,
                                           const Component *comp,
                                           const FlowPlanDimensionSet *dims);

FlowAdaptiveStatus flow_adaptive_handle_pressure_event(
    FlowAdaptiveController *controller,
    const FlowEnvironmentState *env,
    size_t *morphed_candidate_index_out);

#endif
