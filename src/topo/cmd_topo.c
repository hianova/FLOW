/* topo/cmd_topo.c
 * CLI handler for 'flowy topo' and legacy orchestrator subcommands:
 *   shell, absorb, anneal, landscape, refactor, morph, what-if,
 *   remediate, autopilot, daemon.
 *
 * Part of FLOW Living Architecture - Method A refactoring.
 * Extracted from src/flowy_main.c.
 */
#include "../cmd_dispatch.h"
#include "../flowy.h"
#include "../flowy_cli.h"
#include "../orchestrator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void cmd_topo_print_usage(FILE *out) {
    fprintf(out, "Usage: flowy topo <subcommand> [options...]\n\n");
    fprintf(out, "Subcommands:\n");
    fprintf(out, "  absorb <spec.flow>       Absorb architectural specification\n");
    fprintf(out, "  anneal [specs...]        Run simulated annealing to solidify plan\n");
    fprintf(out, "  landscape                Print system energy & topology landscape\n");
    fprintf(out, "  refactor                 Calculate architectural entropy reduction\n");
    fprintf(out, "  morph [speed|memory]     Time-travel morph plan\n");
    fprintf(out, "  what-if [--memory <MB>]  Simulate counterfactual load scenario\n");
    fprintf(out, "  remediate <s1> <s2>      Synthesize remediation proposal for faults\n");
    fprintf(out, "  autopilot [spec]         Run closed-loop autonomous orchestration\n");
    fprintf(out, "  daemon [--nightly]       Run background continuous annealer daemon\n");
}

int run_orchestrator_cmd(int argc, char **argv) {
    if (argc < 2) return EXIT_FAILURE;
    const char *cmd = argv[1];
    if (cmd[0] == '-' && cmd[1] == '-') cmd += 2;
    if (strcmp(cmd, "shell") == 0 && argc < 2) cmd = "shell";

    FlowOrchestrator *orch = flow_orchestrator_create(".");
    char diag[256] = {0};
    int res = EXIT_SUCCESS;

    if (strcmp(cmd, "shell") == 0) {
        flow_orchestrator_absorb(orch, "examples/compiler.flow", diag, sizeof(diag));
        flow_orchestrator_absorb(orch, "examples/project.flow", diag, sizeof(diag));
        res = flowy_interactive_loop(orch, stdin, stdout) ? EXIT_SUCCESS : EXIT_FAILURE;
    } else if (strcmp(cmd, "absorb") == 0) {
        if (argc < 3) {
            fprintf(stderr, "usage: flowy absorb <file.flow>\n");
            res = EXIT_FAILURE;
        } else {
            FlowAbsorbStatus st = flow_orchestrator_absorb(orch, argv[2], diag, sizeof(diag));
            printf("flow-orchestrator: [%s] %s\n", flow_absorb_status_name(st), diag);
            res = (st == FLOW_ABSORB_OK || st == FLOW_ABSORB_ALREADY_ABSORBED) ? EXIT_SUCCESS : EXIT_FAILURE;
        }
    } else if (strcmp(cmd, "anneal") == 0) {
        for (int i = 2; i < argc; ++i) {
            if (argv[i][0] != '-') flow_orchestrator_absorb(orch, argv[i], diag, sizeof(diag));
        }
        if (flow_orchestrator_intent_count(orch) == 0) flow_orchestrator_absorb(orch, "examples/project.flow", diag, sizeof(diag));
        FlowOrchestratorEpoch epoch;
        if (!flow_orchestrator_anneal(orch, 200, 42, &epoch)) {
            fprintf(stderr, "flowy anneal: failed\n");
            res = EXIT_FAILURE;
        } else {
            printf("[epoch_solidified] Epoch=#%llu Energy=%.4f Entropy=%.4f\n", (unsigned long long)epoch.epoch_id, epoch.global_energy, epoch.entropy_score);
            flow_orchestrator_landscape(orch, stdout);
        }
    } else if (strcmp(cmd, "landscape") == 0) {
        for (int i = 2; i < argc; ++i) {
            if (argv[i][0] != '-') flow_orchestrator_absorb(orch, argv[i], diag, sizeof(diag));
        }
        if (flow_orchestrator_intent_count(orch) == 0) flow_orchestrator_absorb(orch, "examples/project.flow", diag, sizeof(diag));
        FlowOrchestratorEpoch epoch;
        flow_orchestrator_anneal(orch, 100, 42, &epoch);
        flow_orchestrator_landscape(orch, stdout);
    } else if (strcmp(cmd, "refactor") == 0) {
        flow_orchestrator_absorb(orch, "examples/project.flow", diag, sizeof(diag));
        FlowOrchestratorEpoch epoch;
        flow_orchestrator_anneal(orch, 100, 42, &epoch);
        double delta = 0.0;
        flow_orchestrator_refactor_entropy(orch, &delta);
        printf("[entropy_reduction] Delta=%.4f\n", delta);
    } else if (strcmp(cmd, "morph") == 0) {
        const char *tactic_str = argc >= 3 ? argv[2] : "speed";
        FlowPlanTactic tactic = FLOW_TACTIC_SPEED;
        if (strcmp(tactic_str, "memory") == 0) tactic = FLOW_TACTIC_MEMORY;
        else if (strcmp(tactic_str, "balanced") == 0) tactic = FLOW_TACTIC_BALANCED;
        flow_orchestrator_absorb(orch, "examples/project.flow", diag, sizeof(diag));
        FlowOrchestratorEpoch epoch;
        flow_orchestrator_anneal(orch, 100, 42, &epoch);
        FlowPlan target_plan;
        if (flow_orchestrator_time_travel(orch, tactic, &target_plan)) {
            printf("[state_time_travel] Morphed to '%s' LatencyScore=%.1f\n", flow_plan_tactic_name(tactic), target_plan.eval.latency_score);
        }
    } else if (strcmp(cmd, "what-if") == 0 || strcmp(cmd, "whatif") == 0) {
        int mem_mb = 32, top_n = 50, threads = 4;
        const char *spec = NULL;
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "--memory") == 0 && i + 1 < argc) mem_mb = atoi(argv[++i]);
            else if (strcmp(argv[i], "--top-n") == 0 && i + 1 < argc) top_n = atoi(argv[++i]);
            else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) threads = atoi(argv[++i]);
            else if (argv[i][0] != '-') spec = argv[i];
        }
        if (spec) flow_orchestrator_absorb(orch, spec, diag, sizeof(diag));
        else flow_orchestrator_absorb(orch, "examples/rank.flow", diag, sizeof(diag));
        FlowCounterfactualReport report;
        flow_orchestrator_simulate_what_if(orch, mem_mb, top_n, threads, &report);
        flowy_print_counterfactual_report(&report, stdout);
    } else if (strcmp(cmd, "remediate") == 0) {
        const char *spec1 = argc >= 3 ? argv[2] : "examples/compiler.flow";
        const char *spec2 = argc >= 4 ? argv[3] : "examples/project.flow";
        FlowRemediationProposal proposal;
        flow_orchestrator_synthesize_remediation(orch, spec1, spec2, &proposal);
        flowy_print_remediation_proposal(&proposal, stdout);
    } else if (strcmp(cmd, "autopilot") == 0) {
        const char *spec = argc >= 3 ? argv[2] : "examples/project.flow";
        flow_orchestrator_absorb(orch, spec, diag, sizeof(diag));
        FlowAutopilotController *ctrl = flow_autopilot_create(orch, NULL);
        FlowPMUTelemetry storm = { .cache_miss_rate = 0.148, .ipc = 0.82 };
        FlowAutopilotIncident inc;
        flow_autopilot_step(ctrl, &storm, &inc);
        flowy_print_autopilot_incident(&inc, stdout);
        flow_autopilot_destroy(ctrl);
    } else if (strcmp(cmd, "daemon") == 0) {
        flow_orchestrator_absorb(orch, "examples/compiler.flow", diag, sizeof(diag));
        flow_orchestrator_absorb(orch, "examples/project.flow", diag, sizeof(diag));
        printf("[started] Living Topology Orchestrator daemon active\n");
        for (size_t c = 0; c < 3; ++c) {
            double delta = 0.0;
            flow_orchestrator_refactor_entropy(orch, &delta);
            FlowOrchestratorEpoch ep;
            flow_orchestrator_anneal(orch, 50, 42 + (uint32_t)c, &ep);
            printf("[cycle #%zu] Entropy=%.4f\n", c + 1, ep.entropy_score);
        }
        printf("[quiesced] Background continuous annealing completed.\n");
    }

    flow_orchestrator_destroy(orch);
    return res;
}

int cmd_topo_run(int argc, char **argv) {
    if (argc < 2) {
        cmd_topo_print_usage(stderr);
        return EXIT_FAILURE;
    }
    const char *subcmd = argv[1];
    if (subcmd[0] == '-' && subcmd[1] == '-') subcmd += 2;
    if (strcmp(subcmd, "-h") == 0 || strcmp(subcmd, "help") == 0) {
        cmd_topo_print_usage(stdout);
        return EXIT_SUCCESS;
    }
    /* Delegate to run_orchestrator_cmd which handles the actual subcommands */
    return run_orchestrator_cmd(argc, argv);
}
