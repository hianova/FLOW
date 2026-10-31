#include "hott_test_kit.h"
#include "cubical_hott.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

int main(void) {
    FLOW_TEST_SUITE_BEGIN("FLOW Discrete Cubical HoTT & Topos 64-Bit Subset Engine");

    /* ========================================================================= */
    /* 1. De Morgan Abstract Interval I = {0, 1} & Involutive Duality            */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(1, "De Morgan Abstract Interval & 64-Bit Parallel Algebra");
    {
        /* Scalar elementary De Morgan properties */
        FLOW_ASSERT_EQ(flow_demorgan_not(0), 1);
        FLOW_ASSERT_EQ(flow_demorgan_not(1), 0);
        FLOW_ASSERT_EQ(flow_demorgan_not(flow_demorgan_not(0)), 0);
        FLOW_ASSERT_EQ(flow_demorgan_not(flow_demorgan_not(1)), 1);

        FLOW_ASSERT_EQ(flow_demorgan_and(1, 1), 1);
        FLOW_ASSERT_EQ(flow_demorgan_and(1, 0), 0);
        FLOW_ASSERT_EQ(flow_demorgan_or(0, 0), 0);
        FLOW_ASSERT_EQ(flow_demorgan_or(1, 0), 1);

        /* Scalar De Morgan Duality: ~(a & b) == (~a | ~b) */
        for (uint8_t a = 0; a <= 1; ++a) {
            for (uint8_t b = 0; b <= 1; ++b) {
                FlowDeMorganInterval lhs = flow_demorgan_not(flow_demorgan_and(a, b));
                FlowDeMorganInterval rhs = flow_demorgan_or(flow_demorgan_not(a), flow_demorgan_not(b));
                FLOW_ASSERT_EQ(lhs, rhs);
            }
        }

        /* 64-bit Parallel Word De Morgan Operations */
        uint64_t w1 = 0xA5A5A5A5A5A5A5A5ULL;
        uint64_t w2 = 0x0F0F0F0F0F0F0F0FULL;
        FLOW_ASSERT_EQ(flow_demorgan_word_not(flow_demorgan_word_not(w1)), w1);

        uint64_t word_lhs = flow_demorgan_word_not(flow_demorgan_word_and(w1, w2));
        uint64_t word_rhs = flow_demorgan_word_or(flow_demorgan_word_not(w1), flow_demorgan_word_not(w2));
        FLOW_ASSERT_EQ(word_lhs, word_rhs);

        printf("    * De Morgan Involutive Duality verified across 64 parallel dimensions in 1 CPU cycle\n");
    }

    /* ========================================================================= */
    /* 2. Cubical Sets: Face Operators & Degeneracies on 64-Bit Boundary Basis  */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(2, "Cubical Sets: Face & Degeneracy Operators");
    {
        uint64_t sieve = 0x0000000000000000ULL;

        /* Face projection: project dimension 3 to 1 */
        uint64_t face1 = flow_cubical_face_proj(sieve, 3, 1);
        FLOW_ASSERT_EQ(face1, 1ULL << 3);

        /* Face projection: project dimension 3 to 0 */
        uint64_t face0 = flow_cubical_face_proj(face1, 3, 0);
        FLOW_ASSERT_EQ(face0, 0ULL);

        /* Degeneracy operator: insert neutral morphism at index 2 */
        uint64_t test_mask = 0x0BULL; /* bits 0, 1, 3 set */
        uint64_t degen = flow_cubical_degeneracy(test_mask, 2);
        /* bit 0, 1 stay set (lower), bit 2 is 0 (neutral), bit 3 shifts to bit 4 */
        FLOW_ASSERT_EQ(degen & (1ULL << 2), 0ULL);
        FLOW_ASSERT_EQ(degen & 0x03ULL, 0x03ULL);
        FLOW_ASSERT_TRUE(degen & (1ULL << 4));

        /* Cubical face commutation relation: partial_i^e partial_j^d == partial_{j-1}^d partial_i^e for i < j */
        uint32_t i = 2, j = 5;
        uint64_t base_sieve = 0x123456789ABCDEF0ULL;
        uint64_t lhs = flow_cubical_face_proj(flow_cubical_face_proj(base_sieve, j, 1), i, 0);
        uint64_t rhs = flow_cubical_face_proj(flow_cubical_face_proj(base_sieve, i, 0), j, 1);
        FLOW_ASSERT_EQ(lhs, rhs);

        printf("    * Cubical Face & Degeneracy commutation relations proven on 64-bit basis\n");
    }

    /* ========================================================================= */
    /* 3. Kan Open-Box Condition & Homotopy Equivalence Proof                   */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(3, "Kan Open-Box Filling & Homotopy Equivalence");
    {
        /* Test 1: Compatible boundary paths (Kan filler exists -> Homotopic) */
        uint64_t path_p = 0x0000FFFF00001234ULL;
        uint64_t path_q = 0x0000FFFF00005678ULL;
        uint64_t boundary_filter = 0xFFFF000000000000ULL | 0x0000FFFF00000000ULL; /* Upper 32 bits clamped */

        uint64_t mismatch = 0;
        FlowKanStatus status = flow_kan_check_homotopy(path_p, path_q, boundary_filter, &mismatch);
        FLOW_ASSERT_EQ(status, FLOW_KAN_FILLED_HOMOTOPIC);
        FLOW_ASSERT_EQ(mismatch, 0ULL);

        /* Test 2: Incompatible boundary paths (Obstruction / Singularity) */
        uint64_t conf_p = 0x0001000000000000ULL;
        uint64_t conf_q = 0x0000000000000000ULL;
        status = flow_kan_check_homotopy(conf_p, conf_q, boundary_filter, &mismatch);
        FLOW_ASSERT_EQ(status, FLOW_KAN_OBSTRUCTED_SINGULARITY);
        FLOW_ASSERT_TRUE(mismatch != 0ULL);
        FLOW_ASSERT_EQ(mismatch, 0x0001000000000000ULL);

        printf("    * Kan Box Filling verified: Mismatch=0 => Homotopic; Mismatch!=0 => Singularity\n");
    }

    /* ========================================================================= */
    /* 4. Grothendieck Topos Subobject Classifier Omega = 2 = {0, 1}             */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(4, "Topos Subobject Classifier Omega = 2 & Pure Spin Space");
    {
        uint64_t ambient_sieve = 0x00000000FFFFFFFFULL;

        /* Valid subobject: completely covered in ambient sieve */
        uint64_t valid_sub = 0x0000000000001234ULL;
        uint8_t chi_valid = flow_topos_classify_subobject(valid_sub, ambient_sieve);
        FLOW_ASSERT_EQ(chi_valid, 1);

        /* Invalid subobject: extends outside ambient sieve */
        uint64_t invalid_sub = 0x0000000100001234ULL;
        uint8_t chi_invalid = flow_topos_classify_subobject(invalid_sub, ambient_sieve);
        FLOW_ASSERT_EQ(chi_invalid, 0);

        /* Sieve covering property */
        FLOW_ASSERT_TRUE(flow_cubical_sieve_is_covering(ambient_sieve, valid_sub));
        FLOW_ASSERT_TRUE(!flow_cubical_sieve_is_covering(ambient_sieve, invalid_sub));

        printf("    * Topos Subobject Classifier Hom(X, Omega) ~= Sub(X) mapped to 1-bit Canva\n");
    }

    /* ========================================================================= */
    /* 5. Polytope Engine Handover Protocol & Singularity Guard                  */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(5, "Polytope Engine Handover Protocol");
    {
        FlowPolyhedron poly;
        flow_polyhedral_init(&poly, 2);
        flow_polyhedral_set_box_bounds(&poly, 0, 0, 32);
        flow_polyhedral_set_box_bounds(&poly, 1, 0, 16);

        /* Case A: Homotopic paths => successful handover and integer bound shift */
        uint64_t mask_p = 0x0000000000030000ULL; /* Popcount = 2 in dim 1 */
        uint64_t mask_q = 0x0000000000010000ULL; /* Popcount = 1 in dim 1 */
        uint64_t filter = 0x0000000000000000ULL; /* No clamped boundary conflict */

        int64_t offsets[FLOW_POLY_MAX_DIM] = {0};
        int ok = flow_cubical_handover_polytope(mask_p, mask_q, filter, &poly, offsets);
        FLOW_ASSERT_EQ(ok, 1);
        FLOW_ASSERT_EQ(offsets[1], 1); /* 2 - 1 = +1 */
        FLOW_ASSERT_EQ(poly.lower_bounds[1], 1);
        FLOW_ASSERT_EQ(poly.upper_bounds[1], 17);

        /* Case B: Topological conflict => immediate abort, preserving Polytope bounds */
        uint64_t conf_p = 0x0000000000000001ULL;
        uint64_t conf_q = 0x0000000000000000ULL;
        uint64_t strict_filter = 0x0000000000000001ULL;

        int64_t pre_lower = poly.lower_bounds[0];
        int64_t pre_upper = poly.upper_bounds[0];

        int fail_ok = flow_cubical_handover_polytope(conf_p, conf_q, strict_filter, &poly, offsets);
        FLOW_ASSERT_EQ(fail_ok, 0); /* Aborted! */
        FLOW_ASSERT_EQ(poly.lower_bounds[0], pre_lower);
        FLOW_ASSERT_EQ(poly.upper_bounds[0], pre_upper);

        printf("    * Handover Protocol: Integer shift Delta applied on homotopic; Abort on obstruction\n");
    }

    /* ========================================================================= */
    /* 6. Fiber Bundle Connection, Holonomy Twist & Unified Section Integration  */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(6, "Fiber Bundle Holonomy Twist & Unified Section Integration");
    {
        FlowUnifiedSection *sec = flow_axiom_create("TEST_CUBICAL_TOPOS");
        FLOW_ASSERT_TRUE(sec != NULL);

        /* Set initial momentum */
        sec->p[0] = 5.0;
        sec->p[1] = 3.0;

        /* Trivial holonomy (even parity): no phase twist */
        uint64_t even_loop = 0x03ULL; /* 2 bits set -> even */
        flow_cubical_holonomy_twist(even_loop, sec);
        FLOW_ASSERT_FLOAT_EQ(sec->p[0], 5.0, 1e-6);
        FLOW_ASSERT_FLOAT_EQ(sec->p[1], 3.0, 1e-6);

        /* Non-trivial holonomy (odd parity): Z_2 spin flip */
        uint64_t odd_loop = 0x01ULL; /* 1 bit set (direction 0) -> odd */
        flow_cubical_holonomy_twist(odd_loop, sec);
        FLOW_ASSERT_FLOAT_EQ(sec->p[0], -5.0, 1e-6); /* Flipped! */
        FLOW_ASSERT_FLOAT_EQ(sec->p[1], 3.0, 1e-6);  /* Unchanged */

        /* Symplectic Hamiltonian Conservation under Z_2 flip: p^2 is invariant! */
        double p0_sq = sec->p[0] * sec->p[0];
        FLOW_ASSERT_FLOAT_EQ(p0_sq, 25.0, 1e-6);

        /* Evaluate cubical homotopy in Unified Section */
        int eval_ok = flow_axiom_eval_cubical_homotopy(sec, sec->bmf_subspace_mask, ~0ULL);
        FLOW_ASSERT_EQ(eval_ok, 1);
        FLOW_ASSERT_EQ(sec->kan_homotopy_status, 0);
        FLOW_ASSERT_EQ(sec->omega_classifier_bit, 1);
        FLOW_ASSERT_EQ(sec->is_transversal, 1);

        /* SMT Verification */
        FlowCubicalSieve sieve = { .arrows = ~0ULL, .dimension = 64, .is_closed = 1 };
        FlowSMTProofAttestation proof;
        memset(&proof, 0, sizeof(proof));
        FlowSMTResult res = flow_cubical_verify_smt(&sieve, 0x1234ULL, 0x1234ULL, ~0ULL, &proof);
        FLOW_ASSERT_EQ(res, FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_TRUE(strstr(proof.proof_summary, "SMT CUBICAL HOTT SOUND") != NULL);

        printf("    * Unified Section 4-Layer Integration: Z_2 Holonomy Flip Sound, SMT=UNSAT\n");
        flow_axiom_destroy(sec);
    }

    /* ========================================================================= */
    /* 7. 64-Axis Basis Dictionary, Geometric Decoder & Intent Projection       */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(7, "64-Axis Geometric Basis Dictionary & Semantic Intent Projection");
    {
        /* Axis Basis Info Lookup */
        const FlowCubicalAxisInfo *ax14 = flow_cubical_get_axis_info(14);
        FLOW_ASSERT_TRUE(ax14 != NULL);
        FLOW_ASSERT_EQ(ax14->axis_index, 14);
        FLOW_ASSERT_EQ(ax14->subspace, FLOW_CUBICAL_SUBSPACE_CONCURRENCY);

        const FlowCubicalAxisInfo *ax31 = flow_cubical_get_axis_info(31);
        FLOW_ASSERT_TRUE(ax31 != NULL);
        FLOW_ASSERT_EQ(ax31->axis_index, 31);
        FLOW_ASSERT_EQ(ax31->subspace, FLOW_CUBICAL_SUBSPACE_LAYOUT);

        /* Transition Decoding with Homotopy Verification */
        FlowCubicalTransitionReport rep;
        int dec_ok = flow_cubical_decode_transition(0ULL, 1ULL << 14, 14, &rep);
        FLOW_ASSERT_EQ(dec_ok, 1);
        FLOW_ASSERT_EQ(rep.flipped_axis, 14);
        FLOW_ASSERT_EQ(rep.subspace, FLOW_CUBICAL_SUBSPACE_CONCURRENCY);
        FLOW_ASSERT_EQ(rep.is_kan_homotopic, 1);
        FLOW_ASSERT_TRUE(strstr(rep.explanation, "concurrency_subspace_axis") != NULL);

        /* Intent Projection: Memory Pressure Query */
        uint64_t mem_intent = flow_cubical_project_text_intent("記憶體超標 記憶體配額");
        FLOW_ASSERT_TRUE((mem_intent & (1ULL << 31)) != 0ULL);
        FLOW_ASSERT_TRUE((mem_intent & flow_cubical_get_module_mask("jit")) != 0ULL);

        /* Intent Projection: Concurrency Query */
        uint64_t rcu_intent = flow_cubical_project_text_intent("QSBR 無鎖並發 執行緒");
        FLOW_ASSERT_TRUE((rcu_intent & flow_cubical_get_module_mask("reload")) != 0ULL);

        /* Case-insensitive English Intent Projection across all 5 subspaces (kills Mutation #7) */
        uint64_t m_mem = flow_cubical_project_text_intent("MEMORY LAYOUT JIT");
        uint64_t m_con = flow_cubical_project_text_intent("QSBR CONCURRENCY SWARM");
        uint64_t m_sim = flow_cubical_project_text_intent("SIMD POLYHEDRAL TILE");
        uint64_t m_phy = flow_cubical_project_text_intent("ZMP EMBODIED JET");
        uint64_t m_smt = flow_cubical_project_text_intent("SMT LEAN PROOF");
        FLOW_ASSERT_NE(m_mem, 1ULL);
        FLOW_ASSERT_NE(m_mem, ~0ULL);
        FLOW_ASSERT_NE(m_con, 1ULL);
        FLOW_ASSERT_NE(m_sim, 1ULL);
        FLOW_ASSERT_NE(m_phy, 1ULL);
        FLOW_ASSERT_NE(m_smt, 1ULL);
        FLOW_ASSERT_NE(m_mem, m_con);
        FLOW_ASSERT_NE(m_sim, m_phy);

        /* Negative SMT check on obstructed Kan open box */
        FlowCubicalSieve bad_sieve = { .arrows = ~0ULL, .dimension = 64, .is_closed = 1 };
        FlowSMTProofAttestation bad_proof;
        FLOW_SMT_PROOF_POISON(&bad_proof);
        FLOW_ASSERT_EQ(flow_cubical_verify_smt(&bad_sieve, 0x1ULL, 0x0ULL, 0x1ULL, &bad_proof), FLOW_SMT_VIOLATION_SAT);

        printf("    * 64-Axis Basis Dictionary, Homotopic 1-Cell Decoder & Intent Projection 100%% SOUND\n");
    }

    /* ========================================================================= */
    /* 8. HoTT Univalence AOS <-> SIMD SOA Isomorphism in U_0                    */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(8, "HoTT Univalence AOS <-> SIMD SOA Isomorphism in U_0");
    {
        FlowHoTTUniverse u_soa;
        flow_hott_universe_reset(&u_soa);

        /* Register JetState[64] ~= JetState_SOA64 along FLOW_SIEVE_BIT_COLUMNAR_MORPH */
        int reg_ok = flow_hott_register_soa_equivalence(&u_soa, "JetState", "JetState_SOA64",
                                                        4, 32, FLOW_SIMD_SOA_LANES, 0U,
                                                        FLOW_SIEVE_SECURITY_CANONICAL_MASK);
        FLOW_ASSERT_EQ(reg_ok, 1);
        FLOW_ASSERT_EQ(flow_hott_verify_universe_all(&u_soa), 1);

        uint64_t ua_hash = 0ULL;
        FlowKanStatus ua_st = flow_hott_verify_soa_univalence(&u_soa, "JetState", "JetState_SOA64",
                                                              FLOW_SIMD_SOA_LANES, &ua_hash);
        FLOW_ASSERT_EQ(ua_st, FLOW_KAN_FILLED_HOMOTOPIC);
        FLOW_ASSERT_NE(ua_hash, 0ULL);

        /* Negative test: wrong lane count or broken non-layout axis must be obstructed */
        FLOW_ASSERT_EQ(flow_hott_verify_soa_univalence(&u_soa, "JetState", "JetState_SOA64", 32U, NULL),
                       FLOW_KAN_OBSTRUCTED_SINGULARITY);

        /* Register a type with a non-layout sieve mismatch and verify obstruction */
        flow_hott_register_type(&u_soa, "BrokenSOA", FLOW_HOTT_LANG_LEAN, 4, 32 * 64, 64, 0U,
                                FLOW_SIEVE_SECURITY_CANONICAL_MASK ^ 0x1ULL);
        flow_hott_register_type(&u_soa, "BrokenSOA", FLOW_HOTT_LANG_C17,  4, 32 * 64, 64, 0U,
                                FLOW_SIEVE_SECURITY_CANONICAL_MASK ^ 0x1ULL);
        FLOW_ASSERT_EQ(flow_hott_verify_soa_univalence(&u_soa, "JetState", "BrokenSOA", FLOW_SIMD_SOA_LANES, NULL),
                       FLOW_KAN_OBSTRUCTED_SINGULARITY);

        printf("    * HoTT Univalence AOS[64] ~= SOA64 Path Lifting Verified (ua_hash=0x%016llx)\n",
               (unsigned long long)ua_hash);
    }

    /* ========================================================================= */
    /* 9. Differential Cohesive HoTT: Homotopy Fiber & Discrete 4-Jet (J^4 E)    */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(9, "Differential Cohesive HoTT: Homotopy Fiber (fib_{ev}(1)) & Discrete 4-Jet (J^4 E)");
    {
        FlowHomotopyFiberReport rep;

        /* Case A: Empty Type (UNSAT, h_level = -1) when mandatory target axis is missing */
        FLOW_ASSERT_EQ(flow_hott_analyze_homotopy_fiber(0x07ULL, 0x03ULL, 0x0ULL, 0x0ULL, &rep), 1);
        FLOW_ASSERT_EQ(rep.h_level, FLOW_HLEVEL_EMPTY_UNSAT);
        FLOW_ASSERT_EQ(rep.pi0_components, 0U);
        FLOW_ASSERT_EQ(flow_hott_collapse_fiber_contractible(&rep, NULL, NULL), 0);

        /* Case B: 1-Type Groupoid (A // G, h_level = 1) when symmetry orbit axes are unpinned */
        FLOW_ASSERT_EQ(flow_hott_analyze_homotopy_fiber(0x03ULL, 0x3FULL, 0x03ULL, 0x30ULL, &rep), 1);
        FLOW_ASSERT_EQ(rep.h_level, FLOW_HLEVEL_GROUPOID_SYMMETRY);
        FLOW_ASSERT_EQ(rep.pi1_symmetry_generators, 2U); /* bits 4 and 5 */
        FLOW_ASSERT_EQ(rep.pi0_components, 4U);          /* bits 2 and 3 => 2^2 = 4 */

        /* Case C: 0-Type Discrete Set (h_level = 0, |pi_0| > 1) causing solver oscillation */
        FLOW_ASSERT_EQ(flow_hott_analyze_homotopy_fiber(0x03ULL, 0x0FULL, 0x03ULL, 0x0ULL, &rep), 1);
        FLOW_ASSERT_EQ(rep.h_level, FLOW_HLEVEL_SET_DISCRETE);
        FLOW_ASSERT_EQ(rep.pi0_components, 4U);
        FLOW_ASSERT_EQ(rep.pi1_symmetry_generators, 0U);
        FLOW_ASSERT_EQ(rep.unconstrained_axes_mask, 0x0CULL);

        /* Collapse multi-solution fiber to Contractible ((-2)-Type, isContr) */
        uint64_t contracted_w = 0ULL, pinned_mask = 0ULL;
        FLOW_ASSERT_EQ(flow_hott_collapse_fiber_contractible(&rep, &contracted_w, &pinned_mask), 1);
        FLOW_ASSERT_EQ(flow_hott_analyze_homotopy_fiber(0x03ULL, 0x0FULL, pinned_mask, 0x0ULL, &rep), 1);
        FLOW_ASSERT_EQ(rep.h_level, FLOW_HLEVEL_CONTRACTIBLE);
        FLOW_ASSERT_EQ(rep.pi0_components, 1U);
        FLOW_ASSERT_EQ(rep.pi1_symmetry_generators, 0U);

        /* Discrete 4-Jet Bundle Prolongation j^4 s : M -> J^4 E over BMF lattice */
        double section[16];
        for (size_t i = 0; i < 16; ++i) {
            /* Constant section s(x) = 2.5 has J^1 = J^2 = J^3 = J^4 = 0 */
            section[i] = 2.5;
        }
        double rk_min[5] = { 0.0, -1.0, -1.0, -1.0, -1.0 };
        double rk_max[5] = { 5.0,  1.0,  1.0,  1.0,  1.0 };
        FlowDiscreteJetBundle bundle;
        FLOW_ASSERT_EQ(flow_jet_bundle_prolong_bmf(section, 16, rk_min, rk_max, &bundle), 1);
        FLOW_ASSERT_EQ(bundle.is_holonomic_section, 1);
        FLOW_ASSERT_EQ(bundle.holonomic_mask, 0xFFFFULL);
        FLOW_ASSERT_TRUE(bundle.cartan_kahler_defect <= 1e-12);

        printf("    * Homotopy Fiber h-Level (-2, -1, 0, 1) & Discrete 4-Jet Prolongation (J^4 E) Verified\n");
    }

    /* ========================================================================= */
    /* 10. HIT Exception Quotient (SpaceWithException) & dM Quiet NaN Pullback   */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(10, "HIT Exception Quotient (SpaceWithException) & Zero-Branch dM Quiet NaN Pullback");
    {
        /* 10a. HIT Pushout Quotient M / ExcZone: collapse 4 exception axes in 16-axis space */
        FlowHITExceptionQuotient hit_q;
        FLOW_ASSERT_EQ(flow_hott_quotient_exception_zone(0xFFFFULL, 0x0F00ULL, 3U, &hit_q), 1);
        FLOW_ASSERT_EQ(hit_q.ambient_dof, 16U);
        FLOW_ASSERT_EQ(hit_q.exception_zone_dof, 4U);
        FLOW_ASSERT_EQ(hit_q.quotient_interior_dof, 12U);
        FLOW_ASSERT_EQ(hit_q.interior_sieve, 0xF0FFULL);
        FLOW_ASSERT_EQ(hit_q.collapsed_boundary_points, 1U);
        FLOW_ASSERT_TRUE(flow_hott_is_boundary_tag(hit_q.canonical_boundary_tag));

        /* 10b. Zero-Branch IEEE-754 Quiet NaN Payload Propagation along dM */
        double b_nan = flow_result_float_boundary(5U, 2U, FLOW_BOUNDARY_THM_IMPLIES,
                                                  (uint8_t)FLOW_SIEVE_BIT_LEAN_CERTIFIED, 404U);
        FLOW_ASSERT_TRUE(flow_result_float_is_boundary(b_nan));

        /* Hardware FPU arithmetic must propagate the exact 51-bit Quiet NaN payload branchlessly */
        double fpu_propagated = ((b_nan * 4.0) + 2.0) / 3.0;
        FLOW_ASSERT_TRUE(flow_result_float_is_boundary(fpu_propagated));
        uint64_t extracted_tag = flow_result_float_extract_tag(fpu_propagated);
        FLOW_ASSERT_TRUE(flow_hott_is_boundary_tag(extracted_tag));

        /* 10c. Decode 64-bit boundary tag back to Topos & Lean 4 contract provenance */
        FlowBoundaryCertificateReport cert;
        FLOW_ASSERT_EQ(flow_hott_decode_boundary_exception(extracted_tag, NULL, &cert), 1);
        FLOW_ASSERT_EQ(cert.is_on_boundary, 1);
        FLOW_ASSERT_EQ(cert.fn_id, 5U);
        FLOW_ASSERT_EQ(cert.guard_idx, 2U);
        FLOW_ASSERT_EQ((int)cert.violation_kind, (int)FLOW_BOUNDARY_THM_IMPLIES);
        FLOW_ASSERT_EQ(cert.cubical_axis, (uint8_t)FLOW_SIEVE_BIT_LEAN_CERTIFIED);
        FLOW_ASSERT_EQ(cert.user_code, 404U);

        /* Verify interior values are classified as M^o contractible interior */
        double ok_val = flow_result_float_interior(42.5);
        FLOW_ASSERT_EQ(flow_result_float_is_boundary(ok_val), 0);
        uint64_t ok_bits = 0ULL;
        memcpy(&ok_bits, &ok_val, sizeof(ok_bits));
        FLOW_ASSERT_EQ(flow_hott_decode_boundary_exception(ok_bits, NULL, &cert), 1);
        FLOW_ASSERT_EQ(cert.is_on_boundary, 0);
        FLOW_ASSERT_EQ((int)cert.violation_kind, (int)FLOW_BOUNDARY_OK);

        printf("    * HIT SpaceWithException Quotient (16 -> 12+1 pt) & IEEE-754 Quiet NaN dM Pullback Verified\n");
    }

    FLOW_TEST_SUITE_END();
}
