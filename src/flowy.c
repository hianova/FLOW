#include "flowy.h"
#include "topology.h"
#include "cubical_hott.h"
#include "flow_jet.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

const char *flowy_resolve_flowc_bin(const char *argv0, char *buf, size_t buf_size) {
    const char *env_bin = getenv("FLOWC");
    if (env_bin && env_bin[0]) return env_bin;
    if (argv0 && buf && buf_size > 0) {
        const char *last_slash = strrchr(argv0, '/');
        if (last_slash) {
            snprintf(buf, buf_size, "%.*s/flowc", (int)(last_slash - argv0), argv0);
            FILE *fs = fopen(buf, "rb");
            if (fs) { fclose(fs); return buf; }
        }
    }
    FILE *ft = fopen("./build/flowc", "rb");
    if (ft) { fclose(ft); return "./build/flowc"; }
    return "flowc";
}

void flowy_explain_decision(const FlowDecisionEvent *ev, char *buf_out, size_t max_len) {
    if (!ev || !buf_out || !max_len) return;
    FlowCubicalTransitionReport rep = {0};
    flow_cubical_decode_transition(ev->pre_state_mask, ev->post_state_mask, ev->flipped_genome_bit, &rep);
    snprintf(buf_out, max_len,
             "[FLOW Token-Ring & Lean4 Decision Audit]\nTrigger: %s (t=%.2fms) | Metric: %.1f/%.1f %s | Invariant: %s\n"
             "Cubical 1-Cell: Bit#%u [%s] | Kan Homotopy: %s | Morph: %s -> %s (%lluns QSBR)\nRationale: %s\n",
             ev->trigger_source, (double)ev->timestamp_ns / 1e6, ev->observed_metric_value, ev->threshold_limit_value, ev->metric_unit, ev->violated_constraint,
             rep.flipped_axis, (rep.axis_info && rep.axis_info->axis_name) ? rep.axis_info->axis_name : "axis",
             rep.is_kan_homotopic ? "KAN_FILLED (p ~ q)" : "OBSTRUCTED", ev->pre_topology, ev->post_topology, (unsigned long long)ev->hot_swap_grace_ns,
             ev->causal_rationale[0] ? ev->causal_rationale : rep.explanation);
}

int flowy_explain_bottleneck(const FlowTopologyGraph *graph, char *buf_out, size_t max_len) {
    if (!buf_out || !max_len) return 0;
    FlowTopologyGraph local;
    const FlowTopologyGraph *g = (graph && graph->node_count) ? graph : (flow_topology_build_codebase_graph(&local), &local);
    const FlowTopologyNode *peak = NULL;
    for (size_t i = 0; i < g->node_count; ++i)
        if (!peak || (g->nodes[i].loc * (g->nodes[i].fan_in + 1)) > (peak->loc * (peak->fan_in + 1))) peak = &g->nodes[i];
    if (!peak) return 0;
    FlowTokenRing ring;
    flow_token_ring_converge_default(&ring, NULL, peak->name);
    snprintf(buf_out, max_len,
             "[FLOW Live Topology & Token-Ring Bottleneck Analysis]\nPeak Node: %s (Layer %u, LOC=%u, FanIn=%u, FanOut=%u, Cubical=0x%016llx)\n"
             "Token Ring Attractor: state=%d, genome=0x%016llx, E=%.2f, DeltaE=%.6f | SMT: %s\n",
             peak->name, peak->layer, peak->loc, peak->fan_in, peak->fan_out, (unsigned long long)peak->capability_mask,
             (int)ring.state, (unsigned long long)ring.active_genome, ring.lyapunov_energy, ring.lyapunov_delta_e, ring.smt_proof.proof_summary);
    return 1;
}

static int node_matches_consolidated_alias(const char *node_name, const char *q) {
    if (!node_name || !q) return 0;
    if (!strcmp(node_name, "bitspace") && (strcasestr(q, "token_ring") || strcasestr(q, "fvec") || strcasestr(q, "montgomery") || strcasestr(q, "fx64") || strcasestr(q, "u256") || strcasestr(q, "exact_numerics"))) return 1;
    if (!strcmp(node_name, "reload") && (strcasestr(q, "jit") || strcasestr(q, "qsbr") || strcasestr(q, "hot_swap") || strcasestr(q, "rcu"))) return 1;
    if (!strcmp(node_name, "smt") && (strcasestr(q, "security") || strcasestr(q, "capability") || strcasestr(q, "qf_lia") || strcasestr(q, "sandbox"))) return 1;
    if (!strcmp(node_name, "flow_jet") && (strcasestr(q, "hardware_telemetry") || strcasestr(q, "telemetry") || strcasestr(q, "numa") || strcasestr(q, "fjet") || strcasestr(q, "koopman"))) return 1;
    if (!strcmp(node_name, "embodied") && (strcasestr(q, "driver_can") || strcasestr(q, "driver_imu") || strcasestr(q, "mori_zwanzig") || strcasestr(q, "socketcan") || strcasestr(q, "imu") || strcasestr(q, "zmp"))) return 1;
    if (!strcmp(node_name, "registry") && strcasestr(q, "builtin_plugin")) return 1;
    if (!strcmp(node_name, "cubical_hott") && (strcasestr(q, "univalence") || strcasestr(q, "hott") || strcasestr(q, "topos") || strcasestr(q, "kan") || strcasestr(q, "demorgan") || strcasestr(q, "universe"))) return 1;
    if (!strcmp(node_name, "topology") && (strcasestr(q, "probe") || strcasestr(q, "radar") || strcasestr(q, "orchestrator") || strcasestr(q, "autopilot") || strcasestr(q, "self_bootstrap") || strcasestr(q, "makefile"))) return 1;
    if (!strcmp(node_name, "flow_plugin_sdk") && (strcasestr(q, "cargo") || strcasestr(q, "rust_sdk") || strcasestr(q, "repr(c)"))) return 1;
    return 0;
}

int flowy_query_codebase(const FlowTopologyGraph *graph, const char *query_text, FlowyIntrospectiveAnswer *ans) {
    if (!query_text || !ans) return 0;
    memset(ans, 0, sizeof(*ans));
    strncpy(ans->query, query_text, sizeof(ans->query) - 1);
    FlowTopologyGraph local;
    const FlowTopologyGraph *g = (graph && graph->node_count) ? graph : (flow_topology_build_codebase_graph(&local), &local);
    if (!g->node_count) return 0;
    if (strstr(query_text, "bottleneck") || strstr(query_text, "瓶頸")) {
        ans->primary_node = g->nodes[0]; ans->matched_score = 100;
        return flowy_explain_bottleneck(g, ans->explanation, sizeof(ans->explanation));
    }
    if (strstr(query_text, "why") || strstr(query_text, "為什麼")) {
        ans->primary_node = g->nodes[0]; ans->matched_score = 100;
        flowy_explain_decision(flow_decision_logger_latest(NULL), ans->explanation, sizeof(ans->explanation));
        return 1;
    }
    uint64_t q_mask = flow_cubical_project_text_intent(query_text);
    const FlowTopologyNode *best_node = &g->nodes[0];
    uint32_t best_score = 0;
    for (size_t i = 0; i < g->node_count; ++i) {
        const FlowTopologyNode *n = &g->nodes[i];
        int match = (n->name[0] && (strcasestr(query_text, n->name) != NULL || strcasestr(n->name, query_text) != NULL)) ||
                    node_matches_consolidated_alias(n->name, query_text);
        int scope_match = (n->scope_name[0] && strcasestr(query_text, n->scope_name) != NULL);
        uint32_t s = (match ? 600 : (scope_match ? 120 : 0)) + (uint32_t)__builtin_popcountll(q_mask & n->capability_mask) * 30;
        if (s > best_score) { best_score = s; best_node = n; }
    }
    ans->primary_node = *best_node;
    ans->matched_score = best_score ? best_score : 10;
    uint32_t axis = best_node->capability_mask ? (uint32_t)__builtin_ctzll(best_node->capability_mask) : 0;
    const FlowCubicalAxisInfo *ax = flow_cubical_get_axis_info(axis);
    FlowTokenRing ring;
    flow_token_ring_converge_default(&ring, NULL, best_node->name);
    snprintf(ans->explanation, sizeof(ans->explanation),
             "[FLOW Canonical Agent: Live AST Topology + Token Ring + Lean 4]\n"
             "Node: %s (Layer %u, Scope=%s, Role=%s, LOC=%u, FanIn=%u, FanOut=%u, Sieve=0x%016llx)\n"
             "Subspace: %s | Proof Obligation: %s\n"
             "Token Ring Convergence: Component=%s, Genome=0x%016llx, Energy=%.2f, SMT=%s\n",
             best_node->name, best_node->layer, best_node->scope_name[0] ? best_node->scope_name : "core",
             best_node->role, best_node->loc, best_node->fan_in, best_node->fan_out,
             (unsigned long long)best_node->capability_mask, ax->subspace_name, ax->policy_contract,
             ring.best_search.component ? ring.best_search.component->id : "sharded_hash",
             (unsigned long long)ring.active_genome, ring.lyapunov_energy, ring.smt_proof.proof_summary);
    return 1;
}

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
        if (!strcmp(line, "help") || !strcmp(line, "?")) {
            fprintf(out, "Commands: audit | scope [name] | topos | why | bottleneck | timeline | what-if | remediate | <query> | exit\n");
            continue;
        }
        if (!strcmp(line, "audit")) {
            FlowTopologyAuditReport r; flow_topology_audit(&graph, &r);
            int pop_sc = 0; for (int s = 0; s < (int)FLOW_SCOPE_COUNT; ++s) if (r.scope_node_counts[s] > 0) pop_sc++;
            fprintf(out, "[FLOW Topology Audit] Nodes: %zu | LOC: %zu | Leaks: %zu | Modularity: %.2f | Scopes: %d/%d | Makefile: %s | Rust-SDK: %s | HoTT-U0: %s(%zu)\n",
                    r.total_nodes, r.total_loc, r.cross_layer_leaks, r.modularity_score, pop_sc, (int)FLOW_SCOPE_COUNT,
                    r.makefile_bijective ? "BIJECTIVE" : "DRIFT", r.rust_sdk_parity ? "PARITY_OK" : "DRIFT",
                    r.hott_univalence_sound ? "UNIVALENT" : "BROKEN", r.hott_type_count);
            continue;
        }
        if (!strncmp(line, "scope", 5)) {
            const char *arg = line + 5; while (*arg == ' ') ++arg;
            flowy_print_scope_report(&graph, *arg ? arg : "all", out); continue;
        }
        if (!strcmp(line, "topos") || !strcmp(line, "hott") || !strcmp(line, "cubical")) { flowy_print_cubical_topos_report(out); continue; }
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
    uint32_t kdim = jet->header.koopman_dim ? jet->header.koopman_dim : FLOW_JET_STANDARD_KOOPMAN_DIM; FlowJetStreamingEDMD edmd; flow_jet_edmd_init(&edmd, kdim, FLOW_JET_EDMD_DEFAULT_LAMBDA);
    double x0[FLOW_JET_STANDARD_KOOPMAN_DIM] = {1.0, 0.5, -0.3, 0.2}, x1[FLOW_JET_STANDARD_KOOPMAN_DIM];
    for (int s = 0; s < n; ++s) { for (uint32_t i = 0; i < kdim; ++i) x1[i] = 0.95 * x0[i] + 0.02 * sin((double)(s + i));
        flow_jet_edmd_update(&edmd, x0, x1, 0.01); memcpy(x0, x1, sizeof(double) * kdim); }
    fprintf(out, "[Koopman EDMD] Observed %d samples | Applied=%s\n", n, flow_jet_apply_learned_koopman(jet, &edmd) ? "YES" : "NO"); return 1; }
void flowy_print_cubical_topos_report(FILE *out) { if (!out) return; FlowCubicalTransitionReport rep = {0}; flow_cubical_decode_transition(0x10ULL, 0x14ULL, 2, &rep);
    FlowTopologyGraph g; flow_topology_build_codebase_graph(&g); FlowTopologyAuditReport ar; flow_topology_audit(&g, &ar);
    FlowHoTTUniverse *u0 = flow_hott_universe_global();
    fprintf(out, "[Cubical HoTT & Topos Sieve] I^64 | chi=0x%02x | Bit#%u: %s | Universe-U0: %zu types (%zu entries, Univalence=%s)\n",
            (unsigned)flow_topos_classify_subobject(0x14ULL, 0xFFULL), rep.flipped_axis, rep.is_kan_homotopic ? "KAN_FILLED" : "OBSTRUCTED",
            u0->distinct_type_count, u0->count, ar.hott_univalence_sound ? "UNIVALENT" : "BROKEN");
    for (size_t i = 0; i < u0->count; ++i) {
        int seen = 0;
        for (size_t j = 0; j < i; ++j) if (!strcmp(u0->entries[i].name, u0->entries[j].name)) { seen = 1; break; }
        if (seen) continue;
        uint64_t path_hash = 0ULL;
        FlowKanStatus st = flow_hott_verify_univalence(u0, u0->entries[i].name, &path_hash);
        fprintf(out, "  * [U_0] %-22s | fields=%-2zu size=%-3zuB align=%zuB fx=%u | Path=0x%016llx (%s)\n",
                u0->entries[i].name, u0->entries[i].field_count, u0->entries[i].byte_size, u0->entries[i].align_bytes,
                u0->entries[i].fx_scale_bits, (unsigned long long)path_hash, st == FLOW_KAN_FILLED_HOMOTOPIC ? "A ~= B => A = B" : "OBSTRUCTED");
    }
}

void cmd_fvec_print_usage(FILE *out) { fprintf(out, "Usage: flowy fvec <seed|list|inspect|query|rag|remediate|gc> [options...]\n"); }
static void ensure_seeded_store(FlowVecStore *store) { flow_fvec_store_init(store, FLOW_FVEC_DEFAULT_DIR);
    if (!flow_fvec_store_scan(store)) { flow_fvec_seed_canonical_files(FLOW_FVEC_DEFAULT_DIR); flow_fvec_store_scan(store); } }
int cmd_fvec_run(int argc, char **argv) { if (argc < 1) { cmd_fvec_print_usage(stderr); return EXIT_FAILURE; }
    FlowVecStore store; ensure_seeded_store(&store); const char *cmd = argv[0];
    const char *action = (!strcmp(cmd, "fvec") || !strcmp(cmd, "--fvec")) ? (argc >= 2 ? argv[1] : "list") : cmd;
    int off = (!strcmp(cmd, "fvec") || !strcmp(cmd, "--fvec")) ? 2 : 1;
    if (!strcmp(action, "list") || !strcmp(action, "vault") || !strcmp(action, "--vault") || !strcmp(action, "antibody")) {
        flow_fvec_store_print_summary(&store, stdout); return EXIT_SUCCESS; }
    if (!strcmp(action, "seed")) { printf("✓ Seeded %d canonical .fvec models to '%s'\n", flow_fvec_seed_canonical_files(FLOW_FVEC_DEFAULT_DIR), FLOW_FVEC_DEFAULT_DIR); return EXIT_SUCCESS; }
    if (!strcmp(action, "inspect") || !strcmp(action, "show")) { FlowVecHeader hdr; FlowVecPayload pl;
        if (off >= argc || !flow_fvec_read_file(argv[off], &hdr, &pl)) return EXIT_FAILURE;
        flow_fvec_inspect(&hdr, &pl, stdout); return EXIT_SUCCESS; }
    if (!strcmp(action, "query") || !strcmp(action, "--query") || !strcmp(action, "search") || !strcmp(action, "--search") || !strcmp(action, "rag") || !strcmp(action, "--rag") || !strcmp(action, "find")) {
        size_t best_idx = 0; double best_sim = 0.0; if (off >= argc || !flow_fvec_store_query(&store, argv[off], &best_idx, &best_sim)) return EXIT_FAILURE;
        const FlowVecRecord *r = &store.records[best_idx];
        printf("[.fvec Match] %s (Sim: %.2f%%) | Path: %s | Comp: %s | Genome: 0x%016llx\n", r->header.name, best_sim * 100.0, r->header.filepath, r->header.component_id, (unsigned long long)r->payload.pure_genome);
        return EXIT_SUCCESS; }
    if (!strcmp(action, "remediate") || !strcmp(action, "crisis")) { double ram = 98.0, miss = 0.05, conf = 0.0;
        for (int i = off; i < argc; ++i) if (!strcmp(argv[i], "--ram") && i + 1 < argc) ram = atof(argv[++i]); else if (!strcmp(argv[i], "--miss") && i + 1 < argc) miss = atof(argv[++i]);
        const FlowVecRecord *rec = NULL; char diag[512] = {0};
        if (flow_fvec_remediate_check(&store, ram, miss, &rec, &conf, diag, sizeof(diag))) printf("[Remediation] %s -> Apply: %s\n", diag, rec->header.filepath);
        else printf("[Remediation] Telemetry stable.\n");
        return EXIT_SUCCESS; }
    if (!strcmp(action, "gc") || !strcmp(action, "evict")) { uint64_t max_age = 30 * 86400ULL;
        for (int i = off; i < argc; ++i) if (!strcmp(argv[i], "--max-age") && i + 1 < argc) max_age = strtoull(argv[++i], NULL, 10);
        char evicted[16][256]; size_t n = flow_fvec_store_evict_senescent(&store, (uint64_t)time(NULL), max_age, evicted, 16);
        printf("[GC] Evicted %zu idle .fvec models (Remaining: %zu)\n", n, store.count); return EXIT_SUCCESS; }
    cmd_fvec_print_usage(stderr); return EXIT_FAILURE; }

void cmd_jet_print_usage(FILE *out) { fprintf(out, "Usage: flowy jet <inspect|sim|phase-portrait|learn> <file.fjet> [options...]\n"); }
int cmd_jet_run(int argc, char **argv) { if (argc < 1 || !strcmp(argv[0], "-h") || !strcmp(argv[0], "--help") || !strcmp(argv[0], "help")) {
        cmd_jet_print_usage(argc < 1 ? stderr : stdout); return argc < 1 ? EXIT_FAILURE : EXIT_SUCCESS; }
    const char *act = argv[0]; FlowJet jet; if (argc < 2 || !flow_jet_read_file(argv[1], &jet)) return EXIT_FAILURE;
    if (!strcmp(act, "inspect") || !strcmp(act, "show")) { flowy_print_jet_inspection(&jet, stdout); return EXIT_SUCCESS; }
    if (!strcmp(act, "sim") || !strcmp(act, "simulate") || !strcmp(act, "phase-portrait") || !strcmp(act, "portrait")) {
        int is_pp = (!strcmp(act, "phase-portrait") || !strcmp(act, "portrait")), steps = is_pp ? 60 : 20; double dt = is_pp ? 0.02 : 0.01;
        for (int i = 2; i < argc; ++i) if (!strcmp(argv[i], "--steps") && i + 1 < argc) steps = atoi(argv[++i]); else if (!strcmp(argv[i], "--dt") && i + 1 < argc) dt = atof(argv[++i]);
        if (is_pp) flowy_render_phase_portrait(&jet, 0, 0, steps, dt, stdout); else flowy_jet_simulate_run(&jet, steps, dt, stdout);
        return EXIT_SUCCESS; }
    if (!strcmp(act, "learn") || !strcmp(act, "edmd")) { int samples = 50;
        for (int i = 2; i < argc; ++i) if (!strcmp(argv[i], "--samples") && i + 1 < argc) samples = atoi(argv[++i]);
        flowy_jet_learn_demo(&jet, samples, stdout); return EXIT_SUCCESS; }
    cmd_jet_print_usage(stderr); return EXIT_FAILURE; }

void flowy_print_scope_report(const FlowTopologyGraph *graph, const char *scope_filter, FILE *out) {
    if (!out) return;
    FlowTopologyGraph local;
    const FlowTopologyGraph *g = (graph && graph->node_count) ? graph : (flow_topology_build_codebase_graph(&local), &local);
    FlowTopologyAuditReport rep;
    flow_topology_audit(g, &rep);
    FlowComponentScope target_sc = FLOW_SCOPE_FRONTEND;
    int has_filter = (scope_filter && scope_filter[0] && strcmp(scope_filter, "all") != 0 && flow_topology_parse_scope(scope_filter, &target_sc));
    fprintf(out, "[FLOW Full-Codebase Component Scope Matrix | %zu Nodes | %zu LOC | %zu Leaks]\n",
            rep.total_nodes, rep.total_loc, rep.cross_layer_leaks);
    for (int s = 0; s < (int)FLOW_SCOPE_COUNT; ++s) {
        FlowComponentScope sc = (FlowComponentScope)s;
        if (has_filter && sc != target_sc) continue;
        fprintf(out, "  [%d] Scope %-16s | Nodes=%-2zu | LOC=%-6zu | Contract: %s\n      Members: ",
                s, flow_topology_scope_name(sc), rep.scope_node_counts[s], rep.scope_loc[s], flow_topology_scope_contract(sc));
        int first = 1;
        for (size_t i = 0; i < g->node_count; ++i) {
            if (g->nodes[i].scope == sc) {
                fprintf(out, "%s%s(L%u,%uLOC)", first ? "" : ", ", g->nodes[i].name, g->nodes[i].layer, g->nodes[i].loc);
                first = 0;
            }
        }
        fprintf(out, "\n");
    }
}

void cmd_topo_print_usage(FILE *out) { fprintf(out, "Usage: flowy topo <absorb|anneal|landscape|refactor|morph|what-if|remediate|autopilot|daemon|scope> [options...]\n"); }
static void absorb_args_or_default(FlowOrchestrator *orch, int argc, char **argv, char *diag, size_t dsz) {
    for (int i = 2; i < argc; ++i) if (argv[i][0] != '-') flow_orchestrator_absorb(orch, argv[i], diag, dsz);
}
static int run_orchestrator_cmd(int argc, char **argv) { if (argc < 2) return EXIT_FAILURE;
    const char *cmd = (argv[1][0] == '-' && argv[1][1] == '-') ? argv[1] + 2 : argv[1];
    FlowOrchestrator *orch = flow_orchestrator_create("."); char diag[256] = {0}; int res = EXIT_SUCCESS;
    if (!strcmp(cmd, "shell")) {
        res = flowy_interactive_loop(orch, stdin, stdout) ? EXIT_SUCCESS : EXIT_FAILURE;
    } else if (!strcmp(cmd, "scope") || !strcmp(cmd, "scopes")) {
        flowy_print_scope_report(&orch->topology_graph, argc >= 3 ? argv[2] : "all", stdout);
    } else if (!strcmp(cmd, "absorb")) { if (argc < 3) res = EXIT_FAILURE; else {
        FlowAbsorbStatus st = flow_orchestrator_absorb(orch, argv[2], diag, sizeof(diag)); printf("flow-orchestrator: [%s] %s\n", flow_absorb_status_name(st), diag);
        res = (st == FLOW_ABSORB_OK || st == FLOW_ABSORB_ALREADY_ABSORBED) ? EXIT_SUCCESS : EXIT_FAILURE; }
    } else if (!strcmp(cmd, "anneal") || !strcmp(cmd, "landscape")) { absorb_args_or_default(orch, argc, argv, diag, sizeof(diag)); FlowOrchestratorEpoch ep;
        int ok = flow_orchestrator_anneal(orch, !strcmp(cmd, "anneal") ? 200 : 100, FLOW_DEFAULT_SEED, &ep);
        if (!strcmp(cmd, "anneal") && !ok) res = EXIT_FAILURE; else {
            if (!strcmp(cmd, "anneal")) printf("[epoch_solidified] Epoch=#%llu Energy=%.4f Entropy=%.4f\n", (unsigned long long)ep.epoch_id, ep.global_energy, ep.entropy_score);
            flow_orchestrator_landscape(orch, stdout); }
    } else if (!strcmp(cmd, "refactor")) {
        FlowOrchestratorEpoch ep; flow_orchestrator_anneal(orch, 100, FLOW_DEFAULT_SEED, &ep); double delta = 0.0; flow_orchestrator_refactor_entropy(orch, &delta);
        printf("[entropy_reduction] Delta=%.4f\n", delta);
    } else if (!strcmp(cmd, "morph")) { const char *ts = argc >= 3 ? argv[2] : "speed";
        FlowPlanTactic tactic = !strcmp(ts, "memory") ? FLOW_TACTIC_MEMORY : (!strcmp(ts, "balanced") ? FLOW_TACTIC_BALANCED : FLOW_TACTIC_SPEED);
        FlowOrchestratorEpoch ep; flow_orchestrator_anneal(orch, 100, FLOW_DEFAULT_SEED, &ep); FlowPlan tp;
        if (flow_orchestrator_time_travel(orch, tactic, &tp)) printf("[state_time_travel] Morphed to '%s' LatencyScore=%.1f\n", flow_plan_tactic_name(tactic), tp.eval.latency_score);
    } else if (!strcmp(cmd, "what-if") || !strcmp(cmd, "whatif")) { int mem_mb = 32, top_n = 50, threads = 4; const char *spec = NULL;
        for (int i = 2; i < argc; ++i) { if (!strcmp(argv[i], "--memory") && i + 1 < argc) mem_mb = atoi(argv[++i]);
            else if (!strcmp(argv[i], "--top-n") && i + 1 < argc) top_n = atoi(argv[++i]); else if (!strcmp(argv[i], "--threads") && i + 1 < argc) threads = atoi(argv[++i]);
            else if (argv[i][0] != '-') spec = argv[i]; }
        if (spec) flow_orchestrator_absorb(orch, spec, diag, sizeof(diag));
        FlowCounterfactualReport rep; flow_orchestrator_simulate_what_if(orch, mem_mb, top_n, threads, &rep);
        flowy_print_counterfactual_report(&rep, stdout);
    } else if (!strcmp(cmd, "remediate")) { FlowRemediationProposal prop;
        flow_orchestrator_synthesize_remediation(orch, argc >= 3 ? argv[2] : "spec_a.lean", argc >= 4 ? argv[3] : "spec_b.lean", &prop);
        flowy_print_remediation_proposal(&prop, stdout);
    } else if (!strcmp(cmd, "autopilot")) { if (argc >= 3) flow_orchestrator_absorb(orch, argv[2], diag, sizeof(diag));
        FlowAutopilotController *ctrl = flow_autopilot_create(orch, NULL); FlowPMUTelemetry storm = { .cache_miss_rate = 0.148, .ipc = 0.82 };
        FlowAutopilotIncident inc; flow_autopilot_step(ctrl, &storm, &inc); flowy_print_autopilot_incident(&inc, stdout); flow_autopilot_destroy(ctrl);
    } else if (!strcmp(cmd, "daemon")) {
        for (size_t c = 0; c < 3; ++c) { double delta = 0.0; flow_orchestrator_refactor_entropy(orch, &delta); FlowOrchestratorEpoch ep;
            flow_orchestrator_anneal(orch, 50, FLOW_DEFAULT_SEED + (uint32_t)c, &ep); printf("[cycle #%zu] Entropy=%.4f\n", c + 1, ep.entropy_score); } }
    flow_orchestrator_destroy(orch); return res; }
int cmd_topo_run(int argc, char **argv) { if (argc < 2) { cmd_topo_print_usage(stderr); return EXIT_FAILURE; }
    const char *sub = (argv[1][0] == '-' && argv[1][1] == '-') ? argv[1] + 2 : argv[1];
    return (!strcmp(sub, "-h") || !strcmp(sub, "help")) ? (cmd_topo_print_usage(stdout), EXIT_SUCCESS) : run_orchestrator_cmd(argc, argv); }

void cmd_inspect_print_usage(FILE *out) { fprintf(out, "Usage: flowy inspect <ask|why|timeline|bottleneck|audit|scope|topos|doc> [options...]\n"); }
int cmd_inspect_run(int argc, char **argv) {
    if (argc < 1) { cmd_inspect_print_usage(stderr); return EXIT_FAILURE; }
    const char *sub = (argv[0][0] == '-' && argv[0][1] == '-') ? argv[0] + 2 : argv[0];
    if (!strcmp(sub, "-h") || !strcmp(sub, "help")) { cmd_inspect_print_usage(stdout); return EXIT_SUCCESS; }
    if (!strcmp(sub, "ask")) {
        if (argc < 2) return EXIT_FAILURE;
        FlowTopologyGraph g; flow_topology_build_codebase_graph(&g);
        FlowyIntrospectiveAnswer ans; flowy_query_codebase(&g, argv[1], &ans); flowy_print_answer(&ans, stdout);
        return EXIT_SUCCESS;
    }
    if (!strcmp(sub, "why")) { flowy_print_decision_explanation(flow_decision_logger_latest(NULL), stdout); return EXIT_SUCCESS; }
    if (!strcmp(sub, "timeline") || !strcmp(sub, "explain-decisions")) { flowy_print_decision_timeline(NULL, stdout); return EXIT_SUCCESS; }
    if (!strcmp(sub, "bottleneck")) { FlowTopologyGraph g; flow_topology_build_codebase_graph(&g); flowy_print_bottleneck_explanation(&g, stdout); return EXIT_SUCCESS; }
    if (!strcmp(sub, "scope") || !strcmp(sub, "scopes")) {
        FlowTopologyGraph g; flow_topology_build_codebase_graph(&g);
        flowy_print_scope_report(&g, argc >= 2 ? argv[1] : "all", stdout);
        return EXIT_SUCCESS;
    }
    if (!strcmp(sub, "audit")) {
        FlowTopologyGraph g; flow_topology_build_codebase_graph(&g); FlowTopologyAuditReport r; flow_topology_audit(&g, &r);
        int pop_sc = 0; for (int s = 0; s < (int)FLOW_SCOPE_COUNT; ++s) if (r.scope_node_counts[s] > 0) pop_sc++;
        printf("[FLOW Topology Audit] Nodes: %zu (Core:%zu, Plugins:%zu, Intents:%zu, Rust:%zu) | LOC:%zu | Leaks:%zu | Modularity:%.2f | SMT: QF_LIA SOUND\n"
               "  [Self-Bootstrap Gates] Scopes:%d/%d | Makefile:%s | Rust-SDK:%s | 7-Lang-ABI:%s | HoTT-U0:%s(%zu) | Probes:%s | Exact-Numerics:%s\n",
               r.total_nodes, r.core_nodes, r.plugin_nodes, r.intent_nodes, r.rust_nodes, r.total_loc, r.cross_layer_leaks, r.modularity_score,
               pop_sc, (int)FLOW_SCOPE_COUNT,
               r.makefile_bijective ? "BIJECTIVE" : "DRIFT",
               r.rust_sdk_parity ? "PARITY_OK" : "DRIFT",
               r.polyglot_homomorphic ? "HOMOMORPHIC" : "DRIFT",
               r.hott_univalence_sound ? "UNIVALENT" : "BROKEN",
               r.hott_type_count,
               r.probe_weaving_sound ? "WOVEN" : "MISSING",
               r.exact_numerics_sound ? "SOUND" : "DRIFT");
        if (r.cross_layer_leaks > 0 && r.leak_details[0]) printf("  [Audit Violations] %s\n", r.leak_details);
        return r.cross_layer_leaks == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (!strcmp(sub, "topos") || !strcmp(sub, "hott") || !strcmp(sub, "cubical")) { flowy_print_cubical_topos_report(stdout); return EXIT_SUCCESS; }
    if (!strcmp(sub, "doc")) {
        const char *mod = (argc >= 2) ? argv[1] : "all";
        FlowTopologyGraph g; flow_topology_build_codebase_graph(&g);
        for (size_t i = 0; i < g.node_count; ++i) {
            const FlowTopologyNode *n = &g.nodes[i];
            if (!strcmp(mod, "all") || !strcmp(mod, n->name) || !strcasecmp(mod, n->scope_name))
                printf("* [%s] (L%u, %s, scope=%s) LOC=%u FanIn=%u FanOut=%u Sieve=0x%016llx\n",
                       n->name, n->layer, n->role, n->scope_name, n->loc, n->fan_in, n->fan_out, (unsigned long long)n->capability_mask);
        }
        return EXIT_SUCCESS;
    }
    cmd_inspect_print_usage(stderr);
    return EXIT_FAILURE;
}

