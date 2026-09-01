#ifndef FLOW_SECURITY_H
#define FLOW_SECURITY_H

#include "flow.h"
#include "plugin.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
    FLOW_SECURITY_PASS = 0,
    FLOW_SECURITY_CONTRACT_VIOLATION = 1,
    FLOW_SECURITY_MEMORY_VIOLATION = 2,
    FLOW_SECURITY_RESOURCE_EXHAUSTION = 3,
    FLOW_SECURITY_TIMEOUT = 4,
    FLOW_SECURITY_DIVERGENCE = 5,
    FLOW_SECURITY_INCOMPLETE = 6
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
} FlowCompositionSpec;

/* Low-level 1-bit chaotic mutation test runner */
int flow_security_run(uint64_t seed, uint64_t base_genome, uint32_t genome_bits,
                      uint32_t rounds, FlowSecurityProbeFn probe,
                      void *userdata, FlowSecurityReport *report);

const char *flow_security_outcome_name(FlowSecurityOutcome outcome);
int flow_security_write_attestation(FILE *output, const char *component,
                                    const FlowSecurityReport *report);

/* 5 Linker Hard-Gate checkers */
FlowSecurityOutcome flow_security_check_contract_gate(
    const FlowCompositionSpec *spec, char *message, size_t message_size);

FlowSecurityOutcome flow_security_check_abi_migration_gate(
    const FlowCompositionSpec *spec, char *message, size_t message_size);

FlowSecurityOutcome flow_security_check_ownership_gate(
    const FlowCompositionSpec *spec, char *message, size_t message_size);

FlowSecurityOutcome flow_security_check_resource_quota_gate(
    const FlowCompositionSpec *spec, char *message, size_t message_size);

FlowSecurityOutcome flow_security_check_composition_gate(
    const FlowCompositionSpec *spec, char *message, size_t message_size);

/* Full compositional security audit with 1-bit chaos probing */
int flow_security_audit_composition(const FlowCompositionSpec *spec,
                                   uint64_t seed, uint32_t rounds,
                                   FlowSecurityReport *report);

int flow_security_write_composition_attestation(
    FILE *output, const FlowCompositionSpec *spec,
    const FlowSecurityReport *report);

#endif
