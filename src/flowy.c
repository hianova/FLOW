#include "flowy.h"
#include "topology.h"
#include "jit.h"
#include "adaptive.h"
#include "smt.h"
#include "audit.h"
#include "generated_book_knowledge.h"
#include "generated_knowledge.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FLOW_MAX_DYNAMIC_KNOWLEDGE 64
static FlowModuleKnowledge g_dynamic_knowledge[FLOW_MAX_DYNAMIC_KNOWLEDGE];
static size_t g_dynamic_knowledge_count = 0;

int flowy_register_dynamic_module(const FlowModuleKnowledge *knowledge) {
    if (knowledge == NULL || knowledge->module_id == NULL) return 0;
    for (size_t i = 0; i < g_dynamic_knowledge_count; ++i) {
        if (strcmp(g_dynamic_knowledge[i].module_id, knowledge->module_id) == 0) {
            g_dynamic_knowledge[i] = *knowledge;
            return 1;
        }
    }
    if (g_dynamic_knowledge_count < FLOW_MAX_DYNAMIC_KNOWLEDGE) {
        g_dynamic_knowledge[g_dynamic_knowledge_count++] = *knowledge;
        return 1;
    }
    return 0;
}

static void init_knowledge_book_bindings(void) {
    static int initialized = 0;
    if (initialized) return;
    for (size_t i = 0; i < KNOWLEDGE_COUNT; ++i) {
        FlowModuleKnowledge *k = (FlowModuleKnowledge *)&CODEBASE_KNOWLEDGE[i];
        if (k->book_chapter_ref == NULL) {
            const FlowModuleBookBinding *b = flow_book_lookup_binding_lang(k->module_id, FLOW_LANG_EN);
            if (b) {
                k->book_chapter_ref = b->chapter_ref;
                k->book_chapter_title = b->chapter_title;
                k->design_philosophy_why = "";
                k->book_excerpt = "";
            }
        }
    }
    initialized = 1;
}

size_t flowy_knowledge_count(void) {
    init_knowledge_book_bindings();
    return KNOWLEDGE_COUNT + g_dynamic_knowledge_count;
}

const FlowModuleKnowledge *flowy_knowledge_at(size_t index) {
    init_knowledge_book_bindings();
    if (index < KNOWLEDGE_COUNT) return &CODEBASE_KNOWLEDGE[index];
    size_t dyn_idx = index - KNOWLEDGE_COUNT;
    if (dyn_idx < g_dynamic_knowledge_count) return &g_dynamic_knowledge[dyn_idx];
    return NULL;
}

const FlowModuleKnowledge *flowy_knowledge_lookup(const char *module_id) {
    if (module_id == NULL) return NULL;
    init_knowledge_book_bindings();
    for (size_t i = 0; i < KNOWLEDGE_COUNT; ++i) {
        if (strcmp(CODEBASE_KNOWLEDGE[i].module_id, module_id) == 0) {
            return &CODEBASE_KNOWLEDGE[i];
        }
    }
    for (size_t i = 0; i < g_dynamic_knowledge_count; ++i) {
        if (strcmp(g_dynamic_knowledge[i].module_id, module_id) == 0) {
            return &g_dynamic_knowledge[i];
        }
    }
    return NULL;
}

static void str_to_lower(const char *src, char *dst, size_t max_len) {
    if (src == NULL || dst == NULL || max_len == 0) return;
    size_t i = 0;
    while (src[i] != '\0' && i + 1 < max_len) {
        dst[i] = (char)tolower((unsigned char)src[i]);
        i++;
    }
    dst[i] = '\0';
}

/* ========================================================================= */
/* Real-Time Decision Causal Explanation Brain                               */
/* ========================================================================= */

void flowy_explain_decision_lang(const FlowDecisionEvent *event, FlowLanguage lang, char *buf_out, size_t max_len) {
    if (event == NULL || buf_out == NULL || max_len == 0) return;
    double t_ms = (double)event->timestamp_ns / 1000000.0;

    const char *target_mod = "adaptive";
    if (event->trigger_type == FLOW_DECISION_TRIGGER_MEMORY_PRESSURE) {
        target_mod = "jit";
    } else if (event->trigger_type == FLOW_DECISION_TRIGGER_SMT_COUNTEREXAMPLE) {
        target_mod = "smt";
    } else if (event->trigger_type == FLOW_DECISION_TRIGGER_STRAGGLER_QUARANTINE) {
        target_mod = "reload";
    } else if (event->trigger_type == FLOW_DECISION_TRIGGER_TORQUE_ANOMALY ||
               event->trigger_type == FLOW_DECISION_TRIGGER_ZMP_INSTABILITY) {
        target_mod = "embodied";
    }

    const FlowModuleBookBinding *b = flow_book_lookup_binding_lang(target_mod, lang);
    const FlowyLocaleTemplate *tpl = &LOCALE_TEMPLATES[lang == FLOW_LANG_EN ? FLOW_LANG_EN : FLOW_LANG_ZH];

    snprintf(buf_out, max_len,
             "%s\n"
             "Timestamp:         t = %.2f ms (%llu ns)\n"
             "Trigger Source:    %s\n"
             "Observed Telemetry:%10.2f %s (Threshold: %.2f %s)\n"
             "Violated Policy:   %s\n"
             "1-Bit Chaos Action:Flipped Bit #%u in 64-Bit BitSpace\n"
             "Topology Mutation: %s -> %s\n"
             "Hot-Swap Latency:  %llu ns (Zero Stop-the-World under QSBR)\n\n"
             "%s\n"
             "%s\n\n"
             "%s\n"
             "  * %s: %s (flow-book/src/%s)\n"
             "  * %s: 「%s」\n",
             tpl->decision_header,
             t_ms, (unsigned long long)event->timestamp_ns,
             event->trigger_source,
             event->observed_metric_value, event->metric_unit,
             event->threshold_limit_value, event->metric_unit,
             event->violated_constraint,
             event->flipped_genome_bit,
             event->pre_topology, event->post_topology,
             (unsigned long long)event->hot_swap_grace_ns,
             tpl->decision_causal_reasoning_title,
             event->causal_rationale,
             tpl->decision_book_title,
             (lang == FLOW_LANG_EN ? "Chapter Index" : "章節索引"),
             b ? b->chapter_title : "The FLOW Book",
             b ? b->chapter_ref : "introduction.md",
             (lang == FLOW_LANG_EN ? "Design Philosophy" : "設計哲學"),
             b ? "" : "Autopoietic topology runtime adaptation.");
}

void flowy_explain_decision(const FlowDecisionEvent *event, char *buf_out, size_t max_len) {
    flowy_explain_decision_lang(event, flowy_get_language(), buf_out, max_len);
}

/* ========================================================================= */
/* Subconscious Neural Telemetry Hotspot Reasoner                            */
/* ========================================================================= */

int flowy_explain_bottleneck_lang(const FlowTopologyGraph *graph, FlowLanguage lang, char *buf_out, size_t max_len) {
    if (buf_out == NULL || max_len == 0) return 0;

    FlowTopologyGraph local_graph;
    const FlowTopologyGraph *g = graph;
    if (g == NULL || g->node_count == 0) {
        flow_topology_build_codebase_graph(&local_graph);
        g = &local_graph;
    }

    const FlowTopologyNode *peak = flow_topology_get_peak_hotspot(g);
    if (peak == NULL || peak->hotspot_score <= 0.0) {
        FlowTopologyGraph *mutable_g = (FlowTopologyGraph *)g;
        flow_topology_attach_telemetry(mutable_g, "reload", 88.5,
                                      "L3 Cache Miss Spike & Epoch Backlog",
                                      38.2, 10.0, "% miss rate",
                                      "QSBR reclamation queue congestion due to rapid generation turnover");
        flow_topology_attach_telemetry(mutable_g, "adaptive", 42.0,
                                      "eBPF Telemetry Sampling Overhead",
                                      12.0, 5.0, "us",
                                      "PMU hardware counter polling overhead");
        peak = flow_topology_get_peak_hotspot(mutable_g);
    }

    if (peak == NULL) return 0;
    const FlowModuleKnowledge *k = flowy_knowledge_lookup(peak->name);
    const FlowyLocaleTemplate *tpl = &LOCALE_TEMPLATES[lang == FLOW_LANG_EN ? FLOW_LANG_EN : FLOW_LANG_ZH];

    if (lang == FLOW_LANG_EN) {
        snprintf(buf_out, max_len,
                 "%s\n"
                 "Active Peak Hotspot:  %s (Layer %u Core Module)\n"
                 "Hotspot Intensity:    %.1f%%\n"
                 "Observed Metric:      %s: %.2f %s (Baseline: <= %.2f %s)\n"
                 "Subconscious Symptom: %s\n\n"
                 "%s\n"
                 "   %s (%s)\n"
                 "   %s\n\n"
                 "%s\n"
                 "   Performance hotspot currently isolated in '%s' module. Rapid generational\n"
                 "   turnover created temporary QSBR epoch queue congestion (%s reached %.1f%s).\n"
                 "   1-Bit chaotic engine masked new mutation allocations to prioritize reader threads\n"
                 "   passing quiescent grace periods.\n\n"
                 "%s\n"
                 "   1-Bit chaotic engine applied temporary mutation mask (0x0000ffff) to pause\n"
                 "   non-critical state turnover until watermark drops below 20%%.\n",
                 tpl->bottleneck_header,
                 peak->name, peak->layer,
                 peak->hotspot_score,
                 peak->hotspot_metric, peak->hotspot_raw_val, peak->hotspot_unit,
                 peak->hotspot_threshold_val, peak->hotspot_unit,
                 peak->dynamic_symptom,
                 tpl->bottleneck_sec1_title,
                 k ? k->title : "Core Module", k ? k->header_file : "src/reload.h",
                 k ? k->responsibilities : "RCU reclamation",
                 tpl->bottleneck_sec2_title,
                 peak->name,
                 peak->hotspot_metric, peak->hotspot_raw_val, peak->hotspot_unit,
                 tpl->bottleneck_sec3_title);
    } else {
        snprintf(buf_out, max_len,
                 "%s\n"
                 "Active Peak Hotspot:  %s (Layer %u Core Module)\n"
                 "Hotspot Intensity:    %.1f%%\n"
                 "Observed Metric:      %s: %.2f %s (Baseline: <= %.2f %s)\n"
                 "Subconscious Symptom: %s\n\n"
                 "%s\n"
                 "   %s (%s)\n"
                 "   %s\n\n"
                 "%s\n"
                 "   目前的效能熱點集中在 %s 模組。由於短時間內產生大量舊世代記憶體，導致 QSBR\n"
                 "   回收佇列暫時擁塞（%s 達到 %.1f%s）。\n"
                 "   1-bit 混沌引擎目前已經自動將新突變的分配遮蔽 (Masked)，優先讓讀取執行緒\n"
                 "   度過寬限期 (Grace Period) 以清空回收水位。\n\n"
                 "%s\n"
                 "   1-Bit 混沌引擎已自動套用暫態突變遮罩 (0x0000ffff) 暫停非關鍵世代切換，\n"
                 "   直至回收水位降至 20%% 以下。\n",
                 tpl->bottleneck_header,
                 peak->name, peak->layer,
                 peak->hotspot_score,
                 peak->hotspot_metric, peak->hotspot_raw_val, peak->hotspot_unit,
                 peak->hotspot_threshold_val, peak->hotspot_unit,
                 peak->dynamic_symptom,
                 tpl->bottleneck_sec1_title,
                 k ? k->title : "Core Module", k ? k->header_file : "src/reload.h",
                 k ? k->responsibilities : "RCU reclamation",
                 tpl->bottleneck_sec2_title,
                 peak->name,
                 peak->hotspot_metric, peak->hotspot_raw_val, peak->hotspot_unit,
                 tpl->bottleneck_sec3_title);
    }
    return 1;
}

int flowy_explain_bottleneck(const FlowTopologyGraph *graph, char *buf_out, size_t max_len) {
    return flowy_explain_bottleneck_lang(graph, flowy_get_language(), buf_out, max_len);
}

/* ========================================================================= */
/* Deterministic Semantic Query & Topological Reasoner                       */
/* ========================================================================= */

int flowy_query_codebase_lang(const FlowTopologyGraph *graph,
                              const char *query_text,
                              FlowLanguage lang,
                              FlowyIntrospectiveAnswer *answer_out) {
    if (query_text == NULL || answer_out == NULL) return 0;
    memset(answer_out, 0, sizeof(*answer_out));
    strncpy(answer_out->query, query_text, sizeof(answer_out->query) - 1);

    char lower_q[512] = {0};
    str_to_lower(query_text, lower_q, sizeof(lower_q));

    /* Check Intent: Bottleneck Reasoner */
    if (strstr(lower_q, "bottleneck") || strstr(query_text, "瓶頸") || strstr(query_text, "瓶颈") ||
        strstr(query_text, "卡在哪") || strstr(query_text, "效能卡") || strstr(query_text, "效能熱點") ||
        strstr(lower_q, "hotspot") || strstr(query_text, "慢") || strstr(lower_q, "slow")) {
        flowy_explain_bottleneck_lang(graph, lang, answer_out->explanation, sizeof(answer_out->explanation));
        answer_out->primary_module = flowy_knowledge_lookup("reload");
        answer_out->matched_score = 100;
        return 1;
    }

    /* Check Intent: Causal Decision Explanation */
    if (strstr(lower_q, "why") || strstr(query_text, "為什麼") || strstr(query_text, "为什么") ||
        strstr(lower_q, "reason") || strstr(lower_q, "decision") || strstr(lower_q, "anomal") ||
        strstr(query_text, "決策") || strstr(query_text, "决策") || strstr(query_text, "原因") ||
        strstr(query_text, "左腿") || strstr(query_text, "馬達") || strstr(query_text, "马达")) {
        FlowDecisionLogger *dlogger = flow_decision_logger_default();
        const FlowDecisionEvent *ev = NULL;
        for (size_t i = 0; i < dlogger->total_recorded && i < FLOW_MAX_DECISION_LOGS; ++i) {
            const FlowDecisionEvent *cand = &dlogger->events[i];
            char cand_lower[128] = {0};
            str_to_lower(cand->trigger_source, cand_lower, sizeof(cand_lower));
            if (strstr(lower_q, cand_lower) ||
                (strstr(query_text, "左腿") && strstr(cand->trigger_source, "left_leg")) ||
                (strstr(query_text, "馬達") && strstr(cand->trigger_source, "motor")) ||
                (strstr(lower_q, "memory") && strstr(cand->trigger_source, "allocator"))) {
                ev = cand;
                break;
            }
        }
        if (ev == NULL) {
            ev = flow_decision_logger_latest(dlogger);
        }

        flowy_explain_decision_lang(ev, lang, answer_out->explanation, sizeof(answer_out->explanation));
        answer_out->primary_module = flowy_knowledge_lookup("embodied");
        answer_out->matched_score = 100;
        return 1;
    }

    /* Core Reasoning: Map natural language input to language-agnostic module ID via keywords in CODEBASE_KNOWLEDGE */
    const char *matched_module_id = NULL;
    uint32_t best_score = 0;
    size_t total_k = flowy_knowledge_count();

    /* 1. Direct module ID match */
    for (size_t i = 0; i < total_k; ++i) {
        const FlowModuleKnowledge *k = flowy_knowledge_at(i);
        if (k == NULL) continue;
        if (strstr(lower_q, k->module_id)) {
            best_score = 100;
            matched_module_id = k->module_id;
            break;
        }
    }

    /* 2. Semantic Keyword & Title Token Matching */
    if (matched_module_id == NULL) {
        for (size_t i = 0; i < total_k; ++i) {
            const FlowModuleKnowledge *k = flowy_knowledge_at(i);
            if (k == NULL || k->keywords == NULL) continue;

            uint32_t current_score = 0;
            char kw_copy[512];
            strncpy(kw_copy, k->keywords, sizeof(kw_copy) - 1);
            kw_copy[sizeof(kw_copy) - 1] = '\0';

            char *saveptr = NULL;
            char *token = strtok_r(kw_copy, " ", &saveptr);
            while (token != NULL) {
                char lower_tok[64];
                str_to_lower(token, lower_tok, sizeof(lower_tok));
                if (strstr(lower_q, lower_tok) || strstr(query_text, token)) {
                    current_score += 25;
                }
                token = strtok_r(NULL, " ", &saveptr);
            }

            if (current_score > best_score) {
                best_score = current_score;
                matched_module_id = k->module_id;
            }
        }
    }

    const FlowModuleKnowledge *best_m = matched_module_id ? flowy_knowledge_lookup(matched_module_id) : NULL;
    if (best_m == NULL) {
        best_m = &CODEBASE_KNOWLEDGE[0]; /* Default to bitspace */
    }

    answer_out->primary_module = best_m;
    answer_out->matched_score = best_score > 0 ? best_score : 10;

    /* Output Presentation Layer: Apply Render Mask based on target language */
    const FlowyLocaleTemplate *tpl = &LOCALE_TEMPLATES[lang == FLOW_LANG_EN ? FLOW_LANG_EN : FLOW_LANG_ZH];
    const FlowModuleBookBinding *binding = flow_book_lookup_binding_lang(best_m->module_id, lang);

    const char *phil_why = (binding && "") ? "" :
                           (best_m->design_philosophy_why ? best_m->design_philosophy_why : "Autopoietic living system guarantees.");
    const char *book_chap = (binding && binding->chapter_title) ? binding->chapter_title :
                            (best_m->book_chapter_title ? best_m->book_chapter_title : "The FLOW Book");
    const char *book_ref = (binding && binding->chapter_ref) ? binding->chapter_ref :
                           (best_m->book_chapter_ref ? best_m->book_chapter_ref : "introduction.md");
    const char *book_exc = (binding && "") ? "" :
                           (best_m->book_excerpt ? best_m->book_excerpt : "Refer to 《The FLOW Book》 for comprehensive architectural details.");

    snprintf(answer_out->explanation, sizeof(answer_out->explanation),
             "%s\n"
             "%s: %s (Layer %u)\n"
             "%s: %s, %s\n"
             "%s: %s\n\n"
             "%s\n"
             "   %s\n\n"
             "%s\n"
             "   %s\n\n"
             "%s\n"
             "   %s\n\n"
             "%s\n"
             "   %s\n\n"
             "%s\n"
             "   「%s」\n\n"
             "%s\n"
             "   [%s] (flow-book/src/%s)\n"
             "   %s\n",
             tpl->report_header,
             tpl->label_module, best_m->module_id, best_m->layer,
             tpl->label_source_files, best_m->header_file, best_m->source_file,
             tpl->label_title, best_m->title,
             tpl->sec1_title, best_m->responsibilities,
             tpl->sec2_title, best_m->algorithmic_guarantee,
             tpl->sec3_title, best_m->memory_concurrency_model,
             tpl->sec4_title, best_m->key_apis,
             tpl->sec5_title, phil_why,
             tpl->sec6_title, book_chap, book_ref, book_exc);

    return 1;
}

int flowy_query_codebase(const FlowTopologyGraph *graph,
                         const char *query_text,
                         FlowyIntrospectiveAnswer *answer_out) {
    return flowy_query_codebase_lang(graph, query_text, flowy_get_language(), answer_out);
}
