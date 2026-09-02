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

    /* 4. Test Subconscious Neural Telemetry Ingestion & Bottleneck Reasoner */
    FlowTopologyGraph net_graph;
    flow_topology_build_codebase_graph(&net_graph);
    CHECK(flow_topology_attach_telemetry(&net_graph, "reload", 91.2,
                                         "QSBR Reclamation Queue Depth",
                                         42.0, 10.0, "epochs",
                                         "Extreme turnover rate stalling RCU reclamation") == 1);
    const FlowTopologyNode *peak = flow_topology_get_peak_hotspot(&net_graph);
    CHECK(peak != NULL);
    CHECK(strcmp(peak->name, "reload") == 0);
    CHECK(peak->hotspot_score == 91.2);

    char bottleneck_explanation[2048] = {0};
    CHECK(flowy_explain_bottleneck(&net_graph, bottleneck_explanation, sizeof(bottleneck_explanation)) == 1);
    CHECK(strstr(bottleneck_explanation, "reload") != NULL);
    CHECK(strstr(bottleneck_explanation, "91.2%") != NULL);
    CHECK(strstr(bottleneck_explanation, "QSBR") != NULL);

    /* 5. Test Natural Language Query: "系統現在效能卡在哪裡？為什麼？" */
    FlowyIntrospectiveAnswer ans_bottleneck;
    CHECK(flowy_query_codebase(&net_graph, "系統現在效能卡在哪裡？為什麼？", &ans_bottleneck) == 1);
    CHECK(strstr(ans_bottleneck.explanation, "reload") != NULL);
    CHECK(strstr(ans_bottleneck.explanation, "SUBCONSCIOUS NEURAL TELEMETRY") != NULL);

    /* 6. Test Timeline Rendering */
    flowy_print_decision_timeline(&logger, stdout);
    flowy_print_bottleneck_explanation(&net_graph, stdout);

    printf("DECISION_EXPLAIN_TEST=passed causal_reasoning=verified telemetry_explainability=verified bottleneck_reasoner=verified\n");
    return 0;
}
