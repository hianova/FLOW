/*
 * ============================================================================
 * FLOW: Lean 4 SSOT 3-Way Differential FFI Harness (Native ASM vs 32-Bit MCU)
 * ============================================================================
 * Compiled twice into `build/test-lean-ssot`:
 *   1. `-DFLOW_FFI_PREFIX=flow_native` (Hardware ARM64/x86_64 ASM + `__int128`)
 *   2. `-DFLOW_FORCE_C17_FALLBACK=1 -DFLOW_FORCE_32BIT_FALLBACK=1 -DFLOW_FFI_PREFIX=flow_mcu32`
 * ============================================================================
 */

#include "flow.h"

#ifndef FLOW_FFI_PREFIX
#define FLOW_FFI_PREFIX flow_native
#endif

#define FLOW_FFI_CAT2(a, b) a##_##b
#define FLOW_FFI_CAT(a, b)  FLOW_FFI_CAT2(a, b)
#define FLOW_FFI_FN(name)   FLOW_FFI_CAT(FLOW_FFI_PREFIX, name)

uint64_t FLOW_FFI_FN(ssot_spec_hash)(uint8_t dummy);
uint32_t FLOW_FFI_FN(clz64_nz)(uint64_t x);
uint32_t FLOW_FFI_FN(ctz64_nz)(uint64_t x);
uint32_t FLOW_FFI_FN(popcnt64)(uint64_t x);
uint64_t FLOW_FFI_FN(rbit64)(uint64_t x);
uint64_t FLOW_FFI_FN(mul_u64_hi)(uint64_t a, uint64_t b);
uint64_t FLOW_FFI_FN(mul_u64_lo)(uint64_t a, uint64_t b);
uint64_t FLOW_FFI_FN(isqrt_u64)(uint64_t x);
uint64_t FLOW_FFI_FN(fp64_reduce)(uint64_t x);
uint64_t FLOW_FFI_FN(fp64_add)(uint64_t a, uint64_t b);
uint64_t FLOW_FFI_FN(fp64_sub)(uint64_t a, uint64_t b);
uint64_t FLOW_FFI_FN(fp64_mul)(uint64_t a, uint64_t b);
uint64_t FLOW_FFI_FN(fp64_sponge)(uint64_t s, uint64_t w);
uint64_t FLOW_FFI_FN(make_tag)(uint16_t fn_id, uint8_t g, uint8_t v, uint8_t ax, uint16_t code);
uint8_t FLOW_FFI_FN(is_tag)(uint64_t bits);
uint16_t FLOW_FFI_FN(tag_code)(uint64_t bits);
uint64_t FLOW_FFI_FN(ternary_dot)(uint64_t nz_a, uint64_t sg_a, uint64_t nz_b, uint64_t sg_b);
uint64_t FLOW_FFI_FN(jet2_solve_root)(uint64_t v_bits, uint64_t d1_bits, uint64_t d2_bits, uint64_t tmax_bits);
uint64_t FLOW_FFI_FN(jet2_eval_taylor)(uint64_t v_bits, uint64_t d1_bits, uint64_t d2_bits, uint64_t dt_bits);
uint64_t FLOW_FFI_FN(capsule64_mac)(uint64_t seed);

uint64_t FLOW_FFI_FN(ssot_spec_hash)(uint8_t dummy) {
    (void)dummy;
    return FLOW_SSOT_SPEC_HASH;
}

uint32_t FLOW_FFI_FN(clz64_nz)(uint64_t x) {
    return (x == 0ULL) ? 64U : flow_clz64_nonzero(x);
}

uint32_t FLOW_FFI_FN(ctz64_nz)(uint64_t x) {
    return (x == 0ULL) ? 64U : flow_ctz64_nonzero(x);
}

uint32_t FLOW_FFI_FN(popcnt64)(uint64_t x) {
    return flow_popcnt64(x);
}

uint64_t FLOW_FFI_FN(rbit64)(uint64_t x) {
    return flow_asm_rbit64(x);
}

uint64_t FLOW_FFI_FN(mul_u64_hi)(uint64_t a, uint64_t b) {
    return flow_u128_hi(flow_u128_mul_u64(a, b));
}

uint64_t FLOW_FFI_FN(mul_u64_lo)(uint64_t a, uint64_t b) {
    return flow_u128_lo(flow_u128_mul_u64(a, b));
}

uint64_t FLOW_FFI_FN(isqrt_u64)(uint64_t x) {
    return flow_isqrt_u64(x);
}

uint64_t FLOW_FFI_FN(fp64_reduce)(uint64_t x) {
    return flow_asm_fp64_reduce(x);
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

uint64_t FLOW_FFI_FN(fp64_sponge)(uint64_t s, uint64_t w) {
    return flow_asm_fp64_sponge_step(s, w);
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

uint64_t FLOW_FFI_FN(ternary_dot)(uint64_t nz_a, uint64_t sg_a, uint64_t nz_b, uint64_t sg_b) {
    FlowBmfTernary64 a = {nz_a, sg_a};
    FlowBmfTernary64 b = {nz_b, sg_b};
    return (uint64_t)(int64_t)flow_bmf_ternary64_dot(a, b);
}

uint64_t FLOW_FFI_FN(jet2_solve_root)(uint64_t v_bits, uint64_t d1_bits, uint64_t d2_bits, uint64_t tmax_bits) {
    int64_t out_t = 0;
    bool ok = flow_jet2_solve_root(flow_jet2_make((int64_t)v_bits, (int64_t)d1_bits, (int64_t)d2_bits),
                                   (int64_t)tmax_bits, &out_t);
    return ok ? (uint64_t)out_t : 0ULL;
}

uint64_t FLOW_FFI_FN(jet2_eval_taylor)(uint64_t v_bits, uint64_t d1_bits, uint64_t d2_bits, uint64_t dt_bits) {
    FlowJet2 j = flow_jet2_make((int64_t)v_bits, (int64_t)d1_bits, (int64_t)d2_bits);
    return (uint64_t)flow_jet2_eval_taylor(j, (int64_t)dt_bits);
}

uint64_t FLOW_FFI_FN(capsule64_mac)(uint64_t seed) {
    uint8_t cap[64];
    for (size_t w = 0U; w < 7U; ++w) {
        uint64_t word = seed ^ ((uint64_t)(w + 1U) * FLOW_GOLDEN_RATIO_64);
        flow_store_le64_bytes(&cap[w * 8U], word);
    }
    flow_store_le64_bytes(&cap[56], 0ULL);
    flow_capsule64_seal(cap, seed);
    if (!flow_capsule64_verify(cap, seed)) return 0ULL;
    return flow_load_le64_bytes(&cap[56]);
}
