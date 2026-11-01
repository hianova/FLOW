#ifndef FLOW_RELOAD_H
#define FLOW_RELOAD_H

#include "flow.h"
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FLOW_RELOAD_ABI_VERSION       UINT32_C(3)
#define FLOW_RELOAD_MAX_RETIRED_DEPTH (sizeof(uint64_t) * 8U)

typedef struct FlowReloadContext FlowReloadContext;

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

typedef struct {
    const char *name, *type;
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
    FLOW_LAYOUT_AOS = 1,
    FLOW_LAYOUT_SOA = 2,
    FLOW_LAYOUT_COLUMNAR = 3
} FlowLayoutKind;

typedef struct FlowUnit {
    uint32_t abi_version;
    uint64_t semantic_schema_hash, constraint_hash, capability_hash;
    const char *name;
    FlowLayoutKind layout;
    int supports_snapshot_cow;
    int (*init)(void *host_context, void **state_out);
    int (*run)(void *host_context, void *state, const void *input, void *output);
    int (*apply)(void *host_context, void *state, const FlowMutation *mutation);
    int (*migrate)(void *host_context, const void *old_state, void *new_state);
    void (*drop)(void *host_context, void *state);
    const FlowSchema *schema;
} FlowUnit;

#ifndef FLOW_CACHE_LINE_SIZE
#define FLOW_CACHE_LINE_SIZE 64
#endif
#define FLOW_CACHE_ALIGNED __attribute__((aligned(FLOW_CACHE_LINE_SIZE)))

typedef struct FlowReloadReader FlowReloadReader;
struct FlowReloadReader {
    FlowReloadContext *context;
    struct FlowReloadReader *next;
    _Atomic uint64_t active_epoch, last_heartbeat_ns, qsbr_epoch;
    _Atomic int registered, is_offline, is_quarantined;
    uint8_t _cache_pad[24];
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

/* QSBR Epoch & Atomic Pointer Hot-Swap API */
FlowReloadContext *flow_reload_create(void *host_context);
int flow_reload_destroy(FlowReloadContext *context);
int flow_reload_reader_register(FlowReloadContext *context, FlowReloadReader *reader);
int flow_reload_reader_unregister(FlowReloadReader *reader);
int flow_reload_publish(FlowReloadContext *context, const FlowUnit *unit, void *state);
int flow_reload_activate(FlowReloadContext *context, const FlowUnit *unit);
const FlowUnit *flow_reload_current_unit(const FlowReloadContext *context);
int flow_reload_begin(FlowReloadContext *context, FlowReloadReader *reader,
                      FlowInvocation *invocation);
void flow_reload_end(FlowInvocation *invocation);
int flow_reload_call(FlowReloadContext *context, FlowReloadReader *reader, const void *input,
                     void *output);
uint64_t flow_reload_generation(const FlowReloadContext *context);
size_t flow_reload_reclaim(FlowReloadContext *context);
void flow_qsbr_checkpoint(FlowReloadReader *reader);
void flow_qsbr_offline(FlowReloadReader *reader);
void flow_qsbr_online(FlowReloadReader *reader);
int flow_qsbr_call(FlowReloadContext *context, const void *input, void *output);
int flow_qsbr_synchronize(FlowReloadContext *context, uint64_t timeout_ns);

/* In-Memory Dual-Mapped W^X Memory Pool (HoTT Univalence Substrate) */
#define FLOW_JIT_DEFAULT_HEAP_BYTES (1024U * 1024U)

typedef struct FlowJITEngine FlowJITEngine;
typedef struct {
    int enable_lto, opt_level;
    size_t initial_code_heap_bytes;
} FlowJITConfig;

typedef struct {
    int is_dual_mapped;
    uintptr_t write_base, exec_base;
    size_t pool_size, pool_used;
    uint64_t tlb_shootdowns_avoided;
} FlowJITPoolStats;

FlowJITEngine *flow_jit_create(const FlowJITConfig *config);
void flow_jit_destroy(FlowJITEngine *engine);
int flow_jit_get_pool_stats(const FlowJITEngine *engine, FlowJITPoolStats *stats_out);

/* Discrete Dwell-Time & Threshold Debounce Primitive (Schmitt Trigger) */
typedef struct {
    double drop_threshold, recovery_threshold;
    uint64_t dwell_time_required_ns, stable_since_ns;
    int current_state;
} FlowSchmittTrigger;

void flow_schmitt_trigger_init(FlowSchmittTrigger *st, double base_min, uint64_t dwell_ns);
int flow_schmitt_trigger_update(FlowSchmittTrigger *st, double current_val,
                                uint64_t current_time_ns, int *state_changed_out);

#endif /* FLOW_RELOAD_H */
