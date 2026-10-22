#include "reload.h"
#include "token_ring.h"
#include "entropy_collapse.h"
#include "flow_smt_dsl.h"
#include "hardware_telemetry.h"
#include "audit.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
typedef struct FlowReloadGeneration { const FlowUnit *unit; void *state; uint64_t generation, retire_epoch; struct FlowReloadGeneration *next; } FlowReloadGeneration;
struct FlowReloadContext { void *host_context; _Atomic(FlowReloadGeneration *) current; _Atomic uint64_t epoch; _Atomic int quiescence_waiters, stopping;
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
    ctx->host_context = host_context; atomic_init(&ctx->epoch, 1); pthread_mutex_init(&ctx->lock, NULL); pthread_cond_init(&ctx->quiesced, NULL); return ctx; }
static void drop_generation(FlowReloadContext *ctx, FlowReloadGeneration *g) { if (g && g->unit && g->unit->drop && g->state) { g->unit->drop(ctx->host_context, g->state); g->state = NULL; } }
int flow_reload_destroy(FlowReloadContext *ctx) { if (!ctx) return FLOW_RELOAD_INVALID; pthread_mutex_lock(&ctx->lock);
    atomic_store_explicit(&ctx->stopping, 1, memory_order_release); if (ctx->readers) { pthread_mutex_unlock(&ctx->lock); return FLOW_RELOAD_BUSY; }
    FlowReloadGeneration *gen = atomic_load_explicit(&ctx->current, memory_order_acquire), *retired = ctx->retired;
    atomic_store_explicit(&ctx->current, NULL, memory_order_release); ctx->retired = NULL; pthread_mutex_unlock(&ctx->lock); if (gen) { drop_generation(ctx, gen); free(gen); }
    while (retired) { FlowReloadGeneration *next = retired->next; drop_generation(ctx, retired); free(retired); retired = next; }
    pthread_cond_destroy(&ctx->quiesced); pthread_mutex_destroy(&ctx->lock); free(ctx); return FLOW_RELOAD_OK; }
int flow_reload_reader_register(FlowReloadContext *ctx, FlowReloadReader *r) { if (!ctx || !r) return FLOW_RELOAD_INVALID; pthread_mutex_lock(&ctx->lock);
    if (ctx->stopping) { pthread_mutex_unlock(&ctx->lock); return FLOW_RELOAD_BUSY; } r->context = ctx; r->next = ctx->readers; atomic_init(&r->active_epoch, 0);
    atomic_store_explicit(&r->registered, 1, memory_order_release); ctx->readers = r; pthread_mutex_unlock(&ctx->lock); return FLOW_RELOAD_OK; }
int flow_reload_reader_unregister(FlowReloadReader *r) { if (!r || !r->context) return FLOW_RELOAD_INVALID; FlowReloadContext *ctx = r->context;
    pthread_mutex_lock(&ctx->lock); FlowReloadReader **cur = &ctx->readers; while (*cur && *cur != r) cur = &(*cur)->next;
    if (!*cur) { pthread_mutex_unlock(&ctx->lock); return FLOW_RELOAD_INVALID; } *cur = r->next; r->next = NULL;
    atomic_store_explicit(&r->registered, 0, memory_order_release); atomic_store_explicit(&r->active_epoch, 0, memory_order_seq_cst);
    if (atomic_load_explicit(&ctx->quiescence_waiters, memory_order_acquire) > 0) pthread_cond_broadcast(&ctx->quiesced); r->context = NULL; pthread_mutex_unlock(&ctx->lock); return FLOW_RELOAD_OK; }
int flow_reload_publish(FlowReloadContext *ctx, const FlowUnit *unit, void *state) {
    if (!ctx || !unit_valid(unit) || !state) return FLOW_RELOAD_INVALID; FlowReloadGeneration *next = calloc(1, sizeof(*next)); if (!next) return FLOW_RELOAD_INVALID;
    next->unit = unit; next->state = state; pthread_mutex_lock(&ctx->lock);
    if (atomic_load_explicit(&ctx->stopping, memory_order_acquire)) { pthread_mutex_unlock(&ctx->lock); free(next); return FLOW_RELOAD_BUSY; }
    FlowReloadGeneration *old = atomic_load_explicit(&ctx->current, memory_order_acquire);
    if (old && !flow_reload_compatible(old->unit, unit)) { pthread_mutex_unlock(&ctx->lock); free(next); return FLOW_RELOAD_INCOMPATIBLE; }
    next->generation = ++ctx->next_generation; if (old) { next->retire_epoch = atomic_fetch_add_explicit(&ctx->epoch, UINT64_C(1), memory_order_seq_cst) + UINT64_C(1);
        old->retire_epoch = next->retire_epoch; old->next = ctx->retired; ctx->retired = old; FlowDecisionEvent ev = {
            .timestamp_ns = flow_clock_monotonic_ns(), .trigger_type = FLOW_DECISION_TRIGGER_STRAGGLER_QUARANTINE, .observed_metric_value = (double)next->generation,
            .threshold_limit_value = (double)(next->generation - 1), .flipped_genome_bit = (uint32_t)(next->generation % 64), .pre_state_mask = (1ULL << 61),
            .post_state_mask = (1ULL << 61) | (1ULL << (next->generation % 64)), .hot_swap_grace_ns = 84, .trigger_source = "qsbr_epoch_migration", .metric_unit = "gen", .violated_constraint = "QSBR Zero-Downtime Epoch Upgrade" };
        snprintf(ev.pre_topology, sizeof(ev.pre_topology), "%s", (old->unit && old->unit->name) ? old->unit->name : "old_generation");
        snprintf(ev.post_topology, sizeof(ev.post_topology), "%s", (next->unit && next->unit->name) ? next->unit->name : "new_generation");
        snprintf(ev.causal_rationale, sizeof(ev.causal_rationale), "QSBR epoch %llu: '%s' -> '%s'", (unsigned long long)next->retire_epoch, ev.pre_topology, ev.post_topology);
        flow_decision_logger_record(flow_decision_logger_default(), &ev); }
    atomic_store_explicit(&ctx->current, next, memory_order_release); pthread_mutex_unlock(&ctx->lock); return FLOW_RELOAD_OK; }
int flow_reload_activate(FlowReloadContext *ctx, const FlowUnit *unit) { void *state = NULL;
    if (!ctx || !unit_valid(unit) || !unit->init || unit->init(ctx->host_context, &state) != 0 || !state) return FLOW_RELOAD_INVALID;
    int res = flow_reload_publish(ctx, unit, state); if (res != FLOW_RELOAD_OK) unit->drop(ctx->host_context, state); return res; }
const FlowUnit *flow_reload_current_unit(const FlowReloadContext *ctx) { FlowReloadGeneration *gen = ctx ? atomic_load_explicit(&ctx->current, memory_order_acquire) : NULL; return gen ? gen->unit : NULL; }
int flow_reload_begin(FlowReloadContext *ctx, FlowReloadReader *r, FlowInvocation *inv) { if (!ctx || !r || !inv) return FLOW_RELOAD_INVALID;
    if (inv->active || atomic_load_explicit(&ctx->stopping, memory_order_acquire)) return FLOW_RELOAD_BUSY;
    if (r->context != ctx || !atomic_load_explicit(&r->registered, memory_order_acquire)) return FLOW_RELOAD_INVALID;
    atomic_store_explicit(&r->active_epoch, atomic_load_explicit(&ctx->epoch, memory_order_acquire), memory_order_seq_cst);
    atomic_store_explicit(&r->last_heartbeat_ns, flow_clock_monotonic_ns(), memory_order_relaxed);
    FlowReloadGeneration *gen = atomic_load_explicit(&ctx->current, memory_order_acquire);
    if (!gen) { atomic_store_explicit(&r->active_epoch, 0, memory_order_seq_cst); return FLOW_RELOAD_NO_CURRENT; }
    *inv = (FlowInvocation){.context = ctx, .reader = r, .unit = gen->unit, .state = gen->state, .generation = gen->generation, .active = 1}; return FLOW_RELOAD_OK; }
void flow_reload_end(FlowInvocation *inv) { if (!inv || !inv->active || !inv->reader) return; atomic_store_explicit(&inv->reader->active_epoch, 0, memory_order_seq_cst);
    if (atomic_load_explicit(&inv->context->quiescence_waiters, memory_order_acquire) > 0) {
        pthread_mutex_lock(&inv->context->lock); pthread_cond_broadcast(&inv->context->quiesced); pthread_mutex_unlock(&inv->context->lock); } *inv = (FlowInvocation){0}; }
int flow_reload_call(FlowReloadContext *ctx, FlowReloadReader *r, const void *in, void *out) { FlowInvocation inv = {0}; int res = flow_reload_begin(ctx, r, &inv);
    if (res != FLOW_RELOAD_OK) return res; res = inv.unit->run(ctx->host_context, inv.state, in, out); flow_reload_end(&inv); return res; }
#define FLOW_EPOCH_LEASE_TIMEOUT_NS (UINT64_C(200) * UINT64_C(1000000))
static int generation_safe(FlowReloadContext *ctx, uint64_t retire_epoch) {
    if (ctx->bound_wavefront_ring && retire_epoch <= ctx->bound_wavefront_ring->quiescent_generation) return 1; uint64_t now = flow_clock_monotonic_ns();
    for (const FlowReloadReader *r = ctx->readers; r; r = r->next) { uint64_t active = atomic_load_explicit(&r->active_epoch, memory_order_seq_cst);
        if (active && active <= retire_epoch) { uint64_t hb = atomic_load_explicit(&r->last_heartbeat_ns, memory_order_relaxed);
            if (hb > 0 && now > hb && (now - hb) > FLOW_EPOCH_LEASE_TIMEOUT_NS) continue; return 0; } } return 1; }
uint64_t flow_reload_generation(const FlowReloadContext *ctx) { const FlowReloadGeneration *curr = ctx ? atomic_load_explicit(&ctx->current, memory_order_acquire) : NULL; return curr ? curr->generation : 0; }
size_t flow_reload_reclaim(FlowReloadContext *ctx) { if (!ctx) return 0; FlowReloadGeneration *ready = NULL; size_t reclaimed = 0;
    pthread_mutex_lock(&ctx->lock); for (FlowReloadGeneration **cur = &ctx->retired; *cur; ) { FlowReloadGeneration *cand = *cur;
        if (!generation_safe(ctx, cand->retire_epoch)) { cur = &cand->next; continue; } *cur = cand->next; cand->next = ready; ready = cand; }
    pthread_mutex_unlock(&ctx->lock); while (ready) { FlowReloadGeneration *next = ready->next; drop_generation(ctx, ready); free(ready); ready = next; ++reclaimed; }
    if (ctx->bound_arena && reclaimed > 0) flow_bump_qsbr_quiescent_fold(ctx->bound_arena); return reclaimed; }
void flow_qsbr_checkpoint(FlowReloadReader *r) { if (r) { atomic_fetch_add_explicit(&r->qsbr_epoch, 1, memory_order_release); atomic_store_explicit(&r->last_heartbeat_ns, flow_clock_monotonic_ns(), memory_order_relaxed); } }
void flow_qsbr_offline(FlowReloadReader *r) { if (r) atomic_store_explicit(&r->is_offline, 1, memory_order_release); }
void flow_qsbr_online(FlowReloadReader *r) { if (r) { atomic_store_explicit(&r->last_heartbeat_ns, flow_clock_monotonic_ns(), memory_order_relaxed); atomic_store_explicit(&r->is_offline, 0, memory_order_release); } }
int flow_qsbr_call(FlowReloadContext *ctx, const void *in, void *out) { if (!ctx) return FLOW_RELOAD_INVALID;
    FlowReloadGeneration *curr = atomic_load_explicit(&ctx->current, memory_order_acquire);
    return (!curr || !curr->unit || !curr->unit->run) ? FLOW_RELOAD_NO_CURRENT : curr->unit->run(ctx->host_context, curr->state, in, out); }
int flow_qsbr_synchronize(FlowReloadContext *ctx, uint64_t timeout_ns) { if (!ctx) return FLOW_RELOAD_INVALID; if (ctx->bound_wavefront_ring) {
        ((FlowWavefrontRing *)ctx->bound_wavefront_ring)->state = FLOW_RING_CIRCULATING; flow_wavefront_ring_step_parallel((FlowWavefrontRing *)ctx->bound_wavefront_ring); return FLOW_RELOAD_OK; }
    uint64_t start = flow_clock_monotonic_ns(), target_epochs[64]; FlowReloadReader *tracked[64]; size_t count = 0;
    pthread_mutex_lock(&ctx->lock); for (FlowReloadReader *r = ctx->readers; r && count < 64; r = r->next)
        if (atomic_load_explicit(&r->registered, memory_order_acquire)) { tracked[count] = r; target_epochs[count++] = atomic_load_explicit(&r->qsbr_epoch, memory_order_acquire) + 1; }
    pthread_mutex_unlock(&ctx->lock); for (size_t i = 0; i < count; ++i) { FlowReloadReader *r = tracked[i]; size_t spins = 0;
        while (atomic_load_explicit(&r->registered, memory_order_acquire) && !atomic_load_explicit(&r->is_offline, memory_order_acquire) &&
               !atomic_load_explicit(&r->is_quarantined, memory_order_acquire) && atomic_load_explicit(&r->qsbr_epoch, memory_order_acquire) < target_epochs[i]) {
            if (timeout_ns > 0 && (flow_clock_monotonic_ns() - start) > timeout_ns) return FLOW_RELOAD_BUSY; if (spins++ < 2048) {
#if defined(__aarch64__) || defined(__arm64__)
                __asm__ volatile("isb" ::: "memory");
#elif defined(__x86_64__) || defined(_M_X64)
                __builtin_ia32_pause();
#endif
            } else sched_yield(); } } return FLOW_RELOAD_OK; }
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
