#include "flow.h"
#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef enum { SECTION_NONE, SECTION_INPUT, SECTION_OUTPUT, SECTION_STATE, SECTION_FLOW, SECTION_REQUIRE, SECTION_ENSURE, SECTION_PREFER, SECTION_RESOURCE,
    SECTION_CAPABILITY, SECTION_DOMAIN, SECTION_JET, SECTION_THERMAL, SECTION_RELOAD, SECTION_PATCHBAY
} Section; static char *trim(char *text) { while (isspace((unsigned char)*text)) ++text; char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) --end; *end = '\0'; return text; }
static int starts_with(const char *text, const char *prefix) { return strncmp(text, prefix, strlen(prefix)) == 0; }
static void copy_word(char *dest, size_t size, const char *line) { if (sscanf(line, "%63s", dest) != 1) dest[0] = '\0'; dest[size - 1] = '\0';
    char *brace = strchr(dest, '{'); if (brace) *brace = '\0'; } static double parse_val_with_unit(const char *str) {
    if (!str) return -1.0;
    while (*str && (*str == '<' || *str == '>' || *str == '=' || *str == ':' || isspace((unsigned char)*str))) ++str;
    if (!*str) return -1.0;
    char *end = NULL; double v = strtod(str, &end);
    if (!end || end == str) return -1.0;
    while (isspace((unsigned char)*end)) ++end;
    if (!*end || !strcasecmp(end, "hz") || !strcasecmp(end, "s") || !strcasecmp(end, "sec") || !strcasecmp(end, "c") || !strcasecmp(end, "w") || !strcasecmp(end, "m")) return v;
    if (!strcasecmp(end, "khz")) return v * 1e3;
    if (!strcasecmp(end, "mhz")) return v * 1e6;
    if (!strcasecmp(end, "ghz")) return v * 1e9;
    if (!strcasecmp(end, "ms")) return v * 1e-3;
    if (!strcasecmp(end, "us")) return v * 1e-6;
    if (!strcasecmp(end, "ns")) return v * 1e-9;
    if (!strcasecmp(end, "%") || !strcasecmp(end, "cm")) return v * 1e-2;
    if (!strcasecmp(end, "mm")) return v * 1e-3;
    return -1.0; }
static size_t parse_bytes_with_unit(const char *str) {
    if (!str || !*str) return 0;
    while (*str && (*str == '<' || *str == '>' || *str == '=' || *str == ':' || isspace((unsigned char)*str))) ++str;
    if (!*str) return 0;
    char *end = NULL; double v = strtod(str, &end); if (!end || end == str || v <= 0.0) return 0;
    while (isspace((unsigned char)*end)) ++end;
    if (!strcasecmp(end, "gb") || !strcasecmp(end, "gib")) v *= (double)(1ULL << 30);
    else if (!strcasecmp(end, "mb") || !strcasecmp(end, "mib")) v *= (double)(1ULL << 20);
    else if (!strcasecmp(end, "kb") || !strcasecmp(end, "kib")) v *= (double)(1ULL << 10);
    else if (*end != '\0' && strcasecmp(end, "b") != 0 && strcasecmp(end, "bytes") != 0) return 0;
    return (size_t)(v + 0.5); }
static size_t parse_field_bytes(const char *tok) { if (!tok || !*tok) return 0;
    if (!strcasecmp(tok, "u64") || !strcasecmp(tok, "i64") || !strcasecmp(tok, "f64")) return 8;
    if (!strcasecmp(tok, "u32") || !strcasecmp(tok, "i32") || !strcasecmp(tok, "f32")) return 4;
    if (!strcasecmp(tok, "u16") || !strcasecmp(tok, "i16")) return 2;
    if (!strcasecmp(tok, "u8") || !strcasecmp(tok, "i8")) return 1;
    long v = strtol(tok, NULL, 10); return v > 0 ? (size_t)v : 0; }
static int parse_constraint(FlowConstraint *c, const char *line) { char name[FLOW_NAME] = {0}, op[FLOW_NAME] = {0}, val[FLOW_NAME] = {0};
    int fields = sscanf(line, "%63s %63s %63s", name, op, val); if (fields < 1) return 0;
    size_t nl = strlen(name); if (nl > 0 && name[nl - 1] == ':') name[nl - 1] = '\0';
    memset(c, 0, sizeof(*c)); strncpy(c->name, name, sizeof(c->name) - 1);
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
static const char *normalize_lean_type_token(const char *t) {
    if (!t) return "";
    if (!strcmp(t, "\xe2\x84\x9d") || !strcmp(t, "Real")) return "Float";
    return t;
}
static int is_lean_erased_proof_field(const char *t_expr) {
    if (!t_expr) return 1;
    while (*t_expr && isspace((unsigned char)*t_expr)) ++t_expr;
    if (!*t_expr) return 1;
    if (!strcmp(t_expr, "Prop") || !strncmp(t_expr, "Prop ", 5) || !strcmp(t_expr, "True") || !strcmp(t_expr, "False")) return 1;
    if (islower((unsigned char)t_expr[0])) return 1;
    if (strchr(t_expr, '<') || strchr(t_expr, '>') || strchr(t_expr, '=') ||
        strstr(t_expr, "\xe2\x89\xa4") || strstr(t_expr, "\xe2\x89\xa5") || strstr(t_expr, "\xe2\x89\xa0") ||
        strstr(t_expr, "\xe2\x88\xa7") || strstr(t_expr, "\xe2\x88\xa8") || strstr(t_expr, "\xc2\xac") ||
        strstr(t_expr, "\xe2\x86\x92") || strstr(t_expr, "\xe2\x86\x94") || strstr(t_expr, "\xe2\x88\x80") ||
        strstr(t_expr, "\xe2\x88\x83") || strstr(t_expr, "->")) return 1;
    return 0;
}
static size_t parse_lean_type_bytes(const char *t) {
    if (!t || !*t) return 0;
    if (!strncmp(t, "UInt64", 6) || !strncmp(t, "Int64", 5) || !strncmp(t, "Float", 5) ||
        !strncmp(t, "\xe2\x84\x9d", 3) || !strncmp(t, "Real", 4) || !strncmp(t, "Nat", 3) || !strncmp(t, "USize", 5)) return 8;
    if (!strncmp(t, "UInt32", 6) || !strncmp(t, "Int32", 5) || !strncmp(t, "Float32", 7)) return 4;
    if (!strncmp(t, "UInt16", 6) || !strncmp(t, "Int16", 5)) return 2;
    if (!strncmp(t, "UInt8", 5) || !strncmp(t, "Int8", 4) || !strncmp(t, "Bool", 4)) return 1;
    if (!strcmp(t, "Int") || (!strncmp(t, "Int", 3) && !isalnum((unsigned char)t[3]))) return 8;
    return isupper((unsigned char)t[0]) ? 8 : 0;
}
static int is_builtin_lean_struct(const char *s) {
    static const char *const B[] = {"StateSchemaField", "ExtentDescriptor", "PctVol", "PointVol", "AnnualizedTime", "SignedPosition", "CausalAxisWitness", "TraceabilityMatrix", NULL};
    for (int i = 0; B[i]; ++i) if (!strcmp(s, B[i])) return 1;
    return 0;
}
static int is_internal_lean_def(const char *s) {
    static const char *const D[] = {"projectBoundedIndex", "bachelierStdDevScaled", "extentOffsetAfterSteps", "bootstrapCausalWitnesses", "stateSchemaFields", NULL};
    for (int i = 0; D[i]; ++i) if (!strcmp(s, D[i])) return 1;
    return 0;
}
static int is_scalar_lean_type(const char *t) {
    static const char *const S[] = {"Nat", "UInt64", "USize", "Int", "Int64", "UInt32", "Int32", "UInt16", "Int16", "UInt8", "Int8", "Bool", "Float", "Float32", "\xe2\x84\x9d", "Real", NULL};
    if (!t || !*t) return 0;
    for (int i = 0; S[i]; ++i) if (!strcmp(t, S[i])) return 1;
    return 0;
}
static const FlowLeanStruct *find_lean_struct(const FlowSpec *spec, const char *name) {
    if (!spec || !name || !name[0]) return NULL;
    for (size_t i = 0; i < spec->lean_struct_count; ++i)
        if (!strcmp(spec->lean_structs[i].name, name)) return &spec->lean_structs[i];
    return NULL;
}
static const FlowLeanEnum *find_lean_enum(const FlowSpec *spec, const char *name) {
    if (!spec || !name || !name[0]) return NULL;
    for (size_t i = 0; i < spec->lean_enum_count; ++i)
        if (!strcmp(spec->lean_enums[i].name, name)) return &spec->lean_enums[i];
    return NULL;
}
static void lean_type_to_c_info(const FlowSpec *spec, const char *lt, char *c_out, size_t c_sz, char *lean_out, size_t l_sz, size_t *bytes_out) {
    const char *ct = "uint64_t", *norm_lt = "UInt64"; size_t b = 8;
    if (!strcmp(lt, "Float") || !strcmp(lt, "\xe2\x84\x9d") || !strcmp(lt, "Real")) { ct = "double"; norm_lt = "Float"; b = 8; }
    else if (!strcmp(lt, "Float32")) { ct = "float"; norm_lt = "Float32"; b = 4; }
    else if (!strcmp(lt, "UInt64") || !strcmp(lt, "Nat") || !strcmp(lt, "USize")) { ct = "uint64_t"; norm_lt = lt; b = 8; }
    else if (!strcmp(lt, "Int") || !strcmp(lt, "Int64")) { ct = "int64_t"; norm_lt = lt; b = 8; }
    else if (!strcmp(lt, "UInt32")) { ct = "uint32_t"; norm_lt = "UInt32"; b = 4; }
    else if (!strcmp(lt, "Int32")) { ct = "int32_t"; norm_lt = "Int32"; b = 4; }
    else if (!strcmp(lt, "UInt16")) { ct = "uint16_t"; norm_lt = "UInt16"; b = 2; }
    else if (!strcmp(lt, "Int16")) { ct = "int16_t"; norm_lt = "Int16"; b = 2; }
    else if (!strcmp(lt, "UInt8")) { ct = "uint8_t"; norm_lt = "UInt8"; b = 1; }
    else if (!strcmp(lt, "Int8")) { ct = "int8_t"; norm_lt = "Int8"; b = 1; }
    else if (!strcmp(lt, "Bool")) { ct = "bool"; norm_lt = "Bool"; b = 1; }
    else if (find_lean_enum(spec, lt)) { ct = "uint32_t"; norm_lt = "UInt32"; b = 4; }
    else { b = parse_lean_type_bytes(lt); if (!b) b = 8; }
    strncpy(c_out, ct, c_sz - 1); c_out[c_sz - 1] = '\0';
    strncpy(lean_out, norm_lt, l_sz - 1); lean_out[l_sz - 1] = '\0';
    if (bytes_out) *bytes_out = b;
}
static void recompute_lean_struct_layout(FlowLeanStruct *st) {
    if (!st) return;
    size_t off = 0, max_al = 1;
    for (size_t i = 0; i < st->field_count; ++i) {
        size_t sz = st->fields[i].size_bytes ? st->fields[i].size_bytes : 8;
        size_t al = (sz >= 8) ? 8 : (sz >= 4 ? 4 : (sz >= 2 ? 2 : 1));
        if (al > max_al) max_al = al;
        off = (off + al - 1) & ~(al - 1);
        off += sz;
    }
    st->align_bytes = max_al;
    st->byte_size = (off + max_al - 1) & ~(max_al - 1);
}
static void add_lean_struct_field(FlowSpec *spec, FlowLeanStruct *st, const char *fn, const char *ft, int populate_spec_fields, size_t *struct_bytes_acc) {
    const FlowLeanStruct *inner = (st && strcmp(st->name, ft) != 0) ? find_lean_struct(spec, ft) : NULL;
    if (inner && inner->field_count > 0) {
        for (size_t k = 0; k < inner->field_count; ++k) {
            char flat_name[FLOW_NAME] = {0};
            snprintf(flat_name, sizeof(flat_name), "%s_%s", fn, inner->fields[k].name);
            if (st && st->field_count < FLOW_FIELD_MAX) {
                FlowLeanStructField *sf = &st->fields[st->field_count++];
                strncpy(sf->name, flat_name, FLOW_NAME - 1);
                strncpy(sf->c_type, inner->fields[k].c_type, sizeof(sf->c_type) - 1);
                strncpy(sf->lean_type, inner->fields[k].lean_type, sizeof(sf->lean_type) - 1);
                sf->size_bytes = inner->fields[k].size_bytes;
            }
            if (populate_spec_fields && spec->state_field_count < FLOW_FIELD_MAX) {
                int dup_sf = 0;
                for (size_t di = 0; di < spec->state_field_count; ++di)
                    if (!strcmp(spec->state_fields[di].name, flat_name)) { dup_sf = 1; break; }
                if (!dup_sf) {
                    FlowFieldSpec *fs = &spec->state_fields[spec->state_field_count++];
                    strncpy(fs->name, flat_name, FLOW_NAME - 1);
                    fs->size_bytes = inner->fields[k].size_bytes;
                    fs->flags = 2u;
                    if (struct_bytes_acc) *struct_bytes_acc += inner->fields[k].size_bytes;
                    if (spec->jet_coord_count < FLOW_FIELD_MAX)
                        strncpy(spec->jet_coord_names[spec->jet_coord_count++], flat_name, FLOW_NAME - 1);
                }
            }
        }
        recompute_lean_struct_layout(st);
        return;
    }
    char ct[16] = {0}, lt_norm[16] = {0}; size_t b = 0;
    lean_type_to_c_info(spec, ft, ct, sizeof(ct), lt_norm, sizeof(lt_norm), &b);
    if (st && st->field_count < FLOW_FIELD_MAX) {
        FlowLeanStructField *sf = &st->fields[st->field_count++];
        strncpy(sf->name, fn, FLOW_NAME - 1);
        strncpy(sf->c_type, ct, sizeof(sf->c_type) - 1);
        strncpy(sf->lean_type, lt_norm, sizeof(sf->lean_type) - 1);
        sf->size_bytes = b;
        recompute_lean_struct_layout(st);
    }
    if (populate_spec_fields && b > 0 && spec->state_field_count < FLOW_FIELD_MAX) {
        int dup_sf = 0;
        for (size_t di = 0; di < spec->state_field_count; ++di)
            if (!strcmp(spec->state_fields[di].name, fn)) { dup_sf = 1; break; }
        if (!dup_sf) {
            FlowFieldSpec *fs = &spec->state_fields[spec->state_field_count++];
            strncpy(fs->name, fn, FLOW_NAME - 1); fs->size_bytes = b; fs->flags = 2u;
            if (struct_bytes_acc) *struct_bytes_acc += b;
            if (spec->jet_coord_count < FLOW_FIELD_MAX)
                strncpy(spec->jet_coord_names[spec->jet_coord_count++], fn, FLOW_NAME - 1);
        }
    }
}
static int has_word_match(const char *hay, const char *word) {
    size_t wlen = strlen(word); if (!wlen) return 0;
    for (const char *p = strstr(hay, word); p; p = strstr(p + 1, word)) {
        int left_ok = (p == hay) || (!isalnum((unsigned char)p[-1]) && p[-1] != '_' && p[-1] != '.');
        int right_ok = !isalnum((unsigned char)p[wlen]) && p[wlen] != '_' && p[wlen] != '\'';
        if (left_ok && right_ok) return 1;
    }
    return 0;
}
static void normalize_lean_body(const char *in, char *out, size_t max_len) {
    static const struct { const char *lean_fn; size_t l_len; const char *c_fn; size_t c_len; } BUILTIN_MAP[] = {
        {"Nat.min", 7, "flow_nat_min", 12},
        {"Nat.max", 7, "flow_nat_max", 12},
        {"Float.abs", 9, "flow_float_abs", 14},
        {"Float.floor", 11, "flow_float_floor", 16},
        {"Float.ceil", 10, "flow_float_ceil", 15},
        {"Float.round", 11, "flow_float_round", 16},
        {"Float.sqrt", 10, "flow_float_sqrt", 15},
        {"Float.exp", 9, "flow_float_exp", 14},
        {"Float.log", 9, "flow_float_log", 14},
        {"Float.sin", 9, "flow_float_sin", 14},
        {"Float.cos", 9, "flow_float_cos", 14},
        {"Float.atan2", 11, "flow_float_atan2", 16},
        {"Float.pow", 9, "flow_float_pow", 14},
        {"Float.max", 9, "flow_float_max", 14},
        {"Float.min", 9, "flow_float_min", 14},
        {"Float.ofNat", 11, "flow_float_of_nat", 17},
        {"Float.ofInt", 11, "flow_float_of_int", 17},
        {"Real.abs", 8, "flow_float_abs", 14},
        {"Real.floor", 10, "flow_float_floor", 16},
        {"Real.ceil", 9, "flow_float_ceil", 15},
        {"Real.round", 10, "flow_float_round", 16},
        {"Real.sqrt", 9, "flow_float_sqrt", 15},
        {"Real.exp", 8, "flow_float_exp", 14},
        {"Real.log", 8, "flow_float_log", 14},
        {"Real.sin", 8, "flow_float_sin", 14},
        {"Real.cos", 8, "flow_float_cos", 14},
        {"Real.atan2", 10, "flow_float_atan2", 16},
        {"Real.pow", 8, "flow_float_pow", 14},
        {"Real.max", 8, "flow_float_max", 14},
        {"Real.min", 8, "flow_float_min", 14},
        {"f_abs", 5, "flow_float_abs", 14},
        {"f_max", 5, "flow_float_max", 14},
        {"f_min", 5, "flow_float_min", 14}
    };
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 20 < max_len; ) {
        if (!strncmp(in + i, "\xe2\x89\xa4", 3)) { out[j++] = '<'; out[j++] = '='; i += 3; continue; }
        if (!strncmp(in + i, "\xe2\x89\xa5", 3)) { out[j++] = '>'; out[j++] = '='; i += 3; continue; }
        if (!strncmp(in + i, "\xe2\x89\xa0", 3)) { out[j++] = '!'; out[j++] = '='; i += 3; continue; }
        if (!strncmp(in + i, "\xe2\x88\xa7", 3)) { out[j++] = '&'; out[j++] = '&'; i += 3; continue; }
        if (!strncmp(in + i, "\xe2\x88\xa8", 3)) { out[j++] = '|'; out[j++] = '|'; i += 3; continue; }
        if (!strncmp(in + i, "\xc2\xac", 2)) { out[j++] = '!'; i += 2; continue; }
        int mapped = 0;
        for (size_t bi = 0; bi < sizeof(BUILTIN_MAP) / sizeof(BUILTIN_MAP[0]); ++bi) {
            size_t ll = BUILTIN_MAP[bi].l_len;
            if (!strncmp(in + i, BUILTIN_MAP[bi].lean_fn, ll) && !isalnum((unsigned char)in[i + ll]) && in[i + ll] != '_') {
                memcpy(out + j, BUILTIN_MAP[bi].c_fn, BUILTIN_MAP[bi].c_len);
                j += BUILTIN_MAP[bi].c_len;
                i += ll;
                mapped = 1;
                break;
            }
        }
        if (mapped) continue;
        if (in[i] == '.' && (i == 0 || (!isalnum((unsigned char)in[i - 1]) && in[i - 1] != '_' && in[i - 1] != ')')) &&
            (isalpha((unsigned char)in[i + 1]) || in[i + 1] == '_')) {
            i++;
        } else {
            out[j++] = in[i++];
        }
    }
    out[j] = '\0';
    for (size_t i = 1; out[i]; ++i) {
        if (out[i] == '.' && (isalnum((unsigned char)out[i - 1]) || out[i - 1] == '_' || out[i - 1] == ')') &&
            (isalpha((unsigned char)out[i + 1]) || out[i + 1] == '_')) {
            size_t k = i + 1;
            while (isalnum((unsigned char)out[k]) || out[k] == '_') ++k;
            while (out[k] == '.' && (isalpha((unsigned char)out[k + 1]) || out[k + 1] == '_')) {
                out[k] = '_';
                ++k;
                while (isalnum((unsigned char)out[k]) || out[k] == '_') ++k;
            }
            i = k - 1;
        }
    }
}
static uint64_t estimate_lean_ast_wcet_ns(const FlowLeanFunction *fn) {
    if (!fn) return FLOW_WCET_BASE_CALL_NS;
    uint64_t cost = FLOW_WCET_BASE_CALL_NS + FLOW_WCET_STRUCT_FIELD_NS * (uint64_t)fn->field_assign_count;
    const char *b = fn->is_struct_transform ? (fn->field_assign_count > 0 ? fn->field_assign_exprs[0] : "") : fn->body;
    for (size_t i = 0; b[i]; ++i) {
        if (b[i] == '+' || b[i] == '*' || b[i] == '&' || b[i] == '|' || b[i] == '<' || b[i] == '>' || b[i] == '=') cost += FLOW_WCET_ALU_OP_NS;
        else if (b[i] == '-' && b[i + 1] != '>') cost += FLOW_WCET_ALU_OP_NS;
        else if (b[i] == '/' || b[i] == '%' || b[i] == '^') cost += FLOW_WCET_COMPLEX_OP_NS;
        else if (!strncmp(b + i, "if ", 3) || !strncmp(b + i, "match ", 6)) cost += FLOW_WCET_BRANCH_OP_NS;
        else if (!strncmp(b + i, "flow_quantize", 13) || !strncmp(b + i, "flow_dequantize", 15)) { cost += FLOW_WCET_COMPLEX_OP_NS; i += 12; }
    }
    return cost;
}
static int is_valid_scalar_lean_body_str(const FlowSpec *spec, const FlowLeanFunction *fn, const char *b) {
    static const char *const KW[] = {
        "if", "then", "else", "let", "in", "match", "with", "true", "false", "decide",
        "flow_nat_min", "flow_nat_max", "flow_quantize", "flow_dequantize",
        "flow_float_abs", "flow_float_floor", "flow_float_ceil", "flow_float_round",
        "flow_float_sqrt", "flow_float_exp", "flow_float_log", "flow_float_sin",
        "flow_float_cos", "flow_float_atan2", "flow_float_pow", "flow_float_max",
        "flow_float_min", "flow_float_of_nat", "flow_float_of_int", "_", NULL
    };
    char let_vars[32][FLOW_NAME]; size_t let_cnt = 0;
    if (!b || !b[0]) return 0;
    int has_struct_param = 0;
    for (size_t i = 0; i < fn->param_count; ++i)
        if (find_lean_struct(spec, fn->param_types[i]) != NULL) has_struct_param = 1;
    for (const char *p = b; *p; ++p) {
        if (*p == '{' || *p == '}' || *p == '[' || *p == ']') return 0;
        if (*p == '.' && isalpha((unsigned char)p[1]) && !has_struct_param && spec->lean_enum_count == 0) return 0;
        if (!strncmp(p, "let ", 4) && (p == b || isspace((unsigned char)p[-1]) || p[-1] == '(')) {
            const char *vs = p + 4;
            while (*vs && isspace((unsigned char)*vs)) ++vs;
            if (*vs == '(') {
                ++vs;
                while (*vs && *vs != ')') {
                    while (*vs && (isspace((unsigned char)*vs) || *vs == ',')) ++vs;
                    char lv[FLOW_NAME] = {0}; size_t lk = 0;
                    while (*vs && (isalnum((unsigned char)*vs) || *vs == '_')) {
                        if (lk + 1 < sizeof(lv)) lv[lk++] = *vs;
                        ++vs;
                    }
                    while (*vs && *vs != ',' && *vs != ')') ++vs;
                    if (lv[0] && let_cnt < 32) strncpy(let_vars[let_cnt++], lv, FLOW_NAME - 1);
                }
            } else {
                char lv[FLOW_NAME] = {0};
                if (sscanf(vs, "%63[^ :=\t\r\n]", lv) == 1 && lv[0] && let_cnt < 32) strncpy(let_vars[let_cnt++], lv, FLOW_NAME - 1);
            }
        }
    }
    for (const char *p = b; *p; ) {
        if (isdigit((unsigned char)*p)) {
            while (isalnum((unsigned char)*p) || *p == '_' || *p == '.') {
                if ((*p == 'e' || *p == 'E') && (p[1] == '+' || p[1] == '-')) ++p;
                ++p;
            }
            continue;
        }
        if (isalpha((unsigned char)*p) || *p == '_') {
            char id[FLOW_NAME] = {0}; size_t k = 0;
            while (isalnum((unsigned char)*p) || *p == '_' || *p == '\'') { if (*p == '\'') return 0; if (k + 1 < sizeof(id)) id[k++] = *p; ++p; }
            if (*p == '.' && (isalpha((unsigned char)p[1]) || p[1] == '_')) {
                ++p;
                while (isalnum((unsigned char)*p) || *p == '_') ++p;
            }
            int ok = is_scalar_lean_type(id);
            for (int i = 0; !ok && KW[i]; ++i) if (!strcmp(id, KW[i])) ok = 1;
            for (size_t i = 0; !ok && i < fn->param_count; ++i) if (!strcmp(id, fn->param_names[i])) ok = 1;
            for (size_t i = 0; !ok && i < let_cnt; ++i) if (!strcmp(id, let_vars[i])) ok = 1;
            for (size_t i = 0; !ok && i < spec->lean_function_count; ++i) if (!strcmp(id, spec->lean_functions[i].name)) ok = 1;
            for (size_t i = 0; !ok && i < spec->lean_enum_count; ++i) {
                if (!strcmp(id, spec->lean_enums[i].name)) ok = 1;
                for (size_t v = 0; !ok && v < spec->lean_enums[i].variant_count; ++v)
                    if (!strcmp(id, spec->lean_enums[i].variants[v])) ok = 1;
            }
            if (!ok) return 0;
        } else ++p;
    }
    return 1;
}
static int is_valid_scalar_lean_body(const FlowSpec *spec, const FlowLeanFunction *fn) {
    return is_valid_scalar_lean_body_str(spec, fn, fn->body);
}
static void subst_whole_ident(const char *src, const char *var, const char *val, char *out, size_t out_max) {
    size_t vlen = strlen(var), rlen = strlen(val), j = 0;
    for (size_t i = 0; src[i] && j + 4 < out_max; ) {
        int l_ok = (i == 0) || (!isalnum((unsigned char)src[i - 1]) && src[i - 1] != '_' && src[i - 1] != '.');
        int r_ok = !isalnum((unsigned char)src[i + vlen]) && src[i + vlen] != '_' && src[i + vlen] != '\'';
        if (l_ok && r_ok && !strncmp(src + i, var, vlen) && j + rlen + 3 < out_max) {
            out[j++] = '('; memcpy(out + j, val, rlen); j += rlen; out[j++] = ')';
            i += vlen; continue;
        }
        out[j++] = src[i++];
    }
    out[j] = '\0';
}
static void parse_struct_record_fields(FlowLeanFunction *fn, const char *prefix, const char *rec_in) {
    if (!fn || !rec_in) return;
    char unfolded[FLOW_LEAN_BODY_MAX];
    strncpy(unfolded, rec_in, sizeof(unfolded) - 1); unfolded[sizeof(unfolded) - 1] = '\0';
    for (int iter = 0; iter < 8; ++iter) {
        const char *cur = unfolded; while (*cur && isspace((unsigned char)*cur)) ++cur;
        if (strncmp(cur, "let ", 4) != 0) break;
        const char *ce = NULL, *sep = NULL; int pd = 0, bd = 0, skip = 0;
        for (const char *p = cur + 4; *p; ++p) {
            if (*p == '(') pd++; else if (*p == ')' && pd > 0) pd--;
            else if (*p == '{') { if (pd == 0 && ce) { sep = p; skip = 0; break; } bd++; }
            else if (*p == '}' && bd > 0) bd--;
            else if (pd == 0 && bd == 0 && !ce && p[0] == ':' && p[1] == '=') { ce = p; ++p; }
            else if (pd == 0 && bd == 0 && ce && (*p == ';' || *p == '\n')) { sep = p; skip = 1; break; }
            else if (pd == 0 && bd == 0 && ce && !strncmp(p, " in ", 4)) { sep = p; skip = 4; break; }
        }
        if (!ce || !sep) break;
        const char *vs = cur + 4; while (vs < ce && isspace((unsigned char)*vs)) ++vs;
        size_t vl = 0; while (vs + vl < ce && (isalnum((unsigned char)vs[vl]) || vs[vl] == '_')) ++vl;
        if (vl == 0 || vl >= FLOW_NAME) break;
        char vname[FLOW_NAME] = {0}, vrhs[FLOW_LINE] = {0}, next_buf[FLOW_LEAN_BODY_MAX] = {0};
        memcpy(vname, vs, vl);
        const char *rs = ce + 2, *re = sep;
        while (rs < re && isspace((unsigned char)*rs)) ++rs;
        while (re > rs && isspace((unsigned char)re[-1])) --re;
        size_t rl = (size_t)(re - rs); if (rl >= sizeof(vrhs)) rl = sizeof(vrhs) - 1;
        memcpy(vrhs, rs, rl); vrhs[rl] = '\0';
        subst_whole_ident(sep + skip, vname, vrhs, next_buf, sizeof(next_buf));
        strncpy(unfolded, next_buf, sizeof(unfolded) - 1); unfolded[sizeof(unfolded) - 1] = '\0';
    }
    const char *lb = strchr(unfolded, '{'), *rb = strrchr(unfolded, '}');
    if (!lb || !rb || rb <= lb) return;
    const char *s = lb + 1, *end = rb;
    const char *f_id_starts[FLOW_FIELD_MAX], *f_id_ends[FLOW_FIELD_MAX], *f_val_starts[FLOW_FIELD_MAX];
    size_t f_cnt = 0; int pd = 0, bd = 0;
    for (const char *p = s; p + 1 < end; ++p) {
        if (*p == '(') pd++; else if (*p == ')' && pd > 0) pd--;
        else if (*p == '{') bd++; else if (*p == '}' && bd > 0) bd--;
        else if (pd == 0 && bd == 0 && p[0] == ':' && p[1] == '=') {
            const char *ie = p;
            while (ie > s && isspace((unsigned char)ie[-1])) --ie;
            const char *is = ie;
            while (is > s && (isalnum((unsigned char)is[-1]) || is[-1] == '_')) --is;
            if (is < ie) {
                const char *prev = is;
                while (prev > s && isspace((unsigned char)prev[-1])) --prev;
                if (prev - s >= 3 && !strncmp(prev - 3, "let", 3) && (prev - 3 == s || isspace((unsigned char)prev[-4]) || prev[-4] == '('))
                    continue;
                if (f_cnt < FLOW_FIELD_MAX) {
                    f_id_starts[f_cnt] = is; f_id_ends[f_cnt] = ie; f_val_starts[f_cnt] = p + 2;
                    f_cnt++;
                }
            }
        }
    }
    for (size_t i = 0; i < f_cnt; ++i) {
        char fname[FLOW_NAME] = {0}, full_name[FLOW_NAME] = {0};
        size_t fl = (size_t)(f_id_ends[i] - f_id_starts[i]);
        if (fl == 0 || fl >= sizeof(fname)) continue;
        memcpy(fname, f_id_starts[i], fl); fname[fl] = '\0';
        if (prefix && prefix[0]) snprintf(full_name, sizeof(full_name), "%s_%s", prefix, fname);
        else strncpy(full_name, fname, sizeof(full_name) - 1);
        const char *vs = f_val_starts[i], *ve = (i + 1 < f_cnt) ? f_id_starts[i + 1] : end;
        while (vs < ve && isspace((unsigned char)*vs)) ++vs;
        while (ve > vs && (isspace((unsigned char)ve[-1]) || ve[-1] == ',' || ve[-1] == ';')) --ve;
        char fexpr[FLOW_LINE] = {0};
        size_t el = (size_t)(ve - vs);
        if (el >= sizeof(fexpr)) el = sizeof(fexpr) - 1;
        memcpy(fexpr, vs, el); fexpr[el] = '\0';
        if (fexpr[0] == '{' && el >= 2 && fexpr[el - 1] == '}') {
            parse_struct_record_fields(fn, full_name, fexpr);
        } else if (fn->field_assign_count < FLOW_FIELD_MAX) {
            strncpy(fn->field_assign_names[fn->field_assign_count], full_name, sizeof(fn->field_assign_names[0]) - 1);
            strncpy(fn->field_assign_exprs[fn->field_assign_count], fexpr, sizeof(fn->field_assign_exprs[0]) - 1);
            fn->field_assign_count++;
        }
    }
}
static int is_kw_tok(const char *p, const char *start, const char *end, const char *kw, size_t kwlen) {
    if ((size_t)(end - p) < kwlen || strncmp(p, kw, kwlen) != 0) return 0;
    int l_ok = (p == start) || isspace((unsigned char)p[-1]) || p[-1] == '(' || p[-1] == ')';
    int r_ok = (p + kwlen == end) || isspace((unsigned char)p[kwlen]) || p[kwlen] == '(' || p[kwlen] == ')';
    return l_ok && r_ok;
}
static int project_lean_tuple_component(const FlowSpec *spec, const char *s, size_t len, size_t comp_idx, size_t arity, char *out, size_t out_max) {
    (void)spec;
    if (!s || !out || out_max == 0) return 0;
    while (len > 0 && isspace((unsigned char)*s)) { ++s; --len; }
    while (len > 0 && isspace((unsigned char)s[len - 1])) --len;
    if (len == 0) { out[0] = '\0'; return 0; }
    const char *end = s + len;
    if (is_kw_tok(s, s, end, "let", 3)) {
        const char *ce = NULL; int pd = 0;
        for (const char *p = s + 3; p + 1 < end; ++p) {
            if (*p == '(') pd++; else if (*p == ')' && pd > 0) pd--;
            else if (pd == 0 && p[0] == ':' && p[1] == '=') { ce = p; break; }
        }
        if (ce) {
            const char *rhs = ce + 2;
            while (rhs < end && isspace((unsigned char)*rhs)) ++rhs;
            const char *rhs_end = NULL, *rest = NULL;
            pd = 0; int ld = 0;
            for (const char *p = rhs; p < end; ++p) {
                if (*p == '(') pd++; else if (*p == ')' && pd > 0) pd--;
                else if (pd == 0 && is_kw_tok(p, rhs, end, "let", 3)) ld++;
                else if (pd == 0 && is_kw_tok(p, rhs, end, "in", 2)) {
                    if (ld == 0) { rhs_end = p; rest = p + 2; break; }
                    ld--;
                }
            }
            if (!rhs_end) {
                pd = 0; int id = 0;
                for (const char *p = rhs; p < end; ++p) {
                    if (*p == '(') pd++; else if (*p == ')' && pd > 0) pd--;
                    else if (pd == 0 && is_kw_tok(p, rhs, end, "if", 2)) id++;
                    else if (pd == 0 && is_kw_tok(p, rhs, end, "else", 4) && id > 0) id--;
                    else if (pd == 0 && id == 0 && (*p == ';' || *p == '\n')) { rhs_end = p; rest = p + 1; break; }
                }
            }
            if (rhs_end && rest) {
                while (rhs_end > rhs && isspace((unsigned char)rhs_end[-1])) --rhs_end;
                while (rest < end && isspace((unsigned char)*rest)) ++rest;
                char rest_proj[FLOW_LEAN_BODY_MAX] = {0};
                if (!project_lean_tuple_component(spec, rest, (size_t)(end - rest), comp_idx, arity, rest_proj, sizeof(rest_proj)))
                    return 0;
                const char *ps = s + 3, *pe = ce;
                while (ps < pe && isspace((unsigned char)*ps)) ++ps;
                while (pe > ps && isspace((unsigned char)pe[-1])) --pe;
                if (pe - ps >= 2 && ps[0] == '(' && pe[-1] == ')') {
                    char tvars[FLOW_TUPLE_MAX][FLOW_NAME] = {{0}};
                    size_t tcnt = 0;
                    const char *q = ps + 1, *qe = pe - 1;
                    while (q < qe && tcnt < FLOW_TUPLE_MAX) {
                        while (q < qe && (isspace((unsigned char)*q) || *q == ',')) ++q;
                        size_t vk = 0;
                        while (q < qe && (isalnum((unsigned char)*q) || *q == '_')) {
                            if (vk + 1 < FLOW_NAME) tvars[tcnt][vk++] = *q;
                            ++q;
                        }
                        while (q < qe && *q != ',') ++q;
                        if (tvars[tcnt][0]) tcnt++;
                    }
                    if (tcnt >= 2) {
                        char accum[FLOW_LEAN_BODY_MAX] = {0};
                        size_t aj = 0;
                        for (size_t ti = 0; ti < tcnt; ++ti) {
                            char comp_rhs[FLOW_LEAN_BODY_MAX] = {0};
                            if (!project_lean_tuple_component(spec, rhs, (size_t)(rhs_end - rhs), ti, tcnt, comp_rhs, sizeof(comp_rhs)))
                                return 0;
                            int w = snprintf(accum + aj, sizeof(accum) - aj, "let %s := (%s) in ", tvars[ti], comp_rhs);
                            if (w < 0 || (size_t)w >= sizeof(accum) - aj) return 0;
                            aj += (size_t)w;
                        }
                        snprintf(out, out_max, "%s%s", accum, rest_proj);
                        return 1;
                    }
                }
                char vname[FLOW_NAME] = {0}; size_t vk = 0;
                for (const char *p = ps; p < pe && *p != ':' && !isspace((unsigned char)*p); ++p)
                    if (vk + 1 < sizeof(vname)) vname[vk++] = *p;
                if (!vname[0]) return 0;
                snprintf(out, out_max, "let %s := %.*s in %s", vname, (int)(rhs_end - rhs), rhs, rest_proj);
                return 1;
            }
        }
    }
    if (is_kw_tok(s, s, end, "if", 2)) {
        const char *th = NULL, *el = NULL; int pd = 0, id = 0;
        for (const char *p = s + 2; p < end; ++p) {
            if (*p == '(') pd++; else if (*p == ')' && pd > 0) pd--;
            else if (pd == 0 && is_kw_tok(p, s, end, "if", 2)) id++;
            else if (pd == 0 && is_kw_tok(p, s, end, "then", 4)) {
                if (id == 0) { th = p; break; }
                id--;
            }
        }
        if (th) {
            pd = 0; id = 0;
            for (const char *p = th + 4; p < end; ++p) {
                if (*p == '(') pd++; else if (*p == ')' && pd > 0) pd--;
                else if (pd == 0 && is_kw_tok(p, s, end, "if", 2)) id++;
                else if (pd == 0 && is_kw_tok(p, s, end, "else", 4)) {
                    if (id == 0) { el = p; break; }
                    id--;
                }
            }
        }
        if (th && el) {
            const char *cs = s + 2, *ce_c = th;
            while (cs < ce_c && isspace((unsigned char)*cs)) ++cs;
            while (ce_c > cs && isspace((unsigned char)ce_c[-1])) --ce_c;
            char t_proj[FLOW_LEAN_BODY_MAX] = {0}, e_proj[FLOW_LEAN_BODY_MAX] = {0};
            if (!project_lean_tuple_component(spec, th + 4, (size_t)(el - (th + 4)), comp_idx, arity, t_proj, sizeof(t_proj)) ||
                !project_lean_tuple_component(spec, el + 4, (size_t)(end - (el + 4)), comp_idx, arity, e_proj, sizeof(e_proj)))
                return 0;
            snprintf(out, out_max, "if %.*s then (%s) else (%s)", (int)(ce_c - cs), cs, t_proj, e_proj);
            return 1;
        }
    }
    if (arity <= 1) {
        if (len >= out_max) len = out_max - 1;
        memcpy(out, s, len); out[len] = '\0';
        return 1;
    }
    while (len >= 2 && s[0] == '(' && s[len - 1] == ')') {
        int d = 0, wrapped = 1;
        for (size_t i = 0; i + 1 < len; ++i) {
            if (s[i] == '(') d++;
            else if (s[i] == ')' && --d == 0) { wrapped = 0; break; }
        }
        if (!wrapped) break;
        ++s; len -= 2;
        while (len > 0 && isspace((unsigned char)*s)) { ++s; --len; }
        while (len > 0 && isspace((unsigned char)s[len - 1])) --len;
    }
    end = s + len;
    if (is_kw_tok(s, s, end, "let", 3) || is_kw_tok(s, s, end, "if", 2))
        return project_lean_tuple_component(spec, s, len, comp_idx, arity, out, out_max);
    const char *seg_s[FLOW_TUPLE_MAX + 1];
    const char *seg_e[FLOW_TUPLE_MAX + 1];
    size_t seg_cnt = 0;
    const char *cur = s; int pd = 0;
    for (const char *p = s; p < end; ++p) {
        if (*p == '(') pd++; else if (*p == ')' && pd > 0) pd--;
        else if (pd == 0 && *p == ',' && seg_cnt < FLOW_TUPLE_MAX) {
            seg_s[seg_cnt] = cur; seg_e[seg_cnt] = p; seg_cnt++;
            cur = p + 1;
        }
    }
    if (seg_cnt > 0 && seg_cnt + 1 >= arity && comp_idx <= seg_cnt) {
        seg_s[seg_cnt] = cur; seg_e[seg_cnt] = end; seg_cnt++;
        const char *cs = seg_s[comp_idx], *ce_s = seg_e[comp_idx];
        while (cs < ce_s && isspace((unsigned char)*cs)) ++cs;
        while (ce_s > cs && isspace((unsigned char)ce_s[-1])) --ce_s;
        size_t cl = (size_t)(ce_s - cs);
        if (cl == 0 || cl >= out_max) return 0;
        memcpy(out, cs, cl); out[cl] = '\0';
        return 1;
    }
    if (spec && (isalpha((unsigned char)*s) || *s == '_')) {
        char cname[FLOW_NAME] = {0}; size_t ck = 0;
        const char *p = s;
        while (p < end && (isalnum((unsigned char)*p) || *p == '_')) {
            if (ck + 1 < sizeof(cname)) cname[ck++] = *p;
            ++p;
        }
        const FlowLeanFunction *callee = NULL;
        for (size_t fi = 0; fi < spec->lean_function_count; ++fi) {
            if (!strcmp(spec->lean_functions[fi].name, cname) && spec->lean_functions[fi].is_tuple_return && comp_idx < spec->lean_functions[fi].tuple_arity) {
                callee = &spec->lean_functions[fi];
                break;
            }
        }
        if (callee) {
            const char *arg_s[FLOW_LEAN_PARAM_MAX], *arg_e[FLOW_LEAN_PARAM_MAX];
            size_t acnt = 0;
            while (p < end && acnt < callee->param_count) {
                while (p < end && isspace((unsigned char)*p)) ++p;
                if (p >= end) break;
                if (*p == '(') {
                    int d = 0; arg_s[acnt] = p + 1;
                    while (p < end) {
                        if (*p == '(') d++;
                        else if (*p == ')' && --d == 0) { arg_e[acnt] = p; ++p; break; }
                        ++p;
                    }
                    acnt++;
                } else {
                    arg_s[acnt] = p;
                    while (p < end && !isspace((unsigned char)*p) && *p != '(' && *p != ')') ++p;
                    arg_e[acnt] = p;
                    acnt++;
                }
            }
            while (p < end && isspace((unsigned char)*p)) ++p;
            if (acnt == callee->param_count && p == end) {
                const char *tpl = callee->tuple_exprs[comp_idx];
                size_t j = 0;
                for (size_t ti = 0; tpl[ti] && j + 4 < out_max; ) {
                    int matched = 0;
                    if (isalpha((unsigned char)tpl[ti]) || tpl[ti] == '_') {
                        int l_ok = (ti == 0) || (!isalnum((unsigned char)tpl[ti - 1]) && tpl[ti - 1] != '_' && tpl[ti - 1] != '.');
                        if (l_ok) {
                            for (size_t pi = 0; pi < callee->param_count; ++pi) {
                                size_t plen = strlen(callee->param_names[pi]);
                                if (plen > 0 && !strncmp(tpl + ti, callee->param_names[pi], plen) &&
                                    !isalnum((unsigned char)tpl[ti + plen]) && tpl[ti + plen] != '_') {
                                    size_t alen = (size_t)(arg_e[pi] - arg_s[pi]);
                                    if (j + alen + 3 < out_max) {
                                        out[j++] = '(';
                                        memcpy(out + j, arg_s[pi], alen);
                                        j += alen;
                                        out[j++] = ')';
                                        ti += plen;
                                        matched = 1;
                                    }
                                    break;
                                }
                            }
                        }
                    }
                    if (!matched) out[j++] = tpl[ti++];
                }
                out[j] = '\0';
                return j > 0;
            }
        }
    }
    return 0;
}
static void inline_lean_scalar_calls(const FlowSpec *spec, char *expr, size_t max_len) {
    if (!spec || !expr || !expr[0]) return;
    for (int iter = 0; iter < 12; ++iter) {
        int changed = 0;
        for (size_t fi = 0; fi < spec->lean_function_count && !changed; ++fi) {
            const FlowLeanFunction *callee = &spec->lean_functions[fi];
            if (callee->is_struct_transform || callee->is_tuple_return || callee->param_count == 0) continue;
            size_t fn_len = strlen(callee->name);
            if (fn_len == 0) continue;
            for (char *p = strstr(expr, callee->name); p; p = strstr(p + 1, callee->name)) {
                int l_ok = (p == expr) || (!isalnum((unsigned char)p[-1]) && p[-1] != '_' && p[-1] != '.');
                int r_ok = (p[fn_len] == ' ' || p[fn_len] == '\t' || p[fn_len] == '(');
                if (!l_ok || !r_ok) continue;
                const char *q = p + fn_len;
                const char *arg_s[FLOW_LEAN_PARAM_MAX], *arg_e[FLOW_LEAN_PARAM_MAX];
                size_t acnt = 0;
                int args_ok = 1;
                while (acnt < callee->param_count) {
                    while (*q == ' ' || *q == '\t') ++q;
                    if (!*q) { args_ok = 0; break; }
                    if (*q == '(') {
                        int d = 0;
                        arg_s[acnt] = q + 1;
                        while (*q) {
                            if (*q == '(') d++;
                            else if (*q == ')' && --d == 0) { arg_e[acnt] = q; ++q; break; }
                            ++q;
                        }
                        if (d != 0) { args_ok = 0; break; }
                        acnt++;
                    } else if (isalnum((unsigned char)*q) || *q == '_') {
                        arg_s[acnt] = q;
                        while (isalnum((unsigned char)*q) || *q == '_' || *q == '.') {
                            if ((*q == 'e' || *q == 'E') && isdigit((unsigned char)arg_s[acnt][0]) && (q[1] == '+' || q[1] == '-')) ++q;
                            ++q;
                        }
                        arg_e[acnt] = q;
                        size_t al = (size_t)(arg_e[acnt] - arg_s[acnt]);
                        if ((al == 2 && (!strncmp(arg_s[acnt], "in", 2) || !strncmp(arg_s[acnt], "if", 2))) ||
                            (al == 3 && !strncmp(arg_s[acnt], "let", 3)) ||
                            (al == 4 && (!strncmp(arg_s[acnt], "then", 4) || !strncmp(arg_s[acnt], "else", 4) || !strncmp(arg_s[acnt], "with", 4))) ||
                            (al == 5 && !strncmp(arg_s[acnt], "match", 5))) {
                            args_ok = 0;
                            break;
                        }
                        acnt++;
                    } else {
                        args_ok = 0;
                        break;
                    }
                }
                if (!args_ok || acnt != callee->param_count) continue;
                char sub_body[FLOW_LEAN_BODY_MAX] = {0};
                size_t j = 0;
                const char *tpl = callee->body;
                int sub_ok = 1;
                for (size_t ti = 0; tpl[ti]; ) {
                    if (j + 4 >= sizeof(sub_body)) { sub_ok = 0; break; }
                    int matched = 0;
                    if (isalpha((unsigned char)tpl[ti]) || tpl[ti] == '_') {
                        int tl_ok = (ti == 0) || (!isalnum((unsigned char)tpl[ti - 1]) && tpl[ti - 1] != '_' && tpl[ti - 1] != '.');
                        if (tl_ok) {
                            for (size_t pi = 0; pi < callee->param_count; ++pi) {
                                size_t plen = strlen(callee->param_names[pi]);
                                if (plen > 0 && !strncmp(tpl + ti, callee->param_names[pi], plen) &&
                                    !isalnum((unsigned char)tpl[ti + plen]) && tpl[ti + plen] != '_') {
                                    size_t alen = (size_t)(arg_e[pi] - arg_s[pi]);
                                    if (j + alen + 3 >= sizeof(sub_body)) { sub_ok = 0; break; }
                                    sub_body[j++] = '(';
                                    memcpy(sub_body + j, arg_s[pi], alen);
                                    j += alen;
                                    sub_body[j++] = ')';
                                    ti += plen;
                                    matched = 1;
                                    break;
                                }
                            }
                        }
                    }
                    if (!sub_ok) break;
                    if (!matched) sub_body[j++] = tpl[ti++];
                }
                if (!sub_ok || j == 0) continue;
                sub_body[j] = '\0';
                size_t pre_l = (size_t)(p - expr);
                size_t post_l = strlen(q);
                if (pre_l + j + post_l + 3 >= max_len) continue;
                char next_buf[FLOW_LEAN_BODY_MAX] = {0};
                if (pre_l + j + post_l + 3 >= sizeof(next_buf)) continue;
                snprintf(next_buf, sizeof(next_buf), "%.*s(%s)%s", (int)pre_l, expr, sub_body, q);
                strncpy(expr, next_buf, max_len - 1);
                expr[max_len - 1] = '\0';
                changed = 1;
                break;
            }
        }
        if (!changed) break;
    }
}
static void try_add_lean_function(FlowSpec *spec, const char *sig_in, const char *body_in) {
    if (!spec || spec->lean_function_count >= FLOW_LEAN_FN_MAX || !sig_in || !body_in) return;
    char sig[FLOW_LEAN_BODY_MAX]; strncpy(sig, sig_in, sizeof(sig) - 1); sig[sizeof(sig) - 1] = '\0';
    char *s = trim(sig); if (!starts_with(s, "def ")) return;
    FlowLeanFunction fn; memset(&fn, 0, sizeof(fn));
    if (sscanf(s + 4, "%63[^ ({:\t\n\r]", fn.name) != 1 || !fn.name[0] || is_internal_lean_def(fn.name)) return;
    for (size_t i = 0; i < spec->lean_function_count; ++i)
        if (!strcmp(spec->lean_functions[i].name, fn.name)) return;
    char *first_lp = strchr(s, '('), *last_rp = NULL, *ret_colon = NULL;
    if (!first_lp) return;
    int pd = 0;
    for (char *p = first_lp; *p; ++p) {
        if (*p == '(' || *p == '{' || *p == '[') pd++;
        else if (*p == ')' || *p == '}' || *p == ']') {
            if (pd > 0) pd--;
            if (pd == 0 && *p == ')') last_rp = p;
        } else if (pd == 0 && *p == ':' && last_rp != NULL) {
            ret_colon = p;
            break;
        }
    }
    if (!last_rp || !ret_colon || last_rp <= first_lp) return;
    char ret_buf[FLOW_LINE] = {0};
    strncpy(ret_buf, ret_colon + 1, sizeof(ret_buf) - 1);
    char *ret_raw = trim(ret_buf);
    size_t rlen = strlen(ret_raw);
    if (rlen >= 2 && ret_raw[0] == '(' && ret_raw[rlen - 1] == ')') {
        ret_raw[rlen - 1] = '\0';
        ret_raw = trim(ret_raw + 1);
    }
    int ret_is_struct = 0, ret_is_scalar = 0, ret_is_tuple = 0;
    if (strstr(ret_raw, "\xc3\x97") != NULL || strchr(ret_raw, '*') != NULL) {
        char tcopy[FLOW_LINE] = {0};
        size_t tj = 0;
        for (size_t i = 0; ret_raw[i] && tj + 2 < sizeof(tcopy); ) {
            if (!strncmp(ret_raw + i, "\xc3\x97", 2)) { tcopy[tj++] = '*'; i += 2; }
            else tcopy[tj++] = ret_raw[i++];
        }
        tcopy[tj] = '\0';
        char *sp = NULL;
        for (char *tok = strtok_r(tcopy, "*", &sp); tok; tok = strtok_r(NULL, "*", &sp)) {
            char *tt = trim(tok);
            if (!*tt || fn.tuple_arity >= FLOW_TUPLE_MAX || !is_scalar_lean_type(tt)) { fn.tuple_arity = 0; break; }
            const char *norm_tt = normalize_lean_type_token(tt);
            strncpy(fn.tuple_types[fn.tuple_arity++], norm_tt, sizeof(fn.tuple_types[0]) - 1);
        }
        if (fn.tuple_arity >= 2) {
            ret_is_tuple = 1;
            fn.is_tuple_return = 1;
            strncpy(fn.return_type, fn.tuple_types[0], sizeof(fn.return_type) - 1);
        }
    }
    if (!ret_is_tuple) {
        char ret_t[FLOW_NAME] = {0}, extra[16] = {0};
        if (sscanf(ret_raw, "%63s %15s", ret_t, extra) != 1) return;
        const char *norm_ret = normalize_lean_type_token(ret_t);
        ret_is_struct = (find_lean_struct(spec, norm_ret) != NULL);
        ret_is_scalar = is_scalar_lean_type(norm_ret) || (find_lean_enum(spec, norm_ret) != NULL);
        if (!ret_is_struct && !ret_is_scalar) return;
        strncpy(fn.return_type, norm_ret, sizeof(fn.return_type) - 1);
    }
    for (char *p = first_lp; p && p < last_rp; ) {
        char *lp = strchr(p, '('); if (!lp || lp >= last_rp) break;
        char *rp = strchr(lp + 1, ')'); if (!rp || rp > last_rp) return;
        char grp[256] = {0}; size_t glen = (size_t)(rp - (lp + 1));
        if (glen >= sizeof(grp)) return;
        memcpy(grp, lp + 1, glen);
        char *colon = strchr(grp, ':'); if (!colon) return;
        *colon = '\0';
        char *p_rhs = trim(colon + 1);
        if (is_lean_erased_proof_field(p_rhs)) { p = rp + 1; continue; }
        char pt[FLOW_NAME] = {0}, pextra[16] = {0};
        if (sscanf(p_rhs, "%63s %15s", pt, pextra) != 1) return;
        const char *norm_pt = normalize_lean_type_token(pt);
        if (!is_scalar_lean_type(norm_pt) && !find_lean_struct(spec, norm_pt) && !find_lean_enum(spec, norm_pt)) return;
        for (char *tok = strtok(grp, " \t\n\r"); tok; tok = strtok(NULL, " \t\n\r")) {
            if (fn.param_count >= FLOW_LEAN_PARAM_MAX) return;
            strncpy(fn.param_names[fn.param_count], tok, sizeof(fn.param_names[0]) - 1);
            strncpy(fn.param_types[fn.param_count], norm_pt, sizeof(fn.param_types[0]) - 1);
            fn.param_count++;
        }
        p = rp + 1;
    }
    if (fn.param_count == 0) return;
    char norm_body[FLOW_LEAN_BODY_MAX] = {0};
    normalize_lean_body(body_in, norm_body, sizeof(norm_body));
    inline_lean_scalar_calls(spec, norm_body, sizeof(norm_body));
    if (ret_is_struct) {
        fn.is_struct_transform = 1;
        parse_struct_record_fields(&fn, "", norm_body);
        if (fn.field_assign_count == 0) return;
        for (size_t ai = 0; ai < fn.field_assign_count; ++ai)
            inline_lean_scalar_calls(spec, fn.field_assign_exprs[ai], sizeof(fn.field_assign_exprs[ai]));
    } else if (ret_is_tuple) {
        for (size_t ti = 0; ti < fn.tuple_arity; ++ti) {
            if (!project_lean_tuple_component(spec, norm_body, strlen(norm_body), ti, fn.tuple_arity, fn.tuple_exprs[ti], sizeof(fn.tuple_exprs[ti])))
                return;
            inline_lean_scalar_calls(spec, fn.tuple_exprs[ti], sizeof(fn.tuple_exprs[ti]));
            if (!is_valid_scalar_lean_body_str(spec, &fn, fn.tuple_exprs[ti]))
                return;
        }
    } else {
        strncpy(fn.body, norm_body, sizeof(fn.body) - 1);
        if (strstr(fn.body, "let (") != NULL) {
            char desugared[FLOW_LEAN_BODY_MAX] = {0};
            if (project_lean_tuple_component(spec, fn.body, strlen(fn.body), 0, 1, desugared, sizeof(desugared)) && desugared[0])
                strncpy(fn.body, desugared, sizeof(fn.body) - 1);
            inline_lean_scalar_calls(spec, fn.body, sizeof(fn.body));
        }
        if (!is_valid_scalar_lean_body(spec, &fn)) return;
    }
    fn.estimated_wcet_ns = estimate_lean_ast_wcet_ns(&fn);
    spec->lean_functions[spec->lean_function_count++] = fn;
}
static char *strip_lean_decl_modifiers(char *line) {
    if (!line) return line;
    for (;;) {
        if (starts_with(line, "public ")) line = trim(line + 7);
        else if (starts_with(line, "private ")) line = trim(line + 8);
        else if (starts_with(line, "noncomputable ")) line = trim(line + 14);
        else if (starts_with(line, "protected ")) line = trim(line + 10);
        else if (starts_with(line, "@[inline] ")) line = trim(line + 10);
        else if (starts_with(line, "@[simp] ")) line = trim(line + 8);
        else break;
    }
    return line;
}
static int is_lean_top_decl(const char *line) {
    if (!strcmp(line, "section") || !strcmp(line, "end") || !strcmp(line, "noncomputable section")) return 1;
    static const char *const P[] = {
        "def ", "theorem ", "lemma ", "abbrev ", "axiom ", "noncomputable ",
        "structure ", "inductive ", "namespace ", "section ", "variable ",
        "end ", "@[", "#print ", "#check ", "#eval ", "opaque ", "open ", "import ", "module", "deriving ", NULL
    };
    for (int i = 0; P[i]; ++i) if (starts_with(line, P[i])) return 1;
    return 0;
}
static int strip_lean_comments(const char *src, char *clean_out, char *nostr_out, size_t cap, char *err_msg, size_t err_size) {
    size_t j = 0; int block_depth = 0, in_str = 0;
    for (size_t i = 0; src[i] && j + 1 < cap; ) {
        if (!in_str && src[i] == '/' && src[i + 1] == '-') { block_depth++; i += 2; continue; }
        if (!in_str && block_depth > 0 && src[i] == '-' && src[i + 1] == '/') { block_depth--; i += 2; if (j > 0 && clean_out[j - 1] != ' ' && clean_out[j - 1] != '\n') { clean_out[j] = ' '; nostr_out[j] = ' '; j++; } continue; }
        if (block_depth > 0) { if (src[i] == '\n') { clean_out[j] = '\n'; nostr_out[j] = '\n'; j++; } i++; continue; }
        if (!in_str && src[i] == '-' && src[i + 1] == '-') { while (src[i] && src[i] != '\n') i++; continue; }
        if (src[i] == '"') {
            in_str = !in_str;
            clean_out[j] = src[i]; nostr_out[j] = ' '; j++; i++; continue;
        }
        if (in_str && src[i] == '\\' && src[i + 1]) {
            clean_out[j] = src[i]; nostr_out[j] = ' '; j++; i++;
            if (j + 1 < cap) { clean_out[j] = src[i]; nostr_out[j] = ' '; j++; i++; }
            continue;
        }
        clean_out[j] = src[i];
        nostr_out[j] = in_str ? ' ' : src[i];
        j++; i++;
    }
    clean_out[j] = '\0'; nostr_out[j] = '\0';
    if (block_depth > 0) {
        if (err_msg && err_size) snprintf(err_msg, err_size, "Lean audit failed: unclosed '/-' block comment");
        return 0;
    }
    for (const char *p = nostr_out; *p; ++p) {
        int left_ok = (p == nostr_out) || (!isalnum((unsigned char)p[-1]) && p[-1] != '_' && p[-1] != '.');
        if (!left_ok) continue;
        static const char *const FORBIDDEN[] = {"sorry", "admit", "sorryAx", "native_decide", NULL};
        for (int f = 0; FORBIDDEN[f]; ++f) {
            size_t flen = strlen(FORBIDDEN[f]);
            if (!strncmp(p, FORBIDDEN[f], flen) && !isalnum((unsigned char)p[flen]) && p[flen] != '_' && p[flen] != '\'') {
                if (err_msg && err_size) snprintf(err_msg, err_size, "Lean audit failed: forbidden '%s' token detected", FORBIDDEN[f]);
                return 0;
            }
        }
        if (!strncmp(p, "axiom", 5) && isspace((unsigned char)p[5])) {
            if (err_msg && err_size) snprintf(err_msg, err_size, "Lean audit failed: custom 'axiom' declaration forbidden");
            return 0;
        }
    }
    return 1;
}
typedef struct {
    char const_names[64][FLOW_NAME];
    int64_t const_vals[64];
    double const_fvals[64];
    int const_is_nat[64];
    int const_is_float[64];
    size_t const_count;
    char var_names[64][FLOW_NAME];
    int64_t var_vals[64];
    double var_fvals[64];
    int var_is_nat[64];
    int var_is_float[64];
    size_t var_count;
    const FlowSpec *spec;
} LeanEvalEnv;
static int lean_eval_slice(const char *s, size_t len, const LeanEvalEnv *env, int is_nat_ctx, int depth, int64_t *out);
static int slice_has_float_semantics(const char *s, size_t len, const LeanEvalEnv *env) {
    for (size_t i = 0; i < len; ++i) {
        if (isdigit((unsigned char)s[i])) {
            size_t k = i;
            while (k < len && (isdigit((unsigned char)s[k]) || s[k] == '_')) ++k;
            if (k + 1 < len && s[k] == '.' && isdigit((unsigned char)s[k + 1])) return 1;
            if (k + 1 < len && (s[k] == 'e' || s[k] == 'E') &&
                (isdigit((unsigned char)s[k + 1]) || ((s[k + 1] == '+' || s[k + 1] == '-') && k + 2 < len && isdigit((unsigned char)s[k + 2]))))
                return 1;
            i = k;
            continue;
        }
        if (isalpha((unsigned char)s[i]) || s[i] == '_') {
            size_t k = i;
            while (k < len && (isalnum((unsigned char)s[k]) || s[k] == '_' || s[k] == '.')) ++k;
            size_t ilen = k - i;
            if (ilen > 11 && !strncmp(s + i, "flow_float_", 11)) return 1;
            if (ilen == 15 && !strncmp(s + i, "flow_dequantize", 15)) return 1;
            if (env && ilen < FLOW_NAME) {
                char id[FLOW_NAME] = {0};
                memcpy(id, s + i, ilen); id[ilen] = '\0';
                for (size_t vi = env->var_count; vi > 0; --vi)
                    if (!strcmp(env->var_names[vi - 1], id)) { if (env->var_is_float[vi - 1]) return 1; break; }
                for (size_t ci = 0; ci < env->const_count; ++ci)
                    if (!strcmp(env->const_names[ci], id) && env->const_is_float[ci]) return 1;
                if (env->spec) {
                    for (size_t fi = 0; fi < env->spec->lean_function_count; ++fi) {
                        const FlowLeanFunction *fn = &env->spec->lean_functions[fi];
                        if (!strcmp(fn->name, id) && (!strcmp(fn->return_type, "Float") || !strcmp(fn->return_type, "Float32")))
                            return 1;
                    }
                }
            }
            i = k - 1;
        }
    }
    return 0;
}
static int lean_eval_float_slice(const char *s, size_t len, const LeanEvalEnv *env, int depth, double *out) {
    if (depth > 32 || !out) return 0;
    while (len > 0 && isspace((unsigned char)*s)) { ++s; --len; }
    while (len > 0 && isspace((unsigned char)s[len - 1])) --len;
    if (!len) return 0;
    while (len >= 2 && s[0] == '(' && s[len - 1] == ')') {
        int d = 0, match_end = 1;
        for (size_t i = 0; i < len - 1; ++i) {
            if (s[i] == '(') d++;
            else if (s[i] == ')' && --d == 0) { match_end = 0; break; }
        }
        if (!match_end) break;
        ++s; len -= 2;
        while (len > 0 && isspace((unsigned char)*s)) { ++s; --len; }
        while (len > 0 && isspace((unsigned char)s[len - 1])) --len;
    }
    const char *end = s + len;
    if (len > 3 && !strncmp(s, "if ", 3)) {
        int pd = 0, id_depth = 0; const char *th = NULL, *el = NULL;
        for (const char *p = s + 3; p < end; ++p) {
            if (*p == '(') pd++; else if (*p == ')') pd--;
            if (pd == 0 && (p == s || isspace((unsigned char)p[-1]))) {
                if (!strncmp(p, "if ", 3)) id_depth++;
                else if (!strncmp(p, "then", 4) && (p + 4 == end || isspace((unsigned char)p[4])) && id_depth == 0 && !th) th = p;
                else if (!strncmp(p, "else", 4) && (p + 4 == end || isspace((unsigned char)p[4])) && th) {
                    if (id_depth == 0) { el = p; break; } else id_depth--;
                }
            }
        }
        if (th && el) {
            int64_t cval = 0;
            if (!lean_eval_slice(s + 3, (size_t)(th - (s + 3)), env, 0, depth + 1, &cval)) return 0;
            if (cval != 0) return lean_eval_float_slice(th + 4, (size_t)(el - (th + 4)), env, depth + 1, out);
            return lean_eval_float_slice(el + 4, (size_t)(end - (el + 4)), env, depth + 1, out);
        }
    }
    if (len > 4 && !strncmp(s, "let ", 4)) {
        const char *ce = NULL, *sep = NULL; int pd = 0, skip = 0;
        for (const char *p = s + 4; p + 1 < end; ++p) {
            if (*p == '(') pd++; else if (*p == ')') pd--;
            if (pd == 0 && !ce && p[0] == ':' && p[1] == '=') ce = p;
        }
        if (ce) {
            pd = 0; int ld = 0;
            for (const char *p = ce + 2; p < end; ++p) {
                if (*p == '(') pd++; else if (*p == ')' && pd > 0) pd--;
                else if (pd == 0 && is_kw_tok(p, ce + 2, end, "let", 3)) ld++;
                else if (pd == 0 && is_kw_tok(p, ce + 2, end, "in", 2)) {
                    if (ld == 0) { sep = p; skip = 2; break; }
                    ld--;
                }
            }
            if (!sep) {
                const char *r0 = ce + 2;
                while (r0 < end && isspace((unsigned char)*r0)) ++r0;
                pd = 0; int id = 0;
                for (const char *p = r0; p < end; ++p) {
                    if (*p == '(') pd++; else if (*p == ')' && pd > 0) pd--;
                    else if (pd == 0 && is_kw_tok(p, r0, end, "if", 2)) id++;
                    else if (pd == 0 && is_kw_tok(p, r0, end, "else", 4) && id > 0) id--;
                    else if (pd == 0 && id == 0 && (*p == ';' || *p == '\n')) { sep = p; skip = 1; break; }
                }
            }
        }
        if (ce && sep) {
            char vname[FLOW_NAME] = {0};
            const char *vs = s + 4; while (vs < ce && isspace((unsigned char)*vs)) ++vs;
            size_t vl = 0; while (vs + vl < ce && !isspace((unsigned char)vs[vl]) && vs[vl] != ':') ++vl;
            if (vl > 0 && vl < sizeof(vname)) {
                memcpy(vname, vs, vl);
                double rhs_fval = 0.0;
                if (!lean_eval_float_slice(ce + 2, (size_t)(sep - (ce + 2)), env, depth + 1, &rhs_fval)) return 0;
                LeanEvalEnv next_env = *env;
                if (next_env.var_count < 64) {
                    strncpy(next_env.var_names[next_env.var_count], vname, FLOW_NAME - 1);
                    next_env.var_fvals[next_env.var_count] = rhs_fval;
                    next_env.var_vals[next_env.var_count] = (int64_t)rhs_fval;
                    next_env.var_is_float[next_env.var_count] = 1;
                    next_env.var_is_nat[next_env.var_count] = 0;
                    next_env.var_count++;
                }
                return lean_eval_float_slice(sep + skip, (size_t)(end - (sep + skip)), &next_env, depth + 1, out);
            }
        }
    }
    for (int pass = 8; pass <= 10; ++pass) {
        if (pass == 10) {
            int pd = 0;
            for (const char *p = s + 1; p < end - 1; ++p) {
                if (*p == '(') pd++; else if (*p == ')') pd--;
                if (pd != 0) continue;
                if (p[0] == '^' && p[-1] != '^' && p[1] != '^') {
                    double a = 0.0, b = 0.0;
                    if (!lean_eval_float_slice(s, (size_t)(p - s), env, depth + 1, &a) ||
                        !lean_eval_float_slice(p + 1, (size_t)(end - (p + 1)), env, depth + 1, &b)) return 0;
                    *out = __builtin_pow(a, b);
                    return 1;
                }
            }
            continue;
        }
        int pd = 0;
        for (const char *p = end - 1; p > s; --p) {
            if (*p == ')') pd++; else if (*p == '(') pd--;
            if (pd != 0) continue;
            if (pass == 8 && (*p == '+' || *p == '-')) {
                const char *prev = p - 1;
                if ((*prev == 'e' || *prev == 'E') && prev > s && (isdigit((unsigned char)prev[-1]) || prev[-1] == '.')) continue;
                while (prev > s && isspace((unsigned char)*prev)) --prev;
                if (*p == '-' && (prev == s && (*prev == '+' || *prev == '-' || *prev == '*' || *prev == '/' || *prev == '%' || *prev == '^' || *prev == '=' || *prev == '<' || *prev == '>' || *prev == '!'))) continue;
                if (*p == '-' && (*prev == '+' || *prev == '-' || *prev == '*' || *prev == '/' || *prev == '%' || *prev == '^' || *prev == '=' || *prev == '<' || *prev == '>' || *prev == '&' || *prev == '|' || *prev == '!')) continue;
                double a = 0.0, b = 0.0;
                if (!lean_eval_float_slice(s, (size_t)(p - s), env, depth + 1, &a) ||
                    !lean_eval_float_slice(p + 1, (size_t)(end - (p + 1)), env, depth + 1, &b)) return 0;
                *out = (*p == '+') ? (a + b) : (a - b);
                return 1;
            }
            if (pass == 9 && (*p == '*' || *p == '/' || *p == '%')) {
                double a = 0.0, b = 0.0;
                if (!lean_eval_float_slice(s, (size_t)(p - s), env, depth + 1, &a) ||
                    !lean_eval_float_slice(p + 1, (size_t)(end - (p + 1)), env, depth + 1, &b)) return 0;
                if (*p == '*') *out = a * b;
                else if (*p == '/') *out = (b == 0.0) ? 0.0 : (a / b);
                else *out = (b == 0.0) ? 0.0 : __builtin_fmod(a, b);
                return 1;
            }
        }
    }
    if (s[0] == '-' && len > 1) {
        double v = 0.0;
        if (!lean_eval_float_slice(s + 1, len - 1, env, depth + 1, &v)) return 0;
        *out = -v;
        return 1;
    }
    const char *tok_s[FLOW_LEAN_PARAM_MAX + 2]; size_t tok_l[FLOW_LEAN_PARAM_MAX + 2], tok_cnt = 0;
    for (const char *p = s; p < end && tok_cnt < FLOW_LEAN_PARAM_MAX + 2; ) {
        while (p < end && isspace((unsigned char)*p)) ++p;
        if (p >= end) break;
        const char *ts = p;
        if (*p == '(') {
            int pd = 0;
            while (p < end) { if (*p == '(') pd++; else if (*p == ')' && --pd == 0) { ++p; break; } ++p; }
        } else {
            while (p < end && !isspace((unsigned char)*p) && *p != '(' && *p != ')') ++p;
        }
        tok_s[tok_cnt] = ts; tok_l[tok_cnt] = (size_t)(p - ts); tok_cnt++;
    }
    if (tok_cnt > 1) {
        if (tok_l[0] >= 14 && !strncmp(tok_s[0], "flow_float_", 11)) {
            const char *op = tok_s[0] + 11;
            size_t opl = tok_l[0] - 11;
            if (tok_cnt == 2) {
                double a = 0.0;
                if (!lean_eval_float_slice(tok_s[1], tok_l[1], env, depth + 1, &a)) return 0;
                if (opl == 3 && !strncmp(op, "abs", 3)) *out = __builtin_fabs(a);
                else if (opl == 3 && !strncmp(op, "neg", 3)) *out = -a;
                else if (opl == 4 && !strncmp(op, "sqrt", 4)) *out = a <= 0.0 ? 0.0 : __builtin_sqrt(a);
                else if (opl == 3 && !strncmp(op, "exp", 3)) *out = __builtin_exp(a);
                else if (opl == 3 && !strncmp(op, "log", 3)) *out = a <= 0.0 ? 0.0 : __builtin_log(a);
                else if (opl == 3 && !strncmp(op, "sin", 3)) *out = __builtin_sin(a);
                else if (opl == 3 && !strncmp(op, "cos", 3)) *out = __builtin_cos(a);
                else if (opl == 5 && !strncmp(op, "floor", 5)) *out = __builtin_floor(a);
                else if (opl == 4 && !strncmp(op, "ceil", 4)) *out = __builtin_ceil(a);
                else if (opl == 5 && !strncmp(op, "round", 5)) *out = __builtin_round(a);
                else if (opl == 6 && (!strncmp(op, "of_nat", 6) || !strncmp(op, "of_int", 6))) *out = a;
                else return 0;
                return 1;
            }
            if (tok_cnt == 3) {
                double a = 0.0, b = 0.0;
                if (!lean_eval_float_slice(tok_s[1], tok_l[1], env, depth + 1, &a) ||
                    !lean_eval_float_slice(tok_s[2], tok_l[2], env, depth + 1, &b)) return 0;
                if (opl == 3 && !strncmp(op, "min", 3)) *out = a < b ? a : b;
                else if (opl == 3 && !strncmp(op, "max", 3)) *out = a > b ? a : b;
                else if (opl == 3 && !strncmp(op, "pow", 3)) *out = __builtin_pow(a, b);
                else if (opl == 5 && !strncmp(op, "atan2", 5)) *out = __builtin_atan2(a, b);
                else if (opl == 3 && !strncmp(op, "add", 3)) *out = a + b;
                else if (opl == 3 && !strncmp(op, "sub", 3)) *out = a - b;
                else if (opl == 3 && !strncmp(op, "mul", 3)) *out = a * b;
                else if (opl == 3 && !strncmp(op, "div", 3)) *out = b == 0.0 ? 0.0 : (a / b);
                else if (opl == 3 && !strncmp(op, "mod", 3)) *out = b == 0.0 ? 0.0 : __builtin_fmod(a, b);
                else return 0;
                return 1;
            }
        }
        if ((tok_l[0] == 13 && !strncmp(tok_s[0], "flow_quantize", 13)) && tok_cnt == 5) {
            double a = 0.0, b = 0.0, c = 0.0, d = 0.0;
            if (!lean_eval_float_slice(tok_s[1], tok_l[1], env, depth + 1, &a) ||
                !lean_eval_float_slice(tok_s[2], tok_l[2], env, depth + 1, &b) ||
                !lean_eval_float_slice(tok_s[3], tok_l[3], env, depth + 1, &c) ||
                !lean_eval_float_slice(tok_s[4], tok_l[4], env, depth + 1, &d)) return 0;
            int64_t bits = (int64_t)d;
            if (c <= b || bits <= 0 || bits > 32) { *out = 0.0; return 1; }
            double dv = a < b ? b : (a > c ? c : a);
            uint64_t max_code = (bits == 32) ? 0xFFFFFFFFULL : ((1ULL << bits) - 1ULL);
            *out = (double)(uint64_t)(((dv - b) / (c - b)) * (double)max_code + 0.5);
            return 1;
        }
        if ((tok_l[0] == 15 && !strncmp(tok_s[0], "flow_dequantize", 15)) && tok_cnt == 5) {
            double a = 0.0, b = 0.0, c = 0.0, d = 0.0;
            if (!lean_eval_float_slice(tok_s[1], tok_l[1], env, depth + 1, &a) ||
                !lean_eval_float_slice(tok_s[2], tok_l[2], env, depth + 1, &b) ||
                !lean_eval_float_slice(tok_s[3], tok_l[3], env, depth + 1, &c) ||
                !lean_eval_float_slice(tok_s[4], tok_l[4], env, depth + 1, &d)) return 0;
            int64_t bits = (int64_t)d;
            if (c <= b || bits <= 0 || bits > 32) { *out = 0.0; return 1; }
            uint64_t max_code = (bits == 32) ? 0xFFFFFFFFULL : ((1ULL << bits) - 1ULL);
            uint64_t raw = (uint64_t)a > max_code ? max_code : (uint64_t)a;
            *out = b + ((double)raw / (double)max_code) * (c - b);
            return 1;
        }
        if (env && env->spec) {
            for (size_t fi = 0; fi < env->spec->lean_function_count; ++fi) {
                const FlowLeanFunction *fn = &env->spec->lean_functions[fi];
                if (!fn->is_struct_transform && strlen(fn->name) == tok_l[0] && !strncmp(tok_s[0], fn->name, tok_l[0]) && tok_cnt == fn->param_count + 1) {
                    LeanEvalEnv fn_env = *env; fn_env.var_count = 0;
                    for (size_t pi = 0; pi < fn->param_count; ++pi) {
                        const FlowLeanStruct *pst = find_lean_struct(env->spec, fn->param_types[pi]);
                        if (pst) {
                            char arg_id[FLOW_NAME] = {0};
                            if (tok_l[pi + 1] < sizeof(arg_id)) {
                                memcpy(arg_id, tok_s[pi + 1], tok_l[pi + 1]); arg_id[tok_l[pi + 1]] = '\0';
                                for (size_t sf = 0; sf < pst->field_count && fn_env.var_count < 64; ++sf) {
                                    char src_v[FLOW_NAME] = {0};
                                    snprintf(src_v, sizeof(src_v), "%s.%s", arg_id, pst->fields[sf].name);
                                    int64_t ival = 0; double fval = 0.0; int is_flt = 0;
                                    for (size_t vi = 0; vi < env->var_count; ++vi)
                                        if (!strcmp(env->var_names[vi], src_v)) { ival = env->var_vals[vi]; fval = env->var_fvals[vi]; is_flt = env->var_is_float[vi]; break; }
                                    snprintf(fn_env.var_names[fn_env.var_count], FLOW_NAME, "%s.%s", fn->param_names[pi], pst->fields[sf].name);
                                    fn_env.var_vals[fn_env.var_count] = ival;
                                    fn_env.var_fvals[fn_env.var_count] = is_flt ? fval : (double)ival;
                                    fn_env.var_is_float[fn_env.var_count] = is_flt;
                                    fn_env.var_is_nat[fn_env.var_count] = !strcmp(pst->fields[sf].lean_type, "Nat");
                                    fn_env.var_count++;
                                }
                            }
                        } else {
                            double faval = 0.0;
                            if (!lean_eval_float_slice(tok_s[pi + 1], tok_l[pi + 1], env, depth + 1, &faval)) return 0;
                            if (fn_env.var_count < 64) {
                                strncpy(fn_env.var_names[fn_env.var_count], fn->param_names[pi], FLOW_NAME - 1);
                                fn_env.var_fvals[fn_env.var_count] = faval;
                                fn_env.var_vals[fn_env.var_count] = (int64_t)faval;
                                fn_env.var_is_float[fn_env.var_count] = 1;
                                fn_env.var_is_nat[fn_env.var_count] = !strcmp(fn->param_types[pi], "Nat");
                                fn_env.var_count++;
                            }
                        }
                    }
                    return lean_eval_float_slice(fn->body, strlen(fn->body), &fn_env, depth + 1, out);
                }
            }
        }
        return 0;
    }
    char atom[128] = {0};
    if (len >= sizeof(atom)) return 0;
    memcpy(atom, s, len); atom[len] = '\0';
    if (!strcmp(atom, "true")) { *out = 1.0; return 1; }
    if (!strcmp(atom, "false")) { *out = 0.0; return 1; }
    if (isdigit((unsigned char)atom[0])) {
        char cleaned[128]; size_t cj = 0;
        for (size_t ci = 0; atom[ci] && cj + 1 < sizeof(cleaned); ++ci) if (atom[ci] != '_') cleaned[cj++] = atom[ci];
        cleaned[cj] = '\0';
        char *ep = NULL;
        double dv = strtod(cleaned, &ep);
        if (ep && *ep == '\0') { *out = dv; return 1; }
        return 0;
    }
    if (env) {
        for (size_t vi = env->var_count; vi > 0; --vi)
            if (!strcmp(env->var_names[vi - 1], atom)) {
                *out = env->var_is_float[vi - 1] ? env->var_fvals[vi - 1] : (double)env->var_vals[vi - 1];
                return 1;
            }
        for (size_t ci = 0; ci < env->const_count; ++ci)
            if (!strcmp(env->const_names[ci], atom)) {
                *out = env->const_is_float[ci] ? env->const_fvals[ci] : (double)env->const_vals[ci];
                return 1;
            }
    }
    return 0;
}
static int lean_eval_slice(const char *s, size_t len, const LeanEvalEnv *env, int is_nat_ctx, int depth, int64_t *out) {
    if (depth > 32 || !out) return 0;
    while (len > 0 && isspace((unsigned char)*s)) { ++s; --len; }
    while (len > 0 && isspace((unsigned char)s[len - 1])) --len;
    if (!len) return 0;
    while (len >= 2 && s[0] == '(' && s[len - 1] == ')') {
        int d = 0, match_end = 1;
        for (size_t i = 0; i < len - 1; ++i) {
            if (s[i] == '(') d++;
            else if (s[i] == ')' && --d == 0) { match_end = 0; break; }
        }
        if (!match_end) break;
        ++s; len -= 2;
        while (len > 0 && isspace((unsigned char)*s)) { ++s; --len; }
        while (len > 0 && isspace((unsigned char)s[len - 1])) --len;
    }
    const char *end = s + len;
    if (len > 3 && !strncmp(s, "if ", 3)) {
        int pd = 0, id_depth = 0; const char *th = NULL, *el = NULL;
        for (const char *p = s + 3; p < end; ++p) {
            if (*p == '(') pd++; else if (*p == ')') pd--;
            if (pd == 0 && (p == s || isspace((unsigned char)p[-1]))) {
                if (!strncmp(p, "if ", 3)) id_depth++;
                else if (!strncmp(p, "then", 4) && (p + 4 == end || isspace((unsigned char)p[4])) && id_depth == 0 && !th) th = p;
                else if (!strncmp(p, "else", 4) && (p + 4 == end || isspace((unsigned char)p[4])) && th) {
                    if (id_depth == 0) { el = p; break; } else id_depth--;
                }
            }
        }
        if (th && el) {
            int64_t cval = 0;
            if (!lean_eval_slice(s + 3, (size_t)(th - (s + 3)), env, 0, depth + 1, &cval)) return 0;
            if (cval != 0) return lean_eval_slice(th + 4, (size_t)(el - (th + 4)), env, is_nat_ctx, depth + 1, out);
            return lean_eval_slice(el + 4, (size_t)(end - (el + 4)), env, is_nat_ctx, depth + 1, out);
        }
    }
    if (len > 4 && !strncmp(s, "let ", 4)) {
        const char *ce = NULL, *sep = NULL; int pd = 0, skip = 0;
        for (const char *p = s + 4; p + 1 < end; ++p) {
            if (*p == '(') pd++; else if (*p == ')') pd--;
            if (pd == 0 && !ce && p[0] == ':' && p[1] == '=') ce = p;
        }
        if (ce) {
            pd = 0; int ld = 0;
            for (const char *p = ce + 2; p < end; ++p) {
                if (*p == '(') pd++; else if (*p == ')' && pd > 0) pd--;
                else if (pd == 0 && is_kw_tok(p, ce + 2, end, "let", 3)) ld++;
                else if (pd == 0 && is_kw_tok(p, ce + 2, end, "in", 2)) {
                    if (ld == 0) { sep = p; skip = 2; break; }
                    ld--;
                }
            }
            if (!sep) {
                const char *r0 = ce + 2;
                while (r0 < end && isspace((unsigned char)*r0)) ++r0;
                pd = 0; int id = 0;
                for (const char *p = r0; p < end; ++p) {
                    if (*p == '(') pd++; else if (*p == ')' && pd > 0) pd--;
                    else if (pd == 0 && is_kw_tok(p, r0, end, "if", 2)) id++;
                    else if (pd == 0 && is_kw_tok(p, r0, end, "else", 4) && id > 0) id--;
                    else if (pd == 0 && id == 0 && (*p == ';' || *p == '\n')) { sep = p; skip = 1; break; }
                }
            }
        }
        if (ce && sep) {
            char vname[FLOW_NAME] = {0};
            const char *vs = s + 4; while (vs < ce && isspace((unsigned char)*vs)) ++vs;
            size_t vl = 0; while (vs + vl < ce && !isspace((unsigned char)vs[vl]) && vs[vl] != ':') ++vl;
            if (vl > 0 && vl < sizeof(vname)) {
                memcpy(vname, vs, vl);
                LeanEvalEnv next_env = *env;
                if (slice_has_float_semantics(ce + 2, (size_t)(sep - (ce + 2)), env)) {
                    double rhs_fval = 0.0;
                    if (!lean_eval_float_slice(ce + 2, (size_t)(sep - (ce + 2)), env, depth + 1, &rhs_fval)) return 0;
                    if (next_env.var_count < 64) {
                        strncpy(next_env.var_names[next_env.var_count], vname, FLOW_NAME - 1);
                        next_env.var_fvals[next_env.var_count] = rhs_fval;
                        next_env.var_vals[next_env.var_count] = (int64_t)rhs_fval;
                        next_env.var_is_float[next_env.var_count] = 1;
                        next_env.var_is_nat[next_env.var_count] = 0;
                        next_env.var_count++;
                    }
                } else {
                    int64_t rhs_val = 0;
                    if (!lean_eval_slice(ce + 2, (size_t)(sep - (ce + 2)), env, is_nat_ctx, depth + 1, &rhs_val)) return 0;
                    if (next_env.var_count < 64) {
                        strncpy(next_env.var_names[next_env.var_count], vname, FLOW_NAME - 1);
                        next_env.var_vals[next_env.var_count] = rhs_val;
                        next_env.var_fvals[next_env.var_count] = (double)rhs_val;
                        next_env.var_is_float[next_env.var_count] = 0;
                        next_env.var_is_nat[next_env.var_count] = is_nat_ctx;
                        next_env.var_count++;
                    }
                }
                return lean_eval_slice(sep + skip, (size_t)(end - (sep + skip)), &next_env, is_nat_ctx, depth + 1, out);
            }
        }
    }
    if (len > 6 && !strncmp(s, "match ", 6)) {
        const char *w = NULL; int pd = 0;
        for (const char *p = s + 6; p + 4 <= end; ++p) {
            if (*p == '(') pd++; else if (*p == ')' && pd > 0) pd--;
            else if (pd == 0 && (p == s + 6 || isspace((unsigned char)p[-1]) || p[-1] == ')') &&
                     !strncmp(p, "with", 4) && (p + 4 == end || isspace((unsigned char)p[4]) || p[4] == '|')) { w = p; break; }
        }
        int64_t sval = 0;
        if (w && lean_eval_slice(s + 6, (size_t)(w - (s + 6)), env, is_nat_ctx, depth + 1, &sval)) {
            const char *arms[16]; size_t arm_cnt = 0; pd = 0;
            for (const char *p = w + 4; p < end; ++p) {
                if (*p == '(') pd++; else if (*p == ')' && pd > 0) pd--;
                else if (pd == 0 && *p == '|' && (p == w + 4 || p[-1] != '|') && (p + 1 == end || p[1] != '|'))
                    if (arm_cnt < 16) arms[arm_cnt++] = p;
            }
            for (size_t a = 0; a < arm_cnt; ++a) {
                const char *a_start = arms[a] + 1, *a_end = (a + 1 < arm_cnt) ? arms[a + 1] : end, *arr = NULL;
                for (const char *p = a_start; p + 1 < a_end; ++p) if (p[0] == '=' && p[1] == '>') { arr = p; break; }
                if (!arr) break;
                const char *pat = a_start; size_t pat_l = (size_t)(arr - a_start);
                while (pat_l > 0 && isspace((unsigned char)*pat)) { pat++; pat_l--; }
                while (pat_l > 0 && isspace((unsigned char)pat[pat_l - 1])) pat_l--;
                int64_t pval = 0;
                if ((pat_l == 1 && pat[0] == '_') || (lean_eval_slice(pat, pat_l, env, is_nat_ctx, depth + 1, &pval) && pval == sval))
                    return lean_eval_slice(arr + 2, (size_t)(a_end - (arr + 2)), env, is_nat_ctx, depth + 1, out);
            }
        }
    }
    for (int pass = 1; pass <= 10; ++pass) {
        if (pass == 10) {
            int pd = 0;
            for (const char *p = s + 1; p < end - 1; ++p) {
                if (*p == '(') pd++; else if (*p == ')') pd--;
                if (pd != 0) continue;
                if (p[0] == '^' && p[-1] != '^' && p[1] != '^') {
                    int64_t a = 0, b = 0;
                    if (!lean_eval_slice(s, (size_t)(p - s), env, is_nat_ctx, depth + 1, &a) ||
                        !lean_eval_slice(p + 1, (size_t)(end - (p + 1)), env, is_nat_ctx, depth + 1, &b)) return 0;
                    if (b < 0) { *out = 0; return 1; }
                    uint64_t r = 1, base = (uint64_t)a, exp = (uint64_t)b;
                    while (exp > 0) { if (exp & 1) r *= base; base *= base; exp >>= 1; }
                    *out = (int64_t)r; return 1;
                }
            }
            continue;
        }
        int pd = 0;
        for (const char *p = end - 1; p > s; --p) {
            if (*p == ')') pd++; else if (*p == '(') pd--;
            if (pd != 0) continue;
            if (pass == 1 && p + 1 < end && p[0] == '|' && p[1] == '|' && p[-1] != '|' && (p + 2 == end || p[2] != '|')) {
                int64_t a = 0, b = 0;
                if (!lean_eval_slice(s, (size_t)(p - s), env, 0, depth + 1, &a) ||
                    !lean_eval_slice(p + 2, (size_t)(end - (p + 2)), env, 0, depth + 1, &b)) return 0;
                *out = (a != 0 || b != 0) ? 1 : 0; return 1;
            }
            if (pass == 2 && p + 1 < end && p[0] == '&' && p[1] == '&' && p[-1] != '&' && (p + 2 == end || p[2] != '&')) {
                int64_t a = 0, b = 0;
                if (!lean_eval_slice(s, (size_t)(p - s), env, 0, depth + 1, &a) ||
                    !lean_eval_slice(p + 2, (size_t)(end - (p + 2)), env, 0, depth + 1, &b)) return 0;
                *out = (a != 0 && b != 0) ? 1 : 0; return 1;
            }
            if (pass == 3) {
                int op_len = 0; const char *op = NULL;
                if (p + 1 < end && ((!strncmp(p, "==", 2)) || (!strncmp(p, "!=", 2)) || (!strncmp(p, "<=", 2)) || (!strncmp(p, ">=", 2)))) {
                    if ((p[0] == '<' && p[-1] == '<') || (p[0] == '>' && p[-1] == '>')) continue;
                    op = p; op_len = 2;
                } else if (*p == '=' && p[-1] != '!' && p[-1] != '<' && p[-1] != '>' && p[-1] != ':' && p[-1] != '=' && (p + 1 == end || (p[1] != '=' && p[1] != '>'))) {
                    op = "="; op_len = 1;
                } else if ((*p == '<' || *p == '>') && p[-1] != '<' && p[-1] != '>' && p[-1] != '-' && (p + 1 == end || (p[1] != '=' && p[1] != '<' && p[1] != '>'))) {
                    op = p; op_len = 1;
                }
                if (op) {
                    if (slice_has_float_semantics(s, len, env)) {
                        double fa = 0.0, fb = 0.0;
                        if (lean_eval_float_slice(s, (size_t)(p - s), env, depth + 1, &fa) &&
                            lean_eval_float_slice(p + op_len, (size_t)(end - (p + op_len)), env, depth + 1, &fb)) {
                            double tol = 1e-12 * (__builtin_fabs(fa) + __builtin_fabs(fb) + 1.0);
                            if ((op_len == 1 && op[0] == '=') || !strncmp(op, "==", 2)) *out = (fa == fb || __builtin_fabs(fa - fb) <= tol);
                            else if (!strncmp(op, "!=", 2)) *out = (fa != fb && __builtin_fabs(fa - fb) > tol);
                            else if (!strncmp(op, "<=", 2)) *out = (fa <= fb + tol);
                            else if (!strncmp(op, ">=", 2)) *out = (fa + tol >= fb);
                            else if (op[0] == '<') *out = (fa < fb);
                            else *out = (fa > fb);
                            return 1;
                        }
                    }
                    int64_t a = 0, b = 0;
                    if (!lean_eval_slice(s, (size_t)(p - s), env, is_nat_ctx, depth + 1, &a) ||
                        !lean_eval_slice(p + op_len, (size_t)(end - (p + op_len)), env, is_nat_ctx, depth + 1, &b)) return 0;
                    if (op_len == 1 && op[0] == '=') *out = (a == b);
                    else if (!strncmp(op, "==", 2)) *out = (a == b);
                    else if (!strncmp(op, "!=", 2)) *out = (a != b);
                    else if (!strncmp(op, "<=", 2)) *out = is_nat_ctx ? ((uint64_t)a <= (uint64_t)b) : (a <= b);
                    else if (!strncmp(op, ">=", 2)) *out = is_nat_ctx ? ((uint64_t)a >= (uint64_t)b) : (a >= b);
                    else if (op[0] == '<') *out = is_nat_ctx ? ((uint64_t)a < (uint64_t)b) : (a < b);
                    else *out = is_nat_ctx ? ((uint64_t)a > (uint64_t)b) : (a > b);
                    return 1;
                }
            }
            if (pass == 4 && p + 2 < end && !strncmp(p, "|||", 3)) {
                int64_t a = 0, b = 0;
                if (!lean_eval_slice(s, (size_t)(p - s), env, is_nat_ctx, depth + 1, &a) ||
                    !lean_eval_slice(p + 3, (size_t)(end - (p + 3)), env, is_nat_ctx, depth + 1, &b)) return 0;
                *out = (int64_t)((uint64_t)a | (uint64_t)b); return 1;
            }
            if (pass == 5 && p + 2 < end && !strncmp(p, "^^^", 3)) {
                int64_t a = 0, b = 0;
                if (!lean_eval_slice(s, (size_t)(p - s), env, is_nat_ctx, depth + 1, &a) ||
                    !lean_eval_slice(p + 3, (size_t)(end - (p + 3)), env, is_nat_ctx, depth + 1, &b)) return 0;
                *out = (int64_t)((uint64_t)a ^ (uint64_t)b); return 1;
            }
            if (pass == 6 && p + 2 < end && !strncmp(p, "&&&", 3)) {
                int64_t a = 0, b = 0;
                if (!lean_eval_slice(s, (size_t)(p - s), env, is_nat_ctx, depth + 1, &a) ||
                    !lean_eval_slice(p + 3, (size_t)(end - (p + 3)), env, is_nat_ctx, depth + 1, &b)) return 0;
                *out = (int64_t)((uint64_t)a & (uint64_t)b); return 1;
            }
            if (pass == 7 && p + 2 < end && (!strncmp(p, "<<<", 3) || !strncmp(p, ">>>", 3))) {
                int64_t a = 0, b = 0;
                if (!lean_eval_slice(s, (size_t)(p - s), env, is_nat_ctx, depth + 1, &a) ||
                    !lean_eval_slice(p + 3, (size_t)(end - (p + 3)), env, is_nat_ctx, depth + 1, &b)) return 0;
                uint64_t sh = (uint64_t)b & 63ULL;
                *out = (p[0] == '<') ? (int64_t)((uint64_t)a << sh) : (int64_t)((uint64_t)a >> sh);
                return 1;
            }
            if (pass == 8 && (*p == '+' || *p == '-')) {
                const char *prev = p - 1;
                while (prev > s && isspace((unsigned char)*prev)) --prev;
                if (*p == '-' && (prev == s && (*prev == '+' || *prev == '-' || *prev == '*' || *prev == '/' || *prev == '%' || *prev == '^' || *prev == '=' || *prev == '<' || *prev == '>' || *prev == '!'))) continue;
                if (*p == '-' && (*prev == '+' || *prev == '-' || *prev == '*' || *prev == '/' || *prev == '%' || *prev == '^' || *prev == '=' || *prev == '<' || *prev == '>' || *prev == '&' || *prev == '|' || *prev == '!')) continue;
                int64_t a = 0, b = 0;
                if (!lean_eval_slice(s, (size_t)(p - s), env, is_nat_ctx, depth + 1, &a) ||
                    !lean_eval_slice(p + 1, (size_t)(end - (p + 1)), env, is_nat_ctx, depth + 1, &b)) return 0;
                if (*p == '+') *out = (int64_t)((uint64_t)a + (uint64_t)b);
                else if (is_nat_ctx) *out = ((uint64_t)a >= (uint64_t)b) ? (int64_t)((uint64_t)a - (uint64_t)b) : 0;
                else *out = (int64_t)((uint64_t)a - (uint64_t)b);
                return 1;
            }
            if (pass == 9 && (*p == '*' || *p == '/' || *p == '%')) {
                int64_t a = 0, b = 0;
                if (!lean_eval_slice(s, (size_t)(p - s), env, is_nat_ctx, depth + 1, &a) ||
                    !lean_eval_slice(p + 1, (size_t)(end - (p + 1)), env, is_nat_ctx, depth + 1, &b)) return 0;
                if (*p == '*') {
                    char rbuf[64] = {0}; const char *rs = p + 1; while (rs < end && isspace((unsigned char)*rs)) ++rs;
                    size_t rl = (size_t)(end - rs); if (rl > 0 && rl < sizeof(rbuf) && memchr(rs, '.', rl)) {
                        memcpy(rbuf, rs, rl); char *ep = NULL; double dv = strtod(rbuf, &ep);
                        if (ep && (*ep == '\0' || isspace((unsigned char)*ep))) { *out = (int64_t)((double)a * dv); return 1; }
                    }
                    *out = (int64_t)((uint64_t)a * (uint64_t)b);
                }
                else if (*p == '/') *out = (b == 0) ? 0 : (is_nat_ctx ? (int64_t)((uint64_t)a / (uint64_t)b) : (a == INT64_MIN && b == -1 ? INT64_MIN : a / b));
                else *out = (b == 0) ? a : (is_nat_ctx ? (int64_t)((uint64_t)a % (uint64_t)b) : (a == INT64_MIN && b == -1 ? 0 : a % b));
                return 1;
            }
        }
    }
    if (s[0] == '!') {
        int64_t v = 0;
        if (!lean_eval_slice(s + 1, len - 1, env, is_nat_ctx, depth + 1, &v)) return 0;
        *out = (v == 0) ? 1 : 0; return 1;
    }
    if (s[0] == '-' && len > 1) {
        int64_t v = 0;
        if (!lean_eval_slice(s + 1, len - 1, env, is_nat_ctx, depth + 1, &v)) return 0;
        *out = is_nat_ctx ? 0 : -v; return 1;
    }
    const char *tok_s[FLOW_LEAN_PARAM_MAX + 2]; size_t tok_l[FLOW_LEAN_PARAM_MAX + 2], tok_cnt = 0;
    for (const char *p = s; p < end && tok_cnt < FLOW_LEAN_PARAM_MAX + 2; ) {
        while (p < end && isspace((unsigned char)*p)) ++p;
        if (p >= end) break;
        const char *ts = p;
        if (*p == '(') {
            int pd = 0;
            while (p < end) { if (*p == '(') pd++; else if (*p == ')' && --pd == 0) { ++p; break; } ++p; }
        } else {
            while (p < end && !isspace((unsigned char)*p) && *p != '(' && *p != ')') ++p;
        }
        tok_s[tok_cnt] = ts; tok_l[tok_cnt] = (size_t)(p - ts); tok_cnt++;
    }
    if (tok_cnt > 1) {
        if (tok_l[0] == 6 && !strncmp(tok_s[0], "decide", 6) && tok_cnt == 2)
            return lean_eval_slice(tok_s[1], tok_l[1], env, is_nat_ctx, depth + 1, out);
        if (tok_l[0] == 12 && (!strncmp(tok_s[0], "flow_nat_min", 12) || !strncmp(tok_s[0], "flow_nat_max", 12)) && tok_cnt == 3) {
            int64_t a = 0, b = 0;
            if (!lean_eval_slice(tok_s[1], tok_l[1], env, 1, depth + 1, &a) ||
                !lean_eval_slice(tok_s[2], tok_l[2], env, 1, depth + 1, &b)) return 0;
            *out = (!strncmp(tok_s[0], "flow_nat_min", 12)) ? ((uint64_t)a < (uint64_t)b ? a : b) : ((uint64_t)a > (uint64_t)b ? a : b);
            return 1;
        }
        if (tok_l[0] == 14 && (!strncmp(tok_s[0], "flow_float_min", 14) || !strncmp(tok_s[0], "flow_float_max", 14)) && tok_cnt == 3) {
            int64_t a = 0, b = 0;
            if (!lean_eval_slice(tok_s[1], tok_l[1], env, 0, depth + 1, &a) ||
                !lean_eval_slice(tok_s[2], tok_l[2], env, 0, depth + 1, &b)) return 0;
            *out = (!strncmp(tok_s[0], "flow_float_min", 14)) ? (a < b ? a : b) : (a > b ? a : b);
            return 1;
        }
        if (tok_l[0] >= 14 && !strncmp(tok_s[0], "flow_float_", 11) && tok_cnt == 2) {
            int64_t a = 0;
            if (!lean_eval_slice(tok_s[1], tok_l[1], env, 0, depth + 1, &a)) return 0;
            if (tok_l[0] == 14 && !strncmp(tok_s[0], "flow_float_abs", 14)) *out = a < 0 ? -a : a;
            else if (tok_l[0] == 15 && !strncmp(tok_s[0], "flow_float_sqrt", 15)) *out = a > 0 ? (int64_t)__builtin_sqrt((double)a) : 0;
            else *out = a;
            return 1;
        }
        if ((tok_l[0] == 13 && !strncmp(tok_s[0], "flow_quantize", 13)) && tok_cnt == 5) {
            int64_t a = 0, b = 0, c = 0, d = 0;
            if (!lean_eval_slice(tok_s[1], tok_l[1], env, 0, depth + 1, &a) ||
                !lean_eval_slice(tok_s[2], tok_l[2], env, 0, depth + 1, &b) ||
                !lean_eval_slice(tok_s[3], tok_l[3], env, 0, depth + 1, &c) ||
                !lean_eval_slice(tok_s[4], tok_l[4], env, 1, depth + 1, &d)) return 0;
            if (c <= b || d <= 0 || d > 32) { *out = 0; return 1; }
            double dv = (double)a < (double)b ? (double)b : ((double)a > (double)c ? (double)c : (double)a);
            uint64_t max_code = (d == 32) ? 0xFFFFFFFFULL : ((1ULL << d) - 1ULL);
            *out = (int64_t)(((dv - (double)b) / (double)(c - b)) * (double)max_code + 0.5);
            return 1;
        }
        if ((tok_l[0] == 15 && !strncmp(tok_s[0], "flow_dequantize", 15)) && tok_cnt == 5) {
            int64_t a = 0, b = 0, c = 0, d = 0;
            if (!lean_eval_slice(tok_s[1], tok_l[1], env, 1, depth + 1, &a) ||
                !lean_eval_slice(tok_s[2], tok_l[2], env, 0, depth + 1, &b) ||
                !lean_eval_slice(tok_s[3], tok_l[3], env, 0, depth + 1, &c) ||
                !lean_eval_slice(tok_s[4], tok_l[4], env, 1, depth + 1, &d)) return 0;
            if (c <= b || d <= 0 || d > 32) { *out = 0; return 1; }
            uint64_t max_code = (d == 32) ? 0xFFFFFFFFULL : ((1ULL << d) - 1ULL);
            uint64_t raw = (uint64_t)a > max_code ? max_code : (uint64_t)a;
            double dv = (double)b + ((double)raw / (double)max_code) * (double)(c - b);
            *out = (int64_t)(dv >= 0.0 ? dv + 0.5 : dv - 0.5);
            return 1;
        }
        if ((tok_l[0] == 24 && (!strncmp(tok_s[0], "Exec.projectBoundedIndex", 24) || !strncmp(tok_s[0], "Spec.projectBoundedIndex", 24))) && tok_cnt == 2) {
            int64_t idx = 0, cap = 1024;
            if (!lean_eval_slice(tok_s[1], tok_l[1], env, 1, depth + 1, &idx)) return 0;
            for (size_t ci = 0; ci < env->const_count; ++ci)
                if (!strcmp(env->const_names[ci], "selectedCapacity")) { cap = env->const_vals[ci]; break; }
            *out = ((uint64_t)idx < (uint64_t)cap) ? idx : (cap > 0 ? cap - 1 : 0);
            return 1;
        }
        if (env->spec) {
            for (size_t fi = 0; fi < env->spec->lean_function_count; ++fi) {
                const FlowLeanFunction *fn = &env->spec->lean_functions[fi];
                if (!fn->is_struct_transform && strlen(fn->name) == tok_l[0] && !strncmp(tok_s[0], fn->name, tok_l[0]) && tok_cnt == fn->param_count + 1) {
                    LeanEvalEnv fn_env = *env; fn_env.var_count = 0;
                    for (size_t pi = 0; pi < fn->param_count; ++pi) {
                        const FlowLeanStruct *pst = find_lean_struct(env->spec, fn->param_types[pi]);
                        if (pst) {
                            char arg_id[FLOW_NAME] = {0};
                            if (tok_l[pi + 1] < sizeof(arg_id)) {
                                memcpy(arg_id, tok_s[pi + 1], tok_l[pi + 1]); arg_id[tok_l[pi + 1]] = '\0';
                                for (size_t sf = 0; sf < pst->field_count && fn_env.var_count < 64; ++sf) {
                                    char src_v[FLOW_NAME] = {0};
                                    snprintf(src_v, sizeof(src_v), "%s.%s", arg_id, pst->fields[sf].name);
                                    int64_t fval = 0; double ffval = 0.0; int is_flt = 0;
                                    for (size_t vi = 0; vi < env->var_count; ++vi)
                                        if (!strcmp(env->var_names[vi], src_v)) { fval = env->var_vals[vi]; ffval = env->var_fvals[vi]; is_flt = env->var_is_float[vi]; break; }
                                    snprintf(fn_env.var_names[fn_env.var_count], FLOW_NAME, "%s.%s", fn->param_names[pi], pst->fields[sf].name);
                                    fn_env.var_vals[fn_env.var_count] = fval;
                                    fn_env.var_fvals[fn_env.var_count] = ffval;
                                    fn_env.var_is_float[fn_env.var_count] = is_flt;
                                    fn_env.var_is_nat[fn_env.var_count] = !strcmp(pst->fields[sf].lean_type, "Nat");
                                    fn_env.var_count++;
                                }
                            }
                        } else {
                            int p_nat = !strcmp(fn->param_types[pi], "Nat");
                            int p_flt = !strcmp(fn->param_types[pi], "Float") || !strcmp(fn->param_types[pi], "Float32");
                            if (p_flt) {
                                double faval = 0.0;
                                if (!lean_eval_float_slice(tok_s[pi + 1], tok_l[pi + 1], env, depth + 1, &faval)) return 0;
                                if (fn_env.var_count < 64) {
                                    strncpy(fn_env.var_names[fn_env.var_count], fn->param_names[pi], FLOW_NAME - 1);
                                    fn_env.var_fvals[fn_env.var_count] = faval;
                                    fn_env.var_vals[fn_env.var_count] = (int64_t)faval;
                                    fn_env.var_is_float[fn_env.var_count] = 1;
                                    fn_env.var_is_nat[fn_env.var_count] = 0;
                                    fn_env.var_count++;
                                }
                            } else {
                                int64_t aval = 0;
                                if (!lean_eval_slice(tok_s[pi + 1], tok_l[pi + 1], env, p_nat, depth + 1, &aval)) return 0;
                                if (fn_env.var_count < 64) {
                                    strncpy(fn_env.var_names[fn_env.var_count], fn->param_names[pi], FLOW_NAME - 1);
                                    fn_env.var_vals[fn_env.var_count] = aval;
                                    fn_env.var_fvals[fn_env.var_count] = (double)aval;
                                    fn_env.var_is_float[fn_env.var_count] = 0;
                                    fn_env.var_is_nat[fn_env.var_count] = p_nat;
                                    fn_env.var_count++;
                                }
                            }
                        }
                    }
                    int ret_nat = !strcmp(fn->return_type, "Nat");
                    return lean_eval_slice(fn->body, strlen(fn->body), &fn_env, ret_nat, depth + 1, out);
                }
            }
        }
        return 0;
    }
    char atom[128] = {0};
    if (len >= sizeof(atom)) return 0;
    memcpy(atom, s, len); atom[len] = '\0';
    if (!strcmp(atom, "true")) { *out = 1; return 1; }
    if (!strcmp(atom, "false")) { *out = 0; return 1; }
    if (isdigit((unsigned char)atom[0])) {
        char cleaned[128]; size_t cj = 0;
        for (size_t ci = 0; atom[ci] && cj + 1 < sizeof(cleaned); ++ci) if (atom[ci] != '_') cleaned[cj++] = atom[ci];
        cleaned[cj] = '\0';
        char *ep = NULL;
        unsigned long long uv = strtoull(cleaned, &ep, 0);
        if (ep && *ep == '\0') { *out = (int64_t)uv; return 1; }
        if (ep && *ep == '.') {
            double dv = strtod(cleaned, &ep);
            if (ep && *ep == '\0') { *out = (int64_t)(dv >= 0.0 ? dv + 0.5 : dv - 0.5); return 1; }
        }
        return 0;
    }
    for (size_t vi = env->var_count; vi > 0; --vi)
        if (!strcmp(env->var_names[vi - 1], atom)) { *out = env->var_vals[vi - 1]; return 1; }
    for (size_t ci = 0; ci < env->const_count; ++ci)
        if (!strcmp(env->const_names[ci], atom)) { *out = env->const_vals[ci]; return 1; }
    return 0;
}
static void try_eval_lean_const_def(LeanEvalEnv *env, const char *def_sig, const char *def_body) {
    if (!env || !def_sig || !def_body || !def_body[0] || env->const_count >= 63) return;
    if (!starts_with(def_sig, "def ") || strchr(def_sig, '(') != NULL) return;
    char cname[FLOW_NAME] = {0}, ctype[32] = {0};
    if (sscanf(def_sig + 4, "%63[^ :\t] : %31[^ :=\t]", cname, ctype) < 1 || !cname[0]) return;
    for (size_t i = 0; i < env->const_count; ++i) if (!strcmp(env->const_names[i], cname)) return;
    char norm_rhs[FLOW_LEAN_BODY_MAX]; normalize_lean_body(def_body, norm_rhs, sizeof(norm_rhs));
    const char *norm_ct = normalize_lean_type_token(ctype);
    int is_flt = !strcmp(norm_ct, "Float") || !strcmp(norm_ct, "Float32") || slice_has_float_semantics(norm_rhs, strlen(norm_rhs), env);
    if (is_flt) {
        double fval = 0.0;
        if (lean_eval_float_slice(norm_rhs, strlen(norm_rhs), env, 0, &fval)) {
            strncpy(env->const_names[env->const_count], cname, FLOW_NAME - 1);
            env->const_fvals[env->const_count] = fval;
            env->const_vals[env->const_count] = (int64_t)fval;
            env->const_is_float[env->const_count] = 1;
            env->const_is_nat[env->const_count] = 0;
            env->const_count++;
            return;
        }
    }
    int64_t cval = 0; int is_nat = !strcmp(norm_ct, "Nat");
    if (lean_eval_slice(norm_rhs, strlen(norm_rhs), env, is_nat, 0, &cval)) {
        strncpy(env->const_names[env->const_count], cname, FLOW_NAME - 1);
        env->const_vals[env->const_count] = cval;
        env->const_fvals[env->const_count] = (double)cval;
        env->const_is_float[env->const_count] = 0;
        env->const_is_nat[env->const_count] = is_nat;
        env->const_count++;
    }
}
static int parse_lean_inductive_variants(FlowLeanEnum *en, LeanEvalEnv *env, const char *text, int enforce_rt_gate) {
    if (!en || !text) return 1;
    const char *p = strchr(text, '|');
    while (p) {
        const char *next_bar = NULL;
        for (const char *q = p + 1; *q; ++q) {
            if (*q == '|' && q[-1] != '|' && q[1] != '|') { next_bar = q; break; }
        }
        char arm[256] = {0};
        size_t alen = next_bar ? (size_t)(next_bar - (p + 1)) : strlen(p + 1);
        if (alen >= sizeof(arm)) alen = sizeof(arm) - 1;
        memcpy(arm, p + 1, alen); arm[alen] = '\0';
        char *der = strstr(arm, "deriving");
        if (der && (der == arm || isspace((unsigned char)der[-1]))) *der = '\0';
        char *tarm = trim(arm);
        char vname[FLOW_NAME] = {0};
        if (sscanf(tarm, "%63[^ (:=\t\r\n]", vname) == 1 && vname[0]) {
            const char *rem = tarm + strlen(vname);
            if (enforce_rt_gate && (has_word_match(rem, en->name) || has_word_match(rem, "List") ||
                                    has_word_match(rem, "Array") || has_word_match(rem, "ByteArray"))) {
                fprintf(stderr, "flowc: [Lean-RT Zero-GC Gate] rejected 'recursive/heap inductive %s' in '%s': dynamic heap allocation / closure / recursion violates hard real-time zero-GC invariants; use fixed-size 'structure' instead.\n",
                        vname, en->name);
                return 0;
            }
            int dup = 0;
            for (size_t vi = 0; vi < en->variant_count; ++vi)
                if (!strcmp(en->variants[vi], vname)) { dup = 1; break; }
            if (!dup && en->variant_count < 16) {
                size_t idx = en->variant_count++;
                strncpy(en->variants[idx], vname, FLOW_NAME - 1);
                if (env && env->const_count + 2 <= 64) {
                    strncpy(env->const_names[env->const_count], vname, FLOW_NAME - 1);
                    env->const_vals[env->const_count] = (int64_t)idx;
                    env->const_is_nat[env->const_count] = 1;
                    env->const_count++;
                    snprintf(env->const_names[env->const_count], FLOW_NAME, "%s.%s", en->name, vname);
                    env->const_vals[env->const_count] = (int64_t)idx;
                    env->const_is_nat[env->const_count] = 1;
                    env->const_count++;
                }
            }
        }
        p = next_bar;
    }
    return 1;
}
static int check_lean_rt_def_gate(const char *def_sig, const char *def_body) {
    if (!def_sig || !starts_with(def_sig, "def ")) return 1;
    char fn_name[FLOW_NAME] = {0};
    if (sscanf(def_sig + 4, "%63[^ ({:\t\n]", fn_name) != 1 || !fn_name[0] || is_internal_lean_def(fn_name)) return 1;
    static const char *const HEAP_TYPES[] = {"List", "Array", "ByteArray", NULL};
    for (int i = 0; HEAP_TYPES[i]; ++i) {
        if (has_word_match(def_sig, HEAP_TYPES[i]) || (def_body && has_word_match(def_body, HEAP_TYPES[i]))) {
            fprintf(stderr, "flowc: [Lean-RT Zero-GC Gate] rejected '%s' in '%s': dynamic heap allocation / closure / recursion violates hard real-time zero-GC invariants; use fixed-size 'structure' instead.\n",
                    HEAP_TYPES[i], fn_name);
            return 0;
        }
    }
    if (def_body) {
        if (strstr(def_body, "++") != NULL) {
            fprintf(stderr, "flowc: [Lean-RT Zero-GC Gate] rejected '++' in '%s': dynamic heap allocation / closure / recursion violates hard real-time zero-GC invariants; use fixed-size 'structure' instead.\n", fn_name);
            return 0;
        }
        if ((has_word_match(def_body, "fun") || strstr(def_body, "\xce\xbb") != NULL) && strstr(def_body, "=>") != NULL) {
            fprintf(stderr, "flowc: [Lean-RT Zero-GC Gate] rejected 'fun =>' in '%s': dynamic heap allocation / closure / recursion violates hard real-time zero-GC invariants; use fixed-size 'structure' instead.\n", fn_name);
            return 0;
        }
        if (has_word_match(def_body, fn_name)) {
            fprintf(stderr, "flowc: [Lean-RT Zero-GC Gate] rejected 'self-recursion' in '%s': dynamic heap allocation / closure / recursion violates hard real-time zero-GC invariants; use fixed-size 'structure' instead.\n", fn_name);
            return 0;
        }
    }
    return 1;
}
static void subst_param_prefix(const char *expr, const char *param, const char *arg, char *out, size_t out_max) {
    size_t plen = strlen(param), alen = strlen(arg), j = 0;
    for (size_t i = 0; expr[i] && j + 4 < out_max; ) {
        int l_ok = (i == 0) || (!isalnum((unsigned char)expr[i - 1]) && expr[i - 1] != '_' && expr[i - 1] != '.');
        if (l_ok && !strncmp(expr + i, param, plen) && expr[i + plen] == '.') {
            if (j + alen + 1 < out_max) {
                memcpy(out + j, arg, alen); j += alen; out[j++] = '.';
                i += plen + 1;
                continue;
            }
        }
        out[j++] = expr[i++];
    }
    out[j] = '\0';
}
static void rewrite_struct_projections(const FlowSpec *spec, char *expr, size_t max_len) {
    if (!spec || !expr || !expr[0]) return;
    for (int iter = 0; iter < 16; ++iter) {
        int changed = 0;
        for (size_t fi = 0; fi < spec->lean_function_count && !changed; ++fi) {
            const FlowLeanFunction *fn = &spec->lean_functions[fi];
            if (!fn->is_struct_transform || fn->param_count == 0) continue;
            size_t fn_len = strlen(fn->name);
            for (char *p = strstr(expr, fn->name); p; p = strstr(p + 1, fn->name)) {
                int l_ok = (p == expr) || (!isalnum((unsigned char)p[-1]) && p[-1] != '_' && p[-1] != '.');
                int r_ok = !isalnum((unsigned char)p[fn_len]) && p[fn_len] != '_' && p[fn_len] != '\'';
                if (!l_ok || !r_ok) continue;
                const char *call_start = p;
                while (call_start > expr && isspace((unsigned char)call_start[-1])) --call_start;
                int has_outer_lp = (call_start > expr && call_start[-1] == '(');
                if (has_outer_lp) --call_start;
                const char *arg_s = p + fn_len, *rp = NULL;
                while (*arg_s && isspace((unsigned char)*arg_s)) ++arg_s;
                if (has_outer_lp) {
                    int pd = 1;
                    for (const char *q = call_start + 1; *q; ++q) {
                        if (*q == '(') pd++;
                        else if (*q == ')' && --pd == 0) { rp = q; break; }
                    }
                } else if (*arg_s == '(') {
                    call_start = p;
                    int pd = 1;
                    for (const char *q = arg_s + 1; *q; ++q) {
                        if (*q == '(') pd++;
                        else if (*q == ')' && --pd == 0) { rp = q; break; }
                    }
                    ++arg_s;
                }
                if (!rp || rp <= arg_s || rp[1] != '.' || !(isalpha((unsigned char)rp[2]) || rp[2] == '_')) continue;
                const char *arg_e = rp;
                while (arg_e > arg_s && isspace((unsigned char)arg_e[-1])) --arg_e;
                char arg_str[FLOW_LINE] = {0};
                size_t al = (size_t)(arg_e - arg_s);
                if (al == 0 || al + 3 >= sizeof(arg_str)) continue;
                if (memchr(arg_s, ' ', al) && !(arg_s[0] == '(' && arg_e[-1] == ')'))
                    snprintf(arg_str, sizeof(arg_str), "(%.*s)", (int)al, arg_s);
                else {
                    memcpy(arg_str, arg_s, al); arg_str[al] = '\0';
                }
                const char *fs = rp + 2, *fe = fs;
                while (isalnum((unsigned char)*fe) || *fe == '_') ++fe;
                char fname[FLOW_NAME] = {0};
                size_t fl = (size_t)(fe - fs);
                if (fl == 0 || fl >= sizeof(fname)) continue;
                memcpy(fname, fs, fl); fname[fl] = '\0';
                const char *fexpr = NULL;
                for (size_t k = 0; k < fn->field_assign_count; ++k)
                    if (!strcmp(fn->field_assign_names[k], fname)) { fexpr = fn->field_assign_exprs[k]; break; }
                if (!fexpr) continue;
                char sub_expr[FLOW_LINE] = {0}, next_buf[FLOW_LEAN_BODY_MAX] = {0};
                subst_param_prefix(fexpr, fn->param_names[0], arg_str, sub_expr, sizeof(sub_expr));
                size_t pre_l = (size_t)(call_start - expr);
                snprintf(next_buf, sizeof(next_buf), "%.*s(%s)%s", (int)pre_l, expr, sub_expr, fe);
                strncpy(expr, next_buf, max_len - 1);
                expr[max_len - 1] = '\0';
                normalize_lean_body(expr, next_buf, sizeof(next_buf));
                strncpy(expr, next_buf, max_len - 1);
                expr[max_len - 1] = '\0';
                changed = 1;
                break;
            }
        }
        if (!changed) break;
    }
}
typedef struct {
    FlowSpec tmp_spec;
    LeanEvalEnv env;
    char thm_lines[FLOW_LEAN_THEOREM_MAX][FLOW_LEAN_BODY_MAX];
} FlowVerifyScratch;

int flow_verify_lean_theorems(const char *lean_source, char *err_msg, size_t err_size) {
    if (err_msg && err_size) err_msg[0] = '\0';
    if (!lean_source || !lean_source[0]) return 0;
    size_t slen = strlen(lean_source);
    char *clean = (char *)calloc(slen + 1, 1), *nostr = (char *)calloc(slen + 1, 1);
    if (!clean || !nostr) { free(clean); free(nostr); return 0; }
    if (!strip_lean_comments(lean_source, clean, nostr, slen + 1, err_msg, err_size)) {
        free(clean); free(nostr); return 0;
    }
    FlowVerifyScratch *scratch = (FlowVerifyScratch *)calloc(1, sizeof(FlowVerifyScratch));
    if (!scratch) { free(clean); free(nostr); return 0; }
    scratch->env.spec = &scratch->tmp_spec;
    char def_sig[FLOW_LEAN_BODY_MAX] = {0}, def_body[FLOW_LEAN_BODY_MAX] = {0}; int in_def = 0;
    size_t thm_cnt = 0; int in_thm = 0;
    FlowLeanStruct *cur_st = NULL; FlowLeanEnum *cur_en = NULL;
    char *saveptr = NULL;
    for (char *raw_line = strtok_r(clean, "\n", &saveptr); raw_line; raw_line = strtok_r(NULL, "\n", &saveptr)) {
        char *line = strip_lean_decl_modifiers(raw_line); if (!*line) continue;
        if (in_def && is_lean_top_decl(line)) {
            if (in_def == 2) { try_add_lean_function(&scratch->tmp_spec, def_sig, def_body); try_eval_lean_const_def(&scratch->env, def_sig, def_body); }
            in_def = 0; def_sig[0] = '\0'; def_body[0] = '\0';
        }
        if (in_thm && is_lean_top_decl(line)) in_thm = 0;
        if (cur_st && is_lean_top_decl(line)) cur_st = NULL;
        if (cur_en && is_lean_top_decl(line)) cur_en = NULL;
        if (cur_st) {
            char fn[FLOW_NAME] = {0}, ft[FLOW_LINE] = {0};
            if (sscanf(line, "%63s : %511[^\r\n]", fn, ft) == 2) {
                char *ce_f = strstr(ft, ":="); if (ce_f) *ce_f = '\0';
                char *tft = trim(ft);
                if (is_lean_erased_proof_field(tft)) continue;
                char ft_tok[FLOW_NAME] = {0};
                sscanf(tft, "%63s", ft_tok);
                add_lean_struct_field(&scratch->tmp_spec, cur_st, fn, normalize_lean_type_token(ft_tok), 0, NULL);
                continue;
            }
        }
        if (cur_en && line[0] == '|') {
            (void)parse_lean_inductive_variants(cur_en, &scratch->env, line, 0);
            continue;
        }
        char sym_name[FLOW_NAME] = {0};
        if (sscanf(line, "structure %63s where", sym_name) == 1 && !is_builtin_lean_struct(sym_name)) {
            if (scratch->tmp_spec.lean_struct_count < FLOW_LEAN_STRUCT_MAX) {
                cur_st = &scratch->tmp_spec.lean_structs[scratch->tmp_spec.lean_struct_count++];
                memset(cur_st, 0, sizeof(*cur_st));
                strncpy(cur_st->name, sym_name, FLOW_NAME - 1);
                cur_st->align_bytes = 8;
            }
            continue;
        }
        if (sscanf(line, "inductive %63s", sym_name) == 1) {
            if (scratch->tmp_spec.lean_enum_count < FLOW_LEAN_ENUM_MAX) {
                cur_en = &scratch->tmp_spec.lean_enums[scratch->tmp_spec.lean_enum_count++];
                memset(cur_en, 0, sizeof(*cur_en));
                strncpy(cur_en->name, sym_name, FLOW_NAME - 1);
                (void)parse_lean_inductive_variants(cur_en, &scratch->env, line, 0);
            }
            continue;
        }
        if (in_def == 1) {
            size_t sl = strlen(def_sig);
            if (sl + 1 + strlen(line) < sizeof(def_sig)) { def_sig[sl] = ' '; strncpy(def_sig + sl + 1, line, sizeof(def_sig) - sl - 2); }
            char *ce = strstr(def_sig, ":=");
            if (ce) { *ce = '\0'; strncpy(def_body, trim(ce + 2), sizeof(def_body) - 1); in_def = 2; }
        } else if (in_def == 2) {
            size_t bl = strlen(def_body);
            if (bl + 1 + strlen(line) < sizeof(def_body)) { if (bl > 0) def_body[bl++] = '\n'; strncpy(def_body + bl, line, sizeof(def_body) - bl - 1); }
        } else if (in_thm && thm_cnt > 0) {
            size_t tl = strlen(scratch->thm_lines[thm_cnt - 1]);
            if (tl + 1 + strlen(line) < sizeof(scratch->thm_lines[0])) { scratch->thm_lines[thm_cnt - 1][tl] = ' '; strncpy(scratch->thm_lines[thm_cnt - 1] + tl + 1, line, sizeof(scratch->thm_lines[0]) - tl - 2); }
        }
        if (starts_with(line, "def ") && !strstr(line, ": Prop") && !strstr(line, ": Type")) {
            char *ce = strstr(line, ":=");
            char *lp = strchr(line, '(');
            if (ce && (!lp || lp > ce) && scratch->env.const_count < 60) {
                char cname[FLOW_NAME] = {0}, ctype[32] = {0};
                if (sscanf(line + 4, "%63[^ :\t] : %31[^ :=\t]", cname, ctype) >= 1 && cname[0]) {
                    char *rhs = trim(ce + 2);
                    if (rhs[0] == '{') {
                        const char *p = rhs + 1;
                        while (p && *p && scratch->env.const_count < 60) {
                            char fname[FLOW_NAME] = {0}; unsigned long long fval = 0;
                            const char * next_ce = strstr(p, ":=");
                            if (!next_ce) break;
                            const char *fs = next_ce - 1;
                            while (fs > p && isspace((unsigned char)*fs)) --fs;
                            const char *fe = fs + 1;
                            while (fs > p && (isalnum((unsigned char)fs[-1]) || fs[-1] == '_')) --fs;
                            size_t fl = (size_t)(fe - fs);
                            if (fl > 0 && fl < sizeof(fname)) {
                                memcpy(fname, fs, fl); fname[fl] = '\0';
                                const char *vp = next_ce + 2;
                                while (*vp && isspace((unsigned char)*vp)) ++vp;
                                char *end_d = NULL; double fdval = strtod(vp, &end_d);
                                int is_f_lit = 0;
                                if (end_d && end_d > vp) {
                                    for (const char *scan = vp; scan < end_d; ++scan)
                                        if (*scan == '.' || *scan == 'e' || *scan == 'E') { is_f_lit = 1; break; }
                                }
                                if (is_f_lit) {
                                    snprintf(scratch->env.const_names[scratch->env.const_count], FLOW_NAME, "%s.%s", cname, fname);
                                    scratch->env.const_fvals[scratch->env.const_count] = fdval;
                                    scratch->env.const_vals[scratch->env.const_count] = (int64_t)fdval;
                                    scratch->env.const_is_nat[scratch->env.const_count] = 0;
                                    scratch->env.const_is_float[scratch->env.const_count] = 1;
                                    scratch->env.const_count++;
                                } else if (sscanf(vp, "%llu", &fval) == 1) {
                                    snprintf(scratch->env.const_names[scratch->env.const_count], FLOW_NAME, "%s.%s", cname, fname);
                                    scratch->env.const_vals[scratch->env.const_count] = (int64_t)fval;
                                    scratch->env.const_fvals[scratch->env.const_count] = (double)fval;
                                    scratch->env.const_is_nat[scratch->env.const_count] = 1;
                                    scratch->env.const_is_float[scratch->env.const_count] = 0;
                                    scratch->env.const_count++;
                                }
                            }
                            p = next_ce + 2;
                        }
                    } else if (rhs[0] != '\0') {
                        char norm_rhs[FLOW_LEAN_BODY_MAX]; normalize_lean_body(rhs, norm_rhs, sizeof(norm_rhs));
                        const char *norm_ct = normalize_lean_type_token(ctype);
                        int is_nat = !strcmp(norm_ct, "Nat");
                        int is_flt = !strcmp(norm_ct, "Float") || !strcmp(norm_ct, "Float32") || slice_has_float_semantics(norm_rhs, strlen(norm_rhs), &scratch->env);
                        double cfval = 0.0; int64_t cval = 0;
                        if (is_flt && lean_eval_float_slice(norm_rhs, strlen(norm_rhs), &scratch->env, 0, &cfval)) {
                            strncpy(scratch->env.const_names[scratch->env.const_count], cname, FLOW_NAME - 1);
                            scratch->env.const_fvals[scratch->env.const_count] = cfval;
                            scratch->env.const_vals[scratch->env.const_count] = (int64_t)cfval;
                            scratch->env.const_is_nat[scratch->env.const_count] = 0;
                            scratch->env.const_is_float[scratch->env.const_count] = 1;
                            scratch->env.const_count++;
                        } else if (lean_eval_slice(norm_rhs, strlen(norm_rhs), &scratch->env, is_nat, 0, &cval)) {
                            strncpy(scratch->env.const_names[scratch->env.const_count], cname, FLOW_NAME - 1);
                            scratch->env.const_vals[scratch->env.const_count] = cval;
                            scratch->env.const_fvals[scratch->env.const_count] = (double)cval;
                            scratch->env.const_is_nat[scratch->env.const_count] = is_nat;
                            scratch->env.const_is_float[scratch->env.const_count] = 0;
                            scratch->env.const_count++;
                        }
                    }
                }
            }
            if (ce) {
                size_t slen_d = (size_t)(ce - line);
                if (slen_d >= sizeof(def_sig)) slen_d = sizeof(def_sig) - 1;
                memcpy(def_sig, line, slen_d); def_sig[slen_d] = '\0';
                strncpy(def_body, trim(ce + 2), sizeof(def_body) - 1);
                in_def = 2;
            } else {
                strncpy(def_sig, line, sizeof(def_sig) - 1); def_body[0] = '\0'; in_def = 1;
            }
        } else if (starts_with(line, "theorem ") && thm_cnt < FLOW_LEAN_THEOREM_MAX) {
            strncpy(scratch->thm_lines[thm_cnt++], line, sizeof(scratch->thm_lines[0]) - 1);
            scratch->thm_lines[thm_cnt - 1][sizeof(scratch->thm_lines[0]) - 1] = '\0';
            in_thm = 1;
        }
    }
    if (in_def == 2) { try_add_lean_function(&scratch->tmp_spec, def_sig, def_body); try_eval_lean_const_def(&scratch->env, def_sig, def_body); }
    for (size_t ti = 0; ti < thm_cnt; ++ti) {
        char *tline = scratch->thm_lines[ti];
        char tname[FLOW_NAME] = {0};
        if (sscanf(tline + 8, "%63[^ (:{\t]", tname) != 1) continue;
        char *ce = strstr(tline, ":=");
        if (!ce) continue;
        char *proof = trim(ce + 2);
        int is_decide_or_rfl = (!strncmp(proof, "by decide", 9) || !strncmp(proof, "by rfl", 6) || !strcmp(proof, "rfl"));
        int is_omega = (!strncmp(proof, "by omega", 8));
        if (!is_decide_or_rfl && !is_omega) continue;
        *ce = '\0';
        int pd = 0; char *prop_colon = NULL;
        for (char *p = tline + 8; *p; ++p) {
            if (*p == '(' || *p == '{') pd++;
            else if (*p == ')' || *p == '}') pd--;
            else if (pd == 0 && *p == ':') { prop_colon = p; break; }
        }
        if (!prop_colon) continue;
        LeanEvalEnv thm_env = scratch->env;
        char hyp_props[16][256]; size_t hyp_cnt = 0;
        for (char *p = tline + 8; p < prop_colon; ) {
            char *lp = strchr(p, '('); if (!lp || lp >= prop_colon) break;
            char *rp = lp + 1;
            for (int d = 1; *rp && rp < prop_colon && d > 0; ++rp) {
                if (*rp == '(') d++;
                else if (*rp == ')') { d--; if (d == 0) break; }
            }
            if (!*rp || rp >= prop_colon || *rp != ')') break;
            char grp[256] = {0}; size_t gl = (size_t)(rp - (lp + 1));
            if (gl < sizeof(grp)) {
                memcpy(grp, lp + 1, gl);
                char *col = strchr(grp, ':');
                if (col) {
                    *col = '\0';
                    char *raw_ptype = trim(col + 1);
                    const char *ptype = normalize_lean_type_token(raw_ptype);
                    const FlowLeanStruct *pst = find_lean_struct(&scratch->tmp_spec, ptype);
                    if (pst) {
                        for (char *tok = strtok(grp, " \t"); tok; tok = strtok(NULL, " \t")) {
                            for (size_t fi = 0; fi < pst->field_count && thm_env.var_count < 64; ++fi) {
                                snprintf(thm_env.var_names[thm_env.var_count], FLOW_NAME, "%s.%s", tok, pst->fields[fi].name);
                                thm_env.var_vals[thm_env.var_count] = 1;
                                thm_env.var_fvals[thm_env.var_count] = 1.0;
                                thm_env.var_is_nat[thm_env.var_count] = !strcmp(pst->fields[fi].lean_type, "Nat") || !strncmp(pst->fields[fi].lean_type, "UInt", 4);
                                thm_env.var_is_float[thm_env.var_count] = !strcmp(pst->fields[fi].lean_type, "Float") || !strcmp(pst->fields[fi].lean_type, "Float32");
                                thm_env.var_count++;
                            }
                        }
                    } else if (is_scalar_lean_type(ptype) || find_lean_enum(&scratch->tmp_spec, ptype)) {
                        int v_nat = !strcmp(ptype, "Nat");
                        int v_flt = !strcmp(ptype, "Float") || !strcmp(ptype, "Float32");
                        for (char *tok = strtok(grp, " \t"); tok && thm_env.var_count < 64; tok = strtok(NULL, " \t")) {
                            strncpy(thm_env.var_names[thm_env.var_count], tok, FLOW_NAME - 1);
                            thm_env.var_vals[thm_env.var_count] = 1;
                            thm_env.var_fvals[thm_env.var_count] = 1.0;
                            thm_env.var_is_nat[thm_env.var_count] = v_nat;
                            thm_env.var_is_float[thm_env.var_count] = v_flt;
                            thm_env.var_count++;
                        }
                    } else if (hyp_cnt < 16) {
                        normalize_lean_body(raw_ptype, hyp_props[hyp_cnt], sizeof(hyp_props[0]));
                        rewrite_struct_projections(&scratch->tmp_spec, hyp_props[hyp_cnt], sizeof(hyp_props[0]));
                        inline_lean_scalar_calls(&scratch->tmp_spec, hyp_props[hyp_cnt], sizeof(hyp_props[0]));
                        hyp_cnt++;
                    }
                }
            }
            p = rp + 1;
        }
        char norm_prop[FLOW_LEAN_BODY_MAX];
        normalize_lean_body(prop_colon + 1, norm_prop, sizeof(norm_prop));
        rewrite_struct_projections(&scratch->tmp_spec, norm_prop, sizeof(norm_prop));
        inline_lean_scalar_calls(&scratch->tmp_spec, norm_prop, sizeof(norm_prop));
        static const int64_t TEST_VECS[4][4] = {{1, 1, 1, 1}, {0, 0, 0, 0}, {2, 1, 3, 2}, {7, 3, 5, 1}};
        size_t n_trials = (thm_env.var_count > 0) ? 4 : 1;
        for (size_t tr = 0; tr < n_trials; ++tr) {
            for (size_t vi = 0; vi < thm_env.var_count; ++vi) {
                thm_env.var_vals[vi] = TEST_VECS[tr][vi & 3];
                thm_env.var_fvals[vi] = (double)TEST_VECS[tr][vi & 3];
            }
            int hyp_ok = 1;
            for (size_t hi = 0; hi < hyp_cnt; ++hi) {
                int64_t hval = 0;
                if (!lean_eval_slice(hyp_props[hi], strlen(hyp_props[hi]), &thm_env, 1, 0, &hval) || hval == 0) {
                    hyp_ok = 0; break;
                }
            }
            if (!hyp_ok) continue;
            int64_t val = 0;
            if (lean_eval_slice(norm_prop, strlen(norm_prop), &thm_env, 1, 0, &val)) {
                if (val == 0) {
                    if (err_msg && err_size) snprintf(err_msg, err_size, "Lean theorem '%s' evaluated to FALSE (counterexample refuted proposition)", tname);
                    free(scratch); free(clean); free(nostr); return 0;
                }
            }
        }
    }
    free(scratch); free(clean); free(nostr);
    return 1;
}
typedef struct {
    char fns[FLOW_LEAN_FN_MAX][FLOW_NAME];
    char bodies[FLOW_LEAN_FN_MAX][FLOW_LINE];
    int is_u32[FLOW_LEAN_FN_MAX];
    int in_thm[FLOW_LEAN_FN_MAX];
} LeanParseCands;

static int parse_lean_spec(FILE *input, FlowSpec *spec) {
    fseek(input, 0, SEEK_END); long fsz = ftell(input); rewind(input);
    if (fsz <= 0 || fsz > 4 * 1024 * 1024) return 0;
    char *full_src = (char *)calloc((size_t)fsz + 1, 1);
    if (!full_src) return 0;
    size_t nread = fread(full_src, 1, (size_t)fsz, input);
    full_src[nread] = '\0';
    char *clean_src = (char *)calloc(nread + 1, 1), *nostr_src = (char *)calloc(nread + 1, 1);
    if (!clean_src || !nostr_src) { free(full_src); free(clean_src); free(nostr_src); return 0; }
    char err_msg[256] = {0};
    if (!strip_lean_comments(full_src, clean_src, nostr_src, nread + 1, err_msg, sizeof(err_msg))) {
        if (err_msg[0]) fprintf(stderr, "flowc: %s\n", err_msg);
        free(full_src); free(clean_src); free(nostr_src); return 0;
    }
    if (!flow_verify_lean_theorems(full_src, err_msg, sizeof(err_msg))) {
        if (err_msg[0]) fprintf(stderr, "flowc: %s\n", err_msg);
        free(full_src); free(clean_src); free(nostr_src); return 0;
    }
    free(full_src); free(nostr_src);
    LeanParseCands *cands = (LeanParseCands *)calloc(1, sizeof(LeanParseCands));
    if (!cands) { free(clean_src); return 0; }
    char sym[FLOW_NAME] = {0}, cand_fn[FLOW_NAME] = {0}, cand_body[FLOW_LINE] = {0};
    size_t cand_count = 0;
    char def_sig[FLOW_LEAN_BODY_MAX] = {0}, def_body[FLOW_LEAN_BODY_MAX] = {0};
    unsigned long long v = 0; int saw_lean = 0, in_struct = 0, explicit_stride = 0, explicit_export = 0, awaiting_body = 0, in_u32_body = 0, in_def = 0, active_thm = -1, is_ext_lib = 0; size_t struct_bytes = 0;
    FlowLeanStruct *cur_st = NULL; FlowLeanEnum *cur_en = NULL;
    memset(spec, 0, sizeof(*spec)); strncpy(spec->input_name, "lean_stream", FLOW_NAME - 1); strncpy(spec->flow_name, "lean_kernel_flow", FLOW_NAME - 1);
    spec->max_count = 1024; spec->input_stride_bytes = 8; spec->input_align_bytes = 8; spec->state_schema_version = 1; spec->top_n = 3;
    spec->deterministic = 1; spec->lean_certified = 1; spec->bounded = 1; spec->memory_mb = 64;
    char *saveptr = NULL;
    for (char *raw_line = strtok_r(clean_src, "\n", &saveptr); raw_line; raw_line = strtok_r(NULL, "\n", &saveptr)) {
        char *line = trim(raw_line); if (!*line) continue;
        if (!strcmp(line, "module") || starts_with(line, "module ") || starts_with(line, "import Cedar")) is_ext_lib = 1;
        if (starts_with(line, "noncomputable ")) saw_lean = 1;
        line = strip_lean_decl_modifiers(line); if (!*line) continue;
        if (!strcmp(line, "section") || starts_with(line, "section ")) { saw_lean = 1; continue; }
        if (in_def && is_lean_top_decl(line)) {
            if (in_def == 2) {
                if (!is_ext_lib && !check_lean_rt_def_gate(def_sig, def_body)) { free(cands); free(clean_src); return 0; }
                try_add_lean_function(spec, def_sig, def_body);
            }
            in_def = 0; def_sig[0] = '\0'; def_body[0] = '\0';
        }
        if (active_thm >= 0 && is_lean_top_decl(line)) active_thm = -1;
        if (cur_en && is_lean_top_decl(line)) cur_en = NULL;
        if (in_def == 1) {
            size_t sl = strlen(def_sig);
            if (sl + 1 + strlen(line) < sizeof(def_sig)) { def_sig[sl] = ' '; strncpy(def_sig + sl + 1, line, sizeof(def_sig) - sl - 2); }
            char *ce = strstr(def_sig, ":=");
            if (ce) {
                *ce = '\0'; char *rhs = trim(ce + 2);
                strncpy(def_body, rhs, sizeof(def_body) - 1);
                in_def = 2;
            }
        } else if (in_def == 2) {
            size_t bl = strlen(def_body);
            if (bl + 1 + strlen(line) < sizeof(def_body)) {
                if (bl > 0) def_body[bl++] = '\n';
                strncpy(def_body + bl, line, sizeof(def_body) - bl - 1);
            }
        }
        if (awaiting_body) {
            strncpy(cand_body, line, FLOW_LINE - 1);
            if (cand_count > 0) strncpy(cands->bodies[cand_count - 1], cand_body, FLOW_LINE - 1);
            if (spec->extern_kernel[0]) strncpy(spec->extern_kernel_body, cand_body, FLOW_LINE - 1);
            awaiting_body = 0; continue;
        }
        if (in_u32_body && cand_body[0] && (starts_with(line, "else ") || starts_with(line, "then "))) {
            size_t cur = strlen(cand_body);
            if (cur + 1 + strlen(line) < FLOW_LINE) {
                cand_body[cur] = ' '; strncpy(cand_body + cur + 1, line, FLOW_LINE - cur - 2);
                if (cand_count > 0) strncpy(cands->bodies[cand_count - 1], cand_body, FLOW_LINE - 1);
                if (spec->extern_kernel[0]) strncpy(spec->extern_kernel_body, cand_body, FLOW_LINE - 1);
            }
            continue;
        }
        in_u32_body = 0;
        if (cur_en && line[0] == '|') {
            if (!parse_lean_inductive_variants(cur_en, NULL, line, !is_ext_lib)) { free(cands); free(clean_src); return 0; }
            continue;
        }
        if (in_struct) {
            char fn[FLOW_NAME] = {0}, ft[FLOW_LINE] = {0};
            if (is_lean_top_decl(line)) {
                in_struct = 0; cur_st = NULL;
            } else if (sscanf(line, "%63s : %511[^\r\n]", fn, ft) == 2) {
                char *ce_f = strstr(ft, ":="); if (ce_f) *ce_f = '\0';
                char *tft = trim(ft);
                if (is_lean_erased_proof_field(tft)) continue;
                if (!is_ext_lib && (has_word_match(tft, "List") || has_word_match(tft, "Array") || has_word_match(tft, "ByteArray"))) {
                    fprintf(stderr, "flowc: [Lean-RT Zero-GC Gate] rejected '%s' in '%s.%s': dynamic heap allocation / closure / recursion violates hard real-time zero-GC invariants; use fixed-size 'structure' instead.\n",
                            tft, cur_st ? cur_st->name : "structure", fn);
                    free(cands); free(clean_src); return 0;
                }
                char ft_tok[FLOW_NAME] = {0};
                sscanf(tft, "%63s", ft_tok);
                add_lean_struct_field(spec, cur_st, fn, normalize_lean_type_token(ft_tok), 1, &struct_bytes);
                continue;
            }
        }
        if (sscanf(line, "structure %63s where", sym) == 1) {
            saw_lean = 1; in_struct = !is_builtin_lean_struct(sym);
            cur_st = NULL;
            if (in_struct && spec->lean_struct_count < FLOW_LEAN_STRUCT_MAX) {
                cur_st = &spec->lean_structs[spec->lean_struct_count++];
                memset(cur_st, 0, sizeof(*cur_st));
                strncpy(cur_st->name, sym, FLOW_NAME - 1);
                cur_st->align_bytes = 8;
            }
        } else if (sscanf(line, "inductive %63s", sym) == 1) {
            saw_lean = 1;
            if (spec->lean_enum_count < FLOW_LEAN_ENUM_MAX) {
                cur_en = &spec->lean_enums[spec->lean_enum_count++];
                memset(cur_en, 0, sizeof(*cur_en));
                strncpy(cur_en->name, sym, FLOW_NAME - 1);
                if (!parse_lean_inductive_variants(cur_en, NULL, line, !is_ext_lib)) { free(cands); free(clean_src); return 0; }
            }
        } else if (sscanf(line, "namespace %63s", sym) == 1) {
            const char *leaf = strrchr(sym, '.');
            const char *ns_tail = leaf ? leaf + 1 : sym;
            if (!strcmp(ns_tail, "Spec") && spec->flow_name[0] && strcmp(spec->flow_name, "lean_kernel_flow") != 0) continue;
            if (!strcmp(sym, "Spec") || !strcmp(sym, "Exec") || !strcmp(ns_tail, "Thm") || !strcmp(ns_tail, "Proofs")) continue;
            saw_lean = 1;
            if (!strncmp(sym, "Embodied.", 9) || !strncmp(sym, "Flow.Embodied.", 14)) {
                strncpy(spec->domain_name, "embodied", FLOW_NAME - 1);
                strncpy(spec->plugin_name, "flow.embodied", FLOW_NAME - 1);
                if (spec->imported_module_count == 0) strncpy(spec->imported_modules[spec->imported_module_count++], "flow.embodied", FLOW_NAME - 1);
            }
            strncpy(spec->flow_name, ns_tail, FLOW_NAME - 1);
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
        } else if (starts_with(line, "def ") && !strstr(line, ": Prop") && !strstr(line, ": Type")) {
            char fn[FLOW_NAME] = {0};
            if (sscanf(line + 4, "%63[^ ({:\t]", fn) == 1 && !is_internal_lean_def(fn)) {
                saw_lean = 1;
                char *ce = strstr(line, ":=");
                if (ce) {
                    size_t slen_d = (size_t)(ce - line);
                    if (slen_d >= sizeof(def_sig)) slen_d = sizeof(def_sig) - 1;
                    memcpy(def_sig, line, slen_d); def_sig[slen_d] = '\0';
                    strncpy(def_body, trim(ce + 2), sizeof(def_body) - 1);
                    in_def = 2;
                } else {
                    strncpy(def_sig, line, sizeof(def_sig) - 1);
                    def_body[0] = '\0';
                    in_def = 1;
                }
                char *last_rp = strrchr(def_sig, ')');
                char *ret_col = last_rp ? strchr(last_rp + 1, ':') : NULL;
                if (last_rp && ret_col) {
                    int is_u32 = strstr(ret_col, "UInt32") != NULL;
                    strncpy(cand_fn, fn, FLOW_NAME - 1);
                    if (is_u32) {
                        if (ce) {
                            char *rhs = trim(ce + 2);
                            in_u32_body = 1;
                            if (*rhs) {
                                strncpy(cand_body, rhs, FLOW_LINE - 1);
                                if (spec->extern_kernel[0] && !strcmp(spec->extern_kernel, fn)) strncpy(spec->extern_kernel_body, cand_body, FLOW_LINE - 1);
                            } else awaiting_body = 1;
                        }
                    } else {
                        strncpy(cand_body, "if stride > 0 && epoch > 0 && baseOff + byteLen >= baseOff then 0 else 1", FLOW_LINE - 1);
                    }
                    if (cand_count < FLOW_LEAN_FN_MAX) {
                        strncpy(cands->fns[cand_count], cand_fn, FLOW_NAME - 1);
                        strncpy(cands->bodies[cand_count], cand_body, FLOW_LINE - 1);
                        cands->is_u32[cand_count] = is_u32;
                        cand_count++;
                    }
                }
            }
        } else if (starts_with(line, "theorem ")) {
            saw_lean = 1;
            if (sscanf(line + 8, "%63[^ (:{\t]", sym) == 1) {
                if (!strcmp(sym, "zmp_torque_safe")) {
                    strncpy(spec->contract_name, "zmp_torque_safe", FLOW_NAME - 1);
                    if (!spec->domain_name[0]) {
                        strncpy(spec->domain_name, "embodied", FLOW_NAME - 1);
                        strncpy(spec->plugin_name, "flow.embodied", FLOW_NAME - 1);
                        if (spec->imported_module_count == 0) strncpy(spec->imported_modules[spec->imported_module_count++], "flow.embodied", FLOW_NAME - 1);
                    }
                }
                if (spec->lean_theorem_count < FLOW_LEAN_THEOREM_MAX) {
                    active_thm = (int)spec->lean_theorem_count++;
                    strncpy(spec->lean_theorems[active_thm].name, sym, FLOW_NAME - 1);
                    spec->lean_theorems[active_thm].anchored_fn[0] = '\0';
                }
            }
            if (active_thm >= 0 && !spec->lean_theorems[active_thm].anchored_fn[0]) {
                for (size_t fi = spec->lean_function_count; fi > 0; --fi) {
                    if (has_word_match(line, spec->lean_functions[fi - 1].name)) {
                        strncpy(spec->lean_theorems[active_thm].anchored_fn, spec->lean_functions[fi - 1].name, FLOW_NAME - 1);
                        break;
                    }
                }
            }
            for (size_t ci = 0; ci < cand_count; ++ci) {
                if (has_word_match(line, cands->fns[ci])) cands->in_thm[ci] = 1;
            }
        } else {
            if (active_thm >= 0 && !spec->lean_theorems[active_thm].anchored_fn[0]) {
                for (size_t fi = spec->lean_function_count; fi > 0; --fi) {
                    if (has_word_match(line, spec->lean_functions[fi - 1].name)) {
                        strncpy(spec->lean_theorems[active_thm].anchored_fn, spec->lean_functions[fi - 1].name, FLOW_NAME - 1);
                        break;
                    }
                }
            }
            for (size_t ci = 0; ci < cand_count; ++ci) {
                if (has_word_match(line, cands->fns[ci])) cands->in_thm[ci] = 1;
            }
        }
    }
    free(clean_src);
    if (in_def == 2) {
        if (!is_ext_lib && !check_lean_rt_def_gate(def_sig, def_body)) { free(cands); return 0; }
        try_add_lean_function(spec, def_sig, def_body);
    }
    if (!explicit_stride && struct_bytes > 0) spec->input_stride_bytes = (struct_bytes + 7u) & ~7u;
    if (explicit_export && spec->extern_kernel[0]) {
        for (size_t ci = 0; ci < cand_count; ++ci) {
            if (!strcmp(cands->fns[ci], spec->extern_kernel)) {
                strncpy(spec->extern_kernel_body, cands->bodies[ci], FLOW_LINE - 1);
                break;
            }
        }
    } else if (cand_count > 0) {
        ssize_t picked = -1;
        for (size_t ci = cand_count; ci > 0; --ci) if (cands->is_u32[ci - 1] && cands->in_thm[ci - 1]) { picked = (ssize_t)(ci - 1); break; }
        if (picked < 0) for (size_t ci = cand_count; ci > 0; --ci) if (cands->is_u32[ci - 1]) { picked = (ssize_t)(ci - 1); break; }
        if (picked < 0) for (size_t ci = cand_count; ci > 0; --ci) if (cands->in_thm[ci - 1]) { picked = (ssize_t)(ci - 1); break; }
        if (picked < 0) picked = (ssize_t)(cand_count - 1);
        strncpy(spec->extern_kernel, cands->fns[picked], FLOW_NAME - 1);
        strncpy(spec->extern_kernel_body, cands->bodies[picked], FLOW_LINE - 1);
    }
    free(cands);
    if (!strcmp(spec->domain_name, "embodied")) {
        snprintf(spec->flow_expression, sizeof(spec->flow_expression), "lean_stream -> flow_embodied_run -> top(3)");
    } else {
        snprintf(spec->flow_expression, sizeof(spec->flow_expression), "lean_stream -> %s%s%s -> top(3)",
                 spec->extern_kernel[0] ? "extern(" : "sort", spec->extern_kernel[0] ? spec->extern_kernel : "", spec->extern_kernel[0] ? ")" : "");
    }
    parse_flow_nodes(spec, spec->flow_expression); return saw_lean;
}
static FILE *open_bound_lean_file(FILE *flow_input, const char *bpath) {
    if (!bpath || !bpath[0]) return NULL;
    FILE *lfp = fopen(bpath, "r");
    if (lfp) return lfp;
    if (flow_input) {
        int fd = fileno(flow_input);
        char in_path[1024] = {0};
#if defined(__APPLE__) && defined(F_GETPATH)
        if (fd >= 0 && fcntl(fd, F_GETPATH, in_path) == -1) in_path[0] = '\0';
#elif defined(__linux__)
        if (fd >= 0) {
            char fd_link[64]; snprintf(fd_link, sizeof(fd_link), "/proc/self/fd/%d", fd);
            ssize_t r = readlink(fd_link, in_path, sizeof(in_path) - 1);
            if (r > 0) in_path[r] = '\0'; else in_path[0] = '\0';
        }
#endif
        char *slash = strrchr(in_path, '/');
        if (slash) {
            *slash = '\0';
            char cand[1536];
            snprintf(cand, sizeof(cand), "%s/%s", in_path, bpath);
            if ((lfp = fopen(cand, "r")) != NULL) return lfp;
            const char *base = strrchr(bpath, '/');
            if (base && base[1]) {
                snprintf(cand, sizeof(cand), "%s/%s", in_path, base + 1);
                if ((lfp = fopen(cand, "r")) != NULL) return lfp;
            }
            char *parent_slash = strrchr(in_path, '/');
            if (parent_slash) {
                *parent_slash = '\0';
                snprintf(cand, sizeof(cand), "%s/%s", in_path, bpath);
                if ((lfp = fopen(cand, "r")) != NULL) return lfp;
            }
        }
    }
    if (!strchr(bpath, '/')) {
        char alt[FLOW_LINE];
        snprintf(alt, sizeof(alt), "plugins/%s", bpath);
        if ((lfp = fopen(alt, "r")) != NULL) return lfp;
        snprintf(alt, sizeof(alt), "tests/%s", bpath);
        if ((lfp = fopen(alt, "r")) != NULL) return lfp;
    }
    return NULL;
}
static void merge_bound_lean_into_spec(FlowSpec *dst, const FlowSpec *src) {
    for (size_t i = 0; i < src->lean_struct_count && dst->lean_struct_count < FLOW_LEAN_STRUCT_MAX; ++i)
        if (!find_lean_struct(dst, src->lean_structs[i].name))
            dst->lean_structs[dst->lean_struct_count++] = src->lean_structs[i];
    for (size_t i = 0; i < src->lean_enum_count && dst->lean_enum_count < FLOW_LEAN_ENUM_MAX; ++i)
        if (!find_lean_enum(dst, src->lean_enums[i].name))
            dst->lean_enums[dst->lean_enum_count++] = src->lean_enums[i];
    for (size_t i = 0; i < src->lean_function_count && dst->lean_function_count < FLOW_LEAN_FN_MAX; ++i) {
        int dup = 0;
        for (size_t j = 0; j < dst->lean_function_count; ++j)
            if (!strcmp(dst->lean_functions[j].name, src->lean_functions[i].name)) { dup = 1; break; }
        if (!dup) dst->lean_functions[dst->lean_function_count++] = src->lean_functions[i];
    }
    for (size_t i = 0; i < src->lean_theorem_count && dst->lean_theorem_count < FLOW_LEAN_THEOREM_MAX; ++i) {
        int dup = 0;
        for (size_t j = 0; j < dst->lean_theorem_count; ++j)
            if (!strcmp(dst->lean_theorems[j].name, src->lean_theorems[i].name)) { dup = 1; break; }
        if (!dup) dst->lean_theorems[dst->lean_theorem_count++] = src->lean_theorems[i];
    }
}
static void strip_quotes(char *s) {
    size_t l = strlen(s);
    if (l >= 2 && s[0] == '"' && s[l - 1] == '"') { memmove(s, s + 1, l - 2); s[l - 2] = '\0'; }
}
int parse_spec(FILE *input, FlowSpec *spec) {
    char raw[FLOW_LINE]; Section section = SECTION_NONE; int line_number = 0, parse_error = 0, brace_depth = 0, flow_nodes_from_bind = 0, in_coords_list = 0;
    int is_literate = 0, is_lean_doc = 0, has_flow_kw = 0, in_code_block = 0, flow_block_completed = 0; long start_pos = ftell(input); if (start_pos >= 0) { while (fgets(raw, sizeof(raw), input)) {
            char *p = trim(raw); if (!strncmp(p, "```flow", 7) || !strncmp(p, "~~~flow", 7)) { is_literate = 1; break; }
            if (starts_with(p, "project ") || starts_with(p, "flow ") || starts_with(p, "bind ") || starts_with(p, "input ") || starts_with(p, "@quota") || starts_with(p, "@embodied") || starts_with(p, "@reload") || starts_with(p, "@patchbay") || starts_with(p, "patchbay ") || !strcmp(p, "patchbay {") || !strcmp(p, "require {")) has_flow_kw = 1;
            if (starts_with(p, "namespace ") || starts_with(p, "theorem ") || starts_with(p, "@[export ") || starts_with(p, "inductive ") || (starts_with(p, "structure ") && strstr(p, " where")) || starts_with(p, "def ") || starts_with(p, "noncomputable ") || !strcmp(p, "section") || starts_with(p, "section ")) is_lean_doc = 1; } fseek(input, start_pos, SEEK_SET);
        if (is_lean_doc && !has_flow_kw && !is_literate) return parse_lean_spec(input, spec); }
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
        if (starts_with(line, "bind ")) {
            char bpath[FLOW_LINE] = {0};
            strncpy(bpath, trim(line + 5), sizeof(bpath) - 1);
            strip_quotes(bpath);
            FILE *lfp = open_bound_lean_file(input, bpath);
            FlowSpec *lspec = calloc(1, sizeof(FlowSpec));
            if (!lfp || !lspec || !parse_lean_spec(lfp, lspec)) {
                if (lfp) fclose(lfp);
                free(lspec);
                fprintf(stderr, "flowc: failed to bind Lean kernel '%s' at line %d\n", bpath, line_number);
                parse_error = 1; continue;
            }
            fclose(lfp);
            FlowSpec *prev = calloc(1, sizeof(FlowSpec));
            if (!prev) { free(lspec); parse_error = 1; continue; }
            *prev = *spec;
            *spec = *lspec;
            free(lspec);
            strncpy(spec->kernel_source_lean, bpath, FLOW_LINE - 1);
            if (prev->project_name[0]) strncpy(spec->project_name, prev->project_name, FLOW_NAME - 1);
            if (prev->flow_name[0]) strncpy(spec->flow_name, prev->flow_name, FLOW_NAME - 1);
            if (prev->domain_name[0]) strncpy(spec->domain_name, prev->domain_name, FLOW_NAME - 1);
            if (prev->plugin_name[0]) strncpy(spec->plugin_name, prev->plugin_name, FLOW_NAME - 1);
            if (prev->resource_name[0]) strncpy(spec->resource_name, prev->resource_name, FLOW_NAME - 1);
            if (prev->capability_name[0]) strncpy(spec->capability_name, prev->capability_name, FLOW_NAME - 1);
            if (prev->contract_name[0]) strncpy(spec->contract_name, prev->contract_name, FLOW_NAME - 1);
            if (prev->fallback_policy[0]) strncpy(spec->fallback_policy, prev->fallback_policy, FLOW_NAME - 1);
            if (prev->extern_kernel[0]) strncpy(spec->extern_kernel, prev->extern_kernel, FLOW_NAME - 1);
            if (prev->input_name[0] && strcmp(prev->input_name, "lean_stream") != 0) strncpy(spec->input_name, prev->input_name, FLOW_NAME - 1);
            if (prev->max_count > 0 && prev->max_count != 1024) spec->max_count = prev->max_count;
            if (prev->input_stride_bytes > 0) spec->input_stride_bytes = prev->input_stride_bytes;
            if (prev->input_align_bytes > 0) spec->input_align_bytes = prev->input_align_bytes;
            if (prev->sample_count > 0) { spec->sample_count = prev->sample_count; memcpy(spec->samples, prev->samples, sizeof(spec->samples)); }
            if (prev->output_name[0]) strncpy(spec->output_name, prev->output_name, FLOW_NAME - 1);
            if (prev->output_type[0]) strncpy(spec->output_type, prev->output_type, FLOW_NAME - 1);
            if (prev->top_n > 0) spec->top_n = prev->top_n;
            if (prev->state_name[0]) strncpy(spec->state_name, prev->state_name, FLOW_NAME - 1);
            if (prev->shared) spec->shared = 1;
            if (prev->read_heavy) spec->read_heavy = 1;
            if (prev->bounded) spec->bounded = 1;
            if (prev->deterministic) spec->deterministic = 1;
            if (prev->lean_certified) spec->lean_certified = 1;
            if (prev->prefer_latency) spec->prefer_latency = 1;
            if (prev->ensure_count > 0) spec->ensure_count += prev->ensure_count;
            for (size_t sfi = 0; sfi < prev->state_field_count && spec->state_field_count < FLOW_FIELD_MAX; ++sfi) {
                int dup_sf = 0;
                for (size_t di = 0; di < spec->state_field_count; ++di)
                    if (!strcmp(spec->state_fields[di].name, prev->state_fields[sfi].name)) { dup_sf = 1; break; }
                if (!dup_sf) spec->state_fields[spec->state_field_count++] = prev->state_fields[sfi];
            }
            for (size_t im = 0; im < prev->imported_module_count && spec->imported_module_count < 8; ++im) {
                int dup_im = 0;
                for (size_t dm = 0; dm < spec->imported_module_count; ++dm)
                    if (!strcmp(spec->imported_modules[dm], prev->imported_modules[im])) { dup_im = 1; break; }
                if (!dup_im) strncpy(spec->imported_modules[spec->imported_module_count++], prev->imported_modules[im], FLOW_NAME - 1);
            }
            if (prev->sla_latency_ns > 0) spec->sla_latency_ns = prev->sla_latency_ns;
            if (prev->arena_limit_bytes > 0) spec->arena_limit_bytes = prev->arena_limit_bytes;
            if (prev->memory_mb > 0) spec->memory_mb = prev->memory_mb;
            if (prev->qsbr_timeout_ns > 0) spec->qsbr_timeout_ns = prev->qsbr_timeout_ns;
            if (prev->migrate_fn[0]) strncpy(spec->migrate_fn, prev->migrate_fn, FLOW_NAME - 1);
            if (prev->reload_from_schema[0]) strncpy(spec->reload_from_schema, prev->reload_from_schema, FLOW_NAME - 1);
            if (prev->reload_to_schema[0]) strncpy(spec->reload_to_schema, prev->reload_to_schema, FLOW_NAME - 1);
            if (prev->ingress_driver[0]) strncpy(spec->ingress_driver, prev->ingress_driver, FLOW_NAME - 1);
            if (prev->egress_driver[0]) strncpy(spec->egress_driver, prev->egress_driver, FLOW_NAME - 1);
            if (prev->state_schema_version > 1) spec->state_schema_version = prev->state_schema_version;
            if (prev->jet.enabled) spec->jet = prev->jet;
            if (prev->jet_coord_count > 0) { spec->jet_coord_count = prev->jet_coord_count; memcpy(spec->jet_coord_names, prev->jet_coord_names, sizeof(spec->jet_coord_names)); }
            if (prev->thermal.enabled) spec->thermal = prev->thermal;
            if (prev->constraint_count > 0) { spec->constraint_count = prev->constraint_count; spec->declared_constraint_count = prev->declared_constraint_count; memcpy(spec->constraints, prev->constraints, sizeof(spec->constraints)); }
            if (prev->patchbay_stage_count > 0) { spec->patchbay_stage_count = prev->patchbay_stage_count; memcpy(spec->patchbay_stages, prev->patchbay_stages, sizeof(spec->patchbay_stages)); }
            merge_bound_lean_into_spec(spec, prev);
            if (prev->flow_node_count > 0) {
                strncpy(spec->flow_expression, prev->flow_expression, FLOW_LINE - 1);
                spec->flow_node_count = prev->flow_node_count;
                memcpy(spec->flow_nodes, prev->flow_nodes, sizeof(spec->flow_nodes));
                flow_nodes_from_bind = 0;
            } else {
                flow_nodes_from_bind = 1;
            }
            free(prev);
            continue;
        }
        if (!strcmp(line, "require {") || !strcmp(line, "@quota {") || !strcmp(line, "quota {") || starts_with(line, "@quota ") || starts_with(line, "quota ")) { section = SECTION_REQUIRE; continue; }
        if (!strcmp(line, "prefer {"))  { section = SECTION_PREFER; continue; } if (!strcmp(line, "ensure {"))  { section = SECTION_ENSURE; continue; }
        if (starts_with(line, "@embodied") || starts_with(line, "embodied ") || !strcmp(line, "embodied {") || starts_with(line, "jet ") || !strcmp(line, "jet {")) {
            section = SECTION_JET; spec->jet.enabled = 1; in_coords_list = 0;
            if (starts_with(line, "@embodied") || starts_with(line, "embodied ") || !strcmp(line, "embodied {")) {
                if (!spec->domain_name[0]) strncpy(spec->domain_name, "embodied", FLOW_NAME - 1);
                if (!spec->plugin_name[0]) strncpy(spec->plugin_name, "flow.embodied", FLOW_NAME - 1);
                if (spec->imported_module_count == 0) strncpy(spec->imported_modules[spec->imported_module_count++], "flow.embodied", FLOW_NAME - 1);
                if (!spec->resource_name[0]) strncpy(spec->resource_name, "hardware", FLOW_NAME - 1);
                if (!spec->capability_name[0]) strncpy(spec->capability_name, "realtime", FLOW_NAME - 1);
                if (!spec->contract_name[0]) strncpy(spec->contract_name, "zmp_torque_safe", FLOW_NAME - 1);
            }
            if (starts_with(line, "jet ")) copy_word(spec->jet.name, FLOW_NAME, line + 4);
            else if (starts_with(line, "@embodied ")) copy_word(spec->jet.name, FLOW_NAME, line + 10);
            else if (starts_with(line, "embodied ")) copy_word(spec->jet.name, FLOW_NAME, line + 9);
            continue;
        }
        if (starts_with(line, "@reload") || starts_with(line, "reload ") || !strcmp(line, "reload {")) { section = SECTION_RELOAD; continue; }
        if (starts_with(line, "@patchbay") || starts_with(line, "patchbay ") || !strcmp(line, "patchbay {")) { section = SECTION_PATCHBAY; continue; }
        if (starts_with(line, "thermal ") || !strcmp(line, "thermal {")) {
            section = SECTION_THERMAL; spec->thermal.enabled = 1; if (starts_with(line, "thermal ")) copy_word(spec->thermal.name, FLOW_NAME, line + 8); continue; }
        if (starts_with(line, "project ")) { section = SECTION_NONE; copy_word(spec->project_name, FLOW_NAME, line + 8);
            if (!spec->flow_name[0] || !strcmp(spec->flow_name, "lean_kernel_flow")) strncpy(spec->flow_name, spec->project_name, FLOW_NAME - 1); continue; } if (starts_with(line, "import ")) {
            section = SECTION_NONE; char mod[FLOW_NAME] = {0}; copy_word(mod, sizeof(mod), line + 7);
            if (!mod[0]) { fprintf(stderr, "flowc: invalid import declaration at line %d\n", line_number); parse_error = 1; } else {
                strncpy(spec->plugin_name, mod, FLOW_NAME - 1);
                if (spec->imported_module_count < 8) strncpy(spec->imported_modules[spec->imported_module_count++], mod, FLOW_NAME - 1); } continue; }
        if (starts_with(line, "contract ") || starts_with(line, "fallback ") || starts_with(line, "extern ")) {
            if (brace_depth == 0) section = SECTION_NONE;
            int is_c = starts_with(line, "contract "), is_e = starts_with(line, "extern ");
            char *dst = is_c ? spec->contract_name : (is_e ? spec->extern_kernel : spec->fallback_policy);
            copy_word(dst, FLOW_NAME, line + (is_e ? 7 : 9));
            if (is_e) strip_quotes(dst);
            if (!dst[0]) { fprintf(stderr, "flowc: invalid %s declaration at line %d\n", is_c ? "contract" : (is_e ? "extern" : "fallback"), line_number); parse_error = 1; } continue; }
        if (!strcmp(line, "}")) {
            if (section == SECTION_NONE || brace_depth == 0) { fprintf(stderr, "flowc: unexpected closing brace at line %d\n", line_number); parse_error = 1; }
            else { --brace_depth; section = SECTION_NONE; in_coords_list = 0; } continue; } switch (section) { case SECTION_INPUT: { int value, id, score; char trailing;
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
                break; } case SECTION_FLOW: { if (flow_nodes_from_bind) { spec->flow_node_count = 0; spec->flow_expression[0] = '\0'; flow_nodes_from_bind = 0; }
                const char *top = strstr(line, "top("); if (!spec->flow_expression[0]) strncpy(spec->flow_expression, line, FLOW_LINE - 1);
                parse_flow_nodes(spec, line); if (top) { int top_value, consumed = 0;
                    if (sscanf(top + 4, "%d%n", &top_value, &consumed) != 1 || top_value <= 0 || top[4 + consumed] != ')') {
                        fprintf(stderr, "flowc: invalid top expression at line %d: %s\n", line_number, line); parse_error = 1; } else spec->top_n = top_value; } break; }
            case SECTION_REQUIRE: { int value; char trailing;
                if (spec->constraint_count >= FLOW_CONSTRAINT_MAX || !parse_constraint(&spec->constraints[spec->constraint_count], line)) {
                    fprintf(stderr, "flowc: invalid requirement line %d: %s\n", line_number, line); parse_error = 1; break; }
                FlowConstraint *cur_c = &spec->constraints[spec->constraint_count];
                spec->declared_constraint_count = ++spec->constraint_count; if (!strcmp(line, "deterministic")) spec->deterministic = 1;
                else if (is_lean_kw(line)) { spec->lean_certified = 1; spec->deterministic = 1; }
                else if (!strcmp(cur_c->name, "memory")) {
                    if ((sscanf(line, "memory < %dmb %c", &value, &trailing) != 1 && sscanf(line, "memory %dmb %c", &value, &trailing) != 1) || value <= 0) {
                        fprintf(stderr, "flowc: invalid requirement line %d: %s\n", line_number, line); parse_error = 1; } else spec->memory_mb = value; }
                else if (!strcmp(cur_c->name, "arena")) {
                    size_t abytes = parse_bytes_with_unit(line + strlen(cur_c->name));
                    if (abytes == 0) { fprintf(stderr, "flowc: invalid arena quota line %d: %s\n", line_number, line); parse_error = 1; }
                    else { spec->arena_limit_bytes = abytes; if (spec->memory_mb == 0) spec->bounded = 1; } }
                else if (!strcmp(cur_c->name, "sla") || !strcmp(cur_c->name, "latency") || !strcmp(cur_c->name, "deadline")) {
                    double sec = parse_val_with_unit(line + strlen(cur_c->name));
                    if (sec <= 0.0) { fprintf(stderr, "flowc: invalid SLA requirement line %d: %s\n", line_number, line); parse_error = 1; }
                    else spec->sla_latency_ns = (uint64_t)(sec * 1e9 + 0.5); }
                else if (!strcmp(cur_c->name, "fallback")) {
                    const char *fb_v = cur_c->value[0] ? cur_c->value : cur_c->operator;
                    if (!fb_v[0]) { fprintf(stderr, "flowc: invalid fallback declaration at line %d\n", line_number); parse_error = 1; }
                    else strncpy(spec->fallback_policy, fb_v, FLOW_NAME - 1); } break; }
            case SECTION_ENSURE: if (!strcmp(line, "deterministic")) { ++spec->ensure_count; spec->deterministic = 1; }
                else if (is_lean_kw(line)) { ++spec->ensure_count; spec->lean_certified = 1; spec->deterministic = 1; }
                else { fprintf(stderr, "flowc: invalid ensure line %d: %s\n", line_number, line); parse_error = 1; } break; case SECTION_PREFER:
                if (!strcmp(line, "latency") || !strcmp(line, "throughput")) spec->prefer_latency = 1; else if (!strcmp(line, "memory")) spec->prefer_latency = 0;
                else { fprintf(stderr, "flowc: invalid preference line %d: %s\n", line_number, line); parse_error = 1; } break; case SECTION_JET: {
                if (in_coords_list) {
                    char cbuf[FLOW_LINE] = {0}; strncpy(cbuf, line, sizeof(cbuf) - 1);
                    if (strchr(cbuf, ']')) in_coords_list = 0;
                    for (char *tok = strtok(cbuf, " [],\t\r\n"); tok && spec->jet_coord_count < FLOW_FIELD_MAX; tok = strtok(NULL, " [],\t\r\n"))
                        strncpy(spec->jet_coord_names[spec->jet_coord_count++], tok, FLOW_NAME - 1);
                    if (!spec->jet.dim && spec->jet_coord_count > 0) spec->jet.dim = spec->jet_coord_count;
                    break;
                }
                char kw[FLOW_NAME] = {0}, val[FLOW_NAME] = {0}; if (sscanf(line, "%63s %63s", kw, val) >= 1) { double v = parse_val_with_unit(line + strlen(kw));
                    if (!strcmp(kw, "frequency") || !strcmp(kw, "target_frequency")) {
                        if (v <= 0.0) { fprintf(stderr, "flowc: invalid jet frequency at line %d: %s\n", line_number, line); parse_error = 1; }
                        else spec->jet.frequency_hz = v;
                    } else if (!strcmp(kw, "symplectic")) spec->jet.symplectic = 1;
                    else if (!strcmp(kw, "drift") || !strcmp(kw, "max_drift")) {
                        if (v <= 0.0) { fprintf(stderr, "flowc: invalid jet max_drift at line %d: %s\n", line_number, line); parse_error = 1; }
                        else spec->jet.max_drift = v;
                    } else if (!strcmp(kw, "zmp") || !strcmp(kw, "zmp_margin")) {
                        if (v <= 0.0) { fprintf(stderr, "flowc: invalid jet zmp_margin at line %d: %s\n", line_number, line); parse_error = 1; }
                        else spec->jet.zmp_margin = v;
                    } else if (!strcmp(kw, "dt")) {
                        if (v <= 0.0) { fprintf(stderr, "flowc: invalid jet dt at line %d: %s\n", line_number, line); parse_error = 1; }
                        else spec->jet.dt = v;
                    } else if (!strcmp(kw, "dim")) {
                        if (v <= 0.0) { fprintf(stderr, "flowc: invalid jet dim at line %d: %s\n", line_number, line); parse_error = 1; }
                        else spec->jet.dim = (size_t)v;
                    } else if (!strcmp(kw, "ingress")) {
                        if (!val[0]) { fprintf(stderr, "flowc: missing ingress driver at line %d\n", line_number); parse_error = 1; }
                        else { strip_quotes(val); strncpy(spec->ingress_driver, val, FLOW_NAME - 1); }
                    } else if (!strcmp(kw, "egress")) {
                        if (!val[0]) { fprintf(stderr, "flowc: missing egress driver at line %d\n", line_number); parse_error = 1; }
                        else { strip_quotes(val); strncpy(spec->egress_driver, val, FLOW_NAME - 1); }
                    } else if (!strcmp(kw, "coord")) {
                        if (!val[0]) { fprintf(stderr, "flowc: missing coord name at line %d\n", line_number); parse_error = 1; }
                        else if (spec->jet_coord_count < FLOW_FIELD_MAX) strncpy(spec->jet_coord_names[spec->jet_coord_count++], val, FLOW_NAME - 1);
                    } else if (!strcmp(kw, "coordinates")) {
                        char cbuf[FLOW_LINE] = {0}; strncpy(cbuf, line + 11, sizeof(cbuf) - 1);
                        if (strchr(cbuf, '[') && !strchr(cbuf, ']')) in_coords_list = 1;
                        spec->jet_coord_count = 0;
                        for (char *tok = strtok(cbuf, " [],\t\r\n"); tok && spec->jet_coord_count < FLOW_FIELD_MAX; tok = strtok(NULL, " [],\t\r\n"))
                            strncpy(spec->jet_coord_names[spec->jet_coord_count++], tok, FLOW_NAME - 1);
                        if (!spec->jet.dim && spec->jet_coord_count > 0) spec->jet.dim = spec->jet_coord_count;
                    }
                    else if (spec->constraint_count < FLOW_CONSTRAINT_MAX && parse_constraint(&spec->constraints[spec->constraint_count], line))
                        spec->declared_constraint_count = ++spec->constraint_count; } break; }
            case SECTION_RELOAD: {
                char kw[FLOW_NAME] = {0}, val[FLOW_NAME] = {0};
                if (sscanf(line, "%63s %63s", kw, val) == 2) {
                    size_t kw_orig_len = strlen(kw), kwl = kw_orig_len;
                    if (kwl > 0 && kw[kwl - 1] == ':') kw[kwl - 1] = '\0';
                    if (!strcmp(kw, "schema_version") || !strcmp(kw, "version")) {
                        char *ep = NULL; long sv = strtol(val, &ep, 10);
                        if (!ep || *ep != '\0' || sv <= 0) { fprintf(stderr, "flowc: invalid reload schema_version at line %d: %s\n", line_number, line); parse_error = 1; }
                        else spec->state_schema_version = (uint32_t)sv;
                    } else if (!strcmp(kw, "qsbr_timeout")) {
                        double sec = parse_val_with_unit(line + kw_orig_len);
                        if (sec <= 0.0) { fprintf(stderr, "flowc: invalid reload qsbr_timeout at line %d: %s\n", line_number, line); parse_error = 1; }
                        else spec->qsbr_timeout_ns = (uint64_t)(sec * 1e9 + 0.5);
                    } else if (!strcmp(kw, "migrate")) {
                        strip_quotes(val);
                        strncpy(spec->migrate_fn, val, FLOW_NAME - 1);
                    } else if (!strcmp(kw, "from") || !strcmp(kw, "from_schema")) {
                        strip_quotes(val);
                        strncpy(spec->reload_from_schema, val, FLOW_NAME - 1);
                    } else if (!strcmp(kw, "to") || !strcmp(kw, "to_schema")) {
                        strip_quotes(val);
                        strncpy(spec->reload_to_schema, val, FLOW_NAME - 1);
                    } else {
                        fprintf(stderr, "flowc: unknown reload directive at line %d: %s\n", line_number, line); parse_error = 1;
                    }
                } else {
                    fprintf(stderr, "flowc: invalid reload line %d: %s\n", line_number, line); parse_error = 1;
                } break; }
            case SECTION_PATCHBAY: {
                if (starts_with(line, "stage ")) {
                    if (spec->patchbay_stage_count >= FLOW_PATCHBAY_STAGE_MAX) { parse_error = 1; break; }
                    char lbuf[FLOW_LINE] = {0}; strncpy(lbuf, line + 6, sizeof(lbuf) - 1);
                    char *sp = NULL, *sname = strtok_r(lbuf, " \t", &sp);
                    if (!sname || !sname[0]) { parse_error = 1; break; }
                    char bpath[FLOW_LINE] = {0}, fn_nm[FLOW_NAME] = {0}, in_st[FLOW_NAME] = {0}, out_st[FLOW_NAME] = {0};
                    uint64_t exp_wcet_ns = 0;
                    for (char *tok = strtok_r(NULL, " \t", &sp); tok; tok = strtok_r(NULL, " \t", &sp)) {
                        if (!strcmp(tok, "bind")) {
                            char *v = strtok_r(NULL, " \t", &sp);
                            if (v) { strncpy(bpath, v, sizeof(bpath) - 1); strip_quotes(bpath); }
                        } else if (!strcmp(tok, "fn")) {
                            char *v = strtok_r(NULL, " \t", &sp);
                            if (v) { strncpy(fn_nm, v, sizeof(fn_nm) - 1); strip_quotes(fn_nm); }
                        } else if (!strcmp(tok, "in")) {
                            char *v = strtok_r(NULL, " \t", &sp);
                            if (v) { strncpy(in_st, v, sizeof(in_st) - 1); strip_quotes(in_st); }
                        } else if (!strcmp(tok, "out")) {
                            char *v = strtok_r(NULL, " \t", &sp);
                            if (v) { strncpy(out_st, v, sizeof(out_st) - 1); strip_quotes(out_st); }
                        } else if (!strcmp(tok, "wcet")) {
                            char *v = strtok_r(NULL, " \t", &sp);
                            if (!v) { parse_error = 1; break; }
                            char wbuf[64] = {0};
                            if (!strcmp(v, "<") || !strcmp(v, "<=")) {
                                char *v2 = strtok_r(NULL, " \t", &sp);
                                if (v2) strncpy(wbuf, v2, sizeof(wbuf) - 1);
                            } else strncpy(wbuf, v, sizeof(wbuf) - 1);
                            double sec = parse_val_with_unit(wbuf);
                            if (sec <= 0.0) { parse_error = 1; break; }
                            exp_wcet_ns = (uint64_t)(sec * 1e9 + 0.5);
                        }
                    }
                    if (parse_error || !bpath[0]) { parse_error = 1; break; }
                    FILE *lfp = open_bound_lean_file(input, bpath);
                    FlowSpec *st_spec = calloc(1, sizeof(FlowSpec));
                    if (!lfp || !st_spec || !parse_lean_spec(lfp, st_spec)) {
                        if (lfp) fclose(lfp);
                        free(st_spec);
                        fprintf(stderr, "flowc: failed to bind patchbay stage '%s' Lean kernel '%s' at line %d\n", sname, bpath, line_number);
                        parse_error = 1; break;
                    }
                    fclose(lfp);
                    merge_bound_lean_into_spec(spec, st_spec);
                    FlowLeanFunction picked_fn_val = {0};
                    int has_picked_fn = 0;
                    if (fn_nm[0]) {
                        for (size_t fi = 0; fi < st_spec->lean_function_count; ++fi)
                            if (!strcmp(st_spec->lean_functions[fi].name, fn_nm)) { picked_fn_val = st_spec->lean_functions[fi]; has_picked_fn = 1; break; }
                    } else {
                        for (size_t fi = st_spec->lean_function_count; fi > 0; --fi)
                            if (st_spec->lean_functions[fi - 1].is_struct_transform) { picked_fn_val = st_spec->lean_functions[fi - 1]; has_picked_fn = 1; break; }
                        if (!has_picked_fn && st_spec->lean_function_count > 0) {
                            picked_fn_val = st_spec->lean_functions[st_spec->lean_function_count - 1];
                            has_picked_fn = 1;
                        }
                    }
                    free(st_spec);
                    const FlowLeanFunction *picked_fn = has_picked_fn ? &picked_fn_val : NULL;
                    if (!picked_fn) { parse_error = 1; break; }
                    FlowPatchbayStage *stg = &spec->patchbay_stages[spec->patchbay_stage_count++];
                    memset(stg, 0, sizeof(*stg));
                    strncpy(stg->stage_name, sname, FLOW_NAME - 1);
                    strncpy(stg->lean_file, bpath, FLOW_LINE - 1);
                    strncpy(stg->fn_name, picked_fn->name, FLOW_NAME - 1);
                    strncpy(stg->in_struct, in_st[0] ? in_st : (picked_fn->param_count > 0 ? picked_fn->param_types[0] : ""), FLOW_NAME - 1);
                    strncpy(stg->out_struct, out_st[0] ? out_st : picked_fn->return_type, FLOW_NAME - 1);
                    const FlowLeanStruct *sin = find_lean_struct(spec, stg->in_struct);
                    const FlowLeanStruct *sout = find_lean_struct(spec, stg->out_struct);
                    stg->in_bytes = sin ? sin->byte_size : parse_lean_type_bytes(stg->in_struct);
                    stg->out_bytes = sout ? sout->byte_size : parse_lean_type_bytes(stg->out_struct);
                    stg->ast_wcet_ns = picked_fn->estimated_wcet_ns ? picked_fn->estimated_wcet_ns : 20ULL;
                    if (exp_wcet_ns > 0 && exp_wcet_ns < stg->ast_wcet_ns) {
                        fprintf(stderr, "flowc: patchbay stage '%s' declared wcet (%lluns) below AST bound (%lluns) at line %d\n",
                                sname, (unsigned long long)exp_wcet_ns, (unsigned long long)stg->ast_wcet_ns, line_number);
                        parse_error = 1; break;
                    }
                    stg->wcet_ns = exp_wcet_ns > 0 ? exp_wcet_ns : stg->ast_wcet_ns;
                    spec->lean_certified = 1; spec->deterministic = 1; spec->bounded = 1;
                    if (!spec->domain_name[0]) strncpy(spec->domain_name, "embodied", FLOW_NAME - 1);
                    if (!spec->plugin_name[0]) strncpy(spec->plugin_name, "flow.embodied", FLOW_NAME - 1);
                    if (spec->imported_module_count == 0) strncpy(spec->imported_modules[spec->imported_module_count++], "flow.embodied", FLOW_NAME - 1);
                    if (!spec->resource_name[0]) strncpy(spec->resource_name, "hardware", FLOW_NAME - 1);
                    if (!spec->capability_name[0]) strncpy(spec->capability_name, "realtime", FLOW_NAME - 1);
                    if (!spec->contract_name[0]) strncpy(spec->contract_name, "zmp_torque_safe", FLOW_NAME - 1);
                    if (!spec->input_name[0]) { strncpy(spec->input_name, "lean_stream", FLOW_NAME - 1); spec->max_count = 1024; }
                    if (spec->patchbay_stage_count == 1 && stg->in_bytes > 0) spec->input_stride_bytes = stg->in_bytes;
                } else if (starts_with(line, "wire ")) {
                    char wbuf[FLOW_LINE] = {0}; strncpy(wbuf, line + 5, sizeof(wbuf) - 1);
                    char wired[FLOW_PATCHBAY_STAGE_MAX][FLOW_NAME]; size_t w_cnt = 0;
                    for (char *nd = strtok(wbuf, "->"); nd && w_cnt < FLOW_PATCHBAY_STAGE_MAX; nd = strtok(NULL, "->")) {
                        char *tnd = trim(nd);
                        if (*tnd) strncpy(wired[w_cnt++], tnd, FLOW_NAME - 1);
                    }
                    if (w_cnt < 2) { parse_error = 1; break; }
                    FlowPatchbayStage ordered[FLOW_PATCHBAY_STAGE_MAX];
                    for (size_t wi = 0; wi < w_cnt; ++wi) {
                        int found = -1;
                        for (size_t si = 0; si < spec->patchbay_stage_count; ++si)
                            if (!strcmp(spec->patchbay_stages[si].stage_name, wired[wi])) { found = (int)si; break; }
                        if (found < 0) {
                            fprintf(stderr, "flowc: unknown patchbay stage '%s' in wire at line %d\n", wired[wi], line_number);
                            parse_error = 1; break;
                        }
                        ordered[wi] = spec->patchbay_stages[found];
                    }
                    if (!parse_error && w_cnt == spec->patchbay_stage_count)
                        memcpy(spec->patchbay_stages, ordered, w_cnt * sizeof(FlowPatchbayStage));
                } else {
                    fprintf(stderr, "flowc: invalid patchbay line %d: %s\n", line_number, line); parse_error = 1;
                } break; }
            case SECTION_THERMAL: { char kw[FLOW_NAME] = {0}, val[FLOW_NAME] = {0};
                if (sscanf(line, "%63s %63s", kw, val) >= 1) { double v = parse_val_with_unit(line + strlen(kw));
                    if (!strcmp(kw, "max_temp") || !strcmp(kw, "temp_max")) spec->thermal.max_temp_c = v;
                    else if (!strcmp(kw, "target_temp") || !strcmp(kw, "temp_target")) spec->thermal.target_temp_c = v;
                    else if (!strcmp(kw, "power_limit") || !strcmp(kw, "power")) spec->thermal.power_limit_w = v;
                    else if (!strcmp(kw, "r_thermal")) spec->thermal.r_thermal = v; else if (!strcmp(kw, "c_thermal")) spec->thermal.c_thermal = v;
                    else if (spec->constraint_count < FLOW_CONSTRAINT_MAX && parse_constraint(&spec->constraints[spec->constraint_count], line))
                        spec->declared_constraint_count = ++spec->constraint_count; } break; } default: {
                const char *sec_err = (section == SECTION_RESOURCE) ? "resource" : (section == SECTION_CAPABILITY) ? "capability" :
                                      (section == SECTION_DOMAIN) ? "domain" : "top-level";
                fprintf(stderr, "flowc: invalid %s line %d: %s\n", sec_err, line_number, line); parse_error = 1; break; } } }
    if (spec->patchbay_stage_count > 1) {
        for (size_t pi = 0; pi + 1 < spec->patchbay_stage_count; ++pi) {
            const FlowPatchbayStage *s_a = &spec->patchbay_stages[pi];
            const FlowPatchbayStage *s_b = &spec->patchbay_stages[pi + 1];
            if (s_a->out_bytes == 0 || s_b->in_bytes == 0 ||
                s_a->out_bytes != s_b->in_bytes ||
                strcmp(s_a->out_struct, s_b->in_struct) != 0) {
                fprintf(stderr, "flowc: patchbay wire mismatch between '%s' (%s:%zuB) and '%s' (%s:%zuB)\n",
                        s_a->stage_name, s_a->out_struct, s_a->out_bytes, s_b->stage_name, s_b->in_struct, s_b->in_bytes);
                parse_error = 1;
            }
        }
    }
    if (spec->patchbay_stage_count > 0 && !spec->flow_node_count) {
        if (!spec->flow_name[0]) strncpy(spec->flow_name, spec->project_name[0] ? spec->project_name : "patchbay_flow", FLOW_NAME - 1);
        snprintf(spec->flow_expression, sizeof(spec->flow_expression), "lean_stream -> %s -> top(3)",
                 !strcmp(spec->domain_name, "embodied") ? "flow_embodied_run" : "sort");
        parse_flow_nodes(spec, spec->flow_expression);
    }
    if (spec->migrate_fn[0] && (!spec->reload_from_schema[0] || !spec->reload_to_schema[0])) {
        for (size_t fi = 0; fi < spec->lean_function_count; ++fi) {
            const FlowLeanFunction *fn = &spec->lean_functions[fi];
            if (fn->is_struct_transform && !strcmp(fn->name, spec->migrate_fn) && fn->param_count > 0) {
                if (!spec->reload_from_schema[0]) strncpy(spec->reload_from_schema, fn->param_types[0], FLOW_NAME - 1);
                if (!spec->reload_to_schema[0]) strncpy(spec->reload_to_schema, fn->return_type, FLOW_NAME - 1);
                break;
            }
        }
    }
    if (!spec->flow_name[0]) { fprintf(stderr, "flowc: spec must define a flow\n"); return 0; }
    if (!spec->input_name[0]) { fprintf(stderr, "flowc: current backend family requires an input declaration\n"); return 0; }
    if (brace_depth != 0) { fprintf(stderr, "flowc: unclosed block at end of spec\n"); parse_error = 1; } if (parse_error) return 0;
    if (spec->input_name[0] && spec->max_count <= 0) { fprintf(stderr, "flowc: input must declare a positive max_count\n"); return 0; } if (spec->top_n <= 0) spec->top_n = 3;
    if (!spec->input_stride_bytes) spec->input_stride_bytes = 8;
    if (!spec->input_align_bytes) spec->input_align_bytes = 8;
    if (!spec->state_schema_version) spec->state_schema_version = 1;
    if (spec->jet.enabled) { if (spec->jet.frequency_hz <= 0.0) spec->jet.frequency_hz = 10000.0; if (spec->jet.max_drift <= 0.0) spec->jet.max_drift = 0.005;
        if (spec->jet.dt <= 0.0) spec->jet.dt = 1.0 / spec->jet.frequency_hz; if (spec->jet.zmp_margin <= 0.0) spec->jet.zmp_margin = 0.02;
        if (!spec->jet.dim) spec->jet.dim = spec->jet_coord_count > 0 ? spec->jet_coord_count : 4; } if (spec->thermal.enabled) { if (spec->thermal.max_temp_c <= 0.0) spec->thermal.max_temp_c = 95.0;
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
    if (ir->memory_limit_mb > 0 || ir->arena_limit_bytes > 0 || ir->sla_latency_ns > 0 || ir->fact_deterministic) add_fact(ir, FLOW_FACT_CONSTRAINT, "require");
    if (ir->fact_range_proven) add_fact(ir, FLOW_FACT_RANGE, "input_max_count"); if (ir->fact_size_preserved) add_fact(ir, FLOW_FACT_SIZE, "length_preserved");
    if (ir->fact_mutability_read_only) add_fact(ir, FLOW_FACT_MUTABILITY, "read_only"); if (ir->fact_deterministic) add_fact(ir, FLOW_FACT_DETERMINISM, "deterministic");
    if (ir->fact_lean_certified) add_fact(ir, FLOW_FACT_LEAN_CERTIFIED, "lean_certified"); ir->hole_count += strstr(ir->flow_expression, "?") != NULL && ir->hole_count == 0;
    if (ir->jet.enabled) add_fact(ir, FLOW_FACT_CAPABILITY, "jet_phase_space"); if (ir->thermal.enabled) add_fact(ir, FLOW_FACT_CAPABILITY, "thermal_envelope"); }
