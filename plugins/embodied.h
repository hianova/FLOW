#ifndef FLOW_EMBODIED_H
#define FLOW_EMBODIED_H
#include "flow.h"
#include "plugin.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#define FLOW_MAX_JOINTS 16
#define FLOW_SUPPORT_POLYGON_VERTICES 8
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
#endif
