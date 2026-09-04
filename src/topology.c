#include "topology.h"
#include "flowy_fvec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void flow_topology_init(FlowTopologyGraph *g) {
    if (g) memset(g, 0, sizeof(*g));
}

uint32_t flow_topology_add_node(FlowTopologyGraph *g, FlowNodeType t, const char *n, const char *m, int c, uint32_t l) {
    if (!g || g->node_count >= FLOW_TOPOLOGY_MAX_NODES) return 0;
    uint32_t id = (uint32_t)g->node_count++;
    FlowTopologyNode *node = &g->nodes[id];
    *node = (FlowTopologyNode){.id = id, .type = t, .is_core = c, .layer = l};
    strncpy(node->name, n ? n : "", sizeof(node->name) - 1);
    strncpy(node->module, m ? m : "", sizeof(node->module) - 1);
    return id;
}

int flow_topology_add_edge(FlowTopologyGraph *g, uint32_t f, uint32_t to, FlowEdgeType t, double w, const char *lbl) {
    if (!g || g->edge_count >= FLOW_TOPOLOGY_MAX_EDGES || f >= g->node_count || to >= g->node_count) return 0;
    FlowTopologyEdge *e = &g->edges[g->edge_count++];
    *e = (FlowTopologyEdge){.from_id = f, .to_id = to, .type = t, .weight = w};
    strncpy(e->label, lbl ? lbl : "", sizeof(e->label) - 1);
    return 1;
}

enum {
    N_PARSER = 0, N_BITSPACE, N_SEARCH, N_SECURITY, N_ADAPTIVE, N_RELOAD, N_JIT, N_SMT,
    N_BACKEND, N_SWARM, N_ORCHESTRATOR, N_EMBODIED, N_AUDIT, N_FLOWY_FVEC, N_MATCHING, N_CXL_FABRIC,
    N_REGISTRY, N_ABI, N_PRIMITIVE, N_GATEWAY, N_FLOWY_CLI,
    N_PLUGIN_BUILTIN, N_COMP_LINEAR, N_COMP_SHARDED, N_COMP_TREE, N_COMP_PMAP,
    N_DOC_CH01, N_DOC_CH02, N_DOC_CH03, N_DOC_CH04, N_DOC_CH05, N_DOC_CH06,
    N_DOC_CH07, N_DOC_CH08, N_DOC_CH09, N_DOC_CH10, N_DOC_CH11, N_NODE_COUNT
};

static const struct { FlowNodeType type; const char *name, *mod; int core; uint32_t layer; } TOPO_NODES[] = {
    [N_PARSER]       = {FLOW_NODE_CORE_MODULE, "parser",       "core",      1, 0},
    [N_BITSPACE]     = {FLOW_NODE_CORE_MODULE, "bitspace",     "core",      1, 0},
    [N_SEARCH]       = {FLOW_NODE_CORE_MODULE, "search",       "core",      1, 0},
    [N_SECURITY]     = {FLOW_NODE_CORE_MODULE, "security",     "core",      1, 0},
    [N_ADAPTIVE]     = {FLOW_NODE_CORE_MODULE, "adaptive",     "core",      1, 0},
    [N_RELOAD]       = {FLOW_NODE_CORE_MODULE, "reload",       "core",      1, 0},
    [N_JIT]          = {FLOW_NODE_CORE_MODULE, "jit",          "core",      1, 0},
    [N_SMT]          = {FLOW_NODE_CORE_MODULE, "smt",          "core",      1, 0},
    [N_BACKEND]      = {FLOW_NODE_CORE_MODULE, "backend",      "core",      1, 0},
    [N_SWARM]        = {FLOW_NODE_CORE_MODULE, "swarm",        "core",      1, 0},
    [N_ORCHESTRATOR] = {FLOW_NODE_CORE_MODULE, "orchestrator", "core",      1, 0},
    [N_EMBODIED]     = {FLOW_NODE_CORE_MODULE, "embodied",     "core",      1, 0},
    [N_AUDIT]        = {FLOW_NODE_CORE_MODULE, "audit",        "core",      1, 0},
    [N_FLOWY_FVEC]   = {FLOW_NODE_CORE_MODULE, "flowy_fvec",   "core",      1, 0},
    [N_MATCHING]     = {FLOW_NODE_CORE_MODULE, "matching",     "core",      1, 0},
    [N_CXL_FABRIC]   = {FLOW_NODE_CORE_MODULE, "cxl_fabric",   "core",      1, 0},
    [N_REGISTRY]     = {FLOW_NODE_CORE_MODULE, "registry",     "interface", 1, 1},
    [N_ABI]          = {FLOW_NODE_CORE_MODULE, "abi",          "interface", 1, 1},
    [N_PRIMITIVE]    = {FLOW_NODE_CORE_MODULE, "primitive",    "interface", 1, 1},
    [N_GATEWAY]      = {FLOW_NODE_CORE_MODULE, "gateway",      "interface", 1, 1},
    [N_FLOWY_CLI]    = {FLOW_NODE_CORE_MODULE, "flowy_cli",    "interface", 1, 1},
    [N_PLUGIN_BUILTIN]= {FLOW_NODE_PLUGIN,     "builtin_plugin","plugin",   0, 2},
    [N_COMP_LINEAR]  = {FLOW_NODE_COMPONENT,   "linear_array", "plugin",    0, 2},
    [N_COMP_SHARDED] = {FLOW_NODE_COMPONENT,   "sharded_hash", "plugin",    0, 2},
    [N_COMP_TREE]    = {FLOW_NODE_COMPONENT,   "ordered_tree", "plugin",    0, 2},
    [N_COMP_PMAP]    = {FLOW_NODE_COMPONENT,   "parallel_map", "plugin",    0, 2},
    [N_DOC_CH01]     = {FLOW_NODE_DOC_CHAPTER, "ch01_what_is_flow", "book", 0, 4},
    [N_DOC_CH02]     = {FLOW_NODE_DOC_CHAPTER, "ch02_intent_vs_implementation", "book", 0, 4},
    [N_DOC_CH03]     = {FLOW_NODE_DOC_CHAPTER, "ch03_topology_graph", "book", 0, 4},
    [N_DOC_CH04]     = {FLOW_NODE_DOC_CHAPTER, "ch04_bitmanifold_engine", "book", 0, 4},
    [N_DOC_CH05]     = {FLOW_NODE_DOC_CHAPTER, "ch05_smt_formal_verification", "book", 0, 4},
    [N_DOC_CH06]     = {FLOW_NODE_DOC_CHAPTER, "ch06_jit_and_geometric_morphing", "book", 0, 4},
    [N_DOC_CH07]     = {FLOW_NODE_DOC_CHAPTER, "ch07_qsbr_lockfree_hotswap", "book", 0, 4},
    [N_DOC_CH08]     = {FLOW_NODE_DOC_CHAPTER, "ch08_hardware_primitive_drivers", "book", 0, 4},
    [N_DOC_CH09]     = {FLOW_NODE_DOC_CHAPTER, "ch09_fvec_universal_lockfile", "book", 0, 4},
    [N_DOC_CH10]     = {FLOW_NODE_DOC_CHAPTER, "ch10_deterministic_flowy_reasoner", "book", 0, 4},
    [N_DOC_CH11]     = {FLOW_NODE_DOC_CHAPTER, "ch11_level5_crucible_and_benchmarks", "book", 0, 4}
};

static const struct { uint8_t from, to, type; const char *label; } TOPO_EDGES[] = {
    {N_PARSER, N_SMT, FLOW_EDGE_CALLS, "lower_to_ir_verify"},
    {N_BITSPACE, N_SECURITY, FLOW_EDGE_CALLS, "safety_mask_compose"},
    {N_BITSPACE, N_SMT, FLOW_EDGE_CALLS, "polytope_pruning_mask"},
    {N_BITSPACE, N_ADAPTIVE, FLOW_EDGE_CALLS, "telemetry_bias"},
    {N_BITSPACE, N_SEARCH, FLOW_EDGE_USES, "search_dimensions"},
    {N_SEARCH, N_SMT, FLOW_EDGE_CALLS, "formal_soundness_proof"},
    {N_SWARM, N_BITSPACE, FLOW_EDGE_USES, "particle_entanglement"},
    {N_ADAPTIVE, N_RELOAD, FLOW_EDGE_CALLS, "live_migration"},
    {N_ADAPTIVE, N_JIT, FLOW_EDGE_CALLS, "rejit_migration"},
    {N_ORCHESTRATOR, N_BITSPACE, FLOW_EDGE_CALLS, "synthesize_topology"},
    {N_ORCHESTRATOR, N_SEARCH, FLOW_EDGE_CALLS, "global_anneal"},
    {N_ORCHESTRATOR, N_SMT, FLOW_EDGE_CALLS, "invariant_attestation"},
    {N_EMBODIED, N_BITSPACE, FLOW_EDGE_CALLS, "physics_safety_mask"},
    {N_EMBODIED, N_SECURITY, FLOW_EDGE_CALLS, "sim_to_real_gate"},
    {N_AUDIT, N_RELOAD, FLOW_EDGE_CALLS, "audit_qsbr_events"},
    {N_FLOWY_FVEC, N_SMT, FLOW_EDGE_CALLS, "affinity_gate_verification"},
    {N_MATCHING, N_SMT, FLOW_EDGE_CALLS, "verify_orderbook_conservation"},
    {N_CXL_FABRIC, N_SMT, FLOW_EDGE_CALLS, "verify_cxl_memory_safety"},
    {N_CXL_FABRIC, N_RELOAD, FLOW_EDGE_CALLS, "qsbr_page_migration"},
    {N_BACKEND, N_ABI, FLOW_EDGE_USES, "emit_abi_adapters"},
    {N_SEARCH, N_REGISTRY, FLOW_EDGE_CALLS, "lookup_components"},
    {N_BACKEND, N_REGISTRY, FLOW_EDGE_CALLS, "query_emitters"},
    {N_PRIMITIVE, N_REGISTRY, FLOW_EDGE_CALLS, "register_primitive"},
    {N_GATEWAY, N_PRIMITIVE, FLOW_EDGE_CALLS, "dispatch_primitive"},
    {N_GATEWAY, N_SWARM, FLOW_EDGE_USES, "hetero_mesh_routing"},
    {N_GATEWAY, N_RELOAD, FLOW_EDGE_CALLS, "qsbr_hotswap"},
    {N_FLOWY_CLI, N_AUDIT, FLOW_EDGE_CALLS, "render_telemetry_reports"},
    {N_PLUGIN_BUILTIN, N_REGISTRY, FLOW_EDGE_IMPLEMENTS, "FlowPluginDescriptor"},
    {N_COMP_LINEAR, N_PLUGIN_BUILTIN, FLOW_EDGE_USES, "declares_linear"},
    {N_COMP_SHARDED, N_PLUGIN_BUILTIN, FLOW_EDGE_USES, "declares_sharded"},
    {N_COMP_TREE, N_PLUGIN_BUILTIN, FLOW_EDGE_USES, "declares_tree"},
    {N_COMP_PMAP, N_PLUGIN_BUILTIN, FLOW_EDGE_USES, "declares_pmap"},
    {N_BACKEND, N_DOC_CH01, FLOW_EDGE_DOCUMENTS, "documents_why"},
    {N_PARSER, N_DOC_CH02, FLOW_EDGE_DOCUMENTS, "documents_why"},
    {N_BITSPACE, N_DOC_CH04, FLOW_EDGE_DOCUMENTS, "documents_why"},
    {N_SEARCH, N_DOC_CH04, FLOW_EDGE_DOCUMENTS, "documents_why"},
    {N_SWARM, N_DOC_CH04, FLOW_EDGE_DOCUMENTS, "documents_why"},
    {N_SMT, N_DOC_CH05, FLOW_EDGE_DOCUMENTS, "documents_why"},
    {N_JIT, N_DOC_CH06, FLOW_EDGE_DOCUMENTS, "documents_why"},
    {N_ADAPTIVE, N_DOC_CH06, FLOW_EDGE_DOCUMENTS, "documents_why"},
    {N_RELOAD, N_DOC_CH07, FLOW_EDGE_DOCUMENTS, "documents_why"},
    {N_PRIMITIVE, N_DOC_CH08, FLOW_EDGE_DOCUMENTS, "documents_why"},
    {N_EMBODIED, N_DOC_CH08, FLOW_EDGE_DOCUMENTS, "documents_why"},
    {N_FLOWY_FVEC, N_DOC_CH09, FLOW_EDGE_DOCUMENTS, "documents_why"},
    {N_AUDIT, N_DOC_CH10, FLOW_EDGE_DOCUMENTS, "documents_why"},
    {N_ORCHESTRATOR, N_DOC_CH10, FLOW_EDGE_DOCUMENTS, "documents_why"},
    {N_COMP_SHARDED, N_DOC_CH11, FLOW_EDGE_DOCUMENTS, "documents_why"},
    {N_GATEWAY, N_DOC_CH11, FLOW_EDGE_DOCUMENTS, "documents_why"},
    {N_MATCHING, N_DOC_CH08, FLOW_EDGE_DOCUMENTS, "documents_why"},
    {N_CXL_FABRIC, N_DOC_CH08, FLOW_EDGE_DOCUMENTS, "documents_why"}
};

void flow_topology_build_codebase_graph(FlowTopologyGraph *graph) {
    if (!graph) return;
    flow_topology_init(graph);
    for (size_t i = 0; i < sizeof(TOPO_NODES) / sizeof(TOPO_NODES[0]); ++i)
        flow_topology_add_node(graph, TOPO_NODES[i].type, TOPO_NODES[i].name, TOPO_NODES[i].mod, TOPO_NODES[i].core, TOPO_NODES[i].layer);
    for (size_t i = 0; i < sizeof(TOPO_EDGES) / sizeof(TOPO_EDGES[0]); ++i)
        flow_topology_add_edge(graph, TOPO_EDGES[i].from, TOPO_EDGES[i].to, (FlowEdgeType)TOPO_EDGES[i].type, 1.0, TOPO_EDGES[i].label);
}

void flow_topology_build_intent_graph(FlowTopologyGraph *graph, const SemanticIR *ir, const Component *comp, const FlowPlan *plan) {
    if (!graph || !ir) return;
    flow_topology_init(graph);
    uint32_t prev_id = 0;
    int has_prev = 0;
    for (size_t i = 0; i < ir->flow_node_count; ++i) {
        uint32_t op_id = flow_topology_add_node(graph, FLOW_NODE_INTENT_OP, ir->flow_nodes[i].name, "intent", 0, 3);
        if (has_prev) {
            flow_topology_add_edge(graph, prev_id, op_id, FLOW_EDGE_DATA_FLOW, (double)(ir->input_max_count > 0 ? ir->input_max_count : 1000), "pipeline_stream");
            flow_topology_add_edge(graph, prev_id, op_id, FLOW_EDGE_SHARD_AFFINITY, 0.95, "locality_affinity");
        }
        prev_id = op_id; has_prev = 1;
    }
    if (comp) {
        uint32_t c_id = flow_topology_add_node(graph, FLOW_NODE_COMPONENT, comp->id, comp->kind, 0, 2);
        if (has_prev) flow_topology_add_edge(graph, prev_id, c_id, FLOW_EDGE_USES, 1.0, "executes_on");
        if (plan) {
            for (size_t d = 0; d < plan->dimension_set.count; ++d) {
                uint32_t d_id = flow_topology_add_node(graph, FLOW_NODE_DIMENSION, plan->dimension_set.dimensions[d].name, "bitspace", 0, 1);
                flow_topology_add_edge(graph, c_id, d_id, FLOW_EDGE_BINDS_DIMENSION, (double)plan->assignment.values[d], "dimension_value");
            }
        }
    }
}

void flow_topology_audit(const FlowTopologyGraph *graph, FlowTopologyAuditReport *report) {
    if (!report) return;
    memset(report, 0, sizeof(*report));
    if (!graph) return;
    report->total_nodes = graph->node_count; report->total_edges = graph->edge_count;

    for (size_t i = 0; i < graph->node_count; ++i) {
        uint32_t l = graph->nodes[i].layer;
        if (l == 0) report->core_nodes++;
        else if (l == 2) report->plugin_nodes++;
        else if (l == 3) report->intent_nodes++;
        else if (l == 4 || graph->nodes[i].type == FLOW_NODE_DOC_CHAPTER) report->doc_nodes++;
    }
    for (size_t e = 0; e < graph->edge_count; ++e) {
        const FlowTopologyEdge *edge = &graph->edges[e];
        if (edge->type == FLOW_EDGE_DOCUMENTS) report->doc_edges++;
        if (graph->nodes[edge->from_id].layer == 0 && graph->nodes[edge->to_id].layer == 2 && edge->type == FLOW_EDGE_CALLS) {
            report->cross_layer_leaks++;
            if (strlen(report->leak_details) < 400) {
                char buf[128];
                snprintf(buf, sizeof(buf), "[LEAK] %s -> %s (%s); ", graph->nodes[edge->from_id].name, graph->nodes[edge->to_id].name, edge->label);
                strncat(report->leak_details, buf, sizeof(report->leak_details) - strlen(report->leak_details) - 1);
            }
        }
    }
    report->average_coupling = graph->node_count ? (double)graph->edge_count / (double)graph->node_count : 0.0;
    report->modularity_score = graph->edge_count ? 1.0 - ((double)report->cross_layer_leaks / (double)graph->edge_count) : 1.0;
}

double flow_topology_compute_affinity(const FlowTopologyGraph *g, uint32_t a, uint32_t b) {
    if (!g || a >= g->node_count || b >= g->node_count) return 0.0;
    if (a == b) return 1.0;
    for (size_t e = 0; e < g->edge_count; ++e) {
        const FlowTopologyEdge *edge = &g->edges[e];
        if ((edge->from_id == a && edge->to_id == b) || (edge->from_id == b && edge->to_id == a))
            if (edge->type == FLOW_EDGE_SHARD_AFFINITY || edge->type == FLOW_EDGE_DATA_FLOW)
                return edge->weight > 1.0 ? 1.0 : edge->weight;
    }
    return 0.1;
}

static const char *flow_node_type_name(FlowNodeType type) {
    static const char * const names[] = {"CoreModule", "Plugin", "Component", "IntentOp", "Dimension", "ShardGroup", "DocChapter", "FVecExperience"};
    return ((int)type >= 0 && (int)type <= 7) ? names[type] : "Unknown";
}

static const char *flow_edge_type_name(FlowEdgeType type) {
    static const char * const names[] = {"CALLS", "USES", "IMPLEMENTS", "BINDS_DIMENSION", "DATA_FLOW", "SHARD_AFFINITY", "DOCUMENTS", "MEMORIALIZES"};
    return ((int)type >= 0 && (int)type <= 7) ? names[type] : "RELATED_TO";
}

int flow_topology_export_json(const FlowTopologyGraph *g, FILE *out) {
    if (!g || !out) return 0;
    fprintf(out, "{\n  \"nodes\": [\n");
    for (size_t i = 0; i < g->node_count; ++i) {
        const FlowTopologyNode *n = &g->nodes[i];
        fprintf(out, "    {\"id\": %u, \"name\": \"%s\", \"type\": \"%s\", \"module\": \"%s\", \"layer\": %u, \"is_core\": %s}%s\n",
                n->id, n->name, flow_node_type_name(n->type), n->module, n->layer, n->is_core ? "true" : "false", i + 1 < g->node_count ? "," : "");
    }
    fprintf(out, "  ],\n  \"edges\": [\n");
    for (size_t e = 0; e < g->edge_count; ++e) {
        const FlowTopologyEdge *edge = &g->edges[e];
        fprintf(out, "    {\"source\": %u, \"target\": %u, \"type\": \"%s\", \"weight\": %.2f, \"label\": \"%s\"}%s\n",
                edge->from_id, edge->to_id, flow_edge_type_name(edge->type), edge->weight, edge->label, e + 1 < g->edge_count ? "," : "");
    }
    fprintf(out, "  ]\n}\n");
    return ferror(out) == 0;
}

int flow_topology_export_dot(const FlowTopologyGraph *g, FILE *out) {
    if (!g || !out) return 0;
    fprintf(out, "digraph FlowTopology {\n  rankdir=LR;\n  node [fontname=\"Helvetica\", fontsize=10, shape=box, style=filled];\n  edge [fontname=\"Helvetica\", fontsize=8];\n\n");
    for (size_t i = 0; i < g->node_count; ++i) {
        const FlowTopologyNode *n = &g->nodes[i];
        const char *c = n->layer == 0 ? "#E0F2FE" : n->layer == 1 ? "#FEF3C7" : n->layer == 2 ? "#DCFCE7" : "#F3E8FF";
        fprintf(out, "  node_%u [label=\"%s\\n(%s)\", fillcolor=\"%s\"];\n", n->id, n->name, flow_node_type_name(n->type), c);
    }
    fprintf(out, "\n");
    for (size_t e = 0; e < g->edge_count; ++e) {
        const FlowTopologyEdge *edge = &g->edges[e];
        fprintf(out, "  node_%u -> node_%u [label=\"%s\"];\n", edge->from_id, edge->to_id, edge->label[0] ? edge->label : flow_edge_type_name(edge->type));
    }
    fprintf(out, "}\n");
    return ferror(out) == 0;
}

int flow_topology_attach_telemetry(FlowTopologyGraph *g, const char *node_name, double score, const char *metric, double raw, double thresh, const char *unit, const char *sym) {
    if (!g || !node_name) return 0;
    for (size_t i = 0; i < g->node_count; ++i) {
        FlowTopologyNode *n = &g->nodes[i];
        if (!strcmp(n->name, node_name) || !strcmp(n->module, node_name)) {
            n->hotspot_score = score; n->hotspot_raw_val = raw; n->hotspot_threshold_val = thresh;
            if (metric) strncpy(n->hotspot_metric, metric, sizeof(n->hotspot_metric) - 1);
            if (unit) strncpy(n->hotspot_unit, unit, sizeof(n->hotspot_unit) - 1);
            if (sym) strncpy(n->dynamic_symptom, sym, sizeof(n->dynamic_symptom) - 1);
            return 1;
        }
    }
    return 0;
}

const FlowTopologyNode *flow_topology_get_peak_hotspot(const FlowTopologyGraph *g) {
    if (!g || !g->node_count) return NULL;
    const FlowTopologyNode *peak = NULL;
    double max_score = -1.0;
    for (size_t i = 0; i < g->node_count; ++i)
        if (g->nodes[i].hotspot_score > max_score) { max_score = g->nodes[i].hotspot_score; peak = &g->nodes[i]; }
    return peak;
}

int flow_topology_attach_fvec_store(FlowTopologyGraph *g, const void *store_ptr) {
    if (!g || !store_ptr) return 0;
    const FlowVecStore *store = (const FlowVecStore *)store_ptr;
    int attached = 0;
    for (size_t i = 0; i < store->count; ++i) {
        const FlowVecRecord *rec = &store->records[i];
        uint32_t f_id = flow_topology_add_node(g, FLOW_NODE_FVEC_EXPERIENCE, rec->header.id, rec->header.trigger_intent, 0, 2);
        for (size_t n = 0; n < g->node_count; ++n) {
            if (g->nodes[n].type == FLOW_NODE_COMPONENT && !strcmp(g->nodes[n].name, rec->header.component_id)) {
                flow_topology_add_edge(g, f_id, g->nodes[n].id, FLOW_EDGE_MEMORIALIZES, 1.0, "memorializes");
                break;
            }
        }
        attached++;
    }
    return attached;
}
