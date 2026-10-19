#ifndef FLOW_FLOWY_H
#define FLOW_FLOWY_H
#include "flow.h"
#include "topology.h"
#include "orchestrator.h"
#include "audit.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#ifndef FLOW_LANG_ENUM_DEFINED
#define FLOW_LANG_ENUM_DEFINED
typedef enum { FLOW_LANG_ZH = 0, FLOW_LANG_EN = 1
} FlowLanguage;
#endif
typedef struct { const char *module_id; const char *title; const char *header_file; const char *source_file; uint32_t layer; /* 0=Core, 1=Interface, 2=Plugin */
    const char *responsibilities; const char *algorithmic_guarantee; const char *memory_concurrency_model; const char *key_apis; const char *keywords; } FlowModuleKnowledge;
typedef struct { const FlowModuleKnowledge *primary_module; const FlowModuleKnowledge *related_modules[4]; size_t related_count; char query[256]; char explanation[4096];
    uint32_t matched_score; } FlowyIntrospectiveAnswer; size_t flowy_knowledge_count(void); const FlowModuleKnowledge *flowy_knowledge_at(size_t index);
const FlowModuleKnowledge *flowy_knowledge_lookup(const char *module_id); int flowy_register_dynamic_module(const FlowModuleKnowledge *knowledge);
int flowy_query_codebase(const FlowTopologyGraph *graph, const char *query_text, FlowyIntrospectiveAnswer *answer_out);
int flowy_query_codebase_lang(const FlowTopologyGraph *graph, const char *query_text, FlowLanguage lang, FlowyIntrospectiveAnswer *answer_out);
void flowy_explain_decision(const FlowDecisionEvent *event, char *buf_out, size_t max_len);
void flowy_explain_decision_lang(const FlowDecisionEvent *event, FlowLanguage lang, char *buf_out, size_t max_len);
int flowy_explain_bottleneck(const FlowTopologyGraph *graph, char *buf_out, size_t max_len);
int flowy_explain_bottleneck_lang(const FlowTopologyGraph *graph, FlowLanguage lang, char *buf_out, size_t max_len);
#include "flowy_cli.h"
#endif /* FLOW_FLOWY_H */
