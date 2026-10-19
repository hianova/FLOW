#ifndef FLOW_SECURITY_H
#define FLOW_SECURITY_H
#include "flow.h"
#include "plugin.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
typedef struct FlowJet FlowJet; typedef struct FlowJITCodeBlock FlowJITCodeBlock; typedef struct FlowLayoutMigrationSpec FlowLayoutMigrationSpec; typedef enum {
    FLOW_SECURITY_PASS = 0, FLOW_SECURITY_CONTRACT_VIOLATION = 1, FLOW_SECURITY_MEMORY_VIOLATION = 2, FLOW_SECURITY_RESOURCE_EXHAUSTION = 3, FLOW_SECURITY_TIMEOUT = 4,
    FLOW_SECURITY_DIVERGENCE = 5, FLOW_SECURITY_INCOMPLETE = 6, FLOW_SECURITY_PHYSICAL_BREACH = 7
} FlowSecurityOutcome; typedef struct { uint64_t seed; uint64_t base_genome; uint64_t mutated_genome; uint32_t mutated_bit; uint32_t round; } FlowSecurityCase;
typedef FlowSecurityOutcome (*FlowSecurityProbeFn)( const FlowSecurityCase *security_case, void *userdata, char *message, size_t message_size); typedef struct { uint64_t seed;
    uint64_t base_genome; uint32_t genome_bits; uint32_t rounds; size_t checks; size_t failures; FlowSecurityOutcome first_failure; uint32_t first_failure_round;
    uint32_t first_failure_bit; uint64_t first_failure_genome; char first_failure_message[160]; } FlowSecurityReport; typedef struct { const SemanticIR *ir;
    const Component *component; const FlowPlanAssignment *plan; const FlowPlanMetrics *metrics; size_t concurrency_threads; size_t memory_limit_bytes;
    int reload_adapter_enabled; int read_only_ownership; size_t composed_component_count; size_t total_composed_bytes; const FlowJet *jet; double max_hamiltonian_drift_ratio;
    double max_velocity_bound; double max_acceleration_bound; } FlowCompositionSpec; int flow_security_run(uint64_t seed, uint64_t base_genome, uint32_t genome_bits,
                      uint32_t rounds, FlowSecurityProbeFn probe, void *userdata, FlowSecurityReport *report);
const char *flow_security_outcome_name(FlowSecurityOutcome outcome);
int flow_security_write_attestation(FILE *output, const char *component, const FlowSecurityReport *report);
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
typedef struct { uint32_t dim; double q_center[FLOW_JET_ATTRACTOR_MAX_DIM]; double p_center[FLOW_JET_ATTRACTOR_MAX_DIM]; double max_radius_q; double max_radius_p;
    double max_koopman_trace; } FlowJetAttractorProfile;
int flow_jet_attractor_profile_init(FlowJetAttractorProfile *profile, uint32_t dim, double r_q, double r_p, double max_trace);
FlowSecurityOutcome flow_security_check_attractor_anomaly(const FlowJet *jet, const FlowJetAttractorProfile *profile, char *message, size_t message_size);
FlowSecurityOutcome flow_security_check_continuity_proof(const FlowJet *prev_jet, const FlowJet *curr_jet, double dt, double max_jerk, double noise_tolerance, char *message, size_t message_size);
typedef struct { double time_to_breach_s; int will_breach; double projected_breach_velocity; } FlowPredictiveBreachReport;
int flow_security_predict_resource_breach(double current_usage, double consumption_rate, double consumption_accel, double quota_limit, double time_horizon_s, FlowPredictiveBreachReport *report);
int flow_security_should_proactive_morph(const FlowPredictiveBreachReport *report, double proactive_lead_time_s);
int flow_security_audit_composition(const FlowCompositionSpec *spec, uint64_t seed, uint32_t rounds, FlowSecurityReport *report);
int flow_security_write_composition_attestation(FILE *output, const FlowCompositionSpec *spec, const FlowSecurityReport *report);
uint64_t flow_security_get_safety_mask(const SemanticIR *ir, const Component *comp, const FlowPlanDimensionSet *dims);
#define FLOW_MTD_MAX_FIELDS 16
typedef struct { uint64_t seed; size_t field_count; size_t field_order[FLOW_MTD_MAX_FIELDS]; size_t field_offsets[FLOW_MTD_MAX_FIELDS];
    size_t padding_bytes[FLOW_MTD_MAX_FIELDS]; size_t total_size; size_t required_alignment; uint64_t canary_token; double shannon_entropy; } FlowMTDLayout; typedef struct {
    int enabled; double min_entropy_threshold; size_t morph_interval_ops; size_t max_padding_jitter; } FlowMTDPolicy;
int flow_security_mtd_generate_layout(uint64_t seed, size_t field_count, const size_t *field_sizes, const size_t *field_alignments, size_t max_padding_jitter, FlowMTDLayout *layout_out);
double flow_security_mtd_calculate_entropy(const FlowMTDLayout *layout); int flow_security_mtd_verify_alignment(const FlowMTDLayout *layout, const size_t *field_alignments);
void flow_security_mtd_report(const FlowMTDLayout *layout, FILE *out); typedef enum { FLOW_COMPLIANCE_PERMISSIVE_STAGING = 0, FLOW_COMPLIANCE_STRICT_PROD = 1
} FlowComplianceMode; uint64_t flow_security_get_compliance_mask(FlowComplianceMode mode, const FlowPlanDimensionSet *dims);
int flow_security_is_mutation_compliant(FlowComplianceMode mode, uint32_t mutated_bit, const FlowPlanDimensionSet *dims);
#endif
