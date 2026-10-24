#include "security.h"
#include "registry.h"
#include "flow_jet.h"
#include "jit.h"
#include "bitspace.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint64_t flow_security_next(uint64_t *state) { uint64_t v = *state ? *state : FLOW_GOLDEN_RATIO_64;
    v ^= v << 13; v ^= v >> 7; v ^= v << 17; return (*state = v); }
const char *flow_security_outcome_name(FlowSecurityOutcome outcome) {
    static const char *const names[] = { "pass", "contract_violation", "memory_violation", "resource_exhaustion", "timeout", "divergence", "incomplete", "physical_breach" };
    return ((unsigned)outcome <= 7) ? names[outcome] : "unknown"; }
static void flow_security_record_failure(FlowSecurityReport *report, const FlowSecurityCase *sc, FlowSecurityOutcome outcome, const char *message) {
    if (++report->failures != 1) return; report->first_failure = outcome; report->first_failure_round = sc->round;
    report->first_failure_bit = sc->mutated_bit; report->first_failure_genome = sc->mutated_genome;
    if (message) snprintf(report->first_failure_message, sizeof(report->first_failure_message), "%s", message); }
int flow_security_run(uint64_t seed, uint64_t base_genome, uint32_t genome_bits, uint32_t rounds, FlowSecurityProbeFn probe, void *userdata, FlowSecurityReport *report) {
    if (!probe || !report || genome_bits == 0 || genome_bits > 64 || rounds == 0) return -1;
    *report = (FlowSecurityReport){.seed = seed, .base_genome = base_genome, .genome_bits = genome_bits, .rounds = rounds, .first_failure = FLOW_SECURITY_PASS};
    uint64_t state = seed == 0 ? FLOW_GOLDEN_RATIO_64 : seed; FlowSecurityCase base = {seed, base_genome, base_genome, UINT32_MAX, UINT32_MAX};
    char message[160] = {0}; FlowSecurityOutcome outcome = probe(&base, userdata, message, sizeof(message)); report->checks++;
    if (outcome != FLOW_SECURITY_PASS) { flow_security_record_failure(report, &base, outcome, message); return 1; } for (uint32_t round = 0; round < rounds; ++round) {
        uint32_t bit = (uint32_t)(flow_security_next(&state) % genome_bits); FlowSecurityCase sc = {seed, base_genome, base_genome ^ (UINT64_C(1) << bit), bit, round};
        message[0] = '\0'; outcome = probe(&sc, userdata, message, sizeof(message)); report->checks++;
        if (outcome != FLOW_SECURITY_PASS) { flow_security_record_failure(report, &sc, outcome, message); return 1; } } return 0; }
#define SEC_FAIL(code, msg, sz, ...) do { if ((msg) && (sz)) snprintf((msg), (sz), __VA_ARGS__); return (code); } while (0)
FlowSecurityOutcome flow_security_check_contract_gate(const FlowCompositionSpec *spec, char *msg, size_t sz) {
    if (!spec || !spec->component) SEC_FAIL(FLOW_SECURITY_CONTRACT_VIOLATION, msg, sz, "null spec or component");
    if (spec->ir && !component_compatible(spec->ir, spec->component)) SEC_FAIL(FLOW_SECURITY_CONTRACT_VIOLATION, msg, sz, "component '%s' violates semantic contract invariants", spec->component->id);
    return FLOW_SECURITY_PASS; }
FlowSecurityOutcome flow_security_check_abi_migration_gate(const FlowCompositionSpec *spec, char *msg, size_t sz) {
    if (!spec || !spec->component) SEC_FAIL(FLOW_SECURITY_DIVERGENCE, msg, sz, "null spec or component");
    if (spec->reload_adapter_enabled && !flow_component_supports_reload(spec->component)) SEC_FAIL(FLOW_SECURITY_DIVERGENCE, msg, sz, "component '%s' does not satisfy reload adapter ABI invariants", spec->component->id);
    return FLOW_SECURITY_PASS; }
FlowSecurityOutcome flow_security_check_ownership_gate(const FlowCompositionSpec *spec, char *msg, size_t sz) {
    if (!spec || !spec->component) SEC_FAIL(FLOW_SECURITY_MEMORY_VIOLATION, msg, sz, "null spec or component");
    int ro = spec->read_only_ownership || (spec->ir && spec->ir->fact_mutability_read_only);
    if (ro && spec->concurrency_threads > 1 && (spec->ir && spec->ir->state_shared && !spec->component->supports_read_heavy)) SEC_FAIL(FLOW_SECURITY_MEMORY_VIOLATION, msg, sz, "read-only ownership boundary breached by uncoordinated concurrent writer");
    if (spec->concurrency_threads > 1 && spec->ir && spec->ir->state_shared && !spec->component->supports_shared) SEC_FAIL(FLOW_SECURITY_CONTRACT_VIOLATION, msg, sz, "concurrent execution on unshared state causes race divergence");
    return FLOW_SECURITY_PASS; }
FlowSecurityOutcome flow_security_check_resource_quota_gate(const FlowCompositionSpec *spec, char *msg, size_t sz) {
    if (!spec || !spec->component) SEC_FAIL(FLOW_SECURITY_RESOURCE_EXHAUSTION, msg, sz, "null spec or component");
    size_t total = spec->component->memory_fixed_bytes + spec->total_composed_bytes + (spec->metrics ? spec->metrics->memory_bytes : 0);
    size_t limit = spec->memory_limit_bytes ? spec->memory_limit_bytes : ((spec->ir && spec->ir->memory_limit_mb > 0) ? (size_t)spec->ir->memory_limit_mb * 1024u * 1024u : 0);
    if (limit > 0 && total > limit) SEC_FAIL(FLOW_SECURITY_RESOURCE_EXHAUSTION, msg, sz, "composed memory (%zu bytes) exceeds quota (%zu bytes)", total, limit);
    if (spec->ir && spec->ir->top_n > 0 && spec->metrics && spec->metrics->capacity > 0 && (size_t)spec->ir->top_n > spec->metrics->capacity) SEC_FAIL(FLOW_SECURITY_RESOURCE_EXHAUSTION, msg, sz, "top_n (%d) exceeds candidate capacity (%zu)", spec->ir->top_n, spec->metrics->capacity);
    return FLOW_SECURITY_PASS; }
FlowSecurityOutcome flow_security_check_physical_barrier_gate(const FlowCompositionSpec *spec, char *msg, size_t sz) {
    if (!spec) SEC_FAIL(FLOW_SECURITY_CONTRACT_VIOLATION, msg, sz, "null spec"); if (!spec->jet) return FLOW_SECURITY_PASS; const FlowJet *jet = spec->jet;
    uint32_t dim = jet->header.vector_dim ? jet->header.vector_dim : FLOW_JET_STANDARD_DIM; if (dim > FLOW_JET_MAX_DIM) dim = FLOW_JET_MAX_DIM;
    for (size_t i = 0; i < dim; ++i) {
        if (isnan(jet->payload.q[i]) || isinf(jet->payload.q[i]) || isnan(jet->payload.p[i]) || isinf(jet->payload.p[i]) || isnan(jet->payload.a[i]) || isinf(jet->payload.a[i]))
            SEC_FAIL(FLOW_SECURITY_PHYSICAL_BREACH, msg, sz, "physical barrier breach: NaN/Inf at dim %zu", i);
        if (spec->max_velocity_bound > 0.0 && fabs(jet->payload.p[i]) > spec->max_velocity_bound) SEC_FAIL(FLOW_SECURITY_PHYSICAL_BREACH, msg, sz, "velocity p[%zu]=%.3f exceeds limit %.3f", i, jet->payload.p[i], spec->max_velocity_bound);
        if (spec->max_acceleration_bound > 0.0 && fabs(jet->payload.a[i]) > spec->max_acceleration_bound) SEC_FAIL(FLOW_SECURITY_PHYSICAL_BREACH, msg, sz, "acceleration a[%zu]=%.3f exceeds limit %.3f", i, jet->payload.a[i], spec->max_acceleration_bound);
    }
    if (isnan(jet->header.hamiltonian_energy) || isinf(jet->header.hamiltonian_energy) || jet->header.hamiltonian_energy <= 0.0)
        SEC_FAIL(FLOW_SECURITY_PHYSICAL_BREACH, msg, sz, "physical barrier breach: invalid baseline Hamiltonian energy %.4f", jet->header.hamiltonian_energy);
    double h0 = jet->header.hamiltonian_energy, drift = fabs(flow_jet_hamiltonian(jet) - h0) / h0, tol = spec->max_hamiltonian_drift_ratio > 0.0 ? spec->max_hamiltonian_drift_ratio : 0.05;
    if (!(drift <= tol) || isnan(drift)) SEC_FAIL(FLOW_SECURITY_PHYSICAL_BREACH, msg, sz, "Hamiltonian drift %.4f exceeds tolerance %.4f", drift, tol);
    return FLOW_SECURITY_PASS; }
FlowSecurityOutcome flow_security_check_jit_wx_gate(const FlowJITCodeBlock *block, uintptr_t write_base, uintptr_t exec_base, char *msg, size_t sz) {
    if (!write_base || !exec_base) SEC_FAIL(FLOW_SECURITY_MEMORY_VIOLATION, msg, sz, "unmapped jit memory base");
    if (write_base == exec_base) SEC_FAIL(FLOW_SECURITY_MEMORY_VIOLATION, msg, sz, "W^X hard gate violation at 0x%lx", (unsigned long)write_base);
    if (block && (!block->start_ip || !block->code_bytes || block->start_ip < exec_base)) SEC_FAIL(FLOW_SECURITY_MEMORY_VIOLATION, msg, sz, "invalid jit code block pointer");
    return FLOW_SECURITY_PASS; }
FlowSecurityOutcome flow_security_check_transposition_gate(const FlowLayoutMigrationSpec *spec, size_t buffer_bytes, char *msg, size_t sz) {
    if (!spec || spec->field_count == 0 || spec->field_count > 8) SEC_FAIL(FLOW_SECURITY_CONTRACT_VIOLATION, msg, sz, "invalid layout migration spec"); size_t struct_size = 0;
    for (size_t f = 0; f < spec->field_count; ++f) { size_t f_sz = spec->field_sizes[f];
        if (f_sz == 0 || f_sz > 256) SEC_FAIL(FLOW_SECURITY_CONTRACT_VIOLATION, msg, sz, "invalid field size %zu", f_sz);
        if (struct_size % (f_sz > 8 ? 8 : f_sz) != 0) SEC_FAIL(FLOW_SECURITY_MEMORY_VIOLATION, msg, sz, "misaligned field offset %zu", struct_size); struct_size += f_sz; }
    if (spec->item_count > 0 && struct_size > 0) {
        if (spec->item_count > (SIZE_MAX / struct_size)) SEC_FAIL(FLOW_SECURITY_MEMORY_VIOLATION, msg, sz, "integer overflow in transposition");
        if (buffer_bytes > 0 && spec->item_count * struct_size > buffer_bytes) SEC_FAIL(FLOW_SECURITY_RESOURCE_EXHAUSTION, msg, sz, "transposition bytes exceed buffer"); }
    return FLOW_SECURITY_PASS; }
int flow_jet_attractor_profile_init(FlowJetAttractorProfile *profile, uint32_t dim, double r_q, double r_p, double max_trace) {
    if (!profile) return 0;
    *profile = (FlowJetAttractorProfile){ .dim = (dim > 0 && dim <= FLOW_JET_ATTRACTOR_MAX_DIM) ? dim : 16, .max_radius_q = r_q > 0.0 ? r_q : 5.0, .max_radius_p = r_p > 0.0 ? r_p : 10.0, .max_koopman_trace = max_trace };
    return 1; }
FlowSecurityOutcome flow_security_check_attractor_anomaly(const FlowJet *jet, const FlowJetAttractorProfile *profile, char *msg, size_t sz) {
    if (!jet || !profile) SEC_FAIL(FLOW_SECURITY_CONTRACT_VIOLATION, msg, sz, "null jet or profile"); uint32_t dim = jet->header.vector_dim ? jet->header.vector_dim : 16;
    if (dim > profile->dim) dim = profile->dim; if (dim > FLOW_JET_ATTRACTOR_MAX_DIM) dim = FLOW_JET_ATTRACTOR_MAX_DIM;
    double rq2 = profile->max_radius_q > 0.0 ? profile->max_radius_q * profile->max_radius_q : 1.0, rp2 = profile->max_radius_p > 0.0 ? profile->max_radius_p * profile->max_radius_p : 1.0, m_sum = 0.0;
    for (size_t i = 0; i < dim; ++i) {
        if (isnan(jet->payload.q[i]) || isinf(jet->payload.q[i]) || isnan(jet->payload.p[i]) || isinf(jet->payload.p[i]))
            SEC_FAIL(FLOW_SECURITY_PHYSICAL_BREACH, msg, sz, "attractor breach: NaN/Inf at dim %zu", i);
        double dq = jet->payload.q[i] - profile->q_center[i], dp = jet->payload.p[i] - profile->p_center[i]; m_sum += (dq * dq) / rq2 + (dp * dp) / rp2;
    }
    if (!(m_sum / (double)dim <= 1.0) || isnan(m_sum) || isinf(m_sum)) SEC_FAIL(FLOW_SECURITY_PHYSICAL_BREACH, msg, sz, "attractor breach: phase distance %.3f > 1.0", m_sum / (double)dim);
    uint32_t kdim = jet->header.koopman_dim ? jet->header.koopman_dim : 8; if (kdim > FLOW_JET_MAX_KOOPMAN_DIM) kdim = FLOW_JET_MAX_KOOPMAN_DIM;
    double trace_K = 0.0; for (size_t i = 0; i < kdim; ++i) trace_K += jet->payload.koopman_matrix[i][i];
    if (!(trace_K <= profile->max_koopman_trace) || isnan(trace_K) || isinf(trace_K)) SEC_FAIL(FLOW_SECURITY_PHYSICAL_BREACH, msg, sz, "Koopman trace %.4f > %.4f", trace_K, profile->max_koopman_trace);
    return FLOW_SECURITY_PASS; }
FlowSecurityOutcome flow_security_check_continuity_proof(const FlowJet *prev_jet, const FlowJet *curr_jet, double dt, double max_jerk, double noise_tolerance, char *msg, size_t sz) {
    if (!prev_jet || !curr_jet || !(dt > 0.0) || isnan(dt) || isinf(dt)) SEC_FAIL(FLOW_SECURITY_CONTRACT_VIOLATION, msg, sz, "invalid arguments for continuity check");
    uint32_t dim = prev_jet->header.vector_dim ? prev_jet->header.vector_dim : 16;
    if (curr_jet->header.vector_dim && curr_jet->header.vector_dim < dim) dim = curr_jet->header.vector_dim; if (dim > FLOW_JET_MAX_DIM) dim = FLOW_JET_MAX_DIM;
    double j_max = max_jerk > 0.0 ? max_jerk : 500.0, tol = noise_tolerance >= 0.0 ? noise_tolerance : 0.01; for (size_t i = 0; i < dim; ++i) {
        double q0 = prev_jet->payload.q[i], p0 = prev_jet->payload.p[i], a0 = prev_jet->payload.a[i], q1 = curr_jet->payload.q[i], p1 = curr_jet->payload.p[i], a1 = curr_jet->payload.a[i];
        if (isnan(q0) || isinf(q0) || isnan(p0) || isinf(p0) || isnan(a0) || isinf(a0) || isnan(q1) || isinf(q1) || isnan(p1) || isinf(p1) || isnan(a1) || isinf(a1))
            SEC_FAIL(FLOW_SECURITY_PHYSICAL_BREACH, msg, sz, "continuity breach: NaN/Inf at dim %zu", i);
        if (!(fabs(a1 - a0) <= j_max * dt + tol)) SEC_FAIL(FLOW_SECURITY_PHYSICAL_BREACH, msg, sz, "continuity breach: accel jump at dim %zu", i);
        if (!(fabs(p1 - (p0 + a0 * dt)) <= 0.5 * j_max * dt * dt + tol)) SEC_FAIL(FLOW_SECURITY_PHYSICAL_BREACH, msg, sz, "continuity breach: velocity step at dim %zu", i);
        if (!(fabs(q1 - (q0 + p0 * dt + 0.5 * a0 * dt * dt)) <= (1.0 / 6.0) * j_max * dt * dt * dt + tol)) SEC_FAIL(FLOW_SECURITY_PHYSICAL_BREACH, msg, sz, "continuity breach: position teleport at dim %zu", i);
    } return FLOW_SECURITY_PASS; }
int flow_security_predict_resource_breach(double current_usage, double consumption_rate, double consumption_accel, double quota_limit, double time_horizon_s, FlowPredictiveBreachReport *report) {
    if (!report) return 0; *report = (FlowPredictiveBreachReport){.time_to_breach_s = -1.0};
    if (current_usage >= quota_limit) { *report = (FlowPredictiveBreachReport){.will_breach = 1, .time_to_breach_s = 0.0, .projected_breach_velocity = consumption_rate}; return 1; }
    double dq = quota_limit - current_usage, v = consumption_rate, a = consumption_accel, t = -1.0; if (fabs(a) < 1e-9) { if (v > 0.0) t = dq / v; } else {
        double discr = v * v + 2.0 * a * dq;
        if (discr >= 0.0) { double sd = sqrt(discr), t1 = (-v + sd) / a, t2 = (-v - sd) / a; t = (t1 > 0.0 && t2 > 0.0) ? fmin(t1, t2) : (t1 > 0.0 ? t1 : (t2 > 0.0 ? t2 : -1.0)); }
    }
    if (t > 0.0 && (time_horizon_s <= 0.0 || t <= time_horizon_s)) *report = (FlowPredictiveBreachReport){.will_breach = 1, .time_to_breach_s = t, .projected_breach_velocity = v + a * t};
    return 1; }
int flow_security_should_proactive_morph(const FlowPredictiveBreachReport *report, double proactive_lead_time_s) {
    return (report && report->will_breach && (proactive_lead_time_s <= 0.0 || report->time_to_breach_s <= proactive_lead_time_s)) ? 1 : 0; }
FlowSecurityOutcome flow_security_check_lean_proof_gate(const FlowCompositionSpec *spec, char *msg, size_t sz) {
    if (!spec || !spec->ir || !spec->component) SEC_FAIL(FLOW_SECURITY_CONTRACT_VIOLATION, msg, sz, "lean proof gate: null spec");
    if (spec->ir->hole_count > 0) SEC_FAIL(FLOW_SECURITY_INCOMPLETE, msg, sz, "lean proof gate rejected: %zu unresolved hole(s)", spec->ir->hole_count);
    if (!spec->ir->fact_deterministic) SEC_FAIL(FLOW_SECURITY_CONTRACT_VIOLATION, msg, sz, "lean proof gate rejected: non-deterministic flow");
    if (spec->ir->input_max_count <= 0) SEC_FAIL(FLOW_SECURITY_CONTRACT_VIOLATION, msg, sz, "lean proof gate rejected: unbounded input");
    if (spec->metrics && spec->metrics->capacity > 0 && spec->metrics->capacity < (size_t)spec->ir->input_max_count) SEC_FAIL(FLOW_SECURITY_CONTRACT_VIOLATION, msg, sz, "lean proof gate rejected: capacity < input_max_count");
    return FLOW_SECURITY_PASS; }
FlowSecurityOutcome flow_security_check_composition_gate(const FlowCompositionSpec *spec, char *msg, size_t sz) {
    typedef FlowSecurityOutcome (*GateFn)(const FlowCompositionSpec *, char *, size_t);
    static const GateFn gates[] = { flow_security_check_contract_gate, flow_security_check_abi_migration_gate, flow_security_check_ownership_gate, flow_security_check_resource_quota_gate, flow_security_check_physical_barrier_gate };
    for (size_t i = 0; i < 5; ++i) { FlowSecurityOutcome out = gates[i](spec, msg, sz); if (out != FLOW_SECURITY_PASS) return out; }
    if (spec && spec->ir && spec->ir->fact_lean_certified) return flow_security_check_lean_proof_gate(spec, msg, sz); return FLOW_SECURITY_PASS; }
uint64_t flow_security_get_safety_mask(const SemanticIR *ir, const Component *comp, const FlowPlanDimensionSet *dims) {
    if (!dims || dims->count == 0) return (uint64_t)-1; uint64_t mask = (uint64_t)-1; unsigned shift = 0;
    int forbid_conc = (ir && comp) && ((ir->fact_mutability_read_only && ir->state_shared && !comp->supports_read_heavy) || (!ir->state_shared && !ir->flow_parallelizable) || (ir->state_shared && !comp->supports_shared));
    for (size_t i = 0; i < dims->count; ++i) { const FlowPlanDimension *d = &dims->dimensions[i]; unsigned bits = flow_dimension_bits(d); if (!bits) continue;
        uint64_t dim_mask = (bits >= 64) ? (uint64_t)-1 : (((uint64_t)1 << bits) - 1);
        if (forbid_conc && (!strcmp(d->name, "threads") || !strcmp(d->name, "shards"))) mask &= ~(dim_mask << shift);
        else if (!strcmp(d->name, "threads") && d->kind == FLOW_DIM_EXPONENT && d->max_val > 6) { uint64_t max_raw = 6 >= d->min_val ? 6 - d->min_val : 0;
            for (unsigned b = 0; b < bits; ++b) if (((uint64_t)1 << b) > max_raw) mask &= ~(UINT64_C(1) << (shift + b)); } shift += bits; } return mask; }
double flow_security_mtd_calculate_entropy(const FlowMTDLayout *layout) { if (!layout || layout->field_count <= 1 || layout->total_size == 0) return 0.0;
    double entropy = 0.0, total = (double)layout->total_size; for (size_t i = 0; i < layout->field_count; ++i) {
        size_t gap = (i + 1 < layout->field_count) ? (layout->field_offsets[i + 1] - layout->field_offsets[i]) : (layout->total_size - layout->field_offsets[i]);
        if (gap > 0) { double p = (double)gap / total; entropy -= p * (log(p) / log(2.0)); } } return entropy; }
int flow_security_mtd_verify_alignment(const FlowMTDLayout *layout, const size_t *field_alignments) { if (!layout) return 0; for (size_t i = 0; i < layout->field_count; ++i) {
        size_t orig = layout->field_order[i], align = (field_alignments && field_alignments[orig] > 0) ? field_alignments[orig] : sizeof(void *);
        if ((layout->field_offsets[i] % align) != 0) return 0; } return 1; }
int flow_security_mtd_generate_layout(uint64_t seed, size_t field_count, const size_t *field_sizes, const size_t *field_alignments, size_t max_padding_jitter, FlowMTDLayout *layout_out) {
    if (!layout_out || field_count == 0 || !field_sizes) return 0; if (field_count > FLOW_MTD_MAX_FIELDS) field_count = FLOW_MTD_MAX_FIELDS;
    *layout_out = (FlowMTDLayout){.seed = seed, .field_count = field_count}; uint64_t rng = seed == 0 ? UINT64_C(0xa1b2c3d4e5f60718) : seed;
    for (size_t i = 0; i < field_count; ++i) layout_out->field_order[i] = i; for (size_t i = field_count - 1; i > 0; --i) {
        size_t j = (size_t)(flow_security_next(&rng) % (i + 1)), tmp = layout_out->field_order[i];
        layout_out->field_order[i] = layout_out->field_order[j]; layout_out->field_order[j] = tmp; } size_t max_align = sizeof(void *), cur_off = 0;
    for (size_t i = 0; i < field_count; ++i) { size_t a = (field_alignments && field_alignments[i] > 0) ? field_alignments[i] : sizeof(void *); if (a > max_align) max_align = a; }
    layout_out->required_alignment = max_align; if (!max_padding_jitter) max_padding_jitter = 16; for (size_t i = 0; i < field_count; ++i) {
        size_t orig = layout_out->field_order[i], sz = field_sizes[orig], align = (field_alignments && field_alignments[orig] > 0) ? field_alignments[orig] : sizeof(void *);
        cur_off = (cur_off + align - 1) & ~(align - 1); layout_out->field_offsets[i] = cur_off; cur_off += sz;
        size_t jitter = ((size_t)(flow_security_next(&rng) % max_padding_jitter) + align - 1) & ~(align - 1); layout_out->padding_bytes[i] = jitter; cur_off += jitter; }
    layout_out->total_size = (cur_off + max_align - 1) & ~(max_align - 1); layout_out->canary_token = flow_security_next(&rng);
    layout_out->shannon_entropy = flow_security_mtd_calculate_entropy(layout_out); return flow_security_mtd_verify_alignment(layout_out, field_alignments); }
uint64_t flow_security_get_compliance_mask(FlowComplianceMode mode, const FlowPlanDimensionSet *dims) {
    if (mode == FLOW_COMPLIANCE_PERMISSIVE_STAGING || !dims || dims->count == 0) return (uint64_t)-1; uint64_t mask = 0; unsigned shift = 0;
    for (size_t i = 0; i < dims->count; ++i) { const FlowPlanDimension *d = &dims->dimensions[i]; unsigned bits = flow_dimension_bits(d); if (!bits) continue;
        uint64_t dim_mask = (bits >= 64) ? (uint64_t)-1 : (((uint64_t)1 << bits) - 1); if (!strncmp(d->name, "tuning_", 7)) mask |= (dim_mask << shift); shift += bits; }
    return mask; }
