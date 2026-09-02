#include "flow.h"
#include "orchestrator.h"
#include "flowy.h"
#include "registry.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "autonomous-orchestration-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

int main(void) {
    printf("Starting Autonomous Level-5 Orchestration & Counterfactual What-If Test...\n");
    flow_registry_init();

    FlowOrchestrator *orch = flow_orchestrator_create(".");
    CHECK(orch != NULL);

    char diag[256] = {0};
    FlowAbsorbStatus st1 = flow_orchestrator_absorb(orch, "examples/compiler.flow", diag, sizeof(diag));
    CHECK(st1 == FLOW_ABSORB_OK);
    FlowAbsorbStatus st2 = flow_orchestrator_absorb(orch, "examples/project.flow", diag, sizeof(diag));
    CHECK(st2 == FLOW_ABSORB_OK);

    /* ========================================================================= */
    /* 1. Counterfactual "What-If" Architectural Sandbox Simulation              */
    /* ========================================================================= */
    {
        printf("  [1/3] Testing Counterfactual Simulation (\"What-If\" Engine)...\n");
        FlowCounterfactualReport report;
        int ok = flow_orchestrator_simulate_what_if(orch, 32, 50, 4, &report);
        CHECK(ok == 1);
        CHECK(report.original_memory_mb > report.hypothetical_memory_mb);
        CHECK(report.hypothetical_memory_mb == 32);
        CHECK(report.qsbr_reclaim_freq_multiplier >= 1.0);
        CHECK(strlen(report.structural_collapse) > 0);
        CHECK(strlen(report.recommendation) > 0);

        printf("        -> Scenario: %s\n", report.hypothetical_description);
        printf("        -> Layout:   %s -> %s\n", report.original_component, report.hypothetical_component);
        printf("        -> QSBR Multiplier: %.1fx, Throughput Delta: %+.1f%%\n",
               report.qsbr_reclaim_freq_multiplier, report.throughput_delta_percent);
        printf("        -> Recommendation: %s\n", report.recommendation);

        flowy_print_counterfactual_report(&report, stdout);
    }

    /* ========================================================================= */
    /* 2. Topological Synthesis & Auto-Remediation (Min-Cut Patch Synthesis)     */
    /* ========================================================================= */
    {
        printf("  [2/3] Testing Topological Synthesis & Auto-Remediation...\n");
        FlowRemediationProposal proposal;
        int ok = flow_orchestrator_synthesize_remediation(orch, "examples/compiler.flow", "examples/project.flow", &proposal);
        CHECK(ok == 1);
        CHECK(proposal.can_auto_remediate == 1);
        CHECK(strcmp(proposal.min_cut_dimension, "memory_limit_mb") == 0);
        CHECK(proposal.required_remediation_bound > proposal.current_bound);
        CHECK(strstr(proposal.proposed_flow_patch, "flow remediated_pipeline") != NULL);

        printf("        -> Min-Cut Bottleneck: %s (Relax from %.0f to %.0f)\n",
               proposal.min_cut_dimension, proposal.current_bound, proposal.required_remediation_bound);
        printf("        -> Synthesized Patch Length: %zu bytes\n", strlen(proposal.proposed_flow_patch));

        flowy_print_remediation_proposal(&proposal, stdout);
    }

    /* ========================================================================= */
    /* 3. Closed-Loop Autonomous Autopilot Controller (Level 5 Auto-Healing)     */
    /* ========================================================================= */
    {
        printf("  [3/3] Testing Level 5 Closed-Loop Autopilot Self-Healing Controller...\n");
        FlowAutopilotController *ctrl = flow_autopilot_create(orch, NULL);
        CHECK(ctrl != NULL);

        /* Case A: Nominal steady-state -> no incident triggered */
        FlowPMUTelemetry nominal = { .cache_miss_rate = 0.015, .ipc = 2.3 };
        FlowAutopilotIncident inc_nom;
        int triggered_nom = flow_autopilot_step(ctrl, &nominal, &inc_nom);
        CHECK(triggered_nom == 0);

        /* Case B: eBPF L3 Cache Storm Anomaly (miss rate 14.8%) */
        FlowPMUTelemetry storm = { .cache_miss_rate = 0.148, .ipc = 0.81 };
        FlowAutopilotIncident inc_storm;
        int triggered_storm = flow_autopilot_step(ctrl, &storm, &inc_storm);
        CHECK(triggered_storm == 1);
        CHECK(inc_storm.incident_id == 1);
        CHECK(strstr(inc_storm.anomaly_cause, "L3 Cache Storm") != NULL);
        CHECK(inc_storm.hot_swap_success == 1);
        CHECK(inc_storm.hot_swap_switch_ns < 1000); /* Sub-microsecond */
        CHECK(inc_storm.smt_proof.buffer_bounds_safety == FLOW_SMT_PROVEN_UNSAT);
        CHECK(inc_storm.smt_proof.memory_quota_bound == FLOW_SMT_PROVEN_UNSAT);
        CHECK(strlen(inc_storm.human_narrative) > 0);

        printf("        -> Incident #%llu Triggered: %s\n", (unsigned long long)inc_storm.incident_id, inc_storm.anomaly_cause);
        printf("        -> Live Hot-Swap Switch Latency: %llu ns\n", (unsigned long long)inc_storm.hot_swap_switch_ns);
        printf("        -> Formal Proof: %s\n", inc_storm.smt_proof.proof_summary);

        flowy_print_autopilot_incident(&inc_storm, stdout);
        flow_autopilot_destroy(ctrl);
    }

    flow_orchestrator_destroy(orch);
    printf("\nAUTONOMOUS_ORCHESTRATION_TEST=passed what_if=verified min_cut_remediation=verified autopilot_l5=verified\n");
    return 0;
}
