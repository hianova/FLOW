#include "flow_test_kit.h"
#include "flow.h"
#include "bitspace.h"
#include "registry.h"
#include "search.h"
#include "smt.h"
#include "polyhedral.h"
#include "moreau_hysteresis.h"
#include "entropy_collapse.h"
#include "token_ring.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "topology.h"
#include "cubical_hott.h"
int main(void) {
    FLOW_TEST_SUITE_BEGIN("Brain: Bit-Manifold Form, SMT Supreme Court & Algebraic Geometry");

    FLOW_ASSERT_TRUE(flow_registry_init());

    /* ========================================================================= */
    /* STAGE 1: BMF BitSpace, Genome Encoding & 1-Bit Mutation                   */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(1, "BMF BitSpace: 64-Bit Genome Decoding, Evaluation & Chaotic Mutation");
    {
        SemanticIR ir;
        memset(&ir, 0, sizeof(ir));
        strncpy(ir.flow_name, "brain_rank_flow", sizeof(ir.flow_name) - 1);
        ir.input_max_count = 100;
        ir.top_n = 100;
        ir.memory_limit_mb = 4;
        ir.state_shared = 1;
        ir.state_read_heavy = 1;
        ir.fact_unordered = 1;

        const FlowPlugin *builtin = flow_registry_lookup("builtin");
        FLOW_ASSERT_TRUE(builtin != NULL);
        const Component *comp = NULL;
        for (size_t i = 0; i < builtin->component_count; ++i) {
            if (strcmp(builtin->components[i].id, "sharded_hash") == 0) {
                comp = &builtin->components[i];
                break;
            }
        }
        FLOW_ASSERT_TRUE(comp != NULL);

        FlowBitSpace space;
        FLOW_ASSERT_TRUE(flow_bitspace_init_single(&ir, comp, &space));
        FLOW_ASSERT_TRUE(space.bit_count > 0);
        FLOW_ASSERT_TRUE(space.schema_hash != 0);

        /* Decode valid plan and test hard gate */
        FlowPlan valid_plan;
        FLOW_ASSERT_TRUE(space.decode(&space, UINT64_C(0x6), &valid_plan));
        FLOW_ASSERT_EQ(valid_plan.component, comp);
        FLOW_ASSERT_TRUE(space.evaluate(&space, &valid_plan, &valid_plan.eval));
        FLOW_ASSERT_TRUE(valid_plan.eval.capacity >= 100);
        FLOW_ASSERT_TRUE(space.hard_gate(&space, &valid_plan, &valid_plan.eval));

        /* Hard gate rejection on undersized genome */
        FlowPlan small_plan;
        FLOW_ASSERT_TRUE(space.decode(&space, UINT64_C(0x0), &small_plan));
        FLOW_ASSERT_TRUE(space.evaluate(&space, &small_plan, &small_plan.eval));
        FLOW_ASSERT_FALSE(space.hard_gate(&space, &small_plan, &small_plan.eval));

        /* Hard gate rejection on oversized genome (exceeds memory) */
        FlowPlan huge_plan;
        FLOW_ASSERT_TRUE(space.decode(&space, UINT64_C(0x1a2b3c), &huge_plan));
        FLOW_ASSERT_TRUE(space.evaluate(&space, &huge_plan, &huge_plan.eval));
        FLOW_ASSERT_FALSE(space.hard_gate(&space, &huge_plan, &huge_plan.eval));

        /* 1-Bit Chaotic Mutation: Exactly 1 bit flipped */
        uint64_t rng = UINT64_C(0x123456789abcdef0);
        uint32_t flipped_bit = 0;
        uint64_t mutated_genome = flow_bitspace_mutate_1bit(&space, valid_plan.genome, &rng, &flipped_bit);
        FLOW_ASSERT_NE(mutated_genome, valid_plan.genome);
        uint64_t diff = mutated_genome ^ valid_plan.genome;
        FLOW_ASSERT_TRUE((diff & (diff - 1)) == 0);
        FLOW_ASSERT_TRUE(flipped_bit < space.bit_count);

        /* Hierarchical BitSpace for multi-candidate exploration */
        SemanticIR multi_ir = ir;
        multi_ir.state_read_heavy = 0;
        multi_ir.fact_ordered = 1;
        multi_ir.fact_unordered = 0;
        FlowBitSpace multi_space;
        FLOW_ASSERT_TRUE(flow_bitspace_init_for_ir(&multi_ir, &multi_space));
        FLOW_ASSERT_TRUE(multi_space.candidate_count >= 2);

        FlowBitSearchResult s_res;
        FLOW_ASSERT_TRUE(flow_bitspace_search(&multi_space, 50, 42, 0, NULL, &s_res));
        FLOW_ASSERT_TRUE(s_res.best_plan.component != NULL);
        FLOW_ASSERT_TRUE(s_res.best_plan.eval.capacity >= 100);
        FLOW_ASSERT_TRUE(s_res.pareto_count > 0);

        printf("  ✓ Stage 1 Passed: 64-Bit BMF Space, 1-bit chaotic mutation & hierarchical search validated.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 2: SMT Supreme Court 4-Theorems Formal Proofs                       */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(2, "SMT Supreme Court: QF_LIA 4-Theorems & Formal Invariant Soundness");
    {
        SemanticIR ir;
        memset(&ir, 0, sizeof(ir));
        strncpy(ir.flow_name, "brain_smt_pipeline", sizeof(ir.flow_name) - 1);
        ir.input_max_count = 1024;
        ir.memory_limit_mb = 16;
        ir.fact_deterministic = 1;

        const Component *comp = select_component(&ir);
        FLOW_ASSERT_TRUE(comp != NULL);

        FlowPlanMetrics sound_metrics = {
            .capacity = 2048, /* > 1024 -> Buffer bounds safety strictly UNSAT (proven) */
            .threads = 4,
            .shards = 1,
            .memory_bytes = 16384, /* << 16MB -> Memory quota bound strictly UNSAT (proven) */
            .latency_score = 4.0,
            .throughput_score = 4000.0,
            .energy = 20.0
        };

        FlowSMTProofAttestation attestation;
        FLOW_ASSERT_TRUE(flow_smt_verify(&ir, comp, NULL, &sound_metrics, &attestation));
        FLOW_ASSERT_EQ(attestation.buffer_bounds_safety, FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_EQ(attestation.memory_quota_bound, FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_EQ(attestation.shard_non_aliasing, FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_EQ(attestation.determinism_invariant, FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_SMT_SOUND(attestation);

        /* SMT-LIB2 script generation check */
        char smt_buffer[4096];
        FILE *smt_out = fmemopen(smt_buffer, sizeof(smt_buffer), "w");
        FLOW_ASSERT_TRUE(smt_out != NULL);
        FLOW_ASSERT_TRUE(flow_smt_generate_proof_script(&ir, comp, NULL, &sound_metrics, smt_out));
        fclose(smt_out);

        FLOW_ASSERT_STR_CONTAINS(smt_buffer, "(set-logic QF_BV)");
        FLOW_ASSERT_STR_CONTAINS(smt_buffer, "declare-const capacity (_ BitVec 64)");
        FLOW_ASSERT_STR_CONTAINS(smt_buffer, "declare-const memory_limit_bytes (_ BitVec 64)");
        FLOW_ASSERT_STR_CONTAINS(smt_buffer, "(check-sat)");

        /* Negative violation test: capacity < input_max_count and memory > limit must trigger SAT violation */
        FlowPlanMetrics violating_metrics = sound_metrics;
        violating_metrics.capacity = 512; /* 512 < 1024 -> buffer overflow! */
        violating_metrics.memory_bytes = 32u * 1024u * 1024u; /* 32MB > 16MB -> memory violation! */
        FlowSMTProofAttestation viol_attest;
        FLOW_ASSERT_FALSE(flow_smt_verify(&ir, comp, NULL, &violating_metrics, &viol_attest));
        FLOW_ASSERT_EQ(viol_attest.buffer_bounds_safety, FLOW_SMT_VIOLATION_SAT);
        FLOW_ASSERT_EQ(viol_attest.memory_quota_bound, FLOW_SMT_VIOLATION_SAT);

        printf("  ✓ Stage 2 Passed: SMT Supreme Court verified 4/4 theorems UNSAT & caught SAT violations.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 3: Polytope Feasible Set & Affine Loop Tiling                       */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(3, "Polytope Projection & Presburger Affine Optimization");
    {
        /* 1. Polyhedral Orthogonal Projection on {0,1}^N */
        FlowPlanDimensionSet dims;
        dims.count = 3;
        dims.dimensions[0] = (FlowPlanDimension){"capacity", FLOW_DIM_EXPONENT, FLOW_DIM_CLASS_STRUCTURAL_JIT, 1, 26, 1, 12, 500};
        dims.dimensions[1] = (FlowPlanDimension){"threads", FLOW_DIM_LINEAR, FLOW_DIM_CLASS_STRUCTURAL_JIT, 1, 64, 1, 1, 200};
        dims.dimensions[2] = (FlowPlanDimension){"shards", FLOW_DIM_LINEAR, FLOW_DIM_CLASS_STRUCTURAL_JIT, 1, 32, 1, 1, 200};

        FlowPolyhedronSystem poly_sys;
        flow_polyhedron_init(&poly_sys, 3);
        flow_polyhedron_add_box_bounds(&poly_sys, 0, 16.0, 1024.0, "capacity_box");
        flow_polyhedron_add_box_bounds(&poly_sys, 1, 1.0, 8.0, "threads_box");
        flow_polyhedron_add_box_bounds(&poly_sys, 2, 1.0, 16.0, "shards_box");

        uint64_t mask = flow_polyhedron_project_mask(&poly_sys, &dims, 64);
        FLOW_ASSERT_NE(mask, 0ULL);

        /* 2. Affine Loop Scheduling with Farkas Lemma */
        FlowPolyhedron poly;
        FLOW_ASSERT_TRUE(flow_polyhedral_init(&poly, 2)); /* 2D loop: 512 x 1024 */
        FLOW_ASSERT_TRUE(flow_polyhedral_set_box_bounds(&poly, 0, 0, 511));
        FLOW_ASSERT_TRUE(flow_polyhedral_set_box_bounds(&poly, 1, 0, 1023));

        FlowPolyhedralSchedule sched;
        FLOW_ASSERT_TRUE(flow_polyhedral_solve_schedule(&poly, 64, 16, &sched));
        FLOW_ASSERT_TRUE(sched.is_bounded);
        FLOW_ASSERT_EQ(sched.total_iterations, 512 * 1024);
        FLOW_ASSERT_TRUE(sched.optimal_tile_size > 0);
        FLOW_ASSERT_TRUE(sched.is_parallelizable);

        FlowSMTProofAttestation poly_proof;
        memset(&poly_proof, 0, sizeof(poly_proof));
        FLOW_ASSERT_EQ(flow_polyhedral_verify_smt(&poly, &sched, &poly_proof), FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_SMT_SOUND(poly_proof);

        printf("  ✓ Stage 3 Passed: Feasible polytope projection & Presburger affine schedule proven.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 4: Moreau Sweeping Hysteresis & Wardrop Potential Routing           */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(4, "Non-Smooth Moreau Sweeping & Wardrop Potential Equilibrium");
    {
        /* Moreau Hysteresis Deadband C = [40.0, 80.0] */
        FlowMoreauHysteresis hys;
        FLOW_ASSERT_TRUE(flow_moreau_init(&hys, 40.0, 80.0, 0));

        FLOW_ASSERT_EQ(flow_moreau_step(&hys, 35.0), 0);
        /* Fluctuations inside deadband must be suppressed */
        FLOW_ASSERT_EQ(flow_moreau_step(&hys, 50.0), 0);
        FLOW_ASSERT_EQ(flow_moreau_step(&hys, 75.0), 0);
        FLOW_ASSERT_EQ(flow_moreau_step(&hys, 60.0), 0);
        FLOW_ASSERT_TRUE(hys.flutters_suppressed >= 3);
        FLOW_ASSERT_EQ(hys.state_transitions, 0);

        /* Upper threshold penetration triggers transition */
        FLOW_ASSERT_EQ(flow_moreau_step(&hys, 85.0), 1);
        FLOW_ASSERT_EQ(hys.state_transitions, 1);

        FlowSMTProofAttestation moreau_proof;
        memset(&moreau_proof, 0, sizeof(moreau_proof));
        FLOW_ASSERT_EQ(flow_moreau_verify_smt(&hys, &moreau_proof), FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_SMT_SOUND(moreau_proof);

        printf("  ✓ Stage 4 Passed: Moreau sweeping suppressed %llu flutters.\n\n",
               (unsigned long long)hys.flutters_suppressed);
    }

    /* ========================================================================= */
    /* STAGE 6: 1-Bit BMF Canvas: Subspace Routing, 1-Bit Switchboard & SMT      */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(6, "1-Bit BMF Canvas: Subspace Routing, 1-Bit Switchboard & SMT Adjudication");
    {
        /* 1. Subspace Registry & Lookup */
        FlowBmfSubspaceRegistry reg;
        flow_bmf_subspace_init_registry(&reg);
        FLOW_ASSERT_TRUE(reg.is_initialized);
        FLOW_ASSERT_TRUE(reg.subspace_count >= 5);

        const FlowBmfSubspace *latte_sub = flow_bmf_subspace_lookup(&reg, FLOW_BMF_SUBSPACE_SMOOTH_FETCH_LATTE);
        FLOW_ASSERT_TRUE(latte_sub != NULL);
        FLOW_ASSERT_EQ(latte_sub->subspace_id, FLOW_BMF_SUBSPACE_SMOOTH_FETCH_LATTE);
        FLOW_ASSERT_TRUE((latte_sub->invariant_mask & FLOW_BMF_SW_ANTI_SPILL_TILT) != 0);
        FLOW_ASSERT_TRUE((latte_sub->invariant_mask & FLOW_BMF_SW_GRIPPER_FORCE_SAFE) != 0);

        /* 2. Subspace Indexing from 16-D Features */
        double latte_features[FLOW_BMF_SUBSPACE_FEATURE_DIM] = {0.84, 0.60, 0.22, 0.0};
        uint32_t indexed_sub = flow_bmf_subspace_index_from_features(&reg, latte_features, FLOW_BMF_SUBSPACE_FEATURE_DIM);
        FLOW_ASSERT_EQ(indexed_sub, FLOW_BMF_SUBSPACE_SMOOTH_FETCH_LATTE);

        /* 3. 1-Bit BMF Canvas Initialization from Subspace */
        FlowBmf1BitCanvas canvas;
        flow_bmf_canvas_init_from_subspace(&canvas, latte_sub);
        FLOW_ASSERT_EQ(canvas.subspace_id, FLOW_BMF_SUBSPACE_SMOOTH_FETCH_LATTE);
        FLOW_ASSERT_TRUE(flow_bmf_canvas_get_switch(&canvas, FLOW_BMF_SW_HARD_SAFETY));
        FLOW_ASSERT_TRUE(flow_bmf_canvas_get_switch(&canvas, FLOW_BMF_SW_ANTI_SPILL_TILT));
        FLOW_ASSERT_TRUE(flow_bmf_canvas_get_switch(&canvas, FLOW_BMF_SW_GRIPPER_FORCE_SAFE));

        /* 4. Invariant Protection: Attempting to clear an invariant switch is safely rejected */
        flow_bmf_canvas_set_switch(&canvas, FLOW_BMF_SW_ANTI_SPILL_TILT, 0);
        FLOW_ASSERT_TRUE(flow_bmf_canvas_get_switch(&canvas, FLOW_BMF_SW_ANTI_SPILL_TILT)); /* Must remain 1 */

        /* 5. Malleable switch toggling */
        flow_bmf_canvas_set_switch(&canvas, FLOW_BMF_SW_SIMD_VECTORIZED, 1);
        FLOW_ASSERT_TRUE(flow_bmf_canvas_get_switch(&canvas, FLOW_BMF_SW_SIMD_VECTORIZED));
        flow_bmf_canvas_set_switch(&canvas, FLOW_BMF_SW_SIMD_VECTORIZED, 0);
        FLOW_ASSERT_FALSE(flow_bmf_canvas_get_switch(&canvas, FLOW_BMF_SW_SIMD_VECTORIZED));

        /* 6. SMT Physical Formal Adjudication */
        FLOW_ASSERT_TRUE(flow_bmf_canvas_adjudicate_smt(&canvas));
        FLOW_ASSERT_TRUE(canvas.is_adjudicated_sound);

        /* Artificial violation detection in SMT */
        canvas.switchboard_bits &= ~FLOW_BMF_SW_HARD_SAFETY;
        FLOW_ASSERT_FALSE(flow_bmf_canvas_adjudicate_smt(&canvas));
        FLOW_ASSERT_FALSE(canvas.is_adjudicated_sound);
        /* Restore safety */
        canvas.switchboard_bits |= FLOW_BMF_SW_HARD_SAFETY;
        FLOW_ASSERT_TRUE(flow_bmf_canvas_adjudicate_smt(&canvas));

        /* 7. Single-cycle 1-Bit Chaotic Mutation Transition */
        uint64_t rng = 0xdeadbeef12345678ULL;
        uint32_t flipped_bit = 0;
        uint64_t prev_bits = canvas.switchboard_bits;
        uint64_t new_bits = flow_bmf_canvas_flip_1bit(&canvas, &rng, &flipped_bit);
        FLOW_ASSERT_NE(new_bits, prev_bits);
        /* Invariants must remain unviolated after flip */
        FLOW_ASSERT_TRUE(flow_bmf_canvas_verify_invariants(&canvas));

        /* 8. Token Ring Attention Projection on 1-Bit Canvas */
        uint64_t attention_mask = ~(FLOW_BMF_SW_SIMD_VECTORIZED);
        uint64_t proj = flow_token_ring_bmf_attention_project(&canvas, attention_mask, 0);
        FLOW_ASSERT_TRUE(proj != 0);
        FLOW_ASSERT_TRUE(flow_bmf_canvas_verify_invariants(&canvas));

        /* 9. Isomorphic Conversion with FlowMaskCanvas */
        FlowMaskCanvas mask_conv;
        flow_bmf_canvas_to_mask_canvas(&canvas, &mask_conv);
        FLOW_ASSERT_EQ(mask_conv.hard_composite_mask, canvas.switchboard_bits);

        FlowBmf1BitCanvas restored_canvas;
        flow_bmf_canvas_from_mask_canvas(&mask_conv, FLOW_BMF_SUBSPACE_SMOOTH_FETCH_LATTE, &restored_canvas);
        FLOW_ASSERT_EQ(restored_canvas.subspace_id, FLOW_BMF_SUBSPACE_SMOOTH_FETCH_LATTE);
        FLOW_ASSERT_EQ(restored_canvas.switchboard_bits, canvas.switchboard_bits);

        printf("  ✓ Stage 6 Passed: 1-Bit BMF Canvas Subspace routing, 1-bit switchboard & SMT formal adjudication verified.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 7: Real AST Workspace Topology Scanner & Cubical HoTT Type Sieve    */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(7, "Workspace AST Scanner & Cubical HoTT Type Matching");
    {
        FlowTopologyGraph graph;
        flow_topology_build_codebase_graph(&graph);
        FLOW_ASSERT_TRUE(graph.node_count > 10);
        FLOW_ASSERT_TRUE(graph.edge_count > 10);

        FlowTopologyAuditReport report;
        flow_topology_audit(&graph, &report);
        FLOW_ASSERT_EQ(report.cross_layer_leaks, 0);
        FLOW_ASSERT_TRUE(report.total_loc > 1000 && report.total_loc < 10000);

        SemanticIR ir;
        memset(&ir, 0, sizeof(ir));
        ir.state_shared = 1;
        ir.state_read_heavy = 1;
        ir.fact_unordered = 1;
        const Component *comp = select_component(&ir);
        FLOW_ASSERT_TRUE(comp != NULL);

        FlowCubicalTypeSieve ir_sieve = flow_cubical_sieve_from_ir(&ir);
        FlowCubicalTypeSieve comp_sieve = flow_cubical_sieve_from_component(comp);
        uint64_t mismatch = 999;
        FLOW_ASSERT_EQ(flow_cubical_match_types(ir_sieve, comp_sieve, &mismatch), 1);
        FLOW_ASSERT_EQ(mismatch, 0ULL);

        printf("  ✓ Stage 7 Passed: AST workspace scanned (%zu LOC, 0 leaks) & Cubical HoTT type sieve matched.\n\n",
               report.total_loc);
    }

    FLOW_TEST_SUITE_END();
    return 0;
}
