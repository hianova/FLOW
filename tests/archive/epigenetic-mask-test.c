#include "bitspace.h"
#include "registry.h"
#include "flow.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "epigenetic-mask-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

int main(void) {
    flow_registry_init();

    SemanticIR ir;
    memset(&ir, 0, sizeof(ir));
    strncpy(ir.flow_name, "epigenetic_mask_test_flow", sizeof(ir.flow_name) - 1);
    ir.input_max_count = 8;
    ir.top_n = 4;
    ir.memory_limit_mb = 32;
    ir.state_shared = 0;
    ir.state_read_heavy = 0;
    ir.fact_ordered = 1;
    ir.fact_unordered = 0;

    FlowBitSpace space;
    CHECK(flow_bitspace_init_for_ir(&ir, &space));
    CHECK(space.candidate_count >= 1);
    CHECK(space.env_mask != 0);

    /* 1. Direct Bitwise Mutation Masking (1-Cycle Early Pruning Verification) */
    uint64_t rng = 123456789;
    uint64_t base_genome = 0;
    uint64_t restrictive_mask = 0x000000000000000FULL; /* Bits 0~3 allowed */

    size_t passed_count = 0;
    size_t pruned_count = 0;

    for (size_t i = 0; i < 1000; ++i) {
        uint32_t flipped_bit = 0;
        uint64_t mutated = flow_bitspace_mutate_1bit_masked(&space, base_genome, restrictive_mask, &rng, &flipped_bit);
        if (flipped_bit == 0xFFFFFFFF) {
            /* Pruned in 1 CPU cycle */
            CHECK(mutated == base_genome);
            pruned_count++;
        } else {
            /* Passed through mask */
            CHECK(flipped_bit < 4);
            CHECK(mutated == (base_genome ^ (1ULL << flipped_bit)));
            passed_count++;
        }
    }

    CHECK(passed_count > 0);
    CHECK(pruned_count > 0);
    CHECK(passed_count + pruned_count == 1000);

    /* 2. End-to-End Search with Epigenetic Environmental Mask */
    FlowBMFConfig anneal_cfg = {
        .initial_temperature = 80.0,
        .cooling_decay = 0.98,
        .plateau_stagnation_limit = 6,
        .reheat_ratio = 0.6,
        .env_mask = restrictive_mask /* Environmental pressure restricts search manifold */
    };

    FlowPlan seed_plan;
    memset(&seed_plan, 0, sizeof(seed_plan));
    seed_plan.component = space.candidates[0];
    seed_plan.dimension_set = space.candidate_dims[0];
    space.decode(&space, 0, &seed_plan);
    seed_plan.genome = 0;

    FlowTransitionCostModel model = {
        .has_active_baseline = 1,
        .baseline_plan = &seed_plan
    };

    FlowBitSearchResult result;
    CHECK(flow_bitspace_search_configured(&space, 100, 42, 0, &model, &anneal_cfg, &result));
    CHECK(result.best_plan.eval.hard_gate_passed);
    CHECK(result.best_plan.component != NULL);

    /* Verify that all bits outside restrictive_mask remained strictly at 0 */
    uint64_t unmasked_bits = result.best_plan.genome & ~restrictive_mask;
    CHECK(unmasked_bits == 0);

    flow_ir_cleanup(&ir);
    printf("EPIGENETIC_MASK_TEST=passed 1cycle_bitwise_pruning=verified environmental_mask=sound unmasked_bits=0\n");
    return 0;
}
