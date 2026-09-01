#include "reload.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    atomic_uint_fast64_t calls;
    atomic_uint_fast64_t drops;
} StressHost;

typedef struct {
    uint64_t generation;
} StressState;

typedef struct {
    FlowReloadContext *context;
    size_t calls;
    atomic_uint_fast64_t *completed;
} WorkerArgs;

static int run_stress(void *host_context, void *raw_state,
                      const void *input, void *output) {
    StressHost *host = host_context;
    StressState *state = raw_state;
    (void)input;
    atomic_fetch_add_explicit(&host->calls, 1, memory_order_relaxed);
    *(uint64_t *)output = state->generation;
    return 0;
}

static void drop_stress(void *host_context, void *raw_state) {
    StressHost *host = host_context;
    atomic_fetch_add_explicit(&host->drops, 1, memory_order_relaxed);
    free(raw_state);
}

static const FlowUnit STRESS_UNIT = {
    .abi_version = FLOW_RELOAD_ABI_VERSION,
    .constraint_hash = UINT64_C(0x5101),
    .capability_hash = UINT64_C(0x5201),
    .name = "reload-stress",
    .run = run_stress,
    .drop = drop_stress
};

static size_t read_size_env(const char *name, size_t fallback) {
    const char *text = getenv(name);
    char *end = NULL;
    unsigned long long value;
    if (text == NULL || *text == '\0') return fallback;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value == 0 ||
        value > (unsigned long long)SIZE_MAX)
        return fallback;
    return (size_t)value;
}

static void *worker_main(void *raw_args) {
    WorkerArgs *args = raw_args;
    FlowReloadReader reader;
    size_t i;
    assert(flow_reload_reader_register(args->context, &reader) == FLOW_RELOAD_OK);
    for (i = 0; i < args->calls; ++i) {
        uint64_t generation = 0;
        assert(flow_reload_call(args->context, &reader, NULL, &generation) ==
               FLOW_RELOAD_OK);
        assert(generation <= UINT64_MAX);
        atomic_fetch_add_explicit(args->completed, 1, memory_order_relaxed);
    }
    assert(flow_reload_reader_unregister(&reader) == FLOW_RELOAD_OK);
    return NULL;
}

static StressState *new_state(uint64_t generation) {
    StressState *state = calloc(1, sizeof(*state));
    assert(state != NULL);
    state->generation = generation;
    return state;
}

int main(void) {
    const size_t thread_count = read_size_env("FLOW_STRESS_THREADS", 16);
    const size_t calls_per_thread = read_size_env("FLOW_STRESS_CALLS", 25000);
    const size_t publish_count = read_size_env("FLOW_STRESS_PUBLISHES", 256);
    StressHost host;
    FlowReloadContext *context;
    atomic_uint_fast64_t completed;
    pthread_t *threads;
    WorkerArgs args;
    size_t i;

    assert(thread_count <= SIZE_MAX / sizeof(*threads));
    threads = calloc(thread_count, sizeof(*threads));
    assert(threads != NULL);
    atomic_init(&host.calls, 0);
    atomic_init(&host.drops, 0);
    atomic_init(&completed, 0);
    context = flow_reload_create(&host);
    assert(context != NULL);
    assert(flow_reload_publish(context, &STRESS_UNIT, new_state(0)) ==
           FLOW_RELOAD_OK);

    args.context = context;
    args.calls = calls_per_thread;
    args.completed = &completed;
    for (i = 0; i < thread_count; ++i)
        assert(pthread_create(&threads[i], NULL, worker_main, &args) == 0);

    for (i = 1; i <= publish_count; ++i) {
        assert(flow_reload_publish(context, &STRESS_UNIT, new_state(i)) ==
               FLOW_RELOAD_OK);
        (void)flow_reload_reclaim(context);
    }
    for (i = 0; i < thread_count; ++i)
        assert(pthread_join(threads[i], NULL) == 0);
    free(threads);

    while (flow_reload_reclaim(context) != 0) {
    }
    assert(atomic_load_explicit(&completed, memory_order_relaxed) ==
           (uint64_t)thread_count * calls_per_thread);
    assert(atomic_load_explicit(&host.calls, memory_order_relaxed) ==
           atomic_load_explicit(&completed, memory_order_relaxed));
    assert(flow_reload_destroy(context) == FLOW_RELOAD_OK);
    assert(atomic_load_explicit(&host.drops, memory_order_relaxed) ==
           (uint64_t)publish_count + 1);
    printf("RELOAD_STRESS_TEST=passed threads=%zu calls=%zu publishes=%zu\n",
           thread_count, thread_count * calls_per_thread, publish_count);
    return EXIT_SUCCESS;
}
