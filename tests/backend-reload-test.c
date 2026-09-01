#include "reload.h"

#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define BACKEND_CAPACITY 128
#define BACKEND_SHARDS 8
#define BACKEND_SLOTS_PER_SHARD \
    ((BACKEND_CAPACITY + BACKEND_SHARDS - 1) / BACKEND_SHARDS)

typedef struct {
    int id;
    int score;
} BackendItem;

typedef struct {
    int id;
    _Atomic int occupied;
    _Atomic int score;
} BackendEntry;

typedef struct {
    BackendEntry entries[BACKEND_CAPACITY];
    size_t length;
} LinearState;

typedef struct {
    BackendEntry entries[BACKEND_SHARDS][BACKEND_SLOTS_PER_SHARD];
} ShardedHashState;

typedef struct {
    atomic_int migration_started;
    atomic_int drops;
} BackendHost;

static const FlowSchemaField LINEAR_FIELDS[] = {
    {"entries", "backend_entry[]", FLOW_SCHEMA_FIELD_PERSISTENT |
                                      FLOW_SCHEMA_FIELD_ORDERED},
    {"length", "u32", FLOW_SCHEMA_FIELD_PERSISTENT}
};
static const FlowSchemaField HASH_FIELDS[] = {
    {"entries", "backend_entry[]", FLOW_SCHEMA_FIELD_PERSISTENT |
                                      FLOW_SCHEMA_FIELD_ORDERED},
    {"length", "u32", FLOW_SCHEMA_FIELD_PERSISTENT},
    {"shards", "u32", FLOW_SCHEMA_FIELD_DEFAULTABLE |
                        FLOW_SCHEMA_FIELD_PERSISTENT}
};
static const FlowSchema LINEAR_SCHEMA = {
    "collection", 1, LINEAR_FIELDS, 2
};
static const FlowSchema HASH_SCHEMA = {
    "collection", 2, HASH_FIELDS, 3
};

static size_t backend_hash(int id) {
    return (size_t)((uint32_t)id * UINT32_C(2654435761));
}

static void sleep_milliseconds(long milliseconds) {
    struct timespec delay = {
        .tv_sec = milliseconds / 1000,
        .tv_nsec = (milliseconds % 1000) * 1000000L
    };
    nanosleep(&delay, NULL);
}

static int init_linear(void *host_context, void **state_out) {
    LinearState *state;
    (void)host_context;
    state = calloc(1, sizeof(*state));
    if (state == NULL) return -1;
    state->length = BACKEND_CAPACITY;
    *state_out = state;
    return 0;
}

static int init_hash(void *host_context, void **state_out) {
    (void)host_context;
    *state_out = calloc(1, sizeof(ShardedHashState));
    return *state_out == NULL ? -1 : 0;
}

static void drop_backend(void *host_context, void *state) {
    BackendHost *host = host_context;
    atomic_fetch_add_explicit(&host->drops, 1, memory_order_relaxed);
    free(state);
}

static int mutation_item(const FlowMutation *mutation, int *id,
                         BackendItem *item) {
    if (mutation == NULL || mutation->key == NULL ||
        mutation->key_size != sizeof(*id)) return 0;
    *id = *(const int *)mutation->key;
    if (mutation->kind == FLOW_MUTATION_DELETE) {
        return mutation->value == NULL && mutation->value_size == 0;
    }
    if (mutation->kind != FLOW_MUTATION_UPSERT || mutation->value == NULL ||
        mutation->value_size != sizeof(*item)) return 0;
    *item = *(const BackendItem *)mutation->value;
    return item->id == *id;
}

static int linear_find(const LinearState *state, int id) {
    size_t i;
    for (i = 0; i < state->length; ++i) {
        if (atomic_load_explicit(&state->entries[i].occupied,
                                 memory_order_acquire) &&
            state->entries[i].id == id)
            return (int)i;
    }
    return -1;
}

static int apply_linear(void *host_context, void *raw_state,
                        const FlowMutation *mutation) {
    LinearState *state = raw_state;
    BackendItem item = {0};
    int id;
    int index;
    (void)host_context;
    if (!mutation_item(mutation, &id, &item)) return -1;
    index = linear_find(state, id);
    if (index < 0) return -1;
    if (mutation->kind == FLOW_MUTATION_DELETE) {
        atomic_store_explicit(&state->entries[index].occupied, 0,
                              memory_order_release);
    } else {
        atomic_store_explicit(&state->entries[index].score, item.score,
                              memory_order_release);
    }
    return 0;
}

static int hash_find(const ShardedHashState *state, int id) {
    const size_t hash = backend_hash(id);
    const size_t shard = hash % BACKEND_SHARDS;
    size_t slot;
    for (slot = 0; slot < BACKEND_SLOTS_PER_SHARD; ++slot) {
        const BackendEntry *entry = &state->entries[shard][slot];
        if (atomic_load_explicit(&entry->occupied, memory_order_acquire) &&
            entry->id == id)
            return (int)slot;
    }
    return -1;
}

static int apply_hash(void *host_context, void *raw_state,
                      const FlowMutation *mutation) {
    ShardedHashState *state = raw_state;
    BackendItem item = {0};
    size_t shard;
    int id;
    int slot;
    (void)host_context;
    if (!mutation_item(mutation, &id, &item)) return -1;
    shard = backend_hash(id) % BACKEND_SHARDS;
    slot = hash_find(state, id);
    if (slot < 0) return -1;
    if (mutation->kind == FLOW_MUTATION_DELETE) {
        atomic_store_explicit(&state->entries[shard][slot].occupied, 0,
                              memory_order_release);
    } else {
        atomic_store_explicit(&state->entries[shard][slot].score, item.score,
                              memory_order_release);
    }
    return 0;
}

static int run_linear(void *host_context, void *raw_state,
                      const void *input, void *output) {
    const LinearState *state = raw_state;
    const int id = *(const int *)input;
    const int index = linear_find(state, id);
    (void)host_context;
    if (index < 0) return -1;
    ((BackendItem *)output)->id = id;
    ((BackendItem *)output)->score =
        atomic_load_explicit(&state->entries[index].score, memory_order_acquire);
    return 0;
}

static int run_hash(void *host_context, void *raw_state,
                    const void *input, void *output) {
    const ShardedHashState *state = raw_state;
    const int id = *(const int *)input;
    const size_t hash = backend_hash(id);
    const size_t shard = hash % BACKEND_SHARDS;
    const int slot = hash_find(state, id);
    (void)host_context;
    if (slot < 0) return -1;
    ((BackendItem *)output)->id = id;
    ((BackendItem *)output)->score = atomic_load_explicit(
        &state->entries[shard][slot].score, memory_order_acquire);
    return 0;
}

static int migrate_linear_to_hash(void *raw_host, const void *raw_old,
                                  void *raw_new) {
    BackendHost *host = raw_host;
    const LinearState *old_state = raw_old;
    ShardedHashState *new_state = raw_new;
    size_t i;

    for (i = 0; i < old_state->length; ++i) {
        const size_t shard = backend_hash(old_state->entries[i].id) %
                             BACKEND_SHARDS;
        size_t slot;
        if (atomic_load_explicit(&old_state->entries[i].occupied,
                                 memory_order_acquire)) {
            for (slot = 0; slot < BACKEND_SLOTS_PER_SHARD; ++slot) {
                if (!atomic_load_explicit(&new_state->entries[shard][slot]
                                              .occupied,
                                          memory_order_acquire))
                    break;
            }
            assert(slot < BACKEND_SLOTS_PER_SHARD);
            BackendEntry *entry = &new_state->entries[shard][slot];
            entry->id = old_state->entries[i].id;
            atomic_store_explicit(
                &entry->score,
                atomic_load_explicit(&old_state->entries[i].score,
                                     memory_order_acquire),
                memory_order_release);
            atomic_store_explicit(&entry->occupied, 1, memory_order_release);
        }
        if (i == old_state->length / 2) {
            atomic_store_explicit(&host->migration_started, 1,
                                  memory_order_release);
            sleep_milliseconds(40);
        }
    }
    return 0;
}

static FlowUnit UNIT_LINEAR = {
    .abi_version = FLOW_RELOAD_ABI_VERSION,
    .constraint_hash = UINT64_C(0x1001),
    .capability_hash = UINT64_C(0x2001),
    .name = "linear_array",
    .init = init_linear,
    .run = run_linear,
    .apply = apply_linear,
    .drop = drop_backend,
    .schema = &LINEAR_SCHEMA
};

static FlowUnit UNIT_HASH = {
    .abi_version = FLOW_RELOAD_ABI_VERSION,
    .constraint_hash = UINT64_C(0x1001),
    .capability_hash = UINT64_C(0x2001),
    .name = "sharded_hash",
    .init = init_hash,
    .run = run_hash,
    .apply = apply_hash,
    .migrate = migrate_linear_to_hash,
    .drop = drop_backend,
    .schema = &HASH_SCHEMA
};

typedef struct {
    FlowReloadContext *context;
    BackendHost *host;
    int accepted;
} WriterArgs;

static void *writer_main(void *raw_args) {
    WriterArgs *args = raw_args;
    FlowReloadReader reader;
    int i;
    assert(flow_reload_reader_register(args->context, &reader) == FLOW_RELOAD_OK);
    while (!atomic_load_explicit(&args->host->migration_started,
                                 memory_order_acquire)) {
    }
    for (i = 0; i < 48; ++i) {
        const int id = i;
        const BackendItem item = {i, 10000 + i};
        const FlowMutation mutation = {
            .kind = FLOW_MUTATION_UPSERT,
            .key = &id,
            .key_size = sizeof(id),
            .value = &item,
            .value_size = sizeof(item)
        };
        assert(flow_reload_apply(args->context, &reader, &mutation) ==
               FLOW_RELOAD_OK);
        ++args->accepted;
    }
    for (i = 0; i < 8; ++i) {
        const int id = i;
        const FlowMutation mutation = {
            .kind = FLOW_MUTATION_DELETE,
            .key = &id,
            .key_size = sizeof(id)
        };
        assert(flow_reload_apply(args->context, &reader, &mutation) ==
               FLOW_RELOAD_OK);
        ++args->accepted;
    }
    assert(flow_reload_reader_unregister(&reader) == FLOW_RELOAD_OK);
    return NULL;
}

static void set_schema_hashes(void) {
    UNIT_LINEAR.semantic_schema_hash = flow_schema_hash(&LINEAR_SCHEMA);
    UNIT_HASH.semantic_schema_hash = flow_schema_hash(&HASH_SCHEMA);
}

static void seed_linear(LinearState *state) {
    int i;
    state->length = BACKEND_CAPACITY;
    for (i = 0; i < BACKEND_CAPACITY; ++i) {
        state->entries[i].id = i;
        atomic_store_explicit(&state->entries[i].score, i * 3,
                              memory_order_relaxed);
        atomic_store_explicit(&state->entries[i].occupied, 1,
                              memory_order_relaxed);
    }
}

static void assert_item(FlowReloadContext *context, FlowReloadReader *reader,
                        int id, int expected_score, int expected_present) {
    BackendItem item = {0};
    const int result = flow_reload_call(context, reader, &id, &item);
    if (!expected_present) {
        assert(result != FLOW_RELOAD_OK);
        return;
    }
    assert(result == FLOW_RELOAD_OK);
    assert(item.id == id);
    assert(item.score == expected_score);
}

int main(void) {
    BackendHost host;
    FlowReloadContext *context;
    FlowReloadReader reader;
    LinearState *initial;
    WriterArgs writer = {0};
    pthread_t thread;
    int i;

    set_schema_hashes();
    atomic_init(&host.migration_started, 0);
    atomic_init(&host.drops, 0);
    context = flow_reload_create(&host);
    initial = calloc(1, sizeof(*initial));
    assert(context != NULL && initial != NULL);
    seed_linear(initial);
    assert(flow_reload_reader_register(context, &reader) == FLOW_RELOAD_OK);
    assert(flow_reload_publish(context, &UNIT_LINEAR, initial) == FLOW_RELOAD_OK);
    for (i = 0; i < 16; ++i) assert_item(context, &reader, i, i * 3, 1);

    writer.context = context;
    writer.host = &host;
    assert(pthread_create(&thread, NULL, writer_main, &writer) == 0);
    assert(flow_reload_live_begin(context, &UNIT_HASH, 128) == FLOW_RELOAD_OK);
    assert(pthread_join(thread, NULL) == 0);
    assert(writer.accepted == 56);
    assert(flow_reload_live_finish(context) == FLOW_RELOAD_OK);

    for (i = 0; i < BACKEND_CAPACITY; ++i) {
        if (i < 8) assert_item(context, &reader, i, 0, 0);
        else if (i < 48) assert_item(context, &reader, i, 10000 + i, 1);
        else assert_item(context, &reader, i, i * 3, 1);
    }
    assert(flow_reload_reclaim(context) == 1);
    assert(flow_reload_reader_unregister(&reader) == FLOW_RELOAD_OK);
    assert(flow_reload_destroy(context) == FLOW_RELOAD_OK);
    assert(atomic_load_explicit(&host.drops, memory_order_relaxed) == 2);
    puts("BACKEND_RELOAD_TEST=passed linear_array_to_sharded_hash mutations=56");
    return EXIT_SUCCESS;
}
