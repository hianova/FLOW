#ifndef FLOW_H
#define FLOW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpsabi"
#endif

/* ============================================================================
 * FLOW — Object-Free HoTT & 2-Jet Register Calculus
 * ============================================================================
 * Canonical 64-byte (512-bit, 8 x u64) register vector substrate.
 * Zero structs. Zero control-flow branches in kernel. Zero float. Zero heap.
 * ============================================================================ */

__extension__ typedef uint64_t flow_cell __attribute__((vector_size(64), aligned(64)));

_Static_assert(sizeof(flow_cell) == 64U, "flow_cell must occupy exactly 64 bytes");
_Static_assert(_Alignof(flow_cell) == 64U, "flow_cell must be 64-byte cache-line aligned");

#define FLOW_VERSION_MAJOR 5
#define FLOW_VERSION_MINOR 0
#define FLOW_VERSION_PATCH 0
#ifndef FLOW_VERSION_STRING
#define FLOW_VERSION_STRING "5.0.0"
#endif

#define FLOW_Q16_SHIFT           16U
#define FLOW_Q16_ONE             INT64_C(65536)
#define FLOW_CONVERGE_EPS_Q16    INT64_C(4)

#define FLOW_FP_MOD              UINT64_C(0xFFFFFFFF00000001)
#define FLOW_GOLDEN_64           UINT64_C(0x9E3779B97F4A7C15)

#define FLOW_SIEVE_SEAL_VALID    (1U << 0)
#define FLOW_SIEVE_DOMAIN_MATCH  (1U << 1)
#define FLOW_SIEVE_NO_OVERFLOW   (1U << 2)
#define FLOW_SIEVE_KAN_HOMOTOPIC (1U << 3)
#define FLOW_SIEVE_HORIZON_VALID (1U << 4)
#define FLOW_SIEVE_CONVERGED     (1U << 5)
#define FLOW_SIEVE_BASE_GATE     (FLOW_SIEVE_SEAL_VALID | FLOW_SIEVE_DOMAIN_MATCH)
#define FLOW_SIEVE_SAFETY_GATE                                                  \
    (FLOW_SIEVE_SEAL_VALID | FLOW_SIEVE_DOMAIN_MATCH | FLOW_SIEVE_NO_OVERFLOW | \
     FLOW_SIEVE_HORIZON_VALID)

/* --- Branchless Bitwise Combinators --- */

static inline uint64_t flow_mask_bool(bool cond) {
    return UINT64_C(0) - (uint64_t)cond;
}

static inline uint64_t flow_mux_u64(uint64_t mask, uint64_t tr, uint64_t fa) {
    return (tr & mask) | (fa & ~mask);
}

static inline int64_t flow_mux_i64(uint64_t mask, int64_t tr, int64_t fa) {
    return (int64_t)flow_mux_u64(mask, (uint64_t)tr, (uint64_t)fa);
}

static inline uint64_t flow_abs_i64(int64_t x) {
    uint64_t ux = (uint64_t)x;
    uint64_t neg = flow_mask_bool(x < 0);
    return (ux ^ neg) - neg;
}

static inline uint64_t flow_umul128(uint64_t a, uint64_t b, uint64_t *out_hi) {
#if defined(__SIZEOF_INT128__) &&                                         \
    (defined(__aarch64__) || defined(__arm64__) || defined(__x86_64__) || \
     (defined(__riscv) && (__riscv_xlen == 64)))
    __extension__ unsigned __int128 z = (unsigned __int128)a * (unsigned __int128)b;
    *out_hi = (uint64_t)(z >> 64U);
    return (uint64_t)z;
#else
    uint64_t a0 = a & UINT64_C(0xFFFFFFFF);
    uint64_t a1 = a >> 32U;
    uint64_t b0 = b & UINT64_C(0xFFFFFFFF);
    uint64_t b1 = b >> 32U;
    uint64_t p00 = a0 * b0;
    uint64_t p01 = a0 * b1;
    uint64_t p10 = a1 * b0;
    uint64_t p11 = a1 * b1;
    uint64_t mid = (p00 >> 32U) + (p01 & UINT64_C(0xFFFFFFFF)) + (p10 & UINT64_C(0xFFFFFFFF));
    *out_hi = p11 + (p01 >> 32U) + (p10 >> 32U) + (mid >> 32U);
    return (p00 & UINT64_C(0xFFFFFFFF)) | (mid << 32U);
#endif
}

static inline void flow_isqrt_step(uint64_t *rem, uint64_t *root, uint32_t shift) {
    uint64_t b = UINT64_C(1) << shift;
    uint64_t cand = *root + b;
    uint64_t ge = flow_mask_bool(*rem >= cand);
    *rem -= (cand & ge);
    *root = (*root >> 1U) + (b & ge);
}

/* Exact 0-ULP branchless 64-bit integer square root (0 branches, 0 divisions, 0 tables) */
static inline uint64_t flow_isqrt_u64(uint64_t x) {
    uint64_t rem = x;
    uint64_t root = 0ULL;
#define FLOW_ISQRT_4(base)                     \
    flow_isqrt_step(&rem, &root, (base));      \
    flow_isqrt_step(&rem, &root, (base) - 2U); \
    flow_isqrt_step(&rem, &root, (base) - 4U); \
    flow_isqrt_step(&rem, &root, (base) - 6U)
    FLOW_ISQRT_4(62U);
    FLOW_ISQRT_4(54U);
    FLOW_ISQRT_4(46U);
    FLOW_ISQRT_4(38U);
    FLOW_ISQRT_4(30U);
    FLOW_ISQRT_4(22U);
    FLOW_ISQRT_4(14U);
    FLOW_ISQRT_4(6U);
#undef FLOW_ISQRT_4
    return root;
}

/* Exact Q48.16 square root without 128-bit runtime helpers */
static inline uint64_t flow_qsqrt_u64(uint64_t x_q16) {
    uint64_t hi_range = flow_mask_bool((x_q16 >> 48U) != 0ULL);
    uint64_t arg = flow_mux_u64(hi_range, x_q16, x_q16 << FLOW_Q16_SHIFT);
    uint64_t r = flow_isqrt_u64(arg);
    return flow_mux_u64(hi_range, r << (FLOW_Q16_SHIFT / 2U), r);
}

/* --- Division-Free Goldilocks F_p (p = 2^64 - 2^32 + 1) Sponge Seal --- */

static inline uint64_t flow_fp_reduce64(uint64_t x) {
    uint64_t ge = flow_mask_bool(x >= FLOW_FP_MOD);
    return x - (FLOW_FP_MOD & ge);
}

static inline uint64_t flow_fp_add(uint64_t a, uint64_t b) {
    uint64_t s = 0ULL;
    uint64_t c = (uint64_t)__builtin_add_overflow(a, b, &s);
    uint64_t folded = s + (UINT64_C(0xFFFFFFFF) & (UINT64_C(0) - c));
    return flow_fp_reduce64(folded);
}

static inline uint64_t flow_fp_sub(uint64_t a, uint64_t b) {
    uint64_t lt = flow_mask_bool(a < b);
    return (a - b) + (FLOW_FP_MOD & lt);
}

static inline uint64_t flow_fp_mul(uint64_t a, uint64_t b) {
    uint64_t z_hi = 0ULL;
    uint64_t z_lo = flow_umul128(a, b, &z_hi);
    uint64_t h0 = z_hi & UINT64_C(0xFFFFFFFF);
    uint64_t h1 = z_hi >> 32U;
    uint64_t t0 = flow_fp_reduce64(z_lo);
    uint64_t t1 = flow_fp_sub(t0, h1);
    uint64_t v = h0 * UINT64_C(0xFFFFFFFF);
    return flow_fp_add(t1, v);
}

static inline uint64_t flow_fp_sbox7(uint64_t x) {
    uint64_t x2 = flow_fp_mul(x, x);
    uint64_t x3 = flow_fp_mul(x2, x);
    uint64_t x6 = flow_fp_mul(x3, x3);
    return flow_fp_mul(x6, x);
}

static inline uint64_t flow_fp_absorb(uint64_t st, uint64_t w, uint64_t idx1) {
    uint64_t w_lo = flow_fp_reduce64(w);
    uint64_t w_hi = (uint64_t)(w >= FLOW_FP_MOD);
    uint64_t s1 = flow_fp_sbox7(flow_fp_add(st, w_lo));
    return flow_fp_add(flow_fp_mul(s1, FLOW_GOLDEN_64), w_hi + idx1);
}

static inline uint64_t flow_seal_cell(flow_cell c) {
    uint64_t st = FLOW_GOLDEN_64;
    st = flow_fp_absorb(st, c[0], 1ULL);
    st = flow_fp_absorb(st, c[1], 2ULL);
    st = flow_fp_absorb(st, c[2], 3ULL);
    st = flow_fp_absorb(st, c[3], 4ULL);
    st = flow_fp_absorb(st, c[4], 5ULL);
    st = flow_fp_absorb(st, c[5], 6ULL);
    st = flow_fp_absorb(st, c[6], 7ULL);
    st = flow_fp_sbox7(st);
    return st | (uint64_t)(st == 0ULL);
}

/* --- Overflow-Capturing Q48.16 Fixed-Point Algebra --- */

static inline int64_t flow_qadd_i64(int64_t a, int64_t b, uint32_t *ovf) {
    int64_t res = 0;
    *ovf |= (uint32_t)__builtin_add_overflow(a, b, &res);
    return res;
}

static inline int64_t flow_qsub_i64(int64_t a, int64_t b, uint32_t *ovf) {
    int64_t res = 0;
    *ovf |= (uint32_t)__builtin_sub_overflow(a, b, &res);
    return res;
}

static inline int64_t flow_qneg_i64(int64_t x, uint32_t *ovf) {
    return flow_qsub_i64(INT64_C(0), x, ovf);
}

static inline int64_t flow_qmul_i64(int64_t a, int64_t b, uint32_t *ovf) {
    uint64_t ua = (uint64_t)a;
    uint64_t ub = (uint64_t)b;
    uint64_t uhi = 0ULL;
    uint64_t ulo = flow_umul128(ua, ub, &uhi);
    uint64_t hi = uhi - (ub & flow_mask_bool(a < 0)) - (ua & flow_mask_bool(b < 0));
    uint64_t shifted = (ulo >> FLOW_Q16_SHIFT) | (hi << (64U - FLOW_Q16_SHIFT));
    int64_t res = (int64_t)shifted;
    uint64_t sign_fill = flow_mask_bool(res < 0);
    *ovf |= (uint32_t)(((hi ^ sign_fill) >> 15U) != 0ULL);
    return res;
}

static inline int64_t flow_qshr1_i64(int64_t x) {
    uint64_t ux = (uint64_t)x;
    uint64_t sign_bit = (ux >> 63U) << 63U;
    return (int64_t)((ux >> 1U) | sign_bit);
}

static inline int64_t flow_qdiv_i64(int64_t num_q16, int64_t den_q16, uint32_t *ovf) {
    uint64_t anum = flow_abs_i64(num_q16);
    uint64_t aden = flow_abs_i64(den_q16);
    uint64_t den_zero = flow_mask_bool(aden == 0ULL);
    uint64_t safe_den = aden | (den_zero & 1ULL);
    uint64_t hi_num = flow_mask_bool((anum >> 47U) != 0ULL);
    uint64_t q_lo = (anum << FLOW_Q16_SHIFT) / safe_den;
    uint64_t safe_den_hi = (safe_den >> FLOW_Q16_SHIFT) | 1ULL;
    uint64_t q_hi = anum / safe_den_hi;
    uint64_t q_mag = flow_mux_u64(hi_num, q_hi, q_lo);
    uint64_t mag_ovf = flow_mask_bool(q_mag > (uint64_t)INT64_MAX);
    *ovf |= (uint32_t)((mag_ovf | den_zero) != 0ULL);
    uint64_t clamped = flow_mux_u64(mag_ovf, (uint64_t)INT64_MAX, q_mag);
    uint64_t neg = flow_mask_bool((num_q16 < 0) != (den_q16 < 0));
    return (int64_t)((clamped ^ neg) - neg);
}

/* --- Universal Branchless HoTT x 2-Jet Transition Law Phi --- */

static inline flow_cell flow_step(flow_cell state, flow_cell bound) {
    uint32_t ovf = 0U;

    /* 1. Seal & Univalence Domain Gate */
    uint64_t exp_seal = flow_seal_cell(bound);
    bool seal_ok = (bound[7] != 0ULL) & (bound[7] == exp_seal);
    bool dom_ok = ((state[5] ^ bound[5]) >> 32U) == 0ULL;

    int64_t b0 = (int64_t)bound[0];
    int64_t b1 = (int64_t)bound[1];
    int64_t b2 = (int64_t)bound[2];
    uint64_t kan_filter = bound[3];
    int64_t dt_max = (int64_t)bound[4];
    uint32_t req_sieve = (uint32_t)(bound[6] & UINT64_C(0xFFFFFFFF));

    bool horizon_ok = (dt_max > 0);
    int64_t safe_dt_max = flow_mux_i64(flow_mask_bool(horizon_ok), dt_max, INT64_C(1));

    /* 2. 2-Jet Fiber Pullback (Operator mode vs Autonomous fiber mode) */
    int64_t x = (int64_t)state[3];
    uint64_t op_mask = flow_mask_bool((b1 != 0) | (b2 != 0));

    int64_t x_sq = flow_qmul_i64(x, x, &ovf);
    int64_t quad_term = flow_qshr1_i64(flow_qmul_i64(b2, x_sq, &ovf));
    int64_t lin_term = flow_qmul_i64(b1, x, &ovf);
    int64_t g0 = flow_qadd_i64(b0, flow_qadd_i64(lin_term, quad_term, &ovf), &ovf);
    int64_t g1 = flow_qadd_i64(b1, flow_qmul_i64(b2, x, &ovf), &ovf);

    uint32_t auto_ovf = 0U;
    int64_t f0 = flow_qsub_i64((int64_t)state[0], b0, &auto_ovf);
    ovf = (uint32_t)flow_mux_u64(op_mask, ovf, auto_ovf);

    int64_t r0 = flow_mux_i64(op_mask, g0, f0);
    int64_t r1 = flow_mux_i64(op_mask, g1, (int64_t)state[1]);
    int64_t r2 = flow_mux_i64(op_mask, b2, (int64_t)state[2]);

    /* 3. Branchless 2-Jet Citardauq Root & Wick-Rotated Curvature Tunneling */
    uint32_t disc_ovf = 0U;
    int64_t r1_sq = flow_qmul_i64(r1, r1, &disc_ovf);
    int64_t r0_r2 = flow_qmul_i64(r0, r2, &disc_ovf);
    int64_t two_r0_r2 = flow_qadd_i64(r0_r2, r0_r2, &disc_ovf);
    int64_t disc = flow_qsub_i64(r1_sq, two_r0_r2, &disc_ovf);

    int64_t d_mag = (int64_t)flow_qsqrt_u64(flow_abs_i64(disc));
    int64_t neg_d_mag = flow_qneg_i64(d_mag, &disc_ovf);
    int64_t signed_d = flow_mux_i64(flow_mask_bool(r1 < 0), neg_d_mag, d_mag);
    int64_t q_cit = flow_qadd_i64(r1, signed_d, &disc_ovf);

    uint32_t div_ovf_a = 0U;
    uint32_t div_ovf_b = 0U;
    uint32_t div_ovf_l = 0U;
    uint32_t div_ovf_t = 0U;
    int64_t neg_two_r0 = flow_qneg_i64(flow_qadd_i64(r0, r0, &div_ovf_a), &div_ovf_a);
    int64_t root_a = flow_qdiv_i64(neg_two_r0, q_cit, &div_ovf_a);
    int64_t root_b = flow_qdiv_i64(flow_qneg_i64(q_cit, &div_ovf_b), r2, &div_ovf_b);
    uint64_t pick_a = flow_mask_bool(flow_abs_i64(root_a) <= flow_abs_i64(root_b));
    int64_t step_quad = flow_mux_i64(pick_a, root_a, root_b);

    int64_t step_lin = flow_qdiv_i64(flow_qneg_i64(r0, &div_ovf_l), r1, &div_ovf_l);

    int32_t mom_prev = (int32_t)(state[5] & UINT64_C(0xFFFFFFFF));
    int64_t mom_dir = flow_mux_i64(flow_mask_bool(mom_prev < 0), -INT64_C(1), INT64_C(1));
    int64_t grad_dir = flow_mux_i64(flow_mask_bool(r1 > 0), -INT64_C(1), INT64_C(1));
    int64_t tun_dir = flow_mux_i64(flow_mask_bool(r1 == 0), mom_dir, grad_dir);
    int64_t tun_d = flow_mux_i64(flow_mask_bool(tun_dir < 0), neg_d_mag, d_mag);
    int64_t tun_num = flow_qadd_i64(flow_qneg_i64(r1, &div_ovf_t), tun_d, &div_ovf_t);
    int64_t abs_r2_i64 =
        (int64_t)flow_mux_u64(flow_mask_bool(flow_abs_i64(r2) > (uint64_t)INT64_MAX),
                              (uint64_t)INT64_MAX, flow_abs_i64(r2));
    int64_t step_tunnel = flow_qdiv_i64(tun_num, abs_r2_i64, &div_ovf_t);

    uint64_t has_r2 = flow_mask_bool(r2 != 0);
    uint64_t real_roots = flow_mask_bool(disc >= 0);
    int64_t raw_step =
        flow_mux_i64(has_r2, flow_mux_i64(real_roots, step_quad, step_tunnel), step_lin);

    /* Non-zero progress (>= 1 LSB) whenever residual r0 != 0 and (r1, r2) != (0, 0) */
    uint64_t active = flow_mask_bool((r0 != 0) & ((r1 != 0) | (r2 != 0)));
    uint64_t zero_quant = active & flow_mask_bool(raw_step == 0);
    int64_t nz_step =
        flow_mux_i64(~active, INT64_C(0), flow_mux_i64(zero_quant, tun_dir, raw_step));

    /* Clamp displacement magnitude to [0, safe_dt_max] */
    uint64_t raw_abs = flow_abs_i64(nz_step);
    uint64_t cap_mask = flow_mask_bool(raw_abs > (uint64_t)safe_dt_max);
    int64_t neg_dt_max = flow_qneg_i64(safe_dt_max, &ovf);
    int64_t signed_cap = flow_mux_i64(flow_mask_bool(nz_step < 0), neg_dt_max, safe_dt_max);
    int64_t delta = flow_mux_i64(cap_mask, signed_cap, nz_step);
    int64_t dt_resolved = (int64_t)flow_abs_i64(delta);

    /* 4. HoTT Cubical Kan Transport, Taylor Pushforward & Sieve Commit */
    int64_t x_cand_i64 = flow_qadd_i64(x, delta, &ovf);
    uint64_t x_orig = state[3];
    uint64_t x_cand = (uint64_t)x_cand_i64;
    bool kan_ok = (((x_orig ^ x_cand) & kan_filter) == 0ULL);
    uint64_t x_proj = (x_cand & ~kan_filter) | (x_orig & kan_filter);

    int64_t xp = (int64_t)x_proj;
    int64_t xp_sq = flow_qmul_i64(xp, xp, &ovf);
    int64_t j0_op =
        flow_qadd_i64(b0,
                      flow_qadd_i64(flow_qmul_i64(b1, xp, &ovf),
                                    flow_qshr1_i64(flow_qmul_i64(b2, xp_sq, &ovf)), &ovf),
                      &ovf);
    int64_t j1_op = flow_qadd_i64(b1, flow_qmul_i64(b2, xp, &ovf), &ovf);

    uint32_t push_ovf = 0U;
    int64_t d_sq = flow_qmul_i64(delta, delta, &push_ovf);
    int64_t j0_au =
        flow_qadd_i64((int64_t)state[0],
                      flow_qadd_i64(flow_qmul_i64(r1, delta, &push_ovf),
                                    flow_qshr1_i64(flow_qmul_i64(r2, d_sq, &push_ovf)), &push_ovf),
                      &push_ovf);
    int64_t j1_au =
        flow_qadd_i64((int64_t)state[1], flow_qmul_i64(r2, delta, &push_ovf), &push_ovf);
    ovf |= (uint32_t)flow_mux_u64(op_mask, 0ULL, push_ovf);

    int64_t j0_next = flow_mux_i64(op_mask, j0_op, j0_au);
    int64_t j1_next = flow_mux_i64(op_mask, j1_op, j1_au);
    int64_t j2_next = r2;
    uint32_t conv_ovf = 0U;
    int64_t res_after = flow_mux_i64(op_mask, j0_next, flow_qsub_i64(j0_next, b0, &conv_ovf));
    bool converged =
        ((ovf | conv_ovf) == 0U) & (flow_abs_i64(res_after) <= (uint64_t)FLOW_CONVERGE_EPS_Q16);

    uint32_t live_sieve = ((uint32_t)seal_ok * FLOW_SIEVE_SEAL_VALID) |
                          ((uint32_t)dom_ok * FLOW_SIEVE_DOMAIN_MATCH) |
                          ((uint32_t)(ovf == 0U) * FLOW_SIEVE_NO_OVERFLOW) |
                          ((uint32_t)kan_ok * FLOW_SIEVE_KAN_HOMOTOPIC) |
                          ((uint32_t)horizon_ok * FLOW_SIEVE_HORIZON_VALID) |
                          ((uint32_t)converged * FLOW_SIEVE_CONVERGED);

    uint32_t eff_req = req_sieve | FLOW_SIEVE_BASE_GATE;
    uint64_t commit = flow_mask_bool((live_sieve & eff_req) == eff_req);

    int32_t mom_next = (int32_t)flow_mux_i64(
        flow_mask_bool(delta != 0),
        flow_mux_i64(flow_mask_bool(delta < 0), -INT64_C(65536), INT64_C(65536)), mom_prev);
    uint64_t aux_next = (state[5] & UINT64_C(0xFFFFFFFF00000000)) | ((uint64_t)(uint32_t)mom_next);

    uint32_t epoch_next = (uint32_t)(state[6] >> 32U) + 1U;
    uint64_t omega_next = ((uint64_t)epoch_next << 32U) | (uint64_t)live_sieve;

    flow_cell out = {
        flow_mux_u64(commit, (uint64_t)j0_next, state[0]),
        flow_mux_u64(commit, (uint64_t)j1_next, state[1]),
        flow_mux_u64(commit, (uint64_t)j2_next, state[2]),
        flow_mux_u64(commit, x_proj, state[3]),
        flow_mux_u64(commit, (uint64_t)dt_resolved, UINT64_C(0)),
        flow_mux_u64(commit, aux_next, state[5]),
        omega_next,
        UINT64_C(0),
    };
    out[7] = flow_seal_cell(out);
    return out;
}

/* --- Little-Endian 64B Capsule Wire Serialization & Lock-Free Arena Transport --- */

static inline uint64_t flow_load_le64(const uint8_t p[8]) {
    return ((uint64_t)p[0]) | ((uint64_t)p[1] << 8U) | ((uint64_t)p[2] << 16U) |
           ((uint64_t)p[3] << 24U) | ((uint64_t)p[4] << 32U) | ((uint64_t)p[5] << 40U) |
           ((uint64_t)p[6] << 48U) | ((uint64_t)p[7] << 56U);
}

static inline void flow_store_le64(uint8_t p[8], uint64_t v) {
    for (uint32_t b = 0U; b < 8U; ++b) {
        p[b] = (uint8_t)((v >> (8U * b)) & UINT64_C(0xFF));
    }
}

static inline flow_cell flow_cell_from_bytes(const uint8_t raw[64]) {
    flow_cell c = {
        flow_load_le64(&raw[0]),  flow_load_le64(&raw[8]),  flow_load_le64(&raw[16]),
        flow_load_le64(&raw[24]), flow_load_le64(&raw[32]), flow_load_le64(&raw[40]),
        flow_load_le64(&raw[48]), flow_load_le64(&raw[56]),
    };
    return c;
}

static inline void flow_cell_to_bytes(flow_cell c, uint8_t out[64]) {
    for (uint32_t i = 0U; i < 8U; ++i) {
        flow_store_le64(&out[i * 8U], c[i]);
    }
}

/* Z / 2^k Z masked Acquire/Release cell transport (SPSC stream + QSBR hot-swap) */
static inline void flow_arena_store_rel(flow_cell *arena, uint64_t idx, uint64_t cap_mask,
                                        flow_cell c) {
#if defined(__SIZEOF_POINTER__) && (__SIZEOF_POINTER__ >= 8)
    uint64_t *slot = (uint64_t *)&arena[idx & cap_mask];
    for (uint32_t i = 0U; i < 6U; ++i) {
        __atomic_store_n(&slot[i], c[i], __ATOMIC_RELAXED);
    }
    __atomic_store_n(&slot[7], c[7], __ATOMIC_RELAXED);
    __atomic_store_n(&slot[6], c[6], __ATOMIC_RELEASE);
#else
    volatile uint64_t *slot = (volatile uint64_t *)&arena[idx & cap_mask];
    for (uint32_t i = 0U; i < 6U; ++i) {
        slot[i] = c[i];
    }
    slot[7] = c[7];
    __atomic_thread_fence(__ATOMIC_RELEASE);
    slot[6] = c[6];
#endif
}

static inline flow_cell flow_arena_load_acq(const flow_cell *arena, uint64_t idx,
                                            uint64_t cap_mask) {
#if defined(__SIZEOF_POINTER__) && (__SIZEOF_POINTER__ >= 8)
    const uint64_t *slot = (const uint64_t *)&arena[idx & cap_mask];
    uint64_t w6 = __atomic_load_n(&slot[6], __ATOMIC_ACQUIRE);
    flow_cell c = {
        __atomic_load_n(&slot[0], __ATOMIC_RELAXED),
        __atomic_load_n(&slot[1], __ATOMIC_RELAXED),
        __atomic_load_n(&slot[2], __ATOMIC_RELAXED),
        __atomic_load_n(&slot[3], __ATOMIC_RELAXED),
        __atomic_load_n(&slot[4], __ATOMIC_RELAXED),
        __atomic_load_n(&slot[5], __ATOMIC_RELAXED),
        w6,
        __atomic_load_n(&slot[7], __ATOMIC_RELAXED),
    };
    return c;
#else
    const volatile uint64_t *slot = (const volatile uint64_t *)&arena[idx & cap_mask];
    uint64_t w6 = slot[6];
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    flow_cell c = {slot[0], slot[1], slot[2], slot[3], slot[4], slot[5], w6, slot[7]};
    return c;
#endif
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#ifdef __cplusplus
}
#endif

#endif /* FLOW_H */
