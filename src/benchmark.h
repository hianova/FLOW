#ifndef BENCHMARK_H
#define BENCHMARK_H

#include <stddef.h>
#include <stdint.h>

#include "flow.h"
#include "registry.h"

uint64_t benchmark_candidate(const SemanticIR *ir, const Component *component,
                             const FlowPlanAssignment *plan);

#endif
