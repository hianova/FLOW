#ifndef FLOW_FLOWY_H
#define FLOW_FLOWY_H

#include "flow.h"
#include "topology.h"
#include "orchestrator.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    const char *module_id;
    const char *title;
    const char *header_file;
    const char *source_file;
    uint32_t layer; /* 0=Core, 1=Interface, 2=Plugin */
    const char *responsibilities;
    const char *algorithmic_guarantee;
    const char *memory_concurrency_model;
    const char *key_apis;
    const char *keywords;
} FlowModuleKnowledge;

typedef struct {
    const FlowModuleKnowledge *primary_module;
    const FlowModuleKnowledge *related_modules[4];
    size_t related_count;
    char query[256];
    char explanation[2048];
    uint32_t matched_score;
} FlowyIntrospectiveAnswer;

/* Introspective Knowledge Base */
size_t flowy_knowledge_count(void);
const FlowModuleKnowledge *flowy_knowledge_at(size_t index);
const FlowModuleKnowledge *flowy_knowledge_lookup(const char *module_id);

/* Deterministic Semantic Query & Topological Reasoner */
int flowy_query_codebase(const FlowTopologyGraph *graph,
                         const char *query_text,
                         FlowyIntrospectiveAnswer *answer_out);

void flowy_print_answer(const FlowyIntrospectiveAnswer *answer, FILE *out);
int flowy_interactive_loop(FlowOrchestrator *orch, FILE *in, FILE *out);

#endif
