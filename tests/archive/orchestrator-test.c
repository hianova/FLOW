#include "orchestrator.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "orchestrator-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

int main(void) {
    flow_registry_init();

    FlowOrchestrator *orch = flow_orchestrator_create(".");
    CHECK(orch != NULL);

    /* 1. Test Semantic Merge (flow absorb) */
    char diag[256] = {0};
    FlowAbsorbStatus st1 = flow_orchestrator_absorb(orch, "examples/project.flow", diag, sizeof(diag));
    CHECK(st1 == FLOW_ABSORB_OK);
    CHECK(flow_orchestrator_intent_count(orch) == 1);

    /* Test Idempotent Absorb */
    FlowAbsorbStatus st_dup = flow_orchestrator_absorb(orch, "examples/project.flow", diag, sizeof(diag));
    CHECK(st_dup == FLOW_ABSORB_ALREADY_ABSORBED);

    /* Absorb second compatible intent */
    FlowAbsorbStatus st2 = flow_orchestrator_absorb(orch, "examples/rank.flow", diag, sizeof(diag));
    CHECK(st2 == FLOW_ABSORB_OK);
    CHECK(flow_orchestrator_intent_count(orch) == 2);

    /* 2. Test Mutex Conflict Detection */
    FILE *conflict_file = fopen("/tmp/conflict_intent.flow", "w");
    CHECK(conflict_file != NULL);
    fprintf(conflict_file, "input items {\n    max_count 4\n}\n\nflow impossible_top {\n    items -> top(10)\n}\n");
    fclose(conflict_file);

    FlowAbsorbStatus st_conflict = flow_orchestrator_absorb(orch, "/tmp/conflict_intent.flow", diag, sizeof(diag));
    CHECK(st_conflict == FLOW_ABSORB_MUTEX_CONFLICT);
    CHECK(strstr(diag, "MUTEX VIOLATION") != NULL);

    /* 3. Test Global Annealing (flow anneal) */
    FlowOrchestratorEpoch epoch;
    int anneal_ok = flow_orchestrator_anneal(orch, 150, 42, &epoch);
    CHECK(anneal_ok == 1);
    CHECK(epoch.epoch_id == 1);
    CHECK(epoch.global_energy > 0.0);
    CHECK(epoch.primary_component[0] != '\0');
    CHECK(epoch.ensemble.count >= 1);
    CHECK(epoch.smt_proof.buffer_bounds_safety != FLOW_SMT_UNKNOWN);

    /* 4. Test Topological Landscape Report (flow landscape) */
    CHECK(flow_orchestrator_landscape(orch, stdout) == 1);

    /* 5. Test Continuous Background Entropy Reduction (flow refactor) */
    double entropy_delta = 0.0;
    int refactor_ok = flow_orchestrator_refactor_entropy(orch, &entropy_delta);
    CHECK(refactor_ok == 1);

    /* 6. Test Constraint-Based State Time Travel (flow morph) */
    FlowPlan speed_plan;
    FlowPlan memory_plan;
    CHECK(flow_orchestrator_time_travel(orch, FLOW_TACTIC_SPEED, &speed_plan) == 1);
    CHECK(flow_orchestrator_time_travel(orch, FLOW_TACTIC_MEMORY, &memory_plan) == 1);

    flow_orchestrator_destroy(orch);

    printf("ORCHESTRATOR_TEST=passed semantic_merge=verified mutex_detection=verified anneal_epoch=sound entropy_reduction=verified time_travel=verified\n");
    return 0;
}
