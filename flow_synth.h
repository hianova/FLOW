#ifndef FLOW_SYNTH_H
#define FLOW_SYNTH_H

#include "searu.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * FLOW_SYNTH — Pure-GPR Symbiotic .lean Law Discovery & .flow Synthesizer (v7.1.0)
 * ============================================================================
 * Zero heap (caller-supplied workspace), zero floating-point types/ops,
 * C17 & C++20 clean, -fsanitize=signed-integer-overflow,shift,bounds clean.
 *
 *   Stage A (Symbolic Invariant & Disturbance-Immune Gauge Discovery):
 *     Runs SEARU_SOLVE_INLINE over integer/piecewise-linear channel DAGs and
 *     disturbance witness sieves to synthesize exact identities, convex cones,
 *     and spread-guarded disturbance-immune estimators verified in Lean 4.
 *
 *   Stage B (Symbiotic Multi-Chart Atlas Co-Evolution):
 *     Couples outer SEARU topology/threshold search with inner FLOW 2-Jet
 *     conic/coordinate refinement over closed-loop flow_step_atlas rollouts,
 *     enforcing a Train/Validation generalization gate before emitting .lean+.flow.
 * ============================================================================ */

#define FLOW_SYNTH_MAX_VARS     24U
#define FLOW_SYNTH_MAX_ASSUMES  16U
#define FLOW_SYNTH_MAX_DISTURBS 8U
#define FLOW_SYNTH_MAX_GOALS    8U
#define FLOW_SYNTH_MAX_CANDS    10U
#define FLOW_SYNTH_MAX_SAMPLES  512U
#define FLOW_SYNTH_MAX_CHARTS   8U
#define FLOW_SYNTH_WITNESS_CNT  64U
#define FLOW_SYNTH_MAX_LAWS     16U

typedef enum {
    FLOW_SOP_FREE = 0,
    FLOW_SOP_ADD,
    FLOW_SOP_SUB,
    FLOW_SOP_ADD_SUB,
    FLOW_SOP_RELU_SUB,
    FLOW_SOP_IMIN,
    FLOW_SOP_IMAX,
    FLOW_SOP_IABS_SUB,
    FLOW_SOP_QMID
} flow_synth_op_t;

typedef enum {
    FLOW_SASSUME_LE = 0,
    FLOW_SASSUME_POS,
    FLOW_SASSUME_BAL,
    FLOW_SASSUME_EQ
} flow_synth_assume_kind_t;

typedef enum {
    FLOW_SGOAL_IDENTITY = 0,
    FLOW_SGOAL_NONNEG,
    FLOW_SGOAL_INVARIANT
} flow_synth_goal_kind_t;

typedef struct {
    char name[24];
    flow_synth_op_t op;
    uint8_t a;
    uint8_t b;
    uint8_t c;
    uint32_t free_deps;
} flow_synth_var_t;

typedef struct {
    flow_synth_assume_kind_t kind;
    uint8_t a;
    uint8_t b;
    uint8_t c;
    uint8_t d;
} flow_synth_assume_t;

typedef struct {
    char name[24];
    uint8_t var_idx;
    int8_t sign;
} flow_synth_disturb_t;

typedef struct {
    flow_synth_goal_kind_t kind;
    uint8_t primary;
    uint8_t num_cands;
    uint8_t cands[FLOW_SYNTH_MAX_CANDS];
} flow_synth_goal_t;

typedef struct {
    char domain[32];
    uint32_t k_exp;
    uint32_t num_charts;
    bool is_policy;
    int64_t friction_q16;
    uint16_t active_mask;
    uint16_t harbor_mask;

    uint32_t num_vars;
    flow_synth_var_t vars[FLOW_SYNTH_MAX_VARS];

    uint32_t num_assumes;
    flow_synth_assume_t assumes[FLOW_SYNTH_MAX_ASSUMES];

    uint32_t num_disturbs;
    flow_synth_disturb_t disturbs[FLOW_SYNTH_MAX_DISTURBS];

    uint32_t num_goals;
    flow_synth_goal_t goals[FLOW_SYNTH_MAX_GOALS];

    uint32_t n_train;
    int64_t train_u[FLOW_SYNTH_MAX_SAMPLES];
    int64_t train_y[FLOW_SYNTH_MAX_SAMPLES];
    uint16_t train_mode[FLOW_SYNTH_MAX_SAMPLES];

    uint32_t n_val;
    int64_t val_u[FLOW_SYNTH_MAX_SAMPLES];
    int64_t val_y[FLOW_SYNTH_MAX_SAMPLES];
    uint16_t val_mode[FLOW_SYNTH_MAX_SAMPLES];
} flow_synth_spec_t;

typedef struct {
    flow_synth_goal_kind_t kind;
    bool valid;
    uint8_t primary;
    uint8_t num_cands;
    uint8_t cands[FLOW_SYNTH_MAX_CANDS];
    int8_t coeffs[FLOW_SYNTH_MAX_CANDS];
    uint8_t guard_a_hi;
    uint8_t guard_a_lo;
    uint8_t guard_b_hi;
    uint8_t guard_b_lo;
    uint8_t guard_flip;
    uint64_t naive_drift;
    uint64_t guarded_drift;
} flow_synth_law_t;

typedef struct {
    int64_t b0;
    int64_t b1;
    int64_t b2;
    int64_t b_exit;
    int64_t b_enter;
    int64_t dt_max;
    int64_t u_gain;
    uint8_t next_lo;
    uint8_t next_hi;
    uint16_t kan_lock;
} flow_synth_rule_t;

typedef struct {
    uint32_t num_laws;
    flow_synth_law_t laws[FLOW_SYNTH_MAX_LAWS];

    uint32_t k_exp;
    uint32_t num_charts;
    int64_t init_x_q16;
    uint16_t init_chart;
    uint16_t init_mode;
    flow_synth_rule_t rules[FLOW_SYNTH_MAX_CHARTS];
    FLOW_ALIGN64 flow_cell state_cell;
    FLOW_ALIGN64 flow_cell chart_cells[FLOW_SYNTH_MAX_CHARTS];

    uint64_t base_train_score;
    uint64_t synth_train_score;
    uint64_t base_val_score;
    uint64_t synth_val_score;
    int64_t base_train_pnl;
    int64_t synth_train_pnl;
    int64_t base_val_pnl;
    int64_t synth_val_pnl;
    bool generalized;
} flow_synth_result_t;

typedef struct {
    flow_synth_spec_t spec;
    flow_synth_result_t res;
    int64_t witness[FLOW_SYNTH_WITNESS_CNT][FLOW_SYNTH_MAX_VARS];
    int64_t scratch_vals[FLOW_SYNTH_MAX_VARS];
    int64_t sorted_u[FLOW_SYNTH_MAX_SAMPLES];
    int64_t sorted_y[FLOW_SYNTH_MAX_SAMPLES];
    uint16_t sorted_mode[FLOW_SYNTH_MAX_SAMPLES];
} flow_synth_ws;

/* --- Integer & Q16.16 Parsing Helpers (0 heap, 0 float) --- */

FLOW_INL bool flow_synth__parse_u64(const char *s, uint64_t max_val, uint64_t *out) {
    if (s == NULL || *s == '\0') {
        return false;
    }
    uint32_t base = 10U;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16U;
        s += 2;
        if (*s == '\0') {
            return false;
        }
    }
    uint64_t acc = 0ULL;
    for (const char *p = s; *p != '\0'; ++p) {
        uint32_t d = 0U;
        if (*p >= '0' && *p <= '9') {
            d = (uint32_t)(*p - '0');
        } else if (base == 16U && *p >= 'a' && *p <= 'f') {
            d = 10U + (uint32_t)(*p - 'a');
        } else if (base == 16U && *p >= 'A' && *p <= 'F') {
            d = 10U + (uint32_t)(*p - 'A');
        } else {
            return false;
        }
        if (d >= base || acc > (max_val - (uint64_t)d) / (uint64_t)base) {
            return false;
        }
        acc = acc * (uint64_t)base + (uint64_t)d;
    }
    *out = acc;
    return true;
}

FLOW_INL bool flow_synth__parse_q16(const char *s, int64_t *out) {
    if (s == NULL || *s == '\0') {
        return false;
    }
    if (strncmp(s, "q16:", 4U) == 0) {
        const char *p = s + 4;
        bool neg = false;
        if (*p == '-' || *p == '+') {
            neg = (*p == '-');
            ++p;
        }
        uint64_t mag = 0ULL;
        if (!flow_synth__parse_u64(p, (uint64_t)FLOW_MAX_Q16, &mag)) {
            return false;
        }
        *out = neg ? -(int64_t)mag : (int64_t)mag;
        return true;
    }
    const char *p = s;
    bool neg = false;
    if (*p == '-' || *p == '+') {
        neg = (*p == '-');
        ++p;
    }
    if (*p == '\0' || (*p == '.' && p[1] == '\0')) {
        return false;
    }
    uint64_t whole = 0ULL;
    uint64_t max_whole = ((uint64_t)FLOW_MAX_Q16) >> FLOW_Q16_SHIFT;
    bool has_digit = false;
    while (*p >= '0' && *p <= '9') {
        has_digit = true;
        uint64_t d = (uint64_t)(*p - '0');
        if (whole > (max_whole - d) / 10ULL) {
            return false;
        }
        whole = whole * 10ULL + d;
        ++p;
    }
    uint64_t frac_num = 0ULL;
    uint64_t frac_den = 1ULL;
    if (*p == '.') {
        ++p;
        uint32_t places = 0U;
        while (*p >= '0' && *p <= '9') {
            has_digit = true;
            if (places < 9U) {
                frac_num = frac_num * 10ULL + (uint64_t)(*p - '0');
                frac_den *= 10ULL;
                ++places;
            }
            ++p;
        }
    }
    if (!has_digit || *p != '\0') {
        return false;
    }
    uint64_t frac_q16 = (frac_num * UINT64_C(65536) + (frac_den >> 1U)) / frac_den;
    uint64_t mag_q16 = (whole << FLOW_Q16_SHIFT) + frac_q16;
    if (mag_q16 > (uint64_t)FLOW_MAX_Q16) {
        return false;
    }
    *out = neg ? -(int64_t)mag_q16 : (int64_t)mag_q16;
    return true;
}

FLOW_INL void flow_synth__fmt_q16(int64_t v, char out[32]) {
    bool neg = (v < 0);
    uint64_t mag = flow_abs_i64(v);
    uint64_t whole = mag >> FLOW_Q16_SHIFT;
    uint64_t frac =
        ((mag & UINT64_C(0xFFFF)) * UINT64_C(10000) + UINT64_C(32768)) >> FLOW_Q16_SHIFT;
    if (frac == 10000ULL) {
        ++whole;
        frac = 0ULL;
    }
    if (whole == 0ULL && frac == 0ULL) {
        neg = false;
    }
    snprintf(out, 32U, "%s%llu.%04llu", neg ? "-" : "", (unsigned long long)whole,
             (unsigned long long)frac);
}

FLOW_INL int flow_synth__find_var(const flow_synth_spec_t *sp, const char *name) {
    if (sp == NULL || name == NULL) {
        return -1;
    }
    for (uint32_t i = 0U; i < sp->num_vars; ++i) {
        if (strcmp(sp->vars[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

FLOW_INL void flow_synth__eval_vars(const flow_synth_spec_t *sp,
                                    int64_t vals[FLOW_SYNTH_MAX_VARS]) {
    for (uint32_t i = 0U; i < sp->num_vars; ++i) {
        const flow_synth_var_t *v = &sp->vars[i];
        int64_t va = vals[v->a];
        int64_t vb = vals[v->b];
        int64_t vc = vals[v->c];
        switch (v->op) {
        case FLOW_SOP_FREE:
            break;
        case FLOW_SOP_ADD:
            vals[i] = va + vb;
            break;
        case FLOW_SOP_SUB:
            vals[i] = va - vb;
            break;
        case FLOW_SOP_ADD_SUB:
            vals[i] = va + vb - vc;
            break;
        case FLOW_SOP_RELU_SUB: {
            int64_t d = va - vb;
            vals[i] = (d > 0) ? d : INT64_C(0);
            break;
        }
        case FLOW_SOP_IMIN:
            vals[i] = (va <= vb) ? va : vb;
            break;
        case FLOW_SOP_IMAX:
            vals[i] = (va >= vb) ? va : vb;
            break;
        case FLOW_SOP_IABS_SUB: {
            int64_t d = va - vb;
            vals[i] = (d >= 0) ? d : -d;
            break;
        }
        case FLOW_SOP_QMID:
            vals[i] = flow_ashr_i64(va + vb, 1U);
            break;
        }
    }
}

/* --- Spec Parser (.fsyn -> flow_synth_spec_t) --- */

FLOW_INL bool flow_synth_parse_file(const char *path, flow_synth_spec_t *sp) {
    if (path == NULL || sp == NULL) {
        return false;
    }
    memset(sp, 0, sizeof(*sp));
    snprintf(sp->domain, sizeof(sp->domain), "FlowSynth");
    sp->k_exp = 2U;
    sp->num_charts = 4U;
    sp->active_mask = 0x0001U;
    sp->harbor_mask = 0x0003U;

    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        fprintf(stderr, "flow_synth: cannot open spec '%s'\n", path);
        return false;
    }

    char line[512];
    bool ok = true;
    while (ok && fgets(line, (int)sizeof(line), fp) != NULL) {
        char *hash = strchr(line, '#');
        if (hash != NULL) {
            *hash = '\0';
        }
        char *toks[16];
        uint32_t nt = 0U;
        char *p = line;
        while (*p != '\0' && nt < 16U) {
            while (*p != '\0' && isspace((unsigned char)*p)) {
                ++p;
            }
            if (*p == '\0') {
                break;
            }
            toks[nt++] = p;
            while (*p != '\0' && !isspace((unsigned char)*p)) {
                ++p;
            }
            if (*p != '\0') {
                *p = '\0';
                ++p;
            }
        }
        if (nt == 0U) {
            continue;
        }

        if (strcmp(toks[0], "domain") == 0 && nt == 2U) {
            snprintf(sp->domain, sizeof(sp->domain), "%s", toks[1]);
        } else if (strcmp(toks[0], "charts") == 0 && nt == 2U) {
            uint64_t nc = 0ULL;
            if (!flow_synth__parse_u64(toks[1], FLOW_SYNTH_MAX_CHARTS, &nc) || nc == 0ULL ||
                (nc & (nc - 1ULL)) != 0ULL) {
                ok = false;
                break;
            }
            sp->num_charts = (uint32_t)nc;
            sp->k_exp = 0U;
            while ((UINT32_C(1) << sp->k_exp) < sp->num_charts) {
                ++sp->k_exp;
            }
        } else if (strcmp(toks[0], "objective") == 0 && nt == 2U) {
            if (strcmp(toks[1], "policy") == 0) {
                sp->is_policy = true;
            } else if (strcmp(toks[1], "approx") == 0) {
                sp->is_policy = false;
            } else {
                ok = false;
            }
        } else if (strcmp(toks[0], "friction") == 0 && nt == 2U) {
            if (!flow_synth__parse_q16(toks[1], &sp->friction_q16) || sp->friction_q16 < 0) {
                ok = false;
            }
        } else if (strcmp(toks[0], "locks") == 0 && nt == 3U) {
            uint64_t act = 0ULL;
            uint64_t hrb = 0ULL;
            if (!flow_synth__parse_u64(toks[1], UINT64_C(0xFFFF), &act) ||
                !flow_synth__parse_u64(toks[2], UINT64_C(0xFFFF), &hrb)) {
                ok = false;
            } else {
                sp->active_mask = (uint16_t)act;
                sp->harbor_mask = (uint16_t)hrb;
            }
        } else if (strcmp(toks[0], "var") == 0 && nt >= 3U) {
            if (sp->num_vars >= FLOW_SYNTH_MAX_VARS) {
                ok = false;
                break;
            }
            uint32_t vid = sp->num_vars++;
            flow_synth_var_t *v = &sp->vars[vid];
            memset(v, 0, sizeof(*v));
            snprintf(v->name, sizeof(v->name), "%s", toks[1]);
            if (strcmp(toks[2], "free") == 0 && nt == 3U) {
                v->op = FLOW_SOP_FREE;
                v->free_deps = UINT32_C(1) << vid;
            } else if (nt == 5U) {
                int ia = flow_synth__find_var(sp, toks[3]);
                int ib = flow_synth__find_var(sp, toks[4]);
                if (ia < 0 || ib < 0) {
                    ok = false;
                    break;
                }
                v->a = (uint8_t)ia;
                v->b = (uint8_t)ib;
                v->free_deps = sp->vars[ia].free_deps | sp->vars[ib].free_deps;
                if (strcmp(toks[2], "add") == 0) {
                    v->op = FLOW_SOP_ADD;
                } else if (strcmp(toks[2], "sub") == 0) {
                    v->op = FLOW_SOP_SUB;
                } else if (strcmp(toks[2], "relu_sub") == 0) {
                    v->op = FLOW_SOP_RELU_SUB;
                } else if (strcmp(toks[2], "imin") == 0) {
                    v->op = FLOW_SOP_IMIN;
                } else if (strcmp(toks[2], "imax") == 0) {
                    v->op = FLOW_SOP_IMAX;
                } else if (strcmp(toks[2], "iabs_sub") == 0) {
                    v->op = FLOW_SOP_IABS_SUB;
                } else if (strcmp(toks[2], "qmid") == 0) {
                    v->op = FLOW_SOP_QMID;
                } else {
                    ok = false;
                }
            } else if (strcmp(toks[2], "add_sub") == 0 && nt == 6U) {
                int ia = flow_synth__find_var(sp, toks[3]);
                int ib = flow_synth__find_var(sp, toks[4]);
                int ic = flow_synth__find_var(sp, toks[5]);
                if (ia < 0 || ib < 0 || ic < 0) {
                    ok = false;
                    break;
                }
                v->op = FLOW_SOP_ADD_SUB;
                v->a = (uint8_t)ia;
                v->b = (uint8_t)ib;
                v->c = (uint8_t)ic;
                v->free_deps =
                    sp->vars[ia].free_deps | sp->vars[ib].free_deps | sp->vars[ic].free_deps;
            } else {
                ok = false;
            }
        } else if (strcmp(toks[0], "assume_le") == 0 && nt == 3U) {
            int ia = flow_synth__find_var(sp, toks[1]);
            int ib = flow_synth__find_var(sp, toks[2]);
            if (ia < 0 || ib < 0 || sp->num_assumes >= FLOW_SYNTH_MAX_ASSUMES) {
                ok = false;
                break;
            }
            flow_synth_assume_t *as = &sp->assumes[sp->num_assumes++];
            as->kind = FLOW_SASSUME_LE;
            as->a = (uint8_t)ia;
            as->b = (uint8_t)ib;
        } else if (strcmp(toks[0], "assume_pos") == 0 && nt == 2U) {
            int ia = flow_synth__find_var(sp, toks[1]);
            if (ia < 0 || sp->num_assumes >= FLOW_SYNTH_MAX_ASSUMES) {
                ok = false;
                break;
            }
            flow_synth_assume_t *as = &sp->assumes[sp->num_assumes++];
            as->kind = FLOW_SASSUME_POS;
            as->a = (uint8_t)ia;
        } else if (strcmp(toks[0], "assume_bal") == 0 && nt == 5U) {
            int ia = flow_synth__find_var(sp, toks[1]);
            int ib = flow_synth__find_var(sp, toks[2]);
            int ic = flow_synth__find_var(sp, toks[3]);
            int id = flow_synth__find_var(sp, toks[4]);
            if (ia < 0 || ib < 0 || ic < 0 || id < 0 || sp->num_assumes >= FLOW_SYNTH_MAX_ASSUMES) {
                ok = false;
                break;
            }
            flow_synth_assume_t *as = &sp->assumes[sp->num_assumes++];
            as->kind = FLOW_SASSUME_BAL;
            as->a = (uint8_t)ia;
            as->b = (uint8_t)ib;
            as->c = (uint8_t)ic;
            as->d = (uint8_t)id;
        } else if (strcmp(toks[0], "assume_eq") == 0 && nt == 3U) {
            int ia = flow_synth__find_var(sp, toks[1]);
            int ib = flow_synth__find_var(sp, toks[2]);
            if (ia < 0 || ib < 0 || sp->num_assumes >= FLOW_SYNTH_MAX_ASSUMES) {
                ok = false;
                break;
            }
            flow_synth_assume_t *as = &sp->assumes[sp->num_assumes++];
            as->kind = FLOW_SASSUME_EQ;
            as->a = (uint8_t)ia;
            as->b = (uint8_t)ib;
        } else if (strcmp(toks[0], "disturb") == 0 && nt == 4U) {
            int iv = flow_synth__find_var(sp, toks[2]);
            if (iv < 0 || sp->vars[iv].op != FLOW_SOP_FREE ||
                sp->num_disturbs >= FLOW_SYNTH_MAX_DISTURBS) {
                ok = false;
                break;
            }
            flow_synth_disturb_t *dt = &sp->disturbs[sp->num_disturbs++];
            snprintf(dt->name, sizeof(dt->name), "%s", toks[1]);
            dt->var_idx = (uint8_t)iv;
            if (strcmp(toks[3], "+") == 0) {
                dt->sign = 1;
            } else if (strcmp(toks[3], "-") == 0) {
                dt->sign = -1;
            } else {
                ok = false;
            }
        } else if ((strcmp(toks[0], "find_identity") == 0 ||
                    strcmp(toks[0], "find_invariant") == 0) &&
                   nt >= 3U && nt <= 2U + FLOW_SYNTH_MAX_CANDS) {
            int ip = flow_synth__find_var(sp, toks[1]);
            if (ip < 0 || sp->num_goals >= FLOW_SYNTH_MAX_GOALS) {
                ok = false;
                break;
            }
            flow_synth_goal_t *gl = &sp->goals[sp->num_goals++];
            gl->kind = (strcmp(toks[0], "find_identity") == 0) ? FLOW_SGOAL_IDENTITY
                                                               : FLOW_SGOAL_INVARIANT;
            gl->primary = (uint8_t)ip;
            gl->num_cands = 0U;
            for (uint32_t j = 2U; j < nt; ++j) {
                int ic = flow_synth__find_var(sp, toks[j]);
                if (ic < 0) {
                    ok = false;
                    break;
                }
                gl->cands[gl->num_cands++] = (uint8_t)ic;
            }
        } else if (strcmp(toks[0], "find_nonneg") == 0 && nt >= 3U &&
                   nt <= 1U + FLOW_SYNTH_MAX_CANDS) {
            if (sp->num_goals >= FLOW_SYNTH_MAX_GOALS) {
                ok = false;
                break;
            }
            flow_synth_goal_t *gl = &sp->goals[sp->num_goals++];
            gl->kind = FLOW_SGOAL_NONNEG;
            gl->primary = 0U;
            gl->num_cands = 0U;
            for (uint32_t j = 1U; j < nt; ++j) {
                int ic = flow_synth__find_var(sp, toks[j]);
                if (ic < 0) {
                    ok = false;
                    break;
                }
                gl->cands[gl->num_cands++] = (uint8_t)ic;
            }
        } else if ((strcmp(toks[0], "train") == 0 || strcmp(toks[0], "val") == 0) &&
                   (nt == 3U || nt == 4U)) {
            int64_t u = 0;
            int64_t y = 0;
            uint64_t md = sp->harbor_mask;
            if (!flow_synth__parse_q16(toks[1], &u) || !flow_synth__parse_q16(toks[2], &y) ||
                (nt == 4U && !flow_synth__parse_u64(toks[3], UINT64_C(0xFFFF), &md))) {
                ok = false;
                break;
            }
            if (strcmp(toks[0], "train") == 0) {
                if (sp->n_train >= FLOW_SYNTH_MAX_SAMPLES) {
                    ok = false;
                    break;
                }
                uint32_t idx = sp->n_train++;
                sp->train_u[idx] = u;
                sp->train_y[idx] = y;
                sp->train_mode[idx] = (uint16_t)md;
            } else {
                if (sp->n_val >= FLOW_SYNTH_MAX_SAMPLES) {
                    ok = false;
                    break;
                }
                uint32_t idx = sp->n_val++;
                sp->val_u[idx] = u;
                sp->val_y[idx] = y;
                sp->val_mode[idx] = (uint16_t)md;
            }
        } else {
            fprintf(stderr, "flow_synth: unknown or malformed directive '%s'\n", toks[0]);
            ok = false;
        }
    }
    fclose(fp);
    return ok;
}

/* --- Stage A: SEARU Symbolic Law & Disturbance-Immune Gauge Discovery --- */

FLOW_INL int8_t flow_synth__decode_coeff3(uint64_t bits, uint32_t idx) {
    uint32_t code = (uint32_t)((bits >> (3U * idx)) & UINT64_C(7));
    switch (code) {
    case 1U:
        return 1;
    case 2U:
        return -1;
    case 3U:
        return 2;
    case 4U:
        return -2;
    case 6U:
        return 1;
    case 7U:
        return -1;
    default:
        return 0;
    }
}

FLOW_INL uint64_t flow_synth__gcd_u64(uint64_t a, uint64_t b) {
    while (b != 0ULL) {
        uint64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

FLOW_INL void flow_synth__build_witnesses(flow_synth_ws *ws) {
    const flow_synth_spec_t *sp = &ws->spec;
    uint64_t rng = UINT64_C(0xD1B54A32D192ED03);

    for (uint32_t w = 0U; w < FLOW_SYNTH_WITNESS_CNT; ++w) {
        int64_t *vals = ws->witness[w];
        for (uint32_t i = 0U; i < sp->num_vars; ++i) {
            if (sp->vars[i].op == FLOW_SOP_FREE) {
                uint64_t r = searu_rng_next(&rng);
                int64_t v = (int64_t)(r % UINT64_C(2001)) - INT64_C(1000);
                if (w < 8U) {
                    v = (int64_t)((r % UINT64_C(31))) - INT64_C(15);
                } else if (w >= 48U) {
                    v = (int64_t)(r % UINT64_C(1500)) + INT64_C(1);
                }
                vals[i] = v;
            } else {
                vals[i] = 0;
            }
        }
        /* Inject sharp equality corner cases on odd/even witnesses 0..7 */
        if (w < 4U && sp->num_vars >= 2U) {
            vals[1] = vals[0];
        }
        for (uint32_t pass = 0U; pass < 3U; ++pass) {
            for (uint32_t ai = 0U; ai < sp->num_assumes; ++ai) {
                const flow_synth_assume_t *as = &sp->assumes[ai];
                if (as->kind == FLOW_SASSUME_POS && sp->vars[as->a].op == FLOW_SOP_FREE) {
                    if (vals[as->a] <= 0) {
                        uint64_t r = searu_rng_next(&rng);
                        vals[as->a] = (int64_t)(r % UINT64_C(200)) + INT64_C(1);
                    }
                } else if (as->kind == FLOW_SASSUME_LE && sp->vars[as->b].op == FLOW_SOP_FREE) {
                    if (vals[as->a] > vals[as->b]) {
                        uint64_t r = searu_rng_next(&rng);
                        int64_t gap = (int64_t)(r % UINT64_C(20));
                        if ((w & 1U) != 0U) {
                            gap |= 1;
                        }
                        vals[as->b] = vals[as->a] + gap;
                    }
                } else if (as->kind == FLOW_SASSUME_BAL && sp->vars[as->c].op == FLOW_SOP_FREE) {
                    int64_t diff = vals[as->a] - vals[as->b];
                    if (diff < 0) {
                        diff = -diff;
                        vals[as->a] = vals[as->b] + diff;
                    }
                    vals[as->c] = vals[as->d] + diff;
                } else if (as->kind == FLOW_SASSUME_EQ && sp->vars[as->a].op == FLOW_SOP_FREE) {
                    flow_synth__eval_vars(sp, vals);
                    vals[as->a] = vals[as->b];
                }
            }
            flow_synth__eval_vars(sp, vals);
        }
    }
}

FLOW_INL void flow_synth__discover_identity(flow_synth_ws *ws, uint32_t gidx,
                                            flow_synth_law_t *law) {
    const flow_synth_spec_t *sp = &ws->spec;
    const flow_synth_goal_t *gl = &sp->goals[gidx];
    memset(law, 0, sizeof(*law));
    law->kind = FLOW_SGOAL_IDENTITY;
    law->primary = gl->primary;
    law->num_cands = gl->num_cands;
    for (uint32_t j = 0U; j < gl->num_cands; ++j) {
        law->cands[j] = gl->cands[j];
    }

    uint32_t m = gl->num_cands;
    uint32_t nbits = 3U * m;
    uint64_t init_can[4] = {UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0)};
    uint64_t best_bits = UINT64_C(0);
    uint64_t best_l1 = UINT64_C(999999);
    bool found_exact = false;
    searu_result sr;

    SEARU_SOLVE_INLINE(
        &sr, nbits, init_can, NULL, UINT64_C(0xA11CE5EED1) + gidx, 4096U, cand, frust, energy, {
            uint64_t bits = cand[0];
            uint64_t err_sum = UINT64_C(0);
            uint64_t l1_sum = UINT64_C(0);
            int8_t c_loc[FLOW_SYNTH_MAX_CANDS];
            for (uint32_t j = 0U; j < m; ++j) {
                c_loc[j] = flow_synth__decode_coeff3(bits, j);
                l1_sum += flow_abs_i64((int64_t)c_loc[j]);
            }
            if (l1_sum == UINT64_C(0)) {
                energy = UINT64_C(1) << 50U;
                frust[0] = (UINT64_C(1) << nbits) - UINT64_C(1);
            } else {
                for (uint32_t w = 0U; w < FLOW_SYNTH_WITNESS_CNT; ++w) {
                    int64_t acc = INT64_C(0);
                    for (uint32_t j = 0U; j < m; ++j) {
                        acc += (int64_t)c_loc[j] * ws->witness[w][gl->cands[j]];
                    }
                    int64_t diff = acc - ws->witness[w][gl->primary];
                    if (diff != INT64_C(0)) {
                        err_sum += flow_abs_i64(diff);
                        for (uint32_t j = 0U; j < m; ++j) {
                            if (ws->witness[w][gl->cands[j]] != INT64_C(0)) {
                                frust[0] |= UINT64_C(7) << (3U * j);
                            }
                        }
                    }
                }
                if (err_sum == UINT64_C(0) && l1_sum < best_l1) {
                    best_l1 = l1_sum;
                    best_bits = bits;
                    found_exact = true;
                }
                if (err_sum == UINT64_C(0)) {
                    for (uint32_t j = 0U; j < m; ++j) {
                        if (c_loc[j] != 0) {
                            frust[0] |= UINT64_C(7) << (3U * j);
                        }
                    }
                }
                energy = (err_sum > UINT64_C(0) ? (UINT64_C(32) + ((err_sum + UINT64_C(31)) >> 5U))
                                                : UINT64_C(0)) +
                         l1_sum;
            }
        });

    if (found_exact) {
        law->valid = true;
        for (uint32_t j = 0U; j < m; ++j) {
            law->coeffs[j] = flow_synth__decode_coeff3(best_bits, j);
        }
    }
}

FLOW_INL void flow_synth__discover_nonneg(flow_synth_ws *ws, uint32_t gidx, flow_synth_law_t *law) {
    const flow_synth_spec_t *sp = &ws->spec;
    const flow_synth_goal_t *gl = &sp->goals[gidx];
    memset(law, 0, sizeof(*law));
    law->kind = FLOW_SGOAL_NONNEG;
    law->num_cands = gl->num_cands;
    for (uint32_t j = 0U; j < gl->num_cands; ++j) {
        law->cands[j] = gl->cands[j];
    }

    uint32_t m = gl->num_cands;
    uint32_t nbits = 3U * m;
    uint64_t init_can[4] = {UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0)};
    uint64_t best_bits = UINT64_C(0);
    uint64_t best_score = UINT64_C(999999);
    bool found = false;
    searu_result sr;

    SEARU_SOLVE_INLINE(
        &sr, nbits, init_can, NULL, UINT64_C(0xB0BB1E5EED2) + gidx, 4096U, cand, frust, energy, {
            uint64_t bits = cand[0];
            int8_t c_loc[FLOW_SYNTH_MAX_CANDS];
            uint64_t l1_sum = UINT64_C(0);
            int64_t coeff_sum = INT64_C(0);
            int64_t mom1 = INT64_C(0);
            uint32_t n_nz = 0U;
            for (uint32_t j = 0U; j < m; ++j) {
                c_loc[j] = flow_synth__decode_coeff3(bits, j);
                if (c_loc[j] != 0) {
                    ++n_nz;
                }
                l1_sum += flow_abs_i64((int64_t)c_loc[j]);
                coeff_sum += (int64_t)c_loc[j];
                mom1 += (int64_t)j * (int64_t)c_loc[j];
            }
            if (l1_sum < UINT64_C(2)) {
                energy = UINT64_C(1) << 50U;
                frust[0] = (UINT64_C(1) << nbits) - UINT64_C(1);
            } else {
                uint64_t neg_viol = UINT64_C(0);
                uint32_t pos_cnt = 0U;
                for (uint32_t w = 0U; w < FLOW_SYNTH_WITNESS_CNT; ++w) {
                    int64_t val = INT64_C(0);
                    for (uint32_t j = 0U; j < m; ++j) {
                        val += (int64_t)c_loc[j] * ws->witness[w][gl->cands[j]];
                    }
                    if (val < INT64_C(0)) {
                        neg_viol += flow_abs_i64(val);
                    } else if (val > INT64_C(0)) {
                        ++pos_cnt;
                    }
                }
                uint64_t gauge_pen = flow_abs_i64(coeff_sum) * UINT64_C(4096);
                uint64_t triv_pen = (pos_cnt < 4U) ? (UINT64_C(1) << 36U) : UINT64_C(0);
                uint64_t arity_pen =
                    (m >= 3U && n_nz < m) ? ((uint64_t)(m - n_nz) * UINT64_C(64)) : UINT64_C(0);
                uint64_t mom1_pen = (m >= 3U) ? (flow_abs_i64(mom1) * UINT64_C(4)) : UINT64_C(0);
                uint64_t score = l1_sum + arity_pen + mom1_pen;
                if (neg_viol == UINT64_C(0) && gauge_pen == UINT64_C(0) &&
                    triv_pen == UINT64_C(0) && score < best_score) {
                    best_score = score;
                    best_bits = bits;
                    found = true;
                }
                if (neg_viol > UINT64_C(0) || triv_pen > UINT64_C(0) || arity_pen > UINT64_C(0) ||
                    mom1_pen > UINT64_C(0)) {
                    frust[0] = (UINT64_C(1) << nbits) - UINT64_C(1);
                }
                energy = neg_viol * UINT64_C(256) + gauge_pen + triv_pen + arity_pen +
                         mom1_pen * UINT64_C(16) + l1_sum;
            }
        });

    if (found) {
        uint64_t g = 0ULL;
        for (uint32_t j = 0U; j < m; ++j) {
            law->coeffs[j] = flow_synth__decode_coeff3(best_bits, j);
            uint64_t a = flow_abs_i64((int64_t)law->coeffs[j]);
            if (a != 0ULL) {
                g = (g == 0ULL) ? a : flow_synth__gcd_u64(g, a);
            }
        }
        if (g > 1ULL) {
            for (uint32_t j = 0U; j < m; ++j) {
                law->coeffs[j] = (int8_t)(law->coeffs[j] / (int64_t)g);
            }
        }
        law->valid = true;
    }
}

FLOW_INL void flow_synth__discover_invariant(flow_synth_ws *ws, uint32_t gidx,
                                             flow_synth_law_t *law) {
    const flow_synth_spec_t *sp = &ws->spec;
    const flow_synth_goal_t *gl = &sp->goals[gidx];
    memset(law, 0, sizeof(*law));
    law->kind = FLOW_SGOAL_INVARIANT;
    law->primary = gl->primary;
    law->num_cands = gl->num_cands;
    for (uint32_t j = 0U; j < gl->num_cands; ++j) {
        law->cands[j] = gl->cands[j];
    }

    if (sp->num_disturbs == 0U) {
        return;
    }

    /* Check baseline naive_var drift across declared disturbances */
    uint64_t naive_drift = 0ULL;
    const int64_t deltas[2] = {INT64_C(2), INT64_C(18)};
    for (uint32_t w = 0U; w < FLOW_SYNTH_WITNESS_CNT; ++w) {
        int64_t base_v = ws->witness[w][gl->primary];
        for (uint32_t di = 0U; di < sp->num_disturbs; ++di) {
            for (uint32_t k = 0U; k < 2U; ++k) {
                memcpy(ws->scratch_vals, ws->witness[w], sizeof(ws->scratch_vals));
                uint8_t fv = sp->disturbs[di].var_idx;
                ws->scratch_vals[fv] += (int64_t)sp->disturbs[di].sign * deltas[k];
                flow_synth__eval_vars(sp, ws->scratch_vals);
                int64_t diff = ws->scratch_vals[gl->primary] - base_v;
                naive_drift += flow_abs_i64(diff);
            }
        }
    }
    law->naive_drift = naive_drift;

    /* Search dual affine formula over cands matching primary cleanly on unperturbed witnesses */
    uint32_t m = gl->num_cands;
    uint32_t nbits = 3U * m;
    uint64_t init_can[4] = {UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0)};
    int8_t best_c[FLOW_SYNTH_MAX_CANDS];
    memset(best_c, 0, sizeof(best_c));
    uint64_t best_guard_drift = ~UINT64_C(0);
    uint64_t best_l1 = UINT64_C(999999);

    for (uint32_t ai = 0U; ai < sp->num_assumes; ++ai) {
        if (sp->assumes[ai].kind != FLOW_SASSUME_BAL) {
            continue;
        }
        const flow_synth_assume_t *bal = &sp->assumes[ai];
        for (uint32_t flip = 0U; flip < 2U; ++flip) {
            uint8_t a_hi = flip ? bal->c : bal->a;
            uint8_t a_lo = flip ? bal->d : bal->b;
            uint8_t b_hi = flip ? bal->a : bal->c;
            uint8_t b_lo = flip ? bal->b : bal->d;

            uint64_t cand_bits = UINT64_C(0);
            uint64_t cand_l1 = UINT64_C(999999);
            bool found_eq = false;
            searu_result sr;

            /* Disallow dual formula depending on the primary side's disturbed free vars */
            uint32_t forbid_mask = sp->vars[a_hi].free_deps | sp->vars[a_lo].free_deps;
            uint64_t pin[4] = {UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0)};
            for (uint32_t j = 0U; j < m; ++j) {
                if ((sp->vars[gl->cands[j]].free_deps & forbid_mask) != 0U) {
                    pin[0] |= UINT64_C(7) << (3U * j);
                }
            }

            SEARU_SOLVE_INLINE(
                &sr, nbits, init_can, pin, UINT64_C(0x14FA41A47) + ai * 2U + flip, 4096U, cand,
                frust, energy, {
                    uint64_t bits = cand[0] & ~pin[0];
                    uint64_t err_sum = UINT64_C(0);
                    uint64_t l1_sum = UINT64_C(0);
                    int8_t c_loc[FLOW_SYNTH_MAX_CANDS];
                    for (uint32_t j = 0U; j < m; ++j) {
                        c_loc[j] = flow_synth__decode_coeff3(bits, j);
                        l1_sum += flow_abs_i64((int64_t)c_loc[j]);
                    }
                    if (l1_sum == UINT64_C(0)) {
                        energy = UINT64_C(1) << 50U;
                        frust[0] = ((UINT64_C(1) << nbits) - UINT64_C(1)) & ~pin[0];
                    } else {
                        uint64_t fail_cnt = UINT64_C(0);
                        for (uint32_t w = 0U; w < FLOW_SYNTH_WITNESS_CNT; ++w) {
                            int64_t acc = INT64_C(0);
                            for (uint32_t j = 0U; j < m; ++j) {
                                acc += (int64_t)c_loc[j] * ws->witness[w][gl->cands[j]];
                            }
                            int64_t diff = acc - ws->witness[w][gl->primary];
                            if (diff != INT64_C(0)) {
                                ++fail_cnt;
                                err_sum += flow_abs_i64(diff);
                                frust[0] = ((UINT64_C(1) << nbits) - UINT64_C(1)) & ~pin[0];
                            }
                        }
                        if (err_sum == UINT64_C(0) && l1_sum < cand_l1) {
                            cand_l1 = l1_sum;
                            cand_bits = bits;
                            found_eq = true;
                        }
                        energy = (fail_cnt * UINT64_C(8) +
                                  ((err_sum > UINT64_C(0))
                                       ? (UINT64_C(32) + ((err_sum + UINT64_C(63)) >> 6U))
                                       : UINT64_C(0))) +
                                 l1_sum;
                    }
                });

            if (!found_eq) {
                continue;
            }
            int8_t c_try[FLOW_SYNTH_MAX_CANDS];
            for (uint32_t j = 0U; j < m; ++j) {
                c_try[j] = flow_synth__decode_coeff3(cand_bits, j);
            }

            /* Test guarded estimator under ALL declared disturbances */
            uint64_t g_drift = 0ULL;
            for (uint32_t w = 0U; w < FLOW_SYNTH_WITNESS_CNT; ++w) {
                int64_t base_v = ws->witness[w][gl->primary];
                for (uint32_t di = 0U; di < sp->num_disturbs; ++di) {
                    for (uint32_t k = 0U; k < 2U; ++k) {
                        memcpy(ws->scratch_vals, ws->witness[w], sizeof(ws->scratch_vals));
                        uint8_t fv = sp->disturbs[di].var_idx;
                        ws->scratch_vals[fv] += (int64_t)sp->disturbs[di].sign * deltas[k];
                        flow_synth__eval_vars(sp, ws->scratch_vals);
                        int64_t sp_a = ws->scratch_vals[a_hi] - ws->scratch_vals[a_lo];
                        int64_t sp_b = ws->scratch_vals[b_hi] - ws->scratch_vals[b_lo];
                        int64_t est = INT64_C(0);
                        if (sp_a <= sp_b) {
                            est = ws->scratch_vals[gl->primary];
                        } else {
                            for (uint32_t j = 0U; j < m; ++j) {
                                est += (int64_t)c_try[j] * ws->scratch_vals[gl->cands[j]];
                            }
                        }
                        g_drift += flow_abs_i64(est - base_v);
                    }
                }
            }
            if (g_drift < best_guard_drift || (g_drift == best_guard_drift && cand_l1 < best_l1)) {
                best_guard_drift = g_drift;
                best_l1 = cand_l1;
                memcpy(best_c, c_try, sizeof(best_c));
                law->guard_a_hi = a_hi;
                law->guard_a_lo = a_lo;
                law->guard_b_hi = b_hi;
                law->guard_b_lo = b_lo;
                law->guard_flip = (uint8_t)flip;
                law->guarded_drift = g_drift;
                law->valid = (g_drift == 0ULL && naive_drift > 0ULL);
            }
        }
    }
    memcpy(law->coeffs, best_c, sizeof(law->coeffs));
}

FLOW_INL void flow_synth_discover_laws(flow_synth_ws *ws) {
    ws->res.num_laws = 0U;
    if (ws->spec.num_goals == 0U) {
        return;
    }
    flow_synth__build_witnesses(ws);
    for (uint32_t g = 0U; g < ws->spec.num_goals && ws->res.num_laws < FLOW_SYNTH_MAX_LAWS; ++g) {
        flow_synth_law_t *dst = &ws->res.laws[ws->res.num_laws++];
        if (ws->spec.goals[g].kind == FLOW_SGOAL_IDENTITY) {
            flow_synth__discover_identity(ws, g, dst);
        } else if (ws->spec.goals[g].kind == FLOW_SGOAL_NONNEG) {
            flow_synth__discover_nonneg(ws, g, dst);
        } else {
            flow_synth__discover_invariant(ws, g, dst);
        }
    }
}

/* --- Cell Minting Helper Matching Flow.lean Exact Bit Layout --- */

FLOW_INL void flow_synth__mint_cells(flow_synth_result_t *r) {
    uint32_t nc = r->num_charts;
    uint32_t cap = nc - 1U;
    int64_t x0 = flow_clamp_sym(r->init_x_q16, FLOW_MAX_Q16);
    uint16_t ch0 = (uint16_t)(r->init_chart & cap);
    r->state_cell[0] = UINT64_C(0);
    r->state_cell[1] = UINT64_C(0);
    r->state_cell[2] = UINT64_C(0);
    r->state_cell[3] = (uint64_t)x0;
    r->state_cell[4] = UINT64_C(0);
    r->state_cell[5] = ((uint64_t)ch0 << 16U) | (uint64_t)r->init_mode;
    r->state_cell[6] = UINT64_C(0);
    r->state_cell[7] = UINT64_C(0);

    for (uint32_t i = 0U; i < nc; ++i) {
        const flow_synth_rule_t *rl = &r->rules[i];
        int64_t c0 = flow_clamp_sym(rl->b0, FLOW_MAX_Q16);
        int64_t c1 = flow_clamp_sym(rl->b1, FLOW_MAX_Q16);
        int64_t c2 = flow_clamp_sym(rl->b2, FLOW_MAX_Q16);
        int64_t lo = flow_clamp_sym(rl->b_exit, FLOW_MAX_Q16);
        int64_t hi = flow_clamp_sym(rl->b_enter, FLOW_MAX_Q16);
        if (hi < lo) {
            hi = lo;
        }
        int64_t dt = flow_clamp_sym(rl->dt_max, FLOW_MAX_Q16);
        if (dt < 1) {
            dt = 1;
        }
        int64_t ug = flow_clamp_sym(rl->u_gain, FLOW_MAX_Q16);
        uint32_t nlo = (uint32_t)(rl->next_lo & cap);
        uint32_t nhi = (uint32_t)(rl->next_hi & cap);
        r->chart_cells[i][0] = (uint64_t)c0;
        r->chart_cells[i][1] = (uint64_t)c1;
        r->chart_cells[i][2] = (uint64_t)c2;
        r->chart_cells[i][3] =
            ((uint64_t)(uint32_t)(int32_t)lo << 32U) | (uint64_t)(uint32_t)(int32_t)hi;
        r->chart_cells[i][4] =
            ((uint64_t)(uint32_t)(int32_t)dt << 32U) | (uint64_t)(uint32_t)(int32_t)ug;
        r->chart_cells[i][5] =
            ((uint64_t)nlo << 24U) | ((uint64_t)nhi << 16U) | (uint64_t)rl->kan_lock;
        r->chart_cells[i][6] = UINT64_C(1) << 32U;
        r->chart_cells[i][7] = UINT64_C(0);
    }
}

/* --- Stage B: Symbiotic Multi-Chart Atlas Co-Evolution --- */

FLOW_INL void flow_synth__fit_segment_2jet(int64_t u_l, int64_t y_l, int64_t u_m, int64_t y_m,
                                           int64_t u_r, int64_t y_r, int64_t b2_bias,
                                           flow_synth_rule_t *out) {
    int64_t b1 = FLOW_Q16_ONE;
    int64_t b2 = 0;

    if (u_r != u_l && y_l != y_r) {
        flow_i128 du_rm = (flow_i128)(u_r - u_l);
        flow_i128 y_lin =
            (flow_i128)y_l + ((flow_i128)(y_r - y_l) * (flow_i128)(u_m - u_l)) / du_rm;
        flow_i128 d_lin = (flow_i128)y_m - y_lin;
        flow_i128 y_sq_l = ((flow_i128)y_l * (flow_i128)y_l) >> 16U;
        flow_i128 y_sq_m = ((flow_i128)y_m * (flow_i128)y_m) >> 16U;
        flow_i128 y_sq_r = ((flow_i128)y_r * (flow_i128)y_r) >> 16U;
        flow_i128 y_sq_interp = y_sq_l + ((y_sq_r - y_sq_l) * (flow_i128)(u_m - u_l)) / du_rm;
        flow_i128 denom = y_sq_interp - y_sq_m;
        if (denom != (flow_i128)0) {
            flow_i128 b2_raw = (flow_shl_i128(d_lin * (flow_i128)b1, 1U)) / denom;
            b2_raw = b2_raw + b2_bias;
            int64_t lo_b2 = -FLOW_MAX_Q16 / 4;
            int64_t hi_b2 = FLOW_MAX_Q16 / 4;
            if (b2_raw > hi_b2) {
                b2_raw = hi_b2;
            }
            if (b2_raw < lo_b2) {
                b2_raw = lo_b2;
            }
            /* Enforce monotonicity: b1 + b2 * y >= b1 / 8 on both endpoints */
            int64_t ymin = (y_l < y_r) ? y_l : y_r;
            int64_t ymax = (y_l > y_r) ? y_l : y_r;
            flow_i128 t_min = (flow_i128)b1 + ((b2_raw * (flow_i128)ymin) >> 16U);
            flow_i128 t_max = (flow_i128)b1 + ((b2_raw * (flow_i128)ymax) >> 16U);
            if (t_min >= (b1 / 8) && t_max >= (b1 / 8)) {
                b2 = (int64_t)b2_raw;
            }
        }
    }

    flow_i128 f_l =
        (flow_i128)b1 * (flow_i128)y_l + (((flow_i128)b2 * (flow_i128)y_l * (flow_i128)y_l) >> 17U);
    flow_i128 f_r =
        (flow_i128)b1 * (flow_i128)y_r + (((flow_i128)b2 * (flow_i128)y_r * (flow_i128)y_r) >> 17U);
    int64_t ug = INT64_C(0);
    if (u_r != u_l) {
        ug = flow_sym_div128_clamped(f_r - f_l, (flow_i128)(u_r - u_l), FLOW_MAX_Q16);
    }
    flow_i128 f_l_q16 = f_l >> 16U;
    flow_i128 u_term = ((flow_i128)ug * (flow_i128)u_l) >> 16U;
    flow_i128 b0_wide = -(f_l_q16 - u_term);
    if (b0_wide > FLOW_MAX_Q16) {
        b0_wide = FLOW_MAX_Q16;
    }
    if (b0_wide < -FLOW_MAX_Q16) {
        b0_wide = -FLOW_MAX_Q16;
    }

    out->b0 = (int64_t)b0_wide;
    out->b1 = b1;
    out->b2 = b2;
    out->u_gain = ug;
    out->dt_max = FLOW_MAX_Q16;
}

FLOW_INL uint64_t flow_synth__eval_approx_mae(const flow_synth_result_t *r, const int64_t *u_arr,
                                              const int64_t *y_arr, const uint16_t *m_arr,
                                              uint32_t n) {
    if (n == 0U) {
        return UINT64_C(0);
    }
    FLOW_ALIGN64 flow_cell arena[FLOW_SYNTH_MAX_CHARTS];
    for (uint32_t c = 0U; c < r->num_charts; ++c) {
        for (uint32_t w = 0U; w < 8U; ++w) {
            arena[c][w] = r->chart_cells[c][w];
        }
    }
    uint64_t cap_mask = (uint64_t)(r->num_charts - 1U);
    uint64_t sum_err = 0ULL;
    for (uint32_t i = 0U; i < n; ++i) {
        FLOW_ALIGN64 flow_cell st;
        for (uint32_t w = 0U; w < 8U; ++w) {
            st[w] = r->state_cell[w];
        }
        uint16_t mode = m_arr ? m_arr[i] : r->init_mode;
        for (uint32_t step = 0U; step < 8U; ++step) {
            flow_step_atlas(st, st, arena, cap_mask, u_arr[i], mode);
        }
        sum_err += flow_abs_i64((int64_t)st[3] - y_arr[i]);
    }
    return sum_err / (uint64_t)n;
}

FLOW_INL int64_t flow_synth__eval_policy_pnl(const flow_synth_result_t *r,
                                             const flow_synth_spec_t *sp, const int64_t *u_arr,
                                             const int64_t *y_arr, const uint16_t *m_arr,
                                             uint32_t n, uint64_t *out_shock_leaks) {
    FLOW_ALIGN64 flow_cell arena[FLOW_SYNTH_MAX_CHARTS];
    for (uint32_t c = 0U; c < r->num_charts; ++c) {
        for (uint32_t w = 0U; w < 8U; ++w) {
            arena[c][w] = r->chart_cells[c][w];
        }
    }
    FLOW_ALIGN64 flow_cell st;
    for (uint32_t w = 0U; w < 8U; ++w) {
        st[w] = r->state_cell[w];
    }
    uint64_t cap_mask = (uint64_t)(r->num_charts - 1U);
    int64_t pnl_q16 = INT64_C(0);
    int64_t prev_pos = INT64_C(0);
    uint64_t shock_leaks = 0ULL;
    int64_t half_fric = sp->friction_q16 >> 1U;

    for (uint32_t t = 0U; t < n; ++t) {
        uint16_t m_req = m_arr[t];
        bool mode_ok = ((m_req & sp->harbor_mask) == sp->harbor_mask);
        uint16_t cur_ch = (uint16_t)((st[5] >> 16U) & cap_mask);
        if (mode_ok && cur_ch == 3U) {
            st[5] = (st[5] & UINT64_C(0xFFFF)) | (uint64_t)sp->harbor_mask;
        }
        if (!mode_ok && r->rules[3].u_gain == 0) {
            st[5] = (UINT64_C(3) << 16U) | (uint64_t)(st[5] & UINT64_C(0xFFFF));
        }
        flow_step_atlas(st, st, arena, cap_mask, u_arr[t], m_req);

        uint16_t post_ch = (uint16_t)((st[5] >> 16U) & cap_mask);
        uint16_t post_mode = (uint16_t)(st[5] & UINT64_C(0xFFFF));
        bool healthy = ((post_mode & sp->harbor_mask) == sp->harbor_mask);
        int64_t pos = INT64_C(0);
        if (healthy && post_ch == 1U) {
            pos = INT64_C(1);
        } else if (healthy && post_ch == 2U) {
            pos = -INT64_C(1);
        }
        if (!mode_ok && pos != INT64_C(0)) {
            ++shock_leaks;
        }
        int64_t delta_pos = (pos >= prev_pos) ? (pos - prev_pos) : (prev_pos - pos);
        int64_t gross = pos * y_arr[t];
        pnl_q16 += gross - delta_pos * half_fric;
        prev_pos = pos;
    }
    if (out_shock_leaks != NULL) {
        *out_shock_leaks = shock_leaks;
    }
    return pnl_q16;
}

FLOW_INL void flow_synth__optimize_approx(flow_synth_ws *ws) {
    const flow_synth_spec_t *sp = &ws->spec;
    flow_synth_result_t *res = &ws->res;
    uint32_t n = sp->n_train;
    if (n < 4U) {
        return;
    }
    for (uint32_t i = 0U; i < n; ++i) {
        ws->sorted_u[i] = sp->train_u[i];
        ws->sorted_y[i] = sp->train_y[i];
        ws->sorted_mode[i] = sp->train_mode[i];
    }
    for (uint32_t i = 1U; i < n; ++i) {
        int64_t ku = ws->sorted_u[i];
        int64_t ky = ws->sorted_y[i];
        uint16_t km = ws->sorted_mode[i];
        uint32_t j = i;
        while (j > 0U && ws->sorted_y[j - 1U] > ky) {
            ws->sorted_u[j] = ws->sorted_u[j - 1U];
            ws->sorted_y[j] = ws->sorted_y[j - 1U];
            ws->sorted_mode[j] = ws->sorted_mode[j - 1U];
            --j;
        }
        ws->sorted_u[j] = ku;
        ws->sorted_y[j] = ky;
        ws->sorted_mode[j] = km;
    }

    uint32_t nc = res->num_charts;
    int64_t init_y_mid = (ws->sorted_y[0] + ws->sorted_y[n - 1U]) / INT64_C(2);
    res->init_x_q16 = init_y_mid;
    res->init_chart = (uint16_t)(nc / 2U);
    res->init_mode = sp->harbor_mask;

    /* Baseline 1-chart global secant */
    flow_synth_result_t base_r;
    memset(&base_r, 0, sizeof(base_r));
    base_r.k_exp = res->k_exp;
    base_r.num_charts = nc;
    base_r.init_x_q16 = init_y_mid;
    base_r.init_chart = 0U;
    base_r.init_mode = sp->harbor_mask;
    flow_synth_rule_t lin_rule;
    memset(&lin_rule, 0, sizeof(lin_rule));
    lin_rule.b1 = FLOW_Q16_ONE;
    lin_rule.b_exit = -FLOW_MAX_Q16;
    lin_rule.b_enter = FLOW_MAX_Q16;
    lin_rule.dt_max = FLOW_MAX_Q16;
    lin_rule.kan_lock = sp->active_mask;
    if (ws->sorted_u[n - 1U] != ws->sorted_u[0]) {
        flow_i128 dy = (flow_i128)(ws->sorted_y[n - 1U] - ws->sorted_y[0]);
        flow_i128 du = (flow_i128)(ws->sorted_u[n - 1U] - ws->sorted_u[0]);
        lin_rule.u_gain = flow_sym_div128_clamped(flow_shl_i128(dy, 16U), du, FLOW_MAX_Q16);
        flow_i128 b0w = -((flow_i128)ws->sorted_y[0] -
                          (((flow_i128)lin_rule.u_gain * (flow_i128)ws->sorted_u[0]) >> 16U));
        lin_rule.b0 = flow_clamp_sym((int64_t)b0w, FLOW_MAX_Q16);
    }
    for (uint32_t c = 0U; c < nc; ++c) {
        base_r.rules[c] = lin_rule;
        base_r.rules[c].next_lo = (uint8_t)c;
        base_r.rules[c].next_hi = (uint8_t)c;
    }
    flow_synth__mint_cells(&base_r);
    res->base_train_score =
        flow_synth__eval_approx_mae(&base_r, sp->train_u, sp->train_y, sp->train_mode, sp->n_train);
    res->base_val_score =
        flow_synth__eval_approx_mae(&base_r, sp->val_u, sp->val_y, sp->val_mode, sp->n_val);

    /* Symbiotic SEARU discrete split + 2-Jet conic curvature search */
    uint32_t num_splits = (nc > 1U) ? (nc - 1U) : 0U;
    uint32_t split_bits = num_splits * 4U;
    uint32_t nbits = split_bits + nc * 4U;
    if (nbits == 0U) {
        nbits = 4U;
    }
    uint64_t init_can[4] = {UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0)};
    uint64_t best_mae = ~UINT64_C(0);
    flow_synth_result_t best_r = base_r;
    searu_result sr;

    SEARU_SOLVE_INLINE(
        &sr, nbits, init_can, NULL, UINT64_C(0x4A661EC52E1), 256U, cand, frust, energy, {
            uint64_t bits = cand[0];
            uint32_t idx[FLOW_SYNTH_MAX_CHARTS + 1U];
            idx[0] = 0U;
            idx[nc] = n - 1U;
            bool ordered = true;
            for (uint32_t s = 0U; s < num_splits; ++s) {
                uint32_t base_s = ((s + 1U) * (n - 1U)) / nc;
                int32_t off = (int32_t)((bits >> (4U * s)) & UINT64_C(15)) - 8;
                int32_t pos = (int32_t)base_s + off;
                if (pos <= (int32_t)idx[s] + 1) {
                    pos = (int32_t)idx[s] + 2;
                }
                if (pos >= (int32_t)(n - 1U - (num_splits - 1U - s) * 2U)) {
                    ordered = false;
                }
                idx[s + 1U] = (uint32_t)pos;
            }
            if (!ordered) {
                energy = UINT64_C(1) << 50U;
                frust[0] = (UINT64_C(1) << split_bits) - UINT64_C(1);
            } else {
                flow_synth_result_t cand_r;
                memset(&cand_r, 0, sizeof(cand_r));
                cand_r.k_exp = res->k_exp;
                cand_r.num_charts = nc;
                cand_r.init_x_q16 = init_y_mid;
                cand_r.init_chart = (uint16_t)(nc / 2U);
                cand_r.init_mode = sp->harbor_mask;
                for (uint32_t c = 0U; c < nc; ++c) {
                    uint32_t i0 = idx[c];
                    uint32_t i1 = idx[c + 1U];
                    uint32_t im = (i0 + i1) >> 1U;
                    uint32_t b2code = (uint32_t)((bits >> (split_bits + 4U * c)) & UINT64_C(15));
                    int64_t b2_bias = ((int64_t)b2code - INT64_C(8)) * INT64_C(1024);
                    cand_r.rules[c].next_lo = (uint8_t)((c > 0U) ? (c - 1U) : 0U);
                    cand_r.rules[c].next_hi = (uint8_t)((c + 1U < nc) ? (c + 1U) : c);
                    cand_r.rules[c].kan_lock = sp->active_mask;
                    cand_r.rules[c].b_exit = (c == 0U) ? -FLOW_MAX_Q16 : ws->sorted_y[i0];
                    cand_r.rules[c].b_enter = (c + 1U == nc) ? FLOW_MAX_Q16 : ws->sorted_y[i1];
                    flow_synth__fit_segment_2jet(
                        ws->sorted_u[i0], ws->sorted_y[i0], ws->sorted_u[im], ws->sorted_y[im],
                        ws->sorted_u[i1], ws->sorted_y[i1], b2_bias, &cand_r.rules[c]);
                }
                flow_synth__mint_cells(&cand_r);
                uint64_t mae = flow_synth__eval_approx_mae(&cand_r, sp->train_u, sp->train_y,
                                                           sp->train_mode, sp->n_train);
                if (mae < best_mae) {
                    best_mae = mae;
                    best_r = cand_r;
                }
                energy = mae;
            }
        });

    uint64_t btr = res->base_train_score;
    uint64_t bvl = res->base_val_score;
    res->init_x_q16 = best_r.init_x_q16;
    res->init_chart = best_r.init_chart;
    res->init_mode = best_r.init_mode;
    memcpy(res->rules, best_r.rules, sizeof(res->rules));
    memcpy(res->state_cell, best_r.state_cell, sizeof(res->state_cell));
    memcpy(res->chart_cells, best_r.chart_cells, sizeof(res->chart_cells));
    res->base_train_score = btr;
    res->base_val_score = bvl;
    res->synth_train_score =
        flow_synth__eval_approx_mae(res, sp->train_u, sp->train_y, sp->train_mode, sp->n_train);
    res->synth_val_score =
        flow_synth__eval_approx_mae(res, sp->val_u, sp->val_y, sp->val_mode, sp->n_val);
    res->generalized = (res->synth_train_score < res->base_train_score) &&
                       (res->synth_val_score < res->base_val_score);
}

FLOW_INL void flow_synth__build_policy_rules(flow_synth_result_t *r, const flow_synth_spec_t *sp,
                                             int64_t b_enter, int64_t b_exit, int64_t b_halt,
                                             int64_t b2_damp, int64_t dt_max) {
    r->k_exp = 2U;
    r->num_charts = 4U;
    r->init_x_q16 = INT64_C(0);
    r->init_chart = 0U;
    r->init_mode = sp->harbor_mask;
    if (b_exit < 1) {
        b_exit = 1;
    }
    if (b_enter <= b_exit) {
        b_enter = b_exit + FLOW_Q16_ONE;
    }
    if (b_halt <= b_enter) {
        b_halt = b_enter + 4 * FLOW_Q16_ONE;
    }

    /* Chart 0: flat deadband [-b_enter, +b_enter] -> (short=2, long=1) */
    r->rules[0].b0 = 0;
    r->rules[0].b1 = FLOW_Q16_ONE;
    r->rules[0].b2 = 0;
    r->rules[0].b_exit = -b_enter;
    r->rules[0].b_enter = b_enter;
    r->rules[0].dt_max = dt_max;
    r->rules[0].u_gain = FLOW_Q16_ONE;
    r->rules[0].next_lo = 2U;
    r->rules[0].next_hi = 1U;
    r->rules[0].kan_lock = sp->active_mask;

    /* Chart 1: long hold [+b_exit, +b_halt] -> (flat=0, harbor=3) */
    r->rules[1].b0 = 0;
    r->rules[1].b1 = FLOW_Q16_ONE;
    r->rules[1].b2 = b2_damp;
    r->rules[1].b_exit = b_exit;
    r->rules[1].b_enter = b_halt;
    r->rules[1].dt_max = dt_max;
    r->rules[1].u_gain = FLOW_Q16_ONE;
    r->rules[1].next_lo = 0U;
    r->rules[1].next_hi = 3U;
    r->rules[1].kan_lock = sp->active_mask;

    /* Chart 2: short hold [-b_halt, -b_exit] -> (harbor=3, flat=0) */
    r->rules[2].b0 = 0;
    r->rules[2].b1 = FLOW_Q16_ONE;
    r->rules[2].b2 = b2_damp;
    r->rules[2].b_exit = -b_halt;
    r->rules[2].b_enter = -b_exit;
    r->rules[2].dt_max = dt_max;
    r->rules[2].u_gain = FLOW_Q16_ONE;
    r->rules[2].next_lo = 3U;
    r->rules[2].next_hi = 0U;
    r->rules[2].kan_lock = sp->active_mask;

    /* Chart 3: harbor circuit-breaker (u_gain = 0 isolates exogenous shocks) */
    r->rules[3].b0 = 0;
    r->rules[3].b1 = FLOW_Q16_ONE;
    r->rules[3].b2 = 0;
    r->rules[3].b_exit = -b_halt;
    r->rules[3].b_enter = b_halt;
    r->rules[3].dt_max = dt_max;
    r->rules[3].u_gain = 0;
    r->rules[3].next_lo = 3U;
    r->rules[3].next_hi = 3U;
    r->rules[3].kan_lock = sp->harbor_mask;

    flow_synth__mint_cells(r);
}

FLOW_INL void flow_synth__optimize_policy(flow_synth_ws *ws) {
    const flow_synth_spec_t *sp = &ws->spec;
    flow_synth_result_t *res = &ws->res;

    /* Chattering micro-threshold baseline */
    flow_synth_result_t base_r;
    memset(&base_r, 0, sizeof(base_r));
    flow_synth__build_policy_rules(&base_r, sp, INT64_C(4096), INT64_C(2048), 500 * FLOW_Q16_ONE, 0,
                                   FLOW_MAX_Q16);
    uint64_t base_leaks = 0ULL;
    res->base_train_pnl = flow_synth__eval_policy_pnl(&base_r, sp, sp->train_u, sp->train_y,
                                                      sp->train_mode, sp->n_train, &base_leaks);
    res->base_val_pnl = flow_synth__eval_policy_pnl(&base_r, sp, sp->val_u, sp->val_y, sp->val_mode,
                                                    sp->n_val, NULL);

    /* Outer SEARU search over hysteresis deadband, damping & halt shells */
    uint64_t init_can[4] = {UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0)};
    int64_t best_b_enter = 2 * FLOW_Q16_ONE;
    int64_t best_b_exit = FLOW_Q16_ONE >> 1U;
    int64_t best_b_halt = 128 * FLOW_Q16_ONE;
    int64_t best_b2 = 0;
    int64_t best_dt = FLOW_MAX_Q16;
    int64_t best_pnl = res->base_train_pnl;
    bool have_best = false;
    searu_result sr;

    SEARU_SOLVE_INLINE(
        &sr, 16U, init_can, NULL, UINT64_C(0x5EED9011CE), 512U, cand, frust, energy, {
            uint64_t bits = cand[0];
            uint32_t ent_idx = (uint32_t)(bits & UINT64_C(63));
            uint32_t ex_idx = (uint32_t)((bits >> 6U) & UINT64_C(15));
            uint32_t hlt_idx = (uint32_t)((bits >> 10U) & UINT64_C(7));
            uint32_t b2_idx = (uint32_t)((bits >> 13U) & UINT64_C(7));

            int64_t b_enter = (int64_t)(ent_idx + 1U) * (FLOW_Q16_ONE >> 2U);
            int64_t b_exit = ((b_enter * (int64_t)(ex_idx + 1U)) >> 4U);
            if (b_exit >= b_enter) {
                b_exit = b_enter - 1;
            }
            if (b_exit < 1) {
                b_exit = 1;
            }
            int64_t b_halt = b_enter + ((int64_t)(hlt_idx + 4U) * (8 * FLOW_Q16_ONE));
            int64_t b2_damp = (b2_idx == 0U) ? 0 : ((int64_t)b2_idx * 4096);

            flow_synth_result_t cand_r;
            memset(&cand_r, 0, sizeof(cand_r));
            flow_synth__build_policy_rules(&cand_r, sp, b_enter, b_exit, b_halt, b2_damp,
                                           FLOW_MAX_Q16);
            uint64_t leaks = 0ULL;
            int64_t pnl = flow_synth__eval_policy_pnl(&cand_r, sp, sp->train_u, sp->train_y,
                                                      sp->train_mode, sp->n_train, &leaks);
            int64_t adj_pnl = pnl - (int64_t)leaks * (1000 * FLOW_Q16_ONE);
            if (!have_best || adj_pnl > best_pnl) {
                have_best = true;
                best_pnl = adj_pnl;
                best_b_enter = b_enter;
                best_b_exit = b_exit;
                best_b_halt = b_halt;
                best_b2 = b2_damp;
            }
            int64_t gap = (INT64_C(2000) * FLOW_Q16_ONE) - adj_pnl;
            energy = (gap > INT64_C(0)) ? ((uint64_t)gap >> 13U) + UINT64_C(1) : UINT64_C(1);
            (void)frust;
        });

    /* Fine continuous Q16.16 coordinate polish (Modulus) */
    for (int32_t sh = 14; sh >= 10; sh -= 2) {
        int64_t step = INT64_C(1) << (uint32_t)sh;
        const int64_t dirs[2] = {-step, step};
        for (uint32_t d = 0U; d < 2U; ++d) {
            int64_t try_ent = best_b_enter + dirs[d];
            if (try_ent > best_b_exit && try_ent < best_b_halt) {
                flow_synth_result_t cand_r;
                memset(&cand_r, 0, sizeof(cand_r));
                flow_synth__build_policy_rules(&cand_r, sp, try_ent, best_b_exit, best_b_halt,
                                               best_b2, best_dt);
                uint64_t leaks = 0ULL;
                int64_t pnl = flow_synth__eval_policy_pnl(&cand_r, sp, sp->train_u, sp->train_y,
                                                          sp->train_mode, sp->n_train, &leaks);
                int64_t adj = pnl - (int64_t)leaks * (1000 * FLOW_Q16_ONE);
                if (adj > best_pnl) {
                    best_pnl = adj;
                    best_b_enter = try_ent;
                }
            }
            int64_t try_ex = best_b_exit + dirs[d];
            if (try_ex >= 1 && try_ex < best_b_enter) {
                flow_synth_result_t cand_r;
                memset(&cand_r, 0, sizeof(cand_r));
                flow_synth__build_policy_rules(&cand_r, sp, best_b_enter, try_ex, best_b_halt,
                                               best_b2, best_dt);
                uint64_t leaks = 0ULL;
                int64_t pnl = flow_synth__eval_policy_pnl(&cand_r, sp, sp->train_u, sp->train_y,
                                                          sp->train_mode, sp->n_train, &leaks);
                int64_t adj = pnl - (int64_t)leaks * (1000 * FLOW_Q16_ONE);
                if (adj > best_pnl) {
                    best_pnl = adj;
                    best_b_exit = try_ex;
                }
            }
        }
    }

    flow_synth__build_policy_rules(res, sp, best_b_enter, best_b_exit, best_b_halt, best_b2,
                                   best_dt);
    res->synth_train_pnl = flow_synth__eval_policy_pnl(res, sp, sp->train_u, sp->train_y,
                                                       sp->train_mode, sp->n_train, NULL);
    res->synth_val_pnl =
        flow_synth__eval_policy_pnl(res, sp, sp->val_u, sp->val_y, sp->val_mode, sp->n_val, NULL);
    res->generalized = (res->synth_train_pnl > res->base_train_pnl) &&
                       (res->synth_val_pnl > res->base_val_pnl) && (res->synth_val_pnl > 0);
}

FLOW_INL void flow_synth_optimize_atlas(flow_synth_ws *ws) {
    ws->res.k_exp = ws->spec.k_exp;
    ws->res.num_charts = ws->spec.num_charts;
    if (ws->spec.is_policy) {
        flow_synth__optimize_policy(ws);
    } else {
        flow_synth__optimize_approx(ws);
    }
}

/* --- Emitters (.flow Binary + 0-sorryAx .lean Verification Module) --- */

FLOW_INL bool flow_synth_emit_bundle(const flow_synth_result_t *res, const char *path) {
    if (res == NULL || path == NULL || res->num_charts == 0U) {
        return false;
    }
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        return false;
    }
    uint8_t buf[64];
    flow_cell_to_bytes(res->state_cell, buf);
    bool ok = (fwrite(buf, 1U, 64U, fp) == 64U);
    for (uint32_t c = 0U; ok && c < res->num_charts; ++c) {
        flow_cell_to_bytes(res->chart_cells[c], buf);
        ok = (fwrite(buf, 1U, 64U, fp) == 64U);
    }
    fclose(fp);
    return ok;
}

FLOW_INL void flow_synth__fprint_expr(FILE *fp, const flow_synth_spec_t *sp, uint32_t vid,
                                      int32_t disturb_var, int8_t disturb_sign) {
    const flow_synth_var_t *v = &sp->vars[vid];
    if (v->op == FLOW_SOP_FREE) {
        if ((int32_t)vid == disturb_var) {
            fprintf(fp, "(%s %s _delta)", v->name, (disturb_sign >= 0) ? "+" : "-");
        } else {
            fprintf(fp, "%s", v->name);
        }
        return;
    }
    switch (v->op) {
    case FLOW_SOP_ADD:
        fprintf(fp, "(");
        flow_synth__fprint_expr(fp, sp, v->a, disturb_var, disturb_sign);
        fprintf(fp, " + ");
        flow_synth__fprint_expr(fp, sp, v->b, disturb_var, disturb_sign);
        fprintf(fp, ")");
        break;
    case FLOW_SOP_SUB:
        fprintf(fp, "(");
        flow_synth__fprint_expr(fp, sp, v->a, disturb_var, disturb_sign);
        fprintf(fp, " - ");
        flow_synth__fprint_expr(fp, sp, v->b, disturb_var, disturb_sign);
        fprintf(fp, ")");
        break;
    case FLOW_SOP_ADD_SUB:
        fprintf(fp, "(");
        flow_synth__fprint_expr(fp, sp, v->a, disturb_var, disturb_sign);
        fprintf(fp, " + ");
        flow_synth__fprint_expr(fp, sp, v->b, disturb_var, disturb_sign);
        fprintf(fp, " - ");
        flow_synth__fprint_expr(fp, sp, v->c, disturb_var, disturb_sign);
        fprintf(fp, ")");
        break;
    case FLOW_SOP_RELU_SUB:
        fprintf(fp, "(Flow.relu (");
        flow_synth__fprint_expr(fp, sp, v->a, disturb_var, disturb_sign);
        fprintf(fp, " - ");
        flow_synth__fprint_expr(fp, sp, v->b, disturb_var, disturb_sign);
        fprintf(fp, "))");
        break;
    case FLOW_SOP_IMIN:
        fprintf(fp, "(Flow.imin ");
        flow_synth__fprint_expr(fp, sp, v->a, disturb_var, disturb_sign);
        fprintf(fp, " ");
        flow_synth__fprint_expr(fp, sp, v->b, disturb_var, disturb_sign);
        fprintf(fp, ")");
        break;
    case FLOW_SOP_IMAX:
        fprintf(fp, "(Flow.imax ");
        flow_synth__fprint_expr(fp, sp, v->a, disturb_var, disturb_sign);
        fprintf(fp, " ");
        flow_synth__fprint_expr(fp, sp, v->b, disturb_var, disturb_sign);
        fprintf(fp, ")");
        break;
    case FLOW_SOP_IABS_SUB:
        fprintf(fp, "(Flow.iabs (");
        flow_synth__fprint_expr(fp, sp, v->a, disturb_var, disturb_sign);
        fprintf(fp, " - ");
        flow_synth__fprint_expr(fp, sp, v->b, disturb_var, disturb_sign);
        fprintf(fp, "))");
        break;
    case FLOW_SOP_QMID:
        fprintf(fp, "(Flow.qmid ");
        flow_synth__fprint_expr(fp, sp, v->a, disturb_var, disturb_sign);
        fprintf(fp, " ");
        flow_synth__fprint_expr(fp, sp, v->b, disturb_var, disturb_sign);
        fprintf(fp, ")");
        break;
    default:
        break;
    }
}

FLOW_INL void flow_synth__fprint_lincomb(FILE *fp, const flow_synth_spec_t *sp, uint32_t m,
                                         const uint8_t *cands, const int8_t *coeffs,
                                         int32_t disturb_var, int8_t disturb_sign) {
    bool first = true;
    for (uint32_t j = 0U; j < m; ++j) {
        int8_t c = coeffs[j];
        if (c == 0) {
            continue;
        }
        int8_t ac = (c > 0) ? c : (int8_t)(-c);
        if (!first) {
            fprintf(fp, (c > 0) ? " + " : " - ");
        } else if (c < 0) {
            fprintf(fp, "-");
        }
        if (ac != 1) {
            fprintf(fp, "%d * ", (int)ac);
        }
        flow_synth__fprint_expr(fp, sp, cands[j], disturb_var, disturb_sign);
        first = false;
    }
    if (first) {
        fprintf(fp, "0");
    }
}

FLOW_INL uint32_t flow_synth__assume_deps(const flow_synth_spec_t *sp,
                                          const flow_synth_assume_t *as) {
    uint32_t d = sp->vars[as->a].free_deps;
    if (as->kind != FLOW_SASSUME_POS) {
        d |= sp->vars[as->b].free_deps;
    }
    if (as->kind == FLOW_SASSUME_BAL) {
        d |= sp->vars[as->c].free_deps | sp->vars[as->d].free_deps;
    }
    return d;
}

FLOW_INL void flow_synth__emit_lean_binders(FILE *fp, const flow_synth_spec_t *sp,
                                            uint32_t used_mask, bool need_delta) {
    fprintf(fp, "   ");
    for (uint32_t i = 0U; i < sp->num_vars; ++i) {
        if (sp->vars[i].op == FLOW_SOP_FREE && ((used_mask >> i) & UINT32_C(1)) != 0U) {
            fprintf(fp, " (%s : Int)", sp->vars[i].name);
        }
    }
    if (need_delta) {
        fprintf(fp, " (_delta : Int)");
    }
    fprintf(fp, "\n");
    uint32_t hi = 0U;
    for (uint32_t ai = 0U; ai < sp->num_assumes; ++ai) {
        const flow_synth_assume_t *as = &sp->assumes[ai];
        uint32_t ad = flow_synth__assume_deps(sp, as);
        if ((ad & used_mask) != ad) {
            continue;
        }
        fprintf(fp, "    (_h_%u : ", (unsigned)hi++);
        if (as->kind == FLOW_SASSUME_LE) {
            flow_synth__fprint_expr(fp, sp, as->a, -1, 0);
            fprintf(fp, " ≤ ");
            flow_synth__fprint_expr(fp, sp, as->b, -1, 0);
        } else if (as->kind == FLOW_SASSUME_POS) {
            flow_synth__fprint_expr(fp, sp, as->a, -1, 0);
            fprintf(fp, " > 0");
        } else if (as->kind == FLOW_SASSUME_BAL) {
            flow_synth__fprint_expr(fp, sp, as->a, -1, 0);
            fprintf(fp, " - ");
            flow_synth__fprint_expr(fp, sp, as->b, -1, 0);
            fprintf(fp, " = ");
            flow_synth__fprint_expr(fp, sp, as->c, -1, 0);
            fprintf(fp, " - ");
            flow_synth__fprint_expr(fp, sp, as->d, -1, 0);
        } else {
            flow_synth__fprint_expr(fp, sp, as->a, -1, 0);
            fprintf(fp, " = ");
            flow_synth__fprint_expr(fp, sp, as->b, -1, 0);
        }
        fprintf(fp, ")\n");
    }
    if (need_delta) {
        fprintf(fp, "    (_h_delta : _delta > 0)\n");
    }
}

FLOW_INL bool flow_synth_emit_lean(const flow_synth_ws *ws, const char *path) {
    if (ws == NULL || path == NULL) {
        return false;
    }
    const flow_synth_spec_t *sp = &ws->spec;
    const flow_synth_result_t *res = &ws->res;

    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        return false;
    }

    fprintf(fp, "import Flow\n\n");
    fprintf(fp, "/-\n");
    fprintf(fp, "  AUTO-SYNTHESIZED BY `flow synth` (FLOW v%s Symbiotic Synthesizer)\n",
            FLOW_VERSION_STRING);
    fprintf(fp, "  Domain: %s | Mode: %s | Charts: %u | Generalized: %s\n", sp->domain,
            sp->is_policy ? "policy" : "approx", (unsigned)res->num_charts,
            res->generalized ? "PASS" : "FAIL");
    fprintf(fp, "-/\n\n");
    fprintf(fp, "namespace %s\n\n", sp->domain);

    /* Stage A theorems */
    for (uint32_t li = 0U; li < res->num_laws; ++li) {
        const flow_synth_law_t *lw = &res->laws[li];
        if (!lw->valid) {
            continue;
        }
        uint32_t used_mask = 0U;
        if (lw->kind == FLOW_SGOAL_IDENTITY) {
            used_mask = sp->vars[lw->primary].free_deps;
            for (uint32_t j = 0U; j < lw->num_cands; ++j) {
                if (lw->coeffs[j] != 0) {
                    used_mask |= sp->vars[lw->cands[j]].free_deps;
                }
            }
            fprintf(fp, "theorem discovered_identity_%u\n", (unsigned)li);
            flow_synth__emit_lean_binders(fp, sp, used_mask, false);
            fprintf(fp, "    : ");
            flow_synth__fprint_lincomb(fp, sp, lw->num_cands, lw->cands, lw->coeffs, -1, 0);
            fprintf(fp, " = ");
            flow_synth__fprint_expr(fp, sp, lw->primary, -1, 0);
            fprintf(fp, " := by\n"
                        "  try dsimp only [Flow.relu, Flow.imin, Flow.imax, Flow.iabs, Flow.qmid] "
                        "at *\n"
                        "  omega\n\n");
        } else if (lw->kind == FLOW_SGOAL_NONNEG) {
            for (uint32_t j = 0U; j < lw->num_cands; ++j) {
                if (lw->coeffs[j] != 0) {
                    used_mask |= sp->vars[lw->cands[j]].free_deps;
                }
            }
            fprintf(fp, "theorem discovered_nonneg_%u\n", (unsigned)li);
            flow_synth__emit_lean_binders(fp, sp, used_mask, false);
            fprintf(fp, "    : ");
            flow_synth__fprint_lincomb(fp, sp, lw->num_cands, lw->cands, lw->coeffs, -1, 0);
            fprintf(fp, " ≥ 0 := by\n"
                        "  try dsimp only [Flow.relu, Flow.imin, Flow.imax, Flow.iabs, Flow.qmid] "
                        "at *\n"
                        "  omega\n\n");
        } else {
            used_mask = sp->vars[lw->primary].free_deps | sp->vars[lw->guard_a_hi].free_deps |
                        sp->vars[lw->guard_a_lo].free_deps | sp->vars[lw->guard_b_hi].free_deps |
                        sp->vars[lw->guard_b_lo].free_deps;
            for (uint32_t j = 0U; j < lw->num_cands; ++j) {
                if (lw->coeffs[j] != 0) {
                    used_mask |= sp->vars[lw->cands[j]].free_deps;
                }
            }
            /* Include free vars from equality assumptions anchoring candidates */
            for (uint32_t ai = 0U; ai < sp->num_assumes; ++ai) {
                if (sp->assumes[ai].kind == FLOW_SASSUME_EQ) {
                    used_mask |= flow_synth__assume_deps(sp, &sp->assumes[ai]);
                }
            }
            fprintf(fp, "theorem discovered_invariant_%u\n", (unsigned)li);
            flow_synth__emit_lean_binders(fp, sp, used_mask, sp->num_disturbs > 0U);
            fprintf(fp, "    : ");
            if (sp->num_disturbs == 0U) {
                fprintf(fp, "True");
            } else {
                for (uint32_t di = 0U; di < sp->num_disturbs; ++di) {
                    int32_t dv = (int32_t)sp->disturbs[di].var_idx;
                    int8_t ds = sp->disturbs[di].sign;
                    if (di > 0U) {
                        fprintf(fp, " ∧\n      ");
                    }
                    fprintf(fp, "(if (");
                    flow_synth__fprint_expr(fp, sp, lw->guard_a_hi, dv, ds);
                    fprintf(fp, " - ");
                    flow_synth__fprint_expr(fp, sp, lw->guard_a_lo, dv, ds);
                    fprintf(fp, ") ≤ (");
                    flow_synth__fprint_expr(fp, sp, lw->guard_b_hi, dv, ds);
                    fprintf(fp, " - ");
                    flow_synth__fprint_expr(fp, sp, lw->guard_b_lo, dv, ds);
                    fprintf(fp, ") then ");
                    flow_synth__fprint_expr(fp, sp, lw->primary, dv, ds);
                    fprintf(fp, " else ");
                    flow_synth__fprint_lincomb(fp, sp, lw->num_cands, lw->cands, lw->coeffs, dv,
                                               ds);
                    fprintf(fp, ") = ");
                    flow_synth__fprint_expr(fp, sp, lw->primary, -1, 0);
                }
            }
            fprintf(fp, " := by\n"
                        "  try dsimp only [Flow.relu, Flow.imin, Flow.imax, Flow.iabs, Flow.qmid] "
                        "at *\n"
                        "  omega\n\n");
        }
    }

    /* Stage B Flow.Rule chart definitions */
    for (uint32_t c = 0U; c < res->num_charts; ++c) {
        const flow_synth_rule_t *rl = &res->rules[c];
        fprintf(fp, "def synthChart%u : Flow.Rule where\n", (unsigned)c);
        fprintf(fp, "  b0      := (%lld)\n", (long long)rl->b0);
        fprintf(fp, "  b1      := (%lld)\n", (long long)rl->b1);
        fprintf(fp, "  b2      := (%lld)\n", (long long)rl->b2);
        fprintf(fp, "  bExit   := (%lld)\n", (long long)rl->b_exit);
        fprintf(fp, "  bEnter  := (%lld)\n", (long long)rl->b_enter);
        fprintf(fp, "  dtMax   := (%lld)\n", (long long)rl->dt_max);
        fprintf(fp, "  uGain   := (%lld)\n", (long long)rl->u_gain);
        fprintf(fp, "  nextLo  := %u\n", (unsigned)rl->next_lo);
        fprintf(fp, "  nextHi  := %u\n", (unsigned)rl->next_hi);
        fprintf(fp, "  kanLock := 0x%04x\n\n", (unsigned)rl->kan_lock);
    }

    fprintf(fp, "def synthCharts : List Flow.Rule := [");
    for (uint32_t c = 0U; c < res->num_charts; ++c) {
        fprintf(fp, "%ssynthChart%u", (c > 0U) ? ", " : "", (unsigned)c);
    }
    fprintf(fp, "]\n\n");

    fprintf(fp, "def synthInitState : Flow.Cell64 :=\n");
    fprintf(fp, "  Flow.mintState %u (%lld) (initChart := %u) (initMode := 0x%04x)\n\n",
            (unsigned)res->k_exp, (long long)res->init_x_q16, (unsigned)res->init_chart,
            (unsigned)res->init_mode);

    fprintf(fp, "theorem synth_atlas_wf :\n    (");
    for (uint32_t c = 0U; c < res->num_charts; ++c) {
        fprintf(fp, "%ssynthChart%u.wf %u", (c > 0U) ? " && " : "", (unsigned)c,
                (unsigned)res->k_exp);
    }
    fprintf(fp, ") = true := by\n  decide\n\n");

    fprintf(fp,
            "theorem synth_chart0_deadband_holds (xNext : Int)\n"
            "    (hBand : synthChart0.bExit ≤ xNext ∧ xNext ≤ synthChart0.bEnter) :\n"
            "    Flow.schmittNext %u 0 synthChart0 xNext = 0 :=\n"
            "  Flow.schmitt_deadband_holds %u 0 synthChart0 xNext (by decide) hBand\n\n",
            (unsigned)res->k_exp, (unsigned)res->k_exp);

    for (uint32_t c = 0U; c < res->num_charts; ++c) {
        if (res->rules[c].u_gain == 0) {
            fprintf(fp,
                    "theorem synth_chart%u_isolates_shocks (x u1 u2 : Int) :\n"
                    "    synthChart%u.evalLinearResidual x u1 = "
                    "synthChart%u.evalLinearResidual x u2 :=\n"
                    "  Flow.zero_ugain_isolates_shocks synthChart%u rfl x u1 u2\n\n",
                    (unsigned)c, (unsigned)c, (unsigned)c, (unsigned)c);
        }
    }

    fprintf(fp, "end %s\n\n", sp->domain);
    fprintf(fp,
            "def main (args : List String) : IO UInt32 := do\n"
            "  match args with\n"
            "  | [outPath] =>\n"
            "      Flow.emitBundle outPath %u %s.synthInitState %s.synthCharts %s.synthChart0 1\n"
            "      return 0\n"
            "  | _ =>\n"
            "      IO.eprintln \"Usage: <spec.lean> <out.flow>\"\n"
            "      return 2\n",
            (unsigned)res->k_exp, sp->domain, sp->domain, sp->domain);

    fclose(fp);
    return true;
}

#ifdef __cplusplus
}
#endif

#endif /* FLOW_SYNTH_H */
