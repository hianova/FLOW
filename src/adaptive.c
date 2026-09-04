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
    size_t candidate_count, current_index, calls_since_switch;
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

static const double PRESSURE_WEIGHTS[6][3] = {
    [FLOW_ENV_PRESSURE_NONE]            = {1.0, 1.0, 0.0},
    [FLOW_ENV_PRESSURE_MEMORY_MODERATE] = {1.0, 1.0, 0.0},
    [FLOW_ENV_PRESSURE_MEMORY_CRITICAL] = {0.0, 100.0, -1.0 / 1024.0},
    [FLOW_ENV_PRESSURE_CACHE_THRASHING] = {50.0, 20.0, 0.0},
    [FLOW_ENV_PRESSURE_LATENCY_SPIKE]   = {100.0, 0.0, 0.0},
    [FLOW_ENV_PRESSURE_BATTERY_SAVER]   = {1.0, 1.0, 0.0},
};

static const struct { const char *name; uint8_t mask; } BIAS_ATTRS[] = {
    {"shards", 1}, {"buffer_bytes", 3}, {"growth_percent", 1},
    {"arena_bytes", 2}, {"initial_capacity", 2}, {"threads", 4}, {"batch_size", 4}
};

static uint8_t lookup_bias_attr(const char *name) {
    for (size_t i = 0; i < sizeof(BIAS_ATTRS) / sizeof(BIAS_ATTRS[0]); ++i)
        if (!strcmp(BIAS_ATTRS[i].name, name)) return BIAS_ATTRS[i].mask;
    return 0;
}

static uint64_t clock_ns(void) {
    struct timespec now;
    return clock_gettime(CLOCK_MONOTONIC, &now) == 0 ? (uint64_t)now.tv_sec * 1000000000ULL + now.tv_nsec : 0;
}

static int config_valid(const FlowAdaptiveConfig *c) {
    return c && c->sample_window && c->cooldown_calls <= SIZE_MAX - c->sample_window &&
           c->journal_capacity && c->min_improvement_percent >= 0.0 && c->min_improvement_percent < 100.0;
}

static int candidate_matches_policy(const FlowAdaptiveConfig *config, const FlowAdaptiveCandidate *cand) {
    const FlowAdaptivePolicy *p = &config->policy;
    if (p->flow_name && p->flow_name[0] && cand->flow_binding && cand->flow_binding[0] &&
        strcmp(p->flow_name, cand->flow_binding) && !(p->require_parallelizable && cand->supports_parallelizable)) return 0;
    const char * const reqs[3] = {p->domain_contract, p->resource, p->capability};
    const char * const acts[3] = {cand->domain_contract, cand->resource, cand->capability};
    for (int i = 0; i < 3; ++i)
        if (reqs[i] && reqs[i][0] && (!acts[i] || strcmp(reqs[i], acts[i]))) return 0;
    if (p->require_parallelizable && !cand->supports_parallelizable) return 0;
    size_t cap = p->input_capacity, per = cand->memory_bytes_per_capacity, fix = cand->memory_fixed_bytes;
    if (cap && per && cap > (SIZE_MAX - fix) / per) return 0;
    return !p->memory_limit_bytes || (fix + cap * per) <= p->memory_limit_bytes;
}

FlowAdaptiveController *flow_adaptive_create(FlowReloadContext *ctx, void *host_ctx, const FlowAdaptiveConfig *cfg,
                                             const FlowAdaptiveCandidate *cands, size_t count, size_t cur_idx, FlowAdaptiveProbe probe) {
    if (!ctx || !config_valid(cfg) || !cands || !count || cur_idx >= count || !probe ||
        flow_reload_current_unit(ctx) != cands[cur_idx].unit) return NULL;
    for (size_t i = 0; i < count; ++i)
        if (!cands[i].name || !cands[i].unit || !cands[i].unit->name ||
            strcmp(cands[i].name, cands[i].unit->name) || !candidate_matches_policy(cfg, &cands[i])) return NULL;

    FlowAdaptiveController *ctrl = calloc(1, sizeof(*ctrl));
    if (!ctrl || !(ctrl->candidates = calloc(count, sizeof(*ctrl->candidates)))) { free(ctrl); return NULL; }
    if (pthread_mutex_init(&ctrl->lock, NULL)) { free(ctrl->candidates); free(ctrl); return NULL; }

    memcpy(ctrl->candidates, cands, count * sizeof(*cands));
    ctrl->context = ctx; ctrl->host_context = host_ctx; ctrl->config = *cfg;
    ctrl->candidate_count = count; ctrl->current_index = cur_idx; ctrl->probe = probe;
    ctrl->anti_thrash = (FlowAntiThrashingConfig){.ema_alpha = 0.25, .anomaly_streak_required = 1, .backoff_multiplier = 1.5};
    return ctrl;
}

int flow_adaptive_destroy(FlowAdaptiveController *ctrl) {
    if (!ctrl) return FLOW_ADAPTIVE_INVALID;
    pthread_mutex_destroy(&ctrl->lock);
    free(ctrl->candidates);
    free(ctrl);
    return FLOW_ADAPTIVE_OK;
}

int flow_adaptive_register_ip_range(FlowAdaptiveController *ctrl, uintptr_t start_ip, uintptr_t end_ip, const char *name, uint32_t cand_idx) {
    if (!ctrl || start_ip >= end_ip) return 0;
    pthread_mutex_lock(&ctrl->lock);
    int ok = ctrl->ip_tracker.range_count < FLOW_MAX_IP_RANGES;
    if (ok) {
        FlowIPRange *r = &ctrl->ip_tracker.ranges[ctrl->ip_tracker.range_count++];
        *r = (FlowIPRange){.start_ip = start_ip, .end_ip = end_ip, .candidate_index = cand_idx};
        strncpy(r->name, name ? name : "", sizeof(r->name) - 1);
    }
    pthread_mutex_unlock(&ctrl->lock);
    return ok;
}

int flow_adaptive_is_ip_attributed(const FlowAdaptiveController *ctrl, uintptr_t ip, uint32_t *cand_idx_out) {
    if (!ctrl) return 0;
    for (size_t i = 0; i < ctrl->ip_tracker.range_count; ++i) {
        const FlowIPRange *r = &ctrl->ip_tracker.ranges[i];
        if (ip >= r->start_ip && ip < r->end_ip) { if (cand_idx_out) *cand_idx_out = r->candidate_index; return 1; }
    }
    return 0;
}

int flow_adaptive_feed_attributed_pmu(FlowAdaptiveController *ctrl, uintptr_t ip, const FlowPMUTelemetry *pmu) {
    uint32_t idx = 0;
    return (ctrl && pmu && flow_adaptive_is_ip_attributed(ctrl, ip, &idx)) ? flow_adaptive_feed_pmu(ctrl, pmu) : FLOW_ADAPTIVE_INVALID;
}

void flow_adaptive_set_anti_thrashing(FlowAdaptiveController *ctrl, const FlowAntiThrashingConfig *cfg) {
    if (ctrl && cfg) {
        pthread_mutex_lock(&ctrl->lock);
        ctrl->anti_thrash = *cfg;
        ctrl->debounce.effective_cooldown_ticks = cfg->cooldown_ticks;
        pthread_mutex_unlock(&ctrl->lock);
    }
}

int flow_adaptive_get_debounce_state(const FlowAdaptiveController *ctrl, FlowDebounceState *out) {
    return (ctrl && out) ? (*out = ctrl->debounce, 1) : 0;
}

int flow_adaptive_call(FlowAdaptiveController *ctrl, FlowReloadReader *reader, const void *input, void *output) {
    if (!ctrl || !reader) return FLOW_ADAPTIVE_INVALID;
    uint64_t start = clock_ns();
    int res = flow_reload_call(ctrl->context, reader, input, output);
    uint64_t el = clock_ns() - start;
    pthread_mutex_lock(&ctrl->lock);
    ctrl->metrics.calls++; ctrl->metrics.total_ns += el; ctrl->calls_since_switch++;
    if (res != FLOW_RELOAD_OK) ctrl->metrics.failures++;
    pthread_mutex_unlock(&ctrl->lock);
    return res;
}

static FlowAdaptiveStatus commit_candidate_switch(FlowAdaptiveController *ctrl, size_t target_idx, int is_pmu) {
    int res = flow_reload_live_begin(ctrl->context, ctrl->candidates[target_idx].unit, ctrl->config.journal_capacity);
    if (res == FLOW_RELOAD_OK) res = flow_reload_live_finish(ctrl->context);
    pthread_mutex_lock(&ctrl->lock);
    ctrl->evaluating = 0;
    if (res != FLOW_RELOAD_OK) { pthread_mutex_unlock(&ctrl->lock); return FLOW_ADAPTIVE_RELOAD_FAILED; }
    ctrl->current_index = target_idx;
    ctrl->calls_since_switch = ctrl->debounce.ticks_since_last_swap = ctrl->debounce.current_anomaly_streak = 0;
    ctrl->debounce.swap_count++;
    if (is_pmu && ctrl->anti_thrash.backoff_multiplier > 1.0 && ctrl->debounce.effective_cooldown_ticks > 0)
        ctrl->debounce.effective_cooldown_ticks = (size_t)(ctrl->debounce.effective_cooldown_ticks * ctrl->anti_thrash.backoff_multiplier);
    pthread_mutex_unlock(&ctrl->lock);
    return FLOW_ADAPTIVE_OK;
}

FlowAdaptiveStatus flow_adaptive_tick(FlowAdaptiveController *ctrl) {
    if (!ctrl) return FLOW_ADAPTIVE_INVALID;
    pthread_mutex_lock(&ctrl->lock);
    if (ctrl->evaluating || ctrl->metrics.calls < ctrl->config.sample_window ||
        ctrl->calls_since_switch < ctrl->config.cooldown_calls) {
        pthread_mutex_unlock(&ctrl->lock); return FLOW_ADAPTIVE_NOT_READY;
    }
    if (flow_reload_current_unit(ctrl->context) != ctrl->candidates[ctrl->current_index].unit) {
        pthread_mutex_unlock(&ctrl->lock); return FLOW_ADAPTIVE_NO_CHANGE;
    }
    ctrl->evaluating = 1;
    FlowAdaptiveMetrics m = ctrl->metrics;
    ctrl->metrics = (FlowAdaptiveMetrics){0};
    size_t cur = ctrl->current_index;
    pthread_mutex_unlock(&ctrl->lock);

    double cur_score, best_score;
    if (ctrl->probe(ctrl->host_context, &ctrl->candidates[cur], &m, &cur_score) != 0) {
        pthread_mutex_lock(&ctrl->lock); ctrl->evaluating = 0; pthread_mutex_unlock(&ctrl->lock);
        return FLOW_ADAPTIVE_NO_CHANGE;
    }
    size_t best_idx = cur;
    best_score = cur_score;
    for (size_t i = 0; i < ctrl->candidate_count; ++i) {
        double score;
        if (i != cur && ctrl->probe(ctrl->host_context, &ctrl->candidates[i], &m, &score) == 0 && score < best_score) {
            best_score = score;
            best_idx = i;
        }
    }
    if (best_idx == cur || cur_score <= 0.0 || (cur_score - best_score) * 100.0 < cur_score * ctrl->config.min_improvement_percent) {
        pthread_mutex_lock(&ctrl->lock); ctrl->evaluating = 0; pthread_mutex_unlock(&ctrl->lock);
        return FLOW_ADAPTIVE_NO_CHANGE;
    }
    return commit_candidate_switch(ctrl, best_idx, 0);
}

FlowAdaptiveStatus flow_adaptive_tick_pmu(FlowAdaptiveController *ctrl, const FlowPMUThresholds *thresh) {
    if (!ctrl || !thresh) return FLOW_ADAPTIVE_INVALID;
    pthread_mutex_lock(&ctrl->lock);
    if (ctrl->evaluating) { pthread_mutex_unlock(&ctrl->lock); return FLOW_ADAPTIVE_NOT_READY; }
    ctrl->evaluating = 1;
    ctrl->debounce.ticks_since_last_swap++;

    double a = ctrl->anti_thrash.ema_alpha > 0.0 ? ctrl->anti_thrash.ema_alpha : 0.25;
    #define EMA(v, c) (v = !v ? (c) : (1.0 - a) * (v) + a * (c))
    EMA(ctrl->debounce.smoothed_miss_rate, ctrl->pmu.cache_miss_rate);
    EMA(ctrl->debounce.smoothed_ipc, ctrl->pmu.ipc);
    #undef EMA

    int high_miss = (thresh->cache_miss_rate_threshold > 0.0 && ctrl->debounce.smoothed_miss_rate >= thresh->cache_miss_rate_threshold);
    int low_ipc = (thresh->min_ipc_threshold > 0.0 && ctrl->debounce.smoothed_ipc > 0.0 && ctrl->debounce.smoothed_ipc <= thresh->min_ipc_threshold);
    ctrl->debounce.current_anomaly_streak = (high_miss || low_ipc) ? (ctrl->debounce.current_anomaly_streak + 1) : 0;

    int ready = (ctrl->debounce.current_anomaly_streak >= ctrl->anti_thrash.anomaly_streak_required);
    int cooled = (ctrl->debounce.ticks_since_last_swap > ctrl->debounce.effective_cooldown_ticks || ctrl->debounce.swap_count == 0);
    if (!ready || !cooled) {
        ctrl->evaluating = 0; pthread_mutex_unlock(&ctrl->lock);
        return !ready ? FLOW_ADAPTIVE_NO_CHANGE : FLOW_ADAPTIVE_NOT_READY;
    }
    size_t cur = ctrl->current_index;
    pthread_mutex_unlock(&ctrl->lock);

    size_t target = cur;
    int best_val = high_miss ? ctrl->candidates[cur].memory_score : ctrl->candidates[cur].latency_score;
    for (size_t i = 0; i < ctrl->candidate_count; ++i) {
        int val = high_miss ? ctrl->candidates[i].memory_score : ctrl->candidates[i].latency_score;
        if (val < best_val) { best_val = val; target = i; }
    }
    if (target == cur) {
        pthread_mutex_lock(&ctrl->lock); ctrl->evaluating = 0; pthread_mutex_unlock(&ctrl->lock);
        return FLOW_ADAPTIVE_NO_CHANGE;
    }
    return commit_candidate_switch(ctrl, target, 1);
}

size_t flow_adaptive_current_index(const FlowAdaptiveController *c) {
    if (!c) return SIZE_MAX;
    pthread_mutex_lock((pthread_mutex_t *)&c->lock);
    size_t idx = c->current_index;
    pthread_mutex_unlock((pthread_mutex_t *)&c->lock);
    return idx;
}

int flow_adaptive_metrics(const FlowAdaptiveController *c, FlowAdaptiveMetrics *m) {
    return (c && m) ? (pthread_mutex_lock((pthread_mutex_t *)&c->lock), *m = c->metrics, pthread_mutex_unlock((pthread_mutex_t *)&c->lock), FLOW_ADAPTIVE_OK) : FLOW_ADAPTIVE_INVALID;
}

int flow_adaptive_feed_pmu(FlowAdaptiveController *c, const FlowPMUTelemetry *p) {
    if (!c || !p) return FLOW_ADAPTIVE_INVALID;
    pthread_mutex_lock(&c->lock);
    c->pmu = *p;
    if (p->l3_cache_references > 0) c->pmu.cache_miss_rate = (double)p->l3_cache_misses / (double)p->l3_cache_references;
    if (p->cpu_cycles > 0) c->pmu.ipc = (double)p->instructions / (double)p->cpu_cycles;
    pthread_mutex_unlock(&c->lock);
    return FLOW_ADAPTIVE_OK;
}

int flow_adaptive_pmu_metrics(const FlowAdaptiveController *c, FlowPMUTelemetry *p) {
    return (c && p) ? (pthread_mutex_lock((pthread_mutex_t *)&c->lock), *p = c->pmu, pthread_mutex_unlock((pthread_mutex_t *)&c->lock), FLOW_ADAPTIVE_OK) : FLOW_ADAPTIVE_INVALID;
}

const char *flow_adaptive_status_name(FlowAdaptiveStatus s) {
    static const char * const names[] = {"ok", "not_ready", "no_change", "invalid", "reload_failed"};
    return (s >= 0 && s <= 4) ? names[s] : "unknown";
}

uint64_t flow_adaptive_telemetry_bias_from_pmu(const FlowPMUTelemetry *pmu, int write_heavy, const FlowPlanDimensionSet *dims) {
    if (!dims || !dims->count) return 0;
    uint64_t bias_mask = 0;
    unsigned shift = 0;
    uint8_t flags = (write_heavy ? 1 : 0) |
                    ((pmu && pmu->cache_miss_rate > 0.30) ? 2 : 0) |
                    ((pmu && pmu->ipc > 0.0 && pmu->ipc < 0.8) ? 4 : 0);

    for (size_t i = 0; i < dims->count; ++i) {
        const FlowPlanDimension *d = &dims->dimensions[i];
        unsigned bits = flow_dimension_bits(d);
        if (!bits) continue;
        uint64_t m = (bits >= 64) ? (uint64_t)-1 : (((uint64_t)1 << bits) - 1);
        int bias = !flags ? (d->dim_class == FLOW_DIM_CLASS_TACTILE_PARAM) : ((lookup_bias_attr(d->name) & flags) != 0);
        if (bias) bias_mask |= (m << shift);
        shift += bits;
    }
    return bias_mask;
}

uint64_t flow_adaptive_get_telemetry_bias(const FlowAdaptiveController *c, const Component *comp, const FlowPlanDimensionSet *dims) {
    (void)comp;
    if (!c || !dims) return 0;
    pthread_mutex_lock((pthread_mutex_t *)&c->lock);
    FlowPMUTelemetry p = c->pmu;
    int wh = (c->metrics.calls > 0 && c->metrics.failures > 0);
    pthread_mutex_unlock((pthread_mutex_t *)&c->lock);
    return flow_adaptive_telemetry_bias_from_pmu(&p, wh, dims);
}

uint64_t flow_adaptive_synthesize_env_mask(const FlowAdaptiveController *c, const FlowEnvironmentState *env, const Component *comp, const FlowPlanDimensionSet *dims) {
    (void)c;
    return flow_component_environment_mask(NULL, comp, dims, env);
}

FlowAdaptiveStatus flow_adaptive_handle_pressure_event(FlowAdaptiveController *ctrl, const FlowEnvironmentState *env, size_t *morphed_out) {
    if (!ctrl || !env) return FLOW_ADAPTIVE_INVALID;
    pthread_mutex_lock(&ctrl->lock);
    size_t curr = ctrl->current_index, best_idx = (size_t)-1;
    double best_score = -1.0e9;
    const double *w = PRESSURE_WEIGHTS[(size_t)env->pressure_level < 6 ? env->pressure_level : 0];

    for (size_t i = 0; i < ctrl->candidate_count; ++i) {
        const FlowAdaptiveCandidate *c = &ctrl->candidates[i];
        double score = w[0] * c->latency_score + w[1] * c->memory_score + w[2] * c->memory_fixed_bytes;
        if (score > best_score) { best_score = score; best_idx = i; }
    }
    if (best_idx == (size_t)-1 || best_idx == curr) {
        pthread_mutex_unlock(&ctrl->lock);
        if (morphed_out) *morphed_out = curr;
        return FLOW_ADAPTIVE_NO_CHANGE;
    }
    int res = flow_reload_activate(ctrl->context, ctrl->candidates[best_idx].unit);
    if (res != FLOW_RELOAD_OK) { pthread_mutex_unlock(&ctrl->lock); return FLOW_ADAPTIVE_RELOAD_FAILED; }
    ctrl->current_index = best_idx; ctrl->calls_since_switch = 0; ctrl->debounce.swap_count++;
    pthread_mutex_unlock(&ctrl->lock);
    if (morphed_out) *morphed_out = best_idx;
    return FLOW_ADAPTIVE_OK;
}

int flow_adaptive_set_golden_baseline(FlowAdaptiveController *c, const FlowUnit *u, void *s) {
    if (!c || !u) return 0;
    pthread_mutex_lock(&c->lock);
    c->golden_unit = u; c->golden_state = s;
    atomic_store_explicit(&c->is_running_golden, 0, memory_order_release);
    atomic_store_explicit(&c->consecutive_errors, 0, memory_order_release);
    pthread_mutex_unlock(&c->lock);
    return 1;
}

int flow_adaptive_fallback_to_golden_baseline(FlowAdaptiveController *c, const char *reason) {
    if (!c || !c->golden_unit) return 0;
    pthread_mutex_lock(&c->lock);
    int ok = (c->golden_state ? flow_reload_publish(c->context, c->golden_unit, c->golden_state)
                              : flow_reload_activate(c->context, c->golden_unit)) == FLOW_RELOAD_OK;
    if (ok) {
        atomic_store_explicit(&c->is_running_golden, 1, memory_order_release);
        atomic_store_explicit(&c->consecutive_errors, 0, memory_order_release);
        FlowMutationSnapshot snap = {.timestamp_ns = clock_ns(), .is_golden_fallback = 1};
        strncpy(snap.component_id, c->golden_unit->schema ? c->golden_unit->schema->name : "golden_baseline", sizeof(snap.component_id) - 1);
        strncpy(snap.author_attestation, reason ? reason : "OOD_SPIKE_FALLBACK", sizeof(snap.author_attestation) - 1);
        flow_audit_trail_record(c->context, &snap);
    }
    pthread_mutex_unlock(&c->lock);
    return ok;
}

int flow_adaptive_record_error_and_check_fallback(FlowAdaptiveController *c, size_t max_errs) {
    if (!c) return 0;
    size_t lim = max_errs ? max_errs : 3;
    return ((size_t)atomic_fetch_add_explicit(&c->consecutive_errors, 1, memory_order_acq_rel) + 1 >= lim && c->golden_unit) ?
        flow_adaptive_fallback_to_golden_baseline(c, "EXCEEDED_CONSECUTIVE_ERROR_THRESHOLD") : 0;
}

int flow_adaptive_is_running_golden(const FlowAdaptiveController *c) {
    return c ? atomic_load_explicit(&c->is_running_golden, memory_order_acquire) : 0;
}

void flow_schmitt_trigger_init(FlowSchmittTrigger *st, double base_min, uint64_t dwell_ns) {
    if (st) *st = (FlowSchmittTrigger){.drop_threshold = base_min * 0.8, .recovery_threshold = base_min * 1.5,
                                      .dwell_time_required_ns = dwell_ns ? dwell_ns : 500000000ULL};
}

int flow_schmitt_trigger_update(FlowSchmittTrigger *st, double val, uint64_t now_ns, int *changed) {
    if (!st) return 0;
    if (changed) *changed = 0;
    if (!st->current_state) {
        if (val < st->drop_threshold) { st->current_state = 1; st->stable_since_ns = 0; if (changed) *changed = 1; }
    } else if (val >= st->recovery_threshold) {
        if (!st->stable_since_ns) st->stable_since_ns = now_ns;
        else if (now_ns - st->stable_since_ns >= st->dwell_time_required_ns) {
            st->current_state = 0; st->stable_since_ns = 0; if (changed) *changed = 1;
        }
    } else st->stable_since_ns = 0;
    return st->current_state;
}
