#ifndef FLOW_BITSPACE_H
#define FLOW_BITSPACE_H
#include "flow.h"
#include "plugin.h"
#include "adaptive.h"
#include "bitmanifold.h"
#include "polyhedral.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#define FLOW_PARETO_MAX 16
#define FLOW_BITSPACE_MAX_CANDIDATES 32
typedef struct { uint64_t hard_safety_mask, hard_contract_mask, hard_resource_mask, hard_plugin_mask, hard_polytope_mask, hard_composite_mask;
    uint64_t domain_preference_mask, dynamic_telemetry_bias, soft_composite_bias; } FlowMaskCanvas; typedef enum {
    FLOW_BMF_SUBSPACE_GENERIC = 0, FLOW_BMF_SUBSPACE_SMOOTH_FETCH_LATTE = 1, FLOW_BMF_SUBSPACE_AGILE_SPRINT = 2,
    FLOW_BMF_SUBSPACE_COLLABORATIVE_HOLD = 3, FLOW_BMF_SUBSPACE_EMERGENCY_PROTECT = 4, FLOW_BMF_SUBSPACE_HFT_LATENCY = 5,
    FLOW_BMF_SUBSPACE_QUIESCENT_IOT = 6, FLOW_BMF_SUBSPACE_MAX = 16
} FlowBmfSubspaceId;
#define FLOW_BMF_SUBSPACE_FEATURE_DIM 16
typedef struct { uint32_t subspace_id; char name[48], description[96]; double centroid[FLOW_BMF_SUBSPACE_FEATURE_DIM];
    uint64_t invariant_mask, malleable_mask, default_switches; } FlowBmfSubspace;
typedef struct { FlowBmfSubspace subspaces[FLOW_BMF_SUBSPACE_MAX]; size_t subspace_count; int is_initialized; } FlowBmfSubspaceRegistry;
void flow_bmf_subspace_init_registry(FlowBmfSubspaceRegistry *reg); const FlowBmfSubspace *flow_bmf_subspace_lookup(const FlowBmfSubspaceRegistry *reg, uint32_t subspace_id);
uint32_t flow_bmf_subspace_index_from_features(const FlowBmfSubspaceRegistry *reg, const double *features, size_t dim);
void flow_bmf_canvas_init_from_subspace(FlowBmf1BitCanvas *canvas, const FlowBmfSubspace *subspace); int flow_bmf_canvas_adjudicate_smt(FlowBmf1BitCanvas *canvas);
void flow_bmf_canvas_to_mask_canvas(const FlowBmf1BitCanvas *canvas, FlowMaskCanvas *mask_canvas_out);
void flow_bmf_canvas_from_mask_canvas(const FlowMaskCanvas *mask_canvas, uint32_t subspace_id, FlowBmf1BitCanvas *canvas_out); typedef struct {
    double energy, latency_score, throughput_score; size_t memory_bytes, capacity; uint64_t benchmark_ns; int hard_gate_passed; char message[128]; } FlowEvaluation;
#define FLOW_GENOME_MAX_WORDS 16
typedef struct { uint64_t words[FLOW_GENOME_MAX_WORDS]; uint32_t active_words, total_bits; } FlowGenome; void flow_genome_init(FlowGenome *g, uint32_t total_bits);
void flow_genome_from_u64(FlowGenome *g, uint64_t val, uint32_t total_bits); uint64_t flow_genome_to_u64(const FlowGenome *g);
int flow_genome_get_bit(const FlowGenome *g, uint32_t bit_idx); void flow_genome_set_bit(FlowGenome *g, uint32_t bit_idx, int val);
void flow_genome_flip_bit(FlowGenome *g, uint32_t bit_idx); void flow_genome_mutate_1bit(FlowGenome *g, uint64_t *rng_state, uint32_t *mutated_bit_out);
int flow_genome_equals(const FlowGenome *a, const FlowGenome *b);
#define FLOW_MAX_LINKAGE_GROUPS 16
#define FLOW_MAX_LINKED_BITS 8
typedef struct { uint32_t bit_indices[FLOW_MAX_LINKED_BITS]; size_t bit_count; char rationale[64]; } FlowGeneLinkageGroup;
typedef struct { FlowGeneLinkageGroup groups[FLOW_MAX_LINKAGE_GROUPS]; size_t group_count; } FlowGeneLinkageMap; void flow_linkage_map_init(FlowGeneLinkageMap *map);
int flow_linkage_map_add_group(FlowGeneLinkageMap *map, const uint32_t *bits, size_t count, const char *rationale);
void flow_genome_mutate_with_linkage(FlowGenome *g, const FlowGeneLinkageMap *linkage, uint64_t *rng_state, uint32_t *primary_bit_out, size_t *linked_flips_out);
typedef struct { const Component *component; FlowPlanDimensionSet dimension_set; FlowPlanAssignment assignment; FlowEvaluation eval;
    uint64_t genome, schema_hash, contract_hash; uint32_t bit_count; } FlowPlan; typedef struct FlowBitSpace FlowBitSpace; struct FlowBitSpace { const SemanticIR *ir;
    const Component *candidates[FLOW_BITSPACE_MAX_CANDIDATES]; FlowPlanDimensionSet candidate_dims[FLOW_BITSPACE_MAX_CANDIDATES];
    uint32_t candidate_bits[FLOW_BITSPACE_MAX_CANDIDATES]; uint64_t candidate_masks[FLOW_BITSPACE_MAX_CANDIDATES];
    FlowMaskCanvas candidate_canvases[FLOW_BITSPACE_MAX_CANDIDATES]; FlowMaskCanvas global_canvas; size_t candidate_count; uint32_t selector_bits, bit_count;
    uint64_t contract_hash, schema_hash, env_mask; int (*decode)(const FlowBitSpace *space, uint64_t genome, FlowPlan *plan);
    int (*evaluate)(const FlowBitSpace *space, const FlowPlan *plan, FlowEvaluation *result);
    int (*hard_gate)(const FlowBitSpace *space, const FlowPlan *plan, const FlowEvaluation *result); }; typedef struct FlowPlanArtifact {
    char flow_name[64], module_name[64], module_version[32], component_id[64]; uint64_t contract_hash, plan_schema_hash, genome; uint32_t seed, bit_count;
    FlowPlanDimensionSet dimensions; FlowPlanAssignment plan; FlowEvaluation metrics; char verification_status[32], attestation_msg[128];
    double jet_omega, jet_barrier_mu, jet_q_saturation; } FlowPlanArtifact; uint64_t flow_compute_contract_hash(const SemanticIR *ir);
uint64_t flow_bitspace_compute_schema_hash(const SemanticIR *ir, const Component *comp, const FlowPlanDimensionSet *dims);
int flow_bitspace_init_single(const SemanticIR *ir, const Component *comp, FlowBitSpace *space); int flow_bitspace_init_for_ir(const SemanticIR *ir, FlowBitSpace *space);
uint64_t flow_bitspace_mutate_1bit(const FlowBitSpace *space, uint64_t genome, uint64_t *rng_state, uint32_t *mutated_bit_out);
uint64_t flow_bitspace_mutate_1bit_masked(const FlowBitSpace *space, uint64_t genome, uint64_t env_mask, uint64_t *rng_state, uint32_t *mutated_bit_out);
uint64_t flow_bitspace_encode(const FlowBitSpace *space, size_t cand_idx, const FlowPlanAssignment *plan); uint64_t flow_bitspace_default_genome(const FlowBitSpace *space);
typedef enum { FLOW_GATE_PASS = 0, FLOW_GATE_FAIL_MEMORY_LIMIT, FLOW_GATE_FAIL_VERIFIER_UNPROVEN,
    FLOW_GATE_FAIL_THREAD_AFFINITY, FLOW_GATE_FAIL_QUOTA_EXCEEDED, FLOW_GATE_FAIL_DIMENSION_BOUND,
    FLOW_GATE_FAIL_REENTRANCY, FLOW_GATE_FAIL_MUTATION_INVALID, FLOW_GATE_CATEGORY_MAX
} FlowGateFailureReason; typedef struct { uint64_t total_mutations, total_failures, failure_counts[FLOW_GATE_CATEGORY_MAX]; } FlowSearchHeatmap; typedef struct {
    FlowPlan best_plan; size_t pareto_count, iterations; FlowPlan pareto_points[FLOW_PARETO_MAX];
    double best_latency, best_memory, latency_regret_percent, memory_regret_percent; uint32_t seed; int measured; FlowSearchHeatmap heatmap; FlowMaskCanvas mask_canvas;
} FlowBitSearchResult; const char *flow_gate_failure_name(FlowGateFailureReason reason);
int flow_mask_canvas_compose(const SemanticIR *ir, const Component *comp, const FlowPlanDimensionSet *dims, const FlowPMUTelemetry *pmu, FlowMaskCanvas *canvas_out);
void flow_mask_canvas_report(const FlowMaskCanvas *canvas, FILE *out);
uint64_t flow_bitspace_mutate_1bit_superposed(const FlowBitSpace *space, uint64_t genome, const FlowMaskCanvas *canvas, double bias_weight, uint64_t *rng_state, uint32_t *mutated_bit_out);
int flow_bitspace_search(const FlowBitSpace *space, size_t iterations, uint32_t seed, int measured, const FlowPlan *seed_plan, FlowBitSearchResult *result_out);
typedef enum { FLOW_TACTIC_SPEED = 0, FLOW_TACTIC_BALANCED = 1, FLOW_TACTIC_MEMORY = 2, FLOW_TACTIC_COUNT = 3 } FlowPlanTactic;
typedef struct { FlowPlan tactics[FLOW_TACTIC_COUNT]; int available[FLOW_TACTIC_COUNT]; size_t count; } FlowPlanEnsemble;
const char *flow_plan_tactic_name(FlowPlanTactic tactic); int flow_bitspace_extract_ensemble(const FlowBitSearchResult *search_res, FlowPlanEnsemble *ensemble_out);
int flow_plan_artifact_save(FILE *output, const FlowPlanArtifact *artifact); int flow_plan_artifact_load(FILE *input, FlowPlanArtifact *artifact);
int flow_plan_to_artifact(const FlowPlan *plan, const SemanticIR *ir, uint32_t seed, FlowPlanArtifact *art);
int flow_artifact_to_plan(const FlowPlanArtifact *art, const FlowBitSpace *space, FlowPlan *plan);
int flow_artifact_validate(const FlowPlanArtifact *art, const SemanticIR *ir, const FlowBitSpace *space, char *err_msg, size_t err_size);
#endif
