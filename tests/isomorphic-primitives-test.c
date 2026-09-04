#include "bitmanifold.h"
#include "smt.h"
#include "wire_frame.h"
#include "reload.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) if (!(cond)) { \
    fprintf(stderr, "isomorphic-primitives-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    exit(1); \
}

int main(void) {
    printf("========================================================================================\n");
    printf("  ⚡ Running FLOW Four Isomorphic Primitives & BitManifold (BMF) Test Suite (#68)\n");
    printf("========================================================================================\n\n");

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 1: BitManifold (BMF) & 64-Bit Subspace Slicing                               */
    /* ---------------------------------------------------------------------------------- */
    printf("--- [Stage 1: BitManifold (BMF) Genome Subspace Slicing & Projections] ---\n");
    {
        /* Test basic compile-time masks */
        CHECK(FLOW_GENOME_MASK(0) == 0ULL);
        CHECK(FLOW_GENOME_MASK(1) == 1ULL);
        CHECK(FLOW_GENOME_MASK(3) == 7ULL);
        CHECK(FLOW_GENOME_MASK(64) == ~0ULL);

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

        CHECK(FLOW_GENOME_GET(genome, 0, 3) == 5);
        CHECK(FLOW_GENOME_GET(genome, 3, 5) == 16);
        CHECK(FLOW_GENOME_GET(genome, 8, 6) == 32);
        CHECK(FLOW_GENOME_GET(genome, 14, 1) == 1);

        /* Modify Field 2 to 24 using FLOW_GENOME_SET */
        genome = FLOW_GENOME_SET(genome, 3, 5, 24);
        CHECK(FLOW_GENOME_GET(genome, 3, 5) == 24);
        CHECK(FLOW_GENOME_GET(genome, 0, 3) == 5);  /* Other fields preserved */
        CHECK(FLOW_GENOME_GET(genome, 8, 6) == 32);
        CHECK(FLOW_GENOME_GET(genome, 14, 1) == 1);

        /* Test flow_manifold_project() */
        uint64_t hard_mask = 0x00FFULL; /* Only lower 8 bits legally allowed */
        uint64_t dynamic_bias = 0x0002ULL;
        uint64_t raw_candidate = 0xFFFFULL;
        uint64_t projected = flow_manifold_project(raw_candidate, hard_mask, dynamic_bias);
        CHECK((projected & ~hard_mask) == 0); /* Upper bits clamped to 0 */

        /* Test flow_manifold_transition() (1-bit chaotic mutation) */
        uint64_t prng_state = 0x123456789ABCDEF0ULL;
        uint32_t mutated_bit = 999;
        uint64_t next_g = flow_manifold_transition(projected, hard_mask, &prng_state, &mutated_bit);
        CHECK(mutated_bit < 8); /* Mutated bit must fall within legal hard mask */
        CHECK(next_g == (projected ^ (1ULL << mutated_bit))); /* Exactly 1 bit flipped */
        printf("  ✓ BMF Subspace Slicing: pack/get/set, manifold project, and 1-bit transition sound.\n\n");
    }

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 2: SMT Hyper-box Constraint Polytope Verification                            */
    /* ---------------------------------------------------------------------------------- */
    printf("--- [Stage 2: SMT Hyper-box Constraint Polytope Verification (QF_LIA)] ---\n");
    {
        FlowSMTProofAttestation proof;
        memset(&proof, 0, sizeof(proof));

        FlowBoxConstraint box[3] = {
            {
                .name = "queue_depth",
                .candidate_value = 1024,
                .min_bound = 1,
                .max_bound = 4096,
                .theorem = FLOW_BOX_THEOREM_BUFFER_BOUNDS,
                .violation_msg = "exceeds queue ring capacity"
            },
            {
                .name = "memory_bytes",
                .candidate_value = 32 * 1024 * 1024,
                .min_bound = 0,
                .max_bound = 64 * 1024 * 1024,
                .theorem = FLOW_BOX_THEOREM_MEMORY_QUOTA,
                .violation_msg = "exceeds memory quota limit"
            },
            {
                .name = "shard_count",
                .candidate_value = 16,
                .min_bound = 1,
                .max_bound = 64,
                .theorem = FLOW_BOX_THEOREM_SHARD_ISOLATION,
                .violation_msg = "exceeds max shard isolation"
            }
        };

        /* Case 2a: All constraints within bounds -> PROVEN UNSAT */
        FlowSMTResult r_pass = flow_smt_verify_box_invariants("io_uring", box, 3, &proof);
        CHECK(r_pass == FLOW_SMT_PROVEN_UNSAT);
        CHECK(proof.buffer_bounds_safety == FLOW_SMT_PROVEN_UNSAT);
        CHECK(proof.memory_quota_bound == FLOW_SMT_PROVEN_UNSAT);
        CHECK(proof.shard_non_aliasing == FLOW_SMT_PROVEN_UNSAT);
        CHECK(proof.determinism_invariant == FLOW_SMT_PROVEN_UNSAT);
        printf("  ✓ SMT Box Sound: %s\n", proof.proof_summary);

        /* Case 2b: Memory candidate exceeds upper bound -> SAT VIOLATION */
        box[1].candidate_value = 128 * 1024 * 1024;
        FlowSMTResult r_fail = flow_smt_verify_box_invariants("io_uring", box, 3, &proof);
        CHECK(r_fail == FLOW_SMT_VIOLATION_SAT);
        CHECK(proof.memory_quota_bound == FLOW_SMT_VIOLATION_SAT);
        CHECK(strstr(proof.proof_summary, "exceeds memory quota limit") != NULL);
        printf("  ✓ SMT Box Violation caught: %s\n\n", proof.proof_summary);
    }

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 3: 9-Byte Wire Pheromone Framing (FlowWireFrame9)                            */
    /* ---------------------------------------------------------------------------------- */
    printf("--- [Stage 3: 9-Byte Wire Pheromone Framing & CRC16 Checks] ---\n");
    {
        CHECK(sizeof(FlowWireFrame9) == 9);

        /* 3a: Antibody (0xAA) */
        uint8_t pkt_ab[9];
        uint64_t orig_hash = 0xFEEDBEEF01234567ULL;
        CHECK(flow_wire_frame9_pack_antibody(orig_hash, pkt_ab) == 1);
        CHECK(pkt_ab[0] == 0xAA);

        uint64_t decoded_hash = 0;
        CHECK(flow_wire_frame9_unpack_antibody(pkt_ab, &decoded_hash) == 1);
        CHECK(decoded_hash == orig_hash);
        printf("  ✓ FlowWireFrame9 Antibody (0xAA): packed & unpacked 8-byte hash.\n");

        /* 3b: Hetero Mesh (0xBB) */
        uint8_t pkt_hm[9];
        CHECK(flow_wire_frame9_pack_hetero(2, 5, 250, 42, 0x5A5A, pkt_hm) == 1);
        CHECK(pkt_hm[0] == 0xBB);

        uint8_t r = 0, nid = 0;
        uint16_t bp = 0, lat = 0, crc = 0;
        CHECK(flow_wire_frame9_unpack_hetero(pkt_hm, &r, &nid, &bp, &lat, &crc) == 1);
        CHECK(r == 2 && nid == 5 && bp == 250 && lat == 42 && crc == 0x5A5A);
        printf("  ✓ FlowWireFrame9 HeteroMesh (0xBB): packed & unpacked fluid telemetry.\n");

        /* 3c: Fleet Sync (0xCC) with CRC16 */
        uint8_t pkt_fl[9];
        CHECK(flow_wire_frame9_pack_fleet(7, 3, -1500, 2400, pkt_fl) == 1);
        CHECK(pkt_fl[0] == 0xCC);

        uint8_t aid = 0, arole = 0;
        int16_t x = 0, y = 0;
        CHECK(flow_wire_frame9_unpack_fleet(pkt_fl, &aid, &arole, &x, &y) == 1);
        CHECK(aid == 7 && arole == 3 && x == -1500 && y == 2400);

        /* Corrupt byte 3 and ensure CRC16 rejects */
        pkt_fl[3] ^= 0xFF;
        CHECK(flow_wire_frame9_unpack_fleet(pkt_fl, &aid, &arole, &x, &y) == 0);
        printf("  ✓ FlowWireFrame9 Fleet (0xCC): CRC16 verification and corruption rejection passed.\n\n");
    }

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 4: QSBR Reader Lifecycle & Cache Line Isolation (FlowPluginRuntimeScope)     */
    /* ---------------------------------------------------------------------------------- */
    printf("--- [Stage 4: QSBR Reader Scope Lifecycle & Cache Alignment] ---\n");
    {
        /* Check 64-byte alignment and false-sharing padding */
        CHECK(sizeof(FlowPluginRuntimeScope) >= 64);
        CHECK(_Alignof(FlowPluginRuntimeScope) >= 64);

        FlowReloadContext *ctx = flow_reload_create(NULL);
        CHECK(ctx != NULL);

        FlowPluginRuntimeScope scope;
        CHECK(flow_plugin_scope_enter(&scope, ctx) == 1);
        CHECK(scope.is_registered == 1);

        for (int i = 0; i < 100; ++i) {
            flow_plugin_scope_checkpoint(&scope);
        }
        CHECK(scope.checkpoint_count == 100);

        flow_plugin_scope_pause(&scope);
        flow_plugin_scope_resume(&scope);
        flow_plugin_scope_exit(&scope);
        CHECK(scope.is_registered == 0);

        /* Test FLOW_WITH_QSBR_SCOPE block macro */
        int executed = 0;
        FLOW_WITH_QSBR_SCOPE(ctx, s, {
            CHECK(s->is_registered == 1);
            flow_plugin_scope_checkpoint(s);
            executed = 1;
        });
        CHECK(executed == 1);

        flow_reload_destroy(ctx);
        printf("  ✓ QSBR Scope: 64-byte aligned, false-sharing isolated, enter/pause/resume/exit sound.\n\n");
    }

    printf("========================================================================================\n");
    printf("  ISOMORPHIC_PRIMITIVES_TEST=PASSED: All 4 unified isomorphic primitives 100%% verified!\n");
    printf("========================================================================================\n");
    return 0;
}
