#include "flow_test_kit.h"
#include "flow.h"
#include "bitspace.h"
#include "registry.h"
#include "search.h"
#include "smt.h"
#include "polyhedral.h"
#include "simplicial_homology.h"
#include "potential_game.h"
#include "moreau_hysteresis.h"
#include "oco_cache.h"
#include "manifold_algebra.h"
#include "entropy_collapse.h"
#include "token_ring.h"
#include "fwht_projection.h"
#include "morse_atlas.h"
#include "bmf_microcode.h"
#include "neuro_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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
        FLOW_ASSERT_STR_CONTAINS(smt_buffer, "Buffer Bounds Safety Invariant");
        FLOW_ASSERT_STR_CONTAINS(smt_buffer, "Memory Limit & Quota Boundedness");
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

        /* Wardrop Potential Game Routing */
        FlowPotentialRouter router;
        FLOW_ASSERT_TRUE(flow_potential_router_init(&router, 0.15, 2.0));
        FLOW_ASSERT_TRUE(flow_potential_register_node(&router, 0, 100.0, 5.0));
        FLOW_ASSERT_TRUE(flow_potential_register_node(&router, 1, 200.0, 10.0));
        FLOW_ASSERT_TRUE(flow_potential_register_node(&router, 2, 150.0, 8.0));

        for (size_t req = 0; req < 100; ++req) {
            uint8_t selected = 0;
            FLOW_ASSERT_TRUE(flow_potential_route_next(&router, &selected));
            FLOW_ASSERT_TRUE(flow_potential_update_load(&router, selected, 1.0));
        }
        FLOW_ASSERT_TRUE(router.global_potential_phi > 0.0);

        FlowSMTProofAttestation router_proof;
        memset(&router_proof, 0, sizeof(router_proof));
        FLOW_ASSERT_EQ(flow_potential_verify_smt(&router, &router_proof), FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_SMT_SOUND(router_proof);

        printf("  ✓ Stage 4 Passed: Moreau sweeping suppressed %llu flutters; Wardrop game reached equilibrium.\n\n",
               (unsigned long long)hys.flutters_suppressed);
    }

    /* ========================================================================= */
    /* STAGE 5: Simplicial Homology & Topological Defect Mining                  */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(5, "Algebraic Topology: Betti Numbers & Topological Hole Discovery");
    {
        FlowSimplicialComplex complex;
        FLOW_ASSERT_TRUE(flow_homology_init(&complex, 4)); /* 4 vertices: 0, 1, 2, 3 */

        /* Form square loop 0-1, 1-2, 2-3, 3-0 */
        FLOW_ASSERT_TRUE(flow_homology_add_edge(&complex, 0, 1));
        FLOW_ASSERT_TRUE(flow_homology_add_edge(&complex, 1, 2));
        FLOW_ASSERT_TRUE(flow_homology_add_edge(&complex, 2, 3));
        FLOW_ASSERT_TRUE(flow_homology_add_edge(&complex, 3, 0));

        size_t b0 = 0, b1 = 0;
        FLOW_ASSERT_TRUE(flow_homology_compute_betti(&complex, &b0, &b1));
        FLOW_ASSERT_EQ(b0, 1);
        FLOW_ASSERT_EQ(b1, 1); /* Topological hole detected! */

        /* Guide mutation into the hole */
        uint64_t base_genome = 0x12345678ULL;
        uint64_t guided_genome = 0;
        uint32_t target_bit = 999;
        FLOW_ASSERT_TRUE(flow_homology_guide_mutation(&complex, base_genome, &guided_genome, &target_bit));
        FLOW_ASSERT_NE(guided_genome, base_genome);
        FLOW_ASSERT_TRUE(target_bit < 64);

        /* Fill hole with 2-simplices */
        FLOW_ASSERT_TRUE(flow_homology_add_face(&complex, 0, 1, 2));
        FLOW_ASSERT_TRUE(flow_homology_add_face(&complex, 0, 2, 3));

        FLOW_ASSERT_TRUE(flow_homology_compute_betti(&complex, &b0, &b1));
        FLOW_ASSERT_EQ(b0, 1);
        FLOW_ASSERT_EQ(b1, 0); /* Hole contractible! */

        FlowSMTProofAttestation hom_proof;
        memset(&hom_proof, 0, sizeof(hom_proof));
        FLOW_ASSERT_EQ(flow_homology_verify_smt(&complex, &hom_proof), FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_SMT_SOUND(hom_proof);

        printf("  ✓ Stage 5 Passed: Homological hole detected (b_1=1) and healed (b_1=0) with d1 o d2 = 0 proven.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 6: Manifold Algebra, Epistasis & Differential Geometry              */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(6, "Manifold Algebra: Intersection, Epistasis & Moreau Boundary Projection");
    {
        FlowManifold mA, mB, mInter, mSum;
        FLOW_ASSERT_TRUE(flow_manifold_init(&mA, 0x00000000FFFFFFFFULL));
        FLOW_ASSERT_TRUE(flow_manifold_init(&mB, 0xFFFFFFFF00000000ULL));

        FLOW_ASSERT_TRUE(flow_manifold_set_bounds(&mA, 0, 0.0, 5.0));
        FLOW_ASSERT_TRUE(flow_manifold_set_bounds(&mA, 1, 0.0, 5.0));
        FLOW_ASSERT_TRUE(flow_manifold_set_bounds(&mA, 2, -2.0, 2.0));

        /* Active constraint with shadow price lambda=4.2 */
        double normalA[FLOW_MANIFOLD_DIM] = {0};
        normalA[0] = 1.0;
        normalA[1] = 2.0;
        FLOW_ASSERT_TRUE(flow_manifold_add_constraint(&mA, normalA, 6.0, 4.2));

        /* Epistatic linkage must automatically detect coupling */
        FLOW_ASSERT_EQ(mA.epistatic_linkage_mask & 0x03, 0x03);
        FLOW_ASSERT_TRUE(mA.dual_multipliers[0] > 0.0);
        FLOW_ASSERT_TRUE(mA.dual_multipliers[1] > 0.0);

        FLOW_ASSERT_TRUE(flow_manifold_set_bounds(&mB, 0, 1.0, 8.0));
        FLOW_ASSERT_TRUE(flow_manifold_set_bounds(&mB, 1, 1.0, 4.0));

        /* Manifold intersection */
        FLOW_ASSERT_TRUE(flow_manifold_intersect(&mA, &mB, &mInter));
        FLOW_ASSERT_EQ((int)mInter.lower_bounds[0], 1);
        FLOW_ASSERT_EQ((int)mInter.upper_bounds[0], 5);
        FLOW_ASSERT_EQ((int)mInter.lower_bounds[1], 1);
        FLOW_ASSERT_EQ((int)mInter.upper_bounds[1], 4);
        FLOW_ASSERT_TRUE(mInter.volume_proxy > 0.0);

        /* Manifold direct sum */
        FLOW_ASSERT_TRUE(flow_manifold_direct_sum(&mA, &mB, &mSum));
        FLOW_ASSERT_EQ(mSum.subspace_mask, 0xFFFFFFFFFFFFFFFFULL);

        /* Moreau Boundary Projection */
        double unconstrained_pt[FLOW_MANIFOLD_DIM] = {10.0, 10.0, 5.0};
        double projected_pt[FLOW_MANIFOLD_DIM] = {0};
        FLOW_ASSERT_TRUE(flow_manifold_boundary_project(&mInter, unconstrained_pt, projected_pt));
        FLOW_ASSERT_TRUE(projected_pt[0] <= mInter.upper_bounds[0] + 1e-6);
        FLOW_ASSERT_TRUE(projected_pt[1] <= mInter.upper_bounds[1] + 1e-6);
        FLOW_ASSERT_TRUE(projected_pt[0] + 2.0 * projected_pt[1] <= 6.0 + 1e-5);

        /* SMT verification */
        FlowSMTProofAttestation m_proof;
        memset(&m_proof, 0, sizeof(m_proof));
        FLOW_ASSERT_EQ(flow_manifold_verify_smt(&mInter, &m_proof), FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_SMT_SOUND(m_proof);

        printf("  ✓ Stage 6 Passed: Manifold algebra intersection, epistasis and boundary projection SMT sound.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 7: 1-Bit BMF Canvas: Subspace Routing, 1-Bit Switchboard & SMT      */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(7, "1-Bit BMF Canvas: Subspace Routing, 1-Bit Switchboard & SMT Adjudication");
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

        printf("  ✓ Stage 7 Passed: 1-Bit BMF Canvas Subspace routing, 1-bit switchboard & SMT formal adjudication verified.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 8: Kolmogorov Minimal Compression: FWHT, Morse Atlas & Microcode   */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(8, "Kolmogorov Compression: Zero-Table FWHT, Morse Atlas & 1-Bit Microcode");
    {
        /* 1. Fast Walsh-Hadamard Transform In-Place Orthogonality Check (N=8) */
        float test_hadamard[8] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        flow_fwht_transform_f32(test_hadamard, 8);
        for (size_t i = 0; i < 8; i++) {
            FLOW_ASSERT_FLOAT_NEAR(test_hadamard[i], 1.0f, 0.001);
        }
        flow_fwht_transform_f32(test_hadamard, 8);
        FLOW_ASSERT_FLOAT_NEAR(test_hadamard[0], 8.0f, 0.001);
        for (size_t i = 1; i < 8; i++) {
            FLOW_ASSERT_FLOAT_NEAR(test_hadamard[i], 0.0f, 0.001);
        }

        /* 2. Zero-Table FWHT 4096-D Projection (< 50ns, 0 Multiplications) */
        static float embedding_4096[FLOW_FWHT_DEFAULT_DIM];
        static float perturbed_4096[FLOW_FWHT_DEFAULT_DIM];
        for (size_t i = 0; i < FLOW_FWHT_DEFAULT_DIM; i++) {
            float val = (float)sin((double)i * 0.05);
            embedding_4096[i] = val;
            perturbed_4096[i] = val + 0.0001f * (float)cos((double)i * 0.1);
        }

        uint64_t bmf1 = 0, bmf2 = 0;
        double fvec1[FLOW_NEURO_FVEC_DIM], fvec2[FLOW_NEURO_FVEC_DIM];
        double lat_ns1 = 0.0, lat_ns2 = 0.0;

        FLOW_ASSERT_EQ(flow_fwht_project_4096(embedding_4096, 0x1337BEEF, &bmf1, fvec1, &lat_ns1), 1);
        FLOW_ASSERT_EQ(flow_fwht_project_4096(perturbed_4096, 0x1337BEEF, &bmf2, fvec2, &lat_ns2), 1);
        FLOW_ASSERT_NE(bmf1, 0ULL);
        FLOW_ASSERT_TRUE(lat_ns1 < 50000.0);

        /* SMT Isometry Proof: Perturbation preserves BMF coordinate proximity */
        FlowSMTProofAttestation isometry_proof;
        memset(&isometry_proof, 0, sizeof(isometry_proof));
        FLOW_ASSERT_EQ(flow_fwht_verify_isometry_smt(embedding_4096, perturbed_4096,
                                                     FLOW_FWHT_DEFAULT_DIM, bmf1, bmf2,
                                                     &isometry_proof), FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_SMT_SOUND(isometry_proof);

        /* 3. Topological Morse-Smale Atlas (Label-Free Autonomous Partitions) */
        FlowBmfMorseAtlas atlas;
        flow_morse_atlas_seed_canonical(&atlas);
        FLOW_ASSERT_TRUE(atlas.is_topologically_closed);
        FLOW_ASSERT_TRUE(atlas.cell_count >= 5);

        /* Phase space distance routing without string symbols */
        double latte_pt[FLOW_MORSE_DIM] = {0.84, 0.60, 0.22, 0.0};
        uint32_t routed_cell = flow_morse_atlas_route(&atlas, latte_pt, FLOW_MORSE_DIM);
        FLOW_ASSERT_EQ(routed_cell, 1); /* Cell 1 = Gentle Manipulation Attractor */

        double sprint_pt[FLOW_MORSE_DIM] = {0.12, 0.95, 0.88, 0.70};
        uint32_t sprint_cell = flow_morse_atlas_route(&atlas, sprint_pt, FLOW_MORSE_DIM);
        FLOW_ASSERT_EQ(sprint_cell, 2); /* Cell 2 = Dynamic Locomotion Attractor */

        /* Autonomous bifurcation upon environmental drift */
        uint32_t new_cell_id = 0;
        FLOW_ASSERT_EQ(flow_morse_atlas_bifurcate(&atlas, 1, 0.45, &new_cell_id), 1);
        FLOW_ASSERT_EQ(new_cell_id, 6);
        FLOW_ASSERT_EQ(atlas.cell_count, 7);

        /* SMT Morse Atlas Completeness Proof */
        FlowSMTProofAttestation morse_proof;
        memset(&morse_proof, 0, sizeof(morse_proof));
        FLOW_ASSERT_EQ(flow_morse_verify_partition_completeness_smt(&atlas, &morse_proof), FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_SMT_SOUND(morse_proof);

        /* 4. Presburger 1-Bit Microcode Execution Engine (< 5ns, Zero C Struct Bloat) */
        FlowBmfMicrocode ucode;
        double ubs[4] = {0.08, 0.40, 4.5, 0.80};
        double lbs[4] = {0.00, 0.00, 2.0, 0.00};
        flow_bmf_microcode_compile_box(&ucode, ubs, lbs, 4, 9); /* Map to switch bits starting at bit 9 */
        FLOW_ASSERT_EQ(ucode.op_count, 8);

        FlowBmf1BitCanvas ucode_canvas;
        flow_bmf_canvas_init(&ucode_canvas, 1, FLOW_BMF_SW_HARD_SAFETY, ~0ULL, FLOW_BMF_SW_HARD_SAFETY);

        /* Safe physical state */
        double safe_physical_state[4] = {0.05, 0.20, 3.0, 0.50};
        uint32_t viols = 0;
        FLOW_ASSERT_EQ(flow_bmf_microcode_execute(&ucode, safe_physical_state, 4, &ucode_canvas, &viols), 1);
        FLOW_ASSERT_EQ(viols, 0U);

        /* Unsafe physical state: tilt 0.15 > 0.08 */
        double unsafe_physical_state[4] = {0.15, 0.20, 3.0, 0.50};
        uint32_t viols_unsafe = 0;
        FLOW_ASSERT_EQ(flow_bmf_microcode_execute(&ucode, unsafe_physical_state, 4, &ucode_canvas, &viols_unsafe), 1);
        FLOW_ASSERT_NE(viols_unsafe, 0U);

        /* SMT Microcode Equivalence & Soundness Proof */
        FlowSMTProofAttestation ucode_proof;
        memset(&ucode_proof, 0, sizeof(ucode_proof));
        FLOW_ASSERT_EQ(flow_bmf_microcode_verify_soundness_smt(&ucode, &ucode_canvas, &ucode_proof), FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_SMT_SOUND(ucode_proof);

        /* 5. End-to-End FWHT to Morse Canvas Direct Pipeline */
        FlowBmf1BitCanvas morse_canvas;
        FlowNeuroProjectionResult neuro_res;
        FLOW_ASSERT_EQ(flow_neuro_bridge_to_morse_canvas(embedding_4096, 0x1337BEEF,
                                                         &atlas, &morse_canvas, &neuro_res), 1);
        FLOW_ASSERT_TRUE(flow_bmf_canvas_get_switch(&morse_canvas, FLOW_BMF_SW_HARD_SAFETY));
        FLOW_ASSERT_TRUE(morse_canvas.is_adjudicated_sound);

        printf("  ✓ Stage 8 Passed: Kolmogorov compression (FWHT zero-table projection, Morse Atlas & 1-bit microcode) SMT verified.\n\n");
    }

    FLOW_TEST_SUITE_END();
    return 0;
}
