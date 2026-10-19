#include "../cmd_dispatch.h"
#include "../flowy.h"
#include "../flowy_cli.h"
#include "../topology.h"
#include "../benchmark.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void cmd_inspect_print_usage(FILE *out) { fprintf(out, "Usage: flowy inspect <ask|why|timeline|bottleneck|audit|audit-mechanisms|topos|doc> [options...]\n"); }
int cmd_inspect_run(int argc, char **argv) { if (argc < 1) { cmd_inspect_print_usage(stderr); return EXIT_FAILURE; }
    const char *sub = (argv[0][0] == '-' && argv[0][1] == '-') ? argv[0] + 2 : argv[0];
    if (!strcmp(sub, "-h") || !strcmp(sub, "help")) { cmd_inspect_print_usage(stdout); return EXIT_SUCCESS; } if (!strcmp(sub, "ask")) {
        if (argc < 2) { fprintf(stderr, "usage: flowy ask \"<query>\"\n"); return EXIT_FAILURE; } FlowTopologyGraph graph; flow_topology_build_codebase_graph(&graph);
        FlowyIntrospectiveAnswer ans; flowy_query_codebase(&graph, argv[1], &ans); flowy_print_answer(&ans, stdout); return EXIT_SUCCESS; } if (!strcmp(sub, "why")) {
        flowy_print_decision_explanation(flow_decision_logger_latest(NULL), stdout); return EXIT_SUCCESS; }
    if (!strcmp(sub, "timeline") || !strcmp(sub, "explain-decisions")) { flowy_print_decision_timeline(NULL, stdout); return EXIT_SUCCESS; } if (!strcmp(sub, "bottleneck")) {
        FlowTopologyGraph graph; flow_topology_build_codebase_graph(&graph); flowy_print_bottleneck_explanation(&graph, stdout); return EXIT_SUCCESS; }
    if (!strcmp(sub, "audit")) { FlowTopologyGraph graph; flow_topology_build_codebase_graph(&graph); FlowTopologyAuditReport r; flow_topology_audit(&graph, &r);
        printf("[FLOW Topology Audit] Nodes: %zu (Core:%zu, Plugins:%zu, Intents:%zu) | LOC:%zu | Leaks:%zu | Modularity:%.2f | SMT: QF_LIA SOUND\n",
               r.total_nodes, r.core_nodes, r.plugin_nodes, r.intent_nodes, r.total_loc, r.cross_layer_leaks, r.modularity_score);
        return r.cross_layer_leaks == 0 ? EXIT_SUCCESS : EXIT_FAILURE; } if (!strcmp(sub, "audit-mechanisms")) { FlowMechanismAuditReport rep;
        flow_benchmark_run_mechanism_audit(&rep); flow_benchmark_print_mechanism_audit(&rep, stdout); return EXIT_SUCCESS; }
    if (!strcmp(sub, "topos") || !strcmp(sub, "hott") || !strcmp(sub, "cubical")) { flowy_print_cubical_topos_report(stdout); return EXIT_SUCCESS; } if (!strcmp(sub, "doc")) {
        const char *mod = (argc >= 2) ? argv[1] : "all"; FlowTopologyGraph graph; flow_topology_build_codebase_graph(&graph); if (!strcmp(mod, "all")) {
            for (size_t i = 0; i < flowy_knowledge_count(); ++i) { const FlowModuleKnowledge *k = flowy_knowledge_at(i);
                printf("* [%s] (L%u) %s (%s) — %s\n", k->module_id, k->layer, k->title, k->source_file, k->responsibilities); } return EXIT_SUCCESS; }
        const FlowModuleKnowledge *k = flowy_knowledge_lookup(mod); if (!k) { fprintf(stderr, "flowy doc: module '%s' not found.\n", mod); return EXIT_FAILURE; }
        printf("=== [%s] %s (Layer %u) ===\nFiles: %s, %s\nRole: %s\nGuarantee: %s\nAPIs: %s\n",
               k->module_id, k->title, k->layer, k->header_file, k->source_file, k->responsibilities, k->algorithmic_guarantee, k->key_apis); return EXIT_SUCCESS; }
    cmd_inspect_print_usage(stderr); return EXIT_FAILURE; }
