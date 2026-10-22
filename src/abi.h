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
} FlowTypeKind;
typedef struct FlowType FlowType;
struct FlowType { FlowTypeKind kind; char name[64]; size_t size_bytes, align_bytes, fixed_count; const FlowType *element_type; };
typedef enum { FLOW_OWN_BORROW, FLOW_OWN_MUT_BORROW, FLOW_OWN_MOVE, FLOW_OWN_RETURN_MOVE, FLOW_OWN_SHARED_REF } FlowOwnership;
typedef enum { FLOW_MEM_RAW_VIEW = 0, FLOW_MEM_RELATIVE_ARCHIVE = 1, FLOW_MEM_ARENA_VIEW = 2, FLOW_MEM_COPIED_OBJECT = 3 } FlowMemoryMode;
#ifndef FLOW_EXTENT_DESCRIPTOR_DEFINED
#define FLOW_EXTENT_DESCRIPTOR_DEFINED
typedef struct { uint64_t base_offset, byte_length, qsbr_epoch; uint32_t stride_bytes, align_bytes, schema_version, flags; uint64_t reserved[3]; } FlowExtentDescriptor;
_Static_assert(sizeof(FlowExtentDescriptor) == 64, "FlowExtentDescriptor must be 64 bytes");
#endif
typedef enum { FLOW_ALIAS_DISJOINT, FLOW_ALIAS_INPLACE_EXACT, FLOW_ALIAS_READ_ONLY_SHARED, FLOW_ALIAS_ANY } FlowAliasingPolicy;
typedef enum { FLOW_REENTRANCY_PURE, FLOW_REENTRANCY_RECURSIVE_SAFE, FLOW_REENTRANCY_NON_REENTRANT, FLOW_REENTRANCY_THREAD_SAFE } FlowReentrancyPolicy;
typedef enum {
    FLOW_EFFECT_PURE = 0, FLOW_EFFECT_READS_STATE = 1 << 0, FLOW_EFFECT_WRITES_STATE = 1 << 1,
    FLOW_EFFECT_ALLOC = 1 << 2, FLOW_EFFECT_IO = 1 << 3, FLOW_EFFECT_BLOCKING = 1 << 4,
    FLOW_EFFECT_PANIC_OR_THROW = 1 << 5, FLOW_EFFECT_RETAIN_POINTER = 1 << 6, FLOW_EFFECT_GLOBAL_MUTATION = 1 << 7
} FlowEffect;
typedef enum { FLOW_ERR_STATUS_CODE, FLOW_ERR_RESULT_STRUCT, FLOW_ERR_NULLABLE, FLOW_ERR_PANIC } FlowErrorPolicy;
typedef struct { FlowMemoryMode mode; size_t min_length, required_alignment; int require_mutable, require_relocatable, require_prevalidated; FlowOwnership expected_ownership; FlowAliasingPolicy aliasing_policy; uint64_t required_lifetime_epoch; } FlowViewContract;
typedef struct { FlowReentrancyPolicy reentrancy; uint32_t effects; FlowErrorPolicy error_policy; int allows_pointer_escape, allows_exception_unwind, is_thread_affine; } FlowControlContract;
typedef struct { char name[64]; FlowType type; FlowOwnership ownership; char description[128]; } FlowParam;
#define FLOW_ABI_MAX_PARAMS 8
#define FLOW_ABI_MAX_FUNCTIONS 8
typedef struct { char name[64]; FlowParam params[FLOW_ABI_MAX_PARAMS]; size_t param_count; FlowType return_type; FlowOwnership return_ownership; FlowErrorPolicy error_policy; uint32_t effects; char docstring[256]; } FlowFunctionSignature;
typedef struct { char component_id[64], module_name[64], version[32]; int thread_safe; size_t max_memory_bytes, function_count; FlowMemoryMode default_memory_mode; FlowControlContract control_contract; FlowViewContract input_view_contract, output_view_contract; FlowFunctionSignature create_fn, destroy_fn, primary_fn, functions[FLOW_ABI_MAX_FUNCTIONS]; } FlowComponentABI;
int flow_abi_build_for_component(const SemanticIR *ir, const Component *comp, FlowComponentABI *abi);
int flow_abi_emit_c_header(FILE *output, const FlowComponentABI *abi);
int flow_abi_emit_rust_adapter(FILE *output, const FlowComponentABI *abi);
int flow_abi_emit_python_adapter(FILE *output, const FlowComponentABI *abi);
int flow_abi_emit_polyglot_adapter(FILE *output, const FlowComponentABI *abi, const char *target_lang);
int flow_abi_emit_lean_adapter(FILE *output, const SemanticIR *ir, const FlowComponentABI *abi, uint64_t genome, size_t capacity, size_t shards);
int flow_abi_audit_lean_certificate(const char *lean_source, char *err_msg, size_t err_size);
int flow_abi_verify_lean_certificate(const char *lean_path, char *err_msg, size_t err_size);
#endif
