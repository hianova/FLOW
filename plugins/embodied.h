#ifndef FLOW_EMBODIED_H
#define FLOW_EMBODIED_H
#include "flow.h"
#include "plugin.h"
#include "flow_jet.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#define FLOW_MAX_JOINTS                  16
#define FLOW_SUPPORT_POLYGON_VERTICES    8
#define FLOW_PHYSICS_GRAVITY_MPS2        9.81
#define FLOW_EMBODIED_DEFAULT_JOINTS     6U
#define FLOW_EMBODIED_DEFAULT_MASS_KG    25.0
#define FLOW_EMBODIED_DEFAULT_DT_SEC     0.001
#define FLOW_EMBODIED_DEFAULT_MAX_ACCEL  50.0
#define FLOW_EMBODIED_DEFAULT_MAX_TORQUE 80.0
#define FLOW_EMBODIED_DEFAULT_COM_Z      0.65
typedef struct {
    double joint_angles[FLOW_MAX_JOINTS];
    double joint_velocities[FLOW_MAX_JOINTS];
    double joint_torques[FLOW_MAX_JOINTS];
    double max_torque_limit[FLOW_MAX_JOINTS];
    size_t joint_count;
    double center_of_mass[3];
    double zmp_position[2];
    double support_polygon[FLOW_SUPPORT_POLYGON_VERTICES][2];
    size_t support_vertex_count;
    double mass_kg;
    double dt_seconds;
} FlowRigidBodyState;
typedef struct {
    FlowRigidBodyState current_state;
    double max_angular_accel;
    double friction_coefficient;
    uint64_t simulated_steps_total;
    uint64_t violations_prevented_total;
} FlowPhysicsEngine;
int flow_physics_init(FlowPhysicsEngine *engine, size_t joint_count, double mass_kg);
int flow_physics_simulate_step(FlowPhysicsEngine *engine, const double *applied_torques);
bool flow_physics_is_zmp_stable(const FlowRigidBodyState *state);
bool flow_physics_is_torque_safe(const FlowRigidBodyState *state, const double *candidate_torques);
const FlowPluginDescriptor *flow_embodied_entry_v1(void);

/* ============================================================================
 * 10kHz Mori-Zwanzig Non-Markovian Shock & Impedance Controller
 * ============================================================================ */
typedef struct {
    size_t joint_count;
    double kp[FLOW_MAX_JOINTS];
    double kd[FLOW_MAX_JOINTS];
    double memory_taps[FLOW_JET_MAX_TAPS];
    size_t tap_count;
    double history_velocities[FLOW_JET_MAX_TAPS][FLOW_MAX_JOINTS];
    size_t history_head;
    double max_torque_limit[FLOW_MAX_JOINTS];
    uint64_t total_10khz_ticks;
    uint64_t collision_shocks_absorbed;
    double peak_contact_torque;
    double dissipated_energy_joules;
    bool is_passivity_maintained;
} FlowMoriZwanzigImpedanceController;

int flow_embodied_mz_init(FlowMoriZwanzigImpedanceController *ctrl, size_t joint_count,
                          const double memory_kernel[], size_t tap_count);
int flow_embodied_mz_step_10khz(FlowMoriZwanzigImpedanceController *ctrl, const double current_q[],
                                const double current_v[], const double target_q[],
                                const double target_v[], double torques_out[], double dt);
int flow_embodied_mz_simulate_impact(FlowMoriZwanzigImpedanceController *ctrl,
                                     double impact_impulse_ns, double *settling_time_ms_out,
                                     double *peak_torque_out);
FlowSMTResult flow_embodied_mz_verify_smt(const FlowMoriZwanzigImpedanceController *ctrl,
                                          FlowSMTProofAttestation *proof_out);

/* ============================================================================
 * 6-DoF IMU Attitude Filter & Hardware Primitive Driver
 * ============================================================================ */
typedef struct {
    float accel_m_s2[3], gyro_rad_s[3], mag_uT[3], temp_celsius;
    uint64_t timestamp_ns;
    uint32_t sequence;
} FlowIMUSample;
typedef struct {
    float roll_rad, pitch_rad, yaw_rad, alpha;
    uint64_t samples_processed;
    uint32_t drift_spikes_rejected;
} FlowIMUAttitudeFilter;
void flow_imu_filter_init(FlowIMUAttitudeFilter *filter, float alpha);
void flow_imu_filter_update(FlowIMUAttitudeFilter *filter, const FlowIMUSample *sample,
                            float dt_seconds);
const FlowPrimitiveDriver *flow_primitive_imu_driver(void);
FlowSMTResult flow_imu_verify_bounds_smt(const FlowIMUSample *sample,
                                         const FlowIMUAttitudeFilter *filter,
                                         FlowSMTProofAttestation *proof_out);

/* ============================================================================
 * SocketCAN / CAN-FD Real-Time Motor Control Bus Driver
 * ============================================================================ */
#define FLOW_CAN_MAX_DLEN 64
#define FLOW_CAN_EFF_FLAG 0x80000000U
#define FLOW_CAN_RTR_FLAG 0x40000000U
#define FLOW_CAN_ERR_FLAG 0x20000000U
#define FLOW_CAN_SFF_MASK 0x000007FFU
#define FLOW_CAN_EFF_MASK 0x1FFFFFFFU
#define FLOW_CANFD_BRS    0x01
#define FLOW_CANFD_ESI    0x02

typedef struct {
    uint32_t can_id;
    uint8_t len, flags, res0, res1;
    uint8_t data[FLOW_CAN_MAX_DLEN];
} FlowCANFrame;

typedef enum {
    FLOW_MOTOR_MODE_IDLE = 0,
    FLOW_MOTOR_MODE_TORQUE = 1,
    FLOW_MOTOR_MODE_VELOCITY = 2,
    FLOW_MOTOR_MODE_POSITION = 3,
    FLOW_MOTOR_MODE_MIT_HYBRID = 4,
    FLOW_MOTOR_MODE_EMERGENCY_STOP = 5
} FlowMotorControlMode;

typedef struct {
    uint8_t motor_id;
    FlowMotorControlMode mode;
    float target_position_rad, target_velocity_rad_s, target_torque_nm, kp, kd;
} FlowMotorCommand;

typedef struct {
    uint8_t motor_id;
    float actual_position_rad, actual_velocity_rad_s, actual_torque_nm, motor_temp_celsius;
    uint16_t fault_flags;
} FlowMotorFeedback;

int flow_can_encode_motor_cmd(const FlowMotorCommand *cmd, FlowCANFrame *frame_out);
int flow_can_decode_motor_cmd(const FlowCANFrame *frame, FlowMotorCommand *cmd_out);
int flow_can_encode_motor_feedback(const FlowMotorFeedback *fb, FlowCANFrame *frame_out);
int flow_can_decode_motor_feedback(const FlowCANFrame *frame, FlowMotorFeedback *fb_out);
typedef struct FlowCANBus FlowCANBus;
FlowCANBus *flow_can_open(const char *interface_name, int use_fd, int bitrate_kbps);
int flow_can_send(FlowCANBus *bus, const FlowCANFrame *frame);
int flow_can_recv(FlowCANBus *bus, FlowCANFrame *frame_out, int timeout_ms);
void flow_can_close(FlowCANBus *bus);
int flow_can_create_loopback_pair(FlowCANBus **bus_a_out, FlowCANBus **bus_b_out);
const FlowPrimitiveDriver *flow_primitive_can_driver(void);
FlowSMTResult flow_can_verify_arbitration_smt(uint32_t emergency_id, uint32_t background_id,
                                              uint32_t bus_bitrate_kbps,
                                              double max_allowed_latency_us,
                                              FlowSMTProofAttestation *proof_out);
#endif
