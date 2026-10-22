#ifndef FLOW_TOPOLOGY_H
#define FLOW_TOPOLOGY_H
#include "flow.h"
#include "registry.h"
#include "bitspace.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#define FLOW_TOPOLOGY_MAX_NODES 256
#define FLOW_TOPOLOGY_MAX_EDGES 1024
typedef enum { FLOW_NODE_CORE_MODULE = 0, FLOW_NODE_PLUGIN = 1, FLOW_NODE_COMPONENT = 2, FLOW_NODE_INTENT_OP = 3, FLOW_NODE_DIMENSION = 4, FLOW_NODE_SHARD_GROUP = 5,
    FLOW_NODE_FVEC_EXPERIENCE = 6 } FlowNodeType;
typedef enum { FLOW_EDGE_CALLS = 0, FLOW_EDGE_USES = 1, FLOW_EDGE_IMPLEMENTS = 2, FLOW_EDGE_BINDS_DIMENSION = 3, FLOW_EDGE_DATA_FLOW = 4,
    FLOW_EDGE_SHARD_AFFINITY = 5, FLOW_EDGE_MEMORIALIZES = 6 } FlowEdgeType;
typedef struct { uint32_t id; FlowNodeType type; char name[64], module[64], role[128]; int is_core; uint32_t layer, loc, fan_in, fan_out;
    uint64_t capability_mask; double hotspot_score; char hotspot_metric[64]; double hotspot_raw_val, hotspot_threshold_val; char hotspot_unit[16], dynamic_symptom[128]; } FlowTopologyNode;
typedef struct { uint32_t from_id, to_id; FlowEdgeType type; double weight; char label[64]; } FlowTopologyEdge;
typedef struct { FlowTopologyNode nodes[FLOW_TOPOLOGY_MAX_NODES]; size_t node_count; FlowTopologyEdge edges[FLOW_TOPOLOGY_MAX_EDGES]; size_t edge_count; } FlowTopologyGraph;
typedef struct { size_t total_nodes, total_edges, core_nodes, plugin_nodes, intent_nodes, cross_layer_leaks, total_loc; char leak_details[512];
    double average_coupling, modularity_score; } FlowTopologyAuditReport;
void flow_topology_init(FlowTopologyGraph *graph);
uint32_t flow_topology_add_node(FlowTopologyGraph *graph, FlowNodeType type, const char *name, const char *module, int is_core, uint32_t layer);
int flow_topology_add_edge(FlowTopologyGraph *graph, uint32_t from_id, uint32_t to_id, FlowEdgeType type, double weight, const char *label);
int flow_topology_attach_telemetry(FlowTopologyGraph *graph, const char *node_name, double hotspot_score, const char *metric_name, double raw_val, double thresh_val, const char *unit, const char *symptom);
const FlowTopologyNode *flow_topology_get_peak_hotspot(const FlowTopologyGraph *graph);
int flow_topology_scan_workspace(FlowTopologyGraph *graph, const char *src_dir);
void flow_topology_build_codebase_graph(FlowTopologyGraph *graph);
void flow_topology_audit(const FlowTopologyGraph *graph, FlowTopologyAuditReport *report);
#endif
