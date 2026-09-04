#include "flow_test_kit.h"
#include "flow_benchmark_harness.h"
#include "flow_mock_driver.h"
#include "flow_bmf_fixture.h"
#include "flow_fixed_ring.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * ============================================================================
 * TEST SUITE #69: FLOW Developer & Testing Infrastructure Kits Test Suite
 * ============================================================================
 */

/* ------------------------------------------------------------------------- */
/* 1. Ring Buffer Types Setup                                                */
/* ------------------------------------------------------------------------- */

typedef struct {
    uint64_t id;
    double value;
} TestRingItem;

FLOW_FIXED_RING_DEFINE(TestItemRing, TestRingItem, 8);

/* ------------------------------------------------------------------------- */
/* 2. Mock Driver Declaration                                                */
/* ------------------------------------------------------------------------- */

FLOW_DECLARE_MOCK_DRIVER(mock_nvme_storage,
                         .driver_name = "mock_nvme_storage",
                         .driver_version = "v2.0",
                         .queue_depth = 4096,
                         .buffer_bytes = 128 * 1024 * 1024,
                         .zero_copy = 1,
                         .is_kernel_bypass = 1,
                         .genome_bits_required = 4,
                         .simulated_latency_cycles = 42);

/* ------------------------------------------------------------------------- */
/* 3. Domain Energy Evaluation Function for BitManifold                      */
/* ------------------------------------------------------------------------- */

static double test_hamming_energy(uint64_t cand, void *ctx) {
    (void)ctx;
    /* Target bit pattern: lower 4 bits all 1s (0x0F) */
    uint64_t target = 0x000000000000000FULL;
    uint64_t diff = (cand ^ target) & 0xFFULL;
    return (double)__builtin_popcountll(diff);
}

/* ------------------------------------------------------------------------- */
/* Main Test Program                                                         */
/* ------------------------------------------------------------------------- */

int main(void) {
    FLOW_TEST_SUITE_BEGIN("Developer & Testing Infrastructure Kits (#69)");

    /* ===================================================================== */
    /* STAGE 1: Zero-Heap Fixed Ring Buffer Primitive (flow_fixed_ring.h)    */
    /* ===================================================================== */
    FLOW_STAGE_BEGIN(1, "Zero-Heap Fixed Ring Buffer (flow_fixed_ring.h)");
    {
        TestItemRing ring;
        TestItemRing_init(&ring);

        FLOW_ASSERT_TRUE(TestItemRing_is_empty(&ring));
        FLOW_ASSERT_FALSE(TestItemRing_is_full(&ring));
        FLOW_ASSERT_EQ(TestItemRing_count(&ring), 0);
        FLOW_ASSERT_EQ(ring.capacity, 8);
        FLOW_ASSERT_EQ(ring.mask, 7);

        /* Push 8 items up to capacity */
        for (uint64_t i = 0; i < 8; ++i) {
            TestRingItem item = { .id = i + 1, .value = (double)(i + 1) * 10.0 };
            int ok = TestItemRing_push(&ring, &item);
            FLOW_ASSERT_TRUE(ok);
        }

        FLOW_ASSERT_TRUE(TestItemRing_is_full(&ring));
        FLOW_ASSERT_EQ(TestItemRing_count(&ring), 8);

        /* 9th push must fail (no overflow) */
        TestRingItem overflow_item = { .id = 999, .value = 999.0 };
        FLOW_ASSERT_FALSE(TestItemRing_push(&ring, &overflow_item));

        /* Peek first item */
        TestRingItem *peeked = NULL;
        FLOW_ASSERT_TRUE(TestItemRing_peek(&ring, &peeked));
        FLOW_ASSERT_TRUE(peeked != NULL && peeked->id == 1);

        /* Pop 3 items (FIFO order) */
        for (uint64_t i = 0; i < 3; ++i) {
            TestRingItem popped;
            FLOW_ASSERT_TRUE(TestItemRing_pop(&ring, &popped));
            FLOW_ASSERT_EQ(popped.id, i + 1);
        }
        FLOW_ASSERT_EQ(TestItemRing_count(&ring), 5);
        FLOW_ASSERT_FALSE(TestItemRing_is_full(&ring));

        /* Push 3 items to exercise wrap-around */
        for (uint64_t i = 8; i < 11; ++i) {
            TestRingItem item = { .id = i + 1, .value = (double)(i + 1) * 10.0 };
            FLOW_ASSERT_TRUE(TestItemRing_push(&ring, &item));
        }
        FLOW_ASSERT_TRUE(TestItemRing_is_full(&ring));

        /* Pop all remaining 8 items and verify ordering */
        uint64_t expected_ids[8] = { 4, 5, 6, 7, 8, 9, 10, 11 };
        for (size_t i = 0; i < 8; ++i) {
            TestRingItem popped;
            FLOW_ASSERT_TRUE(TestItemRing_pop(&ring, &popped));
            FLOW_ASSERT_EQ(popped.id, expected_ids[i]);
        }
        FLOW_ASSERT_TRUE(TestItemRing_is_empty(&ring));

        /* Test Overwrite Push */
        for (uint64_t i = 0; i < 12; ++i) {
            TestRingItem item = { .id = i + 1, .value = (double)(i + 1) };
            TestItemRing_push_overwrite(&ring, &item);
        }
        /* Ring capacity is 8, so items remaining must be 5..12 */
        FLOW_ASSERT_EQ(TestItemRing_count(&ring), 8);
        for (uint64_t i = 0; i < 8; ++i) {
            TestRingItem popped;
            FLOW_ASSERT_TRUE(TestItemRing_pop(&ring, &popped));
            FLOW_ASSERT_EQ(popped.id, i + 5);
        }

        /* Verify 64-byte cache line alignment of head and tail */
        uintptr_t head_addr = (uintptr_t)&ring.head;
        uintptr_t tail_addr = (uintptr_t)&ring.tail;
        FLOW_ASSERT_EQ(head_addr % 64, 0);
        FLOW_ASSERT_EQ(tail_addr % 64, 0);
        printf("  ✓ Ring Buffer FIFO, wrap-around, overwrite, and 64-byte cacheline isolation sound.\n\n");
    }

    /* ===================================================================== */
    /* STAGE 2: Minimal ABI Mock Driver Declarator (flow_mock_driver.h)      */
    /* ===================================================================== */
    FLOW_STAGE_BEGIN(2, "Minimal ABI Mock Driver Declarator (flow_mock_driver.h)");
    {
        FlowPrimitiveRegistry registry;
        flow_primitive_registry_init(&registry);
        FLOW_ASSERT_TRUE(flow_primitive_register(&registry, &mock_nvme_storage));
        FLOW_ASSERT_EQ(flow_primitive_count(&registry), 1);

        const FlowPrimitiveDriver *drv = flow_primitive_lookup(&registry, "mock_nvme_storage");
        FLOW_ASSERT_TRUE(drv != NULL);
        FLOW_ASSERT_STR_EQ(drv->driver_name, "mock_nvme_storage");
        FLOW_ASSERT_STR_EQ(drv->driver_version, "v1.0-mock");

        FlowHardwareBounds bounds;
        FLOW_ASSERT_TRUE(drv->get_hardware_bounds(&bounds));
        FLOW_ASSERT_STR_EQ(bounds.name, "mock_nvme_storage");
        FLOW_ASSERT_EQ(bounds.max_queue_depth, 4096);
        FLOW_ASSERT_EQ(bounds.max_buffer_bytes, 128 * 1024 * 1024);
        FLOW_ASSERT_EQ(bounds.supports_zero_copy, 1);
        FLOW_ASSERT_EQ(bounds.is_kernel_bypass, 1);
        FLOW_ASSERT_EQ(bounds.genome_bits_required, 4);

        char payload[] = "NVMe-over-Fabrics Zero-Copy Block Transfer";
        FlowPrimitiveContext ctx = {
            .active_genome = 0x01,
            .user_data = payload,
            .data_len = strlen(payload),
            .flags = 0
        };
        FlowPrimitiveResult res;
        FLOW_ASSERT_EQ(drv->execute_primitive(&ctx, &res), 0);
        FLOW_ASSERT_EQ(res.status_code, 0);
        FLOW_ASSERT_EQ(res.bytes_transferred, strlen(payload));
        FLOW_ASSERT_EQ(res.latency_cycles, 42);
        FLOW_ASSERT_EQ(res.zero_copy_active, 1);
        printf("  ✓ Mock Driver declarative macro expanded 3-function ABI cleanly.\n\n");
    }

    /* ===================================================================== */
    /* STAGE 3: BitManifold Energy Fixture (flow_bmf_fixture.h)              */
    /* ===================================================================== */
    FLOW_STAGE_BEGIN(3, "BitManifold Domain Energy Fixture (flow_bmf_fixture.h)");
    {
        uint64_t initial_g = 0x00ULL;
        uint64_t hard_mask = 0xFFULL; /* 8 malleable bits */
        FlowBMFAnnealResult anneal_res;

        flow_bmf_anneal_loop(initial_g, hard_mask, test_hamming_energy, NULL,
                             300, 10.0, 0.01, 0x12345678ULL, &anneal_res);

        FLOW_ASSERT_EQ(anneal_res.initial_genome, 0x00ULL);
        FLOW_ASSERT_EQ(anneal_res.initial_energy, 4.0); /* 0x00 has 4 bits diff from 0x0F */
        FLOW_ASSERT_EQ(anneal_res.best_energy, 0.0);   /* Perfect target reached */
        FLOW_ASSERT_EQ(anneal_res.best_genome, 0x0FULL);
        FLOW_ASSERT_TRUE(anneal_res.transitions_accepted > 0);

        /* Test 1-line anneal transition helper */
        uint64_t rng = 0x5555ULL;
        uint64_t next_g = flow_bmf_anneal_transition(0x00ULL, hard_mask, test_hamming_energy,
                                                     NULL, &rng, 0.0); /* Greedy (T=0) */
        FLOW_ASSERT_TRUE(next_g == 0x00ULL || next_g == 0x01ULL || next_g == 0x02ULL || next_g == 0x04ULL || next_g == 0x08ULL);
        printf("  ✓ BMF Annealing Fixture converged to global optimum (0x0F) in %llu iterations.\n\n",
               (unsigned long long)anneal_res.total_iterations);
    }

    /* ===================================================================== */
    /* STAGE 4: Test Kit Assertions & SMT Verification (flow_test_kit.h)     */
    /* ===================================================================== */
    FLOW_STAGE_BEGIN(4, "Test Kit Assertions & SMT Verification (flow_test_kit.h)");
    {
        FLOW_ASSERT_TRUE(1);
        FLOW_ASSERT_FALSE(0);
        FLOW_ASSERT_EQ(100, 100);
        FLOW_ASSERT_NE(100, 200);
        FLOW_ASSERT_STR_EQ("FLOW", "FLOW");
        FLOW_ASSERT_STR_CONTAINS("FLOW BitManifold Architecture", "BitManifold");

        /* SMT Sound Attestation */
        FlowSMTProofAttestation sound_proof;
        sound_proof.buffer_bounds_safety = FLOW_SMT_PROVEN_UNSAT;
        sound_proof.memory_quota_bound   = FLOW_SMT_PROVEN_UNSAT;
        sound_proof.shard_non_aliasing   = FLOW_SMT_PROVEN_UNSAT;
        sound_proof.determinism_invariant = FLOW_SMT_PROVEN_UNSAT;
        snprintf(sound_proof.proof_summary, sizeof(sound_proof.proof_summary),
                 "SMT SOUND: All 4 invariants verified QF_LIA UNSAT");
        FLOW_ASSERT_SMT_SOUND(sound_proof);

        /* SMT Violation Attestation */
        FlowSMTProofAttestation viol_proof;
        viol_proof.buffer_bounds_safety = FLOW_SMT_VIOLATION_SAT;
        viol_proof.memory_quota_bound   = FLOW_SMT_PROVEN_UNSAT;
        viol_proof.shard_non_aliasing   = FLOW_SMT_PROVEN_UNSAT;
        viol_proof.determinism_invariant = FLOW_SMT_PROVEN_UNSAT;
        snprintf(viol_proof.proof_summary, sizeof(viol_proof.proof_summary),
                 "SMT VIOLATION: buffer bounds 8192 exceeded ring capacity 4096");
        FLOW_ASSERT_SMT_VIOLATION(FLOW_SMT_VIOLATION_SAT, viol_proof);

        /* SMT Box Polytope One-Liner */
        FlowBoxConstraint box[2] = {
            { .name = "queue", .candidate_value = 512, .min_bound = 1, .max_bound = 4096,
              .theorem = FLOW_BOX_THEOREM_BUFFER_BOUNDS },
            { .name = "memory", .candidate_value = 16*1024*1024, .min_bound = 0, .max_bound = 64*1024*1024,
              .theorem = FLOW_BOX_THEOREM_MEMORY_QUOTA }
        };
        FLOW_ASSERT_SMT_BOX_SOUND("mock_nvme_storage", box, 2);
        printf("\n");
    }

    /* ===================================================================== */
    /* STAGE 5: Benchmark Statistical Harness (flow_benchmark_harness.h)     */
    /* ===================================================================== */
    FLOW_STAGE_BEGIN(5, "Benchmark Statistical Harness (flow_benchmark_harness.h)");
    {
        TestItemRing bench_ring;
        TestItemRing_init(&bench_ring);
        FlowBenchmarkResult b_res[2];

        /* Benchmark 1: Ring Push & Pop throughput */
        FLOW_BENCHMARK_RUN("Ring Push/Pop FastPath", 100000, {
            TestRingItem it;
            it.id = 42;
            it.value = 1.618;
            TestItemRing_push(&bench_ring, &it);
            TestRingItem out;
            TestItemRing_pop(&bench_ring, &out);
        }, &b_res[0]);

        FLOW_ASSERT_EQ(b_res[0].iterations, 100000);
        FLOW_ASSERT_TRUE(b_res[0].elapsed_ms > 0.0);
        FLOW_ASSERT_TRUE(b_res[0].qps > 100000.0); /* At least 100k ops/sec */
        FLOW_ASSERT_TRUE(b_res[0].p50_ns >= 0.0);
        FLOW_ASSERT_TRUE(b_res[0].p99_ns >= b_res[0].p50_ns);

        /* Benchmark 2: Mock Driver Execution throughput */
        char msg[] = "Quick Brown Fox";
        FlowPrimitiveContext pctx = { .user_data = msg, .data_len = sizeof(msg) };
        volatile int dummy_sink = 0;

        FLOW_BENCHMARK_RUN("Mock Driver Syscall", 100000, {
            FlowPrimitiveResult pres;
            mock_nvme_storage.execute_primitive(&pctx, &pres);
            dummy_sink += pres.status_code;
        }, &b_res[1]);
        (void)dummy_sink;

        FLOW_ASSERT_EQ(b_res[1].iterations, 100000);
        FLOW_ASSERT_TRUE(b_res[1].elapsed_ms > 0.0);
        FLOW_ASSERT_TRUE(b_res[1].qps > 100000.0);

        printf("\n");
        flow_benchmark_print_scorecard(b_res, 2);
    }

    FLOW_TEST_SUITE_END();
    return 0;
}
