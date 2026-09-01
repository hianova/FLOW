#include "flow.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    SECTION_NONE,
    SECTION_INPUT,
    SECTION_OUTPUT,
    SECTION_STATE,
    SECTION_FLOW,
    SECTION_REQUIRE,
    SECTION_ENSURE,
    SECTION_PREFER,
    SECTION_RESOURCE,
    SECTION_CAPABILITY,
    SECTION_DOMAIN
} Section;

static char *trim(char *text) {
    char *end;
    while (isspace((unsigned char)*text)) ++text;
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) --end;
    *end = '\0';
    return text;
}

static int starts_with(const char *text, const char *prefix) {
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

static void copy_word(char *destination, size_t size, const char *line) {
    if (sscanf(line, "%63s", destination) != 1) destination[0] = '\0';
    destination[size - 1] = '\0';
    {
        char *brace = strchr(destination, '{');
        if (brace != NULL) *brace = '\0';
    }
}

static int parse_constraint(FlowConstraint *constraint, const char *line) {
    char name[FLOW_NAME] = {0};
    char operator[FLOW_NAME] = {0};
    char value[FLOW_NAME] = {0};
    int fields;

    fields = sscanf(line, "%63s %63s %63s", name, operator, value);
    if (fields < 1) return 0;
    memset(constraint, 0, sizeof(*constraint));
    strncpy(constraint->name, name, sizeof(constraint->name) - 1);
    if (fields >= 2)
        strncpy(constraint->operator, operator,
                sizeof(constraint->operator) - 1);
    if (fields >= 3)
        strncpy(constraint->value, value, sizeof(constraint->value) - 1);
    strncpy(constraint->expression, line,
            sizeof(constraint->expression) - 1);
    return 1;
}

static void parse_flow_nodes(FlowSpec *spec, const char *expression) {
    char buffer[FLOW_LINE];
    char *node;

    strncpy(buffer, expression, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    node = strtok(buffer, "->");
    while (node != NULL && spec->flow_node_count < FLOW_NODE_MAX) {
        node = trim(node);
        if (*node != '\0') {
            strncpy(spec->flow_nodes[spec->flow_node_count].name, node,
                    sizeof(spec->flow_nodes[spec->flow_node_count].name) - 1);
            spec->flow_nodes[spec->flow_node_count].name[
                sizeof(spec->flow_nodes[spec->flow_node_count].name) - 1] = '\0';
            ++spec->flow_node_count;
        }
        node = strtok(NULL, "->");
    }
}

int parse_spec(FILE *input, FlowSpec *spec) {
    char raw[FLOW_LINE];
    Section section = SECTION_NONE;
    int line_number = 0;
    int parse_error = 0;
    int brace_depth = 0;

    memset(spec, 0, sizeof(*spec));
    while (fgets(raw, sizeof(raw), input) != NULL) {
        char *line;
        ++line_number;
        line = trim(raw);
        if (*line == '\0' || *line == '#') continue;
        if (strchr(line, '{') != NULL) ++brace_depth;

        if (starts_with(line, "input ")) {
            section = SECTION_INPUT;
            copy_word(spec->input_name, sizeof(spec->input_name), line + 6);
            continue;
        }
        if (starts_with(line, "output ")) {
            section = SECTION_OUTPUT;
            copy_word(spec->output_name, sizeof(spec->output_name), line + 7);
            continue;
        }
        if (starts_with(line, "state ")) {
            section = SECTION_STATE;
            copy_word(spec->state_name, sizeof(spec->state_name), line + 6);
            continue;
        }
        if (starts_with(line, "flow ")) {
            section = SECTION_FLOW;
            copy_word(spec->flow_name, sizeof(spec->flow_name), line + 5);
            continue;
        }
        if (strcmp(line, "require {") == 0) {
            section = SECTION_REQUIRE;
            continue;
        }
        if (strcmp(line, "prefer {") == 0) {
            section = SECTION_PREFER;
            continue;
        }
        if (strcmp(line, "ensure {") == 0) {
            section = SECTION_ENSURE;
            continue;
        }
        if (starts_with(line, "resource ")) {
            section = SECTION_RESOURCE;
            copy_word(spec->resource_name, sizeof(spec->resource_name), line + 9);
            continue;
        }
        if (starts_with(line, "capability ")) {
            section = SECTION_CAPABILITY;
            copy_word(spec->capability_name, sizeof(spec->capability_name), line + 11);
            continue;
        }
        if (starts_with(line, "domain ")) {
            section = SECTION_DOMAIN;
            copy_word(spec->domain_name, sizeof(spec->domain_name), line + 7);
            continue;
        }
        if (starts_with(line, "project ")) {
            section = SECTION_NONE;
            copy_word(spec->project_name, sizeof(spec->project_name), line + 8);
            if (spec->flow_name[0] == '\0') {
                strncpy(spec->flow_name, spec->project_name, sizeof(spec->flow_name) - 1);
            }
            continue;
        }
        if (starts_with(line, "import ")) {
            section = SECTION_NONE;
            char mod[FLOW_NAME] = {0};
            copy_word(mod, sizeof(mod), line + 7);
            if (mod[0] == '\0') {
                fprintf(stderr, "flowc: invalid import declaration at line %d\n", line_number);
                parse_error = 1;
            } else {
                strncpy(spec->plugin_name, mod, sizeof(spec->plugin_name) - 1);
                if (spec->imported_module_count < 8) {
                    strncpy(spec->imported_modules[spec->imported_module_count++], mod,
                            sizeof(spec->imported_modules[0]) - 1);
                }
            }
            continue;
        }
        if (starts_with(line, "contract ")) {
            section = SECTION_NONE;
            copy_word(spec->contract_name, sizeof(spec->contract_name), line + 9);
            if (spec->contract_name[0] == '\0') {
                fprintf(stderr, "flowc: invalid contract declaration at line %d\n", line_number);
                parse_error = 1;
            }
            continue;
        }
        if (starts_with(line, "fallback ")) {
            section = SECTION_NONE;
            copy_word(spec->fallback_policy, sizeof(spec->fallback_policy), line + 9);
            if (spec->fallback_policy[0] == '\0') {
                fprintf(stderr, "flowc: invalid fallback declaration at line %d\n", line_number);
                parse_error = 1;
            }
            continue;
        }
        if (strcmp(line, "}") == 0) {
            if (section == SECTION_NONE || brace_depth == 0) {
                fprintf(stderr, "flowc: unexpected closing brace at line %d\n", line_number);
                parse_error = 1;
            } else {
                --brace_depth;
                section = SECTION_NONE;
            }
            continue;
        }

        switch (section) {
            case SECTION_INPUT: {
                int value;
                int id;
                int score;
                char trailing;
                if (sscanf(line, "max_count %d %c", &value, &trailing) == 1 &&
                    value > 0) {
                    spec->max_count = value;
                    break;
                }
                if (sscanf(line, "sample %d %d %c", &id, &score,
                           &trailing) == 2 &&
                    spec->sample_count < FLOW_SAMPLE_MAX) {
                    spec->samples[spec->sample_count].id = id;
                    spec->samples[spec->sample_count].score = score;
                    ++spec->sample_count;
                    break;
                }
                fprintf(stderr, "flowc: invalid input line %d: %s\n", line_number, line);
                parse_error = 1;
                break;
            }
            case SECTION_OUTPUT:
                if (sscanf(line, "type %63s", spec->output_type) != 1 &&
                    sscanf(line, "as %63s", spec->output_type) != 1)
                {
                    fprintf(stderr, "flowc: invalid output line %d: %s\n", line_number, line);
                    parse_error = 1;
                }
                break;
            case SECTION_STATE:
                if (strcmp(line, "shared") == 0) spec->shared = 1;
                else if (strcmp(line, "read_heavy") == 0) spec->read_heavy = 1;
                else if (strcmp(line, "bounded") == 0) spec->bounded = 1;
                else {
                    fprintf(stderr, "flowc: invalid state line %d: %s\n", line_number, line);
                    parse_error = 1;
                }
                break;
            case SECTION_FLOW: {
                const char *top = strstr(line, "top(");
                if (spec->flow_expression[0] == '\0') {
                    strncpy(spec->flow_expression, line, sizeof(spec->flow_expression) - 1);
                    spec->flow_expression[sizeof(spec->flow_expression) - 1] = '\0';
                }
                parse_flow_nodes(spec, line);
                if (top != NULL) {
                    int top_value;
                    int consumed = 0;
                    if (sscanf(top + 4, "%d%n", &top_value, &consumed) != 1 ||
                        top_value <= 0 || top[4 + consumed] != ')') {
                        fprintf(stderr, "flowc: invalid top expression at line %d: %s\n",
                                line_number, line);
                        parse_error = 1;
                    } else {
                        spec->top_n = top_value;
                    }
                }
                break;
            }
            case SECTION_REQUIRE: {
                int value;
                char trailing;
                if (spec->constraint_count >= FLOW_CONSTRAINT_MAX ||
                    !parse_constraint(&spec->constraints[spec->constraint_count],
                                      line)) {
                    fprintf(stderr, "flowc: invalid requirement line %d: %s\n", line_number, line);
                    parse_error = 1;
                    break;
                }
                ++spec->constraint_count;
                if (strcmp(line, "deterministic") == 0) {
                    spec->deterministic = 1;
                } else if (strcmp(spec->constraints[spec->constraint_count - 1].name,
                                  "memory") == 0) {
                    if (sscanf(line, "memory < %dmb %c", &value,
                               &trailing) != 1 || value <= 0) {
                        fprintf(stderr, "flowc: invalid requirement line %d: %s\n", line_number, line);
                        parse_error = 1;
                    } else {
                        spec->memory_mb = value;
                    }
                }
                break;
            }
            case SECTION_ENSURE:
                if (strcmp(line, "deterministic") == 0) {
                    ++spec->ensure_count;
                    spec->deterministic = 1;
                } else {
                    fprintf(stderr, "flowc: invalid ensure line %d: %s\n", line_number, line);
                    parse_error = 1;
                }
                break;
            case SECTION_PREFER:
                if (strcmp(line, "latency") == 0) spec->prefer_latency = 1;
                else {
                    fprintf(stderr, "flowc: invalid preference line %d: %s\n", line_number, line);
                    parse_error = 1;
                }
                break;
            case SECTION_RESOURCE:
                fprintf(stderr, "flowc: invalid resource line %d: %s\n", line_number, line);
                parse_error = 1;
                break;
            case SECTION_CAPABILITY:
                fprintf(stderr, "flowc: invalid capability line %d: %s\n", line_number, line);
                parse_error = 1;
                break;
            case SECTION_DOMAIN:
                fprintf(stderr, "flowc: invalid domain line %d: %s\n", line_number, line);
                parse_error = 1;
                break;
            default:
                fprintf(stderr, "flowc: invalid top-level line %d: %s\n", line_number, line);
                parse_error = 1;
                break;
        }
    }

    if (spec->flow_name[0] == '\0') {
        fprintf(stderr, "flowc: spec must define a flow\n");
        return 0;
    }
    if (spec->input_name[0] == '\0') {
        fprintf(stderr, "flowc: current backend family requires an input declaration\n");
        return 0;
    }
    if (brace_depth != 0) {
        fprintf(stderr, "flowc: unclosed block at end of spec\n");
        parse_error = 1;
    }
    if (parse_error) return 0;
    if (spec->input_name[0] != '\0' && spec->max_count <= 0) {
        fprintf(stderr, "flowc: input must declare a positive max_count\n");
        return 0;
    }
    if (spec->top_n <= 0) spec->top_n = 3;
    return 1;
}
