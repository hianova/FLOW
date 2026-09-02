#ifndef FLOW_ORCHESTRATOR_H
#define FLOW_ORCHESTRATOR_H

#include "flow.h"
#include "registry.h"
#include "bitspace.h"
#include "search.h"
#include "smt.h"
#include "topology.h"

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

#define FLOW_ORCHESTRATOR_MAX_INTENTS 32
#define FLOW_ORCHESTRATOR_MAX_EPOCHS 64

typedef enum {
    FLOW_ABSORB_OK = 0,               /* Successfully absorbed and merged into active topology */
    FLOW_ABSORB_MUTEX_CONFLICT = 1,   /* Mathematical Mutex: SMT proved constraints mutually exclusive */
    FLOW_ABSORB_INVALID = 2,          /* Parsing or contract syntax failure */
    FLOW_ABSORB_ALREADY_ABSORBED = 3  /* Idempotent absorption */
} FlowAbsorbStatus;

typedef struct {
    char flow_name[FLOW_NAME];
    char file_path[256];
    FlowSpec spec;
    SemanticIR ir;
    uint64_t contract_hash;
    uint64_t schema_hash;
    int active;
} FlowAbsorbedIntent;

typedef struct {
    uint64_t epoch_id;
    uint64_t timestamp_ns;
    double global_energy;
    double entropy_score;
    char primary_component[64];
    FlowPlanEnsemble ensemble;
    SearchResult search_result;
    FlowSMTProofAttestation smt_proof;
    size_t active_intent_count;
} FlowOrchestratorEpoch;

typedef struct FlowOrchestrator FlowOrchestrator;

struct FlowOrchestrator {
    char workspace_dir[256];
    FlowAbsorbedIntent intents[FLOW_ORCHESTRATOR_MAX_INTENTS];
    size_t intent_count;
    FlowOrchestratorEpoch epochs[FLOW_ORCHESTRATOR_MAX_EPOCHS];
    size_t epoch_count;
    uint64_t current_epoch_id;
    FlowTopologyGraph topology_graph;
    SemanticIR unified_ir;
    int has_unified_ir;
};

/* Lifecycle */
FlowOrchestrator *flow_orchestrator_create(const char *workspace_dir);
void flow_orchestrator_destroy(FlowOrchestrator *orch);

/* 1. Semantic Merge (flow absorb) */
FlowAbsorbStatus flow_orchestrator_absorb(FlowOrchestrator *orch,
                                          const char *spec_file,
                                          char *diag_msg,
                                          size_t diag_size);

/* 2. Global Constraint Annealing & Epoch Solidification (flow anneal) */
int flow_orchestrator_anneal(FlowOrchestrator *orch,
                             size_t iterations,
                             uint32_t seed,
                             FlowOrchestratorEpoch *epoch_out);

/* 3. Topological Landscape Status (flow landscape) */
int flow_orchestrator_landscape(const FlowOrchestrator *orch, FILE *out);

/* 4. Continuous Background Entropy Reduction (flow refactor) */
int flow_orchestrator_refactor_entropy(FlowOrchestrator *orch, double *entropy_delta_out);

/* 5. Constraint-Based State Time Travel (flow morph) */
int flow_orchestrator_time_travel(FlowOrchestrator *orch,
                                  FlowPlanTactic tactic,
                                  FlowPlan *plan_out);

size_t flow_orchestrator_intent_count(const FlowOrchestrator *orch);

const char *flow_absorb_status_name(FlowAbsorbStatus status);

#endif
