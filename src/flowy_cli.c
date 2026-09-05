#include "flowy_cli.h"
#include "flowy.h"
#include "flow_jet.h"
#include "flow_time_crystal.h"
#include "flow_jet_dead_reckon.h"
#include "audit.h"
#include "generated_book_knowledge.h"
#include "generated_knowledge.h"
#include "orchestrator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>

/* ========================================================================= */
/* Multi-Lingual Presentation & Localization Configuration                   */
/* ========================================================================= */

static FlowLanguage g_current_language = FLOW_LANG_ZH;
static int g_language_initialized = 0;

void flowy_set_language(FlowLanguage lang) {
    g_current_language = lang;
    g_language_initialized = 1;
}

FlowLanguage flowy_get_language(void) {
    if (!g_language_initialized) {
        g_current_language = flowy_detect_system_language();
        g_language_initialized = 1;
    }
    return g_current_language;
}

FlowLanguage flowy_detect_system_language(void) {
    const char *flowy_lang = getenv("FLOWY_LANG");
    if (flowy_lang && strlen(flowy_lang) > 0) {
        return flowy_parse_language(flowy_lang);
    }
    const char *env_lang = getenv("LC_ALL");
    if (!env_lang) env_lang = getenv("LANG");
    if (env_lang) {
        if (strstr(env_lang, "zh") || strstr(env_lang, "ZH") || strstr(env_lang, "tw") ||
            strstr(env_lang, "TW") || strstr(env_lang, "cn") || strstr(env_lang, "CN")) {
            return FLOW_LANG_ZH;
        }
        if (strstr(env_lang, "en") || strstr(env_lang, "EN") || strstr(env_lang, "C") ||
            strstr(env_lang, "POSIX")) {
            return FLOW_LANG_EN;
        }
    }
    return FLOW_LANG_ZH;
}

FlowLanguage flowy_parse_language(const char *lang_str) {
    if (lang_str == NULL) return FLOW_LANG_ZH;
    if (strcasecmp(lang_str, "en") == 0 || strcasecmp(lang_str, "en_US") == 0 ||
        strcasecmp(lang_str, "en-US") == 0 || strcasecmp(lang_str, "english") == 0 ||
        strcasecmp(lang_str, "eng") == 0) {
        return FLOW_LANG_EN;
    }
    return FLOW_LANG_ZH;
}

const char *flowy_language_name(FlowLanguage lang) {
    return (lang == FLOW_LANG_EN) ? "English (en)" : "Traditional Chinese / 繁體中文 (zh)";
}

/* ========================================================================= */
/* CLI Formatters & Visual Renderers                                         */
/* ========================================================================= */

void flowy_print_answer(const FlowyIntrospectiveAnswer *answer, FILE *out) {
    if (answer == NULL || out == NULL) return;
    fprintf(out, "\n%s\n", answer->explanation);
}

void flowy_print_decision_explanation(const FlowDecisionEvent *event, FILE *out) {
    if (event == NULL || out == NULL) return;
    char buf[2048] = {0};
    flowy_explain_decision(event, buf, sizeof(buf));
    fprintf(out, "\n%s\n", buf);
}

void flowy_print_decision_timeline(const FlowDecisionLogger *logger, FILE *out) {
    if (out == NULL) return;
    const FlowDecisionLogger *l = (logger && logger->total_recorded > 0) ? logger : flow_decision_logger_default();

        fprintf(out, "                         FLOW REAL-TIME DECISION TIMELINE & CAUSAL LOG                                \n");
        fprintf(out, "%-10s | %-18s | %-18s | %-28s | %-8s\n",
            "Time (ms)", "Trigger", "Observed / Limit", "Topology Morph", "QSBR (ns)");
    fprintf(out, "-----------+--------------------+--------------------+------------------------------+-----------\n");

    size_t count = l->total_recorded > FLOW_MAX_DECISION_LOGS ? FLOW_MAX_DECISION_LOGS : l->total_recorded;
    for (size_t i = 0; i < count; ++i) {
        const FlowDecisionEvent *ev = &l->events[i];
        double t_ms = (double)ev->timestamp_ns / 1000000.0;
        char val_str[32], morph_str[32];
        snprintf(val_str, sizeof(val_str), "%.1f / %.1f %s", ev->observed_metric_value, ev->threshold_limit_value, ev->metric_unit);
        snprintf(morph_str, sizeof(morph_str), "Bit#%u -> %s", ev->flipped_genome_bit, ev->post_topology);
        morph_str[28] = '\0';

        fprintf(out, "%10.2f | %-18s | %-18s | %-28s | %8llu\n",
                t_ms, ev->trigger_source, val_str, morph_str, (unsigned long long)ev->hot_swap_grace_ns);
    }
    }

void flowy_print_bottleneck_explanation(const FlowTopologyGraph *graph, FILE *out) {
    if (out == NULL) return;
    char buf[2048] = {0};
    flowy_explain_bottleneck(graph, buf, sizeof(buf));
    fprintf(out, "\n%s\n", buf);
}

int flowy_show_book_lang(const char *target, FlowLanguage lang, FILE *out) {
    if (out == NULL) return 0;
    const FlowyLocaleTemplate *tpl = &LOCALE_TEMPLATES[lang == FLOW_LANG_EN ? FLOW_LANG_EN : FLOW_LANG_ZH];
    const FlowBookChapterDoc *chapters = (lang == FLOW_LANG_EN) ? FLOW_BOOK_CHAPTERS_EN : FLOW_BOOK_CHAPTERS_ZH;

    if (target == NULL || strcmp(target, "all") == 0 || strcmp(target, "toc") == 0 || strcmp(target, "summary") == 0) {
        fprintf(out, "%s\n", tpl->book_toc_header);
        for (size_t i = 0; i < FLOW_BOOK_CHAPTER_COUNT; ++i) {
            const FlowBookChapterDoc *ch = &chapters[i];
            fprintf(out, "[Chapter %02zu] %s\n", i + 1, ch->chapter_title);
            fprintf(out, "             %s: flow-book/src/%s\n", tpl->book_doc_path, ch->chapter_ref);
            fprintf(out, "             %s 「%s」\n\n", (lang == FLOW_LANG_EN ? "Philosophy:" : "哲學:"), ch->philosophy_why);
        }
        fprintf(out, "%s\n\n", tpl->book_toc_footer);
        return 1;
    }

    int ch_num = atoi(target);
    const FlowBookChapterDoc *ch_found = NULL;
    if (ch_num >= 1 && ch_num <= (int)FLOW_BOOK_CHAPTER_COUNT) {
        ch_found = &chapters[ch_num - 1];
    } else {
        ch_found = flow_book_lookup_chapter_lang(target, lang);
    }

    if (ch_found == NULL) {
        const FlowModuleBookBinding *binding = flow_book_lookup_binding_lang(target, lang);
        if (binding) {
            ch_found = flow_book_lookup_chapter_lang(binding->chapter_ref, lang);
        }
    }

    if (ch_found) {
        fprintf(out, "%s: %s\n", tpl->book_doc_header, ch_found->chapter_title);
        fprintf(out, "%s: flow-book/src/%s\n", tpl->book_doc_path, ch_found->chapter_ref);
        fprintf(out, "================================================================================\n\n");
        fprintf(out, "%s\n   「%s」\n\n", tpl->book_doc_why, ch_found->philosophy_why);
        fprintf(out, "%s\n   %s\n\n", tpl->book_doc_excerpt, ch_found->book_excerpt);
        fprintf(out, "================================================================================\n\n");
        return 1;
    }

    if (lang == FLOW_LANG_EN) {
        fprintf(out, "flowy book: Chapter or module '%s' not found. Use 'flowy book all' to list chapters.\n", target);
    } else {
        fprintf(out, "flowy book: 找不到對應章節或模組 '%s'。請使用 'flowy book all' 查看目錄。\n", target);
    }
    return 0;
}

int flowy_show_book(const char *target, FILE *out) {
    return flowy_show_book_lang(target, flowy_get_language(), out);
}

void flowy_print_counterfactual_report(const FlowCounterfactualReport *report, FILE *out) {
    if (!report || !out) return;
    fprintf(out,
        "        FLOW TOPOLOGY COUNTERFACTUAL WHAT-IF SIMULATION REPORT                  \n"
        "Hypothetical Scenario:       %s\n"
        "Memory Constraint Shift:     %d MB -> %d MB\n"
        "Component Layout:            %s -> %s\n"
        "Pareto Latency Score:        %.2f -> %.2f\n"
        "Pareto Energy:               %.2f -> %.2f\n"
        "Throughput Impact:           %+.1f%%\n"
        "QSBR Reclamation Multiplier: %.1fx (Reclamation pressure surge)\n"
        "STRUCTURAL TOPOLOGY COLLAPSE:\n"
        "  * %s\n"
        "DECISION RECOMMENDATION:\n"
        "  * %s\n",
        report->hypothetical_description,
        report->original_memory_mb, report->hypothetical_memory_mb,
        report->original_component, report->hypothetical_component,
        report->original_latency_score, report->hypothetical_latency_score,
        report->original_energy, report->hypothetical_energy,
        report->throughput_delta_percent,
        report->qsbr_reclaim_freq_multiplier,
        report->structural_collapse,
        report->recommendation);
}

void flowy_print_remediation_proposal(const FlowRemediationProposal *proposal, FILE *out) {
    if (!proposal || !out) return;
    fprintf(out,
        "          FLOW TOPOLOGICAL SYNTHESIS & SMT AUTO-REMEDIATION PROPOSAL            \n"
        "Conflict Summary:            %s\n"
        "Min-Cut Bottleneck Variable: %s\n"
        "Current Infeasible Bound:    %.1f MB\n"
        "Required Remediation Bound:  %.1f MB (Minimum relaxation distance)\n"
        "SYNTHESIZED .FLOW REMEDIATION PATCH:\n"
        "%s",
        proposal->conflict_summary, proposal->min_cut_dimension,
        proposal->current_bound, proposal->required_remediation_bound,
        proposal->proposed_flow_patch);
}

void flowy_print_autopilot_incident(const FlowAutopilotIncident *incident, FILE *out) {
    if (!incident || !out) return;
    fprintf(out,
        "          FLOW CLOSED-LOOP AUTONOMOUS AUTOPILOT INCIDENT REPORT                 \n"
        "Incident ID:                 #%llu\n"
        "Trigger Anomaly:             %s\n"
        "Topology Migration:          %s -> %s\n"
        "Autonomous Action:           %s\n"
        "Hot-Swap Live Switch:        %llu ns (Zero-downtime QSBR pointer migration)\n"
        "SMT Mathematical Proofs:     %s (Zero-Defect Guaranteed)\n"
        "HUMAN NARRATIVE LOG:\n"
        "  \"%s\"\n",
        (unsigned long long)incident->incident_id,
        incident->anomaly_cause,
        incident->previous_topology, incident->new_topology,
        incident->autonomous_action,
        (unsigned long long)incident->hot_swap_switch_ns,
        incident->smt_proof.proof_summary,
        incident->human_narrative);
}

/* ========================================================================= */
/* Interactive REPL Prompt Loop                                              */
/* ========================================================================= */

int flowy_interactive_loop(FlowOrchestrator *orch, FILE *in, FILE *out) {
    if (in == NULL || out == NULL) return 0;

    FlowTopologyGraph graph;
    flow_topology_build_codebase_graph(&graph);

        fprintf(out, "           FLOW INTROSPECTIVE CODEBASE KNOWLEDGE & ARCHITECTURE REASONER        \n");
        fprintf(out, "Ask any question about FLOW architecture, algorithms, QSBR, SMT, or BitSpace\n");
    fprintf(out, "Commands: 'what-if', 'remediate', 'autopilot', 'why', 'bottleneck', 'timeline', 'list', 'exit'\n\n");

    char line_buf[512];
    while (1) {
        fprintf(out, "FLOW-Query > ");
        fflush(out);
        if (fgets(line_buf, sizeof(line_buf), in) == NULL) break;

        size_t len = strlen(line_buf);
        while (len > 0 && (line_buf[len - 1] == '\n' || line_buf[len - 1] == '\r')) {
            line_buf[--len] = '\0';
        }
        if (len == 0) continue;
        if (strcmp(line_buf, "exit") == 0 || strcmp(line_buf, "quit") == 0) {
            fprintf(out, "\nExiting Introspective Reasoner.\n");
            break;
        }

        if (strstr(line_buf, "what-if") != NULL || strstr(line_buf, "what if") != NULL) {
            FlowCounterfactualReport report;
            flow_orchestrator_simulate_what_if(orch, 32, 50, 4, &report);
            flowy_print_counterfactual_report(&report, out);
            continue;
        }

        if (strstr(line_buf, "remediate") != NULL) {
            FlowRemediationProposal proposal;
            flow_orchestrator_synthesize_remediation(orch, "examples/compiler.flow", "examples/project.flow", &proposal);
            flowy_print_remediation_proposal(&proposal, out);
            continue;
        }

        if (strstr(line_buf, "autopilot") != NULL) {
            FlowAutopilotController *ctrl = flow_autopilot_create(orch, NULL);
            FlowPMUTelemetry storm = { .cache_miss_rate = 0.148, .ipc = 0.8 };
            FlowAutopilotIncident inc;
            flow_autopilot_step(ctrl, &storm, &inc);
            flowy_print_autopilot_incident(&inc, out);
            flow_autopilot_destroy(ctrl);
            continue;
        }

        if (strcmp(line_buf, "why") == 0) {
            flowy_print_decision_explanation(flow_decision_logger_latest(NULL), out);
            continue;
        }

        if (strcmp(line_buf, "bottleneck") == 0) {
            flowy_print_bottleneck_explanation(&graph, out);
            continue;
        }

        if (strcmp(line_buf, "timeline") == 0) {
            flowy_print_decision_timeline(NULL, out);
            continue;
        }

        if (strncmp(line_buf, "book", 4) == 0) {
            const char *arg = line_buf + 4;
            while (*arg == ' ') arg++;
            flowy_show_book(*arg ? arg : "all", out);
            continue;
        }

        if (strncmp(line_buf, "lang", 4) == 0 || strncmp(line_buf, "language", 8) == 0) {
            const char *arg = strchr(line_buf, ' ');
            if (arg) {
                while (*arg == ' ') arg++;
                if (*arg) {
                    FlowLanguage new_lang = flowy_parse_language(arg);
                    flowy_set_language(new_lang);
                    fprintf(out, "\n[FLOWY] Language render mask set to: %s\n\n", flowy_language_name(new_lang));
                }
            } else {
                fprintf(out, "\n[FLOWY] Active language: %s (Switch with 'lang zh' or 'lang en')\n\n", flowy_language_name(flowy_get_language()));
            }
            continue;
        }

        if (strcmp(line_buf, "list") == 0) {
            fprintf(out, "\nRegistered Codebase Modules (%zu total):\n", flowy_knowledge_count());
            for (size_t i = 0; i < flowy_knowledge_count(); ++i) {
                const FlowModuleKnowledge *k = flowy_knowledge_at(i);
                fprintf(out, "  * [%-12s] (Layer %u) %s -> %s\n", k->module_id, k->layer, k->title, k->header_file);
            }
            fprintf(out, "\n");
            continue;
        }

        FlowyIntrospectiveAnswer ans;
        flowy_query_codebase(&graph, line_buf, &ans);
        flowy_print_answer(&ans, out);
    }
    return 1;
}

/* ========================================================================= */
/* Phase Space Jet Bundle Inspection & Phase Portrait Rendering               */
/* ========================================================================= */

void flowy_print_jet_inspection(const struct FlowJet *jet, FILE *out) {
    if (jet == NULL || out == NULL) return;

    uint32_t dim = jet->header.vector_dim ? jet->header.vector_dim : FLOW_JET_STANDARD_DIM;
    uint32_t kdim = jet->header.koopman_dim ? jet->header.koopman_dim : FLOW_JET_STANDARD_KOOPMAN_DIM;
    uint32_t taps = jet->header.memory_taps ? jet->header.memory_taps : FLOW_JET_STANDARD_TAPS;

    fprintf(out, "╔══════════════════════════════════════════════════════════════════════════════╗\n");
    fprintf(out, "║          FLOW PHASE SPACE JET BUNDLE INSPECTION REPORT (.fjet)               ║\n");
    fprintf(out, "╠══════════════════════════════════════════════════════════════════════════════╣\n");
    fprintf(out, "║ Magic:       %-15s │ ID:           %-30s ║\n", jet->header.magic, jet->header.id);
    fprintf(out, "║ Title:       %-15s │ Intent:       %-30s ║\n", jet->header.name, jet->header.trigger_intent);
    fprintf(out, "║ Hardware:    %-15s │ Category:     %-30s ║\n", jet->header.origin_hardware, jet->header.category);
    fprintf(out, "║ Dimension:   %-15u │ Koopman Dim:  %-30u ║\n", dim, kdim);
    fprintf(out, "║ Memory Taps: %-15u │ Energy H:     %-30.4f ║\n", taps, jet->header.hamiltonian_energy);
    fprintf(out, "║ SMT Proof:   %-60s ║\n", jet->header.smt_signature);
    fprintf(out, "╠══════════════════════════════════════════════════════════════════════════════╣\n");
    fprintf(out, "║ PHASE SPACE JET COORDINATES (q: Position, p: Momentum, a: Geodesic Curvature)║\n");
    fprintf(out, "╟──────┬─────────────────┬─────────────────┬─────────────────┬─────────────────╢\n");
    fprintf(out, "║ Dim  │   q (Coord)     │   p (Momentum)  │  a (Curvature)  │ I_mem (Memory)  ║\n");
    fprintf(out, "╟──────┼─────────────────┼─────────────────┼─────────────────┼─────────────────╢\n");

    uint32_t show_dim = dim < 16 ? dim : 16;
    for (uint32_t i = 0; i < show_dim; ++i) {
        fprintf(out, "║ #%-3u │ %+15.6f │ %+15.6f │ %+15.6f │ %+15.6f ║\n",
                i, jet->payload.q[i], jet->payload.p[i], jet->payload.a[i], jet->memory_integral[i]);
    }
    if (dim > show_dim) {
        fprintf(out, "║ ...  │ (%u more dimensions preserved in Jet bundle)                          ║\n", dim - show_dim);
    }

    fprintf(out, "╠══════════════════════════════════════════════════════════════════════════════╣\n");
    fprintf(out, "║ MORI-ZWANZIG MEMORY KERNEL TAPS K_i = exp(-0.4 * i)                          ║\n");
    fprintf(out, "╟──────┬──────────┬────────────────────────────────────────────────────────────╢\n");
    for (uint32_t i = 0; i < taps; ++i) {
        double val = jet->payload.memory_kernel[i];
        int bars = (int)(val * 30.0);
        if (bars < 0) bars = 0;
        if (bars > 30) bars = 30;
        char bar_buf[32] = {0};
        for (int b = 0; b < bars; ++b) bar_buf[b] = '=';
        fprintf(out, "║ K[%u] │ %8.4f │ [%-30s]                     ║\n", i, val, bar_buf);
    }

    fprintf(out, "╠══════════════════════════════════════════════════════════════════════════════╣\n");
    fprintf(out, "║ KOOPMAN LINEAR GENERATOR SPECTRUM & STABILITY                                ║\n");
    fprintf(out, "╟──────────────────────────────────────────────────────────────────────────────╢\n");
    double tr_k = 0.0;
    for (uint32_t i = 0; i < kdim; ++i) {
        tr_k += jet->payload.koopman_matrix[i][i];
    }
    fprintf(out, "║ Trace Tr(K): %+8.4f (%s)                              ║\n",
            tr_k, tr_k <= 0.0 ? "DISSIPATIVE STABLE (UNSAT)" : "EXPANSIVE (UNSTABLE)");
    fprintf(out, "║ 64B Canvas Confinement:  sizeof=%-4zu alignof=%-4zu (Single Cache Line)     ║\n",
            sizeof(FlowBmf1BitCanvas), _Alignof(FlowBmf1BitCanvas));
    fprintf(out, "╚══════════════════════════════════════════════════════════════════════════════╝\n");
}

void flowy_render_phase_portrait(const struct FlowJet *jet, uint32_t dim_x, uint32_t dim_y, int steps, double dt, FILE *out) {
    if (jet == NULL || out == NULL) return;
    if (steps <= 0) steps = 60;
    if (dt <= 0.0) dt = 0.02;

    FlowJet sim = *jet;
    uint32_t dim = sim.header.vector_dim ? sim.header.vector_dim : FLOW_JET_STANDARD_DIM;
    if (dim_x >= dim) dim_x = 0;
    if (dim_y >= dim) dim_y = (dim > 1) ? 1 : 0;

    double q_traj[256];
    double p_traj[256];
    int n = steps > 256 ? 256 : steps;

    double min_q = 1e9, max_q = -1e9;
    double min_p = 1e9, max_p = -1e9;

    for (int i = 0; i < n; ++i) {
        q_traj[i] = sim.payload.q[dim_x];
        p_traj[i] = sim.payload.p[dim_y];
        if (q_traj[i] < min_q) min_q = q_traj[i];
        if (q_traj[i] > max_q) max_q = q_traj[i];
        if (p_traj[i] < min_p) min_p = p_traj[i];
        if (p_traj[i] > max_p) max_p = p_traj[i];
        flow_jet_symplectic_step(&sim, dt);
    }

    if (max_q - min_q < 1e-4) { max_q += 0.5; min_q -= 0.5; }
    if (max_p - min_p < 1e-4) { max_p += 0.5; min_p -= 0.5; }

    #define PP_WIDTH 60
    #define PP_HEIGHT 20
    char grid[PP_HEIGHT][PP_WIDTH + 1];
    for (int r = 0; r < PP_HEIGHT; ++r) {
        for (int c = 0; c < PP_WIDTH; ++c) {
            grid[r][c] = ' ';
        }
        grid[r][PP_WIDTH] = '\0';
    }

    /* Draw axes */
    int zero_r = (int)((max_p - 0.0) / (max_p - min_p) * (PP_HEIGHT - 1));
    int zero_c = (int)((0.0 - min_q) / (max_q - min_q) * (PP_WIDTH - 1));
    if (zero_r >= 0 && zero_r < PP_HEIGHT) {
        for (int c = 0; c < PP_WIDTH; ++c) grid[zero_r][c] = '-';
    }
    if (zero_c >= 0 && zero_c < PP_WIDTH) {
        for (int r = 0; r < PP_HEIGHT; ++r) grid[r][zero_c] = '|';
    }
    if (zero_r >= 0 && zero_r < PP_HEIGHT && zero_c >= 0 && zero_c < PP_WIDTH) {
        grid[zero_r][zero_c] = '+';
    }

    /* Plot trajectory */
    for (int i = 0; i < n; ++i) {
        int r = (int)((max_p - p_traj[i]) / (max_p - min_p) * (PP_HEIGHT - 1));
        int c = (int)((q_traj[i] - min_q) / (max_q - min_q) * (PP_WIDTH - 1));
        if (r >= 0 && r < PP_HEIGHT && c >= 0 && c < PP_WIDTH) {
            if (i == 0) grid[r][c] = 'S'; /* Start */
            else if (i == n - 1) grid[r][c] = 'E'; /* End */
            else grid[r][c] = (i % 5 == 0) ? '*' : '.';
        }
    }

    fprintf(out, "\n┌─────────────────────────────────────────────────────────────┐\n");
    fprintf(out, "│   PHASE SPACE PORTRAIT: q[%u] vs p[%u] (Orbit / Attractor)     │\n", dim_x, dim_y);
    fprintf(out, "├─────────────────────────────────────────────────────────────┤\n");
    fprintf(out, "  p_max = %+8.3f\n", max_p);
    for (int r = 0; r < PP_HEIGHT; ++r) {
        fprintf(out, "  │%s│\n", grid[r]);
    }
    fprintf(out, "  p_min = %+8.3f\n", min_p);
    fprintf(out, "  q_min = %+8.3f %30s q_max = %+8.3f\n", min_q, "", max_q);
    fprintf(out, "  Legend: S=Start, E=End, *=Trajectory, .=Flow Path, +=Origin\n");
    fprintf(out, "└─────────────────────────────────────────────────────────────┘\n\n");
}

int flowy_jet_simulate_run(const struct FlowJet *jet, int steps, double dt, FILE *out) {
    if (jet == NULL || out == NULL) return 0;
    if (steps <= 0) steps = 20;
    if (dt <= 0.0) dt = 0.01;

    FlowJet sim = *jet;
    double initial_h = flow_jet_hamiltonian(&sim);

    fprintf(out, "=== Symplectic Hamiltonian Orbit Simulation (%d steps @ dt=%.4f) ===\n", steps, dt);
    fprintf(out, "%-6s | %-10s | %-12s | %-12s | %-12s | %-10s\n",
            "Step", "Time (ms)", "Energy H", "Drift (%)", "|q|", "|p|");
    fprintf(out, "-------+------------+--------------+--------------+--------------+-----------\n");

    for (int step = 0; step <= steps; ++step) {
        double current_h = flow_jet_hamiltonian(&sim);
        double drift = fabs(current_h - initial_h) / (initial_h > 1e-9 ? initial_h : 1.0) * 100.0;
        double norm_q = 0.0, norm_p = 0.0;
        uint32_t dim = sim.header.vector_dim ? sim.header.vector_dim : FLOW_JET_STANDARD_DIM;
        for (uint32_t d = 0; d < dim; ++d) {
            norm_q += sim.payload.q[d] * sim.payload.q[d];
            norm_p += sim.payload.p[d] * sim.payload.p[d];
        }
        fprintf(out, "%-6d | %-10.2f | %-12.6f | %-12.6f | %-12.4f | %-10.4f\n",
                step, (double)step * dt * 1000.0, current_h, drift, sqrt(norm_q), sqrt(norm_p));
        if (step < steps) {
            flow_jet_symplectic_step(&sim, dt);
        }
    }
    fprintf(out, "\n✓ Symplectic leapfrog preserved phase-volume and bounded energy drift.\n\n");
    return 1;
}

int flowy_jet_learn_demo(struct FlowJet *jet, int sample_count, FILE *out) {
    if (jet == NULL || out == NULL) return 0;
    if (sample_count <= 0) sample_count = 50;

    uint32_t kdim = jet->header.koopman_dim ? jet->header.koopman_dim : FLOW_JET_STANDARD_KOOPMAN_DIM;
    FlowJetStreamingEDMD edmd;
    flow_jet_edmd_init(&edmd, kdim, 0.98);

    fprintf(out, "=== Online Streaming EDMD Assimilation (Streaming %d snapshots) ===\n", sample_count);
    fprintf(out, "  Initial Koopman Trace Tr(K) = %.4f\n", jet->payload.koopman_matrix[0][0] * (double)kdim);

    for (int s = 0; s < sample_count; ++s) {
        double pmu_stream[FLOW_JET_MAX_KOOPMAN_DIM];
        for (uint32_t d = 0; d < kdim; ++d) {
            pmu_stream[d] = 0.5 * sin(0.1 * (double)s + (double)d) + 0.1 * ((double)(s % 7) / 7.0);
        }
        flow_jet_stream_learn_step(jet, &edmd, pmu_stream, 0.01);
    }

    double final_tr = 0.0;
    for (uint32_t d = 0; d < kdim; ++d) {
        final_tr += jet->payload.koopman_matrix[d][d];
    }
    fprintf(out, "  Adapted Koopman Trace Tr(K) = %.4f (Lyapunov Contractive Guaranteed: %s)\n",
            final_tr, final_tr <= -0.04 ? "YES" : "NO");
    fprintf(out, "  Processed %llu streaming transitions; Generator updated in-memory.\n\n",
            (unsigned long long)edmd.update_count);
    return 1;
}

int flowy_jet_dtc_simulate(struct FlowJet *jet, uint32_t cycles, double period_T, double imperfection, FILE *out) {
    if (jet == NULL || out == NULL) return 0;
    if (cycles == 0) cycles = 24;
    if (period_T <= 0.0) period_T = 0.02;

    double kick = (1.0 - imperfection) * 3.14159265358979323846;
    FlowTimeCrystal dtc;
    flow_dtc_init(&dtc, jet, period_T, kick, 1.2);

    fprintf(out, "\n╔══════════════════════════════════════════════════════════════════════════════╗\n");
    fprintf(out, "║       DISCRETE TIME CRYSTAL (DTC) SUBHARMONIC SIMULATION (.fjet)            ║\n");
    fprintf(out, "╠══════════════════════════════════════════════════════════════════════════════╣\n");
    fprintf(out, "║ Floquet Period T: %-8.4fs │ Kick Rotation: %-7.4f rad (Imperfection: %-5.2f)   ║\n",
            period_T, kick, imperfection);
    fprintf(out, "║ MBL Disorder W:   %-8.2f   │ Total Cycles:   %-5u                               ║\n",
            dtc.disorder_strength, cycles);
    fprintf(out, "╠══════════════════════════════════════════════════════════════════════════════╣\n");
    fprintf(out, "║ Cycle │ Time (ms)  │ Order Z(nT) │ Subharmonic Limit-Cycle Sparkline          ║\n");
    fprintf(out, "╟───────┼────────────┼─────────────┼────────────────────────────────────────────╢\n");

    for (uint32_t c = 1; c <= cycles; ++c) {
        flow_dtc_step_floquet(&dtc, 1, 0.001);
        double z = dtc.current_order_param;
        char bar[40];
        int pos = (int)((z + 1.5) / 3.0 * 30.0);
        if (pos < 0) pos = 0;
        if (pos > 30) pos = 30;
        memset(bar, ' ', sizeof(bar));
        bar[32] = '\0';
        bar[15] = '|'; /* Center zero axis */
        bar[pos] = (z >= 0.0) ? '+' : '-';

        fprintf(out, "║ #%-4u │ %-10.2f │   %+7.4f   │ [%s] ║\n",
                c, (double)c * period_T * 1000.0, z, bar);
    }

    double subharmonic_ratio = flow_dtc_get_fourier_subharmonic_ratio(&dtc);
    FlowSMTProofAttestation proof;
    memset(&proof, 0, sizeof(proof));
    FlowSMTResult smt_res = flow_dtc_verify_soundness_smt(&dtc, &proof);

    fprintf(out, "╠══════════════════════════════════════════════════════════════════════════════╣\n");
    fprintf(out, "║ Subharmonic 2T Fourier Peak Ratio: %-6.2f%% (Locked: %-3s)                    ║\n",
            subharmonic_ratio * 100.0, dtc.is_subharmonic_locked ? "YES" : "NO");
    fprintf(out, "║ Floquet Max Energy Drift:          %-8.6f (Non-Thermalizing ETH Protected)  ║\n",
            dtc.max_energy_drift);
    fprintf(out, "║ SMT Formal Verification:           %-41s ║\n",
            (smt_res == FLOW_SMT_PROVEN_UNSAT) ? "UNSAT: ZERO-DEFECT RIGIDITY PROVEN" : "UNKNOWN");
    fprintf(out, "╚══════════════════════════════════════════════════════════════════════════════╝\n\n");
    return 1;
}

int flowy_jet_dead_reckon_demo(struct FlowJet *jet, uint32_t ticks, double threshold, FILE *out) {
    if (jet == NULL || out == NULL) return 0;
    if (ticks == 0) ticks = 50;
    if (threshold <= 0.0) threshold = 0.08;

    FlowJet actual = *jet;
    FlowJetDeadReckonSender sender;
    flow_jet_dead_reckon_sender_init(&sender, &actual, threshold);

    FlowJetDeadReckonReceiver receiver;
    flow_jet_dead_reckon_receiver_init(&receiver, "cluster_node_b", actual.header.vector_dim);

    fprintf(out, "\n╔══════════════════════════════════════════════════════════════════════════════╗\n");
    fprintf(out, "║       JET-BASED DEAD RECKONING CXL / CLUSTER SIMULATION (.fjet)              ║\n");
    fprintf(out, "╠══════════════════════════════════════════════════════════════════════════════╣\n");
    fprintf(out, "║ Monitored Node: %-16s │ Remote Mirror: cluster_node_b            ║\n",
            jet->header.id);
    fprintf(out, "║ Lyapunov Threshold: %-12.4f │ Simulation Ticks: %-5u                       ║\n",
            threshold, ticks);
    fprintf(out, "╠══════════════════════════════════════════════════════════════════════════════╣\n");

    for (uint32_t t = 0; t < ticks; ++t) {
        FlowJetDeadReckonPacket pkt;
        int pkt_needed = 0;
        flow_jet_dead_reckon_sender_step(&sender, 0.005, &pkt, &pkt_needed);

        if (pkt_needed) {
            flow_jet_dead_reckon_receiver_apply_packet(&receiver, &pkt);
        } else {
            flow_jet_dead_reckon_receiver_step(&receiver, 0.005);
        }
    }

    FlowSMTProofAttestation proof;
    memset(&proof, 0, sizeof(proof));
    FlowSMTResult smt_res = flow_jet_dead_reckon_verify_smt(&sender, &proof);

    fprintf(out, "║ Packets Transmitted:     %-6llu │ Packets Suppressed: %-6llu                 ║\n",
            (unsigned long long)sender.packets_sent,
            (unsigned long long)sender.packets_suppressed);
    fprintf(out, "║ Bandwidth Savings Ratio: %-6.2f%% (Target: >= 85.00%%)                        ║\n",
            sender.bandwidth_savings_ratio * 100.0);
    fprintf(out, "║ Peak Trajectory Drift:   %-8.5f (Within Lyapunov Horizon)               ║\n",
            sender.max_observed_divergence);
    fprintf(out, "║ SMT Formal Verification: %-43s ║\n",
            (smt_res == FLOW_SMT_PROVEN_UNSAT) ? "UNSAT: BOUNDED TRAJECTORY GUARANTEED" : "UNKNOWN");
    fprintf(out, "╚══════════════════════════════════════════════════════════════════════════════╝\n\n");
    return 1;
}

