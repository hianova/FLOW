#ifndef FLOW_H
#define FLOW_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE 1
#endif

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif


#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#if !defined(__cplusplus)
#include <stdatomic.h>
#endif


#if defined(FLOW_FREESTANDING) || (defined(__STDC_HOSTED__) && (__STDC_HOSTED__ == 0))
#define FLOW_IS_FREESTANDING 1
#else
#define FLOW_IS_FREESTANDING 0
#include <time.h>
#endif


#include "flow_ssot_gen.h"

#ifdef __cplusplus
extern "C" {
#endif


/*
 * ============================================================================
 * FLOW: Total Deterministic ISA Substrate (L0 First Principles Core)
 * ============================================================================
 * Minimal orthogonal primitives for zero-drift, zero-float deterministic hardware:
 *   1. Hardware Bit Algebra & Timing (CLZ, CTZ, POPCNT, BSWAP, RBIT, cycles)
 *   2. Single Error Model: 10-bit Hardware Sieve & NaN-boxed BoundaryTag
 *   3. FlowI128 / FlowU128 Unified 6 Core Primitives (0 #if forks in upper math)
 *   4. Total Signed Q16 Arithmetic with Hardware NZCV.V Capture
 *   5. 192-Entry Chebyshev Minimax Single-`udiv` Integer Square Root
 *   6. Wave64 Ballot & 1.58-Bit Ternary Dot Product
 *   7. Masked Cyclic Memory Arena (Z / 2^k Z)
 *   8. Goldilocks Prime Field F_p (p = 2^64 - 2^32 + 1) & 64B Capsule Sponge MAC
 *   9. Pure Q48.16 2-Jet Fiber Algebra & Normalized Citardauq Root Solver
 *  10. Universal External-Constraint Polyhedron (FlowQPoly)
 *  11. Hardware SPSC Ring, Page-Aligned UMA & Lock-Free QSBR Pointer Swapping
 * ============================================================================
 */

#define FLOW_VERSION_MAJOR 5
#define FLOW_VERSION_MINOR 0
#define FLOW_VERSION_PATCH 0
#ifndef FLOW_VERSION_STRING
#define FLOW_VERSION_STRING "5.0.0"
#endif


#ifndef FLOW_HW_PAGE_BYTES
#define FLOW_HW_PAGE_BYTES FLOW_SSOT_HW_PAGE_BYTES
#endif

#ifndef FLOW_HW_CACHE_LINE_BYTES
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
#define FLOW_HW_CACHE_LINE_BYTES FLOW_SSOT_HW_CACHE_LINE_APPLE_BYTES
#else
#define FLOW_HW_CACHE_LINE_BYTES FLOW_SSOT_HW_CACHE_LINE_BYTES
#endif

#endif

#ifndef FLOW_HW_L1D_BYTES
#define FLOW_HW_L1D_BYTES FLOW_SSOT_HW_L1D_BYTES
#endif

#ifndef FLOW_HW_VECTOR_BYTES
#define FLOW_HW_VECTOR_BYTES FLOW_SSOT_HW_VECTOR_BYTES
#endif

#ifndef FLOW_HW_WAVE_LANES
#define FLOW_HW_WAVE_LANES FLOW_SSOT_HW_WAVE_LANES
#endif

#ifndef FLOW_HW_FALLBACK_X86_HZ
#define FLOW_HW_FALLBACK_X86_HZ FLOW_SSOT_HW_FALLBACK_X86_HZ
#endif

#ifndef FLOW_HW_FALLBACK_ARM_HZ
#define FLOW_HW_FALLBACK_ARM_HZ FLOW_SSOT_HW_FALLBACK_ARM_HZ
#endif

#ifndef FLOW_HW_FALLBACK_GENERIC_HZ
#define FLOW_HW_FALLBACK_GENERIC_HZ FLOW_SSOT_HW_FALLBACK_GENERIC_HZ
#endif


#define FLOW_CACHE_LINE_SIZE    FLOW_HW_CACHE_LINE_BYTES
#define FLOW_CACHE_ALIGNED      __attribute__((aligned(FLOW_HW_CACHE_LINE_BYTES)))
#define FLOW_WAVE64_SIZE        FLOW_SSOT_WAVE64_SIZE
#define FLOW_Q16_SHIFT          FLOW_SSOT_Q16_SHIFT
#define FLOW_Q16_ONE            FLOW_SSOT_Q16_ONE
#define FLOW_GOLDEN_RATIO_64    FLOW_SSOT_GOLDEN_RATIO_64

#ifndef FLOW_HAS_INT128
#if defined(__SIZEOF_INT128__) && !defined(FLOW_FORCE_32BIT_FALLBACK)
#define FLOW_HAS_INT128 1
#else
#define FLOW_HAS_INT128 0
#endif

#endif


/* ============================================================================
 * 1. Hardware Bit Algebra & Portable SWAR Primitives
 * ============================================================================ */

static inline uint32_t flow_clz32_nonzero_swar(uint32_t x) {
    uint32_t n = 0U;
    if ((x & 0xFFFF0000U) == 0U) { n += 16U; x <<= 16U; }
    if ((x & 0xFF000000U) == 0U) { n += 8U;  x <<= 8U;  }
    if ((x & 0xF0000000U) == 0U) { n += 4U;  x <<= 4U;  }
    if ((x & 0xC0000000U) == 0U) { n += 2U;  x <<= 2U;  }
    if ((x & 0x80000000U) == 0U) { n += 1U; }
    return n;
}

static inline uint32_t flow_ctz32_nonzero_swar(uint32_t x) {
    uint32_t n = 0U;
    if ((x & 0x0000FFFFU) == 0U) { n += 16U; x >>= 16U; }
    if ((x & 0x000000FFU) == 0U) { n += 8U;  x >>= 8U;  }
    if ((x & 0x0000000FU) == 0U) { n += 4U;  x >>= 4U;  }
    if ((x & 0x00000003U) == 0U) { n += 2U;  x >>= 2U;  }
    if ((x & 0x00000001U) == 0U) { n += 1U; }
    return n;
}

static inline uint32_t flow_clz64_nonzero(uint64_t x) {
#if FLOW_HAS_INT128 && !defined(FLOW_FORCE_C17_FALLBACK)
    return (uint32_t)__builtin_clzll(x);
#else
    uint32_t hi = (uint32_t)(x >> 32U);
    return (hi != 0U) ? flow_clz32_nonzero_swar(hi) : (32U + flow_clz32_nonzero_swar((uint32_t)x));
#endif

}

static inline uint32_t flow_ctz64_nonzero(uint64_t x) {
#if FLOW_HAS_INT128 && !defined(FLOW_FORCE_C17_FALLBACK)
    return (uint32_t)__builtin_ctzll(x);
#else
    uint32_t lo = (uint32_t)x;
    return (lo != 0U) ? flow_ctz32_nonzero_swar(lo) : (32U + flow_ctz32_nonzero_swar((uint32_t)(x >> 32U)));
#endif

}

static inline uint32_t flow_popcnt64(uint64_t x) {
#if FLOW_HAS_INT128 && !defined(FLOW_FORCE_C17_FALLBACK)
    return (uint32_t)__builtin_popcountll(x);
#else
    x = x - ((x >> 1U) & UINT64_C(0x5555555555555555));
    x = (x & UINT64_C(0x3333333333333333)) + ((x >> 2U) & UINT64_C(0x3333333333333333));
    x = (x + (x >> 4U)) & UINT64_C(0x0F0F0F0F0F0F0F0F);
    uint32_t fold32 = (uint32_t)x + (uint32_t)(x >> 32U);
    return (fold32 * 0x01010101U) >> 24U;
#endif

}

static inline uint64_t flow_bswap64_portable(uint64_t x) {
#if FLOW_HAS_INT128 && !defined(FLOW_FORCE_C17_FALLBACK)
    return __builtin_bswap64(x);
#else
    x = ((x >> 8U) & UINT64_C(0x00FF00FF00FF00FF)) | ((x & UINT64_C(0x00FF00FF00FF00FF)) << 8U);
    x = ((x >> 16U) & UINT64_C(0x0000FFFF0000FFFF)) | ((x & UINT64_C(0x0000FFFF0000FFFF)) << 16U);
    return (x >> 32U) | (x << 32U);
#endif

}

static inline uint64_t flow_asm_rbit64(uint64_t x) {
    uint64_t out = x;
#if !defined(FLOW_FORCE_C17_FALLBACK) && (defined(__aarch64__) || defined(__arm64__))
    __asm__("rbit %[out], %[in]" : [out] "=r"(out) : [in] "r"(x));
#else
    out = ((out >> 1U) & UINT64_C(0x5555555555555555)) | ((out & UINT64_C(0x5555555555555555)) << 1U);
    out = ((out >> 2U) & UINT64_C(0x3333333333333333)) | ((out & UINT64_C(0x3333333333333333)) << 2U);
    out = ((out >> 4U) & UINT64_C(0x0F0F0F0F0F0F0F0F)) | ((out & UINT64_C(0x0F0F0F0F0F0F0F0F)) << 4U);
    out = flow_bswap64_portable(out);
#endif

    return out;
}

static inline uint64_t flow_hardware_cycles(void) {
#if !defined(FLOW_FORCE_C17_FALLBACK) && (defined(__aarch64__) || defined(__arm64__))
    uint64_t val = 0;
    __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(val));
    return val;
#elif !defined(FLOW_FORCE_C17_FALLBACK) && (defined(__x86_64__) || defined(_M_X64))
    return __builtin_ia32_rdtsc();
#elif !defined(FLOW_FORCE_C17_FALLBACK) && defined(__xtensa__)
    uint32_t ccount = 0;
    __asm__ __volatile__("rsr.ccount %0" : "=a"(ccount));
    return (uint64_t)ccount;
#elif !defined(FLOW_FORCE_C17_FALLBACK) && defined(__riscv)
    uint32_t cyc = 0;
    __asm__ __volatile__("rdcycle %0" : "=r"(cyc));
    return (uint64_t)cyc;
#elif !FLOW_IS_FREESTANDING && defined(CLOCK_MONOTONIC)
    struct timespec ts;
    return clock_gettime(CLOCK_MONOTONIC, &ts) == 0
               ? (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec
               : 0ULL;
#else
    return 0ULL;
#endif

}

static inline uint64_t flow_clock_monotonic_ns(void) {
#if !FLOW_IS_FREESTANDING && defined(CLOCK_MONOTONIC)
    struct timespec ts;
    return clock_gettime(CLOCK_MONOTONIC, &ts) == 0
               ? (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec
               : 0ULL;
#else
    return flow_hardware_cycles();
#endif

}

uint64_t flow_hardware_timer_frequency_hz(void);

static inline uint64_t flow_prng_next(uint64_t *state) {
    uint64_t x = (state && *state) ? *state : FLOW_GOLDEN_RATIO_64;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    if (state) *state = x;
    return x;
}

/* ============================================================================
 * 2. Single Error Model: 10-Bit Hardware Sieve & NaN-Boxed Boundary Tag
 * ============================================================================ */

typedef enum {
    FLOW_ASM_SIEVE_INT_NO_OVERFLOW     = FLOW_SSOT_SIEVE_INT_NO_OVERFLOW,
    FLOW_ASM_SIEVE_INT_NO_CARRY        = FLOW_SSOT_SIEVE_INT_NO_CARRY,
    FLOW_ASM_SIEVE_DOMAIN_VALID        = FLOW_SSOT_SIEVE_DOMAIN_VALID,
    FLOW_ASM_SIEVE_INT_NO_DIVZERO      = FLOW_SSOT_SIEVE_INT_NO_DIVZERO,
    FLOW_ASM_SIEVE_Q_NO_SATURATION     = FLOW_SSOT_SIEVE_Q_NO_SATURATION,
    FLOW_ASM_SIEVE_MEM_MASKED_INBOUNDS = FLOW_SSOT_SIEVE_MEM_MASKED_INBOUNDS,
    FLOW_ASM_SIEVE_MEM_ALIGNED         = FLOW_SSOT_SIEVE_MEM_ALIGNED,
    FLOW_ASM_SIEVE_QPOLY_INTERIOR      = FLOW_SSOT_SIEVE_QPOLY_INTERIOR,
    FLOW_ASM_SIEVE_JET2_TAYLOR_0       = FLOW_SSOT_SIEVE_JET2_TAYLOR_0,
    FLOW_ASM_SIEVE_JET2_TAYLOR_1       = FLOW_SSOT_SIEVE_JET2_TAYLOR_1,
    FLOW_ASM_SIEVE_ALL_SOUND           = FLOW_SSOT_SIEVE_ALL_SOUND
} FlowAsmSieveAxis;

#define FLOW_BOUNDARY_TAG_MAGIC       FLOW_SSOT_BOUNDARY_TAG_MAGIC
#define FLOW_HOTT_TAG_MAGIC           FLOW_SSOT_BOUNDARY_TAG_MAGIC
#define FLOW_BOUNDARY_TAG_PREFIX_MASK FLOW_SSOT_BOUNDARY_TAG_PREFIX_MASK
#define FLOW_BOUNDARY_PAYLOAD_MASK    FLOW_SSOT_BOUNDARY_PAYLOAD_MASK
#define FLOW_HOTT_BOUNDARY_OVERFLOW   FLOW_SSOT_HOTT_BOUNDARY_OVERFLOW

static inline bool flow_sieve_is_all_sound(uint64_t sieve) {
    return (sieve & FLOW_ASM_SIEVE_ALL_SOUND) == FLOW_ASM_SIEVE_ALL_SOUND;
}

typedef enum {
    FLOW_BOUNDARY_OK = 0,
    FLOW_BOUNDARY_PRECOND = 1,
    FLOW_BOUNDARY_POSTCOND = 2,
    FLOW_BOUNDARY_THM_IMPLIES = 3,
    FLOW_BOUNDARY_DOMAIN_SINGULARITY = 4,
    FLOW_BOUNDARY_HIT_COLLAPSE = 5,
    FLOW_BOUNDARY_EXPLICIT_NONE = 6
} FlowBoundaryViolationKind;

typedef struct {
    uint64_t value;
    uint64_t boundary_tag;
} FlowResultU64;

static inline uint64_t flow_hott_make_boundary_tag(uint16_t fn_id, uint8_t guard_idx,
                                                   uint8_t violation_kind, uint8_t cubical_axis,
                                                   uint16_t user_code) {
    uint64_t k4 = (uint64_t)((violation_kind == 0U ? 1U : violation_kind) & 0x0FU);
    return FLOW_BOUNDARY_TAG_MAGIC | (((uint64_t)(fn_id & 0x0FFFU)) << 32U) |
           (((uint64_t)guard_idx) << 24U) | (k4 << 20U) |
           (((uint64_t)(cubical_axis & 0x3FU)) << 12U) | ((uint64_t)(user_code & 0x0FFFU));
}

static inline bool flow_hott_is_boundary_tag(uint64_t raw_bits) {
    return (raw_bits & FLOW_BOUNDARY_TAG_PREFIX_MASK) == FLOW_BOUNDARY_TAG_MAGIC;
}

static inline uint16_t flow_hott_boundary_tag_code(uint64_t bits) {
    return flow_hott_is_boundary_tag(bits) ? (uint16_t)(bits & 0x0FFFULL) : 0U;
}

static inline FlowResultU64 flow_result_u64_interior(uint64_t val) {
    FlowResultU64 r = {val, 0ULL};
    return r;
}

static inline FlowResultU64 flow_result_u64_boundary(uint16_t fn_id, uint8_t guard_idx,
                                                     uint8_t violation_kind, uint8_t cubical_axis,
                                                     uint16_t user_code) {
    FlowResultU64 r = {0ULL, flow_hott_make_boundary_tag(fn_id, guard_idx, violation_kind,
                                                         cubical_axis, user_code)};
    return r;
}

static inline bool flow_result_u64_is_boundary(FlowResultU64 r) {
    return r.boundary_tag != 0ULL;
}

/* ============================================================================
 * 3. FlowI128 / FlowU128 Unified 6 Core Primitives (Zero Upper-Layer Forking)
 * ============================================================================ */

#if FLOW_HAS_INT128 && !defined(FLOW_FORCE_32BIT_FALLBACK) && !defined(FLOW_FORCE_C17_FALLBACK)
typedef __int128_t  FlowI128;
typedef __uint128_t FlowU128;

static inline FlowI128 flow_i128_from_i64(int64_t x) { return (FlowI128)x; }
static inline FlowU128 flow_u128_from_u64(uint64_t x) { return (FlowU128)x; }
static inline FlowI128 flow_i128_make(int64_t hi, uint64_t lo) {
    return (((FlowI128)hi) << 64U) | (FlowI128)lo;
}
static inline FlowU128 flow_u128_make(uint64_t hi, uint64_t lo) {
    return (((FlowU128)hi) << 64U) | (FlowU128)lo;
}
static inline int64_t  flow_i128_hi(FlowI128 a) { return (int64_t)(a >> 64U); }
static inline uint64_t flow_i128_lo(FlowI128 a) { return (uint64_t)a; }
static inline uint64_t flow_u128_hi(FlowU128 a) { return (uint64_t)(a >> 64U); }
static inline uint64_t flow_u128_lo(FlowU128 a) { return (uint64_t)a; }

static inline FlowI128 flow_i128_add(FlowI128 a, FlowI128 b) { return a + b; }
static inline FlowI128 flow_i128_sub(FlowI128 a, FlowI128 b) { return a - b; }
static inline FlowI128 flow_i128_neg(FlowI128 a) { return -a; }
static inline FlowU128 flow_u128_add(FlowU128 a, FlowU128 b) { return a + b; }
static inline FlowU128 flow_u128_sub(FlowU128 a, FlowU128 b) { return a - b; }
static inline FlowU128 flow_u128_neg(FlowU128 a) { return 0 - a; }
static inline FlowU128 flow_u128_from_i128(FlowI128 a) { return (FlowU128)a; }
static inline FlowI128 flow_i128_from_u128(FlowU128 a) { return (FlowI128)a; }
static inline FlowI128 flow_i128_mul(FlowI128 a, FlowI128 b) { return a * b; }
static inline FlowI128 flow_i128_mul_i64(int64_t a, int64_t b) { return (FlowI128)a * (FlowI128)b; }
static inline FlowU128 flow_u128_mul_u64(uint64_t a, uint64_t b) { return (FlowU128)a * (FlowU128)b; }
static inline FlowI128 flow_i128_ashr(FlowI128 a, uint32_t s) { return a >> s; }
static inline FlowI128 flow_i128_shl(FlowI128 a, uint32_t s)  { return a << s; }
static inline FlowU128 flow_u128_shr(FlowU128 a, uint32_t s)  { return a >> s; }
static inline FlowU128 flow_u128_shl(FlowU128 a, uint32_t s)  { return a << s; }
static inline FlowU128 flow_u128_div(FlowU128 num, FlowU128 den) {
    if (den == 0) return 0;
    if (num < den) return 0;
    uint32_t clz_n = (uint32_t)((num >> 64U) ? flow_clz64_nonzero((uint64_t)(num >> 64U)) : (64U + flow_clz64_nonzero((uint64_t)num)));
    uint32_t clz_d = (uint32_t)((den >> 64U) ? flow_clz64_nonzero((uint64_t)(den >> 64U)) : (64U + flow_clz64_nonzero((uint64_t)den)));
    uint32_t shift = clz_d - clz_n;
    FlowU128 d = den << shift;
    FlowU128 rem = num;
    FlowU128 quot = 0;
    for (uint32_t i = 0U; i <= shift; ++i) {
        quot <<= 1U;
        if (rem >= d) {
            rem -= d;
            quot |= 1U;
        }
        d >>= 1U;
    }
    return quot;
}

static inline FlowI128 flow_i128_div(FlowI128 num, FlowI128 den) {
    if (den == 0) return 0;
    bool neg = (num < 0) ^ (den < 0);
    FlowU128 unum = (num < 0) ? (FlowU128)(-num) : (FlowU128)num;
    FlowU128 uden = (den < 0) ? (FlowU128)(-den) : (FlowU128)den;
    FlowU128 uq = flow_u128_div(unum, uden);
    return neg ? -(FlowI128)uq : (FlowI128)uq;
}
static inline int flow_i128_cmp(FlowI128 a, FlowI128 b) { return (a < b) ? -1 : ((a > b) ? 1 : 0); }
static inline int flow_u128_cmp(FlowU128 a, FlowU128 b) { return (a < b) ? -1 : ((a > b) ? 1 : 0); }
static inline bool flow_u128_add_checked(FlowU128 a, FlowU128 b, FlowU128 *out) {
    return __builtin_add_overflow(a, b, out);
}
static inline bool flow_u128_mul_u64_checked(FlowU128 a, uint64_t b, FlowU128 *out) {
    return __builtin_mul_overflow(a, (FlowU128)b, out);
}
static inline int64_t flow_i128_clamp_i64(FlowI128 a) {
    if (a > (FlowI128)INT64_MAX) return INT64_MAX;
    if (a < (FlowI128)INT64_MIN) return INT64_MIN;
    return (int64_t)a;
}
static inline uint64_t flow_u64_div_portable(uint64_t num, uint64_t den) {
    return (den == 0ULL) ? 0ULL : (num / den);
}
#else
typedef struct {
    int64_t hi;
    uint64_t lo;
} FlowI128;

typedef struct {
    uint64_t hi;
    uint64_t lo;
} FlowU128;

static inline FlowI128 flow_i128_make(int64_t hi, uint64_t lo) { FlowI128 r = {hi, lo}; return r; }
static inline FlowU128 flow_u128_make(uint64_t hi, uint64_t lo) { FlowU128 r = {hi, lo}; return r; }
static inline FlowI128 flow_i128_from_i64(int64_t x) { return flow_i128_make((x < 0) ? -1LL : 0LL, (uint64_t)x); }
static inline FlowU128 flow_u128_from_u64(uint64_t x) { return flow_u128_make(0ULL, x); }
static inline int64_t  flow_i128_hi(FlowI128 a) { return a.hi; }
static inline uint64_t flow_i128_lo(FlowI128 a) { return a.lo; }
static inline uint64_t flow_u128_hi(FlowU128 a) { return a.hi; }
static inline uint64_t flow_u128_lo(FlowU128 a) { return a.lo; }

static inline void flow_mul_u64_to_u128(uint64_t a, uint64_t b, uint64_t *out_hi, uint64_t *out_lo) {
    uint64_t a0 = a & 0xFFFFFFFFULL, a1 = a >> 32U;
    uint64_t b0 = b & 0xFFFFFFFFULL, b1 = b >> 32U;
    uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    uint64_t mid = (p00 >> 32U) + (p01 & 0xFFFFFFFFULL) + (p10 & 0xFFFFFFFFULL);
    *out_lo = (p00 & 0xFFFFFFFFULL) | (mid << 32U);
    *out_hi = p11 + (p01 >> 32U) + (p10 >> 32U) + (mid >> 32U);
}

static inline FlowU128 flow_u128_mul_u64(uint64_t a, uint64_t b) {
    FlowU128 r = {0ULL, 0ULL};
    flow_mul_u64_to_u128(a, b, &r.hi, &r.lo);
    return r;
}

static inline FlowI128 flow_i128_mul_i64(int64_t a, int64_t b) {
    uint64_t uhi = 0ULL, ulo = 0ULL;
    flow_mul_u64_to_u128((uint64_t)a, (uint64_t)b, &uhi, &ulo);
    if (a < 0) uhi -= (uint64_t)b;
    if (b < 0) uhi -= (uint64_t)a;
    return flow_i128_make((int64_t)uhi, ulo);
}

static inline FlowI128 flow_i128_add(FlowI128 a, FlowI128 b) {
    uint64_t lo = a.lo + b.lo;
    uint64_t carry = (lo < a.lo) ? 1ULL : 0ULL;
    return flow_i128_make((int64_t)((uint64_t)a.hi + (uint64_t)b.hi + carry), lo);
}

static inline FlowU128 flow_u128_add(FlowU128 a, FlowU128 b) {
    uint64_t lo = a.lo + b.lo;
    uint64_t carry = (lo < a.lo) ? 1ULL : 0ULL;
    return flow_u128_make(a.hi + b.hi + carry, lo);
}

static inline FlowU128 flow_u128_neg(FlowU128 a) {
    uint64_t nlo = 0ULL - a.lo;
    uint64_t borrow = (a.lo != 0ULL) ? 1ULL : 0ULL;
    return flow_u128_make(0ULL - a.hi - borrow, nlo);
}

static inline FlowU128 flow_u128_sub(FlowU128 a, FlowU128 b) {
    FlowU128 nb = flow_u128_neg(b);
    return flow_u128_add(a, nb);
}

static inline FlowU128 flow_u128_from_i128(FlowI128 a) {
    return flow_u128_make((uint64_t)a.hi, a.lo);
}

static inline FlowI128 flow_i128_from_u128(FlowU128 a) {
    return flow_i128_make((int64_t)a.hi, a.lo);
}

static inline FlowI128 flow_i128_neg(FlowI128 a) {
    FlowU128 n = flow_u128_neg(flow_u128_make((uint64_t)a.hi, a.lo));
    return flow_i128_make((int64_t)n.hi, n.lo);
}

static inline bool flow_u128_add_checked(FlowU128 a, FlowU128 b, FlowU128 *out) {
    uint64_t lo = a.lo + b.lo;
    uint64_t carry = (lo < a.lo) ? 1ULL : 0ULL;
    uint64_t hi1 = a.hi + b.hi;
    bool ovf1 = (hi1 < a.hi);
    uint64_t hi2 = hi1 + carry;
    bool ovf2 = (hi2 < hi1);
    out->hi = hi2;
    out->lo = lo;
    return ovf1 || ovf2;
}

static inline bool flow_u128_mul_u64_checked(FlowU128 a, uint64_t b, FlowU128 *out) {
    uint64_t lo_hi = 0ULL, lo_lo = 0ULL, hi_hi = 0ULL, hi_lo = 0ULL;
    flow_mul_u64_to_u128(a.lo, b, &lo_hi, &lo_lo);
    flow_mul_u64_to_u128(a.hi, b, &hi_hi, &hi_lo);
    uint64_t mid = lo_hi + hi_lo;
    bool ovf = (hi_hi != 0ULL) || (mid < lo_hi);
    out->hi = mid;
    out->lo = lo_lo;
    return ovf;
}

static inline FlowI128 flow_i128_sub(FlowI128 a, FlowI128 b) {
    FlowU128 nb = flow_u128_neg(flow_u128_make((uint64_t)b.hi, b.lo));
    return flow_i128_add(a, flow_i128_make((int64_t)nb.hi, nb.lo));
}

static inline FlowI128 flow_i128_mul(FlowI128 a, FlowI128 b) {
    uint64_t p_hi = 0ULL, p_lo = 0ULL;
    flow_mul_u64_to_u128(a.lo, b.lo, &p_hi, &p_lo);
    uint64_t hi = p_hi + ((uint64_t)a.hi * b.lo) + (a.lo * (uint64_t)b.hi);
    return flow_i128_make((int64_t)hi, p_lo);
}

static inline FlowI128 flow_i128_ashr(FlowI128 a, uint32_t s) {
    if (s == 0U) return a;
    if (s < 64U) {
        uint64_t lo = (a.lo >> s) | ((uint64_t)a.hi << (64U - s));
        int64_t hi = (a.hi < 0) ? (int64_t)(~((~(uint64_t)a.hi) >> s)) : (int64_t)((uint64_t)a.hi >> s);
        return flow_i128_make(hi, lo);
    }
    uint32_t s2 = s - 64U;
    int64_t sign_fill = (a.hi < 0) ? -1LL : 0LL;
    uint64_t lo = (a.hi < 0) ? (~((~(uint64_t)a.hi) >> s2)) : ((uint64_t)a.hi >> s2);
    return flow_i128_make(sign_fill, lo);
}

static inline FlowI128 flow_i128_shl(FlowI128 a, uint32_t s) {
    if (s == 0U) return a;
    if (s < 64U) {
        return flow_i128_make((int64_t)(((uint64_t)a.hi << s) | (a.lo >> (64U - s))), a.lo << s);
    }
    return flow_i128_make((int64_t)(a.lo << (s - 64U)), 0ULL);
}

static inline FlowU128 flow_u128_shr(FlowU128 a, uint32_t s) {
    if (s == 0U) return a;
    if (s < 64U) {
        return flow_u128_make(a.hi >> s, (a.lo >> s) | (a.hi << (64U - s)));
    }
    return flow_u128_make(0ULL, a.hi >> (s - 64U));
}

static inline FlowU128 flow_u128_shl(FlowU128 a, uint32_t s) {
    if (s == 0U) return a;
    if (s < 64U) {
        return flow_u128_make((a.hi << s) | (a.lo >> (64U - s)), a.lo << s);
    }
    return flow_u128_make(a.lo << (s - 64U), 0ULL);
}

static inline int flow_u128_cmp(FlowU128 a, FlowU128 b) {
    if (a.hi != b.hi) return (a.hi < b.hi) ? -1 : 1;
    if (a.lo != b.lo) return (a.lo < b.lo) ? -1 : 1;
    return 0;
}

static inline int flow_i128_cmp(FlowI128 a, FlowI128 b) {
    if (a.hi != b.hi) return (a.hi < b.hi) ? -1 : 1;
    if (a.lo != b.lo) return (a.lo < b.lo) ? -1 : 1;
    return 0;
}

static inline FlowU128 flow_u128_div(FlowU128 num, FlowU128 den) {
    if (den.hi == 0ULL && den.lo == 0ULL) return flow_u128_make(0ULL, 0ULL);
    if (flow_u128_cmp(num, den) < 0) return flow_u128_make(0ULL, 0ULL);
    uint32_t clz_n = (num.hi != 0ULL) ? flow_clz64_nonzero(num.hi) : (64U + flow_clz64_nonzero(num.lo));
    uint32_t clz_d = (den.hi != 0ULL) ? flow_clz64_nonzero(den.hi) : (64U + flow_clz64_nonzero(den.lo));
    uint32_t shift = clz_d - clz_n;
    FlowU128 d = flow_u128_shl(den, shift);
    FlowU128 rem = num;
    FlowU128 quot = {0ULL, 0ULL};
    for (uint32_t i = 0U; i <= shift; ++i) {
        quot = flow_u128_shl(quot, 1U);
        if (flow_u128_cmp(rem, d) >= 0) {
            uint64_t borrow = (rem.lo < d.lo) ? 1ULL : 0ULL;
            rem.lo -= d.lo;
            rem.hi -= (d.hi + borrow);
            quot.lo |= 1ULL;
        }
        d = flow_u128_shr(d, 1U);
    }
    return quot;
}

static inline FlowI128 flow_i128_div(FlowI128 num, FlowI128 den) {
    if (den.hi == 0 && den.lo == 0ULL) return flow_i128_make(0, 0ULL);
    bool neg = (num.hi < 0) ^ (den.hi < 0);
    FlowU128 unum = (num.hi < 0) ? flow_u128_neg(flow_u128_make((uint64_t)num.hi, num.lo))
                                 : flow_u128_make((uint64_t)num.hi, num.lo);
    FlowU128 uden = (den.hi < 0) ? flow_u128_neg(flow_u128_make((uint64_t)den.hi, den.lo))
                                 : flow_u128_make((uint64_t)den.hi, den.lo);
    FlowU128 uq = flow_u128_div(unum, uden);
    if (neg) {
        FlowU128 nq = flow_u128_neg(uq);
        return flow_i128_make((int64_t)nq.hi, nq.lo);
    }
    return flow_i128_make((int64_t)uq.hi, uq.lo);
}

static inline int64_t flow_i128_clamp_i64(FlowI128 a) {
    int64_t res = (int64_t)a.lo;
    int64_t sign_ext = (res < 0) ? -1LL : 0LL;
    if (a.hi != sign_ext) return (a.hi < 0) ? INT64_MIN : INT64_MAX;
    return res;
}

static inline uint64_t flow_u64_div_portable(uint64_t num, uint64_t den) {
    if (den == 0ULL) return 0ULL;
    return flow_u128_div(flow_u128_make(0ULL, num), flow_u128_make(0ULL, den)).lo;
}
#endif

static inline uint64_t flow_asm_mul_u64_hi(uint64_t a, uint64_t b) {
    return flow_u128_hi(flow_u128_mul_u64(a, b));
}

static inline uint64_t flow_asm_mul_u64_lo(uint64_t a, uint64_t b) {
    return flow_u128_lo(flow_u128_mul_u64(a, b));
}


/* ============================================================================
 * 4. Total Signed Q-Arithmetic with Hardware NZCV.V Capture
 * ============================================================================ */

static inline FlowResultU64 flow_asm_qadd_i64(int64_t a, int64_t b, uint64_t *inout_sieve) {
    int64_t res = 0;
    uint64_t ovf = 0;
#if !defined(FLOW_FORCE_C17_FALLBACK) && (defined(__aarch64__) || defined(__arm64__))
    __asm__("adds %[res], %[a], %[b]\n\t"
            "cset %[ovf], vs"
            : [res] "=&r"(res), [ovf] "=r"(ovf)
            : [a] "r"(a), [b] "r"(b) : "cc");
#elif !defined(FLOW_FORCE_C17_FALLBACK) && (defined(__x86_64__) || defined(_M_X64))
    res = a; uint8_t ovf8 = 0;
    __asm__("addq %[b], %[res]\n\t"
            "seto %[ovf8]"
            : [res] "+r"(res), [ovf8] "=qm"(ovf8)
            : [b] "r"(b) : "cc");
    ovf = (uint64_t)ovf8;
#else
    ovf = (uint64_t)__builtin_add_overflow(a, b, &res);
#endif

    if (__builtin_expect(ovf != 0ULL, 0)) {
        if (inout_sieve) *inout_sieve &= ~FLOW_ASM_SIEVE_INT_NO_OVERFLOW;
        return flow_result_u64_boundary(FLOW_HOTT_BOUNDARY_OVERFLOW, 0U,
                                        (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY, 0U, (uint16_t)ovf);
    }
    return flow_result_u64_interior((uint64_t)res);
}

static inline FlowResultU64 flow_asm_qsub_i64(int64_t a, int64_t b, uint64_t *inout_sieve) {
    int64_t res = 0;
    uint64_t ovf = 0;
#if !defined(FLOW_FORCE_C17_FALLBACK) && (defined(__aarch64__) || defined(__arm64__))
    __asm__("subs %[res], %[a], %[b]\n\t"
            "cset %[ovf], vs"
            : [res] "=&r"(res), [ovf] "=r"(ovf)
            : [a] "r"(a), [b] "r"(b) : "cc");
#elif !defined(FLOW_FORCE_C17_FALLBACK) && (defined(__x86_64__) || defined(_M_X64))
    res = a; uint8_t ovf8 = 0;
    __asm__("subq %[b], %[res]\n\t"
            "seto %[ovf8]"
            : [res] "+r"(res), [ovf8] "=qm"(ovf8)
            : [b] "r"(b) : "cc");
    ovf = (uint64_t)ovf8;
#else
    ovf = (uint64_t)__builtin_sub_overflow(a, b, &res);
#endif

    if (__builtin_expect(ovf != 0ULL, 0)) {
        if (inout_sieve) *inout_sieve &= ~FLOW_ASM_SIEVE_INT_NO_OVERFLOW;
        return flow_result_u64_boundary(FLOW_HOTT_BOUNDARY_OVERFLOW, 1U,
                                        (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY, 0U, (uint16_t)ovf);
    }
    return flow_result_u64_interior((uint64_t)res);
}

static inline FlowResultU64 flow_asm_qmul_shift16_i64(int64_t a_q16, int64_t b_q16, uint64_t *inout_sieve) {
    FlowI128 prod = flow_i128_ashr(flow_i128_mul_i64(a_q16, b_q16), FLOW_Q16_SHIFT);
    int64_t res = (int64_t)flow_i128_lo(prod);
    int64_t sign_ext = (res < 0) ? -1LL : 0LL;
    if (__builtin_expect(flow_i128_hi(prod) != sign_ext, 0)) {
        if (inout_sieve) *inout_sieve &= ~FLOW_ASM_SIEVE_INT_NO_OVERFLOW;
        return flow_result_u64_boundary(FLOW_HOTT_BOUNDARY_OVERFLOW, 2U,
                                        (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY, 0U, 1U);
    }
    return flow_result_u64_interior((uint64_t)res);
}

static inline FlowResultU64 flow_asm_qdiv_shift16_i64(int64_t num_q16, int64_t den_q16, uint64_t *inout_sieve) {
    if (__builtin_expect(den_q16 == 0, 0)) {
        if (inout_sieve) *inout_sieve &= ~(FLOW_ASM_SIEVE_INT_NO_DIVZERO | FLOW_ASM_SIEVE_DOMAIN_VALID);
        return flow_result_u64_boundary(FLOW_HOTT_BOUNDARY_OVERFLOW, 4U,
                                        (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY, 0U, 1U);
    }
    FlowI128 num_scaled = flow_i128_shl(flow_i128_from_i64(num_q16), FLOW_Q16_SHIFT);
    FlowI128 quot = flow_i128_div(num_scaled, flow_i128_from_i64(den_q16));
    int64_t res = (int64_t)flow_i128_lo(quot);
    int64_t sign_ext = (res < 0) ? -1LL : 0LL;
    if (__builtin_expect(flow_i128_hi(quot) != sign_ext, 0)) {
        if (inout_sieve) *inout_sieve &= ~(FLOW_ASM_SIEVE_INT_NO_OVERFLOW | FLOW_ASM_SIEVE_Q_NO_SATURATION);
        return flow_result_u64_boundary(FLOW_HOTT_BOUNDARY_OVERFLOW, 4U,
                                        (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY, 0U, 2U);
    }
    return flow_result_u64_interior((uint64_t)res);
}

static inline FlowResultU64 flow_asm_qdot_i32(const int32_t *a, const int32_t *x, size_t dim,
                                              uint64_t *inout_sieve) {
    FlowI128 acc = flow_i128_from_i64(0);
    for (size_t j = 0; j < dim; ++j) {
        acc = flow_i128_add(acc, flow_i128_mul_i64((int64_t)a[j], (int64_t)x[j]));
    }
    int64_t dot = (int64_t)flow_i128_lo(acc);
    int64_t sign_ext = (dot < 0) ? -1LL : 0LL;
    if (__builtin_expect(flow_i128_hi(acc) != sign_ext, 0)) {
        if (inout_sieve) *inout_sieve &= ~FLOW_ASM_SIEVE_INT_NO_OVERFLOW;
        dot = (flow_i128_hi(acc) < 0) ? INT64_MIN : INT64_MAX;
        FlowResultU64 out = {(uint64_t)dot,
                             flow_hott_make_boundary_tag(FLOW_HOTT_BOUNDARY_OVERFLOW, 3U,
                                                         (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY,
                                                         0U, 1U)};
        return out;
    }
    return flow_result_u64_interior((uint64_t)dot);
}

static inline int64_t flow_asm_qpoly4d_eval_plane(const int64_t a_q16[4], int64_t b_q16,
                                                  const int64_t x_q16[4], uint64_t *inout_sieve) {
    FlowI128 acc = flow_i128_from_i64(0);
    for (int k = 0; k < 4; ++k) {
        acc = flow_i128_add(acc, flow_i128_mul_i64(a_q16[k], x_q16[k]));
    }
    FlowI128 shifted = flow_i128_ashr(acc, FLOW_Q16_SHIFT);
    int64_t dot = (int64_t)flow_i128_lo(shifted);
    int64_t sign_ext = (dot < 0) ? -1LL : 0LL;
    bool ovf = (flow_i128_hi(shifted) != sign_ext);
    if (__builtin_expect(ovf, 0)) {
        dot = (flow_i128_hi(shifted) < 0) ? INT64_MIN : INT64_MAX;
    }
    FlowResultU64 slack_res = flow_asm_qsub_i64(b_q16, dot, inout_sieve);
    int64_t slack = (int64_t)slack_res.value;
    if (flow_result_u64_is_boundary(slack_res)) {
        slack = (b_q16 >= 0) ? INT64_MAX : INT64_MIN;
    }
    if (inout_sieve) {
        if (ovf) *inout_sieve &= ~FLOW_ASM_SIEVE_INT_NO_OVERFLOW;
        if (slack < 0 || ovf || flow_result_u64_is_boundary(slack_res)) {
            *inout_sieve &= ~FLOW_ASM_SIEVE_QPOLY_INTERIOR;
        }
    }
    return slack;
}

/* ============================================================================
 * 5. 192-Entry Chebyshev Single-`udiv` Universal Square Root
 * ============================================================================ */

static inline uint64_t flow_isqrt_u64(uint64_t x) {
    if (x < 2ULL) {
        return x;
    }
    static const uint32_t isqrt_lin_table[FLOW_SSOT_CHEB_LUT_SIZE] = FLOW_SSOT_ISQRT_LIN_TABLE_INIT;

    uint32_t even_lz = flow_clz64_nonzero(x) & ~1U;
    uint64_t m = x << even_lz;
    uint32_t entry = isqrt_lin_table[(uint32_t)(m >> 56U) - 64U];
    uint32_t frac16 = (uint32_t)(m >> 40U) & 0xFFFFU;
    uint64_t seed32 = (uint64_t)(((entry & 0xFFFFU) << 4U) + (((entry >> 16U) * frac16) >> 16U))
                      << 12U;
    uint64_t r = (seed32 >> (even_lz >> 1U)) | 1ULL;
    r = (r + flow_u64_div_portable(x, r)) >> 1U;
    if (__builtin_expect(r > UINT32_MAX, 0)) {
        r = UINT32_MAX;
    }
    return r - (uint64_t)((r * r) > x);
}

#define flow_asm_isqrt_u64_fast flow_isqrt_u64
#define flow_asm_isqrt_u64      flow_isqrt_u64

static inline FlowResultU64 flow_asm_qsqrt_shift16_i64(int64_t x_q16, uint64_t *inout_sieve) {
    if (__builtin_expect(x_q16 < 0, 0)) {
        if (inout_sieve) *inout_sieve &= ~FLOW_ASM_SIEVE_DOMAIN_VALID;
        return flow_result_u64_boundary(FLOW_HOTT_BOUNDARY_OVERFLOW, 5U,
                                        (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY, 0U, 1U);
    }
    uint64_t root_q16 = flow_isqrt_u64(((uint64_t)x_q16) << FLOW_Q16_SHIFT);
    return flow_result_u64_interior(root_q16);
}

/* ============================================================================
 * 6. Wave64 Ballot & 1.58-Bit Ternary Dot Product
 * ============================================================================ */

typedef struct {
    uint64_t nonzero_mask;
    uint64_t sign_mask;
} FlowBmfTernary64;

static inline FlowBmfTernary64 flow_bmf_ternary64_zero(void) {
    FlowBmfTernary64 t = {0ULL, 0ULL};
    return t;
}

static inline bool flow_bmf_ternary64_is_canonical(FlowBmfTernary64 t) {
    return (t.sign_mask & ~t.nonzero_mask) == 0ULL;
}

static inline FlowBmfTernary64 flow_bmf_ternary64_canonicalize(FlowBmfTernary64 t) {
    t.sign_mask &= t.nonzero_mask;
    return t;
}

static inline int8_t flow_bmf_ternary64_get(FlowBmfTernary64 t, uint32_t lane) {
    if (lane >= 64U) return 0;
    uint64_t nz = (t.nonzero_mask >> lane) & 1ULL;
    uint64_t sg = ((t.sign_mask & t.nonzero_mask) >> lane) & 1ULL;
    return (int8_t)((int32_t)nz - (int32_t)(sg << 1U));
}

static inline int32_t flow_bmf_ternary64_dot(FlowBmfTernary64 a, FlowBmfTernary64 b) {
    uint64_t active = a.nonzero_mask & b.nonzero_mask;
    uint64_t xor_sign = (a.sign_mask & a.nonzero_mask) ^ (b.sign_mask & b.nonzero_mask);
    uint64_t same_sign = active & ~xor_sign;
    uint64_t opp_sign = active & xor_sign;
    return (int32_t)flow_popcnt64(same_sign) - (int32_t)flow_popcnt64(opp_sign);
}

static inline FlowBmfTernary64 flow_bmf_ternary64_encode(const int8_t values[64]) {
    FlowBmfTernary64 out = {0ULL, 0ULL};
    if (!values) return out;
    for (uint32_t i = 0; i < 64U; ++i) {
        if (values[i] > 0) {
            out.nonzero_mask |= (1ULL << i);
        } else if (values[i] < 0) {
            out.nonzero_mask |= (1ULL << i);
            out.sign_mask |= (1ULL << i);
        }
    }
    return out;
}

static inline void flow_bmf_ternary64_decode_all(FlowBmfTernary64 t, int8_t out_values[64]) {
    if (!out_values) return;
    FlowBmfTernary64 canon = flow_bmf_ternary64_canonicalize(t);
    for (uint32_t i = 0; i < 64U; ++i) {
        out_values[i] = flow_bmf_ternary64_get(canon, i);
    }
}

static inline uint32_t flow_ballot_lane_offset(uint64_t ballot_mask, uint32_t lane_id) {
    if (lane_id == 0U) return 0U;
    uint64_t lower_mask = (lane_id >= 64U) ? UINT64_MAX : ((1ULL << lane_id) - 1ULL);
    return flow_popcnt64(ballot_mask & lower_mask);
}

static inline uint32_t flow_ballot_visible_count(uint64_t ballot_mask) {
    return flow_popcnt64(ballot_mask);
}

static inline uint32_t flow_ballot_compact_wave64(uint64_t ballot_mask, uint32_t base_handle,
                                                  uint32_t out_handles[FLOW_WAVE64_SIZE]) {
    uint32_t write_idx = 0U;
    uint64_t remaining = ballot_mask;
    while (remaining != 0ULL) {
        uint32_t lane = flow_ctz64_nonzero(remaining);
        if (out_handles) {
            out_handles[write_idx] = base_handle + lane;
        }
        write_idx++;
        remaining &= (remaining - 1ULL);
    }
    return write_idx;
}

/* ============================================================================
 * 7. Masked Cyclic Memory Arena (Z / 2^k Z)
 * ============================================================================ */

static inline uint64_t flow_asm_arena_load_u64(const uint64_t *base, uint64_t raw_idx,
                                               uint64_t cap_mask, uint64_t *inout_sieve) {
    uint64_t val = 0ULL;
    uint64_t midx = 0ULL;
    uint64_t oob = 0ULL;
#if !defined(FLOW_FORCE_C17_FALLBACK) && (defined(__aarch64__) || defined(__arm64__))
    __asm__ __volatile__("and %[midx], %[idx], %[mask]\n\t"
                         "bic %[oob],  %[idx], %[mask]\n\t"
                         "ldr %[val],  [%[base], %[midx], lsl #3]"
                         : [val] "=r"(val), [midx] "=&r"(midx), [oob] "=&r"(oob)
                         : [base] "r"(base), [idx] "r"(raw_idx), [mask] "r"(cap_mask)
                         : "memory");
#else
    midx = raw_idx & cap_mask;
    oob = raw_idx & ~cap_mask;
    val = base[midx];
#endif

    if (inout_sieve) {
        uint64_t unaligned = (((uintptr_t)base & (FLOW_HW_CACHE_LINE_BYTES - 1U)) != 0U);
        if (oob != 0ULL) *inout_sieve &= ~FLOW_ASM_SIEVE_MEM_MASKED_INBOUNDS;
        if (unaligned)   *inout_sieve &= ~FLOW_ASM_SIEVE_MEM_ALIGNED;
    }
    return val;
}

static inline void flow_asm_arena_store_u64(uint64_t *base, uint64_t raw_idx, uint64_t cap_mask,
                                            uint64_t val, uint64_t *inout_sieve) {
    uint64_t midx = 0ULL;
    uint64_t oob = 0ULL;
#if !defined(FLOW_FORCE_C17_FALLBACK) && (defined(__aarch64__) || defined(__arm64__))
    __asm__ __volatile__("and %[midx], %[idx], %[mask]\n\t"
                         "bic %[oob],  %[idx], %[mask]\n\t"
                         "str %[val],  [%[base], %[midx], lsl #3]"
                         : [midx] "=&r"(midx), [oob] "=&r"(oob)
                         : [base] "r"(base), [idx] "r"(raw_idx), [mask] "r"(cap_mask), [val] "r"(val)
                         : "memory");
#else
    midx = raw_idx & cap_mask;
    oob = raw_idx & ~cap_mask;
    base[midx] = val;
#endif

    if (inout_sieve) {
        uint64_t unaligned = (((uintptr_t)base & (FLOW_HW_CACHE_LINE_BYTES - 1U)) != 0U);
        if (oob != 0ULL) *inout_sieve &= ~FLOW_ASM_SIEVE_MEM_MASKED_INBOUNDS;
        if (unaligned)   *inout_sieve &= ~FLOW_ASM_SIEVE_MEM_ALIGNED;
    }
}

/* ============================================================================
 * 8. Goldilocks Prime Field F_p & 64-Byte Capsule Sponge MAC
 * ============================================================================ */

#define FLOW_GOLDILOCKS_PRIME FLOW_SSOT_GOLDILOCKS_PRIME
#define FLOW_GOLDILOCKS_EPS   FLOW_SSOT_GOLDILOCKS_EPS
#define FLOW_FP64_SPONGE_ROUND_CONST FLOW_SSOT_FP64_SPONGE_ROUND_CONST

static inline uint64_t flow_asm_fp64_reduce(uint64_t x) {
    return (x >= FLOW_GOLDILOCKS_PRIME) ? (x - FLOW_GOLDILOCKS_PRIME) : x;
}

static inline uint64_t flow_asm_fp64_add(uint64_t a, uint64_t b) {
    uint64_t ar = flow_asm_fp64_reduce(a);
    uint64_t br = flow_asm_fp64_reduce(b);
    uint64_t diff = FLOW_GOLDILOCKS_PRIME - br;
    return (ar >= diff) ? (ar - diff) : (ar + br);
}

static inline uint64_t flow_asm_fp64_sub(uint64_t a, uint64_t b) {
    uint64_t ar = flow_asm_fp64_reduce(a);
    uint64_t br = flow_asm_fp64_reduce(b);
    return (ar >= br) ? (ar - br) : (FLOW_GOLDILOCKS_PRIME - (br - ar));
}

static inline uint64_t flow_asm_fp64_mul(uint64_t a, uint64_t b) {
    uint64_t ar = flow_asm_fp64_reduce(a);
    uint64_t br = flow_asm_fp64_reduce(b);
    FlowU128 prod = flow_u128_mul_u64(ar, br);
    uint64_t hi64 = flow_u128_hi(prod);
    uint64_t lo64 = flow_u128_lo(prod);
    uint64_t mid = hi64 & FLOW_GOLDILOCKS_EPS;
    uint64_t hi = hi64 >> 32U;
    uint64_t t0 = flow_asm_fp64_sub(flow_asm_fp64_reduce(lo64), hi);
    uint64_t t1 = mid * FLOW_GOLDILOCKS_EPS;
    return flow_asm_fp64_add(t0, t1);
}

static inline uint64_t flow_asm_fp64_pow7(uint64_t x) {
    uint64_t x2 = flow_asm_fp64_mul(x, x);
    uint64_t x4 = flow_asm_fp64_mul(x2, x2);
    uint64_t x6 = flow_asm_fp64_mul(x4, x2);
    return flow_asm_fp64_mul(x6, x);
}

static inline uint64_t flow_asm_fp64_sponge_step(uint64_t state, uint64_t word) {
    uint64_t mixed = flow_asm_fp64_add(state, flow_asm_fp64_add(word, FLOW_FP64_SPONGE_ROUND_CONST));
    return flow_asm_fp64_pow7(mixed);
}

static inline uint64_t flow_load_le64_bytes(const uint8_t *p) {
    return ((uint64_t)p[0]) | (((uint64_t)p[1]) << 8U) | (((uint64_t)p[2]) << 16U) |
           (((uint64_t)p[3]) << 24U) | (((uint64_t)p[4]) << 32U) | (((uint64_t)p[5]) << 40U) |
           (((uint64_t)p[6]) << 48U) | (((uint64_t)p[7]) << 56U);
}

static inline void flow_store_le64_bytes(uint8_t *p, uint64_t v) {
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8U) & 0xFFU);
    p[2] = (uint8_t)((v >> 16U) & 0xFFU);
    p[3] = (uint8_t)((v >> 24U) & 0xFFU);
    p[4] = (uint8_t)((v >> 32U) & 0xFFU);
    p[5] = (uint8_t)((v >> 40U) & 0xFFU);
    p[6] = (uint8_t)((v >> 48U) & 0xFFU);
    p[7] = (uint8_t)((v >> 56U) & 0xFFU);
}

static inline uint64_t flow_capsule64_calc_mac(const void *capsule64, uint64_t domain_seed) {
    if (!capsule64) return 0ULL;
    const uint8_t *bytes = (const uint8_t *)capsule64;
    uint64_t h = flow_asm_fp64_sponge_step(FLOW_GOLDEN_RATIO_64, domain_seed);
    for (size_t w = 0U; w < 7U; ++w) {
        h = flow_asm_fp64_sponge_step(h, flow_load_le64_bytes(&bytes[w * 8U]));
    }
    return (h == 0ULL) ? 1ULL : h;
}

static inline uint64_t flow_capsule64_seal(void *capsule64, uint64_t domain_seed) {
    if (!capsule64) return 0ULL;
    uint64_t mac = flow_capsule64_calc_mac(capsule64, domain_seed);
    flow_store_le64_bytes(((uint8_t *)capsule64) + 56U, mac);
    return mac;
}

static inline bool flow_capsule64_verify(const void *capsule64, uint64_t domain_seed) {
    if (!capsule64) return false;
    uint64_t stored = flow_load_le64_bytes(((const uint8_t *)capsule64) + 56U);
    return (stored != 0ULL) && (stored == flow_capsule64_calc_mac(capsule64, domain_seed));
}

/* ============================================================================
 * 9. FlowJet2 Fiber Algebra & Normalized Citardauq Root Solver
 * ============================================================================ */

typedef struct {
    int64_t v;
    int64_t d1;
    int64_t d2;
} FlowJet2;
typedef FlowJet2 FlowJetScalar2;
typedef FlowJet2 FlowQJet2;

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
    return flow_jet2_make(flow_i128_clamp_i64(flow_i128_add(flow_i128_from_i64(a.v), flow_i128_from_i64(b.v))),
                          flow_i128_clamp_i64(flow_i128_add(flow_i128_from_i64(a.d1), flow_i128_from_i64(b.d1))),
                          flow_i128_clamp_i64(flow_i128_add(flow_i128_from_i64(a.d2), flow_i128_from_i64(b.d2))));
}

static inline FlowJet2 flow_jet2_sub(FlowJet2 a, FlowJet2 b) {
    return flow_jet2_make(flow_i128_clamp_i64(flow_i128_sub(flow_i128_from_i64(a.v), flow_i128_from_i64(b.v))),
                          flow_i128_clamp_i64(flow_i128_sub(flow_i128_from_i64(a.d1), flow_i128_from_i64(b.d1))),
                          flow_i128_clamp_i64(flow_i128_sub(flow_i128_from_i64(a.d2), flow_i128_from_i64(b.d2))));
}

static inline FlowJet2 flow_jet2_mul(FlowJet2 a, FlowJet2 b) {
    FlowI128 v = flow_i128_ashr(flow_i128_mul_i64(a.v, b.v), FLOW_Q16_SHIFT);
    FlowI128 p_d1v = flow_i128_mul_i64(a.d1, b.v);
    FlowI128 p_vd1 = flow_i128_mul_i64(a.v, b.d1);
    uint64_t rem_d1 = (flow_i128_lo(p_d1v) & 0xFFFFULL) + (flow_i128_lo(p_vd1) & 0xFFFFULL);
    FlowI128 d1 = flow_i128_add(flow_i128_add(flow_i128_ashr(p_d1v, FLOW_Q16_SHIFT),
                                             flow_i128_ashr(p_vd1, FLOW_Q16_SHIFT)),
                                flow_i128_from_i64((int64_t)(rem_d1 >> 16U)));
    FlowI128 p_d2v = flow_i128_mul_i64(a.d2, b.v);
    FlowI128 p_mid = flow_i128_mul_i64(a.d1, b.d1);
    FlowI128 p_vd2 = flow_i128_mul_i64(a.v, b.d2);
    uint64_t rem_d2 = (flow_i128_lo(p_d2v) & 0xFFFFULL) + 2ULL * (flow_i128_lo(p_mid) & 0xFFFFULL) + (flow_i128_lo(p_vd2) & 0xFFFFULL);
    FlowI128 mid_shr = flow_i128_ashr(p_mid, FLOW_Q16_SHIFT);
    FlowI128 d2 = flow_i128_add(flow_i128_add(flow_i128_add(flow_i128_ashr(p_d2v, FLOW_Q16_SHIFT),
                                                            flow_i128_add(mid_shr, mid_shr)),
                                              flow_i128_ashr(p_vd2, FLOW_Q16_SHIFT)),
                                flow_i128_from_i64((int64_t)(rem_d2 >> 16U)));
    return flow_jet2_make(flow_i128_clamp_i64(v), flow_i128_clamp_i64(d1), flow_i128_clamp_i64(d2));
}

static inline int64_t flow_jet2_eval_taylor(FlowJet2 j, int64_t dt_q16) {
    FlowI128 lin = flow_i128_ashr(flow_i128_mul_i64(j.d1, dt_q16), FLOW_Q16_SHIFT);
    FlowI128 p1 = flow_i128_mul_i64(j.d2, dt_q16);
    FlowI128 p1_shr = flow_i128_ashr(p1, FLOW_Q16_SHIFT);
    FlowI128 quad = flow_i128_ashr(flow_i128_mul(p1_shr, flow_i128_from_i64(dt_q16)), FLOW_Q16_SHIFT + 1U);
    FlowI128 sum = flow_i128_add(flow_i128_add(flow_i128_from_i64(j.v), lin), quad);
    return flow_i128_clamp_i64(sum);
}

/*
 * Normalized 64-Bit Citardauq 2-Jet Root Solver (Sole SSOT Engine):
 * Solves `v + d1 * t + 0.5 * d2 * t^2 = 0` (for interior slack v > 0)
 * for the smallest boundary impact time `t_q16` in `(0, t_max_q16]`.
 */
static inline bool flow_jet2_solve_root(FlowJet2 j, int64_t t_max_q16, int64_t *out_t_q16) {
    int64_t v = j.v;
    int64_t d1 = j.d1;
    int64_t d2 = j.d2;
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
        if (r_i <= 0 && c_i <= 0) return false;

        uint64_t max_m = (mv > mr) ? mv : mr;
        if (mc > max_m) max_m = mc;
        uint32_t lz = flow_clz64_nonzero(max_m);
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
        if (v <= 0 || (r_i <= 0 && c_i <= 0)) return false;
        mr = (uint64_t)(r_i < 0 ? -r_i : r_i);
    } else {
        r_i = -d1;
        c_i = -d2;
        if (r_i <= 0 && c_i <= 0) return false;
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
        FlowI128 target = flow_i128_shl(flow_i128_from_i64(v), FLOW_Q16_SHIFT);
        if (c_i < 0) {
            if (r_i <= 0 || flow_i128_cmp(flow_i128_mul_i64(r_i, t_max_q16), target) < 0) {
                return false;
            }
        } else {
            int64_t t_ub = t_max_q16 + 1;
            int64_t t_ub_sq = (int64_t)((uint64_t)t_ub * (uint64_t)t_ub);
            FlowI128 f_ub = flow_i128_add(flow_i128_mul_i64(r_i, t_ub),
                                          flow_i128_ashr(flow_i128_mul_i64(c_i, t_ub_sq), 17U));
            if (flow_i128_cmp(f_ub, target) < 0) {
                return false;
            }
        }
    }

    int64_t disc = r_i * r_i + 2LL * c_i * v;
    if (disc < 0) return false;

    uint64_t sq = flow_isqrt_u64((uint64_t)disc);
    uint64_t q_sum = mr + sq;
    uint64_t num = (r_i >= 0) ? (((uint64_t)v) << (FLOW_Q16_SHIFT + 1U)) : (q_sum << FLOW_Q16_SHIFT);
    uint64_t den = (r_i >= 0) ? q_sum : (uint64_t)c_i;
    if (den == 0ULL) return false;

    uint64_t t_u = flow_u64_div_portable(num, den);
    if (t_u > 0ULL && t_u <= (uint64_t)t_max_q16) {
        *out_t_q16 = (int64_t)t_u;
        return true;
    }
    return false;
}

static inline uint64_t flow_asm_check_jet2(FlowJet2 j, uint64_t *inout_sieve) {
    uint64_t uv = (uint64_t)j.v, ud1 = (uint64_t)j.d1, ud2 = (uint64_t)j.d2;
    bool has_tag = flow_hott_is_boundary_tag(uv) || flow_hott_is_boundary_tag(ud1) || flow_hott_is_boundary_tag(ud2);
    bool has_min = (j.v == INT64_MIN) || (j.d1 == INT64_MIN) || (j.d2 == INT64_MIN);
    bool has_sat = (j.v == INT64_MAX) || (j.d1 == INT64_MAX) || (j.d2 == INT64_MAX);
    uint64_t clear_bits = ((has_tag || has_min) ? FLOW_ASM_SIEVE_DOMAIN_VALID : 0ULL) |
                          (has_sat ? FLOW_ASM_SIEVE_Q_NO_SATURATION : 0ULL);
    uint64_t s = inout_sieve ? *inout_sieve : FLOW_ASM_SIEVE_ALL_SOUND;
    s &= ~clear_bits;
    if (inout_sieve) *inout_sieve = s;
    return s;
}

/* ============================================================================
 * 10. Universal External-Constraint Q-Polyhedron (FlowQPoly)
 * ============================================================================ */

#define FLOW_QPOLY_PLANES FLOW_SSOT_QPOLY_PLANES

typedef struct {
    uint64_t violated_mask;
    uint32_t worst_plane;
    uint32_t tightest_plane;
    int64_t worst_violation;
    int64_t min_slack;
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

#if defined(__GNUC__) || defined(__clang__)
#define FLOW_MAYBE_UNUSED __attribute__((unused))
#else
#define FLOW_MAYBE_UNUSED
#endif


#define FLOW_QPOLY_DEFINE_EX(NAME, DIM, SHIFT, MAX_PLANES)                                      \
    enum { NAME##_DIM = (DIM), NAME##_SHIFT = (SHIFT), NAME##_MAX_PLANES = (MAX_PLANES) };      \
    typedef struct __attribute__((aligned(FLOW_HW_CACHE_LINE_BYTES))) {                         \
        int32_t A[MAX_PLANES][DIM];                                                             \
        int64_t b[MAX_PLANES];                                                                  \
        int64_t b_exit[MAX_PLANES];                                                             \
        uint64_t active_mask;                                                                   \
        uint64_t hysteresis_mask;                                                               \
        uint64_t generation;                                                                    \
    } NAME;                                                                                     \
    FLOW_MAYBE_UNUSED static inline uint64_t NAME##_valid_plane_mask(void) {                    \
        return ((uint32_t)(MAX_PLANES) >= 64U) ? ~0ULL : ((1ULL << (uint32_t)(MAX_PLANES)) - 1ULL); \
    }                                                                                           \
    FLOW_MAYBE_UNUSED static inline void NAME##_init(NAME *p) {                                 \
        if (!p) return;                                                                         \
        __builtin_memset(p, 0, sizeof(*p));                                                     \
        p->generation = 1ULL;                                                                   \
    }                                                                                           \
    FLOW_MAYBE_UNUSED static inline void NAME##_set_hysteresis(NAME *p, uint32_t plane, int64_t b_enter, \
                                             int64_t b_exit_val) {                             \
        if (!p || plane >= (uint32_t)(MAX_PLANES)) return;                                     \
        p->b[plane] = b_enter;                                                                  \
        p->b_exit[plane] = b_exit_val;                                                          \
        uint64_t bit = 1ULL << plane;                                                           \
        p->hysteresis_mask |= bit;                                                              \
        p->active_mask |= bit;                                                                  \
    }                                                                                           \
    FLOW_MAYBE_UNUSED static inline void NAME##_update_external_bounds(NAME *p, const int64_t *b_ext, \
                                                     uint64_t mask) {                           \
        if (!p || !b_ext) return;                                                               \
        uint64_t clean_mask = mask & NAME##_valid_plane_mask();                                 \
        uint64_t rem = clean_mask;                                                              \
        while (rem != 0ULL) {                                                                   \
            uint32_t i = flow_ctz64_nonzero(rem);                                               \
            rem &= (rem - 1ULL);                                                                \
            p->b[i] = b_ext[i];                                                                 \
        }                                                                                       \
        p->active_mask = clean_mask;                                                            \
        p->generation += 1ULL;                                                                  \
    }                                                                                           \
    FLOW_MAYBE_UNUSED static inline FlowQPolyEval NAME##_eval_hysteresis(const NAME *p, const int32_t x[DIM], \
                                                       uint64_t prev_violated_mask) {           \
        if (!p || !x) { FlowQPolyEval empty = {0ULL, 0U, 0U, 0, 0}; return empty; }            \
        uint64_t rem = p->active_mask & NAME##_valid_plane_mask();                              \
        uint64_t use_exit_mask = prev_violated_mask & p->hysteresis_mask;                       \
        uint64_t violated_mask = 0ULL;                                                          \
        uint32_t worst_plane = 0U, tightest_plane = 0U;                                         \
        int64_t worst_violation = 0, min_slack = INT64_MAX;                                     \
        bool any_violated = false, any_satisfied = false;                                       \
        while (rem != 0ULL) {                                                                   \
            uint32_t i = flow_ctz64_nonzero(rem);                                               \
            rem &= (rem - 1ULL);                                                                \
            FlowResultU64 dot_res = flow_asm_qdot_i32(p->A[i], x, (size_t)(DIM), NULL);         \
            int64_t dot = (int64_t)dot_res.value;                                               \
            int64_t bi = ((use_exit_mask >> i) & 1ULL) ? p->b_exit[i] : p->b[i];                \
            if (dot > bi) {                                                                     \
                int64_t viol = dot - bi;                                                        \
                violated_mask |= (1ULL << i);                                                   \
                if (!any_violated || viol > worst_violation) {                                  \
                    worst_violation = viol; worst_plane = i;                                    \
                }                                                                               \
                any_violated = true;                                                            \
            } else {                                                                            \
                int64_t slack = bi - dot;                                                       \
                if (!any_satisfied || slack < min_slack) {                                      \
                    min_slack = slack; tightest_plane = i;                                     \
                }                                                                               \
                any_satisfied = true;                                                           \
            }                                                                                   \
        }                                                                                       \
        if (!any_satisfied) min_slack = 0;                                                      \
        FlowQPolyEval res = {violated_mask, worst_plane, tightest_plane, worst_violation, min_slack};\
        return res;                                                                             \
    }                                                                                           \
    FLOW_MAYBE_UNUSED static inline FlowQPolyEval NAME##_eval(const NAME *p, const int32_t x[DIM]) { \
        return NAME##_eval_hysteresis(p, x, 0ULL);                                              \
    }

#define FLOW_QPOLY_DEFINE(NAME, DIM, SHIFT) FLOW_QPOLY_DEFINE_EX(NAME, DIM, SHIFT, FLOW_QPOLY_PLANES)

/* ============================================================================
 * 11. Hardware SPSC Ring, Page-Aligned UMA & Lock-Free QSBR Pointer Swapping
 * ============================================================================ */

#define FLOW_SPSC_DEFINE(RingType, ElemType, CapacityPow2)                                 \
    _Static_assert(((CapacityPow2) > 0) && (((CapacityPow2) & ((CapacityPow2) - 1)) == 0), \
                   "CapacityPow2 must be a power of 2");                                   \
    typedef struct {                                                                       \
        _Alignas(FLOW_HW_CACHE_LINE_BYTES) _Atomic size_t head;                            \
        uint8_t _pad_head[FLOW_HW_CACHE_LINE_BYTES - sizeof(_Atomic size_t)];              \
        _Alignas(FLOW_HW_CACHE_LINE_BYTES) _Atomic size_t tail;                            \
        uint8_t _pad_tail[FLOW_HW_CACHE_LINE_BYTES - sizeof(_Atomic size_t)];              \
        _Alignas(FLOW_HW_CACHE_LINE_BYTES) ElemType slots[CapacityPow2];                   \
    } RingType;                                                                            \
    static inline void RingType##_init(RingType *r) {                                      \
        if (!r) return;                                                                    \
        __builtin_memset(r, 0, sizeof(*r));                                                \
        atomic_init(&r->head, 0u);                                                         \
        atomic_init(&r->tail, 0u);                                                         \
    }                                                                                      \
    static inline int RingType##_push(RingType *r, ElemType val) {                         \
        if (!r) return 0;                                                                  \
        const size_t h = atomic_load_explicit(&r->head, memory_order_relaxed);             \
        const size_t t = atomic_load_explicit(&r->tail, memory_order_acquire);             \
        if ((h - t) >= (size_t)(CapacityPow2)) return 0;                                   \
        r->slots[h & ((size_t)(CapacityPow2) - 1u)] = val;                                 \
        atomic_store_explicit(&r->head, h + 1u, memory_order_release);                     \
        return 1;                                                                          \
    }                                                                                      \
    static inline int RingType##_pop(RingType *r, ElemType *out) {                         \
        if (!r || !out) return 0;                                                          \
        const size_t t = atomic_load_explicit(&r->tail, memory_order_relaxed);             \
        const size_t h = atomic_load_explicit(&r->head, memory_order_acquire);             \
        if (t == h) return 0;                                                              \
        *out = r->slots[t & ((size_t)(CapacityPow2) - 1u)];                                \
        atomic_store_explicit(&r->tail, t + 1u, memory_order_release);                     \
        return 1;                                                                          \
    }                                                                                      \
    static inline size_t RingType##_size(const RingType *r) {                              \
        if (!r) return 0u;                                                                 \
        const size_t h = atomic_load_explicit(&r->head, memory_order_acquire);             \
        const size_t t = atomic_load_explicit(&r->tail, memory_order_acquire);             \
        return h - t;                                                                      \
    }

#define FLOW_DEFINE_SPSC_RING FLOW_SPSC_DEFINE

typedef struct {
    void *cpu_ptr;
    void *gpu_shared_handle;
    size_t capacity_bytes;
    size_t page_size;
    int is_uma_coherent;
} FlowUmaBuffer;

int flow_uma_buffer_alloc(FlowUmaBuffer *buf, size_t capacity_bytes);
void flow_uma_buffer_free(FlowUmaBuffer *buf);

#define FLOW_QSBR_MAX_READERS 64U

typedef struct FlowQsbrEpoch FlowQsbrEpoch;
typedef FlowQsbrEpoch FlowReloadContext;

typedef struct FlowQsbrReader {
    FlowQsbrEpoch *context;
    uint32_t slot_idx;
    _Atomic uint64_t active_epoch;
    _Atomic uint64_t qsbr_epoch;
    _Atomic int registered;
    _Atomic int is_offline;
    _Atomic int is_quarantined;
    uint8_t _cache_pad[20];
} FLOW_CACHE_ALIGNED FlowQsbrReader;
typedef FlowQsbrReader FlowReloadReader;

struct FlowQsbrEpoch {
    void *host_context;
    _Atomic uint64_t epoch;
    _Atomic int stopping;
    _Atomic(const void *) active_bank;
    _Atomic(FlowQsbrReader *) readers[FLOW_QSBR_MAX_READERS];
} FLOW_CACHE_ALIGNED;

int flow_qsbr_init(FlowQsbrEpoch *ctx, void *host_context);
int flow_qsbr_fini(FlowQsbrEpoch *ctx);
int flow_qsbr_reader_register(FlowQsbrEpoch *ctx, FlowQsbrReader *reader);
int flow_qsbr_reader_unregister(FlowQsbrReader *reader);
int flow_qsbr_publish_ptr(FlowQsbrEpoch *ctx, const void *ptr);
const void *flow_qsbr_load_ptr(const FlowQsbrEpoch *ctx, FlowQsbrReader *reader);
void flow_qsbr_checkpoint(FlowQsbrReader *reader);
void flow_qsbr_offline(FlowQsbrReader *reader);
void flow_qsbr_online(FlowQsbrReader *reader);
int flow_qsbr_synchronize(FlowQsbrEpoch *ctx, uint64_t timeout_ns);

/* Compatibility wrappers */
#define flow_reload_init(ctx, host)            flow_qsbr_init(ctx, host)
#define flow_reload_fini(ctx)                  flow_qsbr_fini(ctx)
#define flow_reload_reader_register(ctx, r)    flow_qsbr_reader_register(ctx, r)
#define flow_reload_reader_unregister(r)       flow_qsbr_reader_unregister(r)

#ifdef __cplusplus
}
#endif


#endif /* FLOW_H */
