#include "flowy.h"
#include "orchestrator.h"
#include "registry.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "flowy-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

int main(void) {
    printf("Starting Flowy Chaos Conversational Assistant Test...\n");
    flow_registry_init();

    /* ===================================================================== */
    /* 1. Test Natural Language Intent Parsing                               */
    /* ===================================================================== */
    FlowyUserIntent intent_mem;
    CHECK(flowy_parse_intent("我覺得現在記憶體太肥了，幫我優化", &intent_mem) == 1);
    CHECK(intent_mem.kind == FLOWY_INTENT_OPTIMIZE_MEMORY);
    CHECK(intent_mem.synthesized_mask == 0x00000000ffff0000ULL);

    FlowyUserIntent intent_lat;
    CHECK(flowy_parse_intent("The latency is too high, maximize throughput and speed", &intent_lat) == 1);
    CHECK(intent_lat.kind == FLOWY_INTENT_OPTIMIZE_LATENCY);
    CHECK(intent_lat.synthesized_mask == 0x000000000000ffffULL);

    FlowyUserIntent intent_sec;
    CHECK(flowy_parse_intent("enforce strict production security compliance audit", &intent_sec) == 1);
    CHECK(intent_sec.kind == FLOWY_INTENT_ENFORCE_SECURITY);

    FlowyUserIntent intent_robot;
    CHECK(flowy_parse_intent("幫機器人做步態規劃與抗震", &intent_robot) == 1);
    CHECK(intent_robot.kind == FLOWY_INTENT_EMBODIED_ROBOTICS);

    FlowyUserIntent intent_smt;
    CHECK(flowy_parse_intent("run SMT formal mathematical proof", &intent_smt) == 1);
    CHECK(intent_smt.kind == FLOWY_INTENT_SMT_PROVE);

    /* ===================================================================== */
    /* 2. Test 1-Bit Chaos Topological Processing                            */
    /* ===================================================================== */
    FlowOrchestrator *orch = flow_orchestrator_create(".");
    char diag[256] = {0};
    flow_orchestrator_absorb(orch, "examples/compiler.flow", diag, sizeof(diag));
    flow_orchestrator_absorb(orch, "examples/project.flow", diag, sizeof(diag));

    FlowyResponse resp_mem;
    CHECK(flowy_process_with_chaos(orch, &intent_mem, &resp_mem) == 1);
    CHECK(resp_mem.ram_reduction_percent > 30.0);
    CHECK(strstr(resp_mem.explanation, "SoA") != NULL);
    CHECK(strstr(resp_mem.ascii_art, "•.•") != NULL);

    FlowyResponse resp_lat;
    CHECK(flowy_process_with_chaos(orch, &intent_lat, &resp_lat) == 1);
    CHECK(resp_lat.latency_reduction_percent > 50.0);
    CHECK(strstr(resp_lat.explanation, "QSBR") != NULL);

    /* Test Rendering */
    flowy_render_response(&resp_mem, stdout);
    flowy_render_response(&resp_lat, stdout);

    flow_orchestrator_destroy(orch);

    printf("FLOWY_TEST=passed natural_language_parsing=verified chaos_bridge=sound human_dialogue=verified\n");
    return 0;
}
