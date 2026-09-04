#include "flow_test_kit.h"
#include "flow.h"
#include "registry.h"
#include "topology.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "topology-test assertion failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

int main(void) {
    flow_registry_init();

    /* 1. Test Codebase Architecture Topology Graph */
    FlowTopologyGraph codebase_graph;
    flow_topology_build_codebase_graph(&codebase_graph);
    CHECK(codebase_graph.node_count >= 10);
    CHECK(codebase_graph.edge_count >= 10);

    FlowTopologyAuditReport report;
    flow_topology_audit(&codebase_graph, &report);
    CHECK(report.core_nodes >= 9);
    CHECK(report.plugin_nodes >= 5);
    CHECK(report.cross_layer_leaks == 0);
    CHECK(report.modularity_score == 1.0);

    /* 2. Test Intent & Dataflow Graph Construction */
    FLOW_TEST_CASE("tests/topology-test.c",
"input task_stream {\n"
        "    max_count 8192\n"
        "}\n"
        "flow data_pipe {\n"
        "    task_stream -> filter -> aggregate -> export\n"
        "}\n"
        "import builtin\n"
        "require {\n"
        "    deterministic\n"
        "    memory < 64mb\n"
        "}\n",
{
const Component *comp = select_component(&ir);
    CHECK(comp != NULL);

    FlowTopologyGraph intent_graph;
    flow_topology_build_intent_graph(&intent_graph, &ir, comp, NULL);
    CHECK(intent_graph.node_count >= 5);
    CHECK(intent_graph.edge_count >= 6);

    double affinity = flow_topology_compute_affinity(&intent_graph, 0, 1);
    CHECK(affinity >= 0.9);

    /* 3. Test Serialization to JSON & DOT */
    char json_buf[4096];
    FILE *json_out = fmemopen(json_buf, sizeof(json_buf), "w");
    CHECK(json_out != NULL);
    CHECK(flow_topology_export_json(&intent_graph, json_out));
    fclose(json_out);
    CHECK(strstr(json_buf, "\"type\": \"IntentOp\"") != NULL);
    CHECK(strstr(json_buf, "\"type\": \"SHARD_AFFINITY\"") != NULL);

    char dot_buf[4096];
    FILE *dot_out = fmemopen(dot_buf, sizeof(dot_buf), "w");
    CHECK(dot_out != NULL);
    CHECK(flow_topology_export_dot(&intent_graph, dot_out));
    fclose(dot_out);
    CHECK(strstr(dot_buf, "digraph FlowTopology") != NULL);

    


    printf("TOPOLOGY_TEST=passed codebase_nodes=%zu modularity=%.2f leaks=0 affinity=%.2f\n",
           report.total_nodes, report.modularity_score, affinity);
    return 0;

});
}