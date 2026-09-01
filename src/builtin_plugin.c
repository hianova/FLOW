#include "backend.h"
#include "reload.h"
#include "bitspace.h"

#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const Component COMPONENTS[] = {
    {"sharded_hash", "collection", "cpu", "pthread", 1, 1, 1, 0, 9, 7, "", "", 0u, 12u, 1},
    {"linear_array", "collection", "cpu", "stdlib", 0, 0, 1, 0, 5, 9, "", "", 0u, 8u, 1},
    {"ordered_tree", "collection", "cpu", "pthread", 1, 0, 0, 0, 6, 6, "", "", 0u, 16u, 0},
    {"bounded_queue", "queue", "cpu", "pthread", 1, 0, 1, 0, 9, 8, "", "bounded_queue", 0u, 8u, 0},
    {"parallel_map", "algorithm", "cpu", "pthread", 0, 0, 1, 1, 8, 6, "", "parallel_map", 0u, 8u, 0},
    {"binary_parser", "parser", "cpu", "stdlib", 0, 0, 1, 0, 7, 7, "", "binary_parser", 0u, 8u, 0},
    {"state_machine", "state_machine", "cpu", "stdlib", 0, 0, 1, 0, 7, 7, "", "state_machine", 0u, 8u, 0},
    {"flowc_bootstrap", "compiler", "cpu", "stdio", 0, 0, 1, 0, 7, 7, "compiler", "", 0u, 8u, 0}
};

static int builtin_compatible(const SemanticIR *ir,
                              const Component *component) {
    size_t i;
    if (ir == NULL || component == NULL) return 0;
    if (strcmp(component->id, "flowc_bootstrap") == 0) {
        return ir->contract_name[0] != '\0' && strcmp(ir->contract_name, "compiler") == 0;
    }
    if (ir->contract_name[0] != '\0' && strcmp(ir->contract_name, "compiler") == 0)
        return 0;

    if (component->flow_binding[0] != '\0' &&
        strcmp(ir->flow_name, component->flow_binding) != 0 &&
        !(ir->flow_parallelizable && component->supports_parallelizable)) {
        return 0;
    }

    if (ir->state_shared != component->supports_shared ||
        (ir->state_read_heavy && !component->supports_read_heavy) ||
        (ir->fact_unordered && !component->supports_unordered) ||
        (ir->flow_parallelizable != component->supports_parallelizable)) {
        return 0;
    }

    for (i = 0; i < ir->declared_constraint_count; ++i)
        if (strcmp(ir->constraints[i].name, "deterministic") != 0 &&
            strcmp(ir->constraints[i].name, "memory") != 0)
            return 0;
    return 1;
}

static int builtin_validate_contract(const SemanticIR *ir,
                                     const FlowPlugin *plugin,
                                     char *message, size_t message_size) {
    (void)plugin;
    if (message != NULL && message_size != 0) message[0] = '\0';
    if (ir == NULL) return 0;
    return 1;
}

static int builtin_memory_model(const SemanticIR *ir,
                                const Component *component,
                                size_t capacity, size_t shards,
                                size_t *estimated_bytes) {
    size_t slots = capacity;
    size_t variable;
    (void)ir;
    if (estimated_bytes == NULL || component == NULL) return 0;
    if (shards == 0) shards = 1;
    if (strcmp(component->id, "sharded_hash") == 0) {
        size_t per_shard;
        if (capacity > SIZE_MAX - (shards - 1u)) return 0;
        per_shard = (capacity + shards - 1u) / shards;
        if (per_shard != 0 && shards > SIZE_MAX / per_shard) return 0;
        slots = per_shard * shards;
    }
    if (component->memory_bytes_per_capacity != 0 &&
        slots > SIZE_MAX / component->memory_bytes_per_capacity)
        return 0;
    variable = slots * component->memory_bytes_per_capacity;
    if (component->memory_fixed_bytes > SIZE_MAX - variable) return 0;
    *estimated_bytes = component->memory_fixed_bytes + variable;
    return 1;
}

static int builtin_enumerate_dimensions(const SemanticIR *ir,
                                        const Component *component,
                                        FlowPlanDimensionSet *dims_out) {
    (void)ir;
    if (dims_out == NULL || component == NULL) return 0;
    dims_out->count = 8;
    dims_out->dimensions[0] = (FlowPlanDimension){"capacity", FLOW_DIM_EXPONENT, FLOW_DIM_CLASS_STRUCTURAL_JIT, 1, 20, 1, 12, 500};
    dims_out->dimensions[1] = (FlowPlanDimension){"threads", FLOW_DIM_EXPONENT, FLOW_DIM_CLASS_STRUCTURAL_JIT, 0, 6, 1, 0, 200};
    dims_out->dimensions[2] = (FlowPlanDimension){"shards", FLOW_DIM_EXPONENT, FLOW_DIM_CLASS_STRUCTURAL_JIT, 0, 6, 1, 0, 200};
    dims_out->dimensions[3] = (FlowPlanDimension){"buffer_bytes", FLOW_DIM_LINEAR, FLOW_DIM_CLASS_TACTILE_PARAM, 1024, 65536, 1024, 16384, 0};
    dims_out->dimensions[4] = (FlowPlanDimension){"initial_capacity", FLOW_DIM_EXPONENT, FLOW_DIM_CLASS_TACTILE_PARAM, 1, 16, 1, 2, 0};
    dims_out->dimensions[5] = (FlowPlanDimension){"growth_percent", FLOW_DIM_LINEAR, FLOW_DIM_CLASS_TACTILE_PARAM, 110, 200, 10, 150, 0};
    dims_out->dimensions[6] = (FlowPlanDimension){"batch_size", FLOW_DIM_LINEAR, FLOW_DIM_CLASS_TACTILE_PARAM, 1024, 65536, 1024, 16384, 0};
    dims_out->dimensions[7] = (FlowPlanDimension){"arena_bytes", FLOW_DIM_LINEAR, FLOW_DIM_CLASS_TACTILE_PARAM, 0, 1048576, 4096, 0, 0};
    return 1;
}

#include <unistd.h>

static size_t builtin_target_threads(const SemanticIR *ir) {
    long online = sysconf(_SC_NPROCESSORS_ONLN);
    if (online < 1) online = 1;
    if (online > 64) online = 64;
    size_t target = (ir != NULL && ir->flow_parallelizable) ? 4 :
                    ((ir != NULL && ir->state_shared) ? 16 : 1);
    if (target > (size_t)online) target = (size_t)online;
    return target == 0 ? 1 : target;
}

static int builtin_evaluate_plan(const SemanticIR *ir,
                                const Component *component,
                                const FlowPlanAssignment *plan,
                                FlowPlanMetrics *metrics_out) {
    size_t capacity;
    size_t threads;
    size_t shards;
    size_t estimated_bytes = 0;

    if (metrics_out == NULL || component == NULL || plan == NULL) return 0;
    memset(metrics_out, 0, sizeof(*metrics_out));

    capacity = plan->count > 0 ? (size_t)plan->values[0] : (size_t)ir->input_max_count;
    threads = plan->count > 1 ? (size_t)plan->values[1] : 1;
    shards = plan->count > 2 ? (size_t)plan->values[2] : 1;
    if (capacity == 0) capacity = 1;
    if (threads == 0) threads = 1;
    if (shards == 0) shards = 1;

    (void)builtin_memory_model(ir, component, capacity, shards, &estimated_bytes);

    metrics_out->capacity = capacity;
    metrics_out->threads = threads;
    metrics_out->shards = shards;
    metrics_out->memory_bytes = estimated_bytes;
    metrics_out->latency_score = (double)component->latency_score;
    metrics_out->throughput_score = (double)threads * (10.0 - (double)component->latency_score + 1.0);

    double target_cap = (ir != NULL && ir->input_max_count > 0) ?
                        (ir->input_max_count < 4096 ? (double)ir->input_max_count : 4096.0) : 4096.0;
    double target_thr = (double)builtin_target_threads(ir);
    double target_shards = (ir != NULL && ir->state_shared) ? 16.0 : 1.0;

    double diff_cap = ((double)capacity < target_cap) ?
                      ((target_cap - (double)capacity) * 50.0) :
                      (((double)capacity - target_cap) * 0.1);
    double diff_thr = fabs((double)threads - target_thr) * 20.0;
    double diff_shd = fabs((double)shards - target_shards) * 50.0;

    metrics_out->energy = (double)estimated_bytes / 1024.0 + (double)component->latency_score * 2.0 +
                          diff_cap + diff_thr + diff_shd;
    return 1;
}

static int builtin_verify_plan(const SemanticIR *ir,
                               const Component *component,
                               const FlowPlanAssignment *plan,
                               VerificationReport *report_out) {
    size_t capacity;
    size_t shards;
    size_t estimated_bytes = 0;

    if (report_out == NULL || component == NULL || plan == NULL) return 0;
    memset(report_out, 0, sizeof(*report_out));

    capacity = plan->count > 0 ? (size_t)plan->values[0] : (size_t)ir->input_max_count;
    shards = plan->count > 2 ? (size_t)plan->values[2] : 1;
    if (capacity == 0) capacity = 1;
    if (shards == 0) shards = 1;

    if (!builtin_memory_model(ir, component, capacity, shards, &estimated_bytes)) {
        report_out->status = VERIFIER_COMPILE_ERROR;
        snprintf(report_out->message, sizeof(report_out->message), "builtin memory model failed");
        return 0;
    }

    report_out->capacity = capacity;
    report_out->estimated_bytes = estimated_bytes;
    report_out->max_count_proven = (size_t)ir->input_max_count <= capacity;
    report_out->runtime_input_guard = !report_out->max_count_proven;
    report_out->status = report_out->max_count_proven ? VERIFIER_PROVEN : VERIFIER_RUNTIME_CHECK;
    snprintf(report_out->message, sizeof(report_out->message),
             report_out->max_count_proven ? "input bound proven" : "input bound requires runtime guard");
    return 1;
}

static int builtin_verify(const SemanticIR *ir, const Component *component,
                          size_t capacity, size_t shards,
                          char *message, size_t message_size) {
    (void)ir;
    (void)component;
    (void)capacity;
    (void)shards;
    if (message != NULL && message_size != 0) message[0] = '\0';
    return 1;
}

/* Builtin benchmark implementations */
typedef struct {
    int id;
    int score;
} BenchItem;

typedef struct {
    int occupied;
    BenchItem item;
} BenchSlot;

typedef struct {
    BenchItem item;
    int left;
    int right;
} BenchNode;

static volatile uint64_t builtin_benchmark_sink;

static uint64_t builtin_clock_ns(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

static size_t builtin_workload_size(const SemanticIR *ir) {
    size_t count = ir->input_max_count > 64 ? 64 : (size_t)ir->input_max_count;
    return count < 8 ? 8 : count;
}

static uint64_t bench_linear(size_t capacity, size_t threads, size_t count) {
    BenchItem items[4096];
    uint64_t start;
    size_t rounds = 32 + threads * 2;
    size_t storage = capacity > 4096 ? 4096 : capacity;
    volatile uint64_t checksum = 0;

    if (storage < count) storage = count;
    start = builtin_clock_ns();
    for (size_t round = 0; round < rounds; ++round) {
        for (size_t i = 0; i < count; ++i) {
            items[i % storage].id = (int)i;
            items[i % storage].score = (int)((i * 17 + round) % 101);
        }
        for (size_t query = 0; query < count; ++query)
            checksum += (uint64_t)(items[query % storage].score + query);
    }
    builtin_benchmark_sink ^= checksum;
    return builtin_clock_ns() - start;
}

static size_t hash_id(int id, size_t shards) {
    return (size_t)((uint32_t)id * UINT32_C(2654435761)) % shards;
}

static uint64_t bench_hash(size_t capacity, size_t threads, size_t shards,
                           size_t count) {
    BenchSlot slots[4096];
    uint64_t start;
    size_t rounds = 32 + threads * 2;
    size_t storage = capacity > 4096 ? 4096 : capacity;
    volatile uint64_t checksum = 0;

    if (storage < count * 2) storage = count * 2;
    if (storage > 4096) storage = 4096;
    if (shards == 0) shards = 1;
    start = builtin_clock_ns();
    for (size_t round = 0; round < rounds; ++round) {
        memset(slots, 0, sizeof(slots));
        for (size_t i = 0; i < count; ++i) {
            size_t start_slot = (hash_id((int)i, shards) + i) % storage;
            for (size_t probe = 0; probe < storage; ++probe) {
                size_t slot = (start_slot + probe) % storage;
                if (!slots[slot].occupied) {
                    slots[slot].occupied = 1;
                    slots[slot].item = (BenchItem){(int)i, (int)((i * 17 + round) % 101)};
                    break;
                }
            }
        }
        for (size_t query = 0; query < count; ++query) {
            size_t start_slot = (hash_id((int)query, shards) + query) % storage;
            for (size_t probe = 0; probe < storage; ++probe) {
                size_t slot = (start_slot + probe) % storage;
                if (slots[slot].occupied && slots[slot].item.id == (int)query) {
                    checksum += (uint64_t)(slots[slot].item.score + query);
                    break;
                }
            }
        }
    }
    builtin_benchmark_sink ^= checksum;
    return builtin_clock_ns() - start;
}

static void tree_insert(BenchNode *nodes, size_t *length, int *root, BenchItem item) {
    int new_index = (int)(*length);
    nodes[(*length)++] = (BenchNode){item, -1, -1};
    if (*root < 0) {
        *root = new_index;
        return;
    }
    for (int current = *root;;) {
        int *next = item.score > nodes[current].item.score ? &nodes[current].left
                                                            : &nodes[current].right;
        if (*next < 0) {
            *next = new_index;
            return;
        }
        current = *next;
    }
}

static uint64_t tree_sum(const BenchNode *nodes, int root) {
    if (root < 0) return 0;
    return (uint64_t)nodes[root].item.score + tree_sum(nodes, nodes[root].left) +
           tree_sum(nodes, nodes[root].right);
}

static uint64_t bench_tree(size_t capacity, size_t threads, size_t count) {
    BenchNode nodes[4096];
    uint64_t start;
    size_t rounds = 32 + threads * 2;
    size_t storage = capacity > 4096 ? 4096 : capacity;
    uint64_t checksum = 0;

    if (storage < count) storage = count;
    start = builtin_clock_ns();
    for (size_t round = 0; round < rounds; ++round) {
        size_t length = 0;
        int root = -1;
        for (size_t i = 0; i < count && i < storage; ++i)
            tree_insert(nodes, &length, &root,
                        (BenchItem){(int)i, (int)((i * 17 + round) % 101)});
        checksum += tree_sum(nodes, root);
    }
    builtin_benchmark_sink ^= checksum;
    return builtin_clock_ns() - start;
}

static uint64_t bench_queue(size_t capacity, size_t threads, size_t count) {
    BenchItem queue[4096];
    uint64_t start;
    size_t rounds = 32 + threads * 2;
    size_t storage = capacity > 4096 ? 4096 : capacity;
    uint64_t checksum = 0;

    if (storage == 0) storage = 1;
    if (count > storage) count = storage;
    start = builtin_clock_ns();
    for (size_t round = 0; round < rounds; ++round) {
        size_t head = 0;
        size_t tail = 0;
        size_t length = 0;
        for (size_t i = 0; i < count; ++i) {
            queue[tail] = (BenchItem){(int)i, (int)(round + i)};
            tail = (tail + 1) % storage;
            ++length;
        }
        while (length > 0) {
            checksum += (uint64_t)queue[head].score;
            head = (head + 1) % storage;
            --length;
        }
    }
    builtin_benchmark_sink ^= checksum;
    return builtin_clock_ns() - start;
}

typedef struct {
    const BenchItem *input;
    BenchItem *output;
    size_t begin;
    size_t end;
} BenchMapTask;

static void *bench_map_worker(void *raw_task) {
    BenchMapTask *task = raw_task;
    for (size_t i = task->begin; i < task->end; ++i) {
        task->output[i] = task->input[i];
        task->output[i].score *= 2;
    }
    return NULL;
}

static void bench_map_serial(const BenchItem *input, BenchItem *output,
                             size_t count) {
    for (size_t i = 0; i < count; ++i) {
        output[i] = input[i];
        output[i].score *= 2;
    }
}

static uint64_t bench_parallel_map(size_t threads, size_t count) {
    BenchItem input[4096];
    BenchItem output[4096];
    pthread_t workers[64];
    BenchMapTask tasks[64];
    uint64_t start;
    size_t rounds = 32 + threads * 2;
    size_t worker_count = threads < count ? threads : count;
    uint64_t checksum = 0;

    if (worker_count > 64) worker_count = 64;
    start = builtin_clock_ns();
    for (size_t round = 0; round < rounds; ++round) {
        for (size_t i = 0; i < count; ++i)
            input[i] = (BenchItem){(int)i, (int)((i + round) % 101)};
        if (worker_count < 2 || count < worker_count * 2) {
            bench_map_serial(input, output, count);
        } else {
            size_t created = 0;
            for (size_t i = 0; i < worker_count; ++i) {
                tasks[i] = (BenchMapTask){
                    input, output, i * count / worker_count,
                    (i + 1) * count / worker_count};
                if (pthread_create(&workers[i], NULL, bench_map_worker,
                                   &tasks[i]) != 0)
                    break;
                ++created;
            }
            if (created != worker_count) {
                for (size_t i = 0; i < created; ++i)
                    (void)pthread_join(workers[i], NULL);
                bench_map_serial(input, output, count);
            } else {
                for (size_t i = 0; i < worker_count; ++i)
                    (void)pthread_join(workers[i], NULL);
            }
        }
        for (size_t i = 0; i < count; ++i)
            checksum += (uint64_t)output[i].score;
    }
    builtin_benchmark_sink ^= checksum;
    return builtin_clock_ns() - start;
}

static uint64_t bench_binary_parser(size_t threads, size_t count) {
    uint64_t start;
    size_t rounds = 32 + threads * 2;
    volatile uint64_t checksum = 0;

    (void)count;
    start = builtin_clock_ns();
    for (size_t round = 0; round < rounds * 4; ++round) {
        const unsigned char packet[] = {0xF1, 0x02, 0x07, 0x2A};
        if (sizeof(packet) >= 4 && packet[0] == 0xF1 &&
            (size_t)packet[1] + 2 <= sizeof(packet))
            checksum += (uint64_t)packet[2] + packet[3];
    }
    builtin_benchmark_sink ^= checksum;
    return builtin_clock_ns() - start;
}

static uint64_t bench_state_machine(size_t threads, size_t count) {
    uint64_t start;
    size_t rounds = 32 + threads * 2;
    volatile uint64_t checksum = 0;

    (void)count;
    start = builtin_clock_ns();
    for (size_t round = 0; round < rounds * 4; ++round) {
        int state = 0;
        const int events[] = {1, 1, 2, 2};
        for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); ++i) {
            if (state == 0 && events[i] == 1) state = 1;
            else if (state == 1 && events[i] == 2) state = 2;
        }
        checksum += (uint64_t)state;
    }
    builtin_benchmark_sink ^= checksum;
    return builtin_clock_ns() - start;
}

static uint64_t builtin_benchmark(const SemanticIR *ir,
                                  const Component *component,
                                  const FlowPlanAssignment *plan) {
    size_t count = builtin_workload_size(ir);
    size_t capacity = plan != NULL && plan->count > 0 ? (size_t)plan->values[0] : 4096;
    size_t threads = plan != NULL && plan->count > 1 ? (size_t)plan->values[1] : 1;
    size_t shards = plan != NULL && plan->count > 2 ? (size_t)plan->values[2] : 1;

    if (component == NULL) return UINT64_MAX;
    if (strcmp(component->id, "bounded_queue") == 0)
        return bench_queue(capacity, threads, count);
    if (strcmp(component->id, "parallel_map") == 0)
        return bench_parallel_map(threads, count);
    if (strcmp(component->id, "binary_parser") == 0)
        return bench_binary_parser(threads, count);
    if (strcmp(component->id, "state_machine") == 0)
        return bench_state_machine(threads, count);
    if (strcmp(component->id, "linear_array") == 0)
        return bench_linear(capacity, threads, count);
    if (strcmp(component->id, "ordered_tree") == 0)
        return bench_tree(capacity, threads, count);
    return bench_hash(capacity, threads, shards, count);
}

typedef struct {
    int id;
    int score;
    int occupied;
} BuiltinEntry;

typedef struct {
    size_t capacity;
    size_t threads;
    size_t shards;
    size_t count;
    BuiltinEntry *entries;
} BuiltinRuntimeState;

static int builtin_state_init(void *host_context, void **state_out) {
    (void)host_context;
    BuiltinRuntimeState *st = calloc(1, sizeof(*st));
    if (st == NULL) return -1;
    st->capacity = 4096;
    st->threads = 1;
    st->shards = 1;
    st->entries = calloc(st->capacity, sizeof(BuiltinEntry));
    if (st->entries == NULL) {
        free(st);
        return -1;
    }
    *state_out = st;
    return 0;
}

static int builtin_state_run(void *host_context, void *state,
                             const void *input, void *output) {
    (void)host_context;
    BuiltinRuntimeState *st = (BuiltinRuntimeState *)state;
    if (st == NULL) return -1;
    if (input != NULL && st->count < st->capacity) {
        int val = *(const int *)input;
        st->entries[st->count].id = val;
        st->entries[st->count].score = val * 2;
        st->entries[st->count].occupied = 1;
        st->count++;
    }
    if (output != NULL) {
        *(int *)output = (int)st->count;
    }
    return 0;
}

static int builtin_state_apply(void *host_context, void *state,
                               const FlowMutation *mutation) {
    (void)host_context;
    BuiltinRuntimeState *st = (BuiltinRuntimeState *)state;
    if (st == NULL || mutation == NULL) return -1;
    if (mutation->kind == FLOW_MUTATION_UPSERT && mutation->value != NULL &&
        mutation->value_size == sizeof(int) && st->count < st->capacity) {
        int val = *(const int *)mutation->value;
        st->entries[st->count].id = mutation->key != NULL ? *(const int *)mutation->key : val;
        st->entries[st->count].score = val;
        st->entries[st->count].occupied = 1;
        st->count++;
        return 0;
    }
    return 0;
}

static int builtin_state_migrate(void *host_context, const void *old_state,
                                 void *new_state) {
    (void)host_context;
    const BuiltinRuntimeState *old_st = (const BuiltinRuntimeState *)old_state;
    BuiltinRuntimeState *new_st = (BuiltinRuntimeState *)new_state;
    if (old_st == NULL || new_st == NULL) return -1;
    size_t copy_count = old_st->count;
    if (copy_count > new_st->capacity) copy_count = new_st->capacity;
    if (copy_count > 0 && old_st->entries != NULL && new_st->entries != NULL) {
        memcpy(new_st->entries, old_st->entries, copy_count * sizeof(BuiltinEntry));
    }
    new_st->count = copy_count;
    return 0;
}

static void builtin_state_drop(void *host_context, void *state) {
    (void)host_context;
    BuiltinRuntimeState *st = (BuiltinRuntimeState *)state;
    if (st != NULL) {
        free(st->entries);
        free(st);
    }
}

static const FlowSchemaField BUILTIN_SCHEMA_FIELDS[] = {
    {"entries", "entry[]", FLOW_SCHEMA_FIELD_PERSISTENT | FLOW_SCHEMA_FIELD_ORDERED},
    {"count", "u64", FLOW_SCHEMA_FIELD_PERSISTENT}
};
static const FlowSchema BUILTIN_UNIT_SCHEMA = {
    "builtin_collection", 1, BUILTIN_SCHEMA_FIELDS, 2
};

static int builtin_create_unit(const FlowPlanArtifact *artifact,
                               const SemanticIR *ir,
                               const Component *component,
                               FlowUnit *unit_out) {
    if (artifact == NULL || component == NULL || unit_out == NULL) return 0;
    memset(unit_out, 0, sizeof(*unit_out));
    unit_out->abi_version = FLOW_RELOAD_ABI_VERSION;
    unit_out->name = component->id;
    unit_out->init = builtin_state_init;
    unit_out->run = builtin_state_run;
    unit_out->apply = builtin_state_apply;
    unit_out->migrate = builtin_state_migrate;
    unit_out->drop = builtin_state_drop;
    unit_out->schema = &BUILTIN_UNIT_SCHEMA;
    unit_out->constraint_hash = flow_compute_contract_hash(ir);
    unit_out->capability_hash = 0x55;
    unit_out->semantic_schema_hash = artifact->plan_schema_hash;
    unit_out->supports_snapshot_cow = 1;
    return 1;
}

static uint64_t builtin_preference_mask(const SemanticIR *ir,
                                        const Component *component,
                                        const FlowPlanDimensionSet *dims) {
    if (dims == NULL || dims->count == 0) return 0;
    uint64_t mask = 0;
    unsigned shift = 0;

    for (size_t i = 0; i < dims->count; ++i) {
        const FlowPlanDimension *d = &dims->dimensions[i];
        unsigned bits = flow_dimension_bits(d);
        if (bits == 0) continue;
        uint64_t dim_mask = (bits >= 64) ? (uint64_t)-1 : (((uint64_t)1 << bits) - 1);

        int prefer = 0;
        if (component != NULL) {
            if (strcmp(component->id, "sharded_hash") == 0) {
                if (ir != NULL && (ir->state_shared || ir->state_read_heavy)) {
                    if (strcmp(d->name, "threads") == 0 || strcmp(d->name, "shards") == 0) {
                        prefer = 1;
                    }
                }
            } else if (strcmp(component->id, "bounded_queue") == 0) {
                if (strcmp(d->name, "buffer_bytes") == 0 || strcmp(d->name, "batch_size") == 0) {
                    prefer = 1;
                }
            } else if (strcmp(component->id, "parallel_map") == 0) {
                if (strcmp(d->name, "threads") == 0 || strcmp(d->name, "batch_size") == 0) {
                    prefer = 1;
                }
            } else if (strcmp(component->id, "linear_array") == 0) {
                if (strcmp(d->name, "capacity") == 0 || strcmp(d->name, "initial_capacity") == 0) {
                    prefer = 1;
                }
            }
        }

        if (prefer) {
            mask |= (dim_mask << shift);
        }
        shift += bits;
    }
    return mask;
}

static uint64_t builtin_environment_mask(const SemanticIR *ir,
                                         const Component *component,
                                         const FlowPlanDimensionSet *dims,
                                         const FlowEnvironmentState *env) {
    (void)ir;
    (void)component;
    if (dims == NULL || dims->count == 0 || env == NULL) return (uint64_t)-1;
    uint64_t mask = (uint64_t)-1;
    unsigned shift = 0;

    for (size_t i = 0; i < dims->count; ++i) {
        const FlowPlanDimension *d = &dims->dimensions[i];
        unsigned bits = flow_dimension_bits(d);
        if (bits == 0) continue;
        uint64_t dim_mask = (bits >= 64) ? (uint64_t)-1 : (((uint64_t)1 << bits) - 1);

        if (env->pressure_level == FLOW_ENV_PRESSURE_MEMORY_CRITICAL) {
            /* Under critical memory pressure: forbid arena allocation, force buffer/batch/arena to minimal values */
            if (strcmp(d->name, "arena_bytes") == 0) {
                mask &= ~(dim_mask << shift); /* strictly 0 */
            } else if (strcmp(d->name, "buffer_bytes") == 0 || strcmp(d->name, "batch_size") == 0) {
                uint64_t allowed = 0x3;
                for (unsigned b = 0; b < bits; ++b) {
                    if (((uint64_t)1 << b) > allowed) {
                        mask &= ~(UINT64_C(1) << (shift + b));
                    }
                }
            } else if (strcmp(d->name, "threads") == 0) {
                uint64_t allowed = 0x1;
                for (unsigned b = 0; b < bits; ++b) {
                    if (((uint64_t)1 << b) > allowed) {
                        mask &= ~(UINT64_C(1) << (shift + b));
                    }
                }
            }
        } else if (env->pressure_level == FLOW_ENV_PRESSURE_CACHE_THRASHING) {
            if (strcmp(d->name, "shards") == 0 || strcmp(d->name, "buffer_bytes") == 0) {
                uint64_t allowed = 0x3;
                for (unsigned b = 0; b < bits; ++b) {
                    if (((uint64_t)1 << b) > allowed) {
                        mask &= ~(UINT64_C(1) << (shift + b));
                    }
                }
            }
        }

        if (env->hardware_arch == FLOW_ARCH_APPLE_SILICON) {
            if (strcmp(d->name, "batch_size") == 0) {
                mask &= ~(UINT64_C(0x1) << shift);
            }
        }

        shift += bits;
    }
    return mask;
}

static const FlowPlugin BUILTIN_PLUGIN = {
    "builtin",
    "1",
    COMPONENTS,
    sizeof(COMPONENTS) / sizeof(COMPONENTS[0]),
    builtin_compatible,
    builtin_memory_model,
    builtin_verify,
    flow_emit_builtin_component,
    NULL,
    NULL,
    builtin_validate_contract,
    NULL,
    NULL,
    builtin_enumerate_dimensions,
    builtin_evaluate_plan,
    builtin_verify_plan,
    builtin_benchmark,
    NULL,
    builtin_preference_mask,
    NULL,
    NULL,
    builtin_environment_mask,
    builtin_create_unit
};

const FlowPlugin *flow_builtin_plugin(void) {
    return &BUILTIN_PLUGIN;
}
