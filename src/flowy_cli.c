#include "flowy_cli.h"
#include "flowy.h"
#include "audit.h"
#include "generated_book_knowledge.h"
#include "generated_knowledge.h"
#include "orchestrator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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
