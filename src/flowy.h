#ifndef FLOW_FLOWY_H
#define FLOW_FLOWY_H

#include "flow.h"
#include "topology.h"
#include "orchestrator.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    const char *module_id;
    const char *title;
    const char *header_file;
    const char *source_file;
    uint32_t layer; /* 0=Core, 1=Interface, 2=Plugin */
    const char *responsibilities;
    const char *algorithmic_guarantee;
    const char *memory_concurrency_model;
    const char *key_apis;
    const char *keywords;
} FlowModuleKnowledge;

typedef struct {
    const FlowModuleKnowledge *primary_module;
    const FlowModuleKnowledge *related_modules[4];
    size_t related_count;
    char query[256];
    char explanation[2048];
    uint32_t matched_score;
} FlowyIntrospectiveAnswer;

/* ========================================================================= */
/* Real-Time Telemetry & Deterministic Decision Explanation Engine           */
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

/* Introspective Knowledge Base */
size_t flowy_knowledge_count(void);
const FlowModuleKnowledge *flowy_knowledge_at(size_t index);
const FlowModuleKnowledge *flowy_knowledge_lookup(const char *module_id);
int flowy_register_dynamic_module(const FlowModuleKnowledge *knowledge);

/* Deterministic Semantic Query & Topological Reasoner */
int flowy_query_codebase(const FlowTopologyGraph *graph,
                         const char *query_text,
                         FlowyIntrospectiveAnswer *answer_out);

void flowy_print_answer(const FlowyIntrospectiveAnswer *answer, FILE *out);

/* Real-Time Decision Causal Explanation */
void flow_decision_logger_init(FlowDecisionLogger *logger);
int flow_decision_logger_record(FlowDecisionLogger *logger, const FlowDecisionEvent *event);
const FlowDecisionEvent *flow_decision_logger_latest(const FlowDecisionLogger *logger);
void flowy_explain_decision(const FlowDecisionEvent *event, char *buf_out, size_t max_len);
void flowy_print_decision_explanation(const FlowDecisionEvent *event, FILE *out);
void flowy_print_decision_timeline(const FlowDecisionLogger *logger, FILE *out);

/* Subconscious Neural Telemetry Reasoning (Bottleneck / Hotspot Reasoner) */
int flowy_explain_bottleneck(const FlowTopologyGraph *graph, char *buf_out, size_t max_len);
void flowy_print_bottleneck_explanation(const FlowTopologyGraph *graph, FILE *out);

/* Interactive Loop */
int flowy_interactive_loop(FlowOrchestrator *orch, FILE *in, FILE *out);

/* Counterfactual "What-If" Simulation Formatter */
void flowy_print_counterfactual_report(const FlowCounterfactualReport *report, FILE *out);

/* Topological Auto-Remediation Formatter */
void flowy_print_remediation_proposal(const FlowRemediationProposal *proposal, FILE *out);

/* Closed-Loop Level 5 Autopilot Incident Formatter */
void flowy_print_autopilot_incident(const FlowAutopilotIncident *incident, FILE *out);

/* ========================================================================= */
/* Level 5 Autonomy Crucible Contest Engine                                  */
/* ========================================================================= */

typedef struct {
    int stage1_smt_rejected;
    char stage1_rejection_log[256];

    int stage2_jit_vetoed;
    char stage2_jit_log[256];
    char stage2_routing_log[256];

    int stage3_hotswap_success;
    uint64_t stage3_latency_ms;
    uint64_t dropped_requests;
    int oom_killer_triggered;
    double energy_delta;
    char stage3_narrative_log[512];

    int stage4_recovery_success;
    char stage4_recovery_log[256];
} FlowyCrucibleResult;

int flowy_crucible_run(FlowyCrucibleResult *result_out, FILE *log_stream);

#endif
