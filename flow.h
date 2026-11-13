#ifndef FLOW_H
#define FLOW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__SIZEOF_INT128__)
#error "FLOW requires a 64-bit target with compiler __int128 support."
#endif

/* ============================================================================
 * FLOW — Zero-Risk 2-Jet & HoTT Manifold Arithmetic Substrate (v6.0.0)
 * ============================================================================
 * Canonical 64-byte (8 x u64) General-Purpose Register (GPR) substrate.
 * - Zero structs. Zero SIMD/FPU instructions (-mgeneral-regs-only clean).
 * - Zero control-flow branches in kernel. Zero heap.
 * - Total 128-bit Guard-Bit Wide-Accumulator (zero overflow/zombie traps).
 * - Odd-symmetric nearest-integer rounding (exact gauge & mirror symmetry).
 * - Lossless 2^-49 sub-LSB autonomous 2-Jet phase conservation (zero drift).
 * ============================================================================ */

__extension__ typedef __int128 flow_i128;
__extension__ typedef unsigned __int128 flow_u128;

typedef uint64_t flow_cell[8];

#if defined(__GNUC__) || defined(__clang__)
#define FLOW_ALIGN64   __attribute__((aligned(64)))
#define FLOW_FORCE_INL static inline __attribute__((always_inline, unused))
#define FLOW_INL       static inline __attribute__((unused))
#else
#define FLOW_ALIGN64
#define FLOW_FORCE_INL static inline
#define FLOW_INL       static inline
#endif

_Static_assert(sizeof(flow_cell) == 64U, "flow_cell must occupy exactly 64 bytes");

#define FLOW_VERSION_MAJOR 6
#define FLOW_VERSION_MINOR 0
#define FLOW_VERSION_PATCH 0
#ifndef FLOW_VERSION_STRING
#define FLOW_VERSION_STRING "6.0.0"
#endif

#define FLOW_Q16_SHIFT        16U
#define FLOW_Q16_ONE          INT64_C(65536)
#define FLOW_MAX_Q16          INT64_C(0x7FFFFFFF)
#define FLOW_MAX_J0_Q16       (INT64_C(1) << 60)
#define FLOW_MAX_J1_Q16       (INT64_C(1) << 45)
#define FLOW_CONVERGE_EPS_Q16 INT64_C(4)
#define FLOW_ATLAS_MAX_CHARTS 256U

#define FLOW_STATUS_CONVERGED (1U << 0)

#ifdef FLOW_EXPORT_KERNEL
void flow_step(uint64_t out[8], const uint64_t state[8], const uint64_t bound[8]);
void flow_step_atlas(uint64_t out[8], const uint64_t state[8], const flow_cell *arena,
                     uint64_t cap_mask, int64_t u_ext, uint16_t mode_req);
#define FLOW_API
#else
#define FLOW_API static inline
#endif

/* --- Branchless GPR Bitwise Combinators --- */

FLOW_FORCE_INL uint64_t flow_mask_bool(bool cond) {
    return UINT64_C(0) - (uint64_t)cond;
}

FLOW_FORCE_INL uint64_t flow_mux_u64(uint64_t mask, uint64_t tr, uint64_t fa) {
    return (tr & mask) | (fa & ~mask);
}

FLOW_FORCE_INL int64_t flow_mux_i64(uint64_t mask, int64_t tr, int64_t fa) {
    return (int64_t)flow_mux_u64(mask, (uint64_t)tr, (uint64_t)fa);
}

FLOW_FORCE_INL flow_u128 flow_mux_u128(uint64_t mask, flow_u128 tr, flow_u128 fa) {
    flow_u128 m = ((flow_u128)mask << 64U) | (flow_u128)mask;
    return (tr & m) | (fa & ~m);
}

FLOW_FORCE_INL flow_i128 flow_mux_i128(uint64_t mask, flow_i128 tr, flow_i128 fa) {
    return (flow_i128)flow_mux_u128(mask, (flow_u128)tr, (flow_u128)fa);
}

FLOW_FORCE_INL uint64_t flow_abs_i64(int64_t x) {
    uint64_t ux = (uint64_t)x;
    uint64_t neg = flow_mask_bool(x < 0);
    return (ux ^ neg) - neg;
}

FLOW_FORCE_INL bool flow_neg_i128(flow_i128 x) {
    return ((int64_t)((flow_u128)x >> 64U)) < 0;
}

FLOW_FORCE_INL flow_u128 flow_abs_i128(flow_i128 x) {
    flow_u128 ux = (flow_u128)x;
    uint64_t m64 = flow_mask_bool(flow_neg_i128(x));
    flow_u128 neg = ((flow_u128)m64 << 64U) | (flow_u128)m64;
    return (ux ^ neg) - neg;
}

FLOW_FORCE_INL int64_t flow_clamp_sym(int64_t v, int64_t lim) {
    int64_t neg_lim = -lim;
    int64_t c1 = flow_mux_i64(flow_mask_bool(v > lim), lim, v);
    return flow_mux_i64(flow_mask_bool(c1 < neg_lim), neg_lim, c1);
}

/* Portable arithmetic right shift (two's complement floor) without signed-shift UB */
FLOW_FORCE_INL int64_t flow_ashr_i128_to_i64(flow_i128 x, uint32_t sh) {
    flow_u128 ux = (flow_u128)x;
    uint64_t m64 = flow_mask_bool(flow_neg_i128(x));
    flow_u128 fill = (((flow_u128)m64 << 64U) | (flow_u128)m64) << (128U - sh);
    return (int64_t)((ux >> sh) | fill);
}

FLOW_FORCE_INL int64_t flow_ashr_i64(int64_t x, uint32_t sh) {
    uint64_t ux = (uint64_t)x;
    uint64_t fill = flow_mask_bool(x < 0) << (64U - sh);
    return (int64_t)((ux >> sh) | fill);
}

/* Odd-symmetric nearest-integer right shift: sym_shr(-x, sh) == -sym_shr(x, sh) */
FLOW_FORCE_INL int64_t flow_sym_shr_i128(flow_i128 x, uint32_t sh) {
    flow_u128 ax = flow_abs_i128(x);
    flow_u128 half = (flow_u128)1ULL << (sh - 1U);
    uint64_t mag = (uint64_t)((ax + half) >> sh);
    uint64_t neg = flow_mask_bool(flow_neg_i128(x));
    return (int64_t)((mag ^ neg) - neg);
}

FLOW_FORCE_INL int64_t flow_sym_shr_i64(int64_t x, uint32_t sh) {
    uint64_t ax = flow_abs_i64(x);
    uint64_t half = UINT64_C(1) << (sh - 1U);
    uint64_t mag = (ax + half) >> sh;
    uint64_t neg = flow_mask_bool(x < 0);
    return (int64_t)((mag ^ neg) - neg);
}

/* --- 0.5-ULP Branchless 128-Bit Integer Square Root (for x < 2^126, 0 branches) --- */

FLOW_FORCE_INL void flow__isqrt_hi_step(uint64_t *rem, uint64_t *root, uint64_t *hi) {
    uint64_t r = (*rem << 2U) | (*hi >> 62U);
    *hi <<= 2U;
    uint64_t cand = (*root << 2U) | UINT64_C(1);
    uint64_t ge = flow_mask_bool(r >= cand);
    *rem = r - (cand & ge);
    *root = (*root << 1U) | (ge & UINT64_C(1));
}

FLOW_FORCE_INL void flow__isqrt_lo_step(uint64_t *rem, uint64_t *root, uint64_t *lo) {
    uint64_t rem_hi = *rem >> 62U;
    uint64_t r = (*rem << 2U) | (*lo >> 62U);
    *lo <<= 2U;
    uint64_t cand = (*root << 2U) | UINT64_C(1);
    uint64_t ge = flow_mask_bool(rem_hi != 0ULL) | flow_mask_bool(r >= cand);
    *rem = r - (cand & ge);
    *root = (*root << 1U) | (ge & UINT64_C(1));
}

FLOW_FORCE_INL uint64_t flow_isqrt_round_u128(flow_u128 x) {
    uint64_t hi = (uint64_t)(x >> 64U);
    uint64_t lo = (uint64_t)x;
    uint64_t rem = 0ULL;
    uint64_t root = 0ULL;
#define FLOW_ISQRT_HI8(r, q, h)         \
    flow__isqrt_hi_step((r), (q), (h)); \
    flow__isqrt_hi_step((r), (q), (h)); \
    flow__isqrt_hi_step((r), (q), (h)); \
    flow__isqrt_hi_step((r), (q), (h)); \
    flow__isqrt_hi_step((r), (q), (h)); \
    flow__isqrt_hi_step((r), (q), (h)); \
    flow__isqrt_hi_step((r), (q), (h)); \
    flow__isqrt_hi_step((r), (q), (h))
#define FLOW_ISQRT_LO8(r, q, l)         \
    flow__isqrt_lo_step((r), (q), (l)); \
    flow__isqrt_lo_step((r), (q), (l)); \
    flow__isqrt_lo_step((r), (q), (l)); \
    flow__isqrt_lo_step((r), (q), (l)); \
    flow__isqrt_lo_step((r), (q), (l)); \
    flow__isqrt_lo_step((r), (q), (l)); \
    flow__isqrt_lo_step((r), (q), (l)); \
    flow__isqrt_lo_step((r), (q), (l))
    FLOW_ISQRT_HI8(&rem, &root, &hi);
    FLOW_ISQRT_HI8(&rem, &root, &hi);
    FLOW_ISQRT_HI8(&rem, &root, &hi);
    FLOW_ISQRT_HI8(&rem, &root, &hi);
    FLOW_ISQRT_LO8(&rem, &root, &lo);
    FLOW_ISQRT_LO8(&rem, &root, &lo);
    FLOW_ISQRT_LO8(&rem, &root, &lo);
    FLOW_ISQRT_LO8(&rem, &root, &lo);
#undef FLOW_ISQRT_HI8
#undef FLOW_ISQRT_LO8
    return root + (uint64_t)(rem > root);
}

/* --- Odd-Symmetric Branchless 128-by-64 Restoring Divider (0 hardware udiv, 0 __divti3) --- */

FLOW_FORCE_INL void flow__udiv64_step(uint64_t *rem, uint64_t *lo, uint64_t den) {
    uint64_t rem_hi = *rem >> 63U;
    uint64_t r = (*rem << 1U) | (*lo >> 63U);
    uint64_t ge = flow_mask_bool(rem_hi != 0ULL) | flow_mask_bool(r >= den);
    *rem = r - (den & ge);
    *lo = (*lo << 1U) | (ge & 1ULL);
}

FLOW_FORCE_INL int64_t flow_sym_div128_clamped(flow_i128 num, flow_i128 den, int64_t dt_max) {
    flow_u128 anum = flow_abs_i128(num);
    uint64_t aden = (uint64_t)flow_abs_i128(den);
    uint64_t den_zero = flow_mask_bool(aden == 0ULL);
    uint64_t safe_den = aden | (den_zero & 1ULL);
    uint64_t hi = (uint64_t)(anum >> 64U);
    uint64_t lo = (uint64_t)anum;
    uint64_t pre_sat = den_zero | flow_mask_bool(hi >= safe_den);
    uint64_t rem = flow_mux_u64(pre_sat, 0ULL, hi);
    uint64_t quot = lo;
#define FLOW_UDIV64_8(r, q, d)        \
    flow__udiv64_step((r), (q), (d)); \
    flow__udiv64_step((r), (q), (d)); \
    flow__udiv64_step((r), (q), (d)); \
    flow__udiv64_step((r), (q), (d)); \
    flow__udiv64_step((r), (q), (d)); \
    flow__udiv64_step((r), (q), (d)); \
    flow__udiv64_step((r), (q), (d)); \
    flow__udiv64_step((r), (q), (d))
    FLOW_UDIV64_8(&rem, &quot, safe_den);
    FLOW_UDIV64_8(&rem, &quot, safe_den);
    FLOW_UDIV64_8(&rem, &quot, safe_den);
    FLOW_UDIV64_8(&rem, &quot, safe_den);
    FLOW_UDIV64_8(&rem, &quot, safe_den);
    FLOW_UDIV64_8(&rem, &quot, safe_den);
    FLOW_UDIV64_8(&rem, &quot, safe_den);
    FLOW_UDIV64_8(&rem, &quot, safe_den);
#undef FLOW_UDIV64_8
    uint64_t round_up = (~pre_sat & flow_mask_bool(rem > ((safe_den - 1ULL) >> 1U))) & UINT64_C(1);
    uint64_t cap = (uint64_t)dt_max;
    uint64_t over_cap = pre_sat | flow_mask_bool(quot >= cap);
    uint64_t mag = flow_mux_u64(over_cap, cap, quot + round_up);
    uint64_t neg = flow_mask_bool(flow_neg_i128(num) != flow_neg_i128(den));
    return (int64_t)((mag ^ neg) - neg);
}

/* --- Little-Endian 64B Wire Serialization & Lock-Free Arena Primitives --- */

FLOW_INL uint64_t flow_load_le64(const uint8_t p[8]) {
    return ((uint64_t)p[0]) | ((uint64_t)p[1] << 8U) | ((uint64_t)p[2] << 16U) |
           ((uint64_t)p[3] << 24U) | ((uint64_t)p[4] << 32U) | ((uint64_t)p[5] << 40U) |
           ((uint64_t)p[6] << 48U) | ((uint64_t)p[7] << 56U);
}

FLOW_INL void flow_store_le64(uint8_t p[8], uint64_t v) {
    for (uint32_t b = 0U; b < 8U; ++b) {
        p[b] = (uint8_t)((v >> (8U * b)) & UINT64_C(0xFF));
    }
}

FLOW_INL void flow_cell_from_bytes(uint64_t out[8], const uint8_t raw[64]) {
    for (uint32_t i = 0U; i < 8U; ++i) {
        out[i] = flow_load_le64(&raw[i * 8U]);
    }
}

FLOW_INL void flow_cell_to_bytes(const uint64_t c[8], uint8_t out[64]) {
    for (uint32_t i = 0U; i < 8U; ++i) {
        flow_store_le64(&out[i * 8U], c[i]);
    }
}

FLOW_FORCE_INL void flow_arena_store_rel(flow_cell *arena, uint64_t idx, uint64_t cap_mask,
                                         const uint64_t c[8]) {
    uint64_t *slot = &arena[idx & cap_mask][0];
    __atomic_store_n(&slot[0], c[0], __ATOMIC_RELAXED);
    __atomic_store_n(&slot[1], c[1], __ATOMIC_RELAXED);
    __atomic_store_n(&slot[2], c[2], __ATOMIC_RELAXED);
    __atomic_store_n(&slot[3], c[3], __ATOMIC_RELAXED);
    __atomic_store_n(&slot[4], c[4], __ATOMIC_RELAXED);
    __atomic_store_n(&slot[5], c[5], __ATOMIC_RELAXED);
    __atomic_store_n(&slot[7], c[7], __ATOMIC_RELAXED);
    __atomic_store_n(&slot[6], c[6], __ATOMIC_RELEASE);
}

FLOW_FORCE_INL void flow_arena_publish_bound(flow_cell *arena, uint64_t idx, uint64_t cap_mask,
                                             uint32_t epoch, const uint64_t bound[8]) {
    uint64_t tmp[8] = {
        bound[0],    bound[1], bound[2], bound[3], bound[4], bound[5], (uint64_t)epoch << 32U,
        UINT64_C(0),
    };
    flow_arena_store_rel(arena, idx, cap_mask, tmp);
}

FLOW_FORCE_INL void flow_arena_load_acq(uint64_t out[8], const flow_cell *arena, uint64_t idx,
                                        uint64_t cap_mask) {
    const uint64_t *slot = &arena[idx & cap_mask][0];
    uint64_t w6 = __atomic_load_n(&slot[6], __ATOMIC_ACQUIRE);
    out[0] = __atomic_load_n(&slot[0], __ATOMIC_RELAXED);
    out[1] = __atomic_load_n(&slot[1], __ATOMIC_RELAXED);
    out[2] = __atomic_load_n(&slot[2], __ATOMIC_RELAXED);
    out[3] = __atomic_load_n(&slot[3], __ATOMIC_RELAXED);
    out[4] = __atomic_load_n(&slot[4], __ATOMIC_RELAXED);
    out[5] = __atomic_load_n(&slot[5], __ATOMIC_RELAXED);
    out[6] = w6;
    out[7] = __atomic_load_n(&slot[7], __ATOMIC_RELAXED);
}

FLOW_INL bool flow_arena_try_swap(flow_cell *arena, uint64_t idx, uint64_t cap_mask,
                                  uint32_t new_epoch, const uint64_t cand[8]) {
    uint64_t cur[8];
    flow_arena_load_acq(cur, arena, idx, cap_mask);
    uint32_t cur_ep = (uint32_t)(cur[6] >> 32U);
    uint64_t n_lo = (cand[5] >> 24U) & UINT64_C(0xFF);
    uint64_t n_hi = (cand[5] >> 16U) & UINT64_C(0xFF);
    int32_t b_exit32 = (int32_t)(uint32_t)(cand[3] >> 32U);
    int32_t b_enter32 = (int32_t)(uint32_t)(cand[3] & UINT64_C(0xFFFFFFFF));
    int32_t dt_max32 = (int32_t)(uint32_t)(cand[4] >> 32U);

    bool ok = (new_epoch > cur_ep) & (n_lo <= cap_mask) & (n_hi <= cap_mask) & (dt_max32 > 0) &
              (b_exit32 <= b_enter32);
    uint64_t m = flow_mask_bool(ok);
    uint64_t cand_w6 = (uint64_t)new_epoch << 32U;
    uint64_t next_c[8] = {
        flow_mux_u64(m, cand[0], cur[0]), flow_mux_u64(m, cand[1], cur[1]),
        flow_mux_u64(m, cand[2], cur[2]), flow_mux_u64(m, cand[3], cur[3]),
        flow_mux_u64(m, cand[4], cur[4]), flow_mux_u64(m, cand[5], cur[5]),
        flow_mux_u64(m, cand_w6, cur[6]), UINT64_C(0),
    };
    flow_arena_store_rel(arena, idx, cap_mask, next_c);
    return ok;
}

/* --- Universal Total Branchless 2-Jet x HoTT Manifold Endomorphism --- */

FLOW_FORCE_INL void flow__step_chart_core(uint64_t out[8], const uint64_t state[8],
                                          const uint64_t bound[8], uint64_t cap_mask, int64_t u_ext,
                                          uint16_t mode_req) {
    /* 1. Total Symmetric Input Normalization (Zero Abort / Zero Overflow Construction) */
    int64_t raw_j0 = (int64_t)state[0];
    int64_t raw_j1 = (int64_t)state[1];
    int64_t raw_j2 = (int64_t)state[2];
    int64_t x = flow_clamp_sym((int64_t)state[3], FLOW_MAX_Q16);
    uint64_t s5 = state[5];
    uint64_t s6 = state[6];
    uint64_t s7 = state[7];

    int64_t b0 = flow_clamp_sym((int64_t)bound[0], FLOW_MAX_Q16);
    int64_t b1 = flow_clamp_sym((int64_t)bound[1], FLOW_MAX_Q16);
    int64_t b2 = flow_clamp_sym((int64_t)bound[2], FLOW_MAX_Q16);

    int64_t raw_exit = (int64_t)(int32_t)(uint32_t)(bound[3] >> 32U);
    int64_t raw_enter = (int64_t)(int32_t)(uint32_t)(bound[3] & UINT64_C(0xFFFFFFFF));
    int64_t b_exit = flow_clamp_sym(raw_exit, FLOW_MAX_Q16);
    int64_t enter_c = flow_clamp_sym(raw_enter, FLOW_MAX_Q16);
    int64_t b_enter = flow_mux_i64(flow_mask_bool(enter_c < b_exit), b_exit, enter_c);

    int64_t raw_dt = (int64_t)(int32_t)(uint32_t)(bound[4] >> 32U);
    int64_t dt_c = flow_clamp_sym(raw_dt, FLOW_MAX_Q16);
    int64_t dt_max = flow_mux_i64(flow_mask_bool(dt_c < INT64_C(1)), INT64_C(1), dt_c);

    int64_t raw_ug = (int64_t)(int32_t)(uint32_t)(bound[4] & UINT64_C(0xFFFFFFFF));
    int64_t u_gain = flow_clamp_sym(raw_ug, FLOW_MAX_Q16);
    int64_t u_in = flow_clamp_sym(u_ext, FLOW_MAX_Q16);

    uint64_t b5 = bound[5];
    uint16_t next_lo = (uint16_t)((b5 >> 24U) & UINT64_C(0xFF));
    uint16_t next_hi = (uint16_t)((b5 >> 16U) & UINT64_C(0xFF));
    uint16_t kan_lock = (uint16_t)(b5 & UINT64_C(0xFFFF));

    /* 2. Exact 2^-49 Guard-Bit Residual G0 & 2^-32 Tangent G1 */
    uint64_t op_mask = flow_mask_bool((b1 != 0) | (b2 != 0));
    int64_t ug_u = u_gain * u_in;
    flow_i128 b0_eff_q49 = ((flow_i128)b0 << 33U) - ((flow_i128)ug_u << 17U);

    int64_t b1_x = b1 * x;
    int64_t x_sq = x * x;
    flow_i128 g0_op = b0_eff_q49 + ((flow_i128)b1_x << 17U) + (flow_i128)b2 * (flow_i128)x_sq;
    int64_t g1_op = (b1 << 16U) + b2 * x;

    int64_t j0_in = flow_clamp_sym(raw_j0, FLOW_MAX_J0_Q16);
    int64_t j1_in = flow_clamp_sym(raw_j1, FLOW_MAX_J1_Q16);
    int64_t j2_in = flow_clamp_sym(raw_j2, FLOW_MAX_Q16);
    uint64_t sub_valid = flow_mask_bool((raw_j0 == j0_in) & (raw_j1 == j1_in));
    uint64_t j0_sub_in = (s7 & UINT64_C(0x1FFFFFFFF)) & sub_valid;
    uint64_t j1_sub_in = (s7 >> 48U) & sub_valid;

    flow_i128 j0_q49 = ((flow_i128)j0_in << 33U) + (flow_i128)j0_sub_in;
    int64_t j1_q32 = (j1_in << 16U) + (int64_t)j1_sub_in;
    flow_i128 g0_au = j0_q49 - b0_eff_q49;

    flow_i128 G0 = flow_mux_i128(op_mask, g0_op, g0_au);
    int64_t G1 = flow_mux_i64(op_mask, g1_op, j1_q32);
    int64_t R2 = flow_mux_i64(op_mask, b2, j2_in);

    /* 3. Unified 128-Bit Citardauq / Minimum-|R| Vertex / Linear Solver */
    flow_i128 disc128 = (flow_i128)G1 * (flow_i128)G1 - G0 * (flow_i128)R2;
    uint64_t real_roots = flow_mask_bool(!flow_neg_i128(disc128));
    flow_u128 disc_pos = flow_mux_u128(real_roots, (flow_u128)disc128, (flow_u128)0ULL);
    int64_t d_mag = (int64_t)flow_isqrt_round_u128(disc_pos);

    /* Gauge-invariant branch sign: flips under (G0, G1, R2) -> (-G0, -G1, -R2) even at G1 == 0 */
    uint64_t d_neg = flow_mask_bool((G1 < 0) | ((G1 == 0) & (R2 < 0)));
    int64_t signed_d = flow_mux_i64(d_neg, -d_mag, d_mag);
    flow_i128 Q_cit = (flow_i128)G1 + (flow_i128)signed_d;

    uint64_t has_r2 = flow_mask_bool(R2 != 0);
    flow_i128 neg_G0 = -G0;
    flow_i128 div_num = flow_mux_i128(has_r2 & ~real_roots, -(flow_i128)G1, neg_G0);
    flow_i128 div_den =
        flow_mux_i128(has_r2, flow_mux_i128(real_roots, Q_cit, (flow_i128)R2), (flow_i128)G1 << 1U);

    int64_t raw_step = flow_sym_div128_clamped(div_num, div_den, dt_max);
    uint64_t active = flow_mask_bool((G0 != (flow_i128)0) & ((G1 != 0) | (R2 != 0)));
    int64_t step_act = flow_mux_i64(active, raw_step, INT64_C(0));

    /* 4. Boundary-Consistent Coordinate Step & Exact 128-Bit Lyapunov Monotonicity Guard */
    int64_t xp_cand = flow_clamp_sym(x + step_act, FLOW_MAX_Q16);
    int64_t delta_cand = xp_cand - x;
    int64_t d_sq = delta_cand * delta_cand;
    flow_i128 push_q49 =
        (((flow_i128)G1 * (flow_i128)delta_cand) << 1U) + (flow_i128)R2 * (flow_i128)d_sq;
    flow_i128 G0_cand = G0 + push_q49;

    uint64_t lyap_ok = flow_mask_bool(flow_abs_i128(G0_cand) <= flow_abs_i128(G0));
    int64_t delta = flow_mux_i64(lyap_ok, delta_cand, INT64_C(0));
    int64_t xp = x + delta;
    uint64_t dt_resolved = flow_abs_i64(delta);

    flow_i128 actual_push_q49 = flow_mux_i128(lyap_ok, push_q49, (flow_i128)0);
    flow_i128 G0_post = G0 + actual_push_q49;
    int64_t G1_post = G1 + R2 * delta;

    /* 5. Exact Post-Step 2-Jet State & Lossless Autonomous Sub-LSB Phase Conservation */
    int64_t j0_op_out = flow_sym_shr_i128(G0_post, 33U);
    int64_t j1_op_out = flow_sym_shr_i64(G1_post, 16U);

    flow_i128 J0_au_next = G0_post + b0_eff_q49;
    int64_t j0_au_out = flow_clamp_sym(flow_ashr_i128_to_i64(J0_au_next, 33U), FLOW_MAX_J0_Q16);
    int64_t j1_au_out = flow_clamp_sym(flow_ashr_i64(G1_post, 16U), FLOW_MAX_J1_Q16);
    uint64_t j0_sub_out = (uint64_t)J0_au_next & UINT64_C(0x1FFFFFFFF);
    uint64_t j1_sub_out = (uint64_t)G1_post & UINT64_C(0xFFFF);
    uint64_t sub_pack = (j1_sub_out << 48U) | j0_sub_out;

    int64_t j0_next = flow_mux_i64(op_mask, j0_op_out, j0_au_out);
    int64_t j1_next = flow_mux_i64(op_mask, j1_op_out, j1_au_out);
    uint64_t sub_next = flow_mux_u64(op_mask, UINT64_C(0), sub_pack);

    /* 6. Unconditional Cubical Kan Projection & Schmitt Hysteresis Gluing */
    uint16_t chart_orig = (uint16_t)((s5 >> 16U) & UINT64_C(0xFFFF));
    uint16_t mode_orig = (uint16_t)(s5 & UINT64_C(0xFFFF));
    uint16_t mode_proj = (uint16_t)((mode_req & (uint16_t)(~kan_lock)) | (mode_orig & kan_lock));

    uint64_t go_lo = flow_mask_bool(xp < b_exit);
    uint64_t go_hi = flow_mask_bool(xp > b_enter);
    uint64_t raw_chart = flow_mux_u64(go_lo, (uint64_t)next_lo, (uint64_t)chart_orig);
    raw_chart = flow_mux_u64(go_hi, (uint64_t)next_hi, raw_chart);
    uint16_t chart_next = (uint16_t)(raw_chart & cap_mask & UINT64_C(0xFFFF));

    bool converged = flow_abs_i128(G0_post) <= ((flow_u128)(uint64_t)FLOW_CONVERGE_EPS_Q16 << 33U);
    uint32_t status_next = (uint32_t)converged * FLOW_STATUS_CONVERGED;
    uint32_t epoch_next = (uint32_t)(s6 >> 32U) + 1U;

    out[0] = (uint64_t)j0_next;
    out[1] = (uint64_t)j1_next;
    out[2] = (uint64_t)R2;
    out[3] = (uint64_t)xp;
    out[4] = dt_resolved;
    out[5] = ((uint64_t)chart_next << 16U) | (uint64_t)mode_proj;
    out[6] = ((uint64_t)epoch_next << 32U) | (uint64_t)status_next;
    out[7] = sub_next;
}

FLOW_API void flow_step(uint64_t out[8], const uint64_t state[8], const uint64_t bound[8]) {
    uint16_t cur_mode = (uint16_t)(state[5] & UINT64_C(0xFFFF));
    flow__step_chart_core(out, state, bound, UINT64_C(0xFFFF), INT64_C(0), cur_mode);
}

FLOW_API void flow_step_atlas(uint64_t out[8], const uint64_t state[8], const flow_cell *arena,
                              uint64_t cap_mask, int64_t u_ext, uint16_t mode_req) {
    uint64_t chart_idx = (state[5] >> 16U) & UINT64_C(0xFFFF);
    const uint64_t *slot = &arena[chart_idx & cap_mask][0];
    (void)__atomic_load_n(&slot[6], __ATOMIC_ACQUIRE);
    flow__step_chart_core(out, state, slot, cap_mask, u_ext, mode_req);
}

#ifdef __cplusplus
}
#endif

#endif /* FLOW_H */
