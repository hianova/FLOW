#include "benchmark.h"

uint64_t benchmark_candidate(const SemanticIR *ir, const Component *component,
                             const FlowPlanAssignment *plan) {
    if (component == NULL) return UINT64_MAX;
    return flow_component_benchmark(ir, component, plan);
}
