#include "../cmd_dispatch.h"
#include "../flowy.h"
#include "../flowy_cli.h"
#include "../orchestrator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void cmd_topo_print_usage(FILE *out) { fprintf(out, "Usage: flowy topo <subcommand> [options...]\n\nSubcommands:\n"
                 "  absorb <spec.flow>       Absorb architectural specification\n"
                 "  anneal [specs...]        Run simulated annealing to solidify plan\n"
                 "  landscape                Print system energy & topology landscape\n"
                 "  refactor                 Calculate architectural entropy reduction\n"
                 "  morph [speed|memory]     Time-travel morph plan\n"
                 "  what-if [--memory <MB>]  Simulate counterfactual load scenario\n"
                 "  remediate <s1> <s2>      Synthesize remediation proposal for faults\n"
                 "  autopilot [spec]         Run closed-loop autonomous orchestration\n"
                 "  daemon [--nightly]       Run background continuous annealer daemon\n"); }
static void absorb_args_or_default(FlowOrchestrator *orch, int argc, char **argv, char *diag, size_t dsz) {
    for (int i = 2; i < argc; ++i) if (argv[i][0] != '-') flow_orchestrator_absorb(orch, argv[i], diag, dsz);
    if (flow_orchestrator_intent_count(orch) == 0) flow_orchestrator_absorb(orch, "examples/project.flow", diag, dsz); } int run_orchestrator_cmd(int argc, char **argv) {
    if (argc < 2) return EXIT_FAILURE; const char *cmd = (argv[1][0] == '-' && argv[1][1] == '-') ? argv[1] + 2 : argv[1];
    FlowOrchestrator *orch = flow_orchestrator_create("."); char diag[256] = {0}; int res = EXIT_SUCCESS; if (!strcmp(cmd, "shell")) {
        flow_orchestrator_absorb(orch, "examples/compiler.flow", diag, sizeof(diag)); flow_orchestrator_absorb(orch, "examples/project.flow", diag, sizeof(diag));
        res = flowy_interactive_loop(orch, stdin, stdout) ? EXIT_SUCCESS : EXIT_FAILURE; } else if (!strcmp(cmd, "absorb")) {
        if (argc < 3) { fprintf(stderr, "usage: flowy absorb <file.flow>\n"); res = EXIT_FAILURE; } else {
            FlowAbsorbStatus st = flow_orchestrator_absorb(orch, argv[2], diag, sizeof(diag)); printf("flow-orchestrator: [%s] %s\n", flow_absorb_status_name(st), diag);
            res = (st == FLOW_ABSORB_OK || st == FLOW_ABSORB_ALREADY_ABSORBED) ? EXIT_SUCCESS : EXIT_FAILURE; }
    } else if (!strcmp(cmd, "anneal") || !strcmp(cmd, "landscape")) { absorb_args_or_default(orch, argc, argv, diag, sizeof(diag)); FlowOrchestratorEpoch ep;
        int ok = flow_orchestrator_anneal(orch, !strcmp(cmd, "anneal") ? 200 : 100, 42, &ep);
        if (!strcmp(cmd, "anneal") && !ok) { fprintf(stderr, "flowy anneal: failed\n"); res = EXIT_FAILURE; } else { if (!strcmp(cmd, "anneal"))
                printf("[epoch_solidified] Epoch=#%llu Energy=%.4f Entropy=%.4f\n", (unsigned long long)ep.epoch_id, ep.global_energy, ep.entropy_score);
            flow_orchestrator_landscape(orch, stdout); } } else if (!strcmp(cmd, "refactor")) { flow_orchestrator_absorb(orch, "examples/project.flow", diag, sizeof(diag));
        FlowOrchestratorEpoch ep; flow_orchestrator_anneal(orch, 100, 42, &ep); double delta = 0.0; flow_orchestrator_refactor_entropy(orch, &delta);
        printf("[entropy_reduction] Delta=%.4f\n", delta); } else if (!strcmp(cmd, "morph")) { const char *ts = argc >= 3 ? argv[2] : "speed";
        FlowPlanTactic tactic = !strcmp(ts, "memory") ? FLOW_TACTIC_MEMORY : (!strcmp(ts, "balanced") ? FLOW_TACTIC_BALANCED : FLOW_TACTIC_SPEED);
        flow_orchestrator_absorb(orch, "examples/project.flow", diag, sizeof(diag)); FlowOrchestratorEpoch ep; flow_orchestrator_anneal(orch, 100, 42, &ep); FlowPlan tp;
        if (flow_orchestrator_time_travel(orch, tactic, &tp))
            printf("[state_time_travel] Morphed to '%s' LatencyScore=%.1f\n", flow_plan_tactic_name(tactic), tp.eval.latency_score);
    } else if (!strcmp(cmd, "what-if") || !strcmp(cmd, "whatif")) { int mem_mb = 32, top_n = 50, threads = 4; const char *spec = "examples/rank.flow";
        for (int i = 2; i < argc; ++i) { if (!strcmp(argv[i], "--memory") && i + 1 < argc) mem_mb = atoi(argv[++i]);
            else if (!strcmp(argv[i], "--top-n") && i + 1 < argc) top_n = atoi(argv[++i]); else if (!strcmp(argv[i], "--threads") && i + 1 < argc) threads = atoi(argv[++i]);
            else if (argv[i][0] != '-') spec = argv[i]; } flow_orchestrator_absorb(orch, spec, diag, sizeof(diag));
        FlowCounterfactualReport rep; flow_orchestrator_simulate_what_if(orch, mem_mb, top_n, threads, &rep); flowy_print_counterfactual_report(&rep, stdout);
    } else if (!strcmp(cmd, "remediate")) { FlowRemediationProposal prop;
        flow_orchestrator_synthesize_remediation(orch, argc >= 3 ? argv[2] : "examples/compiler.flow", argc >= 4 ? argv[3] : "examples/project.flow", &prop);
        flowy_print_remediation_proposal(&prop, stdout); } else if (!strcmp(cmd, "autopilot")) {
        flow_orchestrator_absorb(orch, argc >= 3 ? argv[2] : "examples/project.flow", diag, sizeof(diag)); FlowAutopilotController *ctrl = flow_autopilot_create(orch, NULL);
        FlowPMUTelemetry storm = { .cache_miss_rate = 0.148, .ipc = 0.82 }; FlowAutopilotIncident inc; flow_autopilot_step(ctrl, &storm, &inc);
        flowy_print_autopilot_incident(&inc, stdout); flow_autopilot_destroy(ctrl); } else if (!strcmp(cmd, "daemon")) {
        flow_orchestrator_absorb(orch, "examples/compiler.flow", diag, sizeof(diag)); flow_orchestrator_absorb(orch, "examples/project.flow", diag, sizeof(diag));
        printf("[started] Living Topology Orchestrator daemon active\n"); for (size_t c = 0; c < 3; ++c) {
            double delta = 0.0; flow_orchestrator_refactor_entropy(orch, &delta); FlowOrchestratorEpoch ep; flow_orchestrator_anneal(orch, 50, 42 + (uint32_t)c, &ep);
            printf("[cycle #%zu] Entropy=%.4f\n", c + 1, ep.entropy_score); } printf("[quiesced] Background continuous annealing completed.\n"); }
    flow_orchestrator_destroy(orch); return res; } int cmd_topo_run(int argc, char **argv) { if (argc < 2) { cmd_topo_print_usage(stderr); return EXIT_FAILURE; }
    const char *sub = (argv[1][0] == '-' && argv[1][1] == '-') ? argv[1] + 2 : argv[1];
    if (!strcmp(sub, "-h") || !strcmp(sub, "help")) { cmd_topo_print_usage(stdout); return EXIT_SUCCESS; } return run_orchestrator_cmd(argc, argv); }
