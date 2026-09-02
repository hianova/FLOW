#ifndef FLOW_BITSPACE_H
#define FLOW_BITSPACE_H

#include "flow.h"
#include "plugin.h"
#include "adaptive.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define FLOW_PARETO_MAX 16
#define FLOW_BITSPACE_MAX_CANDIDATES 32

/* 3-Tier Dynamic Mask Canvas (Superposition Architecture & Polyhedral Projection) */
typedef struct {
    uint64_t hard_safety_mask;       /* src/security.c (Ownership, FD limits, Sanitizer safety) */
    uint64_t hard_contract_mask;     /* src/verifier.c (IR contracts, sequential/parallel semantics) */
    uint64_t hard_resource_mask;     /* src/verifier.c (Hard memory quota ceiling) */
    uint64_t hard_plugin_mask;       /* src/plugin.h (Domain-declared hard mutation limits) */
    uint64_t hard_polytope_mask;     /* Orthogonal projection of Polyhedron P = {Ax <= b} on {0,1}^N */
    uint64_t hard_composite_mask;    /* Mathematical Composite Hard Mask */

    uint64_t domain_preference_mask; /* src/plugin.h (Expert domain knowledge guidance) */
    uint64_t dynamic_telemetry_bias; /* src/adaptive.c (eBPF / PMU real-time signals) */
    uint64_t soft_composite_bias;    /* Superposition: Probability-Biasing Manifold */
} FlowMaskCanvas;

/* ========================================================================= */
/* Mathematical Polyhedral Constraint & Hypercube Projection Engine         */
/* Linear Inequality System: \mathcal{P} = { x \in R^D | A x <= b }          */
/* Orthogonal Projection Operator: \Pi_{\mathcal{P}} : R^D -> {0, 1}^N       */
/* ========================================================================= */

#define FLOW_POLYTOPE_MAX_CONSTRAINTS 32
#define FLOW_POLYTOPE_MAX_DIMS 16

typedef enum {
    FLOW_CONSTRAINT_LEQ = 0, /* a^T x <= b */
    FLOW_CONSTRAINT_GEQ = 1, /* a^T x >= b */
    FLOW_CONSTRAINT_EQ  = 2, /* a^T x == b */
    FLOW_CONSTRAINT_INTERVAL = 3 /* b_min <= a^T x <= b_max */
} FlowConstraintOp;

typedef struct {
    double coefficients[FLOW_POLYTOPE_MAX_DIMS];
    FlowConstraintOp op;
    double rhs_min;
    double rhs_max;
    char symbolic_tag[64];
} FlowLinearConstraint;

typedef struct {
    FlowLinearConstraint constraints[FLOW_POLYTOPE_MAX_CONSTRAINTS];
    size_t constraint_count;
    size_t dimension_count;
    double lower_bounds[FLOW_POLYTOPE_MAX_DIMS];
    double upper_bounds[FLOW_POLYTOPE_MAX_DIMS];
} FlowPolyhedronSystem;

/* Polyhedron Lifecycle & Hypercube Projection */
void flow_polyhedron_init(FlowPolyhedronSystem *poly, size_t dim_count);
int flow_polyhedron_add_box_bounds(FlowPolyhedronSystem *poly, size_t dim_idx, double min_val, double max_val, const char *tag);
int flow_polyhedron_add_inequality(FlowPolyhedronSystem *poly, const double *coeffs, FlowConstraintOp op, double bound, const char *tag);
int flow_polyhedron_from_ir(const SemanticIR *ir, const Component *comp, const FlowPlanDimensionSet *dims, FlowPolyhedronSystem *poly);
uint64_t flow_polyhedron_project_mask(const FlowPolyhedronSystem *poly, const FlowPlanDimensionSet *dims, uint32_t total_bits);

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

#define FLOW_GENOME_MAX_WORDS 16 /* 16 * 64 = 1024 bits maximum */

typedef struct {
    uint64_t words[FLOW_GENOME_MAX_WORDS];
    uint32_t active_words;
    uint32_t total_bits;
} FlowGenome;

void flow_genome_init(FlowGenome *g, uint32_t total_bits);
void flow_genome_from_u64(FlowGenome *g, uint64_t val, uint32_t total_bits);
uint64_t flow_genome_to_u64(const FlowGenome *g);
int flow_genome_get_bit(const FlowGenome *g, uint32_t bit_idx);
void flow_genome_set_bit(FlowGenome *g, uint32_t bit_idx, int val);
void flow_genome_flip_bit(FlowGenome *g, uint32_t bit_idx);
void flow_genome_mutate_1bit(FlowGenome *g, uint64_t *rng_state, uint32_t *mutated_bit_out);
int flow_genome_equals(const FlowGenome *a, const FlowGenome *b);

/* SMT-Driven Epistatic Gene Linkage Groups & Super-Bit Coordinated Mutations */
#define FLOW_MAX_LINKAGE_GROUPS 16
#define FLOW_MAX_LINKED_BITS 8

typedef struct {
    uint32_t bit_indices[FLOW_MAX_LINKED_BITS];
    size_t bit_count;
    char rationale[64]; /* e.g., "threads_shards_epistatic_synergy" */
} FlowGeneLinkageGroup;

typedef struct {
    FlowGeneLinkageGroup groups[FLOW_MAX_LINKAGE_GROUPS];
    size_t group_count;
} FlowGeneLinkageMap;

void flow_linkage_map_init(FlowGeneLinkageMap *map);
int flow_linkage_map_add_group(FlowGeneLinkageMap *map, const uint32_t *bits, size_t count, const char *rationale);
void flow_genome_mutate_with_linkage(FlowGenome *g, const FlowGeneLinkageMap *linkage,
                                    uint64_t *rng_state, uint32_t *primary_bit_out, size_t *linked_flips_out);

typedef struct {
    const Component *component;
    FlowPlanDimensionSet dimension_set;
    FlowPlanAssignment assignment;
    FlowEvaluation eval;
    uint64_t genome;
    FlowGenome genome_multiword;
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
    uint64_t candidate_masks[FLOW_BITSPACE_MAX_CANDIDATES]; /* Epigenetic mutation mask per candidate */
    FlowMaskCanvas candidate_canvases[FLOW_BITSPACE_MAX_CANDIDATES]; /* 3-Tier Mask Canvas per candidate */
    FlowMaskCanvas global_canvas; /* Global composite canvas across all candidates */
    size_t candidate_count;
    uint32_t selector_bits;
    uint32_t bit_count;
    uint64_t contract_hash;
    uint64_t schema_hash;
    uint64_t env_mask; /* Epigenetic environmental & semantic constraint mask */

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
uint64_t flow_bitspace_mutate_1bit_masked(const FlowBitSpace *space, uint64_t genome, uint64_t env_mask, uint64_t *rng_state, uint32_t *mutated_bit_out);
uint64_t flow_bitspace_encode(const FlowBitSpace *space, size_t cand_idx, const FlowPlanAssignment *plan);
uint64_t flow_bitspace_default_genome(const FlowBitSpace *space);

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
    FlowMaskCanvas mask_canvas;
} FlowBitSearchResult;

const char *flow_gate_failure_name(FlowGateFailureReason reason);
void flow_search_heatmap_report(const FlowSearchHeatmap *heatmap, FILE *out);

/* 3-Tier Dynamic Mask Canvas Composition & Report */
int flow_mask_canvas_compose(const SemanticIR *ir, const Component *comp,
                            const FlowPlanDimensionSet *dims,
                            const FlowPMUTelemetry *pmu,
                            FlowMaskCanvas *canvas_out);
void flow_mask_canvas_report(const FlowMaskCanvas *canvas, FILE *out);
uint64_t flow_bitspace_mutate_1bit_superposed(const FlowBitSpace *space, uint64_t genome,
                                             const FlowMaskCanvas *canvas, double bias_weight,
                                             uint64_t *rng_state, uint32_t *mutated_bit_out);

/* Transition Cost Model for Emergent Structural vs Parameter Decision-Making */
typedef struct {
    int has_active_baseline;
    const FlowPlan *baseline_plan;
    size_t live_state_bytes;
    size_t horizon_calls;
    double jit_penalty_energy;
    double bandwidth_cost_per_byte;
} FlowTransitionCostModel;

double flow_bitspace_calculate_transition_penalty(const FlowTransitionCostModel *model,
                                                  const FlowPlan *candidate,
                                                  int *is_structural_out);

/* Thermodynamic Boltzmann Probability-Biasing Chaos Configuration */
typedef struct {
    double initial_temperature;      /* Starting thermal energy (default: 80.0 ~ 100.0) */
    double cooling_decay;            /* Geometric cooling rate (default: 0.95 ~ 0.995) */
    size_t plateau_stagnation_limit; /* Plateau steps before thermodynamic reheating (default: 5 ~ 8) */
    double reheat_ratio;             /* Fraction of initial temp restored on plateau (default: 0.6) */
    uint64_t env_mask;               /* Epigenetic environmental & semantic mask (0 = all bits) */
    FlowMaskCanvas mask_canvas;      /* 3-Tier Dynamic Mask Superposition Canvas */
    double soft_bias_weight;         /* Probability weight for soft bias (default: 0.70) */
    int use_mask_canvas;             /* Flag indicating custom mask canvas is active */
} FlowChaosAnnealConfig;

/* Canonical Adaptive Chaos Search with Thermodynamic Probability Biasing */
int flow_bitspace_search_adaptive(const FlowBitSpace *space, size_t iterations, uint32_t seed,
                                  int measured, const FlowTransitionCostModel *transition_model,
                                  FlowBitSearchResult *result_out);

int flow_bitspace_search_configured(const FlowBitSpace *space, size_t iterations, uint32_t seed,
                                    int measured, const FlowTransitionCostModel *transition_model,
                                    const FlowChaosAnnealConfig *anneal_config,
                                    FlowBitSearchResult *result_out);

int flow_bitspace_search(const FlowBitSpace *space, size_t iterations, uint32_t seed,
                         int measured, const FlowPlan *seed_plan, FlowBitSearchResult *result_out);

int flow_bitspace_explain_seed(const FlowBitSpace *space, size_t iterations, uint32_t seed,
                               int measured, const FlowPlan *seed_plan, FILE *out);

/* Backward-Compatible Aliases & Research Baselines */
typedef struct {
    size_t macro_cycles;
    size_t micro_steps_per_cycle;
    double macro_tunneling_prob;
    size_t plateau_stagnation_limit;
} FlowTwoTierChaosConfig;

int flow_bitspace_search_two_tier(const FlowBitSpace *space,
                                  const FlowTwoTierChaosConfig *config,
                                  uint32_t seed, int measured,
                                  const FlowTransitionCostModel *transition_model,
                                  FlowBitSearchResult *result_out);

int flow_bitspace_search_single_tier(const FlowBitSpace *space, size_t iterations, uint32_t seed,
                                     int measured, const FlowTransitionCostModel *transition_model,
                                     FlowBitSearchResult *result_out);

/* Multi-Objective Pareto Plan Ensemble (Tactical Bundle) */
typedef enum {
    FLOW_TACTIC_SPEED = 0,    /* Minimizes latency / maximizes throughput */
    FLOW_TACTIC_BALANCED = 1, /* Knee point on Pareto frontier (minimal energy) */
    FLOW_TACTIC_MEMORY = 2,   /* Minimizes memory footprint */
    FLOW_TACTIC_COUNT = 3
} FlowPlanTactic;

typedef struct {
    FlowPlan tactics[FLOW_TACTIC_COUNT];
    int available[FLOW_TACTIC_COUNT];
    size_t count;
} FlowPlanEnsemble;

const char *flow_plan_tactic_name(FlowPlanTactic tactic);
int flow_bitspace_extract_ensemble(const FlowBitSearchResult *search_res,
                                   FlowPlanEnsemble *ensemble_out);

/* Plan Artifact I/O & Strict Validation (Evidence Spine Persistence) */
int flow_plan_artifact_save(FILE *output, const FlowPlanArtifact *artifact);
int flow_plan_artifact_load(FILE *input, FlowPlanArtifact *artifact);
int flow_plan_to_artifact(const FlowPlan *plan, const SemanticIR *ir, uint32_t seed, FlowPlanArtifact *art);
int flow_artifact_to_plan(const FlowPlanArtifact *art, const FlowBitSpace *space, FlowPlan *plan);
int flow_artifact_validate(const FlowPlanArtifact *art, const SemanticIR *ir,
                           const FlowBitSpace *space, char *err_msg, size_t err_size);

#endif
