#ifndef FLOW_DRIVER_IMU_H
#define FLOW_DRIVER_IMU_H
#include "primitive.h"
#include "smt.h"
#include "flow_smt_dsl.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
typedef struct { float accel_m_s2[3], gyro_rad_s[3], mag_uT[3], temp_celsius; uint64_t timestamp_ns; uint32_t sequence; } FlowIMUSample;
typedef struct { float roll_rad, pitch_rad, yaw_rad, alpha; uint64_t samples_processed; uint32_t drift_spikes_rejected; } FlowIMUAttitudeFilter;
void flow_imu_filter_init(FlowIMUAttitudeFilter *filter, float alpha);
void flow_imu_filter_update(FlowIMUAttitudeFilter *filter, const FlowIMUSample *sample, float dt_seconds);
const FlowPrimitiveDriver *flow_primitive_imu_driver(void);
FlowSMTResult flow_imu_verify_bounds_smt(const FlowIMUSample *sample, const FlowIMUAttitudeFilter *filter, FlowSMTProofAttestation *proof_out);
#endif
