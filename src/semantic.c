#include "flow.h"

#include <string.h>

static int has_flow_node(const SemanticIR *ir, const char *name) {
    size_t i;
    for (i = 0; i < ir->flow_node_count; ++i)
        if (strcmp(ir->flow_nodes[i].name, name) == 0) return 1;
    return 0;
}

static void add_fact(SemanticIR *ir, FlowFactKind kind, const char *detail) {
    if (ir->fact_count >= FLOW_NODE_MAX) return;
    ir->facts[ir->fact_count].kind = kind;
    strncpy(ir->facts[ir->fact_count].detail, detail,
            sizeof(ir->facts[ir->fact_count].detail) - 1);
    ir->facts[ir->fact_count].detail[
        sizeof(ir->facts[ir->fact_count].detail) - 1] = '\0';
    ++ir->fact_count;
    if (kind == FLOW_FACT_HOLE) ++ir->hole_count;
    else if (kind == FLOW_FACT_CONSTRAINT) ++ir->constraint_count;
}

void lower_to_ir(const FlowSpec *spec, SemanticIR *ir) {
    memset(ir, 0, sizeof(*ir));
    strncpy(ir->input_name, spec->input_name, sizeof(ir->input_name) - 1);
    memcpy(ir->samples, spec->samples, sizeof(ir->samples));
    ir->sample_count = spec->sample_count;
    strncpy(ir->output_name, spec->output_name, sizeof(ir->output_name) - 1);
    strncpy(ir->output_type, spec->output_type, sizeof(ir->output_type) - 1);
    strncpy(ir->state_name, spec->state_name, sizeof(ir->state_name) - 1);
    strncpy(ir->flow_name, spec->flow_name, sizeof(ir->flow_name) - 1);
    strncpy(ir->flow_expression, spec->flow_expression, sizeof(ir->flow_expression) - 1);
    memcpy(ir->flow_nodes, spec->flow_nodes, sizeof(ir->flow_nodes));
    ir->flow_node_count = spec->flow_node_count;
    ir->input_max_count = spec->max_count;
    ir->state_shared = spec->shared;
    ir->state_read_heavy = spec->read_heavy;
    ir->state_bounded = spec->bounded;
    ir->top_n = spec->top_n;
    ir->fact_ordered = has_flow_node(ir, "sort");
    ir->fact_unordered = !ir->fact_ordered;
    ir->fact_deterministic = spec->deterministic;
    ir->fact_range_proven = ir->input_max_count > 0;
    /*
     * These are structural hints from the small FLOW vocabulary, not proofs
     * about arbitrary user code. Keep the inference tied to parsed nodes so
     * an unrelated identifier containing "sort" or "transform" cannot alter
     * the optimization contract.
     */
    ir->fact_size_preserved = has_flow_node(ir, "transform") &&
                              has_flow_node(ir, "collect");
    ir->fact_mutability_read_only = !ir->state_shared &&
                                    !has_flow_node(ir, "transition") &&
                                    strcmp(ir->flow_name, "state_machine") != 0;
    ir->flow_parallelizable = has_flow_node(ir, "transform") ||
                              has_flow_node(ir, "parallel");
    ir->memory_limit_mb = spec->memory_mb;
    ir->prefer_latency = spec->prefer_latency;
    ir->ensure_count = spec->ensure_count;
    memcpy(ir->constraints, spec->constraints, sizeof(ir->constraints));
    ir->declared_constraint_count = spec->constraint_count;
    strncpy(ir->resource_name, spec->resource_name, sizeof(ir->resource_name) - 1);
    strncpy(ir->capability_name, spec->capability_name, sizeof(ir->capability_name) - 1);
    strncpy(ir->domain_name, spec->domain_name, sizeof(ir->domain_name) - 1);
    strncpy(ir->contract_name, spec->contract_name, sizeof(ir->contract_name) - 1);
    strncpy(ir->fallback_policy, spec->fallback_policy, sizeof(ir->fallback_policy) - 1);
    strncpy(ir->plugin_name, spec->plugin_name, sizeof(ir->plugin_name) - 1);
    strncpy(ir->project_name, spec->project_name, sizeof(ir->project_name) - 1);
    memcpy(ir->imported_modules, spec->imported_modules, sizeof(ir->imported_modules));
    ir->imported_module_count = spec->imported_module_count;
    if (ir->state_shared) add_fact(ir, FLOW_FACT_SHARED_STATE, "shared");
    if (ir->state_bounded) add_fact(ir, FLOW_FACT_BOUNDED, "bounded");
    if (ir->flow_parallelizable) add_fact(ir, FLOW_FACT_PARALLELIZABLE, "parallelizable");
    if (ir->fact_ordered) add_fact(ir, FLOW_FACT_ORDERED, "ordered");
    if (ir->flow_node_count > 0) add_fact(ir, FLOW_FACT_COLLECTION, "pipeline");
    for (size_t i = 0; i < ir->flow_node_count; ++i) {
        if (strcmp(ir->flow_nodes[i].name, "transform") == 0)
            add_fact(ir, FLOW_FACT_TRANSFORM, "transform");
        if (strchr(ir->flow_nodes[i].name, '?') != NULL)
            add_fact(ir, FLOW_FACT_HOLE, ir->flow_nodes[i].name);
    }
    if (ir->capability_name[0] != '\0') add_fact(ir, FLOW_FACT_CAPABILITY, ir->capability_name);
    if (ir->memory_limit_mb > 0 || ir->fact_deterministic)
        add_fact(ir, FLOW_FACT_CONSTRAINT, "require");
    if (ir->fact_range_proven) add_fact(ir, FLOW_FACT_RANGE, "input_max_count");
    if (ir->fact_size_preserved) add_fact(ir, FLOW_FACT_SIZE, "length_preserved");
    if (ir->fact_mutability_read_only) add_fact(ir, FLOW_FACT_MUTABILITY, "read_only");
    if (ir->fact_deterministic) add_fact(ir, FLOW_FACT_DETERMINISM, "deterministic");
    ir->hole_count += strstr(ir->flow_expression, "?") != NULL && ir->hole_count == 0;
}
