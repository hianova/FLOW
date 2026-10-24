#ifndef FLOW_TOPOLOGY_H
#define FLOW_TOPOLOGY_H
#include "flow.h"
#include "registry.h"
#include "bitspace.h"
#include "smt.h"
#include "token_ring.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#define FLOW_TOPOLOGY_MAX_NODES 256
#define FLOW_TOPOLOGY_MAX_EDGES 1024
typedef enum { FLOW_NODE_CORE_MODULE = 0, FLOW_NODE_PLUGIN = 1, FLOW_NODE_COMPONENT = 2, FLOW_NODE_INTENT_OP = 3, FLOW_NODE_DIMENSION = 4, FLOW_NODE_SHARD_GROUP = 5,
    FLOW_NODE_FVEC_EXPERIENCE = 6 } FlowNodeType;
typedef enum { FLOW_EDGE_CALLS = 0, FLOW_EDGE_USES = 1, FLOW_EDGE_IMPLEMENTS = 2, FLOW_EDGE_BINDS_DIMENSION = 3, FLOW_EDGE_DATA_FLOW = 4,
    FLOW_EDGE_SHARD_AFFINITY = 5, FLOW_EDGE_MEMORIALIZES = 6 } FlowEdgeType;
typedef struct { uint32_t id; FlowNodeType type; char name[64], module[64], role[128]; int is_core; uint32_t layer, loc, fan_in, fan_out;
    uint64_t capability_mask; double hotspot_score; char hotspot_metric[64]; double hotspot_raw_val, hotspot_threshold_val; char hotspot_unit[16], dynamic_symptom[128]; } FlowTopologyNode;
typedef struct { uint32_t from_id, to_id; FlowEdgeType type; double weight; char label[64]; } FlowTopologyEdge;
typedef struct { FlowTopologyNode nodes[FLOW_TOPOLOGY_MAX_NODES]; size_t node_count; FlowTopologyEdge edges[FLOW_TOPOLOGY_MAX_EDGES]; size_t edge_count; } FlowTopologyGraph;
typedef struct { size_t total_nodes, total_edges, core_nodes, plugin_nodes, intent_nodes, cross_layer_leaks, total_loc; char leak_details[512];
    double average_coupling, modularity_score; } FlowTopologyAuditReport;
void flow_topology_init(FlowTopologyGraph *graph);
uint32_t flow_topology_add_node(FlowTopologyGraph *graph, FlowNodeType type, const char *name, const char *module, int is_core, uint32_t layer);
int flow_topology_add_edge(FlowTopologyGraph *graph, uint32_t from_id, uint32_t to_id, FlowEdgeType type, double weight, const char *label);
int flow_topology_attach_telemetry(FlowTopologyGraph *graph, const char *node_name, double hotspot_score, const char *metric_name, double raw_val, double thresh_val, const char *unit, const char *symptom);
const FlowTopologyNode *flow_topology_get_peak_hotspot(const FlowTopologyGraph *graph);
int flow_topology_scan_workspace(FlowTopologyGraph *graph, const char *src_dir);
void flow_topology_build_codebase_graph(FlowTopologyGraph *graph);
void flow_topology_audit(const FlowTopologyGraph *graph, FlowTopologyAuditReport *report);

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

#define FLOW_ORCHESTRATOR_MAX_INTENTS 32
#define FLOW_ORCHESTRATOR_MAX_EPOCHS 64
typedef enum { FLOW_ABSORB_OK = 0, FLOW_ABSORB_MUTEX_CONFLICT = 1, FLOW_ABSORB_INVALID = 2, FLOW_ABSORB_ALREADY_ABSORBED = 3 } FlowAbsorbStatus;
typedef struct { char flow_name[FLOW_NAME], file_path[256]; FlowSpec spec; SemanticIR ir; uint64_t contract_hash, schema_hash; int active; } FlowAbsorbedIntent;
typedef struct { uint64_t epoch_id, timestamp_ns; double global_energy, entropy_score; char primary_component[64]; FlowPlanEnsemble ensemble;
    SearchResult search_result; FlowSMTProofAttestation smt_proof; size_t active_intent_count; uint64_t attractor_cycles; double lyapunov_delta_e; int attractor_converged; } FlowOrchestratorEpoch;
typedef struct FlowOrchestrator { char workspace_dir[256]; FlowAbsorbedIntent intents[FLOW_ORCHESTRATOR_MAX_INTENTS]; size_t intent_count;
    FlowOrchestratorEpoch epochs[FLOW_ORCHESTRATOR_MAX_EPOCHS]; size_t epoch_count; uint64_t current_epoch_id; FlowTopologyGraph topology_graph; SemanticIR unified_ir; int has_unified_ir; } FlowOrchestrator;
FlowOrchestrator *flow_orchestrator_create(const char *workspace_dir); void flow_orchestrator_destroy(FlowOrchestrator *orch);
FlowAbsorbStatus flow_orchestrator_absorb(FlowOrchestrator *orch, const char *spec_file, char *diag_msg, size_t diag_size);
int flow_orchestrator_anneal(FlowOrchestrator *orch, size_t iterations, uint32_t seed, FlowOrchestratorEpoch *epoch_out);
int flow_orchestrator_landscape(const FlowOrchestrator *orch, FILE *out); int flow_orchestrator_refactor_entropy(FlowOrchestrator *orch, double *entropy_delta_out);
int flow_orchestrator_time_travel(FlowOrchestrator *orch, FlowPlanTactic tactic, FlowPlan *plan_out); size_t flow_orchestrator_intent_count(const FlowOrchestrator *orch);
const char *flow_absorb_status_name(FlowAbsorbStatus status);
typedef struct { char hypothetical_description[128]; int original_memory_mb, hypothetical_memory_mb; char original_component[64], hypothetical_component[64];
    double original_latency_score, hypothetical_latency_score, original_energy, hypothetical_energy, throughput_delta_percent, qsbr_reclaim_freq_multiplier;
    char structural_collapse[128], recommendation[256]; int feasible; } FlowCounterfactualReport;
int flow_orchestrator_simulate_what_if(FlowOrchestrator *orch, int hypothetical_memory_mb, int hypothetical_top_n, int hypothetical_threads, FlowCounterfactualReport *report_out);
typedef struct { char conflict_summary[256], min_cut_dimension[64]; double current_bound, required_remediation_bound; char proposed_flow_patch[1024]; int can_auto_remediate; } FlowRemediationProposal;
int flow_orchestrator_synthesize_remediation(FlowOrchestrator *orch, const char *spec_file_a, const char *spec_file_b, FlowRemediationProposal *proposal_out);
typedef struct { uint64_t incident_id, timestamp_ns; char anomaly_cause[128], previous_topology[64], autonomous_action[128], new_topology[64]; FlowSMTProofAttestation smt_proof;
    uint64_t hot_swap_switch_ns; char human_narrative[512]; int hot_swap_success; } FlowAutopilotIncident;
typedef struct FlowAutopilotController { FlowOrchestrator *orch; struct FlowReloadContext *reload_ctx; FlowPMUTelemetry pmu_baseline; double thermal_drift_threshold;
    size_t incidents_count; FlowAutopilotIncident incident_history[16]; } FlowAutopilotController;
FlowAutopilotController *flow_autopilot_create(FlowOrchestrator *orch, struct FlowReloadContext *reload_ctx);
void flow_autopilot_destroy(FlowAutopilotController *ctrl);
int flow_autopilot_step(FlowAutopilotController *ctrl, const FlowPMUTelemetry *current_pmu, FlowAutopilotIncident *incident_out);
#endif

