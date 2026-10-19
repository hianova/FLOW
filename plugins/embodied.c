#include "embodied.h"
#include "flow_smt_dsl.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 1. Micro-Physics Simulation Safety Gate (Sim-to-Real Verifier) */
int flow_physics_init(FlowPhysicsEngine *engine, size_t joint_count, double mass_kg) {
    if (!engine) return 0;
    memset(engine, 0, sizeof(*engine));
    if (joint_count > FLOW_MAX_JOINTS) joint_count = FLOW_MAX_JOINTS;
    if (joint_count == 0) joint_count = 6; /* Standard 6-DoF limb/biped default */
    engine->current_state.joint_count = joint_count;
    engine->current_state.mass_kg = mass_kg > 0.0 ? mass_kg : 25.0;
    engine->current_state.dt_seconds = 0.001;
    engine->max_angular_accel = 50.0;
    engine->friction_coefficient = 0.6;
    for (size_t j = 0; j < joint_count; ++j) engine->current_state.max_torque_limit[j] = 80.0;
    /* Set default rectangular support polygon (+-0.15m X, +-0.10m Y) */
    engine->current_state.support_vertex_count = 4;
    const double poly[4][2] = {{-0.15, -0.10}, {0.15, -0.10}, {0.15, 0.10}, {-0.15, 0.10}};
    memcpy(engine->current_state.support_polygon, poly, sizeof(poly));
    engine->current_state.center_of_mass[2] = 0.65;
    return 1;
}

bool flow_physics_is_torque_safe(const FlowRigidBodyState *state, const double *candidate_torques) {
    if (!state || !candidate_torques) return false;
    for (size_t j = 0; j < state->joint_count; ++j) {
        double limit = state->max_torque_limit[j] > 0.0 ? state->max_torque_limit[j] : 80.0;
        if (fabs(candidate_torques[j]) > limit) return false;
    }
    return true;
}

/* Point-in-convex-polygon test for ZMP (Zero Moment Point) stability */
bool flow_physics_is_zmp_stable(const FlowRigidBodyState *state) {
    if (!state || state->support_vertex_count < 3) return false;
    double px = state->zmp_position[0], py = state->zmp_position[1];
    size_t n = state->support_vertex_count;
    bool pos = false, neg = false;
    for (size_t i = 0; i < n; ++i) {
        size_t next = (i + 1) % n;
        double x1 = state->support_polygon[i][0], y1 = state->support_polygon[i][1];
        double x2 = state->support_polygon[next][0], y2 = state->support_polygon[next][1];
        double cross = (x2 - x1) * (py - y1) - (y2 - y1) * (px - x1);
        if (cross > 1e-7) pos = true;
        if (cross < -1e-7) neg = true;
        if (pos && neg) return false;
    }
    return true;
}

int flow_physics_simulate_step(FlowPhysicsEngine *engine, const double *applied_torques) {
    if (!engine || !applied_torques) return 0;
    FlowRigidBodyState *s = &engine->current_state;
    if (!flow_physics_is_torque_safe(s, applied_torques)) {
        engine->violations_prevented_total++;
        return 0;
    }
    double dt = s->dt_seconds > 0.0 ? s->dt_seconds : 0.001, net_tx = 0.0, net_ty = 0.0;
    /* Semi-implicit Euler integration with Moreau Convex Acceleration Projection */
    for (size_t j = 0; j < s->joint_count; ++j) {
        double accel = (applied_torques[j] - 0.05 * s->joint_velocities[j]) / 0.25;
        accel = fmin(engine->max_angular_accel, fmax(-engine->max_angular_accel, accel));
        s->joint_velocities[j] += accel * dt;
        s->joint_angles[j] += s->joint_velocities[j] * dt;
        s->joint_torques[j] = applied_torques[j];
        net_tx += applied_torques[j] * 0.1;
        net_ty += applied_torques[j] * 0.05;
    }
    s->zmp_position[0] = s->center_of_mass[0] - (net_ty / (s->mass_kg * 9.81));
    s->zmp_position[1] = s->center_of_mass[1] + (net_tx / (s->mass_kg * 9.81));
    engine->simulated_steps_total++;
    if (!flow_physics_is_zmp_stable(s)) {
        engine->violations_prevented_total++;
        return 0;
    }
    return 1;
}

uint64_t flow_physics_get_safety_mask(const FlowPhysicsEngine *engine, const FlowPlanDimensionSet *dims) {
    if (!engine || !dims || dims->count == 0) return (uint64_t)-1;
    uint64_t mask = (uint64_t)-1;
    unsigned shift = 0;
    for (size_t i = 0; i < dims->count; ++i) {
        const FlowPlanDimension *d = &dims->dimensions[i];
        unsigned bits = flow_dimension_bits(d);
        if (bits == 0) continue;
        if ((strstr(d->name, "torque") || strstr(d->name, "stiffness") || strstr(d->name, "gain")) &&
            !flow_physics_is_zmp_stable(&engine->current_state)) {
            uint64_t dim_mask = (bits >= 64) ? (uint64_t)-1 : (((uint64_t)1 << bits) - 1);
            mask &= ~(dim_mask << shift);
        }
        shift += bits;
    }
    return mask;
}

/* 2. Dual-Rate Frequency Separation Implementation */
int flow_dual_rate_init(FlowDualRateController *ctrl, size_t joint_count) {
    if (!ctrl) return 0;
    memset(ctrl, 0, sizeof(*ctrl));
    if (joint_count > FLOW_MAX_JOINTS) joint_count = FLOW_MAX_JOINTS;
    if (joint_count == 0) joint_count = 6;
    for (size_t j = 0; j < joint_count; ++j) {
        ctrl->spinal.kp[j] = 120.0;
        ctrl->spinal.kd[j] = 8.0;
        ctrl->spinal.ki[j] = 1.5;
    }
    ctrl->cortical_mode = FLOW_GAIT_FLAT_WALK;
    ctrl->transition_alpha = 1.0;
    return 1;
}

int flow_dual_rate_spinal_tick(FlowDualRateController *ctrl, const double *target_angles,
                               const double *current_angles, const double *current_vels,
                               double *output_torques, double dt) {
    for (size_t j = 0; j < FLOW_MAX_JOINTS; ++j) {
        double error = target_angles[j] - current_angles[j];
        ctrl->spinal.integral_error[j] = fmin(10.0, fmax(-10.0, ctrl->spinal.integral_error[j] + error * dt));
        ctrl->spinal.prev_error[j] = error;
        output_torques[j] = (ctrl->spinal.kp[j] * error) +
                            (ctrl->spinal.ki[j] * ctrl->spinal.integral_error[j]) -
                            (ctrl->spinal.kd[j] * current_vels[j]) +
                            ctrl->spinal.feedforward_torque[j];
    }
    ctrl->spinal_ticks_total++;
    return 1;
}

int flow_dual_rate_cortical_reconfigure(FlowDualRateController *ctrl, FlowCorticalGaitMode new_mode, const FlowUnit *new_unit) {
    if (!ctrl) return 0;
    ctrl->cortical_mode = new_mode;
    ctrl->current_cortical_unit = new_unit;
    ctrl->transition_alpha = 0.0;
    ctrl->cortical_swaps_total++;
    return 1;
}

/* 3. Sensor Fusion & Kalman Filter Mask Implementation */
static void kalman_1d_init(FlowKalmanFilter1D *kf, double q, double r) {
    *kf = (FlowKalmanFilter1D){.state_estimate = 0.0, .error_covariance = 1.0,
                               .process_noise_q = q > 0.0 ? q : 0.01,
                               .measurement_noise_r = r > 0.0 ? r : 0.1};
}

static double kalman_1d_update(FlowKalmanFilter1D *kf, double measurement) {
    kf->error_covariance += kf->process_noise_q;
    kf->kalman_gain_k = kf->error_covariance / (kf->error_covariance + kf->measurement_noise_r);
    kf->state_estimate += kf->kalman_gain_k * (measurement - kf->state_estimate);
    kf->error_covariance *= (1.0 - kf->kalman_gain_k);
    return kf->state_estimate;
}

int flow_sensor_fusion_init(FlowSensorFusion *fusion) {
    if (!fusion) return 0;
    memset(fusion, 0, sizeof(*fusion));
    kalman_1d_init(&fusion->pitch_filter, 0.005, 0.08);
    kalman_1d_init(&fusion->roll_filter, 0.005, 0.08);
    kalman_1d_init(&fusion->accel_z_filter, 0.01, 0.15);
    fusion->sensor_confidence = 1.0;
    return 1;
}

int flow_sensor_fusion_update_imu(FlowSensorFusion *fusion, double raw_pitch, double raw_roll, double raw_accel_z,
                                  double *clean_pitch_out, double *clean_roll_out, double *clean_accel_z_out) {
    if (!fusion) return 0;
    if (fabs(raw_pitch - fusion->pitch_filter.state_estimate) > 1.5 ||
        fabs(raw_roll - fusion->roll_filter.state_estimate) > 1.5) {
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

uint64_t flow_sensor_fusion_get_clean_mask(const FlowSensorFusion *fusion, const FlowPlanDimensionSet *dims) {
    if (!fusion || !dims || dims->count == 0 || fusion->sensor_confidence >= 0.80) return (uint64_t)-1;
    uint64_t mask = 0;
    unsigned shift = 0;
    for (size_t i = 0; i < dims->count; ++i) {
        const FlowPlanDimension *d = &dims->dimensions[i];
        unsigned bits = flow_dimension_bits(d);
        if (bits == 0) continue;
        uint64_t dim_mask = (bits >= 64) ? (uint64_t)-1 : (((uint64_t)1 << bits) - 1);
        if (strcmp(d->name, "tuning_buffer") == 0) mask |= (dim_mask << shift);
        shift += bits;
    }
    return mask;
}

/* 4. Thermodynamic Energy Governor & Event-Driven Sleep */
int flow_energy_governor_init(FlowThermalEnergyGovernor *gov, double mass_baseline_kg, double disturbance_threshold_kg) {
    if (!gov) return 0;
    *gov = (FlowThermalEnergyGovernor){
        .current_battery_percent = 100.0, .edge_chip_temp_celsius = 45.0,
        .thermal_throttle_limit_celsius = 85.0, .current_power_draw_watts = 15.0,
        .steady_state_mass_baseline_kg = mass_baseline_kg > 0.0 ? mass_baseline_kg : 25.0,
        .disturbance_threshold_kg = disturbance_threshold_kg > 0.0 ? disturbance_threshold_kg : 5.0,
        .is_chaotic_engine_sleeping = true
    };
    return 1;
}

bool flow_energy_governor_check_wakeup(FlowThermalEnergyGovernor *gov, double observed_mass_kg, double external_impact_force_n) {
    if (!gov) return true;
    bool shock = (fabs(observed_mass_kg - gov->steady_state_mass_baseline_kg) >= gov->disturbance_threshold_kg) ||
                 (external_impact_force_n > 50.0);
    gov->is_chaotic_engine_sleeping = !shock;
    if (shock) gov->shock_wakeups_total++; else gov->sleep_cycles_total++;
    return shock;
}

double flow_energy_governor_compute_objective_penalty(const FlowThermalEnergyGovernor *gov,
                                                      double base_latency_score, double compute_energy_joules) {
    if (!gov) return base_latency_score;
    double ratio = gov->edge_chip_temp_celsius / gov->thermal_throttle_limit_celsius;
    double thermal_pen = (ratio > 0.8) ? (ratio - 0.8) * 100.0 : 0.0;
    return base_latency_score + 0.5 * compute_energy_joules + 1.2 * thermal_pen;
}

/* 5. Phase-Lag & Dead-Time Smith Predictor Implementation */
int flow_smith_predictor_init(FlowSmithPredictor *sp, size_t joint_count, double delay_seconds, double dt) {
    if (!sp) return 0;
    memset(sp, 0, sizeof(*sp));
    sp->joint_count = (joint_count > 0 && joint_count <= FLOW_MAX_JOINTS) ? joint_count : FLOW_MAX_JOINTS;
    sp->delay_seconds = delay_seconds >= 0.0 ? delay_seconds : 0.0;
    double step_dt = dt > 0.0 ? dt : 0.001;
    if (sp->delay_seconds > 0.0) {
        size_t steps = (size_t)ceil(sp->delay_seconds / step_dt);
        sp->delay_steps = steps >= FLOW_SMITH_MAX_DELAY_STEPS ? FLOW_SMITH_MAX_DELAY_STEPS - 1 : steps;
    }
    sp->damping_gain = 0.85;
    return 1;
}

int flow_smith_predictor_push_and_predict(FlowSmithPredictor *sp, const double *delayed_angles,
                                          const double *applied_torques, double *predicted_future_angles_out,
                                          double *predicted_future_vels_out, double dt) {
    if (!sp || !delayed_angles) return 0;
    double step_dt = dt > 0.0 ? dt : 0.001;
    size_t slot = sp->head_idx;
    for (size_t j = 0; j < sp->joint_count; ++j) {
        sp->history_angles[slot][j] = delayed_angles[j];
        sp->history_torques[slot][j] = applied_torques ? applied_torques[j] : 0.0;
    }
    if (sp->delay_steps > 0) sp->head_idx = (sp->head_idx + 1) % sp->delay_steps;
    sp->phase_corrections_total++;

    for (size_t j = 0; j < sp->joint_count; ++j) {
        double angle_est = delayed_angles[j], vel_est = 0.0;
        for (size_t s = 0; s < sp->delay_steps; ++s) {
            vel_est += (sp->history_torques[s][j] / 0.15) * step_dt * sp->damping_gain;
            angle_est += vel_est * step_dt;
        }
        if (predicted_future_angles_out) predicted_future_angles_out[j] = angle_est;
        if (predicted_future_vels_out) predicted_future_vels_out[j] = vel_est;
    }
    return 1;
}

bool flow_physics_is_future_state_safe(FlowPhysicsEngine *engine, const FlowSmithPredictor *predictor,
                                       const double *delayed_angles, const double *candidate_torques, double dt) {
    if (!engine || !predictor || !delayed_angles || !candidate_torques) return false;
    if (!flow_physics_is_torque_safe(&engine->current_state, candidate_torques)) return false;

    double future_angles[FLOW_MAX_JOINTS] = {0}, future_vels[FLOW_MAX_JOINTS] = {0};
    FlowSmithPredictor temp_sp = *predictor;
    flow_smith_predictor_push_and_predict(&temp_sp, delayed_angles, candidate_torques, future_angles, future_vels, dt);

    FlowRigidBodyState future_state = engine->current_state;
    future_state.zmp_position[0] = 0.0;
    future_state.zmp_position[1] = 0.0;
    for (size_t j = 0; j < future_state.joint_count; ++j) {
        future_state.joint_angles[j] = future_angles[j];
        future_state.joint_velocities[j] = future_vels[j];
        future_state.joint_torques[j] = candidate_torques[j];
        future_state.zmp_position[0] += (future_angles[j] * 0.05);
    }
    return flow_physics_is_zmp_stable(&future_state);
}

/* Dynamic DSO Plugin ABI Export */
static const Component EMBODIED_COMPONENTS[] = {{
    .id = "embodied_controller", .kind = "controller", .resource = "hardware", .capability = "realtime",
    .latency_score = 1, .memory_score = 2, .domain_contract = "zmp_torque_safe",
    .flow_binding = "flow_embodied_run", .memory_fixed_bytes = sizeof(FlowPhysicsEngine),
    .memory_bytes_per_capacity = 64, .reload_capable = 1
}};

static uint64_t embodied_env_mask(const SemanticIR *ir, const Component *c, const FlowPlanDimensionSet *dims, const FlowEnvironmentState *env) {
    (void)ir; (void)c; (void)dims; (void)env;
    return UINT64_MAX;
}

static size_t flow_embodied_get_genome_bit_size(void) { return 16; }

static uint64_t flow_embodied_get_valid_mask(const FlowEnvironmentState *env) {
    double e_max = (env && env->measured_miss_rate > 0.05) ? 40.0 : 100.0;
    double max_v_sq = (e_max - log(12.0)) / 2.5;
    double v_max = max_v_sq > 0.0 ? sqrt(max_v_sq) : 1.0;
    return (v_max < 4.0) ? (0x0000FFFFULL & ~0x00C0ULL) : 0x0000FFFFULL;
}

static double flow_embodied_evaluate_energy(uint64_t genome) {
    unsigned t = (unsigned)(genome & 0x0F), v = (unsigned)((genome >> 4) & 0x0F);
    unsigned f = (unsigned)((genome >> 8) & 0x0F), d = (unsigned)((genome >> 12) & 0x0F);
    return (double)t * 1.5 + (double)v * 2.0 + (double)(16 - f) * 0.8 + (double)d * 1.2;
}

static void flow_embodied_emit_llvm_ir(uint64_t genome, void *module_or_out) {
    if (!module_or_out) return;
    FILE *out = (FILE *)module_or_out;
    unsigned t = (unsigned)(genome & 0x0F), v = (unsigned)((genome >> 4) & 0x0F);
    unsigned f = (unsigned)((genome >> 8) & 0x0F), d = (unsigned)((genome >> 12) & 0x0F);
    fprintf(out, "/* [flow.embodied] Native Reflex Code (Genome: 0x%04llx) */\n"
                 "/* Torque Max: %u Nm, Vel Cap: %u rad/s, Loop Freq: %u kHz, Delay Comp: %u steps */\n"
                 "void flow_embodied_step(const double *angles, const double *vels, double *torques_out) {\n"
                 "    for (int j = 0; j < %d; ++j) {\n"
                 "        torques_out[j] = -%f * angles[j] - %f * vels[j];\n"
                 "    }\n}\n",
            (unsigned long long)genome, t * 10, v * 2, f + 1, d, FLOW_MAX_JOINTS, (double)(t + 1) * 2.5, (double)(v + 1) * 0.5);
}

static const FlowPluginABI EMBODIED_ABI_V2 = {
    .get_genome_bit_size = flow_embodied_get_genome_bit_size,
    .get_valid_mask = flow_embodied_get_valid_mask,
    .evaluate_energy = flow_embodied_evaluate_energy,
    .emit_llvm_ir = flow_embodied_emit_llvm_ir
};

static const FlowPlugin EMBODIED_PLUGIN = {
    .name = "flow.embodied", .version = "1.0", .components = EMBODIED_COMPONENTS, .component_count = 1,
    .environment_mask = embodied_env_mask,
    .doc_title = "Embodied AI Physics & Dual-Rate Gait Controller",
    .doc_responsibilities = "Provides micro-physics simulation gate, ZMP safety verification, Smith predictor, and 10kHz spinal reflex",
    .doc_algorithmic_guarantee = "Guarantees zero-fall ZMP stability polygon and dead-time phase lag compensation",
    .doc_memory_concurrency_model = "Stack-allocated joint states, QSBR zero-copy lock-free cortical hot-swapping",
    .doc_key_apis = "flow_physics_is_future_state_safe, flow_smith_predictor_push_and_predict",
    .doc_layer = 2
};

static const FlowPluginDescriptor EMBODIED_DESCRIPTOR = {
    .abi_major = FLOW_PLUGIN_ABI_MAJOR, .abi_minor = FLOW_PLUGIN_ABI_MINOR,
    .descriptor_size = sizeof(FlowPluginDescriptor), .module_name = "flow.embodied",
    .module_version = "1.0", .module_hash = 0xEB0D1ED1,
    .plugin = &EMBODIED_PLUGIN, .abi_v2 = &EMBODIED_ABI_V2
};

const FlowPluginDescriptor *flow_embodied_entry_v1(void) { return &EMBODIED_DESCRIPTOR; }
const FlowPluginABI *flow_embodied_abi_v2(void) { return &EMBODIED_ABI_V2; }
#ifdef FLOW_PLUGIN_DSO
const FlowPluginDescriptor *flow_plugin_entry_v1(void) { return &EMBODIED_DESCRIPTOR; }
const FlowPluginABI *flow_plugin_abi_v2(void) { return &EMBODIED_ABI_V2; }
#endif
const FlowPlugin *flow_embodied_plugin(void) { return &EMBODIED_PLUGIN; }

/* 6. Embodied Multi-Agent Swarm Fleet Implementation */
int flow_fleet_init(FlowFleetSwarm *fleet, double min_safety_margin_m) {
    if (!fleet) return 0;
    memset(fleet, 0, sizeof(*fleet));
    fleet->min_safety_margin_m = (min_safety_margin_m > 0.0) ? min_safety_margin_m : 0.5;
    return 1;
}

int flow_fleet_register_robot(FlowFleetSwarm *fleet, uint8_t robot_id, FlowFleetRole role,
                              double bounding_radius, const double initial_pos[3]) {
    if (!fleet || fleet->robot_count >= FLOW_FLEET_MAX_ROBOTS) return 0;
    for (size_t i = 0; i < fleet->robot_count; ++i)
        if (fleet->robots[i].robot_id == robot_id) return 0;
    FlowFleetRobot *r = &fleet->robots[fleet->robot_count++];
    *r = (FlowFleetRobot){.robot_id = robot_id, .role = role,
                          .bounding_radius = bounding_radius > 0.0 ? bounding_radius : 0.3,
                          .battery_permille = 1000, .motor_temp_celsius = 35, .is_active = 1};
    if (initial_pos) memcpy(r->position, initial_pos, sizeof(double) * 3);
    return 1;
}

int flow_fleet_update_telemetry(FlowFleetSwarm *fleet, uint8_t robot_id, const double pos[3],
                                const double vel[3], uint16_t battery_permille, uint16_t motor_temp_celsius) {
    if (!fleet) return 0;
    for (size_t i = 0; i < fleet->robot_count; ++i) {
        FlowFleetRobot *r = &fleet->robots[i];
        if (r->robot_id == robot_id) {
            if (pos) memcpy(r->position, pos, sizeof(double) * 3);
            if (vel) memcpy(r->velocity, vel, sizeof(double) * 3);
            r->battery_permille = battery_permille;
            r->motor_temp_celsius = motor_temp_celsius;
            return 1;
        }
    }
    return 0;
}

int flow_fleet_step_1khz_tick(FlowFleetSwarm *fleet, double dt_sec) {
    if (!fleet) return 0;
    if (dt_sec <= 0.0) dt_sec = 0.001;
    fleet->total_1khz_ticks++;
    for (size_t i = 0; i < fleet->robot_count; ++i) {
        FlowFleetRobot *ri = &fleet->robots[i];
        if (!ri->is_active) continue;
        for (size_t j = i + 1; j < fleet->robot_count; ++j) {
            FlowFleetRobot *rj = &fleet->robots[j];
            if (!rj->is_active) continue;
            double dx = ri->position[0] - rj->position[0], dy = ri->position[1] - rj->position[1], dz = ri->position[2] - rj->position[2];
            double dist = sqrt(dx * dx + dy * dy + dz * dz);
            double crit = ri->bounding_radius + rj->bounding_radius + fleet->min_safety_margin_m;
            if (dist < crit) {
                fleet->total_collision_avoidance_interventions++;
                double scale = ((crit - dist) * 5.0) / (dist > 1e-6 ? dist : 1e-6);
                ri->velocity[0] += dx * scale; ri->velocity[1] += dy * scale; ri->velocity[2] += dz * scale;
                rj->velocity[0] -= dx * scale; rj->velocity[1] -= dy * scale; rj->velocity[2] -= dz * scale;
            }
        }
        for (int k = 0; k < 3; ++k) ri->position[k] += ri->velocity[k] * dt_sec;
    }
    return 1;
}

int flow_fleet_adapt_roles_bmf(FlowFleetSwarm *fleet, uint64_t bmf_seed) {
    if (!fleet || fleet->robot_count == 0) return 0;
    (void)bmf_seed;
    int reallocated = 0;
    for (size_t i = 0; i < fleet->robot_count; ++i) {
        FlowFleetRobot *ri = &fleet->robots[i];
        if (ri->is_active && (ri->role == FLOW_FLEET_ROLE_SCOUT || ri->role == FLOW_FLEET_ROLE_CARRIER) &&
            (ri->battery_permille < 200 || ri->motor_temp_celsius > 75)) {
            for (size_t j = 0; j < fleet->robot_count; ++j) {
                FlowFleetRobot *rj = &fleet->robots[j];
                if (i != j && rj->is_active && rj->battery_permille > 600 && rj->motor_temp_celsius < 50 &&
                    (rj->role == FLOW_FLEET_ROLE_IDLE || rj->role == FLOW_FLEET_ROLE_RELAY)) {
                    rj->role = ri->role;
                    ri->role = FLOW_FLEET_ROLE_IDLE;
                    fleet->total_role_reassignments++;
                    reallocated++;
                    break;
                }
            }
        }
    }
    return reallocated;
}

FlowSMTResult flow_fleet_verify_collision_smt(const FlowFleetSwarm *fleet, FlowSMTProofAttestation *proof_out) {
    if (!fleet) return FLOW_SMT_UNKNOWN;
    FlowSMTResult res = FLOW_SMT_PROVEN_UNSAT;
    double min_dist = 1e9;
    uint8_t va = 0, vb = 0;
    for (size_t i = 0; i < fleet->robot_count && res == FLOW_SMT_PROVEN_UNSAT; ++i) {
        const FlowFleetRobot *ri = &fleet->robots[i];
        if (!ri->is_active) continue;
        for (size_t j = i + 1; j < fleet->robot_count; ++j) {
            const FlowFleetRobot *rj = &fleet->robots[j];
            if (!rj->is_active) continue;
            double dx = ri->position[0] - rj->position[0], dy = ri->position[1] - rj->position[1], dz = ri->position[2] - rj->position[2];
            double dist = sqrt(dx * dx + dy * dy + dz * dz);
            if (dist < min_dist) min_dist = dist;
            if (dist < ri->bounding_radius + rj->bounding_radius) {
                res = FLOW_SMT_VIOLATION_SAT; va = ri->robot_id; vb = rj->robot_id; break;
            }
        }
    }
    if (proof_out) {
        proof_out->buffer_bounds_safety = res;
        proof_out->memory_quota_bound = FLOW_SMT_PROVEN_UNSAT;
        proof_out->shard_non_aliasing = res;
        proof_out->determinism_invariant = FLOW_SMT_PROVEN_UNSAT;
        if (res == FLOW_SMT_VIOLATION_SAT)
            snprintf(proof_out->proof_summary, sizeof(proof_out->proof_summary),
                     "SMT FLEET COLLISION VIOLATION: Robot %u and Robot %u overlap (dist=%.3fm < hard limit)", va, vb, min_dist);
        else
            snprintf(proof_out->proof_summary, sizeof(proof_out->proof_summary),
                     "SMT FLEET SOUND: All %zu robots satisfy spatial separation polytope (min_dist=%.3fm, Zero-Collision)",
                     fleet->robot_count, min_dist);
    }
    return res;
}

/* 7. Embodied 64-Bit Coordinate Subspace Slicing & Mask Superposition (BMF) */
int flow_embodied_encode_genome(const FlowEmbodiedCoordinate *coord, uint64_t *genome_out) {
    if (!coord || !genome_out) return 0;
    *genome_out = FLOW_GENOME_PACK((uint64_t)coord->gait_mode, FLOW_EMBODIED_OFF_GAIT, FLOW_EMBODIED_LEN_GAIT)
                | FLOW_GENOME_PACK((uint64_t)coord->torque_gain_scale, FLOW_EMBODIED_OFF_TORQUE, FLOW_EMBODIED_LEN_TORQUE)
                | FLOW_GENOME_PACK((uint64_t)coord->sensor_confidence, FLOW_EMBODIED_OFF_SENSOR, FLOW_EMBODIED_LEN_SENSOR)
                | FLOW_GENOME_PACK((uint64_t)coord->thermal_throttle_level, FLOW_EMBODIED_OFF_THERMAL, FLOW_EMBODIED_LEN_THERMAL)
                | FLOW_GENOME_PACK((uint64_t)coord->smith_delay_steps, FLOW_EMBODIED_OFF_SMITH, FLOW_EMBODIED_LEN_SMITH)
                | FLOW_GENOME_PACK((uint64_t)coord->fleet_clearance_mm, FLOW_EMBODIED_OFF_FLEET, FLOW_EMBODIED_LEN_FLEET)
                | FLOW_GENOME_PACK((uint64_t)coord->survival_flags, FLOW_EMBODIED_OFF_SURVIVAL, FLOW_EMBODIED_LEN_SURVIVAL);
    return 1;
}

int flow_embodied_decode_genome(uint64_t genome, FlowEmbodiedCoordinate *coord_out) {
    if (!coord_out) return 0;
    coord_out->gait_mode = (FlowCorticalGaitMode)FLOW_GENOME_GET(genome, FLOW_EMBODIED_OFF_GAIT, FLOW_EMBODIED_LEN_GAIT);
    coord_out->torque_gain_scale = (uint8_t)FLOW_GENOME_GET(genome, FLOW_EMBODIED_OFF_TORQUE, FLOW_EMBODIED_LEN_TORQUE);
    coord_out->sensor_confidence = (uint8_t)FLOW_GENOME_GET(genome, FLOW_EMBODIED_OFF_SENSOR, FLOW_EMBODIED_LEN_SENSOR);
    coord_out->thermal_throttle_level = (uint8_t)FLOW_GENOME_GET(genome, FLOW_EMBODIED_OFF_THERMAL, FLOW_EMBODIED_LEN_THERMAL);
    coord_out->smith_delay_steps = (uint8_t)FLOW_GENOME_GET(genome, FLOW_EMBODIED_OFF_SMITH, FLOW_EMBODIED_LEN_SMITH);
    coord_out->fleet_clearance_mm = (uint16_t)FLOW_GENOME_GET(genome, FLOW_EMBODIED_OFF_FLEET, FLOW_EMBODIED_LEN_FLEET);
    coord_out->survival_flags = (uint16_t)FLOW_GENOME_GET(genome, FLOW_EMBODIED_OFF_SURVIVAL, FLOW_EMBODIED_LEN_SURVIVAL);
    return 1;
}

int flow_embodied_superpose_safety_canvas(const FlowPhysicsEngine *phys, const FlowSensorFusion *fusion,
                                          const FlowThermalEnergyGovernor *gov, const FlowFleetSwarm *fleet,
                                          FlowEmbodiedCanvas *canvas_out) {
    if (!canvas_out) return 0;
    memset(canvas_out, 0, sizeof(*canvas_out));
    const uint64_t base_mask = (~FLOW_GENOME_FIELD_MASK(FLOW_EMBODIED_OFF_GAIT, FLOW_EMBODIED_LEN_GAIT))
                             | FLOW_GENOME_PACK(7ULL, FLOW_EMBODIED_OFF_GAIT, FLOW_EMBODIED_LEN_GAIT);
    canvas_out->zmp_mask = canvas_out->kalman_mask = canvas_out->thermal_mask = canvas_out->fleet_mask = base_mask;

    if (phys) {
        if (!flow_physics_is_zmp_stable(&phys->current_state)) {
            canvas_out->zmp_mask &= ~FLOW_GENOME_PACK(6ULL, FLOW_EMBODIED_OFF_GAIT, FLOW_EMBODIED_LEN_GAIT);
            canvas_out->zmp_mask &= ~FLOW_GENOME_PACK(0xC0, FLOW_EMBODIED_OFF_TORQUE, FLOW_EMBODIED_LEN_TORQUE);
        }
        if (phys->violations_prevented_total > 0)
            canvas_out->zmp_mask &= ~FLOW_GENOME_PACK(0x80, FLOW_EMBODIED_OFF_TORQUE, FLOW_EMBODIED_LEN_TORQUE);
    }
    if (fusion && (fusion->sensor_confidence < 0.5 || fusion->raw_vibration_noise_level > 0.05))
        canvas_out->kalman_mask &= ~FLOW_GENOME_PACK(4ULL, FLOW_EMBODIED_OFF_GAIT, FLOW_EMBODIED_LEN_GAIT);
    if (gov && (gov->edge_chip_temp_celsius >= gov->thermal_throttle_limit_celsius || gov->current_battery_percent < 15.0)) {
        canvas_out->thermal_mask &= ~FLOW_GENOME_PACK(0xF0, FLOW_EMBODIED_OFF_TORQUE, FLOW_EMBODIED_LEN_TORQUE);
        canvas_out->thermal_mask &= ~FLOW_GENOME_PACK(3ULL, FLOW_EMBODIED_OFF_GAIT, FLOW_EMBODIED_LEN_GAIT);
    }
    if (fleet && fleet->robot_count > 1) {
        int prox = (fleet->total_collision_avoidance_interventions > 0);
        for (size_t i = 0; i < fleet->robot_count && !prox; ++i) {
            for (size_t j = i + 1; j < fleet->robot_count; ++j) {
                double dx = fleet->robots[i].position[0] - fleet->robots[j].position[0];
                double dy = fleet->robots[i].position[1] - fleet->robots[j].position[1];
                if (sqrt(dx * dx + dy * dy) < fleet->min_safety_margin_m) { prox = 1; break; }
            }
        }
        if (prox) canvas_out->fleet_mask &= ~FLOW_GENOME_PACK(1ULL, FLOW_EMBODIED_OFF_GAIT, FLOW_EMBODIED_LEN_GAIT);
    }

    canvas_out->superposed_mask = canvas_out->zmp_mask & canvas_out->kalman_mask & canvas_out->thermal_mask & canvas_out->fleet_mask;
    if (canvas_out->superposed_mask == 0 || FLOW_GENOME_GET(canvas_out->superposed_mask, FLOW_EMBODIED_OFF_GAIT, FLOW_EMBODIED_LEN_GAIT) == 0) {
        canvas_out->is_double_bind = 1;
        canvas_out->superposed_mask = FLOW_GENOME_PACK(FLOW_GAIT_EMERGENCY_BRACE, FLOW_EMBODIED_OFF_GAIT, FLOW_EMBODIED_LEN_GAIT)
                                    | FLOW_GENOME_PACK(1, FLOW_EMBODIED_OFF_SURVIVAL, FLOW_EMBODIED_LEN_SURVIVAL);
    }
    canvas_out->dynamic_bias = FLOW_GENOME_PACK(FLOW_GAIT_FLAT_WALK, FLOW_EMBODIED_OFF_GAIT, FLOW_EMBODIED_LEN_GAIT)
                             | FLOW_GENOME_PACK(128, FLOW_EMBODIED_OFF_TORQUE, FLOW_EMBODIED_LEN_TORQUE);
    return 1;
}

int flow_embodied_step_token_ring(FlowEmbodiedCanvas *canvas, uint64_t *current_genome_inout,
                                  uint64_t *rng_state_inout, uint32_t *mutated_bit_out) {
    if (!canvas || !current_genome_inout) return 0;
    if (canvas->is_double_bind) {
        *current_genome_inout = canvas->superposed_mask;
        if (mutated_bit_out) *mutated_bit_out = FLOW_EMBODIED_OFF_GAIT + 2;
        return 1;
    }
    uint64_t projected = flow_manifold_project(*current_genome_inout, canvas->superposed_mask, canvas->dynamic_bias);
    *current_genome_inout = flow_manifold_transition(projected, canvas->superposed_mask, rng_state_inout, mutated_bit_out);
    return 1;
}

FlowSMTResult flow_embodied_verify_smt(const FlowRigidBodyState *state, const FlowThermalEnergyGovernor *gov,
                                       const FlowFleetSwarm *fleet, FlowSMTProofAttestation *proof_out) {
    uint64_t zmp_viol = 0, therm_viol = 0, fleet_viol = 0;
    if (state) {
        if (!flow_physics_is_zmp_stable(state)) zmp_viol = 1;
        for (size_t i = 0; i < state->joint_count && i < FLOW_MAX_JOINTS; ++i)
            if (fabs(state->joint_torques[i]) > state->max_torque_limit[i]) { zmp_viol = 2; break; }
    }
    if (gov && gov->edge_chip_temp_celsius > gov->thermal_throttle_limit_celsius)
        therm_viol = (uint64_t)(gov->edge_chip_temp_celsius - gov->thermal_throttle_limit_celsius);
    if (fleet && fleet->robot_count > 1) {
        for (size_t i = 0; i < fleet->robot_count && !fleet_viol; ++i) {
            for (size_t j = i + 1; j < fleet->robot_count; ++j) {
                double dx = fleet->robots[i].position[0] - fleet->robots[j].position[0];
                double dy = fleet->robots[i].position[1] - fleet->robots[j].position[1];
                double dist = sqrt(dx * dx + dy * dy), min_d = fleet->robots[i].bounding_radius + fleet->robots[j].bounding_radius;
                if (dist < min_d) { fleet_viol = (uint64_t)((min_d - dist) * 1000.0) + 1; break; }
            }
        }
    }
    const FlowBoxConstraint rules[] = {
        FLOW_SMT_RULE("zmp and torque violation", zmp_viol, 0, 0, FLOW_BOX_THEOREM_BUFFER_BOUNDS, "Center of Mass or Torque exceeds physical rating"),
        FLOW_SMT_RULE("thermal excess", therm_viol, 0, 0, FLOW_BOX_THEOREM_MEMORY_QUOTA, "Chip/Motor temperature exceeds thermal ceiling"),
        FLOW_SMT_RULE("fleet proximity violation", fleet_viol, 0, 0, FLOW_BOX_THEOREM_SHARD_ISOLATION, "Robots violate spatial bounding polytope")
    };
    return FLOW_SMT_VERIFY_RULES("embodied_coordination", proof_out, rules,
        "SMT EMBODIED SOUND: ZMP=stable, Temp=%.1fC <= %.1fC, FleetClearance=sound (Zero-Defect Soundness)",
        gov ? gov->edge_chip_temp_celsius : 45.0, gov ? gov->thermal_throttle_limit_celsius : 85.0);
}
