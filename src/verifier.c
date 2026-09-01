#include "verifier.h"
#include "security.h"

#include <stdio.h>
#include <string.h>

const char *verification_status_name(VerificationStatus status) {
    switch (status) {
        case VERIFIER_PROVEN: return "proven";
        case VERIFIER_RUNTIME_CHECK: return "runtime_check";
        case VERIFIER_COMPILE_ERROR: return "compile_error";
    }
    return "unknown";
}

int verify_candidate(const SemanticIR *ir, const Component *component,
                     const SearchResult *search, VerificationReport *report) {
    size_t capacity = search != NULL ? (size_t)search->capacity :
                      (size_t)ir->input_max_count;
    size_t shard_count = search != NULL ? (size_t)search->shards : 1;
    size_t estimated_bytes = 0;
    FlowCompositionSpec comp_spec;
    FlowSecurityOutcome sec_outcome;

    if (shard_count == 0) shard_count = 1;

    memset(report, 0, sizeof(*report));
    report->capacity = capacity;
    report->estimated_bytes = estimated_bytes;
    report->runtime_input_guard = 0;

    memset(&comp_spec, 0, sizeof(comp_spec));
    comp_spec.ir = ir;
    comp_spec.component = component;
    comp_spec.plan = search != NULL ? &search->assignment : NULL;
    comp_spec.metrics = search != NULL ? &search->metrics : NULL;
    comp_spec.concurrency_threads = search != NULL ? (size_t)search->threads : 1;
    comp_spec.read_only_ownership = ir != NULL ? ir->fact_mutability_read_only : 0;

    sec_outcome = flow_security_check_composition_gate(&comp_spec, report->message,
                                                       sizeof(report->message));
    if (sec_outcome != FLOW_SECURITY_PASS) {
        report->status = VERIFIER_COMPILE_ERROR;
        return 0;
    }

    if (search != NULL && search->assignment.count > 0) {
        if (!flow_component_verify_plan(ir, component, &search->assignment, report)) {
            report->status = VERIFIER_COMPILE_ERROR;
            if (report->message[0] == '\0')
                snprintf(report->message, sizeof(report->message),
                         "plugin verification failed");
            return 0;
        }
        estimated_bytes = report->estimated_bytes;
        capacity = report->capacity;
    } else {
        if (!flow_component_memory(ir, component, capacity, shard_count,
                                   &estimated_bytes)) {
            report->status = VERIFIER_COMPILE_ERROR;
            snprintf(report->message, sizeof(report->message),
                     "plugin memory model failed");
            return 0;
        }
        report->estimated_bytes = estimated_bytes;
        if (!flow_component_verify(ir, component, capacity, shard_count,
                                   report->message, sizeof(report->message))) {
            report->status = VERIFIER_COMPILE_ERROR;
            if (report->message[0] == '\0')
                snprintf(report->message, sizeof(report->message),
                         "plugin verification failed");
            return 0;
        }
    }

    if (capacity == 0 || (size_t)ir->top_n > capacity) {
        report->status = VERIFIER_COMPILE_ERROR;
        snprintf(report->message, sizeof(report->message),
                 "top_n exceeds selected capacity");
        return 0;
    }
    if (ir->memory_limit_mb > 0 &&
        estimated_bytes > (size_t)ir->memory_limit_mb * 1024u * 1024u) {
        report->status = VERIFIER_COMPILE_ERROR;
        snprintf(report->message, sizeof(report->message),
                 "estimated collection memory exceeds memory constraint");
        return 0;
    }

    report->max_count_proven = (size_t)ir->input_max_count <= capacity;
    report->runtime_input_guard = !report->max_count_proven;
    report->status = report->max_count_proven ? VERIFIER_PROVEN : VERIFIER_RUNTIME_CHECK;
    snprintf(report->message, sizeof(report->message),
             report->max_count_proven ? "input bound proven" :
                                        "input bound requires runtime guard");
    return 1;
}
