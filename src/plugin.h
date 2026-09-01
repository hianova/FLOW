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
    /* Runtime Unit Instantiation hook */
    int (*create_unit)(const struct FlowPlanArtifact *artifact,
                       const SemanticIR *ir,
                       const Component *component,
                       struct FlowUnit *unit_out);
};

#define FLOW_PLUGIN_ABI_MAJOR 1
#define FLOW_PLUGIN_ABI_MINOR 0

typedef struct FlowPluginDescriptor {
    uint32_t abi_major;
    uint32_t abi_minor;
    size_t descriptor_size;
    const char *module_name;
    const char *module_version;
    uint64_t module_hash;
    const FlowPlugin *plugin;
    void *dso_handle;
    _Atomic uint64_t active_references;
} FlowPluginDescriptor;

typedef const FlowPluginDescriptor *(*FlowPluginEntryFn)(void);

#endif
