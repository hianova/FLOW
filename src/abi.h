#ifndef FLOW_ABI_H
#define FLOW_ABI_H
#include "flow.h"
#include "plugin.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
typedef enum { FLOW_TYPE_VOID, FLOW_TYPE_BOOL, FLOW_TYPE_I32, FLOW_TYPE_I64, FLOW_TYPE_U32, FLOW_TYPE_U64,
    FLOW_TYPE_USIZE, FLOW_TYPE_F32, FLOW_TYPE_F64, FLOW_TYPE_STRING, FLOW_TYPE_BYTES,
    FLOW_TYPE_OPAQUE_HANDLE, FLOW_TYPE_STRUCT, FLOW_TYPE_BUFFER, FLOW_TYPE_OPTION, FLOW_TYPE_RESULT
} FlowTypeKind; typedef struct FlowType FlowType; struct FlowType { FlowTypeKind kind; char name[64]; size_t size_bytes, align_bytes, fixed_count;
    const FlowType *element_type; }; typedef enum { FLOW_OWN_BORROW, FLOW_OWN_MUT_BORROW, FLOW_OWN_MOVE, FLOW_OWN_RETURN_MOVE, FLOW_OWN_SHARED_REF } FlowOwnership;
typedef enum { FLOW_MEM_RAW_VIEW = 0, FLOW_MEM_RELATIVE_ARCHIVE = 1, FLOW_MEM_ARENA_VIEW = 2, FLOW_MEM_COPIED_OBJECT = 3 } FlowMemoryMode; typedef struct { void *address;
    size_t length, alignment; int mutability, is_relocatable, is_validated; FlowOwnership ownership; uint64_t lifetime_epoch; FlowMemoryMode mode; } FlowMemoryView;
typedef struct { int32_t offset; } FlowRelPtr; static inline void *flow_relptr_resolve(const FlowRelPtr *ptr) {
    return (!ptr || ptr->offset == 0) ? NULL : (void *)((const char *)ptr + ptr->offset); } static inline void flow_relptr_set(FlowRelPtr *ptr, const void *target) {
    if (!ptr) return; if (!target) { ptr->offset = 0; return; } int64_t diff = (int64_t)((const char *)target - (const char *)ptr);
    ptr->offset = (diff < INT32_MIN || diff > INT32_MAX) ? 0 : (int32_t)diff; }
typedef enum { FLOW_ALIAS_DISJOINT, FLOW_ALIAS_INPLACE_EXACT, FLOW_ALIAS_READ_ONLY_SHARED, FLOW_ALIAS_ANY } FlowAliasingPolicy;
typedef enum { FLOW_REENTRANCY_PURE, FLOW_REENTRANCY_RECURSIVE_SAFE, FLOW_REENTRANCY_NON_REENTRANT, FLOW_REENTRANCY_THREAD_SAFE } FlowReentrancyPolicy; typedef enum {
    FLOW_EFFECT_PURE = 0, FLOW_EFFECT_READS_STATE = 1 << 0, FLOW_EFFECT_WRITES_STATE = 1 << 1,
    FLOW_EFFECT_ALLOC = 1 << 2, FLOW_EFFECT_IO = 1 << 3, FLOW_EFFECT_BLOCKING = 1 << 4,
    FLOW_EFFECT_PANIC_OR_THROW = 1 << 5, FLOW_EFFECT_RETAIN_POINTER = 1 << 6, FLOW_EFFECT_GLOBAL_MUTATION = 1 << 7
} FlowEffect; typedef enum { FLOW_ERR_STATUS_CODE, FLOW_ERR_RESULT_STRUCT, FLOW_ERR_NULLABLE, FLOW_ERR_PANIC } FlowErrorPolicy; typedef struct { FlowMemoryMode mode;
    size_t min_length, required_alignment; int require_mutable, require_relocatable, require_prevalidated; FlowOwnership expected_ownership;
    FlowAliasingPolicy aliasing_policy; uint64_t required_lifetime_epoch; } FlowViewContract; typedef struct { FlowReentrancyPolicy reentrancy; uint32_t effects;
    FlowErrorPolicy error_policy; int allows_pointer_escape, allows_exception_unwind, is_thread_affine; } FlowControlContract;
typedef struct { char name[64]; FlowType type; FlowOwnership ownership; char description[128]; } FlowParam;
#define FLOW_ABI_MAX_PARAMS 8
#define FLOW_ABI_MAX_FUNCTIONS 8
typedef struct { char name[64]; FlowParam params[FLOW_ABI_MAX_PARAMS]; size_t param_count; FlowType return_type; FlowOwnership return_ownership; FlowErrorPolicy error_policy;
    uint32_t effects; char docstring[256]; } FlowFunctionSignature; typedef struct { char component_id[64], module_name[64], version[32]; int thread_safe;
    size_t max_memory_bytes, function_count; FlowMemoryMode default_memory_mode; FlowControlContract control_contract;
    FlowViewContract input_view_contract, output_view_contract; FlowFunctionSignature create_fn, destroy_fn, primary_fn, functions[FLOW_ABI_MAX_FUNCTIONS]; } FlowComponentABI;
typedef struct FlowBackendEmitter FlowBackendEmitter; struct FlowBackendEmitter { void *user_ctx;
    int (*emit_view_decl)(FlowBackendEmitter *emitter, FILE *out, const char *var_name, const FlowViewContract *contract);
    int (*emit_lifetime_guard)(FlowBackendEmitter *emitter, FILE *out, const char *view_name, uint64_t epoch);
    int (*emit_alias_check)(FlowBackendEmitter *emitter, FILE *out, const char *in_view, const char *out_view, FlowAliasingPolicy policy);
    int (*emit_error_bridge)(FlowBackendEmitter *emitter, FILE *out, const FlowControlContract *control, const char *call_expr);
    int (*emit_adapter)(FlowBackendEmitter *emitter, FILE *out, const FlowComponentABI *abi, const char *target_lang); };
int flow_memory_view_init(FlowMemoryView *view, void *address, size_t length, size_t alignment, int mutability, FlowOwnership ownership, uint64_t lifetime_epoch, FlowMemoryMode mode);
int flow_memory_view_validate(const FlowMemoryView *view, size_t required_bytes, size_t required_align, int require_mutable, uint64_t min_epoch, char *err_msg, size_t err_size);
int flow_memory_view_slice(const FlowMemoryView *parent, size_t offset, size_t length, FlowMemoryView *subview_out, char *err_msg, size_t err_size);
int flow_memory_view_resolve_relptr(const FlowMemoryView *view, size_t relptr_offset, void **resolved_out, char *err_msg, size_t err_size);
int flow_view_contract_verify(const FlowViewContract *contract, const FlowMemoryView *view, char *err_msg, size_t err_size);
int flow_aliasing_verify(const FlowMemoryView *in_view, const FlowMemoryView *out_view, FlowAliasingPolicy policy, char *err_msg, size_t err_size);
int flow_control_contract_verify(const FlowControlContract *callee, const FlowControlContract *caller_context, const FlowMemoryView *passed_view, char *err_msg, size_t err_size);
int flow_abi_build_for_component(const SemanticIR *ir, const Component *comp, FlowComponentABI *abi);
int flow_abi_check_composition(const FlowComponentABI *producer, const FlowComponentABI *consumer, char *err_msg, size_t err_size);
int flow_abi_emit_c_header(FILE *output, const FlowComponentABI *abi); int flow_abi_emit_rust_adapter(FILE *output, const FlowComponentABI *abi);
int flow_abi_emit_python_adapter(FILE *output, const FlowComponentABI *abi);
int flow_abi_emit_lean_adapter(FILE *output, const SemanticIR *ir, const FlowComponentABI *abi, uint64_t genome, size_t capacity, size_t shards);
int flow_abi_audit_lean_certificate(const char *lean_source, char *err_msg, size_t err_size);
#endif
