#ifndef FLOW_EMBODIED_H
#define FLOW_EMBODIED_H

#include "flow.h"
#include "bitspace.h"
#include "reload.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define FLOW_MAX_JOINTS 16
#define FLOW_SUPPORT_POLYGON_VERTICES 8

/* ========================================================================= */
/* 1. Micro-Physics Simulation Safety Gate (Sim-to-Real Verifier)            */
/* ========================================================================= */

typedef struct {
    double joint_angles[FLOW_MAX_JOINTS];       /* rad */
    double joint_velocities[FLOW_MAX_JOINTS];   /* rad/s */
    double joint_torques[FLOW_MAX_JOINTS];      /* N*m */
    double max_torque_limit[FLOW_MAX_JOINTS];   /* N*m max physical rating */
    size_t joint_count;
    double center_of_mass[3];                   /* [x, y, z] meters */
    double zmp_position[2];                     /* [x, y] zero moment point */
    double support_polygon[FLOW_SUPPORT_POLYGON_VERTICES][2];
    size_t support_vertex_count;
    double mass_kg;
    double dt_seconds;
} FlowRigidBodyState;

typedef struct {
    FlowRigidBodyState current_state;
    double max_angular_accel;                   /* rad/s^2 */
    double friction_coefficient;                /* mu */
    uint64_t simulated_steps_total;
    uint64_t violations_prevented_total;
} FlowPhysicsEngine;

int flow_physics_init(FlowPhysicsEngine *engine, size_t joint_count, double mass_kg);
int flow_physics_simulate_step(FlowPhysicsEngine *engine, const double *applied_torques);
bool flow_physics_is_zmp_stable(const FlowRigidBodyState *state);
bool flow_physics_is_torque_safe(const FlowRigidBodyState *state, const double *candidate_torques);
uint64_t flow_physics_get_safety_mask(const FlowPhysicsEngine *engine,
                                      const FlowPlanDimensionSet *dims);

/* ========================================================================= */
/* 2. Dual-Rate Frequency Separation (Spinal Reflex vs Cortical Reconfig)    */
/* ========================================================================= */

typedef struct {
    double kp[FLOW_MAX_JOINTS];
    double kd[FLOW_MAX_JOINTS];
    double ki[FLOW_MAX_JOINTS];
    double integral_error[FLOW_MAX_JOINTS];
    double prev_error[FLOW_MAX_JOINTS];
    double feedforward_torque[FLOW_MAX_JOINTS];
} FlowSpinalReflexUnit;

typedef enum {
    FLOW_GAIT_IDLE = 0,
    FLOW_GAIT_FLAT_WALK = 1,
    FLOW_GAIT_STAIR_CLIMB = 2,
    FLOW_GAIT_ROUGH_TERRAIN = 3,
    FLOW_GAIT_EMERGENCY_BRACE = 4
} FlowCorticalGaitMode;

typedef struct {
    FlowSpinalReflexUnit spinal;               /* 1kHz ~ 10kHz ultra-fast reflex loop */
    FlowCorticalGaitMode cortical_mode;        /* 1Hz ~ 10Hz macro JIT reconfig */
    double transition_alpha;                   /* Smooth trajectory interpolation [0.0 .. 1.0] */
    uint64_t spinal_ticks_total;
    uint64_t cortical_swaps_total;
    const FlowUnit *current_cortical_unit;
} FlowDualRateController;

int flow_dual_rate_init(FlowDualRateController *ctrl, size_t joint_count);
int flow_dual_rate_spinal_tick(FlowDualRateController *ctrl,
                              const double *target_angles,
                              const double *current_angles,
                              const double *current_vels,
                              double *output_torques,
                              double dt);
int flow_dual_rate_cortical_reconfigure(FlowDualRateController *ctrl,
                                        FlowCorticalGaitMode new_mode,
                                        const FlowUnit *new_unit);

/* ========================================================================= */
/* 3. Sensor Fusion & Kalman Filter Mask Canvas                              */
/* ========================================================================= */

typedef struct {
    double state_estimate;                     /* True filtered value */
    double error_covariance;                   /* P */
    double process_noise_q;                    /* Q */
    double measurement_noise_r;                /* R */
    double kalman_gain_k;                      /* K */
} FlowKalmanFilter1D;

typedef struct {
    FlowKalmanFilter1D pitch_filter;
    FlowKalmanFilter1D roll_filter;
    FlowKalmanFilter1D accel_z_filter;
    double raw_vibration_noise_level;
    double sensor_confidence;                  /* [0.0 .. 1.0] */
    uint64_t updates_total;
    uint64_t rejected_noise_spikes;
} FlowSensorFusion;

int flow_sensor_fusion_init(FlowSensorFusion *fusion);
int flow_sensor_fusion_update_imu(FlowSensorFusion *fusion,
                                  double raw_pitch,
                                  double raw_roll,
                                  double raw_accel_z,
                                  double *clean_pitch_out,
                                  double *clean_roll_out,
                                  double *clean_accel_z_out);
uint64_t flow_sensor_fusion_get_clean_mask(const FlowSensorFusion *fusion,
                                           const FlowPlanDimensionSet *dims);

/* ========================================================================= */
/* 4. Thermodynamic Energy Throttling & Event-Driven Wakeup                  */
/* ========================================================================= */

typedef struct {
    double current_battery_percent;            /* 0.0 .. 100.0 */
    double edge_chip_temp_celsius;             /* e.g. Nvidia Jetson temperature */
    double thermal_throttle_limit_celsius;     /* e.g. 85.0 C */
    double current_power_draw_watts;
    double steady_state_mass_baseline_kg;
    double disturbance_threshold_kg;           /* e.g. 5.0 kg surprise load */
    bool is_chaotic_engine_sleeping;
    uint64_t sleep_cycles_total;
    uint64_t shock_wakeups_total;
} FlowThermalEnergyGovernor;

int flow_energy_governor_init(FlowThermalEnergyGovernor *gov,
                              double mass_baseline_kg,
                              double disturbance_threshold_kg);
bool flow_energy_governor_check_wakeup(FlowThermalEnergyGovernor *gov,
                                       double observed_mass_kg,
                                       double external_impact_force_n);
double flow_energy_governor_compute_objective_penalty(const FlowThermalEnergyGovernor *gov,
                                                      double base_latency_score,
                                                      double compute_energy_joules);

#endif
