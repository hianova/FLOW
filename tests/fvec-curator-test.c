#include "fvec.h"
#include "topology.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    printf("==================================================================================\n");
    printf("  🏛️ Running Flowy Living Architecture Curator & Gene Vault Tests\n");
    printf("==================================================================================\n");

    const char *test_dir = "/tmp/flow_test_vecs";

    /* Step 1: Seed Canonical .fvec Models */
    int seeded = flow_fvec_seed_canonical_files(test_dir);
    assert(seeded >= 5);
    printf("  [Step 1] Seeded %d canonical .fvec models to '%s'.\n", seeded, test_dir);

    /* Step 2: Initialize & Scan Store */
    FlowVecStore store;
    flow_fvec_store_init(&store, test_dir);
    int scanned = flow_fvec_store_scan(&store);
    assert(scanned == seeded);
    assert(store.count >= 5);
    printf("  [Step 2] Directory scan verified: loaded %zu models into inverted store.\n", store.count);

    /* Step 3: Mount onto FlowTopologyGraph as Historical Experience Nodes */
    FlowTopologyGraph graph;
    flow_topology_init(&graph);
    flow_topology_build_codebase_graph(&graph);
    size_t nodes_before = graph.node_count;
    size_t edges_before = graph.edge_count;

    int attached = flow_topology_attach_fvec_store(&graph, &store);
    assert(attached == (int)store.count);
    assert(graph.node_count == nodes_before + store.count);
    assert(graph.edge_count > edges_before);
    printf("  [Step 3] Graph Fusion verified: attached %d .fvec nodes via MEMORIALIZES edges.\n", attached);

    /* Step 4: Scenario A: Disaster Crisis & Antibody Injection */
    printf("\n  [Scenario A: Emergency OOM Disaster Survival Injection]\n");
    const FlowVecRecord *rec_oom = NULL;
    double conf_oom = 0.0;
    char diag_oom[512] = {0};
    int triggered_oom = flow_fvec_remediate_check(&store, 98.0, 0.02, &rec_oom, &conf_oom, diag_oom, sizeof(diag_oom));
    assert(triggered_oom == 1);
    assert(rec_oom != NULL);
    assert(strstr(rec_oom->header.filepath, "oom_survival") != NULL ||
           strstr(rec_oom->header.trigger_intent, "MEMORY_CRITICAL") != NULL);
    assert(conf_oom >= 0.90);
    printf("    -> System Alert: %s\n", diag_oom);
    printf("    -> Injected Antibody Model: %s (Confidence: %.0f%%)\n", rec_oom->header.filepath, conf_oom * 100.0);
    printf("    -> SMT Proof: %s\n", rec_oom->header.smt_signature);
    printf("    ✓ PASSED (Crisis bypassed chaos tax via instant gene prescription)\n");

    /* Step 5: Scenario B: Human Prompt-to-Vector Query (HFT Trading) */
    printf("\n  [Scenario B: Natural Language Prompt-to-Vector Query]\n");
    const char *hft_prompt = "幫我找一個適合跑高頻交易的配置 lock-free ring buffer";
    size_t best_idx = 0;
    double best_sim = 0.0;
    int query_ok = flow_fvec_store_query(&store, hft_prompt, &best_idx, &best_sim);
    assert(query_ok == 1);
    const FlowVecRecord *hft_match = &store.records[best_idx];
    printf("    -> User Prompt: \"%s\"\n", hft_prompt);
    printf("    -> Matched Model: [%s] (%s)\n", hft_match->header.name, hft_match->header.filepath);
    printf("    -> Cosine Similarity: %.4f (Req: >0.60)\n", best_sim);
    printf("    -> Component: %s | Hardware: %s\n", hft_match->header.component_id, hft_match->header.origin_hardware);
    assert(strstr(hft_match->header.filepath, "hft_ultra_low_latency") != NULL ||
           strstr(hft_match->header.trigger_intent, "HFT_TRADING") != NULL);
    assert(best_sim > 0.60);
    printf("    ✓ PASSED (Accurate Prompt-to-Vector retrieval with sub-15ns config recommendation)\n");

    /* Step 6: Inverted Keyword Search by Intent */
    const FlowVecRecord *intent_results[10];
    size_t found = flow_fvec_store_find_by_intent(&store, "DDoS_DEFENSE", intent_results, 10);
    assert(found >= 1);
    assert(strstr(intent_results[0]->header.filepath, "slowloris_immune_antibody") != NULL);
    printf("  [Step 6] Inverted Index by Intent 'DDoS_DEFENSE' returned '%s'.\n",
           intent_results[0]->header.id);

    printf("==================================================================================\n");
    printf("FVEC_CURATOR_TEST=passed store_scanning=verified topology_fusion=sound scenario_a=verified scenario_b=verified\n");
    return 0;
}
