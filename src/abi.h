#ifndef FLOW_ABI_H
#define FLOW_ABI_H

#include "flow.h"
#include "plugin.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

/* ========================================================================= */
/* 1. Language-Neutral Type System                                           */
/* ========================================================================= */

typedef enum {
    FLOW_TYPE_VOID,
    FLOW_TYPE_BOOL,
    FLOW_TYPE_I32,
    FLOW_TYPE_I64,
    FLOW_TYPE_U32,
    FLOW_TYPE_U64,
    FLOW_TYPE_USIZE,
    FLOW_TYPE_F32,
    FLOW_TYPE_F64,
    FLOW_TYPE_STRING,        /* UTF-8 string (ptr + len) */
    FLOW_TYPE_BYTES,         /* Byte slice [u8] (ptr + len) */
    FLOW_TYPE_OPAQUE_HANDLE, /* *mut c_void / FlowHandle */
    FLOW_TYPE_STRUCT,        /* Named composite struct */
    FLOW_TYPE_BUFFER,        /* Fixed or bounded contiguous buffer */
    FLOW_TYPE_OPTION,        /* Nullable / Optional value */
    FLOW_TYPE_RESULT         /* Value or Error code */
} FlowTypeKind;

typedef struct FlowType FlowType;

struct FlowType {
    FlowTypeKind kind;
    char name[64];
    size_t size_bytes;
    size_t align_bytes;
    const FlowType *element_type; /* For Buffer, Option, Result */
    size_t fixed_count;           /* For fixed arrays / buffer bounds */
};

/* ========================================================================= */
/* 2. Ownership & Lifetime Model                                             */
/* ========================================================================= */

typedef enum {
    FLOW_OWN_BORROW,      /* Read-only borrow (&T, const T*, borrowed ref) */
    FLOW_OWN_MUT_BORROW,  /* Exclusive mutable borrow (&mut T, T*) */
    FLOW_OWN_MOVE,        /* Ownership transferred to callee (T move, consumes ptr) */
    FLOW_OWN_RETURN_MOVE, /* Callee allocates and transfers ownership to caller */
    FLOW_OWN_SHARED_REF   /* Shared reference counted ownership (Arc, PyObject incref) */
} FlowOwnership;

/* ========================================================================= */
/* 3. Zero-Copy Data Plane & Memory Contract                                 */
/* ========================================================================= */

typedef enum {
    FLOW_MEM_RAW_VIEW          = 0, /* Same process/ownership: raw pointer + length (Default fast path) */
    FLOW_MEM_RELATIVE_ARCHIVE  = 1, /* Shared buffer / mmap: offset/relative pointer + layout contract */
    FLOW_MEM_ARENA_VIEW        = 2, /* Region / arena-backed memory view */
    FLOW_MEM_COPIED_OBJECT     = 3  /* Isolated process / untrusted boundary: copied bytes */
} FlowMemoryMode;

typedef struct {
    void *address;            /* Memory base address or pointer */
    size_t length;            /* Size in bytes */
    size_t alignment;         /* Alignment requirement (1, 4, 8, 16, 64) */
    int mutability;           /* 0 = read-only borrow, 1 = mutable */
    FlowOwnership ownership;  /* BORROW, MUT_BORROW, MOVE, RETURN_MOVE, SHARED_REF */
    uint64_t lifetime_epoch;  /* Generation / lifetime epoch (e.g. QSBR / frame sequence) */
    int is_relocatable;       /* 1 = uses relative offset pointers */
    int is_validated;         /* 1 = layout & bounds pre-validated (zero-cost access) */
    FlowMemoryMode mode;      /* RAW_VIEW, RELATIVE_ARCHIVE, ARENA_VIEW, COPIED_OBJECT */
} FlowMemoryView;

/* Relative pointer for zero-copy position-independent archives */
typedef struct {
    int32_t offset;
} FlowRelPtr;

static inline void *flow_relptr_resolve(const FlowRelPtr *ptr) {
    if (ptr == NULL || ptr->offset == 0) return NULL;
    return (void *)((const char *)ptr + ptr->offset);
}

static inline void flow_relptr_set(FlowRelPtr *ptr, const void *target) {
    if (ptr == NULL || target == NULL) {
        if (ptr) ptr->offset = 0;
        return;
    }
    int64_t diff = (int64_t)((const char *)target - (const char *)ptr);
    if (diff < INT32_MIN || diff > INT32_MAX) {
        ptr->offset = 0;
        return;
    }
    ptr->offset = (int32_t)diff;
}

/* ========================================================================= */
/* 4. Aliasing, Reentrancy & Control/Effect Model                            */
/* ========================================================================= */

typedef enum {
    FLOW_ALIAS_DISJOINT,         /* [in, in+len) and [out, out+len) must NOT overlap */
    FLOW_ALIAS_INPLACE_EXACT,     /* in == out strictly */
    FLOW_ALIAS_READ_ONLY_SHARED,  /* Concurrent readers may alias */
    FLOW_ALIAS_ANY               /* Unconstrained aliasing */
} FlowAliasingPolicy;

typedef enum {
    FLOW_REENTRANCY_PURE,            /* Stateless and purely reentrant */
    FLOW_REENTRANCY_RECURSIVE_SAFE,  /* Safe for nested invocation from same thread */
    FLOW_REENTRANCY_NON_REENTRANT,   /* Strictly non-reentrant */
    FLOW_REENTRANCY_THREAD_SAFE      /* Safe across multiple threads */
} FlowReentrancyPolicy;

typedef enum {
    FLOW_EFFECT_PURE             = 0,
    FLOW_EFFECT_READS_STATE      = 1 << 0,
    FLOW_EFFECT_WRITES_STATE     = 1 << 1,
    FLOW_EFFECT_ALLOC            = 1 << 2,
    FLOW_EFFECT_IO               = 1 << 3,
    FLOW_EFFECT_BLOCKING         = 1 << 4,
    FLOW_EFFECT_PANIC_OR_THROW   = 1 << 5, /* Callback/callee can throw or panic */
    FLOW_EFFECT_RETAIN_POINTER   = 1 << 6, /* Escapes/stores pointer past call return */
    FLOW_EFFECT_GLOBAL_MUTATION  = 1 << 7  /* Mutates process/global environment */
} FlowEffect;

/* ========================================================================= */
/* 5. Error Policy & Calling Convention                                      */
/* ========================================================================= */

typedef enum {
    FLOW_ERR_STATUS_CODE,   /* 0 = success, negative = error */
    FLOW_ERR_RESULT_STRUCT, /* Struct returning { int status; Payload data; } */
    FLOW_ERR_NULLABLE,      /* NULL on failure */
    FLOW_ERR_PANIC          /* Unrecoverable abort on contract breach */
} FlowErrorPolicy;

/* ========================================================================= */
/* 6. Formal Contracts: View, Control & Effect Contracts                     */
/* ========================================================================= */

typedef struct {
    FlowMemoryMode mode;
    size_t min_length;
    size_t required_alignment;
    int require_mutable;
    FlowOwnership expected_ownership;
    FlowAliasingPolicy aliasing_policy;
    int require_relocatable;
    int require_prevalidated;
    uint64_t required_lifetime_epoch;
} FlowViewContract;

typedef struct {
    FlowReentrancyPolicy reentrancy;
    uint32_t effects;              /* Bitmask of FlowEffect */
    FlowErrorPolicy error_policy;
    int allows_pointer_escape;     /* 1 = callee stores pointer */
    int allows_exception_unwind;   /* 1 = callee may throw exception */
    int is_thread_affine;          /* 1 = tied to originating thread */
} FlowControlContract;

/* ========================================================================= */
/* 7. Function Signatures & Component ABI                                    */
/* ========================================================================= */

typedef struct {
    char name[64];
    FlowType type;
    FlowOwnership ownership;
    char description[128];
} FlowParam;

#define FLOW_ABI_MAX_PARAMS 8
#define FLOW_ABI_MAX_FUNCTIONS 8

typedef struct {
    char name[64];
    FlowParam params[FLOW_ABI_MAX_PARAMS];
    size_t param_count;
    FlowType return_type;
    FlowOwnership return_ownership;
    FlowErrorPolicy error_policy;
    uint32_t effects;       /* Bitmask of FlowEffect */
    char docstring[256];
} FlowFunctionSignature;

typedef struct {
    char component_id[64];
    char module_name[64];
    char version[32];
    int thread_safe;
    size_t max_memory_bytes;
    FlowMemoryMode default_memory_mode;
    FlowControlContract control_contract;
    FlowViewContract input_view_contract;
    FlowViewContract output_view_contract;

    FlowFunctionSignature create_fn;
    FlowFunctionSignature destroy_fn;
    FlowFunctionSignature primary_fn;

    FlowFunctionSignature functions[FLOW_ABI_MAX_FUNCTIONS];
    size_t function_count;
} FlowComponentABI;

/* ========================================================================= */
/* 8. Backend Emitter Operation Interface                                    */
/* ========================================================================= */

typedef struct FlowBackendEmitter FlowBackendEmitter;

struct FlowBackendEmitter {
    void *user_ctx;
    int (*emit_view_decl)(FlowBackendEmitter *emitter, FILE *out, const char *var_name, const FlowViewContract *contract);
    int (*emit_lifetime_guard)(FlowBackendEmitter *emitter, FILE *out, const char *view_name, uint64_t epoch);
    int (*emit_alias_check)(FlowBackendEmitter *emitter, FILE *out, const char *in_view, const char *out_view, FlowAliasingPolicy policy);
    int (*emit_error_bridge)(FlowBackendEmitter *emitter, FILE *out, const FlowControlContract *control, const char *call_expr);
    int (*emit_adapter)(FlowBackendEmitter *emitter, FILE *out, const FlowComponentABI *abi, const char *target_lang);
};

/* ========================================================================= */
/* 9. Contract Verification & Zero-Copy View Operations                      */
/* ========================================================================= */

int flow_memory_view_init(FlowMemoryView *view, void *address, size_t length,
                          size_t alignment, int mutability, FlowOwnership ownership,
                          uint64_t lifetime_epoch, FlowMemoryMode mode);

int flow_memory_view_validate(const FlowMemoryView *view, size_t required_bytes,
                              size_t required_align, int require_mutable,
                              uint64_t min_epoch, char *err_msg, size_t err_size);

int flow_memory_view_slice(const FlowMemoryView *parent, size_t offset, size_t length,
                           FlowMemoryView *subview_out, char *err_msg, size_t err_size);

int flow_memory_view_resolve_relptr(const FlowMemoryView *view, size_t relptr_offset,
                                    void **resolved_out, char *err_msg, size_t err_size);

int flow_view_contract_verify(const FlowViewContract *contract, const FlowMemoryView *view,
                              char *err_msg, size_t err_size);

int flow_aliasing_verify(const FlowMemoryView *in_view, const FlowMemoryView *out_view,
                         FlowAliasingPolicy policy, char *err_msg, size_t err_size);

int flow_control_contract_verify(const FlowControlContract *callee,
                                 const FlowControlContract *caller_context,
                                 const FlowMemoryView *passed_view,
                                 char *err_msg, size_t err_size);

/* ========================================================================= */
/* 10. ABI Construction & Cross-Language Code Generators                     */
/* ========================================================================= */

int flow_abi_build_for_component(const SemanticIR *ir, const Component *comp, FlowComponentABI *abi);

int flow_abi_check_composition(const FlowComponentABI *producer,
                               const FlowComponentABI *consumer,
                               char *err_msg, size_t err_size);

/* Cross-Language Generators */
int flow_abi_emit_c_header(FILE *output, const FlowComponentABI *abi);
int flow_abi_emit_rust_adapter(FILE *output, const FlowComponentABI *abi);
int flow_abi_emit_python_adapter(FILE *output, const FlowComponentABI *abi);

#endif
