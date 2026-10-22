#include "flow.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef enum { SECTION_NONE, SECTION_INPUT, SECTION_OUTPUT, SECTION_STATE, SECTION_FLOW, SECTION_REQUIRE, SECTION_ENSURE, SECTION_PREFER, SECTION_RESOURCE,
    SECTION_CAPABILITY, SECTION_DOMAIN, SECTION_JET, SECTION_THERMAL
} Section; static char *trim(char *text) { while (isspace((unsigned char)*text)) ++text; char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) --end; *end = '\0'; return text; }
static int starts_with(const char *text, const char *prefix) { return strncmp(text, prefix, strlen(prefix)) == 0; }
static void copy_word(char *dest, size_t size, const char *line) { if (sscanf(line, "%63s", dest) != 1) dest[0] = '\0'; dest[size - 1] = '\0';
    char *brace = strchr(dest, '{'); if (brace) *brace = '\0'; } static double parse_val_with_unit(const char *str) { char *end = NULL; double v = strtod(str, &end);
    if (end) { while (isspace((unsigned char)*end)) ++end; if (!strcasecmp(end, "khz")) v *= 1e3; else if (!strcasecmp(end, "mhz")) v *= 1e6;
        else if (!strcasecmp(end, "ms")) v *= 1e-3; else if (!strcasecmp(end, "us")) v *= 1e-6; else if (!strcasecmp(end, "ns")) v *= 1e-9;
        else if (!strcasecmp(end, "%")) v *= 1e-2; } return v; }
static size_t parse_field_bytes(const char *tok) { if (!tok || !*tok) return 0;
    if (!strcasecmp(tok, "u64") || !strcasecmp(tok, "i64") || !strcasecmp(tok, "f64")) return 8;
    if (!strcasecmp(tok, "u32") || !strcasecmp(tok, "i32") || !strcasecmp(tok, "f32")) return 4;
    if (!strcasecmp(tok, "u16") || !strcasecmp(tok, "i16")) return 2;
    if (!strcasecmp(tok, "u8") || !strcasecmp(tok, "i8")) return 1;
    long v = strtol(tok, NULL, 10); return v > 0 ? (size_t)v : 0; }
static int parse_constraint(FlowConstraint *c, const char *line) { char name[FLOW_NAME] = {0}, op[FLOW_NAME] = {0}, val[FLOW_NAME] = {0};
    int fields = sscanf(line, "%63s %63s %63s", name, op, val); if (fields < 1) return 0; memset(c, 0, sizeof(*c)); strncpy(c->name, name, sizeof(c->name) - 1);
    if (fields >= 2) strncpy(c->operator, op, sizeof(c->operator) - 1); if (fields >= 3) strncpy(c->value, val, sizeof(c->value) - 1);
    strncpy(c->expression, line, sizeof(c->expression) - 1); return 1; } static void parse_flow_nodes(FlowSpec *spec, const char *expression) { char buffer[FLOW_LINE] = {0};
    strncpy(buffer, expression, sizeof(buffer) - 1); for (char *node = strtok(buffer, "->"); node && spec->flow_node_count < FLOW_NODE_MAX; node = strtok(NULL, "->")) {
        node = trim(node); if (!*node) continue;
        if (starts_with(node, "extern(")) {
            char *rp = strchr(node + 7, ')');
            if (rp && (size_t)(rp - (node + 7)) < FLOW_NAME) {
                const char *s = node + 7;
                size_t len = (size_t)(rp - s);
                if (len >= 2 && s[0] == '"' && s[len - 1] == '"') { s++; len -= 2; }
                memcpy(spec->extern_kernel, s, len);
                spec->extern_kernel[len] = '\0';
            }
        }
        strncpy(spec->flow_nodes[spec->flow_node_count++].name, node, FLOW_NAME - 1); } } static int is_lean_kw(const char *line) {
    return !strcmp(line, "lean_certified") || !strcmp(line, "proof_carrying") || !strcmp(line, "no_sorry"); }
static size_t parse_lean_type_bytes(const char *t) {
    if (!t || !*t) return 0;
    if (!strncmp(t, "UInt64", 6) || !strncmp(t, "Int64", 5) || !strncmp(t, "Float", 5) || !strncmp(t, "Nat", 3) || !strncmp(t, "USize", 5)) return 8;
    if (!strncmp(t, "UInt32", 6) || !strncmp(t, "Int32", 5) || !strncmp(t, "Float32", 7)) return 4;
    if (!strncmp(t, "UInt16", 6) || !strncmp(t, "Int16", 5)) return 2;
    if (!strncmp(t, "UInt8", 5) || !strncmp(t, "Int8", 4) || !strncmp(t, "Bool", 4)) return 1;
    return isupper((unsigned char)t[0]) ? 8 : 0;
}
static int is_builtin_lean_struct(const char *s) {
    static const char *const B[] = {"StateSchemaField", "ExtentDescriptor", "PctVol", "PointVol", "AnnualizedTime", "SignedPosition", "CausalAxisWitness", "TraceabilityMatrix", NULL};
    for (int i = 0; B[i]; ++i) if (!strcmp(s, B[i])) return 1;
    return 0;
}
static int is_internal_lean_def(const char *s) {
    static const char *const D[] = {"projectBoundedIndex", "bachelierStdDevScaled", "extentOffsetAfterSteps", "PolicyIdsUnique", "HasSatisfiedEffect", NULL};
    for (int i = 0; D[i]; ++i) if (!strcmp(s, D[i])) return 1;
    return 0;
}
static int parse_lean_spec(FILE *input, FlowSpec *spec) {
    char raw[FLOW_LINE], sym[FLOW_NAME] = {0}, cand_fn[FLOW_NAME] = {0}, cand_body[FLOW_LINE] = {0};
    unsigned long long v = 0; int saw_lean = 0, in_struct = 0, in_block_comment = 0, explicit_stride = 0, explicit_export = 0, awaiting_body = 0, in_u32_body = 0; size_t struct_bytes = 0;
    memset(spec, 0, sizeof(*spec)); strncpy(spec->input_name, "lean_stream", FLOW_NAME - 1); strncpy(spec->flow_name, "lean_kernel_flow", FLOW_NAME - 1);
    spec->max_count = 1024; spec->input_stride_bytes = 8; spec->input_align_bytes = 8; spec->state_schema_version = 1; spec->top_n = 3;
    spec->deterministic = 1; spec->lean_certified = 1; spec->bounded = 1; spec->memory_mb = 64;
    while (fgets(raw, sizeof(raw), input)) {
        if (in_block_comment) {
            char *ce = strstr(raw, "-/");
            if (!ce) continue;
            memmove(raw, ce + 2, strlen(ce + 2) + 1);
            in_block_comment = 0;
        }
        while (strstr(raw, "/-")) {
            char *cs = strstr(raw, "/-"), *ce = strstr(cs + 2, "-/");
            if (ce) memmove(cs, ce + 2, strlen(ce + 2) + 1);
            else { *cs = '\0'; in_block_comment = 1; break; }
        }
        char *c = strstr(raw, "--"); if (c) *c = '\0';
        char *line = trim(raw); if (!*line) continue;
        while (starts_with(line, "public ") || starts_with(line, "private ")) line = trim(line + (starts_with(line, "public ") ? 7 : 8));
        for (const char *p = line; *p; ++p) {
            if (!strncmp(p, "sorry", 5) && (p == line || !isalnum((unsigned char)p[-1])) && !isalnum((unsigned char)p[5])) return 0;
            if (!strncmp(p, "axiom ", 6) && (p == line || isspace((unsigned char)p[-1]))) return 0;
        }
        if (awaiting_body) {
            strncpy(cand_body, line, FLOW_LINE - 1);
            if (spec->extern_kernel[0]) strncpy(spec->extern_kernel_body, cand_body, FLOW_LINE - 1);
            awaiting_body = 0; continue;
        }
        if (in_u32_body && cand_body[0] && (starts_with(line, "else ") || starts_with(line, "then "))) {
            size_t cur = strlen(cand_body);
            if (cur + 1 + strlen(line) < FLOW_LINE) {
                cand_body[cur] = ' '; strncpy(cand_body + cur + 1, line, FLOW_LINE - cur - 2);
                if (spec->extern_kernel[0]) strncpy(spec->extern_kernel_body, cand_body, FLOW_LINE - 1);
            }
            continue;
        }
        in_u32_body = 0;
        if (in_struct) {
            char fn[FLOW_NAME] = {0}, ft[FLOW_NAME] = {0};
            if (starts_with(line, "deriving ") || starts_with(line, "def ") || starts_with(line, "theorem ") ||
                starts_with(line, "structure ") || starts_with(line, "inductive ") || starts_with(line, "namespace ") ||
                starts_with(line, "end ") || starts_with(line, "@[")) {
                in_struct = 0;
            } else if (sscanf(line, "%63s : %63s", fn, ft) == 2 && spec->state_field_count < FLOW_FIELD_MAX) {
                size_t b = parse_lean_type_bytes(ft);
                if (b > 0) {
                    FlowFieldSpec *fs = &spec->state_fields[spec->state_field_count++];
                    strncpy(fs->name, fn, FLOW_NAME - 1); fs->size_bytes = b; fs->flags = 2u; struct_bytes += b;
                }
                continue;
            }
        }
        if (sscanf(line, "structure %63s where", sym) == 1) {
            saw_lean = 1; in_struct = !is_builtin_lean_struct(sym);
        } else if (sscanf(line, "namespace %63s", sym) == 1) {
            if (!strcmp(sym, "Spec") || !strcmp(sym, "Exec") || !strcmp(sym, "Cedar.Thm")) continue;
            saw_lean = 1;
            if (!strncmp(sym, "Embodied.", 9) || !strncmp(sym, "Flow.Embodied.", 14)) {
                strncpy(spec->domain_name, "embodied", FLOW_NAME - 1);
                strncpy(spec->plugin_name, "flow.embodied", FLOW_NAME - 1);
                if (spec->imported_module_count == 0) strncpy(spec->imported_modules[spec->imported_module_count++], "flow.embodied", FLOW_NAME - 1);
            }
            const char *leaf = strrchr(sym, '.');
            strncpy(spec->flow_name, leaf ? leaf + 1 : sym, FLOW_NAME - 1);
        } else if (sscanf(line, "def inputMaxCount : Nat := %llu", &v) == 1 && v > 0) { spec->max_count = (int)v; saw_lean = 1; }
        else if (sscanf(line, "def inputStrideBytes : Nat := %llu", &v) == 1 && v > 0) { spec->input_stride_bytes = (size_t)v; explicit_stride = 1; }
        else if (sscanf(line, "def inputAlignBytes : Nat := %llu", &v) == 1 && v > 0) spec->input_align_bytes = (size_t)v;
        else if (sscanf(line, "def stateSchemaVersion : Nat := %llu", &v) == 1 && v > 0) spec->state_schema_version = (uint32_t)v;
        else if (sscanf(line, "def slaLatencyNs : Nat := %llu", &v) == 1) spec->sla_latency_ns = (uint64_t)v;
        else if (sscanf(line, "def shardCount : Nat := %llu", &v) == 1 && v > 1) { spec->shared = 1; spec->read_heavy = 1; }
        else if (sscanf(line, "def memoryQuotaBytes : Nat := %llu", &v) == 1 && v >= (1ULL << 20)) spec->memory_mb = (int)(v >> 20);
        else if (sscanf(line, "@[export %63[^]]]", sym) == 1) { strncpy(spec->extern_kernel, sym, FLOW_NAME - 1); explicit_export = 1; saw_lean = 1; }
        else if (strstr(line, "{ name := \"") && spec->state_field_count < FLOW_FIELD_MAX) {
            char fn[FLOW_NAME] = {0}; unsigned long long b = 0; unsigned int fl = 2u;
            const char *q = strstr(line, "{ name := \"");
            if (q && sscanf(q, "{ name := \"%63[^\"]\", sizeBytes := %llu, flags := %u", fn, &b, &fl) >= 2 && b > 0) {
                FlowFieldSpec *fs = &spec->state_fields[spec->state_field_count++];
                strncpy(fs->name, fn, FLOW_NAME - 1); fs->size_bytes = (size_t)b; fs->flags = fl;
            }
        } else if (starts_with(line, "def ") && strchr(line, '(') && !strstr(line, ": Prop") && !strstr(line, ": Type")) {
            char fn[FLOW_NAME] = {0};
            if (sscanf(line + 4, "%63[^ ({]", fn) == 1 && !is_internal_lean_def(fn)) {
                saw_lean = 1; strncpy(cand_fn, fn, FLOW_NAME - 1);
                if (strstr(line, ": UInt32")) {
                    const char *ce = strstr(line, ":=");
                    if (ce) {
                        char *rhs = trim((char *)(ce + 2));
                        in_u32_body = 1;
                        if (*rhs) {
                            strncpy(cand_body, rhs, FLOW_LINE - 1);
                            if (spec->extern_kernel[0]) strncpy(spec->extern_kernel_body, cand_body, FLOW_LINE - 1);
                        } else awaiting_body = 1;
                    }
                } else {
                    strncpy(cand_body, "if stride > 0 && epoch > 0 && baseOff + byteLen >= baseOff then 0 else 1", FLOW_LINE - 1);
                }
            }
        } else if (starts_with(line, "theorem ")) {
            saw_lean = 1;
            if (sscanf(line + 8, "%63[^ (: ]", sym) == 1 && !strcmp(sym, "zmp_torque_safe")) {
                strncpy(spec->contract_name, "zmp_torque_safe", FLOW_NAME - 1);
                if (!spec->domain_name[0]) {
                    strncpy(spec->domain_name, "embodied", FLOW_NAME - 1);
                    strncpy(spec->plugin_name, "flow.embodied", FLOW_NAME - 1);
                    if (spec->imported_module_count == 0) strncpy(spec->imported_modules[spec->imported_module_count++], "flow.embodied", FLOW_NAME - 1);
                }
            }
            if (cand_fn[0] && strstr(line, cand_fn)) {
                if (!explicit_export) strncpy(spec->extern_kernel, cand_fn, FLOW_NAME - 1);
                strncpy(spec->extern_kernel_body, cand_body, FLOW_LINE - 1);
            }
        } else if (cand_fn[0] && strstr(line, cand_fn)) {
            if (!explicit_export) strncpy(spec->extern_kernel, cand_fn, FLOW_NAME - 1);
            strncpy(spec->extern_kernel_body, cand_body, FLOW_LINE - 1);
        }
    }
    if (!explicit_stride && struct_bytes > 0) spec->input_stride_bytes = (struct_bytes + 7u) & ~7u;
    if (cand_fn[0] && !spec->extern_kernel[0]) {
        strncpy(spec->extern_kernel, cand_fn, FLOW_NAME - 1);
        strncpy(spec->extern_kernel_body, cand_body, FLOW_LINE - 1);
    }
    if (!strcmp(spec->domain_name, "embodied")) {
        snprintf(spec->flow_expression, sizeof(spec->flow_expression), "lean_stream -> flow_embodied_run -> top(3)");
    } else {
        snprintf(spec->flow_expression, sizeof(spec->flow_expression), "lean_stream -> %s%s%s -> top(3)",
                 spec->extern_kernel[0] ? "extern(" : "sort", spec->extern_kernel[0] ? spec->extern_kernel : "", spec->extern_kernel[0] ? ")" : "");
    }
    parse_flow_nodes(spec, spec->flow_expression); return saw_lean;
}
int parse_spec(FILE *input, FlowSpec *spec) {
    char raw[FLOW_LINE]; Section section = SECTION_NONE; int line_number = 0, parse_error = 0, brace_depth = 0;
    int is_literate = 0, is_lean_doc = 0, in_code_block = 0, flow_block_completed = 0; long start_pos = ftell(input); if (start_pos >= 0) { while (fgets(raw, sizeof(raw), input)) {
            char *p = trim(raw); if (!strncmp(p, "```flow", 7) || !strncmp(p, "~~~flow", 7)) { is_literate = 1; break; }
            if (starts_with(p, "namespace ") || starts_with(p, "theorem ") || starts_with(p, "@[export ")) is_lean_doc = 1; } fseek(input, start_pos, SEEK_SET);
        if (is_lean_doc && !is_literate) return parse_lean_spec(input, spec); }
    memset(spec, 0, sizeof(*spec)); while (fgets(raw, sizeof(raw), input)) { ++line_number; char *line = trim(raw); if (is_literate) { if (!in_code_block) {
                if (!strncmp(line, "```flow", 7) || !strncmp(line, "~~~flow", 7)) { if (flow_block_completed) break; in_code_block = 1; } continue;
            } else if (!strncmp(line, "```", 3) || !strncmp(line, "~~~", 3)) { in_code_block = 0;
                if (spec->flow_name[0] && spec->input_name[0]) { flow_block_completed = 1; break; } continue; } }
        if (!*line || *line == '#' || !strncmp(line, "//", 2)) continue; if (strchr(line, '{')) ++brace_depth;
        static const struct { const char *prefix; Section sec; size_t offset; } SEC_HDRS[] = { {"input ",      SECTION_INPUT,      offsetof(FlowSpec, input_name)},
            {"output ",     SECTION_OUTPUT,     offsetof(FlowSpec, output_name)}, {"state ",      SECTION_STATE,      offsetof(FlowSpec, state_name)},
            {"flow ",       SECTION_FLOW,       offsetof(FlowSpec, flow_name)}, {"resource ",   SECTION_RESOURCE,   offsetof(FlowSpec, resource_name)},
            {"capability ", SECTION_CAPABILITY, offsetof(FlowSpec, capability_name)}, {"domain ",     SECTION_DOMAIN,     offsetof(FlowSpec, domain_name)} };
        int matched_hdr = 0;
        for (size_t i = 0; i < sizeof(SEC_HDRS) / sizeof(SEC_HDRS[0]); ++i) {
            if (starts_with(line, SEC_HDRS[i].prefix)) {
                section = SEC_HDRS[i].sec; copy_word((char *)spec + SEC_HDRS[i].offset, FLOW_NAME, line + strlen(SEC_HDRS[i].prefix));
                matched_hdr = 1; break;
            }
        }
        if (matched_hdr) continue;
        if (!strcmp(line, "require {")) { section = SECTION_REQUIRE; continue; }
        if (!strcmp(line, "prefer {"))  { section = SECTION_PREFER; continue; } if (!strcmp(line, "ensure {"))  { section = SECTION_ENSURE; continue; }
        if (starts_with(line, "jet ") || !strcmp(line, "jet {")) { section = SECTION_JET; spec->jet.enabled = 1;
            if (starts_with(line, "jet ")) copy_word(spec->jet.name, FLOW_NAME, line + 4); continue; } if (starts_with(line, "thermal ") || !strcmp(line, "thermal {")) {
            section = SECTION_THERMAL; spec->thermal.enabled = 1; if (starts_with(line, "thermal ")) copy_word(spec->thermal.name, FLOW_NAME, line + 8); continue; }
        if (starts_with(line, "project ")) { section = SECTION_NONE; copy_word(spec->project_name, FLOW_NAME, line + 8);
            if (!spec->flow_name[0]) strncpy(spec->flow_name, spec->project_name, FLOW_NAME - 1); continue; } if (starts_with(line, "import ")) {
            section = SECTION_NONE; char mod[FLOW_NAME] = {0}; copy_word(mod, sizeof(mod), line + 7);
            if (!mod[0]) { fprintf(stderr, "flowc: invalid import declaration at line %d\n", line_number); parse_error = 1; } else {
                strncpy(spec->plugin_name, mod, FLOW_NAME - 1);
                if (spec->imported_module_count < 8) strncpy(spec->imported_modules[spec->imported_module_count++], mod, FLOW_NAME - 1); } continue; }
        if (starts_with(line, "contract ") || starts_with(line, "fallback ") || starts_with(line, "extern ")) {
            if (brace_depth == 0) section = SECTION_NONE;
            int is_c = starts_with(line, "contract "), is_e = starts_with(line, "extern ");
            char *dst = is_c ? spec->contract_name : (is_e ? spec->extern_kernel : spec->fallback_policy);
            copy_word(dst, FLOW_NAME, line + (is_e ? 7 : 9));
            if (is_e && dst[0] == '"') {
                size_t dlen = strlen(dst);
                if (dlen >= 2 && dst[dlen - 1] == '"') { memmove(dst, dst + 1, dlen - 2); dst[dlen - 2] = '\0'; }
            }
            if (!dst[0]) { fprintf(stderr, "flowc: invalid %s declaration at line %d\n", is_c ? "contract" : (is_e ? "extern" : "fallback"), line_number); parse_error = 1; } continue; }
        if (!strcmp(line, "}")) {
            if (section == SECTION_NONE || brace_depth == 0) { fprintf(stderr, "flowc: unexpected closing brace at line %d\n", line_number); parse_error = 1; }
            else { --brace_depth; section = SECTION_NONE; } continue; } switch (section) { case SECTION_INPUT: { int value, id, score; char trailing;
                if (sscanf(line, "max_count %d %c", &value, &trailing) == 1 && value > 0) { spec->max_count = value; break; }
                if (sscanf(line, "stride %d %c", &value, &trailing) == 1 && value > 0) { spec->input_stride_bytes = (size_t)value; break; }
                if (sscanf(line, "align %d %c", &value, &trailing) == 1 && value > 0) { spec->input_align_bytes = (size_t)value; break; }
                if (sscanf(line, "sample %d %d %c", &id, &score, &trailing) == 2 && spec->sample_count < FLOW_SAMPLE_MAX) {
                    spec->samples[spec->sample_count++] = (FlowSample){id, score}; break; }
                fprintf(stderr, "flowc: invalid input line %d: %s\n", line_number, line); parse_error = 1; break; } case SECTION_OUTPUT:
                if (sscanf(line, "type %63s", spec->output_type) != 1 && sscanf(line, "as %63s", spec->output_type) != 1) {
                    fprintf(stderr, "flowc: invalid output line %d: %s\n", line_number, line); parse_error = 1; } break; case SECTION_STATE: {
                int ver_v = 0; char fn[FLOW_NAME] = {0}, fsz[FLOW_NAME] = {0}, fflg[FLOW_NAME] = {0}, trailing;
                if (!strcmp(line, "shared")) spec->shared = 1; else if (!strcmp(line, "read_heavy")) spec->read_heavy = 1;
                else if (!strcmp(line, "bounded")) spec->bounded = 1;
                else if (sscanf(line, "version %d %c", &ver_v, &trailing) == 1 && ver_v > 0) spec->state_schema_version = (uint32_t)ver_v;
                else if (starts_with(line, "field ") && spec->state_field_count < FLOW_FIELD_MAX && sscanf(line + 6, "%63s %63s %63s", fn, fsz, fflg) >= 2) {
                    size_t b = parse_field_bytes(fsz);
                    if (!b) { fprintf(stderr, "flowc: invalid state field size at line %d: %s\n", line_number, line); parse_error = 1; }
                    else { uint32_t fl = (strstr(line, "defaultable") ? 1u : 0u) | (strstr(line, "persistent") ? 2u : 0u) | (strstr(line, "ordered") ? 4u : 0u);
                        FlowFieldSpec *fs = &spec->state_fields[spec->state_field_count++];
                        strncpy(fs->name, fn, FLOW_NAME - 1); fs->size_bytes = b; fs->flags = fl ? fl : 2u; }
                } else { fprintf(stderr, "flowc: invalid state line %d: %s\n", line_number, line); parse_error = 1; }
                break; } case SECTION_FLOW: { const char *top = strstr(line, "top("); if (!spec->flow_expression[0]) strncpy(spec->flow_expression, line, FLOW_LINE - 1);
                parse_flow_nodes(spec, line); if (top) { int top_value, consumed = 0;
                    if (sscanf(top + 4, "%d%n", &top_value, &consumed) != 1 || top_value <= 0 || top[4 + consumed] != ')') {
                        fprintf(stderr, "flowc: invalid top expression at line %d: %s\n", line_number, line); parse_error = 1; } else spec->top_n = top_value; } break; }
            case SECTION_REQUIRE: { int value; char trailing;
                if (spec->constraint_count >= FLOW_CONSTRAINT_MAX || !parse_constraint(&spec->constraints[spec->constraint_count], line)) {
                    fprintf(stderr, "flowc: invalid requirement line %d: %s\n", line_number, line); parse_error = 1; break; }
                FlowConstraint *cur_c = &spec->constraints[spec->constraint_count];
                spec->declared_constraint_count = ++spec->constraint_count; if (!strcmp(line, "deterministic")) spec->deterministic = 1;
                else if (is_lean_kw(line)) { spec->lean_certified = 1; spec->deterministic = 1; }
                else if (!strcmp(cur_c->name, "memory")) { if (sscanf(line, "memory < %dmb %c", &value, &trailing) != 1 || value <= 0) {
                        fprintf(stderr, "flowc: invalid requirement line %d: %s\n", line_number, line); parse_error = 1; } else spec->memory_mb = value; }
                else if (!strcmp(cur_c->name, "sla") || !strcmp(cur_c->name, "latency")) {
                    double sec = parse_val_with_unit(cur_c->value[0] ? cur_c->value : cur_c->operator);
                    if (sec <= 0.0) { fprintf(stderr, "flowc: invalid SLA requirement line %d: %s\n", line_number, line); parse_error = 1; }
                    else spec->sla_latency_ns = (uint64_t)(sec * 1e9 + 0.5); } break; }
            case SECTION_ENSURE: if (!strcmp(line, "deterministic")) { ++spec->ensure_count; spec->deterministic = 1; }
                else if (is_lean_kw(line)) { ++spec->ensure_count; spec->lean_certified = 1; spec->deterministic = 1; }
                else { fprintf(stderr, "flowc: invalid ensure line %d: %s\n", line_number, line); parse_error = 1; } break; case SECTION_PREFER:
                if (!strcmp(line, "latency") || !strcmp(line, "throughput")) spec->prefer_latency = 1; else if (!strcmp(line, "memory")) spec->prefer_latency = 0;
                else { fprintf(stderr, "flowc: invalid preference line %d: %s\n", line_number, line); parse_error = 1; } break; case SECTION_JET: {
                char kw[FLOW_NAME] = {0}, val[FLOW_NAME] = {0}; if (sscanf(line, "%63s %63s", kw, val) >= 1) { double v = parse_val_with_unit(val);
                    if (!strcmp(kw, "frequency") || !strcmp(kw, "target_frequency")) spec->jet.frequency_hz = v; else if (!strcmp(kw, "symplectic")) spec->jet.symplectic = 1;
                    else if (!strcmp(kw, "drift") || !strcmp(kw, "max_drift")) spec->jet.max_drift = v;
                    else if (!strcmp(kw, "zmp") || !strcmp(kw, "zmp_margin")) spec->jet.zmp_margin = v; else if (!strcmp(kw, "dt")) spec->jet.dt = v;
                    else if (!strcmp(kw, "dim")) spec->jet.dim = (size_t)v;
                    else if (spec->constraint_count < FLOW_CONSTRAINT_MAX && parse_constraint(&spec->constraints[spec->constraint_count], line))
                        spec->declared_constraint_count = ++spec->constraint_count; } break; } case SECTION_THERMAL: { char kw[FLOW_NAME] = {0}, val[FLOW_NAME] = {0};
                if (sscanf(line, "%63s %63s", kw, val) >= 1) { double v = parse_val_with_unit(val);
                    if (!strcmp(kw, "max_temp") || !strcmp(kw, "temp_max")) spec->thermal.max_temp_c = v;
                    else if (!strcmp(kw, "target_temp") || !strcmp(kw, "temp_target")) spec->thermal.target_temp_c = v;
                    else if (!strcmp(kw, "power_limit") || !strcmp(kw, "power")) spec->thermal.power_limit_w = v;
                    else if (!strcmp(kw, "r_thermal")) spec->thermal.r_thermal = v; else if (!strcmp(kw, "c_thermal")) spec->thermal.c_thermal = v;
                    else if (spec->constraint_count < FLOW_CONSTRAINT_MAX && parse_constraint(&spec->constraints[spec->constraint_count], line))
                        spec->declared_constraint_count = ++spec->constraint_count; } break; } default: {
                const char *sec_err = (section == SECTION_RESOURCE) ? "resource" : (section == SECTION_CAPABILITY) ? "capability" :
                                      (section == SECTION_DOMAIN) ? "domain" : "top-level";
                fprintf(stderr, "flowc: invalid %s line %d: %s\n", sec_err, line_number, line); parse_error = 1; break; } } }
    if (!spec->flow_name[0]) { fprintf(stderr, "flowc: spec must define a flow\n"); return 0; }
    if (!spec->input_name[0]) { fprintf(stderr, "flowc: current backend family requires an input declaration\n"); return 0; }
    if (brace_depth != 0) { fprintf(stderr, "flowc: unclosed block at end of spec\n"); parse_error = 1; } if (parse_error) return 0;
    if (spec->input_name[0] && spec->max_count <= 0) { fprintf(stderr, "flowc: input must declare a positive max_count\n"); return 0; } if (spec->top_n <= 0) spec->top_n = 3;
    if (!spec->input_stride_bytes) spec->input_stride_bytes = 8;
    if (!spec->input_align_bytes) spec->input_align_bytes = 8;
    if (!spec->state_schema_version) spec->state_schema_version = 1;
    if (spec->jet.enabled) { if (spec->jet.frequency_hz <= 0.0) spec->jet.frequency_hz = 10000.0; if (spec->jet.max_drift <= 0.0) spec->jet.max_drift = 0.005;
        if (spec->jet.dt <= 0.0) spec->jet.dt = 1.0 / spec->jet.frequency_hz; if (spec->jet.zmp_margin <= 0.0) spec->jet.zmp_margin = 0.02;
        if (!spec->jet.dim) spec->jet.dim = 4; } if (spec->thermal.enabled) { if (spec->thermal.max_temp_c <= 0.0) spec->thermal.max_temp_c = 95.0;
        if (spec->thermal.target_temp_c <= 0.0) spec->thermal.target_temp_c = 85.0; if (spec->thermal.power_limit_w <= 0.0) spec->thermal.power_limit_w = 25.0;
        if (spec->thermal.r_thermal <= 0.0) spec->thermal.r_thermal = 6.0; if (spec->thermal.c_thermal <= 0.0) spec->thermal.c_thermal = 0.05; } return 1; }
static int has_flow_node(const SemanticIR *ir, const char *name) { for (size_t i = 0; i < ir->flow_node_count; ++i) if (!strcmp(ir->flow_nodes[i].name, name)) return 1;
    return 0; } static void add_fact(SemanticIR *ir, FlowFactKind kind, const char *detail) { if (ir->fact_count >= FLOW_NODE_MAX) return;
    ir->facts[ir->fact_count].kind = kind; strncpy(ir->facts[ir->fact_count++].detail, detail, FLOW_NAME - 1); if (kind == FLOW_FACT_HOLE) ++ir->hole_count;
    else if (kind == FLOW_FACT_CONSTRAINT) ++ir->constraint_count; } void lower_to_ir(const FlowSpec *spec, SemanticIR *ir) { memset(ir, 0, sizeof(*ir));
    memcpy(ir, spec, offsetof(FlowSpec, constraint_count)); ir->declared_constraint_count = spec->constraint_count; ir->fact_ordered = has_flow_node(ir, "sort");
    ir->fact_unordered = !ir->fact_ordered; ir->fact_range_proven = ir->input_max_count > 0;
    ir->fact_size_preserved = has_flow_node(ir, "transform") && has_flow_node(ir, "collect");
    ir->fact_mutability_read_only = !ir->state_shared && !has_flow_node(ir, "transition") && strcmp(ir->flow_name, "state_machine") != 0;
    ir->flow_parallelizable = has_flow_node(ir, "transform") || has_flow_node(ir, "parallel"); if (ir->state_shared) add_fact(ir, FLOW_FACT_SHARED_STATE, "shared");
    if (ir->state_bounded) add_fact(ir, FLOW_FACT_BOUNDED, "bounded"); if (ir->flow_parallelizable) add_fact(ir, FLOW_FACT_PARALLELIZABLE, "parallelizable");
    if (ir->fact_ordered) add_fact(ir, FLOW_FACT_ORDERED, "ordered"); if (ir->flow_node_count > 0) add_fact(ir, FLOW_FACT_COLLECTION, "pipeline");
    for (size_t i = 0; i < ir->flow_node_count; ++i) { if (!strcmp(ir->flow_nodes[i].name, "transform")) add_fact(ir, FLOW_FACT_TRANSFORM, "transform");
        if (strchr(ir->flow_nodes[i].name, '?')) add_fact(ir, FLOW_FACT_HOLE, ir->flow_nodes[i].name); }
    if (ir->capability_name[0]) add_fact(ir, FLOW_FACT_CAPABILITY, ir->capability_name);
    if (ir->memory_limit_mb > 0 || ir->fact_deterministic) add_fact(ir, FLOW_FACT_CONSTRAINT, "require");
    if (ir->fact_range_proven) add_fact(ir, FLOW_FACT_RANGE, "input_max_count"); if (ir->fact_size_preserved) add_fact(ir, FLOW_FACT_SIZE, "length_preserved");
    if (ir->fact_mutability_read_only) add_fact(ir, FLOW_FACT_MUTABILITY, "read_only"); if (ir->fact_deterministic) add_fact(ir, FLOW_FACT_DETERMINISM, "deterministic");
    if (ir->fact_lean_certified) add_fact(ir, FLOW_FACT_LEAN_CERTIFIED, "lean_certified"); ir->hole_count += strstr(ir->flow_expression, "?") != NULL && ir->hole_count == 0;
    if (ir->jet.enabled) add_fact(ir, FLOW_FACT_CAPABILITY, "jet_phase_space"); if (ir->thermal.enabled) add_fact(ir, FLOW_FACT_CAPABILITY, "thermal_envelope"); }
