#include "embodied_physics_scenarios.h"
#include "flow_smt_dsl.h"
#include "flow_str.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FLOW_GRAVITY_CONST 9.80665

int flow_friction_cone_init(FlowGraspFrictionCone *cone, double mass_kg, double mu, double crush_limit_n) {
    if (!cone || mass_kg <= 0.0 || mu <= 0.0 || crush_limit_n <= 0.0) return 0;
    double fg = mass_kg * FLOW_GRAVITY_CONST;
    *cone = (FlowGraspFrictionCone){
        .object_mass_kg = mass_kg, .friction_coeff_mu = mu, .crush_force_limit_n = crush_limit_n,
        .dynamic_safety_factor = 1.30, .current_tangential_force_n = fg,
        .current_normal_force_n = (fg * 1.30) / mu, .is_crushed = ((fg * 1.30) / mu > crush_limit_n)
    };
    return 1;
}

int flow_friction_cone_step_reflex(FlowGraspFrictionCone *cone, double ext_accel_m_s2, double dt_sec) {
    (void)dt_sec;
    cone->external_accel_m_s2 = ext_accel_m_s2;
    cone->current_tangential_force_n = cone->object_mass_kg * fabs(FLOW_GRAVITY_CONST + ext_accel_m_s2);
    double req_fn = (cone->current_tangential_force_n * cone->dynamic_safety_factor) / cone->friction_coeff_mu;
    cone->current_normal_force_n = fmin(cone->crush_force_limit_n, req_fn);
    cone->is_slipping = (cone->current_tangential_force_n > cone->friction_coeff_mu * cone->current_normal_force_n);
    cone->is_crushed = (cone->current_normal_force_n > cone->crush_force_limit_n);
    return 1;
}

FlowSMTResult flow_friction_cone_verify_smt(const FlowGraspFrictionCone *cone, FlowSMTProofAttestation *proof_out) {
    if (!cone) return FLOW_SMT_UNKNOWN;
    double max_fric = cone->friction_coeff_mu * cone->current_normal_force_n;
    const FlowBoxConstraint rules[] = {
        FLOW_SMT_RULE("coulomb_friction_no_slip", cone->current_tangential_force_n > max_fric + 1e-4, 0, 0, FLOW_BOX_THEOREM_BUFFER_BOUNDS, "Tangential load exceeds Coulomb friction cone (Grip Slippage)"),
        FLOW_SMT_RULE("crush_force_integrity", cone->current_normal_force_n > cone->crush_force_limit_n + 1e-4, 0, 0, FLOW_BOX_THEOREM_MEMORY_QUOTA, "Normal force exceeds object structural crush limit")
    };
    return FLOW_SMT_VERIFY_RULES("friction_cone", proof_out, rules,
        "SMT FRICTION-CONE SOUND: Fn=%.2fN <= %.2fN (Zero-Crush), Ft=%.2fN <= %.2fN (Zero-Slip)",
        cone->current_normal_force_n, cone->crush_force_limit_n, cone->current_tangential_force_n, max_fric);
}

int flow_impact_absorber_init(FlowImpactAbsorber *ia, double mass_kg, double max_stroke_m, double gear_rating_n) {
    if (!ia || mass_kg <= 0.0 || max_stroke_m <= 0.0 || gear_rating_n <= 0.0) return 0;
    *ia = (FlowImpactAbsorber){ .robot_mass_kg = mass_kg, .max_stroke_m = max_stroke_m, .gear_force_rating_n = gear_rating_n };
    return 1;
}

int flow_impact_absorber_simulate_touchdown(FlowImpactAbsorber *ia, double drop_height_m, double dt_sec) {
    if (!ia || drop_height_m <= 0.0 || dt_sec <= 0.0) return 0;
    double v0 = -sqrt(2.0 * FLOW_GRAVITY_CONST * drop_height_m);
    ia->current_velocity_m_s = v0;
    ia->current_height_m = 0.0;
    double target_stroke = ia->max_stroke_m * 0.75;
    ia->stiffness_k = fmax(5000.0, (ia->robot_mass_kg * v0 * v0) / (target_stroke * target_stroke));
    ia->damping_c = 2.0 * sqrt(ia->stiffness_k * ia->robot_mass_kg);

    double z = 0.0, v = v0, max_f = 0.0, max_rebound_v = 0.0;
    size_t steps = (size_t)(0.150 / dt_sec);
    for (size_t s = 0; s < steps; s++) {
        double contact_time = (double)s * dt_sec;
        double ramp = (contact_time < 0.015) ? (contact_time / 0.015) : 1.0;
        double f_contact = fmax(0.0, -ia->stiffness_k * z - ia->damping_c * v * ramp);
        max_f = fmax(max_f, f_contact);
        v += ((f_contact / ia->robot_mass_kg) - FLOW_GRAVITY_CONST) * dt_sec;
        z += v * dt_sec;
        if (v > max_rebound_v && z >= 0.0) max_rebound_v = v;
    }
    ia->peak_force_experienced_n = max_f;
    ia->restitution_e = fabs(max_rebound_v / v0);
    ia->gear_damaged = (ia->peak_force_experienced_n > ia->gear_force_rating_n);
    ia->rebound_detected = (ia->restitution_e > 0.10);
    return 1;
}

FlowSMTResult flow_impact_absorber_verify_smt(const FlowImpactAbsorber *ia, FlowSMTProofAttestation *proof_out) {
    if (!ia) return FLOW_SMT_UNKNOWN;
    const FlowBoxConstraint rules[] = {
        FLOW_SMT_RULE("gear_impact_stress_limit", ia->peak_force_experienced_n > ia->gear_force_rating_n, 0, 0, FLOW_BOX_THEOREM_MEMORY_QUOTA, "Peak impact impulse exceeded drivetrain torque rating"),
        FLOW_SMT_RULE("restitution_critical_damping", (uint64_t)(ia->restitution_e * 1000.0), 0, 100, FLOW_BOX_THEOREM_BUFFER_BOUNDS, "Restitution coefficient exceeded critical damping envelope")
    };
    return FLOW_SMT_VERIFY_RULES("impact_absorber", proof_out, rules,
        "SMT IMPACT SOUND: PeakForce=%.1fN <= %.1fN, Restitution=%.3f <= 0.100 (Zero-Gear-Shear)",
        ia->peak_force_experienced_n, ia->gear_force_rating_n, ia->restitution_e);
}

int flow_dual_comanip_init(FlowDualCoManipulation *cm, double length_m, double stiffness_k, double yield_force_n) {
    if (!cm || length_m <= 0.0 || stiffness_k <= 0.0 || yield_force_n <= 0.0) return 0;
    *cm = (FlowDualCoManipulation){
        .beam_nominal_length_m = length_m, .beam_elastic_modulus_k = stiffness_k,
        .yield_force_limit_n = yield_force_n, .pos_a = {0.0, 0.0, 1.0}, .pos_b = {length_m, 0.0, 1.0},
        .current_distance_m = length_m
    };
    return 1;
}

int flow_dual_comanip_step(FlowDualCoManipulation *cm, const double disturbance_vel_a[3], double dt_sec) {
    if (!cm || !disturbance_vel_a || dt_sec <= 0.0) return 0;
    for (int i = 0; i < 3; i++) {
        cm->vel_a[i] = disturbance_vel_a[i];
        cm->pos_a[i] += cm->vel_a[i] * dt_sec;
    }
    double dx = cm->pos_b[0] - cm->pos_a[0], dy = cm->pos_b[1] - cm->pos_a[1], dz = cm->pos_b[2] - cm->pos_a[2];
    double dist = fmax(1e-6, sqrt(dx * dx + dy * dy + dz * dz));
    cm->current_distance_m = dist;
    double abs_err = fabs(dist - cm->beam_nominal_length_m);
    cm->max_sync_error_m = fmax(cm->max_sync_error_m, abs_err);
    cm->internal_force_n = cm->beam_elastic_modulus_k * abs_err;
    cm->yield_violated = (cm->internal_force_n > cm->yield_force_limit_n);

    double u[3] = {dx / dist, dy / dist, dz / dist};
    for (int i = 0; i < 3; i++) {
        double target_b = cm->pos_a[i] + cm->beam_nominal_length_m * u[i];
        cm->vel_b[i] = cm->vel_a[i] + 60.0 * (target_b - cm->pos_b[i]);
        cm->pos_b[i] += cm->vel_b[i] * dt_sec;
    }
    return 1;
}

FlowSMTResult flow_dual_comanip_verify_smt(const FlowDualCoManipulation *cm, FlowSMTProofAttestation *proof_out) {
    if (!cm) return FLOW_SMT_UNKNOWN;
    const FlowBoxConstraint rules[] = {
        FLOW_SMT_RULE("rigid_distance_invariant", (uint64_t)(cm->max_sync_error_m * 100000.0), 0, 500, FLOW_BOX_THEOREM_DETERMINISM, "Dual-robot displacement error exceeded 5mm rigid boundary"),
        FLOW_SMT_RULE("beam_yield_stress_limit", cm->internal_force_n > cm->yield_force_limit_n, 0, 0, FLOW_BOX_THEOREM_MEMORY_QUOTA, "Internal stress exceeded beam material yield limit")
    };
    return FLOW_SMT_VERIFY_RULES("dual_co_manip", proof_out, rules,
        "SMT DUAL-ROBOT SOUND: MaxSyncErr=%.4fm <= 0.0050m, F_int=%.2fN <= %.2fN (Zero-Yield-Break)",
        cm->max_sync_error_m, cm->internal_force_n, cm->yield_force_limit_n);
}
