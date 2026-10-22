#ifndef FLOW_AUDIT_H
#define FLOW_AUDIT_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
typedef enum { FLOW_DECISION_TRIGGER_NONE = 0, FLOW_DECISION_TRIGGER_TORQUE_ANOMALY = 1, FLOW_DECISION_TRIGGER_ZMP_INSTABILITY = 2, FLOW_DECISION_TRIGGER_MEMORY_PRESSURE = 3,
    FLOW_DECISION_TRIGGER_CACHE_MISS_SPIKE = 4, FLOW_DECISION_TRIGGER_SMT_COUNTEREXAMPLE = 5, FLOW_DECISION_TRIGGER_THERMAL_SHOCK = 6,
    FLOW_DECISION_TRIGGER_GOLDEN_FALLBACK = 7, FLOW_DECISION_TRIGGER_STRAGGLER_QUARANTINE = 8 } FlowDecisionTriggerType;
typedef struct { uint64_t timestamp_ns; FlowDecisionTriggerType trigger_type; char trigger_source[64]; double observed_metric_value, threshold_limit_value;
    char metric_unit[16], violated_constraint[128]; uint32_t flipped_genome_bit; uint64_t pre_state_mask, post_state_mask;
    char pre_topology[64], post_topology[64], causal_rationale[512]; uint64_t hot_swap_grace_ns; } FlowDecisionEvent;
#define FLOW_MAX_DECISION_LOGS 64
typedef struct { FlowDecisionEvent events[FLOW_MAX_DECISION_LOGS]; size_t head, total_recorded; } FlowDecisionLogger;
void flow_decision_logger_init(FlowDecisionLogger *logger); int flow_decision_logger_record(FlowDecisionLogger *logger, const FlowDecisionEvent *event);
const FlowDecisionEvent *flow_decision_logger_latest(const FlowDecisionLogger *logger); FlowDecisionLogger *flow_decision_logger_default(void);
#endif /* FLOW_AUDIT_H */
