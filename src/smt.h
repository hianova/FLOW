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
typedef struct FlowSMTProofAttestation { FlowSMTResult buffer_bounds_safety; FlowSMTResult memory_quota_bound; FlowSMTResult shard_non_aliasing;
    FlowSMTResult determinism_invariant; char proof_summary[256]; } FlowSMTProofAttestation; int flow_smt_generate_proof_script(const SemanticIR *ir,
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
} FlowBoxConstraint; FlowSMTResult flow_smt_verify_box_invariants(const char *subsystem_tag, const FlowBoxConstraint *constraints, size_t constraint_count,
                                             FlowSMTProofAttestation *proof_out);
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
#endif
