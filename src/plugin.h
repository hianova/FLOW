#ifndef FLOW_PLUGIN_H
#define FLOW_PLUGIN_H
#include "flow.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h>
#define FLOW_DIMENSION_MAX 16
#define FLOW_DIM_NAME_MAX 32
typedef struct FlowComponent Component; typedef struct FlowPlugin FlowPlugin; struct FlowSearchResult; struct FlowPlanArtifact; struct FlowUnit;
typedef enum { FLOW_DIM_EXPONENT, FLOW_DIM_LINEAR, FLOW_DIM_DISCRETE, FLOW_DIM_BOOLEAN } FlowDimensionKind;
typedef enum { FLOW_DIM_CLASS_TACTILE_PARAM = 0, FLOW_DIM_CLASS_STRUCTURAL_JIT = 1 } FlowDimensionClass; typedef struct { char name[FLOW_DIM_NAME_MAX]; FlowDimensionKind kind;
    FlowDimensionClass dim_class; uint64_t min_val, max_val, step, default_val, base_migration_cost_ns; } FlowPlanDimension; typedef struct { size_t count;
    FlowPlanDimension dimensions[FLOW_DIMENSION_MAX]; } FlowPlanDimensionSet; static inline unsigned flow_dimension_bits(const FlowPlanDimension *dim) { if (!dim) return 0;
    if (dim->kind == FLOW_DIM_BOOLEAN) return 1; uint64_t step = (dim->kind == FLOW_DIM_EXPONENT || dim->step == 0) ? 1 : dim->step;
    uint64_t span = dim->max_val >= dim->min_val ? (dim->max_val - dim->min_val) / step : 0; unsigned b = 1; while (((uint64_t)1 << b) <= span && b < 64) ++b; return b; }
typedef struct { size_t count; uint64_t values[FLOW_DIMENSION_MAX]; } FlowPlanAssignment;
typedef struct { size_t capacity, threads, shards, memory_bytes; double latency_score, throughput_score, energy; } FlowPlanMetrics;
typedef enum { VERIFIER_PROVEN, VERIFIER_RUNTIME_CHECK, VERIFIER_COMPILE_ERROR } VerificationStatus; typedef struct FlowVerificationReport { VerificationStatus status;
    size_t capacity, estimated_bytes; int max_count_proven, runtime_input_guard; char message[128]; } VerificationReport;
typedef int (*FlowPluginValidateContractFn)(const SemanticIR *ir, const FlowPlugin *plugin, char *message, size_t message_size);
typedef void (*FlowPluginLowerSemanticsFn)(const FlowSpec *spec, SemanticIR *ir, const FlowPlugin *plugin);
typedef int (*FlowPluginEnumerateDimensionsFn)(const SemanticIR *ir, const Component *component, FlowPlanDimensionSet *dims_out);
typedef int (*FlowPluginEvaluatePlanFn)(const SemanticIR *ir, const Component *component, const FlowPlanAssignment *plan, FlowPlanMetrics *metrics_out);
typedef int (*FlowPluginVerifyPlanFn)(const SemanticIR *ir, const Component *component, const FlowPlanAssignment *plan, VerificationReport *report_out);
typedef int (*FlowPluginCompatibilityFn)(const SemanticIR *ir, const Component *component);
typedef int (*FlowPluginMemoryModelFn)(const SemanticIR *ir, const Component *component, size_t capacity, size_t shards, size_t *estimated_bytes);
typedef int (*FlowPluginVerifyFn)(const SemanticIR *ir, const Component *component, size_t capacity, size_t shards, char *message, size_t message_size);
typedef int (*FlowPluginEmitFn)(FILE *output, const SemanticIR *ir, const Component *component, const struct FlowSearchResult *search, const struct FlowVerificationReport *verification, int reload_adapter);
typedef int (*FlowPluginOracleFn)(const char *fixture_path, char *message, size_t message_size);
typedef int (*FlowPluginPreferenceFn)(const SemanticIR *ir, const Component *component);
typedef uint64_t (*FlowPluginBenchmarkFn)(const SemanticIR *ir, const Component *component, const FlowPlanAssignment *plan);
typedef void (*FlowPluginFreeSemanticsFn)(void *domain_ctx);
typedef uint64_t (*FlowPluginMutationMaskFn)(const SemanticIR *ir, const Component *component, const FlowPlanDimensionSet *dims);
typedef uint64_t (*FlowPluginPreferenceMaskFn)(const SemanticIR *ir, const Component *component, const FlowPlanDimensionSet *dims);
typedef uint64_t (*FlowPluginContractMaskFn)(const SemanticIR *ir, const Component *component, const FlowPlanDimensionSet *dims);
typedef uint64_t (*FlowPluginResourceMaskFn)(const SemanticIR *ir, const Component *component, const FlowPlanDimensionSet *dims, size_t memory_limit_bytes);
typedef enum { FLOW_ENV_PRESSURE_NONE = 0, FLOW_ENV_PRESSURE_MEMORY_MODERATE, FLOW_ENV_PRESSURE_MEMORY_CRITICAL, FLOW_ENV_PRESSURE_CACHE_THRASHING, FLOW_ENV_PRESSURE_LATENCY_SPIKE, FLOW_ENV_PRESSURE_BATTERY_SAVER } FlowEnvPressureLevel;
typedef enum { FLOW_ARCH_GENERIC = 0, FLOW_ARCH_APPLE_SILICON, FLOW_ARCH_INTEL_AVX2, FLOW_ARCH_INTEL_AVX512, FLOW_ARCH_ARM_NEON, FLOW_ARCH_RISCV_VECTOR } FlowHardwareArch;
typedef struct { FlowEnvPressureLevel pressure_level; FlowHardwareArch hardware_arch; size_t available_ram_bytes, active_concurrent_tabs, l2_cache_bytes, l3_cache_bytes;
    double measured_miss_rate, measured_ipc; } FlowEnvironmentState;
typedef uint64_t (*FlowPluginEnvironmentMaskFn)(const SemanticIR *ir, const Component *component, const FlowPlanDimensionSet *dims, const FlowEnvironmentState *env);
struct FlowComponent { const char *id, *kind, *resource, *capability; int supports_shared, supports_read_heavy, supports_unordered, supports_parallelizable;
    int latency_score, memory_score; const char *domain_contract, *flow_binding; size_t memory_fixed_bytes, memory_bytes_per_capacity; int reload_capable;
    uint64_t cubical_type_mask; }; struct FlowPlugin { const char *name, *version; const Component *components; size_t component_count; FlowPluginCompatibilityFn compatible;
    FlowPluginMemoryModelFn memory_model; FlowPluginVerifyFn verify; FlowPluginEmitFn emit; FlowPluginOracleFn oracle; FlowPluginPreferenceFn preference;
    FlowPluginValidateContractFn validate_contract; FlowPluginLowerSemanticsFn lower_domain_semantics; FlowPluginFreeSemanticsFn free_domain_semantics;
    FlowPluginEnumerateDimensionsFn enumerate_dimensions; FlowPluginEvaluatePlanFn evaluate_plan; FlowPluginVerifyPlanFn verify_plan; FlowPluginBenchmarkFn benchmark;
    FlowPluginMutationMaskFn get_mutation_mask; FlowPluginPreferenceMaskFn preference_mask; FlowPluginContractMaskFn contract_mask; FlowPluginResourceMaskFn resource_mask;
    FlowPluginEnvironmentMaskFn environment_mask;
    int (*create_unit)(const struct FlowPlanArtifact *artifact, const SemanticIR *ir, const Component *component, struct FlowUnit *unit_out);
    const char *doc_title, *doc_responsibilities, *doc_algorithmic_guarantee, *doc_memory_concurrency_model, *doc_key_apis; uint32_t doc_layer; void *domain_context; };
#define FLOW_PLUGIN_ABI_MAJOR 1
#define FLOW_PLUGIN_ABI_MINOR 0
typedef struct FlowPluginABI { size_t (*get_genome_bit_size)(void); uint64_t (*get_valid_mask)(const FlowEnvironmentState *env); double (*evaluate_energy)(uint64_t genome);
    void (*emit_llvm_ir)(uint64_t genome, void *module_or_out); } FlowPluginABI; typedef const FlowPluginABI *(*FlowPluginABIv2EntryFn)(void);
typedef struct FlowPluginDescriptor { uint32_t abi_major, abi_minor; size_t descriptor_size; const char *module_name, *module_version; uint64_t module_hash;
    const FlowPlugin *plugin; const FlowPluginABI *abi_v2; void *dso_handle; _Atomic uint64_t active_references; } FlowPluginDescriptor;
typedef const FlowPluginDescriptor *(*FlowPluginEntryFn)(void);
#define FLOW_CONTRACT_MAX_DIMENSIONS 16
#define FLOW_CONTRACT_MAX_COMPONENTS 8
typedef FlowPlanDimension FlowContractDimension; typedef struct { char component_id[64], kind[32];
    int supports_shared, supports_read_heavy, supports_ordered, supports_parallel; size_t memory_bytes_per_slot, memory_fixed_overhead;
    double base_latency_weight, base_throughput_weight; size_t dimension_count; FlowContractDimension dimensions[FLOW_CONTRACT_MAX_DIMENSIONS]; } FlowContractComponent;
typedef struct { const char *module_name, *module_version, *target_domain, *target_contract;
    const char *doc_title, *doc_responsibilities, *doc_algorithmic_guarantee, *doc_memory_model, *doc_key_apis; size_t component_count;
    FlowContractComponent components[FLOW_CONTRACT_MAX_COMPONENTS]; } FlowPluginContract;
#endif
