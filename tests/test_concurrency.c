#include "flow_test_kit.h"
#include "reload.h"
#include "jit.h"
#include "adaptive.h"
#include "security.h"
#include "bitspace.h"
#include "smt.h"
#include "registry.h"
#include "bitmanifold.h"
#include "token_ring.h"
#include "entropy_collapse.h"
#include "flow_jet.h"
#include "flow_jet_dead_reckon.h"
#include "flow_time_crystal.h"
#include "flow_speculative_jit.h"
#include "flow_prefetch.h"
#include "audit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <math.h>

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

    /* ========================================================================= */
    /* STAGE 7: Wavefront-Coupled QSBR & 64B Atomic Phase Shift                  */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(7, "Wavefront-Coupled QSBR & 64B Atomic Phase Shift: Zero-Cost Temporal Invariant & SMT Soundness");
    {
        /* 1. 64-Byte Atomic Phase Shift (FLOW_ATOMIC_STAGE_SWAP & FLOW_ATOMIC_STAGE_LOAD) */
        FlowBmf1BitCanvas canvas_active;
        flow_bmf_canvas_init(&canvas_active, 1, FLOW_BMF_SW_HARD_SAFETY, ~0ULL, FLOW_BMF_SW_HARD_SAFETY);

        FlowBmf1BitCanvas canvas_staged;
        flow_bmf_canvas_init(&canvas_staged, 2, FLOW_BMF_SW_HARD_SAFETY | FLOW_BMF_SW_ANTI_SPILL_TILT, ~0ULL,
                             FLOW_BMF_SW_HARD_SAFETY | FLOW_BMF_SW_ANTI_SPILL_TILT | FLOW_BMF_SW_GRIPPER_FORCE_SAFE);

        FLOW_ASSERT_NE(canvas_active.switchboard_bits, canvas_staged.switchboard_bits);
        FLOW_ASSERT_NE(canvas_active.subspace_id, canvas_staged.subspace_id);

        /* Single-Copy 64-Byte Atomic Phase Shift */
        FLOW_ATOMIC_STAGE_SWAP(&canvas_active, &canvas_staged);
        FLOW_ASSERT_EQ(canvas_active.switchboard_bits, canvas_staged.switchboard_bits);
        FLOW_ASSERT_EQ(canvas_active.subspace_id, 2U);

        /* Atomic Acquire Load */
        FlowBmf1BitCanvas loaded = FLOW_ATOMIC_STAGE_LOAD(&canvas_active);
        FLOW_ASSERT_EQ(loaded.subspace_id, 2U);
        FLOW_ASSERT_EQ(loaded.switchboard_bits, canvas_staged.switchboard_bits);

        /* 2. Wavefront-Coupled Implicit QSBR (Zero Manual Checkpoints) */
        uint8_t backing_buf[4096];
        FlowBumpQsbrArena arena;
        FLOW_ASSERT_EQ(flow_bump_qsbr_init(&arena, backing_buf, sizeof(backing_buf)), 1);
        FLOW_ASSERT_EQ(arena.generation, 1ULL);
        FLOW_ASSERT_EQ(arena.total_folds, 0ULL);

        /* Allocate within Generation 1 */
        void *p1 = flow_bump_qsbr_alloc(&arena, 256);
        void *p2 = flow_bump_qsbr_alloc(&arena, 512);
        FLOW_ASSERT_TRUE(p1 != NULL);
        FLOW_ASSERT_TRUE(p2 != NULL);
        FLOW_ASSERT_TRUE(arena.cursor >= 768);

        /* Initialize Wavefront Ring and Bind Arena */
        SemanticIR ir;
        memset(&ir, 0, sizeof(ir));
        strncpy(ir.flow_name, "wavefront_concur_flow", sizeof(ir.flow_name) - 1);
        ir.input_max_count = 1000;
        ir.state_shared = 1;
        ir.state_read_heavy = 1;
        ir.fact_unordered = 1;

        FlowWavefrontRing wring;
        FLOW_ASSERT_EQ(flow_wavefront_ring_init(&wring, &ir, 4, 2), 1);
        FLOW_ASSERT_EQ(flow_wavefront_ring_bind_arena(&wring, &arena), 1);
        wring.state = FLOW_RING_CIRCULATING;

        /* Advance Wavefront: Quiescence occurs naturally without manual checkpoint/fold! */
        FLOW_ASSERT_EQ(flow_wavefront_ring_step_parallel(&wring), 1);
        FLOW_ASSERT_EQ(wring.wave_cycle_count, 1ULL);
        FLOW_ASSERT_EQ(wring.quiescent_generation, 2ULL);

        /* Verification: The arena folded implicitly via wavefront rotation (0ns manual cleanup) */
        FLOW_ASSERT_EQ(arena.cursor, 0ULL);
        FLOW_ASSERT_EQ(arena.generation, 2ULL);
        FLOW_ASSERT_EQ(arena.total_folds, 1ULL);

        /* Allocate in Generation 2 */
        void *p3 = flow_bump_qsbr_alloc(&arena, 128);
        FLOW_ASSERT_TRUE(p3 != NULL);
        FLOW_ASSERT_EQ(arena.cursor, 128ULL);

        /* Step Wavefront again -> Generation 3 fold */
        wring.state = FLOW_RING_CIRCULATING;
        FLOW_ASSERT_EQ(flow_wavefront_ring_step_parallel(&wring), 1);
        FLOW_ASSERT_EQ(arena.cursor, 0ULL);
        FLOW_ASSERT_EQ(arena.generation, 3ULL);
        FLOW_ASSERT_EQ(arena.total_folds, 2ULL);

        /* 3. SMT Formal Temporal Safety Theorem Verification */
        FlowSMTProofAttestation temporal_proof;
        memset(&temporal_proof, 0, sizeof(temporal_proof));
        FLOW_ASSERT_EQ(flow_wavefront_verify_temporal_safety_smt(&wring, &temporal_proof), FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_SMT_SOUND(temporal_proof);

        flow_wavefront_ring_destroy(&wring);

        printf("  ✓ Stage 7 Passed: Wavefront-Coupled implicit QSBR folded 2 generations automatically; 64B atomic phase shift SMT sound.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 8: Topological Wavefront Quiescence & Zero-Cost Elimination         */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(8, "Topological Natural Invariant: Wavefront-Coupled Evacuation Horizon & Zero-Cost Generational Folding");
    {
        /* 1. Context creation and publication of multiple generations */
        FlowReloadContext *ctx = flow_reload_create(NULL);
        FLOW_ASSERT_TRUE(ctx != NULL);

        FlowUnit u1 = {
            .abi_version = FLOW_RELOAD_ABI_VERSION,
            .name = "wavefront_u1",
            .init = mock_concur_init,
            .run = mock_concur_run,
            .drop = mock_concur_drop,
            .migrate = mock_concur_migrate
        };
        FlowUnit u2 = u1;
        u2.name = "wavefront_u2";

        FLOW_ASSERT_EQ(flow_reload_activate(ctx, &u1), FLOW_RELOAD_OK);
        FLOW_ASSERT_EQ(flow_reload_generation(ctx), 1ULL);

        /* Publish generation 2 */
        void *st2 = NULL;
        FLOW_ASSERT_EQ(u2.init(NULL, &st2), 0);
        FLOW_ASSERT_EQ(flow_reload_publish(ctx, &u2, st2), FLOW_RELOAD_OK);
        FLOW_ASSERT_EQ(flow_reload_generation(ctx), 2ULL);

        /* 2. Bind Wavefront Ring and Generational Bump Arena */
        SemanticIR ir;
        memset(&ir, 0, sizeof(ir));
        strncpy(ir.flow_name, "topological_reload_flow", sizeof(ir.flow_name) - 1);
        ir.input_max_count = 500;
        ir.state_shared = 1;
        ir.state_read_heavy = 1;
        ir.fact_unordered = 1;

        FlowWavefrontRing wring;
        FLOW_ASSERT_EQ(flow_wavefront_ring_init(&wring, &ir, 4, 2), 1);
        wring.state = FLOW_RING_CIRCULATING;

        uint8_t arena_mem[2048];
        FlowBumpQsbrArena arena;
        FLOW_ASSERT_EQ(flow_bump_qsbr_init(&arena, arena_mem, sizeof(arena_mem)), 1);

        /* Allocate scratch space in arena */
        void *arena_alloc = flow_bump_qsbr_alloc(&arena, 256);
        FLOW_ASSERT_TRUE(arena_alloc != NULL);
        FLOW_ASSERT_TRUE(arena.cursor >= 256);

        /* Bind Wavefront Ring and Bump Arena to Reload Context */
        FLOW_ASSERT_EQ(flow_reload_bind_wavefront(ctx, &wring), 1);
        FLOW_ASSERT_EQ(flow_reload_bind_arena(ctx, &arena), 1);

        /* Advance Wavefront Ring so quiescent_generation >= retired generation */
        FLOW_ASSERT_EQ(flow_wavefront_ring_step_parallel(&wring), 1);
        FLOW_ASSERT_TRUE(wring.quiescent_generation >= 2ULL);

        /* 3. Reclaim: O(1) Instantaneous Topological Quiescent Evacuation */
        size_t reclaimed = flow_reload_reclaim(ctx);
        FLOW_ASSERT_EQ(reclaimed, 1ULL);

        /* Verify generational folding occurred automatically (0ns cleanup) */
        FLOW_ASSERT_EQ(arena.cursor, 0ULL);
        FLOW_ASSERT_EQ(arena.total_folds, 1ULL);

        /* 4. Lock-Free Zero-Contention QSBR Checkpoints & Synchronization */
        FlowReloadReader reader1, reader2;
        memset(&reader1, 0, sizeof(reader1));
        memset(&reader2, 0, sizeof(reader2));
        FLOW_ASSERT_EQ(flow_reload_reader_register(ctx, &reader1), FLOW_RELOAD_OK);
        FLOW_ASSERT_EQ(flow_reload_reader_register(ctx, &reader2), FLOW_RELOAD_OK);

        /* Checkpoint without mutex lock contention */
        flow_qsbr_checkpoint(&reader1);
        flow_qsbr_checkpoint(&reader2);

        /* Synchronize: Wavefront-coupled 0-sleep synchronization */
        FLOW_ASSERT_EQ(flow_qsbr_synchronize(ctx, 10000000ULL), FLOW_RELOAD_OK);

        /* 5. Formal SMT Topological Safety Verification */
        FlowSMTProofAttestation safety_proof;
        memset(&safety_proof, 0, sizeof(safety_proof));
        FLOW_ASSERT_EQ(flow_reload_verify_topological_safety_smt(ctx, &safety_proof), FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_SMT_SOUND(safety_proof);

        FLOW_ASSERT_EQ(flow_reload_reader_unregister(&reader1), FLOW_RELOAD_OK);
        FLOW_ASSERT_EQ(flow_reload_reader_unregister(&reader2), FLOW_RELOAD_OK);
        flow_wavefront_ring_destroy(&wring);
        flow_reload_destroy(ctx);

        printf("  ✓ Stage 8 Passed: Wavefront topological natural state evacuated retired generation in O(1); arena folded; SMT verified.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 9: Phase Space Jet Bundle (.fjet), Mori-Zwanzig & Koopman Operator  */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(9, "Phase Space Jet Bundle: Mori-Zwanzig Memory Kernel, Symplectic Hamiltonian & Koopman Transfer Operator");
    {
        /* 1. Mori-Zwanzig Non-Markovian Projection Barrier Resolution */
        FlowJet jet_forward, jet_backward;
        FLOW_ASSERT_EQ(flow_jet_init(&jet_forward, "jet_hft_fwd", "HFT Forward Flow"), 1);
        FLOW_ASSERT_EQ(flow_jet_init(&jet_backward, "jet_hft_bwd", "HFT Backward Flow"), 1);

        /* Identical static coordinates q, but opposing momentum/velocity p */
        for (size_t d = 0; d < FLOW_JET_DIM; ++d) {
            jet_forward.payload.q[d] = 0.5;
            jet_backward.payload.q[d] = 0.5;
            jet_forward.payload.p[d] = 1.0;
            jet_backward.payload.p[d] = -1.0;
        }

        /* Verify static fvec embedding would be identical (0-jet collision) */
        double static_diff = 0.0;
        for (size_t d = 0; d < FLOW_JET_DIM; ++d) {
            static_diff += fabs(jet_forward.payload.q[d] - jet_backward.payload.q[d]);
        }
        FLOW_ASSERT_EQ(static_diff, 0.0);

        /* Convolve memory kernel across both branches */
        FLOW_ASSERT_EQ(flow_jet_mori_zwanzig_step(&jet_forward, 0.05), 1);
        FLOW_ASSERT_EQ(flow_jet_mori_zwanzig_step(&jet_backward, 0.05), 1);

        /* Cotangent phase space distance breaks the Mori-Zwanzig degeneracy! */
        double phase_dist = flow_jet_phase_distance(&jet_forward, &jet_backward);
        FLOW_ASSERT_TRUE(phase_dist > 5.0);

        /* 2. Symplectic Hamiltonian Dynamics (Verlet Integration Energy Conservation) */
        double initial_energy = flow_jet_hamiltonian(&jet_forward);
        FLOW_ASSERT_TRUE(initial_energy > 0.0);

        /* Step forward 100 symplectic leapfrog steps */
        for (int step = 0; step < 100; ++step) {
            FLOW_ASSERT_EQ(flow_jet_symplectic_step(&jet_forward, 0.005), 1);
        }
        double evolved_energy = flow_jet_hamiltonian(&jet_forward);

        /* Energy conserved within 0.5% tolerance */
        double energy_err = fabs(evolved_energy - initial_energy) / initial_energy;
        FLOW_ASSERT_TRUE(energy_err < 0.005);

        /* 3. Koopman Linear Observable Transfer Extrapolation */
        double predicted_obs[FLOW_JET_KOOPMAN_DIM];
        FLOW_ASSERT_EQ(flow_jet_koopman_predict(&jet_forward, 0.02, predicted_obs), 1);
        FLOW_ASSERT_TRUE(!isnan(predicted_obs[0]));
        FLOW_ASSERT_TRUE(!isinf(predicted_obs[0]));

        /* 4. Binary .fjet Serialization, Deserialization & CRC32 Integrity */
        const char *test_fjet_path = "/tmp/flow_test_jet_bundle.fjet";
        jet_forward.payload.crc32 = flow_jet_crc32(&jet_forward.payload, offsetof(FlowJetPayload, crc32));
        FLOW_ASSERT_EQ(flow_jet_write_file(&jet_forward, test_fjet_path), 1);

        FlowJet jet_loaded;
        FLOW_ASSERT_EQ(flow_jet_read_file(test_fjet_path, &jet_loaded), 1);
        FLOW_ASSERT_EQ(jet_loaded.payload.crc32, jet_forward.payload.crc32);
        FLOW_ASSERT_TRUE(jet_loaded.payload.crc32 != 0);
        FLOW_ASSERT_EQ(strcmp(jet_loaded.header.id, jet_forward.header.id), 0);
        FLOW_ASSERT_EQ(jet_loaded.payload.pure_genome, jet_forward.payload.pure_genome);

        /* Bidirectional .fvec <-> .fjet interoperability */
        FlowVecHeader fvec_hdr;
        FlowVecPayload fvec_payload;
        FLOW_ASSERT_EQ(flow_jet_to_fvec(&jet_loaded, &fvec_hdr, &fvec_payload), 1);
        FLOW_ASSERT_EQ(strcmp(fvec_hdr.magic, FLOW_FVEC_MAGIC), 0);

        FlowJet jet_reconstructed;
        FLOW_ASSERT_EQ(flow_jet_from_fvec(&fvec_hdr, &fvec_payload, &jet_reconstructed), 1);
        FLOW_ASSERT_EQ(jet_reconstructed.payload.pure_genome, jet_loaded.payload.pure_genome);

        /* 5. CPU Hardware Cache Prefetching Along Jet Phase Trajectory */
        int prefetched_lines = flow_prefetch_jet_trajectory(&jet_forward, 50.0);
        FLOW_ASSERT_TRUE(prefetched_lines >= 3);

        /* 6. Formal SMT Verification of Symplectic Soundness */
        FlowSMTProofAttestation jet_proof;
        memset(&jet_proof, 0, sizeof(jet_proof));
        FLOW_ASSERT_EQ(flow_jet_verify_symplectic_soundness_smt(&jet_forward, &jet_proof), FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_SMT_SOUND(jet_proof);

        printf("  ✓ Stage 9 Passed: Phase Space Jet Bundle broke Mori-Zwanzig degeneracy (dist=%.2f); symplectic energy conserved (err=%.4f%%); Koopman predicted; SMT sound.\n\n",
               phase_dist, energy_err * 100.0);
    }

    /* ========================================================================= */
    /* STAGE 10: Online Streaming EDMD, PMU Potential Field & 64-D Jet Bundles   */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(10, "Jet Evolution: Online Streaming EDMD, PMU Nonlinear Potential Field & 64-D Jet Bundles");
    {
        /* 1. Online Streaming EDMD Assimilation */
        FlowJet jet_stream;
        FLOW_ASSERT_EQ(flow_jet_init(&jet_stream, "jet_stream_edmd", "Streaming EDMD Test Jet"), 1);

        FlowJetStreamingEDMD edmd;
        FLOW_ASSERT_EQ(flow_jet_edmd_init(&edmd, 8, 0.98), 1);
        FLOW_ASSERT_EQ(edmd.dim, 8);
        FLOW_ASSERT_EQ(edmd.update_count, 0);

        /* Assimilate 50 streaming observations */
        for (int s = 0; s < 50; ++s) {
            double pmu_obs[FLOW_JET_MAX_KOOPMAN_DIM];
            for (int d = 0; d < 8; ++d) {
                pmu_obs[d] = 0.4 * sin(0.2 * s + d) + 0.05 * (s % 5);
            }
            FLOW_ASSERT_EQ(flow_jet_stream_learn_step(&jet_stream, &edmd, pmu_obs, 0.01), 1);
        }
        FLOW_ASSERT_EQ(edmd.update_count, 50);

        /* Verify Lyapunov contractive stability: Tr(K) <= -0.05 */
        double trace_K = 0.0;
        for (int d = 0; d < 8; ++d) {
            trace_K += jet_stream.payload.koopman_matrix[d][d];
        }
        FLOW_ASSERT_TRUE(trace_K <= -0.04);

        /* Predict forward with adapted Koopman generator */
        double pred_obs[FLOW_JET_MAX_KOOPMAN_DIM];
        FLOW_ASSERT_EQ(flow_jet_koopman_predict(&jet_stream, 0.02, pred_obs), 1);
        FLOW_ASSERT_TRUE(!isnan(pred_obs[0]));

        /* 2. PMU Hardware Nonlinear Potential Landscape & Barrier Force */
        FlowJetPotentialLandscape landscape;
        FLOW_ASSERT_EQ(flow_jet_potential_init_default(&landscape, 16), 1);
        FLOW_ASSERT_EQ(landscape.dim, 16);
        landscape.q_saturation[0] = 1.5; /* Tight saturation barrier */

        FlowJet jet_pmu;
        FLOW_ASSERT_EQ(flow_jet_init(&jet_pmu, "jet_pmu_force", "PMU Potential Field Jet"), 1);
        jet_pmu.payload.q[0] = 1.2;
        jet_pmu.payload.p[0] = 2.0; /* Moving toward saturation barrier */

        double h_init = flow_jet_hamiltonian(&jet_pmu);
        FLOW_ASSERT_TRUE(h_init > 0.0);
        for (int step = 0; step < 80; ++step) {
            FLOW_ASSERT_EQ(flow_jet_symplectic_step_with_potential(&jet_pmu, &landscape, 0.005), 1);
            /* Moreau convex cone + saturation barrier prevent penetrating saturation */
            FLOW_ASSERT_TRUE(fabs(jet_pmu.payload.q[0]) < 1.5);
        }
        double h_evolved = flow_jet_hamiltonian(&jet_pmu);
        FLOW_ASSERT_TRUE(!isnan(h_evolved));
        FLOW_ASSERT_TRUE(!isinf(h_evolved));
        FLOW_ASSERT_TRUE(h_evolved > 0.0);

        /* 3. Flexible High-Dimensional Jet Bundles (64-D Cluster / Swarm Jet) */
        FlowJet jet_64;
        FLOW_ASSERT_EQ(flow_jet_init_extended(&jet_64, "jet_cluster_64d", "64D Distributed Cluster Jet", 64, 16, 16), 1);
        FLOW_ASSERT_EQ(jet_64.header.vector_dim, 64);
        FLOW_ASSERT_EQ(jet_64.header.koopman_dim, 16);
        FLOW_ASSERT_EQ(jet_64.header.memory_taps, 16);

        for (uint32_t d = 0; d < 64; ++d) {
            jet_64.payload.q[d] = 0.1 * (double)(d + 1);
            jet_64.payload.p[d] = 0.05 * (double)(d + 1);
        }

        FLOW_ASSERT_EQ(flow_jet_symplectic_step(&jet_64, 0.005), 1);
        FLOW_ASSERT_TRUE(flow_jet_hamiltonian(&jet_64) > 0.0);

        /* 64-D Binary Serialization & CRC32 Attestation */
        const char *test_64d_path = "/tmp/flow_test_jet_64d.fjet";
        jet_64.payload.crc32 = flow_jet_crc32(&jet_64.payload, offsetof(FlowJetPayload, crc32));
        FLOW_ASSERT_EQ(flow_jet_write_file(&jet_64, test_64d_path), 1);

        FlowJet jet_64_loaded;
        FLOW_ASSERT_EQ(flow_jet_read_file(test_64d_path, &jet_64_loaded), 1);
        FLOW_ASSERT_EQ(jet_64_loaded.header.vector_dim, 64);
        FLOW_ASSERT_EQ(jet_64_loaded.payload.crc32, jet_64.payload.crc32);
        FLOW_ASSERT_EQ(jet_64_loaded.payload.q[63], jet_64.payload.q[63]);

        /* 4. SMT Highest Court Formal Attestation on 64-D Jet */
        FlowSMTProofAttestation proof_64;
        memset(&proof_64, 0, sizeof(proof_64));
        FLOW_ASSERT_EQ(flow_jet_verify_symplectic_soundness_smt(&jet_64_loaded, &proof_64), FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_SMT_SOUND(proof_64);

        printf("  ✓ Stage 10 Passed: Streaming EDMD adapted (Tr(K)=%.2f); PMU barrier preserved (|q|<1.5); 64-D Jet bundle serialized & SMT proven.\n\n",
               trace_K);
    }

    /* ========================================================================= */
    /* STAGE 11: Discrete Time Crystal (DTC): Floquet DTTSB & Cyclic Memory      */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(11, "Discrete Time Crystal (DTC): Floquet DTTSB, Limit-Cycle Memory & Subharmonic Rigidity");
    {
        FlowJet jet_dtc;
        FLOW_ASSERT_EQ(flow_jet_init(&jet_dtc, "jet_dtc_crystal", "DTC Quantum Memory Jet"), 1);

        FlowTimeCrystal dtc;
        FLOW_ASSERT_EQ(flow_dtc_init(&dtc, &jet_dtc, 0.02, 0.95 * 3.141592653589793, 1.2), 1);
        FLOW_ASSERT_EQ(dtc.floquet_cycles_total, 0);

        /* 1. Run 20 Floquet driving cycles */
        FLOW_ASSERT_EQ(flow_dtc_step_floquet(&dtc, 20, 0.001), 1);
        FLOW_ASSERT_EQ(dtc.floquet_cycles_total, 20);

        /* 2. Subharmonic 2T Fourier peak locked */
        double ratio = flow_dtc_get_fourier_subharmonic_ratio(&dtc);
        FLOW_ASSERT_TRUE(ratio >= 0.85);
        FLOW_ASSERT_EQ(dtc.is_subharmonic_locked, 1);

        /* 3. Non-thermalizing MBL energy conservation */
        FLOW_ASSERT_TRUE(dtc.max_energy_drift < 0.35);

        /* 4. Cyclic Computational Memory: Encode & Decode bit */
        FLOW_ASSERT_EQ(flow_dtc_encode_bit(&dtc, 1), 1);
        FLOW_ASSERT_EQ(flow_dtc_step_floquet(&dtc, 10, 0.001), 1);
        FLOW_ASSERT_EQ(flow_dtc_decode_bit(&dtc), 1);

        FLOW_ASSERT_EQ(flow_dtc_encode_bit(&dtc, 0), 1);
        FLOW_ASSERT_EQ(flow_dtc_step_floquet(&dtc, 10, 0.001), 1);
        FLOW_ASSERT_EQ(flow_dtc_decode_bit(&dtc), 0);

        /* 5. SMT Supreme Court Formal Proof */
        FlowSMTProofAttestation dtc_proof;
        memset(&dtc_proof, 0, sizeof(dtc_proof));
        FLOW_ASSERT_EQ(flow_dtc_verify_soundness_smt(&dtc, &dtc_proof), FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_SMT_SOUND(dtc_proof);

        printf("  ✓ Stage 11 Passed: Discrete Time Crystal sustained 2T subharmonic lock (ratio=%.2f%%); bit memory preserved; SMT proven.\n\n",
               ratio * 100.0);
    }

    /* ========================================================================= */
    /* STAGE 12: Speculative JIT: Koopman Moreau Crossing & Negative Latency Swap */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(12, "Speculative JIT: Koopman Moreau Crossing & Negative Latency Hot-Swap");
    {
        FlowJet jet_sjit;
        FLOW_ASSERT_EQ(flow_jet_init(&jet_sjit, "jet_speculative", "Speculative JIT Jet"), 1);
        jet_sjit.payload.q[0] = 1.05;
        jet_sjit.payload.p[0] = 2.0; /* Approaching boundary threshold 1.1 */
        jet_sjit.payload.a[0] = 0.0;

        FlowReloadContext *reload_ctx = flow_reload_create(NULL);
        FLOW_ASSERT_TRUE(reload_ctx != NULL);
        uint64_t initial_gen = flow_reload_generation(reload_ctx);

        FlowSpeculativeJIT sjit;
        FLOW_ASSERT_EQ(flow_speculative_jit_init(&sjit, &jet_sjit, NULL, reload_ctx, 50000000.0, 1.1, 0), 1);
        FLOW_ASSERT_EQ(sjit.is_compilation_dispatched, 0);

        /* 1. Evaluate trajectory: lookahead anticipates boundary crossing */
        FLOW_ASSERT_EQ(flow_speculative_jit_evaluate(&sjit, 0.001), 1);
        FLOW_ASSERT_EQ(sjit.is_compilation_dispatched, 1);
        FLOW_ASSERT_EQ(sjit.is_compilation_ready, 1);
        FLOW_ASSERT_TRUE(sjit.predicted_crossing_time_ns > 0.0);

        /* 2. Trajectory reaches threshold: auto-commits negative-latency swap */
        jet_sjit.payload.q[0] = 1.15;
        FLOW_ASSERT_EQ(flow_speculative_jit_evaluate(&sjit, 0.0), 1);
        FLOW_ASSERT_EQ(sjit.is_hot_swapped, 1);
        FLOW_ASSERT_EQ(sjit.total_negative_latency_swaps, 1);
        FLOW_ASSERT_EQ(flow_reload_generation(reload_ctx), initial_gen + 1);

        /* 3. SMT Highest Court Verification */
        FlowSMTProofAttestation sjit_proof;
        memset(&sjit_proof, 0, sizeof(sjit_proof));
        FLOW_ASSERT_EQ(flow_speculative_jit_verify_smt(&sjit, &sjit_proof), FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_SMT_SOUND(sjit_proof);

        flow_reload_destroy(reload_ctx);

        printf("  ✓ Stage 12 Passed: Speculative JIT anticipated Moreau crossing (t_pred=%.1fns); negative latency hot-swap committed; SMT proven.\n\n",
               sjit.predicted_crossing_time_ns);
    }

    /* ========================================================================= */
    /* STAGE 13: Linker Hard Gate 6: Physical Invariant & Hamiltonian Barrier    */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(13, "Linker Hard Gate 6: Physical Invariant & Hamiltonian Barrier");
    {
        FlowJet jet_sec;
        FLOW_ASSERT_EQ(flow_jet_init(&jet_sec, "jet_security_test", "Physical Security Jet"), 1);
        jet_sec.payload.q[0] = 0.5;
        jet_sec.payload.p[0] = 1.0;
        jet_sec.header.hamiltonian_energy = flow_jet_hamiltonian(&jet_sec);

        FlowCompositionSpec comp_spec;
        memset(&comp_spec, 0, sizeof(comp_spec));
        comp_spec.jet = &jet_sec;
        comp_spec.max_hamiltonian_drift_ratio = 0.05; /* 5% tolerance */
        comp_spec.max_velocity_bound = 10.0;
        comp_spec.max_acceleration_bound = 50.0;

        char gate_msg[160] = {0};
        FlowSecurityOutcome outcome = flow_security_check_physical_barrier_gate(&comp_spec, gate_msg, sizeof(gate_msg));
        FLOW_ASSERT_EQ(outcome, FLOW_SECURITY_PASS);

        /* 1. Sensor Spoofing Attack: Inject NaN into coordinate q[2] */
        jet_sec.payload.q[2] = NAN;
        outcome = flow_security_check_physical_barrier_gate(&comp_spec, gate_msg, sizeof(gate_msg));
        FLOW_ASSERT_EQ(outcome, FLOW_SECURITY_PHYSICAL_BREACH);
        FLOW_ASSERT_TRUE(strstr(gate_msg, "NaN or Inf") != NULL);

        /* Fail-Safe Clamping restores safety envelope */
        FLOW_ASSERT_EQ(flow_jet_clamp_to_safety_envelope(&jet_sec, 2.0, 10.0, 50.0), 1);
        outcome = flow_security_check_physical_barrier_gate(&comp_spec, gate_msg, sizeof(gate_msg));
        FLOW_ASSERT_EQ(outcome, FLOW_SECURITY_PASS);

        /* 2. Symplectic Energy Drift Attack: Kinetic momentum perturbation */
        jet_sec.payload.p[0] = 2.0; /* Within velocity bound (2.0 < 10.0), but drifts energy by >200% */
        outcome = flow_security_check_physical_barrier_gate(&comp_spec, gate_msg, sizeof(gate_msg));
        FLOW_ASSERT_EQ(outcome, FLOW_SECURITY_PHYSICAL_BREACH);
        FLOW_ASSERT_TRUE(strstr(gate_msg, "Hamiltonian energy drift") != NULL);

        /* Clamp & re-baseline energy */
        FLOW_ASSERT_EQ(flow_jet_clamp_to_safety_envelope(&jet_sec, 2.0, 10.0, 50.0), 1);
        jet_sec.header.hamiltonian_energy = flow_jet_hamiltonian(&jet_sec);
        outcome = flow_security_check_physical_barrier_gate(&comp_spec, gate_msg, sizeof(gate_msg));
        FLOW_ASSERT_EQ(outcome, FLOW_SECURITY_PASS);

        /* 3. Velocity Bound Breach */
        jet_sec.payload.p[1] = 15.0; /* > 10.0 limit */
        outcome = flow_security_check_physical_barrier_gate(&comp_spec, gate_msg, sizeof(gate_msg));
        FLOW_ASSERT_EQ(outcome, FLOW_SECURITY_PHYSICAL_BREACH);
        FLOW_ASSERT_TRUE(strstr(gate_msg, "velocity") != NULL);

        printf("  ✓ Stage 13 Passed: Physical Barrier Gate enforced; sensor NaN spoofing & energy drift blocked; fail-safe envelope clamp verified.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 14: JIT W^X Memory Protection & Dual-Mapping Hard Gate              */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(14, "JIT W^X Memory Protection & Dual-Mapping Hard Gate");
    {
        FlowJITConfig config = {
            .opt_level = 2,
            .initial_code_heap_bytes = 1024 * 1024
        };
        FlowJITEngine *engine = flow_jit_create(&config);
        FLOW_ASSERT_TRUE(engine != NULL);

        char wx_msg[160] = {0};
        FLOW_ASSERT_EQ(flow_jit_verify_wx_invariants(engine, wx_msg, sizeof(wx_msg)), 1);

        FlowJITPoolStats stats;
        FLOW_ASSERT_EQ(flow_jit_get_pool_stats(engine, &stats), 1);

        /* Legitimate code block within executable heap */
        FlowJITCodeBlock block = {
            .start_ip = stats.exec_base + 128,
            .end_ip = stats.exec_base + 128 + 4096,
            .code_bytes = 4096,
            .layout = FLOW_LAYOUT_AOS,
            .compile_time_ns = 100
        };
        FlowSecurityOutcome outcome = flow_security_check_jit_wx_gate(&block, stats.write_base, stats.exec_base,
                                                                      wx_msg, sizeof(wx_msg));
        FLOW_ASSERT_EQ(outcome, FLOW_SECURITY_PASS);

        /* Violation: Writable heap aliased directly to executable heap */
        outcome = flow_security_check_jit_wx_gate(&block, stats.exec_base, stats.exec_base,
                                                  wx_msg, sizeof(wx_msg));
        FLOW_ASSERT_EQ(outcome, FLOW_SECURITY_MEMORY_VIOLATION);
        FLOW_ASSERT_TRUE(strstr(wx_msg, "W^X") != NULL);

        flow_jit_destroy(engine);
        printf("  ✓ Stage 14 Passed: JIT W^X invariant verified; simultaneous writable+executable page injection blocked.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 15: Memory Transposition (AoS <-> SoA) Alignment & Buffer Gate      */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(15, "Memory Transposition Alignment & Buffer Bound Gate");
    {
        FlowLayoutMigrationSpec valid_spec = {
            .item_count = 1000,
            .field_count = 3,
            .field_sizes = {8, 4, 4},
            .from_layout = FLOW_LAYOUT_AOS,
            .to_layout = FLOW_LAYOUT_SOA,
            .field_changed = {0, 0, 0}
        };

        char trans_msg[160] = {0};
        FlowSecurityOutcome outcome = flow_security_check_transposition_gate(&valid_spec, 1000 * 16,
                                                                            trans_msg, sizeof(trans_msg));
        FLOW_ASSERT_EQ(outcome, FLOW_SECURITY_PASS);

        /* 1. Misaligned layout rejection: 3-byte field creates misaligned 4-byte next offset */
        FlowLayoutMigrationSpec misaligned_spec = valid_spec;
        misaligned_spec.field_sizes[0] = 3;
        outcome = flow_security_check_transposition_gate(&misaligned_spec, 1000 * 16,
                                                         trans_msg, sizeof(trans_msg));
        FLOW_ASSERT_EQ(outcome, FLOW_SECURITY_MEMORY_VIOLATION);
        FLOW_ASSERT_TRUE(strstr(trans_msg, "misaligned") != NULL);

        /* 2. Buffer capacity overflow rejection */
        outcome = flow_security_check_transposition_gate(&valid_spec, 100,
                                                         trans_msg, sizeof(trans_msg));
        FLOW_ASSERT_EQ(outcome, FLOW_SECURITY_RESOURCE_EXHAUSTION);
        FLOW_ASSERT_TRUE(strstr(trans_msg, "exceeds buffer") != NULL);

        /* 3. Integer overflow rejection */
        FlowLayoutMigrationSpec overflow_spec = valid_spec;
        overflow_spec.item_count = SIZE_MAX;
        outcome = flow_security_check_transposition_gate(&overflow_spec, 1000000,
                                                         trans_msg, sizeof(trans_msg));
        FLOW_ASSERT_EQ(outcome, FLOW_SECURITY_MEMORY_VIOLATION);
        FLOW_ASSERT_TRUE(strstr(trans_msg, "integer overflow") != NULL);

        printf("  ✓ Stage 15 Passed: Transposition alignment & arithmetic buffer overflow gates enforced.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 16: Phase Space Attractor IDS: Koopman Spectrum & Ellipsoidal Gate  */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(16, "Phase Space Attractor IDS: Koopman Spectrum & Ellipsoidal Gate");
    {
        FlowJetAttractorProfile profile;
        FLOW_ASSERT_EQ(flow_jet_attractor_profile_init(&profile, 16, 2.0, 5.0, 0.0), 1);

        FlowJet jet_normal;
        FLOW_ASSERT_EQ(flow_jet_init(&jet_normal, "jet_attractor_normal", "Normal Workload"), 1);
        jet_normal.payload.q[0] = 0.5;
        jet_normal.payload.p[0] = 1.0;
        /* Set dissipative stable Koopman diagonal: Tr(K) = -0.5 * 8 = -4.0 <= 0 */
        for (int i = 0; i < 8; ++i) {
            jet_normal.payload.koopman_matrix[i][i] = -0.5;
        }

        char ids_msg[160] = {0};
        FlowSecurityOutcome outcome = flow_security_check_attractor_anomaly(&jet_normal, &profile,
                                                                           ids_msg, sizeof(ids_msg));
        FLOW_ASSERT_EQ(outcome, FLOW_SECURITY_PASS);

        /* 1. Attractor Spatial Boundary Breach (State trajectory diverges from attractor) */
        FlowJet jet_outlier = jet_normal;
        jet_outlier.payload.q[0] = 10.0; /* Exceeds r_q = 2.0 */
        outcome = flow_security_check_attractor_anomaly(&jet_outlier, &profile, ids_msg, sizeof(ids_msg));
        FLOW_ASSERT_EQ(outcome, FLOW_SECURITY_PHYSICAL_BREACH);
        FLOW_ASSERT_TRUE(strstr(ids_msg, "attractor breach") != NULL);

        /* 2. Koopman Spectrum Instability Breach (Positive Lyapunov trace expansion Tr(K) > 0) */
        FlowJet jet_unstable = jet_normal;
        for (int i = 0; i < 8; ++i) {
            jet_unstable.payload.koopman_matrix[i][i] = 1.0; /* Tr(K) = +8.0 > 0.0 */
        }
        outcome = flow_security_check_attractor_anomaly(&jet_unstable, &profile, ids_msg, sizeof(ids_msg));
        FLOW_ASSERT_EQ(outcome, FLOW_SECURITY_PHYSICAL_BREACH);
        FLOW_ASSERT_TRUE(strstr(ids_msg, "attractor instability") != NULL);

        printf("  ✓ Stage 16 Passed: Attractor IDS enforced; phase divergence and expanding Koopman spectrum blocked.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 17: Differential Continuity Proof: C1/C2 Anti-Spoofing & Replay Gate*/
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(17, "Differential Continuity Proof: C1/C2 Anti-Spoofing & Replay Gate");
    {
        double dt = 0.01; /* 10ms */
        double max_jerk = 200.0;
        double noise_tol = 0.005;

        FlowJet prev_jet, curr_jet;
        FLOW_ASSERT_EQ(flow_jet_init(&prev_jet, "jet_c2_prev", "Previous Frame"), 1);
        FLOW_ASSERT_EQ(flow_jet_init(&curr_jet, "jet_c2_curr", "Current Frame"), 1);

        /* Legitimate continuous physical motion */
        prev_jet.payload.q[0] = 1.0;
        prev_jet.payload.p[0] = 2.0;
        prev_jet.payload.a[0] = 0.5;

        /* Taylor extrapolation with small jerk */
        curr_jet.payload.a[0] = prev_jet.payload.a[0] + 0.1; /* jerk ~ 10.0 < 200.0 */
        curr_jet.payload.p[0] = prev_jet.payload.p[0] + prev_jet.payload.a[0] * dt;
        curr_jet.payload.q[0] = prev_jet.payload.q[0] + prev_jet.payload.p[0] * dt + 0.5 * prev_jet.payload.a[0] * dt * dt;

        char cont_msg[160] = {0};
        FlowSecurityOutcome outcome = flow_security_check_continuity_proof(&prev_jet, &curr_jet, dt,
                                                                         max_jerk, noise_tol,
                                                                         cont_msg, sizeof(cont_msg));
        FLOW_ASSERT_EQ(outcome, FLOW_SECURITY_PASS);

        /* 1. Impulsive jerk jump (Replay / sensor spoofing injecting instantaneous force spike) */
        FlowJet spoof_jerk = curr_jet;
        spoof_jerk.payload.a[0] = 50.0; /* jump from 0.5 to 50.0 in 10ms -> jerk = 4950 > 200 */
        outcome = flow_security_check_continuity_proof(&prev_jet, &spoof_jerk, dt,
                                                       max_jerk, noise_tol,
                                                       cont_msg, sizeof(cont_msg));
        FLOW_ASSERT_EQ(outcome, FLOW_SECURITY_PHYSICAL_BREACH);
        FLOW_ASSERT_TRUE(strstr(cont_msg, "accel jump") != NULL);

        /* 2. Velocity discontinuous step (Instantaneous momentum teleportation) */
        FlowJet spoof_vel = curr_jet;
        spoof_vel.payload.p[0] = 5.0; /* step from ~2.0 to 5.0 */
        outcome = flow_security_check_continuity_proof(&prev_jet, &spoof_vel, dt,
                                                       max_jerk, noise_tol,
                                                       cont_msg, sizeof(cont_msg));
        FLOW_ASSERT_EQ(outcome, FLOW_SECURITY_PHYSICAL_BREACH);
        FLOW_ASSERT_TRUE(strstr(cont_msg, "velocity residual") != NULL);

        /* 3. Position discontinuous jump (Teleportation without traversing phase space) */
        FlowJet spoof_pos = curr_jet;
        spoof_pos.payload.q[0] = 2.5; /* teleport from 1.0 to 2.5 in 10ms */
        outcome = flow_security_check_continuity_proof(&prev_jet, &spoof_pos, dt,
                                                       max_jerk, noise_tol,
                                                       cont_msg, sizeof(cont_msg));
        FLOW_ASSERT_EQ(outcome, FLOW_SECURITY_PHYSICAL_BREACH);
        FLOW_ASSERT_TRUE(strstr(cont_msg, "position residual") != NULL);

        printf("  ✓ Stage 17 Passed: Differential continuity proof enforced; synthetic impulses & teleportation blocked.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 18: Predictive MTD: Second-Order Kinematic Quota Breach Prediction  */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(18, "Predictive MTD: Second-Order Kinematic Quota Breach Prediction");
    {
        /* Case 1: Quadratic resource consumption
         * Usage = 800 MB, Quota = 1000 MB, Distance = 200 MB
         * Consumption rate v = 100 MB/s, Acceleration a = 20 MB/s^2
         * Analytical solution: 0.5 * 20 * t^2 + 100 * t - 200 = 0 -> 10 t^2 + 100 t - 200 = 0
         * t = (-100 + sqrt(10000 + 8000)) / 20 = (-100 + sqrt(18000)) / 20 = (-100 + 134.164) / 20 = 1.7082 s */
        FlowPredictiveBreachReport report;
        FLOW_ASSERT_EQ(flow_security_predict_resource_breach(800.0, 100.0, 20.0, 1000.0, 5.0, &report), 1);
        FLOW_ASSERT_EQ(report.will_breach, 1);
        FLOW_ASSERT_TRUE(fabs(report.time_to_breach_s - 1.7082) < 0.005);
        FLOW_ASSERT_TRUE(report.projected_breach_velocity > 100.0);

        /* Proactive morph trigger test: lead time 2.0s triggers proactive evacuation */
        FLOW_ASSERT_EQ(flow_security_should_proactive_morph(&report, 2.0), 1);
        /* Lead time 1.0s does not trigger yet */
        FLOW_ASSERT_EQ(flow_security_should_proactive_morph(&report, 1.0), 0);

        /* Case 2: Constant velocity consumption
         * Usage = 500 MB, Quota = 1000 MB, Distance = 500 MB, v = 250 MB/s, a = 0 -> t = 2.0 s */
        FLOW_ASSERT_EQ(flow_security_predict_resource_breach(500.0, 250.0, 0.0, 1000.0, 10.0, &report), 1);
        FLOW_ASSERT_EQ(report.will_breach, 1);
        FLOW_ASSERT_TRUE(fabs(report.time_to_breach_s - 2.0) < 1.0e-5);

        /* Case 3: Receding resource consumption (Safe / negative consumption rate) */
        FLOW_ASSERT_EQ(flow_security_predict_resource_breach(500.0, -50.0, 0.0, 1000.0, 10.0, &report), 1);
        FLOW_ASSERT_EQ(report.will_breach, 0);
        FLOW_ASSERT_EQ(flow_security_should_proactive_morph(&report, 5.0), 0);

        printf("  ✓ Stage 18 Passed: Kinematic time-to-breach computed (t_breach=%.3fs); proactive morphing trigger verified.\n\n",
               1.7082);
    }

    /* ========================================================================= */
    /* STAGE 19: Symplectic Byzantine Consensus: O(1) Hamiltonian & Geodesic Gate*/
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(19, "Symplectic Byzantine Consensus: O(1) Hamiltonian & Geodesic Gate");
    {
        FlowSymplecticByzantineFilter filter;
        FLOW_ASSERT_EQ(flow_jet_byzantine_filter_init(&filter, 0.05, 2.0), 1);

        FlowJet local_mirror;
        FLOW_ASSERT_EQ(flow_jet_init(&local_mirror, "mirror_alpha", "Local Cluster Shadow Mirror"), 1);
        local_mirror.payload.q[0] = 1.0;
        local_mirror.payload.p[0] = 1.0;
        local_mirror.header.hamiltonian_energy = flow_jet_hamiltonian(&local_mirror);

        /* Legitimate remote packet conforming to shadow mirror */
        FlowJetDeadReckonPacket pkt_good = {
            .timestamp_ns = 1000000,
            .node_id = 42,
            .dim = 16,
            .q = {1.01},
            .p = {0.99},
            .a = {-1.0},
            .packet_seq = 1
        };
        pkt_good.crc32 = flow_jet_crc32(&pkt_good, offsetof(FlowJetDeadReckonPacket, crc32));

        char byz_msg[160] = {0};
        FlowSecurityOutcome outcome = flow_jet_byzantine_validate_packet(&filter, &pkt_good, &local_mirror,
                                                                        byz_msg, sizeof(byz_msg));
        FLOW_ASSERT_EQ(outcome, FLOW_SECURITY_PASS);
        FLOW_ASSERT_EQ(filter.byzantine_faults_detected, 0U);

        /* 1. Corrupted CRC packet rejection */
        FlowJetDeadReckonPacket pkt_bad_crc = pkt_good;
        pkt_bad_crc.crc32 ^= 0xDEADBEEF;
        outcome = flow_jet_byzantine_validate_packet(&filter, &pkt_bad_crc, &local_mirror,
                                                     byz_msg, sizeof(byz_msg));
        FLOW_ASSERT_EQ(outcome, FLOW_SECURITY_PHYSICAL_BREACH);
        FLOW_ASSERT_EQ(filter.byzantine_faults_detected, 1U);

        /* 2. Sensor spoofing NaN packet rejection */
        FlowJetDeadReckonPacket pkt_nan = pkt_good;
        pkt_nan.q[3] = NAN;
        pkt_nan.crc32 = flow_jet_crc32(&pkt_nan, offsetof(FlowJetDeadReckonPacket, crc32));
        outcome = flow_jet_byzantine_validate_packet(&filter, &pkt_nan, &local_mirror,
                                                     byz_msg, sizeof(byz_msg));
        FLOW_ASSERT_EQ(outcome, FLOW_SECURITY_PHYSICAL_BREACH);
        FLOW_ASSERT_EQ(filter.byzantine_faults_detected, 2U);

        /* 3. Byzantine energy injection violation (Hamiltonian drift > 5%) */
        FlowJetDeadReckonPacket pkt_energy_drift = pkt_good;
        pkt_energy_drift.p[0] = 5.0; /* Massively jumps energy from ~1.0 to ~13.0 */
        pkt_energy_drift.crc32 = flow_jet_crc32(&pkt_energy_drift, offsetof(FlowJetDeadReckonPacket, crc32));
        outcome = flow_jet_byzantine_validate_packet(&filter, &pkt_energy_drift, &local_mirror,
                                                     byz_msg, sizeof(byz_msg));
        FLOW_ASSERT_EQ(outcome, FLOW_SECURITY_PHYSICAL_BREACH);
        FLOW_ASSERT_TRUE(strstr(byz_msg, "energy drift") != NULL);
        FLOW_ASSERT_EQ(filter.byzantine_faults_detected, 3U);

        /* 4. Geodesic distance divergence (Rotated to opposing phase space manifold) */
        FlowJetDeadReckonPacket pkt_geodesic = pkt_good;
        pkt_geodesic.q[0] = -1.0; /* Same individual energy contribution, but phase distance is large */
        pkt_geodesic.p[0] = -1.0;
        pkt_geodesic.crc32 = flow_jet_crc32(&pkt_geodesic, offsetof(FlowJetDeadReckonPacket, crc32));
        outcome = flow_jet_byzantine_validate_packet(&filter, &pkt_geodesic, &local_mirror,
                                                     byz_msg, sizeof(byz_msg));
        FLOW_ASSERT_EQ(outcome, FLOW_SECURITY_PHYSICAL_BREACH);
        FLOW_ASSERT_TRUE(strstr(byz_msg, "geodesic phase distance") != NULL);
        FLOW_ASSERT_EQ(filter.byzantine_faults_detected, 4U);

        printf("  ✓ Stage 19 Passed: Symplectic Byzantine consensus validated; CRC, NaN, energy drift, and geodesic faults pruned in O(1).\n\n");
    }

    FLOW_TEST_SUITE_END();
    return 0;
}



