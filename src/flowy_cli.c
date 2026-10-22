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
static FlowLanguage g_lang = FLOW_LANG_ZH;
void flowy_set_language(FlowLanguage lang) { g_lang = lang; }
FlowLanguage flowy_get_language(void) { return g_lang; }
FlowLanguage flowy_parse_language(const char *s) { return (s && (!strncasecmp(s, "en", 2))) ? FLOW_LANG_EN : FLOW_LANG_ZH; }
FlowLanguage flowy_detect_system_language(void) { const char *fl = getenv("FLOWY_LANG"); return fl ? flowy_parse_language(fl) : FLOW_LANG_ZH; }
const char *flowy_language_name(FlowLanguage l) { return (l == FLOW_LANG_EN) ? "English (en)" : "Traditional Chinese (zh)"; }
void flowy_print_answer(const FlowyIntrospectiveAnswer *a, FILE *out) { if (a && out) fprintf(out, "\n%s\n", a->explanation); }
void flowy_print_decision_explanation(const FlowDecisionEvent *ev, FILE *out) { if (!ev || !out) return;
    char buf[1024] = {0}; flowy_explain_decision(ev, buf, sizeof(buf)); fprintf(out, "\n%s\n", buf); }
void flowy_print_decision_timeline(const FlowDecisionLogger *logger, FILE *out) { if (!out) return;
    const FlowDecisionLogger *l = (logger && logger->total_recorded) ? logger : flow_decision_logger_default();
    for (size_t i = 0; i < l->total_recorded && i < FLOW_MAX_DECISION_LOGS; ++i) { const FlowDecisionEvent *ev = &l->events[i];
        fprintf(out, "%.2fms | %s | %.1f/%.1f %s | Bit#%u->%s | %lluns\n", (double)ev->timestamp_ns / 1e6, ev->trigger_source, ev->observed_metric_value, ev->threshold_limit_value, ev->metric_unit, ev->flipped_genome_bit, ev->post_topology, (unsigned long long)ev->hot_swap_grace_ns); } }
void flowy_print_bottleneck_explanation(const FlowTopologyGraph *g, FILE *out) { if (!out) return;
    char buf[1024] = {0}; flowy_explain_bottleneck(g, buf, sizeof(buf)); fprintf(out, "\n%s\n", buf); }
void flowy_print_counterfactual_report(const FlowCounterfactualReport *r, FILE *out) {
    if (r && out) fprintf(out, "[Counterfactual What-If] %s | Mem: %d->%dMB | Comp: %s->%s | Latency: %.2f->%.2f | Energy: %.2f->%.2f | %s\n",
        r->hypothetical_description, r->original_memory_mb, r->hypothetical_memory_mb, r->original_component, r->hypothetical_component, r->original_latency_score, r->hypothetical_latency_score, r->original_energy, r->hypothetical_energy, r->structural_collapse); }
void flowy_print_remediation_proposal(const FlowRemediationProposal *p, FILE *out) {
    if (p && out) fprintf(out, "[SMT/Lean4 Remediation] %s | Min-Cut: %s (%.1f -> %.1f MB)\n%s", p->conflict_summary, p->min_cut_dimension, p->current_bound, p->required_remediation_bound, p->proposed_flow_patch); }
void flowy_print_autopilot_incident(const FlowAutopilotIncident *inc, FILE *out) {
    if (inc && out) fprintf(out, "[Autopilot #%llu] %s | %s -> %s | QSBR: %lluns | SMT: %s\n",
        (unsigned long long)inc->incident_id, inc->anomaly_cause, inc->previous_topology, inc->new_topology, (unsigned long long)inc->hot_swap_switch_ns, inc->smt_proof.proof_summary); }
int flowy_interactive_loop(FlowOrchestrator *orch, FILE *in, FILE *out) { if (!in || !out) return 0; FlowTopologyGraph graph; flow_topology_build_codebase_graph(&graph);
    char line[512]; while (fprintf(out, "FLOW-Agent > "), fflush(out), fgets(line, sizeof(line), in)) {
        line[strcspn(line, "\r\n")] = '\0'; if (!line[0]) continue; if (!strcmp(line, "exit") || !strcmp(line, "quit")) break;
        if (!strcmp(line, "why")) { flowy_print_decision_explanation(flow_decision_logger_latest(NULL), out); continue; }
        if (!strcmp(line, "bottleneck")) { flowy_print_bottleneck_explanation(&graph, out); continue; }
        if (!strcmp(line, "timeline")) { flowy_print_decision_timeline(NULL, out); continue; }
        if (strstr(line, "what-if")) { FlowCounterfactualReport r; flow_orchestrator_simulate_what_if(orch, 32, 50, 4, &r); flowy_print_counterfactual_report(&r, out); continue; }
        if (strstr(line, "remediate")) { FlowRemediationProposal p; flow_orchestrator_synthesize_remediation(orch, "spec_a.lean", "spec_b.lean", &p); flowy_print_remediation_proposal(&p, out); continue; }
        FlowyIntrospectiveAnswer ans; flowy_query_codebase(&graph, line, &ans); flowy_print_answer(&ans, out); } return 1; }
void flowy_print_jet_inspection(const struct FlowJet *jet, FILE *out) { if (!jet || !out) return;
    uint32_t dim = jet->header.vector_dim ? jet->header.vector_dim : FLOW_JET_STANDARD_DIM, kdim = jet->header.koopman_dim ? jet->header.koopman_dim : FLOW_JET_STANDARD_KOOPMAN_DIM;
    double tr_k = 0.0; for (uint32_t i = 0; i < kdim; ++i) tr_k += jet->payload.koopman_matrix[i][i];
    fprintf(out, "[.fjet] %s (%s) | Dim: %u | H: %.4f | Tr(K): %+.4f | SMT: %s\n", jet->header.id, jet->header.name, dim, jet->header.hamiltonian_energy, tr_k, jet->header.smt_signature); }
void flowy_render_phase_portrait(const struct FlowJet *jet, uint32_t dx, uint32_t dy, int steps, double dt, FILE *out) { if (!jet || !out) return;
    FlowJet sim = *jet; int n = (steps > 0 && steps <= 128) ? steps : 32; double s_dt = dt > 0.0 ? dt : 0.02;
    for (int i = 0; i < n; ++i) { if (i % 8 == 0) fprintf(out, "  step %02d: q[%u]=%+8.4f, p[%u]=%+8.4f, H=%.6f\n", i, dx, sim.payload.q[dx], dy, sim.payload.p[dy], flow_jet_hamiltonian(&sim));
        flow_jet_symplectic_step(&sim, s_dt); } }
int flowy_jet_simulate_run(const struct FlowJet *jet, int steps, double dt, FILE *out) { if (!jet || !out) return 0;
    FlowJet sim = *jet; int n = steps > 0 ? steps : 20; double s_dt = dt > 0.0 ? dt : 0.01, h0 = flow_jet_hamiltonian(&sim);
    for (int s = 0; s < n; ++s) flow_jet_symplectic_step(&sim, s_dt);
    fprintf(out, "[Symplectic Sim] %d steps | H_init=%.6f -> H_final=%.6f\n", n, h0, flow_jet_hamiltonian(&sim)); return 1; }
int flowy_jet_learn_demo(struct FlowJet *jet, int samples, FILE *out) { if (!jet || !out) return 0; int n = samples > 0 ? samples : 50;
    uint32_t kdim = jet->header.koopman_dim ? jet->header.koopman_dim : FLOW_JET_STANDARD_KOOPMAN_DIM; FlowJetStreamingEDMD edmd; flow_jet_edmd_init(&edmd, kdim, 0.98);
    double x0[FLOW_JET_STANDARD_KOOPMAN_DIM] = {1.0, 0.5, -0.3, 0.2}, x1[FLOW_JET_STANDARD_KOOPMAN_DIM];
    for (int s = 0; s < n; ++s) { for (uint32_t i = 0; i < kdim; ++i) x1[i] = 0.95 * x0[i] + 0.02 * sin((double)(s + i));
        flow_jet_edmd_update(&edmd, x0, x1, 0.01); memcpy(x0, x1, sizeof(double) * kdim); }
    fprintf(out, "[Koopman EDMD] Observed %d samples | Applied=%s\n", n, flow_jet_apply_learned_koopman(jet, &edmd) ? "YES" : "NO"); return 1; }
void flowy_print_cubical_topos_report(FILE *out) { if (!out) return; FlowCubicalTransitionReport rep = {0}; flow_cubical_decode_transition(0x10ULL, 0x14ULL, 2, &rep);
    fprintf(out, "[Cubical HoTT & Topos Sieve] I^64 | chi=0x%02x | Bit#%u: %s\n", (unsigned)flow_topos_classify_subobject(0x14ULL, 0xFFULL), rep.flipped_axis, rep.is_kan_homotopic ? "KAN_FILLED" : "OBSTRUCTED"); }
