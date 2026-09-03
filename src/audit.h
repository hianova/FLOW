#ifndef FLOW_AUDIT_H
#define FLOW_AUDIT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* ========================================================================= */
/* Global Decision Audit Logger & Real-Time Telemetry Event Infrastructure   */
/* ========================================================================= */

typedef enum {
    FLOW_DECISION_TRIGGER_NONE = 0,
    FLOW_DECISION_TRIGGER_TORQUE_ANOMALY = 1,
    FLOW_DECISION_TRIGGER_ZMP_INSTABILITY = 2,
    FLOW_DECISION_TRIGGER_MEMORY_PRESSURE = 3,
    FLOW_DECISION_TRIGGER_CACHE_MISS_SPIKE = 4,
    FLOW_DECISION_TRIGGER_SMT_COUNTEREXAMPLE = 5,
    FLOW_DECISION_TRIGGER_THERMAL_SHOCK = 6,
    FLOW_DECISION_TRIGGER_GOLDEN_FALLBACK = 7,
    FLOW_DECISION_TRIGGER_STRAGGLER_QUARANTINE = 8
} FlowDecisionTriggerType;

typedef struct {
    uint64_t timestamp_ns;
    FlowDecisionTriggerType trigger_type;
    char trigger_source[64];       /* e.g., "left_leg_motor", "arena_allocator", "pmu_l3_cache" */
    double observed_metric_value;  /* e.g., 85.4 N*m */
    double threshold_limit_value;  /* e.g., 80.0 N*m */
    char metric_unit[16];          /* e.g., "N*m", "MB", "miss_rate" */
    char violated_constraint[128]; /* e.g., "Center of Mass (CoM) & Joint Torque Safe Limit" */
    uint32_t flipped_genome_bit;   /* e.g., 14 */
    char pre_topology[64];         /* e.g., "AoS_LinearArray" */
    char post_topology[64];        /* e.g., "SoA_Sharded_LoadBalance" */
    char causal_rationale[512];    /* Deterministic explanation of WHY the transition occurred */
    uint64_t hot_swap_grace_ns;    /* e.g., 84 ns under QSBR */
} FlowDecisionEvent;

#define FLOW_MAX_DECISION_LOGS 64

typedef struct {
    FlowDecisionEvent events[FLOW_MAX_DECISION_LOGS];
    size_t head;
    size_t total_recorded;
} FlowDecisionLogger;

void flow_decision_logger_init(FlowDecisionLogger *logger);
int flow_decision_logger_record(FlowDecisionLogger *logger, const FlowDecisionEvent *event);
const FlowDecisionEvent *flow_decision_logger_latest(const FlowDecisionLogger *logger);
FlowDecisionLogger *flow_decision_logger_default(void);

#endif /* FLOW_AUDIT_H */
