#include "llvm_lifter.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

/* ============================================================================
 * 1. Zero-UB Math Helpers via Native 128-bit Arithmetic
 * ============================================================================ */

#if defined(__SIZEOF_INT128__)
typedef __int128_t flow_i128;
typedef __uint128_t flow_u128;
#else
#error "64-bit host with __int128_t support is required for FLOW LLVM Lifter."
#endif

static inline int64_t flow_safe_clamp_i128_to_i64(flow_i128 v) {
    if (v > (flow_i128)INT64_MAX)
        return INT64_MAX;
    if (v < (flow_i128)INT64_MIN)
        return INT64_MIN;
    return (int64_t)v;
}

static inline __attribute__((unused)) int64_t flow_safe_add_i64(int64_t a, int64_t b) {
    flow_i128 res = (flow_i128)a + (flow_i128)b;
    return flow_safe_clamp_i128_to_i64(res);
}

static inline __attribute__((unused)) int64_t flow_safe_sub_i64(int64_t a, int64_t b) {
    flow_i128 res = (flow_i128)a - (flow_i128)b;
    return flow_safe_clamp_i128_to_i64(res);
}

static inline __attribute__((unused)) int64_t flow_safe_mul_i64(int64_t a, int64_t b) {
    flow_i128 res = (flow_i128)a * (flow_i128)b;
    return flow_safe_clamp_i128_to_i64(res);
}

static inline int64_t flow_safe_sdiv_i64(int64_t a, int64_t b) {
    if (b == 0)
        return 0;
    if (a == INT64_MIN && b == -1)
        return INT64_MAX;
    return a / b;
}

static inline uint64_t flow_safe_udiv_u64(uint64_t a, uint64_t b) {
    if (b == 0)
        return 0;
    return a / b;
}

static inline int64_t flow_bit_smin(uint32_t w) {
    if (w == 0)
        return 0;
    if (w >= 64)
        return INT64_MIN;
    return -((int64_t)1 << (w - 1));
}

static inline int64_t flow_bit_smax(uint32_t w) {
    if (w == 0)
        return 0;
    if (w >= 64)
        return INT64_MAX;
    return (((int64_t)1 << (w - 1)) - 1);
}

static inline uint64_t flow_bit_umax(uint32_t w) {
    if (w == 0)
        return 0ULL;
    if (w >= 64)
        return UINT64_MAX;
    return (((uint64_t)1 << w) - 1ULL);
}

static inline uint64_t flow_bit_mask(uint32_t w) {
    return flow_bit_umax(w);
}

static inline int64_t flow_sign_extend(uint64_t val, uint32_t w) {
    if (w >= 64 || w == 0)
        return (int64_t)val;
    uint64_t m = (uint64_t)1 << (w - 1);
    return (int64_t)((val ^ m) - m);
}

static inline int flow_clz64(uint64_t x) {
    return x ? __builtin_clzll(x) : 64;
}

static inline int flow_ctz64(uint64_t x) {
    return x ? __builtin_ctzll(x) : 64;
}

/* ============================================================================
 * 2. String Manipulation & Parsing Helpers
 * ============================================================================ */

static void trim_inplace(char *s) {
    if (!s)
        return;
    char *p = s;
    while (*p && isspace((unsigned char)*p))
        p++;
    if (p != s)
        memmove(s, p, strlen(p) + 1);
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }
}

static void strip_comments_and_metadata(char *s) {
    if (!s)
        return;
    char *p = strchr(s, ';');
    if (p)
        *p = '\0';
    p = strstr(s, ", !");
    if (p)
        *p = '\0';
    p = strstr(s, " !dbg");
    if (p)
        *p = '\0';
    p = strstr(s, " !tbaa");
    if (p)
        *p = '\0';
    trim_inplace(s);
}

static uint32_t parse_llvm_type_bits(const char *tok) {
    if (!tok)
        return 32;
    while (*tok && isspace((unsigned char)*tok))
        tok++;
    if (*tok == 'i' && isdigit((unsigned char)tok[1])) {
        return (uint32_t)strtoul(tok + 1, NULL, 10);
    }
    if (!strcmp(tok, "void"))
        return 0;
    if (!strcmp(tok, "ptr") || strchr(tok, '*'))
        return 64;
    return 32;
}

static void clean_identifier(const char *src, char *dst, size_t dst_sz) {
    if (!src || !dst || dst_sz == 0)
        return;
    while (*src && isspace((unsigned char)*src))
        src++;
    size_t i = 0;
    while (*src && !isspace((unsigned char)*src) && *src != ',' && *src != ')' && *src != ']' &&
           i + 1 < dst_sz) {
        dst[i++] = *src++;
    }
    dst[i] = '\0';
}

static void consume_llvm_modifiers(char **args_out, FlowLLVMInst *inst) {
    if (!args_out || !*args_out)
        return;
    static const struct {
        const char *name;
        size_t len;
        int is_nsw;
        int is_nuw;
    } MOD_TABLE[] = {{"nsw", 3, 1, 0},      {"nuw", 3, 0, 1},      {"exact", 5, 0, 0},
                     {"disjoint", 8, 0, 0}, {"samesign", 8, 0, 0}, {"nneg", 4, 0, 0},
                     {"fast", 4, 0, 0},     {"nnan", 4, 0, 0},     {"ninf", 4, 0, 0},
                     {"nsz", 3, 0, 0},      {"arcp", 4, 0, 0},     {"contract", 8, 0, 0},
                     {"afn", 3, 0, 0},      {"reassoc", 7, 0, 0},  {NULL, 0, 0, 0}};

    char *p = *args_out;
    while (*p) {
        while (*p && isspace((unsigned char)*p))
            p++;
        int matched = 0;
        for (size_t i = 0; MOD_TABLE[i].name != NULL; ++i) {
            size_t mlen = MOD_TABLE[i].len;
            if (!strncmp(p, MOD_TABLE[i].name, mlen) && isspace((unsigned char)p[mlen])) {
                if (inst && MOD_TABLE[i].is_nsw)
                    inst->has_nsw = 1;
                if (inst && MOD_TABLE[i].is_nuw)
                    inst->has_nuw = 1;
                p += mlen;
                while (*p && isspace((unsigned char)*p))
                    p++;
                matched = 1;
                break;
            }
        }
        if (!matched)
            break;
    }
    *args_out = p;
}

static uint32_t parse_llvm_param_type_bits(const char *tok_start, const char *tok_end) {
    if (!tok_start || !tok_end || tok_start >= tok_end)
        return 32;

    const char *p = tok_start;
    while (p < tok_end) {
        while (p < tok_end && isspace((unsigned char)*p))
            p++;
        if (p >= tok_end)
            break;

        const char *t_start = p;
        while (p < tok_end && !isspace((unsigned char)*p))
            p++;
        const char *t_end = p;
        size_t t_len = (size_t)(t_end - t_start);

        /* Match integer type: i[0-9]+ */
        if (t_len >= 2 && t_start[0] == 'i' && isdigit((unsigned char)t_start[1])) {
            int all_digits = 1;
            for (size_t k = 1; k < t_len; ++k) {
                if (!isdigit((unsigned char)t_start[k])) {
                    all_digits = 0;
                    break;
                }
            }
            if (all_digits) {
                uint32_t bits = (uint32_t)strtoul(t_start + 1, NULL, 10);
                if (bits > 0)
                    return bits;
            }
        }

        /* Match pointer type: ptr or containing '*' */
        if (t_len == 3 && !strncmp(t_start, "ptr", 3))
            return 64;
        if (memchr(t_start, '*', t_len) != NULL)
            return 64;
    }

    return 32;
}

static const char *strip_leading_sigils(const char *name) {
    if (!name)
        return "";
    while (*name == '%' || *name == '@')
        name++;
    return name;
}

static int flow_ident_matches(const char *a, const char *b) {
    if (!a || !b)
        return 0;
    return strcmp(strip_leading_sigils(a), strip_leading_sigils(b)) == 0;
}

const char *flow_llvm_opcode_name(FlowLLVMOpcode op) {
    switch (op) {
    case FLOW_LLVM_OP_ADD:
        return "add";
    case FLOW_LLVM_OP_SUB:
        return "sub";
    case FLOW_LLVM_OP_MUL:
        return "mul";
    case FLOW_LLVM_OP_UDIV:
        return "udiv";
    case FLOW_LLVM_OP_SDIV:
        return "sdiv";
    case FLOW_LLVM_OP_UREM:
        return "urem";
    case FLOW_LLVM_OP_SREM:
        return "srem";
    case FLOW_LLVM_OP_SHL:
        return "shl";
    case FLOW_LLVM_OP_LSHR:
        return "lshr";
    case FLOW_LLVM_OP_ASHR:
        return "ashr";
    case FLOW_LLVM_OP_AND:
        return "and";
    case FLOW_LLVM_OP_OR:
        return "or";
    case FLOW_LLVM_OP_XOR:
        return "xor";
    case FLOW_LLVM_OP_ICMP:
        return "icmp";
    case FLOW_LLVM_OP_SELECT:
        return "select";
    case FLOW_LLVM_OP_BR:
        return "br";
    case FLOW_LLVM_OP_PHI:
        return "phi";
    case FLOW_LLVM_OP_TRUNC:
        return "trunc";
    case FLOW_LLVM_OP_ZEXT:
        return "zext";
    case FLOW_LLVM_OP_SEXT:
        return "sext";
    case FLOW_LLVM_OP_CALL:
        return "call";
    case FLOW_LLVM_OP_RET:
        return "ret";
    default:
        return "unknown";
    }
}

FlowLLVMOpcode flow_llvm_parse_opcode(const char *str) {
    if (!str)
        return (FlowLLVMOpcode)-1;
    if (!strcmp(str, "add"))
        return FLOW_LLVM_OP_ADD;
    if (!strcmp(str, "sub"))
        return FLOW_LLVM_OP_SUB;
    if (!strcmp(str, "mul"))
        return FLOW_LLVM_OP_MUL;
    if (!strcmp(str, "udiv"))
        return FLOW_LLVM_OP_UDIV;
    if (!strcmp(str, "sdiv"))
        return FLOW_LLVM_OP_SDIV;
    if (!strcmp(str, "urem"))
        return FLOW_LLVM_OP_UREM;
    if (!strcmp(str, "srem"))
        return FLOW_LLVM_OP_SREM;
    if (!strcmp(str, "shl"))
        return FLOW_LLVM_OP_SHL;
    if (!strcmp(str, "lshr"))
        return FLOW_LLVM_OP_LSHR;
    if (!strcmp(str, "ashr"))
        return FLOW_LLVM_OP_ASHR;
    if (!strcmp(str, "and"))
        return FLOW_LLVM_OP_AND;
    if (!strcmp(str, "or"))
        return FLOW_LLVM_OP_OR;
    if (!strcmp(str, "xor"))
        return FLOW_LLVM_OP_XOR;
    if (!strcmp(str, "icmp"))
        return FLOW_LLVM_OP_ICMP;
    if (!strcmp(str, "select"))
        return FLOW_LLVM_OP_SELECT;
    if (!strcmp(str, "br"))
        return FLOW_LLVM_OP_BR;
    if (!strcmp(str, "phi"))
        return FLOW_LLVM_OP_PHI;
    if (!strcmp(str, "trunc"))
        return FLOW_LLVM_OP_TRUNC;
    if (!strcmp(str, "zext"))
        return FLOW_LLVM_OP_ZEXT;
    if (!strcmp(str, "sext"))
        return FLOW_LLVM_OP_SEXT;
    if (!strcmp(str, "call") || !strcmp(str, "tail"))
        return FLOW_LLVM_OP_CALL;
    if (!strcmp(str, "ret"))
        return FLOW_LLVM_OP_RET;
    return (FlowLLVMOpcode)-1;
}

/* ============================================================================
 * 3. SSA IR Text Parser: flow_llvm_lift_ir_text
 * ============================================================================ */

int flow_llvm_lift_ir_text(const char *llvm_ir_text, FlowLLVMLiftedFunction *fns_out,
                           size_t max_fns, size_t *fn_count_out) {
    if (!fns_out || !fn_count_out)
        return -1;
    *fn_count_out = 0;
    if (!llvm_ir_text || max_fns == 0 || llvm_ir_text[0] == '\0') {
        return 0;
    }

    const char *cursor = llvm_ir_text;
    char line[2048];
    FlowLLVMLiftedFunction *cur_fn = NULL;
    int in_fn = 0;
    char last_br_cond[64] = {0};

    while (*cursor) {
        size_t line_len = 0;
        while (*cursor && *cursor != '\n' && line_len + 1 < sizeof(line)) {
            line[line_len++] = *cursor++;
        }
        line[line_len] = '\0';
        if (*cursor == '\n')
            cursor++;

        strip_comments_and_metadata(line);
        if (line[0] == '\0')
            continue;

        if (!in_fn) {
            char *def_pos = strstr(line, "define ");
            if (def_pos && *fn_count_out < max_fns) {
                char *at = strchr(def_pos, '@');
                if (at) {
                    char *paren = strchr(at, '(');
                    if (paren) {
                        cur_fn = &fns_out[*fn_count_out];
                        memset(cur_fn, 0, sizeof(*cur_fn));
                        in_fn = 1;
                        last_br_cond[0] = '\0';

                        /* Function name */
                        size_t name_len = (size_t)(paren - (at + 1));
                        if (name_len >= sizeof(cur_fn->fn_name))
                            name_len = sizeof(cur_fn->fn_name) - 1;
                        memcpy(cur_fn->fn_name, at + 1, name_len);
                        cur_fn->fn_name[name_len] = '\0';

                        /* Return type: scan backwards from @ */
                        char *p_back = at - 1;
                        while (p_back > def_pos && isspace((unsigned char)*p_back))
                            p_back--;
                        char *token_end = p_back;
                        while (p_back > def_pos && !isspace((unsigned char)*(p_back - 1)))
                            p_back--;
                        char ret_type_str[32] = {0};
                        size_t rt_len = (size_t)(token_end - p_back + 1);
                        if (rt_len < sizeof(ret_type_str)) {
                            memcpy(ret_type_str, p_back, rt_len);
                            ret_type_str[rt_len] = '\0';
                            cur_fn->ret_bits = parse_llvm_type_bits(ret_type_str);
                        } else {
                            cur_fn->ret_bits = 32;
                        }
                        cur_fn->ret_is_signed = 1;

                        /* Parameter list inside (...) */
                        char *close_paren = strchr(paren, ')');
                        if (close_paren) {
                            char params_buf[512];
                            size_t p_len = (size_t)(close_paren - (paren + 1));
                            if (p_len < sizeof(params_buf)) {
                                memcpy(params_buf, paren + 1, p_len);
                                params_buf[p_len] = '\0';

                                char *saveptr = NULL;
                                char *p_tok = strtok_r(params_buf, ",", &saveptr);
                                while (p_tok && cur_fn->param_count < 16) {
                                    trim_inplace(p_tok);
                                    char *perc = strchr(p_tok, '%');
                                    if (perc) {
                                        clean_identifier(perc,
                                                         cur_fn->param_names[cur_fn->param_count],
                                                         sizeof(cur_fn->param_names[0]));
                                        cur_fn->param_bits[cur_fn->param_count] =
                                            parse_llvm_param_type_bits(p_tok, perc);
                                        cur_fn->param_count++;
                                    }
                                    p_tok = strtok_r(NULL, ",", &saveptr);
                                }
                            }
                        }
                    }
                }
            }
        } else {
            /* Inside function body */
            if (line[0] == '}') {
                in_fn = 0;
                (*fn_count_out)++;
                cur_fn = NULL;
                continue;
            }

            char *colon = strchr(line, ':');
            if (colon && !strchr(line, '=')) {
                /* Basic block label (e.g., entry:, exit:) */
                continue;
            }

            if (!cur_fn || cur_fn->inst_count >= 256)
                continue;
            FlowLLVMInst *inst = &cur_fn->insts[cur_fn->inst_count];
            memset(inst, 0, sizeof(*inst));

            char *eq = strchr(line, '=');
            if (eq) {
                *eq = '\0';
                clean_identifier(line, inst->ssa_name, sizeof(inst->ssa_name));
                char *rhs = eq + 1;
                trim_inplace(rhs);

                /* Skip tail/musttail */
                if (!strncmp(rhs, "tail ", 5))
                    rhs += 5;
                else if (!strncmp(rhs, "musttail ", 9))
                    rhs += 9;
                else if (!strncmp(rhs, "notail ", 7))
                    rhs += 7;
                trim_inplace(rhs);

                char op_str[32] = {0};
                sscanf(rhs, "%31s", op_str);
                inst->op = flow_llvm_parse_opcode(op_str);
                char *args = rhs + strlen(op_str);
                trim_inplace(args);

                /* Consume all modern LLVM IR instruction modifiers */
                consume_llvm_modifiers(&args, inst);

                if (inst->op == FLOW_LLVM_OP_ICMP) {
                    char pred[16] = {0}, ty[16] = {0}, o0[64] = {0}, o1[64] = {0};
                    sscanf(args, "%15s %15s %63[^,], %63s", pred, ty, o0, o1);
                    strncpy(inst->icmp_pred, pred, sizeof(inst->icmp_pred) - 1);
                    clean_identifier(o0, inst->arg0, sizeof(inst->arg0));
                    clean_identifier(o1, inst->arg1, sizeof(inst->arg1));
                    inst->bit_width = 1;
                    cur_fn->inst_count++;
                } else if (inst->op == FLOW_LLVM_OP_SELECT) {
                    char c_ty[16] = {0}, c_val[64] = {0}, t_ty[16] = {0}, t_val[64] = {0},
                         f_ty[16] = {0}, f_val[64] = {0};
                    sscanf(args, "%15s %63[^,], %15s %63[^,], %15s %63s", c_ty, c_val, t_ty, t_val,
                           f_ty, f_val);
                    clean_identifier(c_val, inst->arg0, sizeof(inst->arg0));
                    clean_identifier(t_val, inst->arg1, sizeof(inst->arg1));
                    clean_identifier(f_val, inst->arg2, sizeof(inst->arg2));
                    inst->bit_width = parse_llvm_type_bits(t_ty);
                    cur_fn->inst_count++;
                } else if (inst->op == FLOW_LLVM_OP_TRUNC || inst->op == FLOW_LLVM_OP_ZEXT ||
                           inst->op == FLOW_LLVM_OP_SEXT) {
                    char s_ty[16] = {0}, s_val[64] = {0}, to_kw[16] = {0}, d_ty[16] = {0};
                    sscanf(args, "%15s %63s %15s %15s", s_ty, s_val, to_kw, d_ty);
                    clean_identifier(s_val, inst->arg0, sizeof(inst->arg0));
                    inst->bit_width = parse_llvm_type_bits(d_ty);
                    cur_fn->inst_count++;
                } else if (inst->op == FLOW_LLVM_OP_PHI) {
                    char ty[16] = {0};
                    sscanf(args, "%15s", ty);
                    inst->bit_width = parse_llvm_type_bits(ty);
                    char *b1 = strchr(args, '[');
                    if (b1) {
                        char *b1_end = strchr(b1, ']');
                        if (b1_end) {
                            *b1_end = '\0';
                            char val0[64] = {0}, bb0[64] = {0};
                            sscanf(b1 + 1, "%63[^,], %63s", val0, bb0);
                            clean_identifier(val0, inst->arg0, sizeof(inst->arg0));
                            *b1_end = ']';
                        }
                    }
                    char *b2 = b1 ? strchr(b1 + 1, '[') : NULL;
                    if (b2) {
                        char *b2_end = strchr(b2, ']');
                        if (b2_end) {
                            *b2_end = '\0';
                            char val1[64] = {0}, bb1[64] = {0};
                            sscanf(b2 + 1, "%63[^,], %63s", val1, bb1);
                            clean_identifier(val1, inst->arg1, sizeof(inst->arg1));
                            *b2_end = ']';
                        }
                    }
                    if (last_br_cond[0]) {
                        strncpy(inst->arg2, last_br_cond, sizeof(inst->arg2) - 1);
                    }
                    cur_fn->inst_count++;
                } else if (inst->op == FLOW_LLVM_OP_CALL) {
                    char ty[16] = {0}, callee[64] = {0};
                    sscanf(args, "%15s %63[^(]", ty, callee);
                    inst->bit_width = parse_llvm_type_bits(ty);
                    clean_identifier(callee, inst->arg0, sizeof(inst->arg0));
                    char *c_paren = strchr(args, '(');
                    if (c_paren) {
                        char c_args[256] = {0};
                        char *close_p = strchr(c_paren, ')');
                        if (close_p) {
                            size_t calen = (size_t)(close_p - (c_paren + 1));
                            if (calen < sizeof(c_args)) {
                                memcpy(c_args, c_paren + 1, calen);
                                c_args[calen] = '\0';
                                char aty1[16] = {0}, aval1[64] = {0}, aty2[16] = {0},
                                     aval2[64] = {0};
                                if (sscanf(c_args, "%15s %63[^,], %15s %63s", aty1, aval1, aty2,
                                           aval2) >= 2) {
                                    clean_identifier(aval1, inst->arg1, sizeof(inst->arg1));
                                    clean_identifier(aval2, inst->arg2, sizeof(inst->arg2));
                                }
                            }
                        }
                    }
                    cur_fn->inst_count++;
                } else if (inst->op != (FlowLLVMOpcode)-1) {
                    char ty[16] = {0}, o0[64] = {0}, o1[64] = {0};
                    sscanf(args, "%15s %63[^,], %63s", ty, o0, o1);
                    inst->bit_width = parse_llvm_type_bits(ty);
                    clean_identifier(o0, inst->arg0, sizeof(inst->arg0));
                    clean_identifier(o1, inst->arg1, sizeof(inst->arg1));
                    cur_fn->inst_count++;
                }
            } else {
                /* Non-assignment instructions: ret, br */
                char op_str[32] = {0};
                sscanf(line, "%31s", op_str);
                FlowLLVMOpcode op = flow_llvm_parse_opcode(op_str);
                char *args = line + strlen(op_str);
                trim_inplace(args);

                if (op == FLOW_LLVM_OP_RET) {
                    inst->op = FLOW_LLVM_OP_RET;
                    char ty[16] = {0}, val[64] = {0};
                    sscanf(args, "%15s %63s", ty, val);
                    inst->bit_width = parse_llvm_type_bits(ty);
                    clean_identifier(val, inst->arg0, sizeof(inst->arg0));
                    strncpy(cur_fn->ret_ssa, inst->arg0, sizeof(cur_fn->ret_ssa) - 1);
                    cur_fn->inst_count++;
                } else if (op == FLOW_LLVM_OP_BR) {
                    inst->op = FLOW_LLVM_OP_BR;
                    if (!strncmp(args, "label ", 6)) {
                        clean_identifier(args + 6, inst->arg0, sizeof(inst->arg0));
                    } else if (!strncmp(args, "i1 ", 3)) {
                        char cond[64] = {0}, l1[16] = {0}, t_target[64] = {0}, l2[16] = {0},
                             f_target[64] = {0};
                        sscanf(args + 3, "%63[^,], %15s %63[^,], %15s %63s", cond, l1, t_target, l2,
                               f_target);
                        clean_identifier(cond, inst->arg0, sizeof(inst->arg0));
                        clean_identifier(t_target, inst->arg1, sizeof(inst->arg1));
                        clean_identifier(f_target, inst->arg2, sizeof(inst->arg2));
                        inst->bit_width = 1;
                        strncpy(last_br_cond, inst->arg0, sizeof(last_br_cond) - 1);
                    }
                    cur_fn->inst_count++;
                }
            }
        }
    }

    return 0;
}

/* ============================================================================
 * 4. C Source Lifter: flow_llvm_lift_c_source
 * ============================================================================ */

int flow_llvm_lift_c_source(const char *c_path, FlowLLVMLiftedFunction *fns_out, size_t max_fns,
                            size_t *fn_count_out) {
    if (!c_path || !fns_out || !fn_count_out || max_fns == 0)
        return -1;
    *fn_count_out = 0;

    size_t path_len = strlen(c_path);
    if (path_len > 3 && !strcmp(c_path + path_len - 3, ".ll")) {
        FILE *f = fopen(c_path, "rb");
        if (!f)
            return -1;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz < 0) {
            fclose(f);
            return -1;
        }
        char *buf = (char *)malloc((size_t)sz + 1);
        if (!buf) {
            fclose(f);
            return -1;
        }
        size_t nread = fread(buf, 1, (size_t)sz, f);
        buf[nread] = '\0';
        fclose(f);

        int rc = flow_llvm_lift_ir_text(buf, fns_out, max_fns, fn_count_out);
        free(buf);
        return rc;
    }

    if (access(c_path, R_OK) != 0)
        return -1;

    char tmp_ll[256];
    snprintf(tmp_ll, sizeof(tmp_ll), "/tmp/flow_lift_%d_%u.ll", (int)getpid(), (unsigned)rand());

    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "clang -S -emit-llvm -O1 -fno-discard-value-names -fno-vectorize "
             "-fno-slp-vectorize -Isrc -Iplugins \"%s\" -o \"%s\" >/dev/null 2>&1",
             c_path, tmp_ll);

    int ret = system(cmd);
    if (ret != 0) {
        unlink(tmp_ll);
        return -1;
    }

    FILE *f = fopen(tmp_ll, "rb");
    if (!f) {
        unlink(tmp_ll);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) {
        fclose(f);
        unlink(tmp_ll);
        return -1;
    }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        unlink(tmp_ll);
        return -1;
    }
    size_t nread = fread(buf, 1, (size_t)sz, f);
    buf[nread] = '\0';
    fclose(f);
    unlink(tmp_ll);

    int rc = flow_llvm_lift_ir_text(buf, fns_out, max_fns, fn_count_out);
    free(buf);
    return rc;
}

/* ============================================================================
 * 5. Abstract Interpretation & Bound Inference Engine
 * ============================================================================ */

typedef struct {
    int64_t min_val;
    int64_t max_val;
    uint64_t known_zero;
    uint64_t known_one;
    uint32_t bit_width;
    int div_by_zero_risk;
    int overflow_risk;
} FlowAbstractValue;

static void flow_abstract_reduce(FlowAbstractValue *av) {
    uint32_t w = av->bit_width ? av->bit_width : 32;
    uint64_t m = flow_bit_mask(w);

    av->known_one &= m;
    av->known_zero |= ~m;

    if (w < 64) {
        uint64_t sign_bit = (uint64_t)1 << (w - 1);
        if (av->known_zero & sign_bit) {
            int64_t kb_min = (int64_t)av->known_one;
            int64_t kb_max = (int64_t)((~av->known_zero) & m);
            if (kb_min > av->min_val)
                av->min_val = kb_min;
            if (kb_max < av->max_val)
                av->max_val = kb_max;
        } else if (av->known_one & sign_bit) {
            int64_t kb_min = flow_sign_extend(av->known_one, w);
            int64_t kb_max = flow_sign_extend((~av->known_zero) & m, w);
            if (kb_min > av->min_val)
                av->min_val = kb_min;
            if (kb_max < av->max_val)
                av->max_val = kb_max;
        }
    }

    if (av->min_val == av->max_val) {
        uint64_t c = (uint64_t)av->min_val & m;
        av->known_one = c;
        av->known_zero = (~c & m) | ~m;
    } else if (av->min_val >= 0 && av->max_val >= 0) {
        if (w < 64) {
            av->known_zero |= ((uint64_t)1 << (w - 1));
        }
        if (av->max_val == 0) {
            av->known_zero |= m;
            av->known_one = 0;
        } else {
            int high_bit = 63 - flow_clz64((uint64_t)av->max_val);
            if (high_bit < 63) {
                uint64_t upper_mask = ~(((uint64_t)1 << (high_bit + 1)) - 1ULL);
                av->known_zero |= (upper_mask & m);
            }
        }
    }

    if (av->min_val > av->max_val) {
        av->min_val = av->max_val;
    }
}

static bool flow_parse_constant(const char *arg, int64_t *val_out) {
    if (!arg || !arg[0])
        return false;
    if (strcmp(arg, "true") == 0) {
        *val_out = 1;
        return true;
    }
    if (strcmp(arg, "false") == 0) {
        *val_out = 0;
        return true;
    }

    char *endptr = NULL;
    int base = 10;
    if (arg[0] == '0' && (arg[1] == 'x' || arg[1] == 'X')) {
        base = 16;
    } else if (arg[0] == '-' && arg[1] == '0' && (arg[2] == 'x' || arg[2] == 'X')) {
        base = 16;
    } else if (!isdigit((unsigned char)arg[0]) && arg[0] != '-') {
        return false;
    }

    int64_t v = (int64_t)strtoll(arg, &endptr, base);
    if (endptr && *endptr == '\0') {
        *val_out = v;
        return true;
    }
    return false;
}

static FlowAbstractValue flow_eval_operand(const FlowLLVMLiftedFunction *fn,
                                           const FlowAbstractValue *inst_vals, size_t current_idx,
                                           const char *arg, uint32_t default_w,
                                           const SemanticIR *constraint_ir) {
    FlowAbstractValue res;
    memset(&res, 0, sizeof(res));
    res.bit_width = default_w ? default_w : 32;

    int64_t const_val = 0;
    if (flow_parse_constant(arg, &const_val)) {
        res.min_val = const_val;
        res.max_val = const_val;
        uint64_t c = (uint64_t)const_val & flow_bit_mask(res.bit_width);
        res.known_one = c;
        res.known_zero = (~c & flow_bit_mask(res.bit_width)) | ~flow_bit_mask(res.bit_width);
        flow_abstract_reduce(&res);
        return res;
    }

    for (size_t i = 0; i < current_idx; i++) {
        if (flow_ident_matches(fn->insts[i].ssa_name, arg)) {
            return inst_vals[i];
        }
    }

    for (size_t p = 0; p < fn->param_count; p++) {
        if (flow_ident_matches(fn->param_names[p], arg)) {
            uint32_t pw = fn->param_bits[p] ? fn->param_bits[p] : res.bit_width;
            res.bit_width = pw;
            res.min_val = flow_bit_smin(pw);
            res.max_val = flow_bit_smax(pw);
            res.known_zero = ~flow_bit_mask(pw);
            res.known_one = 0ULL;

            if (constraint_ir) {
                for (size_t c = 0; c < constraint_ir->declared_constraint_count; c++) {
                    const FlowConstraint *fc = &constraint_ir->constraints[c];
                    if (flow_ident_matches(fc->name, arg) ||
                        flow_ident_matches(fc->name, fn->param_names[p])) {
                        int64_t cv = atoll(fc->value);
                        if (strcmp(fc->operator, ">=") == 0 && cv > res.min_val) {
                            res.min_val = cv;
                        } else if (strcmp(fc->operator, "<=") == 0 && cv < res.max_val) {
                            res.max_val = cv;
                        } else if (strcmp(fc->operator, "==") == 0) {
                            res.min_val = cv;
                            res.max_val = cv;
                        } else if (strcmp(fc->operator, ">") == 0 && (cv + 1) > res.min_val) {
                            res.min_val = cv + 1;
                        } else if (strcmp(fc->operator, "<") == 0 && (cv - 1) < res.max_val) {
                            res.max_val = cv - 1;
                        }
                    }
                }
            }

            flow_abstract_reduce(&res);
            return res;
        }
    }

    res.min_val = flow_bit_smin(res.bit_width);
    res.max_val = flow_bit_smax(res.bit_width);
    res.known_zero = ~flow_bit_mask(res.bit_width);
    flow_abstract_reduce(&res);
    return res;
}

int flow_llvm_infer_bounds(FlowLLVMLiftedFunction *fn, const SemanticIR *constraint_ir) {
    if (!fn)
        return -1;
    if (fn->inst_count == 0) {
        fn->overflow_free_proven = 1;
        return 0;
    }

    FlowAbstractValue vals[256];
    memset(vals, 0, sizeof(vals));

    int all_overflow_free = 1;

    for (size_t i = 0; i < fn->inst_count; i++) {
        FlowLLVMInst *inst = &fn->insts[i];
        uint32_t w = inst->bit_width ? inst->bit_width : 32;
        FlowAbstractValue *res = &vals[i];
        res->bit_width = w;

        FlowAbstractValue a = flow_eval_operand(fn, vals, i, inst->arg0, w, constraint_ir);
        FlowAbstractValue b = flow_eval_operand(fn, vals, i, inst->arg1, w, constraint_ir);

        switch (inst->op) {
        case FLOW_LLVM_OP_ADD: {
            flow_i128 wide_min = (flow_i128)a.min_val + (flow_i128)b.min_val;
            flow_i128 wide_max = (flow_i128)a.max_val + (flow_i128)b.max_val;
            res->min_val = flow_safe_clamp_i128_to_i64(wide_min);
            res->max_val = flow_safe_clamp_i128_to_i64(wide_max);

            uint64_t cz = 1ULL, co = 0ULL;
            for (uint32_t bit = 0; bit < w && bit < 64; bit++) {
                uint64_t bm = (uint64_t)1 << bit;
                int az = (a.known_zero & bm) != 0, ao = (a.known_one & bm) != 0;
                int bz = (b.known_zero & bm) != 0, bo = (b.known_one & bm) != 0;
                if ((az || ao) && (bz || bo) && (cz || co)) {
                    int av = ao ? 1 : 0, bv = bo ? 1 : 0, cv = co ? 1 : 0;
                    int sum = av ^ bv ^ cv;
                    int nxt_c = (av & bv) | (cv & (av ^ bv));
                    if (sum)
                        res->known_one |= bm;
                    else
                        res->known_zero |= bm;
                    cz = nxt_c ? 0 : 1;
                    co = nxt_c ? 1 : 0;
                } else {
                    if (az && bz) {
                        cz = 1;
                        co = 0;
                    } else if (ao && bo) {
                        cz = 0;
                        co = 1;
                    } else {
                        cz = 0;
                        co = 0;
                    }
                }
            }

            if (inst->has_nsw || fn->ret_is_signed) {
                flow_i128 smin = (flow_i128)flow_bit_smin(w);
                flow_i128 smax = (flow_i128)flow_bit_smax(w);
                if (wide_min < smin || wide_max > smax) {
                    res->overflow_risk = 1;
                    all_overflow_free = 0;
                }
            }
            if (inst->has_nuw) {
                flow_u128 umax = (flow_u128)flow_bit_umax(w);
                if (wide_min < 0 || (flow_u128)wide_max > umax) {
                    res->overflow_risk = 1;
                    all_overflow_free = 0;
                }
            }
            break;
        }

        case FLOW_LLVM_OP_SUB: {
            flow_i128 wide_min, wide_max;
            if (strcmp(inst->arg0, inst->arg1) == 0) {
                wide_min = 0;
                wide_max = 0;
                res->min_val = 0;
                res->max_val = 0;
                res->known_one = 0;
                res->known_zero = flow_bit_mask(w);
            } else {
                wide_min = (flow_i128)a.min_val - (flow_i128)b.max_val;
                wide_max = (flow_i128)a.max_val - (flow_i128)b.min_val;
                res->min_val = flow_safe_clamp_i128_to_i64(wide_min);
                res->max_val = flow_safe_clamp_i128_to_i64(wide_max);
            }
            if (inst->has_nsw || fn->ret_is_signed) {
                flow_i128 smin = (flow_i128)flow_bit_smin(w);
                flow_i128 smax = (flow_i128)flow_bit_smax(w);
                if (wide_min < smin || wide_max > smax) {
                    res->overflow_risk = 1;
                    all_overflow_free = 0;
                }
            }
            if (inst->has_nuw) {
                flow_u128 umax = (flow_u128)flow_bit_umax(w);
                if (wide_min < 0 || (flow_u128)wide_max > umax) {
                    res->overflow_risk = 1;
                    all_overflow_free = 0;
                }
            }
            break;
        }

        case FLOW_LLVM_OP_MUL: {
            flow_i128 p1 = (flow_i128)a.min_val * (flow_i128)b.min_val;
            flow_i128 p2 = (flow_i128)a.min_val * (flow_i128)b.max_val;
            flow_i128 p3 = (flow_i128)a.max_val * (flow_i128)b.min_val;
            flow_i128 p4 = (flow_i128)a.max_val * (flow_i128)b.max_val;

            flow_i128 wide_min = p1, wide_max = p1;
            if (p2 < wide_min)
                wide_min = p2;
            if (p2 > wide_max)
                wide_max = p2;
            if (p3 < wide_min)
                wide_min = p3;
            if (p3 > wide_max)
                wide_max = p3;
            if (p4 < wide_min)
                wide_min = p4;
            if (p4 > wide_max)
                wide_max = p4;

            res->min_val = flow_safe_clamp_i128_to_i64(wide_min);
            res->max_val = flow_safe_clamp_i128_to_i64(wide_max);

            int lza = flow_ctz64(a.known_one | ~a.known_zero);
            int lzb = flow_ctz64(b.known_one | ~b.known_zero);
            int total_lz = lza + lzb;
            if (total_lz > (int)w)
                total_lz = (int)w;
            if (total_lz > 0 && total_lz < 64) {
                res->known_zero |= (((uint64_t)1 << total_lz) - 1ULL);
            }

            if (inst->has_nsw || fn->ret_is_signed) {
                flow_i128 smin = (flow_i128)flow_bit_smin(w);
                flow_i128 smax = (flow_i128)flow_bit_smax(w);
                if (wide_min < smin || wide_max > smax) {
                    res->overflow_risk = 1;
                    all_overflow_free = 0;
                }
            }
            if (inst->has_nuw) {
                flow_u128 umax = (flow_u128)flow_bit_umax(w);
                if (wide_min < 0 || (flow_u128)wide_max > umax) {
                    res->overflow_risk = 1;
                    all_overflow_free = 0;
                }
            }
            break;
        }

        case FLOW_LLVM_OP_SDIV: {
            if (b.min_val <= 0 && b.max_val >= 0) {
                res->div_by_zero_risk = 1;
                all_overflow_free = 0;
            }

            /* Collect critical non-zero divisor candidates */
            int64_t cand_divs[4];
            size_t n_divs = 0;
            if (b.min_val != 0)
                cand_divs[n_divs++] = b.min_val;
            if (b.max_val != 0 && b.max_val != b.min_val)
                cand_divs[n_divs++] = b.max_val;
            if (b.min_val <= -1 && b.max_val >= -1 && -1 != b.min_val && -1 != b.max_val) {
                cand_divs[n_divs++] = -1;
            }
            if (b.min_val <= 1 && b.max_val >= 1 && 1 != b.min_val && 1 != b.max_val) {
                cand_divs[n_divs++] = 1;
            }

            if (n_divs == 0) {
                /* Pure division by zero */
                res->min_val = 0;
                res->max_val = 0;
            } else {
                int64_t cur_min = flow_safe_sdiv_i64(a.min_val, cand_divs[0]);
                int64_t cur_max = cur_min;
                for (size_t d = 0; d < n_divs; d++) {
                    int64_t div = cand_divs[d];
                    int64_t q_min = flow_safe_sdiv_i64(a.min_val, div);
                    int64_t q_max = flow_safe_sdiv_i64(a.max_val, div);
                    if (q_min < cur_min)
                        cur_min = q_min;
                    if (q_min > cur_max)
                        cur_max = q_min;
                    if (q_max < cur_min)
                        cur_min = q_max;
                    if (q_max > cur_max)
                        cur_max = q_max;
                }
                /* If numerator domain spans zero, 0 / div = 0 is always in the range */
                if (a.min_val <= 0 && a.max_val >= 0) {
                    if (cur_min > 0)
                        cur_min = 0;
                    if (cur_max < 0)
                        cur_max = 0;
                }
                res->min_val = cur_min;
                res->max_val = cur_max;
            }

            /* Detect signed overflow (smin / -1) */
            if (a.min_val <= flow_bit_smin(w) && b.min_val <= -1 && b.max_val >= -1) {
                res->overflow_risk = 1;
                all_overflow_free = 0;
            }

            if (res->min_val < flow_bit_smin(w))
                res->min_val = flow_bit_smin(w);
            if (res->max_val > flow_bit_smax(w))
                res->max_val = flow_bit_smax(w);
            break;
        }

        case FLOW_LLVM_OP_UDIV: {
            int b_can_be_zero = 0;
            if ((b.known_one & flow_bit_mask(w)) == 0ULL) {
                if (b.min_val == 0 || b.max_val == 0 || (b.min_val <= 0 && b.max_val >= 0)) {
                    b_can_be_zero = 1;
                }
            }
            if (b_can_be_zero) {
                res->div_by_zero_risk = 1;
                all_overflow_free = 0;
            }
            uint64_t bmin_u = b.min_val > 0 ? (uint64_t)b.min_val : 1ULL;
            uint64_t bmax_u = b.max_val > 0 ? (uint64_t)b.max_val : 1ULL;
            res->min_val = (int64_t)flow_safe_udiv_u64((uint64_t)a.min_val, bmax_u);
            res->max_val = (int64_t)flow_safe_udiv_u64((uint64_t)a.max_val, bmin_u);
            break;
        }

        case FLOW_LLVM_OP_SREM: {
            if (b.min_val <= 0 && b.max_val >= 0) {
                res->div_by_zero_risk = 1;
                all_overflow_free = 0;
            }
            uint64_t abs_b1 = (b.min_val == INT64_MIN) ? ((uint64_t)INT64_MAX + 1ULL)
                                                       : (uint64_t)llabs(b.min_val);
            uint64_t abs_b2 = (b.max_val == INT64_MIN) ? ((uint64_t)INT64_MAX + 1ULL)
                                                       : (uint64_t)llabs(b.max_val);
            uint64_t max_b = abs_b1 > abs_b2 ? abs_b1 : abs_b2;
            int64_t max_m;
            if (max_b == 0ULL) {
                max_m = 0;
            } else {
                uint64_t m_minus_1 = max_b - 1ULL;
                max_m = (m_minus_1 > (uint64_t)INT64_MAX) ? INT64_MAX : (int64_t)m_minus_1;
            }
            if (a.min_val >= 0) {
                res->min_val = 0;
                res->max_val = a.max_val < max_m ? a.max_val : max_m;
            } else if (a.max_val <= 0) {
                res->min_val = a.min_val > -max_m ? a.min_val : -max_m;
                res->max_val = 0;
            } else {
                res->min_val = -max_m;
                res->max_val = max_m;
            }
            break;
        }

        case FLOW_LLVM_OP_UREM: {
            int b_can_be_zero = 0;
            if ((b.known_one & flow_bit_mask(w)) == 0ULL) {
                if (b.min_val == 0 || b.max_val == 0 || (b.min_val <= 0 && b.max_val >= 0)) {
                    b_can_be_zero = 1;
                }
            }
            if (b_can_be_zero) {
                res->div_by_zero_risk = 1;
                all_overflow_free = 0;
            }
            res->min_val = 0;
            res->max_val = (b.max_val > 0) ? (b.max_val - 1) : 0;
            if (a.max_val < res->max_val)
                res->max_val = a.max_val;
            break;
        }

        case FLOW_LLVM_OP_SHL: {
            int64_t shift = b.min_val;
            if (b.min_val == b.max_val && shift >= 0 && shift < (int64_t)w) {
                if (shift < 64) {
                    uint64_t in_zeros = (shift > 0) ? (((uint64_t)1 << shift) - 1ULL) : 0ULL;
                    res->known_zero = ((a.known_zero << shift) | in_zeros) & flow_bit_mask(w);
                    res->known_one = (a.known_one << shift) & flow_bit_mask(w);
                    if (shift == 0) {
                        res->min_val = a.min_val;
                        res->max_val = a.max_val;
                    } else if (a.min_val >= 0 && inst->has_nsw &&
                               (((flow_i128)a.max_val << shift) <= (flow_i128)flow_bit_smax(w))) {
                        res->min_val = flow_safe_clamp_i128_to_i64((flow_i128)a.min_val << shift);
                        res->max_val = flow_safe_clamp_i128_to_i64((flow_i128)a.max_val << shift);
                    } else {
                        res->min_val = flow_bit_smin(w);
                        res->max_val = flow_bit_smax(w);
                    }
                } else {
                    res->known_zero = flow_bit_mask(w);
                    res->known_one = 0ULL;
                    res->min_val = flow_bit_smin(w);
                    res->max_val = flow_bit_smax(w);
                }
            } else {
                res->min_val = flow_bit_smin(w);
                res->max_val = flow_bit_smax(w);
            }
            break;
        }

        case FLOW_LLVM_OP_LSHR: {
            int64_t shift = b.min_val;
            if (b.min_val == b.max_val && shift >= 0 && shift < (int64_t)w) {
                if (shift == 0) {
                    res->known_zero = a.known_zero & flow_bit_mask(w);
                    res->known_one = a.known_one & flow_bit_mask(w);
                    res->min_val = a.min_val;
                    res->max_val = a.max_val;
                } else if (shift < 64) {
                    uint64_t upper_zeros = ((w - shift) >= 64) ? 0ULL : (~0ULL << (w - shift));
                    res->known_zero = ((a.known_zero >> shift) | upper_zeros) & flow_bit_mask(w);
                    res->known_one = (a.known_one >> shift) & flow_bit_mask(w);
                    if (a.min_val >= 0) {
                        res->min_val = (int64_t)((uint64_t)a.min_val >> shift);
                        res->max_val = (int64_t)((uint64_t)a.max_val >> shift);
                    } else {
                        res->min_val = 0;
                        res->max_val = (int64_t)(flow_bit_umax(w) >> shift);
                    }
                } else {
                    res->min_val = 0;
                    res->max_val = 0;
                    res->known_zero = flow_bit_mask(w);
                    res->known_one = 0ULL;
                }
            } else {
                res->min_val = flow_bit_smin(w);
                res->max_val = flow_bit_smax(w);
                res->known_zero = 0ULL;
                res->known_one = 0ULL;
            }
            break;
        }

        case FLOW_LLVM_OP_ASHR: {
            int64_t shift = b.min_val;
            if (b.min_val == b.max_val && shift >= 0 && shift < (int64_t)w) {
                if (shift < 64) {
                    res->min_val = a.min_val >> shift;
                    res->max_val = a.max_val >> shift;
                    uint64_t sign_bit = (w >= 1 && w <= 64) ? ((uint64_t)1 << (w - 1)) : 0ULL;
                    if (sign_bit && (a.known_zero & sign_bit)) {
                        uint64_t upper_zeros =
                            (shift == 0 || (w - shift) >= 64) ? 0ULL : (~0ULL << (w - shift));
                        res->known_zero =
                            ((a.known_zero >> shift) | upper_zeros) & flow_bit_mask(w);
                        res->known_one = (a.known_one >> shift) & flow_bit_mask(w);
                    } else if (sign_bit && (a.known_one & sign_bit)) {
                        uint64_t upper_ones = (shift == 0 || (w - shift) >= 64)
                                                  ? 0ULL
                                                  : ((~0ULL << (w - shift)) & flow_bit_mask(w));
                        res->known_one = (a.known_one >> shift) | upper_ones;
                        res->known_zero = (a.known_zero >> shift) & flow_bit_mask(w);
                    } else {
                        res->known_zero = (a.known_zero >> shift) & flow_bit_mask(w);
                        res->known_one = (a.known_one >> shift) & flow_bit_mask(w);
                    }
                } else {
                    res->min_val = flow_bit_smin(w);
                    res->max_val = flow_bit_smax(w);
                }
            } else {
                res->min_val = flow_bit_smin(w);
                res->max_val = flow_bit_smax(w);
            }
            break;
        }

        case FLOW_LLVM_OP_AND: {
            res->known_zero = a.known_zero | b.known_zero;
            res->known_one = a.known_one & b.known_one;
            if (a.min_val >= 0 && b.min_val >= 0) {
                res->min_val = 0;
                res->max_val = (a.max_val < b.max_val) ? a.max_val : b.max_val;
            } else if (b.min_val >= 0) {
                res->min_val = 0;
                res->max_val = b.max_val;
            } else if (a.min_val >= 0) {
                res->min_val = 0;
                res->max_val = a.max_val;
            } else {
                res->min_val = flow_bit_smin(w);
                res->max_val = flow_bit_smax(w);
            }
            break;
        }

        case FLOW_LLVM_OP_OR: {
            res->known_zero = a.known_zero & b.known_zero;
            res->known_one = a.known_one | b.known_one;
            if (a.min_val >= 0 && b.min_val >= 0) {
                res->min_val = (a.min_val > b.min_val) ? a.min_val : b.min_val;
                res->max_val = (int64_t)((~res->known_zero) & flow_bit_mask(w));
            } else {
                res->min_val = flow_bit_smin(w);
                res->max_val = flow_bit_smax(w);
            }
            break;
        }

        case FLOW_LLVM_OP_XOR: {
            res->known_zero = (a.known_zero & b.known_zero) | (a.known_one & b.known_one);
            res->known_one = (a.known_zero & b.known_one) | (a.known_one & b.known_zero);
            if (strcmp(inst->arg0, inst->arg1) == 0) {
                res->min_val = 0;
                res->max_val = 0;
                res->known_one = 0;
                res->known_zero = flow_bit_mask(w);
            } else {
                res->min_val = flow_bit_smin(w);
                res->max_val = flow_bit_smax(w);
            }
            break;
        }

        case FLOW_LLVM_OP_ICMP: {
            res->bit_width = 1;
            const char *pred = inst->icmp_pred;
            int def_true = 0, def_false = 0;

            if (strcmp(pred, "slt") == 0) {
                if (a.max_val < b.min_val)
                    def_true = 1;
                else if (a.min_val >= b.max_val)
                    def_false = 1;
            } else if (strcmp(pred, "sle") == 0) {
                if (a.max_val <= b.min_val)
                    def_true = 1;
                else if (a.min_val > b.max_val)
                    def_false = 1;
            } else if (strcmp(pred, "sgt") == 0) {
                if (a.min_val > b.max_val)
                    def_true = 1;
                else if (a.max_val <= b.min_val)
                    def_false = 1;
            } else if (strcmp(pred, "sge") == 0) {
                if (a.min_val >= b.max_val)
                    def_true = 1;
                else if (a.max_val < b.min_val)
                    def_false = 1;
            } else if (strcmp(pred, "eq") == 0) {
                if (a.min_val == a.max_val && b.min_val == b.max_val && a.min_val == b.min_val)
                    def_true = 1;
                else if (a.max_val < b.min_val || a.min_val > b.max_val)
                    def_false = 1;
            } else if (strcmp(pred, "ne") == 0) {
                if (a.max_val < b.min_val || a.min_val > b.max_val)
                    def_true = 1;
                else if (a.min_val == a.max_val && b.min_val == b.max_val && a.min_val == b.min_val)
                    def_false = 1;
            }

            if (def_true) {
                res->min_val = 1;
                res->max_val = 1;
                res->known_one = 1;
                res->known_zero = ~1ULL;
            } else if (def_false) {
                res->min_val = 0;
                res->max_val = 0;
                res->known_one = 0;
                res->known_zero = ~0ULL;
            } else {
                res->min_val = 0;
                res->max_val = 1;
                res->known_one = 0;
                res->known_zero = ~1ULL;
            }
            break;
        }

        case FLOW_LLVM_OP_SELECT: {
            FlowAbstractValue cond = flow_eval_operand(fn, vals, i, inst->arg0, 1, constraint_ir);
            FlowAbstractValue v_true = flow_eval_operand(fn, vals, i, inst->arg1, w, constraint_ir);
            FlowAbstractValue v_false =
                flow_eval_operand(fn, vals, i, inst->arg2, w, constraint_ir);

            if (cond.min_val == 1 && cond.max_val == 1) {
                *res = v_true;
            } else if (cond.min_val == 0 && cond.max_val == 0) {
                *res = v_false;
            } else {
                res->min_val =
                    (v_true.min_val < v_false.min_val) ? v_true.min_val : v_false.min_val;
                res->max_val =
                    (v_true.max_val > v_false.max_val) ? v_true.max_val : v_false.max_val;
                res->known_zero = v_true.known_zero & v_false.known_zero;
                res->known_one = v_true.known_one & v_false.known_one;

                /* Check for clamping patterns */
                for (size_t prev = 0; prev < i; prev++) {
                    if (strcmp(fn->insts[prev].ssa_name, inst->arg0) == 0 &&
                        fn->insts[prev].op == FLOW_LLVM_OP_ICMP) {
                        FlowLLVMInst *icmp_inst = &fn->insts[prev];
                        int64_t clamp_bound = 0;
                        if (flow_parse_constant(icmp_inst->arg1, &clamp_bound)) {
                            if (strcmp(icmp_inst->icmp_pred, "slt") == 0 &&
                                strcmp(inst->arg1, icmp_inst->arg1) == 0) {
                                if (res->min_val < clamp_bound)
                                    res->min_val = clamp_bound;
                            } else if (strcmp(icmp_inst->icmp_pred, "sgt") == 0 &&
                                       strcmp(inst->arg1, icmp_inst->arg1) == 0) {
                                if (res->max_val > clamp_bound)
                                    res->max_val = clamp_bound;
                            }
                        }
                        break;
                    }
                }
            }
            break;
        }

        case FLOW_LLVM_OP_PHI: {
            FlowAbstractValue v0 = flow_eval_operand(fn, vals, i, inst->arg0, w, constraint_ir);
            FlowAbstractValue v1 = flow_eval_operand(fn, vals, i, inst->arg1, w, constraint_ir);
            res->min_val = (v0.min_val < v1.min_val) ? v0.min_val : v1.min_val;
            res->max_val = (v0.max_val > v1.max_val) ? v0.max_val : v1.max_val;
            res->known_zero = v0.known_zero & v1.known_zero;
            res->known_one = v0.known_one & v1.known_one;
            break;
        }

        case FLOW_LLVM_OP_TRUNC: {
            uint64_t mask_out = flow_bit_mask(w);
            res->known_zero = (a.known_zero & mask_out) | ~mask_out;
            res->known_one = a.known_one & mask_out;
            if (a.min_val >= flow_bit_smin(w) && a.max_val <= flow_bit_smax(w)) {
                res->min_val = a.min_val;
                res->max_val = a.max_val;
            } else {
                res->min_val = flow_bit_smin(w);
                res->max_val = flow_bit_smax(w);
            }
            break;
        }

        case FLOW_LLVM_OP_ZEXT: {
            uint32_t in_w = a.bit_width ? a.bit_width : 32;
            if (in_w == w) {
                res->known_zero = a.known_zero & flow_bit_mask(w);
                res->known_one = a.known_one & flow_bit_mask(w);
                res->min_val = a.min_val;
                res->max_val = a.max_val;
            } else if (in_w < w && in_w < 64) {
                uint64_t mask_in = flow_bit_mask(in_w);
                uint64_t mask_out = flow_bit_mask(w);
                res->known_zero = a.known_zero | (mask_out ^ mask_in) | ~mask_out;
                res->known_one = a.known_one & mask_in;
                if (a.min_val >= 0) {
                    res->min_val = a.min_val;
                    res->max_val = a.max_val;
                } else {
                    res->min_val = 0;
                    res->max_val = (int64_t)mask_in;
                }
            } else {
                res->min_val = flow_bit_smin(w);
                res->max_val = flow_bit_smax(w);
            }
            break;
        }

        case FLOW_LLVM_OP_SEXT: {
            uint32_t in_w = a.bit_width ? a.bit_width : 32;
            uint64_t mask_in = flow_bit_mask(in_w);
            uint64_t mask_out = flow_bit_mask(w);
            uint64_t sign_in = (in_w >= 1 && in_w <= 64) ? ((uint64_t)1 << (in_w - 1)) : 0ULL;
            if (sign_in && (a.known_zero & sign_in)) {
                res->known_zero = a.known_zero | (mask_out ^ mask_in) | ~mask_out;
                res->known_one = a.known_one & mask_in;
            } else if (sign_in && (a.known_one & sign_in)) {
                res->known_one = a.known_one | (mask_out ^ mask_in);
                res->known_zero = (a.known_zero & mask_in) | ~mask_out;
            }
            res->min_val = a.min_val;
            res->max_val = a.max_val;
            break;
        }

        case FLOW_LLVM_OP_CALL: {
            if (strstr(inst->arg0, "llvm.smin") || strstr(inst->arg0, "smin")) {
                res->min_val = (a.min_val < b.min_val) ? a.min_val : b.min_val;
                res->max_val = (a.max_val < b.max_val) ? a.max_val : b.max_val;
            } else if (strstr(inst->arg0, "llvm.smax") || strstr(inst->arg0, "smax")) {
                res->min_val = (a.min_val > b.min_val) ? a.min_val : b.min_val;
                res->max_val = (a.max_val > b.max_val) ? a.max_val : b.max_val;
            } else {
                res->min_val = flow_bit_smin(w);
                res->max_val = flow_bit_smax(w);
                res->known_zero = ~flow_bit_mask(w);
            }
            break;
        }

        default:
            res->min_val = flow_bit_smin(w);
            res->max_val = flow_bit_smax(w);
            res->known_zero = ~flow_bit_mask(w);
            break;
        }

        flow_abstract_reduce(res);

        inst->inferred_min = res->min_val;
        inst->inferred_max = res->max_val;
        inst->known_zero_mask = res->known_zero;
        inst->known_one_mask = res->known_one;
    }

    fn->overflow_free_proven = all_overflow_free;
    return 0;
}

/* ============================================================================
 * 6. Lean 4 Model & Boundary Certificate Emission Engine
 * ============================================================================ */

static void sanitize_lean_name(const char *raw, char *out, size_t out_sz, int is_reg) {
    if (!raw || !raw[0]) {
        snprintf(out, out_sz, "%s_0", is_reg ? "v" : "p");
        return;
    }
    const char *p = raw;
    if (*p == '%' || *p == '@')
        p++;

    size_t idx = 0;
    if (is_reg && (isdigit((unsigned char)*p) || *p == '\0')) {
        if (idx + 1 < out_sz)
            out[idx++] = 'v';
        if (idx + 1 < out_sz)
            out[idx++] = '_';
    }

    while (*p && idx + 1 < out_sz) {
        if (isalnum((unsigned char)*p) || *p == '_') {
            out[idx++] = *p;
        } else {
            out[idx++] = '_';
        }
        p++;
    }
    out[idx] = '\0';

    static const char *const keywords[] = {
        "def",    "let",   "in",        "if",        "then",    "else",  "match",
        "with",   "where", "inductive", "structure", "theorem", "lemma", "axiom",
        "opaque", "open",  "import",    "by",        "sorry",   "true",  "false",
        "return", "do",    "fun",       "have",      "show",    "from",  "for"};
    for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); ++i) {
        if (strcmp(out, keywords[i]) == 0) {
            char tmp[64];
            snprintf(tmp, sizeof(tmp), "v_%s", out);
            strncpy(out, tmp, out_sz);
            out[out_sz - 1] = '\0';
            break;
        }
    }
}

static void format_lean_operand(const char *arg, uint32_t width, char *out, size_t out_sz) {
    if (!arg || !arg[0]) {
        snprintf(out, out_sz, "0#%u", width > 0 ? width : 32U);
        return;
    }
    if (arg[0] == '%' || arg[0] == '@') {
        sanitize_lean_name(arg, out, out_sz, 1);
        return;
    }
    if (strcmp(arg, "true") == 0 || strcmp(arg, "false") == 0) {
        snprintf(out, out_sz, "%s", arg);
        return;
    }

    uint32_t w = width > 0 ? width : 32U;
    char *endptr = NULL;
    int base = 10;
    if (arg[0] == '0' && (arg[1] == 'x' || arg[1] == 'X'))
        base = 16;
    int64_t val = (int64_t)strtoll(arg, &endptr, base);
    if (endptr && *endptr == '\0') {
        uint64_t uval = (uint64_t)val & flow_bit_mask(w);
        snprintf(out, out_sz, "%" PRIu64 "#%u", uval, w);
    } else {
        snprintf(out, out_sz, "0#%u", w);
    }
}

static void emit_lean_instruction(FILE *out, const FlowLLVMInst *inst) {
    char dest[64], a0[64], a1[64], a2[64];
    sanitize_lean_name(inst->ssa_name, dest, sizeof(dest), 1);
    format_lean_operand(inst->arg0, inst->bit_width, a0, sizeof(a0));
    format_lean_operand(inst->arg1, inst->bit_width, a1, sizeof(a1));
    format_lean_operand(inst->arg2, inst->bit_width, a2, sizeof(a2));

    switch (inst->op) {
    case FLOW_LLVM_OP_ADD:
        fprintf(out, "  let %s := %s + %s\n", dest, a0, a1);
        break;
    case FLOW_LLVM_OP_SUB:
        fprintf(out, "  let %s := %s - %s\n", dest, a0, a1);
        break;
    case FLOW_LLVM_OP_MUL:
        fprintf(out, "  let %s := %s * %s\n", dest, a0, a1);
        break;
    case FLOW_LLVM_OP_UDIV:
        fprintf(out, "  let %s := %s / %s\n", dest, a0, a1);
        break;
    case FLOW_LLVM_OP_SDIV:
        fprintf(out, "  let %s := BitVec.sdiv %s %s\n", dest, a0, a1);
        break;
    case FLOW_LLVM_OP_UREM:
        fprintf(out, "  let %s := %s %% %s\n", dest, a0, a1);
        break;
    case FLOW_LLVM_OP_SREM:
        fprintf(out, "  let %s := BitVec.srem %s %s\n", dest, a0, a1);
        break;
    case FLOW_LLVM_OP_AND:
        fprintf(out, "  let %s := %s &&& %s\n", dest, a0, a1);
        break;
    case FLOW_LLVM_OP_OR:
        fprintf(out, "  let %s := %s ||| %s\n", dest, a0, a1);
        break;
    case FLOW_LLVM_OP_XOR:
        fprintf(out, "  let %s := %s ^^^ %s\n", dest, a0, a1);
        break;
    case FLOW_LLVM_OP_SHL:
        fprintf(out, "  let %s := %s <<< %s\n", dest, a0, a1);
        break;
    case FLOW_LLVM_OP_LSHR:
        fprintf(out, "  let %s := %s >>> %s\n", dest, a0, a1);
        break;
    case FLOW_LLVM_OP_ASHR:
        fprintf(out, "  let %s := BitVec.sshiftRight' %s %s\n", dest, a0, a1);
        break;
    case FLOW_LLVM_OP_ICMP:
        if (strcmp(inst->icmp_pred, "eq") == 0) {
            fprintf(out, "  let %s := %s == %s\n", dest, a0, a1);
        } else if (strcmp(inst->icmp_pred, "ne") == 0) {
            fprintf(out, "  let %s := %s != %s\n", dest, a0, a1);
        } else if (strcmp(inst->icmp_pred, "ult") == 0) {
            fprintf(out, "  let %s := BitVec.ult %s %s\n", dest, a0, a1);
        } else if (strcmp(inst->icmp_pred, "ule") == 0) {
            fprintf(out, "  let %s := BitVec.ule %s %s\n", dest, a0, a1);
        } else if (strcmp(inst->icmp_pred, "ugt") == 0) {
            fprintf(out, "  let %s := BitVec.ult %s %s\n", dest, a1, a0);
        } else if (strcmp(inst->icmp_pred, "uge") == 0) {
            fprintf(out, "  let %s := BitVec.ule %s %s\n", dest, a1, a0);
        } else if (strcmp(inst->icmp_pred, "slt") == 0) {
            fprintf(out, "  let %s := BitVec.slt %s %s\n", dest, a0, a1);
        } else if (strcmp(inst->icmp_pred, "sle") == 0) {
            fprintf(out, "  let %s := BitVec.sle %s %s\n", dest, a0, a1);
        } else if (strcmp(inst->icmp_pred, "sgt") == 0) {
            fprintf(out, "  let %s := BitVec.slt %s %s\n", dest, a1, a0);
        } else if (strcmp(inst->icmp_pred, "sge") == 0) {
            fprintf(out, "  let %s := BitVec.sle %s %s\n", dest, a1, a0);
        } else {
            fprintf(out, "  let %s := %s == %s\n", dest, a0, a1);
        }
        break;
    case FLOW_LLVM_OP_SELECT:
        fprintf(out, "  let %s := if %s then %s else %s\n", dest, a0, a1, a2);
        break;
    case FLOW_LLVM_OP_PHI:
        if (inst->arg2[0]) {
            fprintf(out, "  let %s := if %s then %s else %s\n", dest, a2, a0, a1);
        } else {
            fprintf(out, "  let %s := %s\n", dest, a0);
        }
        break;
    case FLOW_LLVM_OP_TRUNC:
        fprintf(out, "  let %s := BitVec.truncate %u %s\n", dest, inst->bit_width, a0);
        break;
    case FLOW_LLVM_OP_ZEXT:
        fprintf(out, "  let %s := BitVec.zeroExtend %u %s\n", dest, inst->bit_width, a0);
        break;
    case FLOW_LLVM_OP_SEXT:
        fprintf(out, "  let %s := BitVec.signExtend %u %s\n", dest, inst->bit_width, a0);
        break;
    case FLOW_LLVM_OP_CALL:
        if (strstr(inst->arg0, "llvm.smax") || strstr(inst->arg0, "smax")) {
            fprintf(out, "  let %s := if BitVec.slt %s %s then %s else %s\n", dest, a1, a2, a2, a1);
        } else if (strstr(inst->arg0, "llvm.smin") || strstr(inst->arg0, "smin")) {
            fprintf(out, "  let %s := if BitVec.slt %s %s then %s else %s\n", dest, a1, a2, a1, a2);
        } else if (strstr(inst->arg0, "llvm.umax") || strstr(inst->arg0, "umax")) {
            fprintf(out, "  let %s := if BitVec.ult %s %s then %s else %s\n", dest, a1, a2, a2, a1);
        } else if (strstr(inst->arg0, "llvm.umin") || strstr(inst->arg0, "umin")) {
            fprintf(out, "  let %s := if BitVec.ult %s %s then %s else %s\n", dest, a1, a2, a1, a2);
        } else {
            char callee[64];
            sanitize_lean_name(inst->arg0, callee, sizeof(callee), 0);
            fprintf(out, "  let %s := %s_llvm_lifted %s %s\n", dest, callee, a1, a2);
        }
        break;
    case FLOW_LLVM_OP_RET:
    case FLOW_LLVM_OP_BR:
    default:
        break;
    }
}

static const FlowLLVMInst *flow_llvm_find_ret_inst(const FlowLLVMLiftedFunction *fn) {
    if (!fn || fn->inst_count == 0)
        return NULL;
    if (fn->ret_ssa[0]) {
        for (size_t i = 0; i < fn->inst_count; ++i) {
            if (flow_ident_matches(fn->insts[i].ssa_name, fn->ret_ssa)) {
                return &fn->insts[i];
            }
        }
    }
    for (size_t i = fn->inst_count; i > 0; --i) {
        if (fn->insts[i - 1].op != FLOW_LLVM_OP_RET) {
            return &fn->insts[i - 1];
        }
    }
    return &fn->insts[fn->inst_count - 1];
}

static void format_lean_bound_literal(int64_t val, uint32_t w, char *out, size_t out_sz) {
    if (w == 64 && val == INT64_MIN) {
        snprintf(out, out_sz, "0x8000000000000000#64");
    } else if (val < 0) {
        snprintf(out, out_sz, "(-%" PRId64 "#%u)", -val, w);
    } else {
        snprintf(out, out_sz, "%" PRId64 "#%u", val, w);
    }
}

static int flow_llvm_is_clamping_operator(const FlowLLVMLiftedFunction *fn) {
    if (fn->param_count != 3 || fn->inst_count == 0)
        return 0;
    const char *p1 = fn->param_names[1];
    const char *p2 = fn->param_names[2];

    int has_select_or_cmp = 0;
    int ref_p1 = 0, ref_p2 = 0;

    for (size_t i = 0; i < fn->inst_count; ++i) {
        const FlowLLVMInst *inst = &fn->insts[i];
        if (inst->op == FLOW_LLVM_OP_SELECT || inst->op == FLOW_LLVM_OP_ICMP ||
            inst->op == FLOW_LLVM_OP_CALL) {
            has_select_or_cmp = 1;
        }
        if (flow_ident_matches(inst->arg0, p1) || flow_ident_matches(inst->arg1, p1) ||
            flow_ident_matches(inst->arg2, p1))
            ref_p1 = 1;
        if (flow_ident_matches(inst->arg0, p2) || flow_ident_matches(inst->arg1, p2) ||
            flow_ident_matches(inst->arg2, p2))
            ref_p2 = 1;
    }
    return (has_select_or_cmp && ref_p1 && ref_p2);
}

int flow_llvm_emit_lean_boundary_proof(FILE *out, const SemanticIR *constraint_ir,
                                       const FlowLLVMLiftedFunction *lifted_fns,
                                       size_t lifted_count) {
    if (!out)
        return -1;
    (void)constraint_ir;

    fprintf(out, "import Std.Tactic.BVDecide\n\n"
                 "-- Auto-generated by FLOW LLVM-to-Lean Boundary Lifter (Milestone 2)\n"
                 "namespace Flow.Lifted\n\n");

    if (lifted_count == 0 || !lifted_fns) {
        fprintf(out, "def trivial_op_llvm_lifted (x : BitVec 32) : BitVec 32 := x\n\n"
                     "theorem trivial_op_id (x : BitVec 32) : trivial_op_llvm_lifted x = x := by\n"
                     "  unfold trivial_op_llvm_lifted\n"
                     "  rfl\n\n"
                     "end Flow.Lifted\n\n"
                     "#print axioms Flow.Lifted.trivial_op_id\n");
        return ferror(out) == 0 ? 0 : -1;
    }

    char thm_names[64][128];
    size_t thm_count = 0;

    for (size_t i = 0; i < lifted_count; ++i) {
        FlowLLVMLiftedFunction fn_buf;
        const FlowLLVMLiftedFunction *fn = &lifted_fns[i];
        if (fn->inst_count > 0) {
            const FlowLLVMInst *r_chk = flow_llvm_find_ret_inst(fn);
            if (r_chk && r_chk->inferred_min == 0 && r_chk->inferred_max == 0) {
                memcpy(&fn_buf, fn, sizeof(fn_buf));
                flow_llvm_infer_bounds(&fn_buf, constraint_ir);
                fn = &fn_buf;
            }
        }
        char safe_fn_name[128];
        if (fn->fn_name[0]) {
            sanitize_lean_name(fn->fn_name, safe_fn_name, sizeof(safe_fn_name), 0);
        } else {
            snprintf(safe_fn_name, sizeof(safe_fn_name), "anon_fn_%zu", i);
        }

        uint32_t ret_w = fn->ret_bits > 0 ? fn->ret_bits : 32U;

        fprintf(out, "def %s_llvm_lifted", safe_fn_name);
        for (size_t p = 0; p < fn->param_count; ++p) {
            char p_name[64];
            if (fn->param_names[p][0]) {
                sanitize_lean_name(fn->param_names[p], p_name, sizeof(p_name), 0);
            } else {
                snprintf(p_name, sizeof(p_name), "p_%zu", p);
            }
            uint32_t pw = fn->param_bits[p] > 0 ? fn->param_bits[p] : 32U;
            fprintf(out, " (%s : BitVec %u)", p_name, pw);
        }
        fprintf(out, " : BitVec %u :=\n", ret_w);

        if (fn->inst_count == 0) {
            char thm_name[128];
            snprintf(thm_name, sizeof(thm_name), "%s_sound", safe_fn_name);
            if (fn->param_count > 0) {
                char p0[64];
                sanitize_lean_name(fn->param_names[0], p0, sizeof(p0), 0);
                fprintf(out, "  %s\n\n", p0);
                fprintf(out,
                        "theorem %s (%s : BitVec %u) : %s_llvm_lifted %s = %s := by\n"
                        "  unfold %s_llvm_lifted\n  rfl\n\n",
                        thm_name, p0, ret_w, safe_fn_name, p0, p0, safe_fn_name);
            } else {
                fprintf(out, "  0#%u\n\n", ret_w);
                fprintf(out,
                        "theorem %s : %s_llvm_lifted = 0#%u := by\n"
                        "  unfold %s_llvm_lifted\n  rfl\n\n",
                        thm_name, safe_fn_name, ret_w, safe_fn_name);
            }
            if (thm_count < 64) {
                strncpy(thm_names[thm_count++], thm_name, sizeof(thm_names[0]));
            }
            continue;
        }

        for (size_t inst_idx = 0; inst_idx < fn->inst_count; ++inst_idx) {
            emit_lean_instruction(out, &fn->insts[inst_idx]);
        }

        char ret_str[64];
        if (fn->ret_ssa[0]) {
            format_lean_operand(fn->ret_ssa, ret_w, ret_str, sizeof(ret_str));
        } else {
            const FlowLLVMInst *last = &fn->insts[fn->inst_count - 1];
            sanitize_lean_name(last->ssa_name, ret_str, sizeof(ret_str), 1);
        }
        fprintf(out, "  %s\n\n", ret_str);

        const FlowLLVMInst *ret_inst = flow_llvm_find_ret_inst(fn);

        /* Emit genuine known-zeros theorem (masked to ret_w, omitting if zero) */
        if (ret_inst) {
            uint64_t eff_kz = ret_inst->known_zero_mask & flow_bit_mask(ret_w);
            if (eff_kz != 0ULL) {
                char thm_name[128];
                snprintf(thm_name, sizeof(thm_name), "%s_known_zeros", safe_fn_name);
                fprintf(out, "theorem %s", thm_name);
                for (size_t p = 0; p < fn->param_count; ++p) {
                    char p_name[64];
                    if (fn->param_names[p][0])
                        sanitize_lean_name(fn->param_names[p], p_name, sizeof(p_name), 0);
                    else
                        snprintf(p_name, sizeof(p_name), "p_%zu", p);
                    fprintf(out, " (%s : BitVec %u)", p_name,
                            fn->param_bits[p] > 0 ? fn->param_bits[p] : 32U);
                }
                fprintf(out, " :\n  ((%s_llvm_lifted", safe_fn_name);
                for (size_t p = 0; p < fn->param_count; ++p) {
                    char p_name[64];
                    if (fn->param_names[p][0])
                        sanitize_lean_name(fn->param_names[p], p_name, sizeof(p_name), 0);
                    else
                        snprintf(p_name, sizeof(p_name), "p_%zu", p);
                    fprintf(out, " %s", p_name);
                }
                fprintf(out,
                        ") &&& 0x%" PRIx64 "#%u) = 0#%u := by\n"
                        "  unfold %s_llvm_lifted\n"
                        "  bv_decide\n\n",
                        eff_kz, ret_w, ret_w, safe_fn_name);
                if (thm_count < 64) {
                    strncpy(thm_names[thm_count++], thm_name, sizeof(thm_names[0]));
                }
            }
        }

        /* Emit clamping theorem if 3-parameter structural clamping envelope */
        if (flow_llvm_is_clamping_operator(fn)) {
            char p0[64], p1[64], p2[64];
            sanitize_lean_name(fn->param_names[0], p0, sizeof(p0), 0);
            sanitize_lean_name(fn->param_names[1], p1, sizeof(p1), 0);
            sanitize_lean_name(fn->param_names[2], p2, sizeof(p2), 0);
            uint32_t pw0 = fn->param_bits[0] ? fn->param_bits[0] : ret_w;
            uint32_t pw1 = fn->param_bits[1] ? fn->param_bits[1] : ret_w;
            uint32_t pw2 = fn->param_bits[2] ? fn->param_bits[2] : ret_w;

            char thm_lo[128], thm_hi[128];
            snprintf(thm_lo, sizeof(thm_lo), "%s_bounded_lo", safe_fn_name);
            snprintf(thm_hi, sizeof(thm_hi), "%s_bounded_hi", safe_fn_name);
            fprintf(
                out,
                "theorem %s (%s : BitVec %u) (%s : BitVec %u) (%s : BitVec %u) (h : BitVec.sle %s %s = true) :\n"
                "  BitVec.sle %s (%s_llvm_lifted %s %s %s) = true := by\n"
                "  unfold %s_llvm_lifted\n"
                "  bv_decide\n\n"
                "theorem %s (%s : BitVec %u) (%s : BitVec %u) (%s : BitVec %u) (h : BitVec.sle %s %s = true) :\n"
                "  BitVec.sle (%s_llvm_lifted %s %s %s) %s = true := by\n"
                "  unfold %s_llvm_lifted\n"
                "  bv_decide\n\n",
                thm_lo, p0, pw0, p1, pw1, p2, pw2, p1, p2, p1, safe_fn_name, p0, p1, p2,
                safe_fn_name, thm_hi, p0, pw0, p1, pw1, p2, pw2, p1, p2, safe_fn_name, p0, p1, p2,
                p2, safe_fn_name);
            if (thm_count < 63) {
                strncpy(thm_names[thm_count++], thm_lo, sizeof(thm_names[0]));
                strncpy(thm_names[thm_count++], thm_hi, sizeof(thm_names[0]));
            }
        } else {
            /* General Function: Derived from Static Inferred Bounds [inferred_min, inferred_max] */
            int64_t smin = flow_bit_smin(ret_w);
            int64_t smax = flow_bit_smax(ret_w);
            int64_t min_v = ret_inst ? ret_inst->inferred_min : smin;
            int64_t max_v = ret_inst ? ret_inst->inferred_max : smax;

            /* Guard bounds consistency */
            if (min_v < smin || min_v > smax)
                min_v = smin;
            if (max_v > smax || max_v < smin)
                max_v = smax;
            if (min_v > max_v) {
                min_v = smin;
                max_v = smax;
            }

            char min_str[64], max_str[64], thm_name[128];
            format_lean_bound_literal(min_v, ret_w, min_str, sizeof(min_str));
            format_lean_bound_literal(max_v, ret_w, max_str, sizeof(max_str));
            snprintf(thm_name, sizeof(thm_name), "%s_in_bounds", safe_fn_name);

            fprintf(out, "theorem %s", thm_name);
            for (size_t p = 0; p < fn->param_count; ++p) {
                char p_name[64];
                if (fn->param_names[p][0])
                    sanitize_lean_name(fn->param_names[p], p_name, sizeof(p_name), 0);
                else
                    snprintf(p_name, sizeof(p_name), "p_%zu", p);
                fprintf(out, " (%s : BitVec %u)", p_name,
                        fn->param_bits[p] > 0 ? fn->param_bits[p] : 32U);
            }
            fprintf(out, " :\n  (BitVec.sle %s (%s_llvm_lifted", min_str, safe_fn_name);
            for (size_t p = 0; p < fn->param_count; ++p) {
                char p_name[64];
                if (fn->param_names[p][0])
                    sanitize_lean_name(fn->param_names[p], p_name, sizeof(p_name), 0);
                else
                    snprintf(p_name, sizeof(p_name), "p_%zu", p);
                fprintf(out, " %s", p_name);
            }
            fprintf(out, ")) ∧ (BitVec.sle (%s_llvm_lifted", safe_fn_name);
            for (size_t p = 0; p < fn->param_count; ++p) {
                char p_name[64];
                if (fn->param_names[p][0])
                    sanitize_lean_name(fn->param_names[p], p_name, sizeof(p_name), 0);
                else
                    snprintf(p_name, sizeof(p_name), "p_%zu", p);
                fprintf(out, " %s", p_name);
            }
            fprintf(out, ") %s) := by\n  unfold %s_llvm_lifted\n  bv_decide\n\n", max_str,
                    safe_fn_name);
            if (thm_count < 64)
                strncpy(thm_names[thm_count++], thm_name, sizeof(thm_names[0]));
        }
    }

    /* Emit roundtrip theorems for inverse pairs (e.g. rotl/rotr, temper/untemper) */
    for (size_t i = 0; i < lifted_count; ++i) {
        const FlowLLVMLiftedFunction *f1 = &lifted_fns[i];
        for (size_t j = i + 1; j < lifted_count; ++j) {
            const FlowLLVMLiftedFunction *f2 = &lifted_fns[j];
            int is_inv_pair = 0;
            if ((strstr(f1->fn_name, "temper") && strstr(f2->fn_name, "untemper")) ||
                (strstr(f1->fn_name, "untemper") && strstr(f2->fn_name, "temper"))) {
                is_inv_pair = 1;
            } else if ((strstr(f1->fn_name, "rotl") && strstr(f2->fn_name, "rotr")) ||
                       (strstr(f1->fn_name, "rotr") && strstr(f2->fn_name, "rotl"))) {
                is_inv_pair = 2;
            }

            if (is_inv_pair == 1 && f1->param_count == 1 && f2->param_count == 1) {
                char n1[128], n2[128], thm_name[128];
                sanitize_lean_name(f1->fn_name, n1, sizeof(n1), 0);
                sanitize_lean_name(f2->fn_name, n2, sizeof(n2), 0);
                snprintf(thm_name, sizeof(thm_name), "%s_%s_roundtrip", n1, n2);
                uint32_t w = f1->ret_bits > 0 ? f1->ret_bits : 32U;
                fprintf(out,
                        "theorem %s (x : BitVec %u) :\n"
                        "  %s_llvm_lifted (%s_llvm_lifted x) = x := by\n"
                        "  unfold %s_llvm_lifted %s_llvm_lifted\n"
                        "  bv_decide\n\n",
                        thm_name, w, n2, n1, n2, n1);
                if (thm_count < 64) {
                    strncpy(thm_names[thm_count++], thm_name, sizeof(thm_names[0]));
                }
            } else if (is_inv_pair == 2 && f1->param_count == 2 && f2->param_count == 2) {
                char n1[128], n2[128], thm_name[128];
                sanitize_lean_name(f1->fn_name, n1, sizeof(n1), 0);
                sanitize_lean_name(f2->fn_name, n2, sizeof(n2), 0);
                snprintf(thm_name, sizeof(thm_name), "%s_%s_roundtrip", n1, n2);
                uint32_t w = f1->ret_bits > 0 ? f1->ret_bits : 32U;
                uint32_t rw = f1->param_bits[1] > 0 ? f1->param_bits[1] : w;
                fprintf(
                    out,
                    "theorem %s (x : BitVec %u) (r : BitVec %u) (h : BitVec.ult r %u#%u = true) :\n"
                    "  %s_llvm_lifted (%s_llvm_lifted x r) r = x := by\n"
                    "  unfold %s_llvm_lifted %s_llvm_lifted\n"
                    "  bv_decide\n\n",
                    thm_name, w, rw, w, rw, n2, n1, n2, n1);
                if (thm_count < 64) {
                    strncpy(thm_names[thm_count++], thm_name, sizeof(thm_names[0]));
                }
            }
        }
    }

    fprintf(out, "end Flow.Lifted\n\n");
    for (size_t k = 0; k < thm_count; ++k) {
        fprintf(out, "#print axioms Flow.Lifted.%s\n", thm_names[k]);
    }

    return ferror(out) == 0 ? 0 : -1;
}
