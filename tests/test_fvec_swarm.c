#include "flow_test_kit.h"
#include "flow_jet.h"
#include "embodied.h"
#include "bitspace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <sys/socket.h>
#include <fcntl.h>

int main(void) {
    FLOW_TEST_SUITE_BEGIN("Fvec & Swarm: Architectural Memory, Swarm Federation, Neuro-Bridge & Pre-Play");

    flow_hardware_telemetry_init();

    /* ========================================================================= */
    /* STAGE 1: .fvec Format, 1024-Byte Header & CRC32 Integrity                 */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(1, ".fvec Binary Format, 1024-Byte Header & CRC32 Checksum");
    {
        char test_path[256];
        snprintf(test_path, sizeof(test_path), "/tmp/test_fvec_%d.fvec", (int)getpid());

        FlowVecHeader hdr_in;
        memset(&hdr_in, 0, sizeof(hdr_in));
        strncpy(hdr_in.magic, FLOW_FVEC_MAGIC, sizeof(hdr_in.magic) - 1);
        strncpy(hdr_in.id, "vec_hft_ultra_low_latency", sizeof(hdr_in.id) - 1);
        strncpy(hdr_in.name, "HFT Ultra Low-Latency Lock-Free Queue", sizeof(hdr_in.name) - 1);
        strncpy(hdr_in.origin_hardware, "x86_avx2, L1=64K, Cores=64", sizeof(hdr_in.origin_hardware) - 1);
        strncpy(hdr_in.trigger_intent, "HFT_TRADING", sizeof(hdr_in.trigger_intent) - 1);
        strncpy(hdr_in.category, "HFT", sizeof(hdr_in.category) - 1);
        strncpy(hdr_in.component_id, "bounded_queue", sizeof(hdr_in.component_id) - 1);
        strncpy(hdr_in.description, "Sub-15ns lock-free ring buffer for algorithmic trading.", sizeof(hdr_in.description) - 1);
        strncpy(hdr_in.smt_signature, "BUFFER_UNSAT:MEM_UNSAT:SHARD_UNSAT:DET_UNSAT", sizeof(hdr_in.smt_signature) - 1);
        hdr_in.energy_score = 12.50;
        hdr_in.created_at_unix = 1772590000;
        hdr_in.vector_dim = FLOW_VAULT_DIM;
        hdr_in.payload_size = sizeof(FlowVecPayload);

        FlowVecPayload payload_in;
        memset(&payload_in, 0, sizeof(payload_in));
        for (int i = 0; i < FLOW_VAULT_DIM; ++i) {
            payload_in.features[i] = (double)(i + 1) * 0.05;
        }
        payload_in.pure_genome = 0x000000b01a627c6bULL;
        payload_in.hard_composite_mask = 0x000000000000ffffULL;
        payload_in.soft_composite_bias = 0x0000000000000001ULL;
        payload_in.proof.buffer_bounds_safety = FLOW_SMT_PROVEN_UNSAT;
        payload_in.proof.memory_quota_bound = FLOW_SMT_PROVEN_UNSAT;
        payload_in.proof.shard_non_aliasing = FLOW_SMT_PROVEN_UNSAT;
        payload_in.proof.determinism_invariant = FLOW_SMT_PROVEN_UNSAT;

        /* Test Header Serialization & Deserialization */
        char header_buf[FLOW_FVEC_HEADER_SIZE];
        FLOW_ASSERT_TRUE(flow_fvec_header_serialize(&hdr_in, header_buf, sizeof(header_buf)));

        FlowVecHeader hdr_deser;
        FLOW_ASSERT_TRUE(flow_fvec_header_deserialize(header_buf, sizeof(header_buf), &hdr_deser));
        FLOW_ASSERT_STR_EQ(hdr_deser.id, hdr_in.id);
        FLOW_ASSERT_STR_EQ(hdr_deser.trigger_intent, hdr_in.trigger_intent);
        FLOW_ASSERT_STR_EQ(hdr_deser.smt_signature, hdr_in.smt_signature);

        /* Test File Roundtrip */
        FLOW_ASSERT_TRUE(flow_fvec_write_file(test_path, &hdr_in, &payload_in));

        FlowVecHeader hdr_read;
        FlowVecPayload payload_read;
        FLOW_ASSERT_TRUE(flow_fvec_read_file(test_path, &hdr_read, &payload_read));
        FLOW_ASSERT_STR_EQ(hdr_read.id, hdr_in.id);
        FLOW_ASSERT_EQ(payload_read.pure_genome, payload_in.pure_genome);

        /* Corrupt 1 byte in .fvec payload -> CRC32 must reject */
        FILE *cf = fopen(test_path, "r+b");
        FLOW_ASSERT_TRUE(cf != NULL);
        fseek(cf, FLOW_FVEC_HEADER_SIZE + 8, SEEK_SET);
        uint8_t flip = 0xFF;
        fwrite(&flip, 1, 1, cf);
        fclose(cf);
        FLOW_ASSERT_FALSE(flow_fvec_read_file(test_path, &hdr_read, &payload_read));
        unlink(test_path);

        /* Test .fjet roundtrip and header/payload corruption rejection (kills Mutation #4) */
        char jet_path[256];
        snprintf(jet_path, sizeof(jet_path), "/tmp/test_jet_%d.fjet", (int)getpid());
        FlowJet jet_in, jet_out;
        FLOW_ASSERT_EQ(flow_jet_init(&jet_in, "jet_crc_test", "CRC32 Integrity Test"), 1);
        FLOW_ASSERT_EQ(flow_jet_write_file(&jet_in, jet_path), 1);
        FLOW_ASSERT_EQ(flow_jet_read_file(jet_path, &jet_out), 1);
        /* Corrupt 1 byte in .fjet header -> header CRC32 must reject */
        FILE *jf = fopen(jet_path, "r+b");
        FLOW_ASSERT_TRUE(jf != NULL);
        fseek(jf, 32, SEEK_SET);
        fwrite(&flip, 1, 1, jf);
        fclose(jf);
        FLOW_ASSERT_EQ(flow_jet_read_file(jet_path, &jet_out), 0);
        unlink(jet_path);

        printf("  ✓ Stage 1 Passed: .fvec & .fjet header/payload serialization and CRC32 tamper rejection verified.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 2: FlowVecStore Canonical Repository & Semantic Similarity Query    */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(2, "FlowVecStore: Canonical Archetypes & Semantic Similarity Query");
    {
        char test_dir[256];
        snprintf(test_dir, sizeof(test_dir), "/tmp/flow_test_vec_store_%d", (int)getpid());
        int seeded = flow_fvec_seed_canonical_files(test_dir);
        FLOW_ASSERT_TRUE(seeded >= 5);

        FlowVecStore store;
        flow_fvec_store_init(&store, test_dir);
        int scanned = flow_fvec_store_scan(&store);
        FLOW_ASSERT_TRUE(scanned >= 5);
        FLOW_ASSERT_EQ(store.count, (size_t)scanned);

        size_t best_idx = 0;
        double best_sim = 0.0;
        FLOW_ASSERT_TRUE(flow_fvec_store_query(&store, "high-frequency trading ultra-low latency lock-free ring buffer", &best_idx, &best_sim));
        FLOW_ASSERT_TRUE(best_sim >= 0.50);

        const FlowVecRecord *rec = flow_fvec_store_lookup_by_id(&store, store.records[0].header.id);
        FLOW_ASSERT_TRUE(rec != NULL);
        FLOW_ASSERT_TRUE(strlen(rec->header.component_id) > 0);

        printf("  ✓ Stage 2 Passed: %d canonical .fvec files seeded & queried (sim=%.2f).\n\n",
               scanned, best_sim);
    }

    /* ========================================================================= */
    /* STAGE 3: Immune Promoter & Swarm Lymphatic Antibody Gossip                */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(3, "Immune Promoter & 9-Byte Swarm Lymphatic Antibody Packet");
    {
        uint64_t hash_in = UINT64_C(0xCAFEBABE12345678);
        uint8_t pkt[FLOW_SWARM_LYMPH_PKT_SIZE];
        FLOW_ASSERT_EQ(flow_swarm_lymphatic_encode(hash_in, pkt), 1);
        FLOW_ASSERT_EQ(pkt[0], FLOW_SWARM_MSG_ANTIBODY);

        uint64_t hash_out = 0;
        FLOW_ASSERT_EQ(flow_swarm_lymphatic_decode(pkt, &hash_out), 1);
        FLOW_ASSERT_EQ(hash_out, hash_in);

        printf("  ✓ Stage 3 Passed: 9-byte lymphatic antibody packet encoded & decoded lossless.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 4: Heterogeneous Swarm Mesh & SMT Capacity Conservation             */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(4, "Heterogeneous Swarm Mesh: Pheromone Routing & SMT Soundness");
    {
        FlowHeteroMesh mesh;
        flow_hetero_mesh_init(&mesh);
        FLOW_ASSERT_EQ(flow_hetero_mesh_register_node(&mesh, 1, FLOW_SWARM_ROLE_COMPUTE_ROUTER, "router_a", 0x1111, 40000), 1);
        FLOW_ASSERT_EQ(flow_hetero_mesh_register_node(&mesh, 2, FLOW_SWARM_ROLE_COMPUTE_ROUTER, "router_b", 0x1111, 60000), 1);

        uint8_t selected = 0;
        FLOW_ASSERT_EQ(flow_hetero_mesh_route_target(&mesh, FLOW_SWARM_ROLE_COMPUTE_ROUTER, &selected), 1);
        FLOW_ASSERT_TRUE(selected == 1 || selected == 2);

        FlowSMTProofAttestation mesh_proof;
        FLOW_SMT_PROOF_POISON(&mesh_proof);
        FLOW_ASSERT_EQ(flow_hetero_mesh_verify_smt(&mesh, 50000, &mesh_proof), FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_SMT_SOUND(mesh_proof);

        FlowSMTProofAttestation bad_mesh_proof;
        FLOW_SMT_PROOF_POISON(&bad_mesh_proof);
        FLOW_ASSERT_EQ(flow_hetero_mesh_verify_smt(&mesh, 500000, &bad_mesh_proof), FLOW_SMT_VIOLATION_SAT);

        printf("  ✓ Stage 4 Passed: Heterogeneous swarm mesh routed to node %u & SMT proven.\n\n", selected);
    }

    /* STAGE 5: Speculative Gene Pre-Staging Vault (Zero-Coldstart Swap)         */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(5, "Speculative Gene Pre-Staging Vault: Zero-Coldstart Atomic Architecture Swap");
    {
        /* Ensure canonical .fvec repository directory is ready */
        char test_vault_dir[256];
        snprintf(test_vault_dir, sizeof(test_vault_dir), "/tmp/flow_test_prestage_vecs_%d", (int)getpid());
        flow_fvec_seed_canonical_files(test_vault_dir);

        FlowFvecPreStagingVault vault;
        FLOW_ASSERT_EQ(flow_fvec_prestaging_init(&vault), 1);
        FLOW_ASSERT_EQ(vault.slot_count, 0ULL);

        /* Register pre-staged architectural genes */
        char hft_fvec_path[256];
        char oom_fvec_path[256];
        snprintf(hft_fvec_path, sizeof(hft_fvec_path), "%s/hft_ultra_low_latency.fvec", test_vault_dir);
        snprintf(oom_fvec_path, sizeof(oom_fvec_path), "%s/oom_survival_v3.fvec", test_vault_dir);

        FLOW_ASSERT_EQ(flow_fvec_prestaging_register(&vault, hft_fvec_path, "HFT_BURST"), 1);
        FLOW_ASSERT_EQ(flow_fvec_prestaging_register(&vault, oom_fvec_path, "OOM_SURGE"), 1);
        FLOW_ASSERT_EQ(vault.slot_count, 2ULL);

        /* Execute atomic QSBR memory swap */
        FlowBmf1BitCanvas active_canvas;
        double swap_latency_ns = 0.0;
        FLOW_ASSERT_EQ(flow_fvec_prestaging_swap_atomic(&vault, "HFT_BURST", &active_canvas, &swap_latency_ns), 1);
        FLOW_ASSERT_TRUE(swap_latency_ns > 0.0);
        FLOW_ASSERT_EQ(vault.total_speculative_swaps, 1ULL);
        FLOW_ASSERT_TRUE(active_canvas.is_adjudicated_sound);

        /* Execute second atomic swap to OOM survival */
        double swap2_latency_ns = 0.0;
        FLOW_ASSERT_EQ(flow_fvec_prestaging_swap_atomic(&vault, "OOM_SURGE", &active_canvas, &swap2_latency_ns), 1);
        FLOW_ASSERT_TRUE(swap2_latency_ns > 0.0);
        FLOW_ASSERT_EQ(vault.total_speculative_swaps, 2ULL);

        /* SMT Soundness Verification for Zero-Coldstart Deadline (positive + negative) */
        FlowSMTProofAttestation prestaging_proof;
        FLOW_SMT_PROOF_POISON(&prestaging_proof);
        FLOW_ASSERT_EQ(flow_fvec_verify_prestaging_soundness_smt(&vault, "HFT_BURST", 40.0, &prestaging_proof), FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_SMT_SOUND(prestaging_proof);

        FlowSMTProofAttestation bad_prestaging_proof;
        FLOW_SMT_PROOF_POISON(&bad_prestaging_proof);
        FLOW_ASSERT_EQ(flow_fvec_verify_prestaging_soundness_smt(&vault, "HFT_BURST", 9999.0, &bad_prestaging_proof), FLOW_SMT_VIOLATION_SAT);
        FLOW_ASSERT_EQ(flow_fvec_verify_prestaging_soundness_smt(&vault, "NONEXISTENT_TRIGGER", 10.0, &bad_prestaging_proof), FLOW_SMT_VIOLATION_SAT);

        printf("  ✓ Stage 5 Passed: Speculative Gene Pre-Staging Vault swapped architecture in %.2fns; zero-coldstart guaranteed.\n\n",
               swap_latency_ns);
    }

    FLOW_TEST_SUITE_END();
    return 0;
}
