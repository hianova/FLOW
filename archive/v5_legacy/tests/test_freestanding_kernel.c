/*
 * ============================================================================
 * FLOW: Bare-Metal / Ring-0 Freestanding ISA Verification Kernel
 * ============================================================================
 * Compiled with:
 *   -std=c11 -ffreestanding -nostdlib -fno-builtin -DFLOW_FREESTANDING=1
 *
 * Proves that FLOW's core ISA substrate (`flow_asm.h` + `flow_hott_jet.h`)
 * has ZERO dependency on any OS kernel, POSIX layer, or hosted C library
 * (`__STDC_HOSTED__ == 0`, `nm -u` undefined symbol count == 0).
 * ============================================================================
 */

#include "flow_hott_jet.h"

_Static_assert(FLOW_IS_FREESTANDING == 1, "Must compile in freestanding mode");
#if defined(__STDC_HOSTED__)
_Static_assert(__STDC_HOSTED__ == 0, "__STDC_HOSTED__ must be 0 under -ffreestanding");
#endif

FLOW_QPOLY_DEFINE_EX(FlowRing0Poly4D, 4, 16, 4)

uint64_t flow_freestanding_ring0_step(uint64_t *arena_64, uint64_t raw_idx, int64_t x_q16,
                                      int64_t v_q16, int64_t a_q16, uint64_t *out_sieve);

uint64_t flow_freestanding_ring0_step(uint64_t *arena_64, uint64_t raw_idx, int64_t x_q16,
                                      int64_t v_q16, int64_t a_q16, uint64_t *out_sieve) {
    uint64_t sieve = FLOW_ASM_SIEVE_ALL_SOUND;
    uint64_t t0 = flow_hardware_cycles();

    /* 1. Flag-setting Q48.16 integer arithmetic + exact 128-bit operators */
    FlowResultU64 sum = flow_asm_qadd_i64(x_q16, v_q16, &sieve);
    FlowResultU64 prod = flow_asm_qmul_shift16_i64((int64_t)sum.value, a_q16, &sieve);
    FlowResultU64 quot =
        flow_asm_qdiv_shift16_i64(x_q16, v_q16 != 0 ? v_q16 : FLOW_Q16_ONE, &sieve);
    FlowResultU64 root = flow_asm_qsqrt_shift16_i64(x_q16 > 0 ? x_q16 : FLOW_Q16_ONE, &sieve);

    /* 2. Z / 2^k Z masked cyclic memory arena (64-word ring, mask = 63) */
    flow_asm_arena_store_u64(arena_64, raw_idx, 63ULL, prod.value ^ quot.value ^ root.value,
                             &sieve);
    uint64_t loaded = flow_asm_arena_load_u64(arena_64, raw_idx, 63ULL, &sieve);

    /* 3. Pure Q48.16 2-Jet Taylor, algebra & root solver */
    FlowJet2 j0 = flow_jet2_make(x_q16, v_q16, a_q16);
    int64_t dt_q16 = FLOW_Q16_ONE >> 4U;
    int64_t next_v = flow_jet2_eval_taylor(j0, dt_q16);
    FlowJet2 j1 = flow_jet2_make(next_v, v_q16 + ((a_q16 * dt_q16) >> FLOW_Q16_SHIFT), a_q16);
    FlowJet2 j_prod = flow_jet2_mul(j0, j1);
    FlowJet2 j_div =
        flow_jet2_div(j_prod, flow_jet2_make(v_q16 != 0 ? v_q16 : FLOW_Q16_ONE, a_q16, 0));
    FlowJet2 j_sq = flow_jet2_sqrt(flow_jet2_abs(j0));
    (void)flow_asm_jet2_self_check_step(j0, j1, dt_q16, 1024, 1024, &sieve);

    int64_t t_hit_q16 = 0;
    (void)flow_jet2_solve_root(j0, FLOW_Q16_ONE, &t_hit_q16);
    int64_t t_fast_q16 = 0;
    (void)flow_jet2_solve_root_fast64(x_q16 > 0 ? x_q16 : FLOW_Q16_ONE, v_q16, a_q16,
                                      FLOW_Q16_ONE * 4, &t_fast_q16);

    /* 4. 4D Q-Polyhedron evaluation, scalar quantization & dual-threshold hysteresis */
    int32_t q_scal = FlowRing0Poly4D_quantize_scalar(x_q16, (v_q16 > 0) ? v_q16 : FLOW_Q16_ONE);
    int64_t plane_a[4] = {FLOW_Q16_ONE, (int64_t)q_scal, 0, 0};
    int64_t state_x[4] = {next_v, j_div.v, j_sq.v, t_hit_q16 ^ t_fast_q16};
    int64_t slack = flow_asm_qpoly4d_eval_plane(plane_a, FLOW_Q16_ONE * 100, state_x, &sieve);

    FlowRing0Poly4D r0_poly;
    FlowRing0Poly4D_init(&r0_poly);
    r0_poly.A[0][0] = 1;
    FlowRing0Poly4D_set_hysteresis(&r0_poly, 0U, FLOW_Q16_ONE * 10, FLOW_Q16_ONE * 8);
    int32_t x_vec32[4] = {(int32_t)x_q16, (int32_t)v_q16, (int32_t)a_q16, 0};
    FlowQPolyEval r0_ev = FlowRing0Poly4D_eval_hysteresis(&r0_poly, x_vec32, raw_idx & 1ULL);
    bool sch_hi = flow_schmitt_hyst_hi_i64(FLOW_Q16_ONE * 10, FLOW_Q16_ONE * 8, x_q16, false);
    bool sch_lo = flow_schmitt_hyst_lo_i64(FLOW_Q16_ONE * 8, FLOW_Q16_ONE * 10, x_q16, true);
    int64_t sat_q = flow_q16_div_sat(flow_q16_mul_sat(x_q16, v_q16), FLOW_Q16_ONE);

    /* 5. Wave64 1.58-bit ternary dot product */
    FlowBmfTernary64 w = {UINT64_C(0x5555555555555555), UINT64_C(0xAAAAAAAAAAAAAAAA)};
    int32_t dot = flow_bmf_ternary64_dot(w, w);

    /* 6. Goldilocks F_p field, 64B capsule seal, 128-bit conservation split, rbit & Lerp */
    uint64_t fp_prod = flow_asm_fp64_mul(loaded, (uint64_t)x_q16 + 1ULL);
    uint64_t fp_p7 = flow_asm_fp64_sponge_step(fp_prod, flow_asm_isqrt_u64_fast(loaded));
    uint64_t pri_split = 0ULL, rem_split = 0ULL;
    (void)flow_qsplit_u64(loaded, 250ULL, 10000ULL, &pri_split, &rem_split);
    uint8_t cap64[64];
    for (size_t i = 0U; i < 64U; ++i) {
        cap64[i] = (uint8_t)(i ^ (raw_idx & 0xFFULL));
    }
    flow_capsule64_seal(cap64, FLOW_GOLDEN_RATIO_64);
    uint64_t cap_ok = flow_capsule64_verify(cap64, FLOW_GOLDEN_RATIO_64) ? 1ULL : 0ULL;
    uint64_t rb = flow_asm_rbit64(raw_idx + 1ULL);
    FlowResultU64 l_q16 = flow_asm_qlerp_q16(x_q16, v_q16, FLOW_Q16_ONE >> 1U, &sieve);
    FlowJet2 j_mid = flow_jet2_lerp(j0, j1, FLOW_Q16_ONE >> 1U);

    uint64_t t1 = flow_hardware_cycles();
    if (out_sieve) {
        *out_sieve = sieve;
    }
    return loaded ^ (uint64_t)slack ^ r0_ev.violated_mask ^ (uint64_t)sch_hi ^ (uint64_t)sch_lo ^
           (uint64_t)sat_q ^ (uint64_t)dot ^ fp_p7 ^ (pri_split + rem_split) ^ cap_ok ^ rb ^
           l_q16.value ^ (uint64_t)j_mid.v ^ (t1 - t0);
}
