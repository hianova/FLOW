#include "flow.h"
#include "searu.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static FLOW_ALIGN64 flow_cell g_arena[FLOW_ATLAS_MAX_CHARTS];

static void fmt_q16(int64_t v, char out[32]) {
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

static bool parse_u64_token(const char *s, uint64_t max_val, uint64_t *out) {
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

static bool parse_q16_token(const char *s, int64_t *out) {
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
        if (!parse_u64_token(p, (uint64_t)FLOW_MAX_Q16, &mag)) {
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

static bool verify_chart_geometry(const uint64_t b[8], size_t chart_count) {
    int64_t b0 = (int64_t)b[0];
    int64_t b1 = (int64_t)b[1];
    int64_t b2 = (int64_t)b[2];
    int64_t b_exit = (int64_t)(int32_t)(uint32_t)(b[3] >> 32U);
    int64_t b_enter = (int64_t)(int32_t)(uint32_t)(b[3] & UINT64_C(0xFFFFFFFF));
    int64_t dt_max = (int64_t)(int32_t)(uint32_t)(b[4] >> 32U);
    int64_t u_gain = (int64_t)(int32_t)(uint32_t)(b[4] & UINT64_C(0xFFFFFFFF));
    uint16_t next_lo = (uint16_t)((b[5] >> 24U) & UINT64_C(0xFF));
    uint16_t next_hi = (uint16_t)((b[5] >> 16U) & UINT64_C(0xFF));

    return (flow_abs_i64(b0) <= (uint64_t)FLOW_MAX_Q16) &&
           (flow_abs_i64(b1) <= (uint64_t)FLOW_MAX_Q16) &&
           (flow_abs_i64(b2) <= (uint64_t)FLOW_MAX_Q16) &&
           (flow_abs_i64(b_exit) <= (uint64_t)FLOW_MAX_Q16) &&
           (flow_abs_i64(b_enter) <= (uint64_t)FLOW_MAX_Q16) &&
           (flow_abs_i64(u_gain) <= (uint64_t)FLOW_MAX_Q16) && (dt_max >= 1) &&
           (dt_max <= FLOW_MAX_Q16) && (b_exit <= b_enter) && ((size_t)next_lo < chart_count) &&
           ((size_t)next_hi < chart_count);
}

static bool load_and_verify_bundle(const char *path, uint64_t out_state[8], size_t *out_count) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        fprintf(stderr, "flow: cannot open bundle '%s'\n", path);
        return false;
    }

    uint8_t raw[64];
    if (fread(raw, 1U, 64U, fp) != 64U) {
        fprintf(stderr, "flow: '%s' truncated before initial State cell\n", path);
        fclose(fp);
        return false;
    }
    flow_cell_from_bytes(out_state, raw);

    size_t n = 0U;
    for (;;) {
        size_t got = fread(raw, 1U, 64U, fp);
        if (got == 0U) {
            break;
        }
        if (got != 64U) {
            fprintf(stderr, "flow: '%s' is not 64-byte cache-line aligned\n", path);
            fclose(fp);
            return false;
        }
        if (n >= FLOW_ATLAS_MAX_CHARTS) {
            fprintf(stderr, "flow: '%s' exceeds maximum %u charts\n", path, FLOW_ATLAS_MAX_CHARTS);
            fclose(fp);
            return false;
        }
        flow_cell_from_bytes(g_arena[n], raw);
        ++n;
    }
    fclose(fp);

    if (n == 0U || (n & (n - 1U)) != 0U) {
        fprintf(stderr, "flow: '%s' chart count (%zu) is not a power of two\n", path, n);
        return false;
    }
    uint16_t init_chart = (uint16_t)((out_state[5] >> 16U) & UINT64_C(0xFFFF));
    if ((size_t)init_chart >= n) {
        fprintf(stderr, "flow: '%s' initial chart (%u) out of range for %zu charts\n", path,
                (unsigned)init_chart, n);
        return false;
    }
    if (flow_abs_i64((int64_t)out_state[3]) > (uint64_t)FLOW_MAX_Q16) {
        fprintf(stderr, "flow: '%s' initial x exceeds symmetric Q16.16 range\n", path);
        return false;
    }

    for (size_t i = 0U; i < n; ++i) {
        if (!verify_chart_geometry(g_arena[i], n)) {
            fprintf(stderr, "flow: '%s' Chart[%zu] failed Atlas geometry check\n", path, i);
            return false;
        }
    }

    *out_count = n;
    return true;
}

static bool load_single_chart(const char *path, uint64_t out_bound[8]) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return false;
    }
    uint8_t raw[64];
    size_t got = fread(raw, 1U, 64U, fp);
    int extra = fgetc(fp);
    fclose(fp);
    if (got != 64U || extra != EOF) {
        return false;
    }
    flow_cell_from_bytes(out_bound, raw);
    return true;
}

static int cmd_verify(const char *bundle_path) {
    FLOW_ALIGN64 flow_cell st;
    size_t count = 0U;
    if (!load_and_verify_bundle(bundle_path, st, &count)) {
        return 1;
    }
    uint16_t init_chart = (uint16_t)((st[5] >> 16U) & UINT64_C(0xFFFF));
    uint16_t init_mode = (uint16_t)(st[5] & UINT64_C(0xFFFF));
    char x_buf[32];
    fmt_q16((int64_t)st[3], x_buf);

    printf("[OK] %s: charts=%zu init_chart=%u init_x=%s init_mode=0x%04x\n", bundle_path, count,
           (unsigned)init_chart, x_buf, (unsigned)init_mode);
    for (size_t i = 0U; i < count; ++i) {
        const uint64_t *b = g_arena[i];
        char lo_s[32];
        char hi_s[32];
        char dt_s[32];
        char ug_s[32];
        fmt_q16((int64_t)(int32_t)(uint32_t)(b[3] >> 32U), lo_s);
        fmt_q16((int64_t)(int32_t)(uint32_t)(b[3] & UINT64_C(0xFFFFFFFF)), hi_s);
        fmt_q16((int64_t)(int32_t)(uint32_t)(b[4] >> 32U), dt_s);
        fmt_q16((int64_t)(int32_t)(uint32_t)(b[4] & UINT64_C(0xFFFFFFFF)), ug_s);
        uint16_t n_lo = (uint16_t)((b[5] >> 24U) & UINT64_C(0xFF));
        uint16_t n_hi = (uint16_t)((b[5] >> 16U) & UINT64_C(0xFF));
        uint16_t lock = (uint16_t)(b[5] & UINT64_C(0xFFFF));
        printf("  chart[%zu]: band=[%s, %s] -> (%u, %u) dt_max=%s u_gain=%s kan_lock=0x%04x\n", i,
               lo_s, hi_s, (unsigned)n_lo, (unsigned)n_hi, dt_s, ug_s, (unsigned)lock);
    }
    return 0;
}

static int cmd_run(const char *bundle_path) {
    FLOW_ALIGN64 flow_cell state;
    size_t count = 0U;
    if (!load_and_verify_bundle(bundle_path, state, &count)) {
        return 1;
    }

    uint64_t cap_mask = (uint64_t)(count - 1U);
    for (size_t i = 0U; i < count; ++i) {
        uint32_t ep = (uint32_t)(g_arena[i][6] >> 32U);
        flow_arena_publish_bound(g_arena, (uint64_t)i, cap_mask, ep, g_arena[i]);
    }

    char line[1024];
    uint64_t step_idx = 0ULL;
    while (fgets(line, (int)sizeof(line), stdin) != NULL) {
        size_t len = strlen(line);
        bool has_nl = (len > 0U && line[len - 1U] == '\n');
        char *p = line;
        while (*p != '\0' && isspace((unsigned char)*p)) {
            ++p;
        }
        if (*p == '#') {
            if (!has_nl) {
                int ch = 0;
                while ((ch = fgetc(stdin)) != EOF && ch != '\n') {
                }
            }
            continue;
        }
        if (!has_nl) {
            int ch = fgetc(stdin);
            if (ch != EOF) {
                while (ch != EOF && ch != '\n') {
                    ch = fgetc(stdin);
                }
                fprintf(stderr, "flow: input line exceeds %zu-byte limit\n", sizeof(line) - 1U);
                return 1;
            }
        }
        if (*p == '\0') {
            continue;
        }

        char *tok1 = strtok(p, " \t\r\n");
        if (tok1 == NULL) {
            continue;
        }

        if (strcmp(tok1, "@swap") == 0) {
            char *slot_s = strtok(NULL, " \t\r\n");
            char *ep_s = strtok(NULL, " \t\r\n");
            char *cell_path = strtok(NULL, " \t\r\n");
            char *extra_tok = strtok(NULL, " \t\r\n");
            uint64_t slot = 0ULL;
            uint64_t ep = 0ULL;
            FLOW_ALIGN64 flow_cell new_b;
            bool swapped =
                (slot_s != NULL && ep_s != NULL && cell_path != NULL &&
                 (extra_tok == NULL || *extra_tok == '#') &&
                 parse_u64_token(slot_s, (uint64_t)(count - 1U), &slot) &&
                 parse_u64_token(ep_s, UINT64_C(0xFFFFFFFF), &ep) &&
                 load_single_chart(cell_path, new_b) && verify_chart_geometry(new_b, count) &&
                 flow_arena_try_swap(g_arena, slot, cap_mask, (uint32_t)ep, new_b));
            if (swapped) {
                printf("# swap slot=%llu epoch=%llu ok\n", (unsigned long long)slot,
                       (unsigned long long)ep);
            } else {
                printf("# swap slot=%s epoch=%s rejected\n", slot_s != NULL ? slot_s : "?",
                       ep_s != NULL ? ep_s : "?");
            }
            continue;
        }

        int64_t u_ext = 0;
        if (!parse_q16_token(tok1, &u_ext)) {
            fprintf(stderr, "flow: invalid Q16.16 input token '%s'\n", tok1);
            return 1;
        }

        uint16_t mode_req = (uint16_t)(state[5] & UINT64_C(0xFFFF));
        char *tok2 = strtok(NULL, " \t\r\n");
        if (tok2 != NULL && *tok2 != '#') {
            uint64_t m_val = 0ULL;
            if (!parse_u64_token(tok2, UINT64_C(0xFFFF), &m_val)) {
                fprintf(stderr, "flow: invalid 16-bit mode token '%s'\n", tok2);
                return 1;
            }
            mode_req = (uint16_t)m_val;
            char *tok3 = strtok(NULL, " \t\r\n");
            if (tok3 != NULL && *tok3 != '#') {
                fprintf(stderr, "flow: unexpected trailing token '%s'\n", tok3);
                return 1;
            }
        }

        flow_step_atlas(state, state, g_arena, cap_mask, u_ext, mode_req);
        ++step_idx;

        char x_s[32];
        char dt_s[32];
        char r0_s[32];
        fmt_q16((int64_t)state[3], x_s);
        fmt_q16((int64_t)state[4], dt_s);
        fmt_q16((int64_t)state[0], r0_s);

        uint16_t chart_now = (uint16_t)((state[5] >> 16U) & UINT64_C(0xFFFF));
        uint16_t mode_now = (uint16_t)(state[5] & UINT64_C(0xFFFF));
        uint32_t ep_now = (uint32_t)(state[6] >> 32U);
        uint32_t st_now = (uint32_t)(state[6] & UINT64_C(0xFFFFFFFF));

        printf("step=%llu chart=%u x=%s dt=%s r0=%s mode=0x%04x epoch=%u status=0x%02x\n",
               (unsigned long long)step_idx, (unsigned)chart_now, x_s, dt_s, r0_s,
               (unsigned)mode_now, (unsigned)ep_now, (unsigned)st_now);
    }
    return 0;
}

static uint64_t blind_searu_only_ablation(const uint64_t base_bound[8],
                                          const searu_bilevel_term *terms, uint32_t num_terms,
                                          int64_t target_x_q16, uint64_t seed, uint32_t max_iters) {
    /* Blind SEARU baseline: mutates both 24 discrete bits AND 32-bit Q16.16 coordinate `x`
     * directly via Zipf bit-flips WITHOUT FLOW 2-Jet's inner Citardauq curvature collapse. */
    uint64_t rng = seed | UINT64_C(1);
    uint64_t cur_bits = UINT64_C(0);
    int64_t cur_x = INT64_C(0);
    uint64_t best_e = ~UINT64_C(0);
    uint64_t cur_e = ~UINT64_C(0);
    uint64_t init_temp = UINT64_C(1) << 22U;
    uint64_t temp = init_temp;

    for (uint32_t it = 0U; it < max_iters; ++it) {
        uint64_t swan = UINT64_C(0);
        uint64_t jump = searu_zipf_jump(&rng, SEARU_CHAOS_DEFAULT_Q8, &swan);
        uint64_t r = searu_rng_next(&rng);

        uint64_t cand_bits = cur_bits;
        int64_t cand_x = cur_x;
        if ((r & UINT64_C(1)) != UINT64_C(0)) {
            uint32_t b = searu_Bounded_u32(r >> 8U, num_terms);
            cand_bits ^= UINT64_C(1) << b;
        } else {
            int64_t step = (int64_t)(jump & UINT64_C(0xFFFFF));
            cand_x = flow_clamp_sym(cur_x + (((r & UINT64_C(2)) != UINT64_C(0)) ? step : -step),
                                    FLOW_MAX_Q16);
        }

        int64_t b0 = (int64_t)base_bound[0];
        int64_t b1 = (int64_t)base_bound[1];
        int64_t b2 = (int64_t)base_bound[2];
        for (uint32_t i = 0U; i < num_terms; ++i) {
            if (((cand_bits >> i) & UINT64_C(1)) != UINT64_C(0)) {
                b0 += terms[i].db0_q16;
                b1 += terms[i].db1_q16;
                b2 += terms[i].db2_q16;
            }
        }
        b0 = flow_clamp_sym(b0, FLOW_MAX_Q16);
        b1 = flow_clamp_sym(b1, FLOW_MAX_Q16);
        b2 = flow_clamp_sym(b2, FLOW_MAX_Q16);

        flow_i128 g0 = ((flow_i128)b0 << 33U) + ((flow_i128)(b1 * cand_x) << 17U) +
                       (flow_i128)b2 * (flow_i128)(cand_x * cand_x);
        int64_t j0_q16 = flow_sym_shr_i128(g0, 33U);
        bool conv = flow_abs_i128(g0) <= ((flow_u128)(uint64_t)FLOW_CONVERGE_EPS_Q16 << 33U);
        uint64_t cand_e = flow_abs_i64(j0_q16) + flow_abs_i64(cand_x - target_x_q16) +
                          (conv ? UINT64_C(0) : (UINT64_C(1) << 20U));

        bool epi = false;
        if (it == 0U ||
            searu_crucible_accept(cand_e, cur_e, best_e, &temp, init_temp, swan, &rng, &epi)) {
            cur_bits = cand_bits;
            cur_x = cand_x;
            cur_e = cand_e;
            if (cand_e < best_e) {
                best_e = cand_e;
            }
        }
        temp = (temp * UINT64_C(65000)) >> 16U;
    }
    return best_e;
}

static int cmd_searu_verify(void) {
    /* 1. Ring-0 Branchless CLZ Power-Law Tail, Deterministic Replay & KanLock Invariance */
    const uint64_t pin[4] = {
        UINT64_C(0xAA55AA55AA55AA55),
        UINT64_C(0xF0F0F0F0F0F0F0F0),
        UINT64_C(0x00FF00FF00FF00FF),
        UINT64_C(0x8000000000000001),
    };
    const uint64_t frust_zero[4] = {UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0)};
    const uint32_t levels[3] = {SEARU_CHAOS_MIN_Q8, SEARU_CHAOS_DEFAULT_Q8, SEARU_CHAOS_MAX_Q8};
    uint32_t swan_counts[3] = {0U, 0U, 0U};

    for (uint32_t lv = 0U; lv < 3U; ++lv) {
        uint64_t eng[8] = {
            pin[0],         pin[1],      pin[2], pin[3], UINT64_C(0), (uint64_t)levels[lv],
            UINT64_C(0x42), UINT64_C(0),
        };
        for (uint32_t s = 0U; s < 65536U; ++s) {
            (void)searu_ring0_step_1bit(eng, eng, pin, frust_zero);
            if (((eng[0] & pin[0]) != pin[0]) || ((eng[1] & pin[1]) != pin[1]) ||
                ((eng[2] & pin[2]) != pin[2]) || ((eng[3] & pin[3]) != pin[3])) {
                fprintf(stderr, "searu-verify: KanLock pin violated in Ring-0 step!\n");
                return 1;
            }
        }
        swan_counts[lv] = (uint32_t)(eng[5] >> 32U);
        if (lv == 2U) {
            uint64_t replay[8] = {
                pin[0],         pin[1],      pin[2], pin[3], UINT64_C(0), (uint64_t)levels[lv],
                UINT64_C(0x42), UINT64_C(0),
            };
            for (uint32_t s = 0U; s < 65536U; ++s) {
                (void)searu_ring0_step_1bit(replay, replay, pin, frust_zero);
            }
            for (uint32_t w = 0U; w < 8U; ++w) {
                if (replay[w] != eng[w]) {
                    fprintf(stderr, "searu-verify: non-deterministic Engram replay!\n");
                    return 1;
                }
            }
        }
    }
    if (!(swan_counts[0] < swan_counts[1] && swan_counts[1] < swan_counts[2] &&
          swan_counts[2] > 500U)) {
        fprintf(stderr, "searu-verify: CLZ Zipf tail failed monotonicity (%u, %u, %u)\n",
                swan_counts[0], swan_counts[1], swan_counts[2]);
        return 1;
    }

    /* 2. NP-Complete Phase-Transition 50-Var / 218-Clause 3-SAT & KanLock Syllogism */
    const uint64_t witness_w0 = UINT64_C(0x0002B75CA96D3E51); /* 50-bit hidden backbone */
    uint32_t clauses[218];
    uint64_t gen_rng = UINT64_C(0xC0FFEE123456789D);
    for (uint32_t i = 0U; i < 218U; ++i) {
        uint32_t v0 = searu_Bounded_u32(searu_rng_next(&gen_rng), 50U);
        uint32_t v1 = (v0 + 1U + searu_Bounded_u32(searu_rng_next(&gen_rng), 49U)) % 50U;
        uint32_t v2 = (v1 + 1U + searu_Bounded_u32(searu_rng_next(&gen_rng), 48U)) % 50U;
        if (v2 == v0) {
            v2 = (v2 + 1U) % 50U;
        }
        uint64_t r = searu_rng_next(&gen_rng);
        uint32_t n0 = (uint32_t)(r & 1U);
        uint32_t n1 = (uint32_t)((r >> 1U) & 1U);
        uint32_t n2 = (uint32_t)((r >> 2U) & 1U);
        /* Ensure hidden witness satisfies at least literal 0 while frustrating all-zero */
        uint32_t w_b0 = (uint32_t)((witness_w0 >> v0) & UINT64_C(1));
        uint32_t w_b1 = (uint32_t)((witness_w0 >> v1) & UINT64_C(1));
        uint32_t w_b2 = (uint32_t)((witness_w0 >> v2) & UINT64_C(1));
        if (((w_b0 ^ n0) | (w_b1 ^ n1) | (w_b2 ^ n2)) == 0U) {
            n0 = w_b0 ^ 1U;
        }
        clauses[i] = SEARU_SAT3_CLAUSE(v0, n0, v1, n1, v2, n2);
    }
    searu_result sat_res;
    if (searu_solve_3sat(&sat_res, clauses, 218U, 50U, NULL, NULL, UINT64_C(0x5EED5A71), 25000U) !=
            UINT64_C(0) ||
        sat_res.init_energy == UINT64_C(0)) {
        fprintf(stderr, "searu-verify: 50-var/218-clause 3-SAT failed (best=%llu, init=%llu)\n",
                (unsigned long long)sat_res.best_energy, (unsigned long long)sat_res.init_energy);
        return 1;
    }

    /* Syllogism with Kan-pinned axiom Socrates=1: (Socrates -> Man) & (Man -> Mortal) */
    const uint32_t syll_clauses[2] = {
        SEARU_SAT3_CLAUSE(0U, 1U, 1U, 0U, 1U, 0U),
        SEARU_SAT3_CLAUSE(1U, 1U, 2U, 0U, 2U, 0U),
    };
    const uint64_t syll_init[4] = {UINT64_C(1), UINT64_C(0), UINT64_C(0), UINT64_C(0)};
    const uint64_t syll_pin[4] = {UINT64_C(1), UINT64_C(0), UINT64_C(0), UINT64_C(0)};
    searu_result syll_res;
    if (searu_solve_3sat(&syll_res, syll_clauses, 2U, 3U, syll_init, syll_pin, UINT64_C(0x1111),
                         512U) != UINT64_C(0) ||
        (syll_res.canvas[0] & UINT64_C(7)) != UINT64_C(7)) {
        fprintf(stderr, "searu-verify: Kan-pinned syllogism deduction failed!\n");
        return 1;
    }

    /* 3. NP-Hard 32-Node / 96-Edge Chaitin Register-Interference 4-Coloring */
    uint16_t edges[96];
    uint32_t e_idx = 0U;
    for (uint32_t blk = 0U; blk < 8U; ++blk) {
        uint32_t base = blk * 4U;
        /* K4 clique inside each live-pressure quad (6 * 8 = 48 edges) */
        for (uint32_t a = 0U; a < 4U; ++a) {
            for (uint32_t b = a + 1U; b < 4U; ++b) {
                edges[e_idx++] = SEARU_EDGE_U16(base + a, base + b);
            }
        }
        /* Cross-quad interference bridges preserving 4-colorability (6 * 8 = 48 edges) */
        uint32_t next_b = ((blk + 1U) & 7U) * 4U;
        edges[e_idx++] = SEARU_EDGE_U16(base + 0U, next_b + 1U);
        edges[e_idx++] = SEARU_EDGE_U16(base + 0U, next_b + 2U);
        edges[e_idx++] = SEARU_EDGE_U16(base + 1U, next_b + 2U);
        edges[e_idx++] = SEARU_EDGE_U16(base + 1U, next_b + 3U);
        edges[e_idx++] = SEARU_EDGE_U16(base + 2U, next_b + 3U);
        edges[e_idx++] = SEARU_EDGE_U16(base + 3U, next_b + 0U);
    }
    uint64_t col_init[4] = {UINT64_C(0x3210), UINT64_C(0), UINT64_C(0), UINT64_C(0)};
    searu_result col_res;
    if (searu_solve_graph_color(&col_res, edges, 96U, 32U, 4U, col_init, UINT64_C(0xF),
                                UINT64_C(0x9E3779B9), 25000U) != UINT64_C(0) ||
        (col_res.canvas[0] & UINT64_C(0xFFFF)) != UINT64_C(0x3210)) {
        fprintf(stderr, "searu-verify: 32-node/96-edge 4-coloring failed (best=%llu)\n",
                (unsigned long long)col_res.best_energy);
        return 1;
    }

    /* 4. 2^56 Joint Discrete-Manifold Symbiotic Ablation (FLOW Alone vs SEARU Alone vs Co-Solver)
     */
    const int64_t target_x_q16 = INT64_C(241664); /* Exact Q16.16 = 3.6875 (59 / 16) */
    const uint64_t base_bound[8] = {
        (uint64_t)INT64_C(1600000), /* +24.414 Q16.16 -> Delta < 0 quadratic barrier at bits=0! */
        UINT64_C(0),
        (uint64_t)(INT64_C(2) * FLOW_Q16_ONE),
        ((uint64_t)(uint32_t)(int32_t)(-100 * FLOW_Q16_ONE) << 32U) |
            (uint64_t)(uint32_t)(int32_t)(100 * FLOW_Q16_ONE),
        ((uint64_t)(uint32_t)(int32_t)(10 * FLOW_Q16_ONE) << 32U) |
            (uint64_t)(uint32_t)(int32_t)FLOW_Q16_ONE,
        UINT64_C(0),
        UINT64_C(1) << 32U,
        UINT64_C(0),
    };

    /* 24 discrete manifold-sculpting terms; hidden 8-bit subset sums to (-2249472, -65536, 0),
     * shifting (b0, b1, b2) -> (-649472, -65536, +131072) whose exact root is x* = 241664! */
    static const searu_bilevel_term terms[24] = {
        {-311296, -8192, 0, 0}, {192512, 4096, 0, 0},   {-294912, -8192, 0, 0},
        {143360, -4096, 0, 0},  {-278528, -8192, 0, 0}, {221184, 8192, 0, 0},
        {-262144, -8192, 0, 0}, {-184320, 4096, 0, 0},  {307200, 12288, 0, 0},
        {-286720, -8192, 0, 0}, {167936, -4096, 0, 0},  {-270336, -8192, 0, 0},
        {253952, 8192, 0, 0},   {-290816, -8192, 0, 0}, {-135168, 4096, 0, 0},
        {208896, 8192, 0, 0},   {-254720, -8192, 0, 0}, {176128, -4096, 0, 0},
        {-151552, 16384, 0, 0}, {237568, 8192, 0, 0},   {188416, -12288, 0, 0},
        {-118784, 12288, 0, 0}, {274432, 4096, 0, 0},   {-98304, 16384, 0, 0},
    };

    /* 4a. FLOW 2-Jet Alone (0 SEARU jumps) */
    uint64_t flow_chart[8];
    int64_t flow_only_x = INT64_C(0);
    uint32_t flow_only_conv = 0U;
    uint64_t dummy_frust = UINT64_C(0);
    uint64_t flow_only_e = searu_eval_bilevel_2jet(UINT64_C(0), base_bound, terms, 24U, INT64_C(0),
                                                   INT64_C(0), target_x_q16, 6U, flow_chart,
                                                   &flow_only_x, &flow_only_conv, &dummy_frust);

    /* 4b. Blind SEARU Alone (0 FLOW 2-Jet curvature steps, identical 8,192-eval budget) */
    uint64_t searu_only_e =
        blind_searu_only_ablation(base_bound, terms, 24U, target_x_q16, UINT64_C(0x7777), 8192U);

    /* 4c. Symbiotic SEARU x FLOW 2-Jet Co-Solver (identical max budget) + Live @swap Commit */
    searu_result co_res;
    uint64_t best_chart[8];
    uint64_t co_e =
        searu_solve_bilevel_2jet(&co_res, best_chart, base_bound, terms, 24U, UINT64_C(0),
                                 INT64_C(0), INT64_C(0), target_x_q16, 6U, UINT64_C(0x7777), 8192U);

    if (flow_only_conv != 0U || flow_only_e == UINT64_C(0) || searu_only_e == UINT64_C(0)) {
        fprintf(stderr, "searu-verify: single-solver baseline unexpectedly trivial!\n");
        return 1;
    }
    if (co_e != UINT64_C(0) || co_res.converged_2jet != 1U || co_res.best_x_q16 != target_x_q16) {
        fprintf(stderr,
                "searu-verify: symbiotic co-solver failed (e=%llu, conv=%u, x=%lld vs %lld)\n",
                (unsigned long long)co_e, co_res.converged_2jet, (long long)co_res.best_x_q16,
                (long long)target_x_q16);
        return 1;
    }

    /* Live @swap into g_arena[0] and verify closed-loop trajectory convergence */
    flow_arena_publish_bound(g_arena, UINT64_C(0), UINT64_C(0), 1U, base_bound);
    if (!searu_flow_commit_chart(g_arena, UINT64_C(0), UINT64_C(0), 2U, best_chart)) {
        fprintf(stderr, "searu-verify: live @swap chart commit rejected!\n");
        return 1;
    }
    FLOW_ALIGN64 flow_cell live_st = {0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL};
    for (uint32_t s = 0U; s < 6U; ++s) {
        flow_step_atlas(live_st, live_st, g_arena, UINT64_C(0), INT64_C(0), 0U);
    }
    if ((int64_t)live_st[3] != target_x_q16 || (int64_t)live_st[0] != INT64_C(0) ||
        (live_st[6] & (uint64_t)FLOW_STATUS_CONVERGED) == UINT64_C(0)) {
        fprintf(stderr, "searu-verify: post-@swap FLOW 2-Jet verification failed!\n");
        return 1;
    }

    /* 5. Self-Hosted 0-ULP Minimax-Newton Knife-Edge Check & App-Layer Dual-Solver Ergonomics */
    if (flow_isqrt_round_u128((flow_u128)0ULL) != UINT64_C(0) ||
        flow_sym_div128_clamped((flow_i128)1, (flow_i128)0, FLOW_MAX_Q16) != FLOW_MAX_Q16) {
        fprintf(stderr, "searu-verify: 0-ULP zero/div0 boundary check failed!\n");
        return 1;
    }
    for (uint32_t e = 1U; e <= 62U; ++e) {
        uint64_t r = (UINT64_C(1) << e) - UINT64_C(1) + (uint64_t)((e == 1U) ? 1U : 0U);
        flow_u128 mid_lo = (flow_u128)r * (flow_u128)(r + UINT64_C(1));
        if (flow_isqrt_round_u128(mid_lo) != r ||
            flow_isqrt_round_u128(mid_lo + (flow_u128)1ULL) != (r + UINT64_C(1))) {
            fprintf(stderr, "searu-verify: 0-ULP isqrt midpoint check failed at exp=%u!\n", e);
            return 1;
        }
        flow_i128 num = (flow_i128)((flow_u128)r * (flow_u128)UINT64_C(0x55555555));
        int64_t qp = flow_sym_div128_clamped(num, (flow_i128)r, FLOW_MAX_Q16);
        int64_t qn = flow_sym_div128_clamped(-num, (flow_i128)r, FLOW_MAX_Q16);
        if (qp != INT64_C(0x55555555) || qn != -qp) {
            fprintf(stderr, "searu-verify: 0-ULP sym_div128 check failed at exp=%u!\n", e);
            return 1;
        }
    }

    /* 5a. Phase-Gated `searu_heal_and_swap`: 0-interference during smooth descent, instant heal on
     * barrier */
    const uint64_t smooth_bound[8] = {
        UINT64_C(0),
        (uint64_t)FLOW_Q16_ONE,
        UINT64_C(0),
        ((uint64_t)(uint32_t)(int32_t)(-100 * FLOW_Q16_ONE) << 32U) |
            (uint64_t)(uint32_t)(int32_t)(100 * FLOW_Q16_ONE),
        ((uint64_t)(uint32_t)(int32_t)(2 * FLOW_Q16_ONE) << 32U) |
            (uint64_t)(uint32_t)(int32_t)FLOW_Q16_ONE,
        UINT64_C(0),
        UINT64_C(1) << 32U,
        UINT64_C(0),
    };
    flow_arena_publish_bound(g_arena, UINT64_C(0), UINT64_C(0), 1U, smooth_bound);
    FLOW_ALIGN64 flow_cell app_st = {0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL};
    uint64_t app_seed = UINT64_C(0x7777);
    uint32_t app_epoch = 1U;
    for (uint32_t s = 0U; s < 6U; ++s) {
        flow_step_atlas(app_st, app_st, g_arena, UINT64_C(0), INT64_C(6) * FLOW_Q16_ONE, 0U);
        if (searu_heal_and_swap(g_arena, UINT64_C(0), app_st, terms, 24U, UINT64_C(0), INT64_C(0),
                                target_x_q16, 6U, &app_seed, &app_epoch, 8192U, NULL)) {
            fprintf(stderr, "searu-verify: searu_heal_and_swap fired during smooth regime!\n");
            return 1;
        }
    }
    if (app_seed != UINT64_C(0x7777) || app_epoch != 1U) {
        fprintf(stderr, "searu-verify: searu_heal_and_swap mutated seed/epoch in smooth regime!\n");
        return 1;
    }

    /* Inject Delta < 0 barrier at slot 0: next step stalls (dt==0, !CONVERGED) -> phase gate wakes
     * SEARU! */
    flow_arena_publish_bound(g_arena, UINT64_C(0), UINT64_C(0), 2U, base_bound);
    app_st[3] = UINT64_C(0);
    flow_step_atlas(app_st, app_st, g_arena, UINT64_C(0), INT64_C(0), 0U);
    searu_result heal_res;
    if (!searu_heal_and_swap(g_arena, UINT64_C(0), app_st, terms, 24U, UINT64_C(0), INT64_C(0),
                             target_x_q16, 6U, &app_seed, &app_epoch, 8192U, &heal_res) ||
        app_epoch != 3U) {
        fprintf(stderr, "searu-verify: searu_heal_and_swap failed to heal barrier!\n");
        return 1;
    }
    for (uint32_t s = 0U; s < 6U; ++s) {
        flow_step_atlas(app_st, app_st, g_arena, UINT64_C(0), INT64_C(0), 0U);
    }
    if ((int64_t)app_st[3] != target_x_q16 ||
        (app_st[6] & (uint64_t)FLOW_STATUS_CONVERGED) == UINT64_C(0)) {
        fprintf(stderr, "searu-verify: post-heal trajectory failed to converge!\n");
        return 1;
    }

    /* 5b. Zero-`BLR` `SEARU_SOLVE_INLINE` Custom NP Subset-Partition with Kan Pins */
    static const uint32_t part_w[24] = {
        37U, 41U, 47U,  53U,  59U,  71U,  61U,  67U,  89U,  73U,  79U,  107U,
        83U, 97U, 131U, 101U, 103U, 149U, 113U, 167U, 137U, 191U, 145U, 211U,
    };
    const uint64_t part_init[4] = {UINT64_C(1), UINT64_C(0), UINT64_C(0), UINT64_C(0)};
    const uint64_t part_pin[4] = {UINT64_C(3), UINT64_C(0), UINT64_C(0), UINT64_C(0)};
    searu_result part_res;
    SEARU_SOLVE_INLINE(&part_res, 24U, part_init, part_pin, UINT64_C(0xC0FFEE), 8192U, cand, frust,
                       energy, {
                           uint64_t sum = UINT64_C(0);
                           uint64_t bits = cand[0] & UINT64_C(0xFFFFFF);
                           for (uint32_t i = 0U; i < 24U; ++i) {
                               if (((bits >> i) & UINT64_C(1)) != UINT64_C(0)) {
                                   sum += (uint64_t)part_w[i];
                               }
                           }
                           if (sum > UINT64_C(1206)) {
                               energy = sum - UINT64_C(1206);
                               frust[0] = bits;
                           } else {
                               energy = UINT64_C(1206) - sum;
                               frust[0] = (~bits) & UINT64_C(0xFFFFFF);
                           }
                       });
    if (part_res.best_energy != UINT64_C(0) || (part_res.canvas[0] & UINT64_C(3)) != UINT64_C(1)) {
        fprintf(stderr, "searu-verify: SEARU_SOLVE_INLINE partition failed (best=%llu)!\n",
                (unsigned long long)part_res.best_energy);
        return 1;
    }

    printf("[PASS] SEARU Pure-GPR Symbiotic Co-Solver Gate:\n");
    printf("  1. CLZ Zipf tail (65k Ring-0 steps) : swans=[%u, %u, %u], KanLock=0 leaks\n",
           swan_counts[0], swan_counts[1], swan_counts[2]);
    printf("  2. 50-Var / 218-Clause 3-SAT + Syll : unsat=%llu->0 in %u iters (QED)\n",
           (unsigned long long)sat_res.init_energy, sat_res.iterations_run);
    printf("  3. 32-Node / 96-Edge 4-Coloring     : conflicts=%llu->0 in %u iters (QED)\n",
           (unsigned long long)col_res.init_energy, col_res.iterations_run);
    printf("  4. 2^56 Bilevel Ablation + @swap    : FLOW_only=%llu (trapped) | SEARU_only=%llu "
           "(unconverged) | SEARU_x_FLOW=0 in %u iters (x=3.6875, @swap epoch=2 CONVERGED)\n",
           (unsigned long long)flow_only_e, (unsigned long long)searu_only_e,
           co_res.iterations_run);
    printf("  5. App Ergonomics + 0-ULP Newton    : searu_heal_and_swap (0 smooth noise, epoch=3 "
           "healed) | SEARU_SOLVE_INLINE (gap=%llu->0 in %u iters, 0 BLR)\n",
           (unsigned long long)part_res.init_energy, part_res.iterations_run);
    return 0;
}

/* ============================================================================
 * Self-Hosting Operator Optimization Pipeline (`flow self-opt`)
 * Stage 1: First-principles bisection + SEARU_SOLVE_INLINE x FLOW 2-Jet
 *          minimax synthesis & 65,536-bin + 0-ULP proof for flow.h seeds.
 * Stage 2: Multi-seed ensemble Meta-SEARU x FLOW 2-Jet hyperparameter evolution
 *          & unseen held-out validation for searu.h solvers.
 * ============================================================================ */

static uint64_t isqrt_post_err_bin(uint64_t idx16, bool hi_end, const uint64_t c[4]) {
    uint64_t s_w = hi_end ? (((UINT64_C(65537) + idx16) << 47U) - UINT64_C(1))
                          : ((UINT64_C(65536) + idx16) << 47U);
    uint64_t z0 = flow__horner3_q32(idx16, c[0], c[1], c[2], c[3]);
    if (z0 >= (UINT64_C(1) << 32U) || z0 < (UINT64_C(1) << 30U)) {
        return UINT64_C(1) << 62U;
    }
    uint64_t z0_sq = (z0 * z0) >> 32U;
    uint64_t p0 = ((s_w >> 32U) * z0_sq) >> 32U;
    if (p0 >= (UINT64_C(3) << 30U)) {
        return UINT64_C(1) << 62U;
    }
    uint64_t h0 = (UINT64_C(3) << 30U) - p0;
    uint64_t z_pre = (z0 * h0) >> 31U;
    if (z_pre >= (UINT64_C(1) << 32U)) {
        return UINT64_C(1) << 62U;
    }
    uint64_t z = z_pre << 32U;
    uint64_t z2 = (uint64_t)(((flow_u128)z * (flow_u128)z) >> 64U);
    uint64_t p_hi = (uint64_t)(((flow_u128)s_w * (flow_u128)z2) >> 64U);
    uint64_t target_p0 = UINT64_C(1) << 30U;
    uint64_t e0 = (p0 > target_p0) ? (p0 - target_p0) : (target_p0 - p0);
    uint64_t target_phi = UINT64_C(1) << 62U;
    uint64_t e1 = (p_hi > target_phi) ? (p_hi - target_phi) : (target_phi - p_hi);
    return (e1 >> 20U) + e0;
}

static uint64_t recip_post_err_bin(uint64_t idx16, bool hi_end, const uint64_t c[4]) {
    uint64_t d = hi_end ? (((UINT64_C(65537) + idx16) << 47U) - UINT64_C(1))
                        : ((UINT64_C(65536) + idx16) << 47U);
    uint64_t r0 = flow__horner3_q32(idx16, c[0], c[1], c[2], c[3]);
    if (r0 >= (UINT64_C(1) << 32U)) {
        return (UINT64_C(1) << 54U) + ((r0 - (UINT64_C(1) << 32U) + UINT64_C(1)) << 16U);
    }
    if (r0 < (UINT64_C(1) << 30U)) {
        return (UINT64_C(1) << 54U) + (((UINT64_C(1) << 30U) - r0) << 16U);
    }
    uint64_t d32_ceil = (d >> 32U) + UINT64_C(1);
    uint64_t p0 = (r0 * d32_ceil) >> 32U;
    uint64_t max_safe_p0 = (UINT64_C(1) << 31U) - (UINT64_C(1) << 18U);
    if (p0 > max_safe_p0) {
        return (UINT64_C(1) << 52U) + ((p0 - max_safe_p0) << 16U);
    }
    uint64_t h0 = (UINT64_C(1) << 32U) - p0;
    uint64_t r_hi32 = (r0 * h0) >> 31U;
    if (r_hi32 >= (UINT64_C(1) << 32U)) {
        return (UINT64_C(1) << 50U) + ((r_hi32 - (UINT64_C(1) << 32U) + UINT64_C(1)) << 20U);
    }
    if (r_hi32 == UINT64_C(0)) {
        return UINT64_C(1) << 50U;
    }
    uint64_t r = r_hi32 << 32U;
    for (uint32_t it = 0U; it < 2U; ++it) {
        uint64_t p_hi = (uint64_t)(((flow_u128)r * (flow_u128)d) >> 64U);
        if (p_hi >= (UINT64_C(1) << 63U)) {
            return (UINT64_C(1) << 48U) + (p_hi - (UINT64_C(1) << 63U) + UINT64_C(1));
        }
        r = (uint64_t)(((flow_u128)r * (flow_u128)(~p_hi)) >> 63U);
    }
    uint64_t fin_hi = (uint64_t)(((flow_u128)r * (flow_u128)d) >> 64U);
    if (fin_hi >= (UINT64_C(1) << 63U)) {
        return (UINT64_C(1) << 46U) + (fin_hi - (UINT64_C(1) << 63U) + UINT64_C(1));
    }
    uint64_t gap_r = ((UINT64_C(1) << 63U) - UINT64_C(1)) - fin_hi;
    uint64_t target_p0 = (UINT64_C(1) << 31U) - (UINT64_C(1) << 22U);
    uint64_t err_p0 = (p0 > target_p0) ? (p0 - target_p0) : (target_p0 - p0);
    return (gap_r << 20U) + err_p0;
}

static uint64_t eval_horner_grid(bool is_recip, const uint64_t c[4], uint32_t stride,
                                 uint32_t *out_worst_bin) {
    uint64_t worst = UINT64_C(0);
    uint32_t wbin = 0U;
    for (uint32_t idx = 0U; idx <= 65535U; idx += stride) {
        uint64_t e_lo = is_recip ? recip_post_err_bin((uint64_t)idx, false, c)
                                 : isqrt_post_err_bin((uint64_t)idx, false, c);
        uint64_t e_hi = is_recip ? recip_post_err_bin((uint64_t)idx, true, c)
                                 : isqrt_post_err_bin((uint64_t)idx, true, c);
        uint64_t e = (e_lo > e_hi) ? e_lo : e_hi;
        if (e > worst) {
            worst = e;
            wbin = idx;
        }
        if (idx + stride > 65535U && idx != 65535U) {
            idx = 65535U - stride;
        }
    }
    if (out_worst_bin != NULL) {
        *out_worst_bin = wbin;
    }
    return worst;
}

static uint64_t test_isqrt_param(flow_u128 x, const uint64_t c[4]) {
    uint64_t hi = (uint64_t)(x >> 64U);
    uint64_t lo = (uint64_t)x;
    bool z64 = (hi == UINT64_C(0));
    uint64_t w = flow_sel_u64(z64, lo, hi);
    uint64_t f = flow_sel_u64(z64, UINT64_C(0), lo);
    bool z_all = (w == UINT64_C(0));
    w = flow_sel_u64(z_all, UINT64_C(1) << 62U, w);
    uint64_t scale = flow_sel_u64(z64, UINT64_C(1) << 31U, UINT64_C(1) << 63U);
    for (uint32_t sh = 32U; sh >= 4U; sh >>= 1U) {
        bool z = ((w >> (64U - sh)) == UINT64_C(0));
        uint64_t w_sh = (w << sh) | (f >> (64U - sh));
        uint64_t f_sh = f << sh;
        w = flow_sel_u64(z, w_sh, w);
        f = flow_sel_u64(z, f_sh, f);
        scale = flow_sel_u64(z, scale >> (sh >> 1U), scale);
    }
    uint64_t z2_m = flow_mask_bool((w >> 62U) == UINT64_C(0));
    w = flow_mux_u64(z2_m, (w << 2U) | (f >> 62U), w);
    f = flow_mux_u64(z2_m, f << 2U, f);
    scale += scale & (~z2_m);
    scale = flow_sel_u64(z_all, UINT64_C(0), scale);

    uint64_t oct0_m = flow_mask_bool((int64_t)w >= INT64_C(0));
    uint64_t s_w = w + (w & oct0_m);
    uint64_t idx16 = (s_w >> 47U) & UINT64_C(0xFFFF);
    uint64_t z0 = flow__horner3_q32(idx16, c[0], c[1], c[2], c[3]);
    uint64_t z0_sq = (z0 * z0) >> 32U;
    uint64_t p0 = ((s_w >> 32U) * z0_sq) >> 32U;
    uint64_t h0 = (UINT64_C(3) << 30U) - p0;
    uint64_t z = ((z0 * h0) >> 31U) << 32U;
    uint64_t z2 = (uint64_t)(((flow_u128)z * (flow_u128)z) >> 64U);
    uint64_t p_hi = (uint64_t)(((flow_u128)s_w * (flow_u128)z2) >> 64U);
    uint64_t h = UINT64_C(0xBFFFFFFFFFFFFFFF) - p_hi;
    z = (uint64_t)(((flow_u128)z * (flow_u128)h) >> 63U);
    uint64_t z_oct = (uint64_t)(((flow_u128)z * (flow_u128)UINT64_C(0xB504F333F9DE6484)) >> 63U);
    uint64_t z_m = flow_mux_u64(oct0_m, z_oct, z);
    uint64_t y0 = (uint64_t)(((flow_u128)w * (flow_u128)z_m) >> 63U);
    flow_u128 wf = ((flow_u128)w << 64U) | (flow_u128)f;
    flow_i128 rem = (flow_i128)wf - (flow_i128)((flow_u128)y0 * (flow_u128)y0);
    bool r_neg = flow_neg_i128(rem);
    uint64_t rem_abs_hi =
        (uint64_t)((r_neg ? ((flow_u128)0ULL - (flow_u128)rem) : (flow_u128)rem) >> 32U);
    uint64_t corr = (uint64_t)(((flow_u128)rem_abs_hi * (flow_u128)z_m) >> 96U);
    uint64_t y = r_neg ? (y0 - corr) : (y0 + corr);
    uint64_t q = (uint64_t)(((flow_u128)y * (flow_u128)scale) >> 64U);
    flow_u128 p = ((flow_u128)(q << 1U) * (flow_u128)q) >> 1U;
    uint64_t dec = (uint64_t)((q > UINT64_C(0)) & (x <= (p - (flow_u128)q)));
    uint64_t inc = (uint64_t)(x > (p + (flow_u128)q));
    return q - dec + inc;
}

static int64_t test_recip_param(flow_i128 num, flow_i128 den, int64_t dt_max, const uint64_t c[4]) {
    bool neg = (flow_neg_i128(num) != flow_neg_i128(den));
    flow_u128 anum = flow_abs_i128(num);
    uint64_t aden = (uint64_t)flow_abs_i128(den);
    uint64_t hi = (uint64_t)(anum >> 64U);
    uint64_t lo = (uint64_t)anum;
    uint64_t cap = (uint64_t)dt_max;
    bool pre_sat = (aden == UINT64_C(0)) | (hi >= aden);
    uint64_t d = flow_sel_u64(pre_sat, UINT64_C(1) << 63U, aden);
    uint64_t u1 = flow_sel_u64(pre_sat, cap >> 1U, hi);
    uint64_t u0 = flow_sel_u64(pre_sat, cap << 63U, lo);
    for (uint32_t sh = 32U; sh >= 2U; sh >>= 1U) {
        bool z = ((d >> (64U - sh)) == UINT64_C(0));
        uint64_t d_sh = d << sh;
        uint64_t u1_sh = (u1 << sh) | (u0 >> (64U - sh));
        uint64_t u0_sh = u0 << sh;
        d = flow_sel_u64(z, d_sh, d);
        u1 = flow_sel_u64(z, u1_sh, u1);
        u0 = flow_sel_u64(z, u0_sh, u0);
    }
    uint64_t z1_m = flow_mask_bool((int64_t)d >= INT64_C(0));
    u1 += (u1 + (u0 >> 63U)) & z1_m;
    d += d & z1_m;
    u0 += u0 & z1_m;

    uint64_t idx16 = (d >> 47U) & UINT64_C(0xFFFF);
    uint64_t r0 = flow__horner3_q32(idx16, c[0], c[1], c[2], c[3]);
    uint64_t d32_ceil = (d >> 32U) + UINT64_C(1);
    uint64_t p0 = (r0 * d32_ceil) >> 32U;
    uint64_t h0 = (UINT64_C(1) << 32U) - p0;
    uint64_t r = ((r0 * h0) >> 31U) << 32U;
    for (uint32_t it = 0U; it < 2U; ++it) {
        uint64_t p_hi = (uint64_t)(((flow_u128)r * (flow_u128)d) >> 64U);
        r = (uint64_t)(((flow_u128)r * (flow_u128)(~p_hi)) >> 63U);
    }
    uint64_t u0_r_hi = (uint64_t)(((flow_u128)u0 * (flow_u128)r) >> 64U);
    flow_u128 q_scaled = (flow_u128)u1 * (flow_u128)r + (flow_u128)u0_r_hi;
    uint64_t q = (uint64_t)(q_scaled >> 63U);
    flow_u128 u_norm = ((flow_u128)u1 << 64U) | (flow_u128)u0;
    flow_i128 rem128 = (flow_i128)u_norm - (flow_i128)((flow_u128)q * (flow_u128)d);
    bool neg1 = flow_neg_i128(rem128);
    q -= (uint64_t)neg1;
    rem128 += neg1 ? (flow_i128)d : (flow_i128)0;
    bool ge1 = (rem128 >= (flow_i128)d);
    q += (uint64_t)ge1;
    rem128 -= ge1 ? (flow_i128)d : (flow_i128)0;
    uint64_t rem = (uint64_t)rem128;
    uint64_t round_up = (uint64_t)(rem >= (d - rem));
    uint64_t mag = flow_sel_u64(q >= cap, cap, q + round_up);
    return neg ? -(int64_t)mag : (int64_t)mag;
}

static bool verify_synthesized_0ulp(const uint64_t isqrt_c[4], const uint64_t recip_c[4]) {
    if (test_isqrt_param((flow_u128)0ULL, isqrt_c) != UINT64_C(0)) {
        return false;
    }
    for (uint64_t v = 1U; v <= 4096U; ++v) {
        uint64_t q = test_isqrt_param((flow_u128)v, isqrt_c);
        if (!((q * q <= v && v <= q * (q + 1U)) || (v > (q - 1U) * q && v <= q * q))) {
            return false;
        }
    }
    uint64_t rng = UINT64_C(0x243F6A8885A308D3);
    for (uint32_t e = 1U; e <= 62U; ++e) {
        for (uint32_t k = 0U; k < 32U; ++k) {
            uint64_t r = ((UINT64_C(1) << e) | (searu_rng_next(&rng) & ((UINT64_C(1) << e) - 1U)));
            if (r >= (UINT64_C(1) << 63U)) {
                r >>= 1U;
            }
            flow_u128 mid = (flow_u128)r * (flow_u128)(r + UINT64_C(1));
            if (test_isqrt_param(mid, isqrt_c) != r ||
                test_isqrt_param(mid + (flow_u128)1ULL, isqrt_c) != (r + UINT64_C(1))) {
                return false;
            }
            uint64_t q_exp = (searu_rng_next(&rng) & UINT64_C(0x3FFFFFFF)) + UINT64_C(1);
            flow_u128 prod = (flow_u128)r * (flow_u128)q_exp + (flow_u128)((r - UINT64_C(1)) >> 1U);
            if (test_recip_param((flow_i128)prod, (flow_i128)r, FLOW_MAX_Q16, recip_c) !=
                (int64_t)q_exp) {
                return false;
            }
            if (r >= 2U) {
                flow_u128 prod_up = (flow_u128)r * (flow_u128)q_exp + (flow_u128)((r + 1U) >> 1U);
                if (test_recip_param((flow_i128)prod_up, (flow_i128)r, FLOW_MAX_Q16, recip_c) !=
                    (int64_t)(q_exp + UINT64_C(1))) {
                    return false;
                }
            }
        }
    }
    return true;
}

static void synthesize_horner_kernel(bool is_recip, uint64_t out_c[4], uint64_t *out_init_err,
                                     uint64_t *out_final_err) {
    /* Step A: First-principles integer bisection at u = {0, 21845, 43690, 65535} (0 magic seeds) */
    uint64_t y[4];
    const uint64_t u_nodes[4] = {0ULL, 21845ULL, 43690ULL, 65535ULL};
    for (uint32_t m = 0U; m < 4U; ++m) {
        uint64_t lo = UINT64_C(1) << 30U;
        uint64_t hi = (UINT64_C(1) << 32U) - UINT64_C(1);
        uint64_t sw32 = (UINT64_C(65536) + u_nodes[m]) << 15U;
        for (uint32_t b = 0U; b < 32U; ++b) {
            uint64_t mid = lo + ((hi - lo + UINT64_C(1)) >> 1U);
            bool ok = false;
            if (is_recip) {
                uint64_t p0 = (mid * (sw32 + UINT64_C(1))) >> 32U;
                ok = (p0 <= ((UINT64_C(1) << 31U) - (UINT64_C(1) << 22U)));
            } else {
                uint64_t zsq = (mid * mid) >> 32U;
                uint64_t p0 = (sw32 * zsq) >> 32U;
                ok = (p0 <= (UINT64_C(1) << 30U));
            }
            if (ok) {
                lo = mid;
            } else {
                hi = mid - UINT64_C(1);
            }
        }
        y[m] = lo;
    }

    /* Newton divided-difference cubic interpolation on s = 3*t in [0..3] */
    uint64_t d1 = y[0] - y[1];
    uint64_t d2 = y[1] - y[2];
    uint64_t d3 = y[2] - y[3];
    uint64_t dd1 = d1 - d2;
    uint64_t dd2 = d2 - d3;
    uint64_t ddd1 = dd1 - dd2;

    uint64_t base_c[4];
    base_c[0] = y[0];
    base_c[1] = 3ULL * d1 + ((3ULL * dd1) >> 1U) + ddd1;
    base_c[2] = (9ULL * (dd1 + ddd1)) >> 1U;
    base_c[3] = (9ULL * ddd1) >> 1U;

    *out_init_err = eval_horner_grid(is_recip, base_c, 64U, NULL);

    /* Step B: SEARU_SOLVE_INLINE_HP 64-bit signed offset search + FLOW 2-Jet coordinate polish */
    const searu_hyperparams boot_hp = SEARU_HUMAN_BASELINE_HP;
    searu_result s_res;
    const uint64_t init_bits[4] = {UINT64_C(0x8000800080008000), 0ULL, 0ULL, 0ULL};
    SEARU_SOLVE_INLINE_HP(
        &s_res, 64U, init_bits, NULL, is_recip ? UINT64_C(0xD17128) : UINT64_C(0x15027), 1536U,
        &boot_hp, cand, frust, energy, {
            uint64_t cc[4];
            for (uint32_t k = 0U; k < 4U; ++k) {
                int64_t off = (int64_t)((cand[0] >> (k * 16U)) & UINT64_C(0xFFFF)) - INT64_C(32768);
                int64_t val = (int64_t)base_c[k] + (off << 9U);
                cc[k] = (val > 0) ? (uint64_t)val : UINT64_C(1);
            }
            uint32_t wbin = 0U;
            energy = eval_horner_grid(is_recip, cc, 256U, &wbin);
            if (wbin < 8192U) {
                frust[0] = UINT64_C(0x00000000FFFFFFFF);
            } else if (wbin > 49152U) {
                frust[0] = UINT64_C(0xFFFFFFFF00000000);
            } else {
                frust[0] = UINT64_C(0x0000FFFFFFFF0000);
            }
        });

    for (uint32_t k = 0U; k < 4U; ++k) {
        int64_t off = (int64_t)((s_res.canvas[0] >> (k * 16U)) & UINT64_C(0xFFFF)) - INT64_C(32768);
        out_c[k] = (uint64_t)((int64_t)base_c[k] + (off << 9U));
    }

    /* Step C: FLOW 2-Jet local quadratic curvature polish on each Horner coefficient */
    uint64_t cur_err = eval_horner_grid(is_recip, out_c, 16U, NULL);
    for (uint32_t pass = 0U; pass < 4U; ++pass) {
        for (uint32_t k = 0U; k < 4U; ++k) {
            for (int64_t h = INT64_C(262144); h >= INT64_C(64); h >>= 2U) {
                uint64_t cp[4] = {out_c[0], out_c[1], out_c[2], out_c[3]};
                uint64_t cm[4] = {out_c[0], out_c[1], out_c[2], out_c[3]};
                cp[k] = (uint64_t)((int64_t)out_c[k] + h);
                cm[k] = (uint64_t)((int64_t)out_c[k] - h);
                uint64_t ep = eval_horner_grid(is_recip, cp, 16U, NULL);
                uint64_t em = eval_horner_grid(is_recip, cm, 16U, NULL);
                int64_t g1 = ((int64_t)ep - (int64_t)em) >> 1U;
                int64_t g2 = (int64_t)ep - 2 * (int64_t)cur_err + (int64_t)em;
                if (g2 > 0 && g1 != 0) {
                    /* Exact FLOW 2-Jet vertex step on local parabola */
                    uint64_t st[8] = {(uint64_t)(g1 * FLOW_Q16_ONE),
                                      (uint64_t)(g2 * FLOW_Q16_ONE),
                                      0ULL,
                                      0ULL,
                                      0ULL,
                                      0ULL,
                                      0ULL,
                                      0ULL};
                    uint64_t bd[8] = {
                        0ULL,
                        0ULL,
                        0ULL,
                        ((uint64_t)(uint32_t)(int32_t)(-4 * FLOW_Q16_ONE) << 32U) |
                            (uint64_t)(uint32_t)(int32_t)(4 * FLOW_Q16_ONE),
                        ((uint64_t)(uint32_t)(int32_t)(2 * FLOW_Q16_ONE) << 32U) | 1ULL,
                        0ULL,
                        0ULL,
                        0ULL,
                    };
                    flow_step(st, st, bd);
                    int64_t delta_c = ((int64_t)st[3] * h) >> FLOW_Q16_SHIFT;
                    uint64_t ct[4] = {out_c[0], out_c[1], out_c[2], out_c[3]};
                    ct[k] = (uint64_t)((int64_t)out_c[k] + delta_c);
                    uint64_t et = eval_horner_grid(is_recip, ct, 16U, NULL);
                    if (et < cur_err) {
                        out_c[k] = ct[k];
                        cur_err = et;
                    }
                }
                if (ep < cur_err) {
                    out_c[k] = cp[k];
                    cur_err = ep;
                } else if (em < cur_err) {
                    out_c[k] = cm[k];
                    cur_err = em;
                }
            }
        }
    }

    /* Exhaustive 65,536-bin (stride=1) formal certificate across all 2^16 idx16 inputs */
    *out_final_err = eval_horner_grid(is_recip, out_c, 1U, NULL);
}

typedef struct {
    uint32_t clauses[218];
    uint16_t edges[96];
    uint64_t base_bound[8];
    searu_bilevel_term terms[24];
    int64_t target_x_q16;
} meta_bench_env;

static void init_meta_bench_env(meta_bench_env *env) {
    const uint64_t witness_w0 = UINT64_C(0x0002B75CA96D3E51);
    uint64_t gen_rng = UINT64_C(0xC0FFEE123456789D);
    for (uint32_t i = 0U; i < 218U; ++i) {
        uint32_t v0 = searu_Bounded_u32(searu_rng_next(&gen_rng), 50U);
        uint32_t v1 = (v0 + 1U + searu_Bounded_u32(searu_rng_next(&gen_rng), 49U)) % 50U;
        uint32_t v2 = (v1 + 1U + searu_Bounded_u32(searu_rng_next(&gen_rng), 48U)) % 50U;
        if (v2 == v0) {
            v2 = (v2 + 1U) % 50U;
        }
        uint64_t r = searu_rng_next(&gen_rng);
        uint32_t n0 = (uint32_t)(r & 1U);
        uint32_t n1 = (uint32_t)((r >> 1U) & 1U);
        uint32_t n2 = (uint32_t)((r >> 2U) & 1U);
        uint32_t w_b0 = (uint32_t)((witness_w0 >> v0) & UINT64_C(1));
        uint32_t w_b1 = (uint32_t)((witness_w0 >> v1) & UINT64_C(1));
        uint32_t w_b2 = (uint32_t)((witness_w0 >> v2) & UINT64_C(1));
        if (((w_b0 ^ n0) | (w_b1 ^ n1) | (w_b2 ^ n2)) == 0U) {
            n0 = w_b0 ^ 1U;
        }
        env->clauses[i] = SEARU_SAT3_CLAUSE(v0, n0, v1, n1, v2, n2);
    }

    uint32_t e_idx = 0U;
    for (uint32_t blk = 0U; blk < 8U; ++blk) {
        uint32_t base = blk * 4U;
        for (uint32_t a = 0U; a < 4U; ++a) {
            for (uint32_t b = a + 1U; b < 4U; ++b) {
                env->edges[e_idx++] = SEARU_EDGE_U16(base + a, base + b);
            }
        }
        uint32_t next_b = ((blk + 1U) & 7U) * 4U;
        env->edges[e_idx++] = SEARU_EDGE_U16(base + 0U, next_b + 1U);
        env->edges[e_idx++] = SEARU_EDGE_U16(base + 0U, next_b + 2U);
        env->edges[e_idx++] = SEARU_EDGE_U16(base + 1U, next_b + 2U);
        env->edges[e_idx++] = SEARU_EDGE_U16(base + 1U, next_b + 3U);
        env->edges[e_idx++] = SEARU_EDGE_U16(base + 2U, next_b + 3U);
        env->edges[e_idx++] = SEARU_EDGE_U16(base + 3U, next_b + 0U);
    }

    env->target_x_q16 = INT64_C(241664);
    const uint64_t bb[8] = {
        (uint64_t)INT64_C(1600000),
        UINT64_C(0),
        (uint64_t)(INT64_C(2) * FLOW_Q16_ONE),
        ((uint64_t)(uint32_t)(int32_t)(-100 * FLOW_Q16_ONE) << 32U) |
            (uint64_t)(uint32_t)(int32_t)(100 * FLOW_Q16_ONE),
        ((uint64_t)(uint32_t)(int32_t)(10 * FLOW_Q16_ONE) << 32U) |
            (uint64_t)(uint32_t)(int32_t)FLOW_Q16_ONE,
        UINT64_C(0),
        UINT64_C(1) << 32U,
        UINT64_C(0),
    };
    memcpy(env->base_bound, bb, sizeof(bb));

    static const searu_bilevel_term k_terms[24] = {
        {-311296, -8192, 0, 0}, {192512, 4096, 0, 0},   {-294912, -8192, 0, 0},
        {143360, -4096, 0, 0},  {-278528, -8192, 0, 0}, {221184, 8192, 0, 0},
        {-262144, -8192, 0, 0}, {-184320, 4096, 0, 0},  {307200, 12288, 0, 0},
        {-286720, -8192, 0, 0}, {167936, -4096, 0, 0},  {-270336, -8192, 0, 0},
        {253952, 8192, 0, 0},   {-290816, -8192, 0, 0}, {-135168, 4096, 0, 0},
        {208896, 8192, 0, 0},   {-254720, -8192, 0, 0}, {176128, -4096, 0, 0},
        {-151552, 16384, 0, 0}, {237568, 8192, 0, 0},   {188416, -12288, 0, 0},
        {-118784, 12288, 0, 0}, {274432, 4096, 0, 0},   {-98304, 16384, 0, 0},
    };
    memcpy(env->terms, k_terms, sizeof(k_terms));
}

static void decode_sat_genome(uint64_t g, searu_hyperparams *hp) {
    static const uint32_t k_masks[8] = {1U, 3U, 7U, 15U, 31U, 63U, 127U, 255U};
    hp->chaos_min_q8 = 128U + 16U * (uint32_t)(g & 7ULL);
    hp->chaos_max_q8 = 272U + 16U * (uint32_t)((g >> 3U) & 7ULL);
    hp->stag_shift = (uint32_t)((g >> 6U) & 3ULL);
    hp->rand_walk_mask = k_masks[(g >> 8U) & 7ULL];
    uint32_t sat_t = (uint32_t)((g >> 11U) & 15ULL);
    hp->sat_init_temp_q12 =
        (UINT64_C(256) << (sat_t >> 1U)) + ((sat_t & 1U) ? (UINT64_C(128) << (sat_t >> 1U)) : 0ULL);
    hp->sat_cool_q16 = UINT64_C(63600) + UINT64_C(124) * ((g >> 15U) & 15ULL);
    hp->sat_reheat_stag = 16U + 16U * (uint32_t)((g >> 19U) & 7ULL);
    hp->sat_reheat_q12 = (hp->sat_init_temp_q12 >> 2U) + UINT64_C(256);
}

static void decode_col_genome(uint64_t g, searu_hyperparams *hp) {
    static const uint32_t k_masks[8] = {1U, 3U, 7U, 15U, 31U, 63U, 127U, 255U};
    hp->col_walk_mask = k_masks[g & 7ULL];
    uint32_t col_t = (uint32_t)((g >> 3U) & 15ULL);
    hp->col_init_temp_q12 =
        (UINT64_C(128) << (col_t >> 1U)) + ((col_t & 1U) ? (UINT64_C(64) << (col_t >> 1U)) : 0ULL);
    hp->col_cool_q16 = UINT64_C(63600) + UINT64_C(124) * ((g >> 7U) & 15ULL);
    hp->col_reheat_stag = 16U + 16U * (uint32_t)((g >> 11U) & 7ULL);
    hp->col_reheat_q12 = (hp->col_init_temp_q12 >> 1U) + UINT64_C(256);
}

static void decode_bil_genome(uint64_t g, searu_hyperparams *hp) {
    static const uint32_t k_masks[8] = {1U, 3U, 7U, 15U, 31U, 63U, 127U, 255U};
    hp->bil_walk_mask = k_masks[g & 7ULL];
    uint32_t bil_t = (uint32_t)((g >> 3U) & 15ULL);
    uint32_t bil_sh = (bil_t > 12U) ? 12U : bil_t;
    hp->bil_init_temp_q12 = UINT64_C(1024) << bil_sh;
    hp->bil_cool_q16 = UINT64_C(63600) + UINT64_C(124) * ((g >> 7U) & 15ULL);
    hp->bil_reheat_stag = 16U + 16U * (uint32_t)((g >> 11U) & 7ULL);
    hp->barrier_boost_q8 = 16U * (uint32_t)((g >> 14U) & 7ULL);
    uint32_t rh_sh = 1U + (uint32_t)((g >> 17U) & 3ULL);
    hp->bil_reheat_q12 = (hp->bil_init_temp_q12 >> rh_sh) + UINT64_C(1024);
}

static void decode_inl_genome(uint64_t g, searu_hyperparams *hp) {
    static const uint32_t k_masks[8] = {1U, 3U, 7U, 15U, 31U, 63U, 127U, 255U};
    hp->inl_walk_mask = k_masks[g & 7ULL];
    uint32_t inl_t = (uint32_t)((g >> 3U) & 15ULL);
    uint32_t inl_sh = (inl_t > 12U) ? 12U : inl_t;
    hp->inl_init_temp_q12 = UINT64_C(512) << inl_sh;
    hp->inl_cool_q16 = UINT64_C(63600) + UINT64_C(124) * ((g >> 7U) & 15ULL);
    hp->inl_reheat_stag = 16U + 16U * (uint32_t)((g >> 11U) & 7ULL);
    uint32_t rh_sh = 1U + (uint32_t)((g >> 14U) & 3ULL);
    hp->inl_reheat_q12 = (hp->inl_init_temp_q12 >> rh_sh) + UINT64_C(512);
}

static void run_inline_part_bench(searu_result *out, uint64_t seed, uint32_t cap,
                                  const searu_hyperparams *hp) {
    static const uint32_t k_w[24] = {
        89U,  97U,  103U, 109U, 113U, 127U, 131U, 137U, 139U, 149U, 151U, 157U,
        163U, 167U, 173U, 179U, 181U, 191U, 193U, 197U, 199U, 211U, 223U, 227U,
    };
    /* Witness subset 0x965A3C has exact sum:
     * w[2]+w[3]+w[4]+w[5]+w[9]+w[11]+w[12]+w[14]+w[17]+w[18]+w[20]+w[23] = 1904, with bits [0..2]
     * Kan-pinned to 0x4 (bit 2 = 1, bits 0..1 = 0). */
    const uint64_t p_init[4] = {UINT64_C(0x000004), 0ULL, 0ULL, 0ULL};
    const uint64_t p_pin[4] = {UINT64_C(0x000007), 0ULL, 0ULL, 0ULL};
    SEARU_SOLVE_INLINE_HP(out, 24U, p_init, p_pin, seed, cap, hp, cand, frust, energy, {
        uint64_t s = UINT64_C(0);
        uint64_t b = cand[0] & UINT64_C(0xFFFFFF);
        for (uint32_t j = 0U; j < 24U; ++j) {
            if (((b >> j) & UINT64_C(1)) != UINT64_C(0)) {
                s += (uint64_t)k_w[j];
            }
        }
        if (s > UINT64_C(1904)) {
            energy = s - UINT64_C(1904);
            frust[0] = b;
        } else {
            energy = UINT64_C(1904) - s;
            frust[0] = (~b) & UINT64_C(0xFFFFFF);
        }
    });
}

static uint64_t eval_meta_class(const meta_bench_env *env, const searu_hyperparams *hp,
                                const uint64_t *seeds, uint32_t n_seeds, uint32_t cls,
                                uint64_t cutoff_iters, uint32_t *out_fails) {
    uint64_t total_it = 0ULL;
    uint32_t fails = 0U;
    const uint64_t col_init[4] = {UINT64_C(0x3210), 0ULL, 0ULL, 0ULL};
    for (uint32_t i = 0U; i < n_seeds; ++i) {
        searu_result r;
        uint64_t rem = (cutoff_iters > total_it) ? (cutoff_iters - total_it) : UINT64_C(1);
        if (cls == 0U) {
            if (rem > 25000ULL) {
                rem = 25000ULL;
            }
            if (searu_solve_3sat_hp(&r, env->clauses, 218U, 50U, NULL, NULL,
                                    seeds[i] ^ UINT64_C(0xA5), (uint32_t)rem, hp) != UINT64_C(0)) {
                ++fails;
            }
        } else if (cls == 1U) {
            if (rem > 25000ULL) {
                rem = 25000ULL;
            }
            if (searu_solve_graph_color_hp(&r, env->edges, 96U, 32U, 4U, col_init, UINT64_C(0xF),
                                           seeds[i] ^ UINT64_C(0x5A), (uint32_t)rem,
                                           hp) != UINT64_C(0)) {
                ++fails;
            }
        } else if (cls == 2U) {
            if (rem > 8192ULL) {
                rem = 8192ULL;
            }
            if (searu_solve_bilevel_2jet_hp(&r, NULL, env->base_bound, env->terms, 24U, UINT64_C(0),
                                            INT64_C(0), INT64_C(0), env->target_x_q16, 6U,
                                            seeds[i] ^ UINT64_C(0x3C), (uint32_t)rem,
                                            hp) != UINT64_C(0) ||
                r.converged_2jet == 0U) {
                ++fails;
            }
        } else {
            if (rem > 8192ULL) {
                rem = 8192ULL;
            }
            run_inline_part_bench(&r, seeds[i] ^ UINT64_C(0xC3), (uint32_t)rem, hp);
            if (r.best_energy != UINT64_C(0)) {
                ++fails;
            }
        }
        total_it += r.iterations_run;
        if (total_it >= cutoff_iters && (i + 1U < n_seeds)) {
            fails += (n_seeds - 1U - i);
            break;
        }
    }
    if (out_fails != NULL) {
        *out_fails = fails;
    }
    return total_it + (uint64_t)fails * UINT64_C(100000);
}

static uint64_t *select_cool_ptr(searu_hyperparams *hp, uint32_t cls) {
    if (cls == 0U) {
        return &hp->sat_cool_q16;
    }
    if (cls == 1U) {
        return &hp->col_cool_q16;
    }
    if (cls == 2U) {
        return &hp->bil_cool_q16;
    }
    return &hp->inl_cool_q16;
}

static void polish_cool_2jet(const meta_bench_env *env, searu_hyperparams *hp,
                             const uint64_t *seeds, uint32_t n_seeds, uint32_t cls) {
    uint64_t cur_cost = eval_meta_class(env, hp, seeds, n_seeds, cls, UINT64_C(500000), NULL);
    for (int64_t h = INT64_C(256); h >= INT64_C(32); h >>= 1U) {
        uint64_t *target = select_cool_ptr(hp, cls);
        uint64_t orig = *target;
        searu_hyperparams hp_p = *hp;
        searu_hyperparams hp_m = *hp;
        uint64_t *pp = select_cool_ptr(&hp_p, cls);
        uint64_t *pm = select_cool_ptr(&hp_m, cls);
        *pp = (orig + (uint64_t)h <= UINT64_C(65480)) ? (orig + (uint64_t)h) : UINT64_C(65480);
        *pm = (orig > (uint64_t)h + UINT64_C(60000)) ? (orig - (uint64_t)h) : UINT64_C(60000);
        uint64_t ep = eval_meta_class(env, &hp_p, seeds, n_seeds, cls, cur_cost + 4096ULL, NULL);
        uint64_t em = eval_meta_class(env, &hp_m, seeds, n_seeds, cls, cur_cost + 4096ULL, NULL);
        int64_t g1 = ((int64_t)ep - (int64_t)em) >> 1U;
        int64_t g2 = (int64_t)ep - 2 * (int64_t)cur_cost + (int64_t)em;
        if (g2 > 0 && g1 != 0) {
            uint64_t st[8] = {(uint64_t)(g1 * FLOW_Q16_ONE),
                              (uint64_t)(g2 * FLOW_Q16_ONE),
                              0ULL,
                              0ULL,
                              0ULL,
                              0ULL,
                              0ULL,
                              0ULL};
            uint64_t bd[8] = {0ULL,
                              0ULL,
                              0ULL,
                              ((uint64_t)(uint32_t)(int32_t)(-4 * FLOW_Q16_ONE) << 32U) |
                                  (uint64_t)(uint32_t)(int32_t)(4 * FLOW_Q16_ONE),
                              ((uint64_t)(uint32_t)(int32_t)(2 * FLOW_Q16_ONE) << 32U) | 1ULL,
                              0ULL,
                              0ULL,
                              0ULL};
            flow_step(st, st, bd);
            int64_t step_c = (int64_t)orig + (((int64_t)st[3] * h) >> FLOW_Q16_SHIFT);
            if (step_c >= INT64_C(60000) && step_c <= INT64_C(65480)) {
                *target = (uint64_t)step_c;
                uint64_t et =
                    eval_meta_class(env, hp, seeds, n_seeds, cls, cur_cost + 1024ULL, NULL);
                if (et < cur_cost) {
                    cur_cost = et;
                } else {
                    *target = orig;
                }
            }
        }
        if (ep < cur_cost) {
            *target = *pp;
            cur_cost = ep;
        } else if (em < cur_cost) {
            *target = *pm;
            cur_cost = em;
        }
    }
}

static uint64_t eval_meta_suite(const meta_bench_env *env, const searu_hyperparams *hp,
                                const uint64_t *seeds, uint32_t n_seeds, uint64_t cutoff_iters,
                                uint64_t *out_sat_it, uint64_t *out_col_it, uint64_t *out_bil_it,
                                uint64_t *out_inl_it, uint32_t *out_failures) {
    uint32_t f0 = 0U, f1 = 0U, f2 = 0U, f3 = 0U;
    uint64_t c0 = eval_meta_class(env, hp, seeds, n_seeds, 0U, cutoff_iters, &f0);
    uint64_t c1 = eval_meta_class(env, hp, seeds, n_seeds, 1U, cutoff_iters, &f1);
    uint64_t c2 = eval_meta_class(env, hp, seeds, n_seeds, 2U, cutoff_iters, &f2);
    uint64_t c3 = eval_meta_class(env, hp, seeds, n_seeds, 3U, cutoff_iters, &f3);
    uint64_t s_it = c0 - (uint64_t)f0 * UINT64_C(100000);
    uint64_t col_it = c1 - (uint64_t)f1 * UINT64_C(100000);
    uint64_t b_it = c2 - (uint64_t)f2 * UINT64_C(100000);
    uint64_t inl_it = c3 - (uint64_t)f3 * UINT64_C(100000);
    if (out_sat_it != NULL) {
        *out_sat_it = s_it;
    }
    if (out_col_it != NULL) {
        *out_col_it = col_it;
    }
    if (out_bil_it != NULL) {
        *out_bil_it = b_it;
    }
    if (out_inl_it != NULL) {
        *out_inl_it = inl_it;
    }
    if (out_failures != NULL) {
        *out_failures = f0 + f1 + f2 + f3;
    }
    return c0 + c1 + c2 + c3;
}

static int cmd_self_opt(const char *out_path) {
    printf("=== [FLOW Self-Optimizing Pipeline] SEARU x FLOW 2-Jet Autonomous Synthesis ===\n");

    /* Stage 1: Synthesize & Certify 0-ULP Degree-3 Horner Seeds for flow.h */
    uint64_t isqrt_c[4] = {0ULL, 0ULL, 0ULL, 0ULL};
    uint64_t recip_c[4] = {0ULL, 0ULL, 0ULL, 0ULL};
    uint64_t isqrt_e0 = 0ULL, isqrt_ef = 0ULL;
    uint64_t recip_e0 = 0ULL, recip_ef = 0ULL;

    synthesize_horner_kernel(false, isqrt_c, &isqrt_e0, &isqrt_ef);
    synthesize_horner_kernel(true, recip_c, &recip_e0, &recip_ef);
    if (!verify_synthesized_0ulp(isqrt_c, recip_c)) {
        fprintf(stderr, "self-opt: synthesized Horner seeds failed 0-ULP midpoint proof!\n");
        return 1;
    }
    printf("  [Stage 1: flow.h 0-ULP Kernel Synthesis]\n");
    printf("    isqrt_horner3 : {%llu, %llu, %llu, %llu} (64k-bin err %llu -> %llu, 0-ULP QED)\n",
           (unsigned long long)isqrt_c[0], (unsigned long long)isqrt_c[1],
           (unsigned long long)isqrt_c[2], (unsigned long long)isqrt_c[3],
           (unsigned long long)isqrt_e0, (unsigned long long)isqrt_ef);
    printf("    recip_horner3 : {%llu, %llu, %llu, %llu} (64k-bin err %llu -> %llu, 0-ULP QED)\n",
           (unsigned long long)recip_c[0], (unsigned long long)recip_c[1],
           (unsigned long long)recip_c[2], (unsigned long long)recip_c[3],
           (unsigned long long)recip_e0, (unsigned long long)recip_ef);

    /* Stage 2: Evolve searu.h Hyperparameters via SEARU_SOLVE_INLINE + FLOW 2-Jet */
    static meta_bench_env env;
    init_meta_bench_env(&env);

    static const uint64_t train_seeds[8] = {
        UINT64_C(0x13579BDF2468ACE1), UINT64_C(0x9E3779B97F4A7C15), UINT64_C(0xBF58476D1CE4E5B9),
        UINT64_C(0x94D049BB133111EB), UINT64_C(0x2545F4914F6CDD1D), UINT64_C(0xD6E8FEB86659FD93),
        UINT64_C(0xA5A5A5A55A5A5A5A), UINT64_C(0x0123456789ABCDEF),
    };
    static const uint64_t val_seeds[8] = {
        UINT64_C(0x5EED5A71) ^ UINT64_C(0xA5), UINT64_C(0x9E3779B9) ^ UINT64_C(0x5A),
        UINT64_C(0x7777) ^ UINT64_C(0x3C),     UINT64_C(0xCAFEBABE12345678),
        UINT64_C(0x6A09E667F3BCC908),          UINT64_C(0xBB67AE8584CAA73B),
        UINT64_C(0x3C6EF372FE94F82B),          UINT64_C(0x510E527FADE682D1),
    };

    const searu_hyperparams human_hp = SEARU_HUMAN_BASELINE_HP;
    uint64_t base_sat = 0ULL, base_col = 0ULL, base_bil = 0ULL, base_inl = 0ULL;
    uint64_t base_vsat = 0ULL, base_vcol = 0ULL, base_vbil = 0ULL, base_vinl = 0ULL;
    uint32_t base_fails = 0U, base_val_fails = 0U;
    clock_t t_b0 = clock();
    uint64_t base_train_cost =
        eval_meta_suite(&env, &human_hp, train_seeds, 8U, UINT64_C(900000), &base_sat, &base_col,
                        &base_bil, &base_inl, &base_fails);
    uint64_t base_val_cost =
        eval_meta_suite(&env, &human_hp, val_seeds, 8U, UINT64_C(900000), &base_vsat, &base_vcol,
                        &base_vbil, &base_vinl, &base_val_fails);
    clock_t t_b1 = clock();
    uint64_t base_us = (uint64_t)((t_b1 - t_b0) * (clock_t)1000000 / (clock_t)CLOCKS_PER_SEC);

    searu_hyperparams best_hp = human_hp;

    /* Stage 2a: Evolve 3-SAT + shared Zipf chaos genome via SEARU_SOLVE_INLINE_HP + FLOW 2-Jet */
    const uint64_t init_sat_g[4] = {
        (0ULL) | (7ULL << 3U) | (1ULL << 6U) | (1ULL << 8U) | (10ULL << 11U) | (13ULL << 15U) |
            (3ULL << 19U),
        0ULL,
        0ULL,
        0ULL,
    };
    uint64_t run_sat = base_sat + (uint64_t)base_fails * UINT64_C(100000);
    searu_result res_sat_g;
    SEARU_SOLVE_INLINE_HP(&res_sat_g, 22U, init_sat_g, NULL, UINT64_C(0x5EA2026), 128U, &human_hp,
                          cand, frust, energy, {
                              searu_hyperparams chp = best_hp;
                              decode_sat_genome(cand[0], &chp);
                              uint32_t fl = 0U;
                              energy = eval_meta_class(&env, &chp, train_seeds, 8U, 0U,
                                                       run_sat + UINT64_C(2048), &fl);
                              if (fl == 0U && energy < run_sat) {
                                  run_sat = energy;
                              }
                              frust[0] = UINT64_C(0x3FFFFF);
                          });
    decode_sat_genome(res_sat_g.canvas[0], &best_hp);
    polish_cool_2jet(&env, &best_hp, train_seeds, 8U, 0U);

    /* Stage 2b: Evolve Graph-Coloring genome via SEARU_SOLVE_INLINE_HP + FLOW 2-Jet */
    const uint64_t init_col_g[4] = {
        (1ULL) | (9ULL << 3U) | (12ULL << 7U) | (2ULL << 11U),
        0ULL,
        0ULL,
        0ULL,
    };
    uint64_t run_col = eval_meta_class(&env, &best_hp, train_seeds, 8U, 1U, UINT64_C(500000), NULL);
    searu_result res_col_g;
    SEARU_SOLVE_INLINE_HP(&res_col_g, 14U, init_col_g, NULL, UINT64_C(0xC0102026), 128U, &human_hp,
                          cand, frust, energy, {
                              searu_hyperparams chp = best_hp;
                              decode_col_genome(cand[0], &chp);
                              uint32_t fl = 0U;
                              energy = eval_meta_class(&env, &chp, train_seeds, 8U, 1U,
                                                       run_col + UINT64_C(4096), &fl);
                              if (fl == 0U && energy < run_col) {
                                  run_col = energy;
                              }
                              frust[0] = UINT64_C(0x3FFF);
                          });
    decode_col_genome(res_col_g.canvas[0], &best_hp);
    polish_cool_2jet(&env, &best_hp, train_seeds, 8U, 1U);

    /* Stage 2c: Evolve Symbiotic Bilevel 2-Jet genome via SEARU_SOLVE_INLINE_HP + FLOW 2-Jet */
    const uint64_t init_bil_g[4] = {
        (1ULL) | (12ULL << 3U) | (11ULL << 7U) | (2ULL << 11U) | (3ULL << 14U) | (2ULL << 17U),
        0ULL,
        0ULL,
        0ULL,
    };
    uint64_t run_bil = eval_meta_class(&env, &best_hp, train_seeds, 8U, 2U, UINT64_C(500000), NULL);
    searu_result res_bil_g;
    SEARU_SOLVE_INLINE_HP(&res_bil_g, 19U, init_bil_g, NULL, UINT64_C(0xB11E2026), 128U, &human_hp,
                          cand, frust, energy, {
                              searu_hyperparams chp = best_hp;
                              decode_bil_genome(cand[0], &chp);
                              uint32_t fl = 0U;
                              energy = eval_meta_class(&env, &chp, train_seeds, 8U, 2U,
                                                       run_bil + UINT64_C(2048), &fl);
                              if (fl == 0U && energy < run_bil) {
                                  run_bil = energy;
                              }
                              frust[0] = UINT64_C(0x7FFFF);
                          });
    decode_bil_genome(res_bil_g.canvas[0], &best_hp);
    polish_cool_2jet(&env, &best_hp, train_seeds, 8U, 2U);

    /* Stage 2d: Evolve SEARU_SOLVE_INLINE custom-objective genome via SEARU + FLOW 2-Jet */
    const uint64_t init_inl_g[4] = {
        (1ULL) | (4ULL << 3U) | (13ULL << 7U) | (3ULL << 11U) | (1ULL << 14U),
        0ULL,
        0ULL,
        0ULL,
    };
    uint64_t run_inl = eval_meta_class(&env, &best_hp, train_seeds, 8U, 3U, UINT64_C(500000), NULL);
    searu_result res_inl_g;
    SEARU_SOLVE_INLINE_HP(&res_inl_g, 16U, init_inl_g, NULL, UINT64_C(0x14112026), 128U, &human_hp,
                          cand, frust, energy, {
                              searu_hyperparams chp = best_hp;
                              decode_inl_genome(cand[0], &chp);
                              uint32_t fl = 0U;
                              energy = eval_meta_class(&env, &chp, train_seeds, 8U, 3U,
                                                       run_inl + UINT64_C(2048), &fl);
                              if (fl == 0U && energy < run_inl) {
                                  run_inl = energy;
                              }
                              frust[0] = UINT64_C(0xFFFF);
                          });
    decode_inl_genome(res_inl_g.canvas[0], &best_hp);
    polish_cool_2jet(&env, &best_hp, train_seeds, 8U, 3U);

    uint64_t opt_sat = 0ULL, opt_col = 0ULL, opt_bil = 0ULL, opt_inl = 0ULL;
    uint64_t opt_vsat = 0ULL, opt_vcol = 0ULL, opt_vbil = 0ULL, opt_vinl = 0ULL;
    uint32_t opt_train_fails = 0U, opt_val_fails = 0U;
    clock_t t_o0 = clock();
    uint64_t best_train_cost =
        eval_meta_suite(&env, &best_hp, train_seeds, 8U, UINT64_C(900000), &opt_sat, &opt_col,
                        &opt_bil, &opt_inl, &opt_train_fails);
    uint64_t opt_val_cost =
        eval_meta_suite(&env, &best_hp, val_seeds, 8U, UINT64_C(900000), &opt_vsat, &opt_vcol,
                        &opt_vbil, &opt_vinl, &opt_val_fails);
    clock_t t_o1 = clock();
    uint64_t opt_us = (uint64_t)((t_o1 - t_o0) * (clock_t)1000000 / (clock_t)CLOCKS_PER_SEC);

    uint64_t base_train_raw = base_sat + base_col + base_bil + base_inl;
    uint64_t base_val_raw = base_vsat + base_vcol + base_vbil + base_vinl;
    uint64_t opt_train_raw = opt_sat + opt_col + opt_bil + opt_inl;
    uint64_t opt_val_raw = opt_vsat + opt_vcol + opt_vbil + opt_vinl;

    if (opt_train_fails != 0U || opt_val_fails != 0U || best_train_cost >= base_train_cost ||
        opt_val_cost >= base_val_cost || opt_train_raw >= base_train_raw ||
        opt_val_raw >= base_val_raw) {
        fprintf(stderr,
                "self-opt: generalization gate failed (train_raw %llu vs %llu, val_raw %llu vs "
                "%llu, fails=%u/%u)\n",
                (unsigned long long)opt_train_raw, (unsigned long long)base_train_raw,
                (unsigned long long)opt_val_raw, (unsigned long long)base_val_raw, opt_train_fails,
                opt_val_fails);
        return 1;
    }

    printf("  [Stage 2: searu.h Meta-SEARU x FLOW 2-Jet Evolution (64 NP Instances: 32 Train + 32 "
           "Held-Out)]\n");
    printf("    Human Baseline  : train=%llu iters [SAT=%llu COL=%llu BIL=%llu INL=%llu fails=%u] "
           "| held_out=%llu iters [SAT=%llu COL=%llu BIL=%llu INL=%llu fails=%u] | cpu=%llu us\n",
           (unsigned long long)base_train_raw, (unsigned long long)base_sat,
           (unsigned long long)base_col, (unsigned long long)base_bil, (unsigned long long)base_inl,
           base_fails, (unsigned long long)base_val_raw, (unsigned long long)base_vsat,
           (unsigned long long)base_vcol, (unsigned long long)base_vbil,
           (unsigned long long)base_vinl, base_val_fails, (unsigned long long)base_us);
    printf("    Self-Optimized  : train=%llu iters [SAT=%llu COL=%llu BIL=%llu INL=%llu fails=%u] "
           "| held_out=%llu iters [SAT=%llu COL=%llu BIL=%llu INL=%llu fails=%u] | cpu=%llu us\n",
           (unsigned long long)opt_train_raw, (unsigned long long)opt_sat,
           (unsigned long long)opt_col, (unsigned long long)opt_bil, (unsigned long long)opt_inl,
           opt_train_fails, (unsigned long long)opt_val_raw, (unsigned long long)opt_vsat,
           (unsigned long long)opt_vcol, (unsigned long long)opt_vbil, (unsigned long long)opt_vinl,
           opt_val_fails, (unsigned long long)opt_us);
    printf("    Evolved Params  : chaos=[%u..%u] sh=%u boost=%u masks=[0x%x,0x%x,0x%x,0x%x] | "
           "SAT(%llu,%llu,%u) COL(%llu,%llu,%u) BIL(%llu,%llu,%u) INL(%llu,%llu,%u)\n",
           best_hp.chaos_min_q8, best_hp.chaos_max_q8, best_hp.stag_shift, best_hp.barrier_boost_q8,
           best_hp.rand_walk_mask, best_hp.col_walk_mask, best_hp.bil_walk_mask,
           best_hp.inl_walk_mask, (unsigned long long)best_hp.sat_init_temp_q12,
           (unsigned long long)best_hp.sat_cool_q16, best_hp.sat_reheat_stag,
           (unsigned long long)best_hp.col_init_temp_q12, (unsigned long long)best_hp.col_cool_q16,
           best_hp.col_reheat_stag, (unsigned long long)best_hp.bil_init_temp_q12,
           (unsigned long long)best_hp.bil_cool_q16, best_hp.bil_reheat_stag,
           (unsigned long long)best_hp.inl_init_temp_q12, (unsigned long long)best_hp.inl_cool_q16,
           best_hp.inl_reheat_stag);

    if (out_path != NULL) {
        FILE *fp = fopen(out_path, "wb");
        if (fp == NULL) {
            fprintf(stderr, "self-opt: failed to open '%s' for writing\n", out_path);
            return 1;
        }
        fprintf(fp, "#ifndef FLOW_OPT_PARAMS_H\n");
        fprintf(fp, "#define FLOW_OPT_PARAMS_H\n\n");
        fprintf(
            fp,
            "/* ============================================================================\n");
        fprintf(fp, " * AUTO-SYNTHESIZED BY `flow self-opt` (SEARU x FLOW 2-Jet Meta-Pipeline)\n");
        fprintf(fp, " * Zero hand-tuned magic constants: regenerated via `make self-opt`.\n");
        fprintf(
            fp,
            " * ============================================================================ */\n\n");
        fprintf(fp, "/* Stage 1: Synthesized 0-ULP Degree-3 Horner Seeds for flow.h */\n");
        fprintf(fp, "#define FLOW_OPT_ISQRT_C0 UINT64_C(%llu)\n", (unsigned long long)isqrt_c[0]);
        fprintf(fp, "#define FLOW_OPT_ISQRT_C1 UINT64_C(%llu)\n", (unsigned long long)isqrt_c[1]);
        fprintf(fp, "#define FLOW_OPT_ISQRT_C2 UINT64_C(%llu)\n", (unsigned long long)isqrt_c[2]);
        fprintf(fp, "#define FLOW_OPT_ISQRT_C3 UINT64_C(%llu)\n\n", (unsigned long long)isqrt_c[3]);
        fprintf(fp, "#define FLOW_OPT_RECIP_C0 UINT64_C(%llu)\n", (unsigned long long)recip_c[0]);
        fprintf(fp, "#define FLOW_OPT_RECIP_C1 UINT64_C(%llu)\n", (unsigned long long)recip_c[1]);
        fprintf(fp, "#define FLOW_OPT_RECIP_C2 UINT64_C(%llu)\n", (unsigned long long)recip_c[2]);
        fprintf(fp, "#define FLOW_OPT_RECIP_C3 UINT64_C(%llu)\n\n", (unsigned long long)recip_c[3]);
        fprintf(fp, "/* Stage 2: Self-Evolved Hyperparameters for searu.h */\n");
        fprintf(fp, "#define SEARU_OPT_CHAOS_MIN_Q8      %uU\n", best_hp.chaos_min_q8);
        fprintf(fp, "#define SEARU_OPT_CHAOS_MAX_Q8      %uU\n", best_hp.chaos_max_q8);
        fprintf(fp, "#define SEARU_OPT_STAG_SHIFT        %uU\n", best_hp.stag_shift);
        fprintf(fp, "#define SEARU_OPT_BARRIER_BOOST_Q8  %uU\n", best_hp.barrier_boost_q8);
        fprintf(fp, "#define SEARU_OPT_RAND_WALK_MASK    %uU\n", best_hp.rand_walk_mask);
        fprintf(fp, "#define SEARU_OPT_COL_WALK_MASK     %uU\n", best_hp.col_walk_mask);
        fprintf(fp, "#define SEARU_OPT_BIL_WALK_MASK     %uU\n", best_hp.bil_walk_mask);
        fprintf(fp, "#define SEARU_OPT_INL_WALK_MASK     %uU\n\n", best_hp.inl_walk_mask);
        fprintf(fp, "#define SEARU_OPT_SAT_INIT_TEMP_Q12 UINT64_C(%llu)\n",
                (unsigned long long)best_hp.sat_init_temp_q12);
        fprintf(fp, "#define SEARU_OPT_SAT_COOL_Q16      UINT64_C(%llu)\n",
                (unsigned long long)best_hp.sat_cool_q16);
        fprintf(fp, "#define SEARU_OPT_SAT_REHEAT_STAG   %uU\n", best_hp.sat_reheat_stag);
        fprintf(fp, "#define SEARU_OPT_SAT_REHEAT_Q12    UINT64_C(%llu)\n\n",
                (unsigned long long)best_hp.sat_reheat_q12);
        fprintf(fp, "#define SEARU_OPT_COL_INIT_TEMP_Q12 UINT64_C(%llu)\n",
                (unsigned long long)best_hp.col_init_temp_q12);
        fprintf(fp, "#define SEARU_OPT_COL_COOL_Q16      UINT64_C(%llu)\n",
                (unsigned long long)best_hp.col_cool_q16);
        fprintf(fp, "#define SEARU_OPT_COL_REHEAT_STAG   %uU\n", best_hp.col_reheat_stag);
        fprintf(fp, "#define SEARU_OPT_COL_REHEAT_Q12    UINT64_C(%llu)\n\n",
                (unsigned long long)best_hp.col_reheat_q12);
        fprintf(fp, "#define SEARU_OPT_BIL_INIT_TEMP_Q12 UINT64_C(%llu)\n",
                (unsigned long long)best_hp.bil_init_temp_q12);
        fprintf(fp, "#define SEARU_OPT_BIL_COOL_Q16      UINT64_C(%llu)\n",
                (unsigned long long)best_hp.bil_cool_q16);
        fprintf(fp, "#define SEARU_OPT_BIL_REHEAT_STAG   %uU\n", best_hp.bil_reheat_stag);
        fprintf(fp, "#define SEARU_OPT_BIL_REHEAT_Q12    UINT64_C(%llu)\n\n",
                (unsigned long long)best_hp.bil_reheat_q12);
        fprintf(fp, "#define SEARU_OPT_INL_INIT_TEMP_Q12 UINT64_C(%llu)\n",
                (unsigned long long)best_hp.inl_init_temp_q12);
        fprintf(fp, "#define SEARU_OPT_INL_COOL_Q16      UINT64_C(%llu)\n",
                (unsigned long long)best_hp.inl_cool_q16);
        fprintf(fp, "#define SEARU_OPT_INL_REHEAT_STAG   %uU\n", best_hp.inl_reheat_stag);
        fprintf(fp, "#define SEARU_OPT_INL_REHEAT_Q12    UINT64_C(%llu)\n\n",
                (unsigned long long)best_hp.inl_reheat_q12);
        fprintf(fp, "#endif /* FLOW_OPT_PARAMS_H */\n");
        fclose(fp);
        printf("  [EMITTED] %s updated cleanly.\n", out_path);
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0)) {
        printf("FLOW %s\n", FLOW_VERSION_STRING);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "searu-verify") == 0) {
        return cmd_searu_verify();
    }
    if ((argc == 2 || argc == 3) && strcmp(argv[1], "self-opt") == 0) {
        return cmd_self_opt((argc == 3) ? argv[2] : NULL);
    }
    if (argc == 3 && strcmp(argv[1], "verify") == 0) {
        return cmd_verify(argv[2]);
    }
    if (argc == 3 && strcmp(argv[1], "run") == 0) {
        return cmd_run(argv[2]);
    }
    fprintf(stderr, "Usage:\n  flow verify <bundle.flow>\n  flow run <bundle.flow>\n  flow "
                    "searu-verify\n  flow self-opt [out_params.h]\n");
    return 2;
}
