#ifndef FLOW_SMT_DSL_H
#define FLOW_SMT_DSL_H
#include "smt.h"
#include <stdarg.h>
#include <stdio.h>
static inline FlowSMTResult flow_smt_verify_rules(const char *tag, const FlowBoxConstraint *rules, size_t count, FlowSMTProofAttestation *proof_out, const char *summary_fmt, ...) {
    FlowSMTResult res = flow_smt_verify_box_invariants(tag, rules, count, proof_out);
    if (res == FLOW_SMT_PROVEN_UNSAT && proof_out && summary_fmt) { va_list ap; va_start(ap, summary_fmt);
        vsnprintf(proof_out->proof_summary, sizeof(proof_out->proof_summary), summary_fmt, ap); va_end(ap); } return res; }
#define FLOW_SMT_RULE(name_str, cand, min_b, max_b, th, msg_str) { (name_str), (uint64_t)(cand), (uint64_t)(min_b), (uint64_t)(max_b), (th), (msg_str) }
#define FLOW_SMT_VERIFY_RULES(tag, proof_out, rules_arr, ...) flow_smt_verify_rules((tag), (rules_arr), sizeof(rules_arr) / sizeof((rules_arr)[0]), (proof_out), __VA_ARGS__)
#endif
