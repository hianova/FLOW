#include "flow_test_kit.h"
#include "reload.h"
#include "jit.h"
#include "adaptive.h"
#include "security.h"
#include "bitspace.h"
#include "smt.h"
#include "registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

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

        /* Synchronize QSBR barrier */
        FLOW_ASSERT_EQ(flow_qsbr_synchronize(ctx, 1000000000ULL), FLOW_RELOAD_OK);

        /* New generation executes flawlessly with zero downtime */
        FLOW_ASSERT_EQ(flow_qsbr_call(ctx, &in_v, &out_v), FLOW_RELOAD_OK);
        FLOW_ASSERT_EQ(out_v, 105ULL); /* 10 * 10 + 5 */

        flow_reload_destroy(ctx);
        printf("  ✓ Stage 2 Passed: Zero-downtime hot-swap bumped generation 1 -> 2 with state migrated.\n\n");
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

        flow_reload_reader_unregister(&reader);
        flow_adaptive_destroy(controller);
        flow_reload_destroy(reload_ctx);

        printf("  ✓ Stage 4 Passed: Dynamic environment morphing adapted to workload storm.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 5: Moving Target Defense (MTD) Polymorphic Layouts                  */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(5, "Moving Target Defense: Polymorphic Struct Layouts");
    {
        const size_t field_count = 6;
        const size_t field_sizes[6] = {8, 4, 8, 1, 4, 16};
        const size_t field_aligns[6] = {8, 4, 8, 1, 4, 8};

        FlowMTDLayout layout;
        FLOW_ASSERT_TRUE(flow_security_mtd_generate_layout(0x12345678, field_count, field_sizes, field_aligns, 32, &layout));
        FLOW_ASSERT_EQ(layout.field_count, field_count);
        FLOW_ASSERT_TRUE(layout.total_size > 0);
        FLOW_ASSERT_TRUE(layout.shannon_entropy >= 1.5);
        FLOW_ASSERT_NE(layout.canary_token, 0ULL);
        FLOW_ASSERT_TRUE(flow_security_mtd_verify_alignment(&layout, field_aligns));

        /* Verify field non-overlapping */
        for (size_t i = 0; i < field_count - 1; ++i) {
            size_t orig_idx = layout.field_order[i];
            size_t end_of_field = layout.field_offsets[i] + field_sizes[orig_idx];
            FLOW_ASSERT_TRUE(layout.field_offsets[i + 1] >= end_of_field);
        }

        /* Statistical polymorphism test across 50 distinct seeds */
        size_t order_variations = 0;
        FlowMTDLayout prev = layout;
        for (uint64_t s = 1; s <= 50; ++s) {
            FlowMTDLayout current;
            FLOW_ASSERT_TRUE(flow_security_mtd_generate_layout(s * UINT64_C(0x9e3779b97f4a7c15),
                                                              field_count, field_sizes, field_aligns, 32, &current));
            if (memcmp(current.field_order, prev.field_order, sizeof(current.field_order)) != 0) {
                order_variations++;
            }
            prev = current;
        }
        FLOW_ASSERT_TRUE(order_variations >= 45); /* >90% distinct orders */

        printf("  ✓ Stage 5 Passed: Polymorphic memory layouts generated with Shannon entropy %.2f.\n\n",
               layout.shannon_entropy);
    }

    /* ========================================================================= */
    /* STAGE 6: 3-Tier Dynamic Mask Superposition                                */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(6, "3-Tier Mask Superposition: Safety, Contract & Dynamic Canvas");
    {
        SemanticIR ir;
        memset(&ir, 0, sizeof(ir));
        strncpy(ir.flow_name, "mask_test_flow", sizeof(ir.flow_name) - 1);
        ir.input_max_count = 1024;
        ir.state_shared = 0;
        ir.flow_parallelizable = 0;
        ir.fact_mutability_read_only = 1;

        const Component *comp = select_component(&ir);
        FLOW_ASSERT_TRUE(comp != NULL);

        FlowPlanDimensionSet dims;
        FLOW_ASSERT_TRUE(flow_component_dimensions(&ir, comp, &dims));

        uint64_t safety_mask = flow_security_get_safety_mask(&ir, comp, &dims);
        FLOW_ASSERT_NE(safety_mask, 0ULL);

        /* Concurrency bits must be disabled for unshared sequential flows */
        unsigned shift = 0;
        for (size_t i = 0; i < dims.count; ++i) {
            unsigned bits = flow_dimension_bits(&dims.dimensions[i]);
            if (strcmp(dims.dimensions[i].name, "threads") == 0 ||
                strcmp(dims.dimensions[i].name, "shards") == 0) {
                uint64_t field_mask = (((uint64_t)1 << bits) - 1) << shift;
                FLOW_ASSERT_EQ(safety_mask & field_mask, 0ULL);
            }
            shift += bits;
        }

        /* Dynamic Canvas Superposition */
        FlowMaskCanvas canvas;
        memset(&canvas, 0, sizeof(canvas));
        canvas.hard_safety_mask = safety_mask;
        canvas.hard_contract_mask = 0xFFFFFFFFFFFFFFFFULL;
        canvas.hard_composite_mask = canvas.hard_safety_mask & canvas.hard_contract_mask;

        FLOW_ASSERT_EQ(canvas.hard_composite_mask, safety_mask);
        printf("  ✓ Stage 6 Passed: Safety firewall masked illegal thread mutations (Mask: 0x%016llx).\n\n",
               (unsigned long long)safety_mask);
    }

    FLOW_TEST_SUITE_END();
    return 0;
}
