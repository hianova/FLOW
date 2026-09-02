#include "flowy.h"
#include "embodied.h"
#include "adaptive.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "decision-explain-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

int main(void) {
    printf("Starting FLOW Real-Time Telemetry & Decision Causal Explainer Test...\n");

    /* 1. Test Decision Logger Initialization and Custom Event Ingestion */
    FlowDecisionLogger logger;
    flow_decision_logger_init(&logger);

    FlowDecisionEvent ev1 = {
        .timestamp_ns = 5000000ULL, /* t = 5.0 ms */
        .trigger_type = FLOW_DECISION_TRIGGER_TORQUE_ANOMALY,
        .trigger_source = "left_leg_motor",
        .observed_metric_value = 85.4,
        .threshold_limit_value = 80.0,
        .metric_unit = "N*m",
        .violated_constraint = "Center of Mass (CoM) Stability & Joint Torque Rating Limit",
        .flipped_genome_bit = 14,
        .pre_topology = "Single_Actuator_Direct",
        .post_topology = "Bipedal_Load_Balanced",
        .causal_rationale = "At t=5.0ms, eBPF detected left_leg_motor torque anomaly (85.4 N*m > 80.0 N*m limit). To satisfy CoM stability, 1-bit chaotic engine flipped bit #14, shifting 62% load to right_leg_motor.",
        .hot_swap_grace_ns = 84
    };
    CHECK(flow_decision_logger_record(&logger, &ev1) == 1);
    CHECK(logger.total_recorded == 1);

    const FlowDecisionEvent *latest = flow_decision_logger_latest(&logger);
    CHECK(latest != NULL);
    CHECK(latest->timestamp_ns == 5000000ULL);
    CHECK(strcmp(latest->trigger_source, "left_leg_motor") == 0);

    /* 2. Test Deterministic Causal Explanation Generation */
    char explanation[2048] = {0};
    flowy_explain_decision(latest, explanation, sizeof(explanation));
    CHECK(strstr(explanation, "left_leg_motor") != NULL);
    CHECK(strstr(explanation, "85.40 N*m") != NULL);
    CHECK(strstr(explanation, "Flipped Bit #14") != NULL);
    CHECK(strstr(explanation, "QSBR") != NULL);

    /* 3. Test Flowy Answering "Why" Questions via Semantic Match */
    FlowTopologyGraph graph;
    flow_topology_build_codebase_graph(&graph);

    FlowyIntrospectiveAnswer ans_why;
    CHECK(flowy_query_codebase(&graph, "為什麼機器人左腿馬達在 t=5ms 減速？", &ans_why) == 1);
    CHECK(strstr(ans_why.explanation, "left_leg") != NULL);
    CHECK(strstr(ans_why.explanation, "CAUSAL REASONING") != NULL);

    /* 4. Test Timeline Rendering */
    flowy_print_decision_timeline(&logger, stdout);

    printf("DECISION_EXPLAIN_TEST=passed causal_reasoning=verified telemetry_explainability=verified\n");
    return 0;
}
