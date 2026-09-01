#ifndef FLOW_H
#define FLOW_H

#include <stddef.h>
#include <stdio.h>

#define FLOW_LINE 512
#define FLOW_NAME 64
#define FLOW_NODE_MAX 32
#define FLOW_SAMPLE_MAX 32
#define FLOW_CONSTRAINT_MAX 32

typedef struct {
    char name[FLOW_NAME];
} FlowNode;

typedef struct {
    int id;
    int score;
} FlowSample;

typedef enum {
    FLOW_FACT_SHARED_STATE,
    FLOW_FACT_COLLECTION,
    FLOW_FACT_TRANSFORM,
    FLOW_FACT_PARALLELIZABLE,
    FLOW_FACT_ORDERED,
    FLOW_FACT_BOUNDED,
    FLOW_FACT_CAPABILITY,
    FLOW_FACT_CONSTRAINT,
    FLOW_FACT_HOLE,
    FLOW_FACT_RANGE,
    FLOW_FACT_SIZE,
    FLOW_FACT_MUTABILITY,
    FLOW_FACT_DETERMINISM
} FlowFactKind;

typedef struct {
    FlowFactKind kind;
    char detail[FLOW_NAME];
} FlowFact;

typedef struct {
    char name[FLOW_NAME];
    char operator[FLOW_NAME];
    char value[FLOW_NAME];
    char expression[FLOW_LINE];
} FlowConstraint;

typedef struct {
    char input_name[FLOW_NAME];
    int max_count;
    FlowSample samples[FLOW_SAMPLE_MAX];
    size_t sample_count;
    char output_name[FLOW_NAME];
    char output_type[FLOW_NAME];
    char state_name[FLOW_NAME];
    int shared;
    int read_heavy;
    int bounded;
    char flow_name[FLOW_NAME];
    char flow_expression[FLOW_LINE];
    FlowNode flow_nodes[FLOW_NODE_MAX];
    size_t flow_node_count;
    int top_n;
    int deterministic;
    int memory_mb;
    int prefer_latency;
    int ensure_count;
    FlowConstraint constraints[FLOW_CONSTRAINT_MAX];
    size_t constraint_count;
    char resource_name[FLOW_NAME];
    char capability_name[FLOW_NAME];
    char domain_name[FLOW_NAME];
    char contract_name[FLOW_NAME];
    char fallback_policy[FLOW_NAME];
    char plugin_name[FLOW_NAME];
    char project_name[FLOW_NAME];
    char imported_modules[8][FLOW_NAME];
    size_t imported_module_count;
} FlowSpec;

typedef struct SemanticIR {
    char input_name[FLOW_NAME];
    int input_max_count;
    FlowSample samples[FLOW_SAMPLE_MAX];
    size_t sample_count;
    char output_name[FLOW_NAME];
    char output_type[FLOW_NAME];
    char state_name[FLOW_NAME];
    int state_shared;
    int state_read_heavy;
    int state_bounded;
    char flow_name[FLOW_NAME];
    char flow_expression[FLOW_LINE];
    FlowNode flow_nodes[FLOW_NODE_MAX];
    size_t flow_node_count;
    int flow_parallelizable;
    int fact_ordered;
    int fact_range_proven;
    int fact_size_preserved;
    int fact_mutability_read_only;
    int top_n;
    int fact_unordered;
    int fact_deterministic;
    int memory_limit_mb;
    int prefer_latency;
    int ensure_count;
    FlowConstraint constraints[FLOW_CONSTRAINT_MAX];
    size_t declared_constraint_count;
    char resource_name[FLOW_NAME];
    char capability_name[FLOW_NAME];
    char domain_name[FLOW_NAME];
    char contract_name[FLOW_NAME];
    char fallback_policy[FLOW_NAME];
    char plugin_name[FLOW_NAME];
    char project_name[FLOW_NAME];
    char imported_modules[8][FLOW_NAME];
    size_t imported_module_count;
    size_t workload_bytes;
    FlowFact facts[FLOW_NODE_MAX];
    size_t fact_count;
    size_t constraint_count;
    size_t hole_count;
    void *domain_ctx;
    void (*domain_ctx_free)(void *ctx);
} SemanticIR;

int parse_spec(FILE *input, FlowSpec *spec);
void lower_to_ir(const FlowSpec *spec, SemanticIR *ir);

#endif
