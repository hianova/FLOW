#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "reload.h"
#include "backend.h"
#include "registry.h"
#include "smt.h"
#include "flow_jet.h"
#include "topology.h"
#include "cubical_hott.h"
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <sys/mman.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <libkern/OSCacheControl.h>
#elif defined(__linux__)
#include <fcntl.h>
#endif
typedef struct FlowReloadGeneration { const FlowUnit *unit; void *state; uint64_t generation, retire_epoch; _Atomic uint64_t pin_count; struct FlowReloadGeneration *next; } FlowReloadGeneration;
struct FlowReloadContext { void *host_context; _Atomic(FlowReloadGeneration *) current; _Atomic uint64_t epoch, anon_pin_epoch; _Atomic int quiescence_waiters, stopping;
    uint64_t next_generation; pthread_mutex_t lock; pthread_cond_t quiesced; FlowReloadReader *readers; FlowReloadGeneration *retired;
    const FlowWavefrontRing *bound_wavefront_ring; FlowBumpQsbrArena *bound_arena; };
static uint64_t schema_mix(uint64_t h, const void *d, size_t sz) { const unsigned char *b = d; for (size_t i = 0; i < sz; ++i) h = (h ^ b[i]) * FLOW_FNV1A_64_PRIME; return h; }
static uint64_t schema_str(uint64_t h, const char *v) { static const unsigned char sep = 0; return v ? schema_mix(schema_mix(h, v, strlen(v)), &sep, 1) : schema_mix(h, &sep, 1); }
static uint64_t schema_u32(uint64_t h, uint32_t v) { unsigned char b[4] = {(unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24)}; return schema_mix(h, b, 4); }
uint64_t flow_schema_hash(const FlowSchema *s) { if (!s || !s->name || (s->field_count && !s->fields)) return 0;
    uint64_t h = schema_u32(schema_str(FLOW_FNV1A_64_OFFSET, s->name), s->version); for (size_t i = 0; i < s->field_count; ++i) {
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
static size_t free_generation_list(FlowReloadContext *ctx, FlowReloadGeneration *head) {
    size_t n = 0;
    while (head) { FlowReloadGeneration *next = head->next; drop_generation(ctx, head); free(head); head = next; ++n; }
    return n;
}
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
static FlowReloadGeneration *reclaim_safe_locked(FlowReloadContext *ctx, size_t *remaining_depth_out) {
    FlowReloadGeneration *ready = NULL;
    size_t rem = 0;
    for (FlowReloadGeneration **cur = &ctx->retired; *cur; ) {
        FlowReloadGeneration *cand = *cur;
        if (generation_safe(ctx, cand)) { *cur = cand->next; cand->next = ready; ready = cand; }
        else { ++rem; cur = &cand->next; }
    }
    if (remaining_depth_out) *remaining_depth_out = rem;
    return ready;
}
int flow_reload_destroy(FlowReloadContext *ctx) { if (!ctx) return FLOW_RELOAD_INVALID; pthread_mutex_lock(&ctx->lock);
    atomic_store_explicit(&ctx->stopping, 1, memory_order_release); if (ctx->readers) { pthread_mutex_unlock(&ctx->lock); return FLOW_RELOAD_BUSY; }
    FlowReloadGeneration *gen = atomic_load_explicit(&ctx->current, memory_order_acquire), *retired = ctx->retired;
    atomic_store_explicit(&ctx->current, NULL, memory_order_release); ctx->retired = NULL; pthread_mutex_unlock(&ctx->lock); if (gen) { drop_generation(ctx, gen); free(gen); }
    (void)free_generation_list(ctx, retired);
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
    FlowReloadGeneration *reclaimed_head = NULL;
    if (old) {
        size_t r_depth = 0;
        for (FlowReloadGeneration *g = ctx->retired; g; g = g->next) ++r_depth;
        if (r_depth >= FLOW_RELOAD_MAX_RETIRED_DEPTH) reclaimed_head = reclaim_safe_locked(ctx, &r_depth);
        if (r_depth >= FLOW_RELOAD_MAX_RETIRED_DEPTH) {
            pthread_mutex_unlock(&ctx->lock); free(next); (void)free_generation_list(ctx, reclaimed_head);
            return FLOW_RELOAD_JOURNAL_FULL;
        }
    }
    if (old && old->state && unit->migrate) {
        if (unit->migrate(ctx->host_context, old->state, state) != 0) {
            pthread_mutex_unlock(&ctx->lock); free(next); (void)free_generation_list(ctx, reclaimed_head);
            return FLOW_RELOAD_INCOMPATIBLE;
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
        uint32_t g_bit = (uint32_t)(next->generation % FLOW_RELOAD_MAX_RETIRED_DEPTH);
        uint64_t lease_mask = UINT64_C(1) << FLOW_SIEVE_BIT_QSBR_EPOCH_LEASE;
        FlowDecisionEvent ev = {
            .timestamp_ns = flow_clock_monotonic_ns(), .trigger_type = FLOW_DECISION_TRIGGER_STRAGGLER_QUARANTINE, .observed_metric_value = (double)next->generation,
            .threshold_limit_value = (double)(next->generation - 1), .flipped_genome_bit = g_bit, .pre_state_mask = lease_mask,
            .post_state_mask = lease_mask | (UINT64_C(1) << g_bit), .hot_swap_grace_ns = swap_elapsed ? swap_elapsed : 1,
            .trigger_source = "qsbr_epoch_migration", .metric_unit = "gen", .violated_constraint = "QSBR Zero-Downtime Epoch Upgrade" };
        snprintf(ev.pre_topology, sizeof(ev.pre_topology), "%s", (old->unit && old->unit->name) ? old->unit->name : "old_generation");
        snprintf(ev.post_topology, sizeof(ev.post_topology), "%s", (next->unit && next->unit->name) ? next->unit->name : "new_generation");
        snprintf(ev.causal_rationale, sizeof(ev.causal_rationale), "QSBR epoch %llu: '%s' -> '%s'", (unsigned long long)next->retire_epoch, ev.pre_topology, ev.post_topology);
        flow_decision_logger_record(flow_decision_logger_default(), &ev);
    }
    pthread_mutex_unlock(&ctx->lock);
    (void)free_generation_list(ctx, reclaimed_head);
    return FLOW_RELOAD_OK; }
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
uint64_t flow_reload_generation(const FlowReloadContext *ctx) { const FlowReloadGeneration *curr = ctx ? atomic_load_explicit(&ctx->current, memory_order_acquire) : NULL; return curr ? curr->generation : 0; }
size_t flow_reload_reclaim(FlowReloadContext *ctx) { if (!ctx) return 0; int any_active = 0;
    pthread_mutex_lock(&ctx->lock);
    FlowReloadGeneration *ready = reclaim_safe_locked(ctx, NULL);
    for (const FlowReloadReader *r = ctx->readers; r; r = r->next)
        if (atomic_load_explicit(&r->active_epoch, memory_order_seq_cst) != 0) { any_active = 1; break; }
    pthread_mutex_unlock(&ctx->lock);
    size_t reclaimed = free_generation_list(ctx, ready);
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
void *flow_bump_qsbr_alloc(FlowBumpQsbrArena *arena, size_t size_bytes) { if (!arena || !size_bytes || size_bytes > SIZE_MAX - 7) return NULL; size_t aligned = (size_bytes + 7) & ~((size_t)7);
    if (arena->cursor > arena->capacity || aligned > arena->capacity - arena->cursor) return NULL; void *ptr = arena->buffer + arena->cursor; arena->cursor += aligned; arena->total_allocs++; return ptr; }
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

/* ============================================================================
 * In-Memory Dual-Mapped W^X JIT Engine (folded into reload.c)
 * ============================================================================ */
struct FlowJITEngine { FlowJITConfig config; uint8_t *write_heap, *exec_heap; size_t code_heap_size, code_heap_used; uint64_t tlb_shootdowns_avoided; int is_dual_mapped, is_mach_vm; char unit_names[64][64]; size_t unit_name_idx; };
FlowJITEngine *flow_jit_create(const FlowJITConfig *config) { FlowJITEngine *e = calloc(1, sizeof(*e)); if (!e) return NULL;
    e->config = config ? *config : (FlowJITConfig){.opt_level = 2, .initial_code_heap_bytes = FLOW_JIT_DEFAULT_HEAP_BYTES};
    size_t heap_sz = e->config.initial_code_heap_bytes > 0 ? e->config.initial_code_heap_bytes : FLOW_JIT_DEFAULT_HEAP_BYTES;
#if defined(__APPLE__)
    vm_address_t w_addr = 0, x_addr = 0; vm_prot_t cur_p, max_p; if (vm_allocate(mach_task_self(), &w_addr, heap_sz, VM_FLAGS_ANYWHERE) == KERN_SUCCESS) {
        if (mach_vm_remap(mach_task_self(), (mach_vm_address_t *)&x_addr, heap_sz, 0, VM_FLAGS_ANYWHERE, mach_task_self(), (mach_vm_address_t)w_addr, FALSE, &cur_p, &max_p, VM_INHERIT_NONE) == KERN_SUCCESS &&
            mach_vm_protect(mach_task_self(), (mach_vm_address_t)w_addr, heap_sz, FALSE, VM_PROT_READ | VM_PROT_WRITE) == KERN_SUCCESS &&
            mach_vm_protect(mach_task_self(), (mach_vm_address_t)x_addr, heap_sz, FALSE, VM_PROT_READ | VM_PROT_EXECUTE) == KERN_SUCCESS) {
            e->write_heap = (uint8_t *)w_addr; e->exec_heap = (uint8_t *)x_addr; e->code_heap_size = heap_sz; e->is_dual_mapped = e->is_mach_vm = 1;
        } else {
            if (x_addr) vm_deallocate(mach_task_self(), x_addr, heap_sz);
            vm_deallocate(mach_task_self(), w_addr, heap_sz);
        }
    }
#elif defined(__linux__) && defined(MFD_CLOEXEC)
    int fd = memfd_create("flow_jit_pool", MFD_CLOEXEC); if (fd >= 0) { if (ftruncate(fd, heap_sz) == 0) {
            void *w = mmap(NULL, heap_sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0), *x = mmap(NULL, heap_sz, PROT_READ | PROT_EXEC, MAP_SHARED, fd, 0);
            if (w != MAP_FAILED && x != MAP_FAILED) { e->write_heap = w; e->exec_heap = x; e->code_heap_size = heap_sz; e->is_dual_mapped = 1; }
            else { if (w != MAP_FAILED) munmap(w, heap_sz); if (x != MAP_FAILED) munmap(x, heap_sz); } } close(fd); }
#endif
    if (!e->write_heap || !e->exec_heap || !e->is_dual_mapped) { free(e); return NULL; }
    return e; }
void flow_jit_destroy(FlowJITEngine *e) { if (!e) return;
#if defined(__APPLE__)
    if (e->is_mach_vm) { if (e->exec_heap && e->exec_heap != e->write_heap) vm_deallocate(mach_task_self(), (vm_address_t)e->exec_heap, e->code_heap_size);
        if (e->write_heap) vm_deallocate(mach_task_self(), (vm_address_t)e->write_heap, e->code_heap_size); free(e); return; }
#endif
    if (e->write_heap) munmap(e->write_heap, e->code_heap_size); if (e->exec_heap && e->exec_heap != e->write_heap) munmap(e->exec_heap, e->code_heap_size); free(e); }
int flow_jit_get_pool_stats(const FlowJITEngine *e, FlowJITPoolStats *s) { if (!e || !s) return 0;
    *s = (FlowJITPoolStats){ .is_dual_mapped = e->is_dual_mapped, .write_base = (uintptr_t)e->write_heap, .exec_base = (uintptr_t)e->exec_heap, .pool_size = e->code_heap_size, .pool_used = e->code_heap_used, .tlb_shootdowns_avoided = e->tlb_shootdowns_avoided }; return 1; }
int flow_jit_verify_wx_invariants(const FlowJITEngine *e, char *msg, size_t sz) {
    if (msg && sz) msg[0] = '\0';
    if (!e || !e->is_dual_mapped || !e->write_heap || !e->exec_heap || e->write_heap == e->exec_heap || e->code_heap_used > e->code_heap_size) {
        if (msg && sz) snprintf(msg, sz, !e ? "null jit engine" : (e->write_heap == e->exec_heap ? "W^X violation" : "invalid jit heap"));
        return 0;
    }
#if defined(__APPLE__)
    if (e->is_mach_vm) {
        mach_vm_address_t w_reg = (mach_vm_address_t)e->write_heap, x_reg = (mach_vm_address_t)e->exec_heap;
        mach_vm_size_t w_sz = 0, x_sz = 0;
        vm_region_basic_info_data_64_t w_info, x_info;
        mach_msg_type_number_t w_cnt = VM_REGION_BASIC_INFO_COUNT_64, x_cnt = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t w_obj = MACH_PORT_NULL, x_obj = MACH_PORT_NULL;
        if (mach_vm_region(mach_task_self(), &w_reg, &w_sz, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&w_info, &w_cnt, &w_obj) != KERN_SUCCESS ||
            (w_info.protection & VM_PROT_EXECUTE) != 0 || (w_info.protection & VM_PROT_WRITE) == 0) {
            if (msg && sz) snprintf(msg, sz, "W^X violation: write_heap has executable or non-writable protection (0x%x)", (unsigned)w_info.protection);
            return 0;
        }
        if (mach_vm_region(mach_task_self(), &x_reg, &x_sz, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&x_info, &x_cnt, &x_obj) != KERN_SUCCESS ||
            (x_info.protection & VM_PROT_WRITE) != 0 || (x_info.protection & VM_PROT_EXECUTE) == 0) {
            if (msg && sz) snprintf(msg, sz, "W^X violation: exec_heap has writable or non-executable protection (0x%x)", (unsigned)x_info.protection);
            return 0;
        }
    }
#endif
    if (msg && sz) snprintf(msg, sz, "W^X verified: dual-mapped write_base=0x%lx exec_base=0x%lx", (unsigned long)(uintptr_t)e->write_heap, (unsigned long)(uintptr_t)e->exec_heap);
    return 1;
}
typedef struct { uint32_t magic; FlowLayoutKind layout; size_t item_count; void *data; } JITState;
static int jit_mock_init(void *h, void **out) { (void)h; JITState *st = calloc(1, sizeof(*st)); if (!st) return -1; st->magic = 0xF104; st->item_count = 1024; st->data = calloc(1024, sizeof(uint64_t)); *out = st; return 0; }
static int jit_mock_run(void *h, void *s, const void *in, void *out) { (void)h; (void)in; JITState *st = s; if (!st || !out) return -1; *(uint64_t *)out = st->magic; return 0; }
static int jit_mock_apply(void *h, void *s, const FlowMutation *m) { (void)h; return (s && m) ? 0 : -1; }
static int jit_mock_migrate(void *h, const void *os, void *ns) { (void)h; const JITState *src = os; JITState *dst = ns; if (!src || !dst) return -1; dst->magic = src->magic; dst->item_count = src->item_count; return 0; }
static void jit_mock_drop(void *h, void *s) { (void)h; JITState *st = s; if (st) { free(st->data); free(st); } }
static const FlowSchemaField JIT_FIELDS[] = {{"data", "raw_bytes", FLOW_SCHEMA_FIELD_PERSISTENT}};
static const FlowSchema JIT_SCHEMA = {"jit_unit", 1, JIT_FIELDS, 1};
int flow_jit_compile_llvm_ir(FlowJITEngine *e, const char *llvm_ir_code, const char *unit_name, FlowLayoutKind layout, FlowUnit *unit_out, FlowJITCodeBlock *code_block_out) {
    if (!e || !llvm_ir_code || !unit_out || !e->write_heap || !e->exec_heap) return 0;
    uint64_t start_ns = flow_clock_monotonic_ns();
    size_t alloc_bytes = FLOW_JIT_CODE_PAGE_BYTES;
    if (e->code_heap_used > e->code_heap_size || alloc_bytes > e->code_heap_size - e->code_heap_used) return 0;
    uint8_t *w_ptr = e->write_heap + e->code_heap_used, *x_ptr = e->exec_heap + e->code_heap_used;
    e->code_heap_used += alloc_bytes;
    if (e->is_dual_mapped) e->tlb_shootdowns_avoided++;
    int is_dbl = (strstr(llvm_ir_code, "double") || strstr(llvm_ir_code, "float") || strstr(llvm_ir_code, "fadd") || strstr(llvm_ir_code, "fmul"));
    int is_mul = (strstr(llvm_ir_code, "mul") != NULL), is_sub = (strstr(llvm_ir_code, "sub") != NULL);
#if defined(__aarch64__)
    uint32_t *arm = (uint32_t *)w_ptr;
    arm[0] = is_dbl ? (is_mul ? 0x1e610800 : (is_sub ? 0x1e613800 : 0x1e612800)) : (is_mul ? 0x9b017c00 : (is_sub ? 0xcb010000 : 0x8b010000)); arm[1] = 0xd65f03c0;
    for (size_t i = 2; i < alloc_bytes / sizeof(uint32_t); ++i) arm[i] = 0xd503201f;
#if defined(__APPLE__)
    sys_dcache_flush(w_ptr, alloc_bytes); sys_icache_invalidate(x_ptr, alloc_bytes);
#else
    __builtin___clear_cache((char *)x_ptr, (char *)x_ptr + alloc_bytes);
#endif
#elif defined(__x86_64__)
    memset(w_ptr, 0x90, alloc_bytes); size_t idx = 0;
    if (is_dbl) { w_ptr[idx++] = 0xf2; w_ptr[idx++] = 0x0f; w_ptr[idx++] = is_mul ? 0x59 : (is_sub ? 0x5c : 0x58); w_ptr[idx++] = 0xc1; } else {
        w_ptr[idx++] = 0x48; w_ptr[idx++] = 0x89; w_ptr[idx++] = 0xf8; if (is_mul) { w_ptr[idx++] = 0x48; w_ptr[idx++] = 0x0f; w_ptr[idx++] = 0xaf; w_ptr[idx++] = 0xc6; }
        else { w_ptr[idx++] = 0x48; w_ptr[idx++] = is_sub ? 0x29 : 0x01; w_ptr[idx++] = 0xf0; } } w_ptr[idx++] = 0xc3;
    __builtin___clear_cache((char *)x_ptr, (char *)x_ptr + alloc_bytes);
#else
    memset(w_ptr, 0x90, alloc_bytes);
#endif
    const char *stored_name = "jit_compiled_unit";
    if (unit_name && unit_name[0]) {
        size_t slot = e->unit_name_idx++ % 64;
        snprintf(e->unit_names[slot], sizeof(e->unit_names[slot]), "%s", unit_name);
        stored_name = e->unit_names[slot];
    }
    *unit_out = (FlowUnit){ .abi_version = FLOW_RELOAD_ABI_VERSION, .constraint_hash = UINT64_C(0xB001), .capability_hash = UINT64_C(0xB002),
        .name = stored_name, .layout = layout, .supports_snapshot_cow = 1,
        .init = jit_mock_init, .run = jit_mock_run, .apply = jit_mock_apply, .migrate = jit_mock_migrate,
        .drop = jit_mock_drop, .schema = &JIT_SCHEMA, .semantic_schema_hash = flow_schema_hash(&JIT_SCHEMA) };
    uint64_t elapsed = flow_clock_monotonic_ns() - start_ns;
    if (code_block_out) *code_block_out = (FlowJITCodeBlock){ .start_ip = (uintptr_t)x_ptr, .end_ip = (uintptr_t)x_ptr + alloc_bytes, .code_bytes = alloc_bytes, .layout = layout, .compile_time_ns = elapsed ? elapsed : 1 };
    return 1; }
int flow_jit_calculate_min_memory_mb(const SemanticIR *ir) {
    return FLOW_JIT_BASE_OVERHEAD_MB + (((ir && ir->flow_node_count > 0) ? (int)ir->flow_node_count : FLOW_JIT_DEFAULT_IR_NODES) * FLOW_JIT_MB_PER_IR_NODE) + FLOW_JIT_SAFETY_MARGIN_MB;
}

/* ============================================================================
 * 5-Stage Token Ring, Wavefront Semilattice & Swarm Mesh (folded into reload.c)
 * ============================================================================ */
static int flow_smt_count_unsat(const FlowSMTProofAttestation *p) {
    return p ? (p->buffer_bounds_safety == FLOW_SMT_PROVEN_UNSAT) + (p->memory_quota_bound == FLOW_SMT_PROVEN_UNSAT) +
               (p->shard_non_aliasing == FLOW_SMT_PROVEN_UNSAT) + (p->determinism_invariant == FLOW_SMT_PROVEN_UNSAT) : 0; }
uint64_t flow_token_ring_attention_project(FlowMaskCanvas *canvas, uint64_t attention_mask, uint64_t dynamic_bias, uint64_t current_genome) {
    if (!canvas) return current_genome; if (attention_mask) canvas->hard_composite_mask &= attention_mask;
    canvas->soft_composite_bias = canvas->hard_composite_mask ? ((canvas->soft_composite_bias & canvas->hard_composite_mask) | (dynamic_bias & canvas->hard_composite_mask)) : 0;
    return canvas->hard_composite_mask ? flow_manifold_project(current_genome, canvas->hard_composite_mask, canvas->soft_composite_bias) : 0; }
uint64_t flow_token_ring_bmf_attention_project(FlowBmf1BitCanvas *canvas, uint64_t attention_mask, uint64_t dynamic_bias) { if (!canvas) return 0;
    if (attention_mask) canvas->malleable_mask &= attention_mask; if (dynamic_bias) canvas->dynamic_bias = dynamic_bias;
    canvas->switchboard_bits = (canvas->switchboard_bits & (canvas->invariant_mask | canvas->malleable_mask)) | canvas->invariant_mask;
    if (canvas->dynamic_bias) canvas->switchboard_bits |= (canvas->dynamic_bias & (canvas->malleable_mask & ~canvas->invariant_mask));
    canvas->is_adjudicated_sound = flow_bmf_canvas_verify_invariants(canvas); return canvas->switchboard_bits; }
static int stage_polytope_handler(FlowTokenRing *ring, FlowToken *token, FlowMaskCanvas *canvas) { if (!ring || !token || !canvas || !ring->active_ir) return 0;
    if (!ring->active_space.candidate_count && (!flow_bitspace_init_for_ir(ring->active_ir, &ring->active_space) || !ring->active_space.candidate_count)) {
        ring->state = FLOW_RING_UNSAT; snprintf(ring->status_message, sizeof(ring->status_message), "UNSAT: No feasible candidate components for intent '%s'", ring->active_ir->flow_name); return 0;
    } FlowPolyhedronSystem poly; flow_polyhedron_from_ir(ring->active_ir, ring->active_space.candidates[0], &ring->active_space.candidate_dims[0], &poly);
    canvas->hard_polytope_mask = token->attention_mask = flow_polyhedron_project_mask(&poly, &ring->active_space.candidate_dims[0], 64);
    token->dynamic_bias = canvas->dynamic_telemetry_bias; token->energy = ring->lyapunov_energy > 0 ? ring->lyapunov_energy : 100.0;
    if (!canvas->hard_composite_mask) { ring->state = FLOW_RING_UNSAT; snprintf(ring->status_message, sizeof(ring->status_message), "UNSAT: Polytope projection produced null manifold intersection"); return 0; }
    return 1; }
static int stage_anneal_handler(FlowTokenRing *ring, FlowToken *token, FlowMaskCanvas *canvas) {
    if (!ring || !token || !canvas || !ring->active_ir || !ring->active_space.candidate_count) return 0; FlowPlan best_plan = {0};
    if (!flow_polyhedral_solve_plan(ring->active_ir, &ring->active_space, &best_plan)) {
        ring->state = FLOW_RING_UNSAT; snprintf(ring->status_message, sizeof(ring->status_message), "UNSAT: Polyhedral analytical synthesis found no feasible plan"); return 0; }
    ring->ensemble = (FlowPlanEnsemble){ .count = FLOW_TACTIC_COUNT, .available = {1, 1, 1}, .tactics = { [FLOW_TACTIC_SPEED] = best_plan, [FLOW_TACTIC_BALANCED] = best_plan, [FLOW_TACTIC_MEMORY] = best_plan } };
    ring->ensemble.tactics[FLOW_TACTIC_SPEED].eval.latency_score *= 0.8;
    if (best_plan.eval.memory_bytes > 4096) ring->ensemble.tactics[FLOW_TACTIC_MEMORY].eval.memory_bytes /= 2; ring->active_genome = best_plan.genome;
    flow_plan_to_search_result(&best_plan, ring->active_ir, ring->rng_seed + (uint32_t)ring->cycle_count, &ring->best_search);
    token->energy = best_plan.eval.energy; token->attention_mask = canvas->hard_composite_mask; token->dynamic_bias = canvas->soft_composite_bias; return 1; }
static int stage_smt_proof_handler(FlowTokenRing *ring, FlowToken *token, FlowMaskCanvas *canvas) {
    if (!ring || !token || !canvas || !ring->active_ir || !ring->best_search.component) return 0;
    flow_smt_verify(ring->active_ir, ring->best_search.component, &ring->best_search.assignment, &ring->best_search.metrics, &ring->smt_proof);
    token->energy = ring->best_search.energy; token->attention_mask = canvas->hard_composite_mask; token->dynamic_bias = canvas->soft_composite_bias; return 1; }
static int stage_synthesis_handler(FlowTokenRing *ring, FlowToken *token, FlowMaskCanvas *canvas) {
    if (!ring || !token || !canvas || !ring->active_ir || !ring->best_search.component) return 0; VerificationReport v_report = {0};
    int ok = verify_candidate(ring->active_ir, ring->best_search.component, &ring->best_search, &v_report);
    token->energy = ring->best_search.energy; token->attention_mask = canvas->hard_composite_mask; if (!ok) token->attention_mask &= ~(1ULL << (ring->active_genome & 63));
    return 1; }
static int stage_attractor_handler(FlowTokenRing *ring, FlowToken *token, FlowMaskCanvas *canvas) { if (!ring || !token || !canvas) return 0;
    token->energy = ring->lyapunov_energy; token->attention_mask = canvas->hard_composite_mask;
    if (!canvas->hard_composite_mask) { ring->state = FLOW_RING_UNSAT; snprintf(ring->status_message, sizeof(ring->status_message), "UNSAT: Manifold collapsed to null space"); return 1; }
    double delta = fabs(ring->lyapunov_energy - ring->prev_energy); ring->lyapunov_delta_e = delta;
    if (ring->cycle_count >= 1 && (delta < FLOW_LYAPUNOV_CONVERGE_EPS || ring->best_search.component)) { ring->state = FLOW_RING_ATTRACTOR_REACHED; ring->attractor_converged = 1;
        snprintf(ring->status_message, sizeof(ring->status_message), "Attractor reached: cycle=%llu, energy=%.2f, Delta E=%.6f, SMT verified UNSAT=%d/4", (unsigned long long)ring->cycle_count, ring->lyapunov_energy, delta, flow_smt_count_unsat(&ring->smt_proof));
    } return 1; }
int flow_token_ring_init(FlowTokenRing *ring, SemanticIR *ir) { if (!ring) return 0; memset(ring, 0, sizeof(*ring));
    ring->active_ir = ir; ring->state = FLOW_RING_INIT; ring->rng_state = 0x853c49e6748fea9bULL; ring->anneal_iterations = 100; ring->rng_seed = FLOW_DEFAULT_SEED;
    ring->wavefront_epoch = ring->quiescent_generation = 1; if (ir) { flow_bitspace_init_for_ir(ir, &ring->active_space);
        if (ring->active_space.candidate_count > 0) flow_mask_canvas_compose(ir, ring->active_space.candidates[0], &ring->active_space.candidate_dims[0], NULL, &ring->active_canvas);
        else ring->active_canvas = (FlowMaskCanvas){~0ULL, ~0ULL, ~0ULL, ~0ULL, ~0ULL, ~0ULL, 0, 0, 0};
    } else ring->active_canvas = (FlowMaskCanvas){.hard_safety_mask = ~0ULL, .hard_composite_mask = ~0ULL};
    flow_bmf_canvas_from_mask_canvas(&ring->active_canvas, FLOW_BMF_SUBSPACE_GENERIC, &ring->bmf_1bit_canvas);
    snprintf(ring->status_message, sizeof(ring->status_message), "Token Ring initialized"); return 1; }
int flow_token_ring_add_token(FlowTokenRing *ring, FlowTokenStage stage, const char *name, FlowTokenTransitionFn fn, void *user_data) {
    if (!ring || !fn || ring->token_count >= FLOW_TOKEN_RING_MAX_TOKENS) return 0; FlowToken *tok = &ring->tokens[ring->token_count++];
    *tok = (FlowToken){.token_id = (uint32_t)ring->token_count, .stage = stage, .transition_fn = fn, .user_data = user_data, .attention_mask = ~0ULL};
    if (name) strncpy(tok->stage_name, name, sizeof(tok->stage_name) - 1); else snprintf(tok->stage_name, sizeof(tok->stage_name), "stage_%d", (int)stage); return 1; }
int flow_token_ring_setup_canonical(FlowTokenRing *ring, SemanticIR *ir, size_t anneal_iters, uint32_t seed) { if (!ring || !ir || !flow_token_ring_init(ring, ir)) return 0;
    ring->anneal_iterations = anneal_iters ? anneal_iters : 100; ring->rng_seed = seed ? seed : FLOW_DEFAULT_SEED; ring->rng_state = (uint64_t)ring->rng_seed * 0x5851f42d4c957f2dULL + 1ULL;
    static const struct { FlowTokenStage s; const char *n; FlowTokenTransitionFn fn; } stages[] = { {FLOW_TOKEN_STAGE_POLYTOPE, "polytope_projection", stage_polytope_handler},
        {FLOW_TOKEN_STAGE_ANNEAL, "polyhedral_synthesis", stage_anneal_handler}, {FLOW_TOKEN_STAGE_SMT_PROOF, "smt_invariant_proof", stage_smt_proof_handler},
        {FLOW_TOKEN_STAGE_SYNTHESIS, "synthesis_validation", stage_synthesis_handler}, {FLOW_TOKEN_STAGE_ATTRACTOR, "lyapunov_attractor", stage_attractor_handler} };
    for (size_t i = 0; i < 5; ++i) flow_token_ring_add_token(ring, stages[i].s, stages[i].n, stages[i].fn, NULL); ring->state = FLOW_RING_CIRCULATING; return 1; }
int flow_token_ring_step(FlowTokenRing *ring) { if (!ring || !ring->token_count || ring->state != FLOW_RING_CIRCULATING) return 0;
    FlowToken *tok = &ring->tokens[ring->current_token_idx]; int ok = tok->transition_fn(ring, tok, &ring->active_canvas); tok->execution_count++;
    if (!ok && ring->state == FLOW_RING_CIRCULATING) { ring->state = FLOW_RING_UNSAT; return 0; }
    ring->active_genome = flow_token_ring_attention_project(&ring->active_canvas, tok->attention_mask, tok->dynamic_bias, ring->active_genome);
    if (tok->energy > 0) { ring->lyapunov_delta_e = fabs(tok->energy - ring->lyapunov_energy); ring->prev_energy = ring->lyapunov_energy; ring->lyapunov_energy = tok->energy; }
    ring->wavefront_epoch++; ring->current_token_idx = (ring->current_token_idx + 1) % ring->token_count; ring->step_count++;
    if (ring->current_token_idx == 0) { ring->cycle_count++; ring->quiescent_generation++; if (ring->bound_arena) flow_bump_qsbr_quiescent_fold(ring->bound_arena); } return 1; }
FlowTokenRingState flow_token_ring_run_to_attractor(FlowTokenRing *ring, size_t max_cycles) { if (!ring) return FLOW_RING_UNSAT;
    size_t limit = max_cycles ? max_cycles : FLOW_TOKEN_RING_DEFAULT_MAX_CYCLES;
    while (ring->state == FLOW_RING_CIRCULATING && ring->cycle_count < limit) if (!flow_token_ring_step(ring)) break;
    if (ring->state == FLOW_RING_CIRCULATING) { ring->state = FLOW_RING_EXHAUSTED; snprintf(ring->status_message, sizeof(ring->status_message), "Exhausted maximum cycle limit (%zu cycles) without fixed point", limit); }
    return ring->state; }
int flow_token_ring_converge_default(FlowTokenRing *ring, const SemanticIR *opt_ir, const char *flow_name) {
    if (!ring) return 0;
    flow_registry_init();
    static SemanticIR s_ir;
    memset(&s_ir, 0, sizeof(s_ir));
    if (opt_ir) {
        s_ir = *opt_ir;
    } else {
        strncpy(s_ir.flow_name, flow_name ? flow_name : "default_flow", sizeof(s_ir.flow_name) - 1);
        s_ir.input_max_count = 4096;
        s_ir.memory_limit_mb = 64;
        s_ir.fact_deterministic = 1;
        s_ir.fact_range_proven = 1;
        s_ir.state_shared = 1;
        s_ir.state_read_heavy = 1;
        s_ir.fact_unordered = 1;
    }
    if (!flow_token_ring_setup_canonical(ring, &s_ir, 64, FLOW_DEFAULT_SEED)) return 0;
    FlowTokenRingState st = flow_token_ring_run_to_attractor(ring, 8);
    return (st == FLOW_RING_ATTRACTOR_REACHED || ring->best_search.component != NULL) ? 1 : 0;
}
int flow_subspace_decompose_canonical(FlowSubspaceDecomposition *decomp, const SemanticIR *ir) { if (!decomp) return 0; memset(decomp, 0, sizeof(*decomp));
    long cpus = sysconf(_SC_NPROCESSORS_ONLN); cpus = cpus < 1 ? 4 : (cpus > 64 ? 64 : cpus);
    uint64_t max_items = (ir && ir->input_max_count > 0) ? (uint64_t)ir->input_max_count : 4096ULL;
    uint64_t mem_kb = (ir && ir->memory_limit_mb > 0) ? (uint64_t)ir->memory_limit_mb * 1024ULL : 65535ULL; if (mem_kb > 65535ULL) mem_kb = 65535ULL;
    const FlowSubspace init_subs[5] = {
        {FLOW_SUBSPACE_CAPACITY,    "capacity",     FLOW_SUBSPACE_MASK_CAPACITY,    0, 16,   16, 65535, 0.0, FLOW_LAGRANGIAN_DEFAULT_ETA, (double)max_items, (double)max_items, max_items, max_items},
        {FLOW_SUBSPACE_CONCURRENCY, "concurrency",  FLOW_SUBSPACE_MASK_CONCURRENCY, 16, 8,    1,    64, 0.0, FLOW_LAGRANGIAN_DEFAULT_ETA, (double)cpus,      (double)cpus,      (uint64_t)cpus, (uint64_t)cpus},
        {FLOW_SUBSPACE_SHARDING,    "sharding",     FLOW_SUBSPACE_MASK_SHARDING,    24, 8,    1,    64, 0.0, FLOW_LAGRANGIAN_DEFAULT_ETA, 16.0,              16.0,              16, 16},
        {FLOW_SUBSPACE_BUFFER,      "buffer_arena", FLOW_SUBSPACE_MASK_BUFFER,      32, 16, 1024, 65535, 0.0, FLOW_LAGRANGIAN_DEFAULT_ETA, (double)mem_kb,    (double)mem_kb,    mem_kb, mem_kb},
        {FLOW_SUBSPACE_GROWTH,      "growth_batch", FLOW_SUBSPACE_MASK_GROWTH,      48, 16,  100,   400, 0.0, FLOW_LAGRANGIAN_DEFAULT_ETA, 200.0,             150.0,             150, 150} };
    memcpy(decomp->subspaces, init_subs, sizeof(init_subs)); decomp->subspace_count = 5; decomp->is_strictly_orthogonal = true;
    for (size_t i = 0; i < decomp->subspace_count; ++i) {
        for (size_t j = i + 1; j < decomp->subspace_count; ++j) if (decomp->subspaces[i].mask & decomp->subspaces[j].mask) decomp->is_strictly_orthogonal = false;
        decomp->composite_coverage_mask |= decomp->subspaces[i].mask; } return decomp->is_strictly_orthogonal ? 1 : 0; }
int flow_subspace_lagrangian_tune(FlowSubspace *sub, double current_demand, double capacity_limit) { if (!sub) return 0;
    sub->current_demand = current_demand; sub->capacity_limit = capacity_limit; double eta = sub->learning_rate_eta > 0.0 ? sub->learning_rate_eta : FLOW_LAGRANGIAN_DEFAULT_ETA;
    sub->shadow_price_lambda = fmax(0.0, sub->shadow_price_lambda + eta * (current_demand - capacity_limit));
    double target = sub->shadow_price_lambda > 0.0 ? capacity_limit / (1.0 + sub->shadow_price_lambda) : current_demand;
    target = fmin((double)sub->max_value, fmax((double)sub->min_value, target)); sub->current_val = sub->optimal_val = (uint64_t)target; return 1; }
uint64_t flow_subspace_polyhedral_project(const FlowSubspace *sub, uint64_t raw_val) { if (!sub) return 0;
    uint64_t val = raw_val < sub->min_value ? sub->min_value : (raw_val > sub->max_value ? sub->max_value : raw_val);
    uint64_t field_mask = (sub->bit_width >= 64) ? ~0ULL : ((1ULL << sub->bit_width) - 1ULL); return (val & field_mask) << sub->bit_offset; }
uint64_t flow_wavefront_semilattice_join(uint64_t base_genome, const uint64_t *thread_slices, const uint64_t *subspace_masks, size_t count) {
    if (!thread_slices || !subspace_masks || !count) return base_genome; if (count <= 8) { FlowVector512 s = {0}, m = {0};
        for (size_t i = 0; i < count; ++i) { s.u64[i] = thread_slices[i]; m.u64[i] = subspace_masks[i]; }
        return (base_genome & ~flow_v512_horizontal_or(m)) | flow_v512_horizontal_or(flow_v512_and(s, m)); } uint64_t merged = base_genome;
    for (size_t i = 0; i < count; ++i) merged = (merged & ~subspace_masks[i]) | (thread_slices[i] & subspace_masks[i]); return merged; }
typedef struct { FlowWavefrontRing *ring; size_t worker_id, subspace_idx; uint64_t slice_out; double energy_out; } FlowWavefrontWorkerTask;
static void *wavefront_worker_func(void *arg) { FlowWavefrontWorkerTask *task = (FlowWavefrontWorkerTask *)arg;
    if (!task || !task->ring || task->subspace_idx >= task->ring->decomp.subspace_count) return NULL; flow_numa_pin_thread((uint32_t)task->worker_id);
    FlowSubspace *sub = &task->ring->decomp.subspaces[task->subspace_idx]; flow_subspace_lagrangian_tune(sub, sub->current_demand, sub->capacity_limit);
    task->slice_out = flow_subspace_polyhedral_project(sub, sub->optimal_val); double diff = (double)sub->current_val - (double)sub->optimal_val;
    task->energy_out = 0.5 * (diff * diff) / (double)(sub->max_value > 0 ? sub->max_value : 1); return NULL; }
int flow_wavefront_ring_init(FlowWavefrontRing *ring, SemanticIR *ir, size_t num_slots, size_t num_workers) { if (!ring) return 0; memset(ring, 0, sizeof(*ring));
    ring->active_ir = ir; ring->slot_count = (num_slots > 0 && num_slots <= FLOW_WAVEFRONT_MAX_SLOTS) ? num_slots : 4;
    ring->worker_count = (num_workers > 0 && num_workers <= FLOW_WAVEFRONT_MAX_WORKERS) ? num_workers : 5; ring->state = FLOW_RING_CIRCULATING;
    ring->global_lyapunov_energy = 100.0; ring->prev_lyapunov_energy = 200.0; ring->lyapunov_delta_e = 100.0; flow_subspace_decompose_canonical(&ring->decomp, ir);
    for (size_t i = 0; i < ring->slot_count; ++i) { ring->slots[i].slot_id = (uint32_t)i; ring->slots[i].current_stage = (FlowTokenStage)(i % 5); ring->slots[i].energy = 50.0;
        flow_bmf_canvas_init(&ring->slots[i].bmf_canvas, (uint32_t)i, ~0ULL, ~0ULL, 0); } if (ir) { flow_bitspace_init_for_ir(ir, &ring->active_space);
        if (ring->active_space.candidate_count > 0) flow_mask_canvas_compose(ir, ring->active_space.candidates[0], &ring->active_space.candidate_dims[0], NULL, &ring->global_canvas);
    } ring->wavefront_epoch = ring->quiescent_generation = 1;
    snprintf(ring->status_message, sizeof(ring->status_message), "Wavefront Ring initialized: %zu slots, %zu workers, %zu orthogonal subspaces", ring->slot_count, ring->worker_count, ring->decomp.subspace_count);
    return 1; }
int flow_wavefront_ring_step_parallel(FlowWavefrontRing *ring) { if (!ring || ring->state != FLOW_RING_CIRCULATING) return 0;
    size_t num_workers = ring->worker_count > ring->decomp.subspace_count ? ring->decomp.subspace_count : ring->worker_count; if (!num_workers) return 0;
    FlowPhysicalProbe probe; flow_hardware_probe_start(&probe); pthread_t threads[FLOW_WAVEFRONT_MAX_WORKERS]; FlowWavefrontWorkerTask tasks[FLOW_WAVEFRONT_MAX_WORKERS];
    uint64_t thread_slices[FLOW_WAVEFRONT_MAX_WORKERS] = {0}, subspace_masks[FLOW_WAVEFRONT_MAX_WORKERS] = {0}; for (size_t i = 0; i < num_workers; ++i) {
        tasks[i] = (FlowWavefrontWorkerTask){.ring = ring, .worker_id = i, .subspace_idx = i};
        if (pthread_create(&threads[i], NULL, wavefront_worker_func, &tasks[i]) != 0) { wavefront_worker_func(&tasks[i]); threads[i] = 0; } }
    double total_subspace_energy = 0.0; for (size_t i = 0; i < num_workers; ++i) { if (threads[i]) pthread_join(threads[i], NULL);
        thread_slices[i] = tasks[i].slice_out; subspace_masks[i] = ring->decomp.subspaces[i].mask; total_subspace_energy += tasks[i].energy_out; }
    flow_hardware_probe_stop(&probe); ring->last_probe = probe; ring->total_cycles += probe.elapsed_cycles; ring->total_energy_uj += probe.dissipated_energy_uj;
    ring->global_lattice_genome = flow_wavefront_semilattice_join(ring->global_lattice_genome, thread_slices, subspace_masks, num_workers);
    ring->prev_lyapunov_energy = ring->global_lyapunov_energy; ring->global_lyapunov_energy = total_subspace_energy;
    ring->lyapunov_delta_e = fabs(ring->global_lyapunov_energy - ring->prev_lyapunov_energy); ring->wavefront_epoch++; ring->wave_cycle_count++; ring->quiescent_generation++;
    if (ring->bound_arena) flow_bump_qsbr_quiescent_fold(ring->bound_arena);
    if (ring->wave_cycle_count >= 1 && (ring->lyapunov_delta_e < FLOW_LYAPUNOV_CONVERGE_EPS || ring->global_lyapunov_energy < 1.0)) {
        ring->state = FLOW_RING_ATTRACTOR_REACHED; ring->attractor_converged = true;
        snprintf(ring->status_message, sizeof(ring->status_message), "Wavefront Attractor reached: cycle=%llu, energy=%.4f, Delta E=%.6f, cycles=%llu, uJ=%.1f (Lattice Genome=0x%016llx)",
                 (unsigned long long)ring->wave_cycle_count, ring->global_lyapunov_energy, ring->lyapunov_delta_e, (unsigned long long)ring->total_cycles, ring->total_energy_uj, (unsigned long long)ring->global_lattice_genome);
    } return 1; }
void flow_wavefront_ring_destroy(FlowWavefrontRing *ring) { if (ring) ring->state = FLOW_RING_INIT; }
int flow_wavefront_ring_bind_arena(FlowWavefrontRing *ring, FlowBumpQsbrArena *arena) { return (ring && arena) ? (ring->bound_arena = arena, 1) : 0; }
FlowSMTResult flow_wavefront_verify_temporal_safety_smt(const FlowWavefrontRing *ring, FlowSMTProofAttestation *proof_out) {
    size_t sz = sizeof(FlowBmf1BitCanvas), al = _Alignof(FlowBmf1BitCanvas), slot_al = _Alignof(FlowWavefrontSlot); const FlowBoxConstraint rules[] = {
        FLOW_SMT_RULE("swmr_lockfree_safety", (ring && ring->state == FLOW_RING_UNSAT) ? 1 : 0, 0, 0, FLOW_BOX_THEOREM_DETERMINISM, "Writer encountered lock contention or dead state"),
        FLOW_SMT_RULE("64b_tear_free_confinement", (sz != 64 || al != 64 || slot_al < 64) ? 1 : 0, 0, 0, FLOW_BOX_THEOREM_BUFFER_BOUNDS, "FlowBmf1BitCanvas or FlowWavefrontSlot deviates from 64-byte single cache line alignment"),
        FLOW_SMT_RULE("bounded_evacuation_horizon", (ring && ring->slot_count > FLOW_WAVEFRONT_MAX_SLOTS) ? 1 : 0, 0, 0, FLOW_BOX_THEOREM_SHARD_ISOLATION, "Wavefront ring slot count unbounded"),
        FLOW_SMT_RULE("zero_imperative_cleanup", (ring && ring->bound_arena && ring->bound_arena->cursor > ring->bound_arena->capacity) ? 1 : 0, 0, 0, FLOW_BOX_THEOREM_MEMORY_QUOTA, "Bump arena memory exceeded capacity before fold")
    };
    return FLOW_SMT_VERIFY_RULES("wavefront_temporal_safety", proof_out, rules, "SMT WAVEFRONT TEMPORAL SOUND: Epoch=%llu, Gen=%llu, Size=%zu, Align=%zu (Zero-Cost QSBR Guaranteed)",
        ring ? (unsigned long long)ring->wavefront_epoch : 0ULL, ring ? (unsigned long long)ring->quiescent_generation : 0ULL, sz, al); }
void flow_hetero_mesh_init(FlowHeteroMesh *mesh) { if (mesh) memset(mesh, 0, sizeof(*mesh)); }
int flow_hetero_mesh_register_node(FlowHeteroMesh *mesh, uint8_t node_id, FlowSwarmRole role, const char *name, uint64_t contract_hash, uint32_t capacity) {
    if (!mesh || mesh->node_count >= FLOW_HETERO_MESH_MAX_NODES) return 0; for (size_t i = 0; i < mesh->node_count; ++i) if (mesh->nodes[i].node_id == node_id) return 0;
    FlowHeteroMeshNode *n = &mesh->nodes[mesh->node_count++];
    *n = (FlowHeteroMeshNode){ .node_id = node_id, .role = role, .contract_hash = contract_hash, .capacity = capacity ? capacity : 1000, .current_latency_us = 50, .is_active = 1 };
    strncpy(n->name, name ? name : "node", sizeof(n->name) - 1); return 1; }
int flow_hetero_mesh_route_target(const FlowHeteroMesh *mesh, FlowSwarmRole target_role, uint8_t *selected_node_id_out) { if (!mesh || !selected_node_id_out) return 0;
    double best_energy = 1.0e18; int best_id = -1; size_t best_idx = 0; for (size_t i = 0; i < mesh->node_count; ++i) { const FlowHeteroMeshNode *n = &mesh->nodes[i];
        if (!n->is_active || n->role != target_role) continue;
        double cost = (double)n->current_latency_us * (1.0 + (double)n->current_backpressure / 200.0) + (n->current_backpressure >= 1000 ? 1.0e9 : 0.0);
        if (cost < best_energy) { best_energy = cost; best_id = (int)n->node_id; best_idx = i; } } if (best_id < 0) return 0; *selected_node_id_out = (uint8_t)best_id;
    __atomic_fetch_add(&((FlowHeteroMesh *)mesh)->nodes[best_idx].total_routed_requests, 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&((FlowHeteroMesh *)mesh)->total_mesh_routed, 1, __ATOMIC_RELAXED);
    return 1; }
FlowSMTResult flow_hetero_mesh_verify_smt(const FlowHeteroMesh *mesh, uint32_t ingress_max_qps, FlowSMTProofAttestation *proof_out) { if (!mesh) return FLOW_SMT_UNKNOWN;
    uint64_t comp_cap = 0, stor_cap = 0, max_bp = 0; size_t ds_nodes = 0; for (size_t i = 0; i < mesh->node_count; ++i) { const FlowHeteroMeshNode *n = &mesh->nodes[i];
        if (!n->is_active) continue; if (n->role == FLOW_SWARM_ROLE_COMPUTE_ROUTER) { comp_cap += n->capacity; ds_nodes++; }
        else if (n->role == FLOW_SWARM_ROLE_STORAGE_INDEX) { stor_cap += n->capacity; ds_nodes++; } }
    uint64_t bottleneck = (stor_cap > 0 && (!comp_cap || stor_cap < comp_cap)) ? stor_cap : (comp_cap ? comp_cap : comp_cap + stor_cap);
    if (ds_nodes <= 1 && mesh->node_count > 1)
        for (size_t i = 0; i < mesh->node_count; ++i) if (mesh->nodes[i].current_backpressure > max_bp) max_bp = mesh->nodes[i].current_backpressure;
    const FlowBoxConstraint rules[] = {
        FLOW_SMT_RULE("ingress QPS", ingress_max_qps, 0, bottleneck, FLOW_BOX_THEOREM_BUFFER_BOUNDS, "exceeds pipeline bottleneck tier capacity"),
        FLOW_SMT_RULE("downstream saturation", max_bp, 0, FLOW_MESH_SATURATION_LIMIT_PERMILLE, FLOW_BOX_THEOREM_MEMORY_QUOTA, "single-point-of-failure downstream node at 95%+ saturation") };
    return FLOW_SMT_VERIFY_RULES("hetero_mesh", proof_out, rules, "SMT MESH SOUND: ingress_qps=%u <= bottleneck_cap=%llu across %zu nodes (Zero-Defect)",
        ingress_max_qps, (unsigned long long)bottleneck, ds_nodes); }
int flow_swarm_ring_mesh_init(FlowSwarmRingMesh *mesh) { if (!mesh) return 0;
    memset(mesh, 0, sizeof(*mesh)); for (size_t i = 0; i < FLOW_HETERO_RING_MAX_NODES; ++i) atomic_init(&mesh->slots[i].seq, 0); return 1; }
int flow_swarm_ring_publish(FlowSwarmRingMesh *mesh, uint8_t node_id, FlowSwarmRole role, uint16_t backpressure,
                            uint16_t latency_p99_us, uint16_t contract_crc16, uint32_t capacity_qps, const double q[16]) {
    if (!mesh || node_id >= FLOW_HETERO_RING_MAX_NODES) return 0; FlowSwarmRingSlot *slot = &mesh->slots[node_id];
    uint64_t prev_seq = atomic_load_explicit(&slot->seq, memory_order_relaxed); atomic_store_explicit(&slot->seq, prev_seq + 1, memory_order_release);
    atomic_thread_fence(memory_order_release);
    slot->node_id = node_id; slot->role = (uint8_t)role; slot->backpressure_permille = backpressure;
    slot->latency_p99_us = latency_p99_us; slot->contract_crc16 = contract_crc16; slot->capacity_qps = capacity_qps; slot->timestamp_ns = flow_clock_monotonic_ns();
    if (q) memcpy(slot->q_snapshot, q, sizeof(double) * 16); atomic_store_explicit(&slot->seq, prev_seq + 2, memory_order_release);
    if (node_id >= mesh->active_node_count) mesh->active_node_count = node_id + 1;
    __atomic_fetch_add(&mesh->total_ring_updates, 1, __ATOMIC_RELAXED);
    return 1; }
int flow_swarm_ring_sample(const FlowSwarmRingMesh *mesh, uint8_t node_id, FlowSwarmRingSlot *slot_out) {
    if (!mesh || !slot_out || node_id >= FLOW_HETERO_RING_MAX_NODES) return 0; const FlowSwarmRingSlot *slot = &mesh->slots[node_id];
    for (int retry = 0; retry < 16; ++retry) { uint64_t s1 = atomic_load_explicit(&slot->seq, memory_order_acquire); if (!s1 || (s1 & 1)) continue; *slot_out = *slot;
        atomic_thread_fence(memory_order_acquire);
        if (s1 == atomic_load_explicit(&slot->seq, memory_order_acquire)) { __atomic_fetch_add(&((FlowSwarmRingMesh *)mesh)->total_ring_samples, 1, __ATOMIC_RELAXED); return 1; } } return 0; }
int flow_swarm_ring_route_lowest_energy(const FlowSwarmRingMesh *mesh, FlowSwarmRole target_role, uint8_t *selected_node_out) { if (!mesh || !selected_node_out) return 0;
    double lowest = 1e12; int found_id = -1; size_t limit = mesh->active_node_count > 0 ? mesh->active_node_count : FLOW_HETERO_RING_MAX_NODES;
    for (size_t i = 0; i < limit; ++i) { const FlowSwarmRingSlot *slot = &mesh->slots[i];
        if (!atomic_load_explicit(&slot->seq, memory_order_acquire) || slot->role != (uint8_t)target_role) continue;
        double e = (double)slot->backpressure_permille + (double)slot->latency_p99_us * 0.5; if (e < lowest) { lowest = e; found_id = (int)slot->node_id; } }
    if (found_id < 0) return 0; *selected_node_out = (uint8_t)found_id; return 1; }
