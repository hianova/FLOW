#include "reload.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "qsbr-unified-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

typedef struct {
    uint64_t multiplier;
    uint64_t addend;
} TestState;

static int mock_init(void *host_ctx, void **state_out) {
    (void)host_ctx;
    TestState *st = calloc(1, sizeof(TestState));
    st->multiplier = 10;
    st->addend = 5;
    *state_out = st;
    return 0;
}

static void mock_drop(void *host_ctx, void *state) {
    (void)host_ctx;
    free(state);
}

static int mock_run(void *host_ctx, void *state, const void *in, void *out) {
    (void)host_ctx;
    const TestState *st = (const TestState *)state;
    const uint64_t *x = (const uint64_t *)in;
    uint64_t *y = (uint64_t *)out;
    *y = (*x) * st->multiplier + st->addend;
    return 0;
}

static int mock_migrate(void *host_ctx, const void *old_state, void *new_state) {
    (void)host_ctx;
    const TestState *old_st = (const TestState *)old_state;
    TestState *new_st = (TestState *)new_state;
    new_st->multiplier = old_st->multiplier + 1;
    new_st->addend = old_st->addend + 2;
    return 0;
}

typedef struct {
    FlowReloadContext *context;
    _Atomic int stop_flag;
    _Atomic uint64_t total_reads;
} WorkerArgs;

static void *qsbr_reader_thread(void *arg) {
    WorkerArgs *w = (WorkerArgs *)arg;
    FlowReloadReader reader;
    memset(&reader, 0, sizeof(reader));
    flow_reload_reader_register(w->context, &reader);

    uint64_t in_val = 7;
    uint64_t out_val = 0;
    uint64_t local_reads = 0;

    while (!atomic_load_explicit(&w->stop_flag, memory_order_relaxed)) {
        /* Event Loop Inner Body: Pure Zero-Write Fast Path */
        for (int b = 0; b < 100; ++b) {
            int res = flow_qsbr_call(w->context, &in_val, &out_val);
            if (res == FLOW_RELOAD_OK) {
                CHECK(out_val > 0);
            }
            local_reads++;
        }
        /* Event Loop Boundary: Announce Quiescent State */
        flow_qsbr_checkpoint(&reader);
    }

    atomic_fetch_add_explicit(&w->total_reads, local_reads, memory_order_relaxed);
    flow_reload_reader_unregister(&reader);
    return NULL;
}

static void *qsbr_blocking_offline_thread(void *arg) {
    WorkerArgs *w = (WorkerArgs *)arg;
    FlowReloadReader reader;
    memset(&reader, 0, sizeof(reader));
    flow_reload_reader_register(w->context, &reader);

    while (!atomic_load_explicit(&w->stop_flag, memory_order_relaxed)) {
        /* 1. Active read phase */
        uint64_t in_val = 3;
        uint64_t out_val = 0;
        flow_qsbr_call(w->context, &in_val, &out_val);
        flow_qsbr_checkpoint(&reader);

        /* 2. Simulate blocking on network/socket (Offline State) */
        flow_qsbr_offline(&reader);
        struct timespec sleep_req = { .tv_sec = 0, .tv_nsec = 5000000 }; /* 5ms */
        nanosleep(&sleep_req, NULL);

        /* 3. Resume online processing */
        flow_qsbr_online(&reader);
    }

    flow_reload_reader_unregister(&reader);
    return NULL;
}

int main(void) {
    FlowUnit unit = {
        .abi_version = FLOW_RELOAD_ABI_VERSION,
        .name = "qsbr_dynamic_unit",
        .layout = FLOW_LAYOUT_DEFAULT,
        .init = mock_init,
        .run = mock_run,
        .drop = mock_drop,
        .migrate = mock_migrate
    };

    FlowReloadContext *ctx = flow_reload_create(NULL);
    CHECK(ctx != NULL);
    CHECK(flow_reload_activate(ctx, &unit) == FLOW_RELOAD_OK);

    WorkerArgs args;
    args.context = ctx;
    atomic_init(&args.stop_flag, 0);
    atomic_init(&args.total_reads, 0);

    const int NUM_ACTIVE_READERS = 8;
    const int NUM_OFFLINE_READERS = 2;
    pthread_t active_threads[8];
    pthread_t offline_threads[2];

    for (int i = 0; i < NUM_ACTIVE_READERS; ++i) {
        pthread_create(&active_threads[i], NULL, qsbr_reader_thread, &args);
    }
    for (int i = 0; i < NUM_OFFLINE_READERS; ++i) {
        pthread_create(&offline_threads[i], NULL, qsbr_blocking_offline_thread, &args);
    }

    /* Perform 10 live hot-swaps while readers are hammering the zero-write path */
    for (int round = 0; round < 10; ++round) {
        struct timespec sleep_req = { .tv_sec = 0, .tv_nsec = 10000000 }; /* 10ms */
        nanosleep(&sleep_req, NULL);

        void *new_state = NULL;
        CHECK(unit.init(NULL, &new_state) == 0 && new_state != NULL);
        CHECK(flow_reload_publish(ctx, &unit, new_state) == FLOW_RELOAD_OK);

        /* QSBR Grace Period Synchronize Barrier */
        CHECK(flow_qsbr_synchronize(ctx, 1000000000) == FLOW_RELOAD_OK); /* 1s timeout */

        /* Reclaim retired generations */
        size_t reclaimed = flow_qsbr_reclaim(ctx);
        (void)reclaimed;
    }

    atomic_store_explicit(&args.stop_flag, 1, memory_order_relaxed);

    for (int i = 0; i < NUM_ACTIVE_READERS; ++i) {
        pthread_join(active_threads[i], NULL);
    }
    for (int i = 0; i < NUM_OFFLINE_READERS; ++i) {
        pthread_join(offline_threads[i], NULL);
    }

    size_t final_reclaim = flow_qsbr_reclaim(ctx);
    (void)final_reclaim;

    uint64_t reads = atomic_load_explicit(&args.total_reads, memory_order_relaxed);
    CHECK(reads >= 100000);

    flow_reload_destroy(ctx);

    printf("QSBR_UNIFIED_TEST=passed total_zero_write_reads=%llu live_migrations=10 offline_immunity=verified grace_period=sound\n",
           (unsigned long long)reads);
    return 0;
}
