#ifndef FLOW_SECURITY_H
#define FLOW_SECURITY_H

#include "flow.h"
#include "plugin.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct FlowJet FlowJet;
typedef struct FlowJITCodeBlock FlowJITCodeBlock;
typedef struct FlowLayoutMigrationSpec FlowLayoutMigrationSpec;
typedef struct FlowJetDeadReckonPacket FlowJetDeadReckonPacket;

typedef enum {
    FLOW_SECURITY_PASS = 0,
    FLOW_SECURITY_CONTRACT_VIOLATION = 1,
    FLOW_SECURITY_MEMORY_VIOLATION = 2,
    FLOW_SECURITY_RESOURCE_EXHAUSTION = 3,
    FLOW_SECURITY_TIMEOUT = 4,
    FLOW_SECURITY_DIVERGENCE = 5,
    FLOW_SECURITY_INCOMPLETE = 6,
    FLOW_SECURITY_PHYSICAL_BREACH = 7
} FlowSecurityOutcome;

typedef struct {
    uint64_t seed;
    uint64_t base_genome;
    uint64_t mutated_genome;
    uint32_t mutated_bit;
    uint32_t round;
} FlowSecurityCase;

typedef FlowSecurityOutcome (*FlowSecurityProbeFn)(
    const FlowSecurityCase *security_case, void *userdata, char *message,
    size_t message_size);

typedef struct {
    uint64_t seed;
    uint64_t base_genome;
    uint32_t genome_bits;
    uint32_t rounds;
    size_t checks;
    size_t failures;
    FlowSecurityOutcome first_failure;
    uint32_t first_failure_round;
    uint32_t first_failure_bit;
    uint64_t first_failure_genome;
    char first_failure_message[160];
} FlowSecurityReport;

/* Composition specification submitted to the Linker Hard Gates */
typedef struct {
    const SemanticIR *ir;
    const Component *component;
    const FlowPlanAssignment *plan;
    const FlowPlanMetrics *metrics;
    size_t concurrency_threads;
    size_t memory_limit_bytes;
    int reload_adapter_enabled;
    int read_only_ownership;
    size_t composed_component_count;
    size_t total_composed_bytes;
    const FlowJet *jet;
    double max_hamiltonian_drift_ratio;  /* e.g. 0.05 (5% drift tolerance) */
    double max_velocity_bound;           /* Upper bound for |p_i| */
    double max_acceleration_bound;       /* Upper bound for |a_i| */
} FlowCompositionSpec;

/* Low-level 1-bit chaotic mutation test runner */
int flow_security_run(uint64_t seed, uint64_t base_genome, uint32_t genome_bits,
                      uint32_t rounds, FlowSecurityProbeFn probe,
                      void *userdata, FlowSecurityReport *report);

const char *flow_security_outcome_name(FlowSecurityOutcome outcome);
int flow_security_write_attestation(FILE *output, const char *component,
                                    const FlowSecurityReport *report);

/* 6 Linker Hard-Gate checkers */
FlowSecurityOutcome flow_security_check_contract_gate(
    const FlowCompositionSpec *spec, char *message, size_t message_size);

FlowSecurityOutcome flow_security_check_abi_migration_gate(
    const FlowCompositionSpec *spec, char *message, size_t message_size);

FlowSecurityOutcome flow_security_check_ownership_gate(
    const FlowCompositionSpec *spec, char *message, size_t message_size);

FlowSecurityOutcome flow_security_check_resource_quota_gate(
    const FlowCompositionSpec *spec, char *message, size_t message_size);

FlowSecurityOutcome flow_security_check_physical_barrier_gate(
    const FlowCompositionSpec *spec, char *message, size_t message_size);

FlowSecurityOutcome flow_security_check_composition_gate(
    const FlowCompositionSpec *spec, char *message, size_t message_size);

/* JIT & Memory Transposition Hard-Gate checkers */
FlowSecurityOutcome flow_security_check_jit_wx_gate(
    const FlowJITCodeBlock *block, uintptr_t write_base, uintptr_t exec_base,
    char *message, size_t message_size);

FlowSecurityOutcome flow_security_check_transposition_gate(
    const FlowLayoutMigrationSpec *spec, size_t buffer_bytes,
    char *message, size_t message_size);

/* ========================================================================= */
/* Phase Space Attractor IDS & Koopman Spectrum Anomaly Gate                 */
/* ========================================================================= */
#define FLOW_JET_ATTRACTOR_MAX_DIM 64

typedef struct {
    uint32_t dim;
    double q_center[FLOW_JET_ATTRACTOR_MAX_DIM];
    double p_center[FLOW_JET_ATTRACTOR_MAX_DIM];
    double max_radius_q;
    double max_radius_p;
    double max_koopman_trace;
} FlowJetAttractorProfile;

int flow_jet_attractor_profile_init(FlowJetAttractorProfile *profile, uint32_t dim,
                                   double r_q, double r_p, double max_trace);

FlowSecurityOutcome flow_security_check_attractor_anomaly(
    const FlowJet *jet, const FlowJetAttractorProfile *profile,
    char *message, size_t message_size);

/* ========================================================================= */
/* Differential Continuity Physical Proof (C1/C2 Anti-Spoofing & Replay)    */
/* ========================================================================= */
FlowSecurityOutcome flow_security_check_continuity_proof(
    const FlowJet *prev_jet, const FlowJet *curr_jet,
    double dt, double max_jerk, double noise_tolerance,
    char *message, size_t message_size);

/* ========================================================================= */
/* Predictive MTD & Second-Order Resource Quota Breach Extrapolation         */
/* ========================================================================= */
typedef struct {
    double time_to_breach_s;
    int will_breach;
    double projected_breach_velocity;
} FlowPredictiveBreachReport;

int flow_security_predict_resource_breach(
    double current_usage, double consumption_rate, double consumption_accel,
    double quota_limit, double time_horizon_s,
    FlowPredictiveBreachReport *report);

int flow_security_should_proactive_morph(
    const FlowPredictiveBreachReport *report, double proactive_lead_time_s);

/* ========================================================================= */
/* Symplectic Byzantine Consensus (Zero-RPC O(1) Hamiltonian & Geodesic)     */
/* ========================================================================= */
typedef struct {
    uint32_t byzantine_faults_detected;
    double max_hamiltonian_drift_tolerance;
    double max_phase_distance_tolerance;
} FlowSymplecticByzantineFilter;

int flow_jet_byzantine_filter_init(
    FlowSymplecticByzantineFilter *filter,
    double max_h_drift, double max_phase_dist);

FlowSecurityOutcome flow_jet_byzantine_validate_packet(
    FlowSymplecticByzantineFilter *filter,
    const FlowJetDeadReckonPacket *packet,
    const FlowJet *local_shadow_mirror,
    char *message, size_t message_size);

/* Full compositional security audit with BMF probing */
int flow_security_audit_composition(const FlowCompositionSpec *spec,
                                   uint64_t seed, uint32_t rounds,
                                   FlowSecurityReport *report);

int flow_security_write_composition_attestation(
    FILE *output, const FlowCompositionSpec *spec,
    const FlowSecurityReport *report);

/* Pre-emptive Safety Mask Generator (Hard Gate 1-Cycle Bitwise Pruning) */
uint64_t flow_security_get_safety_mask(const SemanticIR *ir,
                                       const Component *comp,
                                       const FlowPlanDimensionSet *dims);

/* Moving Target Defense (MTD) Fluid Polymorphic Memory Layout */
#define FLOW_MTD_MAX_FIELDS 16

typedef struct {
    uint64_t seed;
    size_t field_count;
    size_t field_order[FLOW_MTD_MAX_FIELDS];     /* Permuted index of original fields */
    size_t field_offsets[FLOW_MTD_MAX_FIELDS];   /* Dynamic runtime byte offsets */
    size_t padding_bytes[FLOW_MTD_MAX_FIELDS];   /* Non-deterministic inter-field jitter padding */
    size_t total_size;                           /* Total struct size with jitter padding */
    size_t required_alignment;                   /* Natural alignment boundary */
    uint64_t canary_token;                       /* Dynamic canary token for buffer guard */
    double shannon_entropy;                      /* Offset distribution entropy */
} FlowMTDLayout;

typedef struct {
    int enabled;
    double min_entropy_threshold;                /* Default: 2.0 bits */
    size_t morph_interval_ops;                   /* Morph interval in operations */
    size_t max_padding_jitter;                   /* Max extra padding bytes per field */
} FlowMTDPolicy;

int flow_security_mtd_generate_layout(uint64_t seed, size_t field_count,
                                      const size_t *field_sizes,
                                      const size_t *field_alignments,
                                      size_t max_padding_jitter,
                                      FlowMTDLayout *layout_out);
double flow_security_mtd_calculate_entropy(const FlowMTDLayout *layout);
int flow_security_mtd_verify_alignment(const FlowMTDLayout *layout, const size_t *field_alignments);
void flow_security_mtd_report(const FlowMTDLayout *layout, FILE *out);

/* ========================================================================= */
/* Bounded BMF Compliance Mode & Regulatory Production Gate                */
/* ========================================================================= */

typedef enum {
    FLOW_COMPLIANCE_PERMISSIVE_STAGING = 0, /* Full unconstrained 1024-bit exploration */
    FLOW_COMPLIANCE_STRICT_PROD = 1         /* Strictly bounded: algorithm/lifecycle locked, only tuning */
} FlowComplianceMode;

uint64_t flow_security_get_compliance_mask(FlowComplianceMode mode,
                                           const FlowPlanDimensionSet *dims);
int flow_security_is_mutation_compliant(FlowComplianceMode mode,
                                        uint32_t mutated_bit,
                                        const FlowPlanDimensionSet *dims);

#endif
