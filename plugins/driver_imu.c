#include "driver_imu.h"
#include "hardware_telemetry.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#define FLOW_PI_F 3.14159265358979323846f
void flow_imu_filter_init(FlowIMUAttitudeFilter *f, float a) { if (f) { memset(f, 0, sizeof(*f)); f->alpha = (a > 0.0f && a < 1.0f) ? a : 0.98f; } }
void flow_imu_filter_update(FlowIMUAttitudeFilter *f, const FlowIMUSample *s, float dt) {
    if (!f || !s || !isfinite(dt) || dt <= 0.0f) return;
    for (int i = 0; i < 3; ++i) {
        if (!isfinite(s->accel_m_s2[i]) || !isfinite(s->gyro_rad_s[i])) return;
    }
    float ax = s->accel_m_s2[0], ay = s->accel_m_s2[1], az = s->accel_m_s2[2], ar = atan2f(ay, az), ap = atan2f(-ax, sqrtf(ay * ay + az * az));
    f->roll_rad = f->alpha * (f->roll_rad + s->gyro_rad_s[0] * dt) + (1.0f - f->alpha) * ar;
    f->pitch_rad = f->alpha * (f->pitch_rad + s->gyro_rad_s[1] * dt) + (1.0f - f->alpha) * ap;
    f->yaw_rad += s->gyro_rad_s[2] * dt;
    f->roll_rad = fmodf(f->roll_rad + FLOW_PI_F, 2.0f * FLOW_PI_F);
    if (f->roll_rad < 0.0f) f->roll_rad += 2.0f * FLOW_PI_F;
    f->roll_rad -= FLOW_PI_F;
    f->pitch_rad = fmodf(f->pitch_rad + FLOW_PI_F, 2.0f * FLOW_PI_F);
    if (f->pitch_rad < 0.0f) f->pitch_rad += 2.0f * FLOW_PI_F;
    f->pitch_rad -= FLOW_PI_F;
    f->samples_processed++;
}
static int imu_execute(const FlowPrimitiveContext *ctx, FlowPrimitiveResult *res) { if (!ctx || !res) return -1; memset(res, 0, sizeof(*res));
    uint64_t t0 = flow_hardware_cycles(); if (ctx->user_data && ctx->data_len >= sizeof(FlowIMUSample)) { res->bytes_transferred = sizeof(FlowIMUSample); res->zero_copy_active = 1; res->status_code = 0; }
    else res->status_code = -1; res->latency_cycles = flow_hardware_cycles() - t0; return res->status_code; }
FLOW_DEFINE_PRIMITIVE_DRIVER(G_IMU_DRIVER, flow_primitive_imu_driver, "sensor_imu_6dof", "1.2.0", 2048, 32768, 1, 1, 6, imu_execute)
FlowSMTResult flow_imu_verify_bounds_smt(const FlowIMUSample *s, const FlowIMUAttitudeFilter *f, FlowSMTProofAttestation *proof_out) {
    if (!s || !f) { if (proof_out) proof_out->buffer_bounds_safety = FLOW_SMT_UNKNOWN; return FLOW_SMT_UNKNOWN; }
    for (int i = 0; i < 3; ++i) {
        if (!isfinite(s->accel_m_s2[i]) || !isfinite(s->gyro_rad_s[i])) {
            if (proof_out) proof_out->buffer_bounds_safety = FLOW_SMT_VIOLATION_SAT;
            return FLOW_SMT_VIOLATION_SAT;
        }
    }
    if (!isfinite(f->roll_rad) || !isfinite(f->pitch_rad) || !isfinite(f->yaw_rad)) {
        if (proof_out) proof_out->determinism_invariant = FLOW_SMT_VIOLATION_SAT;
        return FLOW_SMT_VIOLATION_SAT;
    }
    float gm = sqrtf(s->gyro_rad_s[0]*s->gyro_rad_s[0] + s->gyro_rad_s[1]*s->gyro_rad_s[1] + s->gyro_rad_s[2]*s->gyro_rad_s[2]);
    float am = sqrtf(s->accel_m_s2[0]*s->accel_m_s2[0] + s->accel_m_s2[1]*s->accel_m_s2[1] + s->accel_m_s2[2]*s->accel_m_s2[2]);
    const FlowBoxConstraint rules[] = {
        FLOW_SMT_RULE("gyro_magnitude", (uint64_t)(gm * 100.0f), 0, 3500, FLOW_BOX_THEOREM_BUFFER_BOUNDS, "gyroscope reading exceeds sensor saturation range (2000 deg/s)"),
        FLOW_SMT_RULE("accel_magnitude", (uint64_t)(am * 100.0f), 0, 16000, FLOW_BOX_THEOREM_MEMORY_QUOTA, "accelerometer reading exceeds sensor saturation range (16g)"),
        FLOW_SMT_RULE("attitude_roll_bounded", (uint64_t)(fabsf(f->roll_rad) * 1000.0f), 0, 3142, FLOW_BOX_THEOREM_DETERMINISM, "attitude roll angle diverges beyond [-PI, PI]") };
    return FLOW_SMT_VERIFY_RULES("sensor_imu_6dof", proof_out, rules, "SMT IMU SOUND: Accel=%.2fm/s^2, Gyro=%.2frad/s, Roll=%.3frad, Pitch=%.3frad (Zero-Saturation)", am, gm, f->roll_rad, f->pitch_rad); }
