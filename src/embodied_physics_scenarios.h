#ifndef FLOW_EMBODIED_PHYSICS_SCENARIOS_H
#define FLOW_EMBODIED_PHYSICS_SCENARIOS_H

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
 * FLOW Embodied Advanced Physical Mechanics Scenarios (embodied_physics_scenarios.h)
 * ============================================================================
 * Implements SMT-verified physical mechanics models for extreme real-world robotics:
 * 1. Coulomb Friction Cone & Dynamic Anti-Slip Manifold (delicate grip vs slip)
 * 2. Non-Smooth Restitution & Impact Impulse Absorption (landing shock protection)
 * 3. Dual-Robot Rigid Co-Manipulation Invariant (multi-agent rigid beam transport)
 * ============================================================================
 */

/*
 * ----------------------------------------------------------------------------
 * 1. Coulomb Friction Cone & Dynamic Anti-Slip Manifold
 * ----------------------------------------------------------------------------
 */
typedef struct {
    double object_mass_kg;             /* Mass of gripped object */
    double friction_coeff_mu;          /* Coulomb static friction coefficient */
    double crush_force_limit_n;        /* Maximum normal force before structural crush */
    double current_normal_force_n;     /* Normal gripping force applied */
    double current_tangential_force_n; /* Tangential disturbance / gravitational force */
    double external_accel_m_s2;        /* Dynamic acceleration disturbance */
    double dynamic_safety_factor;      /* Safety margin (e.g. 1.25) */
    bool is_slipping;                  /* 1 if tangential force exceeds friction cone */
    bool is_crushed;                   /* 1 if normal force exceeds crush threshold */
} FlowGraspFrictionCone;

int flow_friction_cone_init(FlowGraspFrictionCone *cone,
                            double mass_kg,
                            double mu,
                            double crush_limit_n);

int flow_friction_cone_step_reflex(FlowGraspFrictionCone *cone,
                                   double ext_accel_m_s2,
                                   double dt_sec);

FlowSMTResult flow_friction_cone_verify_smt(const FlowGraspFrictionCone *cone,
                                            FlowSMTProofAttestation *proof_out);

/*
 * ----------------------------------------------------------------------------
 * 2. Non-Smooth Restitution & Impact Impulse Absorption
 * ----------------------------------------------------------------------------
 */
typedef struct {
    double robot_mass_kg;              /* Total mass falling onto ground */
    double max_stroke_m;               /* Maximum compliance travel */
    double gear_force_rating_n;        /* Structural load limit of drivetrain / gear teeth */
    double current_height_m;           /* Height above touchdown plane */
    double current_velocity_m_s;       /* Velocity along vertical axis */
    double ground_contact_force_n;     /* Ground reaction force */
    double stiffness_k;                /* Dynamic virtual spring stiffness */
    double damping_c;                  /* Dynamic critical damping coefficient */
    double restitution_e;              /* Measured bounce coefficient */
    double peak_force_experienced_n;   /* Max force registered during impact */
    bool gear_damaged;                 /* True if peak force exceeded gearbox rating */
    bool rebound_detected;             /* True if upward bounce exceeded tolerance */
} FlowImpactAbsorber;

int flow_impact_absorber_init(FlowImpactAbsorber *ia,
                              double mass_kg,
                              double max_stroke_m,
                              double gear_rating_n);

int flow_impact_absorber_simulate_touchdown(FlowImpactAbsorber *ia,
                                            double drop_height_m,
                                            double dt_sec);

FlowSMTResult flow_impact_absorber_verify_smt(const FlowImpactAbsorber *ia,
                                              FlowSMTProofAttestation *proof_out);

/*
 * ----------------------------------------------------------------------------
 * 3. Dual-Robot Rigid Co-Manipulation Invariant
 * ----------------------------------------------------------------------------
 */
typedef struct {
    double beam_nominal_length_m;      /* Invariant distance between end-effectors */
    double beam_elastic_modulus_k;     /* Material longitudinal stiffness (N/m) */
    double yield_force_limit_n;        /* Maximum tensile/compressive force before yield */
    double pos_a[3];                   /* Robot A end-effector [x, y, z] */
    double pos_b[3];                   /* Robot B end-effector [x, y, z] */
    double vel_a[3];                   /* Robot A velocity */
    double vel_b[3];                   /* Robot B velocity */
    double current_distance_m;         /* Real-time Euclidean distance */
    double internal_force_n;           /* Internal stress tension / compression */
    double max_sync_error_m;           /* Maximum distance deviation observed */
    bool yield_violated;               /* True if internal force exceeded yield limit */
} FlowDualCoManipulation;

int flow_dual_comanip_init(FlowDualCoManipulation *cm,
                           double length_m,
                           double stiffness_k,
                           double yield_force_n);

int flow_dual_comanip_step(FlowDualCoManipulation *cm,
                           const double disturbance_vel_a[3],
                           double dt_sec);

FlowSMTResult flow_dual_comanip_verify_smt(const FlowDualCoManipulation *cm,
                                           FlowSMTProofAttestation *proof_out);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_EMBODIED_PHYSICS_SCENARIOS_H */
