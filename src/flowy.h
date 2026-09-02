#ifndef FLOW_FLOWY_H
#define FLOW_FLOWY_H

#include "flow.h"
#include "orchestrator.h"
#include "bitspace.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    FLOWY_INTENT_OPTIMIZE_MEMORY = 0,
    FLOWY_INTENT_OPTIMIZE_LATENCY = 1,
    FLOWY_INTENT_ENFORCE_SECURITY = 2,
    FLOWY_INTENT_EMBODIED_ROBOTICS = 3,
    FLOWY_INTENT_SMT_PROVE = 4,
    FLOWY_INTENT_GENERAL_STATUS = 5,
    FLOWY_INTENT_UNKNOWN = 6
} FlowyIntentKind;

typedef struct {
    FlowyIntentKind kind;
    char raw_prompt[512];
    char target_component[64];
    uint64_t synthesized_mask;
    double memory_target_mb;
    double latency_target_ms;
    bool require_strict_audit;
    bool request_jit_apply;
} FlowyUserIntent;

typedef struct {
    FlowyUserIntent intent;
    FlowOrchestratorEpoch epoch_result;
    double initial_ram_mb;
    double optimized_ram_mb;
    double ram_reduction_percent;
    double initial_latency_ms;
    double optimized_latency_ms;
    double latency_reduction_percent;
    char explanation[1024];
    char ascii_art[512];
} FlowyResponse;

/* 1. Natural Language Intent Parser */
int flowy_parse_intent(const char *natural_language_input, FlowyUserIntent *intent_out);

/* 2. 1-Bit Chaos Annealing & Constraint Optimization Bridge */
int flowy_process_with_chaos(FlowOrchestrator *orch,
                             const FlowyUserIntent *intent,
                             FlowyResponse *response_out);

/* 3. Human-Centric Topological Dialogue Renderer */
void flowy_render_response(const FlowyResponse *response, FILE *out);

/* 4. Interactive CLI Assistant Loop */
int flowy_interactive_loop(FlowOrchestrator *orch, FILE *in, FILE *out);

#endif
