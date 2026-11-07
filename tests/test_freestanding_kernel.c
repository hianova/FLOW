/*
 * ============================================================================
 * FLOW v4: Bare-Metal / Ring-0 Freestanding ISA Verification Kernel
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
    FlowResultU64 root = flow_asm_qsqrt_shift16_i64(x_q16 > 0 ? x_q16 : FLOW_Q16_ONE, &sieve);

    /* 2. Z / 2^k Z masked cyclic memory arena (64-word ring, mask = 63) */
    flow_asm_arena_store_u64(arena_64, raw_idx, 63ULL, prod.value ^ root.value, &sieve);
    uint64_t loaded = flow_asm_arena_load_u64(arena_64, raw_idx, 63ULL, &sieve);

    /* 3. Pure Q48.16 2-Jet Taylor & root solver */
    FlowJet2 j0 = flow_jet2_make(x_q16, v_q16, a_q16);
    int64_t dt_q16 = FLOW_Q16_ONE >> 4U;
    int64_t next_v = flow_jet2_eval_taylor(j0, dt_q16);
    FlowJet2 j1 = flow_jet2_make(next_v, v_q16 + ((a_q16 * dt_q16) >> FLOW_Q16_SHIFT), a_q16);
    (void)flow_asm_jet2_self_check_step(j0, j1, dt_q16, 1024, 1024, &sieve);

    int64_t t_hit_q16 = 0;
    (void)flow_jet2_solve_root(j0, FLOW_Q16_ONE, &t_hit_q16);

    /* 4. 4D Q-Polyhedron evaluation */
    int64_t plane_a[4] = {FLOW_Q16_ONE, 0, 0, 0};
    int64_t state_x[4] = {next_v, v_q16, a_q16, t_hit_q16};
    int64_t slack = flow_asm_qpoly4d_eval_plane(plane_a, FLOW_Q16_ONE * 100, state_x, &sieve);

    /* 5. Wave64 1.58-bit ternary dot product */
    FlowBmfTernary64 w = {UINT64_C(0x5555555555555555), UINT64_C(0xAAAAAAAAAAAAAAAA)};
    int32_t dot = flow_bmf_ternary64_dot(w, w);

    uint64_t t1 = flow_hardware_cycles();
    if (out_sieve) {
        *out_sieve = sieve;
    }
    return loaded ^ (uint64_t)slack ^ (uint64_t)dot ^ (t1 - t0);
}
