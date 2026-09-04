#ifndef FLOW_DRIVER_IMU_H
#define FLOW_DRIVER_IMU_H

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
 * FLOW 6-DOF / 9-DOF IMU Sensor Stream Primitive Driver (driver_imu.h)
 * ============================================================================
 * Provides high-frequency (1kHz ~ 10kHz) inertial measurement streaming,
 * zero-copy ring buffering, complementary attitude estimation, and SMT
 * sensor bound verification for FLOW's embodied spinal reflexes.
 * ============================================================================
 */

typedef struct {
    float accel_m_s2[3];              /* Linear acceleration [ax, ay, az] in m/s^2 */
    float gyro_rad_s[3];              /* Angular velocity [gx, gy, gz] in rad/s */
    float mag_uT[3];                  /* Magnetic field [mx, my, mz] in micro-Tesla */
    float temp_celsius;               /* Sensor die temperature */
    uint64_t timestamp_ns;            /* Monotonic hardware timestamp */
    uint32_t sequence;                /* Frame counter for packet loss detection */
} FlowIMUSample;

typedef struct {
    float roll_rad;                   /* Filtered roll angle in radians */
    float pitch_rad;                  /* Filtered pitch angle in radians */
    float yaw_rad;                    /* Filtered yaw angle in radians */
    float alpha;                      /* Complementary filter weight (e.g. 0.98) */
    uint64_t samples_processed;
    uint32_t drift_spikes_rejected;
} FlowIMUAttitudeFilter;

/* Attitude Filter Lifecycle */
void flow_imu_filter_init(FlowIMUAttitudeFilter *filter, float alpha);
void flow_imu_filter_update(FlowIMUAttitudeFilter *filter, const FlowIMUSample *sample, float dt_seconds);

/* 3-Function Primitive Driver Interface */
const FlowPrimitiveDriver *flow_primitive_imu_driver(void);

/*
 * SMT Formal Sensor Range & Attitude Boundedness Theorem:
 * Proves that accelerometer and gyroscope measurements strictly satisfy
 * physical hardware sensor limits (+/-16g, +/-2000 deg/s) and that
 * orientation angles remain topologically bounded within [-PI, PI].
 */
FlowSMTResult flow_imu_verify_bounds_smt(const FlowIMUSample *sample,
                                        const FlowIMUAttitudeFilter *filter,
                                        FlowSMTProofAttestation *proof_out);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_DRIVER_IMU_H */
