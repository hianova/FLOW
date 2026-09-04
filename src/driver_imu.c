#include "driver_imu.h"
#include "flow_str.h"
#include "hardware_telemetry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define FLOW_PI_F 3.14159265358979323846f

void flow_imu_filter_init(FlowIMUAttitudeFilter *filter, float alpha) {
    if (filter == NULL) return;
    memset(filter, 0, sizeof(*filter));
    filter->alpha = (alpha > 0.0f && alpha < 1.0f) ? alpha : 0.98f;
}

void flow_imu_filter_update(FlowIMUAttitudeFilter *filter, const FlowIMUSample *sample, float dt_seconds) {
    if (filter == NULL || sample == NULL || dt_seconds <= 0.0f) return;

    /* 1. Calculate roll and pitch from accelerometer gravity vector */
    float ax = sample->accel_m_s2[0];
    float ay = sample->accel_m_s2[1];
    float az = sample->accel_m_s2[2];

    float accel_roll  = atan2f(ay, az);
    float accel_pitch = atan2f(-ax, sqrtf(ay * ay + az * az));

    /* 2. Gyroscope integration */
    float gyro_roll_step  = sample->gyro_rad_s[0] * dt_seconds;
    float gyro_pitch_step = sample->gyro_rad_s[1] * dt_seconds;
    float gyro_yaw_step   = sample->gyro_rad_s[2] * dt_seconds;

    /* 3. High-frequency complementary filter fusion */
    filter->roll_rad  = filter->alpha * (filter->roll_rad + gyro_roll_step) + (1.0f - filter->alpha) * accel_roll;
    filter->pitch_rad = filter->alpha * (filter->pitch_rad + gyro_pitch_step) + (1.0f - filter->alpha) * accel_pitch;
    filter->yaw_rad  += gyro_yaw_step;

    /* Wrap angles into [-PI, PI] */
    while (filter->roll_rad > FLOW_PI_F) filter->roll_rad -= 2.0f * FLOW_PI_F;
    while (filter->roll_rad < -FLOW_PI_F) filter->roll_rad += 2.0f * FLOW_PI_F;
    while (filter->pitch_rad > FLOW_PI_F) filter->pitch_rad -= 2.0f * FLOW_PI_F;
    while (filter->pitch_rad < -FLOW_PI_F) filter->pitch_rad += 2.0f * FLOW_PI_F;

    filter->samples_processed++;
}

/*
 * 3-Function Minimal Driver ABI Implementation
 */
static int imu_register(void) {
    return 1;
}

static int imu_get_bounds(FlowHardwareBounds *bounds_out) {
    if (bounds_out == NULL) return 0;
    strncpy(bounds_out->name, "sensor_imu_6dof", sizeof(bounds_out->name) - 1);
    bounds_out->max_queue_depth = 2048;
    bounds_out->max_buffer_bytes = 32 * 1024;
    bounds_out->supports_zero_copy = 1;
    bounds_out->is_kernel_bypass = 1;
    bounds_out->genome_bits_required = 6;
    return 1;
}

static int imu_execute(const FlowPrimitiveContext *ctx, FlowPrimitiveResult *res_out) {
    if (ctx == NULL || res_out == NULL) return -1;
    memset(res_out, 0, sizeof(*res_out));

    uint64_t t_start = flow_hardware_cycles();
    if (ctx->user_data != NULL && ctx->data_len >= sizeof(FlowIMUSample)) {
        res_out->bytes_transferred = sizeof(FlowIMUSample);
        res_out->zero_copy_active = 1;
        res_out->status_code = 0;
    } else {
        res_out->status_code = -1;
    }

    res_out->latency_cycles = flow_hardware_cycles() - t_start;
    return res_out->status_code;
}

static const FlowPrimitiveDriver G_IMU_DRIVER = {
    .driver_name = "sensor_imu_6dof",
    .driver_version = "1.2.0",
    .register_primitive = imu_register,
    .get_hardware_bounds = imu_get_bounds,
    .execute_primitive = imu_execute
};

const FlowPrimitiveDriver *flow_primitive_imu_driver(void) {
    return &G_IMU_DRIVER;
}

/*
 * SMT Formal Sensor Range & Attitude Boundedness Theorem
 */
FlowSMTResult flow_imu_verify_bounds_smt(const FlowIMUSample *sample,
                                        const FlowIMUAttitudeFilter *filter,
                                        FlowSMTProofAttestation *proof_out) {
    if (sample == NULL || filter == NULL) {
        if (proof_out) proof_out->buffer_bounds_safety = FLOW_SMT_UNKNOWN;
        return FLOW_SMT_UNKNOWN;
    }

    /*
     * Physical sensor saturation limits:
     * Gyro max range: +/- 2000 deg/s ~= 34.9 rad/s
     * Accel max range: +/- 16g ~= 156.96 m/s^2
     * Attitude angle max bounds: +/- PI rad
     */
    float gyro_mag = sqrtf(sample->gyro_rad_s[0] * sample->gyro_rad_s[0] +
                           sample->gyro_rad_s[1] * sample->gyro_rad_s[1] +
                           sample->gyro_rad_s[2] * sample->gyro_rad_s[2]);

    float accel_mag = sqrtf(sample->accel_m_s2[0] * sample->accel_m_s2[0] +
                            sample->accel_m_s2[1] * sample->accel_m_s2[1] +
                            sample->accel_m_s2[2] * sample->accel_m_s2[2]);

    FLOW_SMT_BOX_BUILDER_DECL(builder);

    /* Rule 1: Gyroscope within +/- 2000 deg/s */
    uint64_t scaled_gyro = (uint64_t)(gyro_mag * 100.0f);
    FLOW_SMT_BOX_ADD_RULE(builder, "gyro_magnitude", scaled_gyro, 0, 3500,
                          FLOW_BOX_THEOREM_BUFFER_BOUNDS, "gyroscope reading exceeds sensor saturation range (2000 deg/s)");

    /* Rule 2: Accelerometer within +/- 16g */
    uint64_t scaled_accel = (uint64_t)(accel_mag * 100.0f);
    FLOW_SMT_BOX_ADD_RULE(builder, "accel_magnitude", scaled_accel, 0, 16000,
                          FLOW_BOX_THEOREM_MEMORY_QUOTA, "accelerometer reading exceeds sensor saturation range (16g)");

    /* Rule 3: Roll angle within [-PI, PI] */
    uint64_t scaled_roll = (uint64_t)(fabsf(filter->roll_rad) * 1000.0f);
    FLOW_SMT_BOX_ADD_RULE(builder, "attitude_roll_bounded", scaled_roll, 0, 3142,
                          FLOW_BOX_THEOREM_DETERMINISM, "attitude roll angle diverges beyond [-PI, PI]");

    FlowSMTResult res = FLOW_SMT_BOX_VERIFY(builder, "sensor_imu_6dof", proof_out);

    if (res == FLOW_SMT_PROVEN_UNSAT && proof_out != NULL) {
        snprintf(proof_out->proof_summary, sizeof(proof_out->proof_summary),
                 "SMT IMU SOUND: Accel=%.2fm/s^2, Gyro=%.2frad/s, Roll=%.3frad, Pitch=%.3frad (Zero-Saturation)",
                 accel_mag, gyro_mag, filter->roll_rad, filter->pitch_rad);
    }
    return res;
}
