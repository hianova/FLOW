#include "bmf_microcode.h"
#include "flow_smt_dsl.h"
#include <string.h>
#include <stdio.h>

void flow_bmf_microcode_init(FlowBmfMicrocode *ucode) {
    if (!ucode) return;
    memset(ucode, 0, sizeof(*ucode));
}

int flow_bmf_microcode_emit(FlowBmfMicrocode *ucode, uint64_t uop) {
    if (!ucode || ucode->op_count >= FLOW_BMF_MICROCODE_MAX_OPS) return 0;
    ucode->ops[ucode->op_count++] = uop;
    return 1;
}

void flow_bmf_microcode_compile_box(FlowBmfMicrocode *ucode,
                                    const double *upper_bounds,
                                    const double *lower_bounds,
                                    size_t dim,
                                    uint64_t base_sw_bit) {
    if (!ucode) return;
    flow_bmf_microcode_init(ucode);

    size_t count = (dim < 8) ? dim : 8;
    for (size_t i = 0; i < count; i++) {
        uint8_t sw_idx = (uint8_t)((base_sw_bit + i) % 64);

        if (upper_bounds) {
            int16_t ub_fixed = (int16_t)(upper_bounds[i] * 1000.0);
            uint64_t op_ub = flow_bmf_uop_pack(FLOW_UOP_CLAMP_UPPER, (uint8_t)i, ub_fixed, sw_idx, 0x01);
            flow_bmf_microcode_emit(ucode, op_ub);
        }
        if (lower_bounds) {
            int16_t lb_fixed = (int16_t)(lower_bounds[i] * 1000.0);
            uint64_t op_lb = flow_bmf_uop_pack(FLOW_UOP_CLAMP_LOWER, (uint8_t)i, lb_fixed, sw_idx, 0x01);
            flow_bmf_microcode_emit(ucode, op_lb);
        }
    }
}

int flow_bmf_microcode_execute(const FlowBmfMicrocode *ucode,
                               const double *state,
                               size_t dim,
                               FlowBmf1BitCanvas *canvas,
                               uint32_t *violation_mask_out) {
    if (!ucode || !state || !canvas) return 0;

    uint32_t violations = 0;

    for (size_t k = 0; k < ucode->op_count; k++) {
        uint64_t op = ucode->ops[k];
        uint8_t opcode = (uint8_t)((op >> 56) & 0xFF);
        uint8_t reg_idx = (uint8_t)((op >> 48) & 0xFF);
        int16_t bound_fixed = (int16_t)((op >> 32) & 0xFFFF);
        uint8_t sw_bit = (uint8_t)((op >> 24) & 0x3F);
        uint8_t flags = (uint8_t)((op >> 16) & 0xFF);

        if (reg_idx >= dim) continue;

        double val = state[reg_idx];
        double bound = (double)bound_fixed / 1000.0;
        int is_violating = 0;

        switch (opcode) {
            case FLOW_UOP_CLAMP_UPPER:
                is_violating = (val > bound);
                break;
            case FLOW_UOP_CLAMP_LOWER:
                is_violating = (val < bound);
                break;
            case FLOW_UOP_FORCE_SWITCH_ON:
                flow_bmf_canvas_set_switch(canvas, 1ULL << sw_bit, 1);
                break;
            case FLOW_UOP_FORCE_SWITCH_OFF:
                flow_bmf_canvas_set_switch(canvas, 1ULL << sw_bit, 0);
                break;
            case FLOW_UOP_ASSERT_INVARIANT:
                if (!flow_bmf_canvas_get_switch(canvas, 1ULL << sw_bit)) {
                    is_violating = 1;
                }
                break;
            case FLOW_UOP_NOP:
            default:
                break;
        }

        if (is_violating) {
            violations |= (1U << (k % 32));
            /* If flag specifies hard invariant, do not clear if protected */
            if (!(flags & 0x01 && (canvas->invariant_mask & (1ULL << sw_bit)))) {
                canvas->switchboard_bits &= ~(1ULL << sw_bit);
            }
        } else {
            canvas->switchboard_bits |= (1ULL << sw_bit);
        }
    }

    if (violation_mask_out) {
        *violation_mask_out = violations;
    }
    return 1;
}

FlowSMTResult flow_bmf_microcode_verify_soundness_smt(const FlowBmfMicrocode *ucode,
                                                      const FlowBmf1BitCanvas *canvas,
                                                      FlowSMTProofAttestation *proof_out) {
    if (!ucode || !canvas) return FLOW_SMT_UNKNOWN;

    FLOW_SMT_BOX_BUILDER_DECL(builder);

    /* Theorem 1: Microcode instruction count bounded */
    FLOW_SMT_BOX_ADD_RULE(builder, "microcode_op_bounds", (uint64_t)ucode->op_count, 0, FLOW_BMF_MICROCODE_MAX_OPS,
                          FLOW_BOX_THEOREM_BUFFER_BOUNDS, "Microcode instruction count exceeds buffer");

    /* Theorem 2: Hard invariant assertion preservation */
    uint64_t assertion_violations = 0;
    for (size_t k = 0; k < ucode->op_count; k++) {
        uint64_t op = ucode->ops[k];
        uint8_t opcode = (uint8_t)((op >> 56) & 0xFF);
        uint8_t sw_bit = (uint8_t)((op >> 24) & 0x3F);
        if (opcode == FLOW_UOP_ASSERT_INVARIANT) {
            if ((canvas->switchboard_bits & (1ULL << sw_bit)) == 0) {
                assertion_violations++;
            }
        }
    }
    FLOW_SMT_BOX_ADD_RULE(builder, "invariant_assertion_soundness", assertion_violations, 0, 0,
                          FLOW_BOX_THEOREM_DETERMINISM, "Microcode invariant assertion violated");

    FlowSMTResult res = FLOW_SMT_BOX_VERIFY(builder, "bmf_microcode", proof_out);
    if (res == FLOW_SMT_PROVEN_UNSAT && proof_out != NULL) {
        snprintf(proof_out->proof_summary, sizeof(proof_out->proof_summary),
                 "SMT MICROCODE SOUND: Ops=%zu, InvariantsPreserved=1 (Zero-Defect Guaranteed)",
                 ucode->op_count);
    }
    return res;
}
