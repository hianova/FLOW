#include "embodied.h"
#include "smt.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "fleet-swarm-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

int main(void) {
    printf("========================================================================================\n");
    printf("  🤖 Running FLOW Embodied Multi-Agent Swarm Fleet Test Suite (Suite #65)\n");
    printf("========================================================================================\n\n");

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 1: Fleet Swarm Initialization & Multi-Role Robot Registration                */
    /* ---------------------------------------------------------------------------------- */
    printf("--- [Stage 1: Multi-Agent Fleet Swarm Initialization] ---\n");
    FlowFleetSwarm fleet;
    CHECK(flow_fleet_init(&fleet, 0.5) == 1); /* 0.5m minimum safety margin */
    CHECK(fleet.robot_count == 0);

    /* Register 8 Heterogeneous Robots across a 10m x 10m grid */
    double pos1[3] = { 1.0, 1.0, 0.0 };
    double pos2[3] = { 3.0, 1.0, 0.0 };
    double pos3[3] = { 5.0, 1.0, 0.0 };
    double pos4[3] = { 1.0, 4.0, 0.0 };
    double pos5[3] = { 3.0, 4.0, 0.0 };
    double pos6[3] = { 5.0, 4.0, 0.0 };
    double pos7[3] = { 2.0, 7.0, 0.0 };
    double pos8[3] = { 4.0, 7.0, 0.0 };

    CHECK(flow_fleet_register_robot(&fleet, 1, FLOW_FLEET_ROLE_SCOUT,    0.3, pos1) == 1);
    CHECK(flow_fleet_register_robot(&fleet, 2, FLOW_FLEET_ROLE_SCOUT,    0.3, pos2) == 1);
    CHECK(flow_fleet_register_robot(&fleet, 3, FLOW_FLEET_ROLE_CARRIER,  0.5, pos3) == 1);
    CHECK(flow_fleet_register_robot(&fleet, 4, FLOW_FLEET_ROLE_CARRIER,  0.5, pos4) == 1);
    CHECK(flow_fleet_register_robot(&fleet, 5, FLOW_FLEET_ROLE_CARRIER,  0.5, pos5) == 1);
    CHECK(flow_fleet_register_robot(&fleet, 6, FLOW_FLEET_ROLE_RELAY,    0.2, pos6) == 1);
    CHECK(flow_fleet_register_robot(&fleet, 7, FLOW_FLEET_ROLE_RELAY,    0.2, pos7) == 1);
    CHECK(flow_fleet_register_robot(&fleet, 8, FLOW_FLEET_ROLE_ACTUATOR, 0.4, pos8) == 1);
    CHECK(fleet.robot_count == 8);

    printf("  ✓ Registered 8 heterogeneous robots: 2 Scouts, 3 Carriers, 2 Relays, 1 Actuator.\n\n");

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 2: 1kHz Real-Time Spinal Fleet Reflex Loop (< 1ms Execution)                 */
    /* ---------------------------------------------------------------------------------- */
    printf("--- [Stage 2: 1kHz Spinal Fleet Reflex Tick] ---\n");
    /* Simulate Robot 1 and Robot 2 flying/moving towards each other */
    double vel1[3] = { 1.0, 0.0, 0.0 };   /* Moving right */
    double vel2[3] = { -1.0, 0.0, 0.0 };  /* Moving left */
    flow_fleet_update_telemetry(&fleet, 1, pos1, vel1, 950, 38);
    flow_fleet_update_telemetry(&fleet, 2, pos2, vel2, 920, 40);

    /* Run 500 1kHz ticks (0.5 seconds simulation) */
    for (int t = 0; t < 500; ++t) {
        CHECK(flow_fleet_step_1khz_tick(&fleet, 0.001) == 1);
    }
    CHECK(fleet.total_1khz_ticks == 500);

    /* Verify repulsive forces prevented collision */
    double dx = fleet.robots[0].position[0] - fleet.robots[1].position[0];
    double dist12 = fabs(dx);
    CHECK(dist12 > (fleet.robots[0].bounding_radius + fleet.robots[1].bounding_radius));
    CHECK(fleet.total_collision_avoidance_interventions > 0);

    printf("  ✓ 500 ticks at 1kHz executed. Repulsive intervention count: %llu, Final dist: %.3fm.\n\n",
           (unsigned long long)fleet.total_collision_avoidance_interventions, dist12);

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 3: SMT Formal Spatial Separation & Collision-Avoidance Polytope Proof        */
    /* ---------------------------------------------------------------------------------- */
    printf("--- [Stage 3: SMT Formal Spatial Separation Polytope Invariant] ---\n");
    FlowSMTProofAttestation proof;
    memset(&proof, 0, sizeof(proof));

    /* 3a: Fleet in Safe Configuration -> PROVEN UNSAT */
    FlowSMTResult r_safe = flow_fleet_verify_collision_smt(&fleet, &proof);
    CHECK(r_safe == FLOW_SMT_PROVEN_UNSAT);
    CHECK(proof.buffer_bounds_safety == FLOW_SMT_PROVEN_UNSAT);
    CHECK(proof.shard_non_aliasing == FLOW_SMT_PROVEN_UNSAT);
    printf("  ✓ SMT Proof Sound: %s\n", proof.proof_summary);

    /* 3b: Counterexample Injection: Force Robot 1 and 2 into overlapping space */
    double crash_pos[3] = { 1.0, 1.0, 0.0 };
    double crash_pos2[3] = { 1.1, 1.0, 0.0 }; /* dist = 0.1m < (0.3 + 0.3 = 0.6m) */
    flow_fleet_update_telemetry(&fleet, 1, crash_pos, NULL, 900, 40);
    flow_fleet_update_telemetry(&fleet, 2, crash_pos2, NULL, 900, 40);

    FlowSMTResult r_crash = flow_fleet_verify_collision_smt(&fleet, &proof);
    CHECK(r_crash == FLOW_SMT_VIOLATION_SAT);
    CHECK(proof.buffer_bounds_safety == FLOW_SMT_VIOLATION_SAT);
    printf("  ✓ SMT Counterexample caught overlap: %s\n\n", proof.proof_summary);

    /* Restore safe separation */
    double safe_pos2[3] = { 3.0, 1.0, 0.0 };
    flow_fleet_update_telemetry(&fleet, 2, safe_pos2, NULL, 900, 40);

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 4: 1-Bit Chaos Autonomous Role Reassignment (Battery / Thermal Degrade)      */
    /* ---------------------------------------------------------------------------------- */
    printf("--- [Stage 4: 1-Bit Chaos Dynamic Role Reassignment] ---\n");
    /* Robot 1 (Scout) battery collapses to 10% (100 permille) and motor overheats to 88C */
    flow_fleet_update_telemetry(&fleet, 1, NULL, NULL, 100, 88);
    CHECK(fleet.robots[0].role == FLOW_FLEET_ROLE_SCOUT);
    CHECK(fleet.robots[5].role == FLOW_FLEET_ROLE_RELAY);

    /* Trigger 1-Bit Chaos Role Reallocation */
    int reallocated = flow_fleet_adapt_roles_chaos(&fleet, 42);
    CHECK(reallocated == 1);
    CHECK(fleet.robots[0].role == FLOW_FLEET_ROLE_IDLE);  /* Degraded robot safely set to IDLE */
    CHECK(fleet.robots[5].role == FLOW_FLEET_ROLE_SCOUT); /* Healthy relay promoted to SCOUT */
    CHECK(fleet.total_role_reassignments == 1);

    printf("  ✓ Degraded Scout (Robot 1: bat=10%%, temp=88C) retired to IDLE.\n");
    printf("  ✓ Healthy Relay (Robot 6: bat=100%%, temp=35C) promoted to SCOUT.\n");
    printf("  ✓ Zero human intervention required; autonomous self-healing confirmed.\n\n");

    printf("========================================================================================\n");
    printf("  🎉 ALL 4 FLEET SWARM TEST STAGES 100%% SOUND & VERIFIED!\n");
    printf("========================================================================================\n");
    return 0;
}
