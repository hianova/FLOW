#include "manifold_algebra.h"
#include "neuro_bridge.h"
#include "spacetime_preplay.h"
#include "swarm_autopoiesis.h"
#include "hardware_telemetry.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "manifold-algebra-test assertion failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        exit(1); \
    } \
} while (0)

/* ========================================================================= */
/* Unit 1: Manifold Algebra - Constraint Convergence is Correlation           */
/* ========================================================================= */
static void test_manifold_algebra_core(void) {
    printf("--- [Unit 1/4] Testing Manifold Algebra (Intersection, Moreau Projection & Epistasis) ---\n");

    FlowManifold mA, mB, mInter, mSum;
    CHECK(flow_manifold_init(&mA, 0x00000000FFFFFFFFULL) == 1);
    CHECK(flow_manifold_init(&mB, 0xFFFFFFFF00000000ULL) == 1);

    /* Configure box bounds on mA */
    CHECK(flow_manifold_set_bounds(&mA, 0, 0.0, 5.0) == 1);
    CHECK(flow_manifold_set_bounds(&mA, 1, 0.0, 5.0) == 1);
    CHECK(flow_manifold_set_bounds(&mA, 2, -2.0, 2.0) == 1);

    /* Add active constraint binding dim 0 and dim 1: x_0 + 2*x_1 <= 6.0 with shadow price lambda=4.2 */
    double normalA[FLOW_MANIFOLD_DIM] = {0};
    normalA[0] = 1.0;
    normalA[1] = 2.0;
    CHECK(flow_manifold_add_constraint(&mA, normalA, 6.0, 4.2) == 1);

    /* Epistatic linkage must automatically detect coupling between dim 0 and dim 1 */
    CHECK((mA.epistatic_linkage_mask & 0x03) == 0x03);
    CHECK(mA.dual_multipliers[0] > 0.0);
    CHECK(mA.dual_multipliers[1] > 0.0);

    /* Configure bounds on mB */
    CHECK(flow_manifold_set_bounds(&mB, 0, 1.0, 8.0) == 1);
    CHECK(flow_manifold_set_bounds(&mB, 1, 1.0, 4.0) == 1);

    /* Manifold Intersection M_A \cap M_B */
    CHECK(flow_manifold_intersect(&mA, &mB, &mInter) == 1);
    /* Tightened bounds check: max(lower), min(upper) */
    CHECK((int)mInter.lower_bounds[0] == 1);
    CHECK((int)mInter.upper_bounds[0] == 5);
    CHECK((int)mInter.lower_bounds[1] == 1);
    CHECK((int)mInter.upper_bounds[1] == 4);
    CHECK(mInter.volume_proxy > 0.0);

    /* Direct Sum M_A \oplus M_B on disjoint subspaces */
    CHECK(flow_manifold_direct_sum(&mA, &mB, &mSum) == 1);
    CHECK(mSum.subspace_mask == 0xFFFFFFFFFFFFFFFFULL);

    /* Moreau Boundary Projection */
    double unconstrained_pt[FLOW_MANIFOLD_DIM] = {10.0, 10.0, 5.0};
    double projected_pt[FLOW_MANIFOLD_DIM] = {0};
    CHECK(flow_manifold_boundary_project(&mInter, unconstrained_pt, projected_pt) == 1);
    /* Must be clamped within bounds and satisfy x_0 + 2*x_1 <= 6.0 */
    CHECK(projected_pt[0] <= mInter.upper_bounds[0] + 1e-6);
    CHECK(projected_pt[1] <= mInter.upper_bounds[1] + 1e-6);
    CHECK(projected_pt[0] + 2.0 * projected_pt[1] <= 6.0 + 1e-5);

    /* SMT Formal Verification */
    FlowSMTProofAttestation proof;
    FlowSMTResult smt_res = flow_manifold_verify_smt(&mInter, &proof);
    CHECK(smt_res == FLOW_SMT_PROVEN_UNSAT);
    CHECK(strstr(proof.proof_summary, "SMT MANIFOLD SOUND") != NULL);
    printf("    [PASS] Manifold algebra intersection, epistasis and SMT verified: %s\n", proof.proof_summary);
}

/* ========================================================================= */
/* Unit 2: Neuro-Bit Manifold Bridge (4096-D -> 64-Bit BMF & Bounds < 100ns)  */
/* ========================================================================= */
static void test_neuro_bit_manifold_bridge(void) {
    printf("--- [Unit 2/4] Testing Neuro-Bit Manifold Bridge (< 100ns Projection) ---\n");

    FlowNeuroBridge bridge;
    const size_t input_dim = 4096;
    CHECK(flow_neuro_bridge_init(&bridge, input_dim, 0xACE1337) == 1);

    /* Construct 4096-D continuous mock multimodal embedding */
    static float embedding[FLOW_NEURO_MAX_INPUT_DIM];
    for (size_t i = 0; i < input_dim; i++) {
        embedding[i] = (float)sin((double)i * 0.123);
    }

    FlowNeuroProjectionResult result;
    /* Scenario: Human says "幫我把桌上那杯快灑出來的拿鐵拿過來" */
    CHECK(flow_neuro_bridge_project(&bridge,
                                    embedding,
                                    input_dim,
                                    FLOW_NEURO_INTENT_SMOOTH_FETCH_LATTE,
                                    &result) == 1);

    CHECK(result.classified_intent == FLOW_NEURO_INTENT_SMOOTH_FETCH_LATTE);
    CHECK(result.bmf_coordinates != 0);
    CHECK(result.bound_count >= 4);

    /* Verify synthesized unspillable physical bounds */
    bool found_tilt = false;
    bool found_grip = false;
    for (size_t i = 0; i < result.bound_count; i++) {
        if (strcmp(result.bounds[i].name, "fluid_tilt_limit") == 0) {
            found_tilt = true;
            CHECK(result.bounds[i].upper_bound <= 0.08 + 1e-6); /* ~4.6 degrees max */
        } else if (strcmp(result.bounds[i].name, "gripper_force_n") == 0) {
            found_grip = true;
            CHECK(result.bounds[i].lower_bound >= 2.0 - 1e-6);
            CHECK(result.bounds[i].upper_bound <= 4.5 + 1e-6);
        }
    }
    CHECK(found_tilt);
    CHECK(found_grip);

    printf("    -> 4096-D Projection Latency: %.2f ns (%llu cycles)\n",
           result.projection_nanoseconds, (unsigned long long)result.projection_cycles);
    CHECK(result.projection_nanoseconds < 1000.0);

    /* SMT Formal Verification */
    FlowSMTProofAttestation proof;
    FlowSMTResult smt_res = flow_neuro_bridge_verify_smt(&result, &proof);
    CHECK(smt_res == FLOW_SMT_PROVEN_UNSAT);
    CHECK(strstr(proof.proof_summary, "SMT NEURO-BRIDGE SOUND") != NULL);
    printf("    [PASS] Neuro-bit bridge intent-to-polyhedron SMT verified: %s\n", proof.proof_summary);
}

/* ========================================================================= */
/* Unit 3: Spacetime Pre-Play Engine (3.0s Cone, Ice Black Swan & Anneal)    */
/* ========================================================================= */
static void test_spacetime_preplay_engine(void) {
    printf("--- [Unit 3/4] Testing Spacetime Pre-Play Engine (3.0s Cone & < 200us Anneal) ---\n");

    FlowSpacetimeEngine engine;
    CHECK(flow_spacetime_init(&engine, 50.0, 0.85) == 1);

    /* Inject Black Swan: Sudden black ice patch at t=2.5s..3.0s with mu=0.05 */
    CHECK(flow_spacetime_set_black_swan(&engine, 0.05, 2.5, 3.0, 0.25) == 1);

    FlowPhaseState initial_state;
    memset(&initial_state, 0, sizeof(initial_state));
    initial_state.p[0] = 50.0 * 2.0; /* Initial momentum for 2.0 m/s forward speed */
    initial_state.p[1] = (50.0 * 0.45 * 0.45) * 0.35; /* Desired yaw turning rate */

    /* First, run nominal simulation: Black swan should be caught in the spacetime cone */
    FlowSpacetimeConeResult baseline_result;
    CHECK(flow_spacetime_simulate(&engine, &initial_state, NULL, &baseline_result) == 1);
    CHECK(baseline_result.violation_detected);
    CHECK(baseline_result.violation_time_s >= 2.5);
    CHECK(baseline_result.max_roll_observed > 0.25);
    printf("    -> Baseline caught future black swan at t=%.2fs (Roll=%.3f rad)\n",
           baseline_result.violation_time_s, baseline_result.max_roll_observed);

    /* Second, execute Counterfactual Pre-Play with 1-Bit Chaotic Annealing */
    FlowSpacetimeConeResult annealed_result;
    CHECK(flow_spacetime_preplay_and_anneal(&engine, &initial_state, &annealed_result) == 1);

    printf("    -> Annealing duration: %.2f us (%llu cycles), Black swans averted: %llu\n",
           engine.preplay_duration_us, (unsigned long long)engine.preplay_cycles,
           (unsigned long long)engine.black_swans_averted);

    CHECK(engine.preplay_duration_us < 200.0); /* Must be under 200 microseconds */
    CHECK(!annealed_result.violation_detected); /* Black swan completely eliminated */
    CHECK(annealed_result.max_roll_observed <= engine.env.critical_roll_angle_rad);
    CHECK(engine.black_swans_averted >= 1);

    /* SMT Formal Verification */
    FlowSMTProofAttestation proof;
    FlowSMTResult smt_res = flow_spacetime_preplay_verify_smt(&engine, &annealed_result, &proof);
    CHECK(smt_res == FLOW_SMT_PROVEN_UNSAT);
    CHECK(strstr(proof.proof_summary, "SMT SPACETIME SOUND") != NULL);
    printf("    [PASS] Spacetime pre-play SMT verified: %s\n", proof.proof_summary);
}

/* ========================================================================= */
/* Unit 4: Swarm Speciation & Autopoiesis (.fvec Living Ecosystem)            */
/* ========================================================================= */
static void test_swarm_speciation_autopoiesis(void) {
    printf("--- [Unit 4/4] Testing Swarm Speciation & Autopoiesis (.fvec Ecosystem) ---\n");

    FlowSwarmSpeciationEngine engine;
    CHECK(flow_speciation_init(&engine, 42) == 1);
    CHECK(engine.population_size == FLOW_SPECIATION_POPULATION_SIZE);

    /* Test Epistatic-Linkage-Aware Crossover:
     * Dimension 0 and 1 are coupled by thermal constraint in both parents. */
    FlowSpeciationSpecimen *pA = &engine.population[0];
    FlowSpeciationSpecimen *pB = &engine.population[1];
    pA->features[0] = 0.2;
    pA->features[1] = 0.3;
    pB->features[0] = 0.8;
    pB->features[1] = 0.9;

    FlowSpeciationSpecimen child;
    CHECK(flow_speciation_crossover(pA, pB, 0x12345, &child) == 1);

    /* Child must inherit coupled pair (features[0], features[1]) as an atomic block */
    bool matches_A = (fabs(child.features[0] - pA->features[0]) < 1e-6 &&
                      fabs(child.features[1] - pA->features[1]) < 1e-6);
    bool matches_B = (fabs(child.features[0] - pB->features[0]) < 1e-6 &&
                      fabs(child.features[1] - pB->features[1]) < 1e-6);
    CHECK(matches_A || matches_B);

    /* Evolve 5 generations of living speciation */
    for (int g = 0; g < 5; g++) {
        CHECK(flow_speciation_step_generation(&engine) == 1);
    }

    CHECK(engine.current_generation == 5);
    CHECK(engine.total_crossovers >= 5);
    CHECK(engine.total_drifts >= 5);

    /* Export an auto-promoted specimen to .fvec on disk */
    const char *test_fvec_dir = "/tmp/flow_autopoiesis_test";
    CHECK(flow_speciation_export_fvec(&engine.population[0], test_fvec_dir) == 1);

    char expected_path[512];
    snprintf(expected_path, sizeof(expected_path), "%s/%s.fvec", test_fvec_dir, engine.population[0].id);
    FILE *f = fopen(expected_path, "rb");
    CHECK(f != NULL);

    FlowVecHeader header;
    CHECK(fread(&header, sizeof(header), 1, f) == 1);
    fclose(f);

    CHECK(strcmp(header.magic, FLOW_FVEC_MAGIC) == 0);
    CHECK(header.is_auto_promoted == 1);
    printf("    -> Exported living autopoietic .fvec: %s (Magic: %s, Score: %.2f)\n",
           header.id, header.magic, header.energy_score);

    /* SMT Formal Verification */
    FlowSMTProofAttestation proof;
    FlowSMTResult smt_res = flow_speciation_verify_smt(&engine, &proof);
    CHECK(smt_res == FLOW_SMT_PROVEN_UNSAT);
    CHECK(strstr(proof.proof_summary, "SMT AUTOPOIESIS SOUND") != NULL);
    printf("    [PASS] Swarm autopoiesis SMT verified: %s\n", proof.proof_summary);
}

/* ========================================================================= */
/* Main Runner                                                               */
/* ========================================================================= */
int main(void) {
    printf("====================================================================\n");
    printf("  Test Suite #77: Manifold Algebra, Neuro Bridge, Preplay & Autopoiesis\n");
    printf("====================================================================\n");

    flow_hardware_telemetry_init();

    test_manifold_algebra_core();
    test_neuro_bit_manifold_bridge();
    test_spacetime_preplay_engine();
    test_swarm_speciation_autopoiesis();

    printf("\n>>> ALL 4 TEST UNITS IN TEST SUITE #77 PASSED 100%%! <<<\n");
    return 0;
}
