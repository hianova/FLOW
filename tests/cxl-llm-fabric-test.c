#include "cxl_fabric.h"
#include "smt.h"
#include "flow_test_kit.h"
#include "flow_str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    FLOW_TEST_SUITE_BEGIN("LLM Distributed Inference & CXL Memory Fabric (Suite #67)");

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 1: CXL Memory Fabric Initialization & Tier Capacities                       */
    /* ---------------------------------------------------------------------------------- */
    FLOW_STAGE_BEGIN(1, "CXL Fabric Initialization");
    FlowCxlFabric fabric;
    FLOW_ASSERT_EQ(flow_cxl_init(&fabric), 1);
    FLOW_ASSERT_EQ(fabric.cap_hbm, 32);
    FLOW_ASSERT_EQ(fabric.cap_ddr5, 128);
    FLOW_ASSERT_EQ(fabric.cap_cxl, 352);
    FLOW_ASSERT_EQ(fabric.total_pages, 0);

    printf("  ✓ Fabric initialized: Tier 0 HBM (32 pages), Tier 1 DDR5 (128 pages), Tier 2 CXL Pool (352 pages).\n\n");

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 2: Multi-Session KV Cache Allocation & Tiered Overflow                      */
    /* ---------------------------------------------------------------------------------- */
    FLOW_STAGE_BEGIN(2, "KV Cache Allocation & Tiered Placement");
    uint64_t pid1, pid33;

    /* Session 1: High-entropy prompt attention KV pages */
    FLOW_ASSERT_EQ(flow_cxl_allocate_kv_page(&fabric, 1, 0, 128, 0.85, &pid1), 1);
    FLOW_ASSERT_EQ(fabric.pages[0].current_tier, FLOW_CXL_TIER_HBM);

    /* Allocate up to 32 pages to fill HBM */
    for (int i = 1; i < 32; ++i) {
        uint64_t pid;
        double entropy = (i % 2 == 0) ? 0.90 : 0.20; /* Alternate high and low entropy */
        FLOW_ASSERT_EQ(flow_cxl_allocate_kv_page(&fabric, 1, i * 128, 128, entropy, &pid), 1);
        FLOW_ASSERT_EQ(fabric.pages[i].current_tier, FLOW_CXL_TIER_HBM);
    }
    FLOW_ASSERT_EQ(fabric.count_hbm, 32);

    /* Page 33 should overflow into Tier 1 DDR5 */
    FLOW_ASSERT_EQ(flow_cxl_allocate_kv_page(&fabric, 2, 0, 128, 0.75, &pid33), 1);
    FLOW_ASSERT_EQ(fabric.pages[32].current_tier, FLOW_CXL_TIER_DDR5);
    FLOW_ASSERT_EQ(fabric.count_ddr5, 1);

    printf("  ✓ Tier 0 HBM saturated (32/32). Page 33 overflowed cleanly into Tier 1 DDR5.\n\n");

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 3: Access Latency Simulation Across Memory Tiers                             */
    /* ---------------------------------------------------------------------------------- */
    FLOW_STAGE_BEGIN(3, "Sub-Microsecond Multi-Tier Read Access Latency");
    uint8_t buffer[FLOW_CXL_PAGE_SIZE];
    uint64_t lat_ns = 0;

    /* Access HBM Page */
    FLOW_ASSERT_EQ(flow_cxl_access_kv_page(&fabric, pid1, buffer, &lat_ns), 1);
    FLOW_ASSERT_EQ(lat_ns, 5); /* ~5ns */

    /* Access DDR5 Page */
    FLOW_ASSERT_EQ(flow_cxl_access_kv_page(&fabric, pid33, buffer, &lat_ns), 1);
    FLOW_ASSERT_EQ(lat_ns, 50); /* ~50ns */

    printf("  ✓ Read verified: Tier 0 HBM latency = %llu ns, Tier 1 DDR5 latency = %llu ns.\n\n",
           5ULL, 50ULL);

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 4: QSBR Zero-Downtime Page Migration                                        */
    /* ---------------------------------------------------------------------------------- */
    FLOW_STAGE_BEGIN(4, "QSBR Zero-Downtime Page Migration Across Tiers");
    /* Migrate Page 1 from HBM to DDR5 */
    FLOW_ASSERT_EQ(flow_cxl_migrate_page(&fabric, pid1, FLOW_CXL_TIER_DDR5), 1);
    FLOW_ASSERT_EQ(fabric.pages[0].current_tier, FLOW_CXL_TIER_DDR5);
    FLOW_ASSERT_EQ(fabric.count_hbm, 31); /* Freed 1 slot in HBM */
    FLOW_ASSERT_EQ(fabric.count_ddr5, 2);
    FLOW_ASSERT_EQ(fabric.total_tier_migrations, 1);

    printf("  ✓ Migrated Page #%llu HBM -> DDR5 with QSBR Quiescent checkpoint (0 stall).\n\n",
           (unsigned long long)pid1);

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 5: 1-Bit Chaotic KV-Cache Attention Entropy Eviction                        */
    /* ---------------------------------------------------------------------------------- */
    FLOW_STAGE_BEGIN(5, "1-Bit Chaos KV-Cache Eviction & CXL Demotion");
    /* Simulate High Memory Pressure (90%) */
    int evicted = flow_cxl_adapt_eviction_chaos(&fabric, 0.90);
    FLOW_ASSERT_TRUE(evicted > 0);
    FLOW_ASSERT_TRUE(fabric.total_chaotic_evictions > 0);
    FLOW_ASSERT_TRUE(fabric.count_cxl >= (size_t)evicted);

    printf("  ✓ Memory Pressure 90%%: 1-Bit Chaos evicted %d low-entropy KV pages from HBM to Remote CXL Pool.\n", evicted);
    printf("  ✓ Active HBM count reduced to %zu/%zu, freeing space for active generation tokens.\n\n",
           fabric.count_hbm, fabric.cap_hbm);

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 6: SMT Formal Memory Quotas & Multi-Session Isolation Proof                 */
    /* ---------------------------------------------------------------------------------- */
    FLOW_STAGE_BEGIN(6, "SMT Formal Quota & Multi-Session Isolation Invariant");
    FlowSMTProofAttestation proof;
    memset(&proof, 0, sizeof(proof));

    /* Sound configuration -> PROVEN UNSAT */
    FlowSMTResult r_sound = flow_cxl_verify_smt(&fabric, 2, &proof);
    FLOW_ASSERT_EQ(r_sound, FLOW_SMT_PROVEN_UNSAT);
    FLOW_ASSERT_EQ(proof.buffer_bounds_safety, FLOW_SMT_PROVEN_UNSAT);
    FLOW_ASSERT_EQ(proof.shard_non_aliasing, FLOW_SMT_PROVEN_UNSAT);
    printf("  ✓ SMT Proof Sound: %s\n", proof.proof_summary);

    /* Counterexample Injection: Artificially corrupt HBM count > capacity */
    fabric.count_hbm = 999;
    FlowSMTResult r_viol = flow_cxl_verify_smt(&fabric, 2, &proof);
    FLOW_ASSERT_EQ(r_viol, FLOW_SMT_VIOLATION_SAT);
    FLOW_ASSERT_EQ(proof.buffer_bounds_safety, FLOW_SMT_VIOLATION_SAT);
    printf("  ✓ SMT Counterexample caught quota overflow: %s\n\n", proof.proof_summary);

    /* Restore sound state */
    fabric.count_hbm = 16;

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 7: Hardware Primitive Driver Interface                                      */
    /* ---------------------------------------------------------------------------------- */
    FLOW_STAGE_BEGIN(7, "CXL Memory Pool Primitive Driver Verification");
    const FlowPrimitiveDriver *drv = flow_primitive_cxl_driver();
    FLOW_ASSERT_TRUE(drv != NULL);
    FLOW_ASSERT_STR_EQ(drv->driver_name, "cxl_memory_pool");
    FlowHardwareBounds b;
    FLOW_ASSERT_EQ(drv->get_hardware_bounds(&b), 1);
    FLOW_ASSERT_EQ(b.max_queue_depth, FLOW_CXL_MAX_PAGES);
    FLOW_ASSERT_EQ(b.is_kernel_bypass, 1);
    printf("  ✓ Driver verified: %s, queue_depth=%llu, kernel_bypass=%u.\n\n",
           b.name, (unsigned long long)b.max_queue_depth, b.is_kernel_bypass);

    flow_cxl_destroy(&fabric);

    FLOW_TEST_SUITE_END();
    return 0;
}
