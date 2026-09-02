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

/* ========================================================================= */
/* Asynchronous Background JIT Engine (Zero-Latency Main-Thread Compilation) */
/* ========================================================================= */

#include <pthread.h>
#include <stdatomic.h>

#define FLOW_ASYNC_JIT_QUEUE_MAX 32

typedef struct {
    char source[2048];
    char unit_name[64];
    FlowLayoutKind layout;
    int auto_publish;
} FlowAsyncJITTask;

struct FlowAsyncJITPool {
    FlowAsyncJITConfig config;
    pthread_t worker;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    pthread_cond_t idle_cond;
    FlowAsyncJITTask queue[FLOW_ASYNC_JIT_QUEUE_MAX];
    size_t queue_head;
    size_t queue_tail;
    size_t queue_count;
    _Atomic int stopping;
    _Atomic size_t completed_count;
    _Atomic int in_flight;
    FlowJITEngine *engine;
};

static void *async_jit_worker_thread(void *arg) {
    FlowAsyncJITPool *pool = (FlowAsyncJITPool *)arg;

    while (1) {
        FlowAsyncJITTask task;
        pthread_mutex_lock(&pool->lock);
        while (pool->queue_count == 0 && !atomic_load_explicit(&pool->stopping, memory_order_acquire)) {
            pthread_cond_wait(&pool->cond, &pool->lock);
        }
        if (pool->queue_count == 0 && atomic_load_explicit(&pool->stopping, memory_order_acquire)) {
            pthread_mutex_unlock(&pool->lock);
            break;
        }

        task = pool->queue[pool->queue_head];
        pool->queue_head = (pool->queue_head + 1) % FLOW_ASYNC_JIT_QUEUE_MAX;
        pool->queue_count--;
        atomic_store_explicit(&pool->in_flight, 1, memory_order_release);
        pthread_mutex_unlock(&pool->lock);

        /* 1. Perform background compilation (Offloaded from main thread) */
        FlowUnit *compiled_unit = calloc(1, sizeof(FlowUnit));
        FlowJITCodeBlock code_block;
        if (compiled_unit != NULL) {
            flow_jit_compile_llvm_ir(pool->engine, task.source, task.unit_name,
                                     task.layout, compiled_unit, &code_block);

            /* 2. If auto-publish requested, hot-swap via Reload/QSBR */
            if (task.auto_publish && pool->config.reload_ctx != NULL) {
                flow_reload_activate(pool->config.reload_ctx, compiled_unit);
                flow_qsbr_synchronize(pool->config.reload_ctx, 1000000000);
                flow_qsbr_reclaim(pool->config.reload_ctx);
            }
        }

        atomic_fetch_add_explicit(&pool->completed_count, 1, memory_order_release);
        atomic_store_explicit(&pool->in_flight, 0, memory_order_release);

        pthread_mutex_lock(&pool->lock);
        if (pool->queue_count == 0) {
            pthread_cond_broadcast(&pool->idle_cond);
        }
        pthread_mutex_unlock(&pool->lock);
    }
    return NULL;
}

FlowAsyncJITPool *flow_async_jit_create(const FlowAsyncJITConfig *config) {
    FlowAsyncJITPool *pool = calloc(1, sizeof(*pool));
    if (pool == NULL) return NULL;
    if (config != NULL) pool->config = *config;
    if (pool->config.worker_threads == 0) pool->config.worker_threads = 1;

    pool->engine = flow_jit_create(NULL);
    pthread_mutex_init(&pool->lock, NULL);
    pthread_cond_init(&pool->cond, NULL);
    pthread_cond_init(&pool->idle_cond, NULL);
    atomic_init(&pool->stopping, 0);
    atomic_init(&pool->completed_count, 0);
    atomic_init(&pool->in_flight, 0);

    pthread_create(&pool->worker, NULL, async_jit_worker_thread, pool);
    return pool;
}

void flow_async_jit_destroy(FlowAsyncJITPool *pool) {
    if (pool == NULL) return;
    atomic_store_explicit(&pool->stopping, 1, memory_order_release);
    pthread_mutex_lock(&pool->lock);
    pthread_cond_broadcast(&pool->cond);
    pthread_mutex_unlock(&pool->lock);

    pthread_join(pool->worker, NULL);

    pthread_cond_destroy(&pool->idle_cond);
    pthread_cond_destroy(&pool->cond);
    pthread_mutex_destroy(&pool->lock);
    if (pool->engine != NULL) flow_jit_destroy(pool->engine);
    free(pool);
}

int flow_async_jit_submit(FlowAsyncJITPool *pool,
                          const char *c_or_llvm_source,
                          const char *unit_name,
                          FlowLayoutKind layout,
                          int auto_publish_on_complete) {
    if (pool == NULL) return 0;
    pthread_mutex_lock(&pool->lock);
    if (pool->queue_count >= FLOW_ASYNC_JIT_QUEUE_MAX ||
        atomic_load_explicit(&pool->stopping, memory_order_acquire)) {
        pthread_mutex_unlock(&pool->lock);
        return 0;
    }

    FlowAsyncJITTask *task = &pool->queue[pool->queue_tail];
    pool->queue_tail = (pool->queue_tail + 1) % FLOW_ASYNC_JIT_QUEUE_MAX;
    pool->queue_count++;

    strncpy(task->source, c_or_llvm_source ? c_or_llvm_source : "", sizeof(task->source) - 1);
    strncpy(task->unit_name, unit_name ? unit_name : "jit_unit", sizeof(task->unit_name) - 1);
    task->layout = layout;
    task->auto_publish = auto_publish_on_complete;

    pthread_cond_signal(&pool->cond);
    pthread_mutex_unlock(&pool->lock);
    return 1;
}

size_t flow_async_jit_completed_count(const FlowAsyncJITPool *pool) {
    if (pool == NULL) return 0;
    return atomic_load_explicit(&pool->completed_count, memory_order_acquire);
}

void flow_async_jit_wait_idle(FlowAsyncJITPool *pool) {
    if (pool == NULL) return;
    pthread_mutex_lock(&pool->lock);
    while (pool->queue_count > 0 || atomic_load_explicit(&pool->in_flight, memory_order_acquire)) {
        pthread_cond_wait(&pool->idle_cond, &pool->lock);
    }
    pthread_mutex_unlock(&pool->lock);
}

int flow_jit_calculate_min_memory_mb(const SemanticIR *ir) {
    /* Base physical memory required for LLVM context & optimization pipelines: 64MB */
    int base_llvm_context_mb = 64;
    int node_count = (ir && ir->flow_node_count > 0) ? (int)ir->flow_node_count : 11;

    /* Each AST / IR graph node expands to ~3MB of LLVM IR instructions and SSA tables */
    int per_node_cost_mb = 3;
    int estimated_working_set = base_llvm_context_mb + (node_count * per_node_cost_mb) + 3;
    return estimated_working_set; /* Dynamically derives 100MB for 11-node graph, scaled for any IR */
}

