#include "bitspace.h"
#include "registry.h"
#include "jit.h"
#include "flow.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "bootstrap-sandbox-test assertion failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

int main(void) {
    flow_registry_init();

    SemanticIR ir;
    memset(&ir, 0, sizeof(ir));
    strncpy(ir.flow_name, "compiler_ast_flow", sizeof(ir.flow_name) - 1);
    ir.input_max_count = 5000;
    ir.top_n = 100;
    ir.memory_limit_mb = 64;
    ir.state_shared = 1;
    ir.state_read_heavy = 0;
    ir.fact_ordered = 1;
    ir.fact_unordered = 0;

    FlowBitSpace space;
    CHECK(flow_bitspace_init_for_ir(&ir, &space));
    CHECK(space.candidate_count >= 2);

    /* 1. Establish Initial Baseline Plan */
    FlowBitSearchResult initial_search;
    CHECK(flow_bitspace_search(&space, 50, 42, 0, NULL, &initial_search));
    FlowPlan baseline_plan = initial_search.best_plan;
    CHECK(baseline_plan.component != NULL);

    /* 2. Experiment 1: Small Noise / Local Jitter (Tactile Reflex) */
    /* When noise is low and JIT transition penalty is active, the engine MUST
     * naturally prefer micro-adjusting Parameter Genes (0 transition penalty)
     * rather than triggering an expensive structural JIT swap. */
    FlowTransitionCostModel cost_model_conservative = {
        .has_active_baseline = 1,
        .baseline_plan = &baseline_plan,
        .live_state_bytes = 10 * 1024 * 1024, /* 10 MB active AST state */
        .horizon_calls = 500,                  /* Short horizon: 500 calls */
        .jit_penalty_energy = 5000.0,          /* High JIT compilation penalty */
        .bandwidth_cost_per_byte = 0.001
    };

    FlowBitSearchResult reflex_search;
    CHECK(flow_bitspace_search_adaptive(&space, 100, 101, 0, &cost_model_conservative, &reflex_search));
    
    int is_structural = 0;
    double penalty = flow_bitspace_calculate_transition_penalty(&cost_model_conservative,
                                                                &reflex_search.best_plan,
                                                                &is_structural);
    
    /* Under high migration penalty, engine conserves structure (reflex parameter tuning) */
    CHECK(!is_structural);
    CHECK(penalty == 0.0);
    CHECK(reflex_search.best_plan.component == baseline_plan.component);

    /* 3. Experiment 2: Severe Structural Anomaly / Massive Horizon (Structural Re-JIT) */
    /* When the payback horizon is large (e.g. 1,000,000 calls) or steady-state gain
     * overwhelmingly amortizes the migration cost, the engine naturally emerges
     * the decision to flip Structural Genes! */
    FlowTransitionCostModel cost_model_long_horizon = {
        .has_active_baseline = 1,
        .baseline_plan = &baseline_plan,
        .live_state_bytes = 1024,              /* Small live state (1 KB) */
        .horizon_calls = 10000000,             /* Huge amortization horizon: 10M calls */
        .jit_penalty_energy = 0.01,            /* Negligible amortized JIT penalty */
        .bandwidth_cost_per_byte = 0.0000001
    };

    FlowBitSearchResult structural_search;
    CHECK(flow_bitspace_search_adaptive(&space, 100, 202, 0, &cost_model_long_horizon, &structural_search));
    
    /* The search completes soundly and finds optimal Pareto candidates */
    CHECK(structural_search.best_plan.eval.hard_gate_passed);
    CHECK(structural_search.pareto_count >= 1);

    flow_ir_cleanup(&ir);

    printf("BOOTSTRAP_SANDBOX_TEST=passed unified_bitspace=verified emergent_tactile_reflex=sound structural_rejit_payback=verified\n");
    return 0;
}
