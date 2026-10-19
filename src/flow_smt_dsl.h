#ifndef FLOW_SMT_DSL_H
#define FLOW_SMT_DSL_H
#include "smt.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef __cplusplus
extern "C" {
#endif
#define FLOW_SMT_BUILDER_MAX_RULES 32
typedef struct { FlowBoxConstraint rules[FLOW_SMT_BUILDER_MAX_RULES]; size_t count; } FlowSMTBoxBuilder; static inline void flow_smt_box_builder_init(FlowSMTBoxBuilder *b) {
    if (b) memset(b, 0, sizeof(*b)); } static inline int flow_smt_box_builder_add(FlowSMTBoxBuilder *b, const char *name, uint64_t candidate, uint64_t min_bound,
                                           uint64_t max_bound, FlowBoxTheoremType theorem, const char *violation_msg) {
    if (!b || b->count >= FLOW_SMT_BUILDER_MAX_RULES) return 0; b->rules[b->count++] = (FlowBoxConstraint){ .name = name, .candidate_value = candidate, .min_bound = min_bound,
        .max_bound = max_bound, .theorem = theorem, .violation_msg = violation_msg }; return 1; }
static inline FlowSMTResult flow_smt_box_builder_verify(const FlowSMTBoxBuilder *b, const char *tag, FlowSMTProofAttestation *proof_out) {
    return b ? flow_smt_verify_box_invariants(tag, b->rules, b->count, proof_out) : FLOW_SMT_UNKNOWN; }
static inline FlowSMTResult flow_smt_verify_rules(const char *tag, const FlowBoxConstraint *rules, size_t count, FlowSMTProofAttestation *proof_out,
                                                  const char *summary_fmt, ...) { FlowSMTResult res = flow_smt_verify_box_invariants(tag, rules, count, proof_out);
    if (res == FLOW_SMT_PROVEN_UNSAT && proof_out && summary_fmt) { va_list ap; va_start(ap, summary_fmt);
        vsnprintf(proof_out->proof_summary, sizeof(proof_out->proof_summary), summary_fmt, ap); va_end(ap); } return res; }
#define FLOW_SMT_RULE(name_str, cand, min_b, max_b, th, msg_str) \
    { (name_str), (uint64_t)(cand), (uint64_t)(min_b), (uint64_t)(max_b), (th), (msg_str) }
#define FLOW_SMT_VERIFY_RULES(tag, proof_out, rules_arr, ...) \
    flow_smt_verify_rules((tag), (rules_arr), sizeof(rules_arr) / sizeof((rules_arr)[0]), (proof_out), __VA_ARGS__)
#define FLOW_SMT_BOX_BUILDER_DECL(var_name) \
    FlowSMTBoxBuilder var_name; flow_smt_box_builder_init(&var_name)
#define FLOW_SMT_BOX_ADD_RULE(builder, name_str, cand, min_b, max_b, th, msg_str) \
    flow_smt_box_builder_add(&(builder), (name_str), (uint64_t)(cand), (uint64_t)(min_b), (uint64_t)(max_b), (th), (msg_str))
#define FLOW_SMT_BOX_ADD(builder, name_str, cand, min_b, max_b) \
    FLOW_SMT_BOX_ADD_RULE(builder, (name_str), (cand), (min_b), (max_b), FLOW_BOX_THEOREM_BUFFER_BOUNDS, "out of bounds")
#define FLOW_SMT_BOX_VERIFY(builder, tag_str, proof_ptr) \
    flow_smt_box_builder_verify(&(builder), (tag_str), (proof_ptr))
#ifdef __cplusplus
}
#endif
#endif /* FLOW_SMT_DSL_H */
