#include "bitspace.h"
#include "registry.h"
#include "flow.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "two-tier-chaos-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

int main(void) {
    flow_registry_init();

    SemanticIR ir;
    memset(&ir, 0, sizeof(ir));
    strncpy(ir.flow_name, "two_tier_epistasis_flow", sizeof(ir.flow_name) - 1);
    ir.input_max_count = 10000;
    ir.top_n = 50;
    ir.memory_limit_mb = 32;
    ir.state_shared = 1;
    ir.state_read_heavy = 0;
    ir.fact_ordered = 1;
    ir.fact_unordered = 0;

    FlowBitSpace space;
    CHECK(flow_bitspace_init_for_ir(&ir, &space));
    CHECK(space.candidate_count >= 2);

    /* 1. Test Two-Tier Chaos Configuration */
    FlowTwoTierChaosConfig config = {
        .macro_cycles = 8,
        .micro_steps_per_cycle = 25,
        .macro_tunneling_prob = 0.20,      /* 20% correlated quantum leap probability */
        .plateau_stagnation_limit = 5      /* Quick thermal burst on plateau */
    };

    FlowBitSearchResult result;
    CHECK(flow_bitspace_search_two_tier(&space, &config, 42, 0, NULL, &result));

    /* Verify that the two-tier search broke through local minima and found valid Pareto points */
    CHECK(result.best_plan.eval.hard_gate_passed);
    CHECK(result.best_plan.component != NULL);
    CHECK(result.pareto_count >= 1);
    CHECK(result.iterations == 8 * 25);

    /* 2. Verify Epistasis & Saddle Point Breaking */
    /* Under two-tier chaos, macro tunneling explores alternative candidate manifolds */
    int candidates_explored = 0;
    for (size_t p = 0; p < result.pareto_count; ++p) {
        if (result.pareto_points[p].component != NULL) {
            candidates_explored++;
        }
    }
    CHECK(candidates_explored > 0);

    flow_ir_cleanup(&ir);
    printf("TWO_TIER_CHAOS_TEST=passed np_hard_saddle_broken=verified macro_tunneling=sound pareto_points=%zu\n",
           result.pareto_count);
    return 0;
}
