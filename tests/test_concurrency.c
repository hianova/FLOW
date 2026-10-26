#include "hott_test_kit.h"
#include "reload.h"
#include "bitspace.h"
#include "registry.h"
#include "flow_jet.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <math.h>
#include <pthread.h>
#include <sys/mman.h>

#include <sched.h>
#include <unistd.h>

typedef struct {
    FlowReloadContext *ctx;
    atomic_int *readers_ready;
    atomic_int *stop_flag;
    atomic_uint_fast64_t *calls_done;
} ConcurWorkerArg;

static void *concur_reader_thread(void *arg) {
    ConcurWorkerArg *w = (ConcurWorkerArg *)arg;
    FlowReloadReader r;
    memset(&r, 0, sizeof(r));
    if (flow_reload_reader_register(w->ctx, &r) != FLOW_RELOAD_OK) return NULL;
    atomic_fetch_add_explicit(w->readers_ready, 1, memory_order_release);
    uint64_t in_v = 3, out_v = 0;
    while (!atomic_load_explicit(w->stop_flag, memory_order_acquire)) {
        FlowInvocation inv;
        memset(&inv, 0, sizeof(inv));
        if (flow_reload_begin(w->ctx, &r, &inv) == FLOW_RELOAD_OK) {
            inv.unit->run(NULL, inv.state, &in_v, &out_v);
            flow_reload_end(&inv);
            atomic_fetch_add_explicit(w->calls_done, 1, memory_order_relaxed);
        }
        if (flow_qsbr_call(w->ctx, &in_v, &out_v) == FLOW_RELOAD_OK) {
            atomic_fetch_add_explicit(w->calls_done, 1, memory_order_relaxed);
        }
        flow_qsbr_checkpoint(&r);
    }
    flow_reload_reader_unregister(&r);
    return NULL;
}

typedef struct {
    uint64_t multiplier;
    uint64_t addend;
} MockConcurState;

static int mock_concur_init(void *host_ctx, void **state_out) {
    (void)host_ctx;
    MockConcurState *st = calloc(1, sizeof(MockConcurState));
    st->multiplier = 10;
    st->addend = 5;
    *state_out = st;
    return 0;
}

static void mock_concur_drop(void *host_ctx, void *state) {
    (void)host_ctx;
    free(state);
}

static int mock_concur_run(void *host_ctx, void *state, const void *in, void *out) {
    (void)host_ctx;
    const MockConcurState *st = (const MockConcurState *)state;
    const uint64_t *x = (const uint64_t *)in;
    uint64_t *y = (uint64_t *)out;
    *y = (*x) * st->multiplier + st->addend;
    return 0;
}

static int mock_concur_migrate(void *host_ctx, const void *old_state, void *new_state) {
    (void)host_ctx;
    const MockConcurState *old_st = (const MockConcurState *)old_state;
    MockConcurState *new_st = (MockConcurState *)new_state;
    new_st->multiplier = old_st->multiplier + 1;
    new_st->addend = old_st->addend + 2;
    return 0;
}

static int mock_probe(void *host_ctx, const FlowAdaptiveCandidate *candidate,
                      const FlowAdaptiveMetrics *metrics, double *score_out) {
    (void)host_ctx;
    (void)metrics;
    *score_out = (double)candidate->latency_score;
    return 0;
}

int main(void) {
    FLOW_TEST_SUITE_BEGIN("Concurrency: QSBR RCU, Live Reload, Zero-TLB & Moving Target Defense");

    FLOW_ASSERT_TRUE(flow_registry_init());

    /* ========================================================================= */
    /* STAGE 1: QSBR Quiescent State Based Reclamation Fast Path                 */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(1, "QSBR Lock-Free Reader Fast Path & Quiescent Checkpoints");
    {
        FlowUnit unit = {
            .abi_version = FLOW_RELOAD_ABI_VERSION,
            .name = "qsbr_concur_unit",
            .layout = FLOW_LAYOUT_DEFAULT,
            .init = mock_concur_init,
            .run = mock_concur_run,
            .drop = mock_concur_drop,
            .migrate = mock_concur_migrate
        };

        FlowReloadContext *ctx = flow_reload_create(NULL);
        FLOW_ASSERT_TRUE(ctx != NULL);
        FLOW_ASSERT_EQ(flow_reload_activate(ctx, &unit), FLOW_RELOAD_OK);

        FlowReloadReader reader;
        memset(&reader, 0, sizeof(reader));
        FLOW_ASSERT_EQ(flow_reload_reader_register(ctx, &reader), FLOW_RELOAD_OK);

        /* Fast path execution without atomic writes in inner loop */
        uint64_t in_val = 7, out_val = 0;
        for (int i = 0; i < 50; ++i) {
            FLOW_ASSERT_EQ(flow_qsbr_call(ctx, &in_val, &out_val), FLOW_RELOAD_OK);
            FLOW_ASSERT_EQ(out_val, 75ULL); /* 7 * 10 + 5 */
        }
        flow_qsbr_checkpoint(&reader);

        /* Offline / Online state transitions */
        flow_qsbr_offline(&reader);
        flow_qsbr_online(&reader);

        FLOW_ASSERT_EQ(flow_reload_reader_unregister(&reader), FLOW_RELOAD_OK);
        flow_reload_destroy(ctx);

        printf("  ✓ Stage 1 Passed: QSBR lock-free reader loop & offline/online cycle validated.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 2: Live Hot-Reload & Dynamic State Migration                        */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(2, "Live Hot-Reload: Zero-Downtime Generation Migration");
    {
        FlowReloadContext *ctx = flow_reload_create(NULL);
        FLOW_ASSERT_TRUE(ctx != NULL);

        FlowUnit v1 = {
            .abi_version = FLOW_RELOAD_ABI_VERSION,
            .name = "module_v1",
            .layout = FLOW_LAYOUT_DEFAULT,
            .init = mock_concur_init,
            .run = mock_concur_run,
            .drop = mock_concur_drop,
            .migrate = mock_concur_migrate
        };

        FLOW_ASSERT_EQ(flow_reload_activate(ctx, &v1), FLOW_RELOAD_OK);
        FLOW_ASSERT_EQ(flow_reload_generation(ctx), 1ULL);

        uint64_t in_v = 10, out_v = 0;
        FLOW_ASSERT_EQ(flow_qsbr_call(ctx, &in_v, &out_v), FLOW_RELOAD_OK);
        FLOW_ASSERT_EQ(out_v, 105ULL); /* 10 * 10 + 5 */

        /* Hot-swap to v2 with state migration */
        FlowUnit v2 = v1;
        v2.name = "module_v2";
        void *new_state = NULL;
        FLOW_ASSERT_EQ(v2.init(NULL, &new_state), 0);
        FLOW_ASSERT_TRUE(new_state != NULL);
        FLOW_ASSERT_EQ(flow_reload_publish(ctx, &v2, new_state), FLOW_RELOAD_OK);
        FLOW_ASSERT_EQ(flow_reload_generation(ctx), 2ULL);

        /* Real-time causal loop verification: hot-swap must record live decision event */
        const FlowDecisionEvent *ev = flow_decision_logger_latest(NULL);
        FLOW_ASSERT_TRUE(ev != NULL);
        FLOW_ASSERT_EQ(ev->trigger_type, FLOW_DECISION_TRIGGER_STRAGGLER_QUARANTINE);
        FLOW_ASSERT_STR_EQ(ev->trigger_source, "qsbr_epoch_migration");
        FLOW_ASSERT_STR_EQ(ev->post_topology, "module_v2");

        /* Synchronize QSBR barrier */
        FLOW_ASSERT_EQ(flow_qsbr_synchronize(ctx, 1000000000ULL), FLOW_RELOAD_OK);

        /* New generation executes flawlessly with migrated state: 10 * (10+1) + (5+2) = 117 */
        FLOW_ASSERT_EQ(flow_qsbr_call(ctx, &in_v, &out_v), FLOW_RELOAD_OK);
        FLOW_ASSERT_EQ(out_v, 117ULL);

        /* Active reader must block reclamation of retired generation until flow_reload_end */
        FlowReloadReader pin_reader;
        memset(&pin_reader, 0, sizeof(pin_reader));
        FLOW_ASSERT_EQ(flow_reload_reader_register(ctx, &pin_reader), FLOW_RELOAD_OK);
        FlowInvocation pin_inv;
        memset(&pin_inv, 0, sizeof(pin_inv));
        FLOW_ASSERT_EQ(flow_reload_begin(ctx, &pin_reader, &pin_inv), FLOW_RELOAD_OK);

        void *v3_state = NULL;
        FLOW_ASSERT_EQ(v2.init(NULL, &v3_state), 0);
        FLOW_ASSERT_EQ(flow_reload_publish(ctx, &v2, v3_state), FLOW_RELOAD_OK);
        /* Reader is still pinned on generation 2 -> synchronize must time out and reclaim must return 0 */
        FLOW_ASSERT_EQ(flow_qsbr_synchronize(ctx, 50000ULL), FLOW_RELOAD_BUSY);
        FLOW_ASSERT_EQ(flow_reload_reclaim(ctx), 0ULL);

        /* Reservation-Before-Publication (qsbr.rs philosophy): saturate retired queue up to FLOW_RELOAD_MAX_RETIRED_DEPTH
         * while reader remains pinned; the next publish/activate MUST abort with FLOW_RELOAD_JOURNAL_FULL
         * without mutating current generation or state! */
        for (size_t r_i = 2; r_i < FLOW_RELOAD_MAX_RETIRED_DEPTH; ++r_i) {
            FLOW_ASSERT_EQ(flow_reload_activate(ctx, &v2), FLOW_RELOAD_OK);
        }
        uint64_t gen_at_cap = flow_reload_generation(ctx);
        void *ovf_state = NULL;
        FLOW_ASSERT_EQ(v2.init(NULL, &ovf_state), 0);
        FLOW_ASSERT_EQ(flow_reload_publish(ctx, &v2, ovf_state), FLOW_RELOAD_JOURNAL_FULL);
        FLOW_ASSERT_EQ(flow_reload_generation(ctx), gen_at_cap);
        v2.drop(NULL, ovf_state);
        FLOW_ASSERT_EQ(flow_reload_activate(ctx, &v2), FLOW_RELOAD_JOURNAL_FULL);
        FLOW_ASSERT_EQ(flow_reload_generation(ctx), gen_at_cap);

        flow_reload_end(&pin_inv);
        /* Once reader unpins, publish immediately succeeds via inline opportunistic reclamation! */
        FLOW_ASSERT_EQ(flow_reload_activate(ctx, &v2), FLOW_RELOAD_OK);
        FLOW_ASSERT_EQ(flow_reload_generation(ctx), gen_at_cap + 1ULL);
        flow_qsbr_checkpoint(&pin_reader);
        FLOW_ASSERT_EQ(flow_qsbr_synchronize(ctx, 100000000ULL), FLOW_RELOAD_OK);
        (void)flow_reload_reclaim(ctx);
        FLOW_ASSERT_EQ(flow_reload_reader_unregister(&pin_reader), FLOW_RELOAD_OK);
        /* Double unregister must fail safely */
        FLOW_ASSERT_EQ(flow_reload_reader_unregister(&pin_reader), FLOW_RELOAD_INVALID);

        /* Error path coverage: invalid ABI & incompatible constraint hash */
        FlowUnit bad_abi = v1;
        bad_abi.abi_version = FLOW_RELOAD_ABI_VERSION + 99;
        FLOW_ASSERT_EQ(flow_reload_activate(ctx, &bad_abi), FLOW_RELOAD_INVALID);
        FlowUnit incompat_u = v1;
        incompat_u.constraint_hash = 0xDEADCAFEULL;
        FLOW_ASSERT_EQ(flow_reload_activate(ctx, &incompat_u), FLOW_RELOAD_INCOMPATIBLE);

        /* Real multithreaded QSBR reader/writer stress */
        atomic_int readers_ready = 0;
        atomic_int stop_flag = 0;
        atomic_uint_fast64_t calls_done = 0;
        ConcurWorkerArg warg = {
            .ctx = ctx,
            .readers_ready = &readers_ready,
            .stop_flag = &stop_flag,
            .calls_done = &calls_done
        };
        pthread_t tids[4];
        for (int t = 0; t < 4; ++t) {
            FLOW_ASSERT_EQ(pthread_create(&tids[t], NULL, concur_reader_thread, &warg), 0);
        }
        while (atomic_load_explicit(&readers_ready, memory_order_acquire) < 4 ||
               atomic_load_explicit(&calls_done, memory_order_relaxed) < 8) {
            sched_yield();
        }
        for (int swap = 0; swap < 8; ++swap) {
            void *ns = NULL;
            FLOW_ASSERT_EQ(v2.init(NULL, &ns), 0);
            FLOW_ASSERT_EQ(flow_reload_publish(ctx, &v2, ns), FLOW_RELOAD_OK);
            FLOW_ASSERT_EQ(flow_qsbr_synchronize(ctx, 500000000ULL), FLOW_RELOAD_OK);
            (void)flow_reload_reclaim(ctx);
        }
        atomic_store_explicit(&stop_flag, 1, memory_order_release);
        for (int t = 0; t < 4; ++t) {
            pthread_join(tids[t], NULL);
        }
        (void)flow_reload_reclaim(ctx);
        FLOW_ASSERT_TRUE(atomic_load(&calls_done) >= 8);

        flow_reload_destroy(ctx);
        printf("  ✓ Stage 2 Passed: Zero-downtime hot-swap bumped generation 1 -> 2 with state migrated & MT QSBR verified.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 3: Zero-TLB Shootdown & JIT Dual-Mapped Memory Pool                 */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(3, "Zero-TLB Shootdown: JIT Code Heap & IPI-Free Hot Swapping");
    {
        FLOW_ASSERT_TRUE(sizeof(FlowReloadReader) >= 64);
        FLOW_ASSERT_EQ(sizeof(FlowReloadReader) % 64, 0ULL);

        FlowJITConfig config = {
            .enable_lto = 1,
            .opt_level = 3,
            .initial_code_heap_bytes = 2 * 1024 * 1024
        };
        FlowJITEngine *engine = flow_jit_create(&config);
        FLOW_ASSERT_TRUE(engine != NULL);

        FlowJITPoolStats stats;
        FLOW_ASSERT_TRUE(flow_jit_get_pool_stats(engine, &stats));
        FLOW_ASSERT_TRUE(stats.pool_size >= 2 * 1024 * 1024);
        FLOW_ASSERT_NE(stats.write_base, 0ULL);

        const char *ir_code = "define void @flow_run() { ret void }";
        FlowUnit units[3];
        FlowJITCodeBlock blocks[3];

        for (int i = 0; i < 3; ++i) {
            char name[32];
            snprintf(name, sizeof(name), "concur_unit_%d", i);
            FLOW_ASSERT_TRUE(flow_jit_compile_llvm_ir(engine, ir_code, name, FLOW_LAYOUT_SOA, &units[i], &blocks[i]));
            FLOW_ASSERT_NE(blocks[i].start_ip, 0ULL);
            FLOW_ASSERT_EQ(blocks[i].code_bytes, 4096ULL);
        }

        FLOW_ASSERT_TRUE(flow_jit_get_pool_stats(engine, &stats));
        FLOW_ASSERT_EQ(stats.tlb_shootdowns_avoided, 3ULL);

        flow_jit_destroy(engine);
        printf("  ✓ Stage 3 Passed: 3 JIT units compiled into dual-mapped heap without TLB shootdowns.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 4: Dynamic Environment Morphing                                     */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(4, "Dynamic Environment Morphing: Adaptive Workload Convergence");
    {
        FlowUnit unit_fast = {
            .abi_version = FLOW_RELOAD_ABI_VERSION,
            .name = "fast_throughput_sharded",
            .layout = FLOW_LAYOUT_AOS,
            .init = mock_concur_init,
            .run = mock_concur_run,
            .drop = mock_concur_drop,
            .migrate = mock_concur_migrate
        };
        FlowUnit unit_compact = {
            .abi_version = FLOW_RELOAD_ABI_VERSION,
            .name = "compact_soa_array",
            .layout = FLOW_LAYOUT_SOA,
            .init = mock_concur_init,
            .run = mock_concur_run,
            .drop = mock_concur_drop,
            .migrate = mock_concur_migrate
        };

        FlowReloadContext *reload_ctx = flow_reload_create(NULL);
        FLOW_ASSERT_TRUE(reload_ctx != NULL);
        FLOW_ASSERT_EQ(flow_reload_activate(reload_ctx, &unit_fast), FLOW_RELOAD_OK);

        FlowAdaptiveConfig config = {
            .sample_window = 10,
            .cooldown_calls = 5,
            .journal_capacity = 32,
            .min_improvement_percent = 5.0,
            .policy = {
                .flow_name = "browser_pipeline",
                .domain_contract = "web",
                .input_capacity = 4096,
                .memory_limit_bytes = 1024 * 1024
            }
        };

        FlowAdaptiveCandidate candidates[2] = {
            {
                .name = "fast_throughput_sharded",
                .unit = &unit_fast,
                .flow_binding = "browser_pipeline",
                .domain_contract = "web",
                .latency_score = 95,
                .memory_score = 30,
                .memory_fixed_bytes = 65536,
                .memory_bytes_per_capacity = 16
            },
            {
                .name = "compact_soa_array",
                .unit = &unit_compact,
                .flow_binding = "browser_pipeline",
                .domain_contract = "web",
                .latency_score = 50,
                .memory_score = 98,
                .memory_fixed_bytes = 2048,
                .memory_bytes_per_capacity = 4
            }
        };

        FlowAdaptiveController *controller = flow_adaptive_create(
            reload_ctx, NULL, &config, candidates, 2, 0, mock_probe);
        FLOW_ASSERT_TRUE(controller != NULL);

        FlowReloadReader reader;
        memset(&reader, 0, sizeof(reader));
        FLOW_ASSERT_EQ(flow_reload_reader_register(reload_ctx, &reader), FLOW_RELOAD_OK);

        uint64_t in_val = 42, out_val = 0;
        FLOW_ASSERT_EQ(flow_adaptive_call(controller, &reader, &in_val, &out_val), FLOW_RELOAD_OK);
        FLOW_ASSERT_EQ(flow_adaptive_current_index(controller), 0ULL);

        FlowEnvironmentState env_critical = {
            .pressure_level = FLOW_ENV_PRESSURE_MEMORY_CRITICAL,
            .available_ram_bytes = 8 * 1024 * 1024,
            .active_concurrent_tabs = 100,
            .l2_cache_bytes = 512 * 1024,
            .measured_miss_rate = 0.05
        };

        size_t morphed_index = 0;
        FlowAdaptiveStatus morph_status = flow_adaptive_handle_pressure_event(
            controller, &env_critical, &morphed_index);

        FLOW_ASSERT_EQ(morph_status, FLOW_ADAPTIVE_OK);
        FLOW_ASSERT_EQ(morphed_index, 1ULL);
        FLOW_ASSERT_EQ(flow_adaptive_current_index(controller), 1ULL);

        /* Real-time causal loop verification: pressure morph must record live decision event */
        const FlowDecisionEvent *ev_press = flow_decision_logger_latest(NULL);
        FLOW_ASSERT_TRUE(ev_press != NULL);
        FLOW_ASSERT_EQ(ev_press->trigger_type, FLOW_DECISION_TRIGGER_MEMORY_PRESSURE);
        FLOW_ASSERT_STR_EQ(ev_press->trigger_source, "adaptive_pressure_controller");

        flow_reload_reader_unregister(&reader);
        flow_adaptive_destroy(controller);
        flow_reload_destroy(reload_ctx);

        printf("  ✓ Stage 4 Passed: Dynamic environment morphing adapted to workload storm.\n\n");
    }


    FLOW_TEST_SUITE_END();
    return 0;
}
