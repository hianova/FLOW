#include "embodied.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================= */
/* 1. Micro-Physics Simulation Safety Gate (Sim-to-Real Verifier)            */
/* ========================================================================= */

int flow_physics_init(FlowPhysicsEngine *engine, size_t joint_count, double mass_kg) {
    if (engine == NULL) return 0;
    memset(engine, 0, sizeof(*engine));
    if (joint_count > FLOW_MAX_JOINTS) joint_count = FLOW_MAX_JOINTS;
    if (joint_count == 0) joint_count = 6; /* Standard 6-DoF limb/biped default */

    engine->current_state.joint_count = joint_count;
    engine->current_state.mass_kg = mass_kg > 0.0 ? mass_kg : 25.0;
    engine->current_state.dt_seconds = 0.001; /* 1ms integration timestep */
    engine->max_angular_accel = 50.0;         /* 50 rad/s^2 */
    engine->friction_coefficient = 0.6;       /* Standard rubber-concrete friction */

    for (size_t j = 0; j < joint_count; ++j) {
        engine->current_state.max_torque_limit[j] = 80.0; /* 80 N*m max rating */
    }

    /* Set default rectangular support polygon (+-0.15m X, +-0.10m Y) */
    engine->current_state.support_vertex_count = 4;
    engine->current_state.support_polygon[0][0] = -0.15; engine->current_state.support_polygon[0][1] = -0.10;
    engine->current_state.support_polygon[1][0] =  0.15; engine->current_state.support_polygon[1][1] = -0.10;
    engine->current_state.support_polygon[2][0] =  0.15; engine->current_state.support_polygon[2][1] =  0.10;
    engine->current_state.support_polygon[3][0] = -0.15; engine->current_state.support_polygon[3][1] =  0.10;

    engine->current_state.center_of_mass[0] = 0.0;
    engine->current_state.center_of_mass[1] = 0.0;
    engine->current_state.center_of_mass[2] = 0.65; /* 0.65m height */

    return 1;
}

bool flow_physics_is_torque_safe(const FlowRigidBodyState *state, const double *candidate_torques) {
    if (state == NULL || candidate_torques == NULL) return false;
    for (size_t j = 0; j < state->joint_count; ++j) {
        double limit = state->max_torque_limit[j] > 0.0 ? state->max_torque_limit[j] : 80.0;
        if (fabs(candidate_torques[j]) > limit) {
            return false; /* Motor torque overload / burnout risk */
        }
    }
    return true;
}

/* Point-in-convex-polygon test for ZMP (Zero Moment Point) stability */
bool flow_physics_is_zmp_stable(const FlowRigidBodyState *state) {
    if (state == NULL || state->support_vertex_count < 3) return false;
    double px = state->zmp_position[0];
    double py = state->zmp_position[1];

    size_t n = state->support_vertex_count;
    bool positive = false;
    bool negative = false;

    for (size_t i = 0; i < n; ++i) {
        size_t next = (i + 1) % n;
        double x1 = state->support_polygon[i][0];
        double y1 = state->support_polygon[i][1];
        double x2 = state->support_polygon[next][0];
        double y2 = state->support_polygon[next][1];

        double cross = (x2 - x1) * (py - y1) - (y2 - y1) * (px - x1);
        if (cross > 1e-7) positive = true;
        if (cross < -1e-7) negative = true;
        if (positive && negative) return false; /* ZMP is outside support polygon (tipping!) */
    }
    return true;
}

int flow_physics_simulate_step(FlowPhysicsEngine *engine, const double *applied_torques) {
    if (engine == NULL || applied_torques == NULL) return 0;
    FlowRigidBodyState *s = &engine->current_state;

    if (!flow_physics_is_torque_safe(s, applied_torques)) {
        engine->violations_prevented_total++;
        return 0; /* Unsafe torque rejected */
    }

    double dt = s->dt_seconds > 0.0 ? s->dt_seconds : 0.001;

    /* Semi-implicit Euler integration of multi-joint dynamics */
    for (size_t j = 0; j < s->joint_count; ++j) {
        double inertia = 0.25; /* Approximate joint rotational inertia kg*m^2 */
        double accel = (applied_torques[j] - 0.05 * s->joint_velocities[j]) / inertia;

        if (fabs(accel) > engine->max_angular_accel) {
            accel = (accel > 0) ? engine->max_angular_accel : -engine->max_angular_accel;
        }

        s->joint_velocities[j] += accel * dt;
        s->joint_angles[j] += s->joint_velocities[j] * dt;
        s->joint_torques[j] = applied_torques[j];
    }

    /* Compute approximate ZMP: x_zmp = x_com - (z_com / g) * x_accel */
    double g = 9.81;
    double net_torque_x = 0.0, net_torque_y = 0.0;
    for (size_t j = 0; j < s->joint_count; ++j) {
        net_torque_x += applied_torques[j] * 0.1;
        net_torque_y += applied_torques[j] * 0.05;
    }
    s->zmp_position[0] = s->center_of_mass[0] - (net_torque_y / (s->mass_kg * g));
    s->zmp_position[1] = s->center_of_mass[1] + (net_torque_x / (s->mass_kg * g));

    engine->simulated_steps_total++;

    if (!flow_physics_is_zmp_stable(s)) {
        engine->violations_prevented_total++;
        return 0; /* Unstable pose rejected */
    }

    return 1;
}

uint64_t flow_physics_get_safety_mask(const FlowPhysicsEngine *engine,
                                      const FlowPlanDimensionSet *dims) {
    if (engine == NULL || dims == NULL || dims->count == 0) return (uint64_t)-1;
    uint64_t mask = (uint64_t)-1;
    unsigned shift = 0;

    for (size_t i = 0; i < dims->count; ++i) {
        const FlowPlanDimension *d = &dims->dimensions[i];
        unsigned bits = flow_dimension_bits(d);
        if (bits == 0) continue;

        /* If dimension affects torque/stiffness and robot is near boundary, mask out high-risk bits */
        if (strstr(d->name, "torque") || strstr(d->name, "stiffness") || strstr(d->name, "gain")) {
            if (!flow_physics_is_zmp_stable(&engine->current_state)) {
                uint64_t dim_mask = (bits >= 64) ? (uint64_t)-1 : (((uint64_t)1 << bits) - 1);
                mask &= ~(dim_mask << shift);
            }
        }
        shift += bits;
    }
    return mask;
}

/* ========================================================================= */
/* 2. Dual-Rate Frequency Separation Implementation                          */
/* ========================================================================= */

int flow_dual_rate_init(FlowDualRateController *ctrl, size_t joint_count) {
    if (ctrl == NULL) return 0;
    memset(ctrl, 0, sizeof(*ctrl));
    if (joint_count > FLOW_MAX_JOINTS) joint_count = FLOW_MAX_JOINTS;
    if (joint_count == 0) joint_count = 6;

    for (size_t j = 0; j < joint_count; ++j) {
        ctrl->spinal.kp[j] = 120.0; /* 120 N*m/rad */
        ctrl->spinal.kd[j] = 8.0;   /* 8 N*m/(rad/s) */
        ctrl->spinal.ki[j] = 1.5;   /* 1.5 N*m/(rad*s) */
    }
    ctrl->cortical_mode = FLOW_GAIT_FLAT_WALK;
    ctrl->transition_alpha = 1.0;
    return 1;
}

int flow_dual_rate_spinal_tick(FlowDualRateController *ctrl,
                              const double *target_angles,
                              const double *current_angles,
                              const double *current_vels,
                              double *output_torques,
                              double dt) {
    if (ctrl == NULL || target_angles == NULL || current_angles == NULL ||
        current_vels == NULL || output_torques == NULL) return 0;
    if (dt <= 0.0) dt = 0.001;

    for (size_t j = 0; j < FLOW_MAX_JOINTS; ++j) {
        double error = target_angles[j] - current_angles[j];
        ctrl->spinal.integral_error[j] += error * dt;

        /* Anti-windup clamping */
        if (ctrl->spinal.integral_error[j] > 10.0) ctrl->spinal.integral_error[j] = 10.0;
        if (ctrl->spinal.integral_error[j] < -10.0) ctrl->spinal.integral_error[j] = -10.0;

        double d_error = (error - ctrl->spinal.prev_error[j]) / dt;
        ctrl->spinal.prev_error[j] = error;
        (void)d_error;

        double tau = (ctrl->spinal.kp[j] * error) +
                     (ctrl->spinal.ki[j] * ctrl->spinal.integral_error[j]) -
                     (ctrl->spinal.kd[j] * current_vels[j]) +
                     ctrl->spinal.feedforward_torque[j];

        output_torques[j] = tau;
    }
    ctrl->spinal_ticks_total++;
    return 1;
}

int flow_dual_rate_cortical_reconfigure(FlowDualRateController *ctrl,
                                        FlowCorticalGaitMode new_mode,
                                        const FlowUnit *new_unit) {
    if (ctrl == NULL) return 0;
    ctrl->cortical_mode = new_mode;
    ctrl->current_cortical_unit = new_unit;
    ctrl->transition_alpha = 0.0; /* Begin smooth trajectory interpolation */
    ctrl->cortical_swaps_total++;
    return 1;
}

/* ========================================================================= */
/* 3. Sensor Fusion & Kalman Filter Mask Implementation                      */
/* ========================================================================= */

static void kalman_1d_init(FlowKalmanFilter1D *kf, double q, double r) {
    kf->state_estimate = 0.0;
    kf->error_covariance = 1.0;
    kf->process_noise_q = q > 0.0 ? q : 0.01;
    kf->measurement_noise_r = r > 0.0 ? r : 0.1;
    kf->kalman_gain_k = 0.0;
}

static double kalman_1d_update(FlowKalmanFilter1D *kf, double measurement) {
    /* 1. Time Update (Predict) */
    kf->error_covariance += kf->process_noise_q;

    /* 2. Measurement Update (Correct) */
    kf->kalman_gain_k = kf->error_covariance / (kf->error_covariance + kf->measurement_noise_r);
    kf->state_estimate += kf->kalman_gain_k * (measurement - kf->state_estimate);
    kf->error_covariance *= (1.0 - kf->kalman_gain_k);

    return kf->state_estimate;
}

int flow_sensor_fusion_init(FlowSensorFusion *fusion) {
    if (fusion == NULL) return 0;
    memset(fusion, 0, sizeof(*fusion));
    kalman_1d_init(&fusion->pitch_filter, 0.005, 0.08);
    kalman_1d_init(&fusion->roll_filter, 0.005, 0.08);
    kalman_1d_init(&fusion->accel_z_filter, 0.01, 0.15);
    fusion->sensor_confidence = 1.0;
    return 1;
}

int flow_sensor_fusion_update_imu(FlowSensorFusion *fusion,
                                  double raw_pitch,
                                  double raw_roll,
                                  double raw_accel_z,
                                  double *clean_pitch_out,
                                  double *clean_roll_out,
                                  double *clean_accel_z_out) {
    if (fusion == NULL) return 0;

    /* Detect high-frequency spurious noise spike (e.g. foot landing shock vibration) */
    double diff_p = fabs(raw_pitch - fusion->pitch_filter.state_estimate);
    double diff_r = fabs(raw_roll - fusion->roll_filter.state_estimate);

    if (diff_p > 1.5 || diff_r > 1.5) {
        fusion->rejected_noise_spikes++;
        fusion->sensor_confidence = 0.60;
    } else {
        fusion->sensor_confidence = 0.98;
    }

    double cp = kalman_1d_update(&fusion->pitch_filter, raw_pitch);
    double cr = kalman_1d_update(&fusion->roll_filter, raw_roll);
    double cz = kalman_1d_update(&fusion->accel_z_filter, raw_accel_z);

    if (clean_pitch_out) *clean_pitch_out = cp;
    if (clean_roll_out) *clean_roll_out = cr;
    if (clean_accel_z_out) *clean_accel_z_out = cz;

    fusion->updates_total++;
    return 1;
}

uint64_t flow_sensor_fusion_get_clean_mask(const FlowSensorFusion *fusion,
                                           const FlowPlanDimensionSet *dims) {
    if (fusion == NULL || dims == NULL || dims->count == 0) return (uint64_t)-1;
    /* If sensor confidence is low (spurious noise/hallucination), lock high-risk mutations */
    if (fusion->sensor_confidence < 0.80) {
        uint64_t mask = 0;
        unsigned shift = 0;
        for (size_t i = 0; i < dims->count; ++i) {
            const FlowPlanDimension *d = &dims->dimensions[i];
            unsigned bits = flow_dimension_bits(d);
            if (bits == 0) continue;
            uint64_t dim_mask = (bits >= 64) ? (uint64_t)-1 : (((uint64_t)1 << bits) - 1);
            /* Only allow conservative baseline tuning, reject aggressive structural mutation */
            if (strcmp(d->name, "tuning_buffer") == 0) {
                mask |= (dim_mask << shift);
            }
            shift += bits;
        }
        return mask;
    }
    return (uint64_t)-1;
}

/* ========================================================================= */
/* 4. Thermodynamic Energy Governor & Event-Driven Sleep                     */
/* ========================================================================= */

int flow_energy_governor_init(FlowThermalEnergyGovernor *gov,
                              double mass_baseline_kg,
                              double disturbance_threshold_kg) {
    if (gov == NULL) return 0;
    memset(gov, 0, sizeof(*gov));
    gov->current_battery_percent = 100.0;
    gov->edge_chip_temp_celsius = 45.0;
    gov->thermal_throttle_limit_celsius = 85.0;
    gov->current_power_draw_watts = 15.0;
    gov->steady_state_mass_baseline_kg = mass_baseline_kg > 0.0 ? mass_baseline_kg : 25.0;
    gov->disturbance_threshold_kg = disturbance_threshold_kg > 0.0 ? disturbance_threshold_kg : 5.0;
    gov->is_chaotic_engine_sleeping = true; /* Sleep at steady state by default */
    return 1;
}

bool flow_energy_governor_check_wakeup(FlowThermalEnergyGovernor *gov,
                                       double observed_mass_kg,
                                       double external_impact_force_n) {
    if (gov == NULL) return true;

    double delta_mass = fabs(observed_mass_kg - gov->steady_state_mass_baseline_kg);
    bool shock_detected = (delta_mass >= gov->disturbance_threshold_kg) || (external_impact_force_n > 50.0);

    if (shock_detected) {
        gov->is_chaotic_engine_sleeping = false;
        gov->shock_wakeups_total++;
        return true; /* Wake up 1-bit chaotic annealing to adapt to payload change! */
    }

    gov->is_chaotic_engine_sleeping = true;
    gov->sleep_cycles_total++;
    return false; /* Keep sleeping (0W CPU computation) */
}

double flow_energy_governor_compute_objective_penalty(const FlowThermalEnergyGovernor *gov,
                                                      double base_latency_score,
                                                      double compute_energy_joules) {
    if (gov == NULL) return base_latency_score;

    double lambda_energy = 0.5;
    double lambda_thermal = 1.2;

    double thermal_ratio = gov->edge_chip_temp_celsius / gov->thermal_throttle_limit_celsius;
    double thermal_penalty = (thermal_ratio > 0.8) ? (thermal_ratio - 0.8) * 100.0 : 0.0;

    return base_latency_score + (lambda_energy * compute_energy_joules) + (lambda_thermal * thermal_penalty);
}
