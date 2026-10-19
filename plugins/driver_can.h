#ifndef FLOW_DRIVER_CAN_H
#define FLOW_DRIVER_CAN_H

#include "primitive.h"
#include "smt.h"
#include "flow_smt_dsl.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW SocketCAN / CAN-FD Real-Time Hardware Primitive Driver (driver_can.h)
 * ============================================================================
 * Exposes real-time robot motor control bus capabilities to FLOW's 1kHz
 * spinal reflex loop and SMT formal verification engine.
 *
 * Supported Protocols:
 * - Standard CAN 2.0B (11-bit / 29-bit, up to 1 Mbps, 8-byte payload)
 * - CAN-FD (Flexible Data-Rate, up to 5 Mbps data phase, 64-byte payload)
 * - MIT Impedance Control Protocol (tau = tau_ff + kp*(p_des - p) + kd*(v_des - v))
 * - Unitree / CyberGear / Robomaster servo actuators
 * ============================================================================
 */

#define FLOW_CAN_MAX_DLEN 64
#define FLOW_CAN_EFF_FLAG 0x80000000U /* Extended frame format (29-bit) */
#define FLOW_CAN_RTR_FLAG 0x40000000U /* Remote transmission request */
#define FLOW_CAN_ERR_FLAG 0x20000000U /* Error message frame */
#define FLOW_CAN_SFF_MASK 0x000007FFU /* Standard frame format (11-bit) */
#define FLOW_CAN_EFF_MASK 0x1FFFFFFFU /* Extended frame format mask */

#define FLOW_CANFD_BRS    0x01        /* Bit rate switch */
#define FLOW_CANFD_ESI    0x02        /* Error state indicator */

/* Wire format frame matching Linux can_frame / canfd_frame */
typedef struct {
    uint32_t can_id;                  /* 11 or 29 bit identifier + flags */
    uint8_t  len;                     /* Data length: 0..8 (classic), 0..64 (CAN-FD) */
    uint8_t  flags;                   /* FLOW_CANFD_* flags */
    uint8_t  res0;
    uint8_t  res1;
    uint8_t  data[FLOW_CAN_MAX_DLEN]; /* Payload bytes */
} FlowCANFrame;

/* Motor Control Modes */
typedef enum {
    FLOW_MOTOR_MODE_IDLE = 0,
    FLOW_MOTOR_MODE_TORQUE = 1,
    FLOW_MOTOR_MODE_VELOCITY = 2,
    FLOW_MOTOR_MODE_POSITION = 3,
    FLOW_MOTOR_MODE_MIT_HYBRID = 4,    /* tau = tau_ff + kp*(p_des - p) + kd*(v_des - v) */
    FLOW_MOTOR_MODE_EMERGENCY_STOP = 5
} FlowMotorControlMode;

/* High-level Motor Command (Dispatched over CAN to Actuator) */
typedef struct {
    uint8_t motor_id;
    FlowMotorControlMode mode;
    float target_position_rad;        /* Target angle in radians [-4*PI .. 4*PI] */
    float target_velocity_rad_s;      /* Target angular velocity [-100.0 .. 100.0] */
    float target_torque_nm;           /* Feedforward torque [-120.0 .. 120.0] */
    float kp;                         /* Stiffness [0.0 .. 500.0] */
    float kd;                         /* Damping [0.0 .. 20.0] */
} FlowMotorCommand;

/* High-level Motor Feedback (Reported over CAN from Actuator) */
typedef struct {
    uint8_t motor_id;
    float actual_position_rad;
    float actual_velocity_rad_s;
    float actual_torque_nm;
    float motor_temp_celsius;
    uint16_t fault_flags;
} FlowMotorFeedback;

/* Frame Serialization & Deserialization */
int flow_can_encode_motor_cmd(const FlowMotorCommand *cmd, FlowCANFrame *frame_out);
int flow_can_decode_motor_cmd(const FlowCANFrame *frame, FlowMotorCommand *cmd_out);

int flow_can_encode_motor_feedback(const FlowMotorFeedback *fb, FlowCANFrame *frame_out);
int flow_can_decode_motor_feedback(const FlowCANFrame *frame, FlowMotorFeedback *fb_out);

/* CAN Bus Transceiver / Hardware Session */
typedef struct FlowCANBus FlowCANBus;

/*
 * Open SocketCAN on Linux (e.g. "can0", "vcan0") or high-speed loopback on non-Linux
 */
FlowCANBus *flow_can_open(const char *interface_name, int use_fd, int bitrate_kbps);
int flow_can_send(FlowCANBus *bus, const FlowCANFrame *frame);
int flow_can_recv(FlowCANBus *bus, FlowCANFrame *frame_out, int timeout_ms);
void flow_can_close(FlowCANBus *bus);

/* Create in-memory hardware-accurate loopback pair for testing / simulation */
int flow_can_create_loopback_pair(FlowCANBus **bus_a_out, FlowCANBus **bus_b_out);

/* 3-Function Primitive Driver Interface */
const FlowPrimitiveDriver *flow_primitive_can_driver(void);

/*
 * SMT Formal Real-Time Arbitration & Non-Interference Proof:
 * Proves that an emergency stop frame (e.g. ID 0x001) will strictly preempt
 * lower priority telemetry frames (e.g. ID 0x700), with worst-case arbitration
 * delay guaranteed strictly less than max_allowed_latency_us (e.g. <= 120 us).
 */
FlowSMTResult flow_can_verify_arbitration_smt(uint32_t emergency_id,
                                             uint32_t background_id,
                                             uint32_t bus_bitrate_kbps,
                                             double max_allowed_latency_us,
                                             FlowSMTProofAttestation *proof_out);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_DRIVER_CAN_H */
