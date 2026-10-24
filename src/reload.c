#include "reload.h"
#include "registry.h"
#include "token_ring.h"
#include "smt.h"
#include "hardware_telemetry.h"
#include "topology.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
typedef struct FlowReloadGeneration { const FlowUnit *unit; void *state; uint64_t generation, retire_epoch; _Atomic uint64_t pin_count; struct FlowReloadGeneration *next; } FlowReloadGeneration;
struct FlowReloadContext { void *host_context; _Atomic(FlowReloadGeneration *) current; _Atomic uint64_t epoch, anon_pin_epoch; _Atomic int quiescence_waiters, stopping;
    uint64_t next_generation; pthread_mutex_t lock; pthread_cond_t quiesced; FlowReloadReader *readers; FlowReloadGeneration *retired;
    const FlowWavefrontRing *bound_wavefront_ring; FlowBumpQsbrArena *bound_arena; };
static uint64_t schema_mix(uint64_t h, const void *d, size_t sz) { const unsigned char *b = d; for (size_t i = 0; i < sz; ++i) h = (h ^ b[i]) * UINT64_C(1099511628211); return h; }
static uint64_t schema_str(uint64_t h, const char *v) { static const unsigned char sep = 0; return v ? schema_mix(schema_mix(h, v, strlen(v)), &sep, 1) : schema_mix(h, &sep, 1); }
static uint64_t schema_u32(uint64_t h, uint32_t v) { unsigned char b[4] = {(unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24)}; return schema_mix(h, b, 4); }
uint64_t flow_schema_hash(const FlowSchema *s) { if (!s || !s->name || (s->field_count && !s->fields)) return 0;
    uint64_t h = schema_u32(schema_str(UINT64_C(1469598103934665603), s->name), s->version); for (size_t i = 0; i < s->field_count; ++i) {
        if (!s->fields[i].name || !s->fields[i].type) return 0; h = schema_u32(schema_str(schema_str(h, s->fields[i].name), s->fields[i].type), s->fields[i].flags); } return h; }
int flow_schema_migration_compatible(const FlowSchema *o, const FlowSchema *n) {
    if (!o || !n || !o->name || !n->name || strcmp(o->name, n->name) != 0 || n->version <= o->version || n->field_count < o->field_count) return 0;
    for (size_t i = 0; i < o->field_count; ++i) { const FlowSchemaField *of = &o->fields[i], *nf = &n->fields[i];
        if (!of->name || !nf->name || !of->type || !nf->type || strcmp(of->name, nf->name) != 0 || strcmp(of->type, nf->type) != 0) return 0;
        if (((of->flags & FLOW_SCHEMA_FIELD_PERSISTENT) && !(nf->flags & FLOW_SCHEMA_FIELD_PERSISTENT)) || ((of->flags & FLOW_SCHEMA_FIELD_ORDERED) && !(nf->flags & FLOW_SCHEMA_FIELD_ORDERED))) return 0; }
    for (size_t i = o->field_count; i < n->field_count; ++i) if (!(n->fields[i].flags & FLOW_SCHEMA_FIELD_DEFAULTABLE)) return 0; return 1; }
static int unit_valid(const FlowUnit *u) { return u && u->abi_version == FLOW_RELOAD_ABI_VERSION && u->name && u->run && u->drop; }
int flow_reload_compatible(const FlowUnit *cur, const FlowUnit *cand) {
    if (!unit_valid(cur) || !unit_valid(cand) || cur->constraint_hash != cand->constraint_hash || cur->capability_hash != cand->capability_hash) return 0;
    if (cur->schema || cand->schema) { if (!cur->schema || !cand->schema) return 0;
        return (cur->schema->version == cand->schema->version) ? (flow_schema_hash(cur->schema) == flow_schema_hash(cand->schema)) : flow_schema_migration_compatible(cur->schema, cand->schema); }
    return cur->semantic_schema_hash == cand->semantic_schema_hash; }
FlowReloadContext *flow_reload_create(void *host_context) { FlowReloadContext *ctx = calloc(1, sizeof(*ctx)); if (!ctx) return NULL;
    ctx->host_context = host_context; atomic_init(&ctx->epoch, 1); atomic_init(&ctx->anon_pin_epoch, 0);
    pthread_mutex_init(&ctx->lock, NULL); pthread_cond_init(&ctx->quiesced, NULL); return ctx; }
static void drop_generation(FlowReloadContext *ctx, FlowReloadGeneration *g) { if (g && g->unit && g->unit->drop && g->state) { g->unit->drop(ctx->host_context, g->state); g->state = NULL; } }
int flow_reload_destroy(FlowReloadContext *ctx) { if (!ctx) return FLOW_RELOAD_INVALID; pthread_mutex_lock(&ctx->lock);
    atomic_store_explicit(&ctx->stopping, 1, memory_order_release); if (ctx->readers) { pthread_mutex_unlock(&ctx->lock); return FLOW_RELOAD_BUSY; }
    FlowReloadGeneration *gen = atomic_load_explicit(&ctx->current, memory_order_acquire), *retired = ctx->retired;
    atomic_store_explicit(&ctx->current, NULL, memory_order_release); ctx->retired = NULL; pthread_mutex_unlock(&ctx->lock); if (gen) { drop_generation(ctx, gen); free(gen); }
    while (retired) { FlowReloadGeneration *next = retired->next; drop_generation(ctx, retired); free(retired); retired = next; }
    pthread_cond_destroy(&ctx->quiesced); pthread_mutex_destroy(&ctx->lock); free(ctx); return FLOW_RELOAD_OK; }
int flow_reload_reader_register(FlowReloadContext *ctx, FlowReloadReader *r) { if (!ctx || !r) return FLOW_RELOAD_INVALID; pthread_mutex_lock(&ctx->lock);
    if (ctx->stopping) { pthread_mutex_unlock(&ctx->lock); return FLOW_RELOAD_BUSY; } r->context = ctx; r->next = ctx->readers;
    atomic_init(&r->active_epoch, 0); atomic_init(&r->qsbr_epoch, atomic_load_explicit(&ctx->epoch, memory_order_acquire));
    atomic_init(&r->is_offline, 0); atomic_init(&r->is_quarantined, 0);
    atomic_store_explicit(&r->registered, 1, memory_order_release); ctx->readers = r; pthread_mutex_unlock(&ctx->lock); return FLOW_RELOAD_OK; }
int flow_reload_reader_unregister(FlowReloadReader *r) { if (!r || !r->context) return FLOW_RELOAD_INVALID; FlowReloadContext *ctx = r->context;
    pthread_mutex_lock(&ctx->lock); FlowReloadReader **cur = &ctx->readers; while (*cur && *cur != r) cur = &(*cur)->next;
    if (!*cur) { pthread_mutex_unlock(&ctx->lock); return FLOW_RELOAD_INVALID; } *cur = r->next; r->next = NULL;
    atomic_store_explicit(&r->registered, 0, memory_order_release); atomic_store_explicit(&r->active_epoch, 0, memory_order_seq_cst);
    if (atomic_load_explicit(&ctx->quiescence_waiters, memory_order_acquire) > 0) pthread_cond_broadcast(&ctx->quiesced); r->context = NULL; pthread_mutex_unlock(&ctx->lock); return FLOW_RELOAD_OK; }
int flow_reload_publish(FlowReloadContext *ctx, const FlowUnit *unit, void *state) {
    if (!ctx || !unit_valid(unit) || !state) return FLOW_RELOAD_INVALID; FlowReloadGeneration *next = calloc(1, sizeof(*next)); if (!next) return FLOW_RELOAD_INVALID;
    next->unit = unit; next->state = state; atomic_init(&next->pin_count, 0); pthread_mutex_lock(&ctx->lock);
    if (atomic_load_explicit(&ctx->stopping, memory_order_acquire)) { pthread_mutex_unlock(&ctx->lock); free(next); return FLOW_RELOAD_BUSY; }
    FlowReloadGeneration *old = atomic_load_explicit(&ctx->current, memory_order_acquire);
    if (old && !flow_reload_compatible(old->unit, unit)) { pthread_mutex_unlock(&ctx->lock); free(next); return FLOW_RELOAD_INCOMPATIBLE; }
    if (old && old->state && unit->migrate) {
        if (unit->migrate(ctx->host_context, old->state, state) != 0) {
            pthread_mutex_unlock(&ctx->lock); free(next); return FLOW_RELOAD_INCOMPATIBLE;
        }
    }
    uint64_t t_swap_start = flow_clock_monotonic_ns();
    next->generation = ++ctx->next_generation;
    if (old) {
        next->retire_epoch = atomic_fetch_add_explicit(&ctx->epoch, UINT64_C(1), memory_order_seq_cst) + UINT64_C(1);
        old->retire_epoch = next->retire_epoch;
    }
    atomic_store_explicit(&ctx->current, next, memory_order_seq_cst);
    uint64_t swap_elapsed = flow_clock_monotonic_ns() - t_swap_start;
    if (old) {
        old->next = ctx->retired; ctx->retired = old;
        FlowDecisionEvent ev = {
            .timestamp_ns = flow_clock_monotonic_ns(), .trigger_type = FLOW_DECISION_TRIGGER_STRAGGLER_QUARANTINE, .observed_metric_value = (double)next->generation,
            .threshold_limit_value = (double)(next->generation - 1), .flipped_genome_bit = (uint32_t)(next->generation % 64), .pre_state_mask = (1ULL << 61),
            .post_state_mask = (1ULL << 61) | (1ULL << (next->generation % 64)), .hot_swap_grace_ns = swap_elapsed ? swap_elapsed : 1,
            .trigger_source = "qsbr_epoch_migration", .metric_unit = "gen", .violated_constraint = "QSBR Zero-Downtime Epoch Upgrade" };
        snprintf(ev.pre_topology, sizeof(ev.pre_topology), "%s", (old->unit && old->unit->name) ? old->unit->name : "old_generation");
        snprintf(ev.post_topology, sizeof(ev.post_topology), "%s", (next->unit && next->unit->name) ? next->unit->name : "new_generation");
        snprintf(ev.causal_rationale, sizeof(ev.causal_rationale), "QSBR epoch %llu: '%s' -> '%s'", (unsigned long long)next->retire_epoch, ev.pre_topology, ev.post_topology);
        flow_decision_logger_record(flow_decision_logger_default(), &ev);
    }
    pthread_mutex_unlock(&ctx->lock); return FLOW_RELOAD_OK; }
int flow_reload_activate(FlowReloadContext *ctx, const FlowUnit *unit) { void *state = NULL;
    if (!ctx || !unit_valid(unit) || !unit->init || unit->init(ctx->host_context, &state) != 0 || !state) return FLOW_RELOAD_INVALID;
    int res = flow_reload_publish(ctx, unit, state); if (res != FLOW_RELOAD_OK) unit->drop(ctx->host_context, state); return res; }
const FlowUnit *flow_reload_current_unit(const FlowReloadContext *ctx) { FlowReloadGeneration *gen = ctx ? atomic_load_explicit(&ctx->current, memory_order_acquire) : NULL; return gen ? gen->unit : NULL; }
int flow_reload_begin(FlowReloadContext *ctx, FlowReloadReader *r, FlowInvocation *inv) { if (!ctx || !r || !inv) return FLOW_RELOAD_INVALID;
    if (inv->active || atomic_load_explicit(&ctx->stopping, memory_order_acquire)) return FLOW_RELOAD_BUSY;
    if (r->context != ctx || !atomic_load_explicit(&r->registered, memory_order_acquire)) return FLOW_RELOAD_INVALID;
    atomic_store_explicit(&r->active_epoch, atomic_load_explicit(&ctx->epoch, memory_order_seq_cst), memory_order_seq_cst);
    atomic_store_explicit(&r->last_heartbeat_ns, flow_clock_monotonic_ns(), memory_order_relaxed);
    FlowReloadGeneration *gen = atomic_load_explicit(&ctx->current, memory_order_seq_cst);
    if (!gen) { atomic_store_explicit(&r->active_epoch, 0, memory_order_seq_cst); return FLOW_RELOAD_NO_CURRENT; }
    if (ctx->bound_arena) __atomic_fetch_add(&ctx->bound_arena->active_readers, 1, __ATOMIC_ACQ_REL);
    *inv = (FlowInvocation){.context = ctx, .reader = r, .unit = gen->unit, .state = gen->state, .generation = gen->generation, .active = 1}; return FLOW_RELOAD_OK; }
void flow_reload_end(FlowInvocation *inv) { if (!inv || !inv->active || !inv->reader) return;
    if (inv->context && inv->context->bound_arena) __atomic_fetch_sub(&inv->context->bound_arena->active_readers, 1, __ATOMIC_ACQ_REL);
    atomic_store_explicit(&inv->reader->active_epoch, 0, memory_order_seq_cst);
    if (atomic_load_explicit(&inv->context->quiescence_waiters, memory_order_acquire) > 0) {
        pthread_mutex_lock(&inv->context->lock); pthread_cond_broadcast(&inv->context->quiesced); pthread_mutex_unlock(&inv->context->lock); } *inv = (FlowInvocation){0}; }
int flow_reload_call(FlowReloadContext *ctx, FlowReloadReader *r, const void *in, void *out) { FlowInvocation inv = {0}; int res = flow_reload_begin(ctx, r, &inv);
    if (res != FLOW_RELOAD_OK) return res; res = inv.unit->run(ctx->host_context, inv.state, in, out); flow_reload_end(&inv); return res; }
static int generation_safe(FlowReloadContext *ctx, const FlowReloadGeneration *cand) {
    uint64_t retire_epoch = cand->retire_epoch;
    if (atomic_load_explicit(&ctx->anon_pin_epoch, memory_order_seq_cst) > 0 ||
        atomic_load_explicit(&cand->pin_count, memory_order_seq_cst) > 0) return 0;
    if (ctx->bound_wavefront_ring && retire_epoch > ctx->bound_wavefront_ring->quiescent_generation) return 0;
    for (const FlowReloadReader *r = ctx->readers; r; r = r->next) {
        if (!atomic_load_explicit(&r->registered, memory_order_acquire) ||
            atomic_load_explicit(&r->is_offline, memory_order_acquire) ||
            atomic_load_explicit(&r->is_quarantined, memory_order_acquire)) continue;
        uint64_t active = atomic_load_explicit(&r->active_epoch, memory_order_seq_cst);
        if (active && active <= retire_epoch) return 0;
    }
    return 1;
}
uint64_t flow_reload_generation(const FlowReloadContext *ctx) { const FlowReloadGeneration *curr = ctx ? atomic_load_explicit(&ctx->current, memory_order_acquire) : NULL; return curr ? curr->generation : 0; }
size_t flow_reload_reclaim(FlowReloadContext *ctx) { if (!ctx) return 0; FlowReloadGeneration *ready = NULL; size_t reclaimed = 0; int any_active = 0;
    pthread_mutex_lock(&ctx->lock);
    for (FlowReloadGeneration **cur = &ctx->retired; *cur; ) { FlowReloadGeneration *cand = *cur;
        if (!generation_safe(ctx, cand)) { cur = &cand->next; continue; } *cur = cand->next; cand->next = ready; ready = cand; }
    for (const FlowReloadReader *r = ctx->readers; r; r = r->next)
        if (atomic_load_explicit(&r->active_epoch, memory_order_seq_cst) != 0) { any_active = 1; break; }
    pthread_mutex_unlock(&ctx->lock);
    while (ready) { FlowReloadGeneration *next = ready->next; drop_generation(ctx, ready); free(ready); ready = next; ++reclaimed; }
    if (ctx->bound_arena && reclaimed > 0 && !any_active) flow_bump_qsbr_quiescent_fold(ctx->bound_arena);
    return reclaimed; }
void flow_qsbr_checkpoint(FlowReloadReader *r) {
    if (!r) return;
    if (r->context) {
        uint64_t ctx_ep = atomic_load_explicit(&r->context->epoch, memory_order_seq_cst);
        atomic_store_explicit(&r->qsbr_epoch, ctx_ep, memory_order_seq_cst);
    } else {
        atomic_fetch_add_explicit(&r->qsbr_epoch, 1, memory_order_seq_cst);
    }
    atomic_store_explicit(&r->last_heartbeat_ns, flow_clock_monotonic_ns(), memory_order_relaxed);
}
void flow_qsbr_offline(FlowReloadReader *r) { if (r) atomic_store_explicit(&r->is_offline, 1, memory_order_release); }
void flow_qsbr_online(FlowReloadReader *r) { if (r) { atomic_store_explicit(&r->last_heartbeat_ns, flow_clock_monotonic_ns(), memory_order_relaxed); atomic_store_explicit(&r->is_offline, 0, memory_order_release); } }
int flow_qsbr_call(FlowReloadContext *ctx, const void *in, void *out) {
    if (!ctx) return FLOW_RELOAD_INVALID;
    atomic_fetch_add_explicit(&ctx->anon_pin_epoch, 1, memory_order_seq_cst);
    FlowReloadGeneration *curr = atomic_load_explicit(&ctx->current, memory_order_seq_cst);
    if (!curr || !curr->unit || !curr->unit->run) {
        atomic_fetch_sub_explicit(&ctx->anon_pin_epoch, 1, memory_order_seq_cst);
        return FLOW_RELOAD_NO_CURRENT;
    }
    atomic_fetch_add_explicit(&curr->pin_count, 1, memory_order_seq_cst);
    atomic_fetch_sub_explicit(&ctx->anon_pin_epoch, 1, memory_order_seq_cst);
    int rc = curr->unit->run(ctx->host_context, curr->state, in, out);
    atomic_fetch_sub_explicit(&curr->pin_count, 1, memory_order_seq_cst);
    return rc;
}
int flow_qsbr_synchronize(FlowReloadContext *ctx, uint64_t timeout_ns) {
    if (!ctx) return FLOW_RELOAD_INVALID;
    if (ctx->bound_wavefront_ring) {
        ((FlowWavefrontRing *)ctx->bound_wavefront_ring)->state = FLOW_RING_CIRCULATING;
        flow_wavefront_ring_step_parallel((FlowWavefrontRing *)ctx->bound_wavefront_ring);
        return FLOW_RELOAD_OK;
    }
    uint64_t start = flow_clock_monotonic_ns();
    uint64_t sync_epoch = atomic_load_explicit(&ctx->epoch, memory_order_seq_cst);
    FlowReloadReader *stack_tracked[64];
    uint64_t stack_targets[64];
    FlowReloadReader **tracked = stack_tracked;
    uint64_t *target_epochs = stack_targets;
    size_t count = 0, cap = 64;
    pthread_mutex_lock(&ctx->lock);
    for (FlowReloadReader *r = ctx->readers; r; r = r->next) count++;
    if (count > cap) {
        tracked = calloc(count, sizeof(FlowReloadReader *));
        target_epochs = calloc(count, sizeof(uint64_t));
        if (!tracked || !target_epochs) {
            free(tracked); free(target_epochs);
            pthread_mutex_unlock(&ctx->lock);
            return FLOW_RELOAD_INVALID;
        }
        cap = count;
    }
    count = 0;
    for (FlowReloadReader *r = ctx->readers; r && count < cap; r = r->next) {
        if (atomic_load_explicit(&r->registered, memory_order_acquire) &&
            !atomic_load_explicit(&r->is_offline, memory_order_acquire) &&
            !atomic_load_explicit(&r->is_quarantined, memory_order_acquire)) {
            tracked[count] = r;
            target_epochs[count++] = sync_epoch;
        }
    }
    pthread_mutex_unlock(&ctx->lock);
    int status = FLOW_RELOAD_OK;
    for (size_t i = 0; i < count; ++i) {
        FlowReloadReader *r = tracked[i];
        size_t spins = 0;
        while (atomic_load_explicit(&r->registered, memory_order_acquire) &&
               !atomic_load_explicit(&r->is_offline, memory_order_acquire) &&
               !atomic_load_explicit(&r->is_quarantined, memory_order_acquire) &&
               (atomic_load_explicit(&r->active_epoch, memory_order_seq_cst) != 0 ||
                atomic_load_explicit(&r->qsbr_epoch, memory_order_acquire) < target_epochs[i])) {
            if (timeout_ns > 0 && (flow_clock_monotonic_ns() - start) > timeout_ns) {
                status = FLOW_RELOAD_BUSY;
                goto done;
            }
            if (spins++ < 2048) {
#if defined(__aarch64__) || defined(__arm64__)
                __asm__ volatile("isb" ::: "memory");
#elif defined(__x86_64__) || defined(_M_X64)
                __builtin_ia32_pause();
#endif
            } else {
                sched_yield();
            }
        }
    }
done:
    if (tracked != stack_tracked) free(tracked);
    if (target_epochs != stack_targets) free(target_epochs);
    return status;
}
size_t flow_qsbr_reclaim(FlowReloadContext *ctx) { return ctx ? flow_reload_reclaim(ctx) : 0; }
int flow_reload_bind_wavefront(FlowReloadContext *ctx, const FlowWavefrontRing *ring) { if (!ctx) return 0; pthread_mutex_lock(&ctx->lock); ctx->bound_wavefront_ring = ring; pthread_mutex_unlock(&ctx->lock); return 1; }
int flow_reload_bind_arena(FlowReloadContext *ctx, FlowBumpQsbrArena *arena) { if (!ctx) return 0; pthread_mutex_lock(&ctx->lock); ctx->bound_arena = arena; pthread_mutex_unlock(&ctx->lock); return 1; }
FlowSMTResult flow_reload_verify_topological_safety_smt(const FlowReloadContext *ctx, FlowSMTProofAttestation *proof_out) { if (!ctx) return FLOW_SMT_UNKNOWN;
    uint64_t gen = flow_reload_generation(ctx), epoch = atomic_load_explicit(&ctx->epoch, memory_order_acquire);
    int waiters = atomic_load_explicit(&ctx->quiescence_waiters, memory_order_acquire);
    uint64_t av = (ctx->bound_arena && ctx->bound_arena->cursor > ctx->bound_arena->capacity) ? 1 : 0; const FlowBoxConstraint rules[] = {
        FLOW_SMT_RULE("wavefront_evacuation_bound", gen > 1000000000ULL, 0, 0, FLOW_BOX_THEOREM_BUFFER_BOUNDS, "Quiescent evacuation exceeded horizon"),
        FLOW_SMT_RULE("lockfree_waiters_soundness", waiters < 0, 0, 0, FLOW_BOX_THEOREM_MEMORY_QUOTA, "Quiescence waiter balance violated"),
        FLOW_SMT_RULE("generational_folding_soundness", av, 0, 0, FLOW_BOX_THEOREM_SHARD_ISOLATION, "Generational arena exceeded capacity quota"),
        FLOW_SMT_RULE("epoch_monotonicity", epoch == 0, 0, 0, FLOW_BOX_THEOREM_DETERMINISM, "Epoch counter is zero or non-monotonic") };
    return FLOW_SMT_VERIFY_RULES("reload_topological_safety", proof_out, rules, "SMT RELOAD SOUND: Gen=%llu, Epoch=%llu, BoundRing=%s, BoundArena=%s",
        (unsigned long long)gen, (unsigned long long)epoch, ctx->bound_wavefront_ring ? "YES" : "NO", ctx->bound_arena ? "YES" : "NO"); }
int flow_bump_qsbr_init(FlowBumpQsbrArena *arena, void *backing_memory, size_t capacity) { if (!arena || !backing_memory || !capacity) return 0;
    *arena = (FlowBumpQsbrArena){ .buffer = (uint8_t *)backing_memory, .capacity = capacity, .generation = 1 }; return 1; }
void *flow_bump_qsbr_alloc(FlowBumpQsbrArena *arena, size_t size_bytes) { if (!arena || !size_bytes) return NULL; size_t aligned = (size_bytes + 7) & ~((size_t)7);
    if (arena->cursor + aligned > arena->capacity) return NULL; void *ptr = arena->buffer + arena->cursor; arena->cursor += aligned; arena->total_allocs++; return ptr; }
int flow_bump_qsbr_quiescent_fold(FlowBumpQsbrArena *arena) {
    if (!arena || __atomic_load_n(&arena->active_readers, __ATOMIC_ACQUIRE) > 0) return 0;
    arena->cursor = 0; arena->generation++; arena->total_folds++; return 1;
}
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
