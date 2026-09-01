#include "security.h"
#include "registry.h"
#include "bitspace.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SEC_CHECK(cond) if (!(cond)) { fprintf(stderr, "security-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; }

typedef struct {
    unsigned calls;
    unsigned fail_at;
} SecurityProbeState;

static int one_bit_difference(uint64_t left, uint64_t right) {
    uint64_t value = left ^ right;
    if (value == 0) return 0;
    while ((value & 1u) == 0) value >>= 1;
    return (value & (value - 1u)) == 0;
}

static FlowSecurityOutcome probe(const FlowSecurityCase *security_case,
                                 void *userdata, char *message,
                                 size_t message_size) {
    SecurityProbeState *state = userdata;
    if (security_case->round != UINT32_MAX &&
        !one_bit_difference(security_case->base_genome,
                            security_case->mutated_genome)) {
        snprintf(message, message_size, "mutation is not one bit");
        return FLOW_SECURITY_CONTRACT_VIOLATION;
    }
    if (state->calls++ == state->fail_at) {
        snprintf(message, message_size, "intentional hard-gate probe failure");
        return FLOW_SECURITY_RESOURCE_EXHAUSTION;
    }
    return FLOW_SECURITY_PASS;
}

int main(void) {
    FlowSecurityReport report;
    SecurityProbeState passing = {0, UINT_MAX};
    SecurityProbeState failing = {0, 9};
    int status;
    char msg[128];

    if (!flow_registry_init()) return 1;

    /* --------------------------------------------------------------------- */
    /* 1. Low-level 1-bit chaotic mutation engine verification               */
    /* --------------------------------------------------------------------- */
    status = flow_security_run(UINT64_C(0x123456789abcdef0),
                               UINT64_C(0x00000000000abcde), 20, 32, probe,
                               &passing, &report);
    SEC_CHECK(status == 0 && report.checks == 33 && report.failures == 0);
    SEC_CHECK(flow_security_write_attestation(stdout, "security-test", &report));

    status = flow_security_run(UINT64_C(0x123456789abcdef0),
                               UINT64_C(0x00000000000abcde), 20, 32, probe,
                               &failing, &report);
    SEC_CHECK(status == 1 && report.failures == 1 &&
              report.first_failure == FLOW_SECURITY_RESOURCE_EXHAUSTION);

    /* --------------------------------------------------------------------- */
    /* 2. Composition Hard Gates: Baseline Valid Composition                 */
    /* --------------------------------------------------------------------- */
    const FlowPlugin *builtin = flow_registry_lookup("builtin");
    const Component *comp_array = NULL;
    size_t c_idx;
    for (c_idx = 0; c_idx < builtin->component_count; ++c_idx) {
        if (strcmp(builtin->components[c_idx].id, "linear_array") == 0) {
            comp_array = &builtin->components[c_idx];
            break;
        }
    }
    SEC_CHECK(comp_array != NULL);

    SemanticIR ir;
    memset(&ir, 0, sizeof(ir));
    strncpy(ir.flow_name, "test_flow", sizeof(ir.flow_name) - 1);
    ir.input_max_count = 64;
    ir.memory_limit_mb = 1;
    ir.fact_unordered = 1;

    FlowPlanAssignment plan;
    memset(&plan, 0, sizeof(plan));
    plan.count = 3;
    plan.values[0] = 64;  /* capacity */
    plan.values[1] = 1;   /* threads */
    plan.values[2] = 1;   /* shards */

    FlowPlanMetrics metrics;
    memset(&metrics, 0, sizeof(metrics));
    metrics.capacity = 64;
    metrics.threads = 1;
    metrics.shards = 1;
    metrics.memory_bytes = 256;

    FlowCompositionSpec valid_spec;
    memset(&valid_spec, 0, sizeof(valid_spec));
    valid_spec.ir = &ir;
    valid_spec.component = comp_array;
    valid_spec.plan = &plan;
    valid_spec.metrics = &metrics;
    valid_spec.concurrency_threads = 1;
    valid_spec.memory_limit_bytes = 1024 * 1024;
    valid_spec.reload_adapter_enabled = 0;

    status = flow_security_audit_composition(&valid_spec, UINT64_C(0x42), 16, &report);
    SEC_CHECK(status == 0 && report.failures == 0);
    SEC_CHECK(flow_security_write_composition_attestation(stdout, &valid_spec, &report));

    /* --------------------------------------------------------------------- */
    /* 3. Resource Quota Boundary Breach                                     */
    /* --------------------------------------------------------------------- */
    FlowCompositionSpec quota_breach_spec = valid_spec;
    quota_breach_spec.memory_limit_bytes = 128; /* Quota is smaller than 256 bytes needed */
    FlowSecurityOutcome outcome = flow_security_check_resource_quota_gate(&quota_breach_spec, msg, sizeof(msg));
    SEC_CHECK(outcome == FLOW_SECURITY_RESOURCE_EXHAUSTION);

    /* --------------------------------------------------------------------- */
    /* 4. Ownership & Concurrency Boundary Breach                            */
    /* --------------------------------------------------------------------- */
    FlowCompositionSpec ownership_breach_spec = valid_spec;
    ownership_breach_spec.read_only_ownership = 1;
    ownership_breach_spec.concurrency_threads = 4;
    ir.state_shared = 1;
    outcome = flow_security_check_ownership_gate(&ownership_breach_spec, msg, sizeof(msg));
    SEC_CHECK(outcome == FLOW_SECURITY_MEMORY_VIOLATION);
    ir.state_shared = 0;

    /* --------------------------------------------------------------------- */
    /* 5. ABI & Migration Divergence Breach                                  */
    /* --------------------------------------------------------------------- */
    Component unreloadable_comp = *comp_array;
    unreloadable_comp.reload_capable = 0;
    FlowCompositionSpec abi_breach_spec = valid_spec;
    abi_breach_spec.component = &unreloadable_comp;
    abi_breach_spec.reload_adapter_enabled = 1;
    outcome = flow_security_check_abi_migration_gate(&abi_breach_spec, msg, sizeof(msg));
    SEC_CHECK(outcome == FLOW_SECURITY_DIVERGENCE);

    /* --------------------------------------------------------------------- */
    /* 6. Multi-Component Emergent Composition Quota Overflow                */
    /* --------------------------------------------------------------------- */
    FlowCompositionSpec multi_comp_spec = valid_spec;
    multi_comp_spec.memory_limit_bytes = 1000;
    multi_comp_spec.total_composed_bytes = 600; /* Other component uses 600 bytes */
    metrics.memory_bytes = 500;                  /* Current component uses 500 bytes -> total 1100 > 1000 */
    outcome = flow_security_check_resource_quota_gate(&multi_comp_spec, msg, sizeof(msg));
    SEC_CHECK(outcome == FLOW_SECURITY_RESOURCE_EXHAUSTION);

    /* --------------------------------------------------------------------- */
    /* 7. Hierarchical Multi-Candidate BitSpace Security Audit                */
    /* --------------------------------------------------------------------- */
    SemanticIR hier_ir = ir;
    hier_ir.state_shared = 1;
    hier_ir.state_read_heavy = 0;
    hier_ir.fact_ordered = 1;
    hier_ir.fact_unordered = 0;
    FlowBitSpace hier_space;
    SEC_CHECK(flow_bitspace_init_for_ir(&hier_ir, &hier_space));
    SEC_CHECK(hier_space.candidate_count >= 2);
    FlowBitSearchResult hier_audit;
    SEC_CHECK(flow_bitspace_search(&hier_space, 32, 12345, 0, NULL, &hier_audit));
    SEC_CHECK(hier_audit.best_plan.component != NULL);

    printf("FLOW_SECURITY_TEST=passed checks=%zu rejected=%zu outcome=%s\n",
           report.checks, (size_t)1,
           flow_security_outcome_name(FLOW_SECURITY_RESOURCE_EXHAUSTION));
    return 0;
}
