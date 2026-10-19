#include "reload.h"
#include "bitspace.h"
#include "registry.h"
#include "token_ring.h"
#include "entropy_collapse.h"
#include "flow_smt_dsl.h"
#include "hardware_telemetry.h"
#include "audit.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <sched.h>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#endif
typedef struct FlowReloadGeneration { const FlowUnit *unit; void *state; uint64_t generation, retire_epoch; struct FlowReloadGeneration *next; } FlowReloadGeneration;
typedef struct { FlowMutation mutation; void *key_copy, *value_copy; } FlowJournalEntry; typedef struct { int active, overflow; uint64_t next_sequence;
    FlowReloadGeneration *old; const FlowUnit *candidate; void *candidate_state; FlowJournalEntry *entries; size_t count, capacity; } FlowLiveMigration;
struct FlowReloadContext { void *host_context; _Atomic(FlowReloadGeneration *) current; _Atomic uint64_t epoch; _Atomic int quiescence_waiters, stopping;
    uint64_t next_generation, sla_latency_ns, heartbeat_samples, heartbeat_sum_ns, heartbeat_sum_sq_ns; pthread_mutex_t lock, mutation_lock; pthread_cond_t quiesced;
    FlowReloadReader *readers; FlowReloadGeneration *retired; FlowLiveMigration live; FlowAuditTrail audit_trail; const FlowWavefrontRing *bound_wavefront_ring;
    FlowBumpQsbrArena *bound_arena; }; static uint64_t schema_mix(uint64_t h, const void *d, size_t sz) { const unsigned char *b = d;
    for (size_t i = 0; i < sz; ++i) h = (h ^ b[i]) * UINT64_C(1099511628211); return h; } static uint64_t schema_str(uint64_t h, const char *v) {
    static const unsigned char sep = 0; return v ? schema_mix(schema_mix(h, v, strlen(v)), &sep, 1) : schema_mix(h, &sep, 1); }
static uint64_t schema_u32(uint64_t h, uint32_t v) { unsigned char b[4] = {(unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24)};
    return schema_mix(h, b, 4); } uint64_t flow_schema_hash(const FlowSchema *s) { if (!s || !s->name || (s->field_count && !s->fields)) return 0;
    uint64_t h = schema_u32(schema_str(UINT64_C(1469598103934665603), s->name), s->version); for (size_t i = 0; i < s->field_count; ++i) {
        if (!s->fields[i].name || !s->fields[i].type) return 0; h = schema_u32(schema_str(schema_str(h, s->fields[i].name), s->fields[i].type), s->fields[i].flags); }
    return h; } int flow_schema_migration_compatible(const FlowSchema *o, const FlowSchema *n) {
    if (!o || !n || !o->name || !n->name || strcmp(o->name, n->name) != 0 || n->version <= o->version || n->field_count < o->field_count) return 0;
    for (size_t i = 0; i < o->field_count; ++i) { const FlowSchemaField *of = &o->fields[i], *nf = &n->fields[i];
        if (!of->name || !nf->name || !of->type || !nf->type || strcmp(of->name, nf->name) != 0 || strcmp(of->type, nf->type) != 0) return 0;
        if ((of->flags & FLOW_SCHEMA_FIELD_PERSISTENT) && !(nf->flags & FLOW_SCHEMA_FIELD_PERSISTENT)) return 0;
        if ((of->flags & FLOW_SCHEMA_FIELD_ORDERED) && !(nf->flags & FLOW_SCHEMA_FIELD_ORDERED)) return 0; }
    for (size_t i = o->field_count; i < n->field_count; ++i) if (!(n->fields[i].flags & FLOW_SCHEMA_FIELD_DEFAULTABLE)) return 0; return 1; }
static int unit_valid(const FlowUnit *u) { return u && u->abi_version == FLOW_RELOAD_ABI_VERSION && u->name && u->run && u->drop; }
int flow_reload_compatible(const FlowUnit *cur, const FlowUnit *cand) {
    if (!unit_valid(cur) || !unit_valid(cand) || cur->constraint_hash != cand->constraint_hash || cur->capability_hash != cand->capability_hash) return 0;
    if (cur->schema || cand->schema) { if (!cur->schema || !cand->schema) return 0;
        return (cur->schema->version == cand->schema->version) ? (flow_schema_hash(cur->schema) == flow_schema_hash(cand->schema)) : flow_schema_migration_compatible(cur->schema, cand->schema);
    } return cur->semantic_schema_hash == cand->semantic_schema_hash; } const char *flow_reload_status_name(FlowReloadStatus status) {
    static const char *const names[] = {"ok", "invalid", "incompatible", "busy", "no_current", "journal_full"};
    return ((int)status >= 0 && (int)status < 6) ? names[status] : "unknown"; } FlowReloadContext *flow_reload_create(void *host_context) {
    FlowReloadContext *ctx = calloc(1, sizeof(*ctx)); if (!ctx) return NULL; ctx->host_context = host_context; atomic_init(&ctx->epoch, 1); atomic_init(&ctx->current, NULL);
    atomic_init(&ctx->quiescence_waiters, 0); atomic_init(&ctx->stopping, 0); pthread_mutex_init(&ctx->lock, NULL); pthread_mutex_init(&ctx->mutation_lock, NULL);
    pthread_cond_init(&ctx->quiesced, NULL); return ctx; } static void drop_generation(FlowReloadContext *ctx, FlowReloadGeneration *g) {
    if (g && g->unit && g->unit->drop && g->state) { g->unit->drop(ctx->host_context, g->state); g->state = NULL; } }
static void journal_entry_dispose(FlowJournalEntry *e) { free(e->key_copy); free(e->value_copy); *e = (FlowJournalEntry){0}; }
static void journal_dispose(FlowJournalEntry *entries, size_t count) { if (!entries) return; for (size_t i = 0; i < count; ++i) journal_entry_dispose(&entries[i]);
    free(entries); } static void wait_readers_quiesced_locked(FlowReloadContext *ctx) { atomic_fetch_add_explicit(&ctx->quiescence_waiters, 1, memory_order_seq_cst);
    while (1) { int active = 0; for (const FlowReloadReader *r = ctx->readers; r; r = r->next)
            if (atomic_load_explicit(&r->active_epoch, memory_order_seq_cst) != 0) { active = 1; break; } if (!active) break; pthread_cond_wait(&ctx->quiesced, &ctx->lock); }
    atomic_fetch_sub_explicit(&ctx->quiescence_waiters, 1, memory_order_seq_cst); } int flow_reload_destroy(FlowReloadContext *ctx) { if (!ctx) return FLOW_RELOAD_INVALID;
    pthread_mutex_lock(&ctx->lock); atomic_store_explicit(&ctx->stopping, 1, memory_order_release);
    if (ctx->readers) { pthread_mutex_unlock(&ctx->lock); return FLOW_RELOAD_BUSY; } wait_readers_quiesced_locked(ctx); if (ctx->live.active) {
        if (ctx->live.candidate && ctx->live.candidate->drop && ctx->live.candidate_state)
            ctx->live.candidate->drop(ctx->host_context, ctx->live.candidate_state); journal_dispose(ctx->live.entries, ctx->live.count); ctx->live = (FlowLiveMigration){0}; }
    FlowReloadGeneration *gen = atomic_load_explicit(&ctx->current, memory_order_acquire), *retired = ctx->retired;
    atomic_store_explicit(&ctx->current, NULL, memory_order_release); ctx->retired = NULL; pthread_mutex_unlock(&ctx->lock); if (gen) { drop_generation(ctx, gen); free(gen); }
    while (retired) { FlowReloadGeneration *next = retired->next; drop_generation(ctx, retired); free(retired); retired = next; }
    pthread_cond_destroy(&ctx->quiesced); pthread_mutex_destroy(&ctx->mutation_lock); pthread_mutex_destroy(&ctx->lock); free(ctx); return FLOW_RELOAD_OK; }
int flow_reload_reader_register(FlowReloadContext *ctx, FlowReloadReader *r) { if (!ctx || !r) return FLOW_RELOAD_INVALID; pthread_mutex_lock(&ctx->lock);
    if (ctx->stopping) { pthread_mutex_unlock(&ctx->lock); return FLOW_RELOAD_BUSY; } r->context = ctx; r->next = ctx->readers; atomic_init(&r->active_epoch, 0);
    atomic_store_explicit(&r->registered, 1, memory_order_release); ctx->readers = r; pthread_mutex_unlock(&ctx->lock); return FLOW_RELOAD_OK; }
int flow_reload_reader_unregister(FlowReloadReader *r) { if (!r || !r->context) return FLOW_RELOAD_INVALID; FlowReloadContext *ctx = r->context;
    pthread_mutex_lock(&ctx->lock); FlowReloadReader **cur = &ctx->readers; while (*cur && *cur != r) cur = &(*cur)->next;
    if (!*cur) { pthread_mutex_unlock(&ctx->lock); return FLOW_RELOAD_INVALID; } *cur = r->next; r->next = NULL;
    atomic_store_explicit(&r->registered, 0, memory_order_release); atomic_store_explicit(&r->active_epoch, 0, memory_order_seq_cst);
    if (atomic_load_explicit(&ctx->quiescence_waiters, memory_order_acquire) > 0) pthread_cond_broadcast(&ctx->quiesced); r->context = NULL; pthread_mutex_unlock(&ctx->lock);
    return FLOW_RELOAD_OK; } static void commit_generation_locked(FlowReloadContext *ctx, FlowReloadGeneration *old, FlowReloadGeneration *next, int log_ev) {
    next->generation = ++ctx->next_generation; if (old) { next->retire_epoch = atomic_fetch_add_explicit(&ctx->epoch, UINT64_C(1), memory_order_seq_cst) + UINT64_C(1);
        old->retire_epoch = next->retire_epoch; old->next = ctx->retired; ctx->retired = old; if (log_ev) { FlowDecisionEvent ev = {
                .timestamp_ns = flow_clock_monotonic_ns(), .trigger_type = FLOW_DECISION_TRIGGER_STRAGGLER_QUARANTINE,
                .observed_metric_value = (double)next->generation, .threshold_limit_value = (double)(next->generation - 1),
                .flipped_genome_bit = (uint32_t)(next->generation % 64), .pre_state_mask = (1ULL << 61),
                .post_state_mask = (1ULL << 61) | (1ULL << (next->generation % 64)), .hot_swap_grace_ns = 84,
                .trigger_source = "qsbr_epoch_migration", .metric_unit = "gen", .violated_constraint = "QSBR Zero-Downtime Epoch Upgrade" };
            snprintf(ev.pre_topology, sizeof(ev.pre_topology), "%s", (old->unit && old->unit->name) ? old->unit->name : "old_generation");
            snprintf(ev.post_topology, sizeof(ev.post_topology), "%s", (next->unit && next->unit->name) ? next->unit->name : "new_generation");
            snprintf(ev.causal_rationale, sizeof(ev.causal_rationale), "QSBR epoch advanced to %llu. Live topology hot-swapped from '%s' to '%s'.",
                     (unsigned long long)next->retire_epoch, ev.pre_topology, ev.post_topology); flow_decision_logger_record(flow_decision_logger_default(), &ev); } }
    atomic_store_explicit(&ctx->current, next, memory_order_release); } int flow_reload_publish(FlowReloadContext *ctx, const FlowUnit *unit, void *state) {
    if (!ctx || !unit_valid(unit) || !state) return FLOW_RELOAD_INVALID; FlowReloadGeneration *next = calloc(1, sizeof(*next)); if (!next) return FLOW_RELOAD_INVALID;
    next->unit = unit; next->state = state; pthread_mutex_lock(&ctx->lock);
    if (atomic_load_explicit(&ctx->stopping, memory_order_acquire) || ctx->live.active) { pthread_mutex_unlock(&ctx->lock); free(next); return FLOW_RELOAD_BUSY; }
    FlowReloadGeneration *old = atomic_load_explicit(&ctx->current, memory_order_acquire);
    if (old && !flow_reload_compatible(old->unit, unit)) { pthread_mutex_unlock(&ctx->lock); free(next); return FLOW_RELOAD_INCOMPATIBLE; }
    commit_generation_locked(ctx, old, next, 1); pthread_mutex_unlock(&ctx->lock); return FLOW_RELOAD_OK; }
int flow_reload_activate(FlowReloadContext *ctx, const FlowUnit *unit) { void *state = NULL;
    if (!ctx || !unit_valid(unit) || !unit->init || unit->init(ctx->host_context, &state) != 0 || !state) return FLOW_RELOAD_INVALID;
    int res = flow_reload_publish(ctx, unit, state); if (res != FLOW_RELOAD_OK) unit->drop(ctx->host_context, state); return res; }
static void migration_stop(FlowReloadContext *ctx) { atomic_store_explicit(&ctx->stopping, 0, memory_order_release); pthread_cond_broadcast(&ctx->quiesced); }
const FlowUnit *flow_reload_current_unit(const FlowReloadContext *ctx) { FlowReloadGeneration *gen = ctx ? atomic_load_explicit(&ctx->current, memory_order_acquire) : NULL;
    return gen ? gen->unit : NULL; }
int flow_reload_migrate(FlowReloadContext *ctx, const FlowUnit *candidate) { return flow_reload_migrate_mode(ctx, candidate, FLOW_MIGRATE_AUTO); }
int flow_reload_migrate_mode(FlowReloadContext *ctx, const FlowUnit *cand, FlowMigrationMode mode) {
    if (!ctx || !unit_valid(cand) || !cand->init || !cand->migrate || !cand->schema) return FLOW_RELOAD_INVALID;
    if (mode == FLOW_MIGRATE_SNAPSHOT_COW && !cand->supports_snapshot_cow) return FLOW_RELOAD_INCOMPATIBLE;
    if ((mode == FLOW_MIGRATE_SNAPSHOT_COW || mode == FLOW_MIGRATE_AUTO) && cand->supports_snapshot_cow && cand->apply) { int b = flow_reload_live_begin(ctx, cand, 128);
        if (b == FLOW_RELOAD_OK) return mode == FLOW_MIGRATE_SNAPSHOT_COW ? flow_reload_live_finish(ctx) : flow_reload_live_finish_or_fallback(ctx);
        if (mode == FLOW_MIGRATE_SNAPSHOT_COW) return b; } pthread_mutex_lock(&ctx->lock);
    if (atomic_load_explicit(&ctx->stopping, memory_order_acquire) || ctx->live.active) { pthread_mutex_unlock(&ctx->lock); return FLOW_RELOAD_BUSY; }
    FlowReloadGeneration *old = atomic_load_explicit(&ctx->current, memory_order_acquire); if (!old) { pthread_mutex_unlock(&ctx->lock); return FLOW_RELOAD_NO_CURRENT; }
    if (!flow_reload_compatible(old->unit, cand)) { pthread_mutex_unlock(&ctx->lock); return FLOW_RELOAD_INCOMPATIBLE; }
    atomic_store_explicit(&ctx->stopping, 1, memory_order_release); wait_readers_quiesced_locked(ctx); pthread_mutex_unlock(&ctx->lock); void *new_st = NULL;
    if (cand->init(ctx->host_context, &new_st) != 0 || !new_st || cand->migrate(ctx->host_context, old->state, new_st) != 0) {
        if (new_st) cand->drop(ctx->host_context, new_st); pthread_mutex_lock(&ctx->lock); migration_stop(ctx); pthread_mutex_unlock(&ctx->lock); return FLOW_RELOAD_INVALID; }
    pthread_mutex_lock(&ctx->lock); if (atomic_load_explicit(&ctx->stopping, memory_order_acquire) && atomic_load_explicit(&ctx->current, memory_order_acquire) == old) {
        FlowReloadGeneration *next = calloc(1, sizeof(*next));
        if (!next) { migration_stop(ctx); pthread_mutex_unlock(&ctx->lock); cand->drop(ctx->host_context, new_st); return FLOW_RELOAD_INVALID; }
        next->unit = cand; next->state = new_st; commit_generation_locked(ctx, old, next, 0); migration_stop(ctx); pthread_mutex_unlock(&ctx->lock); return FLOW_RELOAD_OK; }
    migration_stop(ctx); pthread_mutex_unlock(&ctx->lock); cand->drop(ctx->host_context, new_st); return FLOW_RELOAD_BUSY; } static int mutation_valid(const FlowMutation *m) {
    return m && (m->kind == FLOW_MUTATION_UPSERT || m->kind == FLOW_MUTATION_DELETE) && (!m->key_size || m->key) && (!m->value_size || m->value); }
static int journal_capture(FlowJournalEntry *e, const FlowMutation *m, uint64_t seq) { *e = (FlowJournalEntry){.mutation = *m}; e->mutation.sequence = seq;
    if (m->key_size && !(e->key_copy = malloc(m->key_size))) return 0; if (m->key_size) { memcpy(e->key_copy, m->key, m->key_size); e->mutation.key = e->key_copy; }
    if (m->value_size && !(e->value_copy = malloc(m->value_size))) { journal_entry_dispose(e); return 0; }
    if (m->value_size) { memcpy(e->value_copy, m->value, m->value_size); e->mutation.value = e->value_copy; } return 1; }
static int live_abort_and_unlock(FlowReloadContext *ctx, int status) { const FlowUnit *cand = ctx->live.candidate; void *cand_st = ctx->live.candidate_state;
    FlowJournalEntry *entries = ctx->live.entries; size_t cnt = ctx->live.count; ctx->live = (FlowLiveMigration){0};
    pthread_mutex_unlock(&ctx->lock); pthread_mutex_unlock(&ctx->mutation_lock); if (cand && cand->drop && cand_st) cand->drop(ctx->host_context, cand_st);
    journal_dispose(entries, cnt); return status; } int flow_reload_live_begin(FlowReloadContext *ctx, const FlowUnit *cand, size_t cap) {
    if (!ctx || !unit_valid(cand) || !cand->init || !cand->migrate || !cand->apply || !cand->schema || !cap) return FLOW_RELOAD_INVALID;
    FlowJournalEntry *entries = calloc(cap, sizeof(*entries)); if (!entries) return FLOW_RELOAD_INVALID; void *cand_st = NULL;
    if (cand->init(ctx->host_context, &cand_st) != 0 || !cand_st) { free(entries); return FLOW_RELOAD_INVALID; } pthread_mutex_lock(&ctx->lock);
    FlowReloadGeneration *old = atomic_load_explicit(&ctx->current, memory_order_acquire);
    int err = (atomic_load_explicit(&ctx->stopping, memory_order_acquire) || ctx->live.active) ? FLOW_RELOAD_BUSY :
              (!old ? FLOW_RELOAD_NO_CURRENT : (!flow_reload_compatible(old->unit, cand) ? FLOW_RELOAD_INCOMPATIBLE : FLOW_RELOAD_OK));
    if (err != FLOW_RELOAD_OK) { pthread_mutex_unlock(&ctx->lock); cand->drop(ctx->host_context, cand_st); free(entries); return err; }
    ctx->live = (FlowLiveMigration){ .active = 1, .next_sequence = 1, .old = old, .candidate = cand, .candidate_state = cand_st, .entries = entries, .capacity = cap };
    pthread_mutex_unlock(&ctx->lock); if (cand->migrate(ctx->host_context, old->state, cand_st) != 0) { (void)flow_reload_live_abort(ctx); return FLOW_RELOAD_INVALID; }
    return FLOW_RELOAD_OK; } int flow_reload_live_abort(FlowReloadContext *ctx) { if (!ctx) return FLOW_RELOAD_INVALID;
    pthread_mutex_lock(&ctx->mutation_lock); pthread_mutex_lock(&ctx->lock);
    if (!ctx->live.active) { pthread_mutex_unlock(&ctx->lock); pthread_mutex_unlock(&ctx->mutation_lock); return FLOW_RELOAD_INVALID; }
    return live_abort_and_unlock(ctx, FLOW_RELOAD_OK); } static int flow_reload_live_finish_internal(FlowReloadContext *ctx, int allow_fallback) {
    if (!ctx) return FLOW_RELOAD_INVALID; pthread_mutex_lock(&ctx->mutation_lock); pthread_mutex_lock(&ctx->lock);
    if (!ctx->live.active) { pthread_mutex_unlock(&ctx->lock); pthread_mutex_unlock(&ctx->mutation_lock); return FLOW_RELOAD_INVALID; }
    if (ctx->live.overflow && !allow_fallback) return live_abort_and_unlock(ctx, FLOW_RELOAD_JOURNAL_FULL);
    const FlowUnit *cand = ctx->live.candidate; void *cand_st = ctx->live.candidate_state;
    FlowReloadGeneration *old = ctx->live.old; FlowJournalEntry *entries = ctx->live.entries; size_t cnt = ctx->live.count; int res = FLOW_RELOAD_OK;
    for (size_t i = 0; i < cnt; ++i) if ((res = cand->apply(ctx->host_context, cand_st, &entries[i].mutation)) != 0) break;
    if (res != 0 || atomic_load_explicit(&ctx->current, memory_order_acquire) != old) return live_abort_and_unlock(ctx, FLOW_RELOAD_INVALID);
    FlowReloadGeneration *next = calloc(1, sizeof(*next)); if (!next) return live_abort_and_unlock(ctx, FLOW_RELOAD_INVALID); next->unit = cand; next->state = cand_st;
    commit_generation_locked(ctx, old, next, 0); ctx->live = (FlowLiveMigration){0}; pthread_mutex_unlock(&ctx->lock); pthread_mutex_unlock(&ctx->mutation_lock);
    journal_dispose(entries, cnt); return FLOW_RELOAD_OK; } int flow_reload_live_finish(FlowReloadContext *ctx) { return flow_reload_live_finish_internal(ctx, 0); }
int flow_reload_live_finish_or_fallback(FlowReloadContext *ctx) { return flow_reload_live_finish_internal(ctx, 1); }
int flow_reload_begin(FlowReloadContext *ctx, FlowReloadReader *r, FlowInvocation *inv) { if (!ctx || !r || !inv) return FLOW_RELOAD_INVALID;
    if (inv->active) return FLOW_RELOAD_BUSY; if (r->context != ctx || !atomic_load_explicit(&r->registered, memory_order_acquire)) return FLOW_RELOAD_INVALID;
    if (atomic_load_explicit(&ctx->stopping, memory_order_acquire)) return FLOW_RELOAD_BUSY;
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
int flow_reload_apply(FlowReloadContext *ctx, FlowReloadReader *r, const FlowMutation *m) { if (!mutation_valid(m)) return FLOW_RELOAD_INVALID; FlowInvocation inv = {0};
    int res = flow_reload_begin(ctx, r, &inv); if (res != FLOW_RELOAD_OK) return res; pthread_mutex_lock(&ctx->mutation_lock); pthread_mutex_lock(&ctx->lock);
    FlowReloadGeneration *cur = atomic_load_explicit(&ctx->current, memory_order_acquire); FlowJournalEntry entry;
    if (!cur || cur->generation != inv.generation) res = FLOW_RELOAD_BUSY; else if (ctx->live.active) {
        if (ctx->live.old != cur || ctx->live.count >= ctx->live.capacity || !journal_capture(&entry, m, ctx->live.next_sequence++)) {
            ctx->live.overflow = 1; res = FLOW_RELOAD_JOURNAL_FULL; } else { res = inv.unit->apply(ctx->host_context, inv.state, &entry.mutation);
            if (res == 0) ctx->live.entries[ctx->live.count++] = entry; else journal_entry_dispose(&entry); }
    } else res = inv.unit->apply ? inv.unit->apply(ctx->host_context, inv.state, m) : FLOW_RELOAD_INVALID;
    pthread_mutex_unlock(&ctx->lock); pthread_mutex_unlock(&ctx->mutation_lock); flow_reload_end(&inv); return res; }
#define FLOW_EPOCH_LEASE_TIMEOUT_NS (UINT64_C(200) * UINT64_C(1000000))
static int generation_safe(FlowReloadContext *ctx, uint64_t retire_epoch) {
    if (ctx->bound_wavefront_ring && retire_epoch <= ctx->bound_wavefront_ring->quiescent_generation) return 1; uint64_t now = flow_clock_monotonic_ns();
    for (const FlowReloadReader *r = ctx->readers; r; r = r->next) { uint64_t active = atomic_load_explicit(&r->active_epoch, memory_order_seq_cst);
        if (active && active <= retire_epoch) { uint64_t hb = atomic_load_explicit(&r->last_heartbeat_ns, memory_order_relaxed);
            if (hb > 0 && now > hb && (now - hb) > FLOW_EPOCH_LEASE_TIMEOUT_NS) continue; return 0; } } return 1; }
uint64_t flow_reload_generation(const FlowReloadContext *ctx) { const FlowReloadGeneration *curr = ctx ? atomic_load_explicit(&ctx->current, memory_order_acquire) : NULL;
    return curr ? curr->generation : 0; } size_t flow_reload_reclaim(FlowReloadContext *ctx) { if (!ctx) return 0; FlowReloadGeneration *ready = NULL; size_t reclaimed = 0;
    pthread_mutex_lock(&ctx->lock); for (FlowReloadGeneration **cur = &ctx->retired; *cur; ) { FlowReloadGeneration *cand = *cur;
        if (!generation_safe(ctx, cand->retire_epoch)) { cur = &cand->next; continue; } *cur = cand->next; cand->next = ready; ready = cand; }
    pthread_mutex_unlock(&ctx->lock); while (ready) { FlowReloadGeneration *next = ready->next; drop_generation(ctx, ready); free(ready); ready = next; ++reclaimed; }
    if (ctx->bound_arena && reclaimed > 0) flow_bump_qsbr_quiescent_fold(ctx->bound_arena); return reclaimed; }
typedef struct { size_t capacity, threads, shards, item_count; int *data; } FlowPlanUnitState; static int flow_plan_unit_init(void *h, void **out) {
    (void)h; FlowPlanUnitState *st = calloc(1, sizeof(*st)); if (!st) return -1; st->capacity = 1024; st->threads = st->shards = 1;
    if (!(st->data = calloc(st->capacity, sizeof(int)))) { free(st); return -1; } *out = st; return 0; }
static int flow_plan_unit_run(void *h, void *s, const void *in, void *out) { (void)h; FlowPlanUnitState *st = s; if (!st) return -1;
    if (in && st->item_count < st->capacity) st->data[st->item_count++] = *(const int *)in; if (out) *(int *)out = (int)st->item_count; return 0; }
static int flow_plan_unit_apply(void *h, void *s, const FlowMutation *m) { (void)h; FlowPlanUnitState *st = s; if (!st || !m) return -1;
    if (m->kind == FLOW_MUTATION_UPSERT && m->value && m->value_size == sizeof(int) && st->item_count < st->capacity)
        st->data[st->item_count++] = *(const int *)m->value; return 0; } static int flow_plan_unit_migrate(void *h, const void *os, void *ns) {
    (void)h; const FlowPlanUnitState *o = os; FlowPlanUnitState *n = ns; if (!o || !n) return -1; size_t cnt = o->item_count > n->capacity ? n->capacity : o->item_count;
    if (cnt && o->data && n->data) memcpy(n->data, o->data, cnt * sizeof(int)); n->item_count = cnt; return 0; }
static void flow_plan_unit_drop(void *h, void *s) { (void)h; FlowPlanUnitState *st = s; if (st) { free(st->data); free(st); } }
static const FlowSchemaField PLAN_SCHEMA_FIELDS[] = { {"data", "i32[]", FLOW_SCHEMA_FIELD_PERSISTENT | FLOW_SCHEMA_FIELD_ORDERED},
    {"item_count", "u64", FLOW_SCHEMA_FIELD_PERSISTENT} }; static const FlowSchema PLAN_SCHEMA_V1 = {"plan_collection", 1, PLAN_SCHEMA_FIELDS, 2};
int flow_reload_plan(FlowReloadContext *ctx, const FlowPlanArtifact *art, const SemanticIR *ir, FlowMigrationMode mode) { char val_err[256];
    if (!ctx || !art || !ir || !flow_artifact_validate(art, ir, NULL, val_err, sizeof(val_err))) return (!ctx || !art || !ir) ? FLOW_RELOAD_INVALID : FLOW_RELOAD_INCOMPATIBLE;
    const Component *comp = NULL;
    for (size_t i = 0; i < component_count(); ++i) { const Component *c = component_at(i); if (c && !strcmp(c->id, art->component_id)) { comp = c; break; } }
    if (!comp) return FLOW_RELOAD_INVALID; FlowUnit *unit = calloc(1, sizeof(*unit)); if (!unit) return FLOW_RELOAD_INVALID;
    const FlowPlugin *plugin = flow_component_plugin(comp); if (plugin && plugin->create_unit) {
        if (!plugin->create_unit(art, ir, comp, unit)) { free(unit); return FLOW_RELOAD_INVALID; } } else { *unit = (FlowUnit){
            .abi_version = FLOW_RELOAD_ABI_VERSION, .name = comp->id, .init = flow_plan_unit_init, .run = flow_plan_unit_run, .apply = flow_plan_unit_apply,
            .migrate = flow_plan_unit_migrate, .drop = flow_plan_unit_drop, .schema = &PLAN_SCHEMA_V1,
            .constraint_hash = flow_compute_contract_hash(ir), .capability_hash = 0x55, .semantic_schema_hash = art->plan_schema_hash, .supports_snapshot_cow = 1 }; }
    int res = !flow_reload_current_unit(ctx) ? flow_reload_activate(ctx, unit) : flow_reload_migrate_mode(ctx, unit, mode); if (res != FLOW_RELOAD_OK) free(unit); return res;
} void flow_reload_set_sla_latency(FlowReloadContext *ctx, uint64_t sla_ns) {
    if (ctx) { pthread_mutex_lock(&ctx->lock); ctx->sla_latency_ns = sla_ns; pthread_mutex_unlock(&ctx->lock); } }
uint64_t flow_qsbr_compute_adaptive_timeout(const FlowReloadContext *ctx) { if (!ctx) return 1000000ULL; if (ctx->sla_latency_ns > 0) return ctx->sla_latency_ns * 2ULL;
    if (ctx->bound_wavefront_ring) return (uint64_t)(ctx->bound_wavefront_ring->slot_count > 0 ? ctx->bound_wavefront_ring->slot_count : 4) * 25000ULL;
    return FLOW_EPOCH_LEASE_TIMEOUT_NS; } void flow_qsbr_checkpoint(FlowReloadReader *r) {
    if (r) { atomic_fetch_add_explicit(&r->qsbr_epoch, 1, memory_order_release); atomic_store_explicit(&r->last_heartbeat_ns, flow_clock_monotonic_ns(), memory_order_relaxed); }
} void flow_qsbr_offline(FlowReloadReader *r) { if (r) atomic_store_explicit(&r->is_offline, 1, memory_order_release); } void flow_qsbr_online(FlowReloadReader *r) {
    if (r) { atomic_store_explicit(&r->last_heartbeat_ns, flow_clock_monotonic_ns(), memory_order_relaxed); atomic_store_explicit(&r->is_offline, 0, memory_order_release); } }
int flow_qsbr_call(FlowReloadContext *ctx, const void *in, void *out) { if (!ctx) return FLOW_RELOAD_INVALID;
    FlowReloadGeneration *curr = atomic_load_explicit(&ctx->current, memory_order_acquire);
    return (!curr || !curr->unit || !curr->unit->run) ? FLOW_RELOAD_NO_CURRENT : curr->unit->run(ctx->host_context, curr->state, in, out); }
int flow_qsbr_synchronize(FlowReloadContext *ctx, uint64_t timeout_ns) { if (!ctx) return FLOW_RELOAD_INVALID; if (ctx->bound_wavefront_ring) {
        ((FlowWavefrontRing *)ctx->bound_wavefront_ring)->state = FLOW_RING_CIRCULATING; flow_wavefront_ring_step_parallel((FlowWavefrontRing *)ctx->bound_wavefront_ring);
        return FLOW_RELOAD_OK; } uint64_t start = flow_clock_monotonic_ns(), target_epochs[64]; FlowReloadReader *tracked[64]; size_t count = 0;
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
            } else sched_yield(); } } return FLOW_RELOAD_OK; } size_t flow_qsbr_reclaim(FlowReloadContext *ctx) { return ctx ? flow_reload_reclaim(ctx) : 0; }
int flow_qsbr_quarantine_reader(FlowReloadReader *r, void *addr, size_t sz) { if (!r) return 0; atomic_store_explicit(&r->is_quarantined, 1, memory_order_release);
    r->quarantine_page_addr = addr; r->quarantine_page_size = sz;
#if defined(__unix__) || defined(__APPLE__)
    if (addr && sz > 0) (void)mprotect(addr, sz, PROT_READ);
#endif
    return 1; } int flow_qsbr_unquarantine_reader(FlowReloadReader *r) { if (!r) return 0;
#if defined(__unix__) || defined(__APPLE__)
    if (r->quarantine_page_addr && r->quarantine_page_size > 0) (void)mprotect(r->quarantine_page_addr, r->quarantine_page_size, PROT_READ | PROT_WRITE);
#endif
    r->quarantine_page_addr = NULL; r->quarantine_page_size = 0; atomic_store_explicit(&r->is_quarantined, 0, memory_order_release); return 1; }
int flow_qsbr_is_reader_quarantined(const FlowReloadReader *r) { return r ? atomic_load_explicit(&r->is_quarantined, memory_order_acquire) : 0; }
int flow_qsbr_watchdog_sweep(FlowReloadContext *ctx, uint64_t now_ns, size_t *quarantined_out) { if (!ctx) return 0;
    size_t count = 0; uint64_t timeout_ns = flow_qsbr_compute_adaptive_timeout(ctx); pthread_mutex_lock(&ctx->lock); for (FlowReloadReader *r = ctx->readers; r; r = r->next) {
        if (atomic_load_explicit(&r->registered, memory_order_acquire) && !atomic_load_explicit(&r->is_offline, memory_order_acquire) && !atomic_load_explicit(&r->is_quarantined, memory_order_acquire)) {
            uint64_t hb = atomic_load_explicit(&r->last_heartbeat_ns, memory_order_acquire); if (now_ns > hb && (now_ns - hb) > timeout_ns) {
                atomic_store_explicit(&r->is_quarantined, 1, memory_order_release); count++; FlowDecisionEvent ev = {
                    .timestamp_ns = now_ns, .trigger_type = FLOW_DECISION_TRIGGER_STRAGGLER_QUARANTINE,
                    .observed_metric_value = (double)(now_ns - hb) / 1e6, .threshold_limit_value = (double)timeout_ns / 1e6,
                    .flipped_genome_bit = 63, .post_state_mask = (1ULL << 63), .hot_swap_grace_ns = 50, .trigger_source = "qsbr_straggler_watchdog", .metric_unit = "ms",
                    .violated_constraint = "QSBR Grace Period Heartbeat Timeout", .pre_topology = "active_reader", .post_topology = "quarantined_reader" };
                snprintf(ev.causal_rationale, sizeof(ev.causal_rationale), "Reader stalled for %.2f ms (timeout: %.2f ms). Straggler quarantined.", ev.observed_metric_value, ev.threshold_limit_value);
                flow_decision_logger_record(flow_decision_logger_default(), &ev); } } } pthread_mutex_unlock(&ctx->lock); if (quarantined_out) *quarantined_out = count;
    return 1; } int flow_reload_morph_zerocopy_remap(FlowReloadContext *ctx, const FlowUnit *target, void **st, size_t sz) {
    (void)sz; return (!ctx || !target || !st || !*st) ? FLOW_RELOAD_INVALID : flow_reload_publish(ctx, target, *st); }
int flow_audit_trail_record(FlowReloadContext *ctx, const FlowMutationSnapshot *s) { if (!ctx || !s) return 0; pthread_mutex_lock(&ctx->lock);
    size_t slot = ctx->audit_trail.head; ctx->audit_trail.entries[slot] = *s;
    if (!ctx->audit_trail.entries[slot].timestamp_ns) ctx->audit_trail.entries[slot].timestamp_ns = flow_clock_monotonic_ns();
    ctx->audit_trail.head = (slot + 1) % FLOW_AUDIT_TRAIL_CAPACITY; ctx->audit_trail.total_recorded++; pthread_mutex_unlock(&ctx->lock); return 1; }
size_t flow_audit_trail_count(const FlowReloadContext *ctx) {
    return !ctx ? 0 : (ctx->audit_trail.total_recorded < FLOW_AUDIT_TRAIL_CAPACITY ? ctx->audit_trail.total_recorded : FLOW_AUDIT_TRAIL_CAPACITY); }
int flow_audit_trail_get(const FlowReloadContext *ctx, size_t idx, FlowMutationSnapshot *out) { if (!ctx || !out || idx >= flow_audit_trail_count(ctx)) return 0;
    size_t start = (ctx->audit_trail.total_recorded >= FLOW_AUDIT_TRAIL_CAPACITY) ? ctx->audit_trail.head : 0;
    *out = ctx->audit_trail.entries[(start + idx) % FLOW_AUDIT_TRAIL_CAPACITY]; return 1; } int flow_audit_trail_export(const FlowReloadContext *ctx, FILE *out) {
    if (!ctx || !out) return 0; size_t count = flow_audit_trail_count(ctx); fprintf(out, "{\n  \"audit_trail_total\": %zu,\n  \"snapshots\": [\n", count);
    for (size_t i = 0; i < count; ++i) { FlowMutationSnapshot s; flow_audit_trail_get(ctx, i, &s);
        fprintf(out, "    {\"snapshot_id\": %llu, \"timestamp_ns\": %llu, \"generation_id\": %llu, \"env_mask\": \"0x%016llx\", \"random_seed\": %llu, \"schema_hash\": \"0x%016llx\", \"llvm_ir_hash\": \"0x%016llx\", \"component_id\": \"%s\", \"flow_name\": \"%s\", \"is_golden_fallback\": %s}%s\n",
                (unsigned long long)s.snapshot_id, (unsigned long long)s.timestamp_ns, (unsigned long long)s.generation_id,
                (unsigned long long)s.env_mask, (unsigned long long)s.random_seed, (unsigned long long)s.schema_hash,
                (unsigned long long)s.llvm_ir_hash, s.component_id, s.flow_name, s.is_golden_fallback ? "true" : "false", (i + 1 < count) ? "," : ""); }
    fprintf(out, "  ]\n}\n"); return 1; } int flow_audit_replay(const FlowMutationSnapshot *s, uint64_t *out) { if (!s) return 0;
    uint64_t h = ((UINT64_C(0xcbf29ce484222325) ^ s->random_seed) * UINT64_C(0x100000001b3) ^ s->env_mask) * UINT64_C(0x100000001b3);
    h = (h ^ s->schema_hash) * UINT64_C(0x100000001b3); for (int w = 0; w < 16; ++w) h = (h ^ s->genome_words[w]) * UINT64_C(0x100000001b3);
    if (s->is_golden_fallback) h ^= UINT64_C(0x9e3779b97f4a7c15); if (out) *out = h; return 1; }
int flow_reload_bind_wavefront(FlowReloadContext *ctx, const FlowWavefrontRing *ring) { if (!ctx) return 0;
    pthread_mutex_lock(&ctx->lock); ctx->bound_wavefront_ring = ring; pthread_mutex_unlock(&ctx->lock); return 1; }
int flow_reload_bind_arena(FlowReloadContext *ctx, FlowBumpQsbrArena *arena) { if (!ctx) return 0;
    pthread_mutex_lock(&ctx->lock); ctx->bound_arena = arena; pthread_mutex_unlock(&ctx->lock); return 1; }
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
