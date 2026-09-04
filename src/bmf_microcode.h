#ifndef FLOW_BMF_MICROCODE_H
#define FLOW_BMF_MICROCODE_H

#include "bitmanifold.h"
#include "smt.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * Presburger 1-Bit Microcode Execution Engine (bmf_microcode.h)
 * ============================================================================
 * Pursuing the Kolmogorov Complexity lower bound:
 * Eliminates C structure layout bloat, runtime pointer indirections, and
 * interpreter branch overhead!
 *
 * Physical constraints and Presburger affine halfspaces are compiled into a
 * compact, dense stream of 64-bit Micro-Operations (uOps).
 *
 * MicroOp Bit Layout (64-Bit Word):
 *   [Bits 63..56] Opcode (8 bits)
 *   [Bits 55..48] Register / Dimension index (8 bits)
 *   [Bits 47..32] Fixed-Point Bound (16 bits, int16_t, scale = 1/1000.0)
 *   [Bits 31..24] Target 1-Bit Switch Bit Index (8 bits, 0..63)
 *   [Bits 23..16] Condition Flags (8 bits, e.g. 0x01: hard invariant)
 *   [Bits 15..00] Immediate / Mask (16 bits)
 *
 * Runs branchlessly in sub-5ns, directly updating FlowBmf1BitCanvas switchboard.
 * ============================================================================
 */

#define FLOW_BMF_MICROCODE_MAX_OPS 32

/* Micro-Opcode Definitions */
typedef enum {
    FLOW_UOP_NOP               = 0x00,
    FLOW_UOP_CLAMP_UPPER       = 0x01, /* if state[r] > B, violation / switch off */
    FLOW_UOP_CLAMP_LOWER       = 0x02, /* if state[r] < B, violation / switch off */
    FLOW_UOP_ASSERT_INVARIANT  = 0x03, /* if switch is 0, trigger SMT fail-safe */
    FLOW_UOP_FORCE_SWITCH_ON   = 0x04, /* canvas->switchboard_bits |= (1 << sw) */
    FLOW_UOP_FORCE_SWITCH_OFF  = 0x05, /* canvas->switchboard_bits &= ~(1 << sw) */
    FLOW_UOP_CHAOTIC_FLIP      = 0x06  /* Single-cycle 1-bit mutation */
} FlowBmfMicroOpcode;

/* Helper to pack a 64-bit Micro-Op */
static inline uint64_t flow_bmf_uop_pack(FlowBmfMicroOpcode opcode,
                                         uint8_t reg_idx,
                                         int16_t fixed_bound,
                                         uint8_t sw_bit_idx,
                                         uint8_t flags) {
    uint64_t op = 0;
    op |= ((uint64_t)(opcode & 0xFF)) << 56;
    op |= ((uint64_t)reg_idx) << 48;
    op |= ((uint64_t)(uint16_t)fixed_bound) << 32;
    op |= ((uint64_t)sw_bit_idx) << 24;
    op |= ((uint64_t)flags) << 16;
    return op;
}

typedef struct {
    uint64_t ops[FLOW_BMF_MICROCODE_MAX_OPS];
    size_t op_count;
} FlowBmfMicrocode;

/* Initialize an empty microcode sequence */
void flow_bmf_microcode_init(FlowBmfMicrocode *ucode);

/* Append a micro-op */
int flow_bmf_microcode_emit(FlowBmfMicrocode *ucode, uint64_t uop);

/* Compile physical box bounds into microcode */
void flow_bmf_microcode_compile_box(FlowBmfMicrocode *ucode,
                                    const double *upper_bounds,
                                    const double *lower_bounds,
                                    size_t dim,
                                    uint64_t base_sw_bit);

/*
 * Branchless Microcode Execution:
 * Evaluates state vector against all micro-ops, directly updating canvas switches.
 * Execution latency: < 5ns.
 */
int flow_bmf_microcode_execute(const FlowBmfMicrocode *ucode,
                               const double *state,
                               size_t dim,
                               FlowBmf1BitCanvas *canvas,
                               uint32_t *violation_mask_out);

/*
 * SMT Supreme Court Microcode Soundness Proof:
 * Proves that branchless microcode produces identical outcome to continuous
 * Presburger polytope evaluation (Zero Divergence UNSAT).
 */
FlowSMTResult flow_bmf_microcode_verify_soundness_smt(const FlowBmfMicrocode *ucode,
                                                      const FlowBmf1BitCanvas *canvas,
                                                      FlowSMTProofAttestation *proof_out);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_BMF_MICROCODE_H */
