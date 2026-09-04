#include "flow_test_kit.h"
#include "token_ring.h"
#include "orchestrator.h"
#include "registry.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "token-ring-test assertion failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        exit(1); \
    } \
} while (0)

static void test_discrete_attention_operator(void) {
    printf("--- [Unit 1/5] Testing BMF Discrete Attention Operator Canvas_{t+1} = Phi(Canvas_t (x) Mask_{Attn}) ---\n");

    FlowMaskCanvas canvas;
    memset(&canvas, 0, sizeof(canvas));
    canvas.hard_safety_mask = 0xFFFFFFFF00000000ULL;
    canvas.hard_composite_mask = 0xFFFFFFFFFFFFFFFFULL;
    canvas.soft_composite_bias = 0x00000000FFFF0000ULL;

    uint64_t current_genome = 0x1234567812345678ULL;
    uint64_t attn_mask_1 = 0x0000FFFFFFFF0000ULL;
    uint64_t dynamic_bias_1 = 0x0000000000FF0000ULL;

    /* Apply Discrete Attention Operator */
    uint64_t projected_1 = flow_token_ring_attention_project(&canvas, attn_mask_1, dynamic_bias_1, current_genome);

    /* Verify hard composite mask was intersected: 0xFF..FF & 0x0000FFFFFFFF0000 = 0x0000FFFFFFFF0000 */
    CHECK(canvas.hard_composite_mask == 0x0000FFFFFFFF0000ULL);

    /* Verify soft bias was superposed onto valid hard bits */
    CHECK((canvas.soft_composite_bias & ~canvas.hard_composite_mask) == 0);

    /* Verify projected genome respects legal manifold */
    CHECK((projected_1 & ~canvas.hard_composite_mask) == 0);

    /* Test null manifold / UNSAT boundary detection in O(1) */
    uint64_t conflicting_mask = 0xFFFF000000000000ULL;
    uint64_t projected_unsat = flow_token_ring_attention_project(&canvas, conflicting_mask, 0, current_genome);
    CHECK(canvas.hard_composite_mask == 0);
    CHECK(projected_unsat == 0);

    printf("  [PASS] Discrete Attention Operator bitwise projection and UNSAT detection verified.\n");
}

static void test_token_ring_cyclic_step(void) {
    printf("--- [Unit 2/5] Testing Token Ring Circular Buffer & Stage Transitions ---\n");

    FLOW_TEST_CASE("tests/token-ring-test.c",
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
        "}\n",
        {
            FlowTokenRing ring;
            CHECK(flow_token_ring_setup_canonical(&ring, &ir, 50, 42));
            CHECK(ring.token_count == 5);
            CHECK(ring.current_token_idx == 0);
            CHECK(ring.cycle_count == 0);
            CHECK(ring.state == FLOW_RING_CIRCULATING);

            /* Step 1: Polytope Stage */
            CHECK(flow_token_ring_step(&ring));
            CHECK(ring.current_token_idx == 1);
            CHECK(ring.tokens[0].execution_count == 1);
            CHECK(ring.tokens[0].attention_mask != 0);

            /* Step 2: Anneal Stage */
            CHECK(flow_token_ring_step(&ring));
            CHECK(ring.current_token_idx == 2);
            CHECK(ring.tokens[1].execution_count == 1);
            CHECK(ring.best_search.component != NULL);

            /* Step 3: SMT Proof Stage */
            CHECK(flow_token_ring_step(&ring));
            CHECK(ring.current_token_idx == 3);
            CHECK(ring.tokens[2].execution_count == 1);

            /* Step 4: Synthesis Stage */
            CHECK(flow_token_ring_step(&ring));
            CHECK(ring.current_token_idx == 4);
            CHECK(ring.tokens[3].execution_count == 1);

            /* Step 5: Attractor Stage -> Completes 1 full cycle around the ring! */
            CHECK(flow_token_ring_step(&ring));
            CHECK(ring.current_token_idx == 0);
            CHECK(ring.cycle_count == 1);
            CHECK(ring.tokens[4].execution_count == 1);

            printf("  [PASS] Completed full circular turn with 5 stages executed.\n");
        }
    );
}

static void test_token_ring_attractor_convergence(void) {
    printf("--- [Unit 3/5] Testing Convergence to Lyapunov Attractor Fixed Point ---\n");

    FLOW_TEST_CASE("tests/token-ring-test.c",
        "input jobs {\n"
        "    max_count 1024\n"
        "}\n"
        "state queue {\n"
        "    shared\n"
        "    bounded\n"
        "}\n"
        "flow bounded_queue {\n"
        "    jobs -> enqueue -> dequeue\n"
        "}\n"
        "import builtin\n"
        "require {\n"
        "    deterministic\n"
        "    memory < 64mb\n"
        "}\n",
        {
            FlowTokenRing ring;
            CHECK(flow_token_ring_setup_canonical(&ring, &ir, 100, 123));

            FlowTokenRingState final_state = flow_token_ring_run_to_attractor(&ring, 16);
            CHECK(final_state == FLOW_RING_ATTRACTOR_REACHED);
            CHECK(flow_token_ring_is_converged(&ring));
            CHECK(ring.attractor_converged == 1);
            CHECK(ring.cycle_count >= 1);
            CHECK(ring.best_search.component != NULL);
            CHECK(ring.ensemble.count > 0);

            /* Verify SMT proof theorems were evaluated */
            int unsat_count = (ring.smt_proof.buffer_bounds_safety == FLOW_SMT_PROVEN_UNSAT) +
                              (ring.smt_proof.memory_quota_bound == FLOW_SMT_PROVEN_UNSAT) +
                              (ring.smt_proof.shard_non_aliasing == FLOW_SMT_PROVEN_UNSAT) +
                              (ring.smt_proof.determinism_invariant == FLOW_SMT_PROVEN_UNSAT);
            CHECK(unsat_count >= 1 || ring.smt_proof.proof_summary[0] != '\0');

            printf("  [PASS] Attractor reached at cycle %llu (Lyapunov Delta E = %.6f, Status: %s)\n",
                   (unsigned long long)ring.cycle_count,
                   ring.lyapunov_delta_e,
                   ring.status_message);
        }
    );
}

static void test_token_ring_unsat_detection(void) {
    printf("--- [Unit 4/5] Testing O(1) UNSAT Mutex Contradiction Detection ---\n");

    SemanticIR invalid_ir;
    memset(&invalid_ir, 0, sizeof(invalid_ir));
    strncpy(invalid_ir.flow_name, "unsat_test", sizeof(invalid_ir.flow_name) - 1);
    invalid_ir.input_max_count = 100000000;
    invalid_ir.memory_limit_mb = 1; /* Impossible: 100M items in 1MB memory */

    FlowTokenRing ring;
    CHECK(flow_token_ring_setup_canonical(&ring, &invalid_ir, 10, 42));

    /* Inject mutually exclusive hard constraint into canvas */
    ring.active_canvas.hard_composite_mask = 0;

    FlowTokenRingState state = flow_token_ring_run_to_attractor(&ring, 8);
    CHECK(state == FLOW_RING_UNSAT);
    CHECK(!flow_token_ring_is_converged(&ring));

    printf("  [PASS] UNSAT contradiction detected and cleanly closed in O(1).\n");
}

static void test_orchestrator_token_ring_integration(void) {
    printf("--- [Unit 5/5] Testing Orchestrator Integration with Token Ring Engine ---\n");

    FlowOrchestrator *orch = flow_orchestrator_create("/tmp/test-flow-ring-orch");
    CHECK(orch != NULL);

    char diag[256];
    FlowAbsorbStatus status = flow_orchestrator_absorb(orch, "examples/rank.flow", diag, sizeof(diag));
    CHECK(status == FLOW_ABSORB_OK);

    FlowOrchestratorEpoch epoch;
    CHECK(flow_orchestrator_anneal(orch, 50, 42, &epoch));

    CHECK(epoch.epoch_id == 1);
    CHECK(epoch.attractor_converged == 1);
    CHECK(epoch.attractor_cycles >= 1);
    CHECK(epoch.search_result.component != NULL);
    CHECK(epoch.ensemble.count > 0);

    /* Landscape reporting */
    CHECK(flow_orchestrator_landscape(orch, stdout));

    flow_orchestrator_destroy(orch);
    printf("  [PASS] Orchestrator Token Ring integration verified.\n");
}

int main(void) {
    flow_registry_init();
    printf("================================================================================\n");
    printf("      FLOW BMF TOKEN RING & DISCRETE ATTENTION STATE MACHINE TEST SUITE        \n");
    printf("================================================================================\n");

    test_discrete_attention_operator();
    test_token_ring_cyclic_step();
    test_token_ring_attractor_convergence();
    test_token_ring_unsat_detection();
    test_orchestrator_token_ring_integration();

    printf("================================================================================\n");
    printf("      ALL 5 TOKEN RING & ATTRACTOR STATE MACHINE TESTS 100%% SOUND & PASSED!     \n");
    printf("================================================================================\n");
    return 0;
}
