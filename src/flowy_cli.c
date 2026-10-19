#include "flowy_cli.h"
#include "flowy.h"
#include "flow_jet.h"
#include "cubical_hott.h"
#include "audit.h"
#include "orchestrator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
static FlowLanguage g_current_language = FLOW_LANG_ZH; static int g_language_initialized = 0;
void flowy_set_language(FlowLanguage lang) { g_current_language = lang; g_language_initialized = 1; } FlowLanguage flowy_get_language(void) {
    if (!g_language_initialized) { g_current_language = flowy_detect_system_language(); g_language_initialized = 1; } return g_current_language; }
FlowLanguage flowy_parse_language(const char *s) {
    return (s && (!strcasecmp(s, "en") || !strcasecmp(s, "en_US") || !strcasecmp(s, "en-US") || !strcasecmp(s, "english") || !strcasecmp(s, "eng"))) ? FLOW_LANG_EN : FLOW_LANG_ZH;
} FlowLanguage flowy_detect_system_language(void) { const char *fl = getenv("FLOWY_LANG"); if (fl && fl[0]) return flowy_parse_language(fl);
    const char *el = getenv("LC_ALL"); if (!el) el = getenv("LANG"); if (el) { if (strcasestr(el, "zh") || strcasestr(el, "tw") || strcasestr(el, "cn")) return FLOW_LANG_ZH;
        if (strcasestr(el, "en") || strstr(el, "C") || strstr(el, "POSIX")) return FLOW_LANG_EN; } return FLOW_LANG_ZH; }
const char *flowy_language_name(FlowLanguage lang) { return (lang == FLOW_LANG_EN) ? "English (en)" : "Traditional Chinese / 繁體中文 (zh)"; }
void flowy_print_answer(const FlowyIntrospectiveAnswer *answer, FILE *out) { if (answer && out) fprintf(out, "\n%s\n", answer->explanation); }
void flowy_print_decision_explanation(const FlowDecisionEvent *event, FILE *out) { if (!event || !out) return;
    char buf[2048] = {0}; flowy_explain_decision(event, buf, sizeof(buf)); fprintf(out, "\n%s\n", buf); }
void flowy_print_decision_timeline(const FlowDecisionLogger *logger, FILE *out) { if (!out) return;
    const FlowDecisionLogger *l = (logger && logger->total_recorded > 0) ? logger : flow_decision_logger_default();
    fprintf(out, "Time(ms) | Trigger | Observed/Limit | Topology Morph | QSBR(ns)\n");
    size_t count = l->total_recorded > FLOW_MAX_DECISION_LOGS ? FLOW_MAX_DECISION_LOGS : l->total_recorded; for (size_t i = 0; i < count; ++i) {
        const FlowDecisionEvent *ev = &l->events[i];
        fprintf(out, "%.2f | %s | %.1f/%.1f %s | Bit#%u->%s | %llu\n", (double)ev->timestamp_ns / 1e6, ev->trigger_source, ev->observed_metric_value, ev->threshold_limit_value, ev->metric_unit, ev->flipped_genome_bit, ev->post_topology, (unsigned long long)ev->hot_swap_grace_ns);
    } } void flowy_print_bottleneck_explanation(const FlowTopologyGraph *graph, FILE *out) { if (!out) return;
    char buf[2048] = {0}; flowy_explain_bottleneck(graph, buf, sizeof(buf)); fprintf(out, "\n%s\n", buf); }
void flowy_print_counterfactual_report(const FlowCounterfactualReport *r, FILE *out) {
    if (r && out) fprintf(out, "[Counterfactual What-If] %s | Mem: %d->%dMB | Comp: %s->%s | Latency: %.2f->%.2f | Energy: %.2f->%.2f | Collapse: %s | Rec: %s\n",
        r->hypothetical_description, r->original_memory_mb, r->hypothetical_memory_mb, r->original_component, r->hypothetical_component, r->original_latency_score, r->hypothetical_latency_score, r->original_energy, r->hypothetical_energy, r->structural_collapse, r->recommendation);
} void flowy_print_remediation_proposal(const FlowRemediationProposal *p, FILE *out) {
    if (p && out) fprintf(out, "[SMT Remediation] %s | Min-Cut: %s (%.1f -> %.1f MB)\n%s", p->conflict_summary, p->min_cut_dimension, p->current_bound, p->required_remediation_bound, p->proposed_flow_patch);
} void flowy_print_autopilot_incident(const FlowAutopilotIncident *inc, FILE *out) {
    if (inc && out) fprintf(out, "[Autopilot Incident #%llu] %s | %s -> %s | Action: %s | QSBR: %lluns | SMT: %s\n  \"%s\"\n",
        (unsigned long long)inc->incident_id, inc->anomaly_cause, inc->previous_topology, inc->new_topology, inc->autonomous_action, (unsigned long long)inc->hot_swap_switch_ns, inc->smt_proof.proof_summary, inc->human_narrative);
} int flowy_interactive_loop(FlowOrchestrator *orch, FILE *in, FILE *out) { if (!in || !out) return 0; FlowTopologyGraph graph; flow_topology_build_codebase_graph(&graph);
    fprintf(out, "FLOW Introspective Reasoner (Commands: what-if, remediate, autopilot, why, bottleneck, timeline, list, exit)\n"); char line_buf[512]; while (1) {
        fprintf(out, "FLOW-Query > "); fflush(out); if (!fgets(line_buf, sizeof(line_buf), in)) break; size_t len = strlen(line_buf);
        while (len > 0 && (line_buf[len - 1] == '\n' || line_buf[len - 1] == '\r')) line_buf[--len] = '\0'; if (!len) continue;
        if (!strcmp(line_buf, "exit") || !strcmp(line_buf, "quit")) break;
        if (strstr(line_buf, "what-if") || strstr(line_buf, "what if")) { FlowCounterfactualReport r; flow_orchestrator_simulate_what_if(orch, 32, 50, 4, &r); flowy_print_counterfactual_report(&r, out); continue; }
        if (strstr(line_buf, "remediate")) { FlowRemediationProposal p; flow_orchestrator_synthesize_remediation(orch, "examples/compiler.flow", "examples/project.flow", &p); flowy_print_remediation_proposal(&p, out); continue; }
        if (strstr(line_buf, "autopilot")) { FlowAutopilotController *ctrl = flow_autopilot_create(orch, NULL);
            FlowPMUTelemetry storm = { .cache_miss_rate = 0.148, .ipc = 0.8 }; FlowAutopilotIncident inc;
            flow_autopilot_step(ctrl, &storm, &inc); flowy_print_autopilot_incident(&inc, out); flow_autopilot_destroy(ctrl); continue; }
        if (!strcmp(line_buf, "why")) { flowy_print_decision_explanation(flow_decision_logger_latest(NULL), out); continue; }
        if (!strcmp(line_buf, "bottleneck")) { flowy_print_bottleneck_explanation(&graph, out); continue; }
        if (!strcmp(line_buf, "timeline")) { flowy_print_decision_timeline(NULL, out); continue; } if (!strcmp(line_buf, "list")) {
            for (size_t i = 0; i < graph.node_count; ++i) fprintf(out, "  * [%-14s] (Layer %u) %s [cubical=0x%016llx]\n", graph.nodes[i].name, graph.nodes[i].layer, graph.nodes[i].role, (unsigned long long)graph.nodes[i].capability_mask);
            continue; } FlowyIntrospectiveAnswer ans; flowy_query_codebase(&graph, line_buf, &ans); flowy_print_answer(&ans, out); } return 1; }
void flowy_print_jet_inspection(const struct FlowJet *jet, FILE *out) { if (!jet || !out) return;
    uint32_t dim = jet->header.vector_dim ? jet->header.vector_dim : FLOW_JET_STANDARD_DIM, kdim = jet->header.koopman_dim ? jet->header.koopman_dim : FLOW_JET_STANDARD_KOOPMAN_DIM;
    double tr_k = 0.0; for (uint32_t i = 0; i < kdim; ++i) tr_k += jet->payload.koopman_matrix[i][i];
    fprintf(out, "[.fjet] %s (%s) | Intent: %s | Dim: %u | Energy H: %.4f | Tr(K): %+.4f (%s) | SMT: %s\n",
            jet->header.id, jet->header.name, jet->header.trigger_intent, dim, jet->header.hamiltonian_energy, tr_k, tr_k <= 0.0 ? "DISSIPATIVE STABLE" : "EXPANSIVE", jet->header.smt_signature);
    for (uint32_t i = 0; i < (dim < 8 ? dim : 8); ++i) fprintf(out, "  #%02u: q=%+10.5f p=%+10.5f a=%+10.5f I_mem=%+10.5f\n", i, jet->payload.q[i], jet->payload.p[i], jet->payload.a[i], jet->memory_integral[i]);
} void flowy_render_phase_portrait(const struct FlowJet *jet, uint32_t dim_x, uint32_t dim_y, int steps, double dt, FILE *out) { if (!jet || !out) return;
    FlowJet sim = *jet; int n = (steps > 0 && steps <= 128) ? steps : 32; double step_dt = dt > 0.0 ? dt : 0.02;
    fprintf(out, "[Phase Orbit q[%u] vs p[%u] (%d steps @ dt=%.3f)]\n", dim_x, dim_y, n, step_dt); for (int i = 0; i < n; ++i) {
        if (i == 0 || i == n - 1 || i % 8 == 0) fprintf(out, "  step %02d: q[%u]=%+8.4f, p[%u]=%+8.4f, H=%.6f\n", i, dim_x, sim.payload.q[dim_x], dim_y, sim.payload.p[dim_y], flow_jet_hamiltonian(&sim));
        flow_jet_symplectic_step(&sim, step_dt); } } int flowy_jet_simulate_run(const struct FlowJet *jet, int steps, double dt, FILE *out) { if (!jet || !out) return 0;
    FlowJet sim = *jet; int n = steps > 0 ? steps : 20; double step_dt = dt > 0.0 ? dt : 0.01, h0 = flow_jet_hamiltonian(&sim);
    for (int s = 0; s < n; ++s) flow_jet_symplectic_step(&sim, step_dt); double h1 = flow_jet_hamiltonian(&sim);
    fprintf(out, "[Symplectic Sim] %d steps @ dt=%.4f | H_init=%.6f -> H_final=%.6f (Drift=%.4f%%)\n", n, step_dt, h0, h1, fabs(h1 - h0) / (h0 > 1e-9 ? h0 : 1.0) * 100.0);
    return 1; } int flowy_jet_learn_demo(struct FlowJet *jet, int sample_count, FILE *out) { if (!jet || !out) return 0; int n = sample_count > 0 ? sample_count : 50;
    uint32_t kdim = jet->header.koopman_dim ? jet->header.koopman_dim : FLOW_JET_STANDARD_KOOPMAN_DIM; FlowJetStreamingEDMD edmd; flow_jet_edmd_init(&edmd, kdim, 0.98);
    double x0[FLOW_JET_STANDARD_KOOPMAN_DIM] = {1.0, 0.5, -0.3, 0.2}, x1[FLOW_JET_STANDARD_KOOPMAN_DIM]; for (int s = 0; s < n; ++s) {
        for (uint32_t i = 0; i < kdim; ++i) x1[i] = 0.95 * x0[i] + 0.02 * sin((double)(s + i)); flow_jet_edmd_update(&edmd, x0, x1, 0.01);
        memcpy(x0, x1, sizeof(double) * kdim); } int ok = flow_jet_apply_learned_koopman(jet, &edmd);
    fprintf(out, "[Koopman EDMD] Observed %d samples (dim=%u) | Applied=%s\n", n, kdim, ok ? "YES" : "NO"); return 1; } void flowy_print_cubical_topos_report(FILE *out) {
    if (!out) return; FlowCubicalTransitionReport rep = {0}; flow_cubical_decode_transition(0x0010ULL, 0x0014ULL, 2, &rep);
    uint8_t chi = flow_topos_classify_subobject(0x0014ULL, 0x00FFULL);
    fprintf(out, "[Cubical HoTT & Topos Sieve] 64-D De Morgan Cube I^64 | Topos Classifier chi=0x%02x | Sample Transition Bit#%u (%s): %s\n",
            (unsigned)chi, rep.flipped_axis, rep.axis_info ? rep.axis_info->axis_name : "axis", rep.is_kan_homotopic ? "KAN_FILLED" : "OBSTRUCTED"); }
