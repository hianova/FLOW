#include "../cmd_dispatch.h"
#include "../flowy.h"
#include "../flowy_cli.h"
#include "../topology.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void cmd_inspect_print_usage(FILE *out) { fprintf(out, "Usage: flowy inspect <ask|why|timeline|bottleneck|audit|topos|doc> [options...]\n"); }
int cmd_inspect_run(int argc, char **argv) {
    if (argc < 1) { cmd_inspect_print_usage(stderr); return EXIT_FAILURE; }
    const char *sub = (argv[0][0] == '-' && argv[0][1] == '-') ? argv[0] + 2 : argv[0];
    if (!strcmp(sub, "-h") || !strcmp(sub, "help")) { cmd_inspect_print_usage(stdout); return EXIT_SUCCESS; }
    if (!strcmp(sub, "ask")) {
        if (argc < 2) return EXIT_FAILURE;
        FlowTopologyGraph g; flow_topology_build_codebase_graph(&g);
        FlowyIntrospectiveAnswer ans; flowy_query_codebase(&g, argv[1], &ans); flowy_print_answer(&ans, stdout);
        return EXIT_SUCCESS;
    }
    if (!strcmp(sub, "why")) { flowy_print_decision_explanation(flow_decision_logger_latest(NULL), stdout); return EXIT_SUCCESS; }
    if (!strcmp(sub, "timeline") || !strcmp(sub, "explain-decisions")) { flowy_print_decision_timeline(NULL, stdout); return EXIT_SUCCESS; }
    if (!strcmp(sub, "bottleneck")) { FlowTopologyGraph g; flow_topology_build_codebase_graph(&g); flowy_print_bottleneck_explanation(&g, stdout); return EXIT_SUCCESS; }
    if (!strcmp(sub, "audit")) {
        FlowTopologyGraph g; flow_topology_build_codebase_graph(&g); FlowTopologyAuditReport r; flow_topology_audit(&g, &r);
        printf("[FLOW Topology Audit] Nodes: %zu (Core:%zu, Plugins:%zu, Intents:%zu) | LOC:%zu | Leaks:%zu | Modularity:%.2f | SMT: QF_LIA SOUND\n",
               r.total_nodes, r.core_nodes, r.plugin_nodes, r.intent_nodes, r.total_loc, r.cross_layer_leaks, r.modularity_score);
        return r.cross_layer_leaks == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (!strcmp(sub, "topos") || !strcmp(sub, "hott") || !strcmp(sub, "cubical")) { flowy_print_cubical_topos_report(stdout); return EXIT_SUCCESS; }
    if (!strcmp(sub, "doc")) {
        const char *mod = (argc >= 2) ? argv[1] : "all";
        FlowTopologyGraph g; flow_topology_build_codebase_graph(&g);
        for (size_t i = 0; i < g.node_count; ++i) {
            const FlowTopologyNode *n = &g.nodes[i];
            if (!strcmp(mod, "all") || !strcmp(mod, n->name))
                printf("* [%s] (L%u, %s) LOC=%u FanIn=%u FanOut=%u Sieve=0x%016llx\n",
                       n->name, n->layer, n->role, n->loc, n->fan_in, n->fan_out, (unsigned long long)n->capability_mask);
        }
        return EXIT_SUCCESS;
    }
    cmd_inspect_print_usage(stderr);
    return EXIT_FAILURE;
}
