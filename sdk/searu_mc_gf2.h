#ifndef SEARU_MC_GF2_H
#define SEARU_MC_GF2_H

#include "searu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * SEARU_MC_GF2 — Standalone GF(2) ANF-RREF & Multiplicative-Complexity Solver
 * ============================================================================
 * Split out of core/searu.h into sdk/ so Ring-0 kernel consumers remain free
 * of circuit-synthesis workspace structs and pass infrastructure.
 * ============================================================================ */

#define SEARU_MC_MAX_IN    8U
#define SEARU_MC_MAX_OUT   16U
#define SEARU_MC_MAX_AND   16U
#define SEARU_MC_MAX_BASIS (1U + SEARU_MC_MAX_IN + SEARU_MC_MAX_AND)

typedef struct {
    uint32_t n_in;
    uint32_t n_in_port0;
    uint32_t n_out;
    uint64_t target_tt[SEARU_MC_MAX_OUT][4];
    uint32_t max_and;
    uint32_t max_depth;
    uint32_t budget_scale;
    uint64_t seed;
} searu_mc_spec;

typedef struct {
    uint32_t num_and;
    uint32_t mult_depth;
    uint32_t iterations_run;
    uint32_t mode_used;
    uint32_t and_lhs_mask[SEARU_MC_MAX_AND];
    uint32_t and_rhs_mask[SEARU_MC_MAX_AND];
    uint32_t out_mask[SEARU_MC_MAX_OUT];
    bool exact;
} searu_mc_plan;

typedef struct {
    uint64_t x_tt[SEARU_MC_MAX_IN][4];
    uint64_t basis_tt[SEARU_MC_MAX_BASIS][4];
    uint64_t basis_anf[SEARU_MC_MAX_BASIS][4];
    uint64_t target_anf[SEARU_MC_MAX_OUT][4];
    uint64_t ech_vec[SEARU_MC_MAX_BASIS][4];
    uint32_t ech_lead[SEARU_MC_MAX_BASIS];
    uint32_t ech_comb[SEARU_MC_MAX_BASIS];
    uint64_t gray_comb[1024];
} searu_mc_ws;

#ifdef SEARU_EXPORT_MC_GF2
bool searu_solve_mc_gf2(searu_mc_plan *out, const searu_mc_spec *spec, searu_mc_ws *ws);
#define SEARU_MC_API
#else
#define SEARU_MC_API static inline
#endif

/* --- Layer 6: Pure-GPR F2 ANF-RREF & Bilinear Tensor Multiplicative-Complexity Synthesizer --- */

FLOW_FORCE_INL void searu__mc_active_mask(uint64_t mask[4], uint32_t n_in, uint32_t *out_nwords) {
    mask[0] = UINT64_C(0);
    mask[1] = UINT64_C(0);
    mask[2] = UINT64_C(0);
    mask[3] = UINT64_C(0);
    if (n_in < 6U) {
        uint32_t ns = UINT32_C(1) << n_in;
        mask[0] = (UINT64_C(1) << ns) - UINT64_C(1);
        *out_nwords = 1U;
    } else if (n_in == 6U) {
        mask[0] = UINT64_MAX;
        *out_nwords = 1U;
    } else if (n_in == 7U) {
        mask[0] = UINT64_MAX;
        mask[1] = UINT64_MAX;
        *out_nwords = 2U;
    } else {
        mask[0] = UINT64_MAX;
        mask[1] = UINT64_MAX;
        mask[2] = UINT64_MAX;
        mask[3] = UINT64_MAX;
        *out_nwords = 4U;
    }
}

FLOW_FORCE_INL void searu__mc_mobius256(uint64_t dst[4], const uint64_t src[4], uint32_t n_in,
                                        uint32_t n_words, const uint64_t mask[4]) {
    for (uint32_t w = 0U; w < 4U; ++w) {
        dst[w] = (w < n_words) ? (src[w] & mask[w]) : UINT64_C(0);
    }
    for (uint32_t w = 0U; w < n_words; ++w) {
        uint64_t v = dst[w];
        v ^= (v & UINT64_C(0x5555555555555555)) << 1U;
        if (n_in >= 2U) {
            v ^= (v & UINT64_C(0x3333333333333333)) << 2U;
        }
        if (n_in >= 3U) {
            v ^= (v & UINT64_C(0x0F0F0F0F0F0F0F0F)) << 4U;
        }
        if (n_in >= 4U) {
            v ^= (v & UINT64_C(0x00FF00FF00FF00FF)) << 8U;
        }
        if (n_in >= 5U) {
            v ^= (v & UINT64_C(0x0000FFFF0000FFFF)) << 16U;
        }
        if (n_in >= 6U) {
            v ^= (v & UINT64_C(0x00000000FFFFFFFF)) << 32U;
        }
        dst[w] = v;
    }
    if (n_in >= 7U) {
        dst[1] ^= dst[0];
        if (n_in >= 8U) {
            dst[3] ^= dst[2];
            dst[2] ^= dst[0];
            dst[3] ^= dst[1];
        }
    }
    for (uint32_t w = 0U; w < n_words; ++w) {
        dst[w] &= mask[w];
    }
}

FLOW_FORCE_INL uint32_t searu__mc_first_bit256(const uint64_t v[4], uint32_t n_words) {
    for (uint32_t w = 0U; w < n_words; ++w) {
        if (v[w] != UINT64_C(0)) {
            return (w << 6U) | searu_ctz64(v[w]);
        }
    }
    return 256U;
}

FLOW_FORCE_INL uint32_t searu__mc_anf_rref(const searu_mc_spec *spec, searu_mc_ws *ws,
                                           uint32_t n_basis, uint32_t n_words,
                                           uint32_t out_mask[SEARU_MC_MAX_OUT],
                                           uint32_t *out_dep_mask, uint32_t *out_uncov) {
    uint32_t n_ech = 0U;
    uint32_t dep_m = 0U;
    for (uint32_t idx = 0U; idx < n_basis; ++idx) {
        uint64_t cur[4];
        for (uint32_t w = 0U; w < 4U; ++w) {
            cur[w] = (w < n_words) ? ws->basis_anf[idx][w] : UINT64_C(0);
        }
        uint32_t comb = UINT32_C(1) << idx;
        for (uint32_t q = 0U; q < n_ech; ++q) {
            uint32_t lb = ws->ech_lead[q];
            if (((cur[lb >> 6U] >> (lb & 63U)) & UINT64_C(1)) != UINT64_C(0)) {
                for (uint32_t w = 0U; w < n_words; ++w) {
                    cur[w] ^= ws->ech_vec[q][w];
                }
                comb ^= ws->ech_comb[q];
            }
        }
        uint32_t lead = searu__mc_first_bit256(cur, n_words);
        if (lead >= 256U) {
            dep_m |= (UINT32_C(1) << idx);
        } else {
            uint32_t lw = lead >> 6U;
            uint32_t lb = lead & 63U;
            for (uint32_t q = 0U; q < n_ech; ++q) {
                if (((ws->ech_vec[q][lw] >> lb) & UINT64_C(1)) != UINT64_C(0)) {
                    for (uint32_t w = 0U; w < n_words; ++w) {
                        ws->ech_vec[q][w] ^= cur[w];
                    }
                    ws->ech_comb[q] ^= comb;
                }
            }
            for (uint32_t w = 0U; w < 4U; ++w) {
                ws->ech_vec[n_ech][w] = cur[w];
            }
            ws->ech_lead[n_ech] = lead;
            ws->ech_comb[n_ech] = comb;
            ++n_ech;
        }
    }

    uint32_t err = 0U;
    uint64_t uncov_v[4] = {UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0)};
    for (uint32_t o = 0U; o < spec->n_out; ++o) {
        uint64_t rem[4];
        for (uint32_t w = 0U; w < 4U; ++w) {
            rem[w] = (w < n_words) ? ws->target_anf[o][w] : UINT64_C(0);
        }
        uint32_t om = 0U;
        for (uint32_t q = 0U; q < n_ech; ++q) {
            uint32_t lb = ws->ech_lead[q];
            if (((rem[lb >> 6U] >> (lb & 63U)) & UINT64_C(1)) != UINT64_C(0)) {
                for (uint32_t w = 0U; w < n_words; ++w) {
                    rem[w] ^= ws->ech_vec[q][w];
                }
                om ^= ws->ech_comb[q];
            }
        }
        out_mask[o] = om;
        for (uint32_t w = 0U; w < n_words; ++w) {
            err += searu_popcnt64(rem[w]);
            uncov_v[w] |= rem[w];
        }
    }
    uint32_t uncov = 0U;
    for (uint32_t w = 0U; w < n_words; ++w) {
        uncov += searu_popcnt64(uncov_v[w]);
    }
    if (out_dep_mask != NULL) {
        *out_dep_mask = dep_m;
    }
    if (out_uncov != NULL) {
        *out_uncov = uncov;
    }
    return err;
}

FLOW_FORCE_INL bool searu__mc_verify_plan(const searu_mc_spec *spec, searu_mc_ws *ws,
                                          const searu_mc_plan *plan, uint32_t n_words,
                                          const uint64_t active_mask[4], uint32_t *out_depth) {
    uint32_t depths[SEARU_MC_MAX_BASIS];
    for (uint32_t i = 0U; i < 1U + spec->n_in; ++i) {
        depths[i] = 0U;
    }
    for (uint32_t w = 0U; w < n_words; ++w) {
        ws->basis_tt[0][w] = active_mask[w];
    }
    for (uint32_t i = 0U; i < spec->n_in; ++i) {
        for (uint32_t w = 0U; w < n_words; ++w) {
            ws->basis_tt[1U + i][w] = ws->x_tt[i][w] & active_mask[w];
        }
    }
    uint32_t max_d = 0U;
    for (uint32_t k = 0U; k < plan->num_and; ++k) {
        uint32_t base = 1U + spec->n_in + k;
        uint32_t lm = plan->and_lhs_mask[k] & ((UINT32_C(1) << base) - UINT32_C(1));
        uint32_t rm = plan->and_rhs_mask[k] & ((UINT32_C(1) << base) - UINT32_C(1));
        uint64_t lv[4] = {UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0)};
        uint64_t rv[4] = {UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0)};
        uint32_t md = 0U;
        uint32_t m = lm;
        while (m != 0U) {
            uint32_t idx = searu_ctz64((uint64_t)m);
            for (uint32_t w = 0U; w < n_words; ++w) {
                lv[w] ^= ws->basis_tt[idx][w];
            }
            if (depths[idx] > md) {
                md = depths[idx];
            }
            m &= (m - 1U);
        }
        m = rm;
        while (m != 0U) {
            uint32_t idx = searu_ctz64((uint64_t)m);
            for (uint32_t w = 0U; w < n_words; ++w) {
                rv[w] ^= ws->basis_tt[idx][w];
            }
            if (depths[idx] > md) {
                md = depths[idx];
            }
            m &= (m - 1U);
        }
        for (uint32_t w = 0U; w < n_words; ++w) {
            ws->basis_tt[base][w] = (lv[w] & rv[w]) & active_mask[w];
        }
        depths[base] = (lm != 0U && rm != 0U) ? (md + 1U) : 0U;
        if (depths[base] > max_d) {
            max_d = depths[base];
        }
    }
    if (out_depth != NULL) {
        *out_depth = max_d;
    }
    uint32_t n_b = 1U + spec->n_in + plan->num_and;
    uint32_t valid_b = (UINT32_C(1) << n_b) - UINT32_C(1);
    for (uint32_t o = 0U; o < spec->n_out; ++o) {
        uint64_t acc[4] = {UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0)};
        uint32_t m = plan->out_mask[o] & valid_b;
        while (m != 0U) {
            uint32_t idx = searu_ctz64((uint64_t)m);
            for (uint32_t w = 0U; w < n_words; ++w) {
                acc[w] ^= ws->basis_tt[idx][w];
            }
            m &= (m - 1U);
        }
        for (uint32_t w = 0U; w < n_words; ++w) {
            if ((acc[w] ^ spec->target_tt[o][w]) & active_mask[w]) {
                return false;
            }
        }
    }
    return true;
}

FLOW_FORCE_INL uint32_t searu__mc_u64_rref(const uint64_t *vecs, uint32_t n_vecs,
                                           const uint64_t *targets, uint32_t n_targets,
                                           uint32_t out_comb[SEARU_MC_MAX_OUT],
                                           uint32_t *out_dep_mask) {
    uint64_t ev[16];
    uint32_t el[16];
    uint32_t ec[16];
    uint32_t ne = 0U;
    uint32_t dep = 0U;
    for (uint32_t i = 0U; i < n_vecs && i < 16U; ++i) {
        uint64_t c = vecs[i];
        uint32_t cb = UINT32_C(1) << i;
        for (uint32_t q = 0U; q < ne; ++q) {
            if (((c >> el[q]) & UINT64_C(1)) != UINT64_C(0)) {
                c ^= ev[q];
                cb ^= ec[q];
            }
        }
        if (c == UINT64_C(0)) {
            dep |= (UINT32_C(1) << i);
        } else {
            uint32_t lead = searu_ctz64(c);
            for (uint32_t q = 0U; q < ne; ++q) {
                if (((ev[q] >> lead) & UINT64_C(1)) != UINT64_C(0)) {
                    ev[q] ^= c;
                    ec[q] ^= cb;
                }
            }
            ev[ne] = c;
            el[ne] = lead;
            ec[ne] = cb;
            ++ne;
        }
    }
    uint32_t err = 0U;
    for (uint32_t t = 0U; t < n_targets; ++t) {
        uint64_t rem = targets[t];
        uint32_t om = 0U;
        for (uint32_t q = 0U; q < ne; ++q) {
            if (((rem >> el[q]) & UINT64_C(1)) != UINT64_C(0)) {
                rem ^= ev[q];
                om ^= ec[q];
            }
        }
        if (out_comb != NULL) {
            out_comb[t] = om;
        }
        err += searu_popcnt64(rem);
    }
    if (out_dep_mask != NULL) {
        *out_dep_mask = dep;
    }
    return err;
}

FLOW_FORCE_INL uint64_t searu__mc_outer64(uint32_t a_mask, uint32_t b_mask, uint32_t n0,
                                          uint32_t n1_stride) {
    uint64_t r = UINT64_C(0);
    for (uint32_t i = 0U; i < n0; ++i) {
        if (((a_mask >> i) & 1U) != 0U) {
            r |= ((uint64_t)b_mask) << (i * n1_stride);
        }
    }
    return r;
}

FLOW_FORCE_INL uint32_t searu__mc_gray_span(const uint64_t *b, uint32_t K, const uint64_t *targets,
                                            uint32_t n_targets, searu_mc_ws *ws,
                                            uint32_t out_masks[], uint32_t *out_used) {
    uint32_t n_sub = UINT32_C(1) << K;
    ws->gray_comb[0] = UINT64_C(0);
    uint64_t cur = UINT64_C(0);
    for (uint32_t s = 1U; s < n_sub; ++s) {
        uint32_t g_prev = (s - 1U) ^ ((s - 1U) >> 1U);
        uint32_t g_cur = s ^ (s >> 1U);
        uint32_t bit = searu_ctz64((uint64_t)(g_cur ^ g_prev));
        cur ^= b[bit];
        ws->gray_comb[s] = cur;
    }
    uint32_t err = 0U;
    uint32_t used = 0U;
    for (uint32_t t = 0U; t < n_targets; ++t) {
        uint64_t tv = targets[t];
        uint32_t bw = 99U;
        uint32_t bm = 0U;
        for (uint32_t s = 0U; s < n_sub; ++s) {
            uint32_t w = searu_popcnt64(tv ^ ws->gray_comb[s]);
            if (w < bw) {
                bw = w;
                bm = s ^ (s >> 1U);
                if (w == 0U) {
                    break;
                }
            }
        }
        out_masks[t] = bm;
        used |= bm;
        err += bw;
    }
    *out_used = used;
    return err;
}

FLOW_FORCE_INL void searu__mc_prune_dead_and(const searu_mc_spec *spec, searu_mc_plan *plan) {
    uint32_t K = plan->num_and;
    if (K == 0U) {
        return;
    }
    uint32_t live_and = 0U;
    for (uint32_t o = 0U; o < spec->n_out; ++o) {
        live_and |= (plan->out_mask[o] >> (1U + spec->n_in));
    }
    live_and &= (K < 32U) ? ((UINT32_C(1) << K) - UINT32_C(1)) : UINT32_MAX;
    for (int32_t k = (int32_t)K - 1; k >= 0; --k) {
        if (((live_and >> (uint32_t)k) & 1U) != 0U) {
            uint32_t used_sub =
                (plan->and_lhs_mask[k] | plan->and_rhs_mask[k]) >> (1U + spec->n_in);
            live_and |= used_sub;
        }
    }
    uint32_t remap[SEARU_MC_MAX_AND];
    uint32_t new_k = 0U;
    for (uint32_t k = 0U; k < K; ++k) {
        if (((live_and >> k) & 1U) != 0U) {
            remap[k] = new_k++;
        } else {
            remap[k] = 0U;
        }
    }
    if (new_k == K) {
        return;
    }
    uint32_t prim_mask = (UINT32_C(1) << (1U + spec->n_in)) - UINT32_C(1);
    for (uint32_t k = 0U; k < K; ++k) {
        if (((live_and >> k) & 1U) == 0U) {
            continue;
        }
        uint32_t nk = remap[k];
        uint32_t lm = plan->and_lhs_mask[k] & prim_mask;
        uint32_t rm = plan->and_rhs_mask[k] & prim_mask;
        for (uint32_t r = 0U; r < k; ++r) {
            if (((live_and >> r) & 1U) == 0U) {
                continue;
            }
            if (((plan->and_lhs_mask[k] >> (1U + spec->n_in + r)) & 1U) != 0U) {
                lm |= UINT32_C(1) << (1U + spec->n_in + remap[r]);
            }
            if (((plan->and_rhs_mask[k] >> (1U + spec->n_in + r)) & 1U) != 0U) {
                rm |= UINT32_C(1) << (1U + spec->n_in + remap[r]);
            }
        }
        plan->and_lhs_mask[nk] = lm;
        plan->and_rhs_mask[nk] = rm;
    }
    for (uint32_t o = 0U; o < spec->n_out; ++o) {
        uint32_t om = plan->out_mask[o] & prim_mask;
        for (uint32_t r = 0U; r < K; ++r) {
            if (((live_and >> r) & 1U) != 0U &&
                ((plan->out_mask[o] >> (1U + spec->n_in + r)) & 1U) != 0U) {
                om |= UINT32_C(1) << (1U + spec->n_in + remap[r]);
            }
        }
        plan->out_mask[o] = om;
    }
    plan->num_and = new_k;
}

FLOW_FORCE_INL uint64_t searu__mc_eval_general(const searu_mc_spec *spec, searu_mc_ws *ws,
                                               uint32_t K, uint32_t max_depth, uint32_t n_words,
                                               const uint64_t active_mask[4],
                                               const uint32_t lhs_m[], const uint32_t rhs_m[],
                                               uint32_t *opt_eff_l, uint32_t *opt_eff_r,
                                               uint32_t out_mask[], uint32_t *out_dead_gates,
                                               uint32_t *out_err, uint32_t *out_dmax) {
    uint32_t depths[SEARU_MC_MAX_BASIS];
    uint32_t eff_l[SEARU_MC_MAX_AND];
    uint32_t eff_r[SEARU_MC_MAX_AND];
    for (uint32_t i = 0U; i < 1U + spec->n_in; ++i) {
        depths[i] = 0U;
    }
    uint32_t dmax = 0U;
    for (uint32_t k = 0U; k < K; ++k) {
        uint32_t avail = (UINT32_C(1) << (1U + spec->n_in)) - UINT32_C(1);
        for (uint32_t r = 0U; r < k; ++r) {
            if (depths[1U + spec->n_in + r] < max_depth) {
                avail |= (UINT32_C(1) << (1U + spec->n_in + r));
            }
        }
        uint32_t lm = lhs_m[k] & avail;
        uint32_t rm = rhs_m[k] & avail;
        eff_l[k] = lm;
        eff_r[k] = rm;
        if (opt_eff_l != NULL) {
            opt_eff_l[k] = lm;
        }
        if (opt_eff_r != NULL) {
            opt_eff_r[k] = rm;
        }

        uint64_t lv[4] = {UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0)};
        uint64_t rv[4] = {UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0)};
        uint32_t md = 0U;
        uint32_t m = lm;
        while (m != 0U) {
            uint32_t idx = searu_ctz64((uint64_t)m);
            for (uint32_t w = 0U; w < n_words; ++w) {
                lv[w] ^= ws->basis_tt[idx][w];
            }
            if (depths[idx] > md) {
                md = depths[idx];
            }
            m &= (m - 1U);
        }
        m = rm;
        while (m != 0U) {
            uint32_t idx = searu_ctz64((uint64_t)m);
            for (uint32_t w = 0U; w < n_words; ++w) {
                rv[w] ^= ws->basis_tt[idx][w];
            }
            if (depths[idx] > md) {
                md = depths[idx];
            }
            m &= (m - 1U);
        }
        uint32_t bid = 1U + spec->n_in + k;
        for (uint32_t w = 0U; w < n_words; ++w) {
            ws->basis_tt[bid][w] = (lv[w] & rv[w]) & active_mask[w];
        }
        searu__mc_mobius256(ws->basis_anf[bid], ws->basis_tt[bid], spec->n_in, n_words,
                            active_mask);
        uint32_t kd = (lm != 0U && rm != 0U) ? (md + 1U) : 0U;
        depths[bid] = kd;
        if (kd > dmax) {
            dmax = kd;
        }
    }

    uint32_t dep_b = 0U;
    uint32_t uncov = 0U;
    uint32_t err =
        searu__mc_anf_rref(spec, ws, 1U + spec->n_in + K, n_words, out_mask, &dep_b, &uncov);
    uint32_t dep_gates = dep_b >> (1U + spec->n_in);
    uint32_t dead_g = dep_gates;
    for (uint32_t k = 0U; k < K; ++k) {
        if (eff_l[k] <= 1U || eff_r[k] <= 1U || eff_l[k] == eff_r[k] ||
            (eff_l[k] ^ eff_r[k]) == 1U) {
            dead_g |= (UINT32_C(1) << k);
        }
    }
    *out_dead_gates = dead_g;
    *out_err = err;
    *out_dmax = dmax;
    return ((uint64_t)err << 8U) | ((uint64_t)uncov << 4U) | (uint64_t)searu_popcnt64(dep_gates);
}

FLOW_FORCE_INL bool searu__mc_search_k_depth(const searu_mc_spec *spec, searu_mc_ws *ws, uint32_t K,
                                             uint32_t eff_max_d, uint32_t n_words,
                                             const uint64_t active_mask[4], uint32_t n_res,
                                             uint32_t n_it, uint64_t *rng_io,
                                             uint32_t *total_iters_io, searu_mc_plan *out_plan) {
    uint64_t rng = *rng_io;
    uint32_t total_iters = *total_iters_io;
    bool solved_k = false;

    for (uint32_t rs = 0U; rs < n_res && !solved_k; ++rs) {
        uint32_t L[SEARU_MC_MAX_AND];
        uint32_t R[SEARU_MC_MAX_AND];
        uint32_t bL[SEARU_MC_MAX_AND];
        uint32_t bR[SEARU_MC_MAX_AND];
        uint32_t om[SEARU_MC_MAX_OUT];
        for (uint32_t k = 0U; k < K; ++k) {
            uint32_t nb = 1U + spec->n_in + k;
            uint32_t full_k = (UINT32_C(1) << nb) - UINT32_C(1);
            L[k] = 2U + searu_Bounded_u32(searu_rng_next(&rng), full_k - 1U);
            R[k] = 2U + searu_Bounded_u32(searu_rng_next(&rng), full_k - 1U);
            bL[k] = L[k];
            bR[k] = R[k];
        }
        uint32_t dead_g = 0U, cur_err = 0U, cur_dm = 0U;
        uint64_t cur_sc = searu__mc_eval_general(spec, ws, K, eff_max_d, n_words, active_mask, L, R,
                                                 NULL, NULL, om, &dead_g, &cur_err, &cur_dm);
        uint64_t best_sc = cur_sc;
        uint32_t stag = 0U;

        for (uint32_t it = 0U; it < n_it; ++it) {
            if (cur_err == 0U) {
                solved_k = true;
                break;
            }
            ++total_iters;
            uint32_t Lc[SEARU_MC_MAX_AND];
            uint32_t Rc[SEARU_MC_MAX_AND];
            for (uint32_t k = 0U; k < K; ++k) {
                Lc[k] = L[k];
                Rc[k] = R[k];
            }
            uint32_t nm = (stag < 120U) ? 1U : (1U + (uint32_t)(searu_rng_next(&rng) & 3U));
            for (uint32_t m_i = 0U; m_i < nm; ++m_i) {
                uint64_t rv = searu_rng_next(&rng);
                uint32_t k = 0U;
                if (dead_g != 0U && ((rv & 3U) != 0U)) {
                    uint32_t pick = searu_Bounded_u32(rv >> 4U, K);
                    uint32_t rot_d = ((dead_g >> pick) | (dead_g << ((K - pick) & 31U))) &
                                     ((UINT32_C(1) << K) - UINT32_C(1));
                    uint32_t off = (rot_d != 0U) ? searu_ctz64((uint64_t)rot_d) : 0U;
                    k = pick + off;
                    if (k >= K) {
                        k -= K;
                    }
                } else {
                    k = searu_Bounded_u32(rv >> 8U, K);
                }
                uint32_t nb = 1U + spec->n_in + k;
                uint32_t full_k = (UINT32_C(1) << nb) - UINT32_C(1);
                if (((rv >> 16U) & 7U) == 0U) {
                    if (((rv >> 20U) & 1U) == 0U) {
                        Lc[k] = 2U + searu_Bounded_u32(searu_rng_next(&rng), full_k - 1U);
                    } else {
                        Rc[k] = 2U + searu_Bounded_u32(searu_rng_next(&rng), full_k - 1U);
                    }
                } else {
                    uint32_t bit = UINT32_C(1) << searu_Bounded_u32(rv >> 24U, nb);
                    if (((rv >> 20U) & 1U) == 0U) {
                        Lc[k] ^= bit;
                    } else {
                        Rc[k] ^= bit;
                    }
                }
            }
            uint32_t ndead = 0U, nerr = 0U, ndm = 0U;
            uint64_t nsc = searu__mc_eval_general(spec, ws, K, eff_max_d, n_words, active_mask, Lc,
                                                  Rc, NULL, NULL, om, &ndead, &nerr, &ndm);
            if (nerr == 0U) {
                for (uint32_t k = 0U; k < K; ++k) {
                    L[k] = Lc[k];
                    R[k] = Rc[k];
                }
                cur_err = 0U;
                solved_k = true;
                break;
            }
            bool acc = (nsc <= cur_sc);
            if (!acc && stag > 96U && nsc <= cur_sc + UINT64_C(512) &&
                (searu_rng_next(&rng) & 15U) == 0U) {
                acc = true;
            }
            if (acc) {
                stag = (nsc < cur_sc) ? 0U : (stag + 1U);
                cur_sc = nsc;
                dead_g = ndead;
                for (uint32_t k = 0U; k < K; ++k) {
                    L[k] = Lc[k];
                    R[k] = Rc[k];
                }
                if (nsc < best_sc) {
                    best_sc = nsc;
                    for (uint32_t k = 0U; k < K; ++k) {
                        bL[k] = Lc[k];
                        bR[k] = Rc[k];
                    }
                }
            } else {
                ++stag;
                if (stag > 600U) {
                    for (uint32_t k = 0U; k < K; ++k) {
                        L[k] = bL[k];
                        R[k] = bR[k];
                    }
                    uint32_t kk = searu_Bounded_u32(searu_rng_next(&rng), K);
                    uint32_t nb = 1U + spec->n_in + kk;
                    L[kk] ^= UINT32_C(1) << searu_Bounded_u32(searu_rng_next(&rng), nb);
                    R[kk] ^= UINT32_C(1) << searu_Bounded_u32(searu_rng_next(&rng), nb);
                    cur_sc = searu__mc_eval_general(spec, ws, K, eff_max_d, n_words, active_mask, L,
                                                    R, NULL, NULL, om, &dead_g, &cur_err, &cur_dm);
                    stag = 0U;
                }
            }
        }

        if (solved_k) {
            searu_mc_plan cand_plan;
            for (uint32_t k = 0U; k < SEARU_MC_MAX_AND; ++k) {
                cand_plan.and_lhs_mask[k] = 0U;
                cand_plan.and_rhs_mask[k] = 0U;
            }
            for (uint32_t o = 0U; o < SEARU_MC_MAX_OUT; ++o) {
                cand_plan.out_mask[o] = 0U;
            }
            cand_plan.num_and = K;
            cand_plan.iterations_run = total_iters;
            cand_plan.mode_used = 2U;
            cand_plan.exact = true;
            (void)searu__mc_eval_general(spec, ws, K, eff_max_d, n_words, active_mask, L, R,
                                         cand_plan.and_lhs_mask, cand_plan.and_rhs_mask,
                                         cand_plan.out_mask, &dead_g, &cur_err,
                                         &cand_plan.mult_depth);
            searu__mc_prune_dead_and(spec, &cand_plan);
            if (!searu__mc_verify_plan(spec, ws, &cand_plan, n_words, active_mask,
                                       &cand_plan.mult_depth) ||
                cand_plan.mult_depth > eff_max_d) {
                solved_k = false;
            } else {
                *out_plan = cand_plan;
            }
        }
    }
    *rng_io = rng;
    *total_iters_io = total_iters;
    return solved_k;
}

SEARU_MC_API bool searu_solve_mc_gf2(searu_mc_plan *out, const searu_mc_spec *spec,
                                     searu_mc_ws *ws) {
    if (out == NULL || spec == NULL || ws == NULL) {
        return false;
    }
    for (uint32_t i = 0U; i < SEARU_MC_MAX_AND; ++i) {
        out->and_lhs_mask[i] = 0U;
        out->and_rhs_mask[i] = 0U;
    }
    for (uint32_t o = 0U; o < SEARU_MC_MAX_OUT; ++o) {
        out->out_mask[o] = 0U;
    }
    out->num_and = 0U;
    out->mult_depth = 0U;
    out->iterations_run = 0U;
    out->mode_used = 0U;
    out->exact = false;

    if (spec->n_in == 0U || spec->n_in > SEARU_MC_MAX_IN || spec->n_out == 0U ||
        spec->n_out > SEARU_MC_MAX_OUT || spec->max_and > SEARU_MC_MAX_AND) {
        return false;
    }

    uint32_t n_words = 1U;
    uint64_t active_mask[4];
    searu__mc_active_mask(active_mask, spec->n_in, &n_words);
    uint32_t n_states = UINT32_C(1) << spec->n_in;

    for (uint32_t i = 0U; i < spec->n_in; ++i) {
        for (uint32_t w = 0U; w < 4U; ++w) {
            ws->x_tt[i][w] = UINT64_C(0);
        }
        for (uint32_t s = 0U; s < n_states; ++s) {
            if (((s >> i) & 1U) != 0U) {
                ws->x_tt[i][s >> 6U] |= (UINT64_C(1) << (s & 63U));
            }
        }
        for (uint32_t w = 0U; w < n_words; ++w) {
            ws->basis_tt[1U + i][w] = ws->x_tt[i][w] & active_mask[w];
        }
        searu__mc_mobius256(ws->basis_anf[1U + i], ws->basis_tt[1U + i], spec->n_in, n_words,
                            active_mask);
    }
    for (uint32_t w = 0U; w < 4U; ++w) {
        ws->basis_tt[0][w] = active_mask[w];
    }
    searu__mc_mobius256(ws->basis_anf[0], ws->basis_tt[0], spec->n_in, n_words, active_mask);

    for (uint32_t o = 0U; o < spec->n_out; ++o) {
        searu__mc_mobius256(ws->target_anf[o], spec->target_tt[o], spec->n_in, n_words,
                            active_mask);
    }

    /* Tier 0: Exact K=0 GF(2)-Affine check */
    uint32_t dep0 = 0U, uncov0 = 0U;
    uint32_t err0 =
        searu__mc_anf_rref(spec, ws, 1U + spec->n_in, n_words, out->out_mask, &dep0, &uncov0);
    if (err0 == 0U &&
        searu__mc_verify_plan(spec, ws, out, n_words, active_mask, &out->mult_depth)) {
        out->exact = true;
        out->mode_used = 0U;
        return true;
    }
    if (spec->max_and == 0U) {
        return false;
    }

    uint32_t scale = (spec->budget_scale > 0U) ? spec->budget_scale : 1U;
    uint32_t total_iters = 0U;
    uint64_t rng = spec->seed | UINT64_C(0x9E3779B97F4A7C15);

    /*Tier 1: Auto-Detected Two-Port Bilinear Tensor Decomposition (mult_depth == 1) */
    uint32_t n0 = spec->n_in_port0;
    uint32_t n1 = (n0 > 0U && n0 < spec->n_in) ? (spec->n_in - n0) : 0U;
    if (n0 > 0U && n0 <= 6U && n1 <= 6U && (n0 * n1) <= 64U) {
        bool is_bil = true;
        uint32_t p0_mask = (UINT32_C(1) << n0) - UINT32_C(1);
        uint32_t p1_mask = (UINT32_C(1) << n1) - UINT32_C(1);
        uint64_t T_o[SEARU_MC_MAX_OUT];
        uint32_t aff_o[SEARU_MC_MAX_OUT];
        for (uint32_t o = 0U; o < spec->n_out; ++o) {
            T_o[o] = UINT64_C(0);
            aff_o[o] = 0U;
            for (uint32_t s = 0U; s < n_states; ++s) {
                if (((ws->target_anf[o][s >> 6U] >> (s & 63U)) & UINT64_C(1)) == UINT64_C(0)) {
                    continue;
                }
                uint32_t wt = searu_popcnt64((uint64_t)s);
                if (wt == 0U) {
                    aff_o[o] |= UINT32_C(1);
                } else if (wt == 1U) {
                    aff_o[o] |= UINT32_C(1) << (1U + searu_ctz64((uint64_t)s));
                } else {
                    uint32_t w0 = searu_popcnt64((uint64_t)(s & p0_mask));
                    uint32_t w1 = searu_popcnt64((uint64_t)((s >> n0) & p1_mask));
                    if (w0 != 1U || w1 != 1U || wt != 2U) {
                        is_bil = false;
                        break;
                    }
                    uint32_t bi = searu_ctz64((uint64_t)(s & p0_mask));
                    uint32_t bj = searu_ctz64((uint64_t)((s >> n0) & p1_mask));
                    T_o[o] |= UINT64_C(1) << (bi * n1 + bj);
                }
            }
            if (!is_bil) {
                break;
            }
        }
        if (is_bil) {
            bool is_sym = (n0 == n1);
            for (uint32_t o = 0U; o < spec->n_out && is_sym; ++o) {
                for (uint32_t i = 0U; i < n0 && is_sym; ++i) {
                    for (uint32_t j = i + 1U; j < n1; ++j) {
                        uint32_t b_ij = (uint32_t)((T_o[o] >> (i * n1 + j)) & UINT64_C(1));
                        uint32_t b_ji = (uint32_t)((T_o[o] >> (j * n0 + i)) & UINT64_C(1));
                        if (b_ij != b_ji) {
                            is_sym = false;
                        }
                    }
                }
            }
            /* Compute slice F2 rank lower bound */
            uint64_t piv[SEARU_MC_MAX_OUT];
            uint32_t npiv = 0U;
            for (uint32_t o = 0U; o < spec->n_out; ++o) {
                uint64_t c = T_o[o];
                for (uint32_t q = 0U; q < npiv; ++q) {
                    uint32_t lb = searu_ctz64(piv[q]);
                    if (((c >> lb) & UINT64_C(1)) != UINT64_C(0)) {
                        c ^= piv[q];
                    }
                }
                if (c != UINT64_C(0)) {
                    piv[npiv++] = c;
                }
            }
            uint32_t k_lb = (npiv > 0U) ? npiv : 1U;
            uint32_t k_hi = (spec->max_and < 12U) ? spec->max_and : 12U;
            bool allow_3way = ((n0 * spec->n_out) <= 64U) && ((n1 * spec->n_out) <= 64U);
            uint64_t T_i[8];
            uint64_t T_j[8];
            for (uint32_t x = 0U; x < 8U; ++x) {
                T_i[x] = UINT64_C(0);
                T_j[x] = UINT64_C(0);
            }
            if (allow_3way) {
                for (uint32_t o = 0U; o < spec->n_out; ++o) {
                    for (uint32_t bi = 0U; bi < n0; ++bi) {
                        for (uint32_t bj = 0U; bj < n1; ++bj) {
                            if (((T_o[o] >> (bi * n1 + bj)) & UINT64_C(1)) != UINT64_C(0)) {
                                T_i[bi] |= UINT64_C(1) << (bj * spec->n_out + o);
                                T_j[bj] |= UINT64_C(1) << (bi * spec->n_out + o);
                            }
                        }
                    }
                }
            }

            for (uint32_t K = k_lb; K <= k_hi; ++K) {
                bool use_gray = (K <= 10U);
                uint32_t bil_passes = is_sym ? 2U : 1U;
                for (uint32_t pass = 0U; pass < bil_passes; ++pass) {
                    bool sym_mode = is_sym && (pass == 0U);
                    uint32_t n_res = sym_mode ? (12U * scale) : (20U * scale);
                    uint32_t n_it = sym_mode ? 2400U : 1600U;
                    for (uint32_t rs = 0U; rs < n_res; ++rs) {
                        uint32_t U[16];
                        uint32_t V[16];
                        uint32_t W[16];
                        uint32_t Uc[16];
                        uint32_t Vc[16];
                        uint32_t Wc[16];
                        uint64_t bv[16];
                        uint32_t wm[SEARU_MC_MAX_OUT];
                        uint32_t nwm[SEARU_MC_MAX_OUT];
                        uint32_t used = 0U;
                        uint32_t nused = 0U;
                        for (uint32_t k = 0U; k < K; ++k) {
                            U[k] = 1U + searu_Bounded_u32(searu_rng_next(&rng), p0_mask);
                            V[k] = sym_mode
                                       ? U[k]
                                       : (1U + searu_Bounded_u32(searu_rng_next(&rng), p1_mask));
                            W[k] = 0U;
                            bv[k] = searu__mc_outer64(U[k], V[k], n0, n1);
                        }
                        uint32_t dep_u = 0U;
                        uint32_t ce =
                            use_gray ? searu__mc_gray_span(bv, K, T_o, spec->n_out, ws, wm, &used)
                                     : searu__mc_u64_rref(bv, K, T_o, spec->n_out, wm, &dep_u);
                        if (!use_gray) {
                            used = ((UINT32_C(1) << K) - UINT32_C(1)) & ~dep_u;
                        }
                        for (uint32_t k = 0U; k < K; ++k) {
                            uint32_t wk = 0U;
                            for (uint32_t o = 0U; o < spec->n_out; ++o) {
                                if (((wm[o] >> k) & 1U) != 0U) {
                                    wk |= (UINT32_C(1) << o);
                                }
                            }
                            W[k] = wk;
                        }
                        uint32_t stag = 0U;
                        for (uint32_t it = 0U; it < n_it; ++it) {
                            if (ce == 0U) {
                                break;
                            }
                            ++total_iters;
                            for (uint32_t k = 0U; k < K; ++k) {
                                Uc[k] = U[k];
                                Vc[k] = V[k];
                                Wc[k] = W[k];
                            }
                            uint32_t nf = (stag < 32U) ? 1U : 2U;
                            uint32_t dead_b = (((UINT32_C(1) << K) - UINT32_C(1)) & ~used);
                            for (uint32_t f = 0U; f < nf; ++f) {
                                uint32_t k = 0U;
                                uint64_t rv = searu_rng_next(&rng);
                                if (dead_b != 0U && (rv & 3U) != 0U) {
                                    k = searu_ctz64((uint64_t)dead_b);
                                } else {
                                    k = searu_Bounded_u32(rv >> 8U, K);
                                }
                                if (sym_mode) {
                                    if (dead_b != 0U && ((rv >> 16U) & 3U) == 0U) {
                                        Uc[k] =
                                            1U + searu_Bounded_u32(searu_rng_next(&rng), p0_mask);
                                    } else {
                                        Uc[k] ^= UINT32_C(1) << searu_Bounded_u32(rv >> 24U, n0);
                                        if (Uc[k] == 0U) {
                                            Uc[k] = 1U;
                                        }
                                    }
                                    Vc[k] = Uc[k];
                                } else if ((rv & 1U) == 0U) {
                                    Uc[k] = 1U + searu_Bounded_u32(searu_rng_next(&rng), p0_mask);
                                    Vc[k] = 1U + searu_Bounded_u32(searu_rng_next(&rng), p1_mask);
                                } else if ((rv & 2U) == 0U) {
                                    Uc[k] ^= UINT32_C(1) << searu_Bounded_u32(rv >> 24U, n0);
                                    if (Uc[k] == 0U) {
                                        Uc[k] = 1U;
                                    }
                                } else {
                                    Vc[k] ^= UINT32_C(1) << searu_Bounded_u32(rv >> 24U, n1);
                                    if (Vc[k] == 0U) {
                                        Vc[k] = 1U;
                                    }
                                }
                            }
                            uint32_t ne = 0U;
                            if (!sym_mode && use_gray && allow_3way) {
                                for (uint32_t als = 0U; als < 2U; ++als) {
                                    for (uint32_t k = 0U; k < K; ++k) {
                                        bv[k] = searu__mc_outer64(Uc[k], Vc[k], n0, n1);
                                    }
                                    ne = searu__mc_gray_span(bv, K, T_o, spec->n_out, ws, nwm,
                                                             &nused);
                                    if (ne == 0U) {
                                        break;
                                    }
                                    for (uint32_t k = 0U; k < K; ++k) {
                                        uint32_t wk = 0U;
                                        for (uint32_t o = 0U; o < spec->n_out; ++o) {
                                            if (((nwm[o] >> k) & 1U) != 0U) {
                                                wk |= (UINT32_C(1) << o);
                                            }
                                        }
                                        Wc[k] = wk;
                                    }
                                    for (uint32_t k = 0U; k < K; ++k) {
                                        bv[k] = searu__mc_outer64(Vc[k], Wc[k], n1, spec->n_out);
                                    }
                                    uint32_t um[8];
                                    uint32_t u_used = 0U;
                                    uint32_t ne_u =
                                        searu__mc_gray_span(bv, K, T_i, n0, ws, um, &u_used);
                                    for (uint32_t k = 0U; k < K; ++k) {
                                        uint32_t uk = 0U;
                                        for (uint32_t bi = 0U; bi < n0; ++bi) {
                                            if (((um[bi] >> k) & 1U) != 0U) {
                                                uk |= (UINT32_C(1) << bi);
                                            }
                                        }
                                        Uc[k] = uk;
                                    }
                                    if (ne_u == 0U) {
                                        for (uint32_t k = 0U; k < K; ++k) {
                                            bv[k] = searu__mc_outer64(Uc[k], Vc[k], n0, n1);
                                        }
                                        ne = searu__mc_gray_span(bv, K, T_o, spec->n_out, ws, nwm,
                                                                 &nused);
                                        break;
                                    }
                                    for (uint32_t k = 0U; k < K; ++k) {
                                        bv[k] = searu__mc_outer64(Uc[k], Wc[k], n0, spec->n_out);
                                    }
                                    uint32_t vm[8];
                                    uint32_t v_used = 0U;
                                    (void)searu__mc_gray_span(bv, K, T_j, n1, ws, vm, &v_used);
                                    for (uint32_t k = 0U; k < K; ++k) {
                                        uint32_t vk = 0U;
                                        for (uint32_t bj = 0U; bj < n1; ++bj) {
                                            if (((vm[bj] >> k) & 1U) != 0U) {
                                                vk |= (UINT32_C(1) << bj);
                                            }
                                        }
                                        Vc[k] = vk;
                                    }
                                    for (uint32_t k = 0U; k < K; ++k) {
                                        bv[k] = searu__mc_outer64(Uc[k], Vc[k], n0, n1);
                                    }
                                    ne = searu__mc_gray_span(bv, K, T_o, spec->n_out, ws, nwm,
                                                             &nused);
                                    if (ne == 0U) {
                                        break;
                                    }
                                }
                            } else {
                                for (uint32_t k = 0U; k < K; ++k) {
                                    bv[k] = searu__mc_outer64(Uc[k], Vc[k], n0, n1);
                                }
                                if (use_gray) {
                                    ne = searu__mc_gray_span(bv, K, T_o, spec->n_out, ws, nwm,
                                                             &nused);
                                } else {
                                    uint32_t ndep = 0U;
                                    ne = searu__mc_u64_rref(bv, K, T_o, spec->n_out, nwm, &ndep);
                                    nused = ((UINT32_C(1) << K) - UINT32_C(1)) & ~ndep;
                                }
                            }

                            bool acc = (ne <= ce);
                            if (!acc && stag > 40U && ne <= ce + 1U &&
                                (searu_rng_next(&rng) & 15U) == 0U) {
                                acc = true;
                            }
                            if (acc) {
                                stag = (ne < ce) ? 0U : (stag + 1U);
                                ce = ne;
                                used = nused;
                                for (uint32_t k = 0U; k < K; ++k) {
                                    U[k] = Uc[k];
                                    V[k] = Vc[k];
                                    W[k] = Wc[k];
                                }
                                for (uint32_t o = 0U; o < spec->n_out; ++o) {
                                    wm[o] = nwm[o];
                                }
                            } else {
                                ++stag;
                            }
                        }
                        if (ce == 0U) {
                            out->num_and = K;
                            out->iterations_run = total_iters;
                            out->mode_used = 1U;
                            for (uint32_t k = 0U; k < K; ++k) {
                                out->and_lhs_mask[k] = U[k] << 1U;
                                out->and_rhs_mask[k] = V[k] << (1U + n0);
                            }
                            for (uint32_t o = 0U; o < spec->n_out; ++o) {
                                out->out_mask[o] = aff_o[o] | (wm[o] << (1U + spec->n_in));
                            }
                            searu__mc_prune_dead_and(spec, out);
                            if (searu__mc_verify_plan(spec, ws, out, n_words, active_mask,
                                                      &out->mult_depth)) {
                                out->exact = true;
                                return true;
                            }
                        }
                    }
                }
            }
        }
    }

    /* Tier 2: General Multi-Depth SEARU x ANF-RREF Solver */
    uint32_t alg_deg = 0U;
    uint32_t nl_vecs = 0U;
    for (uint32_t o = 0U; o < spec->n_out; ++o) {
        uint64_t nl[4] = {UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0)};
        for (uint32_t s = 0U; s < n_states; ++s) {
            if (((ws->target_anf[o][s >> 6U] >> (s & 63U)) & UINT64_C(1)) != UINT64_C(0)) {
                uint32_t d = searu_popcnt64((uint64_t)s);
                if (d > alg_deg) {
                    alg_deg = d;
                }
                if (d >= 2U) {
                    nl[s >> 6U] |= (UINT64_C(1) << (s & 63U));
                }
            }
        }
        /* Store nonlinear residual rows in ech_vec temporarily to compute exact F2 quotient rank */
        for (uint32_t q = 0U; q < nl_vecs; ++q) {
            uint32_t lb = ws->ech_lead[q];
            if (((nl[lb >> 6U] >> (lb & 63U)) & UINT64_C(1)) != UINT64_C(0)) {
                for (uint32_t u = 0U; u < n_words; ++u) {
                    nl[u] ^= ws->ech_vec[q][u];
                }
            }
        }
        uint32_t lead = searu__mc_first_bit256(nl, n_words);
        if (lead < 256U && nl_vecs < SEARU_MC_MAX_BASIS) {
            for (uint32_t u = 0U; u < n_words; ++u) {
                ws->ech_vec[nl_vecs][u] = nl[u];
            }
            ws->ech_lead[nl_vecs] = lead;
            ++nl_vecs;
        }
    }

    uint32_t min_req_d = 0U;
    if (alg_deg >= 2U) {
        uint32_t span = 1U;
        while (span < alg_deg) {
            span <<= 1U;
            ++min_req_d;
        }
    }
    uint32_t k_lb = (nl_vecs > 0U) ? nl_vecs : 1U;
    if (k_lb > spec->max_and) {
        return false;
    }
    if (spec->max_depth > 0U && spec->max_depth < min_req_d) {
        return false;
    }

    searu_mc_plan best_plan;
    bool has_best = false;
    uint32_t scout_restarts = 48U * scale;
    uint32_t scout_iters = 4096U;
    uint32_t tight_restarts = 128U * scale;
    uint32_t tight_iters = 6144U;

    uint32_t K = k_lb;
    bool tightening = false;
    while (K <= spec->max_and) {
        uint32_t eff_max_d = (spec->max_depth > 0U) ? spec->max_depth : K;
        if (eff_max_d < min_req_d) {
            break;
        }
        uint32_t n_res = tightening ? tight_restarts : scout_restarts;
        uint32_t n_it = tightening ? tight_iters : scout_iters;
        bool solved_k = searu__mc_search_k_depth(spec, ws, K, eff_max_d, n_words, active_mask,
                                                 n_res, n_it, &rng, &total_iters, &best_plan);
        if (solved_k) {
            has_best = true;
            if (best_plan.num_and <= k_lb) {
                break;
            }
            K = best_plan.num_and - 1U;
            tightening = true;
        } else {
            if (tightening) {
                break;
            }
            ++K;
        }
    }

    if (!has_best) {
        return false;
    }

    /* Depth-Compression Polish when max_depth was unconstrained */
    if (spec->max_depth == 0U) {
        while (best_plan.mult_depth > min_req_d) {
            uint32_t d_cap = best_plan.mult_depth - 1U;
            searu_mc_plan d_plan;
            if (searu__mc_search_k_depth(spec, ws, best_plan.num_and, d_cap, n_words, active_mask,
                                         128U * scale, 6144U, &rng, &total_iters, &d_plan)) {
                best_plan = d_plan;
            } else {
                break;
            }
        }
    }

    /* Final zero-error affine fan-in & depth polish */
    uint32_t best_pop = 0U;
    for (uint32_t k = 0U; k < best_plan.num_and; ++k) {
        best_pop +=
            searu_popcnt64(best_plan.and_lhs_mask[k]) + searu_popcnt64(best_plan.and_rhs_mask[k]);
    }
    for (uint32_t o = 0U; o < spec->n_out; ++o) {
        best_pop += searu_popcnt64(best_plan.out_mask[o]);
    }
    for (uint32_t pol = 0U; pol < 512U && best_plan.num_and > 0U; ++pol) {
        searu_mc_plan cand = best_plan;
        uint64_t rv = searu_rng_next(&rng);
        uint32_t k = searu_Bounded_u32(rv, cand.num_and);
        uint32_t *target_m = ((rv & 1U) != 0U) ? &cand.and_lhs_mask[k] : &cand.and_rhs_mask[k];
        if (*target_m != 0U) {
            uint32_t bit = UINT32_C(1) << searu_Bounded_u32(rv >> 16U, 1U + spec->n_in + k);
            *target_m &= ~bit;
            uint32_t cd = 0U;
            if (searu__mc_verify_plan(spec, ws, &cand, n_words, active_mask, &cd) &&
                (spec->max_depth == 0U || cd <= spec->max_depth)) {
                uint32_t cpop = 0U;
                for (uint32_t j = 0U; j < cand.num_and; ++j) {
                    cpop +=
                        searu_popcnt64(cand.and_lhs_mask[j]) + searu_popcnt64(cand.and_rhs_mask[j]);
                }
                for (uint32_t o = 0U; o < spec->n_out; ++o) {
                    cpop += searu_popcnt64(cand.out_mask[o]);
                }
                if (cd < best_plan.mult_depth || (cd == best_plan.mult_depth && cpop < best_pop)) {
                    cand.mult_depth = cd;
                    best_plan = cand;
                    best_pop = cpop;
                }
            }
        }
    }

    best_plan.iterations_run = total_iters;
    *out = best_plan;
    out->exact = searu__mc_verify_plan(spec, ws, out, n_words, active_mask, &out->mult_depth);
    return out->exact;
}

#ifdef __cplusplus
}
#endif

#endif /* SEARU_MC_GF2_H */
