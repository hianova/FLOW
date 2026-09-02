#ifndef FLOW_PLUGIN_H
#define FLOW_PLUGIN_H

#include "flow.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define FLOW_DIMENSION_MAX 16
#define FLOW_DIM_NAME_MAX 32

typedef struct FlowComponent Component;
typedef struct FlowPlugin FlowPlugin;
struct FlowSearchResult;
struct FlowPlanArtifact;
struct FlowUnit;

typedef enum {
    FLOW_DIM_EXPONENT,   /* 2^k (e.g. capacity = 1 << k) */
    FLOW_DIM_LINEAR,     /* min_val .. max_val with step */
    FLOW_DIM_DISCRETE,   /* discrete values or choices */
    FLOW_DIM_BOOLEAN     /* 0 or 1 */
} FlowDimensionKind;

/* Dimension Semantics: Physical Classification & Migration Class */
typedef enum {
    FLOW_DIM_CLASS_TACTILE_PARAM = 0,    /* Real-time parameter (0 migration penalty, no JIT reload needed) */
    FLOW_DIM_CLASS_STRUCTURAL_JIT = 1    /* Structural gene (requires JIT recompilation & state migration) */
} FlowDimensionClass;

typedef struct {
    char name[FLOW_DIM_NAME_MAX];
    FlowDimensionKind kind;
    FlowDimensionClass dim_class;
    uint64_t min_val;
    uint64_t max_val;
    uint64_t step;
    uint64_t default_val;
    uint64_t base_migration_cost_ns;
} FlowPlanDimension;

typedef struct {
    size_t count;
    FlowPlanDimension dimensions[FLOW_DIMENSION_MAX];
} FlowPlanDimensionSet;

static inline unsigned flow_dimension_bits(const FlowPlanDimension *dim) {
    if (dim == NULL) return 0;
    if (dim->kind == FLOW_DIM_BOOLEAN) return 1;
    if (dim->kind == FLOW_DIM_EXPONENT) {
        uint64_t span = dim->max_val >= dim->min_val ? dim->max_val - dim->min_val : 0;
        unsigned b = 1;
        while (((uint64_t)1 << b) <= span && b < 64) ++b;
        return b;
    }
    {
        uint64_t step = dim->step == 0 ? 1 : dim->step;
        uint64_t span = dim->max_val >= dim->min_val ? (dim->max_val - dim->min_val) / step : 0;
        unsigned b = 1;
        while (((uint64_t)1 << b) <= span && b < 64) ++b;
        return b;
    }
}

typedef struct {
    size_t count;
    uint64_t values[FLOW_DIMENSION_MAX];
} FlowPlanAssignment;

typedef struct {
    size_t capacity;
    size_t threads;
    size_t shards;
    size_t memory_bytes;
    double latency_score;
    double throughput_score;
    double energy;
} FlowPlanMetrics;

typedef enum {
    VERIFIER_PROVEN,
    VERIFIER_RUNTIME_CHECK,
    VERIFIER_COMPILE_ERROR
} VerificationStatus;

typedef struct FlowVerificationReport {
    VerificationStatus status;
    size_t capacity;
    size_t estimated_bytes;
    int max_count_proven;
    int runtime_input_guard;
    char message[128];
} VerificationReport;

/* Function pointer hooks provided by a Domain Module / Plugin */

/* 1. Validate domain semantic contracts and assumptions for this spec */
typedef int (*FlowPluginValidateContractFn)(const SemanticIR *ir,
                                            const FlowPlugin *plugin,
                                            char *message, size_t message_size);

/* 2. Optional hook to lower domain-specific semantics into IR facts */
typedef void (*FlowPluginLowerSemanticsFn)(const FlowSpec *spec,
                                          SemanticIR *ir,
                                          const FlowPlugin *plugin);

/* 3. Enumerate plan dimensions for a candidate */
typedef int (*FlowPluginEnumerateDimensionsFn)(const SemanticIR *ir,
                                              const Component *component,
                                              FlowPlanDimensionSet *dims_out);

/* 4. Evaluate multi-objective cost / metrics for a concrete plan assignment */
typedef int (*FlowPluginEvaluatePlanFn)(const SemanticIR *ir,
                                       const Component *component,
                                       const FlowPlanAssignment *plan,
                                       FlowPlanMetrics *metrics_out);

/* 5. Verify plan under hard constraints and domain contracts */
typedef int (*FlowPluginVerifyPlanFn)(const SemanticIR *ir,
                                     const Component *component,
                                     const FlowPlanAssignment *plan,
                                     VerificationReport *report_out);

/* 6. General compatibility of candidate with IR */
typedef int (*FlowPluginCompatibilityFn)(const SemanticIR *ir,
                                         const Component *component);

/* 7. Memory estimation model (legacy / fallback) */
typedef int (*FlowPluginMemoryModelFn)(const SemanticIR *ir,
                                       const Component *component,
                                       size_t capacity, size_t shards,
                                       size_t *estimated_bytes);

/* 8. Verification hook (legacy / fallback) */
typedef int (*FlowPluginVerifyFn)(const SemanticIR *ir,
                                  const Component *component,
                                  size_t capacity, size_t shards,
                                  char *message, size_t message_size);

/* 9. Native code emission */
typedef int (*FlowPluginEmitFn)(FILE *output, const SemanticIR *ir,
                                const Component *component,
                                const struct FlowSearchResult *search,
                                const struct FlowVerificationReport *verification,
                                int reload_adapter);

/* 10. Differential / Property Oracle */
typedef int (*FlowPluginOracleFn)(const char *fixture_path,
                                  char *message, size_t message_size);

/* 11. Workload preference */
typedef int (*FlowPluginPreferenceFn)(const SemanticIR *ir,
                                      const Component *component);

/* 12. Benchmark execution hook */
typedef uint64_t (*FlowPluginBenchmarkFn)(const SemanticIR *ir,
                                         const Component *component,
                                         const FlowPlanAssignment *plan);

/* 13. Opaque domain context cleanup hook */
typedef void (*FlowPluginFreeSemanticsFn)(void *domain_ctx);

/* 14. Epigenetic Environmental Mutation Mask Hook */
typedef uint64_t (*FlowPluginMutationMaskFn)(const SemanticIR *ir,
                                             const Component *component,
                                             const FlowPlanDimensionSet *dims);

/* 15. Domain Preference Mask Hook */
typedef uint64_t (*FlowPluginPreferenceMaskFn)(const SemanticIR *ir,
                                               const Component *component,
                                               const FlowPlanDimensionSet *dims);

/* 16. Semantic Contract Mask Hook */
typedef uint64_t (*FlowPluginContractMaskFn)(const SemanticIR *ir,
                                             const Component *component,
                                             const FlowPlanDimensionSet *dims);

/* 17. Resource Quota Mask Hook */
typedef uint64_t (*FlowPluginResourceMaskFn)(const SemanticIR *ir,
                                             const Component *component,
                                             const FlowPlanDimensionSet *dims,
                                             size_t memory_limit_bytes);

/* Environment Pressure Level for Dynamic Adaptability & Extreme Morphing */
typedef enum {
    FLOW_ENV_PRESSURE_NONE = 0,
    FLOW_ENV_PRESSURE_MEMORY_MODERATE,
    FLOW_ENV_PRESSURE_MEMORY_CRITICAL,    /* E.g. 100 tabs open, RAM near exhaustion */
    FLOW_ENV_PRESSURE_CACHE_THRASHING,    /* L2/L3 miss rate > 25% */
    FLOW_ENV_PRESSURE_LATENCY_SPIKE,      /* P99 tail latency violation */
    FLOW_ENV_PRESSURE_BATTERY_SAVER       /* Low power mode / TDP constraint */
} FlowEnvPressureLevel;

/* Hardware Architecture Class for Silicon Specialization */
typedef enum {
    FLOW_ARCH_GENERIC = 0,
    FLOW_ARCH_APPLE_SILICON,   /* Unified memory, 128-byte cache line, AMX/NEON */
    FLOW_ARCH_INTEL_AVX2,      /* 64-byte cache line, 256-bit SIMD */
    FLOW_ARCH_INTEL_AVX512,    /* 64-byte cache line, 512-bit SIMD */
    FLOW_ARCH_ARM_NEON         /* Generic AArch64 / Mobile */
} FlowHardwareArch;

typedef struct {
    FlowEnvPressureLevel pressure_level;
    FlowHardwareArch hardware_arch;
    size_t available_ram_bytes;
    size_t active_concurrent_tabs;
    size_t l2_cache_bytes;
    size_t l3_cache_bytes;
    double measured_miss_rate;
    double measured_ipc;
} FlowEnvironmentState;

/* 18. Dynamic Environment Adaptation Mask Hook */
typedef uint64_t (*FlowPluginEnvironmentMaskFn)(const SemanticIR *ir,
                                                const Component *component,
                                                const FlowPlanDimensionSet *dims,
                                                const FlowEnvironmentState *env);

struct FlowComponent {
    const char *id;
    const char *kind;
    const char *resource;
    const char *capability;
    int supports_shared;
    int supports_read_heavy;
    int supports_unordered;
    int supports_parallelizable;
    int latency_score;
    int memory_score;
    const char *domain_contract;
    const char *flow_binding;
    size_t memory_fixed_bytes;
    size_t memory_bytes_per_capacity;
    int reload_capable;
};

struct FlowPlugin {
    const char *name;
    const char *version;
    const Component *components;
    size_t component_count;
    FlowPluginCompatibilityFn compatible;
    FlowPluginMemoryModelFn memory_model;
    FlowPluginVerifyFn verify;
    FlowPluginEmitFn emit;
    FlowPluginOracleFn oracle;
    FlowPluginPreferenceFn preference;
    /* Domain module hooks */
    FlowPluginValidateContractFn validate_contract;
    FlowPluginLowerSemanticsFn lower_domain_semantics;
    FlowPluginFreeSemanticsFn free_domain_semantics;
    FlowPluginEnumerateDimensionsFn enumerate_dimensions;
    FlowPluginEvaluatePlanFn evaluate_plan;
    FlowPluginVerifyPlanFn verify_plan;
    FlowPluginBenchmarkFn benchmark;
    FlowPluginMutationMaskFn get_mutation_mask;
    FlowPluginPreferenceMaskFn preference_mask;
    FlowPluginContractMaskFn contract_mask;
    FlowPluginResourceMaskFn resource_mask;
    FlowPluginEnvironmentMaskFn environment_mask;
    /* Runtime Unit Instantiation hook */
    int (*create_unit)(const struct FlowPlanArtifact *artifact,
                       const SemanticIR *ir,
                       const Component *component,
                       struct FlowUnit *unit_out);
    /* Declarative Knowledge Self-Synthesis Metadata (for Zero-Maintenance Flowy Introspection) */
    const char *doc_title;
    const char *doc_responsibilities;
    const char *doc_algorithmic_guarantee;
    const char *doc_memory_concurrency_model;
    const char *doc_key_apis;
    uint32_t doc_layer;
    void *domain_context;
};

#define FLOW_PLUGIN_ABI_MAJOR 1
#define FLOW_PLUGIN_ABI_MINOR 0

/* ========================================================================= */
/* Standardized FLOW Plugin ABI v2 (Canonical 4-Function Pure Contract)      */
/* ========================================================================= */

typedef struct FlowPluginABI {
    /* 1. Dimension Declaration: returns total bits required for domain genome */
    size_t (*get_genome_bit_size)(void);

    /* 2. Constraint Projection: pre-computes non-linear/physical polytope into 1-Bit Mask */
    uint64_t (*get_valid_mask)(const FlowEnvironmentState *env);

    /* 3. Energy Scoring: evaluates scalar domain energy for candidate genome */
    double (*evaluate_energy)(uint64_t genome);

    /* 4. Code Emission: lowers candidate genome into native LLVM IR / C AST */
    void (*emit_llvm_ir)(uint64_t genome, void *module_or_out);
} FlowPluginABI;

typedef const FlowPluginABI *(*FlowPluginABIv2EntryFn)(void);

typedef struct FlowPluginDescriptor {
    uint32_t abi_major;
    uint32_t abi_minor;
    size_t descriptor_size;
    const char *module_name;
    const char *module_version;
    uint64_t module_hash;
    const FlowPlugin *plugin;
    const FlowPluginABI *abi_v2;
    void *dso_handle;
    _Atomic uint64_t active_references;
} FlowPluginDescriptor;

typedef const FlowPluginDescriptor *(*FlowPluginEntryFn)(void);

/* ========================================================================= */
/* Declarative Plugin Contracts (Zero-C-Callback Specification)              */
/* ========================================================================= */

#define FLOW_CONTRACT_MAX_DIMENSIONS 16
#define FLOW_CONTRACT_MAX_COMPONENTS 8

typedef struct {
    char name[FLOW_DIM_NAME_MAX];
    FlowDimensionKind kind;
    FlowDimensionClass dim_class;
    uint64_t min_val;
    uint64_t max_val;
    uint64_t step;
    uint64_t default_val;
    uint64_t base_migration_cost_ns;
} FlowContractDimension;

typedef struct {
    char component_id[64];
    char kind[32];                     /* "collection", "algorithm", "engine" */
    int supports_shared;
    int supports_read_heavy;
    int supports_ordered;
    int supports_parallel;
    size_t memory_bytes_per_slot;
    size_t memory_fixed_overhead;
    double base_latency_weight;
    double base_throughput_weight;
    size_t dimension_count;
    FlowContractDimension dimensions[FLOW_CONTRACT_MAX_DIMENSIONS];
} FlowContractComponent;

typedef struct {
    const char *module_name;
    const char *module_version;
    const char *target_domain;          /* E.g. "finance", "telemetry", "ai_pipeline" */
    const char *target_contract;        /* E.g. "low_latency", "bounded_memory" */
    const char *doc_title;              /* Living doc title */
    const char *doc_responsibilities;   /* Subsystem responsibilities */
    const char *doc_algorithmic_guarantee; /* Complexity & invariants */
    const char *doc_memory_model;       /* Concurrency & memory layout */
    const char *doc_key_apis;           /* Authoritative APIs */
    size_t component_count;
    FlowContractComponent components[FLOW_CONTRACT_MAX_COMPONENTS];
} FlowPluginContract;

#endif
