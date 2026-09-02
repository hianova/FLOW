#ifndef FLOW_JIT_H
#define FLOW_JIT_H

#include "reload.h"
#include "flow.h"

#include <stddef.h>
#include <stdint.h>

typedef struct FlowJITEngine FlowJITEngine;

typedef struct {
    uintptr_t start_ip;
    uintptr_t end_ip;
    size_t code_bytes;
    FlowLayoutKind layout;
    uint64_t compile_time_ns;
} FlowJITCodeBlock;

typedef struct {
    int enable_lto;
    int opt_level; /* 0, 1, 2, 3 */
    size_t initial_code_heap_bytes;
} FlowJITConfig;

/* Dual-Mapped Zero-TLB-Shootdown JIT Memory Pool Stats */
typedef struct {
    int is_dual_mapped;
    uintptr_t write_base;
    uintptr_t exec_base;
    size_t pool_size;
    size_t pool_used;
    uint64_t tlb_shootdowns_avoided;
} FlowJITPoolStats;

/* Initialize / Teardown In-Memory JIT Engine */
FlowJITEngine *flow_jit_create(const FlowJITConfig *config);
void flow_jit_destroy(FlowJITEngine *engine);
int flow_jit_get_pool_stats(const FlowJITEngine *engine, FlowJITPoolStats *stats_out);

/* In-Memory Zero-I/O Compilation from LLVM IR text into executable FlowUnit */
int flow_jit_compile_llvm_ir(FlowJITEngine *engine,
                             const char *llvm_ir_code,
                             const char *unit_name,
                             FlowLayoutKind layout,
                             FlowUnit *unit_out,
                             FlowJITCodeBlock *code_block_out);

/* Dynamically derive the physical compiler working-set RAM threshold (MB) */
int flow_jit_calculate_min_memory_mb(const SemanticIR *ir);

/* Dynamic JIT State Migration Routine Generators */
typedef struct {
    size_t item_count;
    size_t field_count;
    size_t field_sizes[8];
    FlowLayoutKind from_layout;
    FlowLayoutKind to_layout;
    uint8_t field_changed[8];
} FlowLayoutMigrationSpec;

/* Execute in-memory state layout migration between AoS and SoA / Columnar */
int flow_jit_migrate_state_layout(const FlowLayoutMigrationSpec *spec,
                                 const void *old_raw_state,
                                 void *new_raw_state,
                                 size_t *bytes_copied_out,
                                 size_t *bytes_transformed_out);

/* Estimate Layout Migration Cost & Amortization Payback Calls */
int flow_jit_calculate_migration_cost(const FlowLayoutMigrationSpec *spec,
                                      double steady_state_gain_ns_per_call,
                                      double *migration_cost_ns_out,
                                      double *payback_calls_out);

/* ========================================================================= */
/* Asynchronous Background JIT Engine (Zero-Latency Main-Thread Compilation) */
/* ========================================================================= */

typedef struct FlowAsyncJITPool FlowAsyncJITPool;

typedef struct {
    size_t worker_threads;
    FlowReloadContext *reload_ctx;
} FlowAsyncJITConfig;

FlowAsyncJITPool *flow_async_jit_create(const FlowAsyncJITConfig *config);
void flow_async_jit_destroy(FlowAsyncJITPool *pool);

/* Submit compilation task to background worker; returns immediately (0ms blocking for main thread) */
int flow_async_jit_submit(FlowAsyncJITPool *pool,
                          const char *c_or_llvm_source,
                          const char *unit_name,
                          FlowLayoutKind layout,
                          int auto_publish_on_complete);

/* Number of background compilations completed */
size_t flow_async_jit_completed_count(const FlowAsyncJITPool *pool);

/* Wait for all pending background compilations to finish */
void flow_async_jit_wait_idle(FlowAsyncJITPool *pool);

#endif
