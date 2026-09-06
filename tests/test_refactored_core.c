#include "flow_test_kit.h"
#include "flow_jet.h"
#include "polyhedral.h"
#include "swarm.h"
#include "hardware_telemetry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdalign.h>
#include <math.h>
#include <time.h>

int main(void) {
    FLOW_TEST_SUITE_BEGIN("FLOW Core Refactoring: Thermal Contact-Jet, Quadratic Polyhedral, and 64-Node Ring Mesh");

    flow_hardware_telemetry_init();

    /* ========================================================================= */
    /* 1. PILLAR P0: Thermal Contact-Jet & Anti-Throttling Real-Time Guarantees  */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(1, "P0: Thermal Contact-Jet & Thermodynamic Dissipation Control");
    {
        FlowJet jet;
        int ok = flow_jet_init(&jet, "jet_contact_thermal", "Thermal Contact Phase Space");
        FLOW_ASSERT_EQ(ok, 1);
        FLOW_ASSERT_FLOAT_EQ(jet.payload.s, 0.0, 1e-6);
        FLOW_ASSERT_FLOAT_EQ(jet.payload.thermal.temp_ambient_c, 50.0, 1e-3);
        FLOW_ASSERT_FLOAT_EQ(jet.payload.thermal.temp_throttle_c, 95.0, 1e-3);
        FLOW_ASSERT_FLOAT_EQ(jet.payload.thermal.temp_target_c, 85.0, 1e-3);
        FLOW_ASSERT_EQ((int)jet.payload.thermal.is_throttled, 0);

        /* Test thermal step under 8.0W load for 100ms */
        for (int i = 0; i < 100; ++i) {
            flow_jet_thermal_step(&jet, 8.0, 0.001);
        }
        FLOW_ASSERT_TRUE(jet.payload.thermal.temp_c > 45.0);
        FLOW_ASSERT_TRUE(jet.payload.s > 0.0);
        FLOW_ASSERT_TRUE(jet.payload.thermal.dtemp_dt > 0.0);

        /* Test horizon projection: predict temp 50ms into future */
        double projected_temp = flow_jet_thermal_predict_horizon(&jet, 0.050);
        FLOW_ASSERT_TRUE(projected_temp > jet.payload.thermal.temp_c);

        /* Test contact step combining symplectic Verlet and thermal dissipation */
        double initial_s = jet.payload.s;
        double initial_q0 = jet.payload.q[0];
        jet.payload.p[0] = 1.0;
        flow_jet_contact_step(&jet, 0.001, 6.0);
        FLOW_ASSERT_TRUE(jet.payload.q[0] != initial_q0);
        FLOW_ASSERT_TRUE(jet.payload.s > initial_s);

        /* Test hardware probe thermal coupling */
        FlowPhysicalProbe probe;
        flow_hardware_probe_start(&probe);
        for (volatile int k = 0; k < 100000; ++k);
        flow_hardware_probe_stop(&probe);
        flow_hardware_probe_update_thermal(&probe, &jet.payload.thermal, 0.001);
        FLOW_ASSERT_TRUE(jet.payload.thermal.active_power_w > 0.0);

        /* High-Ambient Chassis 10kHz WCET Loop Simulation (1,000 ticks) */
        const double T_amb = 50.0;
        const double T_throttle = 95.0;
        flow_jet_thermal_init_default(&jet.payload.thermal);
        jet.payload.thermal.temp_ambient_c = T_amb;
        jet.payload.thermal.temp_c = T_amb;

        size_t deadline_misses = 0;
        double dt_tick = 0.001; /* 1ms per step */

        for (int step = 0; step < 1000; ++step) {
            /* Compute proactive thermal prune factor with 50ms horizon */
            double prune_factor = flow_jet_thermal_prune_factor(&jet, 0.050);
            FLOW_ASSERT_TRUE(prune_factor >= 0.2 && prune_factor <= 1.0);

            double nominal_cycles = 90000.0;
            double effective_cycles = nominal_cycles * prune_factor;
            double active_power = 2.0 + 6.0 * prune_factor;

            flow_jet_thermal_step(&jet, active_power, dt_tick);

            /* Check WCET: at 3.0GHz, deadline is 100us */
            double tick_us = (effective_cycles / 3.0e9) * 1e6;
            if (tick_us > 100.0 || jet.payload.thermal.is_throttled) {
                deadline_misses++;
            }
        }

        FLOW_ASSERT_EQ(deadline_misses, 0ULL);
        FLOW_ASSERT_EQ((int)jet.payload.thermal.is_throttled, 0);
        FLOW_ASSERT_TRUE(jet.payload.thermal.temp_c < T_throttle);
        printf("    * P0 Result: Peak Temp=%.1f C, Throttled=%s, 10kHz Deadline Misses=%zu/1000\n",
               jet.payload.thermal.temp_c, jet.payload.thermal.is_throttled ? "YES" : "NO", deadline_misses);
    }

    /* ========================================================================= */
    /* 2. PILLAR P1: Quadratic Hessian Convexification in Polyhedral Model       */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(2, "P1: Quadratic Hessian Convexification & Volume Recovery");
    {
        FlowPolyhedron poly;
        int ok = flow_polyhedral_init(&poly, 2);
        FLOW_ASSERT_EQ(ok, 1);
        FLOW_ASSERT_EQ(poly.quad_constraint_count, 0ULL);

        /* Conservative Affine Box Bounds */
        flow_polyhedral_set_box_bounds(&poly, 0, 0, 38);
        flow_polyhedral_set_box_bounds(&poly, 1, 0, 18);

        /* Add Local Quadratic Hessian Constraint */
        double center[FLOW_POLY_MAX_DIM] = {32.0, 12.0};
        double hessian[FLOW_POLY_MAX_DIM][FLOW_POLY_MAX_DIM] = {
            { 0.0028, 0.0 },
            { 0.0,    0.00195 }
        };
        double gradient[FLOW_POLY_MAX_DIM] = { 0.015, 0.012 };
        double capacity_budget = 0.55;

        ok = flow_polyhedral_add_quadratic_constraint(&poly, center, hessian, gradient, capacity_budget);
        FLOW_ASSERT_EQ(ok, 1);
        FLOW_ASSERT_EQ(poly.quad_constraint_count, 1ULL);
        FLOW_ASSERT_TRUE(poly.quad_constraints[0].semi_axes[0] > 10.0);
        FLOW_ASSERT_TRUE(poly.quad_constraints[0].semi_axes[1] > 10.0);

        /* Solve schedule and check volume recovery */
        FlowPolyhedralSchedule sched;
        ok = flow_polyhedral_solve_schedule(&poly, 64, 16, &sched);
        FLOW_ASSERT_EQ(ok, 1);
        FLOW_ASSERT_TRUE(sched.has_quadratic_curvature);
        FLOW_ASSERT_TRUE(sched.quadratic_recovered_volume > 700.0);
        FLOW_ASSERT_TRUE(sched.optimal_tile_size >= 4);
        FLOW_ASSERT_TRUE(sched.optimal_simd_width >= 4);

        /* Verify formal SMT proof including Theorem 4 (Quadratic Convexity) */
        FlowSMTProofAttestation proof;
        FlowSMTResult smt_res = flow_polyhedral_verify_smt(&poly, &sched, &proof);
        FLOW_ASSERT_EQ(smt_res, FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_TRUE(strstr(proof.proof_summary, "QuadVol=") != NULL);

        printf("    * P1 Result: Affine Box=38x18, Quad Recovered Volume=%.1f, T*=%zu, V*=%zu, Proof=%s\n",
               sched.quadratic_recovered_volume, sched.optimal_tile_size, sched.optimal_simd_width,
               proof.proof_summary);
    }

    /* ========================================================================= */
    /* 3. PILLAR P2: 64-Node Lock-Free Zero-Copy Circular Descriptor Ring Mesh   */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(3, "P2: 64-Node Lock-Free Circular Descriptor Ring Mesh");
    {
        /* Chapter 12 Hardware Invariant: Strict 64-byte alignment */
        FLOW_ASSERT_EQ(sizeof(FlowSwarmRingSlot) % 64, 0ULL);
        FLOW_ASSERT_TRUE(sizeof(FlowSwarmRingSlot) >= 64);
        FLOW_ASSERT_TRUE(alignof(FlowSwarmRingSlot) >= 64);

        FlowSwarmRingMesh mesh;
        int ok = flow_swarm_ring_mesh_init(&mesh);
        FLOW_ASSERT_EQ(ok, 1);
        FLOW_ASSERT_EQ(mesh.active_node_count, 0ULL);

        /* Publish telemetry across all 64 heterogeneous nodes */
        for (uint8_t i = 0; i < 64; ++i) {
            FlowSwarmRole role = (FlowSwarmRole)(i % 5);
            uint16_t backpressure = (uint16_t)(100 + (i * 12) % 700);
            uint16_t latency = (uint16_t)(5 + (i * 3) % 80);
            uint16_t crc16 = (uint16_t)(0x1000 + i);
            uint32_t cap = 10000 + (uint32_t)i * 500;

            double coords[16];
            for (size_t d = 0; d < 16; ++d) {
                coords[d] = (double)i * 0.5 + (double)d * 0.1;
            }

            int p_ok = flow_swarm_ring_publish(&mesh, i, role, backpressure, latency, crc16, cap, coords);
            FLOW_ASSERT_EQ(p_ok, 1);
        }

        FLOW_ASSERT_EQ(mesh.active_node_count, 64ULL);
        FLOW_ASSERT_EQ(mesh.total_ring_updates, 64ULL);

        /* Lock-free sample test across multiple slots */
        for (uint8_t i = 0; i < 64; ++i) {
            FlowSwarmRingSlot sampled;
            int s_ok = flow_swarm_ring_sample(&mesh, i, &sampled);
            FLOW_ASSERT_EQ(s_ok, 1);
            FLOW_ASSERT_EQ(sampled.node_id, i);
            FLOW_ASSERT_EQ(sampled.role, (uint8_t)(i % 5));
            FLOW_ASSERT_FLOAT_EQ(sampled.q_snapshot[0], (double)i * 0.5, 1e-4);
            FLOW_ASSERT_FLOAT_EQ(sampled.q_snapshot[15], (double)i * 0.5 + 1.5, 1e-4);
        }

        /* Test Dynamic Lowest Energy Fluid Routing */
        uint8_t selected_router = 0xFF;
        int r_ok = flow_swarm_ring_route_lowest_energy(&mesh, FLOW_SWARM_ROLE_COMPUTE_ROUTER, &selected_router);
        FLOW_ASSERT_EQ(r_ok, 1);
        FLOW_ASSERT_TRUE(selected_router < 64);
        FLOW_ASSERT_EQ((int)(selected_router % 5), (int)FLOW_SWARM_ROLE_COMPUTE_ROUTER);

        /* High-throughput lock-free sampling benchmark (1,000,000 samples) */
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        FlowSwarmRingSlot bench_slot;
        for (size_t n = 0; n < 1000000; ++n) {
            uint8_t target = (uint8_t)(n & 63);
            flow_swarm_ring_sample(&mesh, target, &bench_slot);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double elapsed_sec = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) * 1e-9;
        double throughput_mops = (1.0 / elapsed_sec);

        FLOW_ASSERT_TRUE(throughput_mops > 5.0); /* Guaranteed > 5M ops/s zero-copy sampling */
        printf("    * P2 Result: 64-Node Mesh Size=%zu bytes (alignas=%zu), Throughput=%.2f M ops/s, Selected Router Node ID=%u\n",
               sizeof(FlowSwarmRingSlot), alignof(FlowSwarmRingSlot), throughput_mops, selected_router);
    }

    FLOW_TEST_SUITE_END();
    return 0;
}
