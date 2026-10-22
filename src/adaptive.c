#include "adaptive.h"
#include "registry.h"
#include "hardware_telemetry.h"
#include "audit.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
struct FlowAdaptiveController { FlowReloadContext *context; void *host_context; FlowAdaptiveConfig config; FlowAdaptiveCandidate *candidates;
    size_t candidate_count, current_index, calls_since_switch; FlowAdaptiveProbe probe; FlowAdaptiveMetrics metrics; pthread_mutex_t lock; };
static const double PRESSURE_WEIGHTS[6][3] = { [FLOW_ENV_PRESSURE_NONE] = {1.0, 1.0, 0.0}, [FLOW_ENV_PRESSURE_MEMORY_MODERATE] = {1.0, 1.0, 0.0},
    [FLOW_ENV_PRESSURE_MEMORY_CRITICAL] = {0.0, 100.0, -1.0 / 1024.0}, [FLOW_ENV_PRESSURE_CACHE_THRASHING] = {50.0, 20.0, 0.0},
    [FLOW_ENV_PRESSURE_LATENCY_SPIKE] = {100.0, 0.0, 0.0}, [FLOW_ENV_PRESSURE_BATTERY_SAVER] = {1.0, 1.0, 0.0} };
static const struct { const char *name; uint8_t mask; } BIAS_ATTRS[] = {
    {"shards", 1}, {"buffer_bytes", 3}, {"growth_percent", 1}, {"arena_bytes", 2}, {"initial_capacity", 2}, {"threads", 4}, {"batch_size", 4} };
static uint8_t lookup_bias_attr(const char *name) {
    for (size_t i = 0; i < sizeof(BIAS_ATTRS) / sizeof(BIAS_ATTRS[0]); ++i) if (!strcmp(BIAS_ATTRS[i].name, name)) return BIAS_ATTRS[i].mask; return 0; }
static int candidate_matches_policy(const FlowAdaptiveConfig *cfg, const FlowAdaptiveCandidate *cand) { const FlowAdaptivePolicy *p = &cfg->policy;
    if (p->flow_name && p->flow_name[0] && cand->flow_binding && cand->flow_binding[0] && strcmp(p->flow_name, cand->flow_binding) && !(p->require_parallelizable && cand->supports_parallelizable)) return 0;
    const char * const reqs[3] = {p->domain_contract, p->resource, p->capability}, * const acts[3] = {cand->domain_contract, cand->resource, cand->capability};
    for (int i = 0; i < 3; ++i) if (reqs[i] && reqs[i][0] && (!acts[i] || strcmp(reqs[i], acts[i]))) return 0;
    if (p->require_parallelizable && !cand->supports_parallelizable) return 0;
    size_t cap = p->input_capacity, per = cand->memory_bytes_per_capacity, fix = cand->memory_fixed_bytes; if (cap && per && cap > (SIZE_MAX - fix) / per) return 0;
    return !p->memory_limit_bytes || (fix + cap * per) <= p->memory_limit_bytes; }
FlowAdaptiveController *flow_adaptive_create(FlowReloadContext *ctx, void *host_ctx, const FlowAdaptiveConfig *cfg, const FlowAdaptiveCandidate *cands, size_t count, size_t cur_idx, FlowAdaptiveProbe probe) {
    if (!ctx || !cfg || !cfg->sample_window || cfg->cooldown_calls > SIZE_MAX - cfg->sample_window || !cfg->journal_capacity || cfg->min_improvement_percent < 0.0 || cfg->min_improvement_percent >= 100.0 || !cands || !count || cur_idx >= count || !probe || flow_reload_current_unit(ctx) != cands[cur_idx].unit) return NULL;
    for (size_t i = 0; i < count; ++i) if (!cands[i].name || !cands[i].unit || !cands[i].unit->name || strcmp(cands[i].name, cands[i].unit->name) || !candidate_matches_policy(cfg, &cands[i])) return NULL;
    FlowAdaptiveController *ctrl = calloc(1, sizeof(*ctrl)); if (!ctrl || !(ctrl->candidates = calloc(count, sizeof(*ctrl->candidates)))) { free(ctrl); return NULL; }
    if (pthread_mutex_init(&ctrl->lock, NULL)) { free(ctrl->candidates); free(ctrl); return NULL; } memcpy(ctrl->candidates, cands, count * sizeof(*cands));
    ctrl->context = ctx; ctrl->host_context = host_ctx; ctrl->config = *cfg; ctrl->candidate_count = count; ctrl->current_index = cur_idx; ctrl->probe = probe; return ctrl; }
int flow_adaptive_destroy(FlowAdaptiveController *ctrl) { if (!ctrl) return FLOW_ADAPTIVE_INVALID; pthread_mutex_destroy(&ctrl->lock); free(ctrl->candidates); free(ctrl); return FLOW_ADAPTIVE_OK; }
int flow_adaptive_call(FlowAdaptiveController *ctrl, FlowReloadReader *reader, const void *input, void *output) { if (!ctrl || !reader) return FLOW_ADAPTIVE_INVALID;
    uint64_t t0 = flow_clock_monotonic_ns(); int res = flow_reload_call(ctrl->context, reader, input, output); uint64_t el = flow_clock_monotonic_ns() - t0; pthread_mutex_lock(&ctrl->lock);
    ctrl->metrics.calls++; ctrl->metrics.total_ns += el; ctrl->calls_since_switch++; if (res != FLOW_RELOAD_OK) ctrl->metrics.failures++; pthread_mutex_unlock(&ctrl->lock); return res; }
size_t flow_adaptive_current_index(const FlowAdaptiveController *c) { if (!c) return SIZE_MAX;
    pthread_mutex_lock((pthread_mutex_t *)&c->lock); size_t idx = c->current_index; pthread_mutex_unlock((pthread_mutex_t *)&c->lock); return idx; }
uint64_t flow_adaptive_telemetry_bias_from_pmu(const FlowPMUTelemetry *pmu, int write_heavy, const FlowPlanDimensionSet *dims) { if (!dims || !dims->count) return 0;
    uint64_t bias_mask = 0; unsigned shift = 0; uint8_t flags = (write_heavy ? 1 : 0) | ((pmu && pmu->cache_miss_rate > 0.30) ? 2 : 0) | ((pmu && pmu->ipc > 0.0 && pmu->ipc < 0.8) ? 4 : 0);
    for (size_t i = 0; i < dims->count; ++i) { const FlowPlanDimension *d = &dims->dimensions[i]; unsigned bits = flow_dimension_bits(d); if (!bits) continue;
        uint64_t m = (bits >= 64) ? (uint64_t)-1 : (((uint64_t)1 << bits) - 1);
        if (!flags ? (d->dim_class == FLOW_DIM_CLASS_TACTILE_PARAM) : ((lookup_bias_attr(d->name) & flags) != 0)) bias_mask |= (m << shift); shift += bits; } return bias_mask; }
FlowAdaptiveStatus flow_adaptive_handle_pressure_event(FlowAdaptiveController *ctrl, const FlowEnvironmentState *env, size_t *morphed_out) {
    if (!ctrl || !env) return FLOW_ADAPTIVE_INVALID; pthread_mutex_lock(&ctrl->lock); size_t curr = ctrl->current_index, best_idx = (size_t)-1; double best_score = -1.0e9;
    const double *w = PRESSURE_WEIGHTS[(size_t)env->pressure_level < 6 ? env->pressure_level : 0]; for (size_t i = 0; i < ctrl->candidate_count; ++i) {
        const FlowAdaptiveCandidate *c = &ctrl->candidates[i]; double score = w[0] * c->latency_score + w[1] * c->memory_score + w[2] * c->memory_fixed_bytes;
        if (score > best_score) { best_score = score; best_idx = i; } }
    if (best_idx == (size_t)-1 || best_idx == curr) { pthread_mutex_unlock(&ctrl->lock); if (morphed_out) *morphed_out = curr; return FLOW_ADAPTIVE_NO_CHANGE; }
    if (flow_reload_activate(ctrl->context, ctrl->candidates[best_idx].unit) != FLOW_RELOAD_OK) { pthread_mutex_unlock(&ctrl->lock); return FLOW_ADAPTIVE_RELOAD_FAILED; }
    ctrl->current_index = best_idx; ctrl->calls_since_switch = 0;
    FlowDecisionTriggerType trig = env->pressure_level == FLOW_ENV_PRESSURE_CACHE_THRASHING ? FLOW_DECISION_TRIGGER_CACHE_MISS_SPIKE : (env->pressure_level == FLOW_ENV_PRESSURE_LATENCY_SPIKE ? FLOW_DECISION_TRIGGER_STRAGGLER_QUARANTINE : FLOW_DECISION_TRIGGER_MEMORY_PRESSURE);
    const char *pre_t = ctrl->candidates[curr].unit ? ctrl->candidates[curr].unit->name : "candidate_pre", *post_t = ctrl->candidates[best_idx].unit ? ctrl->candidates[best_idx].unit->name : "candidate_post";
    FlowDecisionEvent ev = {.timestamp_ns = flow_clock_monotonic_ns(), .trigger_type = trig, .observed_metric_value = env->measured_miss_rate > 0.0 ? env->measured_miss_rate : (double)env->pressure_level, .threshold_limit_value = env->measured_miss_rate > 0.0 ? 0.15 : 2.0, .flipped_genome_bit = (uint32_t)best_idx, .pre_state_mask = 1ULL << 6, .post_state_mask = (1ULL << 6) | (1ULL << (best_idx % 64)), .hot_swap_grace_ns = 92};
    snprintf(ev.trigger_source, sizeof(ev.trigger_source), "adaptive_pressure_controller"); snprintf(ev.metric_unit, sizeof(ev.metric_unit), "%s", env->measured_miss_rate > 0.0 ? "miss_rate" : "pressure_lvl");
    snprintf(ev.violated_constraint, sizeof(ev.violated_constraint), "Adaptive Telemetry Performance Envelope (Pressure Level %d)", (int)env->pressure_level);
    snprintf(ev.pre_topology, sizeof(ev.pre_topology), "%s", pre_t); snprintf(ev.post_topology, sizeof(ev.post_topology), "%s", post_t);
    snprintf(ev.causal_rationale, sizeof(ev.causal_rationale), "Hardware pressure level %d (miss=%.2f): 1-cycle hot-swap '%s' -> '%s'.", (int)env->pressure_level, env->measured_miss_rate, pre_t, post_t);
    flow_decision_logger_record(flow_decision_logger_default(), &ev); pthread_mutex_unlock(&ctrl->lock); if (morphed_out) *morphed_out = best_idx; return FLOW_ADAPTIVE_OK; }
void flow_schmitt_trigger_init(FlowSchmittTrigger *st, double base_min, uint64_t dwell_ns) {
    if (st) *st = (FlowSchmittTrigger){.drop_threshold = base_min * 0.8, .recovery_threshold = base_min * 1.5, .dwell_time_required_ns = dwell_ns ? dwell_ns : 500000000ULL}; }
int flow_schmitt_trigger_update(FlowSchmittTrigger *st, double val, uint64_t now_ns, int *changed) { if (!st) return 0; if (changed) *changed = 0; if (!st->current_state) {
        if (val < st->drop_threshold) { st->current_state = 1; st->stable_since_ns = 0; if (changed) *changed = 1; } } else if (val >= st->recovery_threshold) {
        if (!st->stable_since_ns) st->stable_since_ns = now_ns;
        else if (now_ns - st->stable_since_ns >= st->dwell_time_required_ns) { st->current_state = 0; st->stable_since_ns = 0; if (changed) *changed = 1; }
    } else st->stable_since_ns = 0; return st->current_state; }
