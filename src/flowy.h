#ifndef FLOW_FLOWY_H
#define FLOW_FLOWY_H
#include "flow.h"
#include "reload.h"
#include "bitspace.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#ifndef FLOW_LANG_ENUM_DEFINED
#define FLOW_LANG_ENUM_DEFINED
typedef enum { FLOW_LANG_ZH = 0, FLOW_LANG_EN = 1 } FlowLanguage;
#endif
void flowy_explain_decision(const FlowDecisionEvent *event, char *buf_out, size_t max_len);
const char *flowy_resolve_flowc_bin(const char *argv0, char *buf, size_t buf_size);

void flowy_set_language(FlowLanguage lang); FlowLanguage flowy_get_language(void); FlowLanguage flowy_detect_system_language(void);
FlowLanguage flowy_parse_language(const char *lang_str); const char *flowy_language_name(FlowLanguage lang);
void flowy_print_decision_explanation(const FlowDecisionEvent *event, FILE *out);
void flowy_print_decision_timeline(const FlowDecisionLogger *logger, FILE *out);
struct FlowJet; void flowy_print_jet_inspection(const struct FlowJet *jet, FILE *out);
void flowy_render_phase_portrait(const struct FlowJet *jet, uint32_t dim_x, uint32_t dim_y, int steps, double dt, FILE *out);
int flowy_jet_simulate_run(const struct FlowJet *jet, int steps, double dt, FILE *out); int flowy_jet_learn_demo(struct FlowJet *jet, int sample_count, FILE *out);
void flowy_print_cubical_topos_report(FILE *out);

int cmd_inspect_run(int argc, char **argv); int cmd_fvec_run(int argc, char **argv); int cmd_jet_run(int argc, char **argv);
void cmd_inspect_print_usage(FILE *out); void cmd_fvec_print_usage(FILE *out); void cmd_jet_print_usage(FILE *out);
#endif /* FLOW_FLOWY_H */

