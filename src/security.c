#include "security.h"
#include "registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t flow_security_next(uint64_t *state) {
    uint64_t value = *state;
    if (value == 0) value = UINT64_C(0x9e3779b97f4a7c15);
    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    *state = value;
    return value;
}

const char *flow_security_outcome_name(FlowSecurityOutcome outcome) {
    switch (outcome) {
        case FLOW_SECURITY_PASS: return "pass";
        case FLOW_SECURITY_CONTRACT_VIOLATION: return "contract_violation";
        case FLOW_SECURITY_MEMORY_VIOLATION: return "memory_violation";
        case FLOW_SECURITY_RESOURCE_EXHAUSTION: return "resource_exhaustion";
        case FLOW_SECURITY_TIMEOUT: return "timeout";
        case FLOW_SECURITY_DIVERGENCE: return "divergence";
        case FLOW_SECURITY_INCOMPLETE: return "incomplete";
        default: return "unknown";
    }
}

static void flow_security_record_failure(FlowSecurityReport *report,
                                         const FlowSecurityCase *security_case,
                                         FlowSecurityOutcome outcome,
                                         const char *message) {
    report->failures++;
    if (report->failures != 1) return;
    report->first_failure = outcome;
    report->first_failure_round = security_case->round;
    report->first_failure_bit = security_case->mutated_bit;
    report->first_failure_genome = security_case->mutated_genome;
    if (message != NULL) {
        strncpy(report->first_failure_message, message,
                sizeof(report->first_failure_message) - 1);
        report->first_failure_message[
            sizeof(report->first_failure_message) - 1] = '\0';
    }
}

int flow_security_run(uint64_t seed, uint64_t base_genome, uint32_t genome_bits,
                      uint32_t rounds, FlowSecurityProbeFn probe,
                      void *userdata, FlowSecurityReport *report) {
    uint64_t state;
    uint32_t round;
    if (probe == NULL || report == NULL || genome_bits == 0 ||
        genome_bits > 64 || rounds == 0)
        return -1;
    memset(report, 0, sizeof(*report));
    report->seed = seed;
    report->base_genome = base_genome;
    report->genome_bits = genome_bits;
    report->rounds = rounds;
    report->first_failure = FLOW_SECURITY_PASS;
    state = seed == 0 ? UINT64_C(0x9e3779b97f4a7c15) : seed;

    {
        FlowSecurityCase base = {seed, base_genome, base_genome, UINT32_MAX,
                                 UINT32_MAX};
        char message[160] = {0};
        FlowSecurityOutcome outcome = probe(&base, userdata, message,
                                            sizeof(message));
        report->checks++;
        if (outcome != FLOW_SECURITY_PASS) {
            flow_security_record_failure(report, &base, outcome, message);
            return 1;
        }
    }

    for (round = 0; round < rounds; ++round) {
        uint64_t random = flow_security_next(&state);
        uint32_t bit = (uint32_t)(random % genome_bits);
        FlowSecurityCase security_case = {
            seed, base_genome, base_genome ^ (UINT64_C(1) << bit), bit, round};
        char message[160] = {0};
        FlowSecurityOutcome outcome = probe(&security_case, userdata, message,
                                            sizeof(message));
        report->checks++;
        if (outcome != FLOW_SECURITY_PASS) {
            flow_security_record_failure(report, &security_case, outcome,
                                         message);
            return 1;
        }
    }
    return 0;
}

int flow_security_write_attestation(FILE *output, const char *component,
                                    const FlowSecurityReport *report) {
    if (output == NULL || component == NULL || report == NULL) return 0;
    if (report->failures == 0) {
        fprintf(output,
                "security_attestation component=%s status=verified seed=%llu "
                "genome=0x%016llx bits=%u rounds=%u checks=%zu failures=0\n",
                component, (unsigned long long)report->seed,
                (unsigned long long)report->base_genome, report->genome_bits,
                report->rounds, report->checks);
    } else {
        fprintf(output,
                "security_attestation component=%s status=rejected "
                "seed=%llu genome=0x%016llx bits=%u rounds=%u checks=%zu "
                "failures=%zu outcome=%s round=%u bit=%u mutated=0x%016llx "
                "message=%s\n",
                component, (unsigned long long)report->seed,
                (unsigned long long)report->base_genome, report->genome_bits,
                report->rounds, report->checks, report->failures,
                flow_security_outcome_name(report->first_failure),
                report->first_failure_round, report->first_failure_bit,
                (unsigned long long)report->first_failure_genome,
                report->first_failure_message);
    }
    return ferror(output) == 0;
}

/* ========================================================================= */
/* Linker Hard Gates Implementation                                          */
/* ========================================================================= */

FlowSecurityOutcome flow_security_check_contract_gate(
    const FlowCompositionSpec *spec, char *message, size_t message_size) {
    if (spec == NULL || spec->component == NULL) {
        if (message && message_size) snprintf(message, message_size, "null spec or component");
        return FLOW_SECURITY_CONTRACT_VIOLATION;
    }
    if (spec->ir != NULL) {
        if (!component_compatible(spec->ir, spec->component)) {
            if (message && message_size)
                snprintf(message, message_size,
                         "component '%s' violates semantic contract invariants",
                         spec->component->id);
            return FLOW_SECURITY_CONTRACT_VIOLATION;
        }
    }
    return FLOW_SECURITY_PASS;
}

FlowSecurityOutcome flow_security_check_abi_migration_gate(
    const FlowCompositionSpec *spec, char *message, size_t message_size) {
    if (spec == NULL || spec->component == NULL) {
        if (message && message_size) snprintf(message, message_size, "null spec or component");
        return FLOW_SECURITY_DIVERGENCE;
    }
    if (spec->reload_adapter_enabled) {
        if (!flow_component_supports_reload(spec->component)) {
            if (message && message_size)
                snprintf(message, message_size,
                         "component '%s' does not satisfy reload adapter ABI invariants",
                         spec->component->id);
            return FLOW_SECURITY_DIVERGENCE;
        }
    }
    return FLOW_SECURITY_PASS;
}

FlowSecurityOutcome flow_security_check_ownership_gate(
    const FlowCompositionSpec *spec, char *message, size_t message_size) {
    int is_read_only;
    if (spec == NULL || spec->component == NULL) {
        if (message && message_size) snprintf(message, message_size, "null spec or component");
        return FLOW_SECURITY_MEMORY_VIOLATION;
    }
    is_read_only = spec->read_only_ownership ||
                   (spec->ir != NULL && spec->ir->fact_mutability_read_only);
    if (is_read_only && spec->concurrency_threads > 1 &&
        (spec->ir != NULL && spec->ir->state_shared && !spec->component->supports_read_heavy)) {
        if (message && message_size)
            snprintf(message, message_size,
                     "read-only ownership boundary breached by uncoordinated concurrent writer");
        return FLOW_SECURITY_MEMORY_VIOLATION;
    }
    if (spec->concurrency_threads > 1 && spec->ir != NULL &&
        spec->ir->state_shared && !spec->component->supports_shared) {
        if (message && message_size)
            snprintf(message, message_size,
                     "concurrent execution on unshared state causes race divergence");
        return FLOW_SECURITY_CONTRACT_VIOLATION;
    }
    return FLOW_SECURITY_PASS;
}

FlowSecurityOutcome flow_security_check_resource_quota_gate(
    const FlowCompositionSpec *spec, char *message, size_t message_size) {
    size_t total_memory = 0;
    size_t limit = 0;
    if (spec == NULL || spec->component == NULL) {
        if (message && message_size) snprintf(message, message_size, "null spec or component");
        return FLOW_SECURITY_RESOURCE_EXHAUSTION;
    }
    total_memory += spec->component->memory_fixed_bytes;
    total_memory += spec->total_composed_bytes;
    if (spec->metrics != NULL) {
        total_memory += spec->metrics->memory_bytes;
    }
    limit = spec->memory_limit_bytes;
    if (limit == 0 && spec->ir != NULL && spec->ir->memory_limit_mb > 0) {
        limit = (size_t)spec->ir->memory_limit_mb * 1024u * 1024u;
    }
    if (limit > 0 && total_memory > limit) {
        if (message && message_size)
            snprintf(message, message_size,
                     "composed memory (%zu bytes) exceeds quota (%zu bytes)",
                     total_memory, limit);
        return FLOW_SECURITY_RESOURCE_EXHAUSTION;
    }
    if (spec->ir != NULL && spec->ir->top_n > 0 && spec->metrics != NULL &&
        spec->metrics->capacity > 0 && (size_t)spec->ir->top_n > spec->metrics->capacity) {
        if (message && message_size)
            snprintf(message, message_size,
                     "top_n (%d) exceeds candidate capacity (%zu)",
                     spec->ir->top_n, spec->metrics->capacity);
        return FLOW_SECURITY_RESOURCE_EXHAUSTION;
    }
    return FLOW_SECURITY_PASS;
}

FlowSecurityOutcome flow_security_check_composition_gate(
    const FlowCompositionSpec *spec, char *message, size_t message_size) {
    FlowSecurityOutcome outcome;
    outcome = flow_security_check_contract_gate(spec, message, message_size);
    if (outcome != FLOW_SECURITY_PASS) return outcome;
    outcome = flow_security_check_abi_migration_gate(spec, message, message_size);
    if (outcome != FLOW_SECURITY_PASS) return outcome;
    outcome = flow_security_check_ownership_gate(spec, message, message_size);
    if (outcome != FLOW_SECURITY_PASS) return outcome;
    outcome = flow_security_check_resource_quota_gate(spec, message, message_size);
    if (outcome != FLOW_SECURITY_PASS) return outcome;
    return FLOW_SECURITY_PASS;
}

/* ========================================================================= */
/* Compositional 1-Bit Chaos Fuzzer & Auditor                                */
/* ========================================================================= */

#include "bitspace.h"

typedef struct {
    const FlowCompositionSpec *base_spec;
    FlowBitSpace space;
} CompositionProbeContext;

static FlowSecurityOutcome composition_probe_fn(
    const FlowSecurityCase *security_case, void *userdata, char *message,
    size_t message_size) {
    CompositionProbeContext *ctx = (CompositionProbeContext *)userdata;
    const FlowCompositionSpec *base = ctx->base_spec;
    FlowCompositionSpec mutated_spec = *base;
    FlowPlan cand_plan;
    FlowPlanMetrics mutated_metrics;

    if (!ctx->space.decode(&ctx->space, security_case->mutated_genome, &cand_plan)) {
        if (message && message_size)
            snprintf(message, message_size, "mutated genome decode failed");
        return FLOW_SECURITY_CONTRACT_VIOLATION;
    }
    if (!ctx->space.evaluate(&ctx->space, &cand_plan, &cand_plan.eval)) {
        if (message && message_size)
            snprintf(message, message_size, "mutated plan evaluation failed");
        return FLOW_SECURITY_CONTRACT_VIOLATION;
    }

    memset(&mutated_metrics, 0, sizeof(mutated_metrics));
    mutated_metrics.energy = cand_plan.eval.energy;
    mutated_metrics.latency_score = cand_plan.eval.latency_score;
    mutated_metrics.throughput_score = cand_plan.eval.throughput_score;
    mutated_metrics.memory_bytes = cand_plan.eval.memory_bytes;
    mutated_metrics.capacity = cand_plan.eval.capacity;

    mutated_spec.plan = &cand_plan.assignment;
    mutated_spec.metrics = &mutated_metrics;

    return flow_security_check_composition_gate(&mutated_spec, message, message_size);
}

int flow_security_audit_composition(const FlowCompositionSpec *spec,
                                   uint64_t seed, uint32_t rounds,
                                   FlowSecurityReport *report) {
    CompositionProbeContext ctx;
    uint32_t genome_bits = 0;
    char message[160] = {0};
    FlowSecurityOutcome base_outcome;

    if (spec == NULL || report == NULL) return -1;

    base_outcome = flow_security_check_composition_gate(spec, message, sizeof(message));
    if (base_outcome != FLOW_SECURITY_PASS) {
        memset(report, 0, sizeof(*report));
        report->seed = seed;
        report->checks = 1;
        report->failures = 1;
        report->first_failure = base_outcome;
        strncpy(report->first_failure_message, message, sizeof(report->first_failure_message) - 1);
        return 1;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.base_spec = spec;
    if (spec->component != NULL) {
        flow_bitspace_init_single(spec->ir, spec->component, &ctx.space);
        genome_bits = ctx.space.bit_count;
    }
    if (genome_bits == 0) genome_bits = 16;
    if (rounds == 0) rounds = 32;

    return flow_security_run(seed, UINT64_C(0), genome_bits, rounds,
                             composition_probe_fn, &ctx, report);
}

int flow_security_write_composition_attestation(
    FILE *output, const FlowCompositionSpec *spec,
    const FlowSecurityReport *report) {
    if (output == NULL || spec == NULL || report == NULL) return 0;
    const char *comp_id = spec->component ? spec->component->id : "unknown";
    if (report->failures == 0) {
        fprintf(output,
                "composition_security_attestation component=%s status=verified "
                "threads=%zu reload=%d quota_limit=%zu checks=%zu failures=0\n",
                comp_id, spec->concurrency_threads,
                spec->reload_adapter_enabled, spec->memory_limit_bytes,
                report->checks);
    } else {
        fprintf(output,
                "composition_security_attestation component=%s status=rejected "
                "outcome=%s round=%u bit=%u error=\"%s\"\n",
                comp_id, flow_security_outcome_name(report->first_failure),
                report->first_failure_round, report->first_failure_bit,
                report->first_failure_message);
    }
    return ferror(output) == 0;
}
