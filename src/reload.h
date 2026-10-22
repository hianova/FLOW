#ifndef FLOW_RELOAD_H
#define FLOW_RELOAD_H
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#ifndef FLOW_SMT_RESULT_DEFINED
#define FLOW_SMT_RESULT_DEFINED
typedef enum { FLOW_SMT_PROVEN_UNSAT = 0, FLOW_SMT_VIOLATION_SAT = 1, FLOW_SMT_UNKNOWN = 2 } FlowSMTResult;
#endif
typedef struct FlowSMTProofAttestation FlowSMTProofAttestation;
#define FLOW_RELOAD_ABI_VERSION UINT32_C(3)
typedef struct FlowReloadContext FlowReloadContext; struct FlowPlanArtifact; struct SemanticIR; struct FlowWavefrontRing; struct FlowBumpQsbrArena;
typedef enum { FLOW_MUTATION_UPSERT = 1, FLOW_MUTATION_DELETE = 2 } FlowMutationKind;
typedef struct { FlowMutationKind kind; uint64_t sequence; const void *key; size_t key_size; const void *value; size_t value_size; } FlowMutation;
enum { FLOW_SCHEMA_FIELD_DEFAULTABLE = UINT32_C(1) << 0, FLOW_SCHEMA_FIELD_PERSISTENT = UINT32_C(1) << 1, FLOW_SCHEMA_FIELD_ORDERED = UINT32_C(1) << 2 };
typedef struct { const char *name, *type; uint32_t flags; } FlowSchemaField;
typedef struct { const char *name; uint32_t version; const FlowSchemaField *fields; size_t field_count; } FlowSchema;
typedef enum { FLOW_LAYOUT_DEFAULT = 0, FLOW_LAYOUT_AOS = 1, FLOW_LAYOUT_SOA = 2, FLOW_LAYOUT_COLUMNAR = 3 } FlowLayoutKind;
typedef struct FlowUnit { uint32_t abi_version; uint64_t semantic_schema_hash, constraint_hash, capability_hash; const char *name; FlowLayoutKind layout; int supports_snapshot_cow;
    int (*init)(void *host_context, void **state_out); int (*run)(void *host_context, void *state, const void *input, void *output);
    int (*apply)(void *host_context, void *state, const FlowMutation *mutation); int (*migrate)(void *host_context, const void *old_state, void *new_state);
    void (*drop)(void *host_context, void *state); const FlowSchema *schema; } FlowUnit;
#ifndef FLOW_CACHE_LINE_SIZE
#define FLOW_CACHE_LINE_SIZE 64
#endif
#define FLOW_CACHE_ALIGNED __attribute__((aligned(FLOW_CACHE_LINE_SIZE)))
typedef struct FlowReloadReader FlowReloadReader;
struct FlowReloadReader { FlowReloadContext *context; struct FlowReloadReader *next; _Atomic uint64_t active_epoch, last_heartbeat_ns, qsbr_epoch; _Atomic int registered, is_offline, is_quarantined; uint8_t _cache_pad[24]; } FLOW_CACHE_ALIGNED;
typedef struct { FlowReloadContext *context; FlowReloadReader *reader; const FlowUnit *unit; void *state; uint64_t generation; int active; } FlowInvocation;
typedef enum { FLOW_RELOAD_OK = 0, FLOW_RELOAD_INVALID = 1, FLOW_RELOAD_INCOMPATIBLE = 2, FLOW_RELOAD_BUSY = 3, FLOW_RELOAD_NO_CURRENT = 4, FLOW_RELOAD_JOURNAL_FULL = 5 } FlowReloadStatus;
FlowReloadContext *flow_reload_create(void *host_context); int flow_reload_destroy(FlowReloadContext *context);
int flow_reload_reader_register(FlowReloadContext *context, FlowReloadReader *reader); int flow_reload_reader_unregister(FlowReloadReader *reader);
int flow_reload_publish(FlowReloadContext *context, const FlowUnit *unit, void *state); int flow_reload_activate(FlowReloadContext *context, const FlowUnit *unit);
const FlowUnit *flow_reload_current_unit(const FlowReloadContext *context);
int flow_reload_begin(FlowReloadContext *context, FlowReloadReader *reader, FlowInvocation *invocation); void flow_reload_end(FlowInvocation *invocation);
int flow_reload_call(FlowReloadContext *context, FlowReloadReader *reader, const void *input, void *output);
uint64_t flow_reload_generation(const FlowReloadContext *context); size_t flow_reload_reclaim(FlowReloadContext *context); uint64_t flow_schema_hash(const FlowSchema *schema);
int flow_schema_migration_compatible(const FlowSchema *old_schema, const FlowSchema *new_schema); int flow_reload_compatible(const FlowUnit *current, const FlowUnit *candidate);
void flow_qsbr_checkpoint(FlowReloadReader *reader); void flow_qsbr_offline(FlowReloadReader *reader); void flow_qsbr_online(FlowReloadReader *reader);
int flow_qsbr_call(FlowReloadContext *context, const void *input, void *output); int flow_qsbr_synchronize(FlowReloadContext *context, uint64_t timeout_ns);
size_t flow_qsbr_reclaim(FlowReloadContext *context);
int flow_reload_bind_wavefront(FlowReloadContext *context, const struct FlowWavefrontRing *ring); int flow_reload_bind_arena(FlowReloadContext *context, struct FlowBumpQsbrArena *arena);
FlowSMTResult flow_reload_verify_topological_safety_smt(const FlowReloadContext *context, FlowSMTProofAttestation *proof_out);
#endif
