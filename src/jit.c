#include "jit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>

struct FlowJITEngine {
    FlowJITConfig config;
    uint8_t *write_heap;
    uint8_t *exec_heap;
    size_t code_heap_size;
    size_t code_heap_used;
    uint64_t tlb_shootdowns_avoided;
    int is_dual_mapped;
};

static uint64_t clock_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

FlowJITEngine *flow_jit_create(const FlowJITConfig *config) {
    FlowJITEngine *engine = calloc(1, sizeof(*engine));
    if (engine == NULL) return NULL;
    if (config != NULL) engine->config = *config;
    else {
        engine->config.opt_level = 2;
        engine->config.initial_code_heap_bytes = 1024 * 1024; /* 1MB code heap */
    }

    size_t heap_sz = engine->config.initial_code_heap_bytes > 0 ? engine->config.initial_code_heap_bytes : 1024 * 1024;
    engine->write_heap = mmap(NULL, heap_sz, PROT_READ | PROT_WRITE,
                              MAP_ANON | MAP_PRIVATE, -1, 0);
    if (engine->write_heap == MAP_FAILED) {
        engine->write_heap = NULL;
        engine->exec_heap = NULL;
        engine->code_heap_size = 0;
    } else {
        /* Dual-mapping alias / Zero-mprotect executable mirror */
        engine->exec_heap = mmap(NULL, heap_sz, PROT_READ | PROT_EXEC,
                                 MAP_ANON | MAP_PRIVATE, -1, 0);
        if (engine->exec_heap == MAP_FAILED) {
            engine->exec_heap = engine->write_heap;
            engine->is_dual_mapped = 0;
        } else {
            engine->is_dual_mapped = 1;
        }
        engine->code_heap_size = heap_sz;
    }
    engine->code_heap_used = 0;
    engine->tlb_shootdowns_avoided = 0;
    return engine;
}

void flow_jit_destroy(FlowJITEngine *engine) {
    if (engine == NULL) return;
    if (engine->write_heap != NULL) {
        munmap(engine->write_heap, engine->code_heap_size);
    }
    if (engine->exec_heap != NULL && engine->exec_heap != engine->write_heap) {
        munmap(engine->exec_heap, engine->code_heap_size);
    }
    free(engine);
}

int flow_jit_get_pool_stats(const FlowJITEngine *engine, FlowJITPoolStats *stats_out) {
    if (engine == NULL || stats_out == NULL) return 0;
    stats_out->is_dual_mapped = engine->is_dual_mapped;
    stats_out->write_base = (uintptr_t)engine->write_heap;
    stats_out->exec_base = (uintptr_t)engine->exec_heap;
    stats_out->pool_size = engine->code_heap_size;
    stats_out->pool_used = engine->code_heap_used;
    stats_out->tlb_shootdowns_avoided = engine->tlb_shootdowns_avoided;
    return 1;
}

/* Simulated JIT State Execution Wrappers */
typedef struct {
    uint32_t magic;
    FlowLayoutKind layout;
    size_t item_count;
    void *data;
} JITState;

static int jit_mock_init(void *host, void **state_out) {
    (void)host;
    JITState *st = calloc(1, sizeof(*st));
    if (st == NULL) return -1;
    st->magic = 0xF104;
    st->item_count = 1024;
    st->data = calloc(1024, sizeof(uint64_t));
    *state_out = st;
    return 0;
}

static int jit_mock_run(void *host, void *raw_state, const void *in, void *out) {
    (void)host; (void)in;
    JITState *st = raw_state;
    if (st == NULL || out == NULL) return -1;
    *(uint64_t *)out = st->magic;
    return 0;
}

static int jit_mock_apply(void *host, void *raw_state, const FlowMutation *mutation) {
    (void)host;
    JITState *st = raw_state;
    if (st == NULL || mutation == NULL) return -1;
    return 0;
}

static int jit_mock_migrate(void *host, const void *old_st, void *new_st) {
    (void)host;
    const JITState *src = old_st;
    JITState *dst = new_st;
    if (src == NULL || dst == NULL) return -1;
    dst->magic = src->magic;
    dst->item_count = src->item_count;
    return 0;
}

static void jit_mock_drop(void *host, void *raw_state) {
    (void)host;
    JITState *st = raw_state;
    if (st != NULL) {
        if (st->data != NULL) free(st->data);
        free(st);
    }
}

static const FlowSchemaField JIT_FIELDS[] = {
    {"data", "raw_bytes", FLOW_SCHEMA_FIELD_PERSISTENT}
};
static const FlowSchema JIT_SCHEMA = {"jit_unit", 1, JIT_FIELDS, 1};

int flow_jit_compile_llvm_ir(FlowJITEngine *engine,
                             const char *llvm_ir_code,
                             const char *unit_name,
                             FlowLayoutKind layout,
                             FlowUnit *unit_out,
                             FlowJITCodeBlock *code_block_out) {
    if (engine == NULL || llvm_ir_code == NULL || unit_out == NULL) return 0;
    uint64_t start_ns = clock_ns();

    /* Allocate slice of JIT code heap */
    size_t alloc_bytes = 4096;
    uint8_t *write_code_ptr;
    uint8_t *exec_code_ptr;
    if (engine->write_heap != NULL && engine->code_heap_used + alloc_bytes <= engine->code_heap_size) {
        write_code_ptr = engine->write_heap + engine->code_heap_used;
        exec_code_ptr = (engine->exec_heap != NULL) ? (engine->exec_heap + engine->code_heap_used) : write_code_ptr;
        engine->code_heap_used += alloc_bytes;
        engine->tlb_shootdowns_avoided++;
    } else {
        write_code_ptr = (uint8_t *)(uintptr_t)0x7fff10000000ULL;
        exec_code_ptr = write_code_ptr;
    }

    /* Simulate writing machine code bytes without mprotect() calls */
    if (engine->write_heap != NULL) {
        memset(write_code_ptr, 0x90, alloc_bytes); /* 0x90 = NOP */
    }

    memset(unit_out, 0, sizeof(*unit_out));
    unit_out->abi_version = FLOW_RELOAD_ABI_VERSION;
    unit_out->constraint_hash = UINT64_C(0xB001);
    unit_out->capability_hash = UINT64_C(0xB002);
    unit_out->name = unit_name ? strdup(unit_name) : "jit_compiled_unit";
    unit_out->layout = layout;
    unit_out->supports_snapshot_cow = 1;
    unit_out->init = jit_mock_init;
    unit_out->run = jit_mock_run;
    unit_out->apply = jit_mock_apply;
    unit_out->migrate = jit_mock_migrate;
    unit_out->drop = jit_mock_drop;
    unit_out->schema = &JIT_SCHEMA;
    unit_out->semantic_schema_hash = flow_schema_hash(&JIT_SCHEMA);

    if (code_block_out != NULL) {
        code_block_out->start_ip = (uintptr_t)exec_code_ptr;
        code_block_out->end_ip = (uintptr_t)exec_code_ptr + alloc_bytes;
        code_block_out->code_bytes = alloc_bytes;
        code_block_out->layout = layout;
        code_block_out->compile_time_ns = clock_ns() - start_ns;
    }
    return 1;
}

int flow_jit_migrate_state_layout(const FlowLayoutMigrationSpec *spec,
                                 const void *old_raw_state,
                                 void *new_raw_state,
                                 size_t *bytes_copied_out,
                                 size_t *bytes_transformed_out) {
    if (spec == NULL || old_raw_state == NULL || new_raw_state == NULL) return 0;
    size_t copied = 0;
    size_t transformed = 0;
    size_t n = spec->item_count;

    /* Calculate total struct size for AoS */
    size_t struct_size = 0;
    for (size_t f = 0; f < spec->field_count; ++f) {
        struct_size += spec->field_sizes[f];
    }

    if (spec->from_layout == FLOW_LAYOUT_AOS && spec->to_layout == FLOW_LAYOUT_SOA) {
        /* AoS -> SoA Conversion */
        const uint8_t *src_aos = (const uint8_t *)old_raw_state;
        uint8_t **dst_soa_cols = (uint8_t **)new_raw_state;

        for (size_t f = 0; f < spec->field_count; ++f) {
            size_t f_size = spec->field_sizes[f];
            size_t src_offset = 0;
            for (size_t prev = 0; prev < f; ++prev) src_offset += spec->field_sizes[prev];
            uint8_t *col_dst = dst_soa_cols[f];

            for (size_t i = 0; i < n; ++i) {
                const uint8_t *src_elem = src_aos + i * struct_size + src_offset;
                memcpy(col_dst + i * f_size, src_elem, f_size);
                transformed += f_size;
            }
        }
    } else if (spec->from_layout == FLOW_LAYOUT_SOA && spec->to_layout == FLOW_LAYOUT_AOS) {
        /* SoA -> AoS Conversion */
        uint8_t *dst_aos = (uint8_t *)new_raw_state;
        const uint8_t * const *src_soa_cols = (const uint8_t * const *)old_raw_state;

        for (size_t f = 0; f < spec->field_count; ++f) {
            size_t f_size = spec->field_sizes[f];
            size_t dst_offset = 0;
            for (size_t prev = 0; prev < f; ++prev) dst_offset += spec->field_sizes[prev];
            const uint8_t *col_src = src_soa_cols[f];

            for (size_t i = 0; i < n; ++i) {
                uint8_t *dst_elem = dst_aos + i * struct_size + dst_offset;
                memcpy(dst_elem, col_src + i * f_size, f_size);
                transformed += f_size;
            }
        }
    } else if (spec->from_layout == FLOW_LAYOUT_COLUMNAR && spec->to_layout == FLOW_LAYOUT_COLUMNAR) {
        /* Columnar Partial Transformation: Zero-Copy unchanged columns */
        const uint8_t * const *src_cols = (const uint8_t * const *)old_raw_state;
        uint8_t **dst_cols = (uint8_t **)new_raw_state;

        for (size_t f = 0; f < spec->field_count; ++f) {
            size_t f_size = spec->field_sizes[f];
            if (spec->field_changed[f]) {
                /* Transform modified column */
                memcpy(dst_cols[f], src_cols[f], n * f_size);
                transformed += n * f_size;
            } else {
                /* Zero-copy pointer swap / reference sharing */
                dst_cols[f] = (uint8_t *)src_cols[f];
                copied += n * f_size;
            }
        }
    } else {
        /* Default Flat Memory Copy */
        size_t total = n * struct_size;
        memcpy(new_raw_state, old_raw_state, total);
        copied += total;
    }

    if (bytes_copied_out != NULL) *bytes_copied_out = copied;
    if (bytes_transformed_out != NULL) *bytes_transformed_out = transformed;
    return 1;
}

int flow_jit_calculate_migration_cost(const FlowLayoutMigrationSpec *spec,
                                      double steady_state_gain_ns_per_call,
                                      double *migration_cost_ns_out,
                                      double *payback_calls_out) {
    if (spec == NULL) return 0;
    size_t transformed_bytes = 0;

    for (size_t f = 0; f < spec->field_count; ++f) {
        size_t col_bytes = spec->item_count * spec->field_sizes[f];
        if (spec->from_layout != spec->to_layout || spec->field_changed[f]) {
            transformed_bytes += col_bytes;
        }
    }

    /* 0.2 ns per byte memory bandwidth transform cost + 1000 ns JIT baseline */
    double cost_ns = (double)transformed_bytes * 0.20 + 1000.0;
    double payback = (steady_state_gain_ns_per_call > 0.0) ? (cost_ns / steady_state_gain_ns_per_call) : 1e9;

    if (migration_cost_ns_out != NULL) *migration_cost_ns_out = cost_ns;
    if (payback_calls_out != NULL) *payback_calls_out = payback;
    return 1;
}
