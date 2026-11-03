#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "hott_test_kit.h"
#include "flow_bridge.h"
#include "flow_gpu.h"
#include "reload.h"

#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* Helper dummy operators for bridge batch test */
static int batch_dummy_op(const void *in, void *out) {
    if (!in || !out)
        return -1;
    *(int64_t *)out = *(const int64_t *)in;
    return 0;
}

static int batch_dummy_fallback(const void *in, void *out) {
    (void)in;
    if (out)
        *(int64_t *)out = 0;
    return 0;
}

int main(void) {
    FLOW_TEST_SUITE_BEGIN(
        "Linux 7.4-rc1 Hardware Grounding: UMA Shared Memory & Wave64 Batch 2-Jet Sieve");

    /* =====================================================================
     * Stage 1: UMA 4KB/16KB Page-Aligned Shared Memory Pool
     * ===================================================================== */
    FLOW_STAGE_BEGIN(1, "Unified Memory Architecture (UMA) Page-Aligned Shared Pool");
    {
        FlowUmaBuffer buf;
        memset(&buf, 0, sizeof(buf));

        int alloc_res = flow_uma_buffer_alloc(&buf, 32768);
        FLOW_ASSERT_EQ(alloc_res, 0);
        FLOW_ASSERT_TRUE(buf.cpu_ptr != NULL);
        FLOW_ASSERT_TRUE(buf.gpu_shared_handle == buf.cpu_ptr);
        FLOW_ASSERT_TRUE(buf.page_size == 4096U || buf.page_size == 16384U);
        FLOW_ASSERT_TRUE(buf.capacity_bytes >= 32768U);
        FLOW_ASSERT_EQ(buf.capacity_bytes % buf.page_size, 0U);
        FLOW_ASSERT_EQ((uintptr_t)buf.cpu_ptr % buf.page_size, 0U);
        FLOW_ASSERT_EQ(buf.is_uma_coherent, 1);

        /* Verify read/write zero-defect memory access */
        uint8_t *byte_ptr = (uint8_t *)buf.cpu_ptr;
        for (size_t i = 0; i < buf.capacity_bytes; i += 64) {
            byte_ptr[i] = (uint8_t)(i & 0xFF);
        }
        for (size_t i = 0; i < buf.capacity_bytes; i += 64) {
            FLOW_ASSERT_EQ(byte_ptr[i], (uint8_t)(i & 0xFF));
        }

        flow_uma_buffer_free(&buf);
        FLOW_ASSERT_TRUE(buf.cpu_ptr == NULL);
        FLOW_ASSERT_EQ(buf.capacity_bytes, 0U);
    }

    /* =====================================================================
     * Stage 2: Wave64 Batch 2-Jet Sieve Stepper (64 Parallel Channels)
     * ===================================================================== */
    FLOW_STAGE_BEGIN(2, "Wave64 Batch 2-Jet Sieve Stepper & Ballot Bitmasks");
    {
        FlowPhysicalBridge bridge;
        memset(&bridge, 0, sizeof(bridge));
        bridge.engineering_operator = batch_dummy_op;
        bridge.envelope.fallback_operator = batch_dummy_fallback;
        bridge.envelope.max_latency_cycles = 100000ULL;
        bridge.jet_envelope.v_min = -2.0;
        bridge.jet_envelope.v_max = 2.0;
        bridge.jet_envelope.max_lipschitz_d1 = 5.0;
        bridge.jet_envelope.max_curvature_d2 = 20.0;
        bridge.jet_envelope.lyapunov_decay_rate = 0.5;
        bridge.jet_envelope.transversality_floor = 1e-4;
        bridge.jet_envelope.required_kan_sieve = FLOW_JET_SIEVE_CANONICAL_ALL;
        bridge.jet_envelope.kan_boundary_filter = FLOW_JET_SIEVE_CANONICAL_ALL;

        FlowJetBatch64 batch_in;
        memset(&batch_in, 0, sizeof(batch_in));

        /* Populate 64 channels:
         * Channels 0..31: Sound dissipative 2-Jet interior states
         * Channels 32..63: Ruptured states (exceeding v_max or curvature d2)
         */
        for (int i = 0; i < 32; ++i) {
            batch_in.states[i].v = 0.5;
            batch_in.states[i].d1 = -1.0;
            batch_in.states[i].d2 = 2.0;
        }
        for (int i = 32; i < 64; ++i) {
            batch_in.states[i].v = 10.0; /* Severe violation: v_max is 2.0 */
            batch_in.states[i].d1 = 1.0;
            batch_in.states[i].d2 = 50.0;
        }
        batch_in.active_mask = 0xFFFFFFFFFFFFFFFFULL; /* All 64 channels active */

        FlowJetBatchResult64 batch_out;
        int status = flow_bridge_execute_hott_jet_batch64(&bridge, &batch_in, &batch_out);
        FLOW_ASSERT_EQ(status, FLOW_BRIDGE_SUCCESS);
        FLOW_ASSERT_EQ(batch_out.active_count, 64U);
        FLOW_ASSERT_EQ(batch_out.interior_count, 32U);
        FLOW_ASSERT_EQ(batch_out.fallback_count, 32U);
        FLOW_ASSERT_EQ(batch_out.interior_ballot, 0x00000000FFFFFFFFULL);
        FLOW_ASSERT_EQ(batch_out.fallback_ballot, 0xFFFFFFFF00000000ULL);

        /* Inspect boundary tags for ruptured channels */
        for (int i = 32; i < 64; ++i) {
            FLOW_ASSERT_TRUE(flow_hott_is_boundary_tag(batch_out.boundary_tags[i]));
        }

        /* Test sparse active mask: only channels 0, 15, 31 (interior) and 45, 63 (fallback) */
        batch_in.active_mask =
            (1ULL << 0) | (1ULL << 15) | (1ULL << 31) | (1ULL << 45) | (1ULL << 63);
        status = flow_bridge_execute_hott_jet_batch64(&bridge, &batch_in, &batch_out);
        FLOW_ASSERT_EQ(status, FLOW_BRIDGE_SUCCESS);
        FLOW_ASSERT_EQ(batch_out.active_count, 5U);
        FLOW_ASSERT_EQ(batch_out.interior_count, 3U);
        FLOW_ASSERT_EQ(batch_out.fallback_count, 2U);
        FLOW_ASSERT_EQ(batch_out.interior_ballot, (1ULL << 0) | (1ULL << 15) | (1ULL << 31));
        FLOW_ASSERT_EQ(batch_out.fallback_ballot, (1ULL << 45) | (1ULL << 63));

        /* Cubical non-degeneracy Wave64 subobject classifier + stream compaction */
        FlowJet2 sparse_jets[64];
        for (size_t i = 0; i < 64U; ++i) {
            sparse_jets[i] = flow_jet2_make((double)i, 1e-7, -1e-7); /* within degeneracy eps */
        }
        const uint32_t nondeg_idx[6] = {3U, 14U, 25U, 36U, 47U, 58U};
        uint64_t expected_mask = 0ULL;
        for (size_t k = 0; k < 6U; ++k) {
            uint32_t idx = nondeg_idx[k];
            expected_mask |= (1ULL << idx);
            sparse_jets[idx] =
                (k & 1U) ? flow_jet2_make(0.0, 0.0, 0.25) : flow_jet2_make(0.0, -0.25, 0.0);
        }
        uint64_t nd_mask = flow_jet2_nondegenerate_ballot64(sparse_jets, 64U, 1e-4, 1e-4);
        FLOW_ASSERT_EQ(nd_mask, expected_mask);
        uint32_t compacted[64] = {0};
        uint32_t compacted_cnt = flow_gpu_ballot_compact_wave64(nd_mask, 0U, compacted);
        FLOW_ASSERT_EQ(compacted_cnt, 6U);
        for (size_t k = 0; k < 6U; ++k) {
            FLOW_ASSERT_EQ(compacted[k], nondeg_idx[k]);
        }
    }

    FLOW_TEST_SUITE_END();
    return 0;
}
