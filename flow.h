#ifndef FLOW_H
#define FLOW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__has_include)
#if __has_include("flow_opt_params.h")
#include "flow_opt_params.h"
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__SIZEOF_INT128__)
#error "FLOW requires a 64-bit target with compiler __int128 support."
#endif

/* ============================================================================
 * FLOW — Zero-Risk 2-Jet & HoTT Manifold Arithmetic Substrate (v7.0.0)
 * ============================================================================
 * Canonical 64-byte (8 x u64) General-Purpose Register (GPR) substrate.
 * - Zero structs. Zero SIMD/FPU instructions (-mgeneral-regs-only clean).
 * - Zero control-flow branches in kernel. Zero heap.
 * - Total 128-bit Guard-Bit Wide-Accumulator (zero overflow/zombie traps).
 * - Odd-symmetric nearest-integer rounding (exact gauge & mirror symmetry).
 * - Lossless 2^-49 sub-LSB autonomous 2-Jet phase conservation (zero drift).
 * - Self-synthesized 0-ULP Horner seeds via SEARU x FLOW 2-Jet (flow_opt_params.h).
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

#ifdef __cplusplus
static_assert(sizeof(flow_cell) == 64U, "flow_cell must occupy exactly 64 bytes");
#else
_Static_assert(sizeof(flow_cell) == 64U, "flow_cell must occupy exactly 64 bytes");
#endif

#define FLOW_VERSION_MAJOR 7
#define FLOW_VERSION_MINOR 0
#define FLOW_VERSION_PATCH 0
#ifndef FLOW_VERSION_STRING
#define FLOW_VERSION_STRING "7.0.0"
#endif

#define FLOW_Q16_SHIFT        16U
#define FLOW_Q16_ONE          INT64_C(65536)
#define FLOW_MAX_Q16          INT64_C(0x7FFFFFFF)
#define FLOW_MAX_J0_Q16       (INT64_C(1) << 60)
#define FLOW_MAX_J1_Q16       (INT64_C(1) << 45)
#define FLOW_CONVERGE_EPS_Q16 INT64_C(4)
#define FLOW_ATLAS_MAX_CHARTS 256U

#define FLOW_STATUS_CONVERGED (1U << 0)
#define FLOW_SWAP_LOCK_TAG    UINT32_C(0x464C4F57)

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
    uint64_t m = UINT64_C(0) - (uint64_t)cond;
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
    __asm__("" : "+r"(m));
#endif
    return m;
}

FLOW_FORCE_INL uint64_t flow_mux_u64(uint64_t mask, uint64_t tr, uint64_t fa) {
    return (tr & mask) | (fa & ~mask);
}

FLOW_FORCE_INL int64_t flow_mux_i64(uint64_t mask, int64_t tr, int64_t fa) {
    return (int64_t)flow_mux_u64(mask, (uint64_t)tr, (uint64_t)fa);
}

FLOW_FORCE_INL uint64_t *flow_mux_ptr_u64(uint64_t mask, uint64_t *tr, uint64_t *fa) {
    return (uint64_t *)(uintptr_t)flow_mux_u64(mask, (uint64_t)(uintptr_t)tr,
                                               (uint64_t)(uintptr_t)fa);
}

FLOW_FORCE_INL flow_u128 flow_mux_u128(uint64_t mask, flow_u128 tr, flow_u128 fa) {
    flow_u128 m = ((flow_u128)mask << 64U) | (flow_u128)mask;
    return (tr & m) | (fa & ~m);
}

FLOW_FORCE_INL flow_i128 flow_mux_i128(uint64_t mask, flow_i128 tr, flow_i128 fa) {
    return (flow_i128)flow_mux_u128(mask, (flow_u128)tr, (flow_u128)fa);
}

FLOW_FORCE_INL uint64_t flow_sel_u64(bool cond, uint64_t tr, uint64_t fa) {
#if defined(__x86_64__)
    return flow_mux_u64(flow_mask_bool(cond), tr, fa);
#else
    return cond ? tr : fa;
#endif
}

FLOW_FORCE_INL int64_t flow_sel_i64(bool cond, int64_t tr, int64_t fa) {
#if defined(__x86_64__)
    return flow_mux_i64(flow_mask_bool(cond), tr, fa);
#else
    return cond ? tr : fa;
#endif
}

FLOW_FORCE_INL flow_u128 flow_sel_u128(bool cond, flow_u128 tr, flow_u128 fa) {
#if defined(__x86_64__)
    return flow_mux_u128(flow_mask_bool(cond), tr, fa);
#else
    return cond ? tr : fa;
#endif
}

FLOW_FORCE_INL flow_i128 flow_sel_i128(bool cond, flow_i128 tr, flow_i128 fa) {
#if defined(__x86_64__)
    return flow_mux_i128(flow_mask_bool(cond), tr, fa);
#else
    return cond ? tr : fa;
#endif
}

FLOW_FORCE_INL uint64_t flow_abs_i64(int64_t x) {
    uint64_t ux = (uint64_t)x;
    return (x < 0) ? (UINT64_C(0) - ux) : ux;
}

FLOW_FORCE_INL bool flow_neg_i128(flow_i128 x) {
    return ((int64_t)((flow_u128)x >> 64U)) < 0;
}

FLOW_FORCE_INL flow_u128 flow_abs_i128(flow_i128 x) {
    flow_u128 ux = (flow_u128)x;
    return flow_neg_i128(x) ? ((flow_u128)0ULL - ux) : ux;
}

FLOW_FORCE_INL int64_t flow_clamp_sym(int64_t v, int64_t lim) {
    int64_t neg_lim = -lim;
    int64_t c1 = flow_sel_i64(v > lim, lim, v);
    return flow_sel_i64(c1 < neg_lim, neg_lim, c1);
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
    return flow_neg_i128(x) ? -(int64_t)mag : (int64_t)mag;
}

FLOW_FORCE_INL int64_t flow_sym_shr_i64(int64_t x, uint32_t sh) {
    uint64_t ax = flow_abs_i64(x);
    uint64_t half = UINT64_C(1) << (sh - 1U);
    uint64_t mag = (ax + half) >> sh;
    return (x < 0) ? -(int64_t)mag : (int64_t)mag;
}

/* --- SEARU-Synthesized Degree-3 Minimax Horner Seeds (0 LUT, 0 Cache Side-Channel) --- */

#ifndef FLOW_OPT_ISQRT_C0
#define FLOW_OPT_ISQRT_C0 UINT64_C(3037237717)
#define FLOW_OPT_ISQRT_C1 UINT64_C(1496413340)
#define FLOW_OPT_ISQRT_C2 UINT64_C(895914507)
#define FLOW_OPT_ISQRT_C3 UINT64_C(288003884)
#endif

#ifndef FLOW_OPT_RECIP_C0
#define FLOW_OPT_RECIP_C0 UINT64_C(4286839701)
#define FLOW_OPT_RECIP_C1 UINT64_C(4062939648)
#define FLOW_OPT_RECIP_C2 UINT64_C(2905977288)
#define FLOW_OPT_RECIP_C3 UINT64_C(981935668)
#endif

FLOW_FORCE_INL uint64_t flow__horner3_q32(uint64_t u16, uint64_t c0, uint64_t c1, uint64_t c2,
                                          uint64_t c3) {
    uint64_t t2 = c2 - ((u16 * c3) >> 16U);
    uint64_t t1 = c1 - ((u16 * t2) >> 16U);
    return c0 - ((u16 * t1) >> 16U);
}

/* --- 0-ULP Constant-Time Newton-Heron 128-Bit Integer Square Root (for x < 2^126) --- */

FLOW_FORCE_INL uint64_t flow_isqrt_round_u128(flow_u128 x) {
    uint64_t hi = (uint64_t)(x >> 64U);
    uint64_t lo = (uint64_t)x;

    bool z64 = (hi == UINT64_C(0));
    uint64_t w = flow_sel_u64(z64, lo, hi);
    uint64_t f = flow_sel_u64(z64, UINT64_C(0), lo);
    bool z_all = (w == UINT64_C(0));
    w = flow_sel_u64(z_all, UINT64_C(1) << 62U, w);
    uint64_t scale = flow_sel_u64(z64, UINT64_C(1) << 31U, UINT64_C(1) << 63U);

#define FLOW_NORM_SQRT_STAGE(SH2)                               \
    do {                                                        \
        bool z = ((w >> (64U - (SH2))) == UINT64_C(0));         \
        uint64_t w_sh = (w << (SH2)) | (f >> (64U - (SH2)));    \
        uint64_t f_sh = f << (SH2);                             \
        w = flow_sel_u64(z, w_sh, w);                           \
        f = flow_sel_u64(z, f_sh, f);                           \
        scale = flow_sel_u64(z, scale >> ((SH2) >> 1U), scale); \
    } while (0)
    FLOW_NORM_SQRT_STAGE(32U);
    FLOW_NORM_SQRT_STAGE(16U);
    FLOW_NORM_SQRT_STAGE(8U);
    FLOW_NORM_SQRT_STAGE(4U);
#undef FLOW_NORM_SQRT_STAGE

    uint64_t z2_m = flow_mask_bool((w >> 62U) == UINT64_C(0));
    w = flow_mux_u64(z2_m, (w << 2U) | (f >> 62U), w);
    f = flow_mux_u64(z2_m, f << 2U, f);
    scale += scale & (~z2_m);
    scale = flow_sel_u64(z_all, UINT64_C(0), scale);

    uint64_t oct0_m = flow_mask_bool((int64_t)w >= INT64_C(0));
    uint64_t s_w = w + (w & oct0_m);

    uint64_t idx16 = (s_w >> 47U) & UINT64_C(0xFFFF);
    uint64_t z0 = flow__horner3_q32(idx16, FLOW_OPT_ISQRT_C0, FLOW_OPT_ISQRT_C1, FLOW_OPT_ISQRT_C2,
                                    FLOW_OPT_ISQRT_C3);

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

/* --- 0-ULP Constant-Time Newton-Raphson 128-by-64 Clamped Divider (0 udiv, 0 __divti3) --- */

FLOW_FORCE_INL int64_t flow_sym_div128_clamped(flow_i128 num, flow_i128 den, int64_t dt_max) {
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

#define FLOW_NORM_DIV_STAGE(SH)                               \
    do {                                                      \
        bool z = ((d >> (64U - (SH))) == UINT64_C(0));        \
        uint64_t d_sh = d << (SH);                            \
        uint64_t u1_sh = (u1 << (SH)) | (u0 >> (64U - (SH))); \
        uint64_t u0_sh = u0 << (SH);                          \
        d = flow_sel_u64(z, d_sh, d);                         \
        u1 = flow_sel_u64(z, u1_sh, u1);                      \
        u0 = flow_sel_u64(z, u0_sh, u0);                      \
    } while (0)
    FLOW_NORM_DIV_STAGE(32U);
    FLOW_NORM_DIV_STAGE(16U);
    FLOW_NORM_DIV_STAGE(8U);
    FLOW_NORM_DIV_STAGE(4U);
    FLOW_NORM_DIV_STAGE(2U);
#undef FLOW_NORM_DIV_STAGE

    uint64_t z1_m = flow_mask_bool((int64_t)d >= INT64_C(0));
    u1 += (u1 + (u0 >> 63U)) & z1_m;
    d += d & z1_m;
    u0 += u0 & z1_m;

    uint64_t idx16 = (d >> 47U) & UINT64_C(0xFFFF);
    uint64_t r0 = flow__horner3_q32(idx16, FLOW_OPT_RECIP_C0, FLOW_OPT_RECIP_C1, FLOW_OPT_RECIP_C2,
                                    FLOW_OPT_RECIP_C3);

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
    uint64_t *slot = &arena[idx & cap_mask][0];
    uint64_t sink[8];
    uint64_t cur_w6 = __atomic_load_n(&slot[6], __ATOMIC_ACQUIRE);
    uint32_t cur_ep = (uint32_t)(cur_w6 >> 32U);
    uint32_t cur_tag = (uint32_t)(cur_w6 & UINT64_C(0xFFFFFFFF));
    uint64_t n_lo = (cand[5] >> 24U) & UINT64_C(0xFF);
    uint64_t n_hi = (cand[5] >> 16U) & UINT64_C(0xFF);
    int32_t b_exit32 = (int32_t)(uint32_t)(cand[3] >> 32U);
    int32_t b_enter32 = (int32_t)(uint32_t)(cand[3] & UINT64_C(0xFFFFFFFF));
    int32_t dt_max32 = (int32_t)(uint32_t)(cand[4] >> 32U);

    bool pre_ok = (cur_tag != FLOW_SWAP_LOCK_TAG) & (new_epoch > cur_ep) & (n_lo <= cap_mask) &
                  (n_hi <= cap_mask) & (dt_max32 > 0) & (b_exit32 <= b_enter32);
    uint64_t pre_m = flow_mask_bool(pre_ok);

    sink[6] = ~cur_w6;
    uint64_t *cas_dst = flow_mux_ptr_u64(pre_m, &slot[6], &sink[6]);
    uint64_t exp_w6 = cur_w6;
    uint64_t lock_w6 = (cur_w6 & UINT64_C(0xFFFFFFFF00000000)) | (uint64_t)FLOW_SWAP_LOCK_TAG;
    bool cas_won = __atomic_compare_exchange_n(cas_dst, &exp_w6, lock_w6, false, __ATOMIC_ACQ_REL,
                                               __ATOMIC_ACQUIRE);
    bool ok = pre_ok & cas_won;
    uint64_t ok_m = flow_mask_bool(ok);
    uint64_t *wr_dst = flow_mux_ptr_u64(ok_m, slot, &sink[0]);

    __atomic_store_n(&wr_dst[0], cand[0], __ATOMIC_RELAXED);
    __atomic_store_n(&wr_dst[1], cand[1], __ATOMIC_RELAXED);
    __atomic_store_n(&wr_dst[2], cand[2], __ATOMIC_RELAXED);
    __atomic_store_n(&wr_dst[3], cand[3], __ATOMIC_RELAXED);
    __atomic_store_n(&wr_dst[4], cand[4], __ATOMIC_RELAXED);
    __atomic_store_n(&wr_dst[5], cand[5], __ATOMIC_RELAXED);
    __atomic_store_n(&wr_dst[7], UINT64_C(0), __ATOMIC_RELAXED);
    __atomic_store_n(&wr_dst[6], (uint64_t)new_epoch << 32U, __ATOMIC_RELEASE);
    return ok;
}

/* --- Universal Total Branchless 2-Jet x HoTT Manifold Endomorphism --- */

FLOW_FORCE_INL void flow__step_chart_core(uint64_t out[8], const uint64_t state[8],
                                          const uint64_t bound[8], uint64_t cap_mask, int64_t u_ext,
                                          uint16_t mode_req) {
    /* 1. Total Symmetric Input Normalization (All inputs consumed up-front -> 0 pointer spills) */
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

    uint64_t b3 = bound[3];
    int64_t raw_exit = (int64_t)(int32_t)(uint32_t)(b3 >> 32U);
    int64_t raw_enter = (int64_t)(int32_t)(uint32_t)(b3 & UINT64_C(0xFFFFFFFF));
    int64_t b_exit0 = flow_clamp_sym(raw_exit, FLOW_MAX_Q16);
    int64_t enter_c = flow_clamp_sym(raw_enter, FLOW_MAX_Q16);
    int64_t b_enter0 = flow_sel_i64(enter_c < b_exit0, b_exit0, enter_c);
    uint64_t hyst_pack = ((uint64_t)(uint32_t)b_enter0 << 32U) | (uint64_t)(uint32_t)b_exit0;

    uint64_t b4 = bound[4];
    int64_t raw_dt = (int64_t)(int32_t)(uint32_t)(b4 >> 32U);
    int64_t dt_c = flow_clamp_sym(raw_dt, FLOW_MAX_Q16);
    int64_t dt_max = flow_sel_i64(dt_c < INT64_C(1), INT64_C(1), dt_c);

    int64_t raw_ug = (int64_t)(int32_t)(uint32_t)(b4 & UINT64_C(0xFFFFFFFF));
    int64_t u_gain = flow_clamp_sym(raw_ug, FLOW_MAX_Q16);
    int64_t u_in = flow_clamp_sym(u_ext, FLOW_MAX_Q16);

    uint64_t b5 = bound[5];
    uint64_t cm16 = cap_mask & UINT64_C(0xFFFF);
    uint16_t kan_lock = (uint16_t)(b5 & UINT64_C(0xFFFF));
    uint16_t mode_orig = (uint16_t)(s5 & UINT64_C(0xFFFF));
    uint16_t mode_p0 = (uint16_t)((mode_req & (uint16_t)(~kan_lock)) | (mode_orig & kan_lock));
    bool op_mode0 = (b1 != 0) | (b2 != 0);
    uint64_t n_lo0 = ((b5 >> 24U) & UINT64_C(0xFF)) & cm16;
    uint64_t n_hi0 = ((b5 >> 16U) & UINT64_C(0xFF)) & cm16;
    uint64_t ch_o0 = ((s5 >> 16U) & cm16);
    uint64_t topo_pack = ((uint64_t)op_mode0 << 48U) | (n_lo0 << 40U) | (n_hi0 << 32U) |
                         (ch_o0 << 16U) | (uint64_t)mode_p0;
    uint64_t ex_pack =
        ((s6 & UINT64_C(0xFFFFFFFF00000000)) + (UINT64_C(1) << 32U)) | (uint64_t)(uint32_t)x;

    /* 2. Exact 2^-49 Guard-Bit Residual G0 & 2^-32 Tangent G1 */
    int64_t b0_eff_q32 = (b0 << 16U) - u_gain * u_in;
    flow_i128 b0_eff_q49 = (flow_i128)b0_eff_q32 << 17U;

    int64_t x0 = (int64_t)(int32_t)(uint32_t)ex_pack;
    int64_t b1_x = b1 * x0;
    int64_t x_sq = x0 * x0;
    flow_i128 g0_op = b0_eff_q49 + ((flow_i128)b1_x << 17U) + (flow_i128)b2 * (flow_i128)x_sq;
    int64_t g1_op = (b1 << 16U) + b2 * x0;

    int64_t j0_in = flow_clamp_sym(raw_j0, FLOW_MAX_J0_Q16);
    int64_t j1_in = flow_clamp_sym(raw_j1, FLOW_MAX_J1_Q16);
    int64_t j2_in = flow_clamp_sym(raw_j2, FLOW_MAX_Q16);
    bool sub_ok = (raw_j0 == j0_in) & (raw_j1 == j1_in);
    uint64_t j0_sub_in = flow_sel_u64(sub_ok, s7 & UINT64_C(0x1FFFFFFFF), UINT64_C(0));
    uint64_t j1_sub_in = flow_sel_u64(sub_ok, s7 >> 48U, UINT64_C(0));

    flow_i128 j0_q49 = ((flow_i128)j0_in << 33U) + (flow_i128)j0_sub_in;
    int64_t j1_q32 = (j1_in << 16U) + (int64_t)j1_sub_in;
    flow_i128 g0_au = j0_q49 - b0_eff_q49;

    flow_i128 G0 = flow_sel_i128(op_mode0, g0_op, g0_au);
    int64_t G1 = flow_sel_i64(op_mode0, g1_op, j1_q32);
    int64_t R2 = flow_sel_i64(op_mode0, b2, j2_in);

    /* 3. Unified 128-Bit Citardauq / Minimum-|R| Vertex / Linear Solver */
    flow_i128 disc128 = (flow_i128)G1 * (flow_i128)G1 - G0 * (flow_i128)R2;
    bool real_roots = !flow_neg_i128(disc128);
    flow_u128 disc_pos = flow_sel_u128(real_roots, (flow_u128)disc128, (flow_u128)0ULL);
    int64_t d_mag = (int64_t)flow_isqrt_round_u128(disc_pos);

    /* Gauge-invariant branch sign: flips under (G0, G1, R2) -> (-G0, -G1, -R2) even at G1 == 0 */
    bool d_neg = (G1 < 0) | ((G1 == 0) & (R2 < 0));
    int64_t signed_d = d_neg ? -d_mag : d_mag;
    flow_i128 Q_cit = (flow_i128)G1 + (flow_i128)signed_d;

    bool has_r2 = (R2 != 0);
    flow_i128 neg_G0 = -G0;
    flow_i128 div_num = flow_sel_i128(has_r2 & (!real_roots), -(flow_i128)G1, neg_G0);
    flow_i128 div_den =
        flow_sel_i128(has_r2, flow_sel_i128(real_roots, Q_cit, (flow_i128)R2), (flow_i128)G1 << 1U);

    int64_t raw_step = flow_sym_div128_clamped(div_num, div_den, dt_max);
    bool active = (G0 != (flow_i128)0) & ((G1 != 0) | has_r2);
    int64_t step_act = flow_sel_i64(active, raw_step, INT64_C(0));

    /* 4. Boundary-Consistent Coordinate Step & Exact 128-Bit Lyapunov Monotonicity Guard */
    int64_t x_cur = (int64_t)(int32_t)(uint32_t)ex_pack;
    int64_t xp_cand = flow_clamp_sym(x_cur + step_act, FLOW_MAX_Q16);
    int64_t delta_cand = xp_cand - x_cur;
    int64_t d_sq = delta_cand * delta_cand;
    flow_i128 push_q49 =
        (((flow_i128)G1 * (flow_i128)delta_cand) << 1U) + (flow_i128)R2 * (flow_i128)d_sq;
    flow_i128 G0_cand = G0 + push_q49;

    bool lyap_ok = flow_abs_i128(G0_cand) <= flow_abs_i128(G0);
    int64_t delta = flow_sel_i64(lyap_ok, delta_cand, INT64_C(0));
    int64_t xp = x_cur + delta;
    uint64_t dt_resolved = flow_abs_i64(delta);

    flow_i128 G0_post = flow_sel_i128(lyap_ok, G0_cand, G0);
    int64_t G1_post = G1 + R2 * delta;

    /* 5. Exact Post-Step 2-Jet State & Lossless Autonomous Sub-LSB Phase Conservation */
    bool op_mode = ((topo_pack >> 48U) & UINT64_C(1)) != UINT64_C(0);
    int64_t j0_op_out = flow_sym_shr_i128(G0_post, 33U);
    int64_t j1_op_out = flow_sym_shr_i64(G1_post, 16U);

    flow_i128 J0_au_next = G0_post + ((flow_i128)b0_eff_q32 << 17U);
    int64_t j0_au_out = flow_clamp_sym((int64_t)(((uint64_t)(flow_u128)J0_au_next >> 33U) |
                                                 ((uint64_t)((flow_u128)J0_au_next >> 64U) << 31U)),
                                       FLOW_MAX_J0_Q16);
    int64_t j1_au_out = flow_clamp_sym(flow_ashr_i64(G1_post, 16U), FLOW_MAX_J1_Q16);
    uint64_t j0_sub_out = (uint64_t)J0_au_next & UINT64_C(0x1FFFFFFFF);
    uint64_t j1_sub_out = (uint64_t)G1_post & UINT64_C(0xFFFF);
    uint64_t sub_pack = (j1_sub_out << 48U) | j0_sub_out;

    int64_t j0_next = flow_sel_i64(op_mode, j0_op_out, j0_au_out);
    int64_t j1_next = flow_sel_i64(op_mode, j1_op_out, j1_au_out);
    uint64_t sub_next = flow_sel_u64(op_mode, UINT64_C(0), sub_pack);

    /* 6. Unconditional Cubical Kan Projection & Schmitt Hysteresis Gluing */
    int64_t b_exit = (int64_t)(int32_t)(uint32_t)(hyst_pack & UINT64_C(0xFFFFFFFF));
    int64_t b_enter = (int64_t)(int32_t)(uint32_t)(hyst_pack >> 32U);
    uint64_t next_lo = (topo_pack >> 40U) & UINT64_C(0xFF);
    uint64_t next_hi = (topo_pack >> 32U) & UINT64_C(0xFF);
    uint64_t chart_orig = (topo_pack >> 16U) & UINT64_C(0xFFFF);
    uint64_t mode_proj = topo_pack & UINT64_C(0xFFFF);

    uint64_t raw_chart = flow_sel_u64(xp < b_exit, next_lo, chart_orig);
    uint64_t chart_next = flow_sel_u64(xp > b_enter, next_hi, raw_chart);

    bool converged = flow_abs_i128(G0_post) <= ((flow_u128)(uint64_t)FLOW_CONVERGE_EPS_Q16 << 33U);
    uint32_t status_next = (uint32_t)converged * FLOW_STATUS_CONVERGED;

    out[0] = (uint64_t)j0_next;
    out[1] = (uint64_t)j1_next;
    out[2] = (uint64_t)R2;
    out[3] = (uint64_t)xp;
    out[4] = dt_resolved;
    out[5] = ((uint64_t)chart_next << 16U) | mode_proj;
    out[6] = (ex_pack & UINT64_C(0xFFFFFFFF00000000)) | (uint64_t)status_next;
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
    uint64_t w6_pre = __atomic_load_n(&slot[6], __ATOMIC_ACQUIRE);
    uint64_t snap[8] = {
        __atomic_load_n(&slot[0], __ATOMIC_RELAXED),
        __atomic_load_n(&slot[1], __ATOMIC_RELAXED),
        __atomic_load_n(&slot[2], __ATOMIC_RELAXED),
        __atomic_load_n(&slot[3], __ATOMIC_RELAXED),
        __atomic_load_n(&slot[4], __ATOMIC_RELAXED),
        __atomic_load_n(&slot[5], __ATOMIC_RELAXED),
        w6_pre,
        UINT64_C(0),
    };
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    uint64_t w6_post = __atomic_load_n(&slot[6], __ATOMIC_RELAXED);
    bool clean =
        ((uint32_t)(w6_pre & UINT64_C(0xFFFFFFFF)) != FLOW_SWAP_LOCK_TAG) & (w6_pre == w6_post);

    uint64_t step_out[8];
    flow__step_chart_core(step_out, state, snap, cap_mask, u_ext, mode_req);

    int64_t raw_j0 = (int64_t)state[0];
    int64_t raw_j1 = (int64_t)state[1];
    int64_t raw_j2 = (int64_t)state[2];
    int64_t x_hold = flow_clamp_sym((int64_t)state[3], FLOW_MAX_Q16);
    int64_t j0_hold = flow_clamp_sym(raw_j0, FLOW_MAX_J0_Q16);
    int64_t j1_hold = flow_clamp_sym(raw_j1, FLOW_MAX_J1_Q16);
    int64_t j2_hold = flow_clamp_sym(raw_j2, FLOW_MAX_Q16);
    uint64_t sub_valid = flow_mask_bool((raw_j0 == j0_hold) & (raw_j1 == j1_hold));
    uint64_t sub_hold = (state[7] & UINT64_C(0xFFFF0001FFFFFFFF)) & sub_valid;
    uint64_t chart_hold = ((state[5] >> 16U) & cap_mask & UINT64_C(0xFFFF)) << 16U;
    uint64_t mode_hold = state[5] & UINT64_C(0xFFFF);
    uint64_t ep_hold = step_out[6] & UINT64_C(0xFFFFFFFF00000000);

    out[0] = flow_sel_u64(clean, step_out[0], (uint64_t)j0_hold);
    out[1] = flow_sel_u64(clean, step_out[1], (uint64_t)j1_hold);
    out[2] = flow_sel_u64(clean, step_out[2], (uint64_t)j2_hold);
    out[3] = flow_sel_u64(clean, step_out[3], (uint64_t)x_hold);
    out[4] = flow_sel_u64(clean, step_out[4], UINT64_C(0));
    out[5] = flow_sel_u64(clean, step_out[5], chart_hold | mode_hold);
    out[6] = flow_sel_u64(clean, step_out[6], ep_hold);
    out[7] = flow_sel_u64(clean, step_out[7], sub_hold);
}

#ifdef __cplusplus
}
#endif

#endif /* FLOW_H */
