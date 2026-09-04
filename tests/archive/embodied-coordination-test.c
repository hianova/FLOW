#include "embodied.h"
#include "bitmanifold.h"
#include "smt.h"
#include "flow_test_kit.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    FLOW_TEST_SUITE_BEGIN("Embodied Perceptual Coordination & Mask Superposition (Suite #71)");

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 1: 64-Bit Coordinate Subspace Slicing & Invariant Roundtrip                  */
    /* ---------------------------------------------------------------------------------- */
    FLOW_STAGE_BEGIN(1, "64-Bit Embodied Coordinate Subspace Slicing & Bit-Exact Roundtrip");
    {
        FlowEmbodiedCoordinate coord = {
            .gait_mode = FLOW_GAIT_FLAT_WALK,
            .torque_gain_scale = 192,         /* 75% torque rating */
            .sensor_confidence = 58,          /* High confidence (58/63) */
            .thermal_throttle_level = 12,     /* Mild thermal management */
            .smith_delay_steps = 4,           /* 4ms EtherCAT bus latency */
            .fleet_clearance_mm = 650,        /* 0.65m collision margin */
            .survival_flags = 0
        };

        uint64_t encoded_genome = 0;
        FLOW_ASSERT_TRUE(flow_embodied_encode_genome(&coord, &encoded_genome));
        FLOW_ASSERT_NE(encoded_genome, 0);

        FlowEmbodiedCoordinate decoded;
        memset(&decoded, 0, sizeof(decoded));
        FLOW_ASSERT_TRUE(flow_embodied_decode_genome(encoded_genome, &decoded));

        FLOW_ASSERT_EQ(decoded.gait_mode, FLOW_GAIT_FLAT_WALK);
        FLOW_ASSERT_EQ(decoded.torque_gain_scale, 192);
        FLOW_ASSERT_EQ(decoded.sensor_confidence, 58);
        FLOW_ASSERT_EQ(decoded.thermal_throttle_level, 12);
        FLOW_ASSERT_EQ(decoded.smith_delay_steps, 4);
        FLOW_ASSERT_EQ(decoded.fleet_clearance_mm, 650);
        FLOW_ASSERT_EQ(decoded.survival_flags, 0);

        printf("  ✓ Subspace Bitfield Packing: 0x%016llx -> Bit-Exact Orthogonal Recovery verified.\n\n",
               (unsigned long long)encoded_genome);
    }

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 2: Multi-Sensory Mask Hypergeometric Superposition (Nominal Telemetry)       */
    /* ---------------------------------------------------------------------------------- */
    FLOW_STAGE_BEGIN(2, "Multi-Sensory Mask Hypergeometric Superposition");
    {
        FlowPhysicsEngine phys;
        FLOW_ASSERT_TRUE(flow_physics_init(&phys, 6, 25.0));

        FlowSensorFusion fusion;
        FLOW_ASSERT_TRUE(flow_sensor_fusion_init(&fusion));

        FlowThermalEnergyGovernor gov;
        FLOW_ASSERT_TRUE(flow_energy_governor_init(&gov, 25.0, 5.0));
        gov.edge_chip_temp_celsius = 48.5;
        gov.current_battery_percent = 88.0;

        FlowFleetSwarm fleet;
        FLOW_ASSERT_TRUE(flow_fleet_init(&fleet, 0.5));
        double posA[3] = { 0.0, 0.0, 0.0 };
        double posB[3] = { 2.5, 0.0, 0.0 }; /* 2.5m separation > 0.5m */
        flow_fleet_register_robot(&fleet, 1, FLOW_FLEET_ROLE_CARRIER, 0.4, posA);
        flow_fleet_register_robot(&fleet, 2, FLOW_FLEET_ROLE_SCOUT,   0.2, posB);

        FlowEmbodiedCanvas canvas;
        FLOW_ASSERT_TRUE(flow_embodied_superpose_safety_canvas(&phys, &fusion, &gov, &fleet, &canvas));

        FLOW_ASSERT_EQ(canvas.is_double_bind, 0);
        FLOW_ASSERT_NE(canvas.superposed_mask, 0);
        FLOW_ASSERT_EQ(canvas.superposed_mask, canvas.zmp_mask & canvas.kalman_mask & canvas.thermal_mask & canvas.fleet_mask);

        printf("  ✓ Superposition verified: Mask_ZMP & Mask_Kalman & Mask_Thermal & Mask_Fleet -> Sound feasible space.\n\n");
    }

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 3: Token Ring Closed-Loop Discrete Attention & 1-Bit Chaotic Transition      */
    /* ---------------------------------------------------------------------------------- */
    FLOW_STAGE_BEGIN(3, "Token Ring Closed-Loop Discrete Attention & 1-Bit Transition");
    {
        FlowPhysicsEngine phys;
        flow_physics_init(&phys, 6, 25.0);
        FlowSensorFusion fusion;
        flow_sensor_fusion_init(&fusion);
        FlowThermalEnergyGovernor gov;
        flow_energy_governor_init(&gov, 25.0, 5.0);
        gov.edge_chip_temp_celsius = 50.0;
        gov.current_battery_percent = 80.0;

        FlowEmbodiedCanvas canvas;
        flow_embodied_superpose_safety_canvas(&phys, &fusion, &gov, NULL, &canvas);

        uint64_t current_genome = 0; /* Initial zero state */
        uint64_t prng_state = 0x9876543210ABCDEFULL;
        uint32_t mutated_bit = 999;

        /* Step Token Ring Attention loop */
        FLOW_ASSERT_TRUE(flow_embodied_step_token_ring(&canvas, &current_genome, &prng_state, &mutated_bit));
        FLOW_ASSERT_NE(current_genome, 0);
        FLOW_ASSERT_TRUE(mutated_bit < 64);
        /* Mutated bit must strictly obey the superposed hard mask */
        FLOW_ASSERT_TRUE((canvas.superposed_mask & (1ULL << mutated_bit)) != 0);

        printf("  ✓ Token Ring discrete attention step flipped legal bit #%u: next genome = 0x%016llx.\n\n",
               mutated_bit, (unsigned long long)current_genome);
    }

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 4: Multi-Hazard Double-Bind Conflict & O(1) SMT Static Survival Fallback     */
    /* ---------------------------------------------------------------------------------- */
    FLOW_STAGE_BEGIN(4, "Double-Bind Conflict & O(1) SMT Static Survival Brace Mode");
    {
        /* Simulate catastrophic multi-hazard state:
         * 1. ZMP tipping: CoM position outside support polygon
         * 2. Extreme sensor vibration: confidence zeroed
         * 3. Thermal limit breached: 96°C > 85°C limit and low battery (8%)
         * 4. Swarm collision impending: inter-robot dist 0.1m < 0.5m
         */
        FlowPhysicsEngine phys;
        flow_physics_init(&phys, 6, 25.0);
        phys.current_state.zmp_position[0] = 0.85; /* Severe tip-over */
        phys.violations_prevented_total = 5;

        FlowSensorFusion fusion;
        flow_sensor_fusion_init(&fusion);
        fusion.sensor_confidence = 0.05;
        fusion.raw_vibration_noise_level = 0.35;

        FlowThermalEnergyGovernor gov;
        flow_energy_governor_init(&gov, 25.0, 5.0);
        gov.edge_chip_temp_celsius = 96.0;
        gov.current_battery_percent = 8.0;

        FlowFleetSwarm fleet;
        flow_fleet_init(&fleet, 0.5);
        double posNear1[3] = { 1.0, 1.0, 0.0 };
        double posNear2[3] = { 1.1, 1.0, 0.0 }; /* 0.1m separation! */
        flow_fleet_register_robot(&fleet, 1, FLOW_FLEET_ROLE_CARRIER, 0.3, posNear1);
        flow_fleet_register_robot(&fleet, 2, FLOW_FLEET_ROLE_SCOUT,   0.3, posNear2);
        fleet.total_collision_avoidance_interventions = 12;

        FlowEmbodiedCanvas canvas;
        FLOW_ASSERT_TRUE(flow_embodied_superpose_safety_canvas(&phys, &fusion, &gov, &fleet, &canvas));

        /* Must detect Double-Bind situation */
        FLOW_ASSERT_EQ(canvas.is_double_bind, 1);

        /* Fallback must activate FLOW_GAIT_EMERGENCY_BRACE with survival flag */
        uint64_t gait = FLOW_GENOME_GET(canvas.superposed_mask, FLOW_EMBODIED_OFF_GAIT, FLOW_EMBODIED_LEN_GAIT);
        uint64_t survival = FLOW_GENOME_GET(canvas.superposed_mask, FLOW_EMBODIED_OFF_SURVIVAL, FLOW_EMBODIED_LEN_SURVIVAL);
        FLOW_ASSERT_EQ(gait, FLOW_GAIT_EMERGENCY_BRACE);
        FLOW_ASSERT_EQ(survival, 1);

        /* Verify Token Ring executes safe brace with zero crash */
        uint64_t active_genome = 0;
        uint64_t prng = 0x12345678ULL;
        uint32_t mut = 0;
        FLOW_ASSERT_TRUE(flow_embodied_step_token_ring(&canvas, &active_genome, &prng, &mut));

        FlowEmbodiedCoordinate safe_coord;
        flow_embodied_decode_genome(active_genome, &safe_coord);
        FLOW_ASSERT_EQ(safe_coord.gait_mode, FLOW_GAIT_EMERGENCY_BRACE);

        printf("  ✓ Double-Bind detected: SMT in O(1) routed to Static Survival Brace (Gait=EMERGENCY_BRACE, Survival=1).\n\n");
    }

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 5: SMT Multi-Theorem Formal Safety Polytope Proof                            */
    /* ---------------------------------------------------------------------------------- */
    FLOW_STAGE_BEGIN(5, "SMT Formal Multi-Theorem Safety Polytope Verification");
    {
        FlowSMTProofAttestation proof;
        memset(&proof, 0, sizeof(proof));

        /* 5a: Sound condition */
        FlowPhysicsEngine phys;
        flow_physics_init(&phys, 6, 25.0);
        FlowThermalEnergyGovernor gov;
        flow_energy_governor_init(&gov, 25.0, 5.0);
        gov.edge_chip_temp_celsius = 52.0;

        FlowFleetSwarm fleet;
        flow_fleet_init(&fleet, 0.5);
        double p1[3] = { 0.0, 0.0, 0.0 };
        double p2[3] = { 4.0, 0.0, 0.0 };
        flow_fleet_register_robot(&fleet, 1, FLOW_FLEET_ROLE_SCOUT, 0.3, p1);
        flow_fleet_register_robot(&fleet, 2, FLOW_FLEET_ROLE_RELAY, 0.3, p2);

        FlowSMTResult r_sound = flow_embodied_verify_smt(&phys.current_state, &gov, &fleet, &proof);
        FLOW_ASSERT_EQ(r_sound, FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_SMT_SOUND(proof);
        FLOW_ASSERT_STR_CONTAINS(proof.proof_summary, "SMT EMBODIED SOUND");

        /* 5b: Violation condition (Overheat 95°C > 85°C) */
        gov.edge_chip_temp_celsius = 95.0;
        FlowSMTProofAttestation proof_viol;
        memset(&proof_viol, 0, sizeof(proof_viol));
        FlowSMTResult r_viol = flow_embodied_verify_smt(&phys.current_state, &gov, &fleet, &proof_viol);
        FLOW_ASSERT_EQ(r_viol, FLOW_SMT_VIOLATION_SAT);
        FLOW_ASSERT_SMT_VIOLATION(r_viol, proof_viol);
        FLOW_ASSERT_STR_CONTAINS(proof_viol.proof_summary, "thermal ceiling");

        printf("  ✓ SMT formal proofs sound: Proven UNSAT in nominal state; SAT counterexample caught thermal violation.\n\n");
    }

    FLOW_TEST_SUITE_END();
    return 0;
}
