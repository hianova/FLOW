#ifndef FLOW_RELOAD_H
#define FLOW_RELOAD_H

#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>

#define FLOW_RELOAD_ABI_VERSION UINT32_C(3)

typedef struct FlowReloadContext FlowReloadContext;
struct FlowPlanArtifact;
struct SemanticIR;

typedef enum {
    FLOW_MUTATION_UPSERT = 1,
    FLOW_MUTATION_DELETE = 2
} FlowMutationKind;

typedef struct {
    FlowMutationKind kind;
    uint64_t sequence;
    const void *key;
    size_t key_size;
    const void *value;
    size_t value_size;
} FlowMutation;

enum {
    FLOW_SCHEMA_FIELD_DEFAULTABLE = UINT32_C(1) << 0,
    FLOW_SCHEMA_FIELD_PERSISTENT = UINT32_C(1) << 1,
    FLOW_SCHEMA_FIELD_ORDERED = UINT32_C(1) << 2
};

typedef struct {
    const char *name;
    const char *type;
    uint32_t flags;
} FlowSchemaField;

typedef struct {
    const char *name;
    uint32_t version;
    const FlowSchemaField *fields;
    size_t field_count;
} FlowSchema;

typedef enum {
    FLOW_LAYOUT_DEFAULT = 0,
    FLOW_LAYOUT_AOS = 1,          /* Array of Structs */
    FLOW_LAYOUT_SOA = 2,          /* Struct of Arrays */
    FLOW_LAYOUT_COLUMNAR = 3      /* Columnar Partitioned */
} FlowLayoutKind;

typedef struct {
    FlowLayoutKind source_layout;
    FlowLayoutKind target_layout;
    size_t state_bytes;
    size_t column_count;
    size_t modified_column_count;
    double clone_cost_ns;
    double transform_cost_ns;
    double amortized_payback_ns_per_call;
    double break_even_calls;
} FlowMigrationCostModel;

typedef enum {
    FLOW_MIGRATE_AUTO = 0,             /* Auto: snapshot if supported, else stop-the-world */
    FLOW_MIGRATE_SNAPSHOT_COW = 1,     /* Copy-on-write snapshot during concurrent writes */
    FLOW_MIGRATE_STOP_THE_WORLD = 2    /* Quiesced stop-the-world migration */
} FlowMigrationMode;

typedef struct FlowUnit {
    uint32_t abi_version;
    uint64_t semantic_schema_hash;
    uint64_t constraint_hash;
    uint64_t capability_hash;
    const char *name;
    FlowLayoutKind layout;
    int supports_snapshot_cow;
    int (*init)(void *host_context, void **state_out);
    int (*run)(void *host_context, void *state,
               const void *input, void *output);
    int (*apply)(void *host_context, void *state,
                 const FlowMutation *mutation);
    int (*migrate)(void *host_context, const void *old_state,
                   void *new_state);
    void (*drop)(void *host_context, void *state);
    const FlowSchema *schema;
} FlowUnit;

#ifndef FLOW_CACHE_LINE_SIZE
#define FLOW_CACHE_LINE_SIZE 64
#endif

#if defined(__GNUC__) || defined(__clang__)
#define FLOW_CACHE_ALIGNED __attribute__((aligned(FLOW_CACHE_LINE_SIZE)))
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#include <stdalign.h>
#define FLOW_CACHE_ALIGNED alignas(FLOW_CACHE_LINE_SIZE)
#else
#define FLOW_CACHE_ALIGNED
#endif

typedef struct FlowReloadReader FlowReloadReader;

struct FlowReloadReader {
    /* Keep this object address-stable until unregister succeeds. */
    FlowReloadContext *context;
    struct FlowReloadReader *next;
    _Atomic uint64_t active_epoch;
    _Atomic int registered;
    _Atomic uint64_t last_heartbeat_ns;
    _Atomic uint64_t qsbr_epoch;
    _Atomic int is_offline;
    _Atomic int is_quarantined;      /* 1 if thread is quarantined due to straggler timeout */
    void *quarantine_page_addr;      /* Read-barrier protected old generation memory page */
    size_t quarantine_page_size;
    uint8_t _cache_pad[8];           /* Explicit false-sharing buffer aligned to 64 bytes */
} FLOW_CACHE_ALIGNED;

typedef struct {
    FlowReloadContext *context;
    FlowReloadReader *reader;
    const FlowUnit *unit;
    void *state;
    uint64_t generation;
    int active;
} FlowInvocation;

typedef enum {
    FLOW_RELOAD_OK = 0,
    FLOW_RELOAD_INVALID = 1,
    FLOW_RELOAD_INCOMPATIBLE = 2,
    FLOW_RELOAD_BUSY = 3,
    FLOW_RELOAD_NO_CURRENT = 4,
    FLOW_RELOAD_JOURNAL_FULL = 5
} FlowReloadStatus;

FlowReloadContext *flow_reload_create(void *host_context);
int flow_reload_destroy(FlowReloadContext *context);

int flow_reload_reader_register(FlowReloadContext *context,
                                FlowReloadReader *reader);
int flow_reload_reader_unregister(FlowReloadReader *reader);

/* A successful publish transfers state ownership to the runtime. */
int flow_reload_publish(FlowReloadContext *context, const FlowUnit *unit,
                        void *state);
int flow_reload_activate(FlowReloadContext *context, const FlowUnit *unit);
/* The returned unit is caller-owned and remains valid for the ABI lifetime. */
const FlowUnit *flow_reload_current_unit(const FlowReloadContext *context);
int flow_reload_migrate(FlowReloadContext *context, const FlowUnit *candidate);
int flow_reload_migrate_mode(FlowReloadContext *context, const FlowUnit *candidate,
                             FlowMigrationMode mode);
int flow_reload_live_begin(FlowReloadContext *context,
                           const FlowUnit *candidate,
                           size_t journal_capacity);
/* Begin performs the candidate snapshot while writes continue on old state. */
int flow_reload_live_finish(FlowReloadContext *context);
int flow_reload_live_finish_or_fallback(FlowReloadContext *context);
/* Finish replays the bounded journal and publishes, or aborts on failure. */
int flow_reload_live_abort(FlowReloadContext *context);

/* Virtual Memory Zero-Copy Page Remap Morphing (OOM-Resistant under 99% RAM Pressure) */
int flow_reload_morph_zerocopy_remap(FlowReloadContext *context,
                                     const FlowUnit *target_unit,
                                     void **state_inout,
                                     size_t state_size);

/* Dynamic plan artifact reload integration */
int flow_reload_plan(FlowReloadContext *context, const struct FlowPlanArtifact *artifact,
                     const struct SemanticIR *ir, FlowMigrationMode mode);

/* Lock-free RCU fast path read operations (zero mutex locks on read) */
int flow_reload_begin(FlowReloadContext *context, FlowReloadReader *reader,
                      FlowInvocation *invocation);
void flow_reload_end(FlowInvocation *invocation);
int flow_reload_call(FlowReloadContext *context, FlowReloadReader *reader,
                     const void *input, void *output);
int flow_reload_apply(FlowReloadContext *context, FlowReloadReader *reader,
                      const FlowMutation *mutation);
uint64_t flow_reload_generation(const FlowReloadContext *context);
int flow_reload_is_active(const FlowReloadContext *context);
const FlowUnit *flow_reload_current_unit(const FlowReloadContext *context);
void *flow_reload_current_state(const FlowReloadContext *context);
const char *flow_reload_status_name(FlowReloadStatus status);

size_t flow_reload_reclaim(FlowReloadContext *context);
uint64_t flow_schema_hash(const FlowSchema *schema);
int flow_schema_migration_compatible(const FlowSchema *old_schema,
                                     const FlowSchema *new_schema);
int flow_reload_compatible(const FlowUnit *current, const FlowUnit *candidate);

/* ========================================================================= */
/* Unified QSBR (Quiescent State Based Reclamation) - Zero-Write Read Path   */
/* ========================================================================= */

/* Reader thread announces a quiescent state (safe point) at event loop boundary */
void flow_qsbr_checkpoint(FlowReloadReader *reader);

/* Reader thread marks itself offline before sleeping or blocking on I/O */
void flow_qsbr_offline(FlowReloadReader *reader);

/* Reader thread marks itself online when resuming active processing */
void flow_qsbr_online(FlowReloadReader *reader);

/* Absolute zero-atomic-write fast read call (pure Acquire read, 0 cache bouncing) */
int flow_qsbr_call(FlowReloadContext *context, const void *input, void *output);

/* Synchronize: waits for all online registered readers to complete a QSBR grace period */
int flow_qsbr_synchronize(FlowReloadContext *context, uint64_t timeout_ns);

/* Reclaims all retired generations that have passed the QSBR grace period */
size_t flow_qsbr_reclaim(FlowReloadContext *context);

/* QSBR Straggler Watchdog & Quarantine System (Dynamically Derived from SLA / Chebyshev Bounds) */
void flow_reload_set_sla_latency(FlowReloadContext *context, uint64_t sla_latency_ns);
uint64_t flow_qsbr_compute_adaptive_timeout(const FlowReloadContext *context);
int flow_qsbr_watchdog_sweep(FlowReloadContext *context, uint64_t current_time_ns, size_t *quarantined_count_out);
int flow_qsbr_quarantine_reader(FlowReloadReader *reader, void *page_addr, size_t page_size);
int flow_qsbr_unquarantine_reader(FlowReloadReader *reader);
int flow_qsbr_is_reader_quarantined(const FlowReloadReader *reader);

/* ========================================================================= */
/* Deterministic Audit Trail & Mutation Snapshots                            */
/* ========================================================================= */

#define FLOW_AUDIT_TRAIL_CAPACITY 256

typedef struct {
    uint64_t snapshot_id;
    uint64_t timestamp_ns;
    uint64_t generation_id;
    uint64_t env_mask;           /* Environmental & Hardware Mask */
    uint64_t random_seed;        /* PRNG Seed that generated this JIT unit */
    uint64_t schema_hash;        /* Schema & ABI layout hash */
    uint64_t llvm_ir_hash;       /* Hash of synthesized IR / C / machine code */
    uint64_t genome_words[16];   /* 1024-Bit exact genome */
    uint32_t genome_bits;        /* Total bits */
    char component_id[64];       /* Component identifier */
    char flow_name[64];          /* Flow intent name */
    char author_attestation[64]; /* SMT & Safety Attestation */
    int is_golden_fallback;      /* 1 if this was a fallback to Golden Baseline */
} FlowMutationSnapshot;

typedef struct {
    FlowMutationSnapshot entries[FLOW_AUDIT_TRAIL_CAPACITY];
    size_t head;
    size_t total_recorded;
} FlowAuditTrail;

int flow_audit_trail_record(FlowReloadContext *context, const FlowMutationSnapshot *snapshot);
size_t flow_audit_trail_count(const FlowReloadContext *context);
int flow_audit_trail_get(const FlowReloadContext *context, size_t index, FlowMutationSnapshot *out);
int flow_audit_trail_export(const FlowReloadContext *context, FILE *out);
int flow_audit_replay(const FlowMutationSnapshot *snapshot, uint64_t *reproduced_ir_hash_out);

#endif
