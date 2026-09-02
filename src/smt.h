#ifndef FLOW_SMT_H
#define FLOW_SMT_H

#include "flow.h"
#include "plugin.h"
#include "search.h"

#include <stdio.h>
#include <stdint.h>

typedef enum {
    FLOW_SMT_PROVEN_UNSAT = 0,    /* Negation is UNSAT -> Theorem holds universally */
    FLOW_SMT_VIOLATION_SAT = 1,   /* Counterexample found -> Invariant violated */
    FLOW_SMT_UNKNOWN = 2          /* Unconstrained or unbounded */
} FlowSMTResult;

typedef struct {
    FlowSMTResult buffer_bounds_safety;
    FlowSMTResult memory_quota_bound;
    FlowSMTResult shard_non_aliasing;
    FlowSMTResult determinism_invariant;
    char proof_summary[256];
} FlowSMTProofAttestation;

/* Generate complete SMT-LIB2 script to output stream */
int flow_smt_generate_proof_script(const SemanticIR *ir,
                                   const Component *component,
                                   const FlowPlanAssignment *plan,
                                   const FlowPlanMetrics *metrics,
                                   FILE *out);

/* Evaluate and formally verify all four theorems */
int flow_smt_verify(const SemanticIR *ir,
                    const Component *component,
                    const FlowPlanAssignment *plan,
                    const FlowPlanMetrics *metrics,
                    FlowSMTProofAttestation *proof_out);

/* Evaluate and formally verify with microsecond time-budget watchdog */
int flow_smt_verify_with_budget(const SemanticIR *ir,
                                const Component *component,
                                const FlowPlanAssignment *plan,
                                const FlowPlanMetrics *metrics,
                                uint64_t budget_us,
                                FlowSMTProofAttestation *proof_out);

const char *flow_smt_result_name(FlowSMTResult res);

#endif
