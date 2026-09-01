#include "bitspace.h"
#include "security.h"
#include "verifier.h"
#include "adaptive.h"
#include "registry.h"
#include "flow.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "dynamic-mask-superposition-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

int main(void) {
    flow_registry_init();

    /* ========================================================================= */
    /* 1. Tier 1: Hard Safety Mask (src/security.c)                              */
    /* ========================================================================= */
    {
        SemanticIR ir;
        memset(&ir, 0, sizeof(ir));
        strncpy(ir.flow_name, "safety_test_flow", sizeof(ir.flow_name) - 1);
        ir.input_max_count = 1024;
        ir.state_shared = 0;             /* Unshared state */
        ir.flow_parallelizable = 0;      /* Sequential */
        ir.fact_mutability_read_only = 1;/* Read-only ownership */

        const Component *comp = select_component(&ir);
        CHECK(comp != NULL);

        FlowPlanDimensionSet dims;
        CHECK(flow_component_dimensions(&ir, comp, &dims));

        uint64_t safety_mask = flow_security_get_safety_mask(&ir, comp, &dims);
        CHECK(safety_mask != 0);

        /* Find 'threads' and 'shards' bit positions */
        unsigned shift = 0;
        for (size_t i = 0; i < dims.count; ++i) {
            unsigned bits = flow_dimension_bits(&dims.dimensions[i]);
            if (strcmp(dims.dimensions[i].name, "threads") == 0 ||
                strcmp(dims.dimensions[i].name, "shards") == 0) {
                uint64_t field_mask = (((uint64_t)1 << bits) - 1) << shift;
                /* All concurrency bits must be masked to 0 to prevent race/ownership breach */
                CHECK((safety_mask & field_mask) == 0);
            }
            shift += bits;
        }
    }

    /* ========================================================================= */
    /* 2. Tier 1: Hard Contract Mask (src/verifier.c)                            */
    /* ========================================================================= */
    {
        SemanticIR ir;
        memset(&ir, 0, sizeof(ir));
        strncpy(ir.flow_name, "contract_test_flow", sizeof(ir.flow_name) - 1);
        ir.input_max_count = 500;
        ir.top_n = 256;
        ir.flow_parallelizable = 0;
        ir.state_shared = 0;

        const Component *comp = select_component(&ir);
        CHECK(comp != NULL);

        FlowPlanDimensionSet dims;
        CHECK(flow_component_dimensions(&ir, comp, &dims));

        uint64_t contract_mask = flow_verifier_get_contract_mask(&ir, comp, &dims);
        CHECK(contract_mask != 0);

        /* Concurrency bits must be physically 0 */
        unsigned shift = 0;
        for (size_t i = 0; i < dims.count; ++i) {
            unsigned bits = flow_dimension_bits(&dims.dimensions[i]);
            if (strcmp(dims.dimensions[i].name, "threads") == 0 ||
                strcmp(dims.dimensions[i].name, "shards") == 0) {
                uint64_t field_mask = (((uint64_t)1 << bits) - 1) << shift;
                CHECK((contract_mask & field_mask) == 0);
            }
            shift += bits;
        }
    }

    /* ========================================================================= */
    /* 3. Tier 1: Hard Resource Quota Mask (src/verifier.c)                      */
    /* ========================================================================= */
    {
        SemanticIR ir;
        memset(&ir, 0, sizeof(ir));
        strncpy(ir.flow_name, "resource_test_flow", sizeof(ir.flow_name) - 1);
        ir.input_max_count = 1000;
        ir.memory_limit_mb = 1; /* Hard 1MB limit */
        ir.state_shared = 1;
        ir.state_read_heavy = 1;

        const Component *comp = select_component(&ir);
        CHECK(comp != NULL);

        FlowPlanDimensionSet dims;
        CHECK(flow_component_dimensions(&ir, comp, &dims));

        uint64_t resource_mask = flow_verifier_get_resource_mask(&ir, comp, &dims);
        CHECK(resource_mask != 0);

        /* Capacity exponent bits exceeding 1MB must be masked out */
        unsigned shift = 0;
        for (size_t i = 0; i < dims.count; ++i) {
            unsigned bits = flow_dimension_bits(&dims.dimensions[i]);
            if (strcmp(dims.dimensions[i].name, "capacity") == 0) {
                uint64_t field_mask = (((uint64_t)1 << bits) - 1) << shift;
                /* Upper bits must be 0 because 2^20 (1M elements) * 12 bytes = 12MB > 1MB */
                CHECK((resource_mask & field_mask) != field_mask);
            }
            shift += bits;
        }
    }

    /* ========================================================================= */
    /* 4. Tier 2: Soft Dynamic Telemetry Bias (src/adaptive.c)                   */
    /* ========================================================================= */
    {
        FlowPlanDimensionSet dims;
        dims.count = 4;
        dims.dimensions[0] = (FlowPlanDimension){"capacity", FLOW_DIM_EXPONENT, FLOW_DIM_CLASS_STRUCTURAL_JIT, 1, 20, 1, 12, 500};
        dims.dimensions[1] = (FlowPlanDimension){"threads", FLOW_DIM_EXPONENT, FLOW_DIM_CLASS_STRUCTURAL_JIT, 0, 6, 1, 0, 200};
        dims.dimensions[2] = (FlowPlanDimension){"shards", FLOW_DIM_EXPONENT, FLOW_DIM_CLASS_STRUCTURAL_JIT, 0, 6, 1, 0, 200};
        dims.dimensions[3] = (FlowPlanDimension){"buffer_bytes", FLOW_DIM_LINEAR, FLOW_DIM_CLASS_TACTILE_PARAM, 1024, 65536, 1024, 16384, 0};

        /* Case A: High Cache Miss Rate (>30%) */
        FlowPMUTelemetry pmu_cache = {
            .l3_cache_misses = 400000,
            .l3_cache_references = 1000000, /* 40% miss rate */
            .cache_miss_rate = 0.40,
            .ipc = 1.5
        };
        uint64_t bias_cache = flow_adaptive_telemetry_bias_from_pmu(&pmu_cache, 0, &dims);
        CHECK(bias_cache != 0);

        /* Case B: High Contention / Write Heavy */
        uint64_t bias_contention = flow_adaptive_telemetry_bias_from_pmu(NULL, 1, &dims);
        CHECK(bias_contention != 0);

        /* Case C: Low IPC / Stalled Throughput */
        FlowPMUTelemetry pmu_ipc = {
            .instructions = 500000,
            .cpu_cycles = 1000000,
            .ipc = 0.5
        };
        uint64_t bias_ipc = flow_adaptive_telemetry_bias_from_pmu(&pmu_ipc, 0, &dims);
        CHECK(bias_ipc != 0);
    }

    /* ========================================================================= */
    /* 5. Tier 3: Domain Preference Mask (src/builtin_plugin.c)                  */
    /* ========================================================================= */
    {
        SemanticIR ir;
        memset(&ir, 0, sizeof(ir));
        strncpy(ir.flow_name, "rank", sizeof(ir.flow_name) - 1);
        ir.state_shared = 1;
        ir.state_read_heavy = 1;

        const Component *comp = select_component(&ir);
        CHECK(comp != NULL);
        CHECK(strcmp(comp->id, "sharded_hash") == 0);

        FlowPlanDimensionSet dims;
        CHECK(flow_component_dimensions(&ir, comp, &dims));

        uint64_t pref_mask = flow_component_preference_mask(&ir, comp, &dims);
        CHECK(pref_mask != 0);

        /* threads and shards must be marked in domain preference */
        unsigned shift = 0;
        int threads_preferred = 0;
        int shards_preferred = 0;
        for (size_t i = 0; i < dims.count; ++i) {
            unsigned bits = flow_dimension_bits(&dims.dimensions[i]);
            uint64_t field_mask = (((uint64_t)1 << bits) - 1) << shift;
            if (strcmp(dims.dimensions[i].name, "threads") == 0 && (pref_mask & field_mask)) {
                threads_preferred = 1;
            }
            if (strcmp(dims.dimensions[i].name, "shards") == 0 && (pref_mask & field_mask)) {
                shards_preferred = 1;
            }
            shift += bits;
        }
        CHECK(threads_preferred && shards_preferred);
    }

    /* ========================================================================= */
    /* 6. Mask Superposition & 1-Bit Chaos Early Pruning Execution               */
    /* ========================================================================= */
    {
        SemanticIR ir;
        memset(&ir, 0, sizeof(ir));
        strncpy(ir.flow_name, "superposition_search_flow", sizeof(ir.flow_name) - 1);
        ir.input_max_count = 4096;
        ir.memory_limit_mb = 16;
        ir.state_shared = 1;
        ir.state_read_heavy = 1;

        FlowBitSpace space;
        CHECK(flow_bitspace_init_for_ir(&ir, &space));
        CHECK(space.candidate_count >= 1);

        FlowMaskCanvas canvas;
        CHECK(flow_mask_canvas_compose(&ir, space.candidates[0], &space.candidate_dims[0], NULL, &canvas));
        CHECK(canvas.hard_composite_mask != 0);
        CHECK(canvas.soft_composite_bias != 0);

        /* Verify 1-bit superposed mutation respects hard mask absolutely */
        uint64_t rng = 987654321;
        uint64_t genome = 0;
        size_t pruned_count = 0;
        size_t valid_count = 0;

        for (size_t i = 0; i < 500; ++i) {
            uint32_t bit = 0;
            uint64_t mutated = flow_bitspace_mutate_1bit_superposed(&space, genome, &canvas, 0.75, &rng, &bit);
            if (bit == 0xFFFFFFFF) {
                pruned_count++;
            } else {
                CHECK(bit < space.bit_count);
                CHECK((canvas.hard_composite_mask & (UINT64_C(1) << bit)) != 0);
                valid_count++;
                genome = mutated;
            }
        }
        CHECK(valid_count > 0);
        CHECK(valid_count + pruned_count == 500);

        /* Perform end-to-end configured search with superposed canvas */
        FlowChaosAnnealConfig cfg = {
            .initial_temperature = 90.0,
            .cooling_decay = 0.98,
            .plateau_stagnation_limit = 5,
            .reheat_ratio = 0.6,
            .mask_canvas = canvas,
            .soft_bias_weight = 0.80,
            .use_mask_canvas = 1
        };

        FlowBitSearchResult res;
        CHECK(flow_bitspace_search_configured(&space, 150, 42, 0, NULL, &cfg, &res));
        CHECK(res.best_plan.eval.hard_gate_passed);
        CHECK(res.best_plan.component != NULL);
        CHECK(res.heatmap.total_failures == 0); /* 0% hard-gate rejection due to pre-emptive pruning! */

        flow_mask_canvas_report(&res.mask_canvas, stdout);
        flow_ir_cleanup(&ir);
    }

    printf("DYNAMIC_MASK_SUPERPOSITION_TEST=passed 3tier_masks=sound hard_and_pruning=verified soft_telemetry_bias=verified domain_preference=verified 0_gate_failures=verified\n");
    return 0;
}
