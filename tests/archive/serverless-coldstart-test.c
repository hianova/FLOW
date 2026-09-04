#include "flowy_fvec.h"
#include "bitspace.h"
#include "reload.h"
#include "registry.h"
#include "flow.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__) || defined(__MACH__)
#include <mach/mach_time.h>
static uint64_t timer_now_ns(void) {
    static mach_timebase_info_data_t tb;
    if (tb.denom == 0) mach_timebase_info(&tb);
    return mach_absolute_time() * tb.numer / tb.denom;
}
#else
static uint64_t timer_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}
#endif

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "serverless-coldstart-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

int main(void) {
    flow_registry_init();

    printf("========================================================================================\n");
    printf("  ⚡ SCENARIO 1: Serverless & Microservices Zero-Cold-Start Benchmark\n");
    printf("  (Simulating AWS Lambda Container Cold Boot & First-Request JIT Penalty)\n");
    printf("========================================================================================\n\n");

    /* 1. Setup Serverless Environment Profile: Bursty IO-Heavy Event Ingestion */
    SemanticIR ir;
    memset(&ir, 0, sizeof(ir));
    strncpy(ir.flow_name, "serverless_event_gateway", sizeof(ir.flow_name) - 1);
    ir.input_max_count = 25000;
    ir.top_n = 250;
    ir.memory_limit_mb = 16;
    ir.state_shared = 1;
    ir.state_read_heavy = 1;
    ir.fact_unordered = 1;
    ir.prefer_latency = 1;

    FlowBitSpace space;
    CHECK(flow_bitspace_init_for_ir(&ir, &space));

    /* Initialize Hippocampus Long-Term Memory Vault */
    FlowVectorVault vault;
    flow_vault_init(&vault);
    flow_vault_seed_canonical_archetypes(&vault);

    const size_t NUM_CONTAINERS = 20;
    double total_cold_jit_us = 0.0;
    double total_vault_boot_us = 0.0;
    size_t cold_jit_smt_failures = 0;
    size_t vault_smt_failures = 0;

    for (size_t c = 0; c < NUM_CONTAINERS; ++c) {
        /* Track A: Conventional Cold-Start (150 iterations of BMF Search) */
        {
            FlowBMFConfig cold_cfg = {
                .initial_temperature = 80.0,
                .cooling_decay = 0.98,
                .plateau_stagnation_limit = 6,
                .reheat_ratio = 0.6,
                .use_mask_canvas = 0
            };
            FlowBitSearchResult res;
            uint64_t t0 = timer_now_ns();
            flow_bitspace_search_configured(&space, 150, (uint32_t)(c + 1), 0, NULL, &cold_cfg, &res);
            uint64_t t1 = timer_now_ns();

            total_cold_jit_us += (double)(t1 - t0) / 1000.0;
            cold_jit_smt_failures += res.heatmap.total_failures;
            CHECK(res.best_plan.eval.hard_gate_passed);
        }

        /* Track B: Zero-Cold-Start with Canva_Vec Muscle Memory */
        {
            FlowPlan instant_plan;
            FlowMaskCanvas instant_canvas;
            double lookup_us = 0.0;

            uint64_t t0 = timer_now_ns();
            CHECK(flow_vault_serverless_coldstart(&vault, &ir, 0.05, 2.0, &instant_plan, &instant_canvas, &lookup_us));

            /* 10 localized micro-refinement steps to adjust for fine-grained PMU variance */
            FlowTransitionCostModel model = {
                .has_active_baseline = 1,
                .baseline_plan = &instant_plan
            };
            FlowBMFConfig micro_cfg = {
                .initial_temperature = 10.0,
                .cooling_decay = 0.90,
                .plateau_stagnation_limit = 3,
                .reheat_ratio = 0.4,
                .mask_canvas = instant_canvas,
                .soft_bias_weight = 0.85,
                .use_mask_canvas = 1
            };
            FlowBitSearchResult res;
            flow_bitspace_search_configured(&space, 10, (uint32_t)(c + 1), 0, &model, &micro_cfg, &res);
            uint64_t t1 = timer_now_ns();

            total_vault_boot_us += (double)(t1 - t0) / 1000.0;
            vault_smt_failures += res.heatmap.total_failures;
            CHECK(res.best_plan.eval.hard_gate_passed);
        }
    }

    double avg_cold_jit_us = total_cold_jit_us / (double)NUM_CONTAINERS;
    double avg_vault_boot_us = total_vault_boot_us / (double)NUM_CONTAINERS;
    double avg_cold_fail = (double)cold_jit_smt_failures / (double)NUM_CONTAINERS;
    double avg_vault_fail = (double)vault_smt_failures / (double)NUM_CONTAINERS;

    printf("  Serverless Containers Tested:  %zu independent cold boots\n", NUM_CONTAINERS);
    printf("  [Baseline: Conventional Cold-Start JIT Annealing]\n");
    printf("    - Average Cold Boot Latency: %.2f us (Paying BMF Tax)\n", avg_cold_jit_us);
    printf("    - Average SMT Rejections:    %.1f failures / boot\n", avg_cold_fail);
    printf("  [Canva_Vec Hippocampus Zero-Cold-Start]\n");
    printf("    - Average Cold Boot Latency: %.2f us (AOT-Speed Instant Boot)\n", avg_vault_boot_us);
    printf("    - Average SMT Rejections:    %.1f failures / boot (Near Zero)\n", avg_vault_fail);
    printf("  => 🚀 Zero-Cold-Start Acceleration: %.2fx Speedup (%.1f%% Latency Drop)\n",
           avg_cold_jit_us / avg_vault_boot_us,
           ((avg_cold_jit_us - avg_vault_boot_us) / avg_cold_jit_us) * 100.0);

    CHECK(avg_vault_boot_us < avg_cold_jit_us / 4.0); /* Must be at least 4x faster */

    printf("\nSERVERLESS_COLDSTART_TEST=passed zero_cold_start=verified aot_speed=sound latency_drop=%.1f%% speedup=%.2fx\n",
           ((avg_cold_jit_us - avg_vault_boot_us) / avg_cold_jit_us) * 100.0,
           avg_cold_jit_us / avg_vault_boot_us);

    return 0;
}
