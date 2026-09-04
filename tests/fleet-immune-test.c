#include "flowy_fvec.h"
#include "bitspace.h"
#include "reload.h"
#include "registry.h"
#include "smt.h"

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

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "fleet-immune-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

#define FLEET_SIZE 1000

int main(void) {
    flow_registry_init();

    printf("========================================================================================\n");
    printf("  🛡️ SCENARIO 2: Digital Immune System - Fleet-Wide Antibody Sharing Benchmark\n");
    printf("  (Simulating 1,000 Nodes Defending Against a Novel Slowloris DDoS Attack)\n");
    printf("========================================================================================\n\n");

    /* Environment: Normal API Gateway transitioning into Acute Slowloris DDoS */
    SemanticIR ddos_ir;
    memset(&ddos_ir, 0, sizeof(ddos_ir));
    strncpy(ddos_ir.flow_name, "edge_api_gateway", sizeof(ddos_ir.flow_name) - 1);
    ddos_ir.input_max_count = 100000;
    ddos_ir.top_n = 1000;
    ddos_ir.memory_limit_mb = 32;
    ddos_ir.state_shared = 1;
    ddos_ir.state_read_heavy = 0; /* High write contention from malicious half-open connections */
    ddos_ir.fact_unordered = 1;
    ddos_ir.fact_deterministic = 1;

    FlowBitSpace space;
    CHECK(flow_bitspace_init_for_ir(&ddos_ir, &space));

    /* --------------------------------------------------------------------- */
    /* PHASE 1: Node 0 (Patient Zero) encounters the novel attack            */
    /* --------------------------------------------------------------------- */
    printf("--- [Phase 1: Patient Zero (Node 0) Novel Attack Emergence] ---\n");
    printf("  * Node 0 suffers an unclassified Slowloris attack (Socket saturation, high miss rate).\n");
    printf("  * No existing antibody in Node 0's local vault. BMF activates from scratch.\n");

    uint64_t t0 = timer_now_ns();

    /* Node 0 runs full deliberative BMF search (Prefrontal Cortex) */
    FlowBMFConfig bmf_cfg = {
        .initial_temperature = 80.0,
        .cooling_decay = 0.98,
        .plateau_stagnation_limit = 6,
        .reheat_ratio = 0.6,
        .use_mask_canvas = 0
    };
    FlowBitSearchResult patient_zero_res;
    CHECK(flow_bitspace_search_configured(&space, 200, 42, 0, NULL, &bmf_cfg, &patient_zero_res));
    CHECK(patient_zero_res.best_plan.eval.hard_gate_passed);

    uint64_t t1 = timer_now_ns();
    double patient_zero_discovery_us = (double)(t1 - t0) / 1000.0;

    printf("  * Node 0 synthesized defensive Mask Canva in %.2f us (Paying BMF Tax: %llu mutations, %llu rejections)\n",
           patient_zero_discovery_us,
           (unsigned long long)patient_zero_res.heatmap.total_mutations,
           (unsigned long long)patient_zero_res.heatmap.total_failures);
    printf("  * Discovered Defense Genome: 0x%016llx (Component: %s, Energy: %.2f)\n",
           (unsigned long long)patient_zero_res.best_plan.genome,
           patient_zero_res.best_plan.component->id,
           patient_zero_res.best_plan.eval.energy);

    /* Node 0 packages Canva into an Antibody Vector */
    FlowVaultEntry antibody;
    memset(&antibody, 0, sizeof(antibody));
    strncpy(antibody.id, "vec_antibody_slowloris_fleet", sizeof(antibody.id) - 1);
    strncpy(antibody.name, "Slowloris DDoS Quarantine Antibody", sizeof(antibody.name) - 1);
    strncpy(antibody.origin_node_id, "node-000.core.cluster", sizeof(antibody.origin_node_id) - 1);
    antibody.category = FLOW_VAULT_CAT_IMMUNE_ANTIBODY;
    antibody.pure_genome = patient_zero_res.best_plan.genome;
    antibody.canvas = patient_zero_res.mask_canvas;
    antibody.baseline_energy = patient_zero_res.best_plan.eval.energy;
    strncpy(antibody.component_id, patient_zero_res.best_plan.component->id, sizeof(antibody.component_id) - 1);

    char gossip_packet[512];
    CHECK(flow_vault_broadcast_antibody(NULL, &antibody, gossip_packet, sizeof(gossip_packet)) > 0);
    printf("  * Node 0 compressed Canva into 16-D Antibody Vector and emitted gossip broadcast packet (%zu bytes).\n\n",
           strlen(gossip_packet));

    /* --------------------------------------------------------------------- */
    /* PHASE 2: Nodes 1 .. 999 ingest the antibody for Herd Immunity        */
    /* --------------------------------------------------------------------- */
    printf("--- [Phase 2: Fleet Herd Immunity (Nodes 1 .. %d)] ---\n", FLEET_SIZE - 1);
    printf("  * Attack spreads across the remaining %d nodes in the cluster.\n", FLEET_SIZE - 1);

    double total_ingest_time_us = 0.0;
    double total_switch_time_us = 0.0;
    size_t herd_bmf_avoided_count = 0;

    for (size_t n = 1; n < FLEET_SIZE; ++n) {
        FlowVectorVault node_vault;
        flow_vault_init(&node_vault);

        /* 1. Ingest gossip packet into local Hippocampus */
        uint64_t in0 = timer_now_ns();
        size_t ingested_idx = 0;
        CHECK(flow_vault_ingest_antibody(&node_vault, gossip_packet, &ingested_idx));
        uint64_t in1 = timer_now_ns();
        total_ingest_time_us += (double)(in1 - in0) / 1000.0;

        /* 2. Instant zero-BMF state mount */
        uint64_t sw0 = timer_now_ns();
        const FlowVaultEntry *ab = flow_vault_get(&node_vault, ingested_idx);
        FlowPlan node_defense_plan;
        space.decode(&space, ab->pure_genome, &node_defense_plan);
        space.evaluate(&space, &node_defense_plan, &node_defense_plan.eval);
        CHECK(space.hard_gate(&space, &node_defense_plan, &node_defense_plan.eval));
        uint64_t sw1 = timer_now_ns();

        total_switch_time_us += (double)(sw1 - sw0) / 1000.0;
        herd_bmf_avoided_count++;
    }

    double avg_ingest_ns = (total_ingest_time_us / (double)(FLEET_SIZE - 1)) * 1000.0;
    double avg_switch_ns = (total_switch_time_us / (double)(FLEET_SIZE - 1)) * 100.0;

    printf("  * %zu / %zu Nodes successfully ingested and mounted antibody with 100%% SMT Soundness.\n",
           herd_bmf_avoided_count, (size_t)FLEET_SIZE - 1);
    printf("  * Average Ingestion Time per Node:  %.1f ns\n", avg_ingest_ns);
    printf("  * Average QSBR Switch Time per Node: %.1f ns\n", avg_switch_ns);

    /* Compute Cluster Resource Comparison */
    double cluster_bmf_without_antibodies_us = patient_zero_discovery_us * (double)FLEET_SIZE;
    double cluster_actual_time_us = patient_zero_discovery_us + total_ingest_time_us + total_switch_time_us;
    double compute_savings_percent = ((cluster_bmf_without_antibodies_us - cluster_actual_time_us) / cluster_bmf_without_antibodies_us) * 100.0;

    printf("\n  ========================================================================================\n");
    printf("  Cluster Compute Without Antibodies (1,000 Redundant BMF Searches): %.2f ms\n",
           cluster_bmf_without_antibodies_us / 1000.0);
    printf("  Cluster Compute With Antibody Memory (1 Discovery + 999 Instant Swaps): %.2f ms\n",
           cluster_actual_time_us / 1000.0);
    printf("  => 🛡️ Fleet-Wide Herd Immunity Efficiency: %.2f%% Cluster CPU Overhead Eliminated!\n",
           compute_savings_percent);
    printf("  ========================================================================================\n");

    CHECK(compute_savings_percent > 95.0);

    printf("\nFLEET_IMMUNE_TEST=passed fleet_size=%d patient_zero=verified herd_immunity=sound compute_savings=%.2f%%\n",
           FLEET_SIZE, compute_savings_percent);
    return 0;
}
