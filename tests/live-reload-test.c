#include "reload.h"

#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef struct {
    atomic_int migration_started;
    atomic_int failures;
    atomic_int drops;
} LiveHost;

typedef struct {
    atomic_int value;
} CounterState;

static const FlowSchemaField V1_FIELDS[] = {
    {"value", "i32", FLOW_SCHEMA_FIELD_PERSISTENT}
};
static const FlowSchemaField V2_FIELDS[] = {
    {"value", "i32", FLOW_SCHEMA_FIELD_PERSISTENT},
    {"updated_at", "u64", FLOW_SCHEMA_FIELD_DEFAULTABLE |
                            FLOW_SCHEMA_FIELD_PERSISTENT}
};
static const FlowSchemaField V3_FIELDS[] = {
    {"value", "i32", FLOW_SCHEMA_FIELD_PERSISTENT},
    {"updated_at", "u64", FLOW_SCHEMA_FIELD_DEFAULTABLE |
                            FLOW_SCHEMA_FIELD_PERSISTENT},
    {"region", "u32", FLOW_SCHEMA_FIELD_DEFAULTABLE}
};

static const FlowSchema SCHEMA_V1 = {"counter", 1, V1_FIELDS, 1};
static const FlowSchema SCHEMA_V2 = {"counter", 2, V2_FIELDS, 2};
static const FlowSchema SCHEMA_V3 = {"counter", 3, V3_FIELDS, 3};

static int init_counter(void *host_context, void **state_out) {
    CounterState *state;
    (void)host_context;
    state = calloc(1, sizeof(*state));
    if (state == NULL) return -1;
    atomic_init(&state->value, 0);
    *state_out = state;
    return 0;
}

static int run_counter(void *host_context, void *state,
                       const void *input, void *output) {
    (void)host_context;
    (void)input;
    *(int *)output = atomic_load_explicit(&((CounterState *)state)->value,
                                          memory_order_relaxed);
    return 0;
}

static int apply_counter(void *host_context, void *state,
                         const FlowMutation *mutation) {
    CounterState *counter = state;
    (void)host_context;
    if (mutation->kind != FLOW_MUTATION_UPSERT ||
        mutation->value_size != sizeof(int) || mutation->value == NULL)
        return -1;
    atomic_fetch_add_explicit(&counter->value, *(const int *)mutation->value,
                              memory_order_relaxed);
    return 0;
}

static int migrate_counter(void *host_context, const void *old_state,
                           void *new_state) {
    LiveHost *host = host_context;
    const CounterState *old_counter = old_state;
    CounterState *new_counter = new_state;
    struct timespec delay = {0, 50000000L};
    atomic_store_explicit(&new_counter->value,
                          atomic_load_explicit(&old_counter->value,
                                               memory_order_relaxed),
                          memory_order_relaxed);
    atomic_store_explicit(&host->migration_started, 1, memory_order_release);
    nanosleep(&delay, NULL);
    return 0;
}

static void drop_counter(void *host_context, void *state) {
    LiveHost *host = host_context;
    atomic_fetch_add_explicit(&host->drops, 1, memory_order_relaxed);
    free(state);
}

static FlowUnit UNIT_V1 = {
    .abi_version = FLOW_RELOAD_ABI_VERSION,
    .constraint_hash = 0x44,
    .capability_hash = 0x55,
    .name = "counter-v1",
    .run = run_counter,
    .apply = apply_counter,
    .drop = drop_counter,
    .schema = &SCHEMA_V1
};

static FlowUnit UNIT_V2 = {
    .abi_version = FLOW_RELOAD_ABI_VERSION,
    .constraint_hash = 0x44,
    .capability_hash = 0x55,
    .name = "counter-v2",
    .init = init_counter,
    .run = run_counter,
    .apply = apply_counter,
    .migrate = migrate_counter,
    .drop = drop_counter,
    .schema = &SCHEMA_V2
};

static FlowUnit UNIT_V3 = {
    .abi_version = FLOW_RELOAD_ABI_VERSION,
    .constraint_hash = 0x44,
    .capability_hash = 0x55,
    .name = "counter-v3",
    .init = init_counter,
    .run = run_counter,
    .apply = apply_counter,
    .migrate = migrate_counter,
    .drop = drop_counter,
    .schema = &SCHEMA_V3
};

typedef struct {
    FlowReloadContext *context;
    LiveHost *host;
    int writes;
    int accepted;
} WriterArgs;

static void *writer_main(void *argument) {
    WriterArgs *args = argument;
    FlowReloadReader reader;
    int delta = 1;
    int i;
    FlowMutation mutation = {
        .kind = FLOW_MUTATION_UPSERT,
        .key = NULL,
        .key_size = 0,
        .value = &delta,
        .value_size = sizeof(delta)
    };
    assert(flow_reload_reader_register(args->context, &reader) == FLOW_RELOAD_OK);
    while (!atomic_load_explicit(&args->host->migration_started,
                                 memory_order_acquire)) sched_yield();
    for (i = 0; i < args->writes; ++i) {
        int result = flow_reload_apply(args->context, &reader, &mutation);
        if (result == FLOW_RELOAD_OK) {
            ++args->accepted;
        } else if (result == FLOW_RELOAD_JOURNAL_FULL) {
            break;
        } else {
            atomic_fetch_add_explicit(&args->host->failures, 1,
                                      memory_order_relaxed);
            break;
        }
    }
    assert(flow_reload_reader_unregister(&reader) == FLOW_RELOAD_OK);
    return NULL;
}

static void set_hashes(void) {
    UNIT_V1.semantic_schema_hash = flow_schema_hash(&SCHEMA_V1);
    UNIT_V2.semantic_schema_hash = flow_schema_hash(&SCHEMA_V2);
    UNIT_V3.semantic_schema_hash = flow_schema_hash(&SCHEMA_V3);
}

static void run_live_success_test(LiveHost *host) {
    FlowReloadContext *context = flow_reload_create(host);
    FlowReloadReader reader;
    CounterState *initial = calloc(1, sizeof(*initial));
    WriterArgs writer = {0};
    pthread_t thread;
    int output = 0;

    assert(context != NULL && initial != NULL);
    atomic_init(&initial->value, 100);
    assert(flow_reload_reader_register(context, &reader) == FLOW_RELOAD_OK);
    assert(flow_reload_publish(context, &UNIT_V1, initial) == FLOW_RELOAD_OK);
    writer.context = context;
    writer.host = host;
    writer.writes = 20;
    atomic_store_explicit(&host->migration_started, 0, memory_order_release);
    assert(pthread_create(&thread, NULL, writer_main, &writer) == 0);
    assert(flow_reload_live_begin(context, &UNIT_V2, 32) == FLOW_RELOAD_OK);
    assert(pthread_join(thread, NULL) == 0);
    assert(writer.accepted == writer.writes);
    assert(flow_reload_live_finish(context) == FLOW_RELOAD_OK);
    assert(flow_reload_call(context, &reader, NULL, &output) == FLOW_RELOAD_OK);
    assert(output == 120);
    assert(flow_reload_reclaim(context) == 1);
    assert(flow_reload_reader_unregister(&reader) == FLOW_RELOAD_OK);
    assert(flow_reload_destroy(context) == FLOW_RELOAD_OK);
}

static void run_live_overflow_test(LiveHost *host) {
    FlowReloadContext *context = flow_reload_create(host);
    FlowReloadReader reader;
    CounterState *initial = calloc(1, sizeof(*initial));
    WriterArgs writer = {0};
    pthread_t thread;
    int output = 0;

    assert(context != NULL && initial != NULL);
    atomic_init(&initial->value, 200);
    assert(flow_reload_reader_register(context, &reader) == FLOW_RELOAD_OK);
    assert(flow_reload_publish(context, &UNIT_V2, initial) == FLOW_RELOAD_OK);
    writer.context = context;
    writer.host = host;
    writer.writes = 20;
    atomic_store_explicit(&host->migration_started, 0, memory_order_release);
    assert(pthread_create(&thread, NULL, writer_main, &writer) == 0);
    assert(flow_reload_live_begin(context, &UNIT_V3, 4) == FLOW_RELOAD_OK);
    assert(pthread_join(thread, NULL) == 0);
    assert(writer.accepted == 4);
    assert(flow_reload_live_finish(context) == FLOW_RELOAD_JOURNAL_FULL);
    assert(flow_reload_call(context, &reader, NULL, &output) == FLOW_RELOAD_OK);
    assert(output == 204);
    assert(flow_reload_reader_unregister(&reader) == FLOW_RELOAD_OK);
    assert(flow_reload_destroy(context) == FLOW_RELOAD_OK);
}

static void run_live_overflow_fallback_test(LiveHost *host) {
    FlowReloadContext *context = flow_reload_create(host);
    FlowReloadReader reader;
    CounterState *initial = calloc(1, sizeof(*initial));
    WriterArgs writer = {0};
    pthread_t thread;
    int output = 0;

    assert(context != NULL && initial != NULL);
    atomic_init(&initial->value, 300);
    assert(flow_reload_reader_register(context, &reader) == FLOW_RELOAD_OK);
    assert(flow_reload_publish(context, &UNIT_V2, initial) == FLOW_RELOAD_OK);
    writer.context = context;
    writer.host = host;
    writer.writes = 20;
    atomic_store_explicit(&host->migration_started, 0, memory_order_release);
    assert(pthread_create(&thread, NULL, writer_main, &writer) == 0);
    assert(flow_reload_live_begin(context, &UNIT_V3, 4) == FLOW_RELOAD_OK);
    assert(pthread_join(thread, NULL) == 0);
    assert(writer.accepted == 4);
    assert(flow_reload_live_finish_or_fallback(context) == FLOW_RELOAD_OK);
    assert(flow_reload_call(context, &reader, NULL, &output) == FLOW_RELOAD_OK);
    assert(output == 304);
    assert(flow_reload_reclaim(context) == 1);
    assert(flow_reload_reader_unregister(&reader) == FLOW_RELOAD_OK);
    assert(flow_reload_destroy(context) == FLOW_RELOAD_OK);
}

int main(void) {
    LiveHost host;
    set_hashes();
    atomic_init(&host.migration_started, 0);
    atomic_init(&host.failures, 0);
    atomic_init(&host.drops, 0);
    run_live_success_test(&host);
    run_live_overflow_test(&host);
    run_live_overflow_fallback_test(&host);
    assert(atomic_load_explicit(&host.failures, memory_order_relaxed) == 0);
    printf("LIVE_RELOAD_TEST=passed drops=%d\n",
           atomic_load_explicit(&host.drops, memory_order_relaxed));
    return EXIT_SUCCESS;
}
