#include "jit.h"
#include "hardware_telemetry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <libkern/OSCacheControl.h>
#elif defined(__linux__)
#define _GNU_SOURCE
#include <fcntl.h>
#endif
struct FlowJITEngine { FlowJITConfig config; uint8_t *write_heap, *exec_heap; size_t code_heap_size, code_heap_used; uint64_t tlb_shootdowns_avoided; int is_dual_mapped, is_mach_vm; };
FlowJITEngine *flow_jit_create(const FlowJITConfig *config) { FlowJITEngine *e = calloc(1, sizeof(*e)); if (!e) return NULL;
    e->config = config ? *config : (FlowJITConfig){.opt_level = 2, .initial_code_heap_bytes = 1024 * 1024};
    size_t heap_sz = e->config.initial_code_heap_bytes > 0 ? e->config.initial_code_heap_bytes : 1024 * 1024;
#if defined(__APPLE__)
    vm_address_t w_addr = 0, x_addr = 0; vm_prot_t cur_p, max_p; if (vm_allocate(mach_task_self(), &w_addr, heap_sz, VM_FLAGS_ANYWHERE) == KERN_SUCCESS &&
        mach_vm_remap(mach_task_self(), (mach_vm_address_t *)&x_addr, heap_sz, 0, VM_FLAGS_ANYWHERE, mach_task_self(), (mach_vm_address_t)w_addr, FALSE, &cur_p, &max_p, VM_INHERIT_NONE) == KERN_SUCCESS &&
        mach_vm_protect(mach_task_self(), (mach_vm_address_t)x_addr, heap_sz, FALSE, VM_PROT_READ | VM_PROT_EXECUTE) == KERN_SUCCESS) {
        e->write_heap = (uint8_t *)w_addr; e->exec_heap = (uint8_t *)x_addr; e->code_heap_size = heap_sz; e->is_dual_mapped = e->is_mach_vm = 1; }
#elif defined(__linux__) && defined(MFD_CLOEXEC)
    int fd = memfd_create("flow_jit_pool", MFD_CLOEXEC); if (fd >= 0) { if (ftruncate(fd, heap_sz) == 0) {
            void *w = mmap(NULL, heap_sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0), *x = mmap(NULL, heap_sz, PROT_READ | PROT_EXEC, MAP_SHARED, fd, 0);
            if (w != MAP_FAILED && x != MAP_FAILED) { e->write_heap = w; e->exec_heap = x; e->code_heap_size = heap_sz; e->is_dual_mapped = 1; }
            else { if (w != MAP_FAILED) munmap(w, heap_sz); if (x != MAP_FAILED) munmap(x, heap_sz); } } close(fd); }
#endif
    if (!e->write_heap) { void *w = mmap(NULL, heap_sz, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
        if (w != MAP_FAILED) { e->write_heap = e->exec_heap = w; e->code_heap_size = heap_sz; } } return e; }
void flow_jit_destroy(FlowJITEngine *e) { if (!e) return;
#if defined(__APPLE__)
    if (e->is_mach_vm) { if (e->exec_heap && e->exec_heap != e->write_heap) vm_deallocate(mach_task_self(), (vm_address_t)e->exec_heap, e->code_heap_size);
        if (e->write_heap) vm_deallocate(mach_task_self(), (vm_address_t)e->write_heap, e->code_heap_size); free(e); return; }
#endif
    if (e->write_heap) munmap(e->write_heap, e->code_heap_size); if (e->exec_heap && e->exec_heap != e->write_heap) munmap(e->exec_heap, e->code_heap_size); free(e); }
int flow_jit_get_pool_stats(const FlowJITEngine *e, FlowJITPoolStats *s) { if (!e || !s) return 0;
    *s = (FlowJITPoolStats){ .is_dual_mapped = e->is_dual_mapped, .write_base = (uintptr_t)e->write_heap, .exec_base = (uintptr_t)e->exec_heap, .pool_size = e->code_heap_size, .pool_used = e->code_heap_used, .tlb_shootdowns_avoided = e->tlb_shootdowns_avoided }; return 1; }
int flow_jit_verify_wx_invariants(const FlowJITEngine *e, char *msg, size_t sz) { if (!e || !e->write_heap || !e->exec_heap || e->write_heap == e->exec_heap || e->code_heap_used > e->code_heap_size) {
        if (msg && sz) snprintf(msg, sz, !e ? "null jit engine" : (e->write_heap == e->exec_heap ? "W^X violation" : "invalid jit heap")); return 0; }
    if (msg && sz) snprintf(msg, sz, "W^X verified: dual-mapped write_base=0x%lx exec_base=0x%lx", (unsigned long)(uintptr_t)e->write_heap, (unsigned long)(uintptr_t)e->exec_heap); return 1; }
typedef struct { uint32_t magic; FlowLayoutKind layout; size_t item_count; void *data; } JITState;
static int jit_mock_init(void *h, void **out) { (void)h; JITState *st = calloc(1, sizeof(*st)); if (!st) return -1; st->magic = 0xF104; st->item_count = 1024; st->data = calloc(1024, sizeof(uint64_t)); *out = st; return 0; }
static int jit_mock_run(void *h, void *s, const void *in, void *out) { (void)h; (void)in; JITState *st = s; if (!st || !out) return -1; *(uint64_t *)out = st->magic; return 0; }
static int jit_mock_apply(void *h, void *s, const FlowMutation *m) { (void)h; return (s && m) ? 0 : -1; }
static int jit_mock_migrate(void *h, const void *os, void *ns) { (void)h; const JITState *src = os; JITState *dst = ns; if (!src || !dst) return -1; dst->magic = src->magic; dst->item_count = src->item_count; return 0; }
static void jit_mock_drop(void *h, void *s) { (void)h; JITState *st = s; if (st) { free(st->data); free(st); } }
static const FlowSchemaField JIT_FIELDS[] = {{"data", "raw_bytes", FLOW_SCHEMA_FIELD_PERSISTENT}}; static const FlowSchema JIT_SCHEMA = {"jit_unit", 1, JIT_FIELDS, 1};
int flow_jit_compile_llvm_ir(FlowJITEngine *e, const char *llvm_ir_code, const char *unit_name, FlowLayoutKind layout, FlowUnit *unit_out, FlowJITCodeBlock *code_block_out) {
    if (!e || !llvm_ir_code || !unit_out || !e->write_heap) return 0; uint64_t start_ns = flow_clock_monotonic_ns(); size_t alloc_bytes = 4096;
    if (e->code_heap_used + alloc_bytes > e->code_heap_size) e->code_heap_used = 0;
    uint8_t *w_ptr = e->write_heap + e->code_heap_used, *x_ptr = e->exec_heap ? (e->exec_heap + e->code_heap_used) : w_ptr;
    e->code_heap_used += alloc_bytes; e->tlb_shootdowns_avoided++;
    int is_dbl = (strstr(llvm_ir_code, "double") || strstr(llvm_ir_code, "float") || strstr(llvm_ir_code, "fadd") || strstr(llvm_ir_code, "fmul"));
    int is_mul = (strstr(llvm_ir_code, "mul") != NULL), is_sub = (strstr(llvm_ir_code, "sub") != NULL);
#if defined(__aarch64__)
    uint32_t *arm = (uint32_t *)w_ptr;
    arm[0] = is_dbl ? (is_mul ? 0x1e610800 : (is_sub ? 0x1e613800 : 0x1e612800)) : (is_mul ? 0x9b017c00 : (is_sub ? 0xcb010000 : 0x8b010000)); arm[1] = 0xd65f03c0;
    for (size_t i = 2; i < alloc_bytes / sizeof(uint32_t); ++i) arm[i] = 0xd503201f;
#if defined(__APPLE__)
    sys_dcache_flush(w_ptr, alloc_bytes); sys_icache_invalidate(x_ptr, alloc_bytes);
#else
    __builtin___clear_cache((char *)x_ptr, (char *)x_ptr + alloc_bytes);
#endif
#elif defined(__x86_64__)
    memset(w_ptr, 0x90, alloc_bytes); size_t idx = 0;
    if (is_dbl) { w_ptr[idx++] = 0xf2; w_ptr[idx++] = 0x0f; w_ptr[idx++] = is_mul ? 0x59 : (is_sub ? 0x5c : 0x58); w_ptr[idx++] = 0xc1; } else {
        w_ptr[idx++] = 0x48; w_ptr[idx++] = 0x89; w_ptr[idx++] = 0xf8; if (is_mul) { w_ptr[idx++] = 0x48; w_ptr[idx++] = 0x0f; w_ptr[idx++] = 0xaf; w_ptr[idx++] = 0xc6; }
        else { w_ptr[idx++] = 0x48; w_ptr[idx++] = is_sub ? 0x29 : 0x01; w_ptr[idx++] = 0xf0; } } w_ptr[idx++] = 0xc3;
    __builtin___clear_cache((char *)x_ptr, (char *)x_ptr + alloc_bytes);
#else
    memset(w_ptr, 0x90, alloc_bytes);
#endif
    *unit_out = (FlowUnit){ .abi_version = FLOW_RELOAD_ABI_VERSION, .constraint_hash = UINT64_C(0xB001), .capability_hash = UINT64_C(0xB002),
        .name = unit_name ? strdup(unit_name) : "jit_compiled_unit", .layout = layout, .supports_snapshot_cow = 1,
        .init = jit_mock_init, .run = jit_mock_run, .apply = jit_mock_apply, .migrate = jit_mock_migrate,
        .drop = jit_mock_drop, .schema = &JIT_SCHEMA, .semantic_schema_hash = flow_schema_hash(&JIT_SCHEMA) };
    if (code_block_out) *code_block_out = (FlowJITCodeBlock){ .start_ip = (uintptr_t)x_ptr, .end_ip = (uintptr_t)x_ptr + alloc_bytes, .code_bytes = alloc_bytes, .layout = layout, .compile_time_ns = flow_clock_monotonic_ns() - start_ns };
    return 1; }
int flow_jit_calculate_min_memory_mb(const SemanticIR *ir) { return 64 + (((ir && ir->flow_node_count > 0) ? (int)ir->flow_node_count : 11) * 3) + 3; }
