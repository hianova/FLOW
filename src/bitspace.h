#ifndef FLOW_BITSPACE_H
#define FLOW_BITSPACE_H

#include "flow.h"
#include "plugin.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define FLOW_PARETO_MAX 16
#define FLOW_BITSPACE_MAX_CANDIDATES 32

typedef struct {
    double energy;
    double latency_score;
    double throughput_score;
    size_t memory_bytes;
    size_t capacity;
    uint64_t benchmark_ns;
    int hard_gate_passed;
    char message[128];
} FlowEvaluation;

typedef struct {
    const Component *component;
    FlowPlanDimensionSet dimension_set;
    FlowPlanAssignment assignment;
    FlowEvaluation eval;
    uint64_t genome;
    uint32_t bit_count;
    uint64_t schema_hash;
    uint64_t contract_hash;
} FlowPlan;

typedef struct FlowBitSpace FlowBitSpace;

struct FlowBitSpace {
    const SemanticIR *ir;
    const Component *candidates[FLOW_BITSPACE_MAX_CANDIDATES];
    FlowPlanDimensionSet candidate_dims[FLOW_BITSPACE_MAX_CANDIDATES];
    uint32_t candidate_bits[FLOW_BITSPACE_MAX_CANDIDATES];
    size_t candidate_count;
    uint32_t selector_bits;
    uint32_t bit_count;
    uint64_t contract_hash;
    uint64_t schema_hash;

    int (*decode)(const FlowBitSpace *space, uint64_t genome, FlowPlan *plan);
    int (*evaluate)(const FlowBitSpace *space, const FlowPlan *plan, FlowEvaluation *result);
    int (*hard_gate)(const FlowBitSpace *space, const FlowPlan *plan, const FlowEvaluation *result);
};

/* Unified Plan Artifact (Evidence Spine) */
typedef struct FlowPlanArtifact {
    char flow_name[64];
    char module_name[64];
    char module_version[32];
    char component_id[64];
    uint64_t contract_hash;
    uint64_t plan_schema_hash;
    uint32_t seed;
    uint32_t bit_count;
    uint64_t genome;
    FlowPlanDimensionSet dimensions;
    FlowPlanAssignment plan;
    FlowEvaluation metrics;
    char verification_status[32];
    char attestation_msg[128];
} FlowPlanArtifact;

/* Hash computation */
uint64_t flow_compute_contract_hash(const SemanticIR *ir);
uint64_t flow_bitspace_compute_schema_hash(const SemanticIR *ir, const Component *comp, const FlowPlanDimensionSet *dims);

/* BitSpace Lifecycle & Operations */
int flow_bitspace_init_single(const SemanticIR *ir, const Component *comp, FlowBitSpace *space);
int flow_bitspace_init_for_ir(const SemanticIR *ir, FlowBitSpace *space);
uint64_t flow_bitspace_mutate_1bit(const FlowBitSpace *space, uint64_t genome, uint64_t *rng_state, uint32_t *mutated_bit_out);
uint64_t flow_bitspace_encode(const FlowBitSpace *space, size_t cand_idx, const FlowPlanAssignment *plan);

/* Constraint Failure Categories for High-Speed Zero-Overhead Heatmap */
typedef enum {
    FLOW_GATE_PASS = 0,
    FLOW_GATE_FAIL_MEMORY_LIMIT,      /* Exceeds IR memory_limit_mb */
    FLOW_GATE_FAIL_VERIFIER_UNPROVEN, /* Verifier compile error or failed proof */
    FLOW_GATE_FAIL_THREAD_AFFINITY,   /* Thread / concurrency / component incompatibility */
    FLOW_GATE_FAIL_QUOTA_EXCEEDED,    /* Resource quota limit exceeded */
    FLOW_GATE_FAIL_DIMENSION_BOUND,   /* Value outside allowed dimension range or top_n */
    FLOW_GATE_FAIL_REENTRANCY,        /* Reentrancy / effect contract violation */
    FLOW_GATE_FAIL_MUTATION_INVALID,  /* Invalid bit mutation / decode failure */
    FLOW_GATE_CATEGORY_MAX
} FlowGateFailureReason;

typedef struct {
    uint64_t total_mutations;
    uint64_t total_failures;
    uint64_t failure_counts[FLOW_GATE_CATEGORY_MAX];
} FlowSearchHeatmap;

/* Search / Optimize over Hierarchical FlowBitSpace using 1-Bit Chaos Engine */
typedef struct {
    FlowPlan best_plan;
    size_t pareto_count;
    FlowPlan pareto_points[FLOW_PARETO_MAX];
    double best_latency;
    double best_memory;
    double latency_regret_percent;
    double memory_regret_percent;
    size_t iterations;
    uint32_t seed;
    int measured;
    FlowSearchHeatmap heatmap;
} FlowBitSearchResult;

const char *flow_gate_failure_name(FlowGateFailureReason reason);
void flow_search_heatmap_report(const FlowSearchHeatmap *heatmap, FILE *out);

int flow_bitspace_search(const FlowBitSpace *space, size_t iterations, uint32_t seed,
                         int measured, const FlowPlan *seed_plan, FlowBitSearchResult *result_out);

int flow_bitspace_explain_seed(const FlowBitSpace *space, size_t iterations, uint32_t seed,
                               int measured, const FlowPlan *seed_plan, FILE *out);

/* Plan Artifact I/O & Strict Validation (Evidence Spine Persistence) */
int flow_plan_artifact_save(FILE *output, const FlowPlanArtifact *artifact);
int flow_plan_artifact_load(FILE *input, FlowPlanArtifact *artifact);
int flow_plan_to_artifact(const FlowPlan *plan, const SemanticIR *ir, uint32_t seed, FlowPlanArtifact *art);
int flow_artifact_to_plan(const FlowPlanArtifact *art, const FlowBitSpace *space, FlowPlan *plan);
int flow_artifact_validate(const FlowPlanArtifact *art, const SemanticIR *ir,
                           const FlowBitSpace *space, char *err_msg, size_t err_size);

#endif
