#include "bitmanifold.h"
#include "smt.h"
#include "wire_frame.h"
#include "reload.h"
#include "flow_test_kit.h"
#include "flow_smt_dsl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    FLOW_TEST_SUITE_BEGIN("Four Isomorphic Primitives & BitManifold (BMF) (#68)");

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 1: BitManifold (BMF) & 64-Bit Subspace Slicing                               */
    /* ---------------------------------------------------------------------------------- */
    FLOW_STAGE_BEGIN(1, "BitManifold (BMF) Genome Subspace Slicing & Projections");
    {
        /* Test basic compile-time masks */
        FLOW_ASSERT_EQ(FLOW_GENOME_MASK(0), 0ULL);
        FLOW_ASSERT_EQ(FLOW_GENOME_MASK(1), 1ULL);
        FLOW_ASSERT_EQ(FLOW_GENOME_MASK(3), 7ULL);
        FLOW_ASSERT_EQ(FLOW_GENOME_MASK(64), ~0ULL);

        /* Test Pack, Get, and Set */
        uint64_t genome = 0;
        /* Field 1: Kind (offset 0, width 3) = 5 */
        /* Field 2: Shards (offset 3, width 5) = 16 */
        /* Field 3: Threads (offset 8, width 6) = 32 */
        /* Field 4: Flag (offset 14, width 1) = 1 */
        genome = FLOW_GENOME_PACK(5, 0, 3)
               | FLOW_GENOME_PACK(16, 3, 5)
               | FLOW_GENOME_PACK(32, 8, 6)
               | FLOW_GENOME_PACK(1, 14, 1);

        FLOW_ASSERT_EQ(FLOW_GENOME_GET(genome, 0, 3), 5);
        FLOW_ASSERT_EQ(FLOW_GENOME_GET(genome, 3, 5), 16);
        FLOW_ASSERT_EQ(FLOW_GENOME_GET(genome, 8, 6), 32);
        FLOW_ASSERT_EQ(FLOW_GENOME_GET(genome, 14, 1), 1);

        /* Modify Field 2 to 24 using FLOW_GENOME_SET */
        genome = FLOW_GENOME_SET(genome, 3, 5, 24);
        FLOW_ASSERT_EQ(FLOW_GENOME_GET(genome, 3, 5), 24);
        FLOW_ASSERT_EQ(FLOW_GENOME_GET(genome, 0, 3), 5);  /* Other fields preserved */
        FLOW_ASSERT_EQ(FLOW_GENOME_GET(genome, 8, 6), 32);
        FLOW_ASSERT_EQ(FLOW_GENOME_GET(genome, 14, 1), 1);

        /* Test flow_manifold_project() */
        uint64_t hard_mask = 0x00FFULL; /* Only lower 8 bits legally allowed */
        uint64_t dynamic_bias = 0x0002ULL;
        uint64_t raw_candidate = 0xFFFFULL;
        uint64_t projected = flow_manifold_project(raw_candidate, hard_mask, dynamic_bias);
        FLOW_ASSERT_EQ(projected & ~hard_mask, 0); /* Upper bits clamped to 0 */

        /* Test flow_manifold_transition() (1-bit chaotic mutation) */
        uint64_t prng_state = 0x123456789ABCDEF0ULL;
        uint32_t mutated_bit = 999;
        uint64_t next_g = flow_manifold_transition(projected, hard_mask, &prng_state, &mutated_bit);
        FLOW_ASSERT_TRUE(mutated_bit < 8); /* Mutated bit must fall within legal hard mask */
        FLOW_ASSERT_EQ(next_g, (projected ^ (1ULL << mutated_bit))); /* Exactly 1 bit flipped */
        printf("  ✓ BMF Subspace Slicing: pack/get/set, manifold project, and 1-bit transition sound.\n\n");
    }

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 2: SMT Hyper-box Constraint Polytope Verification                            */
    /* ---------------------------------------------------------------------------------- */
    FLOW_STAGE_BEGIN(2, "SMT Hyper-box Constraint Polytope Verification (QF_LIA)");
    {
        FLOW_SMT_BOX_BUILDER_DECL(builder);
        FLOW_SMT_BOX_ADD_RULE(builder, "queue_depth", 1024, 1, 4096, FLOW_BOX_THEOREM_BUFFER_BOUNDS, "exceeds queue ring capacity");
        FLOW_SMT_BOX_ADD_RULE(builder, "memory_bytes", 32 * 1024 * 1024, 0, 64 * 1024 * 1024, FLOW_BOX_THEOREM_MEMORY_QUOTA, "exceeds memory quota limit");
        FLOW_SMT_BOX_ADD_RULE(builder, "shard_count", 16, 1, 64, FLOW_BOX_THEOREM_SHARD_ISOLATION, "exceeds max shard isolation");

        /* Case 2a: All constraints within bounds -> PROVEN UNSAT */
        FlowSMTProofAttestation proof;
        FlowSMTResult r_pass = FLOW_SMT_BOX_VERIFY(builder, "io_uring", &proof);
        FLOW_ASSERT_EQ(r_pass, FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_SMT_SOUND(proof);

        /* Case 2b: Memory candidate exceeds upper bound -> SAT VIOLATION */
        FLOW_SMT_BOX_BUILDER_DECL(fail_builder);
        FLOW_SMT_BOX_ADD_RULE(fail_builder, "queue_depth", 1024, 1, 4096, FLOW_BOX_THEOREM_BUFFER_BOUNDS, "exceeds queue ring capacity");
        FLOW_SMT_BOX_ADD_RULE(fail_builder, "memory_bytes", 128 * 1024 * 1024, 0, 64 * 1024 * 1024, FLOW_BOX_THEOREM_MEMORY_QUOTA, "exceeds memory quota limit");
        FLOW_SMT_BOX_ADD_RULE(fail_builder, "shard_count", 16, 1, 64, FLOW_BOX_THEOREM_SHARD_ISOLATION, "exceeds max shard isolation");

        FlowSMTProofAttestation fail_proof;
        FlowSMTResult r_fail = FLOW_SMT_BOX_VERIFY(fail_builder, "io_uring", &fail_proof);
        FLOW_ASSERT_EQ(r_fail, FLOW_SMT_VIOLATION_SAT);
        FLOW_ASSERT_SMT_VIOLATION(r_fail, fail_proof);
        FLOW_ASSERT_STR_CONTAINS(fail_proof.proof_summary, "exceeds memory quota limit");
        printf("\n");
    }

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 3: 9-Byte Wire Pheromone Framing (FlowWireFrame9)                            */
    /* ---------------------------------------------------------------------------------- */
    FLOW_STAGE_BEGIN(3, "9-Byte Wire Pheromone Framing & CRC16 Checks");
    {
        FLOW_ASSERT_EQ(sizeof(FlowWireFrame9), 9);

        /* 3a: Antibody (0xAA) */
        uint8_t pkt_ab[9];
        uint64_t orig_hash = 0xFEEDBEEF01234567ULL;
        FLOW_ASSERT_TRUE(flow_wire_frame9_pack_antibody(orig_hash, pkt_ab));
        FLOW_ASSERT_EQ(pkt_ab[0], 0xAA);

        uint64_t decoded_hash = 0;
        FLOW_ASSERT_TRUE(flow_wire_frame9_unpack_antibody(pkt_ab, &decoded_hash));
        FLOW_ASSERT_EQ(decoded_hash, orig_hash);
        printf("  ✓ FlowWireFrame9 Antibody (0xAA): packed & unpacked 8-byte hash.\n");

        /* 3b: Hetero Mesh (0xBB) */
        uint8_t pkt_hm[9];
        FLOW_ASSERT_TRUE(flow_wire_frame9_pack_hetero(2, 5, 250, 42, 0x5A5A, pkt_hm));
        FLOW_ASSERT_EQ(pkt_hm[0], 0xBB);

        uint8_t r = 0, nid = 0;
        uint16_t bp = 0, lat = 0, crc = 0;
        FLOW_ASSERT_TRUE(flow_wire_frame9_unpack_hetero(pkt_hm, &r, &nid, &bp, &lat, &crc));
        FLOW_ASSERT_TRUE(r == 2 && nid == 5 && bp == 250 && lat == 42 && crc == 0x5A5A);
        printf("  ✓ FlowWireFrame9 HeteroMesh (0xBB): packed & unpacked fluid telemetry.\n");

        /* 3c: Fleet Sync (0xCC) with CRC16 */
        uint8_t pkt_fl[9];
        FLOW_ASSERT_TRUE(flow_wire_frame9_pack_fleet(7, 3, -1500, 2400, pkt_fl));
        FLOW_ASSERT_EQ(pkt_fl[0], 0xCC);

        uint8_t aid = 0, arole = 0;
        int16_t x = 0, y = 0;
        FLOW_ASSERT_TRUE(flow_wire_frame9_unpack_fleet(pkt_fl, &aid, &arole, &x, &y));
        FLOW_ASSERT_TRUE(aid == 7 && arole == 3 && x == -1500 && y == 2400);

        /* Verify CRC16 Integrity Detection */
        pkt_fl[4] ^= 0xFF; /* Corrupt packet payload */
        FLOW_ASSERT_FALSE(flow_wire_frame9_unpack_fleet(pkt_fl, &aid, &arole, &x, &y));
        printf("  ✓ FlowWireFrame9 FleetSync (0xCC): CRC16 transmission error successfully rejected.\n\n");
    }

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 4: QSBR Reader Lifecycle & Runtime Scope                                     */
    /* ---------------------------------------------------------------------------------- */
    FLOW_STAGE_BEGIN(4, "QSBR Generation Lifecycle & Cacheline Isolated Scope");
    {
        /* Check 64-byte alignment of FlowPluginRuntimeScope */
        FLOW_ASSERT_EQ(sizeof(FlowPluginRuntimeScope) % 64, 0);

        FlowReloadContext *ctx = flow_reload_create("isomorphic_runtime_test");
        FLOW_ASSERT_TRUE(ctx != NULL);

        FlowPluginRuntimeScope scope;
        FLOW_ASSERT_TRUE(flow_plugin_scope_enter(&scope, ctx));
        FLOW_ASSERT_TRUE(scope.is_registered);

        for (int i = 0; i < 100; ++i) {
            flow_plugin_scope_checkpoint(&scope);
        }
        FLOW_ASSERT_EQ(scope.checkpoint_count, 100);

        flow_plugin_scope_pause(&scope);
        flow_plugin_scope_resume(&scope);
        flow_plugin_scope_exit(&scope);
        FLOW_ASSERT_FALSE(scope.is_registered);

        /* Test FLOW_WITH_QSBR_SCOPE block macro */
        int executed = 0;
        FLOW_WITH_QSBR_SCOPE(ctx, s, {
            FLOW_ASSERT_TRUE(s->is_registered);
            flow_plugin_scope_checkpoint(s);
            executed = 1;
        });
        FLOW_ASSERT_EQ(executed, 1);

        flow_reload_destroy(ctx);
        printf("  ✓ QSBR Scope: 64-byte aligned, false-sharing isolated, enter/pause/resume/exit sound.\n\n");
    }

    FLOW_TEST_SUITE_END();
    return 0;
}
