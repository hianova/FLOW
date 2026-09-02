#ifndef BENCHMARK_H
#define BENCHMARK_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "flow.h"
#include "registry.h"

typedef struct {
    char mechanism_name[64];
    char baseline_name[64];
    double flow_metric_value;
    double baseline_metric_value;
    char unit[32];
    double speedup_or_reduction;
    char qualitative_gain[128];
} FlowMechanismMetric;

typedef struct {
    FlowMechanismMetric metrics[16];
    size_t metric_count;
    double total_audit_time_ms;
} FlowMechanismAuditReport;

uint64_t benchmark_candidate(const SemanticIR *ir, const Component *component,
                             const FlowPlanAssignment *plan);

/* Comprehensive Quantitative Mechanism Efficiency Audit */
int flow_benchmark_run_mechanism_audit(FlowMechanismAuditReport *report_out);
void flow_benchmark_print_mechanism_audit(const FlowMechanismAuditReport *report, FILE *out);

#endif
