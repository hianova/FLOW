#ifndef FLOW_HOTT_JET_H
#define FLOW_HOTT_JET_H

#include "flow_asm.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW: Universal External-Constraint HoTT & Pure Q16 2-Jet Engine
 * ============================================================================
 * De-abstracts HoTT and 2-Jet into pure compile-time invariants and branchless
 * 64-bit / 128-bit integer register algebra over external variables (u_ext, b_ext):
 *
 *   1. Compile-Time Univalence (`FLOW_STATIC_ASSERT_UNIVALENT`): 0 runtime bytes.
 *   2. 64-Bit Cubical Kan & Bitwise HIT Quotient: 1-cycle neighbor transition.
 *   3. Pure Q48.16 2-Jet Fiber (`FlowJet2` / `FlowQJet2`): Taylor evaluation,
 *      128-bit integer quadratic root solver (`flow_jet2_solve_root`), and ASM
 *      differential self-checking (`flow_asm_jet2_self_check_step`).
 *   4. Universal External-Constraint Polyhedron (`FLOW_QPOLY_DEFINE` /
 *      `FLOW_QPOLY_DEFINE_EX`): static normal matrix A + dynamic external bounds
 *      b_ext with 1-cycle branchless dual-threshold hysteresis (b_enter / b_exit).
 * ============================================================================
 */

/* ============================================================================
 * 1. Compile-Time Univalence (A ~= B => A = U_0 B, 0 Runtime Bytes)
 * ============================================================================ */

#ifndef FLOW_STATIC_ASSERT_UNIVALENT
#define FLOW_STATIC_ASSERT_UNIVALENT(TypeA, TypeB)                                       \
    _Static_assert(sizeof(TypeA) == sizeof(TypeB) && _Alignof(TypeA) == _Alignof(TypeB), \
                   "HoTT Univalence violation: TypeA and TypeB are not layout-isomorphic")
#endif

FLOW_STATIC_ASSERT_UNIVALENT(FlowJet2, FlowQJet2);

/* ============================================================================
 * 2. Cubical De Morgan Algebra, Kan Homotopy & Bitwise HIT Quotient
 * ============================================================================ */

typedef uint8_t FlowDeMorganInterval;
#define FLOW_DEMORGAN_ZERO   0U
#define FLOW_DEMORGAN_ONE    1U
#define FLOW_CUBICAL_MAX_DIM FLOW_SSOT_CUBICAL_MAX_DIM

static inline FlowDeMorganInterval flow_demorgan_not(FlowDeMorganInterval i) {
    return (FlowDeMorganInterval)(1U - (i & 1U));
}
static inline FlowDeMorganInterval flow_demorgan_and(FlowDeMorganInterval a,
                                                     FlowDeMorganInterval b) {
    return (FlowDeMorganInterval)((a & 1U) & (b & 1U));
}
static inline FlowDeMorganInterval flow_demorgan_or(FlowDeMorganInterval a,
                                                    FlowDeMorganInterval b) {
    return (FlowDeMorganInterval)((a & 1U) | (b & 1U));
}

static inline uint64_t flow_demorgan_word_not(uint64_t w) {
    return ~w;
}
static inline uint64_t flow_demorgan_word_and(uint64_t w1, uint64_t w2) {
    return w1 & w2;
}
static inline uint64_t flow_demorgan_word_or(uint64_t w1, uint64_t w2) {
    return w1 | w2;
}

static inline uint64_t flow_cubical_face_proj(uint64_t sieve, uint32_t dim_k, uint8_t endpoint) {
    if (dim_k >= FLOW_CUBICAL_MAX_DIM)
        return sieve;
    uint64_t bit = 1ULL << dim_k;
    return (endpoint == 0U) ? (sieve & ~bit) : (sieve | bit);
}

static inline uint64_t flow_cubical_degeneracy(uint64_t sieve, uint32_t dim_k) {
    if (dim_k >= FLOW_CUBICAL_MAX_DIM - 1U)
        return sieve & ((1ULL << (FLOW_CUBICAL_MAX_DIM - 1U)) - 1ULL);
    uint64_t lower_mask = (dim_k > 0U) ? ((1ULL << dim_k) - 1ULL) : 0ULL;
    return (sieve & lower_mask) | ((sieve & ~lower_mask) << 1U);
}

static inline bool flow_cubical_sieve_is_covering(uint64_t sieve, uint64_t target_basis) {
    return (sieve & target_basis) == target_basis;
}

static inline uint8_t flow_topos_classify_subobject(uint64_t subobject_mask,
                                                    uint64_t ambient_sieve) {
    return ((subobject_mask & ambient_sieve) == subobject_mask) ? 1U : 0U;
}

/* Pure bitwise HIT exception quotient: M^\circ = M \ U_exception (0 structs) */
static inline uint64_t flow_hott_quotient_sieve(uint64_t ambient_sieve,
                                                uint64_t exception_zone_mask) {
    return ambient_sieve & ~exception_zone_mask;
}

static inline uint32_t flow_hott_quotient_dof(uint64_t ambient_sieve,
                                              uint64_t exception_zone_mask) {
    return (uint32_t)__builtin_popcountll(ambient_sieve & ~exception_zone_mask);
}

typedef enum {
    FLOW_KAN_FILLED_HOMOTOPIC = 0,
    FLOW_KAN_OBSTRUCTED_SINGULARITY = 1
} FlowKanStatus;

static inline FlowKanStatus flow_kan_check_homotopy(uint64_t mask_p, uint64_t mask_q,
                                                    uint64_t boundary_filter,
                                                    uint64_t *mismatch_out) {
    uint64_t diff = (mask_p ^ mask_q) & boundary_filter;
    if (mismatch_out)
        *mismatch_out = diff;
    return (diff == 0ULL) ? FLOW_KAN_FILLED_HOMOTOPIC : FLOW_KAN_OBSTRUCTED_SINGULARITY;
}

static inline FlowKanStatus flow_cubical_step_neighbor(uint64_t mask, uint32_t bit,
                                                       uint64_t boundary_filter,
                                                       uint64_t *next_mask) {
    if (bit >= FLOW_CUBICAL_MAX_DIM) {
        if (next_mask)
            *next_mask = mask;
        return FLOW_KAN_OBSTRUCTED_SINGULARITY;
    }
    uint64_t cand = mask ^ (1ULL << bit);
    FlowKanStatus st = flow_kan_check_homotopy(mask, cand, boundary_filter, NULL);
    if (next_mask)
        *next_mask = (st == FLOW_KAN_FILLED_HOMOTOPIC) ? cand : mask;
    return st;
}

/* ============================================================================
 * 3. Pure Q48.16 2-Jet Fiber Algebra, Root Solver & Self-Bootstrapping Verifier
 * ============================================================================ */

#if FLOW_HAS_INT128
__extension__ static inline int64_t flow_q16_clamp_i128(__int128 x) {
    if (x > (__int128)INT64_MAX)
        return INT64_MAX;
    if (x < (__int128)INT64_MIN)
        return INT64_MIN;
    return (int64_t)x;
}
#endif

static inline int64_t flow_q16_neg_sat(int64_t x) {
    return (x == INT64_MIN) ? INT64_MAX : -x;
}

static inline int64_t flow_q16_abs_sat(int64_t x) {
    return (x == INT64_MIN) ? INT64_MAX : ((x < 0) ? -x : x);
}

static inline int64_t flow_q16_add_sat(int64_t a, int64_t b) {
#if FLOW_HAS_INT128
    __extension__ typedef __int128 i128;
    return flow_q16_clamp_i128((i128)a + (i128)b);
#else
    return flow_i128_clamp_i64(flow_i128_add_pair(flow_i128_from_i64(a), flow_i128_from_i64(b)));
#endif
}

static inline int64_t flow_q16_sub_sat(int64_t a, int64_t b) {
#if FLOW_HAS_INT128
    __extension__ typedef __int128 i128;
    return flow_q16_clamp_i128((i128)a - (i128)b);
#else
    return flow_i128_clamp_i64(flow_i128_sub_pair(flow_i128_from_i64(a), flow_i128_from_i64(b)));
#endif
}

static inline int64_t flow_q16_mul_sat(int64_t a, int64_t b) {
#if FLOW_HAS_INT128
    __extension__ typedef __int128 i128;
    return flow_q16_clamp_i128(((i128)a * (i128)b) >> FLOW_Q16_SHIFT);
#else
    return flow_i128_clamp_i64(flow_i128_ashr(flow_i128_mul_i64(a, b), FLOW_Q16_SHIFT));
#endif
}

static inline int64_t flow_q16_div_sat(int64_t a, int64_t b) {
    if (b == 0) {
        return (a == 0) ? 0 : ((a > 0) ? INT64_MAX : INT64_MIN);
    }
#if FLOW_HAS_INT128
    __extension__ typedef __int128 i128;
    return flow_q16_clamp_i128(flow_i128_div_i128((i128)a * (i128)FLOW_Q16_ONE, (i128)b));
#else
    return flow_i128_clamp_i64(
        flow_i128_div_pair(flow_i128_mul_i64(a, FLOW_Q16_ONE), flow_i128_from_i64(b)));
#endif
}

static inline FlowJet2 flow_jet2_make(int64_t v_q16, int64_t d1_q16, int64_t d2_q16) {
    FlowJet2 r = {v_q16, d1_q16, d2_q16};
    return r;
}

static inline FlowJet2 flow_jet2_var(int64_t v_q16) {
    return flow_jet2_make(v_q16, FLOW_Q16_ONE, 0);
}

static inline FlowJet2 flow_jet2_const(int64_t c_q16) {
    return flow_jet2_make(c_q16, 0, 0);
}

static inline FlowJet2 flow_jet2_add(FlowJet2 a, FlowJet2 b) {
#if FLOW_HAS_INT128
    __extension__ typedef __int128 i128;
    return flow_jet2_make(flow_q16_clamp_i128((i128)a.v + (i128)b.v),
                          flow_q16_clamp_i128((i128)a.d1 + (i128)b.d1),
                          flow_q16_clamp_i128((i128)a.d2 + (i128)b.d2));
#else
    return flow_jet2_make(
        flow_i128_clamp_i64(flow_i128_add_pair(flow_i128_from_i64(a.v), flow_i128_from_i64(b.v))),
        flow_i128_clamp_i64(flow_i128_add_pair(flow_i128_from_i64(a.d1), flow_i128_from_i64(b.d1))),
        flow_i128_clamp_i64(
            flow_i128_add_pair(flow_i128_from_i64(a.d2), flow_i128_from_i64(b.d2))));
#endif
}

static inline FlowJet2 flow_jet2_sub(FlowJet2 a, FlowJet2 b) {
#if FLOW_HAS_INT128
    __extension__ typedef __int128 i128;
    return flow_jet2_make(flow_q16_clamp_i128((i128)a.v - (i128)b.v),
                          flow_q16_clamp_i128((i128)a.d1 - (i128)b.d1),
                          flow_q16_clamp_i128((i128)a.d2 - (i128)b.d2));
#else
    return flow_jet2_make(
        flow_i128_clamp_i64(flow_i128_sub_pair(flow_i128_from_i64(a.v), flow_i128_from_i64(b.v))),
        flow_i128_clamp_i64(flow_i128_sub_pair(flow_i128_from_i64(a.d1), flow_i128_from_i64(b.d1))),
        flow_i128_clamp_i64(
            flow_i128_sub_pair(flow_i128_from_i64(a.d2), flow_i128_from_i64(b.d2))));
#endif
}

static inline FlowJet2 flow_jet2_mul(FlowJet2 a, FlowJet2 b) {
#if FLOW_HAS_INT128
    __extension__ typedef __int128 i128;
    i128 v = ((i128)a.v * (i128)b.v) >> FLOW_Q16_SHIFT;
    i128 d1 = ((i128)a.d1 * (i128)b.v + (i128)a.v * (i128)b.d1) >> FLOW_Q16_SHIFT;
    i128 d2 = ((i128)a.d2 * (i128)b.v + 2 * (i128)a.d1 * (i128)b.d1 + (i128)a.v * (i128)b.d2) >>
              FLOW_Q16_SHIFT;
    return flow_jet2_make(flow_q16_clamp_i128(v), flow_q16_clamp_i128(d1), flow_q16_clamp_i128(d2));
#else
    FlowI128Pair v = flow_i128_ashr(flow_i128_mul_i64(a.v, b.v), FLOW_Q16_SHIFT);
    FlowI128Pair d1 = flow_i128_ashr(
        flow_i128_add_pair(flow_i128_mul_i64(a.d1, b.v), flow_i128_mul_i64(a.v, b.d1)),
        FLOW_Q16_SHIFT);
    FlowI128Pair mid = flow_i128_shl(flow_i128_mul_i64(a.d1, b.d1), 1U);
    FlowI128Pair d2_sum = flow_i128_add_pair(flow_i128_add_pair(flow_i128_mul_i64(a.d2, b.v), mid),
                                             flow_i128_mul_i64(a.v, b.d2));
    FlowI128Pair d2 = flow_i128_ashr(d2_sum, FLOW_Q16_SHIFT);
    return flow_jet2_make(flow_i128_clamp_i64(v), flow_i128_clamp_i64(d1), flow_i128_clamp_i64(d2));
#endif
}

static inline FlowJet2 flow_jet2_div(FlowJet2 a, FlowJet2 b) {
    if (b.v == 0 || b.v == INT64_MIN) {
        return flow_jet2_const(0);
    }
#if FLOW_HAS_INT128
    __extension__ typedef __int128 i128;
    i128 bv = (i128)b.v;
    i128 v = flow_i128_div_i128((i128)a.v * (i128)FLOW_Q16_ONE, bv);
    i128 d1 = flow_i128_div_i128((i128)a.d1 * (i128)FLOW_Q16_ONE - v * (i128)b.d1, bv);
    i128 d2 = flow_i128_div_i128(
        (i128)a.d2 * (i128)FLOW_Q16_ONE - 2 * d1 * (i128)b.d1 - v * (i128)b.d2, bv);
    return flow_jet2_make(flow_q16_clamp_i128(v), flow_q16_clamp_i128(d1), flow_q16_clamp_i128(d2));
#else
    FlowI128Pair bv = flow_i128_from_i64(b.v);
    FlowI128Pair bd1 = flow_i128_from_i64(b.d1);
    FlowI128Pair bd2 = flow_i128_from_i64(b.d2);
    FlowI128Pair v = flow_i128_div_pair(flow_i128_mul_i64(a.v, FLOW_Q16_ONE), bv);
    FlowI128Pair d1_num =
        flow_i128_sub_pair(flow_i128_mul_i64(a.d1, FLOW_Q16_ONE), flow_i128_mul_wrap(v, bd1));
    FlowI128Pair d1 = flow_i128_div_pair(d1_num, bv);
    FlowI128Pair two_d1_bd1 = flow_i128_shl(flow_i128_mul_wrap(d1, bd1), 1U);
    FlowI128Pair d2_num =
        flow_i128_sub_pair(flow_i128_sub_pair(flow_i128_mul_i64(a.d2, FLOW_Q16_ONE), two_d1_bd1),
                           flow_i128_mul_wrap(v, bd2));
    FlowI128Pair d2 = flow_i128_div_pair(d2_num, bv);
    return flow_jet2_make(flow_i128_clamp_i64(v), flow_i128_clamp_i64(d1), flow_i128_clamp_i64(d2));
#endif
}

static inline FlowJet2 flow_jet2_neg(FlowJet2 x) {
    return flow_jet2_make(flow_q16_neg_sat(x.v), flow_q16_neg_sat(x.d1), flow_q16_neg_sat(x.d2));
}

static inline FlowJet2 flow_jet2_abs(FlowJet2 x) {
    if (x.v > 0)
        return x;
    if (x.v < 0)
        return flow_jet2_neg(x);
    return flow_jet2_const(0);
}

static inline FlowJet2 flow_jet2_sqrt(FlowJet2 x) {
    if (x.v <= 0) {
        return flow_jet2_const(0);
    }
#if FLOW_HAS_INT128
    __extension__ typedef __int128 i128;
    __extension__ typedef unsigned __int128 u128;
    int64_t r = (int64_t)flow_asm_isqrt_u128(((u128)(uint64_t)x.v) << FLOW_Q16_SHIFT);
    if (r <= 0) {
        return flow_jet2_const(0);
    }
    i128 two_r = 2 * (i128)r;
    i128 d1 = flow_i128_div_i128((i128)x.d1 * (i128)FLOW_Q16_ONE, two_r);
    i128 d2 = flow_i128_div_i128((i128)x.d2 * (i128)FLOW_Q16_ONE - 2 * d1 * d1, two_r);
    return flow_jet2_make(r, flow_q16_clamp_i128(d1), flow_q16_clamp_i128(d2));
#else
    uint64_t ux = (uint64_t)x.v;
    int64_t r = (int64_t)flow_isqrt_u128_pair(ux >> 48U, ux << FLOW_Q16_SHIFT);
    if (r <= 0) {
        return flow_jet2_const(0);
    }
    FlowI128Pair two_r = flow_i128_shl(flow_i128_from_i64(r), 1U);
    FlowI128Pair d1 = flow_i128_div_pair(flow_i128_mul_i64(x.d1, FLOW_Q16_ONE), two_r);
    FlowI128Pair two_d1_sq = flow_i128_shl(flow_i128_mul_wrap(d1, d1), 1U);
    FlowI128Pair d2 = flow_i128_div_pair(
        flow_i128_sub_pair(flow_i128_mul_i64(x.d2, FLOW_Q16_ONE), two_d1_sq), two_r);
    return flow_jet2_make(r, flow_i128_clamp_i64(d1), flow_i128_clamp_i64(d2));
#endif
}

/*
 * Pure Q48.16 2-Jet Unit-Interval Convex Combination / Homotopy Lerp (`alpha_q16` in [0, 65536]):
 *   lerp(j0, j1, alpha) = (1 - alpha) * j0 + alpha * j1
 */
static inline FlowJet2 flow_jet2_lerp(FlowJet2 j0, FlowJet2 j1, int64_t alpha_q16) {
    if (alpha_q16 <= 0)
        return j0;
    if (alpha_q16 >= FLOW_Q16_ONE)
        return j1;
#if FLOW_HAS_INT128
    __extension__ typedef __int128 i128;
    i128 w0 = (i128)(FLOW_Q16_ONE - alpha_q16);
    i128 w1 = (i128)alpha_q16;
    i128 v = (w0 * (i128)j0.v + w1 * (i128)j1.v) >> FLOW_Q16_SHIFT;
    i128 d1 = (w0 * (i128)j0.d1 + w1 * (i128)j1.d1) >> FLOW_Q16_SHIFT;
    i128 d2 = (w0 * (i128)j0.d2 + w1 * (i128)j1.d2) >> FLOW_Q16_SHIFT;
    return flow_jet2_make(flow_q16_clamp_i128(v), flow_q16_clamp_i128(d1), flow_q16_clamp_i128(d2));
#else
    int64_t w0 = FLOW_Q16_ONE - alpha_q16;
    int64_t w1 = alpha_q16;
    FlowI128Pair v =
        flow_i128_ashr(flow_i128_add_pair(flow_i128_mul_i64(w0, j0.v), flow_i128_mul_i64(w1, j1.v)),
                       FLOW_Q16_SHIFT);
    FlowI128Pair d1 = flow_i128_ashr(
        flow_i128_add_pair(flow_i128_mul_i64(w0, j0.d1), flow_i128_mul_i64(w1, j1.d1)),
        FLOW_Q16_SHIFT);
    FlowI128Pair d2 = flow_i128_ashr(
        flow_i128_add_pair(flow_i128_mul_i64(w0, j0.d2), flow_i128_mul_i64(w1, j1.d2)),
        FLOW_Q16_SHIFT);
    return flow_jet2_make(flow_i128_clamp_i64(v), flow_i128_clamp_i64(d1), flow_i128_clamp_i64(d2));
#endif
}

#define flow_jet2_ema_step flow_jet2_lerp

/*
 * Evaluate 2-Jet Taylor polynomial at `dt_q16` (where `1.0 == FLOW_Q16_ONE == 65536`):
 *   j(dt) = v + d1 * dt + 0.5 * d2 * dt^2
 */
static inline int64_t flow_jet2_eval_taylor(FlowJet2 j, int64_t dt_q16) {
#if FLOW_HAS_INT128
    __extension__ typedef __int128 i128;
    i128 lin_q16 = ((i128)j.d1 * (i128)dt_q16) >> FLOW_Q16_SHIFT;
    i128 p1 = (i128)j.d2 * (i128)dt_q16;
    i128 p2 = 0;
    i128 quad_q16 = 0;
    if (!__builtin_mul_overflow(p1, (i128)dt_q16, &p2)) {
        quad_q16 = p2 >> (2U * FLOW_Q16_SHIFT + 1U);
    } else {
        quad_q16 = ((p1 >> FLOW_Q16_SHIFT) * (i128)dt_q16) >> (FLOW_Q16_SHIFT + 1U);
    }
    return flow_q16_clamp_i128((i128)j.v + lin_q16 + quad_q16);
#else
    FlowI128Pair lin_q16 = flow_i128_ashr(flow_i128_mul_i64(j.d1, dt_q16), FLOW_Q16_SHIFT);
    FlowI128Pair p1 = flow_i128_mul_i64(j.d2, dt_q16);
    FlowI128Pair p2 = {0, 0ULL};
    FlowI128Pair quad_q16 = {0, 0ULL};
    if (!flow_i128_mul_i64_checked(p1, dt_q16, &p2)) {
        quad_q16 = flow_i128_ashr(p2, 2U * FLOW_Q16_SHIFT + 1U);
    } else {
        FlowI128Pair p1_shr = flow_i128_ashr(p1, FLOW_Q16_SHIFT);
        quad_q16 = flow_i128_ashr(flow_i128_mul_wrap(p1_shr, flow_i128_from_i64(dt_q16)),
                                  FLOW_Q16_SHIFT + 1U);
    }
    return flow_i128_clamp_i64(
        flow_i128_add_pair(flow_i128_add_pair(flow_i128_from_i64(j.v), lin_q16), quad_q16));
#endif
}

/*
 * Solve `v + d1 * t + 0.5 * d2 * t^2 = 0` for the smallest root `t_q16 in (0, t_max_q16]`
 * using 100% deterministic 128-bit integer arithmetic (`flow_asm_isqrt_u128`).
 * Note: The polynomial `2*v + 2*d1*t + d2*t^2 = 0` is homogeneous in `(v, d1, d2)`,
 * so `j` may be passed either in Q48.16 or unscaled integer lattice units; `t_max_q16`
 * and `*out_t_q16` are always in Q48.16 (`1.0 == 65536`).
 */
static inline bool flow_jet2_solve_root(FlowJet2 j, int64_t t_max_q16, int64_t *out_t_q16) {
    if (t_max_q16 <= 0 || j.v == INT64_MIN || j.d1 == INT64_MIN || j.d2 == INT64_MIN ||
        flow_hott_is_boundary_tag((uint64_t)j.v) || flow_hott_is_boundary_tag((uint64_t)j.d1) ||
        flow_hott_is_boundary_tag((uint64_t)j.d2)) {
        return false;
    }

    int64_t jv = j.v;
    int64_t jd1 = j.d1;
    int64_t jd2 = j.d2;

    /* Pre-normalize homogeneous coefficients if near 2^46 so disc << 32 never overflows u128 */
    uint64_t max_mag = (uint64_t)flow_q16_abs_sat(jv);
    uint64_t mag_d1 = (uint64_t)flow_q16_abs_sat(jd1);
    uint64_t mag_d2 = (uint64_t)flow_q16_abs_sat(jd2);
    if (mag_d1 > max_mag)
        max_mag = mag_d1;
    if (mag_d2 > max_mag)
        max_mag = mag_d2;
    if (max_mag >= (1ULL << 46U)) {
        uint32_t shr = 18U - (uint32_t)__builtin_clzll(max_mag);
        jv >>= shr;
        jd1 >>= shr;
        jd2 >>= shr;
    }

#if FLOW_HAS_INT128
    __extension__ typedef __int128 i128;
    __extension__ typedef unsigned __int128 u128;
    i128 v = (i128)jv;
    i128 d1 = (i128)jd1;
    i128 d2 = (i128)jd2;

    if (d2 == 0) {
        if (d1 == 0) {
            return false;
        }
        i128 num = -v * (i128)FLOW_Q16_ONE;
        i128 den = d1;
        if (!((num > 0 && den > 0) || (num < 0 && den < 0))) {
            return false;
        }
        u128 unum = (num < 0) ? (u128)(-num) : (u128)num;
        u128 uden = (den < 0) ? (u128)(-den) : (u128)den;
        u128 t_u = flow_u128_div_u128(unum + (uden >> 1U), uden);
        if (t_u == 0U)
            t_u = 1U;
        if (t_u <= (u128)(uint64_t)t_max_q16) {
            if (out_t_q16) {
                *out_t_q16 = (int64_t)t_u;
            }
            return true;
        }
        return false;
    }

    /* Discriminant of (1/2) d2 t^2 + d1 t + v = 0 is disc = d1^2 - 2 d2 v */
    i128 disc = d1 * d1 - 2 * d2 * v;
    if (disc < 0) {
        return false;
    }
    u128 udisc_q32 = ((u128)disc) << (2U * FLOW_Q16_SHIFT);
    i128 sqrt_disc_q16 = (i128)flow_asm_isqrt_u128(udisc_q32);
    i128 minus_d1_q16 = -(d1 * (i128)FLOW_Q16_ONE);

    i128 nums[2] = {minus_d1_q16 - sqrt_disc_q16, minus_d1_q16 + sqrt_disc_q16};
    u128 uden = (d2 < 0) ? (u128)(-d2) : (u128)d2;
    int64_t best_t = INT64_MAX;
    bool found = false;

    for (size_t k = 0; k < 2U; ++k) {
        i128 num = nums[k];
        if (!((num > 0 && d2 > 0) || (num < 0 && d2 < 0))) {
            continue;
        }
        u128 unum = (num < 0) ? (u128)(-num) : (u128)num;
        u128 t_u = flow_u128_div_u128(unum + (uden >> 1U), uden);
        if (t_u == 0U)
            t_u = 1U;
        if (t_u <= (u128)(uint64_t)t_max_q16) {
            int64_t t_cand = (int64_t)t_u;
            if (!found || t_cand < best_t) {
                best_t = t_cand;
                found = true;
            }
        }
    }
    if (found && out_t_q16) {
        *out_t_q16 = best_t;
    }
    return found;
#else
    if (jd2 == 0) {
        if (jd1 == 0) {
            return false;
        }
        FlowI128Pair num = flow_i128_mul_i64(-jv, FLOW_Q16_ONE);
        bool num_pos = (num.hi > 0) || (num.hi == 0 && num.lo > 0ULL);
        bool num_neg = (num.hi < 0);
        if (!((num_pos && jd1 > 0) || (num_neg && jd1 < 0))) {
            return false;
        }
        FlowU128Pair unum = num_neg ? flow_u128_neg_pair((uint64_t)num.hi, num.lo)
                                    : flow_u128_make((uint64_t)num.hi, num.lo);
        uint64_t uden64 = (jd1 < 0) ? (0ULL - (uint64_t)jd1) : (uint64_t)jd1;
        FlowU128Pair rounded = {0ULL, 0ULL};
        (void)flow_u128_add_checked(unum, flow_u128_make(0ULL, uden64 >> 1U), &rounded);
        FlowU128Pair t_u = flow_u128_div_pair(rounded, flow_u128_make(0ULL, uden64));
        if (t_u.hi == 0ULL && t_u.lo == 0ULL)
            t_u.lo = 1ULL;
        if (t_u.hi == 0ULL && t_u.lo <= (uint64_t)t_max_q16) {
            if (out_t_q16) {
                *out_t_q16 = (int64_t)t_u.lo;
            }
            return true;
        }
        return false;
    }

    FlowI128Pair disc = flow_i128_sub_pair(flow_i128_mul_i64(jd1, jd1),
                                           flow_i128_shl(flow_i128_mul_i64(jd2, jv), 1U));
    if (disc.hi < 0) {
        return false;
    }
    FlowI128Pair udisc_q32 = flow_i128_shl(disc, 2U * FLOW_Q16_SHIFT);
    uint64_t sq64 = flow_isqrt_u128_pair((uint64_t)udisc_q32.hi, udisc_q32.lo);
    FlowI128Pair sqrt_disc_q16 = {0, sq64};
    FlowI128Pair minus_d1_q16 = flow_i128_mul_i64(-jd1, FLOW_Q16_ONE);
    FlowI128Pair nums[2] = {flow_i128_sub_pair(minus_d1_q16, sqrt_disc_q16),
                            flow_i128_add_pair(minus_d1_q16, sqrt_disc_q16)};
    uint64_t uden64 = (jd2 < 0) ? (0ULL - (uint64_t)jd2) : (uint64_t)jd2;
    int64_t best_t = INT64_MAX;
    bool found = false;

    for (size_t k = 0; k < 2U; ++k) {
        FlowI128Pair num = nums[k];
        bool num_pos = (num.hi > 0) || (num.hi == 0 && num.lo > 0ULL);
        bool num_neg = (num.hi < 0);
        if (!((num_pos && jd2 > 0) || (num_neg && jd2 < 0))) {
            continue;
        }
        FlowU128Pair unum = num_neg ? flow_u128_neg_pair((uint64_t)num.hi, num.lo)
                                    : flow_u128_make((uint64_t)num.hi, num.lo);
        FlowU128Pair rounded = {0ULL, 0ULL};
        (void)flow_u128_add_checked(unum, flow_u128_make(0ULL, uden64 >> 1U), &rounded);
        FlowU128Pair t_u = flow_u128_div_pair(rounded, flow_u128_make(0ULL, uden64));
        if (t_u.hi == 0ULL && t_u.lo == 0ULL)
            t_u.lo = 1ULL;
        if (t_u.hi == 0ULL && t_u.lo <= (uint64_t)t_max_q16) {
            int64_t t_cand = (int64_t)t_u.lo;
            if (!found || t_cand < best_t) {
                best_t = t_cand;
                found = true;
            }
        }
    }
    if (found && out_t_q16) {
        *out_t_q16 = best_t;
    }
    return found;
#endif
}

/*
 * Non-Cancelling 64-Bit Hardware-Register 2-Jet Root Solver (`Citardauq` + Horizon Sieve):
 * Solves `v + d1 * t + 0.5 * d2 * t^2 = 0` (for interior slack `v > 0`) for the first
 * boundary impact time `t_q16` in `(0, t_max_q16]`:
 *   - Keeps standard Q16 inputs in `[2^12, 2^30)` unshifted so `disc` executes the 1-`udiv`
 *     `flow_asm_isqrt_u64_fast` path directly, normalizing tiny/huge inputs into `[2^21, 2^22)`.
 *   - Applies an O(1) zero-division convex/concave horizon reachability sieve before `isqrt`.
 *   - Branchlessly selects Citardauq `t = 2*v / (|r_i| + sq)` (`r_i >= 0`) vs `(|r_i| + sq) / c_i`
 *     (`r_i < 0`) to guarantee zero catastrophic subtractive cancellation.
 */
static inline bool flow_jet2_solve_root_fast64(int64_t v, int64_t d1, int64_t d2, int64_t t_max_q16,
                                               int64_t *out_t_q16) {
    if (__builtin_expect(!out_t_q16 || v <= 0 || t_max_q16 <= 0, 0)) {
        return false;
    }
    uint64_t mv = (uint64_t)v;
    uint64_t mr = (d1 < 0) ? (0ULL - (uint64_t)d1) : (uint64_t)d1;
    uint64_t mc = (d2 < 0) ? (0ULL - (uint64_t)d2) : (uint64_t)d2;
    uint64_t or_m = mv | mr | mc;

    int64_t r_i;
    int64_t c_i;
    if (__builtin_expect(or_m < (1ULL << 12U) || or_m >= (1ULL << 30U), 0)) {
        if (d1 == INT64_MIN || d2 == INT64_MIN || flow_hott_is_boundary_tag((uint64_t)v) ||
            flow_hott_is_boundary_tag((uint64_t)d1) || flow_hott_is_boundary_tag((uint64_t)d2)) {
            return false;
        }
        r_i = -d1;
        c_i = -d2;
        if (r_i <= 0 && c_i <= 0) {
            return false;
        }
        uint64_t max_m = (mv > mr) ? mv : mr;
        if (mc > max_m) {
            max_m = mc;
        }
        uint32_t lz = (uint32_t)__builtin_clzll(max_m);
        if (lz < 42U) {
            uint32_t shr = 42U - lz;
            v >>= shr;
            r_i >>= shr;
            c_i >>= shr;
        } else if (lz > 42U) {
            uint32_t shl = lz - 42U;
            v = (int64_t)((uint64_t)v << shl);
            r_i = (int64_t)((uint64_t)r_i << shl);
            c_i = (int64_t)((uint64_t)c_i << shl);
        }
        if (v <= 0 || (r_i <= 0 && c_i <= 0)) {
            return false;
        }
        mr = (uint64_t)(r_i < 0 ? -r_i : r_i);
        mc = (uint64_t)(c_i < 0 ? -c_i : c_i);
    } else {
        r_i = -d1;
        c_i = -d2;
        if (r_i <= 0 && c_i <= 0) {
            return false;
        }
    }

    if (c_i == 0) {
        uint64_t t_u = flow_u64_div_portable(((uint64_t)v) << FLOW_Q16_SHIFT, mr);
        if (t_u > 0ULL && t_u <= (uint64_t)t_max_q16) {
            *out_t_q16 = (int64_t)t_u;
            return true;
        }
        return false;
    }

    if ((uint64_t)t_max_q16 < (1ULL << 30U)) {
#if FLOW_HAS_INT128
        __extension__ typedef __int128 i128;
        i128 target = (i128)v << FLOW_Q16_SHIFT;
        if (c_i < 0) {
            if (r_i <= 0 || (i128)r_i * (i128)t_max_q16 < target) {
                return false;
            }
        } else {
            uint64_t t_ub = (uint64_t)t_max_q16 + 1ULL;
            i128 f_ub = (i128)r_i * (i128)t_ub + (((i128)c_i * (i128)(t_ub * t_ub)) >> 17U);
            if (f_ub < target) {
                return false;
            }
        }
#else
        FlowI128Pair target = flow_i128_shl(flow_i128_from_i64(v), FLOW_Q16_SHIFT);
        if (c_i < 0) {
            if (r_i <= 0 || flow_i128_cmp(flow_i128_mul_i64(r_i, t_max_q16), target) < 0) {
                return false;
            }
        } else {
            int64_t t_ub = t_max_q16 + 1;
            int64_t t_ub_sq = (int64_t)((uint64_t)t_ub * (uint64_t)t_ub);
            FlowI128Pair f_ub = flow_i128_add_pair(
                flow_i128_mul_i64(r_i, t_ub), flow_i128_ashr(flow_i128_mul_i64(c_i, t_ub_sq), 17U));
            if (flow_i128_cmp(f_ub, target) < 0) {
                return false;
            }
        }
#endif
    }

    int64_t disc = r_i * r_i + 2LL * c_i * v;
    if (disc < 0) {
        return false;
    }
    uint64_t sq = flow_asm_isqrt_u64_fast((uint64_t)disc);
    uint64_t q_sum = mr + sq;
    uint64_t num =
        (r_i >= 0) ? (((uint64_t)v) << (FLOW_Q16_SHIFT + 1U)) : (q_sum << FLOW_Q16_SHIFT);
    uint64_t den = (r_i >= 0) ? q_sum : (uint64_t)c_i;
    if (den == 0ULL) {
        return false;
    }
    uint64_t t_u = flow_u64_div_portable(num, den);
    if (t_u > 0ULL && t_u <= (uint64_t)t_max_q16) {
        *out_t_q16 = (int64_t)t_u;
        return true;
    }
    return false;
}

static inline uint64_t flow_jet2_nondegenerate_ballot64(const FlowJet2 *jets, size_t count,
                                                        int64_t eps_d1_q16, int64_t eps_d2_q16) {
    if (!jets || count == 0U || eps_d1_q16 < 0 || eps_d2_q16 < 0) {
        return 0ULL;
    }
    size_t n = (count > 64U) ? 64U : count;
    uint64_t ballot = 0ULL;
    for (size_t i = 0U; i < n; ++i) {
        if (jets[i].v == INT64_MIN || jets[i].d1 == INT64_MIN || jets[i].d2 == INT64_MIN ||
            flow_hott_is_boundary_tag((uint64_t)jets[i].v) ||
            flow_hott_is_boundary_tag((uint64_t)jets[i].d1) ||
            flow_hott_is_boundary_tag((uint64_t)jets[i].d2)) {
            continue;
        }
        if (flow_q16_abs_sat(jets[i].d1) > eps_d1_q16 ||
            flow_q16_abs_sat(jets[i].d2) > eps_d2_q16) {
            ballot |= (1ULL << i);
        }
    }
    return ballot;
}

static inline uint64_t flow_asm_jet2_self_check_step(FlowJet2 prev, FlowJet2 next, int64_t dt_q16,
                                                     int64_t tol_v_q16, int64_t tol_d1_q16,
                                                     uint64_t *inout_sieve) {
    uint64_t local_sieve = inout_sieve ? *inout_sieve : FLOW_ASM_SIEVE_ALL_SOUND;
    (void)flow_asm_check_jet2(prev, &local_sieve);
    (void)flow_asm_check_jet2(next, &local_sieve);

    bool dt_valid = (dt_q16 > 0) && (dt_q16 != INT64_MIN) && (dt_q16 != INT64_MAX) &&
                    !flow_hott_is_boundary_tag((uint64_t)dt_q16);
    if (!dt_valid) {
        local_sieve &= ~(FLOW_ASM_SIEVE_DOMAIN_VALID | FLOW_ASM_SIEVE_JET2_TAYLOR_0 |
                         FLOW_ASM_SIEVE_JET2_TAYLOR_1);
        if (inout_sieve)
            *inout_sieve = local_sieve;
        return local_sieve;
    }

    int64_t v_hat_q16 = flow_jet2_eval_taylor(prev, dt_q16);
#if FLOW_HAS_INT128
    __extension__ typedef __int128 i128;
    i128 d1_hat_i128 =
        (i128)prev.d1 + ((((i128)prev.d2 + (i128)next.d2) * (i128)dt_q16) >> (FLOW_Q16_SHIFT + 1U));
    int64_t d1_hat_q16 = flow_q16_clamp_i128(d1_hat_i128);

    i128 diff_v = (i128)next.v - (i128)v_hat_q16;
    i128 diff_d1 = (i128)next.d1 - (i128)d1_hat_q16;
    if (diff_v < 0)
        diff_v = -diff_v;
    if (diff_d1 < 0)
        diff_d1 = -diff_d1;

    int64_t r0_q16 = (diff_v > (i128)INT64_MAX) ? INT64_MAX : (int64_t)diff_v;
    int64_t r1_q16 = (diff_d1 > (i128)INT64_MAX) ? INT64_MAX : (int64_t)diff_d1;
#else
    FlowI128Pair sum_d2 =
        flow_i128_add_pair(flow_i128_from_i64(prev.d2), flow_i128_from_i64(next.d2));
    FlowI128Pair d1_delta =
        flow_i128_ashr(flow_i128_mul_wrap(sum_d2, flow_i128_from_i64(dt_q16)), FLOW_Q16_SHIFT + 1U);
    int64_t d1_hat_q16 =
        flow_i128_clamp_i64(flow_i128_add_pair(flow_i128_from_i64(prev.d1), d1_delta));

    FlowI128Pair diff_v =
        flow_i128_sub_pair(flow_i128_from_i64(next.v), flow_i128_from_i64(v_hat_q16));
    FlowI128Pair diff_d1 =
        flow_i128_sub_pair(flow_i128_from_i64(next.d1), flow_i128_from_i64(d1_hat_q16));
    FlowU128Pair abs_v = (diff_v.hi < 0) ? flow_u128_neg_pair((uint64_t)diff_v.hi, diff_v.lo)
                                         : flow_u128_make((uint64_t)diff_v.hi, diff_v.lo);
    FlowU128Pair abs_d1 = (diff_d1.hi < 0) ? flow_u128_neg_pair((uint64_t)diff_d1.hi, diff_d1.lo)
                                           : flow_u128_make((uint64_t)diff_d1.hi, diff_d1.lo);
    int64_t r0_q16 =
        (abs_v.hi != 0ULL || abs_v.lo > (uint64_t)INT64_MAX) ? INT64_MAX : (int64_t)abs_v.lo;
    int64_t r1_q16 =
        (abs_d1.hi != 0ULL || abs_d1.lo > (uint64_t)INT64_MAX) ? INT64_MAX : (int64_t)abs_d1.lo;
#endif

    FlowResultU64 margin0 = flow_asm_qsub_i64(tol_v_q16, r0_q16, &local_sieve);
    FlowResultU64 margin1 = flow_asm_qsub_i64(tol_d1_q16, r1_q16, &local_sieve);

    uint64_t clear_bits = (((int64_t)margin0.value < 0 || flow_result_u64_is_boundary(margin0))
                               ? FLOW_ASM_SIEVE_JET2_TAYLOR_0
                               : 0ULL) |
                          (((int64_t)margin1.value < 0 || flow_result_u64_is_boundary(margin1))
                               ? FLOW_ASM_SIEVE_JET2_TAYLOR_1
                               : 0ULL);
    local_sieve &= ~clear_bits;

    if (inout_sieve) {
        *inout_sieve = local_sieve;
    }
    return local_sieve;
}

/* ============================================================================
 * 4. Universal External-Constraint Q-Polyhedron (FlowQPoly)
 * ============================================================================
 * Static deterministic normal matrix A[MAX_PLANES][DIM] paired with dynamic
 * external constraint bounds b[MAX_PLANES] and optional branchless hysteresis
 * bounds b_exit[MAX_PLANES].
 * ============================================================================ */

#define FLOW_QPOLY_PLANES FLOW_SSOT_QPOLY_PLANES

typedef struct {
    uint64_t violated_mask;  /* Sieve: bit i = 1 iff plane i is violated (a_i . x > b_active_i) */
    uint32_t worst_plane;    /* Plane index with largest positive violation */
    uint32_t tightest_plane; /* Interior plane index with minimum non-negative slack */
    int64_t worst_violation; /* Q-scaled maximum violation (>= 0) */
    int64_t min_slack;       /* Q-scaled minimum slack among satisfied active planes */
} FlowQPolyEval;

typedef struct {
    uint32_t plane;
    int64_t t_hit_q;
    bool will_exit;
} FlowQPolyForecast;

int flow_qpoly_quantize_inner(size_t plane_count, size_t dim, uint32_t shift, const int64_t *a_num,
                              const int64_t *b_num, int64_t denom, const int64_t *box_min,
                              const int64_t *box_max, int32_t *a_q_out, int64_t *b_q_out);

FlowQPolyForecast flow_qpoly_predict_next_face(const int32_t *A_rows, const int64_t *b,
                                               uint64_t active, size_t dim, const int32_t *x,
                                               const int32_t *dx, const int32_t *ddx);

static inline int64_t flow_asm_select_i64(uint64_t cond_bit, int64_t if_true, int64_t if_false) {
    int64_t out = 0;
#if !defined(FLOW_FORCE_C17_FALLBACK) && (defined(__aarch64__) || defined(__arm64__))
    __asm__("cmp  %[cond], #0\n\t"
            "csel %[out], %[t], %[f], ne"
            : [out] "=r"(out)
            : [cond] "r"(cond_bit), [t] "r"(if_true), [f] "r"(if_false)
            : "cc");
#else
    out = (cond_bit != 0ULL) ? if_true : if_false;
#endif
    return out;
}

static inline bool flow_schmitt_hyst_hi_i64(int64_t b_enter, int64_t b_exit, int64_t x,
                                            bool prev_latched) {
    if (x >= b_enter)
        return true;
    if (x <= b_exit)
        return false;
    return prev_latched;
}

static inline bool flow_schmitt_hyst_lo_i64(int64_t b_enter, int64_t b_exit, int64_t x,
                                            bool prev_latched) {
    if (x <= b_enter)
        return true;
    if (x >= b_exit)
        return false;
    return prev_latched;
}

#define FLOW_QPOLY_DEFINE_EX(NAME, DIM, SHIFT, MAX_PLANES)                                      \
    enum {                                                                                      \
        NAME##_DIM = (DIM),                                                                     \
        NAME##_SHIFT = (SHIFT),                                                                 \
        NAME##_MAX_PLANES = (MAX_PLANES)                                                        \
    };                                                                                          \
    _Static_assert((MAX_PLANES) >= 1 && (MAX_PLANES) <= FLOW_QPOLY_PLANES,                      \
                   #NAME " MAX_PLANES must be in [1, 64]");                                     \
    typedef struct __attribute__((aligned(FLOW_HW_CACHE_LINE_BYTES))) {                         \
        int32_t A[MAX_PLANES][DIM]; /* Static deterministic constraint matrix */                \
        int64_t b[MAX_PLANES];      /* Dynamic external bound b_enter */                        \
        int64_t b_exit[MAX_PLANES]; /* Optional hysteresis recovery bound b_exit */             \
        uint64_t active_mask;       /* Active constraint planes bitmask */                      \
        uint64_t hysteresis_mask;   /* Planes with dual-threshold hysteresis */                 \
        uint64_t generation;        /* Deterministic external constraint epoch */               \
    } NAME;                                                                                     \
    _Static_assert(sizeof(NAME) <= FLOW_HW_L1D_BYTES, #NAME " exceeds L1D cache capacity");     \
                                                                                                \
    static inline __attribute__((unused)) uint64_t NAME##_valid_plane_mask(void) {              \
        return ((uint32_t)(MAX_PLANES) >= 64U) ? ~0ULL                                          \
                                               : ((1ULL << (uint32_t)(MAX_PLANES)) - 1ULL);     \
    }                                                                                           \
                                                                                                \
    static inline __attribute__((unused)) void NAME##_init(NAME *p) {                           \
        if (!p)                                                                                 \
            return;                                                                             \
        uint8_t *raw_bytes = (uint8_t *)(void *)p;                                              \
        for (size_t z = 0U; z < sizeof(*p); ++z) {                                              \
            raw_bytes[z] = 0U;                                                                  \
        }                                                                                       \
        p->active_mask = 0ULL;                                                                  \
        p->hysteresis_mask = 0ULL;                                                              \
        p->generation = 1ULL;                                                                   \
    }                                                                                           \
                                                                                                \
    static inline __attribute__((unused)) int32_t NAME##_quantize_scalar(int64_t num,           \
                                                                         int64_t denom) {       \
        if (denom <= 0)                                                                         \
            return 0;                                                                           \
        FlowI128Pair scaled = flow_i128_mul_i64(num, (int64_t)(1ULL << (SHIFT)));               \
        FlowI128Pair D = flow_i128_from_i64(denom);                                             \
        FlowI128Pair D_half = flow_i128_from_i64(denom >> 1U);                                  \
        FlowI128Pair q = (scaled.hi >= 0)                                                       \
                             ? flow_i128_div_pair(flow_i128_add_pair(scaled, D_half), D)        \
                             : flow_i128_neg_pair(flow_i128_div_pair(                           \
                                   flow_i128_add_pair(flow_i128_neg_pair(scaled), D_half), D)); \
        if (flow_i128_cmp(q, flow_i128_from_i64(INT32_MAX)) >= 0)                               \
            return INT32_MAX;                                                                   \
        if (flow_i128_cmp(q, flow_i128_from_i64(INT32_MIN)) <= 0)                               \
            return INT32_MIN;                                                                   \
        return (int32_t)(int64_t)q.lo;                                                          \
    }                                                                                           \
                                                                                                \
    static inline __attribute__((unused)) void NAME##_set_hysteresis(                           \
        NAME *p, uint32_t plane, int64_t b_enter_val, int64_t b_exit_val) {                     \
        if (!p || plane >= (uint32_t)(MAX_PLANES))                                              \
            return;                                                                             \
        p->b[plane] = b_enter_val;                                                              \
        p->b_exit[plane] = b_exit_val;                                                          \
        uint64_t bit = 1ULL << plane;                                                           \
        p->hysteresis_mask |= bit;                                                              \
        p->active_mask |= bit;                                                                  \
    }                                                                                           \
                                                                                                \
    static inline __attribute__((unused)) void NAME##_update_external_bounds(                   \
        NAME *p, const int64_t *b_ext, uint64_t mask) {                                         \
        if (!p || !b_ext)                                                                       \
            return;                                                                             \
        uint64_t clean_mask = mask & NAME##_valid_plane_mask();                                 \
        uint64_t rem = clean_mask;                                                              \
        while (rem != 0ULL) {                                                                   \
            uint32_t i = (uint32_t)__builtin_ctzll(rem);                                        \
            rem &= (rem - 1ULL);                                                                \
            p->b[i] = b_ext[i];                                                                 \
        }                                                                                       \
        p->active_mask = clean_mask;                                                            \
        p->generation += 1ULL;                                                                  \
    }                                                                                           \
                                                                                                \
    static inline __attribute__((unused)) FlowQPolyEval NAME##_eval_hysteresis(                 \
        const NAME *p, const int32_t x[DIM], uint64_t prev_violated_mask) {                     \
        if (!p || !x) {                                                                         \
            FlowQPolyEval empty = {0ULL, 0U, 0U, 0, 0};                                         \
            return empty;                                                                       \
        }                                                                                       \
        uint64_t valid_mask = NAME##_valid_plane_mask();                                        \
        uint64_t rem = p->active_mask & valid_mask;                                             \
        uint64_t use_exit_mask =                                                                \
            prev_violated_mask ? (prev_violated_mask & p->hysteresis_mask) : 0ULL;              \
        int32_t x_local[DIM];                                                                   \
        for (size_t j = 0; j < (size_t)(DIM); ++j) {                                            \
            x_local[j] = x[j];                                                                  \
        }                                                                                       \
        uint64_t violated_mask = 0ULL;                                                          \
        uint32_t worst_plane = 0U;                                                              \
        uint32_t tightest_plane = 0U;                                                           \
        int64_t worst_violation = 0;                                                            \
        int64_t min_slack = INT64_MAX;                                                          \
        bool any_violated = false;                                                              \
        bool any_satisfied = false;                                                             \
        while (rem != 0ULL) {                                                                   \
            uint32_t i = (uint32_t)__builtin_ctzll(rem);                                        \
            rem &= (rem - 1ULL);                                                                \
            FlowResultU64 dot_res = flow_asm_qdot_i32(p->A[i], x_local, (size_t)(DIM), NULL);   \
            int64_t dot = (int64_t)dot_res.value;                                               \
            bool ovf = (dot_res.boundary_tag != 0ULL);                                          \
            int64_t b_enter_i = p->b[i];                                                        \
            int64_t bi = b_enter_i;                                                             \
            if (__builtin_expect(use_exit_mask != 0ULL, 0)) {                                   \
                int64_t b_exit_i = p->b_exit[i];                                                \
                bi = flow_asm_select_i64((use_exit_mask >> i) & 1ULL, b_exit_i, b_enter_i);     \
            }                                                                                   \
            if (dot > bi || (ovf && dot == INT64_MAX)) {                                        \
                int64_t viol = ovf ? INT64_MAX : flow_asm_qsub_sat_pos_i64(dot, bi);            \
                violated_mask |= (1ULL << i);                                                   \
                if (!any_violated || viol > worst_violation) {                                  \
                    worst_violation = viol;                                                     \
                    worst_plane = i;                                                            \
                }                                                                               \
                any_violated = true;                                                            \
            } else {                                                                            \
                int64_t slack = flow_asm_qsub_sat_pos_i64(bi, dot);                             \
                if (!any_satisfied || slack < min_slack) {                                      \
                    min_slack = slack;                                                          \
                    tightest_plane = i;                                                         \
                }                                                                               \
                any_satisfied = true;                                                           \
            }                                                                                   \
        }                                                                                       \
        if (!any_satisfied)                                                                     \
            min_slack = 0;                                                                      \
        FlowQPolyEval res = {violated_mask, worst_plane, tightest_plane, worst_violation,       \
                             min_slack};                                                        \
        return res;                                                                             \
    }                                                                                           \
                                                                                                \
    static inline __attribute__((unused)) FlowQPolyEval NAME##_eval(const NAME *p,              \
                                                                    const int32_t x[DIM]) {     \
        return NAME##_eval_hysteresis(p, x, 0ULL);                                              \
    }                                                                                           \
                                                                                                \
    static inline __attribute__((unused)) int NAME##_quantize_inner(                            \
        NAME *p, size_t plane_count, const int64_t *a_num, const int64_t *b_num, int64_t denom, \
        const int64_t box_min[DIM], const int64_t box_max[DIM]) {                               \
        if (!p || plane_count == 0 || plane_count > (size_t)(MAX_PLANES))                       \
            return 0;                                                                           \
        NAME##_init(p);                                                                         \
        if (!flow_qpoly_quantize_inner(plane_count, (size_t)(DIM), (uint32_t)(SHIFT), a_num,    \
                                       b_num, denom, box_min, box_max, &p->A[0][0], p->b))      \
            return 0;                                                                           \
        for (size_t i = 0; i < plane_count; ++i) {                                              \
            p->b_exit[i] = p->b[i];                                                             \
        }                                                                                       \
        uint64_t mask = (plane_count >= 64U) ? ~0ULL : ((1ULL << plane_count) - 1ULL);          \
        p->active_mask = mask & NAME##_valid_plane_mask();                                      \
        return 1;                                                                               \
    }

#define FLOW_QPOLY_DEFINE(NAME, DIM, SHIFT) \
    FLOW_QPOLY_DEFINE_EX(NAME, DIM, SHIFT, FLOW_QPOLY_PLANES)

#ifdef __cplusplus
}
#endif

#endif /* FLOW_HOTT_JET_H */
