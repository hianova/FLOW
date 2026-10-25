#ifndef FLOW_SMT_H
#define FLOW_SMT_H
#include "flow.h"
#include "plugin.h"
#include "bitspace.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#ifndef FLOW_SMT_RESULT_DEFINED
#define FLOW_SMT_RESULT_DEFINED
typedef enum {
    FLOW_SMT_UNINITIALIZED = 0,   /* Zero-initialized attestation has not been verified */
    FLOW_SMT_PROVEN_UNSAT = 1,    /* Negation is UNSAT -> Theorem holds universally */
    FLOW_SMT_VIOLATION_SAT = 2,   /* Counterexample found -> Invariant violated */
    FLOW_SMT_UNKNOWN = 3          /* Unconstrained or unbounded */
} FlowSMTResult;
#endif
#ifndef FLOW_SMT_PROOF_ATTESTATION_DEFINED
#define FLOW_SMT_PROOF_ATTESTATION_DEFINED
typedef struct FlowSMTProofAttestation { FlowSMTResult buffer_bounds_safety; FlowSMTResult memory_quota_bound; FlowSMTResult shard_non_aliasing;
    FlowSMTResult determinism_invariant; char proof_summary[256]; } FlowSMTProofAttestation;
#endif
int flow_smt_generate_proof_script(const SemanticIR *ir,
                                   const Component *component, const FlowPlanAssignment *plan, const FlowPlanMetrics *metrics, FILE *out);
int flow_smt_verify(const SemanticIR *ir, const Component *component, const FlowPlanAssignment *plan, const FlowPlanMetrics *metrics, FlowSMTProofAttestation *proof_out);
int flow_smt_verify_with_budget(const SemanticIR *ir, const Component *component, const FlowPlanAssignment *plan, const FlowPlanMetrics *metrics, uint64_t budget_us,
                                FlowSMTProofAttestation *proof_out); const char *flow_smt_result_name(FlowSMTResult res);
int verify_candidate(const SemanticIR *ir, const Component *component, const SearchResult *search, VerificationReport *report);
const char *verification_status_name(VerificationStatus status); uint64_t flow_verifier_get_contract_mask(const SemanticIR *ir, const Component *comp,
                                         const FlowPlanDimensionSet *dims); uint64_t flow_verifier_get_resource_mask(const SemanticIR *ir, const Component *comp,
                                         const FlowPlanDimensionSet *dims); typedef enum { FLOW_BOX_THEOREM_BUFFER_BOUNDS = 0, FLOW_BOX_THEOREM_MEMORY_QUOTA  = 1,
    FLOW_BOX_THEOREM_SHARD_ISOLATION = 2, FLOW_BOX_THEOREM_DETERMINISM   = 3
} FlowBoxTheoremType; typedef struct { const char *name; uint64_t candidate_value; uint64_t min_bound; uint64_t max_bound; FlowBoxTheoremType theorem;
    const char *violation_msg; /* Optional custom violation description, e.g. "exceeds hardware physical limit" */
} FlowBoxConstraint;
static inline FlowSMTResult flow_smt_verify_box_invariants(const char *subsystem_tag, const FlowBoxConstraint *constraints, size_t constraint_count,
                                                           FlowSMTProofAttestation *proof_out) {
    FlowSMTResult res[4] = {FLOW_SMT_PROVEN_UNSAT, FLOW_SMT_PROVEN_UNSAT, FLOW_SMT_PROVEN_UNSAT, FLOW_SMT_PROVEN_UNSAT};
    const FlowBoxConstraint *first_v = NULL; char detail[160] = {0};
    for (size_t i = 0; i < constraint_count; ++i) {
        const FlowBoxConstraint *c = &constraints[i];
        if (c->candidate_value < c->min_bound || c->candidate_value > c->max_bound) {
            if ((int)c->theorem >= 0 && (int)c->theorem < 4) res[c->theorem] = FLOW_SMT_VIOLATION_SAT;
            if (!first_v) {
                first_v = c;
                if (c->violation_msg)
                    snprintf(detail, sizeof(detail), "candidate %s %llu %s %llu", c->name ? c->name : "parameter", (unsigned long long)c->candidate_value, c->violation_msg, (unsigned long long)c->max_bound);
                else
                    snprintf(detail, sizeof(detail), "%s candidate %llu %s bound %llu", c->name ? c->name : "value", (unsigned long long)c->candidate_value, c->candidate_value > c->max_bound ? "exceeds upper" : "below lower", (unsigned long long)(c->candidate_value > c->max_bound ? c->max_bound : c->min_bound));
            }
        }
    }
    if (proof_out) {
        proof_out->buffer_bounds_safety = res[0]; proof_out->memory_quota_bound = res[1]; proof_out->shard_non_aliasing = res[2]; proof_out->determinism_invariant = res[3];
        if (first_v) snprintf(proof_out->proof_summary, sizeof(proof_out->proof_summary), "SMT VIOLATION: %s", detail);
        else snprintf(proof_out->proof_summary, sizeof(proof_out->proof_summary), "SMT BOX SOUND [%s]: %zu hyper-box constraints verified QF_LIA UNSAT", subsystem_tag ? subsystem_tag : "Core", constraint_count);
    }
    return first_v ? FLOW_SMT_VIOLATION_SAT : FLOW_SMT_PROVEN_UNSAT;
}
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 5, 6)))
#endif
static inline FlowSMTResult flow_smt_verify_rules(const char *tag, const FlowBoxConstraint *rules, size_t count, FlowSMTProofAttestation *proof_out, const char *summary_fmt, ...) {
    FlowSMTResult res = flow_smt_verify_box_invariants(tag, rules, count, proof_out);
    if (res == FLOW_SMT_PROVEN_UNSAT && proof_out && summary_fmt) {
        va_list ap;
        va_start(ap, summary_fmt);
        vsnprintf(proof_out->proof_summary, sizeof(proof_out->proof_summary), summary_fmt, ap);
        va_end(ap);
    }
    return res;
}
#define FLOW_SMT_RULE(name_str, cand, min_b, max_b, th, msg_str) { (name_str), (uint64_t)(cand), (uint64_t)(min_b), (uint64_t)(max_b), (th), (msg_str) }
#define FLOW_SMT_VERIFY_RULES(tag, proof_out, rules_arr, ...) flow_smt_verify_rules((tag), (rules_arr), sizeof(rules_arr) / sizeof((rules_arr)[0]), (proof_out), __VA_ARGS__)

/* ============================================================================
 * Capability Security, Composition Gates & MTD Layout (folded into smt.h)
 * ============================================================================ */
typedef struct FlowJet FlowJet;
typedef struct FlowJITCodeBlock FlowJITCodeBlock;
typedef struct FlowLayoutMigrationSpec FlowLayoutMigrationSpec;
typedef enum { FLOW_SECURITY_PASS = 0, FLOW_SECURITY_CONTRACT_VIOLATION = 1, FLOW_SECURITY_MEMORY_VIOLATION = 2, FLOW_SECURITY_RESOURCE_EXHAUSTION = 3,
    FLOW_SECURITY_TIMEOUT = 4, FLOW_SECURITY_DIVERGENCE = 5, FLOW_SECURITY_INCOMPLETE = 6, FLOW_SECURITY_PHYSICAL_BREACH = 7 } FlowSecurityOutcome;
typedef struct { uint64_t seed, base_genome, mutated_genome; uint32_t mutated_bit, round; } FlowSecurityCase;
typedef FlowSecurityOutcome (*FlowSecurityProbeFn)(const FlowSecurityCase *security_case, void *userdata, char *message, size_t message_size);
typedef struct { uint64_t seed, base_genome; uint32_t genome_bits, rounds; size_t checks, failures; FlowSecurityOutcome first_failure;
    uint32_t first_failure_round, first_failure_bit; uint64_t first_failure_genome; char first_failure_message[160]; } FlowSecurityReport;
typedef struct { const SemanticIR *ir; const Component *component; const FlowPlanAssignment *plan; const FlowPlanMetrics *metrics;
    size_t concurrency_threads, memory_limit_bytes; int reload_adapter_enabled, read_only_ownership; size_t composed_component_count, total_composed_bytes;
    const FlowJet *jet; double max_hamiltonian_drift_ratio, max_velocity_bound, max_acceleration_bound; } FlowCompositionSpec;
int flow_security_run(uint64_t seed, uint64_t base_genome, uint32_t genome_bits, uint32_t rounds, FlowSecurityProbeFn probe, void *userdata, FlowSecurityReport *report);
const char *flow_security_outcome_name(FlowSecurityOutcome outcome);
FlowSecurityOutcome flow_security_check_contract_gate(const FlowCompositionSpec *spec, char *message, size_t message_size);
FlowSecurityOutcome flow_security_check_abi_migration_gate(const FlowCompositionSpec *spec, char *message, size_t message_size);
FlowSecurityOutcome flow_security_check_ownership_gate(const FlowCompositionSpec *spec, char *message, size_t message_size);
FlowSecurityOutcome flow_security_check_resource_quota_gate(const FlowCompositionSpec *spec, char *message, size_t message_size);
FlowSecurityOutcome flow_security_check_physical_barrier_gate(const FlowCompositionSpec *spec, char *message, size_t message_size);
FlowSecurityOutcome flow_security_check_lean_proof_gate(const FlowCompositionSpec *spec, char *message, size_t message_size);
FlowSecurityOutcome flow_security_check_composition_gate(const FlowCompositionSpec *spec, char *message, size_t message_size);
FlowSecurityOutcome flow_security_check_jit_wx_gate(const FlowJITCodeBlock *block, uintptr_t write_base, uintptr_t exec_base, char *message, size_t message_size);
FlowSecurityOutcome flow_security_check_transposition_gate(const FlowLayoutMigrationSpec *spec, size_t buffer_bytes, char *message, size_t message_size);
#define FLOW_JET_ATTRACTOR_MAX_DIM 64
typedef struct { uint32_t dim; double q_center[FLOW_JET_ATTRACTOR_MAX_DIM], p_center[FLOW_JET_ATTRACTOR_MAX_DIM], max_radius_q, max_radius_p, max_koopman_trace; } FlowJetAttractorProfile;
int flow_jet_attractor_profile_init(FlowJetAttractorProfile *profile, uint32_t dim, double r_q, double r_p, double max_trace);
FlowSecurityOutcome flow_security_check_attractor_anomaly(const FlowJet *jet, const FlowJetAttractorProfile *profile, char *message, size_t message_size);
FlowSecurityOutcome flow_security_check_continuity_proof(const FlowJet *prev_jet, const FlowJet *curr_jet, double dt, double max_jerk, double noise_tolerance, char *message, size_t message_size);
typedef struct { double time_to_breach_s; int will_breach; double projected_breach_velocity; } FlowPredictiveBreachReport;
int flow_security_predict_resource_breach(double current_usage, double consumption_rate, double consumption_accel, double quota_limit, double time_horizon_s, FlowPredictiveBreachReport *report);
int flow_security_should_proactive_morph(const FlowPredictiveBreachReport *report, double proactive_lead_time_s);
uint64_t flow_security_get_safety_mask(const SemanticIR *ir, const Component *comp, const FlowPlanDimensionSet *dims);
#define FLOW_MTD_MAX_FIELDS 16
typedef struct { uint64_t seed; size_t field_count, field_order[FLOW_MTD_MAX_FIELDS], field_offsets[FLOW_MTD_MAX_FIELDS], padding_bytes[FLOW_MTD_MAX_FIELDS], total_size, required_alignment;
    uint64_t canary_token; double shannon_entropy; } FlowMTDLayout;
typedef struct { int enabled; double min_entropy_threshold; size_t morph_interval_ops, max_padding_jitter; } FlowMTDPolicy;
int flow_security_mtd_generate_layout(uint64_t seed, size_t field_count, const size_t *field_sizes, const size_t *field_alignments, size_t max_padding_jitter, FlowMTDLayout *layout_out);
double flow_security_mtd_calculate_entropy(const FlowMTDLayout *layout);
int flow_security_mtd_verify_alignment(const FlowMTDLayout *layout, const size_t *field_alignments);
typedef enum { FLOW_COMPLIANCE_PERMISSIVE_STAGING = 0, FLOW_COMPLIANCE_STRICT_PROD = 1 } FlowComplianceMode;
uint64_t flow_security_get_compliance_mask(FlowComplianceMode mode, const FlowPlanDimensionSet *dims);
#endif
