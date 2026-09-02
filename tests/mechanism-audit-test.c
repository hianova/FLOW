#include "benchmark.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "mechanism-audit-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

int main(void) {
    printf("Executing FLOW Quantitative Mechanism Efficiency & Effectiveness Audit...\n");

    FlowMechanismAuditReport report;
    CHECK(flow_benchmark_run_mechanism_audit(&report) == 1);
    CHECK(report.metric_count >= 6);

    flow_benchmark_print_mechanism_audit(&report, stdout);

    printf("MECHANISM_AUDIT_TEST=passed audited_mechanisms=%zu total_time_ms=%.2f\n",
           report.metric_count, report.total_audit_time_ms);
    return 0;
}
