#include "topology.h"
#include "cubical_hott.h"
#include "registry.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void flow_topology_init(FlowTopologyGraph *g) { if (g) memset(g, 0, sizeof(*g)); }

uint32_t flow_topology_add_node(FlowTopologyGraph *g, FlowNodeType t, const char *n, const char *m, int c, uint32_t l) {
    if (!g || g->node_count >= FLOW_TOPOLOGY_MAX_NODES) return 0;
    uint32_t id = (uint32_t)g->node_count++;
    FlowTopologyNode *node = &g->nodes[id];
    *node = (FlowTopologyNode){ .id = id, .type = t, .is_core = c, .layer = l, .capability_mask = n ? flow_cubical_get_module_mask(n) : 0 };
    strncpy(node->name, n ? n : "", sizeof(node->name) - 1);
    strncpy(node->module, m ? m : "", sizeof(node->module) - 1);
    strncpy(node->role, m ? m : "core", sizeof(node->role) - 1);
    return id;
}

int flow_topology_add_edge(FlowTopologyGraph *g, uint32_t f, uint32_t to, FlowEdgeType t, double w, const char *lbl) {
    if (!g || g->edge_count >= FLOW_TOPOLOGY_MAX_EDGES || f >= g->node_count || to >= g->node_count) return 0;
    FlowTopologyEdge *e = &g->edges[g->edge_count++];
    *e = (FlowTopologyEdge){.from_id = f, .to_id = to, .type = t, .weight = w};
    strncpy(e->label, lbl ? lbl : "", sizeof(e->label) - 1);
    g->nodes[f].fan_out++;
    g->nodes[to].fan_in++;
    return 1;
}

static int find_node_by_name(const FlowTopologyGraph *g, const char *name) {
    if (!g || !name) return -1;
    for (size_t i = 0; i < g->node_count; ++i)
        if (!strcmp(g->nodes[i].name, name)) return (int)i;
    return -1;
}

static int ensure_stem_node_in_dir(FlowTopologyGraph *g, const char *dir_path, const char *stem) {
    int idx = find_node_by_name(g, stem);
    if (idx >= 0) return idx;
    if (!strcmp(stem, "flowc") || !strcmp(stem, "flowy_main")) return -1;
    if (dir_path && strstr(dir_path, "plugins"))
        return (int)flow_topology_add_node(g, FLOW_NODE_PLUGIN, stem, "plugin", 0, 2);
    if (!strcmp(stem, "registry") || !strcmp(stem, "abi") || !strcmp(stem, "primitive") ||
        !strcmp(stem, "plugin") || !strcmp(stem, "flow") || !strncmp(stem, "flowy", 5))
        return (int)flow_topology_add_node(g, FLOW_NODE_CORE_MODULE, stem, "interface", 1, 1);
    if (strstr(stem, "plugin") != NULL || !strncmp(stem, "driver_", 7) || !strncmp(stem, "embodied", 8) || !strncmp(stem, "flow_embodied", 13))
        return (int)flow_topology_add_node(g, FLOW_NODE_PLUGIN, stem, "plugin", 0, 2);
    return (int)flow_topology_add_node(g, FLOW_NODE_CORE_MODULE, stem, "core", 1, 0);
}

static void scan_file_loc_and_includes(FlowTopologyGraph *g, int node_idx, const char *filepath) {
    FILE *f = fopen(filepath, "r");
    if (!f) return;
    char line[512];
    uint32_t lines = 0;
    while (fgets(line, sizeof(line), f)) {
        lines++;
        if (line[0] == '#' && strstr(line, "include")) {
            char *q1 = strchr(line, '"');
            if (!q1) continue;
            char *q2 = strchr(q1 + 1, '"');
            if (!q2) continue;
            char inc[128] = {0}, stem[64] = {0};
            size_t len = (size_t)(q2 - (q1 + 1));
            if (len >= sizeof(inc)) len = sizeof(inc) - 1;
            memcpy(inc, q1 + 1, len);
            const char *slash = strrchr(inc, '/'), *base = slash ? slash + 1 : inc;
            strncpy(stem, base, sizeof(stem) - 1);
            char *dot = strrchr(stem, '.');
            if (dot) *dot = '\0';
            int target_idx = ensure_stem_node_in_dir(g, NULL, stem);
            if (target_idx >= 0 && target_idx != node_idx) {
                int exists = 0;
                for (size_t e = 0; e < g->edge_count; ++e)
                    if (g->edges[e].from_id == (uint32_t)node_idx && g->edges[e].to_id == (uint32_t)target_idx) { exists = 1; break; }
                if (!exists)
                    flow_topology_add_edge(g, (uint32_t)node_idx, (uint32_t)target_idx, FLOW_EDGE_USES, 1.0, "includes_header");
            }
        }
    }
    fclose(f);
    g->nodes[node_idx].loc += lines;
}

int flow_topology_scan_workspace(FlowTopologyGraph *g, const char *src_dir) {
    if (!g) return 0;
    const char *dir_path = (src_dir && src_dir[0]) ? src_dir : "src";
    DIR *d = opendir(dir_path);
    if (!d) return 0;
    struct dirent *ent;
    int scanned = 0;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        size_t len = strlen(ent->d_name);
        if (len > 2 && (!strcmp(ent->d_name + len - 2, ".c") || !strcmp(ent->d_name + len - 2, ".h"))) {
            if (strstr(dir_path, "tests")) continue;
            char stem[64] = {0};
            strncpy(stem, ent->d_name, sizeof(stem) - 1);
            char *dot = strrchr(stem, '.');
            if (dot) *dot = '\0';
            int idx = ensure_stem_node_in_dir(g, dir_path, stem);
            if (idx >= 0) {
                char full[512];
                snprintf(full, sizeof(full), "%s/%s", dir_path, ent->d_name);
                scan_file_loc_and_includes(g, idx, full);
                scanned++;
            }
        } else if (len > 5 && (!strcmp(ent->d_name + len - 5, ".lean") || !strcmp(ent->d_name + len - 5, ".flow"))) {
            char stem[64] = {0};
            strncpy(stem, ent->d_name, sizeof(stem) - 1);
            char *dot = strrchr(stem, '.');
            if (dot) *dot = '\0';
            int idx = find_node_by_name(g, stem);
            if (idx < 0) idx = (int)flow_topology_add_node(g, FLOW_NODE_INTENT_OP, stem, "intent", 0, 3);
            if (idx >= 0) {
                char full[512];
                snprintf(full, sizeof(full), "%s/%s", dir_path, ent->d_name);
                scan_file_loc_and_includes(g, idx, full);
            }
            int parser_idx = find_node_by_name(g, "parser");
            if (idx >= 0 && parser_idx >= 0)
                flow_topology_add_edge(g, (uint32_t)idx, (uint32_t)parser_idx, FLOW_EDGE_USES, 1.0, "parsed_by");
            scanned++;
        }
    }
    closedir(d);
    return scanned;
}

void flow_topology_build_codebase_graph(FlowTopologyGraph *graph) {
    if (!graph) return;
    flow_topology_init(graph);
    static const char *const dirs[] = {"src", "src/fvec", "src/jet", "src/topo", "src/inspect", "plugins", "tests"};
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i)
        flow_topology_scan_workspace(graph, dirs[i]);
    if (flow_registry_init()) {
        int reg_idx = find_node_by_name(graph, "registry");
        int bp_idx = find_node_by_name(graph, "builtin_plugin");
        if (bp_idx >= 0 && reg_idx >= 0)
            flow_topology_add_edge(graph, (uint32_t)bp_idx, (uint32_t)reg_idx, FLOW_EDGE_IMPLEMENTS, 1.0, "FlowPluginDescriptor");
        for (size_t i = 0; i < component_count(); ++i) {
            const Component *c = component_at(i);
            if (!c || !c->id) continue;
            int c_idx = find_node_by_name(graph, c->id);
            if (c_idx < 0) c_idx = (int)flow_topology_add_node(graph, FLOW_NODE_COMPONENT, c->id, "plugin", 0, 2);
            if (bp_idx >= 0 && c_idx >= 0)
                flow_topology_add_edge(graph, (uint32_t)c_idx, (uint32_t)bp_idx, FLOW_EDGE_USES, 1.0, "registered_component");
        }
    }
}

void flow_topology_audit(const FlowTopologyGraph *graph, FlowTopologyAuditReport *report) {
    if (!report) return;
    memset(report, 0, sizeof(*report));
    if (!graph) return;
    report->total_nodes = graph->node_count;
    report->total_edges = graph->edge_count;
    for (size_t i = 0; i < graph->node_count; ++i) {
        uint32_t l = graph->nodes[i].layer;
        report->total_loc += graph->nodes[i].loc;
        if (l == 0 || l == 1) report->core_nodes++;
        else if (l == 2) report->plugin_nodes++;
        else if (l == 3) report->intent_nodes++;
    }
    for (size_t e = 0; e < graph->edge_count; ++e) {
        const FlowTopologyEdge *edge = &graph->edges[e];
        if ((graph->nodes[edge->from_id].layer == 0 || graph->nodes[edge->from_id].layer == 1) && graph->nodes[edge->to_id].layer == 2) {
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
