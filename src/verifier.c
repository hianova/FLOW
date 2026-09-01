#include "verifier.h"
#include "security.h"
#include "registry.h"

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

uint64_t flow_verifier_get_contract_mask(const SemanticIR *ir,
                                         const Component *comp,
                                         const FlowPlanDimensionSet *dims) {
    if (dims == NULL || dims->count == 0) return (uint64_t)-1;
    uint64_t mask = flow_component_contract_mask(ir, comp, dims);
    unsigned shift = 0;

    int forbid_parallel = 0;
    if (ir != NULL) {
        if (!ir->flow_parallelizable && !ir->state_shared) {
            forbid_parallel = 1;
        }
    }

    for (size_t i = 0; i < dims->count; ++i) {
        const FlowPlanDimension *d = &dims->dimensions[i];
        unsigned bits = flow_dimension_bits(d);
        if (bits == 0) continue;
        uint64_t dim_mask = (bits >= 64) ? (uint64_t)-1 : (((uint64_t)1 << bits) - 1);

        if (forbid_parallel && (strcmp(d->name, "threads") == 0 || strcmp(d->name, "shards") == 0)) {
            /* Strictly sequential / non-parallel contract: mask out all non-zero bits */
            mask &= ~(dim_mask << shift);
        }
        shift += bits;
    }
    return mask;
}

uint64_t flow_verifier_get_resource_mask(const SemanticIR *ir,
                                         const Component *comp,
                                         const FlowPlanDimensionSet *dims) {
    if (dims == NULL || dims->count == 0) return (uint64_t)-1;
    size_t limit_bytes = (ir != NULL && ir->memory_limit_mb > 0) ?
                         (size_t)ir->memory_limit_mb * 1024u * 1024u : 0;
    uint64_t mask = (limit_bytes > 0) ? flow_component_resource_mask(ir, comp, dims, limit_bytes) : (uint64_t)-1;
    unsigned shift = 0;

    if (limit_bytes == 0 || comp == NULL) return mask;

    size_t per_cap = comp->memory_bytes_per_capacity > 0 ? comp->memory_bytes_per_capacity : 8;
    size_t max_cap = (limit_bytes > comp->memory_fixed_bytes) ?
                     (limit_bytes - comp->memory_fixed_bytes) / per_cap : 0;

    for (size_t i = 0; i < dims->count; ++i) {
        const FlowPlanDimension *d = &dims->dimensions[i];
        unsigned bits = flow_dimension_bits(d);
        if (bits == 0) continue;

        if (strcmp(d->name, "capacity") == 0 && d->kind == FLOW_DIM_EXPONENT) {
            /* Find maximum allowed exponent */
            uint64_t max_exp = d->min_val;
            while (max_exp < d->max_val && ((uint64_t)1 << (max_exp + 1)) <= max_cap) {
                max_exp++;
            }
            uint64_t max_raw = max_exp >= d->min_val ? max_exp - d->min_val : 0;
            for (unsigned b = 0; b < bits; ++b) {
                if (((uint64_t)1 << b) > max_raw) {
                    mask &= ~(UINT64_C(1) << (shift + b));
                }
            }
        } else if (strcmp(d->name, "arena_bytes") == 0 && d->kind == FLOW_DIM_LINEAR) {
            /* Mask arena sizes that inherently exceed total memory limit */
            uint64_t max_arena = limit_bytes / 2;
            uint64_t step = d->step == 0 ? 1 : d->step;
            uint64_t max_raw = max_arena >= d->min_val ? (max_arena - d->min_val) / step : 0;
            for (unsigned b = 0; b < bits; ++b) {
                if (((uint64_t)1 << b) > max_raw) {
                    mask &= ~(UINT64_C(1) << (shift + b));
                }
            }
        }
        shift += bits;
    }
    return mask;
}
