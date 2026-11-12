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
 * FLOW — Object-Free HoTT & 2-Jet Manifold Execution Substrate
 * ============================================================================
 * Canonical 64-byte (512-bit, 8 x u64) register vector substrate.
 * Zero structs. Zero control-flow branches in kernel. Zero float. Zero heap.
 * Both State and Chart cells are minted & sealed upstream by Flow.lean.
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
#define FLOW_ATLAS_MAX_CHARTS    256U

#define FLOW_FP_MOD              UINT64_C(0xFFFFFFFF00000001)
#define FLOW_GOLDEN_64           UINT64_C(0x9E3779B97F4A7C15)
#define FLOW_BOUND_SEED          UINT64_C(0x464C4F57424E4435) /* "FLOWBND5" */
#define FLOW_STATE_SEED          UINT64_C(0x464C4F5753544135) /* "FLOWSTA5" */

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

static inline uint64_t flow_qsqrt_u64(uint64_t x_q16) {
    uint64_t hi_range = flow_mask_bool((x_q16 >> 48U) != 0ULL);
    uint64_t arg = flow_mux_u64(hi_range, x_q16, x_q16 << FLOW_Q16_SHIFT);
    uint64_t r = flow_isqrt_u64(arg);
    return flow_mux_u64(hi_range, r << (FLOW_Q16_SHIFT / 2U), r);
}

/* --- Internal Division-Free Goldilocks F_p Sponge & Read-Only Seal Verifiers --- */

static inline uint64_t flow__fp_reduce64(uint64_t x) {
    uint64_t ge = flow_mask_bool(x >= FLOW_FP_MOD);
    return x - (FLOW_FP_MOD & ge);
}

static inline uint64_t flow__fp_add(uint64_t a, uint64_t b) {
    uint64_t s = 0ULL;
    uint64_t c = (uint64_t)__builtin_add_overflow(a, b, &s);
    uint64_t folded = s + (UINT64_C(0xFFFFFFFF) & (UINT64_C(0) - c));
    return flow__fp_reduce64(folded);
}

static inline uint64_t flow__fp_sub(uint64_t a, uint64_t b) {
    uint64_t lt = flow_mask_bool(a < b);
    return (a - b) + (FLOW_FP_MOD & lt);
}

static inline uint64_t flow__fp_mul(uint64_t a, uint64_t b) {
    uint64_t z_hi = 0ULL;
    uint64_t z_lo = flow_umul128(a, b, &z_hi);
    uint64_t h0 = z_hi & UINT64_C(0xFFFFFFFF);
    uint64_t h1 = z_hi >> 32U;
    uint64_t t0 = flow__fp_reduce64(z_lo);
    uint64_t t1 = flow__fp_sub(t0, h1);
    uint64_t v = h0 * UINT64_C(0xFFFFFFFF);
    return flow__fp_add(t1, v);
}

static inline uint64_t flow__fp_sbox7(uint64_t x) {
    uint64_t x2 = flow__fp_mul(x, x);
    uint64_t x3 = flow__fp_mul(x2, x);
    uint64_t x6 = flow__fp_mul(x3, x3);
    return flow__fp_mul(x6, x);
}

static inline uint64_t flow__fp_absorb(uint64_t st, uint64_t w, uint64_t idx1) {
    uint64_t w_lo = flow__fp_reduce64(w);
    uint64_t w_hi = (uint64_t)(w >= FLOW_FP_MOD);
    uint64_t s1 = flow__fp_sbox7(flow__fp_add(st, w_lo));
    return flow__fp_add(flow__fp_mul(s1, FLOW_GOLDEN_64), w_hi + idx1);
}

static inline uint64_t flow__seal_lanes(uint64_t seed, uint64_t w0, uint64_t w1, uint64_t w2,
                                        uint64_t w3, uint64_t w4, uint64_t w5, uint64_t w6) {
    uint64_t st = seed;
    st = flow__fp_absorb(st, w0, 1ULL);
    st = flow__fp_absorb(st, w1, 2ULL);
    st = flow__fp_absorb(st, w2, 3ULL);
    st = flow__fp_absorb(st, w3, 4ULL);
    st = flow__fp_absorb(st, w4, 5ULL);
    st = flow__fp_absorb(st, w5, 6ULL);
    st = flow__fp_absorb(st, w6, 7ULL);
    st = flow__fp_sbox7(st);
    return st | (uint64_t)(st == 0ULL);
}

/* Read-only seal verifiers: C never mints or re-signs Chart or State seals */
static inline bool flow_verify_bound(flow_cell b) {
    uint64_t exp = flow__seal_lanes(FLOW_BOUND_SEED, b[0], b[1], b[2], b[3], b[4], b[5],
                                    b[6] & UINT64_C(0xFFFFFFFF));
    return (b[7] != 0ULL) & (b[7] == exp);
}

static inline bool flow_verify_state(flow_cell s) {
    uint64_t exp = flow__seal_lanes(FLOW_STATE_SEED, s[0], s[1], s[2], s[3], s[4], s[5], s[6]);
    return (s[7] != 0ULL) & (s[7] == exp);
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

/* Publishes a Lean-sealed chart with a new runtime Epoch WITHOUT re-signing bound[7] */
static inline void flow_arena_publish_bound(flow_cell *arena, uint64_t idx, uint64_t cap_mask,
                                            uint32_t epoch, flow_cell bound) {
    uint64_t req = bound[6] & UINT64_C(0xFFFFFFFF);
    bound[6] = ((uint64_t)epoch << 32U) | req;
    flow_arena_store_rel(arena, idx, cap_mask, bound);
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

/* --- Universal Branchless HoTT x 2-Jet Manifold Atlas Endomorphism Phi --- */

static inline flow_cell flow_step_chart(flow_cell state, flow_cell bound, uint64_t cap_mask,
                                        int64_t u_ext, uint16_t mode_req) {
    uint32_t ovf = 0U;

    /* 1. Dual Domain-Separated Seal, Univalence & Chart Well-Formedness Gate */
    bool state_seal_ok = flow_verify_state(state);
    bool bound_seal_ok = flow_verify_bound(bound);
    bool seal_ok = state_seal_ok & bound_seal_ok;
    bool dom_ok = ((state[5] ^ bound[5]) >> 32U) == 0ULL;

    int64_t b0 = (int64_t)bound[0];
    int64_t b1 = (int64_t)bound[1];
    int64_t b2 = (int64_t)bound[2];
    int64_t b_exit = (int64_t)(int32_t)(uint32_t)(bound[3] >> 32U);
    int64_t b_enter = (int64_t)(int32_t)(uint32_t)(bound[3] & UINT64_C(0xFFFFFFFF));
    int64_t dt_max = (int64_t)(int32_t)(uint32_t)(bound[4] >> 32U);
    int64_t u_gain = (int64_t)(int32_t)(uint32_t)(bound[4] & UINT64_C(0xFFFFFFFF));

    uint16_t next_lo = (uint16_t)((bound[5] >> 24U) & UINT64_C(0xFF));
    uint16_t next_hi = (uint16_t)((bound[5] >> 16U) & UINT64_C(0xFF));
    uint16_t kan_lock = (uint16_t)(bound[5] & UINT64_C(0xFFFF));
    uint32_t req_sieve = (uint32_t)(bound[6] & UINT64_C(0xFFFFFFFF));

    bool horizon_ok = (dt_max > 0) & (b_exit <= b_enter);
    int64_t safe_dt_max = flow_mux_i64(flow_mask_bool(dt_max > 0), dt_max, INT64_C(1));

    /* 2. External Signal Pullback & 2-Jet Residual Evaluation */
    int64_t u_scaled = flow_qmul_i64(u_gain, u_ext, &ovf);
    int64_t b0_eff = flow_qsub_i64(b0, u_scaled, &ovf);

    int64_t x = (int64_t)state[3];
    uint64_t op_mask = flow_mask_bool((b1 != 0) | (b2 != 0));

    uint32_t op_ovf = 0U;
    int64_t x_sq = flow_qmul_i64(x, x, &op_ovf);
    int64_t quad_term = flow_qshr1_i64(flow_qmul_i64(b2, x_sq, &op_ovf));
    int64_t lin_term = flow_qmul_i64(b1, x, &op_ovf);
    int64_t g0 = flow_qadd_i64(b0_eff, flow_qadd_i64(lin_term, quad_term, &op_ovf), &op_ovf);
    int64_t g1 = flow_qadd_i64(b1, flow_qmul_i64(b2, x, &op_ovf), &op_ovf);

    uint32_t auto_ovf = 0U;
    int64_t f0 = flow_qsub_i64((int64_t)state[0], b0_eff, &auto_ovf);
    ovf |= (uint32_t)flow_mux_u64(op_mask, op_ovf, auto_ovf);

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

    int64_t grad_dir = flow_mux_i64(flow_mask_bool(r1 > 0), -INT64_C(1), INT64_C(1));
    int64_t tun_d = flow_mux_i64(flow_mask_bool(grad_dir < 0), neg_d_mag, d_mag);
    int64_t tun_num = flow_qadd_i64(flow_qneg_i64(r1, &div_ovf_t), tun_d, &div_ovf_t);
    int64_t abs_r2_i64 =
        (int64_t)flow_mux_u64(flow_mask_bool(flow_abs_i64(r2) > (uint64_t)INT64_MAX),
                              (uint64_t)INT64_MAX, flow_abs_i64(r2));
    int64_t step_tunnel = flow_qdiv_i64(tun_num, abs_r2_i64, &div_ovf_t);

    uint64_t has_r2 = flow_mask_bool(r2 != 0);
    uint64_t real_roots = flow_mask_bool(disc >= 0);
    int64_t raw_step =
        flow_mux_i64(has_r2, flow_mux_i64(real_roots, step_quad, step_tunnel), step_lin);

    uint64_t active = flow_mask_bool((r0 != 0) & ((r1 != 0) | (r2 != 0)));
    uint64_t zero_quant = active & flow_mask_bool(raw_step == 0);
    int64_t nz_step =
        flow_mux_i64(~active, INT64_C(0), flow_mux_i64(zero_quant, grad_dir, raw_step));

    uint64_t raw_abs = flow_abs_i64(nz_step);
    uint64_t clamp_mask = flow_mask_bool(raw_abs > (uint64_t)safe_dt_max);
    int64_t neg_dt_max = flow_qneg_i64(safe_dt_max, &ovf);
    int64_t signed_cap = flow_mux_i64(flow_mask_bool(nz_step < 0), neg_dt_max, safe_dt_max);
    int64_t delta = flow_mux_i64(clamp_mask, signed_cap, nz_step);
    int64_t dt_resolved = (int64_t)flow_abs_i64(delta);

    /* 4. Pure Continuous Update, Orthogonal Kan Mode Projection & Schmitt Chart Gluing */
    int64_t xp = flow_qadd_i64(x, delta, &ovf);

    uint32_t post_op_ovf = 0U;
    int64_t xp_sq = flow_qmul_i64(xp, xp, &post_op_ovf);
    int64_t j0_op = flow_qadd_i64(
        b0_eff,
        flow_qadd_i64(flow_qmul_i64(b1, xp, &post_op_ovf),
                      flow_qshr1_i64(flow_qmul_i64(b2, xp_sq, &post_op_ovf)), &post_op_ovf),
        &post_op_ovf);
    int64_t j1_op = flow_qadd_i64(b1, flow_qmul_i64(b2, xp, &post_op_ovf), &post_op_ovf);

    uint32_t push_ovf = 0U;
    int64_t d_sq = flow_qmul_i64(delta, delta, &push_ovf);
    int64_t j0_au =
        flow_qadd_i64((int64_t)state[0],
                      flow_qadd_i64(flow_qmul_i64(r1, delta, &push_ovf),
                                    flow_qshr1_i64(flow_qmul_i64(r2, d_sq, &push_ovf)), &push_ovf),
                      &push_ovf);
    int64_t j1_au =
        flow_qadd_i64((int64_t)state[1], flow_qmul_i64(r2, delta, &push_ovf), &push_ovf);
    ovf |= (uint32_t)flow_mux_u64(op_mask, post_op_ovf, push_ovf);

    int64_t j0_next = flow_mux_i64(op_mask, j0_op, j0_au);
    int64_t j1_next = flow_mux_i64(op_mask, j1_op, j1_au);
    int64_t j2_next = r2;
    uint32_t conv_ovf = 0U;
    int64_t res_after = flow_mux_i64(op_mask, j0_next, flow_qsub_i64(j0_next, b0_eff, &conv_ovf));
    bool converged =
        ((ovf | conv_ovf) == 0U) & (flow_abs_i64(res_after) <= (uint64_t)FLOW_CONVERGE_EPS_Q16);

    uint16_t chart_orig = (uint16_t)((state[5] >> 16U) & UINT64_C(0xFFFF));
    uint16_t mode_orig = (uint16_t)(state[5] & UINT64_C(0xFFFF));
    bool kan_ok = (((uint16_t)(mode_orig ^ mode_req) & kan_lock) == 0U);
    uint16_t mode_proj = (uint16_t)((mode_req & (uint16_t)(~kan_lock)) | (mode_orig & kan_lock));

    uint64_t go_lo = flow_mask_bool(u_ext < b_exit);
    uint64_t go_hi = flow_mask_bool(u_ext > b_enter);
    uint64_t raw_chart = flow_mux_u64(go_lo, (uint64_t)next_lo, (uint64_t)chart_orig);
    raw_chart = flow_mux_u64(go_hi, (uint64_t)next_hi, raw_chart);
    uint16_t chart_next = (uint16_t)(raw_chart & cap_mask & UINT64_C(0xFFFF));

    uint32_t live_sieve = ((uint32_t)seal_ok * FLOW_SIEVE_SEAL_VALID) |
                          ((uint32_t)dom_ok * FLOW_SIEVE_DOMAIN_MATCH) |
                          ((uint32_t)(ovf == 0U) * FLOW_SIEVE_NO_OVERFLOW) |
                          ((uint32_t)kan_ok * FLOW_SIEVE_KAN_HOMOTOPIC) |
                          ((uint32_t)horizon_ok * FLOW_SIEVE_HORIZON_VALID) |
                          ((uint32_t)converged * FLOW_SIEVE_CONVERGED);

    uint32_t eff_req = req_sieve | FLOW_SIEVE_BASE_GATE;
    uint64_t commit = flow_mask_bool((live_sieve & eff_req) == eff_req);

    uint64_t aux_next = (state[5] & UINT64_C(0xFFFFFFFF00000000)) | ((uint64_t)chart_next << 16U) |
                        (uint64_t)mode_proj;
    uint32_t epoch_next = (uint32_t)(state[6] >> 32U) + 1U;
    uint64_t omega_next = ((uint64_t)epoch_next << 32U) | (uint64_t)live_sieve;

    flow_cell out = {
        flow_mux_u64(commit, (uint64_t)j0_next, state[0]),
        flow_mux_u64(commit, (uint64_t)j1_next, state[1]),
        flow_mux_u64(commit, (uint64_t)j2_next, state[2]),
        flow_mux_u64(commit, (uint64_t)xp, state[3]),
        flow_mux_u64(commit, (uint64_t)dt_resolved, UINT64_C(0)),
        flow_mux_u64(commit, aux_next, state[5]),
        omega_next,
        UINT64_C(0),
    };

    /* Anti-laundering: unsealed C-fabricated states never receive a valid state seal */
    uint64_t out_seal =
        flow__seal_lanes(FLOW_STATE_SEED, out[0], out[1], out[2], out[3], out[4], out[5], out[6]);
    out[7] = flow_mux_u64(flow_mask_bool(state_seal_ok), out_seal, UINT64_C(0));
    return out;
}

static inline flow_cell flow_step(flow_cell state, flow_cell bound) {
    uint16_t cur_mode = (uint16_t)(state[5] & UINT64_C(0xFFFF));
    return flow_step_chart(state, bound, UINT64_C(0xFFFF), INT64_C(0), cur_mode);
}

static inline flow_cell flow_step_atlas(flow_cell state, const flow_cell *arena, uint64_t cap_mask,
                                        int64_t u_ext, uint16_t mode_req) {
    uint64_t chart_idx = (state[5] >> 16U) & UINT64_C(0xFFFF);
    flow_cell bound = flow_arena_load_acq(arena, chart_idx, cap_mask);
    return flow_step_chart(state, bound, cap_mask, u_ext, mode_req);
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#ifdef __cplusplus
}
#endif

#endif /* FLOW_H */
