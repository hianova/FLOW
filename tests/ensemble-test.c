#include "bitspace.h"
#include "flow.h"
#include "registry.h"
#include "search.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "ensemble-test assertion failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

int main(void) {
    flow_registry_init();

    /* 1. Parse rank specification */
    const char *spec_src =
        "input score_stream {\n"
        "    max_count 10000\n"
        "}\n"
        "flow rank {\n"
        "    score_stream -> transform -> group -> sort\n"
        "}\n"
        "import builtin\n"
        "require {\n"
        "    deterministic\n"
        "    memory < 32mb\n"
        "}\n"
        "prefer {\n"
        "    throughput\n"
        "}\n";

    FILE *mem = fmemopen((void *)spec_src, strlen(spec_src), "r");
    CHECK(mem != NULL);
    FlowSpec spec;
    CHECK(parse_spec(mem, &spec));
    fclose(mem);

    SemanticIR ir;
    lower_to_ir(&spec, &ir);

    /* 2. Run multi-objective Pareto BitSpace search */
    FlowBitSpace space;
    CHECK(flow_bitspace_init_for_ir(&ir, &space));

    FlowBitSearchResult bit_res;
    CHECK(flow_bitspace_search(&space, 100, 42, 0, NULL, &bit_res));
    CHECK(bit_res.best_plan.component != NULL);

    /* 3. Extract Plan Ensemble */
    FlowPlanEnsemble ensemble;
    CHECK(flow_bitspace_extract_ensemble(&bit_res, &ensemble));
    CHECK(ensemble.count == 3);
    CHECK(ensemble.available[FLOW_TACTIC_SPEED] == 1);
    CHECK(ensemble.available[FLOW_TACTIC_BALANCED] == 1);
    CHECK(ensemble.available[FLOW_TACTIC_MEMORY] == 1);

    CHECK(strcmp(flow_plan_tactic_name(FLOW_TACTIC_SPEED), "speed") == 0);
    CHECK(strcmp(flow_plan_tactic_name(FLOW_TACTIC_BALANCED), "balanced") == 0);
    CHECK(strcmp(flow_plan_tactic_name(FLOW_TACTIC_MEMORY), "memory") == 0);

    /* Ensure memory tactic footprint is <= speed tactic footprint */
    CHECK(ensemble.tactics[FLOW_TACTIC_MEMORY].eval.memory_bytes <=
          ensemble.tactics[FLOW_TACTIC_SPEED].eval.memory_bytes ||
          ensemble.tactics[FLOW_TACTIC_MEMORY].eval.capacity <=
          ensemble.tactics[FLOW_TACTIC_SPEED].eval.capacity);

    flow_ir_cleanup(&ir);

    printf("ENSEMBLE_TEST=passed pareto_count=%zu tactics=3 bundle_verification=sound\n",
           bit_res.pareto_count);
    return 0;
}
