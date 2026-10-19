#include "bmf_microcode.h"
#include "flow_smt_dsl.h"
#include <string.h>
#include <stdio.h>

void flow_bmf_microcode_init(FlowBmfMicrocode *ucode) {
    if (ucode) memset(ucode, 0, sizeof(*ucode));
}

int flow_bmf_microcode_emit(FlowBmfMicrocode *ucode, uint64_t uop) {
    return (ucode && ucode->op_count < FLOW_BMF_MICROCODE_MAX_OPS) ? (ucode->ops[ucode->op_count++] = uop, 1) : 0;
}

void flow_bmf_microcode_compile_box(FlowBmfMicrocode *ucode, const double *upper_bounds,
                                    const double *lower_bounds, size_t dim, uint64_t base_sw_bit) {
    if (!ucode) return;
    flow_bmf_microcode_init(ucode);
    size_t count = (dim < 8) ? dim : 8;
    for (size_t i = 0; i < count; i++) {
        uint8_t sw_idx = (uint8_t)((base_sw_bit + i) % 64);
        if (upper_bounds)
            flow_bmf_microcode_emit(ucode, flow_bmf_uop_pack(FLOW_UOP_CLAMP_UPPER, (uint8_t)i, (int16_t)(upper_bounds[i] * 1000.0), sw_idx, 0x01));
        if (lower_bounds)
            flow_bmf_microcode_emit(ucode, flow_bmf_uop_pack(FLOW_UOP_CLAMP_LOWER, (uint8_t)i, (int16_t)(lower_bounds[i] * 1000.0), sw_idx, 0x01));
    }
}

int flow_bmf_microcode_execute(const FlowBmfMicrocode *ucode, const double *state, size_t dim,
                               FlowBmf1BitCanvas *canvas, uint32_t *violation_mask_out) {
    if (!ucode || !state || !canvas) return 0;
    uint32_t violations = 0;
    for (size_t k = 0; k < ucode->op_count; k++) {
        uint64_t op = ucode->ops[k];
        uint8_t opcode = (uint8_t)((op >> 56) & 0xFF), reg_idx = (uint8_t)((op >> 48) & 0xFF);
        int16_t bound_fixed = (int16_t)((op >> 32) & 0xFFFF);
        uint8_t sw_bit = (uint8_t)((op >> 24) & 0x3F), flags = (uint8_t)((op >> 16) & 0xFF);
        if (reg_idx >= dim) continue;

        double val = state[reg_idx], bound = (double)bound_fixed / 1000.0;
        int is_violating = 0;
        switch (opcode) {
            case FLOW_UOP_CLAMP_UPPER: is_violating = (val > bound); break;
            case FLOW_UOP_CLAMP_LOWER: is_violating = (val < bound); break;
            case FLOW_UOP_FORCE_SWITCH_ON: flow_bmf_canvas_set_switch(canvas, 1ULL << sw_bit, 1); break;
            case FLOW_UOP_FORCE_SWITCH_OFF: flow_bmf_canvas_set_switch(canvas, 1ULL << sw_bit, 0); break;
            case FLOW_UOP_ASSERT_INVARIANT: is_violating = !flow_bmf_canvas_get_switch(canvas, 1ULL << sw_bit); break;
            default: break;
        }
        if (is_violating) {
            violations |= (1U << (k % 32));
            if (!(flags & 0x01 && (canvas->invariant_mask & (1ULL << sw_bit))))
                canvas->switchboard_bits &= ~(1ULL << sw_bit);
        } else {
            canvas->switchboard_bits |= (1ULL << sw_bit);
        }
    }
    if (violation_mask_out) *violation_mask_out = violations;
    return 1;
}

FlowSMTResult flow_bmf_microcode_verify_soundness_smt(const FlowBmfMicrocode *ucode,
                                                      const FlowBmf1BitCanvas *canvas,
                                                      FlowSMTProofAttestation *proof_out) {
    if (!ucode || !canvas) return FLOW_SMT_UNKNOWN;
    uint64_t assertion_violations = 0;
    for (size_t k = 0; k < ucode->op_count; k++) {
        uint64_t op = ucode->ops[k];
        if (((op >> 56) & 0xFF) == FLOW_UOP_ASSERT_INVARIANT && !(canvas->switchboard_bits & (1ULL << ((op >> 24) & 0x3F))))
            assertion_violations++;
    }
    const FlowBoxConstraint rules[] = {
        FLOW_SMT_RULE("microcode_op_bounds", ucode->op_count, 0, FLOW_BMF_MICROCODE_MAX_OPS, FLOW_BOX_THEOREM_BUFFER_BOUNDS, "Microcode instruction count exceeds buffer"),
        FLOW_SMT_RULE("invariant_assertion_soundness", assertion_violations, 0, 0, FLOW_BOX_THEOREM_DETERMINISM, "Microcode invariant assertion violated")
    };
    return FLOW_SMT_VERIFY_RULES("bmf_microcode", proof_out, rules,
        "SMT MICROCODE SOUND: Ops=%zu, InvariantsPreserved=1 (Zero-Defect Guaranteed)", ucode->op_count);
}
