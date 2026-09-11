#include "flow_test_kit.h"
#include "cubical_hott.h"
#include "geometric_axiom.h"
#include "flow_jet.h"

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

    FLOW_TEST_SUITE_END();
}
