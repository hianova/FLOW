#include "flow.h"
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char names[64][FLOW_NAME];
    char exprs[64][FLOW_LINE];
    size_t count;
} LeanConstTable;

static char *trim(char *text) {
    while (isspace((unsigned char)*text))
        ++text;
    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1]))
        --end;
    *end = '\0';
    return text;
}
static int starts_with(const char *text, const char *prefix) {
    return strncmp(text, prefix, strlen(prefix)) == 0;
}
static const char *normalize_lean_type_token(const char *t) {
    return (!t) ? "" : ((!strcmp(t, "\xe2\x84\x9d") || !strcmp(t, "Real")) ? "Float" : t);
}
static int is_lean_erased_proof_field(const char *t_expr) {
    if (!t_expr)
        return 1;
    while (*t_expr && isspace((unsigned char)*t_expr))
        ++t_expr;
    if (!*t_expr || !strcmp(t_expr, "Prop") || !strncmp(t_expr, "Prop ", 5) ||
        !strcmp(t_expr, "True") || !strcmp(t_expr, "False") || islower((unsigned char)t_expr[0]))
        return 1;
    return strchr(t_expr, '<') || strchr(t_expr, '>') || strchr(t_expr, '=') ||
           strstr(t_expr, "\xe2\x89\xa4") || strstr(t_expr, "\xe2\x89\xa5") ||
           strstr(t_expr, "\xe2\x89\xa0") || strstr(t_expr, "\xe2\x88\xa7") ||
           strstr(t_expr, "\xe2\x88\xa8") || strstr(t_expr, "\xc2\xac") ||
           strstr(t_expr, "\xe2\x86\x92") || strstr(t_expr, "\xe2\x86\x94") ||
           strstr(t_expr, "\xe2\x88\x80") || strstr(t_expr, "\xe2\x88\x83") || strstr(t_expr, "->");
}
static size_t parse_lean_type_bytes(const char *t) {
    if (!t || !*t)
        return 0;
#define FLOW_MATCH_SCALAR_BYTES(lean_name, norm_lean, c_type, sz, mode) \
    if (!strncmp(t, lean_name, sizeof(lean_name) - 1) &&                \
        !isalnum((unsigned char)t[sizeof(lean_name) - 1]))              \
        return (sz);
    FLOW_LEAN_SCALAR_TYPES_X(FLOW_MATCH_SCALAR_BYTES)
#undef FLOW_MATCH_SCALAR_BYTES
    return isupper((unsigned char)t[0]) ? 8 : 0;
}
static int is_scalar_lean_type(const char *t) {
    if (!t || !*t)
        return 0;
#define FLOW_IS_SCALAR_TYPE(lean_name, norm_lean, c_type, sz, mode) \
    if (!strcmp(t, lean_name))                                      \
        return 1;
    FLOW_LEAN_SCALAR_TYPES_X(FLOW_IS_SCALAR_TYPE)
#undef FLOW_IS_SCALAR_TYPE
    return 0;
}
static const FlowLeanStruct *find_lean_struct(const FlowSpec *spec, const char *name) {
    for (size_t i = 0; spec && name && name[0] && i < spec->lean_struct_count; ++i)
        if (!strcmp(spec->lean_structs[i].name, name))
            return &spec->lean_structs[i];
    return NULL;
}
static const FlowLeanEnum *find_lean_enum(const FlowSpec *spec, const char *name) {
    for (size_t i = 0; spec && name && name[0] && i < spec->lean_enum_count; ++i)
        if (!strcmp(spec->lean_enums[i].name, name))
            return &spec->lean_enums[i];
    return NULL;
}
static void lean_type_to_c_info(const FlowSpec *spec, const char *lt, char *c_out, size_t c_sz,
                                char *lean_out, size_t l_sz, size_t *bytes_out) {
    const char *ct = "uint64_t", *norm_lt = "UInt64";
    size_t b = 8;
    int matched = 0;
#define FLOW_MAP_SCALAR_INFO(lean_name, norm_lean, c_type, sz, mode) \
    if (!matched && lt && !strcmp(lt, lean_name)) {                  \
        ct = c_type;                                                 \
        norm_lt = norm_lean;                                         \
        b = (sz);                                                    \
        matched = 1;                                                 \
    }
    FLOW_LEAN_SCALAR_TYPES_X(FLOW_MAP_SCALAR_INFO)
#undef FLOW_MAP_SCALAR_INFO
    if (!matched) {
        if (find_lean_enum(spec, lt)) {
            ct = "uint32_t";
            norm_lt = "UInt32";
            b = 4;
        } else {
            b = parse_lean_type_bytes(lt);
            if (!b)
                b = 8;
        }
    }
    strncpy(c_out, ct, c_sz - 1);
    c_out[c_sz - 1] = '\0';
    strncpy(lean_out, norm_lt, l_sz - 1);
    lean_out[l_sz - 1] = '\0';
    if (bytes_out)
        *bytes_out = b;
}
static void recompute_lean_struct_layout(FlowLeanStruct *st) {
    if (!st)
        return;
    size_t off = 0, max_al = 1;
    for (size_t i = 0; i < st->field_count; ++i) {
        size_t sz = st->fields[i].size_bytes ? st->fields[i].size_bytes : 8,
               al = (sz >= 8) ? 8 : (sz >= 4 ? 4 : (sz >= 2 ? 2 : 1));
        if (al > max_al)
            max_al = al;
        off = ((off + al - 1) & ~(al - 1)) + sz;
    }
    st->align_bytes = max_al;
    st->byte_size = (off + max_al - 1) & ~(max_al - 1);
}
static int has_word_match(const char *hay, const char *word) {
    size_t wlen = strlen(word);
    if (!wlen)
        return 0;
    for (const char *p = strstr(hay, word); p; p = strstr(p + 1, word))
        if (((p == hay) || (!isalnum((unsigned char)p[-1]) && p[-1] != '_' && p[-1] != '.')) &&
            (!isalnum((unsigned char)p[wlen]) && p[wlen] != '_' && p[wlen] != '\''))
            return 1;
    return 0;
}
static void replace_substr_ex(const char *src, const char *pat, const char *rep, int word_bound,
                              int wrap_parens, char *out, size_t out_max) {
    if (!src || !pat || !rep || !out || out_max == 0)
        return;
    size_t plen = strlen(pat), rlen = strlen(rep), j = 0;
    if (!plen || (!wrap_parens && !strcmp(pat, rep))) {
        strncpy(out, src, out_max - 1);
        out[out_max - 1] = '\0';
        return;
    }
    for (size_t i = 0; src[i] && j + 4 < out_max;) {
        int match = (!strncmp(src + i, pat, plen));
        if (match && word_bound)
            match = ((i == 0) || (!isalnum((unsigned char)src[i - 1]) && src[i - 1] != '_' &&
                                  src[i - 1] != '.')) &&
                    (!isalnum((unsigned char)src[i + plen]) && src[i + plen] != '_' &&
                     src[i + plen] != '\'');
        if (match && j + rlen + 3 < out_max) {
            if (wrap_parens)
                out[j++] = '(';
            memcpy(out + j, rep, rlen);
            j += rlen;
            if (wrap_parens)
                out[j++] = ')';
            i += plen;
            continue;
        }
        out[j++] = src[i++];
    }
    out[j] = '\0';
}
static void add_lean_struct_field(FlowSpec *spec, FlowLeanStruct *st, const char *fn,
                                  const char *ft, int populate_spec_fields,
                                  size_t *struct_bytes_acc) {
    const FlowLeanStruct *inner =
        (st && strcmp(st->name, ft) != 0) ? find_lean_struct(spec, ft) : NULL;
    if (inner && inner->field_count > 0) {
        for (size_t k = 0; k < inner->field_count; ++k) {
            char flat_name[FLOW_NAME] = {0};
            snprintf(flat_name, sizeof(flat_name), "%s_%s", fn, inner->fields[k].name);
            add_lean_struct_field(spec, st, flat_name, inner->fields[k].lean_type,
                                  populate_spec_fields, struct_bytes_acc);
        }
        for (size_t iv = 0;
             iv < inner->invariant_count && st->invariant_count < FLOW_LEAN_STRUCT_INV_MAX; ++iv) {
            char cur_inv[128] = {0}, next_inv[128] = {0};
            strncpy(cur_inv, inner->invariants[iv], sizeof(cur_inv) - 1);
            for (size_t k = 0; k < inner->field_count; ++k) {
                char flat_name[FLOW_NAME] = {0};
                snprintf(flat_name, sizeof(flat_name), "%s_%s", fn, inner->fields[k].name);
                replace_substr_ex(cur_inv, inner->fields[k].name, flat_name, 1, 0, next_inv,
                                  sizeof(next_inv));
                strncpy(cur_inv, next_inv, sizeof(cur_inv) - 1);
            }
            strncpy(st->invariants[st->invariant_count++], cur_inv, sizeof(st->invariants[0]) - 1);
        }
        return;
    }
    char ct[16] = {0}, lt_norm[16] = {0};
    size_t b = 0;
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
            if (!strcmp(spec->state_fields[di].name, fn)) {
                dup_sf = 1;
                break;
            }
        if (!dup_sf) {
            FlowFieldSpec *fs = &spec->state_fields[spec->state_field_count++];
            strncpy(fs->name, fn, FLOW_NAME - 1);
            fs->size_bytes = b;
            fs->flags = 2u;
            if (struct_bytes_acc)
                *struct_bytes_acc += b;
            if (spec->jet_coord_count < FLOW_FIELD_MAX)
                strncpy(spec->jet_coord_names[spec->jet_coord_count++], fn, FLOW_NAME - 1);
        }
    }
}
static void apply_const_table(const LeanConstTable *consts, char *expr, size_t max_len) {
    for (size_t ci = 0; consts && expr && expr[0] && ci < consts->count; ++ci)
        if (has_word_match(expr, consts->names[ci])) {
            char next_buf[FLOW_LEAN_BODY_MAX] = {0};
            replace_substr_ex(expr, consts->names[ci], consts->exprs[ci], 1, 1, next_buf,
                              sizeof(next_buf));
            strncpy(expr, next_buf, max_len - 1);
            expr[max_len - 1] = '\0';
        }
}
static void strip_enum_qualifiers(const FlowSpec *spec, char *expr, size_t max_len) {
    if (!spec || !expr || !expr[0])
        return;
    for (size_t ei = 0; ei < spec->lean_enum_count; ++ei) {
        char qprefix[FLOW_NAME + 4] = {0};
        snprintf(qprefix, sizeof(qprefix), "%s.", spec->lean_enums[ei].name);
        if (strstr(expr, qprefix) != NULL) {
            char next_buf[FLOW_LEAN_BODY_MAX] = {0};
            replace_substr_ex(expr, qprefix, "", 0, 0, next_buf, sizeof(next_buf));
            strncpy(expr, next_buf, max_len - 1);
            expr[max_len - 1] = '\0';
        }
    }
}
static void rewrite_lean_abs_bars(const char *in, char *out, size_t max_len) {
    if (!in || !out || max_len == 0)
        return;
    char cur[FLOW_LEAN_BODY_MAX];
    strncpy(cur, in, sizeof(cur) - 1);
    cur[sizeof(cur) - 1] = '\0';
    for (int pass = 0; pass < 4; ++pass) {
        char next[FLOW_LEAN_BODY_MAX];
        size_t j = 0;
        int changed = 0;
        for (size_t i = 0; cur[i] && j + 24 < sizeof(next);) {
            if (cur[i] == '|' && (i == 0 || cur[i - 1] != '|') && cur[i + 1] != '|') {
                int pd = 0, abort_bar = 0;
                size_t close_k = 0;
                for (size_t k = i + 1; cur[k]; ++k) {
                    if (cur[k] == '(' || cur[k] == '{')
                        pd++;
                    else if (cur[k] == ')' || cur[k] == '}') {
                        if (pd == 0) {
                            abort_bar = 1;
                            break;
                        }
                        pd--;
                    } else if (cur[k] == '\n' || cur[k] == ';') {
                        abort_bar = 1;
                        break;
                    } else if (cur[k] == '=' && cur[k + 1] == '>') {
                        abort_bar = 1;
                        break;
                    } else if (pd == 0 && cur[k] == '|' && cur[k - 1] != '|' && cur[k + 1] != '|') {
                        close_k = k;
                        break;
                    }
                }
                if (!abort_bar && close_k > i + 1) {
                    size_t s = i + 1, e = close_k;
                    while (s < e && isspace((unsigned char)cur[s]))
                        ++s;
                    while (e > s && isspace((unsigned char)cur[e - 1]))
                        --e;
                    if (s < e) {
                        unsigned char fc = (unsigned char)cur[s], lc = (unsigned char)cur[e - 1];
                        int f_ok = isalnum(fc) || fc == '_' || fc == '(' || fc == '-';
                        int l_ok = isalnum(lc) || lc == '_' || lc == ')';
                        if (f_ok && l_ok && j + (e - s) + 22 < sizeof(next)) {
                            memcpy(next + j, "(flow_float_abs (", 17);
                            j += 17;
                            memcpy(next + j, cur + s, e - s);
                            j += (e - s);
                            memcpy(next + j, "))", 2);
                            j += 2;
                            i = close_k + 1;
                            changed = 1;
                            continue;
                        }
                    }
                }
            }
            next[j++] = cur[i++];
        }
        next[j] = '\0';
        strncpy(cur, next, sizeof(cur) - 1);
        cur[sizeof(cur) - 1] = '\0';
        if (!changed)
            break;
    }
    strncpy(out, cur, max_len - 1);
    out[max_len - 1] = '\0';
}
void flow_lean_normalize_expr(const char *src, char *dst, size_t dst_size) {
    static const struct {
        const char *lean_fn;
        size_t l_len;
        const char *c_fn;
        size_t c_len;
    } BUILTIN_MAP[] = {
#define FLOW_BUILTIN_ENTRY(l_fn, c_fn) {l_fn, sizeof(l_fn) - 1, c_fn, sizeof(c_fn) - 1},
        FLOW_LEAN_BUILTIN_OPS_X(FLOW_BUILTIN_ENTRY)
#undef FLOW_BUILTIN_ENTRY
    };
    if (!src || !dst || dst_size == 0)
        return;
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 20 < dst_size;) {
        if (!strncmp(src + i, "\xe2\x89\xa4", 3)) {
            dst[j++] = '<';
            dst[j++] = '=';
            i += 3;
            continue;
        }
        if (!strncmp(src + i, "\xe2\x89\xa5", 3)) {
            dst[j++] = '>';
            dst[j++] = '=';
            i += 3;
            continue;
        }
        if (!strncmp(src + i, "\xe2\x89\xa0", 3)) {
            dst[j++] = '!';
            dst[j++] = '=';
            i += 3;
            continue;
        }
        if (!strncmp(src + i, "\xe2\x88\xa7", 3)) {
            dst[j++] = '&';
            dst[j++] = '&';
            i += 3;
            continue;
        }
        if (!strncmp(src + i, "\xe2\x88\xa8", 3)) {
            dst[j++] = '|';
            dst[j++] = '|';
            i += 3;
            continue;
        }
        if (!strncmp(src + i, "\xc2\xac", 2)) {
            dst[j++] = '!';
            i += 2;
            continue;
        }
        int mapped = 0;
        for (size_t bi = 0; bi < sizeof(BUILTIN_MAP) / sizeof(BUILTIN_MAP[0]); ++bi) {
            size_t ll = BUILTIN_MAP[bi].l_len;
            if (!strncmp(src + i, BUILTIN_MAP[bi].lean_fn, ll) &&
                !isalnum((unsigned char)src[i + ll]) && src[i + ll] != '_') {
                memcpy(dst + j, BUILTIN_MAP[bi].c_fn, BUILTIN_MAP[bi].c_len);
                j += BUILTIN_MAP[bi].c_len;
                i += ll;
                mapped = 1;
                break;
            }
        }
        if (mapped)
            continue;
        dst[j++] = src[i++];
    }
    dst[j] = '\0';
}
static void normalize_lean_body(const char *in, char *out, size_t max_len) {
    char abs_buf[FLOW_LEAN_BODY_MAX] = {0};
    char norm_buf[FLOW_LEAN_BODY_MAX] = {0};
    rewrite_lean_abs_bars(in, abs_buf, sizeof(abs_buf));
    flow_lean_normalize_expr(abs_buf, norm_buf, sizeof(norm_buf));
    size_t j = 0;
    for (size_t i = 0; norm_buf[i] && j + 1 < max_len;) {
        if (norm_buf[i] == '.' &&
            (i == 0 || (!isalnum((unsigned char)norm_buf[i - 1]) && norm_buf[i - 1] != '_' &&
                        norm_buf[i - 1] != ')')) &&
            (isalpha((unsigned char)norm_buf[i + 1]) || norm_buf[i + 1] == '_')) {
            i++;
        } else {
            out[j++] = norm_buf[i++];
        }
    }
    out[j] = '\0';
    for (size_t i = 1; out[i]; ++i)
        if (out[i] == '.' &&
            (isalnum((unsigned char)out[i - 1]) || out[i - 1] == '_' || out[i - 1] == ')') &&
            (isalpha((unsigned char)out[i + 1]) || out[i + 1] == '_')) {
            size_t k = i + 1;
            while (isalnum((unsigned char)out[k]) || out[k] == '_')
                ++k;
            while (out[k] == '.' && (isalpha((unsigned char)out[k + 1]) || out[k + 1] == '_')) {
                out[k] = '_';
                ++k;
                while (isalnum((unsigned char)out[k]) || out[k] == '_')
                    ++k;
            }
            i = k - 1;
        }
}
static uint64_t estimate_lean_ast_wcet_ns(const FlowLeanFunction *fn) {
    if (!fn)
        return FLOW_WCET_BASE_CALL_NS;
    uint64_t cost =
        FLOW_WCET_BASE_CALL_NS + FLOW_WCET_STRUCT_FIELD_NS * (uint64_t)fn->field_assign_count;
    const char *b = fn->is_struct_transform
                        ? (fn->field_assign_count > 0 ? fn->field_assign_exprs[0] : "")
                        : fn->body;
    for (size_t i = 0; b[i]; ++i) {
        if (b[i] == '+' || b[i] == '*' || b[i] == '&' || b[i] == '|' || b[i] == '<' ||
            b[i] == '>' || b[i] == '=' || (b[i] == '-' && b[i + 1] != '>'))
            cost += FLOW_WCET_ALU_OP_NS;
        else if (b[i] == '/' || b[i] == '%' || b[i] == '^')
            cost += FLOW_WCET_COMPLEX_OP_NS;
        else if (!strncmp(b + i, "if ", 3) || !strncmp(b + i, "match ", 6))
            cost += FLOW_WCET_BRANCH_OP_NS;
        else if (!strncmp(b + i, "flow_quantize", 13) || !strncmp(b + i, "flow_dequantize", 15)) {
            cost += FLOW_WCET_COMPLEX_OP_NS;
            i += 12;
        }
    }
    return cost;
}
static int is_valid_scalar_lean_body_str(const FlowSpec *spec, const FlowLeanFunction *fn,
                                         const char *b) {
    static const char *const KW[] = {"if",
                                     "then",
                                     "else",
                                     "let",
                                     "in",
                                     "match",
                                     "with",
                                     "true",
                                     "false",
                                     "decide",
                                     "flow_quantize",
                                     "flow_dequantize",
                                     "Option",
                                     "FlowResult",
                                     "SpaceWithException",
                                     "point",
                                     "collapse",
                                     "_",
                                     "_ret",
#define FLOW_KW_BUILTIN(l_fn, c_fn) c_fn,
                                     FLOW_LEAN_BUILTIN_OPS_X(FLOW_KW_BUILTIN)
#undef FLOW_KW_BUILTIN
                                         NULL};
    char let_vars[32][FLOW_NAME];
    size_t let_cnt = 0;
    if (!b || !b[0])
        return 0;
    if (strstr(b, "->") || strstr(b, "<->") || strstr(b, "\xe2\x86\x92") ||
        strstr(b, "\xe2\x86\x94"))
        return 0;
    int has_struct_param = fn->is_struct_transform;
    for (size_t i = 0; i < fn->param_count; ++i)
        if (find_lean_struct(spec, fn->param_types[i]) != NULL)
            has_struct_param = 1;
    for (const char *p = b; *p; ++p) {
        if ((unsigned char)*p >= 0x80 || *p == '{' || *p == '}' || *p == '[' || *p == ']')
            return 0;
        if (*p == '.' && isalpha((unsigned char)p[1]) && !has_struct_param &&
            spec->lean_enum_count == 0)
            return 0;
        if (!strncmp(p, "let ", 4) && (p == b || isspace((unsigned char)p[-1]) || p[-1] == '(')) {
            const char *vs = p + 4;
            while (*vs && isspace((unsigned char)*vs))
                ++vs;
            if (*vs == '(') {
                ++vs;
                while (*vs && *vs != ')') {
                    while (*vs && (isspace((unsigned char)*vs) || *vs == ','))
                        ++vs;
                    char lv[FLOW_NAME] = {0};
                    size_t lk = 0;
                    while (*vs && (isalnum((unsigned char)*vs) || *vs == '_')) {
                        if (lk + 1 < sizeof(lv))
                            lv[lk++] = *vs;
                        ++vs;
                    }
                    while (*vs && *vs != ',' && *vs != ')')
                        ++vs;
                    if (lv[0] && let_cnt < 32)
                        strncpy(let_vars[let_cnt++], lv, FLOW_NAME - 1);
                }
            } else {
                char lv[FLOW_NAME] = {0};
                if (sscanf(vs, "%63[^ :=\t\r\n]", lv) == 1 && lv[0] && let_cnt < 32)
                    strncpy(let_vars[let_cnt++], lv, FLOW_NAME - 1);
            }
        } else if (*p == '|' && (p == b || p[-1] != '|') && p[1] != '|') {
            const char *ms = p + 1;
            while (*ms && isspace((unsigned char)*ms))
                ++ms;
            if (*ms == '(') {
                ++ms;
                while (*ms && isspace((unsigned char)*ms))
                    ++ms;
            }
            size_t clen = 0;
            if (!strncmp(ms, "some", 4) && isspace((unsigned char)ms[4]))
                clen = 4;
            else if (!strncmp(ms, "interior", 8) && isspace((unsigned char)ms[8]))
                clen = 8;
            else if (!strncmp(ms, "boundary", 8) && isspace((unsigned char)ms[8]))
                clen = 8;
            else if (!strncmp(ms, "point", 5) && isspace((unsigned char)ms[5]))
                clen = 5;
            if (clen > 0) {
                ms += clen;
                while (*ms && isspace((unsigned char)*ms))
                    ++ms;
                char mv[FLOW_NAME] = {0};
                size_t mk = 0;
                while (*ms && (isalnum((unsigned char)*ms) || *ms == '_')) {
                    if (mk + 1 < sizeof(mv))
                        mv[mk++] = *ms;
                    ++ms;
                }
                if (mv[0] && strcmp(mv, "_") != 0 && let_cnt < 32) {
                    strncpy(let_vars[let_cnt++], mv, FLOW_NAME - 1);
                }
            }
        }
    }
    for (const char *p = b; *p;) {
        if (isdigit((unsigned char)*p)) {
            while (isalnum((unsigned char)*p) || *p == '_' || *p == '.') {
                if ((*p == 'e' || *p == 'E') && (p[1] == '+' || p[1] == '-'))
                    ++p;
                ++p;
            }
            continue;
        }
        if (isalpha((unsigned char)*p) || *p == '_') {
            char id[FLOW_NAME] = {0};
            size_t k = 0;
            while (isalnum((unsigned char)*p) || *p == '_' || *p == '\'') {
                if (*p == '\'')
                    return 0;
                if (k + 1 < sizeof(id))
                    id[k++] = *p;
                ++p;
            }
            if (*p == '.' && (isalpha((unsigned char)p[1]) || p[1] == '_')) {
                ++p;
                while (isalnum((unsigned char)*p) || *p == '_')
                    ++p;
            }
            int ok = is_scalar_lean_type(id);
            for (int i = 0; !ok && KW[i]; ++i)
                if (!strcmp(id, KW[i]))
                    ok = 1;
            for (size_t i = 0; !ok && i < fn->param_count; ++i)
                if (!strcmp(id, fn->param_names[i]))
                    ok = 1;
            for (size_t i = 0; !ok && i < let_cnt; ++i)
                if (!strcmp(id, let_vars[i]))
                    ok = 1;
            for (size_t i = 0; !ok && i < spec->lean_function_count; ++i)
                if (!strcmp(id, spec->lean_functions[i].name))
                    ok = 1;
            for (size_t i = 0; !ok && i < spec->lean_enum_count; ++i) {
                if (!strcmp(id, spec->lean_enums[i].name))
                    ok = 1;
                for (size_t v = 0; !ok && v < spec->lean_enums[i].variant_count; ++v)
                    if (!strcmp(id, spec->lean_enums[i].variants[v]))
                        ok = 1;
            }
            if (!ok)
                return 0;
        } else
            ++p;
    }
    return 1;
}
static void parse_struct_record_fields(FlowLeanFunction *fn, const char *prefix,
                                       const char *rec_in) {
    if (!fn || !rec_in)
        return;
    char unfolded[FLOW_LEAN_BODY_MAX];
    strncpy(unfolded, rec_in, sizeof(unfolded) - 1);
    unfolded[sizeof(unfolded) - 1] = '\0';
    for (int iter = 0; iter < 8; ++iter) {
        const char *cur = unfolded;
        while (*cur && isspace((unsigned char)*cur))
            ++cur;
        if (strncmp(cur, "let ", 4) != 0)
            break;
        const char *ce = NULL, *sep = NULL;
        int pd = 0, bd = 0, skip = 0;
        for (const char *p = cur + 4; *p; ++p) {
            if (*p == '(')
                pd++;
            else if (*p == ')' && pd > 0)
                pd--;
            else if (*p == '{') {
                if (pd == 0 && ce) {
                    sep = p;
                    skip = 0;
                    break;
                }
                bd++;
            } else if (*p == '}' && bd > 0)
                bd--;
            else if (pd == 0 && bd == 0 && !ce && p[0] == ':' && p[1] == '=') {
                ce = p;
                ++p;
            } else if (pd == 0 && bd == 0 && ce && (*p == ';' || *p == '\n')) {
                sep = p;
                skip = 1;
                break;
            } else if (pd == 0 && bd == 0 && ce && !strncmp(p, " in ", 4)) {
                sep = p;
                skip = 4;
                break;
            }
        }
        if (!ce || !sep)
            break;
        const char *vs = cur + 4;
        while (vs < ce && isspace((unsigned char)*vs))
            ++vs;
        size_t vl = 0;
        while (vs + vl < ce && (isalnum((unsigned char)vs[vl]) || vs[vl] == '_'))
            ++vl;
        if (vl == 0 || vl >= FLOW_NAME)
            break;
        char vname[FLOW_NAME] = {0}, vrhs[FLOW_LINE] = {0}, next_buf[FLOW_LEAN_BODY_MAX] = {0};
        memcpy(vname, vs, vl);
        const char *rs = ce + 2, *re = sep;
        while (rs < re && isspace((unsigned char)*rs))
            ++rs;
        while (re > rs && isspace((unsigned char)re[-1]))
            --re;
        size_t rl = (size_t)(re - rs);
        if (rl >= sizeof(vrhs))
            rl = sizeof(vrhs) - 1;
        memcpy(vrhs, rs, rl);
        vrhs[rl] = '\0';
        replace_substr_ex(sep + skip, vname, vrhs, 1, 1, next_buf, sizeof(next_buf));
        strncpy(unfolded, next_buf, sizeof(unfolded) - 1);
        unfolded[sizeof(unfolded) - 1] = '\0';
    }
    const char *lb = strchr(unfolded, '{'), *rb = strrchr(unfolded, '}');
    if (!lb || !rb || rb <= lb)
        return;
    const char *s = lb + 1, *end = rb, *f_id_starts[FLOW_FIELD_MAX], *f_id_ends[FLOW_FIELD_MAX],
               *f_val_starts[FLOW_FIELD_MAX];
    size_t f_cnt = 0;
    int pd = 0, bd = 0;
    for (const char *p = s; p + 1 < end; ++p) {
        if (*p == '(')
            pd++;
        else if (*p == ')' && pd > 0)
            pd--;
        else if (*p == '{')
            bd++;
        else if (*p == '}' && bd > 0)
            bd--;
        else if (pd == 0 && bd == 0 && p[0] == ':' && p[1] == '=') {
            const char *ie = p;
            while (ie > s && isspace((unsigned char)ie[-1]))
                --ie;
            const char *is = ie;
            while (is > s && (isalnum((unsigned char)is[-1]) || is[-1] == '_'))
                --is;
            if (is < ie) {
                const char *prev = is;
                while (prev > s && isspace((unsigned char)prev[-1]))
                    --prev;
                if (prev - s >= 3 && !strncmp(prev - 3, "let", 3) &&
                    (prev - 3 == s || isspace((unsigned char)prev[-4]) || prev[-4] == '('))
                    continue;
                if (f_cnt < FLOW_FIELD_MAX) {
                    f_id_starts[f_cnt] = is;
                    f_id_ends[f_cnt] = ie;
                    f_val_starts[f_cnt] = p + 2;
                    f_cnt++;
                }
            }
        }
    }
    for (size_t i = 0; i < f_cnt; ++i) {
        char fname[FLOW_NAME] = {0}, full_name[FLOW_NAME] = {0};
        size_t fl = (size_t)(f_id_ends[i] - f_id_starts[i]);
        if (fl == 0 || fl >= sizeof(fname))
            continue;
        memcpy(fname, f_id_starts[i], fl);
        fname[fl] = '\0';
        if (prefix && prefix[0])
            snprintf(full_name, sizeof(full_name), "%s_%s", prefix, fname);
        else
            strncpy(full_name, fname, sizeof(full_name) - 1);
        const char *vs = f_val_starts[i], *ve = (i + 1 < f_cnt) ? f_id_starts[i + 1] : end;
        while (vs < ve && isspace((unsigned char)*vs))
            ++vs;
        while (ve > vs && (isspace((unsigned char)ve[-1]) || ve[-1] == ',' || ve[-1] == ';'))
            --ve;
        char fexpr[FLOW_LINE] = {0};
        size_t el = (size_t)(ve - vs);
        if (el >= sizeof(fexpr))
            el = sizeof(fexpr) - 1;
        memcpy(fexpr, vs, el);
        fexpr[el] = '\0';
        if (fexpr[0] == '{' && el >= 2 && fexpr[el - 1] == '}')
            parse_struct_record_fields(fn, full_name, fexpr);
        else if (fn->field_assign_count < FLOW_FIELD_MAX) {
            strncpy(fn->field_assign_names[fn->field_assign_count], full_name,
                    sizeof(fn->field_assign_names[0]) - 1);
            strncpy(fn->field_assign_exprs[fn->field_assign_count], fexpr,
                    sizeof(fn->field_assign_exprs[0]) - 1);
            fn->field_assign_count++;
        }
    }
}
static void append_contract_clause(char *dst, size_t dst_max, const char *clause) {
    if (!dst || !clause || !clause[0] || strstr(dst, clause) != NULL)
        return;
    size_t cur_l = strlen(dst), n_l = strlen(clause);
    if (cur_l == 0 && n_l + 3 < dst_max)
        snprintf(dst, dst_max, "(%s)", clause);
    else if (cur_l > 0 && cur_l + n_l + 7 < dst_max)
        snprintf(dst + cur_l, dst_max - cur_l, " && (%s)", clause);
}
static void try_add_lean_function(FlowSpec *spec, LeanConstTable *consts, const char *sig_in,
                                  const char *body_in, int is_private) {
    if (!spec || !sig_in || !body_in)
        return;
    char sig[FLOW_LEAN_BODY_MAX];
    strncpy(sig, sig_in, sizeof(sig) - 1);
    sig[sizeof(sig) - 1] = '\0';
    char *s = trim(sig);
    size_t kw_len = starts_with(s, "def ") ? 4 : (starts_with(s, "abbrev ") ? 7 : 0);
    if (!kw_len)
        return;
    char *first_lp = strchr(s, '(');
    if (!first_lp) {
        if (consts) {
            char cname[FLOW_NAME] = {0}, ctype[32] = {0};
            if (sscanf(s + kw_len, "%63[^ :\t\n\r] : %31[^ :=\t\n\r]", cname, ctype) >= 1 &&
                cname[0] && is_scalar_lean_type(normalize_lean_type_token(ctype)) &&
                body_in[0] != '{' && body_in[0] != '[') {
                for (char *cp = cname; *cp; ++cp)
                    if (*cp == '.')
                        *cp = '_';
                size_t ci = 0;
                for (; ci < consts->count; ++ci)
                    if (!strcmp(consts->names[ci], cname))
                        break;
                if (ci < 64) {
                    strncpy(consts->names[ci], cname, FLOW_NAME - 1);
                    normalize_lean_body(body_in, consts->exprs[ci], sizeof(consts->exprs[0]));
                    if (ci == consts->count)
                        consts->count++;
                }
            }
        }
        return;
    }
    FlowLeanFunction *fn = (FlowLeanFunction *)calloc(1, sizeof(FlowLeanFunction));
    if (!fn)
        return;
    fn->is_private = is_private ? 1 : 0;
    if (sscanf(s + kw_len, "%63[^ ({:\t\n\r]", fn->name) != 1 || !fn->name[0]) {
        free(fn);
        return;
    }
    for (char *np = fn->name; *np; ++np)
        if (*np == '.')
            *np = '_';
    if (!strcmp(fn->name, "f_abs") || !strcmp(fn->name, "f_max") || !strcmp(fn->name, "f_min")) {
        free(fn);
        return;
    }
    int dup_idx = -1;
    for (size_t i = 0; i < spec->lean_function_count; ++i)
        if (!strcmp(spec->lean_functions[i].name, fn->name)) {
            dup_idx = (int)i;
            break;
        }
    if (dup_idx < 0 && spec->lean_function_count >= FLOW_LEAN_FN_MAX) {
        free(fn);
        return;
    }
    char *last_rp = NULL, *ret_colon = NULL;
    int pd = 0;
    for (char *p = first_lp; *p; ++p) {
        if (*p == '(' || *p == '{' || *p == '[')
            pd++;
        else if (*p == ')' || *p == '}' || *p == ']') {
            if (pd > 0)
                pd--;
            if (pd == 0 && *p == ')')
                last_rp = p;
        } else if (pd == 0 && *p == ':' && last_rp != NULL) {
            ret_colon = p;
            break;
        }
    }
    if (!last_rp || !ret_colon || last_rp <= first_lp) {
        free(fn);
        return;
    }
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
        for (size_t i = 0; ret_raw[i] && tj + 2 < sizeof(tcopy);) {
            if (!strncmp(ret_raw + i, "\xc3\x97", 2)) {
                tcopy[tj++] = '*';
                i += 2;
            } else
                tcopy[tj++] = ret_raw[i++];
        }
        tcopy[tj] = '\0';
        char *sp = NULL;
        for (char *tok = strtok_r(tcopy, "*", &sp); tok; tok = strtok_r(NULL, "*", &sp)) {
            char *tt = trim(tok);
            if (!*tt || fn->tuple_arity >= FLOW_TUPLE_MAX || !is_scalar_lean_type(tt)) {
                fn->tuple_arity = 0;
                break;
            }
            strncpy(fn->tuple_types[fn->tuple_arity++], normalize_lean_type_token(tt),
                    sizeof(fn->tuple_types[0]) - 1);
        }
        if (fn->tuple_arity >= 2) {
            ret_is_tuple = 1;
            fn->is_tuple_return = 1;
            strncpy(fn->return_type, fn->tuple_types[0], sizeof(fn->return_type) - 1);
        }
    }
    if (!ret_is_tuple) {
        char rt1[FLOW_NAME] = {0}, rt2[FLOW_NAME] = {0}, rt3[FLOW_NAME] = {0}, extra[16] = {0};
        int rntok = sscanf(ret_raw, "%63s %63s %63s %15s", rt1, rt2, rt3, extra);
        const char *ret_t = rt1;
        if ((rntok == 2 && (!strcmp(rt1, "Option") || !strcmp(rt1, "FlowResult") ||
                            !strcmp(rt1, "SpaceWithException"))) ||
            (rntok == 3 && (!strcmp(rt1, "FlowResult") || !strcmp(rt1, "SpaceWithException")))) {
            fn->is_result_return = 1;
            ret_t = rt2;
        } else if (rntok != 1) {
            free(fn);
            return;
        }
        const char *norm_ret = normalize_lean_type_token(ret_t);
        ret_is_struct = (find_lean_struct(spec, norm_ret) != NULL);
        ret_is_scalar = is_scalar_lean_type(norm_ret) || (find_lean_enum(spec, norm_ret) != NULL);
        if (!ret_is_struct && !ret_is_scalar) {
            free(fn);
            return;
        }
        strncpy(fn->return_type, norm_ret, sizeof(fn->return_type) - 1);
    }
    for (char *p = first_lp; p && p < last_rp;) {
        char *lp = strchr(p, '(');
        if (!lp || lp >= last_rp)
            break;
        char *rp = strchr(lp + 1, ')');
        if (!rp || rp > last_rp) {
            free(fn);
            return;
        }
        char grp[256] = {0};
        size_t glen = (size_t)(rp - (lp + 1));
        if (glen >= sizeof(grp)) {
            free(fn);
            return;
        }
        memcpy(grp, lp + 1, glen);
        char *colon = strchr(grp, ':');
        if (!colon) {
            free(fn);
            return;
        }
        *colon = '\0';
        char *p_rhs = trim(colon + 1);
        if (is_lean_erased_proof_field(p_rhs)) {
            if (strcmp(p_rhs, "Prop") != 0 && strncmp(p_rhs, "Prop ", 5) != 0 &&
                strcmp(p_rhs, "True") != 0 && strcmp(p_rhs, "False") != 0 &&
                (strchr(p_rhs, '<') || strchr(p_rhs, '>') || strchr(p_rhs, '=') ||
                 strstr(p_rhs, "\xe2\x89\xa4") || strstr(p_rhs, "\xe2\x89\xa5") ||
                 strstr(p_rhs, "\xe2\x89\xa0") || strstr(p_rhs, "\xe2\x88\xa7") ||
                 strstr(p_rhs, "\xe2\x88\xa8"))) {
                char norm_pre[256] = {0};
                normalize_lean_body(p_rhs, norm_pre, sizeof(norm_pre));
                append_contract_clause(fn->precond_expr, sizeof(fn->precond_expr), norm_pre);
            }
            p = rp + 1;
            continue;
        }
        char pt1[FLOW_NAME] = {0}, pt2[FLOW_NAME] = {0}, pt3[FLOW_NAME] = {0}, pextra[16] = {0};
        int pntok = sscanf(p_rhs, "%63s %63s %63s %15s", pt1, pt2, pt3, pextra);
        int param_is_res = 0;
        const char *pt = pt1;
        if ((pntok == 2 && (!strcmp(pt1, "Option") || !strcmp(pt1, "FlowResult") ||
                            !strcmp(pt1, "SpaceWithException"))) ||
            (pntok == 3 && (!strcmp(pt1, "FlowResult") || !strcmp(pt1, "SpaceWithException")))) {
            param_is_res = 1;
            pt = pt2;
        } else if (pntok != 1) {
            free(fn);
            return;
        }
        const char *norm_pt = normalize_lean_type_token(pt);
        if (!is_scalar_lean_type(norm_pt) && !find_lean_struct(spec, norm_pt) &&
            !find_lean_enum(spec, norm_pt)) {
            free(fn);
            return;
        }
        for (char *tok = strtok(grp, " \t\n\r"); tok; tok = strtok(NULL, " \t\n\r")) {
            if (fn->param_count >= FLOW_LEAN_PARAM_MAX) {
                free(fn);
                return;
            }
            strncpy(fn->param_names[fn->param_count], tok, sizeof(fn->param_names[0]) - 1);
            strncpy(fn->param_types[fn->param_count], norm_pt, sizeof(fn->param_types[0]) - 1);
            fn->param_is_result[fn->param_count] = (uint8_t)param_is_res;
            fn->param_count++;
        }
        p = rp + 1;
    }
    if (fn->param_count == 0) {
        free(fn);
        return;
    }
    for (size_t pi = 0; pi < fn->param_count; ++pi) {
        const FlowLeanStruct *st = find_lean_struct(spec, fn->param_types[pi]);
        if (!st)
            continue;
        for (size_t iv = 0; iv < st->invariant_count; ++iv) {
            char cur_inv[256] = {0}, next_inv[256] = {0};
            strncpy(cur_inv, st->invariants[iv], sizeof(cur_inv) - 1);
            for (size_t k = 0; k < st->field_count; ++k) {
                char qf[FLOW_NAME * 2] = {0};
                snprintf(qf, sizeof(qf), "%s.%s", fn->param_names[pi], st->fields[k].name);
                replace_substr_ex(cur_inv, st->fields[k].name, qf, 1, 0, next_inv,
                                  sizeof(next_inv));
                strncpy(cur_inv, next_inv, sizeof(cur_inv) - 1);
            }
            if (is_valid_scalar_lean_body_str(spec, fn, cur_inv))
                append_contract_clause(fn->precond_expr, sizeof(fn->precond_expr), cur_inv);
        }
    }
    for (size_t si = 0; si < spec->lean_struct_count; ++si) {
        const FlowLeanStruct *st = &spec->lean_structs[si];
        for (size_t iv = 0; iv < st->invariant_count; ++iv) {
            int mentions_field = 0;
            for (size_t k = 0; k < st->field_count; ++k) {
                if (has_word_match(st->invariants[iv], st->fields[k].name)) {
                    mentions_field = 1;
                    break;
                }
            }
            if (mentions_field && is_valid_scalar_lean_body_str(spec, fn, st->invariants[iv]))
                append_contract_clause(fn->precond_expr, sizeof(fn->precond_expr),
                                       st->invariants[iv]);
        }
    }
    if (fn->precond_expr[0]) {
        apply_const_table(consts, fn->precond_expr, sizeof(fn->precond_expr));
        strip_enum_qualifiers(spec, fn->precond_expr, sizeof(fn->precond_expr));
        if (!is_valid_scalar_lean_body_str(spec, fn, fn->precond_expr))
            fn->precond_expr[0] = '\0';
    }
    char norm_body[FLOW_LEAN_BODY_MAX] = {0};
    normalize_lean_body(body_in, norm_body, sizeof(norm_body));
    apply_const_table(consts, norm_body, sizeof(norm_body));
    strip_enum_qualifiers(spec, norm_body, sizeof(norm_body));
    if (ret_is_struct) {
        fn->is_struct_transform = 1;
        parse_struct_record_fields(fn, "", norm_body);
        if (fn->field_assign_count == 0) {
            free(fn);
            return;
        }
    } else {
        strncpy(fn->body, norm_body, sizeof(fn->body) - 1);
        if (!is_valid_scalar_lean_body_str(spec, fn, fn->body)) {
            free(fn);
            return;
        }
    }
    fn->estimated_wcet_ns = estimate_lean_ast_wcet_ns(fn);
    if (dup_idx >= 0) {
        FlowLeanFunction *prev = &spec->lean_functions[dup_idx];
        if (!fn->precond_expr[0] && prev->precond_expr[0] &&
            is_valid_scalar_lean_body_str(spec, fn, prev->precond_expr))
            strncpy(fn->precond_expr, prev->precond_expr, sizeof(fn->precond_expr) - 1);
        if (!fn->postcond_expr[0] && prev->postcond_expr[0] &&
            is_valid_scalar_lean_body_str(spec, fn, prev->postcond_expr))
            strncpy(fn->postcond_expr, prev->postcond_expr, sizeof(fn->postcond_expr) - 1);
        if (fn->thm_guard_count == 0 && prev->thm_guard_count > 0) {
            for (size_t gi = 0;
                 gi < prev->thm_guard_count && fn->thm_guard_count < FLOW_LEAN_THM_GUARD_MAX;
                 ++gi) {
                if ((!prev->thm_hyp_exprs[gi][0] ||
                     is_valid_scalar_lean_body_str(spec, fn, prev->thm_hyp_exprs[gi])) &&
                    is_valid_scalar_lean_body_str(spec, fn, prev->thm_conc_exprs[gi])) {
                    size_t dgi = fn->thm_guard_count++;
                    strncpy(fn->thm_names[dgi], prev->thm_names[gi], sizeof(fn->thm_names[0]) - 1);
                    strncpy(fn->thm_hyp_exprs[dgi], prev->thm_hyp_exprs[gi],
                            sizeof(fn->thm_hyp_exprs[0]) - 1);
                    strncpy(fn->thm_conc_exprs[dgi], prev->thm_conc_exprs[gi],
                            sizeof(fn->thm_conc_exprs[0]) - 1);
                }
            }
        }
        spec->lean_functions[dup_idx] = *fn;
    } else {
        spec->lean_functions[spec->lean_function_count++] = *fn;
    }
    free(fn);
}
static char *strip_lean_decl_modifiers_ex(char *line, int *out_is_private) {
    if (out_is_private)
        *out_is_private = 0;
    for (; line;) {
        if (starts_with(line, "public "))
            line = trim(line + 7);
        else if (starts_with(line, "private ")) {
            if (out_is_private)
                *out_is_private = 1;
            line = trim(line + 8);
        } else if (starts_with(line, "noncomputable "))
            line = trim(line + 14);
        else if (starts_with(line, "protected "))
            line = trim(line + 10);
        else if (starts_with(line, "@[")) {
            char *rb = strchr(line, ']');
            if (rb && rb[1])
                line = trim(rb + 1);
            else
                break;
        } else
            break;
    }
    return line;
}
static int is_lean_top_decl(const char *line) {
    if (!strcmp(line, "section") || !strcmp(line, "end") || !strcmp(line, "noncomputable section"))
        return 1;
    static const char *const P[] = {
        "def ",       "private ",   "public ",  "protected ",     "theorem ",
        "lemma ",     "abbrev ",    "axiom ",   "noncomputable ", "structure ",
        "inductive ", "namespace ", "section ", "variable ",      "end ",
        "@[",         "#print ",    "#check ",  "#eval ",         "opaque ",
        "open ",      "import ",    "module",   "deriving ",      NULL};
    for (int i = 0; P[i]; ++i)
        if (starts_with(line, P[i]))
            return 1;
    return 0;
}
static int strip_lean_comments(const char *src, char *clean_out, char *nostr_out, size_t cap,
                               char *err_msg, size_t err_size) {
    size_t j = 0;
    int block_depth = 0, in_str = 0;
    for (size_t i = 0; src[i] && j + 1 < cap;) {
        if (!in_str && src[i] == '/' && src[i + 1] == '-') {
            block_depth++;
            i += 2;
            continue;
        }
        if (!in_str && block_depth > 0 && src[i] == '-' && src[i + 1] == '/') {
            block_depth--;
            i += 2;
            if (j > 0 && clean_out[j - 1] != ' ' && clean_out[j - 1] != '\n') {
                clean_out[j] = ' ';
                nostr_out[j] = ' ';
                j++;
            }
            continue;
        }
        if (block_depth > 0) {
            if (src[i] == '\n') {
                clean_out[j] = '\n';
                nostr_out[j] = '\n';
                j++;
            }
            i++;
            continue;
        }
        if (!in_str && src[i] == '-' && src[i + 1] == '-') {
            while (src[i] && src[i] != '\n')
                i++;
            continue;
        }
        if (!in_str && (i == 0 || isspace((unsigned char)src[i - 1])) &&
            (!strncmp(src + i, "@quota", 6) || !strncmp(src + i, "@embodied", 9) ||
             !strncmp(src + i, "@reload", 7) || !strncmp(src + i, "@patchbay", 9))) {
            if (err_msg && err_size)
                snprintf(
                    err_msg, err_size,
                    "Lean audit failed: @-annotations (@quota/@embodied/@reload/@patchbay) are retired; write pure Lean 4 math and explicit C17");
            return 0;
        }
        if (src[i] == '"') {
            in_str = !in_str;
            clean_out[j] = src[i];
            nostr_out[j] = ' ';
            j++;
            i++;
            continue;
        }
        if (in_str && src[i] == '\\' && src[i + 1]) {
            clean_out[j] = src[i];
            nostr_out[j] = ' ';
            j++;
            i++;
            if (j + 1 < cap) {
                clean_out[j] = src[i];
                nostr_out[j] = ' ';
                j++;
                i++;
            }
            continue;
        }
        clean_out[j] = src[i];
        nostr_out[j] = in_str ? ' ' : src[i];
        j++;
        i++;
    }
    clean_out[j] = '\0';
    nostr_out[j] = '\0';
    if (block_depth > 0) {
        if (err_msg && err_size)
            snprintf(err_msg, err_size, "Lean audit failed: unclosed '/-' block comment");
        return 0;
    }
    for (const char *p = nostr_out; *p; ++p) {
        if ((p != nostr_out) && (isalnum((unsigned char)p[-1]) || p[-1] == '_' || p[-1] == '.'))
            continue;
        static const char *const FORBIDDEN[] = {"sorry", "admit", "sorryAx", "native_decide", NULL};
        for (int f = 0; FORBIDDEN[f]; ++f) {
            size_t flen = strlen(FORBIDDEN[f]);
            if (!strncmp(p, FORBIDDEN[f], flen) && !isalnum((unsigned char)p[flen]) &&
                p[flen] != '_' && p[flen] != '\'') {
                if (err_msg && err_size)
                    snprintf(err_msg, err_size, "Lean audit failed: forbidden '%s' token detected",
                             FORBIDDEN[f]);
                return 0;
            }
        }
        if (!strncmp(p, "axiom", 5) && isspace((unsigned char)p[5])) {
            if (err_msg && err_size)
                snprintf(err_msg, err_size,
                         "Lean audit failed: custom 'axiom' declaration forbidden");
            return 0;
        }
    }
    return 1;
}
static int parse_lean_inductive_variants(FlowLeanEnum *en, const char *text, int enforce_rt_gate) {
    for (const char *p = (en && text) ? strchr(text, '|') : NULL, *next_bar = NULL; p;
         p = next_bar) {
        next_bar = NULL;
        for (const char *q = p + 1; *q; ++q)
            if (*q == '|' && q[-1] != '|' && q[1] != '|') {
                next_bar = q;
                break;
            }
        char arm[256] = {0};
        size_t alen = next_bar ? (size_t)(next_bar - (p + 1)) : strlen(p + 1);
        if (alen >= sizeof(arm))
            alen = sizeof(arm) - 1;
        memcpy(arm, p + 1, alen);
        arm[alen] = '\0';
        char *der = strstr(arm, "deriving");
        if (der && (der == arm || isspace((unsigned char)der[-1])))
            *der = '\0';
        char *tarm = trim(arm), vname[FLOW_NAME] = {0};
        if (sscanf(tarm, "%63[^ (:=\t\r\n]", vname) == 1 && vname[0]) {
            const char *rem = tarm + strlen(vname);
            if (enforce_rt_gate &&
                (has_word_match(rem, en->name) || has_word_match(rem, "List") ||
                 has_word_match(rem, "Array") || has_word_match(rem, "ByteArray"))) {
                fprintf(
                    stderr,
                    "flowc: [Lean-RT Zero-GC Gate] rejected 'recursive/heap inductive %s' in '%s': dynamic heap allocation / closure / recursion violates hard real-time zero-GC invariants; use fixed-size 'structure' instead.\n",
                    vname, en->name);
                return 0;
            }
            int dup = 0;
            for (size_t vi = 0; vi < en->variant_count; ++vi)
                if (!strcmp(en->variants[vi], vname)) {
                    dup = 1;
                    break;
                }
            if (!dup && en->variant_count < 16)
                strncpy(en->variants[en->variant_count++], vname, FLOW_NAME - 1);
        }
    }
    return 1;
}
static int check_lean_rt_def_gate(const char *def_sig, const char *def_body) {
    size_t kw_len =
        (!def_sig) ? 0
                   : (starts_with(def_sig, "def ") ? 4 : (starts_with(def_sig, "abbrev ") ? 7 : 0));
    if (!kw_len)
        return 1;
    char fn_name[FLOW_NAME] = {0};
    if (sscanf(def_sig + kw_len, "%63[^ ({:\t\n]", fn_name) != 1 || !fn_name[0])
        return 1;
    static const char *const HEAP_TYPES[] = {"List", "Array", "ByteArray", NULL};
    for (int i = 0; HEAP_TYPES[i]; ++i)
        if (has_word_match(def_sig, HEAP_TYPES[i]) ||
            (def_body && has_word_match(def_body, HEAP_TYPES[i]))) {
            fprintf(
                stderr,
                "flowc: [Lean-RT Zero-GC Gate] rejected '%s' in '%s': dynamic heap allocation / closure / recursion violates hard real-time zero-GC invariants; use fixed-size 'structure' instead.\n",
                HEAP_TYPES[i], fn_name);
            return 0;
        }
    if (def_body) {
        const char *rej =
            (strstr(def_body, "++") != NULL)
                ? "++"
                : (((has_word_match(def_body, "fun") || strstr(def_body, "\xce\xbb") != NULL) &&
                    strstr(def_body, "=>") != NULL)
                       ? "fun =>"
                       : (has_word_match(def_body, fn_name) ? "self-recursion" : NULL));
        if (rej) {
            fprintf(
                stderr,
                "flowc: [Lean-RT Zero-GC Gate] rejected '%s' in '%s': dynamic heap allocation / closure / recursion violates hard real-time zero-GC invariants; use fixed-size 'structure' instead.\n",
                rej, fn_name);
            return 0;
        }
    }
    return 1;
}
static void extract_lean_theorem_contract(FlowSpec *spec, const LeanConstTable *consts,
                                          const char *thm_line) {
    if (!spec || !thm_line || !starts_with(thm_line, "theorem "))
        return;
    char tbuf[FLOW_LEAN_BODY_MAX] = {0};
    strncpy(tbuf, thm_line, sizeof(tbuf) - 1);
    char thm_name[FLOW_NAME] = {0};
    (void)sscanf(tbuf + 8, "%63[^ (:{\t\r\n]", thm_name);
    char *ce = strstr(tbuf, ":=");
    if (ce)
        *ce = '\0';
    int pd = 0;
    char *prop_colon = NULL;
    for (char *p = tbuf + 8; *p; ++p) {
        if (*p == '(' || *p == '{')
            pd++;
        else if (*p == ')' || *p == '}')
            pd--;
        else if (pd == 0 && *p == ':') {
            prop_colon = p;
            break;
        }
    }
    if (!prop_colon)
        return;
    char thm_params[FLOW_LEAN_PARAM_MAX][FLOW_NAME], hyps[16][256];
    size_t thm_param_cnt = 0, hyp_cnt = 0;
    for (char *p = tbuf + 8; p < prop_colon;) {
        char *lp = strchr(p, '(');
        if (!lp || lp >= prop_colon)
            break;
        char *rp = lp + 1;
        for (int d = 1; *rp && rp < prop_colon && d > 0; ++rp) {
            if (*rp == '(')
                d++;
            else if (*rp == ')') {
                d--;
                if (d == 0)
                    break;
            }
        }
        if (!*rp || rp >= prop_colon || *rp != ')')
            break;
        char grp[256] = {0};
        size_t gl = (size_t)(rp - (lp + 1));
        if (gl < sizeof(grp)) {
            memcpy(grp, lp + 1, gl);
            char *col = strchr(grp, ':');
            if (col) {
                *col = '\0';
                char *raw_ptype = trim(col + 1);
                char rpt1[FLOW_NAME] = {0}, rpt2[FLOW_NAME] = {0};
                int rnt = sscanf(raw_ptype, "%63s %63s", rpt1, rpt2);
                const char *ptype = normalize_lean_type_token(
                    (rnt >= 2 && (!strcmp(rpt1, "Option") || !strcmp(rpt1, "FlowResult") ||
                                  !strcmp(rpt1, "SpaceWithException")))
                        ? rpt2
                        : raw_ptype);
                if (is_scalar_lean_type(ptype) || find_lean_struct(spec, ptype) ||
                    find_lean_enum(spec, ptype)) {
                    for (char *tok = strtok(grp, " \t"); tok && thm_param_cnt < FLOW_LEAN_PARAM_MAX;
                         tok = strtok(NULL, " \t")) {
                        strncpy(thm_params[thm_param_cnt++], tok, FLOW_NAME - 1);
                        thm_params[thm_param_cnt - 1][FLOW_NAME - 1] = '\0';
                    }
                } else if (is_lean_erased_proof_field(raw_ptype) && hyp_cnt < 16 &&
                           strcmp(raw_ptype, "Prop") != 0 && strncmp(raw_ptype, "Prop ", 5) != 0 &&
                           strcmp(raw_ptype, "True") != 0 && strcmp(raw_ptype, "False") != 0 &&
                           (strchr(raw_ptype, '<') || strchr(raw_ptype, '>') ||
                            strchr(raw_ptype, '=') || strstr(raw_ptype, "\xe2\x89\xa4") ||
                            strstr(raw_ptype, "\xe2\x89\xa5") ||
                            strstr(raw_ptype, "\xe2\x89\xa0") ||
                            strstr(raw_ptype, "\xe2\x88\xa7") ||
                            strstr(raw_ptype, "\xe2\x88\xa8"))) {
                    strncpy(hyps[hyp_cnt++], raw_ptype, sizeof(hyps[0]) - 1);
                    hyps[hyp_cnt - 1][sizeof(hyps[0]) - 1] = '\0';
                }
            }
        }
        p = rp + 1;
    }
    if (thm_param_cnt == 0)
        return;
    char *raw_conc = trim(prop_colon + 1);
    for (int split = 1; split && *raw_conc && hyp_cnt < 16;) {
        split = 0;
        pd = 0;
        for (char *p = raw_conc; *p; ++p) {
            if (*p == '(')
                pd++;
            else if (*p == ')' && pd > 0)
                pd--;
            else if (pd == 0 && (!strncmp(p, "\xe2\x86\x92", 3) || !strncmp(p, "->", 2))) {
                size_t a_len = (!strncmp(p, "\xe2\x86\x92", 3)) ? 3u : 2u,
                       hl = (size_t)(p - raw_conc);
                if (hl > 0 && hl < sizeof(hyps[0])) {
                    memcpy(hyps[hyp_cnt], raw_conc, hl);
                    hyps[hyp_cnt][hl] = '\0';
                    trim(hyps[hyp_cnt]);
                    hyp_cnt++;
                }
                raw_conc = trim(p + a_len);
                split = 1;
                break;
            }
        }
    }
    if (!*raw_conc || strstr(raw_conc, "\xe2\x86\x94") || strstr(raw_conc, "<->") ||
        strstr(raw_conc, "\xe2\x86\x92") || strstr(raw_conc, "->"))
        return;
    for (size_t fi = spec->lean_function_count; fi > 0; --fi) {
        FlowLeanFunction *fn = &spec->lean_functions[fi - 1];
        if (fn->is_tuple_return || fn->param_count != thm_param_cnt ||
            !has_word_match(raw_conc, fn->name))
            continue;
        char call_pat[FLOW_LINE] = {0}, paren_call_pat[FLOW_LINE] = {0};
        strncpy(call_pat, fn->name, sizeof(call_pat) - 1);
        for (size_t pi = 0; pi < fn->param_count; ++pi) {
            size_t cl = strlen(call_pat);
            if (cl + 1 + strlen(fn->param_names[pi]) < sizeof(call_pat))
                snprintf(call_pat + cl, sizeof(call_pat) - cl, " %s", fn->param_names[pi]);
        }
        snprintf(paren_call_pat, sizeof(paren_call_pat), "(%s)", call_pat);
        char ccur[FLOW_LEAN_BODY_MAX] = {0}, cnext[FLOW_LEAN_BODY_MAX] = {0};
        strncpy(ccur, raw_conc, sizeof(ccur) - 1);
        for (size_t pi = 0; pi < fn->param_count; ++pi) {
            replace_substr_ex(ccur, thm_params[pi], fn->param_names[pi], 1, 0, cnext,
                              sizeof(cnext));
            strncpy(ccur, cnext, sizeof(ccur) - 1);
        }
        replace_substr_ex(ccur, paren_call_pat, "_ret", 0, 0, cnext, sizeof(cnext));
        replace_substr_ex(cnext, call_pat, "_ret", 1, 0, ccur, sizeof(ccur));
        if (!has_word_match(ccur, "_ret"))
            break;
        normalize_lean_body(ccur, cnext, sizeof(cnext));
        apply_const_table(consts, cnext, sizeof(cnext));
        strip_enum_qualifiers(spec, cnext, sizeof(cnext));
        if (!is_valid_scalar_lean_body_str(spec, fn, cnext))
            break;
        char thm_hyp[256] = {0};
        int all_hyps_valid = 1;
        for (size_t hi = 0; hi < hyp_cnt; ++hi) {
            char hcur[512] = {0}, hnext[512] = {0};
            strncpy(hcur, hyps[hi], sizeof(hcur) - 1);
            for (size_t pi = 0; pi < fn->param_count; ++pi) {
                replace_substr_ex(hcur, thm_params[pi], fn->param_names[pi], 1, 0, hnext,
                                  sizeof(hnext));
                strncpy(hcur, hnext, sizeof(hcur) - 1);
            }
            normalize_lean_body(hcur, hnext, sizeof(hnext));
            apply_const_table(consts, hnext, sizeof(hnext));
            strip_enum_qualifiers(spec, hnext, sizeof(hnext));
            if (!is_valid_scalar_lean_body_str(spec, fn, hnext)) {
                all_hyps_valid = 0;
                break;
            }
            append_contract_clause(thm_hyp, sizeof(thm_hyp), hnext);
        }
        if (!all_hyps_valid)
            break;
        int dup_g = 0;
        for (size_t gi = 0; gi < fn->thm_guard_count; ++gi) {
            if (!strcmp(fn->thm_hyp_exprs[gi], thm_hyp) && !strcmp(fn->thm_conc_exprs[gi], cnext)) {
                dup_g = 1;
                break;
            }
        }
        if (!dup_g && fn->thm_guard_count < FLOW_LEAN_THM_GUARD_MAX) {
            size_t gi = fn->thm_guard_count++;
            strncpy(fn->thm_names[gi], thm_name, sizeof(fn->thm_names[0]) - 1);
            strncpy(fn->thm_hyp_exprs[gi], thm_hyp, sizeof(fn->thm_hyp_exprs[0]) - 1);
            strncpy(fn->thm_conc_exprs[gi], cnext, sizeof(fn->thm_conc_exprs[0]) - 1);
        }
        if (thm_hyp[0]) {
            char imp_clause[512] = {0};
            snprintf(imp_clause, sizeof(imp_clause), "!(%s) || (%s)", thm_hyp, cnext);
            append_contract_clause(fn->postcond_expr, sizeof(fn->postcond_expr), imp_clause);
        } else {
            append_contract_clause(fn->postcond_expr, sizeof(fn->postcond_expr), cnext);
        }
        break;
    }
}
static int should_join_def_body_with_space(const char *def_body, const char *line, int line_indent,
                                           int *def_let_indent) {
    size_t bl = strlen(def_body);
    if (bl == 0) {
        if (starts_with(line, "let "))
            *def_let_indent = line_indent;
        return 0;
    }
    int pd = 0, bd = 0, if_d = 0;
    for (size_t i = 0; i < bl; ++i) {
        if (def_body[i] == '(')
            pd++;
        else if (def_body[i] == ')' && pd > 0)
            pd--;
        else if (def_body[i] == '{')
            bd++;
        else if (def_body[i] == '}' && bd > 0)
            bd--;
        else if (!strncmp(def_body + i, "if", 2) &&
                 (i == 0 || isspace((unsigned char)def_body[i - 1]) || def_body[i - 1] == '(') &&
                 (isspace((unsigned char)def_body[i + 2]) || def_body[i + 2] == '('))
            if_d++;
        else if (!strncmp(def_body + i, "else", 4) &&
                 (i == 0 || isspace((unsigned char)def_body[i - 1]) || def_body[i - 1] == ')') &&
                 (def_body[i + 4] == '\0' || isspace((unsigned char)def_body[i + 4]) ||
                  def_body[i + 4] == '(')) {
            if (if_d > 0)
                if_d--;
        }
    }
    if (pd > 0 || bd > 0 || if_d > 0)
        return 1;
    char lc = def_body[bl - 1];
    if (lc == '+' || lc == '-' || lc == '*' || lc == '/' || lc == '&' || lc == '|' || lc == '^' ||
        lc == '<' || lc == '>' || lc == '=' || lc == ',')
        return 1;
    if ((bl >= 2 && !strcmp(def_body + bl - 2, "=>")) ||
        (bl >= 3 && !strcmp(def_body + bl - 3, " in")) ||
        (bl >= 5 && (!strcmp(def_body + bl - 5, " then") || !strcmp(def_body + bl - 5, " else") ||
                     !strcmp(def_body + bl - 5, " with")))) {
        if (starts_with(line, "let "))
            *def_let_indent = line_indent;
        return 1;
    }
    if (!strcmp(line, "in") || starts_with(line, "in ") || starts_with(line, "then ") ||
        !strcmp(line, "then") || starts_with(line, "else ") || !strcmp(line, "else") ||
        line[0] == '+' || line[0] == '*' || line[0] == '/' || line[0] == '|')
        return 1;
    if (starts_with(line, "let ")) {
        *def_let_indent = line_indent;
        return 0;
    }
    if (*def_let_indent >= 0) {
        if (line_indent > *def_let_indent)
            return 1;
        *def_let_indent = -1;
        return 0;
    }
    return 1;
}
int flow_verify_lean_theorems(const char *lean_source, char *err_msg, size_t err_size) {
    if (err_msg && err_size)
        err_msg[0] = '\0';
    if (!lean_source || !lean_source[0])
        return 0;
    size_t slen = strlen(lean_source);
    char *clean = (char *)calloc(slen + 1, 1), *nostr = (char *)calloc(slen + 1, 1);
    if (!clean || !nostr) {
        free(clean);
        free(nostr);
        return 0;
    }
    int ok = strip_lean_comments(lean_source, clean, nostr, slen + 1, err_msg, err_size);
    free(clean);
    free(nostr);
    return ok;
}
int parse_spec(FILE *input, FlowSpec *spec) {
    if (!input || !spec)
        return 0;
    fseek(input, 0, SEEK_END);
    long fsz = ftell(input);
    rewind(input);
    if (fsz <= 0 || fsz > 4 * 1024 * 1024)
        return 0;
    char *full_src = (char *)calloc((size_t)fsz + 1, 1);
    if (!full_src)
        return 0;
    size_t nread = fread(full_src, 1, (size_t)fsz, input);
    full_src[nread] = '\0';
    char *clean_src = (char *)calloc(nread + 1, 1), *nostr_src = (char *)calloc(nread + 1, 1);
    if (!clean_src || !nostr_src) {
        free(full_src);
        free(clean_src);
        free(nostr_src);
        return 0;
    }
    char err_msg[256] = {0};
    if (!strip_lean_comments(full_src, clean_src, nostr_src, nread + 1, err_msg, sizeof(err_msg))) {
        if (err_msg[0])
            fprintf(stderr, "flowc: %s\n", err_msg);
        free(full_src);
        free(clean_src);
        free(nostr_src);
        return 0;
    }
    free(full_src);
    free(nostr_src);
    LeanConstTable *consts = (LeanConstTable *)calloc(1, sizeof(LeanConstTable));
    if (!consts) {
        free(clean_src);
        return 0;
    }
    char sym[FLOW_NAME] = {0}, def_sig[FLOW_LEAN_BODY_MAX] = {0},
         def_body[FLOW_LEAN_BODY_MAX] = {0}, thm_buf[FLOW_LEAN_BODY_MAX] = {0};
    size_t struct_bytes = 0;
    int saw_lean = 0, in_struct = 0, in_def = 0, def_priv = 0, def_let_indent = -1, active_thm = -1,
        is_ext_lib = 0;
    FlowLeanStruct *cur_st = NULL;
    FlowLeanEnum *cur_en = NULL;
    memset(spec, 0, sizeof(*spec));
    strncpy(spec->flow_name, "lean_module", FLOW_NAME - 1);
    spec->max_count = 1024;
    spec->input_stride_bytes = 8;
    spec->input_align_bytes = 8;
    spec->state_schema_version = 1;
    spec->deterministic = 1;
    spec->lean_certified = 1;
    spec->bounded = 1;
    spec->memory_mb = 64;
    spec->float_abi_ieee64 = 1;
    char *saveptr = NULL;
    for (char *raw_line = strtok_r(clean_src, "\n", &saveptr); raw_line;
         raw_line = strtok_r(NULL, "\n", &saveptr)) {
        int line_indent = 0;
        for (const char *rp = raw_line; *rp == ' ' || *rp == '\t'; ++rp)
            line_indent += (*rp == '\t') ? 2 : 1;
        char *line = trim(raw_line);
        if (!*line)
            continue;
        if (!strcmp(line, "module") || starts_with(line, "module "))
            is_ext_lib = 1;
        if (starts_with(line, "noncomputable ")) {
            saw_lean = 1;
            if (!strcmp(line, "noncomputable section") ||
                starts_with(line, "noncomputable section "))
                spec->float_abi_ieee64 = 1;
        }
        if (strstr(line, "\xe2\x84\x9d") != NULL)
            spec->float_abi_ieee64 = 1;
        int line_priv = 0;
        line = strip_lean_decl_modifiers_ex(line, &line_priv);
        if (!*line)
            continue;
        if (!strcmp(line, "section") || starts_with(line, "section ")) {
            saw_lean = 1;
            continue;
        }
        if (in_def && is_lean_top_decl(line)) {
            if (in_def == 2) {
                if (!is_ext_lib && !check_lean_rt_def_gate(def_sig, def_body)) {
                    free(consts);
                    free(clean_src);
                    return 0;
                }
                try_add_lean_function(spec, consts, def_sig, def_body, def_priv);
            }
            in_def = 0;
            def_priv = 0;
            def_let_indent = -1;
            def_sig[0] = '\0';
            def_body[0] = '\0';
        }
        if (active_thm >= 0 && is_lean_top_decl(line)) {
            if (thm_buf[0])
                extract_lean_theorem_contract(spec, consts, thm_buf);
            active_thm = -1;
            thm_buf[0] = '\0';
        }
        if (cur_en && is_lean_top_decl(line))
            cur_en = NULL;
        if (in_def == 1) {
            size_t sl = strlen(def_sig);
            if (sl + 1 + strlen(line) < sizeof(def_sig)) {
                def_sig[sl] = ' ';
                strncpy(def_sig + sl + 1, line, sizeof(def_sig) - sl - 2);
            }
            char *ce = strstr(def_sig, ":=");
            if (ce) {
                *ce = '\0';
                strncpy(def_body, trim(ce + 2), sizeof(def_body) - 1);
                def_let_indent = starts_with(def_body, "let ") ? line_indent : -1;
                in_def = 2;
            }
        } else if (in_def == 2) {
            size_t bl = strlen(def_body);
            char sep_c =
                should_join_def_body_with_space(def_body, line, line_indent, &def_let_indent)
                    ? ' '
                    : '\n';
            if (bl + 1 + strlen(line) < sizeof(def_body)) {
                if (bl > 0)
                    def_body[bl++] = sep_c;
                strncpy(def_body + bl, line, sizeof(def_body) - bl - 1);
            }
        } else if (active_thm >= 0 && !starts_with(line, "theorem ")) {
            size_t tl = strlen(thm_buf);
            if (tl + 1 + strlen(line) < sizeof(thm_buf)) {
                thm_buf[tl] = ' ';
                strncpy(thm_buf + tl + 1, line, sizeof(thm_buf) - tl - 2);
            }
        }
        if (cur_en && line[0] == '|') {
            if (!parse_lean_inductive_variants(cur_en, line, !is_ext_lib)) {
                free(consts);
                free(clean_src);
                return 0;
            }
            continue;
        }
        if (in_struct) {
            char fn[FLOW_NAME] = {0}, ft[FLOW_LINE] = {0};
            if (is_lean_top_decl(line)) {
                in_struct = 0;
                cur_st = NULL;
            } else if (sscanf(line, "%63s : %511[^\r\n]", fn, ft) == 2) {
                char *ce_f = strstr(ft, ":=");
                if (ce_f)
                    *ce_f = '\0';
                char *tft = trim(ft);
                if (is_lean_erased_proof_field(tft)) {
                    if (cur_st && cur_st->invariant_count < FLOW_LEAN_STRUCT_INV_MAX &&
                        strcmp(tft, "Prop") != 0 && strncmp(tft, "Prop ", 5) != 0 &&
                        strcmp(tft, "True") != 0 && strcmp(tft, "False") != 0 &&
                        (strchr(tft, '<') || strchr(tft, '>') || strchr(tft, '=') ||
                         strstr(tft, "\xe2\x89\xa4") || strstr(tft, "\xe2\x89\xa5") ||
                         strstr(tft, "\xe2\x89\xa0") || strstr(tft, "\xe2\x88\xa7") ||
                         strstr(tft, "\xe2\x88\xa8"))) {
                        char norm_inv[128] = {0};
                        normalize_lean_body(tft, norm_inv, sizeof(norm_inv));
                        apply_const_table(consts, norm_inv, sizeof(norm_inv));
                        if (norm_inv[0])
                            strncpy(cur_st->invariants[cur_st->invariant_count++], norm_inv,
                                    sizeof(cur_st->invariants[0]) - 1);
                    }
                    continue;
                }
                if (!is_ext_lib && (has_word_match(tft, "List") || has_word_match(tft, "Array") ||
                                    has_word_match(tft, "ByteArray"))) {
                    fprintf(
                        stderr,
                        "flowc: [Lean-RT Zero-GC Gate] rejected '%s' in '%s.%s': dynamic heap allocation / closure / recursion violates hard real-time zero-GC invariants; use fixed-size 'structure' instead.\n",
                        tft, cur_st ? cur_st->name : "structure", fn);
                    free(consts);
                    free(clean_src);
                    return 0;
                }
                char ft_tok[FLOW_NAME] = {0};
                sscanf(tft, "%63s", ft_tok);
                add_lean_struct_field(spec, cur_st, fn, normalize_lean_type_token(ft_tok), 1,
                                      &struct_bytes);
                continue;
            }
        }
        if (sscanf(line, "structure %63s where", sym) == 1) {
            saw_lean = 1;
            in_struct = 1;
            cur_st = NULL;
            if (spec->lean_struct_count < FLOW_LEAN_STRUCT_MAX) {
                cur_st = &spec->lean_structs[spec->lean_struct_count++];
                memset(cur_st, 0, sizeof(*cur_st));
                strncpy(cur_st->name, sym, FLOW_NAME - 1);
                cur_st->align_bytes = 8;
            }
        } else if (sscanf(line, "inductive %63s", sym) == 1) {
            saw_lean = 1;
            if (!strcmp(sym, "Option") || !strcmp(sym, "FlowResult") ||
                !strcmp(sym, "SpaceWithException")) {
                cur_en = NULL;
            } else if (spec->lean_enum_count < FLOW_LEAN_ENUM_MAX) {
                cur_en = &spec->lean_enums[spec->lean_enum_count++];
                memset(cur_en, 0, sizeof(*cur_en));
                strncpy(cur_en->name, sym, FLOW_NAME - 1);
                if (!parse_lean_inductive_variants(cur_en, line, !is_ext_lib)) {
                    free(consts);
                    free(clean_src);
                    return 0;
                }
            }
        } else if (sscanf(line, "namespace %63s", sym) == 1) {
            saw_lean = 1;
            const char *leaf = strrchr(sym, '.');
            strncpy(spec->flow_name, leaf ? leaf + 1 : sym, FLOW_NAME - 1);
        } else if ((starts_with(line, "def ") || starts_with(line, "abbrev ")) &&
                   !strstr(line, ": Prop") && !strstr(line, ": Type")) {
            size_t kw_len = starts_with(line, "def ") ? 4 : 7;
            char fn[FLOW_NAME] = {0};
            if (sscanf(line + kw_len, "%63[^ ({:\t]", fn) == 1 && fn[0]) {
                saw_lean = 1;
                def_priv = line_priv;
                char *ce = strstr(line, ":=");
                if (ce) {
                    size_t slen_d = (size_t)(ce - line);
                    if (slen_d >= sizeof(def_sig))
                        slen_d = sizeof(def_sig) - 1;
                    memcpy(def_sig, line, slen_d);
                    def_sig[slen_d] = '\0';
                    strncpy(def_body, trim(ce + 2), sizeof(def_body) - 1);
                    def_let_indent = starts_with(def_body, "let ") ? (line_indent + 2) : -1;
                    in_def = 2;
                } else {
                    strncpy(def_sig, line, sizeof(def_sig) - 1);
                    def_body[0] = '\0';
                    def_let_indent = -1;
                    in_def = 1;
                }
            }
        } else if (starts_with(line, "theorem ")) {
            saw_lean = 1;
            strncpy(thm_buf, line, sizeof(thm_buf) - 1);
            thm_buf[sizeof(thm_buf) - 1] = '\0';
            if (sscanf(line + 8, "%63[^ (:{\t]", sym) == 1 &&
                spec->lean_theorem_count < FLOW_LEAN_THEOREM_MAX) {
                active_thm = (int)spec->lean_theorem_count++;
                strncpy(spec->lean_theorems[active_thm].name, sym, FLOW_NAME - 1);
                spec->lean_theorems[active_thm].anchored_fn[0] = '\0';
            }
            if (active_thm >= 0 && !spec->lean_theorems[active_thm].anchored_fn[0])
                for (size_t fi = spec->lean_function_count; fi > 0; --fi)
                    if (has_word_match(line, spec->lean_functions[fi - 1].name)) {
                        strncpy(spec->lean_theorems[active_thm].anchored_fn,
                                spec->lean_functions[fi - 1].name, FLOW_NAME - 1);
                        break;
                    }
        } else if (active_thm >= 0 && !spec->lean_theorems[active_thm].anchored_fn[0]) {
            for (size_t fi = spec->lean_function_count; fi > 0; --fi)
                if (has_word_match(line, spec->lean_functions[fi - 1].name)) {
                    strncpy(spec->lean_theorems[active_thm].anchored_fn,
                            spec->lean_functions[fi - 1].name, FLOW_NAME - 1);
                    break;
                }
        }
    }
    free(clean_src);
    if (in_def == 2) {
        if (!is_ext_lib && !check_lean_rt_def_gate(def_sig, def_body)) {
            free(consts);
            return 0;
        }
        try_add_lean_function(spec, consts, def_sig, def_body, def_priv);
    }
    if (active_thm >= 0 && thm_buf[0])
        extract_lean_theorem_contract(spec, consts, thm_buf);
    free(consts);
    if (struct_bytes > 0)
        spec->input_stride_bytes = (struct_bytes + 7u) & ~7u;
    return saw_lean;
}
const char *flow_lean_scalar_to_c_type(const char *t) {
    if (!t)
        return "uint64_t";
#define FLOW_MAP_SCALAR_C(lean_name, norm_lean, c_type, sz, mode) \
    if (!strcmp(t, lean_name))                                    \
        return c_type;
    FLOW_LEAN_SCALAR_TYPES_X(FLOW_MAP_SCALAR_C)
#undef FLOW_MAP_SCALAR_C
    return "uint64_t";
}
FlowLeanExprMode flow_c_type_expr_mode(const char *c_type, FlowLeanExprMode fallback) {
    if (!c_type)
        return fallback;
    if (!strcmp(c_type, "FlowJetScalar2"))
        return FLOW_EXPR_JET2;
#define FLOW_MAP_CTYPE_MODE(lean_name, norm_lean, ct_str, sz, mode) \
    if ((mode) != FLOW_EXPR_NAT && !strcmp(c_type, ct_str))         \
        return (mode);
    FLOW_LEAN_SCALAR_TYPES_X(FLOW_MAP_CTYPE_MODE)
#undef FLOW_MAP_CTYPE_MODE
    return fallback;
}
static FlowLeanExprMode compute_lean_fn_expr_mode(const FlowLeanFunction *fn) {
    if (!fn)
        return FLOW_EXPR_NAT;
    if (!strcmp(fn->return_type, "Float") || !strcmp(fn->return_type, "Float32"))
        return FLOW_EXPR_FLOAT;
    for (size_t i = 0; i < fn->param_count; ++i)
        if (!strcmp(fn->param_types[i], "Float") || !strcmp(fn->param_types[i], "Float32"))
            return FLOW_EXPR_FLOAT;
    if (!strcmp(fn->return_type, "Int") || !strcmp(fn->return_type, "Int64") ||
        !strcmp(fn->return_type, "Int32") || !strcmp(fn->return_type, "Int16") ||
        !strcmp(fn->return_type, "Int8"))
        return FLOW_EXPR_INT;
    for (size_t i = 0; i < fn->param_count; ++i)
        if (!strcmp(fn->param_types[i], "Int") || !strcmp(fn->param_types[i], "Int64") ||
            !strcmp(fn->param_types[i], "Int32") || !strcmp(fn->param_types[i], "Int16") ||
            !strcmp(fn->param_types[i], "Int8"))
            return FLOW_EXPR_INT;
    if (!strcmp(fn->return_type, "UInt64") || !strcmp(fn->return_type, "UInt32") ||
        !strcmp(fn->return_type, "UInt16") || !strcmp(fn->return_type, "UInt8"))
        return FLOW_EXPR_UINT;
    return FLOW_EXPR_NAT;
}
const FlowLeanStruct *flow_ir_find_struct(const SemanticIR *ir, const char *name) {
    if (!ir || !name || !name[0])
        return NULL;
    for (size_t i = 0; i < ir->lean_struct_count; ++i)
        if (!strcmp(ir->lean_structs[i].name, name))
            return &ir->lean_structs[i];
    return NULL;
}
const FlowLeanEnum *flow_ir_find_enum(const SemanticIR *ir, const char *name) {
    if (!ir || !name || !name[0])
        return NULL;
    for (size_t i = 0; i < ir->lean_enum_count; ++i)
        if (!strcmp(ir->lean_enums[i].name, name))
            return &ir->lean_enums[i];
    return NULL;
}
const char *flow_ir_type_to_c(const SemanticIR *ir, const char *t) {
    if (!t)
        return "uint64_t";
    if (flow_ir_find_struct(ir, t) || flow_ir_find_enum(ir, t))
        return t;
    return flow_lean_scalar_to_c_type(t);
}
static int ir_fn_is_jet2_eligible_depth(const SemanticIR *ir, const FlowLeanFunction *fn,
                                        int depth) {
    if (depth > 4 || !fn || fn->is_struct_transform || fn->is_tuple_return ||
        fn->is_result_return || fn->param_count == 0)
        return 0;
    if (strcmp(fn->return_type, "Float") != 0 && strcmp(fn->return_type, "Float32") != 0)
        return 0;
    if (strstr(fn->body, "let (") != NULL || strstr(fn->body, "match ") != NULL)
        return 0;
    for (size_t pi = 0; pi < fn->param_count; ++pi) {
        if (fn->param_is_result[pi] || flow_ir_find_struct(ir, fn->param_types[pi]))
            return 0;
        if (strcmp(fn->param_types[pi], "Float") != 0 &&
            strcmp(fn->param_types[pi], "Float32") != 0)
            return 0;
    }
    if (ir) {
        for (size_t k = 0; k < ir->lean_function_count; ++k) {
            if (&ir->lean_functions[k] == fn)
                continue;
            if (has_word_match(fn->body, ir->lean_functions[k].name) &&
                !ir_fn_is_jet2_eligible_depth(ir, &ir->lean_functions[k], depth + 1))
                return 0;
        }
    }
    return 1;
}
void flow_ir_compute_fn_variants(SemanticIR *ir) {
    if (!ir)
        return;
    for (size_t fi = 0; fi < ir->lean_function_count; ++fi) {
        FlowLeanFunction *fn = &ir->lean_functions[fi];
        uint32_t mask = 0;
        if (fn->name[0]) {
            if (!fn->is_private)
                mask |= FLOW_FN_VAR_PRIMARY;
            if (ir_fn_is_jet2_eligible_depth(ir, fn, 0))
                mask |= FLOW_FN_VAR_JET2;
            if (!fn->is_private && strncmp(fn->name, "lean_", 5) != 0) {
                char cand[FLOW_NAME + 8];
                snprintf(cand, sizeof(cand), "lean_%s", fn->name);
                int has_conflict = 0;
                for (size_t j = 0; j < ir->lean_function_count; ++j) {
                    if (!strcmp(ir->lean_functions[j].name, cand)) {
                        has_conflict = 1;
                        break;
                    }
                }
                if (!has_conflict) {
                    mask |= FLOW_FN_VAR_LEAN;
                    if (!fn->is_tuple_return) {
                        mask |= FLOW_FN_VAR_CERTIFIED | FLOW_FN_VAR_TRY;
                        if (!strcmp(flow_ir_type_to_c(ir, fn->return_type), "double"))
                            mask |= FLOW_FN_VAR_BOUNDARY;
                    }
                    if (!fn->is_result_return && !fn->is_tuple_return && fn->param_count > 0) {
                        const FlowLeanStruct *out_st = flow_ir_find_struct(ir, fn->return_type);
                        if (!(out_st && out_st->field_count == 0)) {
                            int has_st_param = 0, bad_st = 0;
                            for (size_t pi = 0; pi < fn->param_count; ++pi) {
                                const FlowLeanStruct *in_st =
                                    flow_ir_find_struct(ir, fn->param_types[pi]);
                                if (in_st) {
                                    if (in_st->field_count == 0) {
                                        bad_st = 1;
                                        break;
                                    }
                                    has_st_param = 1;
                                }
                            }
                            if (has_st_param && !bad_st)
                                mask |= FLOW_FN_VAR_SOA64;
                        }
                    }
                }
            }
        }
        fn->expr_mode = (uint8_t)compute_lean_fn_expr_mode(fn);
        fn->variant_mask = mask;
    }
}
static void add_fact(SemanticIR *ir, FlowFactKind kind, const char *detail) {
    if (ir->fact_count >= FLOW_NODE_MAX)
        return;
    ir->facts[ir->fact_count].kind = kind;
    strncpy(ir->facts[ir->fact_count++].detail, detail, FLOW_NAME - 1);
    if (kind == FLOW_FACT_HOLE)
        ++ir->hole_count;
    else if (kind == FLOW_FACT_CONSTRAINT)
        ++ir->constraint_count;
}
void lower_to_ir(const FlowSpec *spec, SemanticIR *ir) {
    memset(ir, 0, sizeof(*ir));
    memcpy(ir, spec, offsetof(FlowSpec, constraint_count));
    ir->declared_constraint_count = spec->constraint_count;
    ir->fact_unordered = 1;
    ir->fact_range_proven = ir->input_max_count > 0;
    ir->fact_mutability_read_only = !ir->state_shared;
    if (ir->state_shared)
        add_fact(ir, FLOW_FACT_SHARED_STATE, "shared");
    if (ir->state_bounded)
        add_fact(ir, FLOW_FACT_BOUNDED, "bounded");
    if (ir->fact_range_proven)
        add_fact(ir, FLOW_FACT_RANGE, "input_max_count");
    if (ir->fact_mutability_read_only)
        add_fact(ir, FLOW_FACT_MUTABILITY, "read_only");
    if (ir->fact_deterministic)
        add_fact(ir, FLOW_FACT_DETERMINISM, "deterministic");
    if (ir->fact_lean_certified)
        add_fact(ir, FLOW_FACT_LEAN_CERTIFIED, "lean_certified");
    if (ir->memory_limit_mb > 0 || ir->arena_limit_bytes > 0 || ir->sla_latency_ns > 0 ||
        ir->fact_deterministic)
        add_fact(ir, FLOW_FACT_CONSTRAINT, "require");
    flow_ir_compute_fn_variants(ir);
}
