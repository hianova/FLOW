#ifndef FLOW_IR_WIRE_H
#define FLOW_IR_WIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * FLOW_IR_WIRE — Freestanding Canonical `"FIR1"` Binary Wire Codec & Enums
 * ============================================================================
 * Zero heap, zero floats, zero host stdio, zero `#include "flow.h"`,
 * zero 86KB `flow_ir_prog_t` workspace, zero jump tables, C17 & C++20 clean,
 * `-ffreestanding -nostdlib -fno-builtin -mgeneral-regs-only` clean.
 * ============================================================================ */

#define FLOW_IR_MAGIC                  UINT32_C(0x31524946) /* "FIR1" */
#define FLOW_IR_VERSION                UINT32_C(1)
#define FLOW_IR_HEADER_BYTES           64U
#define FLOW_IR_VAL_WIRE_B             32U
#define FLOW_IR_OP_WIRE_B              72U
#define FLOW_IR_MAX_VALS               512U
#define FLOW_IR_MAX_OPS                512U
#define FLOW_IR_MAX_IO                 32U
#define FLOW_IR_MAX_INS                6U
#define FLOW_IR_MAX_LUT_U64            4096U
#define FLOW_IR_FLAG_CLEAN             UINT32_C(1)

#define FLOW_IR_WIRE_PAD8_U32_BYTES(n) ((((size_t)(n) + 1U) >> 1U) * 8U)
#define FLOW_IR_WIRE_BODY_BYTES(nv, ni, no, nop, nlut)                            \
    ((size_t)FLOW_IR_HEADER_BYTES + ((size_t)(nv) * (size_t)FLOW_IR_VAL_WIRE_B) + \
     FLOW_IR_WIRE_PAD8_U32_BYTES(ni) + FLOW_IR_WIRE_PAD8_U32_BYTES(no) +          \
     ((size_t)(nop) * (size_t)FLOW_IR_OP_WIRE_B) + ((size_t)(nlut) * 8U))
#define FLOW_IR_WIRE_BYTES(nv, ni, no, nop, nlut) \
    (FLOW_IR_WIRE_BODY_BYTES(nv, ni, no, nop, nlut) + 8U)

typedef enum {
    FLOW_IR_MULT_LINEAR = 1U,
    FLOW_IR_MULT_CARTESIAN = 2U
} flow_ir_mult_t;

typedef enum {
    FLOW_IR_OP_CONST_U64 = 1U,
    FLOW_IR_OP_SLICE = 2U,
    FLOW_IR_OP_CONCAT = 3U,
    FLOW_IR_OP_XOR_VEC = 4U,
    FLOW_IR_OP_AND_VEC = 5U,
    FLOW_IR_OP_NOT_VEC = 6U,
    FLOW_IR_OP_OR_REDUCE = 7U,
    FLOW_IR_OP_ADD = 8U,
    FLOW_IR_OP_SUB = 9U,
    FLOW_IR_OP_MUL_FXP = 10U,
    FLOW_IR_OP_SQR_FXP = 11U,
    FLOW_IR_OP_GF2M_MUL = 12U,
    FLOW_IR_OP_GF2M_INV = 13U,
    FLOW_IR_OP_LUT_ROM = 14U,
    FLOW_IR_OP_CTRL_SEL = 15U,
    FLOW_IR_OP_SHL_VAR = 16U,
    FLOW_IR_OP_SHR_VAR = 17U,
    FLOW_IR_OP_EQ_ZERO = 18U,
    FLOW_IR_OP_STAGE_PIPE = 19U
} flow_ir_op_kind_t;

#define FLOW_IR_OP_CONE_STAGE FLOW_IR_OP_STAGE_PIPE

static inline void flow_ir_wire_zero(void *dst, size_t n) {
    volatile uint8_t *p = (volatile uint8_t *)dst;
    for (size_t i = 0U; i < n; ++i) {
        p[i] = UINT8_C(0);
    }
}

static inline uint16_t flow_ir_wire_read_le16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0]) | ((uint16_t)p[1] << 8U));
}

static inline uint32_t flow_ir_wire_read_le32(const uint8_t *p) {
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}

static inline uint64_t flow_ir_wire_read_le64(const uint8_t *p) {
    return ((uint64_t)p[0]) | ((uint64_t)p[1] << 8U) | ((uint64_t)p[2] << 16U) |
           ((uint64_t)p[3] << 24U) | ((uint64_t)p[4] << 32U) | ((uint64_t)p[5] << 40U) |
           ((uint64_t)p[6] << 48U) | ((uint64_t)p[7] << 56U);
}

static inline void flow_ir_wire_write_le16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8U) & 0xFFU);
}

static inline void flow_ir_wire_write_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8U) & 0xFFU);
    p[2] = (uint8_t)((v >> 16U) & 0xFFU);
    p[3] = (uint8_t)((v >> 24U) & 0xFFU);
}

static inline void flow_ir_wire_write_le64(uint8_t *p, uint64_t v) {
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8U) & 0xFFU);
    p[2] = (uint8_t)((v >> 16U) & 0xFFU);
    p[3] = (uint8_t)((v >> 24U) & 0xFFU);
    p[4] = (uint8_t)((v >> 32U) & 0xFFU);
    p[5] = (uint8_t)((v >> 40U) & 0xFFU);
    p[6] = (uint8_t)((v >> 48U) & 0xFFU);
    p[7] = (uint8_t)((v >> 56U) & 0xFFU);
}

static inline void flow_ir_wire_write_name24(uint8_t dst[24], const char *src) {
    flow_ir_wire_zero(dst, 24U);
    if (!src) {
        return;
    }
    for (size_t i = 0U; i < 23U && src[i] != '\0'; ++i) {
        dst[i] = (uint8_t)src[i];
    }
}

static inline uint64_t flow_ir_wire_fnv1a64(const uint8_t *buf, size_t len) {
    uint64_t h = UINT64_C(0xCBF29CE484222325);
    for (size_t i = 0U; i < len; ++i) {
        h ^= (uint64_t)buf[i];
        h *= UINT64_C(0x00000100000001B3);
    }
    return h;
}

static inline void flow_ir_wire_write_header(uint8_t dst[FLOW_IR_HEADER_BYTES], uint32_t mult,
                                             uint32_t flags, uint32_t num_vals, uint32_t num_inputs,
                                             uint32_t num_outputs, uint32_t num_ops,
                                             uint32_t num_lut_u64, const char *name) {
    flow_ir_wire_write_le32(dst + 0U, FLOW_IR_MAGIC);
    flow_ir_wire_write_le32(dst + 4U, FLOW_IR_VERSION);
    flow_ir_wire_write_le32(dst + 8U, mult);
    flow_ir_wire_write_le32(dst + 12U, flags);
    flow_ir_wire_write_le32(dst + 16U, num_vals);
    flow_ir_wire_write_le32(dst + 20U, num_inputs);
    flow_ir_wire_write_le32(dst + 24U, num_outputs);
    flow_ir_wire_write_le32(dst + 28U, num_ops);
    flow_ir_wire_write_le32(dst + 32U, num_lut_u64);
    flow_ir_wire_write_le32(dst + 36U, 0U);
    flow_ir_wire_write_name24(dst + 40U, name);
}

static inline void flow_ir_wire_write_val(uint8_t dst[FLOW_IR_VAL_WIRE_B], uint16_t width,
                                          uint8_t is_classical, const char *name) {
    flow_ir_wire_zero(dst, FLOW_IR_VAL_WIRE_B);
    flow_ir_wire_write_le16(dst + 0U, width);
    dst[2] = is_classical;
    flow_ir_wire_write_name24(dst + 8U, name);
}

static inline void flow_ir_wire_write_op(uint8_t dst[FLOW_IR_OP_WIRE_B], uint16_t kind,
                                         uint16_t num_ins, uint32_t out_val, const uint32_t *ins,
                                         uint32_t lut_off, uint32_t lut_len, uint64_t p0,
                                         uint64_t p1, uint64_t p2, uint64_t p3) {
    flow_ir_wire_write_le16(dst + 0U, kind);
    flow_ir_wire_write_le16(dst + 2U, num_ins);
    flow_ir_wire_write_le32(dst + 4U, out_val);
    for (uint16_t k = 0U; k < FLOW_IR_MAX_INS; ++k) {
        uint32_t iv = (ins && k < num_ins) ? ins[k] : 0U;
        flow_ir_wire_write_le32(dst + 8U + (size_t)k * 4U, iv);
    }
    flow_ir_wire_write_le32(dst + 32U, lut_off);
    flow_ir_wire_write_le32(dst + 36U, lut_len);
    flow_ir_wire_write_le64(dst + 40U, p0);
    flow_ir_wire_write_le64(dst + 48U, p1);
    flow_ir_wire_write_le64(dst + 56U, p2);
    flow_ir_wire_write_le64(dst + 64U, p3);
}

static inline size_t flow_ir_wire_seal_trailer(uint8_t *buf, size_t body_len) {
    uint64_t h = flow_ir_wire_fnv1a64(buf, body_len);
    flow_ir_wire_write_le64(buf + body_len, h);
    return body_len + 8U;
}

#ifdef __cplusplus
}
#endif

#endif /* FLOW_IR_WIRE_H */
