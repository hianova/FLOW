#include "flowy.h"
#include "topology.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "flowy-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

int main(void) {
    printf("Starting FLOW Introspective Codebase Knowledge & Reasoner Test...\n");

    /* 1. Test Introspective Knowledge Base Registration */
    size_t k_count = flowy_knowledge_count();
    CHECK(k_count >= 14);

    const FlowModuleKnowledge *k_bit = flowy_knowledge_lookup("bitspace");
    CHECK(k_bit != NULL);
    CHECK(strcmp(k_bit->header_file, "src/bitspace.h") == 0);
    CHECK(strstr(k_bit->algorithmic_guarantee, "12.96 ns") != NULL);

    const FlowModuleKnowledge *k_qsbr = flowy_knowledge_lookup("reload");
    CHECK(k_qsbr != NULL);
    CHECK(strstr(k_qsbr->title, "QSBR") != NULL);
    CHECK(strstr(k_qsbr->key_apis, "flow_reload_call") != NULL);

    /* 2. Test Deterministic Semantic Queries over Codebase Graph */
    FlowTopologyGraph graph;
    flow_topology_build_codebase_graph(&graph);

    /* Query: QSBR memory reclamation */
    FlowyIntrospectiveAnswer ans_qsbr;
    CHECK(flowy_query_codebase(&graph, "how does lock-free QSBR memory reclamation work?", &ans_qsbr) == 1);
    CHECK(ans_qsbr.primary_module != NULL);
    CHECK(strcmp(ans_qsbr.primary_module->module_id, "reload") == 0);
    CHECK(strstr(ans_qsbr.explanation, "Unified QSBR") != NULL);
    CHECK(strstr(ans_qsbr.explanation, "src/reload.h") != NULL);

    /* Query: 1-Bit chaotic search & mask canvas */
    FlowyIntrospectiveAnswer ans_chaos;
    CHECK(flowy_query_codebase(&graph, "1-bit chaos mutation mask canvas", &ans_chaos) == 1);
    CHECK(ans_chaos.primary_module != NULL);
    CHECK(strcmp(ans_chaos.primary_module->module_id, "bitspace") == 0);
    CHECK(strstr(ans_chaos.explanation, "1024-Bit BitSpace") != NULL);

    /* Query: SMT mathematical proofs */
    FlowyIntrospectiveAnswer ans_smt;
    CHECK(flowy_query_codebase(&graph, "SMT formal mathematical proofs and theorems", &ans_smt) == 1);
    CHECK(ans_smt.primary_module != NULL);
    CHECK(strcmp(ans_smt.primary_module->module_id, "smt") == 0);
    CHECK(strstr(ans_smt.explanation, "SMT-LIB2") != NULL);

    /* Query: Embodied Robotics & Sim-to-Real */
    FlowyIntrospectiveAnswer ans_robot;
    CHECK(flowy_query_codebase(&graph, "具身機器人步態規劃與質心抗震防護", &ans_robot) == 1);
    CHECK(ans_robot.primary_module != NULL);
    CHECK(strcmp(ans_robot.primary_module->module_id, "embodied") == 0);
    CHECK(strstr(ans_robot.explanation, "src/embodied.h") != NULL);

    /* Test Printing */
    flowy_print_answer(&ans_qsbr, stdout);
    flowy_print_answer(&ans_robot, stdout);

    printf("FLOWY_TEST=passed introspective_knowledge=verified deterministic_queries=verified\n");
    return 0;
}
