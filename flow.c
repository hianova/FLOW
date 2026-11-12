#include "flow.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static flow_cell g_arena[FLOW_ATLAS_MAX_CHARTS];

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
        uint64_t limit = neg ? ((uint64_t)INT64_MAX + 1ULL) : (uint64_t)INT64_MAX;
        uint64_t mag = 0ULL;
        if (!parse_u64_token(p, limit, &mag)) {
            return false;
        }
        *out = neg ? (int64_t)(UINT64_C(0) - mag) : (int64_t)mag;
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
    uint64_t max_whole = ((uint64_t)INT64_MAX) >> FLOW_Q16_SHIFT;
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
    uint64_t limit = neg ? ((uint64_t)INT64_MAX + 1ULL) : (uint64_t)INT64_MAX;
    if (mag_q16 > limit) {
        return false;
    }
    *out = neg ? (int64_t)(UINT64_C(0) - mag_q16) : (int64_t)mag_q16;
    return true;
}

static bool verify_chart_against_atlas(flow_cell b, uint32_t dom_expected, size_t chart_count) {
    if (!flow_verify_bound(b)) {
        return false;
    }
    uint32_t dom = (uint32_t)(b[5] >> 32U);
    int64_t b_exit = (int64_t)(int32_t)(uint32_t)(b[3] >> 32U);
    int64_t b_enter = (int64_t)(int32_t)(uint32_t)(b[3] & UINT64_C(0xFFFFFFFF));
    int64_t dt_max = (int64_t)(int32_t)(uint32_t)(b[4] >> 32U);
    uint16_t next_lo = (uint16_t)((b[5] >> 24U) & UINT64_C(0xFF));
    uint16_t next_hi = (uint16_t)((b[5] >> 16U) & UINT64_C(0xFF));
    uint32_t req = (uint32_t)(b[6] & UINT64_C(0xFFFFFFFF));

    return (dom == dom_expected) && (dt_max > 0) && (b_exit <= b_enter) &&
           ((size_t)next_lo < chart_count) && ((size_t)next_hi < chart_count) &&
           ((req & FLOW_SIEVE_BASE_GATE) == FLOW_SIEVE_BASE_GATE);
}

static bool load_and_verify_bundle(const char *path, flow_cell *out_state, size_t *out_count) {
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
    flow_cell st = flow_cell_from_bytes(raw);
    if (!flow_verify_state(st)) {
        fprintf(stderr, "flow: '%s' State cell failed FLOW_STATE_SEED MAC verification\n", path);
        fclose(fp);
        return false;
    }

    uint32_t dom = (uint32_t)(st[5] >> 32U);
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
        g_arena[n] = flow_cell_from_bytes(raw);
        ++n;
    }
    fclose(fp);

    if (n == 0U || (n & (n - 1U)) != 0U) {
        fprintf(stderr, "flow: '%s' chart count (%zu) is not a power of two\n", path, n);
        return false;
    }
    uint16_t init_chart = (uint16_t)((st[5] >> 16U) & UINT64_C(0xFFFF));
    if ((size_t)init_chart >= n) {
        fprintf(stderr, "flow: '%s' initial chart (%u) out of range for %zu charts\n", path,
                (unsigned)init_chart, n);
        return false;
    }

    for (size_t i = 0U; i < n; ++i) {
        if (!verify_chart_against_atlas(g_arena[i], dom, n)) {
            fprintf(stderr, "flow: '%s' Chart[%zu] failed seal, domain, or Atlas invariant check\n",
                    path, i);
            return false;
        }
    }

    *out_state = st;
    *out_count = n;
    return true;
}

static bool load_single_chart(const char *path, flow_cell *out_bound) {
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
    *out_bound = flow_cell_from_bytes(raw);
    return true;
}

static int cmd_verify(const char *bundle_path) {
    flow_cell st;
    size_t count = 0U;
    if (!load_and_verify_bundle(bundle_path, &st, &count)) {
        return 1;
    }
    uint32_t dom = (uint32_t)(st[5] >> 32U);
    uint16_t init_chart = (uint16_t)((st[5] >> 16U) & UINT64_C(0xFFFF));
    uint16_t init_mode = (uint16_t)(st[5] & UINT64_C(0xFFFF));
    char x_buf[32];
    fmt_q16((int64_t)st[3], x_buf);

    printf("[OK] %s: domain=0x%08x charts=%zu init_chart=%u init_x=%s init_mode=0x%04x\n",
           bundle_path, (unsigned)dom, count, (unsigned)init_chart, x_buf, (unsigned)init_mode);
    for (size_t i = 0U; i < count; ++i) {
        flow_cell b = g_arena[i];
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
    flow_cell state;
    size_t count = 0U;
    if (!load_and_verify_bundle(bundle_path, &state, &count)) {
        return 1;
    }

    uint64_t cap_mask = (uint64_t)(count - 1U);
    uint32_t dom = (uint32_t)(state[5] >> 32U);
    for (size_t i = 0U; i < count; ++i) {
        uint32_t ep = (uint32_t)(g_arena[i][6] >> 32U);
        flow_arena_publish_bound(g_arena, (uint64_t)i, cap_mask, ep, g_arena[i]);
    }

    char line[1024];
    uint64_t step_idx = 0ULL;
    while (fgets(line, (int)sizeof(line), stdin) != NULL) {
        char *p = line;
        while (*p != '\0' && isspace((unsigned char)*p)) {
            ++p;
        }
        if (*p == '\0' || *p == '#') {
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
            uint64_t slot = 0ULL;
            uint64_t ep = 0ULL;
            flow_cell new_b;
            bool swapped = (slot_s != NULL && ep_s != NULL && cell_path != NULL &&
                            parse_u64_token(slot_s, (uint64_t)(count - 1U), &slot) &&
                            parse_u64_token(ep_s, UINT64_C(0xFFFFFFFF), &ep) &&
                            load_single_chart(cell_path, &new_b) &&
                            flow_arena_try_swap(g_arena, slot, cap_mask, dom, (uint32_t)ep, new_b));
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
            fprintf(stderr, "flow: invalid Q48.16 input token '%s'\n", tok1);
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
        }

        state = flow_step_atlas(state, g_arena, cap_mask, u_ext, mode_req);
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
        uint32_t sv_now = (uint32_t)(state[6] & UINT64_C(0xFFFFFFFF));

        printf("step=%llu chart=%u x=%s dt=%s r0=%s mode=0x%04x epoch=%u sieve=0x%02x\n",
               (unsigned long long)step_idx, (unsigned)chart_now, x_s, dt_s, r0_s,
               (unsigned)mode_now, (unsigned)ep_now, (unsigned)sv_now);
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0)) {
        printf("FLOW %s\n", FLOW_VERSION_STRING);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "verify") == 0) {
        return cmd_verify(argv[2]);
    }
    if (argc == 3 && strcmp(argv[1], "run") == 0) {
        return cmd_run(argv[2]);
    }
    fprintf(stderr, "Usage:\n  flow verify <bundle.flow>\n  flow run <bundle.flow>\n");
    return 2;
}
