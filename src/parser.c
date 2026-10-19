#include "flow.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    SECTION_NONE, SECTION_INPUT, SECTION_OUTPUT, SECTION_STATE, SECTION_FLOW,
    SECTION_REQUIRE, SECTION_ENSURE, SECTION_PREFER, SECTION_RESOURCE,
    SECTION_CAPABILITY, SECTION_DOMAIN, SECTION_JET, SECTION_THERMAL
} Section;

static char *trim(char *text) {
    while (isspace((unsigned char)*text)) ++text;
    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) --end;
    *end = '\0';
    return text;
}

static int starts_with(const char *text, const char *prefix) {
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

static void copy_word(char *dest, size_t size, const char *line) {
    if (sscanf(line, "%63s", dest) != 1) dest[0] = '\0';
    dest[size - 1] = '\0';
    char *brace = strchr(dest, '{');
    if (brace) *brace = '\0';
}

static double parse_val_with_unit(const char *str) {
    char *end = NULL;
    double v = strtod(str, &end);
    if (end) {
        while (isspace((unsigned char)*end)) ++end;
        if (strcasecmp(end, "khz") == 0) v *= 1e3;
        else if (strcasecmp(end, "mhz") == 0) v *= 1e6;
        else if (strcasecmp(end, "ms") == 0) v *= 1e-3;
        else if (strcasecmp(end, "us") == 0) v *= 1e-6;
        else if (strcasecmp(end, "%") == 0) v *= 1e-2;
    }
    return v;
}

static int parse_constraint(FlowConstraint *c, const char *line) {
    char name[FLOW_NAME] = {0}, op[FLOW_NAME] = {0}, val[FLOW_NAME] = {0};
    int fields = sscanf(line, "%63s %63s %63s", name, op, val);
    if (fields < 1) return 0;
    memset(c, 0, sizeof(*c));
    strncpy(c->name, name, sizeof(c->name) - 1);
    if (fields >= 2) strncpy(c->operator, op, sizeof(c->operator) - 1);
    if (fields >= 3) strncpy(c->value, val, sizeof(c->value) - 1);
    strncpy(c->expression, line, sizeof(c->expression) - 1);
    return 1;
}

static void parse_flow_nodes(FlowSpec *spec, const char *expression) {
    char buffer[FLOW_LINE];
    strncpy(buffer, expression, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    for (char *node = strtok(buffer, "->"); node && spec->flow_node_count < FLOW_NODE_MAX; node = strtok(NULL, "->")) {
        node = trim(node);
        if (*node) {
            strncpy(spec->flow_nodes[spec->flow_node_count].name, node, FLOW_NAME - 1);
            spec->flow_nodes[spec->flow_node_count++].name[FLOW_NAME - 1] = '\0';
        }
    }
}

static int is_lean_kw(const char *line) {
    return strcmp(line, "lean_certified") == 0 || strcmp(line, "proof_carrying") == 0 || strcmp(line, "no_sorry") == 0;
}

int parse_spec(FILE *input, FlowSpec *spec) {
    char raw[FLOW_LINE];
    Section section = SECTION_NONE;
    int line_number = 0, parse_error = 0, brace_depth = 0;
    int is_literate = 0, in_code_block = 0, flow_block_completed = 0;

    long start_pos = ftell(input);
    if (start_pos >= 0) {
        while (fgets(raw, sizeof(raw), input)) {
            char *p = trim(raw);
            if (strncmp(p, "```flow", 7) == 0 || strncmp(p, "~~~flow", 7) == 0) { is_literate = 1; break; }
        }
        fseek(input, start_pos, SEEK_SET);
    }

    memset(spec, 0, sizeof(*spec));
    while (fgets(raw, sizeof(raw), input)) {
        ++line_number;
        char *line = trim(raw);
        if (is_literate) {
            if (!in_code_block) {
                if (strncmp(line, "```flow", 7) == 0 || strncmp(line, "~~~flow", 7) == 0) {
                    if (flow_block_completed) break;
                    in_code_block = 1;
                }
                continue;
            } else if (strncmp(line, "```", 3) == 0 || strncmp(line, "~~~", 3) == 0) {
                in_code_block = 0;
                if (spec->flow_name[0] && spec->input_name[0]) { flow_block_completed = 1; break; }
                continue;
            }
        }

        if (!*line || *line == '#' || strncmp(line, "//", 2) == 0) continue;
        if (strchr(line, '{')) ++brace_depth;

        static const struct { const char *prefix; Section sec; size_t offset; } SEC_HDRS[] = {
            {"input ",      SECTION_INPUT,      offsetof(FlowSpec, input_name)},
            {"output ",     SECTION_OUTPUT,     offsetof(FlowSpec, output_name)},
            {"state ",      SECTION_STATE,      offsetof(FlowSpec, state_name)},
            {"flow ",       SECTION_FLOW,       offsetof(FlowSpec, flow_name)},
            {"resource ",   SECTION_RESOURCE,   offsetof(FlowSpec, resource_name)},
            {"capability ", SECTION_CAPABILITY, offsetof(FlowSpec, capability_name)},
            {"domain ",     SECTION_DOMAIN,     offsetof(FlowSpec, domain_name)}
        };
        int matched_hdr = 0;
        for (size_t i = 0; i < sizeof(SEC_HDRS) / sizeof(SEC_HDRS[0]); ++i) {
            if (starts_with(line, SEC_HDRS[i].prefix)) {
                section = SEC_HDRS[i].sec;
                copy_word((char *)spec + SEC_HDRS[i].offset, FLOW_NAME, line + strlen(SEC_HDRS[i].prefix));
                matched_hdr = 1;
                break;
            }
        }
        if (matched_hdr) continue;

        if (strcmp(line, "require {") == 0) { section = SECTION_REQUIRE; continue; }
        if (strcmp(line, "prefer {") == 0)  { section = SECTION_PREFER; continue; }
        if (strcmp(line, "ensure {") == 0)  { section = SECTION_ENSURE; continue; }
        if (starts_with(line, "jet ") || strcmp(line, "jet {") == 0) {
            section = SECTION_JET; spec->jet.enabled = 1;
            if (starts_with(line, "jet ")) copy_word(spec->jet.name, FLOW_NAME, line + 4);
            continue;
        }
        if (starts_with(line, "thermal ") || strcmp(line, "thermal {") == 0) {
            section = SECTION_THERMAL; spec->thermal.enabled = 1;
            if (starts_with(line, "thermal ")) copy_word(spec->thermal.name, FLOW_NAME, line + 8);
            continue;
        }
        if (starts_with(line, "project ")) {
            section = SECTION_NONE;
            copy_word(spec->project_name, FLOW_NAME, line + 8);
            if (!spec->flow_name[0]) strncpy(spec->flow_name, spec->project_name, FLOW_NAME - 1);
            continue;
        }
        if (starts_with(line, "import ")) {
            section = SECTION_NONE;
            char mod[FLOW_NAME] = {0};
            copy_word(mod, sizeof(mod), line + 7);
            if (!mod[0]) { fprintf(stderr, "flowc: invalid import declaration at line %d\n", line_number); parse_error = 1; }
            else {
                strncpy(spec->plugin_name, mod, FLOW_NAME - 1);
                if (spec->imported_module_count < 8)
                    strncpy(spec->imported_modules[spec->imported_module_count++], mod, FLOW_NAME - 1);
            }
            continue;
        }
        if (starts_with(line, "contract ") || starts_with(line, "fallback ")) {
            section = SECTION_NONE;
            int is_contract = starts_with(line, "contract ");
            char *dst = is_contract ? spec->contract_name : spec->fallback_policy;
            copy_word(dst, FLOW_NAME, line + 9);
            if (!dst[0]) {
                fprintf(stderr, "flowc: invalid %s declaration at line %d\n", is_contract ? "contract" : "fallback", line_number);
                parse_error = 1;
            }
            continue;
        }
        if (strcmp(line, "}") == 0) {
            if (section == SECTION_NONE || brace_depth == 0) {
                fprintf(stderr, "flowc: unexpected closing brace at line %d\n", line_number);
                parse_error = 1;
            } else { --brace_depth; section = SECTION_NONE; }
            continue;
        }

        switch (section) {
            case SECTION_INPUT: {
                int value, id, score; char trailing;
                if (sscanf(line, "max_count %d %c", &value, &trailing) == 1 && value > 0) { spec->max_count = value; break; }
                if (sscanf(line, "sample %d %d %c", &id, &score, &trailing) == 2 && spec->sample_count < FLOW_SAMPLE_MAX) {
                    spec->samples[spec->sample_count++] = (FlowSample){id, score};
                    break;
                }
                fprintf(stderr, "flowc: invalid input line %d: %s\n", line_number, line);
                parse_error = 1;
                break;
            }
            case SECTION_OUTPUT:
                if (sscanf(line, "type %63s", spec->output_type) != 1 && sscanf(line, "as %63s", spec->output_type) != 1) {
                    fprintf(stderr, "flowc: invalid output line %d: %s\n", line_number, line);
                    parse_error = 1;
                }
                break;
            case SECTION_STATE:
                if (strcmp(line, "shared") == 0) spec->shared = 1;
                else if (strcmp(line, "read_heavy") == 0) spec->read_heavy = 1;
                else if (strcmp(line, "bounded") == 0) spec->bounded = 1;
                else { fprintf(stderr, "flowc: invalid state line %d: %s\n", line_number, line); parse_error = 1; }
                break;
            case SECTION_FLOW: {
                const char *top = strstr(line, "top(");
                if (!spec->flow_expression[0]) strncpy(spec->flow_expression, line, FLOW_LINE - 1);
                parse_flow_nodes(spec, line);
                if (top) {
                    int top_value, consumed = 0;
                    if (sscanf(top + 4, "%d%n", &top_value, &consumed) != 1 || top_value <= 0 || top[4 + consumed] != ')') {
                        fprintf(stderr, "flowc: invalid top expression at line %d: %s\n", line_number, line);
                        parse_error = 1;
                    } else spec->top_n = top_value;
                }
                break;
            }
            case SECTION_REQUIRE: {
                int value; char trailing;
                if (spec->constraint_count >= FLOW_CONSTRAINT_MAX ||
                    !parse_constraint(&spec->constraints[spec->constraint_count], line)) {
                    fprintf(stderr, "flowc: invalid requirement line %d: %s\n", line_number, line);
                    parse_error = 1;
                    break;
                }
                ++spec->constraint_count;
                spec->declared_constraint_count = spec->constraint_count;
                if (strcmp(line, "deterministic") == 0) spec->deterministic = 1;
                else if (is_lean_kw(line)) { spec->lean_certified = 1; spec->deterministic = 1; }
                else if (strcmp(spec->constraints[spec->constraint_count - 1].name, "memory") == 0) {
                    if (sscanf(line, "memory < %dmb %c", &value, &trailing) != 1 || value <= 0) {
                        fprintf(stderr, "flowc: invalid requirement line %d: %s\n", line_number, line);
                        parse_error = 1;
                    } else spec->memory_mb = value;
                }
                break;
            }
            case SECTION_ENSURE:
                if (strcmp(line, "deterministic") == 0) { ++spec->ensure_count; spec->deterministic = 1; }
                else if (is_lean_kw(line)) { ++spec->ensure_count; spec->lean_certified = 1; spec->deterministic = 1; }
                else { fprintf(stderr, "flowc: invalid ensure line %d: %s\n", line_number, line); parse_error = 1; }
                break;
            case SECTION_PREFER:
                if (strcmp(line, "latency") == 0 || strcmp(line, "throughput") == 0) spec->prefer_latency = 1;
                else if (strcmp(line, "memory") == 0) spec->prefer_latency = 0;
                else { fprintf(stderr, "flowc: invalid preference line %d: %s\n", line_number, line); parse_error = 1; }
                break;
            case SECTION_JET: {
                char kw[FLOW_NAME] = {0}, val[FLOW_NAME] = {0};
                if (sscanf(line, "%63s %63s", kw, val) >= 1) {
                    double v = parse_val_with_unit(val);
                    if (strcmp(kw, "frequency") == 0 || strcmp(kw, "target_frequency") == 0) spec->jet.frequency_hz = v;
                    else if (strcmp(kw, "symplectic") == 0) spec->jet.symplectic = 1;
                    else if (strcmp(kw, "drift") == 0 || strcmp(kw, "max_drift") == 0) spec->jet.max_drift = v;
                    else if (strcmp(kw, "zmp") == 0 || strcmp(kw, "zmp_margin") == 0) spec->jet.zmp_margin = v;
                    else if (strcmp(kw, "dt") == 0) spec->jet.dt = v;
                    else if (strcmp(kw, "dim") == 0) spec->jet.dim = (size_t)v;
                    else if (spec->constraint_count < FLOW_CONSTRAINT_MAX &&
                             parse_constraint(&spec->constraints[spec->constraint_count], line)) {
                        spec->declared_constraint_count = ++spec->constraint_count;
                    }
                }
                break;
            }
            case SECTION_THERMAL: {
                char kw[FLOW_NAME] = {0}, val[FLOW_NAME] = {0};
                if (sscanf(line, "%63s %63s", kw, val) >= 1) {
                    double v = parse_val_with_unit(val);
                    if (strcmp(kw, "max_temp") == 0 || strcmp(kw, "temp_max") == 0) spec->thermal.max_temp_c = v;
                    else if (strcmp(kw, "target_temp") == 0 || strcmp(kw, "temp_target") == 0) spec->thermal.target_temp_c = v;
                    else if (strcmp(kw, "power_limit") == 0 || strcmp(kw, "power") == 0) spec->thermal.power_limit_w = v;
                    else if (strcmp(kw, "r_thermal") == 0) spec->thermal.r_thermal = v;
                    else if (strcmp(kw, "c_thermal") == 0) spec->thermal.c_thermal = v;
                    else if (spec->constraint_count < FLOW_CONSTRAINT_MAX &&
                             parse_constraint(&spec->constraints[spec->constraint_count], line)) {
                        spec->declared_constraint_count = ++spec->constraint_count;
                    }
                }
                break;
            }
            default: {
                const char *sec_err = (section == SECTION_RESOURCE) ? "resource" :
                                      (section == SECTION_CAPABILITY) ? "capability" :
                                      (section == SECTION_DOMAIN) ? "domain" : "top-level";
                fprintf(stderr, "flowc: invalid %s line %d: %s\n", sec_err, line_number, line);
                parse_error = 1;
                break;
            }
        }
    }

    if (!spec->flow_name[0]) { fprintf(stderr, "flowc: spec must define a flow\n"); return 0; }
    if (!spec->input_name[0]) { fprintf(stderr, "flowc: current backend family requires an input declaration\n"); return 0; }
    if (brace_depth != 0) { fprintf(stderr, "flowc: unclosed block at end of spec\n"); parse_error = 1; }
    if (parse_error) return 0;
    if (spec->input_name[0] && spec->max_count <= 0) { fprintf(stderr, "flowc: input must declare a positive max_count\n"); return 0; }
    if (spec->top_n <= 0) spec->top_n = 3;
    if (spec->jet.enabled) {
        if (spec->jet.frequency_hz <= 0.0) spec->jet.frequency_hz = 10000.0;
        if (spec->jet.max_drift <= 0.0) spec->jet.max_drift = 0.005;
        if (spec->jet.dt <= 0.0) spec->jet.dt = 1.0 / spec->jet.frequency_hz;
        if (spec->jet.zmp_margin <= 0.0) spec->jet.zmp_margin = 0.02;
        if (spec->jet.dim == 0) spec->jet.dim = 4;
    }
    if (spec->thermal.enabled) {
        if (spec->thermal.max_temp_c <= 0.0) spec->thermal.max_temp_c = 95.0;
        if (spec->thermal.target_temp_c <= 0.0) spec->thermal.target_temp_c = 85.0;
        if (spec->thermal.power_limit_w <= 0.0) spec->thermal.power_limit_w = 25.0;
        if (spec->thermal.r_thermal <= 0.0) spec->thermal.r_thermal = 6.0;
        if (spec->thermal.c_thermal <= 0.0) spec->thermal.c_thermal = 0.05;
    }
    return 1;
}

static int has_flow_node(const SemanticIR *ir, const char *name) {
    for (size_t i = 0; i < ir->flow_node_count; ++i)
        if (strcmp(ir->flow_nodes[i].name, name) == 0) return 1;
    return 0;
}

static void add_fact(SemanticIR *ir, FlowFactKind kind, const char *detail) {
    if (ir->fact_count >= FLOW_NODE_MAX) return;
    ir->facts[ir->fact_count].kind = kind;
    strncpy(ir->facts[ir->fact_count].detail, detail, FLOW_NAME - 1);
    ir->facts[ir->fact_count++].detail[FLOW_NAME - 1] = '\0';
    if (kind == FLOW_FACT_HOLE) ++ir->hole_count;
    else if (kind == FLOW_FACT_CONSTRAINT) ++ir->constraint_count;
}

void lower_to_ir(const FlowSpec *spec, SemanticIR *ir) {
    memset(ir, 0, sizeof(*ir));
    memcpy(ir, spec, offsetof(FlowSpec, constraint_count));
    ir->declared_constraint_count = spec->constraint_count;
    ir->fact_ordered = has_flow_node(ir, "sort");
    ir->fact_unordered = !ir->fact_ordered;
    ir->fact_range_proven = ir->input_max_count > 0;
    ir->fact_size_preserved = has_flow_node(ir, "transform") && has_flow_node(ir, "collect");
    ir->fact_mutability_read_only = !ir->state_shared && !has_flow_node(ir, "transition") &&
                                    strcmp(ir->flow_name, "state_machine") != 0;
    ir->flow_parallelizable = has_flow_node(ir, "transform") || has_flow_node(ir, "parallel");

    if (ir->state_shared) add_fact(ir, FLOW_FACT_SHARED_STATE, "shared");
    if (ir->state_bounded) add_fact(ir, FLOW_FACT_BOUNDED, "bounded");
    if (ir->flow_parallelizable) add_fact(ir, FLOW_FACT_PARALLELIZABLE, "parallelizable");
    if (ir->fact_ordered) add_fact(ir, FLOW_FACT_ORDERED, "ordered");
    if (ir->flow_node_count > 0) add_fact(ir, FLOW_FACT_COLLECTION, "pipeline");
    for (size_t i = 0; i < ir->flow_node_count; ++i) {
        if (strcmp(ir->flow_nodes[i].name, "transform") == 0) add_fact(ir, FLOW_FACT_TRANSFORM, "transform");
        if (strchr(ir->flow_nodes[i].name, '?')) add_fact(ir, FLOW_FACT_HOLE, ir->flow_nodes[i].name);
    }
    if (ir->capability_name[0]) add_fact(ir, FLOW_FACT_CAPABILITY, ir->capability_name);
    if (ir->memory_limit_mb > 0 || ir->fact_deterministic) add_fact(ir, FLOW_FACT_CONSTRAINT, "require");
    if (ir->fact_range_proven) add_fact(ir, FLOW_FACT_RANGE, "input_max_count");
    if (ir->fact_size_preserved) add_fact(ir, FLOW_FACT_SIZE, "length_preserved");
    if (ir->fact_mutability_read_only) add_fact(ir, FLOW_FACT_MUTABILITY, "read_only");
    if (ir->fact_deterministic) add_fact(ir, FLOW_FACT_DETERMINISM, "deterministic");
    if (ir->fact_lean_certified) add_fact(ir, FLOW_FACT_LEAN_CERTIFIED, "lean_certified");
    ir->hole_count += strstr(ir->flow_expression, "?") != NULL && ir->hole_count == 0;
    if (ir->jet.enabled) add_fact(ir, FLOW_FACT_CAPABILITY, "jet_phase_space");
    if (ir->thermal.enabled) add_fact(ir, FLOW_FACT_CAPABILITY, "thermal_envelope");
}
