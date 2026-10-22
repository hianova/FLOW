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
int flow_jit_calculate_min_memory_mb(const SemanticIR *ir);
typedef struct FlowLayoutMigrationSpec { FlowLayoutKind from_layout, to_layout; size_t item_count, field_count, field_sizes[8]; int field_changed[8]; } FlowLayoutMigrationSpec;
#endif
