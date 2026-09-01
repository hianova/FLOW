#include "adaptive.h"

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
    return controller;
}

int flow_adaptive_destroy(FlowAdaptiveController *controller) {
    if (controller == NULL) return FLOW_ADAPTIVE_INVALID;
    pthread_mutex_destroy(&controller->lock);
    free(controller->candidates);
    free(controller);
    return FLOW_ADAPTIVE_OK;
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
