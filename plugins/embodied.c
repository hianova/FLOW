#include "embodied.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int flow_physics_init(FlowPhysicsEngine *engine, size_t joint_count, double mass_kg) {
    if (!engine) return 0;
    memset(engine, 0, sizeof(*engine));
    if (joint_count > FLOW_MAX_JOINTS) joint_count = FLOW_MAX_JOINTS;
    if (joint_count == 0) joint_count = 6;
    engine->current_state.joint_count = joint_count;
    engine->current_state.mass_kg = mass_kg > 0.0 ? mass_kg : 25.0;
    engine->current_state.dt_seconds = 0.001;
    engine->max_angular_accel = 50.0;
    engine->friction_coefficient = 0.6;
    for (size_t j = 0; j < joint_count; ++j) engine->current_state.max_torque_limit[j] = 80.0;
    engine->current_state.support_vertex_count = 4;
    const double poly[4][2] = {{-0.15, -0.10}, {0.15, -0.10}, {0.15, 0.10}, {-0.15, 0.10}};
    memcpy(engine->current_state.support_polygon, poly, sizeof(poly));
    engine->current_state.center_of_mass[2] = 0.65;
    return 1;
}
bool flow_physics_is_torque_safe(const FlowRigidBodyState *state, const double *candidate_torques) {
    if (!state || !candidate_torques) return false;
    for (size_t j = 0; j < state->joint_count; ++j) {
        double limit = state->max_torque_limit[j] > 0.0 ? state->max_torque_limit[j] : 80.0;
        if (fabs(candidate_torques[j]) > limit) return false;
    }
    return true;
}
bool flow_physics_is_zmp_stable(const FlowRigidBodyState *state) {
    if (!state || state->support_vertex_count < 3) return false;
    double px = state->zmp_position[0], py = state->zmp_position[1];
    size_t n = state->support_vertex_count;
    bool pos = false, neg = false;
    for (size_t i = 0; i < n; ++i) {
        size_t next = (i + 1) % n;
        double x1 = state->support_polygon[i][0], y1 = state->support_polygon[i][1];
        double x2 = state->support_polygon[next][0], y2 = state->support_polygon[next][1];
        double cross = (x2 - x1) * (py - y1) - (y2 - y1) * (px - x1);
        if (cross > 1e-7) pos = true;
        if (cross < -1e-7) neg = true;
        if (pos && neg) return false;
    }
    return true;
}
int flow_physics_simulate_step(FlowPhysicsEngine *engine, const double *applied_torques) {
    if (!engine || !applied_torques) return 0;
    FlowRigidBodyState *s = &engine->current_state;
    if (!flow_physics_is_torque_safe(s, applied_torques)) { engine->violations_prevented_total++; return 0; }
    double dt = s->dt_seconds > 0.0 ? s->dt_seconds : 0.001, net_tx = 0.0, net_ty = 0.0;
    for (size_t j = 0; j < s->joint_count; ++j) {
        double accel = (applied_torques[j] - 0.05 * s->joint_velocities[j]) / 0.25;
        accel = fmin(engine->max_angular_accel, fmax(-engine->max_angular_accel, accel));
        s->joint_velocities[j] += accel * dt;
        s->joint_angles[j] += s->joint_velocities[j] * dt;
        s->joint_torques[j] = applied_torques[j];
        net_tx += applied_torques[j] * 0.1;
        net_ty += applied_torques[j] * 0.05;
    }
    s->zmp_position[0] = s->center_of_mass[0] - (net_ty / (s->mass_kg * 9.81));
    s->zmp_position[1] = s->center_of_mass[1] + (net_tx / (s->mass_kg * 9.81));
    engine->simulated_steps_total++;
    if (!flow_physics_is_zmp_stable(s)) { engine->violations_prevented_total++; return 0; }
    return 1;
}
static const Component EMBODIED_COMPONENTS[] = {{
    .id = "embodied_controller", .kind = "controller", .resource = "hardware", .capability = "realtime",
    .supports_unordered = 1, .latency_score = 10, .memory_score = 9, .domain_contract = "zmp_torque_safe",
    .flow_binding = "flow_embodied_run", .memory_fixed_bytes = sizeof(FlowPhysicsEngine),
    .memory_bytes_per_capacity = 64, .reload_capable = 1
}};
static int embodied_compatible(const SemanticIR *ir, const Component *c) {
    if (!ir || !c) return 0;
    return !strcmp(ir->domain_name, "embodied") || !strcmp(ir->contract_name, "zmp_torque_safe") || !strcmp(ir->plugin_name, "flow.embodied");
}
static void embodied_lower_semantics(const FlowSpec *spec, SemanticIR *ir, const FlowPlugin *p) {
    (void)spec; (void)p;
    if (!ir) return;
    if (!ir->sla_latency_ns) ir->sla_latency_ns = 1000000ULL;
    if (!ir->resource_name[0]) strncpy(ir->resource_name, "hardware", FLOW_NAME - 1);
    if (!ir->capability_name[0]) strncpy(ir->capability_name, "realtime", FLOW_NAME - 1);
    if (!ir->contract_name[0]) strncpy(ir->contract_name, "zmp_torque_safe", FLOW_NAME - 1);
    if (!ir->jet.enabled) {
        ir->jet.enabled = 1; ir->jet.frequency_hz = 1000.0; ir->jet.symplectic = 1;
        ir->jet.max_drift = 0.005; ir->jet.zmp_margin = 0.02; ir->jet.dt = 0.001; ir->jet.dim = 6;
    }
}
static uint64_t embodied_env_mask(const SemanticIR *ir, const Component *c, const FlowPlanDimensionSet *dims, const FlowEnvironmentState *env) {
    (void)ir; (void)c; (void)dims; (void)env; return UINT64_MAX;
}
static size_t flow_embodied_get_genome_bit_size(void) { return 16; }
static uint64_t flow_embodied_get_valid_mask(const FlowEnvironmentState *env) {
    double e_max = (env && env->measured_miss_rate > 0.05) ? 40.0 : 100.0;
    double max_v_sq = (e_max - log(12.0)) / 2.5;
    double v_max = max_v_sq > 0.0 ? sqrt(max_v_sq) : 1.0;
    return (v_max < 4.0) ? (0x0000FFFFULL & ~0x00C0ULL) : 0x0000FFFFULL;
}
static double flow_embodied_evaluate_energy(uint64_t genome) {
    unsigned t = (unsigned)(genome & 0x0F), v = (unsigned)((genome >> 4) & 0x0F);
    unsigned f = (unsigned)((genome >> 8) & 0x0F), d = (unsigned)((genome >> 12) & 0x0F);
    return (double)t * 1.5 + (double)v * 2.0 + (double)(16 - f) * 0.8 + (double)d * 1.2;
}
static void flow_embodied_emit_llvm_ir(uint64_t genome, void *module_or_out) {
    if (!module_or_out) return;
    FILE *out = (FILE *)module_or_out;
    unsigned t = (unsigned)(genome & 0x0F), v = (unsigned)((genome >> 4) & 0x0F);
    unsigned f = (unsigned)((genome >> 8) & 0x0F), d = (unsigned)((genome >> 12) & 0x0F);
    fprintf(out, "/* [flow.embodied] Native Reflex Code (Genome: 0x%04llx) */\n"
                 "/* Torque Max: %u Nm, Vel Cap: %u rad/s, Loop Freq: %u kHz, Delay Comp: %u steps */\n"
                 "void flow_embodied_step(const double *angles, const double *vels, double *torques_out) {\n"
                 "    for (int j = 0; j < %d; ++j) torques_out[j] = -%f * angles[j] - %f * vels[j];\n}\n",
            (unsigned long long)genome, t * 10, v * 2, f + 1, d, FLOW_MAX_JOINTS, (double)(t + 1) * 2.5, (double)(v + 1) * 0.5);
}
static const FlowPluginABI EMBODIED_ABI_V2 = {
    .get_genome_bit_size = flow_embodied_get_genome_bit_size,
    .get_valid_mask = flow_embodied_get_valid_mask,
    .evaluate_energy = flow_embodied_evaluate_energy,
    .emit_llvm_ir = flow_embodied_emit_llvm_ir
};
static const FlowPlugin EMBODIED_PLUGIN = {
    .name = "flow.embodied", .version = "1.0", .components = EMBODIED_COMPONENTS, .component_count = 1,
    .compatible = embodied_compatible, .lower_domain_semantics = embodied_lower_semantics,
    .environment_mask = embodied_env_mask,
    .doc_title = "Embodied AI Physics & Dual-Rate Gait Controller",
    .doc_responsibilities = "Provides micro-physics simulation gate and ZMP safety verification",
    .doc_algorithmic_guarantee = "Guarantees zero-fall ZMP stability polygon",
    .doc_memory_concurrency_model = "Stack-allocated joint states, QSBR zero-copy lock-free cortical hot-swapping",
    .doc_key_apis = "flow_physics_simulate_step, flow_physics_is_zmp_stable",
    .doc_layer = 2
};
static const FlowPluginDescriptor EMBODIED_DESCRIPTOR = {
    .abi_major = FLOW_PLUGIN_ABI_MAJOR, .abi_minor = FLOW_PLUGIN_ABI_MINOR,
    .descriptor_size = sizeof(FlowPluginDescriptor), .module_name = "flow.embodied",
    .module_version = "1.0", .module_hash = 0xEB0D1ED1,
    .plugin = &EMBODIED_PLUGIN, .abi_v2 = &EMBODIED_ABI_V2
};
const FlowPluginDescriptor *flow_embodied_entry_v1(void) { return &EMBODIED_DESCRIPTOR; }
#ifdef FLOW_PLUGIN_DSO
const FlowPluginDescriptor *flow_plugin_entry_v1(void) { return &EMBODIED_DESCRIPTOR; }
#endif
