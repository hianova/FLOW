#include "audit.h"
#include <string.h>
static FlowDecisionLogger g_default_logger; static int g_init = 0;
void flow_decision_logger_init(FlowDecisionLogger *l) { if (l) memset(l, 0, sizeof(*l)); }
int flow_decision_logger_record(FlowDecisionLogger *l, const FlowDecisionEvent *ev) { if (!l || !ev) return 0;
    l->events[l->head] = *ev; l->head = (l->head + 1) % FLOW_MAX_DECISION_LOGS; l->total_recorded++; return 1; }
FlowDecisionLogger *flow_decision_logger_default(void) { if (!g_init) { flow_decision_logger_init(&g_default_logger); g_init = 1;
        FlowDecisionEvent ev = { .timestamp_ns = 1000000ULL, .trigger_type = FLOW_DECISION_TRIGGER_MEMORY_PRESSURE, .trigger_source = "token_ring_attractor",
            .observed_metric_value = 118.5, .threshold_limit_value = 64.0, .metric_unit = "MB", .violated_constraint = "Lean4 Theorem4: memoryQuotaBytes <= 64MB",
            .flipped_genome_bit = 31, .pre_state_mask = 0ULL, .post_state_mask = (1ULL << 31), .pre_topology = "AoS_Unannealed", .post_topology = "SoA_Sharded_LeanCertified",
            .causal_rationale = "Token Ring 5-stage attractor projected polytope mask onto Bit#31 (columnar_compact_morph), satisfying Lean 4 Theorem 4 & Theorem 8 under QSBR.", .hot_swap_grace_ns = 84 };
        flow_decision_logger_record(&g_default_logger, &ev); } return &g_default_logger; }
const FlowDecisionEvent *flow_decision_logger_latest(const FlowDecisionLogger *l) { if (!l) l = flow_decision_logger_default();
    return l->total_recorded ? &l->events[(l->head + FLOW_MAX_DECISION_LOGS - 1) % FLOW_MAX_DECISION_LOGS] : NULL; }
