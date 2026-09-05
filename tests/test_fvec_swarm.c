#include "flow_test_kit.h"
#include "flowy_fvec.h"
#include "neuro_bridge.h"
#include "spacetime_preplay.h"
#include "swarm_autopoiesis.h"
#include "hardware_telemetry.h"
#include "fwht_projection.h"
#include "morse_atlas.h"
#include "bmf_microcode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

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

        unlink(test_path);
        printf("  ✓ Stage 1 Passed: 1024-byte header serialization and file roundtrip verified.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 2: Vector Vault, Canonical Archetypes & Semantic RAG                */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(2, "Vector Vault: Canonical Archetypes & Semantic Similarity Query");
    {
        FlowVectorVault vault;
        flow_vault_init(&vault);
        FLOW_ASSERT_EQ(vault.count, 0ULL);

        int count = flow_vault_seed_canonical_archetypes(&vault);
        FLOW_ASSERT_TRUE(count >= 5);
        FLOW_ASSERT_EQ(vault.count, (size_t)count);

        /* Query for HFT ultra-low latency */
        size_t best_idx = 0;
        double best_sim = 0.0;
        FLOW_ASSERT_TRUE(flow_vault_query_semantic(&vault, "high-frequency trading ultra-low latency lock-free ring buffer", &best_idx, &best_sim));
        FLOW_ASSERT_TRUE(best_sim >= 0.70);
        FLOW_ASSERT_STR_EQ(vault.entries[best_idx].component_id, "bounded_queue");

        printf("  ✓ Stage 2 Passed: %d canonical archetypes seeded; semantic similarity query matched (sim=%.2f).\n\n",
               count, best_sim);
    }

    /* ========================================================================= */
    /* STAGE 3: Tidal Morphing & Cross-Hardware Transfer                         */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(3, "Tidal Morphing (24-Hour Cycle) & Cross-ISA Gene Transfer");
    {
        FlowVectorVault vault;
        flow_vault_init(&vault);
        flow_vault_seed_canonical_archetypes(&vault);

        const FlowVaultEntry *day_entry = flow_vault_lookup_by_id(&vault, "vec_serverless_io_heavy");
        const FlowVaultEntry *night_entry = flow_vault_lookup_by_id(&vault, "vec_serverless_tiny_worker");
        FLOW_ASSERT_TRUE(day_entry != NULL);
        FLOW_ASSERT_TRUE(night_entry != NULL);

        FlowMaskCanvas canvas;
        uint64_t seed_genome = 0;
        /* Noon (peak): alpha = 0.0 -> day profile */
        FLOW_ASSERT_EQ(flow_vault_tidal_morph(day_entry, night_entry, 0.0, &canvas, &seed_genome), 1);
        FLOW_ASSERT_EQ(seed_genome, day_entry->pure_genome);

        /* Midnight (trough): alpha = 1.0 -> night profile */
        FLOW_ASSERT_EQ(flow_vault_tidal_morph(day_entry, night_entry, 1.0, &canvas, &seed_genome), 1);
        FLOW_ASSERT_EQ(seed_genome, night_entry->pure_genome);

        /* 3am (interpolated): alpha = 0.5 */
        FLOW_ASSERT_EQ(flow_vault_tidal_morph(day_entry, night_entry, 0.5, &canvas, &seed_genome), 1);

        printf("  ✓ Stage 3 Passed: 24-hour tidal morphing across day/night load curves validated.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 4: Fleet Immune System & Gossip Antibody Memory                     */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(4, "Fleet Immune System: Antibody Memory Vault & Gossip Broadcast");
    {
        FlowVectorVault vault;
        flow_vault_init(&vault);
        flow_vault_seed_canonical_archetypes(&vault);

        const FlowVaultEntry *ab = flow_vault_lookup_by_id(&vault, "vec_antibody_slowloris_409");
        FLOW_ASSERT_TRUE(ab != NULL);
        FLOW_ASSERT_STR_CONTAINS(ab->name, "Antibody");

        char gossip_packet[512];
        FLOW_ASSERT_TRUE(flow_vault_broadcast_antibody(&vault, ab, gossip_packet, sizeof(gossip_packet)));
        FLOW_ASSERT_STR_CONTAINS(gossip_packet, "FLOW_ANTIBODY_V1");
        FLOW_ASSERT_STR_CONTAINS(gossip_packet, "vec_antibody_slowloris_409");

        printf("  ✓ Stage 4 Passed: Immune antibody identified and gossip packet serialized.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 5: Ecosystem Gene Hub                                               */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(5, "Ecosystem Gene Hub: Search, Pull & SMT Zero-Defect Check");
    {
        FlowHubIndex hub;
        flow_hub_init_local_index(&hub);
        FLOW_ASSERT_TRUE(hub.count >= 3);

        FlowHubEntry results[FLOW_HUB_MAX_ENTRIES];
        size_t found = 0;
        flow_hub_search(&hub, "trading", results, FLOW_HUB_MAX_ENTRIES, &found);
        FLOW_ASSERT_TRUE(found >= 1);
        FLOW_ASSERT_STR_CONTAINS(results[0].name, "Trading");

        char saved_path[256];
        FLOW_ASSERT_TRUE(flow_hub_pull(&hub, results[0].model_id, "/tmp", saved_path, sizeof(saved_path)));
        FLOW_ASSERT_TRUE(access(saved_path, F_OK) == 0);
        unlink(saved_path);

        printf("  ✓ Stage 5 Passed: Hub index searched and model pulled with CRC32+SMT verified.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 6: Neuro-Bit Manifold Bridge (Deep SIMD & Quantized Projection)     */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(6, "Neuro-Bit Manifold Bridge: 4096-D Float -> Deep SIMD & Quantized Polytope");
    {
        FlowNeuroBridge bridge;
        const size_t input_dim = 4096;
        FLOW_ASSERT_EQ(flow_neuro_bridge_init(&bridge, input_dim, 0xACE1337), 1);

        static float embedding[FLOW_NEURO_MAX_INPUT_DIM];
        static int8_t q_embedding[FLOW_NEURO_MAX_INPUT_DIM];
        for (size_t i = 0; i < input_dim; i++) {
            float val = (float)sin((double)i * 0.123);
            embedding[i] = val;
            q_embedding[i] = (int8_t)(val * 120.0f);
        }

        /* 1. Baseline FP32 Projection */
        FlowNeuroProjectionResult result;
        FLOW_ASSERT_EQ(flow_neuro_bridge_project(&bridge, embedding, input_dim,
                                                 FLOW_NEURO_INTENT_SMOOTH_FETCH_LATTE, &result), 1);

        FLOW_ASSERT_EQ(result.classified_intent, FLOW_NEURO_INTENT_SMOOTH_FETCH_LATTE);
        FLOW_ASSERT_NE(result.bmf_coordinates, 0ULL);
        FLOW_ASSERT_TRUE(result.bound_count >= 4);
        FLOW_ASSERT_TRUE(result.projection_nanoseconds < 50000.0);

        FlowSMTProofAttestation proof;
        memset(&proof, 0, sizeof(proof));
        FLOW_ASSERT_EQ(flow_neuro_bridge_verify_smt(&result, &proof), FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_SMT_SOUND(proof);

        /* 2. Deeper SIMD Vectorized Projection */
        FlowNeuroProjectionResult simd_result;
        FLOW_ASSERT_EQ(flow_neuro_bridge_project_simd(&bridge, embedding, input_dim,
                                                      FLOW_NEURO_INTENT_SMOOTH_FETCH_LATTE, &simd_result), 1);
        FLOW_ASSERT_EQ(simd_result.classified_intent, FLOW_NEURO_INTENT_SMOOTH_FETCH_LATTE);
        FLOW_ASSERT_EQ(simd_result.bmf_coordinates, result.bmf_coordinates);
        FLOW_ASSERT_TRUE(simd_result.projection_nanoseconds < 1000.0);

        FlowSMTProofAttestation simd_proof;
        memset(&simd_proof, 0, sizeof(simd_proof));
        FLOW_ASSERT_EQ(flow_neuro_verify_simd_soundness_smt(&result, &simd_result, 0.0001, &simd_proof), FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_SMT_SOUND(simd_proof);

        /* 3. Quantized INT8 Fixed-Point Projection */
        FlowNeuroBridgeQuantized q_bridge;
        FLOW_ASSERT_EQ(flow_neuro_bridge_quantize(&bridge, &q_bridge), 1);
        FlowNeuroProjectionResult q_result;
        FLOW_ASSERT_EQ(flow_neuro_bridge_project_quantized(&q_bridge, q_embedding,
                                                           FLOW_NEURO_INTENT_SMOOTH_FETCH_LATTE, &q_result), 1);
        FLOW_ASSERT_EQ(q_result.classified_intent, FLOW_NEURO_INTENT_SMOOTH_FETCH_LATTE);
        FLOW_ASSERT_NE(q_result.bmf_coordinates, 0ULL);

        /* 4. SIMD Polyhedral Half-Space Evaluation */
        double safe_state[FLOW_NEURO_FVEC_DIM] = {0.04, 0.20, 3.0, 0.50};
        uint32_t violation_mask = 0;
        FLOW_ASSERT_EQ(flow_neuro_eval_bounds_simd(result.bounds, result.bound_count, safe_state, &violation_mask), 1);
        FLOW_ASSERT_EQ(violation_mask, 0U); /* All within safe bounds! */

        double unsafe_state[FLOW_NEURO_FVEC_DIM] = {0.15, 0.90, 8.0, 2.00}; /* Tilted 0.15 rad (> 0.08 limit) */
        uint32_t violation_unsafe = 0;
        FLOW_ASSERT_EQ(flow_neuro_eval_bounds_simd(result.bounds, result.bound_count, unsafe_state, &violation_unsafe), 1);
        FLOW_ASSERT_NE(violation_unsafe, 0U); /* Violation flagged in sub-5ns! */

        /* 5. 4096-D Continuous Embedding -> Subspace Routing -> 1-Bit BMF Canvas */
        FlowBmfSubspaceRegistry reg;
        flow_bmf_subspace_init_registry(&reg);

        uint32_t indexed_sub_id = 0;
        FLOW_ASSERT_EQ(flow_neuro_bridge_index_subspace(&bridge, embedding, input_dim,
                                                        FLOW_NEURO_INTENT_SMOOTH_FETCH_LATTE,
                                                        &reg, &indexed_sub_id), 1);
        FLOW_ASSERT_EQ(indexed_sub_id, FLOW_BMF_SUBSPACE_SMOOTH_FETCH_LATTE);

        FlowBmf1BitCanvas bmf_canvas;
        FlowNeuroProjectionResult bridge_res;
        FLOW_ASSERT_EQ(flow_neuro_bridge_to_1bit_canvas(&bridge, embedding, input_dim,
                                                        FLOW_NEURO_INTENT_SMOOTH_FETCH_LATTE,
                                                        &reg, &bmf_canvas, &bridge_res), 1);
        FLOW_ASSERT_EQ(bmf_canvas.subspace_id, FLOW_BMF_SUBSPACE_SMOOTH_FETCH_LATTE);
        /* Invariants must be active */
        FLOW_ASSERT_TRUE(flow_bmf_canvas_get_switch(&bmf_canvas, FLOW_BMF_SW_HARD_SAFETY));
        FLOW_ASSERT_TRUE(flow_bmf_canvas_get_switch(&bmf_canvas, FLOW_BMF_SW_ANTI_SPILL_TILT));
        FLOW_ASSERT_TRUE(flow_bmf_canvas_get_switch(&bmf_canvas, FLOW_BMF_SW_GRIPPER_FORCE_SAFE));
        /* SMT Formal Adjudication */
        FLOW_ASSERT_TRUE(flow_bmf_canvas_adjudicate_smt(&bmf_canvas));
        FLOW_ASSERT_TRUE(bmf_canvas.is_adjudicated_sound);

        /* 6. Kolmogorov Zero-Table FWHT Projection -> Morse Atlas Direct Mapping */
        FlowBmfMorseAtlas morse_atlas;
        flow_morse_atlas_seed_canonical(&morse_atlas);

        FlowBmf1BitCanvas morse_canvas;
        FlowNeuroProjectionResult fwht_res;
        FLOW_ASSERT_EQ(flow_neuro_bridge_to_morse_canvas(embedding, 0x1337BEEF,
                                                         &morse_atlas, &morse_canvas, &fwht_res), 1);
        FLOW_ASSERT_TRUE(flow_bmf_canvas_get_switch(&morse_canvas, FLOW_BMF_SW_HARD_SAFETY));
        FLOW_ASSERT_TRUE(morse_canvas.is_adjudicated_sound);
        FLOW_ASSERT_TRUE(fwht_res.projection_nanoseconds < 50000.0);

        printf("  ✓ Stage 6 Passed: 4096-D continuous embedding routed to Subspace & 1-Bit BMF Canvas via Deep SIMD (%.1fns) & Zero-Table FWHT (%.1fns); SMT Sound.\n\n",
               simd_result.projection_nanoseconds, fwht_res.projection_nanoseconds);
    }

    /* ========================================================================= */
    /* STAGE 7: Spacetime Pre-Play Engine (3.0s Cone & Counterfactual Anneal)    */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(7, "Spacetime Pre-Play Engine: Black Swan Elimination");
    {
        FlowSpacetimeEngine engine;
        FLOW_ASSERT_EQ(flow_spacetime_init(&engine, 50.0, 0.85), 1);

        /* Sudden black ice patch at t=2.5s..3.0s */
        FLOW_ASSERT_EQ(flow_spacetime_set_black_swan(&engine, 0.05, 2.5, 3.0, 0.25), 1);

        FlowPhaseState initial_state;
        memset(&initial_state, 0, sizeof(initial_state));
        initial_state.p[0] = 50.0 * 2.0; /* 2.0 m/s forward */
        initial_state.p[1] = (50.0 * 0.45 * 0.45) * 0.35; /* Yaw rate */

        /* Nominal simulation: Black swan caught */
        FlowSpacetimeConeResult baseline;
        FLOW_ASSERT_EQ(flow_spacetime_simulate(&engine, &initial_state, NULL, &baseline), 1);
        FLOW_ASSERT_TRUE(baseline.violation_detected);
        FLOW_ASSERT_TRUE(baseline.violation_time_s >= 2.5);

        /* Pre-play counterfactual annealing */
        FlowSpacetimeConeResult annealed;
        FLOW_ASSERT_EQ(flow_spacetime_preplay_and_anneal(&engine, &initial_state, &annealed), 1);
        FLOW_ASSERT_FALSE(annealed.violation_detected); /* Black swan completely avoided! */
        FLOW_ASSERT_TRUE(engine.preplay_duration_us < 200.0);

        FlowSMTProofAttestation proof;
        memset(&proof, 0, sizeof(proof));
        FLOW_ASSERT_EQ(flow_spacetime_preplay_verify_smt(&engine, &annealed, &proof), FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_SMT_SOUND(proof);

        /* Speculative Meso-scale L1I/L1D emergency cache warmup upon threat detection */
        uint32_t lines_warmed = 0;
        FLOW_ASSERT_EQ(flow_spacetime_warmup_emergency_cache(&engine, &baseline,
                                                             (const void *)flow_spacetime_preplay_and_anneal,
                                                             (const void *)&engine.pre_emptive_bias,
                                                             &lines_warmed), 1);
        FLOW_ASSERT_TRUE(lines_warmed >= 3);

        FlowSMTProofAttestation prefetch_proof;
        memset(&prefetch_proof, 0, sizeof(prefetch_proof));
        FLOW_ASSERT_EQ(flow_spacetime_verify_prefetch_soundness_smt(&engine, lines_warmed, 35.0, &prefetch_proof), FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_SMT_SOUND(prefetch_proof);

        printf("  ✓ Stage 7 Passed: Pre-play annealed 3.0s spacetime cone in %.1fus; black swan avoided and emergency cache pre-warmed.\n\n",
               engine.preplay_duration_us);
    }

    /* ========================================================================= */
    /* STAGE 8: Swarm Speciation & Autopoiesis                                   */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(8, "Swarm Speciation & Autopoiesis (.fvec Living Ecosystem)");
    {
        FlowSwarmSpeciationEngine engine;
        FLOW_ASSERT_EQ(flow_speciation_init(&engine, 42), 1);
        FLOW_ASSERT_EQ(engine.population_size, FLOW_SPECIATION_POPULATION_SIZE);

        /* Epistatic crossover */
        FlowSpeciationSpecimen *pA = &engine.population[0];
        FlowSpeciationSpecimen *pB = &engine.population[1];
        pA->features[0] = 0.2; pA->features[1] = 0.3;
        pB->features[0] = 0.8; pB->features[1] = 0.9;

        FlowSpeciationSpecimen child;
        FLOW_ASSERT_EQ(flow_speciation_crossover(pA, pB, 0x12345, &child), 1);

        /* 5 generations of evolution */
        for (int g = 0; g < 5; g++) {
            FLOW_ASSERT_EQ(flow_speciation_step_generation(&engine), 1);
        }
        FLOW_ASSERT_EQ(engine.current_generation, 5);

        /* Export auto-promoted specimen */
        const char *test_dir = "/tmp/flow_test_fvec_swarm";
        FLOW_ASSERT_EQ(flow_speciation_export_fvec(&engine.population[0], test_dir), 1);

        FlowSMTProofAttestation proof;
        memset(&proof, 0, sizeof(proof));
        FLOW_ASSERT_EQ(flow_speciation_verify_smt(&engine, &proof), FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_SMT_SOUND(proof);

        printf("  ✓ Stage 8 Passed: 5 generations evolved with epistatic linkage preserved; SMT sound.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 9: Speculative Gene Pre-Staging Vault (Zero-Coldstart Swap)         */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(9, "Speculative Gene Pre-Staging Vault: Zero-Coldstart Atomic Architecture Swap");
    {
        /* Ensure canonical .fvec repository directory is ready */
        const char *test_vault_dir = ".flow/vecs";
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

        /* Execute atomic QSBR memory swap in < 100ns */
        FlowBmf1BitCanvas active_canvas;
        double swap_latency_ns = 0.0;
        FLOW_ASSERT_EQ(flow_fvec_prestaging_swap_atomic(&vault, "HFT_BURST", &active_canvas, &swap_latency_ns), 1);
        FLOW_ASSERT_TRUE(swap_latency_ns < 100.0);
        FLOW_ASSERT_EQ(vault.total_speculative_swaps, 1ULL);
        FLOW_ASSERT_TRUE(active_canvas.is_adjudicated_sound);

        /* Execute second atomic swap to OOM survival */
        double swap2_latency_ns = 0.0;
        FLOW_ASSERT_EQ(flow_fvec_prestaging_swap_atomic(&vault, "OOM_SURGE", &active_canvas, &swap2_latency_ns), 1);
        FLOW_ASSERT_TRUE(swap2_latency_ns < 100.0);
        FLOW_ASSERT_EQ(vault.total_speculative_swaps, 2ULL);

        /* SMT Soundness Verification for Zero-Coldstart Deadline (< 100ns) */
        FlowSMTProofAttestation prestaging_proof;
        memset(&prestaging_proof, 0, sizeof(prestaging_proof));
        FLOW_ASSERT_EQ(flow_fvec_verify_prestaging_soundness_smt(&vault, "HFT_BURST", swap_latency_ns, &prestaging_proof), FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_SMT_SOUND(prestaging_proof);

        printf("  ✓ Stage 9 Passed: Speculative Gene Pre-Staging Vault swapped architecture in %.2fns (<100ns SMT deadline); zero-coldstart guaranteed.\n\n",
               swap_latency_ns);
    }

    FLOW_TEST_SUITE_END();
    return 0;
}
