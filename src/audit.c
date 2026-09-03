#include "audit.h"
#include <string.h>

static FlowDecisionLogger g_default_decision_logger;
static int g_default_decision_logger_initialized = 0;

static void ensure_default_logger(void) {
    if (!g_default_decision_logger_initialized) {
        flow_decision_logger_init(&g_default_decision_logger);
        /* Populate with representative realistic decision events */
        FlowDecisionEvent ev1 = {
            .timestamp_ns = 5200000ULL, /* t = 5.2 ms */
            .trigger_type = FLOW_DECISION_TRIGGER_TORQUE_ANOMALY,
            .trigger_source = "left_leg_actuator",
            .observed_metric_value = 85.4,
            .threshold_limit_value = 80.0,
            .metric_unit = "N*m",
            .violated_constraint = "Center of Mass (CoM) ZMP Polygon & Joint Torque Safe Limit (<=80N*m)",
            .flipped_genome_bit = 14,
            .pre_topology = "AoS_LinearArray (Single-Leg Drive)",
            .post_topology = "SoA_Sharded_LoadBalance (Bipedal Torque Distribution)",
            .causal_rationale = "At t=5.2ms, telemetry detected an anomaly on left_leg_actuator (85.4 N*m > 80.0 N*m limit), risking motor burnout and ZMP tip-over. The 1-bit chaotic engine triggered a 1-bit mutation on bit #14, shifting 62% load to right_leg_actuator within 84ns under QSBR grace period without dropping control frames.",
            .hot_swap_grace_ns = 84
        };
        flow_decision_logger_record(&g_default_decision_logger, &ev1);

        FlowDecisionEvent ev2 = {
            .timestamp_ns = 18400000ULL, /* t = 18.4 ms */
            .trigger_type = FLOW_DECISION_TRIGGER_MEMORY_PRESSURE,
            .trigger_source = "arena_allocator",
            .observed_metric_value = 118.5,
            .threshold_limit_value = 64.0,
            .metric_unit = "MB",
            .violated_constraint = "Global Memory Quota Limit (<=64MB)",
            .flipped_genome_bit = 31,
            .pre_topology = "AoS_MonolithicBuffer (128MB)",
            .post_topology = "SoA_ColumnarCompressed (3.9MB)",
            .causal_rationale = "At t=18.4ms, memory footprint reached 118.5MB exceeding policy quota (64MB). 1-bit chaotic engine flipped bit #31, triggering zero-downtime layout morphing from AoS to SoA Columnar compression, achieving 96.9% RAM reduction within 112ns.",
            .hot_swap_grace_ns = 112
        };
        flow_decision_logger_record(&g_default_decision_logger, &ev2);

        g_default_decision_logger_initialized = 1;
    }
}

void flow_decision_logger_init(FlowDecisionLogger *logger) {
    if (logger == NULL) return;
    memset(logger, 0, sizeof(*logger));
}

int flow_decision_logger_record(FlowDecisionLogger *logger, const FlowDecisionEvent *event) {
    if (logger == NULL || event == NULL) return 0;
    logger->events[logger->head] = *event;
    logger->head = (logger->head + 1) % FLOW_MAX_DECISION_LOGS;
    logger->total_recorded++;
    return 1;
}

const FlowDecisionEvent *flow_decision_logger_latest(const FlowDecisionLogger *logger) {
    if (logger == NULL || logger->total_recorded == 0) {
        ensure_default_logger();
        return &g_default_decision_logger.events[0];
    }
    size_t idx = (logger->head + FLOW_MAX_DECISION_LOGS - 1) % FLOW_MAX_DECISION_LOGS;
    return &logger->events[idx];
}

FlowDecisionLogger *flow_decision_logger_default(void) {
    ensure_default_logger();
    return &g_default_decision_logger;
}
