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

typedef enum {
    FLOW_NODE_CORE_MODULE = 0,   /* Core compiler modules (parser, semantic, search, bitspace, verifier, reload) */
    FLOW_NODE_PLUGIN = 1,        /* Domain plugins (builtin, custom DSO) */
    FLOW_NODE_COMPONENT = 2,     /* Architectural components (linear_array, sharded_hash, etc.) */
    FLOW_NODE_INTENT_OP = 3,     /* Flow Intent operations (transform, group, sort, top, etc.) */
    FLOW_NODE_DIMENSION = 4,     /* BitSpace dimensions (capacity, threads, shards, buffer_bytes) */
    FLOW_NODE_SHARD_GROUP = 5,   /* Shard partition cluster */
    FLOW_NODE_DOC_CHAPTER = 6    /* Doc-as-Topology: Living Documentation Chapter / Philosophy Node */
} FlowNodeType;

typedef enum {
    FLOW_EDGE_CALLS = 0,             /* Direct function/symbol invocation */
    FLOW_EDGE_USES = 1,              /* Struct or type usage */
    FLOW_EDGE_IMPLEMENTS = 2,        /* Plugin implements Component interface */
    FLOW_EDGE_BINDS_DIMENSION = 3,   /* Component binds to BitSpace dimension */
    FLOW_EDGE_DATA_FLOW = 4,         /* Dataflow pipeline connection (A -> B) */
    FLOW_EDGE_SHARD_AFFINITY = 5,    /* Affinity / Locality between nodes */
    FLOW_EDGE_DOCUMENTS = 6          /* Doc-as-Topology: Code Node -> Book Chapter / Design Philosophy */
} FlowEdgeType;

typedef struct {
    uint32_t id;
    FlowNodeType type;
    char name[64];
    char module[64];
    int is_core;
    uint32_t layer; /* 0=Core, 1=ABI/Registry, 2=Plugin, 3=Intent/User, 4=Doc */

    /* Subconscious Neural Telemetry (attached by background eBPF / PMU / QSBR probes) */
    double hotspot_score;            /* Normalized hotspot intensity [0.0 .. 100.0] */
    char hotspot_metric[64];         /* e.g. "L3 Cache Miss Rate", "QSBR Epoch Queue Depth", "Motor Torque Ratio" */
    double hotspot_raw_val;          /* Raw measured value */
    double hotspot_threshold_val;    /* Normal baseline value */
    char hotspot_unit[16];           /* e.g. "%", "MB/s", "N*m" */
    char dynamic_symptom[128];       /* Short symptom description */
} FlowTopologyNode;

typedef struct {
    uint32_t from_id;
    uint32_t to_id;
    FlowEdgeType type;
    double weight; /* Interaction frequency or coupling weight */
    char label[64];
} FlowTopologyEdge;

typedef struct {
    FlowTopologyNode nodes[FLOW_TOPOLOGY_MAX_NODES];
    size_t node_count;
    FlowTopologyEdge edges[FLOW_TOPOLOGY_MAX_EDGES];
    size_t edge_count;
} FlowTopologyGraph;

typedef struct {
    size_t total_nodes;
    size_t total_edges;
    size_t core_nodes;
    size_t plugin_nodes;
    size_t intent_nodes;
    size_t doc_nodes;
    size_t doc_edges;
    size_t cross_layer_leaks;
    char leak_details[512];
    double average_coupling;
    double modularity_score;
} FlowTopologyAuditReport;

/* Graph Construction & Analysis */
void flow_topology_init(FlowTopologyGraph *graph);
uint32_t flow_topology_add_node(FlowTopologyGraph *graph, FlowNodeType type,
                                const char *name, const char *module, int is_core, uint32_t layer);
int flow_topology_add_edge(FlowTopologyGraph *graph, uint32_t from_id, uint32_t to_id,
                           FlowEdgeType type, double weight, const char *label);

/* Subconscious Neural Telemetry Ingestion (Mechanism -> Neural Graph) */
int flow_topology_attach_telemetry(FlowTopologyGraph *graph, const char *node_name,
                                  double hotspot_score, const char *metric_name,
                                  double raw_val, double thresh_val,
                                  const char *unit, const char *symptom);
const FlowTopologyNode *flow_topology_get_peak_hotspot(const FlowTopologyGraph *graph);

/* Build Complete FLOW Architecture & Spec Topology */
void flow_topology_build_codebase_graph(FlowTopologyGraph *graph);
void flow_topology_build_intent_graph(FlowTopologyGraph *graph, const SemanticIR *ir,
                                      const Component *component, const FlowPlan *plan);

/* Audits & Queries */
void flow_topology_audit(const FlowTopologyGraph *graph, FlowTopologyAuditReport *report);
double flow_topology_compute_affinity(const FlowTopologyGraph *graph, uint32_t node_a, uint32_t node_b);

/* Serialization & Visualization */
int flow_topology_export_json(const FlowTopologyGraph *graph, FILE *output);
int flow_topology_export_dot(const FlowTopologyGraph *graph, FILE *output);

#endif
