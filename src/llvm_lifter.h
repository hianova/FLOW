#ifndef FLOW_LLVM_LIFTER_H
#define FLOW_LLVM_LIFTER_H

#define FLOW_LLVM_LIFTER_DEFINED

#include "flow.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * LLVM SSA Opcodes supported by FLOW Reverse Boundary Lifter
 * Dual naming compatibility: FLOW_LLVM_OP_* and LLVM_OP_*
 */
typedef enum {
    FLOW_LLVM_OP_ADD = 0,
    FLOW_LLVM_OP_SUB,
    FLOW_LLVM_OP_MUL,
    FLOW_LLVM_OP_UDIV,
    FLOW_LLVM_OP_SDIV,
    FLOW_LLVM_OP_UREM,
    FLOW_LLVM_OP_SREM,
    FLOW_LLVM_OP_SHL,
    FLOW_LLVM_OP_LSHR,
    FLOW_LLVM_OP_ASHR,
    FLOW_LLVM_OP_AND,
    FLOW_LLVM_OP_OR,
    FLOW_LLVM_OP_XOR,
    FLOW_LLVM_OP_ICMP,
    FLOW_LLVM_OP_SELECT,
    FLOW_LLVM_OP_BR,
    FLOW_LLVM_OP_PHI,
    FLOW_LLVM_OP_TRUNC,
    FLOW_LLVM_OP_ZEXT,
    FLOW_LLVM_OP_SEXT,
    FLOW_LLVM_OP_CALL,
    FLOW_LLVM_OP_RET,

    /* Aliases for todo.md / prompt compatibility */
    LLVM_OP_ADD = FLOW_LLVM_OP_ADD,
    LLVM_OP_SUB = FLOW_LLVM_OP_SUB,
    LLVM_OP_MUL = FLOW_LLVM_OP_MUL,
    LLVM_OP_UDIV = FLOW_LLVM_OP_UDIV,
    LLVM_OP_SDIV = FLOW_LLVM_OP_SDIV,
    LLVM_OP_UREM = FLOW_LLVM_OP_UREM,
    LLVM_OP_SREM = FLOW_LLVM_OP_SREM,
    LLVM_OP_SHL = FLOW_LLVM_OP_SHL,
    LLVM_OP_LSHR = FLOW_LLVM_OP_LSHR,
    LLVM_OP_ASHR = FLOW_LLVM_OP_ASHR,
    LLVM_OP_AND = FLOW_LLVM_OP_AND,
    LLVM_OP_OR = FLOW_LLVM_OP_OR,
    LLVM_OP_XOR = FLOW_LLVM_OP_XOR,
    LLVM_OP_ICMP = FLOW_LLVM_OP_ICMP,
    LLVM_OP_SELECT = FLOW_LLVM_OP_SELECT,
    LLVM_OP_BR = FLOW_LLVM_OP_BR,
    LLVM_OP_PHI = FLOW_LLVM_OP_PHI,
    LLVM_OP_TRUNC = FLOW_LLVM_OP_TRUNC,
    LLVM_OP_ZEXT = FLOW_LLVM_OP_ZEXT,
    LLVM_OP_SEXT = FLOW_LLVM_OP_SEXT,
    LLVM_OP_CALL = FLOW_LLVM_OP_CALL,
    LLVM_OP_RET = FLOW_LLVM_OP_RET
} FlowLLVMOpcode;

/*
 * Single Lifted LLVM SSA Instruction
 */
typedef struct {
    char ssa_name[64];
    FlowLLVMOpcode op;
    uint32_t bit_width;
    int has_nsw;
    int has_nuw;
    char icmp_pred[16];
    char arg0[64];
    char arg1[64];
    char arg2[64];
    int64_t inferred_min;
    int64_t inferred_max;
    uint64_t known_zero_mask;
    uint64_t known_one_mask;
} FlowLLVMInst;

/*
 * Lifted LLVM Function Definition with SSA Stream & Inferred Bounds
 */
typedef struct {
    char fn_name[128];
    uint32_t ret_bits;
    int ret_is_signed;
    size_t param_count;
    char param_names[16][64];
    uint32_t param_bits[16];
    size_t inst_count;
    FlowLLVMInst insts[256];
    char ret_ssa[64];
    int overflow_free_proven;
} FlowLLVMLiftedFunction;

/*
 * API Prototypes
 */
int flow_llvm_lift_ir_text(const char *llvm_ir_text, FlowLLVMLiftedFunction *fns_out,
                           size_t max_fns, size_t *fn_count_out);

int flow_llvm_lift_c_source(const char *c_path, FlowLLVMLiftedFunction *fns_out, size_t max_fns,
                            size_t *fn_count_out);

int flow_llvm_infer_bounds(FlowLLVMLiftedFunction *fn, const SemanticIR *constraint_ir);

int flow_llvm_emit_lean_boundary_proof(FILE *out, const SemanticIR *constraint_ir,
                                       const FlowLLVMLiftedFunction *lifted_fns,
                                       size_t lifted_count);

const char *flow_llvm_opcode_name(FlowLLVMOpcode op);
FlowLLVMOpcode flow_llvm_parse_opcode(const char *str);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_LLVM_LIFTER_H */
