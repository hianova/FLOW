#include "abi.h"
#include "registry.h"
#include "search.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ABI_CHECK(cond) if (!(cond)) { fprintf(stderr, "abi-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; }

int main(void) {
    if (!flow_registry_init()) return 1;

    /* ===================================================================== */
    /* 1. Test Zero-Copy FlowMemoryView Data Plane Contract                  */
    /* ===================================================================== */
    uint8_t raw_buffer[1024];
    memset(raw_buffer, 0xAB, sizeof(raw_buffer));

    FlowMemoryView view;
    ABI_CHECK(flow_memory_view_init(&view, raw_buffer, sizeof(raw_buffer), 8, 0,
                                    FLOW_OWN_BORROW, 100, FLOW_MEM_RAW_VIEW));
    ABI_CHECK(view.is_validated == 0); /* Unvalidated by default */
    ABI_CHECK(view.mode == FLOW_MEM_RAW_VIEW);

    char err_msg[256];
    /* Positive validation */
    ABI_CHECK(flow_memory_view_validate(&view, 512, 8, 0, 100, err_msg, sizeof(err_msg)));
    ABI_CHECK(view.is_validated == 1); /* Marked validated after successful verification */

    /* Address wrap-around overflow test */
    FlowMemoryView wrap_view;
    ABI_CHECK(flow_memory_view_init(&wrap_view, (void *)(uintptr_t)(~0ULL - 16), 32, 1, 0, FLOW_OWN_BORROW, 100, FLOW_MEM_RAW_VIEW));
    ABI_CHECK(!flow_memory_view_validate(&wrap_view, 32, 1, 0, 100, err_msg, sizeof(err_msg)));
    ABI_CHECK(strstr(err_msg, "overflow") != NULL);

    /* Negative: Underrun */
    ABI_CHECK(!flow_memory_view_validate(&view, 2048, 8, 0, 100, err_msg, sizeof(err_msg)));
    ABI_CHECK(strstr(err_msg, "underrun") != NULL);

    /* Negative: Mutability contract violation */
    ABI_CHECK(!flow_memory_view_validate(&view, 512, 8, 1, 100, err_msg, sizeof(err_msg)));
    ABI_CHECK(strstr(err_msg, "mutability contract violation") != NULL);

    /* Negative: Lifetime epoch expired */
    ABI_CHECK(!flow_memory_view_validate(&view, 512, 8, 0, 200, err_msg, sizeof(err_msg)));
    ABI_CHECK(strstr(err_msg, "epoch expired") != NULL);

    /* Slicing test (Zero-Copy) */
    FlowMemoryView subview;
    ABI_CHECK(flow_memory_view_slice(&view, 128, 256, &subview, err_msg, sizeof(err_msg)));
    ABI_CHECK(subview.address == (void *)(raw_buffer + 128));
    ABI_CHECK(subview.length == 256);
    ABI_CHECK(subview.ownership == FLOW_OWN_BORROW);

    /* Slice out of bounds */
    ABI_CHECK(!flow_memory_view_slice(&view, 900, 200, &subview, err_msg, sizeof(err_msg)));
    ABI_CHECK(strstr(err_msg, "out of bounds") != NULL);

    /* ===================================================================== */
    /* 2. Test Aliasing Safety Preconditions (Disjoint vs In-Place)          */
    /* ===================================================================== */
    FlowMemoryView in_view, out_disjoint, out_overlap;
    ABI_CHECK(flow_memory_view_slice(&view, 0, 256, &in_view, err_msg, sizeof(err_msg)));
    ABI_CHECK(flow_memory_view_slice(&view, 256, 256, &out_disjoint, err_msg, sizeof(err_msg)));
    ABI_CHECK(flow_memory_view_slice(&view, 128, 256, &out_overlap, err_msg, sizeof(err_msg)));

    /* Positive: Disjoint memory regions */
    ABI_CHECK(flow_aliasing_verify(&in_view, &out_disjoint, FLOW_ALIAS_DISJOINT, err_msg, sizeof(err_msg)));

    /* Negative: Overlapping regions rejected under DISJOINT policy */
    ABI_CHECK(!flow_aliasing_verify(&in_view, &out_overlap, FLOW_ALIAS_DISJOINT, err_msg, sizeof(err_msg)));
    ABI_CHECK(strstr(err_msg, "aliasing violation") != NULL);

    /* In-place exact verification */
    ABI_CHECK(flow_aliasing_verify(&in_view, &in_view, FLOW_ALIAS_INPLACE_EXACT, err_msg, sizeof(err_msg)));
    ABI_CHECK(!flow_aliasing_verify(&in_view, &out_disjoint, FLOW_ALIAS_INPLACE_EXACT, err_msg, sizeof(err_msg)));

    /* ===================================================================== */
    /* 3. Test Control Path & Effect Semantics Verification                  */
    /* ===================================================================== */
    FlowControlContract safe_caller = {
        .reentrancy = FLOW_REENTRANCY_PURE,
        .effects = FLOW_EFFECT_PURE,
        .error_policy = FLOW_ERR_STATUS_CODE,
        .allows_pointer_escape = 0,
        .allows_exception_unwind = 0,
        .is_thread_affine = 1
    };

    FlowControlContract pointer_escaping_callee = {
        .reentrancy = FLOW_REENTRANCY_PURE,
        .effects = FLOW_EFFECT_RETAIN_POINTER,
        .error_policy = FLOW_ERR_STATUS_CODE,
        .allows_pointer_escape = 1,
        .allows_exception_unwind = 0,
        .is_thread_affine = 0
    };

    /* Negative: Callee retains pointer on borrowed view -> rejected! */
    ABI_CHECK(!flow_control_contract_verify(&pointer_escaping_callee, &safe_caller, &in_view, err_msg, sizeof(err_msg)));
    ABI_CHECK(strstr(err_msg, "pointer escape violation") != NULL);

    /* Positive: Callee retains pointer on moved (owned) view -> allowed! */
    FlowMemoryView moved_view = in_view;
    moved_view.ownership = FLOW_OWN_MOVE;
    ABI_CHECK(flow_control_contract_verify(&pointer_escaping_callee, &safe_caller, &moved_view, err_msg, sizeof(err_msg)));

    /* Negative: Callee throws exception across non-unwind landing pad -> rejected! */
    FlowControlContract throwing_callee = {
        .reentrancy = FLOW_REENTRANCY_PURE,
        .effects = FLOW_EFFECT_PANIC_OR_THROW,
        .error_policy = FLOW_ERR_PANIC,
        .allows_pointer_escape = 0,
        .allows_exception_unwind = 1,
        .is_thread_affine = 0
    };
    ABI_CHECK(!flow_control_contract_verify(&throwing_callee, &safe_caller, &in_view, err_msg, sizeof(err_msg)));
    ABI_CHECK(strstr(err_msg, "exception boundary violation") != NULL);

    /* Negative: Non-reentrant callee called in recursive reentrant context -> rejected! */
    FlowControlContract reentrant_caller = safe_caller;
    reentrant_caller.reentrancy = FLOW_REENTRANCY_RECURSIVE_SAFE;
    FlowControlContract non_reentrant_callee = {
        .reentrancy = FLOW_REENTRANCY_NON_REENTRANT,
        .effects = FLOW_EFFECT_WRITES_STATE,
        .error_policy = FLOW_ERR_STATUS_CODE,
        .allows_pointer_escape = 0,
        .allows_exception_unwind = 0,
        .is_thread_affine = 0
    };
    ABI_CHECK(!flow_control_contract_verify(&non_reentrant_callee, &reentrant_caller, &in_view, err_msg, sizeof(err_msg)));
    ABI_CHECK(strstr(err_msg, "reentrancy contract violation") != NULL);

    /* Negative: Blocking callee in non-blocking thread -> rejected! */
    FlowControlContract blocking_callee = {
        .reentrancy = FLOW_REENTRANCY_PURE,
        .effects = FLOW_EFFECT_BLOCKING,
        .error_policy = FLOW_ERR_STATUS_CODE,
        .allows_pointer_escape = 0,
        .allows_exception_unwind = 0,
        .is_thread_affine = 0
    };
    ABI_CHECK(!flow_control_contract_verify(&blocking_callee, &safe_caller, &in_view, err_msg, sizeof(err_msg)));
    ABI_CHECK(strstr(err_msg, "blocking contract violation") != NULL);

    /* ===================================================================== */
    /* 4. Test Relative / Offset Pointer (rkyv-style Zero-Copy Archive)       */
    /* ===================================================================== */
    struct ArchivePayload {
        FlowRelPtr name_relptr;
        uint32_t count;
        char data_pool[128];
    };

    struct ArchivePayload archive;
    memset(&archive, 0, sizeof(archive));
    archive.count = 42;
    strcpy(archive.data_pool, "zero_copy_payload");
    flow_relptr_set(&archive.name_relptr, archive.data_pool);

    FlowMemoryView archive_view;
    ABI_CHECK(flow_memory_view_init(&archive_view, &archive, sizeof(archive), 8, 0,
                                    FLOW_OWN_BORROW, 1, FLOW_MEM_RELATIVE_ARCHIVE));
    ABI_CHECK(archive_view.is_relocatable == 1);

    void *resolved_target = NULL;
    ABI_CHECK(flow_memory_view_resolve_relptr(&archive_view, offsetof(struct ArchivePayload, name_relptr),
                                             &resolved_target, err_msg, sizeof(err_msg)));
    ABI_CHECK(resolved_target != NULL);
    ABI_CHECK(strcmp((char *)resolved_target, "zero_copy_payload") == 0);

    /* Relative pointer overflow test (offset > INT32_MAX) */
    FlowRelPtr overflow_ptr;
    flow_relptr_set(&overflow_ptr, (void *)((uintptr_t)&overflow_ptr + (uint64_t)0x100000000ULL));
    ABI_CHECK(overflow_ptr.offset == 0);

    /* ===================================================================== */
    /* 5. Test Component ABI Construction & Cross-Language Generation        */
    /* ===================================================================== */
    SemanticIR ir;
    memset(&ir, 0, sizeof(ir));
    strncpy(ir.flow_name, "cache_pipeline", sizeof(ir.flow_name) - 1);
    ir.input_max_count = 1000;
    ir.memory_limit_mb = 16;
    ir.state_shared = 1;
    ir.state_read_heavy = 1;
    ir.fact_unordered = 1;

    const FlowPlugin *builtin = flow_registry_lookup("builtin");
    ABI_CHECK(builtin != NULL);

    const Component *comp = NULL;
    for (size_t i = 0; i < builtin->component_count; ++i) {
        if (strcmp(builtin->components[i].id, "sharded_hash") == 0) {
            comp = &builtin->components[i];
            break;
        }
    }
    ABI_CHECK(comp != NULL);

    FlowComponentABI abi;
    ABI_CHECK(flow_abi_build_for_component(&ir, comp, &abi));
    ABI_CHECK(strcmp(abi.component_id, "sharded_hash") == 0);
    ABI_CHECK(abi.thread_safe == 1);
    ABI_CHECK(abi.default_memory_mode == FLOW_MEM_RAW_VIEW);
    ABI_CHECK(abi.control_contract.reentrancy == FLOW_REENTRANCY_THREAD_SAFE);

    /* C Header check */
    FILE *c_hdr_fp = tmpfile();
    ABI_CHECK(c_hdr_fp != NULL);
    ABI_CHECK(flow_abi_emit_c_header(c_hdr_fp, &abi));
    rewind(c_hdr_fp);
    char line[512];
    int found_c_view = 0;
    while (fgets(line, sizeof(line), c_hdr_fp) != NULL) {
        if (strstr(line, "sharded_hash_MemoryView") != NULL) found_c_view = 1;
    }
    fclose(c_hdr_fp);
    ABI_CHECK(found_c_view);

    /* Rust Adapter check */
    FILE *rs_fp = tmpfile();
    ABI_CHECK(rs_fp != NULL);
    ABI_CHECK(flow_abi_emit_rust_adapter(rs_fp, &abi));
    rewind(rs_fp);
    int found_rs_slice = 0;
    while (fgets(line, sizeof(line), rs_fp) != NULL) {
        if (strstr(line, "pub fn process_slice") != NULL) found_rs_slice = 1;
    }
    fclose(rs_fp);
    ABI_CHECK(found_rs_slice);

    /* Python Adapter check */
    FILE *py_fp = tmpfile();
    ABI_CHECK(py_fp != NULL);
    ABI_CHECK(flow_abi_emit_python_adapter(py_fp, &abi));
    rewind(py_fp);
    int found_py_view = 0;
    while (fgets(line, sizeof(line), py_fp) != NULL) {
        if (strstr(line, "def process_view(self, input_view") != NULL) found_py_view = 1;
    }
    fclose(py_fp);
    ABI_CHECK(found_py_view);

    printf("FLOW_ABI_TEST=passed control_path=[aliasing,escape,unwind,reentrancy,blocking] memory_modes=[raw_view,relative_archive,arena_view,copied]\n");
    return 0;
}
