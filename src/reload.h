#ifndef FLOW_RELOAD_H
#define FLOW_RELOAD_H
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#ifndef FLOW_SMT_RESULT_DEFINED
#define FLOW_SMT_RESULT_DEFINED
typedef enum { FLOW_SMT_PROVEN_UNSAT = 0, FLOW_SMT_VIOLATION_SAT = 1, FLOW_SMT_UNKNOWN = 2 } FlowSMTResult;
#endif
typedef struct FlowSMTProofAttestation FlowSMTProofAttestation;
#define FLOW_RELOAD_ABI_VERSION UINT32_C(3)
typedef struct FlowReloadContext FlowReloadContext; struct FlowPlanArtifact; struct SemanticIR; struct FlowWavefrontRing; struct FlowBumpQsbrArena;
typedef enum { FLOW_MUTATION_UPSERT = 1, FLOW_MUTATION_DELETE = 2 } FlowMutationKind;
typedef struct { FlowMutationKind kind; uint64_t sequence; const void *key; size_t key_size; const void *value; size_t value_size; } FlowMutation; enum {
    FLOW_SCHEMA_FIELD_DEFAULTABLE = UINT32_C(1) << 0, FLOW_SCHEMA_FIELD_PERSISTENT  = UINT32_C(1) << 1, FLOW_SCHEMA_FIELD_ORDERED     = UINT32_C(1) << 2 };
typedef struct { const char *name, *type; uint32_t flags; } FlowSchemaField;
typedef struct { const char *name; uint32_t version; const FlowSchemaField *fields; size_t field_count; } FlowSchema;
typedef enum { FLOW_LAYOUT_DEFAULT = 0, FLOW_LAYOUT_AOS = 1, FLOW_LAYOUT_SOA = 2, FLOW_LAYOUT_COLUMNAR = 3 } FlowLayoutKind; typedef struct {
    FlowLayoutKind source_layout, target_layout; size_t state_bytes, column_count, modified_column_count;
    double clone_cost_ns, transform_cost_ns, amortized_payback_ns_per_call, break_even_calls; } FlowMigrationCostModel;
typedef enum { FLOW_MIGRATE_AUTO = 0, FLOW_MIGRATE_SNAPSHOT_COW = 1, FLOW_MIGRATE_STOP_THE_WORLD = 2 } FlowMigrationMode; typedef struct FlowUnit { uint32_t abi_version;
    uint64_t semantic_schema_hash, constraint_hash, capability_hash; const char *name; FlowLayoutKind layout; int supports_snapshot_cow;
    int (*init)(void *host_context, void **state_out); int (*run)(void *host_context, void *state, const void *input, void *output);
    int (*apply)(void *host_context, void *state, const FlowMutation *mutation); int (*migrate)(void *host_context, const void *old_state, void *new_state);
    void (*drop)(void *host_context, void *state); const FlowSchema *schema; } FlowUnit;
#ifndef FLOW_CACHE_LINE_SIZE
#define FLOW_CACHE_LINE_SIZE 64
#endif
#if defined(__GNUC__) || defined(__clang__)
#define FLOW_CACHE_ALIGNED __attribute__((aligned(FLOW_CACHE_LINE_SIZE)))
#else
#define FLOW_CACHE_ALIGNED
#endif
typedef struct FlowReloadReader FlowReloadReader; struct FlowReloadReader { FlowReloadContext *context; struct FlowReloadReader *next;
    _Atomic uint64_t active_epoch, last_heartbeat_ns, qsbr_epoch; _Atomic int registered, is_offline, is_quarantined; void *quarantine_page_addr; size_t quarantine_page_size;
    uint8_t _cache_pad[8]; } FLOW_CACHE_ALIGNED; typedef struct { FlowReloadContext *context; FlowReloadReader *reader; const FlowUnit *unit; void *state; uint64_t generation;
    int active; } FlowInvocation; typedef enum { FLOW_RELOAD_OK = 0, FLOW_RELOAD_INVALID = 1, FLOW_RELOAD_INCOMPATIBLE = 2,
    FLOW_RELOAD_BUSY = 3, FLOW_RELOAD_NO_CURRENT = 4, FLOW_RELOAD_JOURNAL_FULL = 5
} FlowReloadStatus; FlowReloadContext *flow_reload_create(void *host_context); int flow_reload_destroy(FlowReloadContext *context);
int flow_reload_reader_register(FlowReloadContext *context, FlowReloadReader *reader); int flow_reload_reader_unregister(FlowReloadReader *reader);
int flow_reload_publish(FlowReloadContext *context, const FlowUnit *unit, void *state); int flow_reload_activate(FlowReloadContext *context, const FlowUnit *unit);
const FlowUnit *flow_reload_current_unit(const FlowReloadContext *context); int flow_reload_migrate(FlowReloadContext *context, const FlowUnit *candidate);
int flow_reload_migrate_mode(FlowReloadContext *context, const FlowUnit *candidate, FlowMigrationMode mode);
int flow_reload_live_begin(FlowReloadContext *context, const FlowUnit *candidate, size_t journal_capacity); int flow_reload_live_finish(FlowReloadContext *context);
int flow_reload_live_finish_or_fallback(FlowReloadContext *context); int flow_reload_live_abort(FlowReloadContext *context);
int flow_reload_morph_zerocopy_remap(FlowReloadContext *context, const FlowUnit *target_unit, void **state_inout, size_t state_size);
int flow_reload_plan(FlowReloadContext *context, const struct FlowPlanArtifact *artifact, const struct SemanticIR *ir, FlowMigrationMode mode);
int flow_reload_begin(FlowReloadContext *context, FlowReloadReader *reader, FlowInvocation *invocation); void flow_reload_end(FlowInvocation *invocation);
int flow_reload_call(FlowReloadContext *context, FlowReloadReader *reader, const void *input, void *output);
int flow_reload_apply(FlowReloadContext *context, FlowReloadReader *reader, const FlowMutation *mutation); uint64_t flow_reload_generation(const FlowReloadContext *context);
int flow_reload_is_active(const FlowReloadContext *context); void *flow_reload_current_state(const FlowReloadContext *context);
const char *flow_reload_status_name(FlowReloadStatus status); size_t flow_reload_reclaim(FlowReloadContext *context); uint64_t flow_schema_hash(const FlowSchema *schema);
int flow_schema_migration_compatible(const FlowSchema *old_schema, const FlowSchema *new_schema);
int flow_reload_compatible(const FlowUnit *current, const FlowUnit *candidate); void flow_qsbr_checkpoint(FlowReloadReader *reader);
void flow_qsbr_offline(FlowReloadReader *reader); void flow_qsbr_online(FlowReloadReader *reader);
int flow_qsbr_call(FlowReloadContext *context, const void *input, void *output); int flow_qsbr_synchronize(FlowReloadContext *context, uint64_t timeout_ns);
size_t flow_qsbr_reclaim(FlowReloadContext *context); void flow_reload_set_sla_latency(FlowReloadContext *context, uint64_t sla_latency_ns);
uint64_t flow_qsbr_compute_adaptive_timeout(const FlowReloadContext *context);
int flow_qsbr_watchdog_sweep(FlowReloadContext *context, uint64_t current_time_ns, size_t *quarantined_count_out);
int flow_qsbr_quarantine_reader(FlowReloadReader *reader, void *page_addr, size_t page_size); int flow_qsbr_unquarantine_reader(FlowReloadReader *reader);
int flow_qsbr_is_reader_quarantined(const FlowReloadReader *reader); int flow_reload_bind_wavefront(FlowReloadContext *context, const struct FlowWavefrontRing *ring);
int flow_reload_bind_arena(FlowReloadContext *context, struct FlowBumpQsbrArena *arena);
FlowSMTResult flow_reload_verify_topological_safety_smt(const FlowReloadContext *context, FlowSMTProofAttestation *proof_out); typedef struct { FlowReloadReader reader;
    FlowReloadContext *context; uint64_t checkpoint_count; int is_registered; } FLOW_CACHE_ALIGNED FlowPluginRuntimeScope;
static inline int flow_plugin_scope_enter(FlowPluginRuntimeScope *s, FlowReloadContext *ctx) { if (!s || !ctx) return 0; s->context = ctx; s->checkpoint_count = 0;
    return (s->is_registered = (flow_reload_reader_register(ctx, &s->reader) == FLOW_RELOAD_OK)); }
static inline void flow_plugin_scope_checkpoint(FlowPluginRuntimeScope *s) { if (s && s->is_registered) { flow_qsbr_checkpoint(&s->reader); s->checkpoint_count++; } }
static inline void flow_plugin_scope_pause(FlowPluginRuntimeScope *s) { if (s && s->is_registered) flow_qsbr_offline(&s->reader); }
static inline void flow_plugin_scope_resume(FlowPluginRuntimeScope *s) { if (s && s->is_registered) flow_qsbr_online(&s->reader); }
static inline void flow_plugin_scope_exit(FlowPluginRuntimeScope *s) { if (s && s->is_registered) { flow_reload_reader_unregister(&s->reader); s->is_registered = 0; } }
#define FLOW_WITH_QSBR_SCOPE(ctx, scope_ptr, ...) \
    do { FlowPluginRuntimeScope _s; if (flow_plugin_scope_enter(&_s, (ctx))) { FlowPluginRuntimeScope *(scope_ptr) = &_s; do { __VA_ARGS__; } while (0); flow_plugin_scope_exit(&_s); } } while (0)
#define FLOW_AUDIT_TRAIL_CAPACITY 256
typedef struct { uint64_t snapshot_id, timestamp_ns, generation_id, env_mask, random_seed, schema_hash, llvm_ir_hash; uint64_t genome_words[16]; uint32_t genome_bits;
    char component_id[64], flow_name[64], author_attestation[64]; int is_golden_fallback; } FlowMutationSnapshot;
typedef struct { FlowMutationSnapshot entries[FLOW_AUDIT_TRAIL_CAPACITY]; size_t head, total_recorded; } FlowAuditTrail;
int flow_audit_trail_record(FlowReloadContext *context, const FlowMutationSnapshot *snapshot); size_t flow_audit_trail_count(const FlowReloadContext *context);
int flow_audit_trail_get(const FlowReloadContext *context, size_t index, FlowMutationSnapshot *out); int flow_audit_trail_export(const FlowReloadContext *context, FILE *out);
int flow_audit_replay(const FlowMutationSnapshot *snapshot, uint64_t *reproduced_ir_hash_out);
#endif
