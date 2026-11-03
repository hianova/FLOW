#ifndef FLOW_ASM_SIEVE_H
#define FLOW_ASM_SIEVE_H

#include "flow.h"
#include "bitspace.h"
#include "cubical_hott.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW 90% ASM + HoTT & 2-Jet Self-Checking & Self-Bootstrapping Substrate
 * ============================================================================
 * Eliminates C Abstract Machine "Undefined Behavior" (signed integer overflow,
 * out-of-bounds pointer indexing, unverified FPU NaN/Inf traps) and subjective
 * post-hoc C rule tables (`FlowBoxConstraint` / `snprintf` strings) by grounding
 * all hot-path operations directly in total ISA instructions:
 *
 *   1. ARM64 / x86_64 Flag-Setting Q-Arithmetic (`adds`/`subs`/`smull`/`smulh` + `cset vs`)
 *      -> directly projects hardware `NZCV.V` into `FlowResultU64` + HoTT Sieve.
 *   2. Hardware FPU Status Register (`mrs fpsr` / `stmxcsr`) + Branchless IEEE-754 Exponent
 *      Masking -> directly projects `IOC`, `DZC`, `OFC`, and Quiet NaN boundary tags.
 *   3. Masked Cyclic Memory Arena (`and` + `bic` + `ldr`/`str`) -> maps any 64-bit
 *      index into Z / 2^k Z before address generation (0 out-of-bounds UB possible).
 *   4. 128-Bit 4D Q32.16 Half-Space Inner Product (`mul`/`smulh` + `adds`/`adc` + `extr`)
 *      & 2-Jet Differential Taylor Self-Consistency (`flow_asm_jet2_self_check_step`).
 * ============================================================================
 */

typedef enum {
    FLOW_ASM_SIEVE_INT_NO_OVERFLOW = (1ULL << 0),     /* Hardware NZCV.V == 0 on Q-arithmetic */
    FLOW_ASM_SIEVE_INT_NO_CARRY = (1ULL << 1),        /* Hardware NZCV.C unsigned bound valid */
    FLOW_ASM_SIEVE_FPU_NO_INVALID = (1ULL << 2),      /* FPSR.IOC == 0 and no NaN payload */
    FLOW_ASM_SIEVE_FPU_NO_DIVZERO = (1ULL << 3),      /* FPSR.DZC == 0 */
    FLOW_ASM_SIEVE_FPU_NO_OVERFLOW = (1ULL << 4),     /* FPSR.OFC == 0 and no +/-Inf */
    FLOW_ASM_SIEVE_MEM_MASKED_INBOUNDS = (1ULL << 5), /* Arena index (raw_idx & ~mask) == 0 */
    FLOW_ASM_SIEVE_MEM_ALIGNED = (1ULL << 6),         /* Base pointer cache-line aligned */
    FLOW_ASM_SIEVE_QPOLY_INTERIOR = (1ULL << 7),      /* Q32.16 hyperplane slack s_min >= 0 */
    FLOW_ASM_SIEVE_JET2_TAYLOR_0 = (1ULL << 8),       /* 0-Jet Taylor residual within tolerance */
    FLOW_ASM_SIEVE_JET2_TAYLOR_1 = (1ULL << 9),       /* 1-Jet tangent residual within tolerance */
    FLOW_ASM_SIEVE_ALL_SOUND = 0x3FFULL
} FlowAsmSieveAxis;

#define FLOW_HOTT_BOUNDARY_OVERFLOW UINT16_C(0x0E01)

/* ============================================================================
 * Pillar A: Zero-UB ASM Signed Q-Arithmetic with Hardware NZCV.V Capture
 * ============================================================================ */

static inline FlowResultU64 flow_asm_qadd_i64(int64_t a, int64_t b, uint64_t *inout_sieve) {
    int64_t res = 0;
    uint64_t ovf = 0;
#if defined(__aarch64__) || defined(__arm64__)
    __asm__ __volatile__("adds %[res], %[a], %[b]\n\t"
                         "cset %[ovf], vs"
                         : [res] "=&r"(res), [ovf] "=r"(ovf)
                         : [a] "r"(a), [b] "r"(b)
                         : "cc");
#elif defined(__x86_64__) || defined(_M_X64)
    res = a;
    uint8_t ovf8 = 0;
    __asm__ __volatile__("addq %[b], %[res]\n\t"
                         "seto %[ovf8]"
                         : [res] "+r"(res), [ovf8] "=qm"(ovf8)
                         : [b] "r"(b)
                         : "cc");
    ovf = (uint64_t)ovf8;
#else
    ovf = (uint64_t)__builtin_add_overflow(a, b, &res);
#endif
    if (inout_sieve) {
        *inout_sieve &= ~(ovf ? FLOW_ASM_SIEVE_INT_NO_OVERFLOW : 0ULL);
    }
    uint64_t btag = ovf ? flow_hott_make_boundary_tag(FLOW_HOTT_BOUNDARY_OVERFLOW, 0U,
                                                      (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY, 0U,
                                                      (uint16_t)ovf)
                        : 0ULL;
    FlowResultU64 out = {(uint64_t)res, btag};
    return out;
}

static inline FlowResultU64 flow_asm_qsub_i64(int64_t a, int64_t b, uint64_t *inout_sieve) {
    int64_t res = 0;
    uint64_t ovf = 0;
#if defined(__aarch64__) || defined(__arm64__)
    __asm__ __volatile__("subs %[res], %[a], %[b]\n\t"
                         "cset %[ovf], vs"
                         : [res] "=&r"(res), [ovf] "=r"(ovf)
                         : [a] "r"(a), [b] "r"(b)
                         : "cc");
#elif defined(__x86_64__) || defined(_M_X64)
    res = a;
    uint8_t ovf8 = 0;
    __asm__ __volatile__("subq %[b], %[res]\n\t"
                         "seto %[ovf8]"
                         : [res] "+r"(res), [ovf8] "=qm"(ovf8)
                         : [b] "r"(b)
                         : "cc");
    ovf = (uint64_t)ovf8;
#else
    ovf = (uint64_t)__builtin_sub_overflow(a, b, &res);
#endif
    if (inout_sieve) {
        *inout_sieve &= ~(ovf ? FLOW_ASM_SIEVE_INT_NO_OVERFLOW : 0ULL);
    }
    uint64_t btag = ovf ? flow_hott_make_boundary_tag(FLOW_HOTT_BOUNDARY_OVERFLOW, 1U,
                                                      (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY, 0U,
                                                      (uint16_t)ovf)
                        : 0ULL;
    FlowResultU64 out = {(uint64_t)res, btag};
    return out;
}

static inline FlowResultU64 flow_asm_qmul_shift16_i64(int64_t a_q16, int64_t b_q16,
                                                      uint64_t *inout_sieve) {
    int64_t res = 0;
    uint64_t ovf = 0;
#if defined(__aarch64__) || defined(__arm64__)
    uint64_t lo = 0, hi = 0;
    __asm__ __volatile__("mul   %[lo],  %[a],  %[b]\n\t"
                         "smulh %[hi],  %[a],  %[b]\n\t"
                         "extr  %[res], %[hi], %[lo], #16\n\t"
                         "asr   %[lo],  %[res], #63\n\t"
                         "cmp   %[lo],  %[hi], asr #15\n\t"
                         "cset  %[ovf], ne"
                         : [res] "=&r"(res), [lo] "=&r"(lo), [hi] "=&r"(hi), [ovf] "=r"(ovf)
                         : [a] "r"(a_q16), [b] "r"(b_q16)
                         : "cc");
#else
    __extension__ typedef __int128 i128;
    i128 prod = ((i128)a_q16 * (i128)b_q16) >> 16;
    res = (int64_t)(uint64_t)prod;
    ovf = (prod > (i128)INT64_MAX || prod < (i128)INT64_MIN) ? 1ULL : 0ULL;
#endif
    if (inout_sieve) {
        *inout_sieve &= ~(ovf ? FLOW_ASM_SIEVE_INT_NO_OVERFLOW : 0ULL);
    }
    uint64_t btag = ovf ? flow_hott_make_boundary_tag(FLOW_HOTT_BOUNDARY_OVERFLOW, 2U,
                                                      (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY, 0U,
                                                      (uint16_t)ovf)
                        : 0ULL;
    FlowResultU64 out = {(uint64_t)res, btag};
    return out;
}

/* ============================================================================
 * Pillar B: Hardware FPU Status Register (FPSR) & Bitwise IEEE-754 Sieve
 * ============================================================================ */

static inline uint64_t flow_asm_fpsr_read(void) {
#if defined(__aarch64__) || defined(__arm64__)
    uint64_t fpsr = 0;
    __asm__ __volatile__("mrs %0, fpsr" : "=r"(fpsr));
    return fpsr;
#elif defined(__x86_64__) || defined(_M_X64)
    uint32_t mxcsr = 0;
    __asm__ __volatile__("stmxcsr %0" : "=m"(mxcsr));
    return (uint64_t)(mxcsr & 0x3FU);
#else
    return 0ULL;
#endif
}

static inline void flow_asm_fpsr_clear(void) {
#if defined(__aarch64__) || defined(__arm64__)
    uint64_t z = 0;
    __asm__ __volatile__("msr fpsr, %0" : : "r"(z) : "memory");
#elif defined(__x86_64__) || defined(_M_X64)
    uint32_t mxcsr = 0;
    __asm__ __volatile__("stmxcsr %0" : "=m"(mxcsr));
    mxcsr &= ~0x3FU;
    __asm__ __volatile__("ldmxcsr %0" : : "m"(mxcsr) : "memory");
#endif
}

static inline double flow_asm_fdiv_f64(double a, double b) {
    double res = 0.0;
#if defined(__aarch64__) || defined(__arm64__)
    __asm__ __volatile__("fdiv %d[res], %d[a], %d[b]" : [res] "=w"(res) : [a] "w"(a), [b] "w"(b));
#elif defined(__x86_64__) || defined(_M_X64)
    res = a;
    __asm__ __volatile__("divsd %[b], %[res]" : [res] "+x"(res) : [b] "x"(b));
#else
    volatile double va = a, vb = b;
    res = va / vb;
#endif
    return res;
}

static inline uint64_t flow_asm_fpu_check_jet2(FlowJet2 j, uint64_t *inout_sieve) {
    uint64_t bv = 0, bd1 = 0, bd2 = 0;
    __builtin_memcpy(&bv, &j.v, sizeof(bv));
    __builtin_memcpy(&bd1, &j.d1, sizeof(bd1));
    __builtin_memcpy(&bd2, &j.d2, sizeof(bd2));

    const uint64_t abs_mask = UINT64_C(0x7FFFFFFFFFFFFFFF);
    const uint64_t inf_bits = UINT64_C(0x7FF0000000000000);

    uint64_t av = bv & abs_mask;
    uint64_t ad1 = bd1 & abs_mask;
    uint64_t ad2 = bd2 & abs_mask;

    uint64_t has_nan = (av > inf_bits) | (ad1 > inf_bits) | (ad2 > inf_bits);
    uint64_t has_inf = (av == inf_bits) | (ad1 == inf_bits) | (ad2 == inf_bits);

    uint64_t fpsr = flow_asm_fpsr_read();
    uint64_t ioc = (fpsr & 1ULL) | has_nan;
    uint64_t dzc = (fpsr >> 1U) & 1ULL;
    uint64_t ofc = ((fpsr >> 2U) & 1ULL) | has_inf;

    uint64_t clear_bits = (ioc ? FLOW_ASM_SIEVE_FPU_NO_INVALID : 0ULL) |
                          (dzc ? FLOW_ASM_SIEVE_FPU_NO_DIVZERO : 0ULL) |
                          (ofc ? FLOW_ASM_SIEVE_FPU_NO_OVERFLOW : 0ULL);

    uint64_t s = inout_sieve ? *inout_sieve : FLOW_ASM_SIEVE_ALL_SOUND;
    s &= ~clear_bits;
    if (inout_sieve) {
        *inout_sieve = s;
    }
    return s;
}

/* ============================================================================
 * Pillar C: ASM Masked Cyclic Memory Arena (Z / 2^k Z Total Indexing)
 * ============================================================================ */

static inline uint64_t flow_asm_arena_load_u64(const uint64_t *base, uint64_t raw_idx,
                                               uint64_t cap_mask, uint64_t *inout_sieve) {
    uint64_t val = 0ULL;
    uint64_t midx = 0ULL;
    uint64_t oob = 0ULL;
#if defined(__aarch64__) || defined(__arm64__)
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
        uint64_t clear_bits = ((oob != 0ULL) ? FLOW_ASM_SIEVE_MEM_MASKED_INBOUNDS : 0ULL) |
                              (unaligned ? FLOW_ASM_SIEVE_MEM_ALIGNED : 0ULL);
        *inout_sieve &= ~clear_bits;
    }
    return val;
}

static inline void flow_asm_arena_store_u64(uint64_t *base, uint64_t raw_idx, uint64_t cap_mask,
                                            uint64_t val, uint64_t *inout_sieve) {
    uint64_t midx = 0ULL;
    uint64_t oob = 0ULL;
#if defined(__aarch64__) || defined(__arm64__)
    __asm__ __volatile__(
        "and %[midx], %[idx], %[mask]\n\t"
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
        uint64_t clear_bits = ((oob != 0ULL) ? FLOW_ASM_SIEVE_MEM_MASKED_INBOUNDS : 0ULL) |
                              (unaligned ? FLOW_ASM_SIEVE_MEM_ALIGNED : 0ULL);
        *inout_sieve &= ~clear_bits;
    }
}

/* ============================================================================
 * Pillar D: ASM 4D Q32.16 Half-Space Dot-Product & 2-Jet Self-Bootstrapping
 * ============================================================================ */

static inline int64_t flow_asm_qpoly4d_eval_plane(const int64_t a_q16[4], int64_t b_q16,
                                                  const int64_t x_q16[4], uint64_t *inout_sieve) {
    int64_t dot_q16 = 0;
    uint64_t dot_ovf = 0;
#if defined(__aarch64__) || defined(__arm64__)
    uint64_t acc_lo = 0, acc_hi = 0, p_lo = 0, p_hi = 0, sign_ext = 0;
    __asm__ __volatile__(
        "mul   %[acc_lo], %[a0], %[x0]\n\t"
        "smulh %[acc_hi], %[a0], %[x0]\n\t"
        "mul   %[p_lo],   %[a1], %[x1]\n\t"
        "smulh %[p_hi],   %[a1], %[x1]\n\t"
        "adds  %[acc_lo], %[acc_lo], %[p_lo]\n\t"
        "adc   %[acc_hi], %[acc_hi], %[p_hi]\n\t"
        "mul   %[p_lo],   %[a2], %[x2]\n\t"
        "smulh %[p_hi],   %[a2], %[x2]\n\t"
        "adds  %[acc_lo], %[acc_lo], %[p_lo]\n\t"
        "adc   %[acc_hi], %[acc_hi], %[p_hi]\n\t"
        "mul   %[p_lo],   %[a3], %[x3]\n\t"
        "smulh %[p_hi],   %[a3], %[x3]\n\t"
        "adds  %[acc_lo], %[acc_lo], %[p_lo]\n\t"
        "adc   %[acc_hi], %[acc_hi], %[p_hi]\n\t"
        "extr  %[dot],    %[acc_hi], %[acc_lo], #16\n\t"
        "asr   %[sign],   %[dot], #63\n\t"
        "cmp   %[sign],   %[acc_hi], asr #15\n\t"
        "cset  %[ovf],    ne"
        : [dot] "=&r"(dot_q16), [acc_lo] "=&r"(acc_lo), [acc_hi] "=&r"(acc_hi), [p_lo] "=&r"(p_lo),
          [p_hi] "=&r"(p_hi), [sign] "=&r"(sign_ext), [ovf] "=r"(dot_ovf)
        : [a0] "r"(a_q16[0]), [x0] "r"(x_q16[0]), [a1] "r"(a_q16[1]), [x1] "r"(x_q16[1]),
          [a2] "r"(a_q16[2]), [x2] "r"(x_q16[2]), [a3] "r"(a_q16[3]), [x3] "r"(x_q16[3])
        : "cc");
#else
    __extension__ typedef __int128 i128;
    i128 acc = (i128)a_q16[0] * (i128)x_q16[0] + (i128)a_q16[1] * (i128)x_q16[1] +
               (i128)a_q16[2] * (i128)x_q16[2] + (i128)a_q16[3] * (i128)x_q16[3];
    i128 shifted = acc >> 16;
    dot_q16 = (int64_t)(uint64_t)shifted;
    dot_ovf = (shifted > (i128)INT64_MAX || shifted < (i128)INT64_MIN) ? 1ULL : 0ULL;
#endif
    FlowResultU64 slack_res = flow_asm_qsub_i64(b_q16, dot_q16, inout_sieve);
    int64_t slack = (int64_t)slack_res.value;
    if (inout_sieve) {
        uint64_t clear_bits = (dot_ovf ? FLOW_ASM_SIEVE_INT_NO_OVERFLOW : 0ULL) |
                              ((slack < 0 || dot_ovf || flow_result_u64_is_boundary(slack_res))
                                   ? FLOW_ASM_SIEVE_QPOLY_INTERIOR
                                   : 0ULL);
        *inout_sieve &= ~clear_bits;
    }
    return slack;
}

static inline uint64_t flow_asm_jet2_self_check_step(FlowJet2 prev, FlowJet2 next, double dt,
                                                     int64_t tol_v_q16, int64_t tol_d1_q16,
                                                     uint64_t *inout_sieve) {
    uint64_t local_sieve = inout_sieve ? *inout_sieve : FLOW_ASM_SIEVE_ALL_SOUND;
    (void)flow_asm_fpu_check_jet2(prev, &local_sieve);
    (void)flow_asm_fpu_check_jet2(next, &local_sieve);

    /* 0-Jet Taylor prediction: v_hat = prev.v + prev.d1 * dt + 0.5 * prev.d2 * dt^2 */
    double v_hat = flow_jet2_eval_taylor(prev, dt);
    /* 1-Jet trapezoidal tangent prediction: d1_hat = prev.d1 + 0.5 * (prev.d2 + next.d2) * dt */
    double d1_hat = prev.d1 + 0.5 * (prev.d2 + next.d2) * dt;

    double err_v = __builtin_fabs(next.v - v_hat) * 65536.0;
    double err_d1 = __builtin_fabs(next.d1 - d1_hat) * 65536.0;

    int64_t r0_q16 =
        (__builtin_isfinite(err_v) && err_v < (double)INT64_MAX) ? (int64_t)err_v : INT64_MAX;
    int64_t r1_q16 =
        (__builtin_isfinite(err_d1) && err_d1 < (double)INT64_MAX) ? (int64_t)err_d1 : INT64_MAX;

    /* Check tol_v_q16 - r0_q16 >= 0 and tol_d1_q16 - r1_q16 >= 0 via ASM subs */
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

#ifdef __cplusplus
}
#endif

#endif /* FLOW_ASM_SIEVE_H */
