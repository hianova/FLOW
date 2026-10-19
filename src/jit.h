#ifndef FLOW_JIT_H
#define FLOW_JIT_H
#include "reload.h"
#include "flow.h"
#include <stddef.h>
#include <stdint.h>
typedef struct FlowJITEngine FlowJITEngine;
typedef struct FlowJITCodeBlock { uintptr_t start_ip, end_ip; size_t code_bytes; FlowLayoutKind layout; uint64_t compile_time_ns; } FlowJITCodeBlock;
typedef struct { int enable_lto, opt_level; size_t initial_code_heap_bytes; } FlowJITConfig;
typedef struct { int is_dual_mapped; uintptr_t write_base, exec_base; size_t pool_size, pool_used; uint64_t tlb_shootdowns_avoided; } FlowJITPoolStats;
FlowJITEngine *flow_jit_create(const FlowJITConfig *config); void flow_jit_destroy(FlowJITEngine *engine);
int flow_jit_get_pool_stats(const FlowJITEngine *engine, FlowJITPoolStats *stats_out);
int flow_jit_verify_wx_invariants(const FlowJITEngine *engine, char *message, size_t message_size);
int flow_jit_compile_llvm_ir(FlowJITEngine *engine, const char *llvm_ir_code, const char *unit_name, FlowLayoutKind layout, FlowUnit *unit_out, FlowJITCodeBlock *code_block_out);
typedef int64_t (*FlowJITBinaryIntFn)(int64_t a, int64_t b); typedef double (*FlowJITBinaryDoubleFn)(double a, double b);
int64_t flow_jit_execute_binary_int(const FlowJITCodeBlock *block, int64_t a, int64_t b);
double flow_jit_execute_binary_double(const FlowJITCodeBlock *block, double a, double b); int flow_jit_calculate_min_memory_mb(const SemanticIR *ir);
typedef struct FlowLayoutMigrationSpec { size_t item_count, field_count, field_sizes[8]; FlowLayoutKind from_layout, to_layout; uint8_t field_changed[8];
} FlowLayoutMigrationSpec;
int flow_jit_migrate_state_layout(const FlowLayoutMigrationSpec *spec, const void *old_raw_state, void *new_raw_state, size_t *bytes_copied_out, size_t *bytes_transformed_out);
int flow_jit_calculate_migration_cost(const FlowLayoutMigrationSpec *spec, double steady_state_gain_ns_per_call, double *migration_cost_ns_out, double *payback_calls_out);
typedef struct FlowAsyncJITPool FlowAsyncJITPool; typedef struct { size_t worker_threads; FlowReloadContext *reload_ctx; } FlowAsyncJITConfig;
FlowAsyncJITPool *flow_async_jit_create(const FlowAsyncJITConfig *config); void flow_async_jit_destroy(FlowAsyncJITPool *pool);
int flow_async_jit_submit(FlowAsyncJITPool *pool, const char *c_or_llvm_source, const char *unit_name, FlowLayoutKind layout, int auto_publish_on_complete);
size_t flow_async_jit_completed_count(const FlowAsyncJITPool *pool); void flow_async_jit_wait_idle(FlowAsyncJITPool *pool);
#endif
