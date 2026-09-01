#ifndef FLOW_RELOAD_H
#define FLOW_RELOAD_H

#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

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

typedef struct FlowReloadReader {
    /* Keep this object address-stable until unregister succeeds. */
    FlowReloadContext *context;
    struct FlowReloadReader *next;
    _Atomic uint64_t active_epoch;
    _Atomic int registered;
} FlowReloadReader;

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

size_t flow_reload_reclaim(FlowReloadContext *context);
uint64_t flow_schema_hash(const FlowSchema *schema);
int flow_schema_migration_compatible(const FlowSchema *old_schema,
                                     const FlowSchema *new_schema);
int flow_reload_compatible(const FlowUnit *current, const FlowUnit *candidate);
const char *flow_reload_status_name(FlowReloadStatus status);

#endif
