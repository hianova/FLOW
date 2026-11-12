/*
 * ============================================================================
 * FLOW: Lean 4 SSOT 3-Way Differential FFI Harness (Native ASM vs 32-Bit MCU)
 * ============================================================================
 * Compiled twice into `build/test-lean-ssot`:
 *   1. `-DFLOW_FFI_PREFIX=flow_native` (Hardware ARM64/x86_64 ASM + `__int128`)
 *   2. `-DFLOW_FORCE_C17_FALLBACK=1 -DFLOW_FORCE_32BIT_FALLBACK=1 -DFLOW_FFI_PREFIX=flow_mcu32`
 * ============================================================================
 */

#include "flow_hott_jet.h"

#ifndef FLOW_FFI_PREFIX
#define FLOW_FFI_PREFIX flow_native
#endif

#define FLOW_FFI_CAT2(a, b) a##_##b
#define FLOW_FFI_CAT(a, b)  FLOW_FFI_CAT2(a, b)
#define FLOW_FFI_FN(name)   FLOW_FFI_CAT(FLOW_FFI_PREFIX, name)

FLOW_QPOLY_DEFINE_EX(FlowVerifyPoly1D, 1, 16, 1)

static inline uint64_t flow_ffi_pack_jet2_mac(FlowJet2 j) {
    uint64_t h = FLOW_GOLDEN_RATIO_64;
    h = (h ^ (uint64_t)j.v) * FLOW_SSOT_FNV_PRIME_64;
    h = (h ^ (uint64_t)j.d1) * FLOW_SSOT_FNV_PRIME_64;
    h = (h ^ (uint64_t)j.d2) * FLOW_SSOT_FNV_PRIME_64;
    return h;
}

uint64_t FLOW_FFI_FN(ssot_spec_hash)(uint8_t dummy);
uint64_t FLOW_FFI_FN(isqrt_u64)(uint64_t x);
uint64_t FLOW_FFI_FN(qsqrt_val)(uint64_t x_q16_bits);
uint64_t FLOW_FFI_FN(qsqrt_tag)(uint64_t x_q16_bits);
uint64_t FLOW_FFI_FN(qlerp_val)(uint64_t x0_bits, uint64_t x1_bits, uint64_t alpha_bits);
uint64_t FLOW_FFI_FN(qlerp_tag)(uint64_t x0_bits, uint64_t x1_bits, uint64_t alpha_bits);
uint64_t FLOW_FFI_FN(qmargin3_val)(uint64_t pos_a, uint64_t neg_b, uint64_t pos_c);
uint64_t FLOW_FFI_FN(qmargin3_sieve)(uint64_t pos_a, uint64_t neg_b, uint64_t pos_c);
uint64_t FLOW_FFI_FN(fp64_add)(uint64_t a, uint64_t b);
uint64_t FLOW_FFI_FN(fp64_sub)(uint64_t a, uint64_t b);
uint64_t FLOW_FFI_FN(fp64_mul)(uint64_t a, uint64_t b);
uint64_t FLOW_FFI_FN(fp64_pow7)(uint64_t x);
uint64_t FLOW_FFI_FN(fp64_sponge)(uint64_t state, uint64_t word);
uint64_t FLOW_FFI_FN(rbit64)(uint64_t x);
uint64_t FLOW_FFI_FN(make_tag)(uint16_t fn_id, uint8_t g, uint8_t v, uint8_t ax, uint16_t code);
uint8_t FLOW_FFI_FN(is_tag)(uint64_t bits);
uint16_t FLOW_FFI_FN(tag_code)(uint64_t bits);
uint64_t FLOW_FFI_FN(kan_step_mask)(uint64_t mask, uint32_t bit, uint64_t filter);
uint8_t FLOW_FFI_FN(kan_step_status)(uint64_t mask, uint32_t bit, uint64_t filter);
uint64_t FLOW_FFI_FN(jet2_root_fast64)(uint64_t v_bits, uint64_t d1_bits, uint64_t d2_bits,
                                       uint64_t tmax_bits);
uint64_t FLOW_FFI_FN(jet2_root_128)(uint64_t v_bits, uint64_t d1_bits, uint64_t d2_bits,
                                    uint64_t tmax_bits);
uint64_t FLOW_FFI_FN(jet2_nonlinear_mac)(uint64_t av, uint64_t ad1, uint64_t ad2, uint64_t bv,
                                         uint64_t bd1, uint64_t bd2, uint64_t dt_bits);
uint8_t FLOW_FFI_FN(schmitt_hyst_1d)(uint64_t b_enter_bits, uint64_t b_exit_bits, uint64_t x_bits,
                                     uint8_t prev_violated);
uint8_t FLOW_FFI_FN(schmitt_hyst_hi)(uint64_t b_enter_bits, uint64_t b_exit_bits, uint64_t x_bits,
                                     uint8_t prev_latched);
uint8_t FLOW_FFI_FN(schmitt_hyst_lo)(uint64_t b_enter_bits, uint64_t b_exit_bits, uint64_t x_bits,
                                     uint8_t prev_latched);
uint64_t FLOW_FFI_FN(qsplit_pri)(uint64_t total, uint64_t num, uint64_t den);
uint64_t FLOW_FFI_FN(qsplit_rem)(uint64_t total, uint64_t num, uint64_t den);
uint64_t FLOW_FFI_FN(capsule64_sample_mac)(uint64_t seed);

uint64_t FLOW_FFI_FN(ssot_spec_hash)(uint8_t dummy) {
    (void)dummy;
    return FLOW_SSOT_SPEC_HASH;
}

uint64_t FLOW_FFI_FN(isqrt_u64)(uint64_t x) {
    return flow_asm_isqrt_u64_fast(x);
}

uint64_t FLOW_FFI_FN(qsqrt_val)(uint64_t x_q16_bits) {
    uint64_t sieve = FLOW_ASM_SIEVE_ALL_SOUND;
    FlowResultU64 r = flow_asm_qsqrt_shift16_i64((int64_t)x_q16_bits, &sieve);
    return r.value;
}

uint64_t FLOW_FFI_FN(qsqrt_tag)(uint64_t x_q16_bits) {
    uint64_t sieve = FLOW_ASM_SIEVE_ALL_SOUND;
    FlowResultU64 r = flow_asm_qsqrt_shift16_i64((int64_t)x_q16_bits, &sieve);
    return r.boundary_tag;
}

uint64_t FLOW_FFI_FN(qlerp_val)(uint64_t x0_bits, uint64_t x1_bits, uint64_t alpha_bits) {
    uint64_t sieve = FLOW_ASM_SIEVE_ALL_SOUND;
    FlowResultU64 r =
        flow_asm_qlerp_q16((int64_t)x0_bits, (int64_t)x1_bits, (int64_t)alpha_bits, &sieve);
    return r.value;
}

uint64_t FLOW_FFI_FN(qlerp_tag)(uint64_t x0_bits, uint64_t x1_bits, uint64_t alpha_bits) {
    uint64_t sieve = FLOW_ASM_SIEVE_ALL_SOUND;
    FlowResultU64 r =
        flow_asm_qlerp_q16((int64_t)x0_bits, (int64_t)x1_bits, (int64_t)alpha_bits, &sieve);
    return r.boundary_tag;
}

uint64_t FLOW_FFI_FN(qmargin3_val)(uint64_t pos_a, uint64_t neg_b, uint64_t pos_c) {
    uint64_t sieve = FLOW_ASM_SIEVE_ALL_SOUND;
    FlowResultU64 r = flow_asm_qmargin3_i64((int64_t)pos_a, (int64_t)neg_b, (int64_t)pos_c, &sieve);
    return r.value;
}

uint64_t FLOW_FFI_FN(qmargin3_sieve)(uint64_t pos_a, uint64_t neg_b, uint64_t pos_c) {
    uint64_t sieve = FLOW_ASM_SIEVE_ALL_SOUND;
    (void)flow_asm_qmargin3_i64((int64_t)pos_a, (int64_t)neg_b, (int64_t)pos_c, &sieve);
    return sieve;
}

uint64_t FLOW_FFI_FN(fp64_add)(uint64_t a, uint64_t b) {
    return flow_asm_fp64_add(a, b);
}

uint64_t FLOW_FFI_FN(fp64_sub)(uint64_t a, uint64_t b) {
    return flow_asm_fp64_sub(a, b);
}

uint64_t FLOW_FFI_FN(fp64_mul)(uint64_t a, uint64_t b) {
    return flow_asm_fp64_mul(a, b);
}

uint64_t FLOW_FFI_FN(fp64_pow7)(uint64_t x) {
    return flow_asm_fp64_pow7(x);
}

uint64_t FLOW_FFI_FN(fp64_sponge)(uint64_t state, uint64_t word) {
    return flow_asm_fp64_sponge_step(state, word);
}

uint64_t FLOW_FFI_FN(rbit64)(uint64_t x) {
    return flow_asm_rbit64(x);
}

uint64_t FLOW_FFI_FN(make_tag)(uint16_t fn_id, uint8_t g, uint8_t v, uint8_t ax, uint16_t code) {
    return flow_hott_make_boundary_tag(fn_id, g, v, ax, code);
}

uint8_t FLOW_FFI_FN(is_tag)(uint64_t bits) {
    return flow_hott_is_boundary_tag(bits) ? 1U : 0U;
}

uint16_t FLOW_FFI_FN(tag_code)(uint64_t bits) {
    return flow_hott_boundary_tag_code(bits);
}

uint64_t FLOW_FFI_FN(kan_step_mask)(uint64_t mask, uint32_t bit, uint64_t filter) {
    uint64_t cur = mask;
    if (bit >= FLOW_CUBICAL_MAX_DIM) {
        return cur;
    }
    uint64_t cand = cur ^ (1ULL << bit);
    FlowKanStatus st = flow_kan_check_homotopy(cur, cand, filter, NULL);
    return (st == FLOW_KAN_FILLED_HOMOTOPIC) ? cand : cur;
}

uint8_t FLOW_FFI_FN(kan_step_status)(uint64_t mask, uint32_t bit, uint64_t filter) {
    if (bit >= FLOW_CUBICAL_MAX_DIM) {
        return (uint8_t)FLOW_KAN_OBSTRUCTED_SINGULARITY;
    }
    uint64_t cand = mask ^ (1ULL << bit);
    FlowKanStatus st = flow_kan_check_homotopy(mask, cand, filter, NULL);
    return (uint8_t)st;
}

uint64_t FLOW_FFI_FN(jet2_root_fast64)(uint64_t v_bits, uint64_t d1_bits, uint64_t d2_bits,
                                       uint64_t tmax_bits) {
    int64_t out_t = 0;
    bool ok = flow_jet2_solve_root_fast64((int64_t)v_bits, (int64_t)d1_bits, (int64_t)d2_bits,
                                          (int64_t)tmax_bits, &out_t);
    return ok ? (uint64_t)out_t : 0ULL;
}

uint64_t FLOW_FFI_FN(jet2_root_128)(uint64_t v_bits, uint64_t d1_bits, uint64_t d2_bits,
                                    uint64_t tmax_bits) {
    int64_t out_t = 0;
    FlowJet2 j = flow_jet2_make((int64_t)v_bits, (int64_t)d1_bits, (int64_t)d2_bits);
    bool ok = flow_jet2_solve_root(j, (int64_t)tmax_bits, &out_t);
    return ok ? (uint64_t)out_t : 0ULL;
}

uint64_t FLOW_FFI_FN(jet2_nonlinear_mac)(uint64_t av, uint64_t ad1, uint64_t ad2, uint64_t bv,
                                         uint64_t bd1, uint64_t bd2, uint64_t dt_bits) {
    FlowJet2 a = flow_jet2_make((int64_t)av, (int64_t)ad1, (int64_t)ad2);
    FlowJet2 b = flow_jet2_make((int64_t)bv, (int64_t)bd1, (int64_t)bd2);
    FlowJet2 jm = flow_jet2_mul(a, b);
    FlowJet2 jd = flow_jet2_div(a, b);
    FlowJet2 js = flow_jet2_sqrt(a);
    int64_t tv = flow_jet2_eval_taylor(a, (int64_t)dt_bits);
    uint64_t sieve = FLOW_ASM_SIEVE_ALL_SOUND;
    (void)flow_asm_jet2_self_check_step(a, b, (int64_t)dt_bits, 65536, 65536, &sieve);
    uint64_t h = FLOW_GOLDEN_RATIO_64;
    h = (h ^ flow_ffi_pack_jet2_mac(jm)) * FLOW_SSOT_FNV_PRIME_64;
    h = (h ^ flow_ffi_pack_jet2_mac(jd)) * FLOW_SSOT_FNV_PRIME_64;
    h = (h ^ flow_ffi_pack_jet2_mac(js)) * FLOW_SSOT_FNV_PRIME_64;
    h = (h ^ (uint64_t)tv) * FLOW_SSOT_FNV_PRIME_64;
    h = (h ^ sieve) * FLOW_SSOT_FNV_PRIME_64;
    return h;
}

uint8_t FLOW_FFI_FN(schmitt_hyst_1d)(uint64_t b_enter_bits, uint64_t b_exit_bits, uint64_t x_bits,
                                     uint8_t prev_violated) {
    FlowVerifyPoly1D poly;
    FlowVerifyPoly1D_init(&poly);
    poly.A[0][0] = 1;
    FlowVerifyPoly1D_set_hysteresis(&poly, 0U, (int64_t)b_enter_bits, (int64_t)b_exit_bits);
    int32_t x_vec[1] = {(int32_t)(int64_t)x_bits};
    FlowQPolyEval ev = FlowVerifyPoly1D_eval_hysteresis(&poly, x_vec, prev_violated ? 1ULL : 0ULL);
    return (ev.violated_mask & 1ULL) ? 1U : 0U;
}

uint8_t FLOW_FFI_FN(schmitt_hyst_hi)(uint64_t b_enter_bits, uint64_t b_exit_bits, uint64_t x_bits,
                                     uint8_t prev_latched) {
    return flow_schmitt_hyst_hi_i64((int64_t)b_enter_bits, (int64_t)b_exit_bits, (int64_t)x_bits,
                                    prev_latched != 0U)
               ? 1U
               : 0U;
}

uint8_t FLOW_FFI_FN(schmitt_hyst_lo)(uint64_t b_enter_bits, uint64_t b_exit_bits, uint64_t x_bits,
                                     uint8_t prev_latched) {
    return flow_schmitt_hyst_lo_i64((int64_t)b_enter_bits, (int64_t)b_exit_bits, (int64_t)x_bits,
                                    prev_latched != 0U)
               ? 1U
               : 0U;
}

uint64_t FLOW_FFI_FN(qsplit_pri)(uint64_t total, uint64_t num, uint64_t den) {
    uint64_t pri = 0ULL;
    (void)flow_qsplit_u64(total, num, den, &pri, NULL);
    return pri;
}

uint64_t FLOW_FFI_FN(qsplit_rem)(uint64_t total, uint64_t num, uint64_t den) {
    uint64_t rem = 0ULL;
    (void)flow_qsplit_u64(total, num, den, NULL, &rem);
    return rem;
}

uint64_t FLOW_FFI_FN(capsule64_sample_mac)(uint64_t seed) {
    uint8_t cap[64];
    for (size_t w = 0U; w < 7U; ++w) {
        uint64_t word = seed ^ ((uint64_t)(w + 1U) * FLOW_GOLDEN_RATIO_64);
        flow_store_le64_bytes(&cap[w * 8U], word);
    }
    flow_store_le64_bytes(&cap[56], 0ULL);
    flow_capsule64_seal(cap, seed);
    if (!flow_capsule64_verify(cap, seed)) {
        return 0ULL;
    }
    uint8_t tampered[64];
    for (size_t i = 0U; i < 64U; ++i) {
        tampered[i] = cap[i];
    }
    tampered[0] ^= 0x01U;
    if (flow_capsule64_verify(tampered, seed)) {
        return 0ULL;
    }
    return flow_load_le64_bytes(&cap[56]);
}
