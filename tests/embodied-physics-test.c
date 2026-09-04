#include "embodied.h"
#include "bitspace.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "embodied-physics-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

int main(void) {
    printf("Starting Embodied FLOW Physical Intelligence Test...\n");

    /* ===================================================================== */
    /* 1. Test Micro-Physics Simulation Safety Gate (Sim-to-Real Verifier)   */
    /* ===================================================================== */
    FlowPhysicsEngine phys;
    CHECK(flow_physics_init(&phys, 6, 25.0) == 1);
    CHECK(phys.current_state.joint_count == 6);
    CHECK(phys.current_state.mass_kg == 25.0);

    /* Safe torques test */
    double safe_torques[6] = { 10.0, -15.0, 20.0, -5.0, 8.0, 12.0 };
    CHECK(flow_physics_is_torque_safe(&phys.current_state, safe_torques));
    CHECK(flow_physics_simulate_step(&phys, safe_torques) == 1);
    CHECK(phys.simulated_steps_total == 1);
    CHECK(phys.violations_prevented_total == 0);

    /* Over-torque burnout test: 120 N*m exceeds 80 N*m limit */
    double dangerous_torques[6] = { 120.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
    CHECK(!flow_physics_is_torque_safe(&phys.current_state, dangerous_torques));
    CHECK(flow_physics_simulate_step(&phys, dangerous_torques) == 0);
    CHECK(phys.violations_prevented_total == 1);

    /* Tipping / ZMP out-of-polygon test */
    phys.current_state.zmp_position[0] = 0.50; /* 0.50m is way outside 0.15m polygon */
    phys.current_state.zmp_position[1] = 0.0;
    CHECK(!flow_physics_is_zmp_stable(&phys.current_state));

    /* Reset to safe state */
    phys.current_state.zmp_position[0] = 0.0;
    phys.current_state.zmp_position[1] = 0.0;
    CHECK(flow_physics_is_zmp_stable(&phys.current_state));

    /* Test Physics Safety Mask */
    FlowPlanDimensionSet dims;
    dims.count = 2;
    strncpy(dims.dimensions[0].name, "joint_torque_gain", sizeof(dims.dimensions[0].name) - 1);
    dims.dimensions[0].kind = FLOW_DIM_DISCRETE;
    dims.dimensions[0].min_val = 0;
    dims.dimensions[0].max_val = 7; /* 3 bits */

    strncpy(dims.dimensions[1].name, "tuning_buffer", sizeof(dims.dimensions[1].name) - 1);
    dims.dimensions[1].kind = FLOW_DIM_DISCRETE;
    dims.dimensions[1].min_val = 0;
    dims.dimensions[1].max_val = 3; /* 2 bits */

    uint64_t safe_mask = flow_physics_get_safety_mask(&phys, &dims);
    CHECK(safe_mask == (uint64_t)-1); /* Stable state allows full tuning */

    /* ===================================================================== */
    /* 2. Test Dual-Rate Frequency Separation (Spinal vs Cortical)           */
    /* ===================================================================== */
    FlowDualRateController dual_ctrl;
    CHECK(flow_dual_rate_init(&dual_ctrl, 6) == 1);
    CHECK(dual_ctrl.cortical_mode == FLOW_GAIT_FLAT_WALK);

    double target_angles[FLOW_MAX_JOINTS] = { 0.1, 0.2, -0.1, 0.0, 0.3, -0.2, 0 };
    double current_angles[FLOW_MAX_JOINTS] = {0};
    double current_vels[FLOW_MAX_JOINTS] = {0};
    double output_torques[FLOW_MAX_JOINTS] = {0};

    /* Run 2000 fast 1kHz spinal reflex steps */
    for (int tick = 0; tick < 2000; ++tick) {
        CHECK(flow_dual_rate_spinal_tick(&dual_ctrl, target_angles, current_angles, current_vels, output_torques, 0.001) == 1);
        /* Simulate joint response with inertia I = 0.1 */
        for (int j = 0; j < 6; ++j) {
            current_vels[j] += (output_torques[j] / 0.1) * 0.001;
            current_angles[j] += current_vels[j] * 0.001;
        }
    }
    CHECK(dual_ctrl.spinal_ticks_total == 2000);
    /* Verify spinal loop tracked target angles closely */
    for (int j = 0; j < 6; ++j) {
        CHECK(fabs(target_angles[j] - current_angles[j]) < 0.01);
    }

    /* Perform low-frequency Cortical Gait Reconfiguration */
    CHECK(flow_dual_rate_cortical_reconfigure(&dual_ctrl, FLOW_GAIT_STAIR_CLIMB, NULL) == 1);
    CHECK(dual_ctrl.cortical_mode == FLOW_GAIT_STAIR_CLIMB);
    CHECK(dual_ctrl.cortical_swaps_total == 1);

    /* ===================================================================== */
    /* 3. Test Sensor Fusion & Kalman Filter Noise Rejection                 */
    /* ===================================================================== */
    FlowSensorFusion fusion;
    CHECK(flow_sensor_fusion_init(&fusion) == 1);

    double true_pitch = 0.20; /* 0.20 rad tilt */
    double clean_pitch = 0.0, clean_roll = 0.0, clean_accel_z = 0.0;

    /* Feed 200 noisy sensor samples */
    for (int i = 0; i < 200; ++i) {
        double noise = ((double)(i % 7) - 3.0) * 0.05; /* +-0.15 rad high-frequency vibration */
        double raw_pitch = true_pitch + noise;
        flow_sensor_fusion_update_imu(&fusion, raw_pitch, 0.0, 9.81, &clean_pitch, &clean_roll, &clean_accel_z);
    }

    /* Verify Kalman filter converges to true pitch while rejecting vibration */
    CHECK(fabs(clean_pitch - true_pitch) < 0.05);
    CHECK(fusion.sensor_confidence >= 0.90);

    /* Feed massive sensor glitch (spurious shock) */
    flow_sensor_fusion_update_imu(&fusion, 5.0, 0.0, 9.81, &clean_pitch, &clean_roll, &clean_accel_z);
    CHECK(fusion.rejected_noise_spikes == 1);
    CHECK(fusion.sensor_confidence < 0.80);

    /* Verify clean mask locks dangerous structural bits during sensor noise glitch */
    uint64_t noise_mask = flow_sensor_fusion_get_clean_mask(&fusion, &dims);
    CHECK(noise_mask != (uint64_t)-1); /* Pruned! */

    /* ===================================================================== */
    /* 4. Test Thermodynamic Energy Governor & Event-Driven Sleep            */
    /* ===================================================================== */
    FlowThermalEnergyGovernor gov;
    CHECK(flow_energy_governor_init(&gov, 25.0, 5.0) == 1);
    CHECK(gov.is_chaotic_engine_sleeping == true);

    /* Steady state: payload is exactly baseline (25.0kg) -> STAY SLEEPING */
    CHECK(flow_energy_governor_check_wakeup(&gov, 25.2, 5.0) == false);
    CHECK(gov.is_chaotic_engine_sleeping == true);
    CHECK(gov.sleep_cycles_total == 1);

    /* Sudden 10kg payload shock (e.g. grabbed heavy box) -> WAKE UP BMF */
    CHECK(flow_energy_governor_check_wakeup(&gov, 36.0, 10.0) == true);
    CHECK(gov.is_chaotic_engine_sleeping == false);
    CHECK(gov.shock_wakeups_total == 1);

    /* Test Energy & Thermal Penalty Calculation */
    double penalty_cool = flow_energy_governor_compute_objective_penalty(&gov, 10.0, 5.0);
    gov.edge_chip_temp_celsius = 82.0; /* Near 85C limit */
    double penalty_hot = flow_energy_governor_compute_objective_penalty(&gov, 10.0, 5.0);
    CHECK(penalty_hot > penalty_cool);

    printf("EMBODIED_PHYSICS_TEST=passed sim_to_real_gate=verified spinal_1khz=sound kalman_fusion=sound thermal_sleep=verified\n");
    return 0;
}
