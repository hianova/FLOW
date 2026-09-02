#include "topology.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void flow_topology_init(FlowTopologyGraph *graph) {
    if (graph == NULL) return;
    memset(graph, 0, sizeof(*graph));
}

uint32_t flow_topology_add_node(FlowTopologyGraph *graph, FlowNodeType type,
                                const char *name, const char *module, int is_core, uint32_t layer) {
    if (graph == NULL || graph->node_count >= FLOW_TOPOLOGY_MAX_NODES) return 0;
    uint32_t id = (uint32_t)graph->node_count++;
    FlowTopologyNode *node = &graph->nodes[id];
    node->id = id;
    node->type = type;
    strncpy(node->name, name ? name : "", sizeof(node->name) - 1);
    strncpy(node->module, module ? module : "", sizeof(node->module) - 1);
    node->is_core = is_core;
    node->layer = layer;
    return id;
}

int flow_topology_add_edge(FlowTopologyGraph *graph, uint32_t from_id, uint32_t to_id,
                           FlowEdgeType type, double weight, const char *label) {
    if (graph == NULL || graph->edge_count >= FLOW_TOPOLOGY_MAX_EDGES) return 0;
    if (from_id >= graph->node_count || to_id >= graph->node_count) return 0;
    FlowTopologyEdge *edge = &graph->edges[graph->edge_count++];
    edge->from_id = from_id;
    edge->to_id = to_id;
    edge->type = type;
    edge->weight = weight;
    strncpy(edge->label, label ? label : "", sizeof(edge->label) - 1);
    return 1;
}

void flow_topology_build_codebase_graph(FlowTopologyGraph *graph) {
    if (graph == NULL) return;
    flow_topology_init(graph);

    /* Layer 0: Core Modules */
    uint32_t n_parser   = flow_topology_add_node(graph, FLOW_NODE_CORE_MODULE, "parser", "core", 1, 0);
    uint32_t n_semantic = flow_topology_add_node(graph, FLOW_NODE_CORE_MODULE, "semantic", "core", 1, 0);
    uint32_t n_bitspace = flow_topology_add_node(graph, FLOW_NODE_CORE_MODULE, "bitspace", "core", 1, 0);
    uint32_t n_search   = flow_topology_add_node(graph, FLOW_NODE_CORE_MODULE, "search", "core", 1, 0);
    uint32_t n_verifier = flow_topology_add_node(graph, FLOW_NODE_CORE_MODULE, "verifier", "core", 1, 0);
    uint32_t n_security = flow_topology_add_node(graph, FLOW_NODE_CORE_MODULE, "security", "core", 1, 0);
    uint32_t n_adaptive = flow_topology_add_node(graph, FLOW_NODE_CORE_MODULE, "adaptive", "core", 1, 0);
    uint32_t n_reload   = flow_topology_add_node(graph, FLOW_NODE_CORE_MODULE, "reload", "core", 1, 0);
    uint32_t n_jit      = flow_topology_add_node(graph, FLOW_NODE_CORE_MODULE, "jit", "core", 1, 0);
    uint32_t n_smt      = flow_topology_add_node(graph, FLOW_NODE_CORE_MODULE, "smt", "core", 1, 0);
    uint32_t n_backend  = flow_topology_add_node(graph, FLOW_NODE_CORE_MODULE, "backend", "core", 1, 0);
    uint32_t n_swarm        = flow_topology_add_node(graph, FLOW_NODE_CORE_MODULE, "swarm", "core", 1, 0);
    uint32_t n_genetic      = flow_topology_add_node(graph, FLOW_NODE_CORE_MODULE, "genetic", "core", 1, 0);
    uint32_t n_orchestrator = flow_topology_add_node(graph, FLOW_NODE_CORE_MODULE, "orchestrator", "core", 1, 0);
    uint32_t n_embodied     = flow_topology_add_node(graph, FLOW_NODE_CORE_MODULE, "embodied", "core", 1, 0);

    /* Layer 1: ABI & Registry Interface Boundary */
    uint32_t n_registry = flow_topology_add_node(graph, FLOW_NODE_CORE_MODULE, "registry", "interface", 1, 1);
    uint32_t n_abi      = flow_topology_add_node(graph, FLOW_NODE_CORE_MODULE, "abi", "interface", 1, 1);

    /* Layer 2: Domain Plugins & Components */
    uint32_t n_plugin_builtin = flow_topology_add_node(graph, FLOW_NODE_PLUGIN, "builtin_plugin", "plugin", 0, 2);
    uint32_t n_comp_linear    = flow_topology_add_node(graph, FLOW_NODE_COMPONENT, "linear_array", "plugin", 0, 2);
    uint32_t n_comp_sharded   = flow_topology_add_node(graph, FLOW_NODE_COMPONENT, "sharded_hash", "plugin", 0, 2);
    uint32_t n_comp_tree      = flow_topology_add_node(graph, FLOW_NODE_COMPONENT, "ordered_tree", "plugin", 0, 2);
    uint32_t n_comp_pmap      = flow_topology_add_node(graph, FLOW_NODE_COMPONENT, "parallel_map", "plugin", 0, 2);

    /* Internal Core Dependencies (Layer 0 -> Layer 0) */
    flow_topology_add_edge(graph, n_parser, n_semantic, FLOW_EDGE_DATA_FLOW, 1.0, "lower_to_ir");
    flow_topology_add_edge(graph, n_semantic, n_verifier, FLOW_EDGE_CALLS, 1.0, "verify_spec");
    flow_topology_add_edge(graph, n_semantic, n_security, FLOW_EDGE_CALLS, 1.0, "security_attestation");
    flow_topology_add_edge(graph, n_semantic, n_bitspace, FLOW_EDGE_CALLS, 1.0, "init_bitspace");
    flow_topology_add_edge(graph, n_bitspace, n_security, FLOW_EDGE_CALLS, 1.0, "safety_mask_compose");
    flow_topology_add_edge(graph, n_bitspace, n_verifier, FLOW_EDGE_CALLS, 1.0, "contract_resource_mask");
    flow_topology_add_edge(graph, n_bitspace, n_adaptive, FLOW_EDGE_CALLS, 1.0, "telemetry_bias");
    flow_topology_add_edge(graph, n_bitspace, n_search, FLOW_EDGE_USES, 1.0, "search_dimensions");
    flow_topology_add_edge(graph, n_search, n_verifier, FLOW_EDGE_CALLS, 1.0, "hard_gate_check");
    flow_topology_add_edge(graph, n_swarm, n_bitspace, FLOW_EDGE_USES, 1.0, "particle_entanglement");
    flow_topology_add_edge(graph, n_genetic, n_semantic, FLOW_EDGE_CALLS, 1.0, "ast_synthesis");
    flow_topology_add_edge(graph, n_adaptive, n_reload, FLOW_EDGE_CALLS, 1.0, "live_migration");
    flow_topology_add_edge(graph, n_adaptive, n_jit, FLOW_EDGE_CALLS, 1.0, "rejit_migration");
    flow_topology_add_edge(graph, n_semantic, n_smt, FLOW_EDGE_CALLS, 1.0, "generate_proof");
    flow_topology_add_edge(graph, n_backend, n_abi, FLOW_EDGE_USES, 1.0, "emit_abi_adapters");
    flow_topology_add_edge(graph, n_orchestrator, n_bitspace, FLOW_EDGE_CALLS, 1.0, "synthesize_topology");
    flow_topology_add_edge(graph, n_orchestrator, n_search, FLOW_EDGE_CALLS, 1.0, "global_anneal");
    flow_topology_add_edge(graph, n_orchestrator, n_smt, FLOW_EDGE_CALLS, 1.0, "invariant_attestation");
    flow_topology_add_edge(graph, n_embodied, n_bitspace, FLOW_EDGE_CALLS, 1.0, "physics_safety_mask");
    flow_topology_add_edge(graph, n_embodied, n_security, FLOW_EDGE_CALLS, 1.0, "sim_to_real_gate");

    /* Boundary Firewalls (Layer 0 -> Layer 1) */
    flow_topology_add_edge(graph, n_search, n_registry, FLOW_EDGE_CALLS, 1.0, "lookup_components");
    flow_topology_add_edge(graph, n_backend, n_registry, FLOW_EDGE_CALLS, 1.0, "query_emitters");

    /* Plugin Implementation Bindings (Layer 2 -> Layer 1) */
    flow_topology_add_edge(graph, n_plugin_builtin, n_registry, FLOW_EDGE_IMPLEMENTS, 1.0, "FlowPluginDescriptor");
    flow_topology_add_edge(graph, n_comp_linear, n_plugin_builtin, FLOW_EDGE_USES, 1.0, "declares_linear");
    flow_topology_add_edge(graph, n_comp_sharded, n_plugin_builtin, FLOW_EDGE_USES, 1.0, "declares_sharded");
    flow_topology_add_edge(graph, n_comp_tree, n_plugin_builtin, FLOW_EDGE_USES, 1.0, "declares_tree");
    flow_topology_add_edge(graph, n_comp_pmap, n_plugin_builtin, FLOW_EDGE_USES, 1.0, "declares_pmap");

    /* Layer 4: Doc-as-Topology Knowledge Nodes & Philosophy Bindings */
    uint32_t n_doc_ch01 = flow_topology_add_node(graph, FLOW_NODE_DOC_CHAPTER, "ch01_what_is_flow", "book", 0, 4);
    uint32_t n_doc_ch02 = flow_topology_add_node(graph, FLOW_NODE_DOC_CHAPTER, "ch02_intent_vs_implementation", "book", 0, 4);
    uint32_t n_doc_ch03 = flow_topology_add_node(graph, FLOW_NODE_DOC_CHAPTER, "ch03_hello_chaos", "book", 0, 4);
    uint32_t n_doc_ch04 = flow_topology_add_node(graph, FLOW_NODE_DOC_CHAPTER, "ch04_topology_graph", "book", 0, 4);
    uint32_t n_doc_ch05 = flow_topology_add_node(graph, FLOW_NODE_DOC_CHAPTER, "ch05_1bit_chaos_engine", "book", 0, 4);
    uint32_t n_doc_ch06 = flow_topology_add_node(graph, FLOW_NODE_DOC_CHAPTER, "ch06_smt_formal_verification", "book", 0, 4);
    uint32_t n_doc_ch07 = flow_topology_add_node(graph, FLOW_NODE_DOC_CHAPTER, "ch07_qsbr_lockfree_hotswap", "book", 0, 4);
    uint32_t n_doc_ch08 = flow_topology_add_node(graph, FLOW_NODE_DOC_CHAPTER, "ch08_memory_high_watermark_survival", "book", 0, 4);
    uint32_t n_doc_ch09 = flow_topology_add_node(graph, FLOW_NODE_DOC_CHAPTER, "ch09_geometric_morphing_aos_soa", "book", 0, 4);
    uint32_t n_doc_ch10 = flow_topology_add_node(graph, FLOW_NODE_DOC_CHAPTER, "ch10_meet_flowy", "book", 0, 4);
    uint32_t n_doc_ch11 = flow_topology_add_node(graph, FLOW_NODE_DOC_CHAPTER, "ch11_semantic_reasoning_sandbox", "book", 0, 4);
    uint32_t n_doc_ch12 = flow_topology_add_node(graph, FLOW_NODE_DOC_CHAPTER, "ch12_dynamic_plugins_abi", "book", 0, 4);
    uint32_t n_doc_ch13 = flow_topology_add_node(graph, FLOW_NODE_DOC_CHAPTER, "ch13_overcoming_epistasis", "book", 0, 4);
    uint32_t n_doc_ch14 = flow_topology_add_node(graph, FLOW_NODE_DOC_CHAPTER, "ch14_swarm_intelligence", "book", 0, 4);
    uint32_t n_doc_ch15 = flow_topology_add_node(graph, FLOW_NODE_DOC_CHAPTER, "ch15_embodied_physical_gates", "book", 0, 4);
    uint32_t n_doc_ch16 = flow_topology_add_node(graph, FLOW_NODE_DOC_CHAPTER, "ch16_level5_crucible_test", "book", 0, 4);
    uint32_t n_doc_ch17 = flow_topology_add_node(graph, FLOW_NODE_DOC_CHAPTER, "ch17_performance_benchmarks", "book", 0, 4);

    /* Bi-directional Knowledge Edges (Code -> Book Chapter: "Why") */
    flow_topology_add_edge(graph, n_parser, n_doc_ch02, FLOW_EDGE_DOCUMENTS, 1.0, "documents_why");
    flow_topology_add_edge(graph, n_semantic, n_doc_ch02, FLOW_EDGE_DOCUMENTS, 1.0, "documents_why");
    flow_topology_add_edge(graph, n_backend, n_doc_ch03, FLOW_EDGE_DOCUMENTS, 1.0, "documents_why");
    flow_topology_add_edge(graph, n_bitspace, n_doc_ch05, FLOW_EDGE_DOCUMENTS, 1.0, "documents_why");
    flow_topology_add_edge(graph, n_search, n_doc_ch05, FLOW_EDGE_DOCUMENTS, 1.0, "documents_why");
    flow_topology_add_edge(graph, n_smt, n_doc_ch06, FLOW_EDGE_DOCUMENTS, 1.0, "documents_why");
    flow_topology_add_edge(graph, n_verifier, n_doc_ch06, FLOW_EDGE_DOCUMENTS, 1.0, "documents_why");
    flow_topology_add_edge(graph, n_reload, n_doc_ch07, FLOW_EDGE_DOCUMENTS, 1.0, "documents_why");
    flow_topology_add_edge(graph, n_jit, n_doc_ch08, FLOW_EDGE_DOCUMENTS, 1.0, "documents_why");
    flow_topology_add_edge(graph, n_adaptive, n_doc_ch09, FLOW_EDGE_DOCUMENTS, 1.0, "documents_why");
    flow_topology_add_edge(graph, n_orchestrator, n_doc_ch11, FLOW_EDGE_DOCUMENTS, 1.0, "documents_why");
    flow_topology_add_edge(graph, n_registry, n_doc_ch12, FLOW_EDGE_DOCUMENTS, 1.0, "documents_why");
    flow_topology_add_edge(graph, n_abi, n_doc_ch12, FLOW_EDGE_DOCUMENTS, 1.0, "documents_why");
    flow_topology_add_edge(graph, n_security, n_doc_ch12, FLOW_EDGE_DOCUMENTS, 1.0, "documents_why");
    flow_topology_add_edge(graph, n_genetic, n_doc_ch13, FLOW_EDGE_DOCUMENTS, 1.0, "documents_why");
    flow_topology_add_edge(graph, n_swarm, n_doc_ch14, FLOW_EDGE_DOCUMENTS, 1.0, "documents_why");
    flow_topology_add_edge(graph, n_embodied, n_doc_ch15, FLOW_EDGE_DOCUMENTS, 1.0, "documents_why");
    flow_topology_add_edge(graph, n_comp_sharded, n_doc_ch16, FLOW_EDGE_DOCUMENTS, 1.0, "documents_why");
    (void)n_doc_ch01; (void)n_doc_ch04; (void)n_doc_ch10; (void)n_doc_ch17;
}

void flow_topology_build_intent_graph(FlowTopologyGraph *graph, const SemanticIR *ir,
                                      const Component *component, const FlowPlan *plan) {
    if (graph == NULL || ir == NULL) return;
    flow_topology_init(graph);

    /* 1. Add User Flow Intent Operations */
    uint32_t prev_op_id = 0;
    int has_prev = 0;
    for (size_t i = 0; i < ir->flow_node_count; ++i) {
        uint32_t op_id = flow_topology_add_node(graph, FLOW_NODE_INTENT_OP,
                                                ir->flow_nodes[i].name, "intent", 0, 3);
        if (has_prev) {
            /* High interaction weight between sequential pipeline stages */
            flow_topology_add_edge(graph, prev_op_id, op_id, FLOW_EDGE_DATA_FLOW,
                                   (double)(ir->input_max_count > 0 ? ir->input_max_count : 1000),
                                   "pipeline_stream");
            /* Shard locality affinity */
            flow_topology_add_edge(graph, prev_op_id, op_id, FLOW_EDGE_SHARD_AFFINITY, 0.95, "locality_affinity");
        }
        prev_op_id = op_id;
        has_prev = 1;
    }

    /* 2. Add Selected Component & Architectural Dimensions */
    if (component != NULL) {
        uint32_t comp_id = flow_topology_add_node(graph, FLOW_NODE_COMPONENT,
                                                  component->id, component->kind, 0, 2);
        if (has_prev) {
            flow_topology_add_edge(graph, prev_op_id, comp_id, FLOW_EDGE_USES, 1.0, "executes_on");
        }

        if (plan != NULL) {
            for (size_t d = 0; d < plan->dimension_set.count; ++d) {
                uint32_t dim_id = flow_topology_add_node(graph, FLOW_NODE_DIMENSION,
                                                         plan->dimension_set.dimensions[d].name,
                                                         "bitspace", 0, 1);
                flow_topology_add_edge(graph, comp_id, dim_id, FLOW_EDGE_BINDS_DIMENSION,
                                       (double)plan->assignment.values[d], "dimension_value");
            }
        }
    }
}

void flow_topology_audit(const FlowTopologyGraph *graph, FlowTopologyAuditReport *report) {
    if (report == NULL) return;
    memset(report, 0, sizeof(*report));
    if (graph == NULL) return;

    report->total_nodes = graph->node_count;
    report->total_edges = graph->edge_count;

    for (size_t i = 0; i < graph->node_count; ++i) {
        if (graph->nodes[i].layer == 0) report->core_nodes++;
        else if (graph->nodes[i].layer == 2) report->plugin_nodes++;
        else if (graph->nodes[i].layer == 3) report->intent_nodes++;
        else if (graph->nodes[i].layer == 4 || graph->nodes[i].type == FLOW_NODE_DOC_CHAPTER) report->doc_nodes++;
    }

    for (size_t e = 0; e < graph->edge_count; ++e) {
        if (graph->edges[e].type == FLOW_EDGE_DOCUMENTS) report->doc_edges++;
    }

    /* Audit Cross-Layer Leak Violations: Core (Layer 0) -> Plugin Impl (Layer 2) */
    for (size_t e = 0; e < graph->edge_count; ++e) {
        const FlowTopologyEdge *edge = &graph->edges[e];
        const FlowTopologyNode *from = &graph->nodes[edge->from_id];
        const FlowTopologyNode *to = &graph->nodes[edge->to_id];

        if (from->layer == 0 && to->layer == 2 && edge->type == FLOW_EDGE_CALLS) {
            report->cross_layer_leaks++;
            if (strlen(report->leak_details) < 400) {
                char buf[128];
                snprintf(buf, sizeof(buf), "[LEAK] %s -> %s (%s); ", from->name, to->name, edge->label);
                strncat(report->leak_details, buf, sizeof(report->leak_details) - strlen(report->leak_details) - 1);
            }
        }
    }

    report->average_coupling = graph->node_count > 0 ? (double)graph->edge_count / (double)graph->node_count : 0.0;
    report->modularity_score = graph->edge_count > 0 ? 1.0 - ((double)report->cross_layer_leaks / (double)graph->edge_count) : 1.0;
}

double flow_topology_compute_affinity(const FlowTopologyGraph *graph, uint32_t node_a, uint32_t node_b) {
    if (graph == NULL || node_a >= graph->node_count || node_b >= graph->node_count) return 0.0;
    if (node_a == node_b) return 1.0;

    for (size_t e = 0; e < graph->edge_count; ++e) {
        const FlowTopologyEdge *edge = &graph->edges[e];
        if ((edge->from_id == node_a && edge->to_id == node_b) ||
            (edge->from_id == node_b && edge->to_id == node_a)) {
            if (edge->type == FLOW_EDGE_SHARD_AFFINITY || edge->type == FLOW_EDGE_DATA_FLOW) {
                return edge->weight > 1.0 ? 1.0 : edge->weight;
            }
        }
    }
    return 0.1;
}

static const char *flow_node_type_name(FlowNodeType type) {
    switch (type) {
        case FLOW_NODE_CORE_MODULE: return "CoreModule";
        case FLOW_NODE_PLUGIN: return "Plugin";
        case FLOW_NODE_COMPONENT: return "Component";
        case FLOW_NODE_INTENT_OP: return "IntentOp";
        case FLOW_NODE_DIMENSION: return "Dimension";
        case FLOW_NODE_SHARD_GROUP: return "ShardGroup";
        case FLOW_NODE_DOC_CHAPTER: return "DocChapter";
        default: return "Unknown";
    }
}

static const char *flow_edge_type_name(FlowEdgeType type) {
    switch (type) {
        case FLOW_EDGE_CALLS: return "CALLS";
        case FLOW_EDGE_USES: return "USES";
        case FLOW_EDGE_IMPLEMENTS: return "IMPLEMENTS";
        case FLOW_EDGE_BINDS_DIMENSION: return "BINDS_DIMENSION";
        case FLOW_EDGE_DATA_FLOW: return "DATA_FLOW";
        case FLOW_EDGE_SHARD_AFFINITY: return "SHARD_AFFINITY";
        case FLOW_EDGE_DOCUMENTS: return "DOCUMENTS";
        default: return "RELATED_TO";
    }
}

int flow_topology_export_json(const FlowTopologyGraph *graph, FILE *output) {
    if (graph == NULL || output == NULL) return 0;
    fprintf(output, "{\n  \"nodes\": [\n");
    for (size_t i = 0; i < graph->node_count; ++i) {
        const FlowTopologyNode *n = &graph->nodes[i];
        fprintf(output, "    {\"id\": %u, \"name\": \"%s\", \"type\": \"%s\", \"module\": \"%s\", \"layer\": %u, \"is_core\": %s}%s\n",
                n->id, n->name, flow_node_type_name(n->type), n->module, n->layer,
                n->is_core ? "true" : "false",
                i + 1 < graph->node_count ? "," : "");
    }
    fprintf(output, "  ],\n  \"edges\": [\n");
    for (size_t e = 0; e < graph->edge_count; ++e) {
        const FlowTopologyEdge *edge = &graph->edges[e];
        fprintf(output, "    {\"source\": %u, \"target\": %u, \"type\": \"%s\", \"weight\": %.2f, \"label\": \"%s\"}%s\n",
                edge->from_id, edge->to_id, flow_edge_type_name(edge->type),
                edge->weight, edge->label,
                e + 1 < graph->edge_count ? "," : "");
    }
    fprintf(output, "  ]\n}\n");
    return ferror(output) == 0;
}

int flow_topology_export_dot(const FlowTopologyGraph *graph, FILE *output) {
    if (graph == NULL || output == NULL) return 0;
    fprintf(output, "digraph FlowTopology {\n");
    fprintf(output, "  rankdir=LR;\n");
    fprintf(output, "  node [fontname=\"Helvetica\", fontsize=10, shape=box, style=filled];\n");
    fprintf(output, "  edge [fontname=\"Helvetica\", fontsize=8];\n\n");

    for (size_t i = 0; i < graph->node_count; ++i) {
        const FlowTopologyNode *n = &graph->nodes[i];
        const char *color = n->layer == 0 ? "#E0F2FE" :
                            n->layer == 1 ? "#FEF3C7" :
                            n->layer == 2 ? "#DCFCE7" : "#F3E8FF";
        fprintf(output, "  node_%u [label=\"%s\\n(%s)\", fillcolor=\"%s\"];\n",
                n->id, n->name, flow_node_type_name(n->type), color);
    }
    fprintf(output, "\n");

    for (size_t e = 0; e < graph->edge_count; ++e) {
        const FlowTopologyEdge *edge = &graph->edges[e];
        fprintf(output, "  node_%u -> node_%u [label=\"%s\"];\n",
                edge->from_id, edge->to_id, edge->label[0] ? edge->label : flow_edge_type_name(edge->type));
    }
    fprintf(output, "}\n");
    return ferror(output) == 0;
}

int flow_topology_attach_telemetry(FlowTopologyGraph *graph, const char *node_name,
                                  double hotspot_score, const char *metric_name,
                                  double raw_val, double thresh_val,
                                  const char *unit, const char *symptom) {
    if (graph == NULL || node_name == NULL) return 0;
    for (size_t i = 0; i < graph->node_count; ++i) {
        FlowTopologyNode *n = &graph->nodes[i];
        if (strcmp(n->name, node_name) == 0 || strcmp(n->module, node_name) == 0) {
            n->hotspot_score = hotspot_score;
            if (metric_name) strncpy(n->hotspot_metric, metric_name, sizeof(n->hotspot_metric) - 1);
            n->hotspot_raw_val = raw_val;
            n->hotspot_threshold_val = thresh_val;
            if (unit) strncpy(n->hotspot_unit, unit, sizeof(n->hotspot_unit) - 1);
            if (symptom) strncpy(n->dynamic_symptom, symptom, sizeof(n->dynamic_symptom) - 1);
            return 1;
        }
    }
    return 0;
}

const FlowTopologyNode *flow_topology_get_peak_hotspot(const FlowTopologyGraph *graph) {
    if (graph == NULL || graph->node_count == 0) return NULL;
    const FlowTopologyNode *peak = NULL;
    double max_score = -1.0;
    for (size_t i = 0; i < graph->node_count; ++i) {
        const FlowTopologyNode *n = &graph->nodes[i];
        if (n->hotspot_score > max_score) {
            max_score = n->hotspot_score;
            peak = n;
        }
    }
    return peak;
}

