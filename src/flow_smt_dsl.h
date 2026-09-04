#ifndef FLOW_SMT_DSL_H
#define FLOW_SMT_DSL_H

#include "smt.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW SMT Hyper-Box Constraint Verification DSL (flow_smt_dsl.h)
 * ============================================================================
 *
 * Provides a pure C17 fluent builder and macro DSL to assemble and verify
 * hyper-box polytope invariants with zero heap allocation.
 * ============================================================================
 */

#define FLOW_SMT_BUILDER_MAX_RULES 32

typedef struct {
    FlowBoxConstraint rules[FLOW_SMT_BUILDER_MAX_RULES];
    size_t count;
} FlowSMTBoxBuilder;

static inline void flow_smt_box_builder_init(FlowSMTBoxBuilder *b) {
    if (b) {
        memset(b, 0, sizeof(*b));
    }
}

static inline int flow_smt_box_builder_add(FlowSMTBoxBuilder *b,
                                           const char *name,
                                           uint64_t candidate,
                                           uint64_t min_bound,
                                           uint64_t max_bound,
                                           FlowBoxTheoremType theorem,
                                           const char *violation_msg) {
    if (!b || b->count >= FLOW_SMT_BUILDER_MAX_RULES) return 0;
    FlowBoxConstraint *c = &b->rules[b->count++];
    c->name = name;
    c->candidate_value = candidate;
    c->min_bound = min_bound;
    c->max_bound = max_bound;
    c->theorem = theorem;
    c->violation_msg = violation_msg;
    return 1;
}

static inline FlowSMTResult flow_smt_box_builder_verify(const FlowSMTBoxBuilder *b,
                                                        const char *tag,
                                                        FlowSMTProofAttestation *proof_out) {
    if (!b) return FLOW_SMT_UNKNOWN;
    return flow_smt_verify_box_invariants(tag, b->rules, b->count, proof_out);
}

#define FLOW_SMT_BOX_BUILDER_DECL(var_name) \
    FlowSMTBoxBuilder var_name; \
    flow_smt_box_builder_init(&var_name)

#define FLOW_SMT_BOX_ADD_RULE(builder, name_str, cand, min_b, max_b, th, msg_str) \
    flow_smt_box_builder_add(&(builder), (name_str), (uint64_t)(cand), (uint64_t)(min_b), (uint64_t)(max_b), (th), (msg_str))

#define FLOW_SMT_BOX_VERIFY(builder, tag_str, proof_ptr) \
    flow_smt_box_builder_verify(&(builder), (tag_str), (proof_ptr))

#ifdef __cplusplus
}
#endif

#endif /* FLOW_SMT_DSL_H */
