#ifndef FLOW_H
#define FLOW_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#define FLOW_LINE 512
#define FLOW_NAME 64
#define FLOW_NODE_MAX 32
#define FLOW_SAMPLE_MAX 32
#define FLOW_CONSTRAINT_MAX 32
#define FLOW_FIELD_MAX 16
#define FLOW_LEAN_FN_MAX 64
#define FLOW_LEAN_PARAM_MAX 8
#define FLOW_LEAN_THEOREM_MAX 32
#define FLOW_LEAN_BODY_MAX 768
typedef struct { char name[FLOW_NAME]; } FlowNode; typedef struct { int id; int score; } FlowSample;
typedef struct { char name[FLOW_NAME]; size_t size_bytes; uint32_t flags; } FlowFieldSpec;
typedef struct { char name[FLOW_NAME]; char return_type[16]; size_t param_count; char param_names[FLOW_LEAN_PARAM_MAX][FLOW_NAME]; char param_types[FLOW_LEAN_PARAM_MAX][16]; char body[FLOW_LEAN_BODY_MAX]; } FlowLeanFunction;
typedef struct { char name[FLOW_NAME]; char anchored_fn[FLOW_NAME]; } FlowLeanTheorem;
typedef enum { FLOW_FACT_SHARED_STATE, FLOW_FACT_COLLECTION,
    FLOW_FACT_TRANSFORM, FLOW_FACT_PARALLELIZABLE, FLOW_FACT_ORDERED, FLOW_FACT_BOUNDED, FLOW_FACT_CAPABILITY, FLOW_FACT_CONSTRAINT, FLOW_FACT_HOLE, FLOW_FACT_RANGE,
    FLOW_FACT_SIZE, FLOW_FACT_MUTABILITY, FLOW_FACT_DETERMINISM, FLOW_FACT_LEAN_CERTIFIED
} FlowFactKind; typedef struct { FlowFactKind kind; char detail[FLOW_NAME]; } FlowFact; typedef struct { char name[FLOW_NAME]; char operator[FLOW_NAME]; char value[FLOW_NAME];
    char expression[FLOW_LINE]; } FlowConstraint; typedef struct { int enabled; char name[FLOW_NAME]; double frequency_hz, max_drift, zmp_margin, dt; int symplectic;
    size_t dim; } FlowJetSpec; typedef struct { int enabled; char name[FLOW_NAME]; double max_temp_c, target_temp_c, power_limit_w, r_thermal, c_thermal; } FlowThermalSpec;
#define FLOW_SPEC_CORE_FIELDS \
    char input_name[FLOW_NAME]; \
    union { int max_count; int input_max_count; }; \
    FlowSample samples[FLOW_SAMPLE_MAX]; \
    size_t sample_count; \
    char output_name[FLOW_NAME]; \
    char output_type[FLOW_NAME]; \
    char state_name[FLOW_NAME]; \
    union { int shared; int state_shared; }; \
    union { int read_heavy; int state_read_heavy; }; \
    union { int bounded; int state_bounded; }; \
    char flow_name[FLOW_NAME]; \
    char flow_expression[FLOW_LINE]; \
    FlowNode flow_nodes[FLOW_NODE_MAX]; \
    size_t flow_node_count; \
    int top_n; \
    union { int deterministic; int fact_deterministic; }; \
    union { int lean_certified; int fact_lean_certified; }; \
    union { int memory_mb; int memory_limit_mb; }; \
    int prefer_latency; \
    int ensure_count; \
    FlowConstraint constraints[FLOW_CONSTRAINT_MAX]; \
    size_t declared_constraint_count; \
    char resource_name[FLOW_NAME]; \
    char capability_name[FLOW_NAME]; \
    char domain_name[FLOW_NAME]; \
    char contract_name[FLOW_NAME]; \
    char fallback_policy[FLOW_NAME]; \
    char plugin_name[FLOW_NAME]; \
    char project_name[FLOW_NAME]; \
    char imported_modules[8][FLOW_NAME]; \
    size_t imported_module_count; \
    size_t input_stride_bytes; \
    size_t input_align_bytes; \
    uint32_t state_schema_version; \
    FlowFieldSpec state_fields[FLOW_FIELD_MAX]; \
    size_t state_field_count; \
    uint64_t sla_latency_ns; \
    char extern_kernel[FLOW_NAME]; \
    char extern_kernel_body[FLOW_LINE]; \
    FlowLeanFunction lean_functions[FLOW_LEAN_FN_MAX]; \
    size_t lean_function_count; \
    FlowLeanTheorem lean_theorems[FLOW_LEAN_THEOREM_MAX]; \
    size_t lean_theorem_count; \
    FlowJetSpec jet; \
    FlowThermalSpec thermal;
typedef struct { FLOW_SPEC_CORE_FIELDS
    size_t constraint_count; } FlowSpec; typedef struct SemanticIR { FLOW_SPEC_CORE_FIELDS
    int flow_parallelizable; int fact_ordered; int fact_range_proven; int fact_size_preserved; int fact_mutability_read_only; int fact_unordered; size_t workload_bytes;
    FlowFact facts[FLOW_NODE_MAX]; size_t fact_count; size_t constraint_count; size_t hole_count; void *domain_ctx; void (*domain_ctx_free)(void *ctx); } SemanticIR;
int parse_spec(FILE *input, FlowSpec *spec); void lower_to_ir(const FlowSpec *spec, SemanticIR *ir);
#endif
