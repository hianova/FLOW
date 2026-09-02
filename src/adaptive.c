#include "adaptive.h"
#include "registry.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct FlowAdaptiveController {
    FlowReloadContext *context;
    void *host_context;
    FlowAdaptiveConfig config;
    FlowAdaptiveCandidate *candidates;
    size_t candidate_count;
    size_t current_index;
    size_t calls_since_switch;
    int evaluating;
    FlowAdaptiveProbe probe;
    FlowAdaptiveMetrics metrics;
    FlowPMUTelemetry pmu;
    FlowIPRangeTracker ip_tracker;
    FlowAntiThrashingConfig anti_thrash;
    FlowDebounceState debounce;
    const FlowUnit *golden_unit;
    void *golden_state;
    _Atomic int is_running_golden;
    _Atomic size_t consecutive_errors;
    pthread_mutex_t lock;
};

static uint64_t clock_ns(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) +
           (uint64_t)now.tv_nsec;
}

static int config_valid(const FlowAdaptiveConfig *config) {
    return config != NULL && config->sample_window != 0 &&
           config->cooldown_calls <= SIZE_MAX - config->sample_window &&
           config->journal_capacity != 0 &&
           config->min_improvement_percent >= 0.0 &&
           config->min_improvement_percent < 100.0;
}

static int candidate_matches_policy(const FlowAdaptiveConfig *config,
                                    const FlowAdaptiveCandidate *candidate) {
    const FlowAdaptivePolicy *policy = &config->policy;
    size_t estimated;
    if (policy->flow_name != NULL && policy->flow_name[0] != '\0' &&
        candidate->flow_binding != NULL && candidate->flow_binding[0] != '\0' &&
        strcmp(policy->flow_name, candidate->flow_binding) != 0 &&
        !(policy->require_parallelizable &&
          candidate->supports_parallelizable))
        return 0;
    if (policy->domain_contract != NULL && policy->domain_contract[0] != '\0' &&
        (candidate->domain_contract == NULL ||
         strcmp(policy->domain_contract, candidate->domain_contract) != 0))
        return 0;
    if (policy->resource != NULL && policy->resource[0] != '\0' &&
        (candidate->resource == NULL ||
         strcmp(policy->resource, candidate->resource) != 0))
        return 0;
    if (policy->capability != NULL && policy->capability[0] != '\0' &&
        (candidate->capability == NULL ||
         strcmp(policy->capability, candidate->capability) != 0))
        return 0;
    if (policy->require_parallelizable &&
        !candidate->supports_parallelizable)
        return 0;
    if (policy->input_capacity == 0)
        return policy->memory_limit_bytes == 0 ||
               candidate->memory_fixed_bytes <= policy->memory_limit_bytes;
    if (candidate->memory_bytes_per_capacity != 0 &&
        policy->input_capacity >
            (SIZE_MAX - candidate->memory_fixed_bytes) /
                candidate->memory_bytes_per_capacity)
        return 0;
    estimated = candidate->memory_fixed_bytes +
                policy->input_capacity * candidate->memory_bytes_per_capacity;
    return policy->memory_limit_bytes == 0 || estimated <= policy->memory_limit_bytes;
}

FlowAdaptiveController *flow_adaptive_create(
    FlowReloadContext *context, void *host_context,
    const FlowAdaptiveConfig *config,
    const FlowAdaptiveCandidate *candidates, size_t candidate_count,
    size_t current_index, FlowAdaptiveProbe probe) {
    FlowAdaptiveController *controller;
    size_t i;
    if (context == NULL || !config_valid(config) || candidates == NULL ||
        candidate_count == 0 || current_index >= candidate_count ||
        probe == NULL)
        return NULL;
    if (flow_reload_current_unit(context) != candidates[current_index].unit)
        return NULL;
    for (i = 0; i < candidate_count; ++i)
        if (candidates[i].name == NULL || candidates[i].unit == NULL ||
            candidates[i].unit->name == NULL ||
            strcmp(candidates[i].name, candidates[i].unit->name) != 0 ||
            !candidate_matches_policy(config, &candidates[i]))
            return NULL;
    controller = calloc(1, sizeof(*controller));
    if (controller == NULL ||
        candidate_count > SIZE_MAX / sizeof(*controller->candidates)) {
        free(controller);
        return NULL;
    }
    controller->candidates = calloc(candidate_count,
                                    sizeof(*controller->candidates));
    if (controller->candidates == NULL) {
        free(controller);
        return NULL;
    }
    if (pthread_mutex_init(&controller->lock, NULL) != 0) {
        free(controller->candidates);
        free(controller);
        return NULL;
    }
    memcpy(controller->candidates, candidates,
           candidate_count * sizeof(*controller->candidates));
    controller->context = context;
    controller->host_context = host_context;
    controller->config = *config;
    controller->candidate_count = candidate_count;
    controller->current_index = current_index;
    controller->probe = probe;
    controller->anti_thrash.ema_alpha = 0.25;
    controller->anti_thrash.anomaly_streak_required = 1;
    controller->anti_thrash.cooldown_ticks = 0;
    controller->anti_thrash.backoff_multiplier = 1.5;
    controller->debounce.effective_cooldown_ticks = 0;
    return controller;
}

int flow_adaptive_destroy(FlowAdaptiveController *controller) {
    if (controller == NULL) return FLOW_ADAPTIVE_INVALID;
    pthread_mutex_destroy(&controller->lock);
    free(controller->candidates);
    free(controller);
    return FLOW_ADAPTIVE_OK;
}

int flow_adaptive_register_ip_range(FlowAdaptiveController *controller,
                                   uintptr_t start_ip, uintptr_t end_ip,
                                   const char *name, uint32_t candidate_index) {
    if (controller == NULL || start_ip >= end_ip) return 0;
    pthread_mutex_lock(&controller->lock);
    if (controller->ip_tracker.range_count >= FLOW_MAX_IP_RANGES) {
        pthread_mutex_unlock(&controller->lock);
        return 0;
    }
    FlowIPRange *r = &controller->ip_tracker.ranges[controller->ip_tracker.range_count++];
    r->start_ip = start_ip;
    r->end_ip = end_ip;
    r->candidate_index = candidate_index;
    strncpy(r->name, name ? name : "", sizeof(r->name) - 1);
    pthread_mutex_unlock(&controller->lock);
    return 1;
}

int flow_adaptive_is_ip_attributed(const FlowAdaptiveController *controller,
                                  uintptr_t ip, uint32_t *candidate_index_out) {
    if (controller == NULL) return 0;
    for (size_t i = 0; i < controller->ip_tracker.range_count; ++i) {
        const FlowIPRange *r = &controller->ip_tracker.ranges[i];
        if (ip >= r->start_ip && ip < r->end_ip) {
            if (candidate_index_out != NULL) *candidate_index_out = r->candidate_index;
            return 1;
        }
    }
    return 0;
}

int flow_adaptive_feed_attributed_pmu(FlowAdaptiveController *controller,
                                      uintptr_t ip, const FlowPMUTelemetry *pmu) {
    uint32_t cand_idx = 0;
    if (controller == NULL || pmu == NULL) return FLOW_ADAPTIVE_INVALID;
    if (!flow_adaptive_is_ip_attributed(controller, ip, &cand_idx)) return FLOW_ADAPTIVE_INVALID;
    return flow_adaptive_feed_pmu(controller, pmu);
}

void flow_adaptive_set_anti_thrashing(FlowAdaptiveController *controller,
                                      const FlowAntiThrashingConfig *config) {
    if (controller == NULL || config == NULL) return;
    pthread_mutex_lock(&controller->lock);
    controller->anti_thrash = *config;
    controller->debounce.effective_cooldown_ticks = config->cooldown_ticks;
    pthread_mutex_unlock(&controller->lock);
}

int flow_adaptive_get_debounce_state(const FlowAdaptiveController *controller,
                                     FlowDebounceState *state_out) {
    if (controller == NULL || state_out == NULL) return 0;
    *state_out = controller->debounce;
    return 1;
}

int flow_adaptive_call(FlowAdaptiveController *controller,
                       FlowReloadReader *reader, const void *input,
                       void *output) {
    uint64_t start;
    uint64_t elapsed;
    int result;
    if (controller == NULL || reader == NULL) return FLOW_ADAPTIVE_INVALID;
    start = clock_ns();
    result = flow_reload_call(controller->context, reader, input, output);
    elapsed = clock_ns() - start;
    pthread_mutex_lock(&controller->lock);
    ++controller->metrics.calls;
    controller->metrics.total_ns += elapsed;
    ++controller->calls_since_switch;
    if (result != FLOW_RELOAD_OK) ++controller->metrics.failures;
    pthread_mutex_unlock(&controller->lock);
    return result;
}

FlowAdaptiveStatus flow_adaptive_tick(FlowAdaptiveController *controller) {
    FlowAdaptiveMetrics metrics;
    double current_score;
    double best_score;
    size_t best_index;
    size_t current_index;
    size_t i;
    int result;
    if (controller == NULL) return FLOW_ADAPTIVE_INVALID;

    pthread_mutex_lock(&controller->lock);
    if (controller->evaluating ||
        controller->metrics.calls < controller->config.sample_window ||
        controller->calls_since_switch < controller->config.cooldown_calls) {
        pthread_mutex_unlock(&controller->lock);
        return FLOW_ADAPTIVE_NOT_READY;
    }
    if (flow_reload_current_unit(controller->context) !=
        controller->candidates[controller->current_index].unit) {
        pthread_mutex_unlock(&controller->lock);
        return FLOW_ADAPTIVE_NO_CHANGE;
    }
    controller->evaluating = 1;
    metrics = controller->metrics;
    controller->metrics = (FlowAdaptiveMetrics){0};
    current_index = controller->current_index;
    pthread_mutex_unlock(&controller->lock);

    if (controller->probe(controller->host_context,
                          &controller->candidates[current_index],
                          &metrics, &current_score) != 0) {
        pthread_mutex_lock(&controller->lock);
        controller->evaluating = 0;
        pthread_mutex_unlock(&controller->lock);
        return FLOW_ADAPTIVE_NO_CHANGE;
    }
    best_index = current_index;
    best_score = current_score;
    for (i = 0; i < controller->candidate_count; ++i) {
        double candidate_score;
        if (i == current_index) continue;
        if (controller->probe(controller->host_context,
                              &controller->candidates[i], &metrics,
                              &candidate_score) != 0)
            continue;
        if (candidate_score < best_score) {
            best_score = candidate_score;
            best_index = i;
        }
    }
    if (best_index == current_index ||
        current_score <= 0.0 ||
        (current_score - best_score) * 100.0 <
            current_score * controller->config.min_improvement_percent) {
        pthread_mutex_lock(&controller->lock);
        controller->evaluating = 0;
        pthread_mutex_unlock(&controller->lock);
        return FLOW_ADAPTIVE_NO_CHANGE;
    }
    if (flow_reload_current_unit(controller->context) !=
        controller->candidates[current_index].unit) {
        pthread_mutex_lock(&controller->lock);
        controller->evaluating = 0;
        pthread_mutex_unlock(&controller->lock);
        return FLOW_ADAPTIVE_NO_CHANGE;
    }
    result = flow_reload_live_begin(
        controller->context, controller->candidates[best_index].unit,
        controller->config.journal_capacity);
    if (result == FLOW_RELOAD_OK)
        result = flow_reload_live_finish(controller->context);
    if (result != FLOW_RELOAD_OK) {
        pthread_mutex_lock(&controller->lock);
        controller->evaluating = 0;
        pthread_mutex_unlock(&controller->lock);
        return FLOW_ADAPTIVE_RELOAD_FAILED;
    }
    if (flow_reload_current_unit(controller->context) !=
        controller->candidates[best_index].unit) {
        pthread_mutex_lock(&controller->lock);
        controller->evaluating = 0;
        pthread_mutex_unlock(&controller->lock);
        return FLOW_ADAPTIVE_NO_CHANGE;
    }
    pthread_mutex_lock(&controller->lock);
    if (controller->current_index != current_index) {
        controller->evaluating = 0;
        pthread_mutex_unlock(&controller->lock);
        return FLOW_ADAPTIVE_NO_CHANGE;
    }
    controller->current_index = best_index;
    controller->calls_since_switch = 0;
    controller->evaluating = 0;
    pthread_mutex_unlock(&controller->lock);
    return FLOW_ADAPTIVE_OK;
}

size_t flow_adaptive_current_index(const FlowAdaptiveController *controller) {
    size_t index;
    if (controller == NULL) return SIZE_MAX;
    pthread_mutex_lock((pthread_mutex_t *)&controller->lock);
    index = controller->current_index;
    pthread_mutex_unlock((pthread_mutex_t *)&controller->lock);
    return index;
}

int flow_adaptive_metrics(const FlowAdaptiveController *controller,
                          FlowAdaptiveMetrics *metrics_out) {
    if (controller == NULL || metrics_out == NULL)
        return FLOW_ADAPTIVE_INVALID;
    pthread_mutex_lock((pthread_mutex_t *)&controller->lock);
    *metrics_out = controller->metrics;
    pthread_mutex_unlock((pthread_mutex_t *)&controller->lock);
    return FLOW_ADAPTIVE_OK;
}

int flow_adaptive_feed_pmu(FlowAdaptiveController *controller,
                           const FlowPMUTelemetry *pmu) {
    if (controller == NULL || pmu == NULL) return FLOW_ADAPTIVE_INVALID;
    pthread_mutex_lock(&controller->lock);
    controller->pmu = *pmu;
    if (pmu->l3_cache_references > 0) {
        controller->pmu.cache_miss_rate =
            (double)pmu->l3_cache_misses / (double)pmu->l3_cache_references;
    }
    if (pmu->cpu_cycles > 0) {
        controller->pmu.ipc =
            (double)pmu->instructions / (double)pmu->cpu_cycles;
    }
    pthread_mutex_unlock(&controller->lock);
    return FLOW_ADAPTIVE_OK;
}

int flow_adaptive_pmu_metrics(const FlowAdaptiveController *controller,
                              FlowPMUTelemetry *pmu_out) {
    if (controller == NULL || pmu_out == NULL) return FLOW_ADAPTIVE_INVALID;
    pthread_mutex_lock((pthread_mutex_t *)&controller->lock);
    *pmu_out = controller->pmu;
    pthread_mutex_unlock((pthread_mutex_t *)&controller->lock);
    return FLOW_ADAPTIVE_OK;
}

FlowAdaptiveStatus flow_adaptive_tick_pmu(FlowAdaptiveController *controller,
                                         const FlowPMUThresholds *thresholds) {
    FlowPMUTelemetry pmu;
    size_t current_index;
    size_t target_index;
    size_t i;
    int high_miss_rate = 0;
    int low_ipc = 0;
    int is_anomaly = 0;
    int result;
    double alpha;

    if (controller == NULL || thresholds == NULL) return FLOW_ADAPTIVE_INVALID;

    pthread_mutex_lock(&controller->lock);
    if (controller->evaluating) {
        pthread_mutex_unlock(&controller->lock);
        return FLOW_ADAPTIVE_NOT_READY;
    }
    pmu = controller->pmu;
    current_index = controller->current_index;
    controller->evaluating = 1;
    controller->debounce.ticks_since_last_swap++;

    /* Exponential Moving Average (EMA) Smoothing */
    alpha = controller->anti_thrash.ema_alpha > 0.0 ? controller->anti_thrash.ema_alpha : 0.25;
    if (controller->debounce.smoothed_miss_rate == 0.0) {
        controller->debounce.smoothed_miss_rate = pmu.cache_miss_rate;
    } else {
        controller->debounce.smoothed_miss_rate =
            (1.0 - alpha) * controller->debounce.smoothed_miss_rate + alpha * pmu.cache_miss_rate;
    }

    if (controller->debounce.smoothed_ipc == 0.0) {
        controller->debounce.smoothed_ipc = pmu.ipc;
    } else {
        controller->debounce.smoothed_ipc =
            (1.0 - alpha) * controller->debounce.smoothed_ipc + alpha * pmu.ipc;
    }

    /* Anomaly Detection over Smoothed Signal */
    if (thresholds->cache_miss_rate_threshold > 0.0 &&
        controller->debounce.smoothed_miss_rate >= thresholds->cache_miss_rate_threshold) {
        high_miss_rate = 1;
        is_anomaly = 1;
    }
    if (thresholds->min_ipc_threshold > 0.0 &&
        controller->debounce.smoothed_ipc > 0.0 &&
        controller->debounce.smoothed_ipc <= thresholds->min_ipc_threshold) {
        low_ipc = 1;
        is_anomaly = 1;
    }

    if (is_anomaly) {
        controller->debounce.current_anomaly_streak++;
    } else {
        controller->debounce.current_anomaly_streak = 0;
    }

    /* Check Anomaly Streak Requirement */
    if (controller->debounce.current_anomaly_streak < controller->anti_thrash.anomaly_streak_required) {
        controller->evaluating = 0;
        pthread_mutex_unlock(&controller->lock);
        return FLOW_ADAPTIVE_NO_CHANGE;
    }

    /* Check Anti-Thrashing Cooldown Window */
    if (controller->debounce.ticks_since_last_swap <= controller->debounce.effective_cooldown_ticks &&
        controller->debounce.swap_count > 0) {
        controller->evaluating = 0;
        pthread_mutex_unlock(&controller->lock);
        return FLOW_ADAPTIVE_NOT_READY;
    }

    pthread_mutex_unlock(&controller->lock);

    target_index = current_index;
    if (high_miss_rate) {
        /* Find candidate with best memory locality / minimal score */
        int best_mem = controller->candidates[current_index].memory_score;
        for (i = 0; i < controller->candidate_count; ++i) {
            if (controller->candidates[i].memory_score < best_mem) {
                best_mem = controller->candidates[i].memory_score;
                target_index = i;
            }
        }
    } else if (low_ipc) {
        /* Find candidate with lowest latency / highest throughput */
        int best_lat = controller->candidates[current_index].latency_score;
        for (i = 0; i < controller->candidate_count; ++i) {
            if (controller->candidates[i].latency_score < best_lat) {
                best_lat = controller->candidates[i].latency_score;
                target_index = i;
            }
        }
    }

    if (target_index == current_index) {
        pthread_mutex_lock(&controller->lock);
        controller->evaluating = 0;
        pthread_mutex_unlock(&controller->lock);
        return FLOW_ADAPTIVE_NO_CHANGE;
    }

    /* Live hot-swap to the hardware-adapted candidate */
    result = flow_reload_live_begin(
        controller->context, controller->candidates[target_index].unit,
        controller->config.journal_capacity);
    if (result == FLOW_RELOAD_OK)
        result = flow_reload_live_finish(controller->context);

    pthread_mutex_lock(&controller->lock);
    controller->evaluating = 0;
    if (result != FLOW_RELOAD_OK) {
        pthread_mutex_unlock(&controller->lock);
        return FLOW_ADAPTIVE_RELOAD_FAILED;
    }
    controller->current_index = target_index;
    controller->calls_since_switch = 0;
    controller->debounce.ticks_since_last_swap = 0;
    controller->debounce.current_anomaly_streak = 0;
    controller->debounce.swap_count++;
    /* Exponential backoff on cooldown */
    if (controller->anti_thrash.backoff_multiplier > 1.0 && controller->debounce.effective_cooldown_ticks > 0) {
        controller->debounce.effective_cooldown_ticks =
            (size_t)(controller->debounce.effective_cooldown_ticks * controller->anti_thrash.backoff_multiplier);
    }
    pthread_mutex_unlock(&controller->lock);
    return FLOW_ADAPTIVE_OK;
}

const char *flow_adaptive_status_name(FlowAdaptiveStatus status) {
    switch (status) {
        case FLOW_ADAPTIVE_OK: return "ok";
        case FLOW_ADAPTIVE_NOT_READY: return "not_ready";
        case FLOW_ADAPTIVE_NO_CHANGE: return "no_change";
        case FLOW_ADAPTIVE_INVALID: return "invalid";
        case FLOW_ADAPTIVE_RELOAD_FAILED: return "reload_failed";
    }
    return "unknown";
}

uint64_t flow_adaptive_telemetry_bias_from_pmu(const FlowPMUTelemetry *pmu,
                                               int write_heavy_state,
                                               const FlowPlanDimensionSet *dims) {
    if (dims == NULL || dims->count == 0) return 0;
    uint64_t bias_mask = 0;
    unsigned shift = 0;

    int bias_contention = write_heavy_state;
    int bias_cache_locality = (pmu != NULL && pmu->cache_miss_rate > 0.30);
    int bias_concurrency = (pmu != NULL && pmu->ipc > 0.0 && pmu->ipc < 0.8);

    for (size_t i = 0; i < dims->count; ++i) {
        const FlowPlanDimension *d = &dims->dimensions[i];
        unsigned bits = flow_dimension_bits(d);
        if (bits == 0) continue;
        uint64_t dim_mask = (bits >= 64) ? (uint64_t)-1 : (((uint64_t)1 << bits) - 1);

        int should_bias = 0;
        if (bias_contention && (strcmp(d->name, "shards") == 0 || strcmp(d->name, "buffer_bytes") == 0 || strcmp(d->name, "growth_percent") == 0)) {
            should_bias = 1;
        }
        if (bias_cache_locality && (strcmp(d->name, "buffer_bytes") == 0 || strcmp(d->name, "arena_bytes") == 0 || strcmp(d->name, "initial_capacity") == 0)) {
            should_bias = 1;
        }
        if (bias_concurrency && (strcmp(d->name, "threads") == 0 || strcmp(d->name, "batch_size") == 0)) {
            should_bias = 1;
        }
        if (!bias_contention && !bias_cache_locality && !bias_concurrency) {
            /* Default: tactile parameters prioritized for real-time micro-tuning */
            if (d->dim_class == FLOW_DIM_CLASS_TACTILE_PARAM) {
                should_bias = 1;
            }
        }

        if (should_bias) {
            bias_mask |= (dim_mask << shift);
        }
        shift += bits;
    }
    return bias_mask;
}

uint64_t flow_adaptive_get_telemetry_bias(const FlowAdaptiveController *controller,
                                          const Component *comp,
                                          const FlowPlanDimensionSet *dims) {
    (void)comp;
    if (controller == NULL || dims == NULL) return 0;
    FlowPMUTelemetry pmu;
    int write_heavy = 0;
    pthread_mutex_lock((pthread_mutex_t *)&controller->lock);
    pmu = controller->pmu;
    if (controller->metrics.calls > 0 && controller->metrics.failures > 0) {
        write_heavy = 1;
    }
    pthread_mutex_unlock((pthread_mutex_t *)&controller->lock);
    return flow_adaptive_telemetry_bias_from_pmu(&pmu, write_heavy, dims);
}

uint64_t flow_adaptive_synthesize_env_mask(const FlowAdaptiveController *controller,
                                           const FlowEnvironmentState *env,
                                           const Component *comp,
                                           const FlowPlanDimensionSet *dims) {
    (void)controller;
    return flow_component_environment_mask(NULL, comp, dims, env);
}

FlowAdaptiveStatus flow_adaptive_handle_pressure_event(
    FlowAdaptiveController *controller,
    const FlowEnvironmentState *env,
    size_t *morphed_candidate_index_out) {
    if (controller == NULL || env == NULL) return FLOW_ADAPTIVE_INVALID;

    size_t best_idx = (size_t)-1;
    double best_suitability = -1.0e9;

    pthread_mutex_lock(&controller->lock);
    size_t curr = controller->current_index;

    for (size_t i = 0; i < controller->candidate_count; ++i) {
        const FlowAdaptiveCandidate *cand = &controller->candidates[i];
        double score = 0.0;

        if (env->pressure_level == FLOW_ENV_PRESSURE_MEMORY_CRITICAL) {
            /* Under critical memory pressure: high memory_score candidate is top priority */
            score = (double)cand->memory_score * 100.0 - (double)cand->memory_fixed_bytes / 1024.0;
        } else if (env->pressure_level == FLOW_ENV_PRESSURE_CACHE_THRASHING) {
            score = (double)cand->latency_score * 50.0 + (double)cand->memory_score * 20.0;
        } else if (env->pressure_level == FLOW_ENV_PRESSURE_LATENCY_SPIKE) {
            score = (double)cand->latency_score * 100.0;
        } else {
            /* Normal */
            score = (double)(cand->latency_score + cand->memory_score);
        }

        if (score > best_suitability) {
            best_suitability = score;
            best_idx = i;
        }
    }

    if (best_idx == (size_t)-1 || best_idx == curr) {
        pthread_mutex_unlock(&controller->lock);
        if (morphed_candidate_index_out) *morphed_candidate_index_out = curr;
        return FLOW_ADAPTIVE_NO_CHANGE;
    }

    /* Perform live hot reload to morphed layout */
    const FlowUnit *target_unit = controller->candidates[best_idx].unit;
    int reload_res = flow_reload_activate(controller->context, target_unit);
    if (reload_res != FLOW_RELOAD_OK) {
        pthread_mutex_unlock(&controller->lock);
        return FLOW_ADAPTIVE_RELOAD_FAILED;
    }

    controller->current_index = best_idx;
    controller->calls_since_switch = 0;
    controller->debounce.swap_count++;
    pthread_mutex_unlock(&controller->lock);

    if (morphed_candidate_index_out) *morphed_candidate_index_out = best_idx;
    return FLOW_ADAPTIVE_OK;
}

/* ========================================================================= */
/* Golden Baseline Fallback & Anomaly Protection                             */
/* ========================================================================= */

int flow_adaptive_set_golden_baseline(FlowAdaptiveController *controller,
                                      const FlowUnit *golden_unit,
                                      void *golden_state) {
    if (controller == NULL || golden_unit == NULL) return 0;
    pthread_mutex_lock(&controller->lock);
    controller->golden_unit = golden_unit;
    controller->golden_state = golden_state;
    atomic_store_explicit(&controller->is_running_golden, 0, memory_order_release);
    atomic_store_explicit(&controller->consecutive_errors, 0, memory_order_release);
    pthread_mutex_unlock(&controller->lock);
    return 1;
}

int flow_adaptive_fallback_to_golden_baseline(FlowAdaptiveController *controller,
                                              const char *reason_diagnostic) {
    if (controller == NULL || controller->golden_unit == NULL) return 0;

    pthread_mutex_lock(&controller->lock);
    const FlowUnit *golden = controller->golden_unit;
    int reload_res;
    if (controller->golden_state != NULL) {
        reload_res = flow_reload_publish(controller->context, golden, controller->golden_state);
    } else {
        reload_res = flow_reload_activate(controller->context, golden);
    }

    if (reload_res == FLOW_RELOAD_OK) {
        atomic_store_explicit(&controller->is_running_golden, 1, memory_order_release);
        atomic_store_explicit(&controller->consecutive_errors, 0, memory_order_release);

        /* Record snapshot in audit trail */
        FlowMutationSnapshot snap;
        memset(&snap, 0, sizeof(snap));
        snap.timestamp_ns = clock_ns();
        snap.is_golden_fallback = 1;
        strncpy(snap.component_id, golden->schema ? golden->schema->name : "golden_baseline", sizeof(snap.component_id) - 1);
        strncpy(snap.author_attestation, reason_diagnostic ? reason_diagnostic : "OOD_SPIKE_FALLBACK", sizeof(snap.author_attestation) - 1);
        flow_audit_trail_record(controller->context, &snap);
    }
    pthread_mutex_unlock(&controller->lock);
    return reload_res == FLOW_RELOAD_OK;
}

int flow_adaptive_record_error_and_check_fallback(FlowAdaptiveController *controller,
                                                  size_t max_consecutive_errors) {
    if (controller == NULL) return 0;
    size_t threshold = max_consecutive_errors > 0 ? max_consecutive_errors : 3;
    size_t errs = (size_t)atomic_fetch_add_explicit(&controller->consecutive_errors, 1, memory_order_acq_rel) + 1;
    if (errs >= threshold && controller->golden_unit != NULL) {
        return flow_adaptive_fallback_to_golden_baseline(controller, "EXCEEDED_CONSECUTIVE_ERROR_THRESHOLD");
    }
    return 0;
}

int flow_adaptive_is_running_golden(const FlowAdaptiveController *controller) {
    if (controller == NULL) return 0;
    return atomic_load_explicit(&controller->is_running_golden, memory_order_acquire);
}

/* ========================================================================= */
/* Schmitt Trigger Anti-Flapping & Hysteresis Controller Implementation      */
/* ========================================================================= */

void flow_schmitt_trigger_init(FlowSchmittTrigger *st, double base_min, uint64_t dwell_ns) {
    if (st == NULL) return;
    memset(st, 0, sizeof(*st));
    st->drop_threshold = base_min * 0.8;      /* 80% of minimum threshold */
    st->recovery_threshold = base_min * 1.5;  /* 150% of minimum threshold */
    st->dwell_time_required_ns = dwell_ns > 0 ? dwell_ns : 500000000ULL; /* 500ms default */
    st->stable_since_ns = 0;
    st->current_state = 0; /* Nominal */
}

int flow_schmitt_trigger_update(FlowSchmittTrigger *st, double current_val, uint64_t current_time_ns, int *state_changed_out) {
    if (st == NULL) return 0;
    if (state_changed_out) *state_changed_out = 0;

    if (st->current_state == 0) {
        /* Nominal Mode -> Check for Drop Threshold breach */
        if (current_val < st->drop_threshold) {
            st->current_state = 1; /* Instant switch to survival mode */
            st->stable_since_ns = 0;
            if (state_changed_out) *state_changed_out = 1;
        }
    } else {
        /* Survival Mode -> Check for sustained recovery above upper threshold */
        if (current_val >= st->recovery_threshold) {
            if (st->stable_since_ns == 0) {
                st->stable_since_ns = current_time_ns;
            } else if ((current_time_ns - st->stable_since_ns) >= st->dwell_time_required_ns) {
                st->current_state = 0; /* Recover back to nominal mode after dwell period */
                st->stable_since_ns = 0;
                if (state_changed_out) *state_changed_out = 1;
            }
        } else {
            /* Flapping reset */
            st->stable_since_ns = 0;
        }
    }

    return st->current_state;
}

