#include "cxl_fabric.h"
#include "smt.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "cxl-llm-fabric-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

int main(void) {
    printf("========================================================================================\n");
    printf("  🧠 Running FLOW LLM Distributed Inference & CXL Memory Fabric Test Suite (Suite #67)\n");
    printf("========================================================================================\n\n");

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 1: CXL Memory Fabric Initialization & Tier Capacities                       */
    /* ---------------------------------------------------------------------------------- */
    printf("--- [Stage 1: CXL Fabric Initialization] ---\n");
    FlowCxlFabric fabric;
    CHECK(flow_cxl_init(&fabric) == 1);
    CHECK(fabric.cap_hbm == 32);
    CHECK(fabric.cap_ddr5 == 128);
    CHECK(fabric.cap_cxl == 352);
    CHECK(fabric.total_pages == 0);

    printf("  ✓ Fabric initialized: Tier 0 HBM (32 pages), Tier 1 DDR5 (128 pages), Tier 2 CXL Pool (352 pages).\n\n");

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 2: Multi-Session KV Cache Allocation & Tiered Overflow                      */
    /* ---------------------------------------------------------------------------------- */
    printf("--- [Stage 2: KV Cache Allocation & Tiered Placement] ---\n");
    uint64_t pid1, pid33;

    /* Session 1: High-entropy prompt attention KV pages */
    CHECK(flow_cxl_allocate_kv_page(&fabric, 1, 0, 128, 0.85, &pid1) == 1);
    CHECK(fabric.pages[0].current_tier == FLOW_CXL_TIER_HBM);

    /* Allocate up to 32 pages to fill HBM */
    for (int i = 1; i < 32; ++i) {
        uint64_t pid;
        double entropy = (i % 2 == 0) ? 0.90 : 0.20; /* Alternate high and low entropy */
        CHECK(flow_cxl_allocate_kv_page(&fabric, 1, i * 128, 128, entropy, &pid) == 1);
        CHECK(fabric.pages[i].current_tier == FLOW_CXL_TIER_HBM);
    }
    CHECK(fabric.count_hbm == 32);

    /* Page 33 should overflow into Tier 1 DDR5 */
    CHECK(flow_cxl_allocate_kv_page(&fabric, 2, 0, 128, 0.75, &pid33) == 1);
    CHECK(fabric.pages[32].current_tier == FLOW_CXL_TIER_DDR5);
    CHECK(fabric.count_ddr5 == 1);

    printf("  ✓ Tier 0 HBM saturated (32/32). Page 33 overflowed cleanly into Tier 1 DDR5.\n\n");

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 3: Access Latency Simulation Across Memory Tiers                             */
    /* ---------------------------------------------------------------------------------- */
    printf("--- [Stage 3: Sub-Microsecond Multi-Tier Read Access Latency] ---\n");
    uint8_t buffer[FLOW_CXL_PAGE_SIZE];
    uint64_t lat_ns = 0;

    /* Access HBM Page */
    CHECK(flow_cxl_access_kv_page(&fabric, pid1, buffer, &lat_ns) == 1);
    CHECK(lat_ns == 5); /* ~5ns */

    /* Access DDR5 Page */
    CHECK(flow_cxl_access_kv_page(&fabric, pid33, buffer, &lat_ns) == 1);
    CHECK(lat_ns == 50); /* ~50ns */

    printf("  ✓ Read verified: Tier 0 HBM latency = %llu ns, Tier 1 DDR5 latency = %llu ns.\n\n",
           5ULL, 50ULL);

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 4: QSBR Zero-Downtime Page Migration                                        */
    /* ---------------------------------------------------------------------------------- */
    printf("--- [Stage 4: QSBR Zero-Downtime Page Migration Across Tiers] ---\n");
    /* Migrate Page 1 from HBM to DDR5 */
    CHECK(flow_cxl_migrate_page(&fabric, pid1, FLOW_CXL_TIER_DDR5) == 1);
    CHECK(fabric.pages[0].current_tier == FLOW_CXL_TIER_DDR5);
    CHECK(fabric.count_hbm == 31); /* Freed 1 slot in HBM */
    CHECK(fabric.count_ddr5 == 2);
    CHECK(fabric.total_tier_migrations == 1);

    printf("  ✓ Migrated Page #%llu HBM -> DDR5 with QSBR Quiescent checkpoint (0 stall).\n\n",
           (unsigned long long)pid1);

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 5: 1-Bit Chaotic KV-Cache Attention Entropy Eviction                        */
    /* ---------------------------------------------------------------------------------- */
    printf("--- [Stage 5: 1-Bit Chaos KV-Cache Eviction & CXL Demotion] ---\n");
    /* Simulate High Memory Pressure (90%) */
    int evicted = flow_cxl_adapt_eviction_chaos(&fabric, 0.90);
    CHECK(evicted > 0);
    CHECK(fabric.total_chaotic_evictions > 0);
    CHECK(fabric.count_cxl >= (size_t)evicted);

    printf("  ✓ Memory Pressure 90%%: 1-Bit Chaos evicted %d low-entropy KV pages from HBM to Remote CXL Pool.\n", evicted);
    printf("  ✓ Active HBM count reduced to %zu/%zu, freeing space for active generation tokens.\n\n",
           fabric.count_hbm, fabric.cap_hbm);

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 6: SMT Formal Memory Quotas & Multi-Session Isolation Proof                 */
    /* ---------------------------------------------------------------------------------- */
    printf("--- [Stage 6: SMT Formal Quota & Multi-Session Isolation Invariant] ---\n");
    FlowSMTProofAttestation proof;
    memset(&proof, 0, sizeof(proof));

    /* Sound configuration -> PROVEN UNSAT */
    FlowSMTResult r_sound = flow_cxl_verify_smt(&fabric, 2, &proof);
    CHECK(r_sound == FLOW_SMT_PROVEN_UNSAT);
    CHECK(proof.buffer_bounds_safety == FLOW_SMT_PROVEN_UNSAT);
    CHECK(proof.shard_non_aliasing == FLOW_SMT_PROVEN_UNSAT);
    printf("  ✓ SMT Proof Sound: %s\n", proof.proof_summary);

    /* Counterexample Injection: Artificially corrupt HBM count > capacity */
    fabric.count_hbm = 999;
    FlowSMTResult r_viol = flow_cxl_verify_smt(&fabric, 2, &proof);
    CHECK(r_viol == FLOW_SMT_VIOLATION_SAT);
    CHECK(proof.buffer_bounds_safety == FLOW_SMT_VIOLATION_SAT);
    printf("  ✓ SMT Counterexample caught quota overflow: %s\n\n", proof.proof_summary);

    /* Restore sound state */
    fabric.count_hbm = 16;

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 7: Hardware Primitive Driver Interface                                      */
    /* ---------------------------------------------------------------------------------- */
    printf("--- [Stage 7: CXL Memory Pool Primitive Driver Verification] ---\n");
    const FlowPrimitiveDriver *drv = flow_primitive_cxl_driver();
    CHECK(drv != NULL && strcmp(drv->driver_name, "cxl_memory_pool") == 0);
    FlowHardwareBounds b;
    CHECK(drv->get_hardware_bounds(&b) == 1);
    CHECK(b.max_queue_depth == FLOW_CXL_MAX_PAGES);
    CHECK(b.is_kernel_bypass == 1);
    printf("  ✓ Driver verified: %s, queue_depth=%llu, kernel_bypass=%u.\n\n",
           b.name, (unsigned long long)b.max_queue_depth, b.is_kernel_bypass);

    flow_cxl_destroy(&fabric);

    printf("========================================================================================\n");
    printf("  🎉 ALL 7 CXL MEMORY FABRIC TEST STAGES 100%% SOUND & VERIFIED!\n");
    printf("========================================================================================\n");
    return 0;
}
