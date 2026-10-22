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
typedef enum { FLOW_LANG_ZH = 0, FLOW_LANG_EN = 1 } FlowLanguage;
#endif
typedef struct {
    FlowTopologyNode primary_node;
    char query[256], explanation[4096];
    uint32_t matched_score;
} FlowyIntrospectiveAnswer;
int flowy_query_codebase(const FlowTopologyGraph *graph, const char *query_text, FlowyIntrospectiveAnswer *answer_out);
void flowy_explain_decision(const FlowDecisionEvent *event, char *buf_out, size_t max_len);
int flowy_explain_bottleneck(const FlowTopologyGraph *graph, char *buf_out, size_t max_len);
#include "flowy_cli.h"
#endif /* FLOW_FLOWY_H */
