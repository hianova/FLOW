#include "bitspace.h"
#include "registry.h"
#include "search.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BIT_CHECK(cond) if (!(cond)) { fprintf(stderr, "bitspace-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; }

int main(void) {
    if (!flow_registry_init()) return 1;

    /* 1. Setup SemanticIR for a ranking flow */
    SemanticIR ir;
    memset(&ir, 0, sizeof(ir));
    strncpy(ir.flow_name, "rank_flow", sizeof(ir.flow_name) - 1);
    ir.input_max_count = 100;
    ir.top_n = 100;
    ir.memory_limit_mb = 4;
    ir.state_shared = 1;
    ir.state_read_heavy = 1;
    ir.fact_unordered = 1;

    const FlowPlugin *builtin = flow_registry_lookup("builtin");
    BIT_CHECK(builtin != NULL);
    const Component *comp = NULL;
    for (size_t i = 0; i < builtin->component_count; ++i) {
        if (strcmp(builtin->components[i].id, "sharded_hash") == 0) {
            comp = &builtin->components[i];
            break;
        }
    }
    BIT_CHECK(comp != NULL);

    /* 2. Initialize Single FlowBitSpace */
    FlowBitSpace space;
    BIT_CHECK(flow_bitspace_init_single(&ir, comp, &space));
    BIT_CHECK(space.bit_count > 0);
    BIT_CHECK(space.schema_hash != 0);

    /* 3. Test Decode & Evaluate */
    FlowPlan valid_plan;
    BIT_CHECK(space.decode(&space, UINT64_C(0x6), &valid_plan));
    BIT_CHECK(valid_plan.component == comp);
    BIT_CHECK(valid_plan.assignment.count == space.candidate_dims[0].count);
    BIT_CHECK(space.evaluate(&space, &valid_plan, &valid_plan.eval));
    BIT_CHECK(valid_plan.eval.capacity >= 100);
    BIT_CHECK(space.hard_gate(&space, &valid_plan, &valid_plan.eval));

    /* Test Hard Gate Boundary Rejection on Undersized Genome (capacity < top_n) */
    FlowPlan small_plan;
    BIT_CHECK(space.decode(&space, UINT64_C(0x0), &small_plan));
    BIT_CHECK(space.evaluate(&space, &small_plan, &small_plan.eval));
    BIT_CHECK(!space.hard_gate(&space, &small_plan, &small_plan.eval)); /* Capacity 2 < top_n 100 */

    /* Test Hard Gate Boundary Rejection on Oversized Genome (memory > limit) */
    FlowPlan huge_plan;
    BIT_CHECK(space.decode(&space, UINT64_C(0x1a2b3c), &huge_plan));
    BIT_CHECK(space.evaluate(&space, &huge_plan, &huge_plan.eval));
    BIT_CHECK(!space.hard_gate(&space, &huge_plan, &huge_plan.eval)); /* Exceeds 4MB limit */

    /* 4. Test 1-Bit Chaotic Mutation */
    uint64_t rng = UINT64_C(0x123456789abcdef0);
    uint32_t flipped_bit = 0;
    uint64_t mutated_genome = flow_bitspace_mutate_1bit(&space, valid_plan.genome, &rng, &flipped_bit);
    BIT_CHECK(mutated_genome != valid_plan.genome);
    uint64_t diff = mutated_genome ^ valid_plan.genome;
    BIT_CHECK((diff & (diff - 1)) == 0); /* Exactly 1 bit flipped */
    BIT_CHECK(flipped_bit < space.bit_count);

    /* 5. Test Hierarchical BitSpace for IR with Multiple Compatible Candidates */
    SemanticIR multi_ir = ir;
    multi_ir.state_read_heavy = 0;
    multi_ir.fact_ordered = 1;
    multi_ir.fact_unordered = 0;
    FlowBitSpace multi_space;
    BIT_CHECK(flow_bitspace_init_for_ir(&multi_ir, &multi_space));
    BIT_CHECK(multi_space.candidate_count >= 2); /* sharded_hash and ordered_tree */
    BIT_CHECK(multi_space.selector_bits >= 1);

    /* Decode candidate 0 vs candidate 1 */
    FlowPlan cand0_plan, cand1_plan;
    BIT_CHECK(multi_space.decode(&multi_space, 0, &cand0_plan));
    BIT_CHECK(multi_space.decode(&multi_space, 1, &cand1_plan));
    BIT_CHECK(cand0_plan.component != cand1_plan.component);

    /* 6. Test 1-Bit Search over Hierarchical FlowBitSpace */
    FlowBitSearchResult s_res;
    BIT_CHECK(flow_bitspace_search(&multi_space, 50, 42, 0, NULL, &s_res));
    BIT_CHECK(s_res.best_plan.component != NULL);
    BIT_CHECK(s_res.best_plan.eval.capacity >= 100);
    BIT_CHECK(s_res.pareto_count > 0);

    /* 7. Test FlowPlanArtifact Serialization & Deserialization (Evidence Spine) */
    FlowPlanArtifact art_saved;
    BIT_CHECK(flow_plan_to_artifact(&s_res.best_plan, &multi_ir, 42, &art_saved));
    BIT_CHECK(art_saved.contract_hash != 0);
    BIT_CHECK(art_saved.plan_schema_hash != 0);

    FILE *tmp_fp = fopen("/tmp/test_plan_artifact.flowplan", "w+");
    BIT_CHECK(tmp_fp != NULL);
    BIT_CHECK(flow_plan_artifact_save(tmp_fp, &art_saved));
    rewind(tmp_fp);

    FlowPlanArtifact art_loaded;
    BIT_CHECK(flow_plan_artifact_load(tmp_fp, &art_loaded));
    fclose(tmp_fp);

    BIT_CHECK(strcmp(art_loaded.flow_name, "rank_flow") == 0);
    BIT_CHECK(strcmp(art_loaded.component_id, s_res.best_plan.component->id) == 0);
    BIT_CHECK(art_loaded.plan_schema_hash == s_res.best_plan.schema_hash);
    BIT_CHECK(art_loaded.contract_hash == s_res.best_plan.contract_hash);
    BIT_CHECK(art_loaded.genome == s_res.best_plan.genome);
    BIT_CHECK(art_loaded.metrics.capacity == s_res.best_plan.eval.capacity);

    /* Test restoring plan from artifact */
    FlowPlan restored_plan;
    BIT_CHECK(flow_artifact_to_plan(&art_loaded, &multi_space, &restored_plan));
    BIT_CHECK(restored_plan.component == s_res.best_plan.component);
    BIT_CHECK(restored_plan.genome == s_res.best_plan.genome);

    /* 8. Test Strict Plan Artifact Validation (Evidence Spine Attestation) */
    char val_err[256];
    BIT_CHECK(flow_artifact_validate(&art_loaded, &multi_ir, &multi_space, val_err, sizeof(val_err)));

    /* Negative Test: Contract Hash Mismatch */
    FlowPlanArtifact bad_art = art_loaded;
    bad_art.contract_hash ^= 0xdeadbeef;
    BIT_CHECK(!flow_artifact_validate(&bad_art, &multi_ir, &multi_space, val_err, sizeof(val_err)));
    BIT_CHECK(strstr(val_err, "contract hash mismatch") != NULL);

    /* Negative Test: Plan Schema Hash Mismatch */
    bad_art = art_loaded;
    bad_art.plan_schema_hash ^= 0xfeedface;
    BIT_CHECK(!flow_artifact_validate(&bad_art, &multi_ir, &multi_space, val_err, sizeof(val_err)));
    BIT_CHECK(strstr(val_err, "plan schema hash mismatch") != NULL);

    /* Negative Test: Module Version Mismatch */
    bad_art = art_loaded;
    strncpy(bad_art.module_version, "99.0.0-incompatible", sizeof(bad_art.module_version) - 1);
    BIT_CHECK(!flow_artifact_validate(&bad_art, &multi_ir, &multi_space, val_err, sizeof(val_err)));
    BIT_CHECK(strstr(val_err, "version mismatch") != NULL);

    /* Negative Test: Incompatible Component Rejection */
    bad_art = art_loaded;
    strncpy(bad_art.component_id, "linear_array", sizeof(bad_art.component_id) - 1);
    BIT_CHECK(!flow_artifact_validate(&bad_art, &multi_ir, &multi_space, val_err, sizeof(val_err)));
    BIT_CHECK(strstr(val_err, "incompatible") != NULL);

    printf("FLOW_BITSPACE_TEST=passed candidates=%zu selector_bits=%u bit_count=%u schema_hash=%llu pareto_points=%zu\n",
           multi_space.candidate_count, multi_space.selector_bits, multi_space.bit_count,
           (unsigned long long)multi_space.schema_hash, s_res.pareto_count);
    return 0;
}
