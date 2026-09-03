#include "flowy_fvec.h"
#include "swarm.h"
#include "smt.h"
#include "bitspace.h"
#include "registry.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "immune-promotion-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

static void clean_dir(const char *dir) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    (void)system(cmd);
}

int main(void) {
    flow_registry_init();

    printf("========================================================================================\n");
    printf("  🧬 Running FLOW Autonomous Immune Promotion & Swarm Lymphatic Broadcast Suite\n");
    printf("========================================================================================\n\n");

    const char *test_dir_zero = "/tmp/flow_immune_node0";
    const char *test_dir_peer = "/tmp/flow_immune_node1";
    clean_dir(test_dir_zero);
    clean_dir(test_dir_peer);
    mkdir(test_dir_zero, 0755);
    mkdir(test_dir_peer, 0755);

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 1: Subconscious Telemetry & 1,000,000 Healthy Request Gate Audit            */
    /* ---------------------------------------------------------------------------------- */
    printf("--- [Stage 1: Subconscious Telemetry 1,000,000 Request Promotion Gate] ---\n");
    FlowImmunePromoter promoter;
    flow_immune_promoter_init(&promoter, 1000000ULL);

    FlowSMTProofAttestation proof;
    memset(&proof, 0, sizeof(proof));
    proof.buffer_bounds_safety = FLOW_SMT_PROVEN_UNSAT;
    proof.memory_quota_bound = FLOW_SMT_PROVEN_UNSAT;
    proof.shard_non_aliasing = FLOW_SMT_PROVEN_UNSAT;
    proof.determinism_invariant = FLOW_SMT_PROVEN_UNSAT;
    strncpy(proof.proof_summary, "ALL_UNSAT_PROVEN", sizeof(proof.proof_summary) - 1);

    uint64_t novel_genome = 0x1A2B3C4D5E6F7080ULL;
    uint64_t novel_hard_mask = 0xFFFFFFFF00000000ULL;
    uint64_t novel_soft_bias = 0x00000000FFFFFFFFULL;

    flow_immune_promoter_set_active(&promoter, novel_genome, novel_hard_mask, novel_soft_bias,
                                   &proof, "sharded_hash", "SYN_FLOOD_DEFENSE", "x86_avx2", 42.0);

    /* Test 1a: Premature check at 500,000 requests must NOT promote */
    for (uint64_t i = 0; i < 500000ULL; ++i) {
        flow_immune_promoter_record_request(&promoter, 1, 1);
    }
    char promoted_path[512] = {0};
    uint32_t conf = 0;
    uint8_t lymph_pkt[9] = {0};
    int res_premature = flow_immune_promoter_check_and_promote(&promoter, test_dir_zero,
                                                              promoted_path, sizeof(promoted_path),
                                                              &conf, lymph_pkt);
    CHECK(res_premature == 0);
    printf("  ✓ Premature promotion blocked: 500,000 requests < 1,000,000 threshold.\n");

    /* Test 1b: Anomaly at request 999,999 must reset streak to 0 */
    for (uint64_t i = 0; i < 499999ULL; ++i) {
        flow_immune_promoter_record_request(&promoter, 1, 1);
    }
    CHECK(promoter.healthy_requests_count == 999999ULL);
    /* Incur 1 anomaly */
    flow_immune_promoter_record_request(&promoter, 0, 1);
    CHECK(promoter.healthy_requests_count == 0);
    CHECK(promoter.anomaly_count == 1);
    int res_anomaly = flow_immune_promoter_check_and_promote(&promoter, test_dir_zero,
                                                            promoted_path, sizeof(promoted_path),
                                                            &conf, lymph_pkt);
    CHECK(res_anomaly == 0);
    printf("  ✓ Anomaly reset verified: Sudden anomaly at #999,999 zeroed streak, preventing hasty promotion.\n");

    /* Test 1c: Re-arm and complete 1,000,000 consecutive healthy requests */
    flow_immune_promoter_set_active(&promoter, novel_genome, novel_hard_mask, novel_soft_bias,
                                   &proof, "sharded_hash", "SYN_FLOOD_DEFENSE", "x86_avx2", 38.5);
    for (uint64_t i = 0; i < 1000000ULL; ++i) {
        flow_immune_promoter_record_request(&promoter, 1, 1);
    }
    CHECK(promoter.healthy_requests_count == 1000000ULL);
    int res_promoted = flow_immune_promoter_check_and_promote(&promoter, test_dir_zero,
                                                             promoted_path, sizeof(promoted_path),
                                                             &conf, lymph_pkt);
    CHECK(res_promoted == 1);
    CHECK(conf == 1);
    CHECK(promoted_path[0] != '\0');
    printf("  ✓ Promotion achieved: 1,000,000 healthy requests completed. Minted '%s' (Confidence: %u)\n\n",
           promoted_path, conf);

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 2: Content-Addressable Hashing & Hebbian Strengthening                      */
    /* ---------------------------------------------------------------------------------- */
    printf("--- [Stage 2: Content-Addressable Hashing & Hebbian Learning Strengthening] ---\n");
    uint64_t content_hash = flow_fvec_compute_content_hash(novel_genome, novel_hard_mask,
                                                          novel_soft_bias, &proof);
    char expected_hash_str[32];
    snprintf(expected_hash_str, sizeof(expected_hash_str), "%016llx", (unsigned long long)content_hash);
    CHECK(strstr(promoted_path, expected_hash_str) != NULL);
    printf("  * Content-Addressable Hash verified: %s\n", expected_hash_str);

    /* Recurring disaster: System encounters the same crisis again and proves it again */
    flow_immune_promoter_set_active(&promoter, novel_genome, novel_hard_mask, novel_soft_bias,
                                   &proof, "sharded_hash", "SYN_FLOOD_DEFENSE", "x86_avx2", 38.5);
    for (uint64_t i = 0; i < 1000000ULL; ++i) {
        flow_immune_promoter_record_request(&promoter, 1, 1);
    }

    char re_promoted_path[512] = {0};
    uint32_t re_conf = 0;
    CHECK(flow_immune_promoter_check_and_promote(&promoter, test_dir_zero,
                                                re_promoted_path, sizeof(re_promoted_path),
                                                &re_conf, lymph_pkt));
    CHECK(strcmp(promoted_path, re_promoted_path) == 0);
    CHECK(re_conf == 2);
    printf("  ✓ Hebbian strengthening verified: Existing hash recognized, reinforced confidence to %u (No file bloating).\n\n",
           re_conf);

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 3: Immune Senescence & LRU Eviction Audit                                    */
    /* ---------------------------------------------------------------------------------- */
    printf("--- [Stage 3: Immune Senescence & LRU Eviction Audit] ---\n");
    /* Create an older auto-promoted model (simulating 45 days of inactivity) */
    uint64_t now_unix = (uint64_t)time(NULL);
    uint64_t stale_genome = 0x9999888877776666ULL;
    uint64_t stale_hash = flow_fvec_compute_content_hash(stale_genome, 0, 0, &proof);
    char stale_path[512];
    uint32_t stale_conf = 0;
    FlowVecHeader stale_hdr;
    memset(&stale_hdr, 0, sizeof(stale_hdr));
    strncpy(stale_hdr.magic, "FVEC_V1", sizeof(stale_hdr.magic) - 1);
    strncpy(stale_hdr.trigger_intent, "RETIRED_API_ATTACK", sizeof(stale_hdr.trigger_intent) - 1);
    FlowVecPayload stale_payload;
    memset(&stale_payload, 0, sizeof(stale_payload));
    CHECK(flow_fvec_promote_or_strengthen(test_dir_zero, &stale_hdr, &stale_payload, stale_hash,
                                          stale_path, sizeof(stale_path), &stale_conf));

    /* Backdate the stale model to 45 days ago */
    FlowVecHeader read_back_hdr;
    FlowVecPayload read_back_payload;
    CHECK(flow_fvec_read_file(stale_path, &read_back_hdr, &read_back_payload));
    read_back_hdr.last_reinforced_unix = now_unix - (45ULL * 86400ULL);
    CHECK(flow_fvec_write_file(stale_path, &read_back_hdr, &read_back_payload));

    /* Also seed 1 factory canonical model */
    flow_fvec_seed_canonical_files(test_dir_zero);

    FlowVecStore audit_store;
    flow_fvec_store_init(&audit_store, test_dir_zero);
    CHECK(flow_fvec_store_scan(&audit_store) > 0);
    size_t count_before = audit_store.count;
    printf("  * Store initialized with %zu models (1 active auto, 1 stale auto, 5 factory).\n", count_before);

    /* Run Senescence GC with 30-day (2,592,000s) threshold */
    char evicted[4][256];
    size_t num_evicted = flow_fvec_store_evict_senescent(&audit_store, now_unix, 30 * 86400ULL, evicted, 4);
    CHECK(num_evicted == 1);
    CHECK(strcmp(evicted[0], stale_path) == 0);
    CHECK(access(stale_path, F_OK) != 0);       /* Stale model deleted from filesystem */
    CHECK(access(promoted_path, F_OK) == 0);     /* Active model still exists */
    printf("  ✓ Senescence GC verified: Evicted %zu expired model ('%s'). Active and Factory models immune.\n\n",
           num_evicted, evicted[0]);

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 4: Swarm 9-Byte Lymphatic Broadcasting & Fleet Immunity                      */
    /* ---------------------------------------------------------------------------------- */
    printf("--- [Stage 4: Swarm 9-Byte Lymphatic Broadcasting & Fleet Immunity] ---\n");
    /* Check 9-byte packet structure */
    CHECK(lymph_pkt[0] == FLOW_SWARM_MSG_ANTIBODY);
    uint64_t decoded_hash = 0;
    CHECK(flow_swarm_lymphatic_decode(lymph_pkt, &decoded_hash));
    CHECK(decoded_hash == content_hash);
    printf("  * 9-Byte UDP Lymphatic Packet verified: Opcode 0x%02X, ContentHash 0x%016llx (Size: %d bytes)\n",
           lymph_pkt[0], (unsigned long long)decoded_hash, FLOW_SWARM_LYMPH_PKT_SIZE);

    /* Node 1 (Peer Node) receives packet and assimilates antibody into local store */
    FlowVecStore peer_store;
    flow_fvec_store_init(&peer_store, test_dir_peer);
    flow_fvec_seed_canonical_files(test_dir_peer);
    flow_fvec_store_scan(&peer_store);
    size_t peer_models_before = peer_store.count;

    CHECK(flow_swarm_lymphatic_assimilate(test_dir_peer, test_dir_zero, decoded_hash));
    flow_fvec_store_scan(&peer_store);
    CHECK(peer_store.count == peer_models_before + 1);

    /* Query assimilated antibody in Node 1's local gene store */
    size_t best_idx = 0;
    double sim = 0.0;
    CHECK(flow_fvec_store_query(&peer_store, "SYN FLOOD DEFENSE ATTACK", &best_idx, &sim));
    CHECK(sim > 0.60);
    printf("  ✓ Fleet Assimilation verified: Node 1 ingested Node 0's antibody without 1,000,000 trial cycles! (Sim: %.2f%%)\n",
           sim * 100.0);

    printf("========================================================================================\n");
    printf("IMMUNE_PROMOTION_TEST=passed hebbian_strengthening=verified senescence_gc=sound swarm_9byte_lymphatic=verified\n");
    printf("========================================================================================\n");

    /* Cleanup temporary test directories */
    clean_dir(test_dir_zero);
    clean_dir(test_dir_peer);
    return 0;
}
