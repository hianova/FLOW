#include "abi.h"
#include "registry.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void sanitize_ident(const char *input, char *output, size_t max_len) {
    size_t j = 0;
    for (size_t i = 0; input[i] != '\0' && j + 1 < max_len; ++i) {
        char c = input[i];
        if (isalnum((unsigned char)c) || c == '_') {
            output[j++] = c;
        } else {
            output[j++] = '_';
        }
    }
    output[j] = '\0';
}

static void to_pascal_case(const char *input, char *output, size_t max_len) {
    size_t j = 0;
    int cap_next = 1;
    for (size_t i = 0; input[i] != '\0' && j + 1 < max_len; ++i) {
        char c = input[i];
        if (c == '_' || c == '-') {
            cap_next = 1;
        } else {
            if (cap_next) {
                output[j++] = (char)toupper((unsigned char)c);
                cap_next = 0;
            } else {
                output[j++] = c;
            }
        }
    }
    output[j] = '\0';
}

int flow_memory_view_init(FlowMemoryView *view, void *address, size_t length,
                          size_t alignment, int mutability, FlowOwnership ownership,
                          uint64_t lifetime_epoch, FlowMemoryMode mode) {
    if (view == NULL) return 0;
    memset(view, 0, sizeof(*view));
    view->address = address;
    view->length = length;
    view->alignment = alignment == 0 ? 1 : alignment;
    view->mutability = mutability ? 1 : 0;
    view->ownership = ownership;
    view->lifetime_epoch = lifetime_epoch;
    view->mode = mode;
    view->is_relocatable = (mode == FLOW_MEM_RELATIVE_ARCHIVE);
    view->is_validated = 0; /* Requires explicit bounds & layout validation */
    return 1;
}

int flow_memory_view_validate(const FlowMemoryView *view, size_t required_bytes,
                              size_t required_align, int require_mutable,
                              uint64_t min_epoch, char *err_msg, size_t err_size) {
    if (err_msg != NULL && err_size != 0) err_msg[0] = '\0';
    if (view == NULL) {
        if (err_msg && err_size) snprintf(err_msg, err_size, "null memory view");
        return 0;
    }
    if (view->address == NULL && required_bytes > 0) {
        if (err_msg && err_size) snprintf(err_msg, err_size, "null memory view address");
        return 0;
    }
    if (view->length < required_bytes) {
        if (err_msg && err_size) {
            snprintf(err_msg, err_size, "buffer underrun: view length %zu < required %zu bytes",
                     view->length, required_bytes);
        }
        return 0;
    }
    if (required_align > 0) {
        uintptr_t addr = (uintptr_t)view->address;
        if ((addr % required_align) != 0) {
            if (err_msg && err_size) {
                snprintf(err_msg, err_size, "alignment breach: address %p not aligned to %zu bytes",
                         view->address, required_align);
            }
            return 0;
        }
    }
    if (require_mutable && !view->mutability) {
        if (err_msg && err_size) {
            snprintf(err_msg, err_size, "mutability contract violation: required mutable access on read-only view");
        }
        return 0;
    }
    if (view->lifetime_epoch < min_epoch) {
        if (err_msg && err_size) {
            snprintf(err_msg, err_size, "lifetime epoch expired: view epoch %llu < required epoch %llu",
                     (unsigned long long)view->lifetime_epoch, (unsigned long long)min_epoch);
        }
        return 0;
    }
    if ((uintptr_t)view->address + required_bytes < (uintptr_t)view->address) {
        if (err_msg && err_size) snprintf(err_msg, err_size, "address wrap-around overflow");
        return 0;
    }
    ((FlowMemoryView *)view)->is_validated = 1;
    return 1;
}

int flow_memory_view_slice(const FlowMemoryView *parent, size_t offset, size_t length,
                           FlowMemoryView *subview_out, char *err_msg, size_t err_size) {
    if (err_msg != NULL && err_size != 0) err_msg[0] = '\0';
    if (parent == NULL || subview_out == NULL) {
        if (err_msg && err_size) snprintf(err_msg, err_size, "null parent or subview");
        return 0;
    }
    if (offset > parent->length || length > parent->length - offset) {
        if (err_msg && err_size) {
            snprintf(err_msg, err_size, "slice out of bounds: parent length %zu, requested [%zu, %zu)",
                     parent->length, offset, offset + length);
        }
        return 0;
    }
    void *sub_addr = (char *)parent->address + offset;
    memset(subview_out, 0, sizeof(*subview_out));
    subview_out->address = sub_addr;
    subview_out->length = length;
    subview_out->alignment = 1;
    subview_out->mutability = parent->mutability;
    subview_out->ownership = FLOW_OWN_BORROW;
    subview_out->lifetime_epoch = parent->lifetime_epoch;
    subview_out->is_relocatable = parent->is_relocatable;
    subview_out->is_validated = parent->is_validated;
    subview_out->mode = parent->mode;
    return 1;
}

int flow_memory_view_resolve_relptr(const FlowMemoryView *view, size_t relptr_offset,
                                    void **resolved_out, char *err_msg, size_t err_size) {
    if (err_msg != NULL && err_size != 0) err_msg[0] = '\0';
    if (resolved_out != NULL) *resolved_out = NULL;
    if (view == NULL || resolved_out == NULL) {
        if (err_msg && err_size) snprintf(err_msg, err_size, "null view or output ptr");
        return 0;
    }
    if (relptr_offset + sizeof(FlowRelPtr) > view->length) {
        if (err_msg && err_size) {
            snprintf(err_msg, err_size, "relptr offset %zu out of bounds (length %zu)",
                     relptr_offset, view->length);
        }
        return 0;
    }
    const FlowRelPtr *ptr = (const FlowRelPtr *)((const char *)view->address + relptr_offset);
    if (ptr->offset == 0) {
        *resolved_out = NULL;
        return 1;
    }
    void *target = flow_relptr_resolve(ptr);
    uintptr_t base = (uintptr_t)view->address;
    uintptr_t end = base + view->length;
    uintptr_t t_addr = (uintptr_t)target;
    if (t_addr < base || t_addr >= end) {
        if (err_msg && err_size) {
            snprintf(err_msg, err_size, "relptr target %p escapes memory view [%p, %p)",
                     target, (void *)base, (void *)end);
        }
        return 0;
    }
    *resolved_out = target;
    return 1;
}

int flow_view_contract_verify(const FlowViewContract *contract, const FlowMemoryView *view,
                              char *err_msg, size_t err_size) {
    if (err_msg != NULL && err_size != 0) err_msg[0] = '\0';
    if (contract == NULL || view == NULL) {
        if (err_msg && err_size) snprintf(err_msg, err_size, "null view or contract");
        return 0;
    }
    if (view->length < contract->min_length) {
        if (err_msg && err_size) {
            snprintf(err_msg, err_size, "view length %zu < contract min length %zu",
                     view->length, contract->min_length);
        }
        return 0;
    }
    if (contract->required_alignment > 0) {
        if (((uintptr_t)view->address % contract->required_alignment) != 0) {
            if (err_msg && err_size) {
                snprintf(err_msg, err_size, "view address %p violates required alignment %zu",
                         view->address, contract->required_alignment);
            }
            return 0;
        }
    }
    if (contract->require_mutable && !view->mutability) {
        if (err_msg && err_size) snprintf(err_msg, err_size, "view violates required mutable access");
        return 0;
    }
    if (view->lifetime_epoch < contract->required_lifetime_epoch) {
        if (err_msg && err_size) snprintf(err_msg, err_size, "view lifetime epoch expired");
        return 0;
    }
    if (contract->require_relocatable && !view->is_relocatable) {
        if (err_msg && err_size) snprintf(err_msg, err_size, "view must be position-independent / relocatable");
        return 0;
    }
    if (contract->require_prevalidated && !view->is_validated) {
        if (err_msg && err_size) snprintf(err_msg, err_size, "view must be pre-validated");
        return 0;
    }
    if (contract->expected_ownership == FLOW_OWN_MOVE && view->ownership == FLOW_OWN_BORROW) {
        if (err_msg && err_size) snprintf(err_msg, err_size, "cannot move borrowed memory view");
        return 0;
    }
    return 1;
}

int flow_aliasing_verify(const FlowMemoryView *in_view, const FlowMemoryView *out_view,
                         FlowAliasingPolicy policy, char *err_msg, size_t err_size) {
    if (err_msg != NULL && err_size != 0) err_msg[0] = '\0';
    if (in_view == NULL || out_view == NULL) {
        if (err_msg && err_size) snprintf(err_msg, err_size, "null input or output view");
        return 0;
    }

    uintptr_t in_start = (uintptr_t)in_view->address;
    uintptr_t in_end = in_start + in_view->length;
    uintptr_t out_start = (uintptr_t)out_view->address;
    uintptr_t out_end = out_start + out_view->length;

    if (policy == FLOW_ALIAS_DISJOINT) {
        uintptr_t max_start = in_start > out_start ? in_start : out_start;
        uintptr_t min_end = in_end < out_end ? in_end : out_end;
        if (max_start < min_end) {
            if (err_msg && err_size) {
                snprintf(err_msg, err_size,
                         "aliasing violation: input [%p, %p) and output [%p, %p) overlap",
                         (void *)in_start, (void *)in_end, (void *)out_start, (void *)out_end);
            }
            return 0;
        }
    } else if (policy == FLOW_ALIAS_INPLACE_EXACT) {
        if (in_start != out_start || in_view->length != out_view->length) {
            if (err_msg && err_size) {
                snprintf(err_msg, err_size, "in-place exact aliasing violation: input and output pointers differ");
            }
            return 0;
        }
    }
    return 1;
}

int flow_control_contract_verify(const FlowControlContract *callee,
                                 const FlowControlContract *caller_context,
                                 const FlowMemoryView *passed_view,
                                 char *err_msg, size_t err_size) {
    if (err_msg != NULL && err_size != 0) err_msg[0] = '\0';
    if (callee == NULL) return 0;

    /* 1. Pointer Escape on Borrowed View check */
    if ((callee->allows_pointer_escape || (callee->effects & FLOW_EFFECT_RETAIN_POINTER)) &&
        passed_view != NULL && passed_view->ownership == FLOW_OWN_BORROW) {
        if (err_msg && err_size) {
            snprintf(err_msg, err_size, "pointer escape violation: callee retains pointer on borrowed view");
        }
        return 0;
    }

    /* 2. Exception Unwind Safety across FFI Boundary */
    if ((callee->allows_exception_unwind || (callee->effects & FLOW_EFFECT_PANIC_OR_THROW)) &&
        caller_context != NULL && !caller_context->allows_exception_unwind) {
        if (err_msg && err_size) {
            snprintf(err_msg, err_size, "exception boundary violation: callee throws exception across non-unwind landing pad");
        }
        return 0;
    }

    /* 3. Reentrancy check */
    if (callee->reentrancy == FLOW_REENTRANCY_NON_REENTRANT &&
        caller_context != NULL && caller_context->reentrancy == FLOW_REENTRANCY_RECURSIVE_SAFE) {
        if (err_msg && err_size) {
            snprintf(err_msg, err_size, "reentrancy contract violation: non-reentrant callee in reentrant context");
        }
        return 0;
    }

    /* 4. Blocking call check */
    if ((callee->effects & FLOW_EFFECT_BLOCKING) &&
        caller_context != NULL && !(caller_context->effects & FLOW_EFFECT_BLOCKING) && caller_context->is_thread_affine) {
        if (err_msg && err_size) {
            snprintf(err_msg, err_size, "blocking contract violation: blocking callee called in non-blocking thread");
        }
        return 0;
    }

    return 1;
}

int flow_abi_build_for_component(const SemanticIR *ir, const Component *comp, FlowComponentABI *abi) {
    if (ir == NULL || comp == NULL || abi == NULL) return 0;
    memset(abi, 0, sizeof(*abi));

    strncpy(abi->component_id, comp->id, sizeof(abi->component_id) - 1);
    const FlowPlugin *plugin = flow_component_plugin(comp);
    if (plugin != NULL) {
        strncpy(abi->module_name, plugin->name, sizeof(abi->module_name) - 1);
        strncpy(abi->version, plugin->version, sizeof(abi->version) - 1);
    } else {
        strncpy(abi->module_name, "builtin", sizeof(abi->module_name) - 1);
        strncpy(abi->version, "1.0.0", sizeof(abi->version) - 1);
    }

    abi->thread_safe = strcmp(comp->capability, "pthread") == 0 || comp->supports_parallelizable;
    abi->max_memory_bytes = ir->memory_limit_mb > 0 ? (size_t)ir->memory_limit_mb * 1024 * 1024 : 64 * 1024 * 1024;
    abi->default_memory_mode = FLOW_MEM_RAW_VIEW;

    /* Formal Control Contract */
    abi->control_contract.reentrancy = ir->state_shared ? FLOW_REENTRANCY_THREAD_SAFE : FLOW_REENTRANCY_PURE;
    abi->control_contract.effects = ir->state_shared ? (FLOW_EFFECT_READS_STATE | FLOW_EFFECT_WRITES_STATE) : FLOW_EFFECT_PURE;
    if (strcmp(comp->capability, "pthread") == 0) {
        abi->control_contract.effects |= FLOW_EFFECT_BLOCKING;
    }
    abi->control_contract.error_policy = FLOW_ERR_STATUS_CODE;
    abi->control_contract.allows_pointer_escape = 0;
    abi->control_contract.allows_exception_unwind = 0;
    abi->control_contract.is_thread_affine = 0;

    /* Formal View Contracts */
    size_t element_size = sizeof(uint32_t);
    size_t min_bytes = (size_t)(ir->input_max_count > 0 ? ir->input_max_count : 1) * element_size;
    abi->input_view_contract.mode = FLOW_MEM_RAW_VIEW;
    abi->input_view_contract.min_length = min_bytes;
    abi->input_view_contract.required_alignment = 8;
    abi->input_view_contract.require_mutable = 0;
    abi->input_view_contract.expected_ownership = FLOW_OWN_BORROW;
    abi->input_view_contract.aliasing_policy = FLOW_ALIAS_DISJOINT;

    abi->output_view_contract.mode = FLOW_MEM_RAW_VIEW;
    abi->output_view_contract.min_length = min_bytes;
    abi->output_view_contract.required_alignment = 8;
    abi->output_view_contract.require_mutable = 1;
    abi->output_view_contract.expected_ownership = FLOW_OWN_MUT_BORROW;
    abi->output_view_contract.aliasing_policy = FLOW_ALIAS_DISJOINT;

    char id_safe[64];
    sanitize_ident(comp->id, id_safe, sizeof(id_safe));

    /* 1. Create function */
    snprintf(abi->create_fn.name, sizeof(abi->create_fn.name), "flow_%s_create", id_safe);
    abi->create_fn.return_type = (FlowType){FLOW_TYPE_OPAQUE_HANDLE, "FlowHandle", sizeof(void*), sizeof(void*), NULL, 0};
    abi->create_fn.return_ownership = FLOW_OWN_RETURN_MOVE;
    abi->create_fn.error_policy = FLOW_ERR_NULLABLE;
    abi->create_fn.effects = FLOW_EFFECT_ALLOC;
    snprintf(abi->create_fn.docstring, sizeof(abi->create_fn.docstring), "Allocates and initializes %s instance.", comp->id);

    /* 2. Destroy function */
    snprintf(abi->destroy_fn.name, sizeof(abi->destroy_fn.name), "flow_%s_destroy", id_safe);
    abi->destroy_fn.param_count = 1;
    strncpy(abi->destroy_fn.params[0].name, "handle", sizeof(abi->destroy_fn.params[0].name) - 1);
    abi->destroy_fn.params[0].type = (FlowType){FLOW_TYPE_OPAQUE_HANDLE, "FlowHandle", sizeof(void*), sizeof(void*), NULL, 0};
    abi->destroy_fn.params[0].ownership = FLOW_OWN_MOVE;
    abi->destroy_fn.return_type = (FlowType){FLOW_TYPE_VOID, "void", 0, 0, NULL, 0};
    abi->destroy_fn.return_ownership = FLOW_OWN_BORROW;
    abi->destroy_fn.error_policy = FLOW_ERR_STATUS_CODE;
    abi->destroy_fn.effects = FLOW_EFFECT_WRITES_STATE;
    snprintf(abi->destroy_fn.docstring, sizeof(abi->destroy_fn.docstring), "Releases %s instance and all associated resources.", comp->id);

    /* 3. Primary processing function */
    snprintf(abi->primary_fn.name, sizeof(abi->primary_fn.name), "flow_%s_process", id_safe);
    abi->primary_fn.param_count = 3;

    /* Param 0: handle */
    strncpy(abi->primary_fn.params[0].name, "handle", sizeof(abi->primary_fn.params[0].name) - 1);
    abi->primary_fn.params[0].type = (FlowType){FLOW_TYPE_OPAQUE_HANDLE, "FlowHandle", sizeof(void*), sizeof(void*), NULL, 0};
    abi->primary_fn.params[0].ownership = ir->state_shared ? FLOW_OWN_MUT_BORROW : FLOW_OWN_BORROW;

    /* Param 1: input data */
    strncpy(abi->primary_fn.params[1].name, "input", sizeof(abi->primary_fn.params[1].name) - 1);
    abi->primary_fn.params[1].type = (FlowType){FLOW_TYPE_BYTES, "const uint8_t*", sizeof(void*), sizeof(void*), NULL, (size_t)ir->input_max_count};
    abi->primary_fn.params[1].ownership = FLOW_OWN_BORROW;

    /* Param 2: output data */
    strncpy(abi->primary_fn.params[2].name, "output", sizeof(abi->primary_fn.params[2].name) - 1);
    abi->primary_fn.params[2].type = (FlowType){FLOW_TYPE_BYTES, "uint8_t*", sizeof(void*), sizeof(void*), NULL, 0};
    abi->primary_fn.params[2].ownership = FLOW_OWN_MUT_BORROW;

    abi->primary_fn.return_type = (FlowType){FLOW_TYPE_I32, "int32_t", 4, 4, NULL, 0};
    abi->primary_fn.return_ownership = FLOW_OWN_BORROW;
    abi->primary_fn.error_policy = FLOW_ERR_STATUS_CODE;
    abi->primary_fn.effects = abi->control_contract.effects;
    snprintf(abi->primary_fn.docstring, sizeof(abi->primary_fn.docstring), "Executes %s flow pipeline transformation.", comp->id);

    return 1;
}

int flow_abi_check_composition(const FlowComponentABI *producer,
                               const FlowComponentABI *consumer,
                               char *err_msg, size_t err_size) {
    if (err_msg != NULL && err_size != 0) err_msg[0] = '\0';
    if (producer == NULL || consumer == NULL) {
        if (err_msg && err_size) snprintf(err_msg, err_size, "null producer or consumer ABI");
        return 0;
    }

    /* Check ownership transfer safety */
    if (producer->primary_fn.return_ownership == FLOW_OWN_BORROW) {
        for (size_t p = 0; p < consumer->primary_fn.param_count; ++p) {
            if (consumer->primary_fn.params[p].ownership == FLOW_OWN_MOVE) {
                if (err_msg && err_size) {
                    snprintf(err_msg, err_size,
                             "ownership violation: consumer '%s' consumes (move) param '%s' but producer '%s' returns borrowed ref",
                             consumer->component_id, consumer->primary_fn.params[p].name, producer->component_id);
                }
                return 0;
            }
        }
    }

    /* Check Control & Effect Contracts */
    if (!flow_control_contract_verify(&consumer->control_contract, &producer->control_contract, NULL, err_msg, err_size)) {
        return 0;
    }

    return 1;
}

int flow_abi_emit_c_header(FILE *output, const FlowComponentABI *abi) {
    if (output == NULL || abi == NULL) return 0;
    char id_upper[64];
    char id_safe[64];
    sanitize_ident(abi->component_id, id_safe, sizeof(id_safe));
    for (size_t i = 0; id_safe[i] != '\0' && i + 1 < sizeof(id_upper); ++i) {
        id_upper[i] = (char)toupper((unsigned char)id_safe[i]);
        id_upper[i + 1] = '\0';
    }

    fprintf(output, "/* Auto-generated by FLOW Language-Neutral Component ABI */\n");
    fprintf(output, "/* Component: %s, Module: %s, Version: %s */\n", abi->component_id, abi->module_name, abi->version);
    fprintf(output, "/* Thread-Safe: %s, Memory Bound: %zu bytes */\n\n", abi->thread_safe ? "YES" : "NO", abi->max_memory_bytes);
    fprintf(output, "#ifndef FLOW_ABI_%s_H\n", id_upper);
    fprintf(output, "#define FLOW_ABI_%s_H\n\n", id_upper);
    fprintf(output, "#include <stdint.h>\n#include <stddef.h>\n#include <stdbool.h>\n\n");
    fprintf(output, "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n");

    fprintf(output, "typedef void* %s_Handle;\n\n", id_safe);

    /* Zero-copy Memory View representation in C */
    fprintf(output, "typedef struct {\n"
                    "    void *address;\n"
                    "    size_t length;\n"
                    "    size_t alignment;\n"
                    "    int mutability;\n"
                    "    int ownership;\n"
                    "    uint64_t lifetime_epoch;\n"
                    "    int is_relocatable;\n"
                    "    int is_validated;\n"
                    "    int mode;\n"
                    "} %s_MemoryView;\n\n", id_safe);

    /* create_fn */
    fprintf(output, "/**\n * %s\n * @return [own_transfer] Opaque instance handle or NULL on failure\n */\n", abi->create_fn.docstring);
    fprintf(output, "%s_Handle %s(void);\n\n", id_safe, abi->create_fn.name);

    /* primary_fn */
    fprintf(output, "/**\n * %s\n * @param handle [in,%s] Instance handle\n",
            abi->primary_fn.docstring, abi->primary_fn.params[0].ownership == FLOW_OWN_MUT_BORROW ? "mut_borrow" : "borrow");
    fprintf(output, " * @param input [in,borrow] Input buffer pointer\n");
    fprintf(output, " * @param input_len Length of input buffer\n");
    fprintf(output, " * @param output [out,mut_borrow] Output buffer pointer\n");
    fprintf(output, " * @param output_len In: capacity, Out: written bytes\n");
    fprintf(output, " * @return 0 on success, negative error code on failure\n */\n");
    fprintf(output, "int32_t %s(%s_Handle handle, const uint8_t *input, size_t input_len, uint8_t *output, size_t *output_len);\n\n",
            abi->primary_fn.name, id_safe);

    /* destroy_fn */
    fprintf(output, "/**\n * %s\n * @param handle [in,consumed_move] Instance handle to release\n */\n", abi->destroy_fn.docstring);
    fprintf(output, "void %s(%s_Handle handle);\n\n", abi->destroy_fn.name, id_safe);

    fprintf(output, "#ifdef __cplusplus\n}\n#endif\n\n#endif /* FLOW_ABI_%s_H */\n", id_upper);
    return ferror(output) == 0;
}

int flow_abi_emit_rust_adapter(FILE *output, const FlowComponentABI *abi) {
    if (output == NULL || abi == NULL) return 0;
    char pascal_name[64];
    char id_safe[64];
    sanitize_ident(abi->component_id, id_safe, sizeof(id_safe));
    to_pascal_case(abi->component_id, pascal_name, sizeof(pascal_name));

    fprintf(output, "// Auto-generated by FLOW Language-Neutral Component ABI\n");
    fprintf(output, "// Component: %s, Module: %s, Version: %s\n", abi->component_id, abi->module_name, abi->version);
    fprintf(output, "// Zero-Copy Memory Contract: raw slice (&[u8]), relative archive, arena\n");
    fprintf(output, "// Thread-Safe: %s, Memory Bound: %zu bytes\n\n", abi->thread_safe ? "true" : "false", abi->max_memory_bytes);
    fprintf(output, "use std::os::raw::{c_int, c_void};\n\n");

    fprintf(output, "#[repr(C)]\n#[derive(Debug, Copy, Clone)]\npub struct FlowHandle(*mut c_void);\n\n");

    fprintf(output, "extern \"C\" {\n");
    fprintf(output, "    fn %s() -> FlowHandle;\n", abi->create_fn.name);
    fprintf(output, "    fn %s(handle: FlowHandle, input: *const u8, input_len: usize, output: *mut u8, output_len: *mut usize) -> c_int;\n", abi->primary_fn.name);
    fprintf(output, "    fn %s(handle: FlowHandle);\n", abi->destroy_fn.name);
    fprintf(output, "}\n\n");

    fprintf(output, "/// Safe RAII wrapper for %s component with automatic drop and zero-copy lifetime safety.\npub struct %s {\n", abi->component_id, pascal_name);
    fprintf(output, "    handle: FlowHandle,\n}\n\n");

    fprintf(output, "impl %s {\n", pascal_name);
    fprintf(output, "    /// Creates a new instance of %s.\n", pascal_name);
    fprintf(output, "    pub fn new() -> Result<Self, c_int> {\n");
    fprintf(output, "        let h = unsafe { %s() };\n", abi->create_fn.name);
    fprintf(output, "        if h.0.is_null() {\n            Err(-1)\n        } else {\n            Ok(Self { handle: h })\n        }\n    }\n\n");

    fprintf(output, "    /// Executes processing transformation with zero-copy borrowed slice (&[u8]).\n");
    fprintf(output, "    pub fn process_slice(&%sself, input: &[u8], output: &mut [u8]) -> Result<usize, c_int> {\n",
            abi->primary_fn.params[0].ownership == FLOW_OWN_MUT_BORROW ? "mut " : "");
    fprintf(output, "        let mut out_len = output.len();\n");
    fprintf(output, "        let res = unsafe {\n");
    fprintf(output, "            %s(self.handle, input.as_ptr(), input.len(), output.as_mut_ptr(), &mut out_len)\n", abi->primary_fn.name);
    fprintf(output, "        };\n");
    fprintf(output, "        if res == 0 { Ok(out_len) } else { Err(res) }\n");
    fprintf(output, "    }\n\n");

    fprintf(output, "    /// Convenience zero-copy buffer allocator helper.\n");
    fprintf(output, "    pub fn process(&%sself, input: &[u8]) -> Result<Vec<u8>, c_int> {\n",
            abi->primary_fn.params[0].ownership == FLOW_OWN_MUT_BORROW ? "mut " : "");
    fprintf(output, "        let mut out_buf = vec![0u8; %zu];\n", abi->max_memory_bytes > 0 ? abi->max_memory_bytes : 65536);
    fprintf(output, "        let mut out_len = out_buf.len();\n");
    fprintf(output, "        let res = unsafe {\n");
    fprintf(output, "            %s(self.handle, input.as_ptr(), input.len(), out_buf.as_mut_ptr(), &mut out_len)\n", abi->primary_fn.name);
    fprintf(output, "        };\n");
    fprintf(output, "        if res == 0 {\n            out_buf.truncate(out_len);\n            Ok(out_buf)\n        } else {\n            Err(res)\n        }\n    }\n}\n\n");

    fprintf(output, "impl Drop for %s {\n", pascal_name);
    fprintf(output, "    fn drop(&mut self) {\n");
    fprintf(output, "        if !self.handle.0.is_null() {\n");
    fprintf(output, "            unsafe { %s(self.handle); }\n", abi->destroy_fn.name);
    fprintf(output, "            self.handle.0 = std::ptr::null_mut();\n");
    fprintf(output, "        }\n    }\n}\n\n");

    if (abi->thread_safe) {
        fprintf(output, "unsafe impl Send for %s {}\n", pascal_name);
        fprintf(output, "unsafe impl Sync for %s {}\n", pascal_name);
    }

    return ferror(output) == 0;
}

int flow_abi_emit_python_adapter(FILE *output, const FlowComponentABI *abi) {
    if (output == NULL || abi == NULL) return 0;
    char pascal_name[64];
    char id_safe[64];
    sanitize_ident(abi->component_id, id_safe, sizeof(id_safe));
    to_pascal_case(abi->component_id, pascal_name, sizeof(pascal_name));

    fprintf(output, "# Auto-generated by FLOW Language-Neutral Component ABI\n");
    fprintf(output, "# Component: %s, Module: %s, Version: %s\n", abi->component_id, abi->module_name, abi->version);
    fprintf(output, "# Zero-Copy Data Plane: Python Buffer Protocol (memoryview) & Raw Pointer View\n");
    fprintf(output, "# Thread-Safe: %s, Memory Bound: %zu bytes\n\n", abi->thread_safe ? "True" : "False", abi->max_memory_bytes);
    fprintf(output, "import ctypes\nimport os\n\n");

    fprintf(output, "class %s:\n", pascal_name);
    fprintf(output, "    \"\"\"FLOW Component Adapter for %s with zero-copy memoryview & lifetime safety.\"\"\"\n", abi->component_id);
    fprintf(output, "    def __init__(self, lib_path=None):\n");
    fprintf(output, "        if lib_path is None:\n");
    fprintf(output, "            lib_path = os.environ.get('FLOW_LIB_PATH', './libflow.dylib' if os.name == 'posix' else './libflow.so')\n");
    fprintf(output, "        self._lib = ctypes.CDLL(lib_path)\n");
    fprintf(output, "        self._lib.%s.restype = ctypes.c_void_p\n", abi->create_fn.name);
    fprintf(output, "        self._lib.%s.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.POINTER(ctypes.c_size_t)]\n", abi->primary_fn.name);
    fprintf(output, "        self._lib.%s.restype = ctypes.c_int32\n", abi->primary_fn.name);
    fprintf(output, "        self._lib.%s.argtypes = [ctypes.c_void_p]\n", abi->destroy_fn.name);
    fprintf(output, "        self._lib.%s.restype = None\n", abi->destroy_fn.name);
    fprintf(output, "        self._handle = self._lib.%s()\n", abi->create_fn.name);
    fprintf(output, "        if not self._handle:\n");
    fprintf(output, "            raise RuntimeError(\"Failed to instantiate %s via FLOW ABI\")\n\n", abi->component_id);

    fprintf(output, "    def __enter__(self):\n        return self\n\n");
    fprintf(output, "    def __exit__(self, exc_type, exc_val, exc_tb):\n        self.close()\n\n");

    fprintf(output, "    def process_view(self, input_view, output_view) -> int:\n");
    fprintf(output, "        \"\"\"Zero-copy memoryview transformation without data copying.\"\"\"\n");
    fprintf(output, "        if not self._handle:\n            raise RuntimeError(\"Instance already closed\")\n");
    fprintf(output, "        in_mv = memoryview(input_view)\n");
    fprintf(output, "        out_mv = memoryview(output_view)\n");
    fprintf(output, "        if in_mv.readonly:\n");
    fprintf(output, "            in_buf = (ctypes.c_char * len(in_mv)).from_buffer_copy(in_mv)\n");
    fprintf(output, "            in_ptr = ctypes.cast(in_buf, ctypes.c_char_p)\n");
    fprintf(output, "        else:\n");
    fprintf(output, "            in_ptr = ctypes.c_char_p(ctypes.addressof(ctypes.c_char.from_buffer(in_mv)))\n");
    fprintf(output, "        out_ptr = ctypes.c_char_p(ctypes.addressof(ctypes.c_char.from_buffer(out_mv)))\n");
    fprintf(output, "        out_len = ctypes.c_size_t(len(out_mv))\n");
    fprintf(output, "        status = self._lib.%s(self._handle, in_ptr, len(in_mv), out_ptr, ctypes.byref(out_len))\n", abi->primary_fn.name);
    fprintf(output, "        if status != 0:\n            raise RuntimeError(f\"FLOW error status: {status}\")\n");
    fprintf(output, "        return out_len.value\n\n");
    fprintf(output, "    def process(self, input_bytes: bytes) -> bytes:\n");
    fprintf(output, "        if not self._handle:\n            raise RuntimeError(\"Instance already closed\")\n");
    fprintf(output, "        max_len = %zu\n", abi->max_memory_bytes > 0 ? abi->max_memory_bytes : 65536);
    fprintf(output, "        out_buf = bytearray(max_len)\n");
    fprintf(output, "        written = self.process_view(input_bytes, out_buf)\n");
    fprintf(output, "        return bytes(out_buf[:written])\n\n");

    fprintf(output, "    def close(self):\n");
    fprintf(output, "        if self._handle:\n");
    fprintf(output, "            self._lib.%s(self._handle)\n", abi->destroy_fn.name);
    fprintf(output, "            self._handle = None\n\n");

    fprintf(output, "    def __del__(self):\n        self.close()\n");

    return ferror(output) == 0;
}
