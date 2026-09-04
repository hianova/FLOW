#include "jit.h"
#include "reload.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "async-jit-worker-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int mock_init(void *host, void **state_out) {
    (void)host;
    uint64_t *val = malloc(sizeof(uint64_t));
    *val = 100;
    *state_out = val;
    return 0;
}

static void mock_drop(void *host, void *state) {
    (void)host;
    free(state);
}

static int mock_run(void *host, void *state, const void *in, void *out) {
    (void)host;
    uint64_t *val = (uint64_t *)state;
    const uint64_t *x = (const uint64_t *)in;
    uint64_t *y = (uint64_t *)out;
    *y = (*x) + (*val);
    return 0;
}

static int mock_migrate(void *host, const void *old_state, void *new_state) {
    (void)host;
    const uint64_t *old_val = (const uint64_t *)old_state;
    uint64_t *new_val = (uint64_t *)new_state;
    *new_val = *old_val + 1;
    return 0;
}

int main(void) {
    /* 1. Setup Reload Context and initial baseline FlowUnit */
    FlowUnit initial_unit = {
        .abi_version = FLOW_RELOAD_ABI_VERSION,
        .name = "initial_baseline_unit",
        .layout = FLOW_LAYOUT_DEFAULT,
        .init = mock_init,
        .run = mock_run,
        .drop = mock_drop,
        .migrate = mock_migrate
    };

    FlowReloadContext *reload_ctx = flow_reload_create(NULL);
    CHECK(reload_ctx != NULL);
    CHECK(flow_reload_activate(reload_ctx, &initial_unit) == FLOW_RELOAD_OK);

    FlowReloadReader main_reader;
    memset(&main_reader, 0, sizeof(main_reader));
    CHECK(flow_reload_reader_register(reload_ctx, &main_reader) == FLOW_RELOAD_OK);

    /* 2. Setup Async JIT Pool */
    FlowAsyncJITConfig jit_cfg = {
        .worker_threads = 1,
        .reload_ctx = reload_ctx
    };
    FlowAsyncJITPool *jit_pool = flow_async_jit_create(&jit_cfg);
    CHECK(jit_pool != NULL);

    /* 3. Submit 3 background compilation tasks */
    CHECK(flow_async_jit_submit(jit_pool, "define void @kernel1() {}", "jit_kernel_1", FLOW_LAYOUT_SOA, 1) == 1);
    CHECK(flow_async_jit_submit(jit_pool, "define void @kernel2() {}", "jit_kernel_2", FLOW_LAYOUT_COLUMNAR, 1) == 1);
    CHECK(flow_async_jit_submit(jit_pool, "define void @kernel3() {}", "jit_kernel_3", FLOW_LAYOUT_AOS, 1) == 1);

    /* 4. Main Thread Event Loop: Continuous zero-latency reads while background worker compiles */
    const size_t REQUESTS = 100000;
    uint64_t in_val = 5;
    uint64_t out_val = 0;
    uint64_t max_single_call_ns = 0;

    uint64_t loop_start_ns = get_time_ns();
    for (size_t i = 0; i < REQUESTS; ++i) {
        uint64_t call_start = get_time_ns();
        int res = flow_qsbr_call(reload_ctx, &in_val, &out_val);
        uint64_t call_dur = get_time_ns() - call_start;

        if (call_dur > max_single_call_ns) {
            max_single_call_ns = call_dur;
        }

        CHECK(res == FLOW_RELOAD_OK);
        CHECK(out_val >= 105);

        if (i % 500 == 0) {
            flow_qsbr_checkpoint(&main_reader);
        }
    }
    uint64_t total_loop_ns = get_time_ns() - loop_start_ns;

    /* 5. Wait for background compilations to complete */
    flow_async_jit_wait_idle(jit_pool);
    CHECK(flow_async_jit_completed_count(jit_pool) == 3);

    /* Main thread maximum call duration must NOT experience 30ms fork-exec stall */
    CHECK(max_single_call_ns < 10000000ULL); /* < 10ms (far below 30ms-50ms clang fork-exec stall) */

    flow_reload_reader_unregister(&main_reader);
    flow_async_jit_destroy(jit_pool);
    flow_reload_destroy(reload_ctx);

    printf("ASYNC_JIT_WORKER_TEST=passed requests=%zu total_time_ms=%.2f max_call_latency_us=%.2f background_compilations=3\n",
           REQUESTS, (double)total_loop_ns / 1000000.0, (double)max_single_call_ns / 1000.0);
    return 0;
}
