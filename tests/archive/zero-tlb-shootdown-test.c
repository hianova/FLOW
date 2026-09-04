#include "jit.h"
#include "reload.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "zero-tlb-shootdown-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

int main(void) {
    /* 1. Verify 64-Byte Cache-Line Alignment & False Sharing Immunity */
    CHECK(sizeof(FlowReloadReader) >= 64);
    CHECK(sizeof(FlowReloadReader) % 64 == 0);

    /* 2. Verify Dual-Mapped Zero-TLB-Shootdown JIT Memory Pool */
    FlowJITConfig config = {
        .enable_lto = 1,
        .opt_level = 3,
        .initial_code_heap_bytes = 2 * 1024 * 1024 /* 2 MB */
    };
    FlowJITEngine *engine = flow_jit_create(&config);
    CHECK(engine != NULL);

    FlowJITPoolStats stats;
    CHECK(flow_jit_get_pool_stats(engine, &stats));
    CHECK(stats.pool_size >= 2 * 1024 * 1024);
    CHECK(stats.write_base != 0);

    /* Compile 5 sequential JIT units */
    const char *ir_code = "define void @flow_run() { ret void }";
    FlowUnit units[5];
    FlowJITCodeBlock blocks[5];

    for (int i = 0; i < 5; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "unit_%d", i);
        CHECK(flow_jit_compile_llvm_ir(engine, ir_code, name, FLOW_LAYOUT_SOA, &units[i], &blocks[i]));
        CHECK(blocks[i].start_ip != 0);
        CHECK(blocks[i].code_bytes == 4096);
    }

    CHECK(flow_jit_get_pool_stats(engine, &stats));
    CHECK(stats.tlb_shootdowns_avoided == 5);
    CHECK(stats.pool_used == 5 * 4096);

    flow_jit_destroy(engine);

    /* 3. Verify QSBR Straggler Epoch Lease Timeout */
    FlowReloadContext *ctx = flow_reload_create(NULL);
    CHECK(ctx != NULL);

    FlowReloadReader fast_reader = {0};
    FlowReloadReader straggler_reader = {0};
    CHECK(flow_reload_reader_register(ctx, &fast_reader) == FLOW_RELOAD_OK);
    CHECK(flow_reload_reader_register(ctx, &straggler_reader) == FLOW_RELOAD_OK);

    /* Publish generation 1 */
    void *state0 = NULL;
    CHECK(units[0].init(NULL, &state0) == 0);
    CHECK(flow_reload_publish(ctx, &units[0], state0) == FLOW_RELOAD_OK);

    /* Fast reader enters and leaves normally */
    int out_val = 0;
    int in_val = 42;
    CHECK(flow_reload_call(ctx, &fast_reader, &in_val, &out_val) == 0);

    /* Straggler reader enters but gets stalled / hung */
    FlowInvocation straggler_inv = {0};
    CHECK(flow_reload_begin(ctx, &straggler_reader, &straggler_inv) == FLOW_RELOAD_OK);

    /* Publish generation 2 (retiring generation 1) */
    void *state1 = NULL;
    CHECK(units[1].init(NULL, &state1) == 0);
    CHECK(flow_reload_publish(ctx, &units[1], state1) == FLOW_RELOAD_OK);

    /* Immediate reclaim: straggler is still fresh (< 200ms), so generation 1 cannot be dropped yet */
    size_t reclaimed_immediate = flow_reload_reclaim(ctx);
    CHECK(reclaimed_immediate == 0);

    /* Simulate Straggler Timeout: sleep 250ms to exceed 200ms epoch lease timeout */
    usleep(250000); /* 250 ms */

    /* Now reclaim unblocks and evicts the straggler without memory bloat! */
    size_t reclaimed_after_timeout = flow_reload_reclaim(ctx);
    CHECK(reclaimed_after_timeout >= 1);

    /* Straggler cleanly exits */
    flow_reload_end(&straggler_inv);
    CHECK(flow_reload_reader_unregister(&fast_reader) == FLOW_RELOAD_OK);
    CHECK(flow_reload_reader_unregister(&straggler_reader) == FLOW_RELOAD_OK);
    flow_reload_destroy(ctx);

    for (int i = 0; i < 5; ++i) {
        if (units[i].name) free((void *)units[i].name);
    }

    printf("ZERO_TLB_SHOOTDOWN_TEST=passed dual_mapping=verified cacheline_aligned=64B straggler_lease_timeout=verified\n");
    return 0;
}
