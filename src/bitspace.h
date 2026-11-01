#ifndef FLOW_BITSPACE_H
#define FLOW_BITSPACE_H
#include "flow.h"
#include "reload.h"
#include "cubical_hott.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#if !defined(__cplusplus)
#include <stdatomic.h>
#endif

static inline uint64_t flow_manifold_transition(uint64_t genome, uint64_t mask, uint64_t *rng_state,
                                                uint32_t *mutated_bit_out) {
    if (!mask) {
        if (mutated_bit_out) {
            *mutated_bit_out = 0;
        }
        return genome;
    }
    uint64_t r = flow_prng_next(rng_state);
    int bit_positions[64], count = 0;
    for (int i = 0; i < 64; ++i) {
        if ((mask >> i) & 1ULL) {
            bit_positions[count++] = i;
        }
    }
    if (!count) {
        if (mutated_bit_out) {
            *mutated_bit_out = 0;
        }
        return genome;
    }
    int chosen_idx = bit_positions[r % (uint64_t)count];
    if (mutated_bit_out) {
        *mutated_bit_out = (uint32_t)chosen_idx;
    }
    return genome ^ (1ULL << chosen_idx);
}

typedef FlowBmf1BitCanvas FlowBmfCanvas;
#define FLOW_ATOMIC_STAGE_SWAP(dst, src)                                                        \
    do {                                                                                        \
        if ((dst) && (src)) {                                                                   \
            __atomic_store_n(&((dst)->subspace_id),                                             \
                             __atomic_load_n(&((src)->subspace_id), __ATOMIC_RELAXED),          \
                             __ATOMIC_RELAXED);                                                 \
            __atomic_store_n(&((dst)->invariant_mask),                                          \
                             __atomic_load_n(&((src)->invariant_mask), __ATOMIC_RELAXED),       \
                             __ATOMIC_RELAXED);                                                 \
            __atomic_store_n(&((dst)->malleable_mask),                                          \
                             __atomic_load_n(&((src)->malleable_mask), __ATOMIC_RELAXED),       \
                             __ATOMIC_RELAXED);                                                 \
            __atomic_store_n(&((dst)->dynamic_bias),                                            \
                             __atomic_load_n(&((src)->dynamic_bias), __ATOMIC_RELAXED),         \
                             __ATOMIC_RELAXED);                                                 \
            uint64_t _ebits = __atomic_load_n((const uint64_t *)(const void *)&((src)->energy), \
                                              __ATOMIC_RELAXED);                                \
            __atomic_store_n((uint64_t *)(void *)&((dst)->energy), _ebits, __ATOMIC_RELAXED);   \
            __atomic_store_n(&((dst)->is_adjudicated_sound),                                    \
                             __atomic_load_n(&((src)->is_adjudicated_sound), __ATOMIC_RELAXED), \
                             __ATOMIC_RELEASE);                                                 \
            __atomic_store_n(&((dst)->switchboard_bits),                                        \
                             __atomic_load_n(&((src)->switchboard_bits), __ATOMIC_ACQUIRE),     \
                             __ATOMIC_RELEASE);                                                 \
        }                                                                                       \
    } while (0)
static inline FlowBmf1BitCanvas flow_atomic_stage_load_impl(const FlowBmf1BitCanvas *src) {
    FlowBmf1BitCanvas _out = {0};
    if (!src)
        return _out;
    _out.switchboard_bits = __atomic_load_n(&src->switchboard_bits, __ATOMIC_ACQUIRE);
    _out.is_adjudicated_sound = __atomic_load_n(&src->is_adjudicated_sound, __ATOMIC_ACQUIRE);
    _out.subspace_id = __atomic_load_n(&src->subspace_id, __ATOMIC_RELAXED);
    _out.invariant_mask = __atomic_load_n(&src->invariant_mask, __ATOMIC_RELAXED);
    _out.malleable_mask = __atomic_load_n(&src->malleable_mask, __ATOMIC_RELAXED);
    _out.dynamic_bias = __atomic_load_n(&src->dynamic_bias, __ATOMIC_RELAXED);
    union {
        uint64_t u;
        double d;
    } _eu;
    _eu.u = __atomic_load_n((const uint64_t *)(const void *)&src->energy, __ATOMIC_RELAXED);
    _out.energy = _eu.d;
    return _out;
}
#define FLOW_ATOMIC_STAGE_LOAD(src) flow_atomic_stage_load_impl(src)
static inline void flow_bmf_canvas_init(FlowBmf1BitCanvas *canvas, uint32_t subspace_id,
                                        uint64_t invariant_mask, uint64_t malleable_mask,
                                        uint64_t initial_switches) {
    if (!canvas) {
        return;
    }
    canvas->subspace_id = subspace_id;
    canvas->invariant_mask = invariant_mask;
    canvas->malleable_mask = malleable_mask;
    canvas->switchboard_bits = (initial_switches | invariant_mask);
    canvas->dynamic_bias = 0;
    canvas->energy = 0.0;
    canvas->is_adjudicated_sound = 0;
}
static inline int flow_bmf_canvas_get_switch(const FlowBmf1BitCanvas *canvas,
                                             uint64_t switch_flag) {
    return canvas ? ((canvas->switchboard_bits & switch_flag) != 0) : 0;
}
static inline void flow_bmf_canvas_set_switch(FlowBmf1BitCanvas *canvas, uint64_t switch_flag,
                                              int value) {
    if (!canvas || (!value && (canvas->invariant_mask & switch_flag))) {
        return;
    }
    if (value) {
        canvas->switchboard_bits |= switch_flag;
    } else {
        canvas->switchboard_bits &= ~switch_flag;
    }
    canvas->is_adjudicated_sound = 0;
}
static inline int flow_bmf_canvas_verify_invariants(const FlowBmf1BitCanvas *canvas) {
    return canvas ? ((canvas->switchboard_bits & canvas->invariant_mask) == canvas->invariant_mask)
                  : 0;
}
static inline uint64_t flow_bmf_canvas_flip_1bit(FlowBmf1BitCanvas *canvas, uint64_t *rng_state,
                                                 uint32_t *mutated_bit_out) {
    if (!canvas) {
        return 0;
    }
    uint64_t flippable_mask = canvas->malleable_mask & ~canvas->invariant_mask;
    if (!flippable_mask) {
        if (mutated_bit_out) {
            *mutated_bit_out = 0;
        }
        return canvas->switchboard_bits;
    }
    canvas->switchboard_bits = flow_manifold_transition(canvas->switchboard_bits, flippable_mask,
                                                        rng_state, mutated_bit_out);
    canvas->is_adjudicated_sound = 0;
    return canvas->switchboard_bits;
}

/* =========================================================================
 * FLOW Exact Numerics & IEEE-754 / 2-Jet / BigInt Engine
 *   1. math: Fast Integer Approximations (exp_approx_q16, rsqrt_approx_i32, silu_approx_i8)
 *            & Hardware IEEE-754 Double + 2-Jet Forward AD
 *   2. bignum: U256 ([uint64_t; 4]), 512-bit mul_wide, Montgomery Reduction, Miller-Rabin
 *   3. linalg: Stack-allocated i-k-j IEEE-754 Matrix, MSE Loss & Gradient Descent
 *   4. random: Pure bitwise Xoshiro256** + SplitMix64 PRNG
 * ========================================================================= */
#ifndef FLOW_EXACT_NUMERICS_DEFINED
#define FLOW_EXACT_NUMERICS_DEFINED
__extension__ typedef __int128 flow_i128_t;
__extension__ typedef unsigned __int128 flow_u128_t;

#define FLOW_STR_INNER(...)     #__VA_ARGS__
#define FLOW_STR(...)           FLOW_STR_INNER(__VA_ARGS__)

#define FLOW_FX16_SHIFT         16
#define FLOW_FX16_ONE           (1 << FLOW_FX16_SHIFT)
#define FLOW_FX16_EXP_BOUND     (10 * FLOW_FX16_ONE)
#define FLOW_FX16_LOG2E         INT64_C(94548)

#define FLOW_U256_LIMBS         4
#define FLOW_U512_LIMBS         8
#define FLOW_U256_BITS          256U
#define FLOW_WORD_BITS          64U
#define FLOW_NEWTON_64BIT_ITERS 5
#define FLOW_SPLITMIX64_MUL1    UINT64_C(0xbf58476d1ce4e5b9)
#define FLOW_SPLITMIX64_MUL2    UINT64_C(0x94d049bb133111eb)

#define FLOW_EXACT_CODEC_C17                                                                     \
    static inline __attribute__((unused)) int flow_exp_approx_q16(int32_t x, int32_t *out) {     \
        if (!out) {                                                                              \
            return 0;                                                                            \
        }                                                                                        \
        if (x < -FLOW_FX16_EXP_BOUND) {                                                          \
            *out = 0;                                                                            \
            return 1;                                                                            \
        }                                                                                        \
        if (x > FLOW_FX16_EXP_BOUND) {                                                           \
            return 0;                                                                            \
        }                                                                                        \
        int32_t x_scaled = (int32_t)(((int64_t)x * FLOW_FX16_LOG2E) >> FLOW_FX16_SHIFT);         \
        int32_t int_part = x_scaled >> FLOW_FX16_SHIFT;                                          \
        int32_t frac_part = x_scaled & (FLOW_FX16_ONE - 1);                                      \
        int32_t approx_frac = FLOW_FX16_ONE + frac_part;                                         \
        if (int_part >= 0) {                                                                     \
            *out = (int32_t)((uint32_t)approx_frac << int_part);                                 \
        } else {                                                                                 \
            *out = approx_frac >> (-int_part);                                                   \
        }                                                                                        \
        return 1;                                                                                \
    }                                                                                            \
    static inline __attribute__((unused)) int flow_rsqrt_approx_i32(uint32_t x, uint32_t *out) { \
        if (!out || x == 0U) {                                                                   \
            return 0;                                                                            \
        }                                                                                        \
        uint32_t msb = 31U - (uint32_t)__builtin_clz(x);                                         \
        uint32_t sqrt_approx = 1U << (msb >> 1U);                                                \
        *out = (uint32_t)FLOW_FX16_ONE / sqrt_approx;                                            \
        return 1;                                                                                \
    }                                                                                            \
    static inline __attribute__((unused)) int flow_silu_approx_i8(int8_t x, int8_t *out) {       \
        if (!out) {                                                                              \
            return 0;                                                                            \
        }                                                                                        \
        int32_t x_q16 = (int32_t)((uint32_t)(int32_t)x << FLOW_FX16_SHIFT);                      \
        int32_t exp_neg_x = 0;                                                                   \
        if (!flow_exp_approx_q16(-x_q16, &exp_neg_x)) {                                          \
            return 0;                                                                            \
        }                                                                                        \
        int32_t denom = FLOW_FX16_ONE + exp_neg_x;                                               \
        if (denom == 0) {                                                                        \
            return 0;                                                                            \
        }                                                                                        \
        int64_t result = ((int64_t)x_q16 * (int64_t)FLOW_FX16_ONE) / (int64_t)denom;             \
        int32_t res_i32 = (int32_t)(result >> FLOW_FX16_SHIFT);                                  \
        if (res_i32 < INT8_MIN) {                                                                \
            res_i32 = INT8_MIN;                                                                  \
        } else if (res_i32 > INT8_MAX) {                                                         \
            res_i32 = INT8_MAX;                                                                  \
        }                                                                                        \
        *out = (int8_t)res_i32;                                                                  \
        return 1;                                                                                \
    }

#define FLOW_LEAN_NAT_OPS_C17                                                                   \
    static inline __attribute__((unused)) uint64_t flow_nat_sub(uint64_t a, uint64_t b) {       \
        return a >= b ? (a - b) : 0ULL;                                                         \
    }                                                                                           \
    static inline __attribute__((unused)) uint64_t flow_nat_div(uint64_t a, uint64_t b) {       \
        return b == 0ULL ? 0ULL : (a / b);                                                      \
    }                                                                                           \
    static inline __attribute__((unused)) uint64_t flow_nat_mod(uint64_t a, uint64_t b) {       \
        return b == 0ULL ? a : (a % b);                                                         \
    }                                                                                           \
    static inline __attribute__((unused)) uint64_t flow_nat_pow(uint64_t base, uint64_t exp) {  \
        uint64_t r = 1ULL;                                                                      \
        while (exp > 0ULL) {                                                                    \
            if (exp & 1ULL) {                                                                   \
                r *= base;                                                                      \
            }                                                                                   \
            base *= base;                                                                       \
            exp >>= 1U;                                                                         \
        }                                                                                       \
        return r;                                                                               \
    }                                                                                           \
    static inline __attribute__((unused)) uint64_t flow_nat_min(uint64_t a, uint64_t b) {       \
        return a < b ? a : b;                                                                   \
    }                                                                                           \
    static inline __attribute__((unused)) uint64_t flow_nat_max(uint64_t a, uint64_t b) {       \
        return a > b ? a : b;                                                                   \
    }                                                                                           \
    static inline __attribute__((unused)) int64_t flow_int_div(int64_t a, int64_t b) {          \
        if (b == 0)                                                                             \
            return 0;                                                                           \
        if (a == INT64_MIN && b == -1)                                                          \
            return INT64_MIN;                                                                   \
        int64_t q = a / b;                                                                      \
        int64_t r = a % b;                                                                      \
        if (r < 0) {                                                                            \
            q = (b > 0) ? (q - 1) : (int64_t)((uint64_t)q + 1ULL);                              \
        }                                                                                       \
        return q;                                                                               \
    }                                                                                           \
    static inline __attribute__((unused)) int64_t flow_int_mod(int64_t a, int64_t b) {          \
        if (b == 0)                                                                             \
            return a;                                                                           \
        if (a == INT64_MIN && b == -1)                                                          \
            return 0;                                                                           \
        int64_t r = a % b;                                                                      \
        if (r < 0) {                                                                            \
            r = (b > 0) ? (int64_t)((uint64_t)r + (uint64_t)b)                                  \
                        : (int64_t)((uint64_t)r - (uint64_t)b);                                 \
        }                                                                                       \
        return r;                                                                               \
    }                                                                                           \
    static inline __attribute__((unused)) int64_t flow_int_tdiv(int64_t a, int64_t b) {         \
        if (b == 0)                                                                             \
            return 0;                                                                           \
        if (a == INT64_MIN && b == -1)                                                          \
            return INT64_MIN;                                                                   \
        return a / b;                                                                           \
    }                                                                                           \
    static inline __attribute__((unused)) int64_t flow_int_tmod(int64_t a, int64_t b) {         \
        if (b == 0)                                                                             \
            return a;                                                                           \
        if (a == INT64_MIN && b == -1)                                                          \
            return 0;                                                                           \
        return a % b;                                                                           \
    }                                                                                           \
    static inline __attribute__((unused)) int64_t flow_int_pow(int64_t base, int64_t exp) {     \
        if (exp < 0) {                                                                          \
            return 0;                                                                           \
        }                                                                                       \
        int64_t r = 1;                                                                          \
        while (exp > 0) {                                                                       \
            if (exp & 1) {                                                                      \
                r *= base;                                                                      \
            }                                                                                   \
            base *= base;                                                                       \
            exp >>= 1;                                                                          \
        }                                                                                       \
        return r;                                                                               \
    }                                                                                           \
    static inline __attribute__((unused)) int64_t flow_int_abs(int64_t x) {                     \
        return x == INT64_MIN ? INT64_MAX : (x < 0 ? -x : x);                                   \
    }                                                                                           \
    static inline __attribute__((unused)) int64_t flow_int_min(int64_t a, int64_t b) {          \
        return a < b ? a : b;                                                                   \
    }                                                                                           \
    static inline __attribute__((unused)) int64_t flow_int_max(int64_t a, int64_t b) {          \
        return a > b ? a : b;                                                                   \
    }                                                                                           \
    static inline __attribute__((unused)) int64_t flow_int_of_nat(uint64_t x) {                 \
        return (int64_t)x;                                                                      \
    }                                                                                           \
    static inline __attribute__((unused)) uint64_t flow_quantize(double val, double min_v,      \
                                                                 double max_v, uint32_t bits) { \
        if (!__builtin_isfinite(val) || !__builtin_isfinite(min_v) ||                           \
            !__builtin_isfinite(max_v) || max_v <= min_v || bits == 0 || bits > 32U)            \
            return 0ULL;                                                                        \
        uint64_t max_q = (bits == 32U) ? 0xFFFFFFFFULL : ((1ULL << bits) - 1ULL);               \
        double c = val < min_v ? min_v : (val > max_v ? max_v : val);                           \
        return (uint64_t)(((c - min_v) / (max_v - min_v)) * (double)max_q + 0.5);               \
    }                                                                                           \
    static inline __attribute__((unused)) double flow_dequantize(uint64_t code, double min_v,   \
                                                                 double max_v, uint32_t bits) { \
        if (!__builtin_isfinite(min_v) || !__builtin_isfinite(max_v) || max_v <= min_v ||       \
            bits == 0 || bits > 32U)                                                            \
            return 0.0;                                                                         \
        uint64_t max_q = (bits == 32U) ? 0xFFFFFFFFULL : ((1ULL << bits) - 1ULL);               \
        uint64_t c = code > max_q ? max_q : code;                                               \
        return min_v + ((double)c / (double)max_q) * (max_v - min_v);                           \
    }

#define FLOW_JET_SCALAR2_TYPEDEF_C17 \
    typedef struct {                 \
        double v;                    \
        double d1;                   \
        double d2;                   \
    } FlowJetScalar2;

#ifndef FLOW_JET_SCALAR2_DEFINED
#define FLOW_JET_SCALAR2_DEFINED
FLOW_JET_SCALAR2_TYPEDEF_C17
#endif

#define FLOW_ABI_CORE_TYPES_C17                                    \
    typedef enum {                                                 \
        FLOW_OK = 0,                                               \
        FLOW_ERR_INVALID_ARG = -1,                                 \
        FLOW_ERR_QUOTA_OR_STRIDE = -2,                             \
        FLOW_ERR_KERNEL_UNBOUND = -3,                              \
        FLOW_ERR_WATCHDOG_ESTOP = -4                               \
    } FlowStatusCode;                                              \
    typedef struct {                                               \
        uint64_t base_offset, byte_length, qsbr_epoch;             \
        uint32_t stride_bytes, align_bytes, schema_version, flags; \
        uint64_t reserved[3];                                      \
    } FlowExtentDescriptor;

#ifndef FLOW_ABI_CORE_TYPES_DEFINED
#define FLOW_ABI_CORE_TYPES_DEFINED
FLOW_ABI_CORE_TYPES_C17
#endif

#define FLOW_JET2_ALU_C17                                                                    \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_make(double v, double d1, \
                                                                        double d2) {         \
        FlowJetScalar2 r;                                                                    \
        r.v = __builtin_isfinite(v) ? v : 0.0;                                               \
        r.d1 = __builtin_isfinite(d1) ? d1 : 0.0;                                            \
        r.d2 = __builtin_isfinite(d2) ? d2 : 0.0;                                            \
        return r;                                                                            \
    }                                                                                        \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_var(double v) {           \
        return flow_jet2_make(v, 1.0, 0.0);                                                  \
    }                                                                                        \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_const(double c) {         \
        return flow_jet2_make(c, 0.0, 0.0);                                                  \
    }                                                                                        \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_of_nat(uint64_t x) {      \
        return flow_jet2_make((double)x, 0.0, 0.0);                                          \
    }                                                                                        \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_of_int(int64_t x) {       \
        return flow_jet2_make((double)x, 0.0, 0.0);                                          \
    }                                                                                        \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_add(FlowJetScalar2 a,     \
                                                                       FlowJetScalar2 b) {   \
        return flow_jet2_make(a.v + b.v, a.d1 + b.d1, a.d2 + b.d2);                          \
    }                                                                                        \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_sub(FlowJetScalar2 a,     \
                                                                       FlowJetScalar2 b) {   \
        return flow_jet2_make(a.v - b.v, a.d1 - b.d1, a.d2 - b.d2);                          \
    }                                                                                        \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_mul(FlowJetScalar2 a,     \
                                                                       FlowJetScalar2 b) {   \
        return flow_jet2_make(a.v * b.v, a.d1 * b.v + a.v * b.d1,                            \
                              a.d2 * b.v + 2.0 * a.d1 * b.d1 + a.v * b.d2);                  \
    }                                                                                        \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_div(FlowJetScalar2 a,     \
                                                                       FlowJetScalar2 b) {   \
        if (b.v == 0.0 || !__builtin_isfinite(b.v)) {                                        \
            return flow_jet2_const(0.0);                                                     \
        }                                                                                    \
        double v = a.v / b.v;                                                                \
        double d1 = (a.d1 - v * b.d1) / b.v;                                                 \
        double d2 = (a.d2 - 2.0 * d1 * b.d1 - v * b.d2) / b.v;                               \
        return flow_jet2_make(v, d1, d2);                                                    \
    }                                                                                        \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_mod(FlowJetScalar2 a,     \
                                                                       FlowJetScalar2 b) {   \
        if (b.v == 0.0 || !__builtin_isfinite(b.v)) {                                        \
            return flow_jet2_const(0.0);                                                     \
        }                                                                                    \
        double q = __builtin_floor(a.v / b.v);                                               \
        return flow_jet2_make(__builtin_fmod(a.v, b.v), a.d1 - q * b.d1, a.d2 - q * b.d2);   \
    }                                                                                        \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_neg(FlowJetScalar2 x) {   \
        return flow_jet2_make(-x.v, -x.d1, -x.d2);                                           \
    }                                                                                        \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_abs(FlowJetScalar2 x) {   \
        double s = (x.v > 0.0) ? 1.0 : ((x.v < 0.0) ? -1.0 : 0.0);                           \
        return flow_jet2_make(__builtin_fabs(x.v), s * x.d1, s * x.d2);                      \
    }                                                                                        \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_floor(FlowJetScalar2 x) { \
        return flow_jet2_make(__builtin_floor(x.v), 0.0, 0.0);                               \
    }                                                                                        \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_ceil(FlowJetScalar2 x) {  \
        return flow_jet2_make(__builtin_ceil(x.v), 0.0, 0.0);                                \
    }                                                                                        \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_round(FlowJetScalar2 x) { \
        return flow_jet2_make(__builtin_round(x.v), 0.0, 0.0);                               \
    }

#define FLOW_JET2_MATH_C17                                                                    \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_sqrt(FlowJetScalar2 x) {   \
        if (x.v <= 0.0) {                                                                     \
            return flow_jet2_const(0.0);                                                      \
        }                                                                                     \
        double r = __builtin_sqrt(x.v);                                                       \
        double d1 = x.d1 / (2.0 * r);                                                         \
        double d2 = (x.d2 - 2.0 * d1 * d1) / (2.0 * r);                                       \
        return flow_jet2_make(r, d1, d2);                                                     \
    }                                                                                         \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_exp(FlowJetScalar2 x) {    \
        double e = __builtin_exp(x.v);                                                        \
        return flow_jet2_make(e, e * x.d1, e * (x.d1 * x.d1 + x.d2));                         \
    }                                                                                         \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_log(FlowJetScalar2 x) {    \
        if (x.v <= 0.0) {                                                                     \
            return flow_jet2_const(0.0);                                                      \
        }                                                                                     \
        double d1 = x.d1 / x.v;                                                               \
        double d2 = (x.d2 - d1 * x.d1) / x.v;                                                 \
        return flow_jet2_make(__builtin_log(x.v), d1, d2);                                    \
    }                                                                                         \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_sin(FlowJetScalar2 x) {    \
        double s = __builtin_sin(x.v), c = __builtin_cos(x.v);                                \
        return flow_jet2_make(s, c * x.d1, -s * x.d1 * x.d1 + c * x.d2);                      \
    }                                                                                         \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_cos(FlowJetScalar2 x) {    \
        double s = __builtin_sin(x.v), c = __builtin_cos(x.v);                                \
        return flow_jet2_make(c, -s * x.d1, -c * x.d1 * x.d1 - s * x.d2);                     \
    }                                                                                         \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_atan2(FlowJetScalar2 y,    \
                                                                         FlowJetScalar2 x) {  \
        double r2 = x.v * x.v + y.v * y.v;                                                    \
        if (r2 <= 0.0) {                                                                      \
            return flow_jet2_const(0.0);                                                      \
        }                                                                                     \
        double d1 = (x.v * y.d1 - y.v * x.d1) / r2;                                           \
        double d2 = ((x.v * y.d2 - y.v * x.d2) - 2.0 * d1 * (x.v * x.d1 + y.v * y.d1)) / r2;  \
        return flow_jet2_make(__builtin_atan2(y.v, x.v), d1, d2);                             \
    }                                                                                         \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_pow(FlowJetScalar2 b,      \
                                                                       FlowJetScalar2 e) {    \
        if (e.d1 == 0.0 && e.d2 == 0.0) {                                                     \
            double p = e.v;                                                                   \
            double v = __builtin_pow(b.v, p);                                                 \
            double gp = (p == 0.0) ? 0.0 : (p * __builtin_pow(b.v, p - 1.0));                 \
            double gpp =                                                                      \
                (p == 0.0 || p == 1.0) ? 0.0 : (p * (p - 1.0) * __builtin_pow(b.v, p - 2.0)); \
            return flow_jet2_make(v, gp * b.d1, gpp * b.d1 * b.d1 + gp * b.d2);               \
        }                                                                                     \
        if (b.v <= 0.0) {                                                                     \
            return flow_jet2_const(0.0);                                                      \
        }                                                                                     \
        return flow_jet2_exp(flow_jet2_mul(e, flow_jet2_log(b)));                             \
    }                                                                                         \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_max(FlowJetScalar2 a,      \
                                                                       FlowJetScalar2 b) {    \
        return (a.v >= b.v) ? a : b;                                                          \
    }                                                                                         \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_min(FlowJetScalar2 a,      \
                                                                       FlowJetScalar2 b) {    \
        return (a.v <= b.v) ? a : b;                                                          \
    }

#define FLOW_JET2_OPS_C17 FLOW_JET2_ALU_C17 FLOW_JET2_MATH_C17

FLOW_EXACT_CODEC_C17
#ifndef FLOW_LEAN_NAT_OPS_DEFINED
#define FLOW_LEAN_NAT_OPS_DEFINED
FLOW_LEAN_NAT_OPS_C17
FLOW_JET2_OPS_C17
#endif

typedef struct {
    uint64_t w[FLOW_U256_LIMBS];
} FlowU256;

FlowU256 flow_u256_zero(void);
FlowU256 flow_u256_one(void);
FlowU256 flow_u256_from_u64(uint64_t v);
int flow_u256_from_hex_str(const char *hex, FlowU256 *out);
int flow_u256_is_zero(const FlowU256 *a);
int flow_u256_is_even(const FlowU256 *a);
uint32_t flow_u256_leading_zeros(const FlowU256 *a);
int flow_u256_cmp(const FlowU256 *a, const FlowU256 *b);
FlowU256 flow_u256_overflowing_add(FlowU256 a, FlowU256 b, int *overflow_out);
FlowU256 flow_u256_overflowing_sub(FlowU256 a, FlowU256 b, int *borrow_out);
FlowU256 flow_u256_shl1(FlowU256 a);
FlowU256 flow_u256_shr1(FlowU256 a);
void flow_u256_mul_wide(FlowU256 a, FlowU256 b, uint64_t out_512[FLOW_U512_LIMBS]);
FlowU256 flow_u256_mul(FlowU256 a, FlowU256 b);
int flow_u256_div_rem(FlowU256 a, FlowU256 b, FlowU256 *q_out, FlowU256 *r_out);
uint64_t flow_u256_inv_word(const FlowU256 *n);
FlowU256 flow_u256_mont_reduce(const uint64_t t_in[FLOW_U512_LIMBS], const FlowU256 *modulus,
                               uint64_t n0_inv);
FlowU256 flow_u256_mod_pow(FlowU256 base, FlowU256 exp, FlowU256 modulus);
int flow_u256_is_probably_prime(const FlowU256 *n, const uint64_t *bases, size_t base_count);

#endif /* FLOW_EXACT_NUMERICS_DEFINED */

#endif /* FLOW_BITSPACE_H */
