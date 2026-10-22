#ifndef FLOW_FLOWY_CLI_H
#define FLOW_FLOWY_CLI_H
#include "flowy.h"
#include "orchestrator.h"
#include "audit.h"
#include <stdio.h>
void flowy_set_language(FlowLanguage lang); FlowLanguage flowy_get_language(void); FlowLanguage flowy_detect_system_language(void);
FlowLanguage flowy_parse_language(const char *lang_str); const char *flowy_language_name(FlowLanguage lang);
void flowy_print_answer(const FlowyIntrospectiveAnswer *answer, FILE *out); void flowy_print_decision_explanation(const FlowDecisionEvent *event, FILE *out);
void flowy_print_decision_timeline(const FlowDecisionLogger *logger, FILE *out); void flowy_print_bottleneck_explanation(const FlowTopologyGraph *graph, FILE *out);
void flowy_print_counterfactual_report(const FlowCounterfactualReport *report, FILE *out);
void flowy_print_remediation_proposal(const FlowRemediationProposal *proposal, FILE *out);
void flowy_print_autopilot_incident(const FlowAutopilotIncident *incident, FILE *out); struct FlowJet; void flowy_print_jet_inspection(const struct FlowJet *jet, FILE *out);
void flowy_render_phase_portrait(const struct FlowJet *jet, uint32_t dim_x, uint32_t dim_y, int steps, double dt, FILE *out);
int flowy_jet_simulate_run(const struct FlowJet *jet, int steps, double dt, FILE *out); int flowy_jet_learn_demo(struct FlowJet *jet, int sample_count, FILE *out);
int flowy_interactive_loop(FlowOrchestrator *orch, FILE *in, FILE *out); void flowy_print_cubical_topos_report(FILE *out);
#endif /* FLOW_FLOWY_CLI_H */
