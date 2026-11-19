#ifndef FLOW_IR_H
#define FLOW_IR_H

#include "flow.h"
#include "flow_ir_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * FLOW_IR — Unified QTT Resource-Typed SSA Morphism Loader & Reference Core
 * ============================================================================
 * Zero heap, zero floats, zero indirect calls (`blr`), zero jump tables,
 * C17 & C++20 clean, `-mgeneral-regs-only` clean.
 *
 * Consumes binary `.fir` bundles emitted by `Flow.Morphism.emitFir` (`Flow.lean`):
 *   - Multiplicity 1 (`LINEAR`): reversible / non-duplicating -> scalar, lattice, unitary
 *   - Multiplicity 2 (`CARTESIAN`): dissipative fan-out / cone -> scalar, lattice only
 * ============================================================================ */

typedef struct {
    uint16_t width;
    uint8_t is_classical;
    char name[24];
} flow_ir_val_t;

typedef struct {
    uint16_t kind;
    uint16_t num_ins;
    uint32_t out_val;
    uint32_t ins[FLOW_IR_MAX_INS];
    uint32_t lut_off;
    uint32_t lut_len;
    uint64_t p0;
    uint64_t p1;
    uint64_t p2;
    uint64_t p3;
} flow_ir_op_t;

typedef struct {
    uint32_t mult;
    uint32_t flags;
    uint32_t num_vals;
    uint32_t num_inputs;
    uint32_t num_outputs;
    uint32_t num_ops;
    uint32_t num_lut_u64;
    char name[24];
    flow_ir_val_t vals[FLOW_IR_MAX_VALS];
    uint32_t inputs[FLOW_IR_MAX_IO];
    uint32_t outputs[FLOW_IR_MAX_IO];
    flow_ir_op_t ops[FLOW_IR_MAX_OPS];
    uint64_t lut_pool[FLOW_IR_MAX_LUT_U64];
    uint64_t fnv1a;
} flow_ir_prog_t;

static inline uint16_t flow_ir__read_le16(const uint8_t *p) {
    return flow_ir_wire_read_le16(p);
}

static inline uint32_t flow_ir__read_le32(const uint8_t *p) {
    return flow_ir_wire_read_le32(p);
}

static inline uint64_t flow_ir__read_le64(const uint8_t *p) {
    return flow_ir_wire_read_le64(p);
}

static inline uint64_t flow_ir__fnv1a64(const uint8_t *buf, size_t len) {
    return flow_ir_wire_fnv1a64(buf, len);
}

static inline void flow_ir__zero(void *dst, size_t n) {
    flow_ir_wire_zero(dst, n);
}

static inline uint64_t flow_ir_mask_u64(uint16_t w) {
    if (w == 0U) {
        return UINT64_C(0);
    }
    if (w >= 64U) {
        return UINT64_MAX;
    }
    return (UINT64_C(1) << w) - UINT64_C(1);
}

static inline bool flow_ir_op_is_linear_admissible(uint16_t kind) {
    return (kind >= FLOW_IR_OP_CONST_U64 && kind <= FLOW_IR_OP_EQ_ZERO);
}

static inline bool flow_ir_admits_unitary(const flow_ir_prog_t *p) {
    if (!p || p->mult != FLOW_IR_MULT_LINEAR) {
        return false;
    }
    for (uint32_t i = 0U; i < p->num_vals; ++i) {
        if (p->vals[i].width == 0U || p->vals[i].width > 256U) {
            return false;
        }
    }
    for (uint32_t i = 0U; i < p->num_ops; ++i) {
        if (!flow_ir_op_is_linear_admissible(p->ops[i].kind)) {
            return false;
        }
    }
    return true;
}

static inline void flow_ir__copy_name24(char dst[24], const uint8_t src[24]) {
    for (size_t i = 0U; i < 23U; ++i) {
        dst[i] = (char)src[i];
    }
    dst[23] = '\0';
}

static inline bool flow_ir_validate(const flow_ir_prog_t *p);

static inline bool flow_ir_load_bytes(flow_ir_prog_t *out, const uint8_t *buf, size_t len) {
    if (!out || !buf || len < FLOW_IR_HEADER_BYTES + 8U) {
        return false;
    }
    flow_ir__zero(out, sizeof(*out));

    if (flow_ir__read_le32(buf + 0U) != FLOW_IR_MAGIC) {
        return false;
    }
    if (flow_ir__read_le32(buf + 4U) != FLOW_IR_VERSION) {
        return false;
    }
    out->mult = flow_ir__read_le32(buf + 8U);
    if (out->mult != FLOW_IR_MULT_LINEAR && out->mult != FLOW_IR_MULT_CARTESIAN) {
        return false;
    }
    out->flags = flow_ir__read_le32(buf + 12U);
    out->num_vals = flow_ir__read_le32(buf + 16U);
    out->num_inputs = flow_ir__read_le32(buf + 20U);
    out->num_outputs = flow_ir__read_le32(buf + 24U);
    out->num_ops = flow_ir__read_le32(buf + 28U);
    out->num_lut_u64 = flow_ir__read_le32(buf + 32U);
    flow_ir__copy_name24(out->name, buf + 40U);

    if (out->num_vals == 0U || out->num_vals > FLOW_IR_MAX_VALS || out->num_inputs == 0U ||
        out->num_inputs > FLOW_IR_MAX_IO || out->num_outputs == 0U ||
        out->num_outputs > FLOW_IR_MAX_IO || out->num_ops > FLOW_IR_MAX_OPS ||
        out->num_lut_u64 > FLOW_IR_MAX_LUT_U64) {
        return false;
    }
    if (out->num_inputs + out->num_ops != out->num_vals) {
        return false;
    }

    size_t in_b = FLOW_IR_WIRE_PAD8_U32_BYTES(out->num_inputs);
    size_t out_b = FLOW_IR_WIRE_PAD8_U32_BYTES(out->num_outputs);
    size_t body_len = FLOW_IR_WIRE_BODY_BYTES(out->num_vals, out->num_inputs, out->num_outputs,
                                              out->num_ops, out->num_lut_u64);
    if (len != body_len + 8U) {
        return false;
    }

    uint64_t expected_hash = flow_ir__read_le64(buf + body_len);
    uint64_t actual_hash = flow_ir__fnv1a64(buf, body_len);
    if (expected_hash != actual_hash) {
        return false;
    }
    out->fnv1a = actual_hash;

    size_t off = FLOW_IR_HEADER_BYTES;
    for (uint32_t i = 0U; i < out->num_vals; ++i) {
        const uint8_t *vr = buf + off;
        uint16_t w = flow_ir__read_le16(vr + 0U);
        uint16_t max_w = (out->mult == FLOW_IR_MULT_LINEAR) ? 256U : 512U;
        if (w == 0U || w > max_w) {
            return false;
        }
        out->vals[i].width = w;
        out->vals[i].is_classical = vr[2];
        flow_ir__copy_name24(out->vals[i].name, vr + 8U);
        off += FLOW_IR_VAL_WIRE_B;
    }

    for (uint32_t i = 0U; i < out->num_inputs; ++i) {
        uint32_t vid = flow_ir__read_le32(buf + off + (size_t)i * 4U);
        if (vid != i) {
            return false;
        }
        out->inputs[i] = vid;
    }
    off += in_b;

    for (uint32_t i = 0U; i < out->num_outputs; ++i) {
        uint32_t vid = flow_ir__read_le32(buf + off + (size_t)i * 4U);
        if (vid >= out->num_vals) {
            return false;
        }
        out->outputs[i] = vid;
    }
    off += out_b;

    for (uint32_t i = 0U; i < out->num_ops; ++i) {
        const uint8_t *opr = buf + off;
        flow_ir_op_t *op = &out->ops[i];
        op->kind = flow_ir__read_le16(opr + 0U);
        op->num_ins = flow_ir__read_le16(opr + 2U);
        op->out_val = flow_ir__read_le32(opr + 4U);
        if (op->kind < FLOW_IR_OP_CONST_U64 || op->kind > FLOW_IR_OP_STAGE_PIPE) {
            return false;
        }
        if (out->mult == FLOW_IR_MULT_LINEAR && !flow_ir_op_is_linear_admissible(op->kind)) {
            return false;
        }
        if (op->num_ins > FLOW_IR_MAX_INS || op->out_val != out->num_inputs + i) {
            return false;
        }
        for (uint16_t k = 0U; k < FLOW_IR_MAX_INS; ++k) {
            uint32_t in_id = flow_ir__read_le32(opr + 8U + (size_t)k * 4U);
            op->ins[k] = in_id;
            if (k < op->num_ins && in_id >= op->out_val) {
                return false;
            }
        }
        op->lut_off = flow_ir__read_le32(opr + 32U);
        op->lut_len = flow_ir__read_le32(opr + 36U);
        if ((uint64_t)op->lut_off + (uint64_t)op->lut_len > (uint64_t)out->num_lut_u64) {
            return false;
        }
        op->p0 = flow_ir__read_le64(opr + 40U);
        op->p1 = flow_ir__read_le64(opr + 48U);
        op->p2 = flow_ir__read_le64(opr + 56U);
        op->p3 = flow_ir__read_le64(opr + 64U);
        off += FLOW_IR_OP_WIRE_B;
    }

    for (uint32_t i = 0U; i < out->num_lut_u64; ++i) {
        out->lut_pool[i] = flow_ir__read_le64(buf + off + (size_t)i * 8U);
    }
    return flow_ir_validate(out);
}

#define FLOW_IR_MAX_FILE_BYTES                                                                \
    (FLOW_IR_HEADER_BYTES + (FLOW_IR_MAX_VALS * FLOW_IR_VAL_WIRE_B) + (FLOW_IR_MAX_IO * 8U) + \
     (FLOW_IR_MAX_OPS * FLOW_IR_OP_WIRE_B) + (FLOW_IR_MAX_LUT_U64 * 8U) + 16U)

static inline uint32_t flow_ir__gf8_mul_0xb(uint32_t a, uint32_t b) {
    uint32_t res = 0U;
    a &= 7U;
    b &= 7U;
    for (uint32_t i = 0U; i < 3U; ++i) {
        if (((b >> i) & 1U) != 0U) {
            res ^= a;
        }
        a <<= 1U;
        if ((a & 8U) != 0U) {
            a ^= 0xBU;
        }
    }
    return res & 7U;
}

static inline uint64_t flow_ir__gf2m_mul(uint64_t a, uint64_t b, uint32_t deg, uint64_t irred) {
    if (deg == 0U || deg > 32U) {
        return 0ULL;
    }
    if (deg == 6U && irred == 0ULL) {
        uint32_t x0 = (uint32_t)(a & 7U);
        uint32_t x1 = (uint32_t)((a >> 3U) & 7U);
        uint32_t y0 = (uint32_t)(b & 7U);
        uint32_t y1 = (uint32_t)((b >> 3U) & 7U);
        uint32_t p0 = flow_ir__gf8_mul_0xb(x0, y0);
        uint32_t p1 = flow_ir__gf8_mul_0xb(x1, y1);
        uint32_t pm = flow_ir__gf8_mul_0xb(x0 ^ x1, y0 ^ y1);
        return (uint64_t)((p0 ^ p1) | ((pm ^ p0) << 3U));
    }
    uint64_t top = UINT64_C(1) << deg;
    uint64_t mask = top - UINT64_C(1);
    uint64_t res = UINT64_C(0);
    a &= mask;
    b &= mask;
    for (uint32_t i = 0U; i < deg; ++i) {
        if (((b >> i) & UINT64_C(1)) != UINT64_C(0)) {
            res ^= a;
        }
        a <<= 1U;
        if ((a & top) != UINT64_C(0)) {
            a ^= irred;
        }
    }
    return res & mask;
}

static inline uint64_t flow_ir__gf2m_inv(uint64_t a, uint32_t deg, uint64_t irred) {
    if (deg == 0U || deg > 32U) {
        return 0ULL;
    }
    uint64_t mask = (UINT64_C(1) << deg) - UINT64_C(1);
    a &= mask;
    if (a == UINT64_C(0)) {
        return UINT64_C(0);
    }
    uint64_t exp = mask - UINT64_C(1); /* 2^deg - 2 */
    uint64_t res = UINT64_C(1);
    uint64_t base = a;
    while (exp > UINT64_C(0)) {
        if ((exp & UINT64_C(1)) != UINT64_C(0)) {
            res = flow_ir__gf2m_mul(res, base, deg, irred);
        }
        base = flow_ir__gf2m_mul(base, base, deg, irred);
        exp >>= 1U;
    }
    return res & mask;
}

static inline bool flow_ir__eval_single_op(const flow_ir_prog_t *p, const flow_ir_op_t *op,
                                           uint64_t omask, const uint64_t args[FLOW_IR_MAX_INS],
                                           uint64_t *out_r) {
    uint16_t k = op->kind;
    uint64_t r = UINT64_C(0);

    if (k == FLOW_IR_OP_CONST_U64) {
        r = op->p0 & omask;
    } else if (k == FLOW_IR_OP_SLICE) {
        if (op->num_ins < 1U || op->p0 >= 64ULL) {
            return false;
        }
        r = (args[0] >> (uint32_t)op->p0) & omask;
    } else if (k == FLOW_IR_OP_CONCAT) {
        uint32_t pos = 0U;
        for (uint16_t j = 0U; j < op->num_ins; ++j) {
            uint32_t iv = op->ins[j];
            uint16_t iw = p->vals[iv].width;
            if (pos < 64U) {
                r |= (args[j] & flow_ir_mask_u64(iw)) << pos;
            }
            pos += iw;
        }
        r &= omask;
    } else if (k == FLOW_IR_OP_XOR_VEC) {
        if (op->num_ins < 2U) {
            return false;
        }
        r = (args[0] ^ args[1]) & omask;
    } else if (k == FLOW_IR_OP_AND_VEC) {
        if (op->num_ins < 2U) {
            return false;
        }
        r = (args[0] & args[1]) & omask;
    } else if (k == FLOW_IR_OP_NOT_VEC) {
        if (op->num_ins < 1U) {
            return false;
        }
        r = (~args[0]) & omask;
    } else if (k == FLOW_IR_OP_OR_REDUCE) {
        if (op->num_ins < 1U) {
            return false;
        }
        r = (args[0] != UINT64_C(0)) ? UINT64_C(1) : UINT64_C(0);
    } else if (k == FLOW_IR_OP_ADD) {
        if (op->num_ins < 2U) {
            return false;
        }
        r = (args[0] + args[1]) & omask;
    } else if (k == FLOW_IR_OP_SUB) {
        if (op->num_ins < 2U) {
            return false;
        }
        r = (args[0] - args[1]) & omask;
    } else if (k == FLOW_IR_OP_MUL_FXP) {
        if (op->num_ins < 2U) {
            return false;
        }
        uint64_t sh = (op->p0 + op->p1 > op->p2) ? (op->p0 + op->p1 - op->p2) : UINT64_C(0);
        flow_u128 p12 = (flow_u128)args[0] * (flow_u128)args[1];
        r = ((sh < 128ULL) ? (uint64_t)(p12 >> (uint32_t)sh) : UINT64_C(0)) & omask;
    } else if (k == FLOW_IR_OP_SQR_FXP) {
        if (op->num_ins < 1U) {
            return false;
        }
        uint64_t sh = (2ULL * op->p0 > op->p2) ? (2ULL * op->p0 - op->p2) : UINT64_C(0);
        flow_u128 p12 = (flow_u128)args[0] * (flow_u128)args[0];
        r = ((sh < 128ULL) ? (uint64_t)(p12 >> (uint32_t)sh) : UINT64_C(0)) & omask;
    } else if (k == FLOW_IR_OP_GF2M_MUL) {
        if (op->num_ins < 2U) {
            return false;
        }
        r = flow_ir__gf2m_mul(args[0], args[1], (uint32_t)op->p0, op->p1) & omask;
    } else if (k == FLOW_IR_OP_GF2M_INV) {
        if (op->num_ins < 1U) {
            return false;
        }
        r = flow_ir__gf2m_inv(args[0], (uint32_t)op->p0, op->p1) & omask;
    } else if (k == FLOW_IR_OP_LUT_ROM) {
        if (op->num_ins < 1U) {
            return false;
        }
        uint64_t addr = args[0];
        if (addr < (uint64_t)op->lut_len) {
            r = p->lut_pool[op->lut_off + (uint32_t)addr] & omask;
        }
    } else if (k == FLOW_IR_OP_CTRL_SEL) {
        if (op->num_ins < 3U) {
            return false;
        }
        r = (((args[0] & UINT64_C(1)) != UINT64_C(0)) ? args[2] : args[1]) & omask;
    } else if (k == FLOW_IR_OP_SHL_VAR) {
        if (op->num_ins < 2U) {
            return false;
        }
        uint64_t sh = args[1];
        r = (sh < 64ULL) ? ((args[0] << (uint32_t)sh) & omask) : UINT64_C(0);
    } else if (k == FLOW_IR_OP_SHR_VAR) {
        if (op->num_ins < 2U) {
            return false;
        }
        uint64_t sh = args[1];
        r = (sh < 64ULL) ? ((args[0] >> (uint32_t)sh) & omask) : UINT64_C(0);
    } else if (k == FLOW_IR_OP_EQ_ZERO) {
        if (op->num_ins < 1U) {
            return false;
        }
        r = (args[0] == UINT64_C(0)) ? UINT64_C(1) : UINT64_C(0);
    } else {
        return false;
    }
    *out_r = r;
    return true;
}

/* Evaluates any <= 64-bit FLOW_IR program purely in GPR registers (zero jump tables). */
static inline bool flow_ir_eval_u64(const flow_ir_prog_t *p, const uint64_t *in_vals,
                                    uint64_t *out_vals) {
    if (!p || !in_vals || !out_vals) {
        return false;
    }
    uint64_t env[FLOW_IR_MAX_VALS];
    for (uint32_t i = 0U; i < p->num_inputs; ++i) {
        if (p->vals[i].width > 64U) {
            return false;
        }
        env[i] = in_vals[i] & flow_ir_mask_u64(p->vals[i].width);
    }
    for (uint32_t i = 0U; i < p->num_ops; ++i) {
        const flow_ir_op_t *op = &p->ops[i];
        uint32_t oid = op->out_val;
        if (oid >= p->num_vals || p->vals[oid].width > 64U) {
            return false;
        }
        uint64_t omask = flow_ir_mask_u64(p->vals[oid].width);
        uint64_t args[FLOW_IR_MAX_INS];
        for (uint16_t j = 0U; j < op->num_ins && j < FLOW_IR_MAX_INS; ++j) {
            args[j] = env[op->ins[j]];
        }
        uint64_t r = UINT64_C(0);
        if (!flow_ir__eval_single_op(p, op, omask, args, &r)) {
            return false;
        }
        env[oid] = r;
    }

    for (uint32_t i = 0U; i < p->num_outputs; ++i) {
        out_vals[i] = env[p->outputs[i]];
    }
    return true;
}

static inline bool flow_ir_op_is_affine_gf2(uint16_t kind) {
    return (kind == FLOW_IR_OP_CONST_U64) || (kind == FLOW_IR_OP_SLICE) ||
           (kind == FLOW_IR_OP_CONCAT) || (kind == FLOW_IR_OP_XOR_VEC) ||
           (kind == FLOW_IR_OP_NOT_VEC);
}

static inline uint32_t flow_ir__ceil_log2_u32(uint32_t n) {
    uint32_t lg = 0U;
    uint32_t span = 1U;
    for (uint32_t i = 0U; i < 31U; ++i) {
        if (span < n) {
            span <<= 1U;
            lg += 1U;
        }
    }
    return lg;
}

typedef struct {
    uint32_t affine_ops;
    uint32_t non_affine_ops;
    uint32_t mult_bits;
    uint32_t mult_depth;
} flow_ir_dag_stats_t;

static inline bool flow_ir_analyze_dag(const flow_ir_prog_t *p, flow_ir_dag_stats_t *out) {
    if (!p || !out || p->num_vals == 0U || p->num_vals > FLOW_IR_MAX_VALS ||
        p->num_ops > FLOW_IR_MAX_OPS) {
        return false;
    }
    flow_ir__zero(out, sizeof(*out));
    uint32_t depth[FLOW_IR_MAX_VALS];
    flow_ir__zero(depth, sizeof(depth));

    for (uint32_t i = 0U; i < p->num_ops; ++i) {
        const flow_ir_op_t *op = &p->ops[i];
        uint32_t oid = op->out_val;
        if (oid >= p->num_vals) {
            return false;
        }
        uint16_t k = op->kind;
        uint32_t ow = (uint32_t)p->vals[oid].width;
        uint32_t iw0 = (op->num_ins > 0U && op->ins[0] < p->num_vals)
                           ? (uint32_t)p->vals[op->ins[0]].width
                           : 0U;

        uint32_t d_in = 0U;
        for (uint16_t j = 0U; j < op->num_ins && j < FLOW_IR_MAX_INS; ++j) {
            uint32_t iv = op->ins[j];
            if (iv < p->num_vals && depth[iv] > d_in) {
                d_in = depth[iv];
            }
        }

        uint32_t mb = 0U;
        uint32_t sd = 0U;
        if (flow_ir_op_is_affine_gf2(k)) {
            out->affine_ops += 1U;
        } else {
            out->non_affine_ops += 1U;
            if (k == FLOW_IR_OP_AND_VEC || k == FLOW_IR_OP_CTRL_SEL) {
                mb = ow;
                sd = 1U;
            } else if (k == FLOW_IR_OP_OR_REDUCE || k == FLOW_IR_OP_EQ_ZERO) {
                uint32_t eff_iw = (iw0 == 0U) ? 1U : iw0;
                uint32_t lg = flow_ir__ceil_log2_u32(eff_iw);
                mb = (iw0 <= 1U) ? 0U : (iw0 - 1U);
                sd = (lg == 0U) ? 1U : lg;
            } else if (k == FLOW_IR_OP_ADD || k == FLOW_IR_OP_SUB) {
                mb = (ow <= 1U) ? 0U : (ow - 1U);
                sd = (ow == 0U) ? 1U : ow;
            } else if (k == FLOW_IR_OP_GF2M_MUL) {
                uint32_t deg = (uint32_t)op->p0;
                mb = deg * deg;
                sd = 1U;
            } else if (k == FLOW_IR_OP_GF2M_INV) {
                uint32_t deg = (uint32_t)op->p0;
                uint32_t steps = (deg > 1U) ? (deg - 1U) : 1U;
                mb = steps * deg * deg;
                sd = steps;
            } else {
                uint32_t eff_iw = (iw0 == 0U) ? 1U : iw0;
                mb = ow * eff_iw;
                sd = (ow == 0U) ? 1U : ow;
            }
        }

        out->mult_bits += mb;
        uint32_t d_out = d_in + sd;
        depth[oid] = d_out;
        if (d_out > out->mult_depth) {
            out->mult_depth = d_out;
        }
    }
    return true;
}

static inline void flow_ir__write_le16(uint8_t *p, uint16_t v) {
    flow_ir_wire_write_le16(p, v);
}

static inline void flow_ir__write_le32(uint8_t *p, uint32_t v) {
    flow_ir_wire_write_le32(p, v);
}

static inline void flow_ir__write_le64(uint8_t *p, uint64_t v) {
    flow_ir_wire_write_le64(p, v);
}

static inline void flow_ir__write_name24(uint8_t dst[24], const char *src) {
    flow_ir_wire_write_name24(dst, src);
}

static inline bool flow_ir__op_arity_ok(const flow_ir_op_t *op) {
    uint16_t k = op->kind;
    if (k == FLOW_IR_OP_CONST_U64) {
        return op->num_ins == 0U;
    } else if (k == FLOW_IR_OP_SLICE || k == FLOW_IR_OP_NOT_VEC || k == FLOW_IR_OP_OR_REDUCE ||
               k == FLOW_IR_OP_SQR_FXP || k == FLOW_IR_OP_EQ_ZERO) {
        return op->num_ins == 1U;
    } else if (k == FLOW_IR_OP_CONCAT) {
        return op->num_ins >= 1U && op->num_ins <= FLOW_IR_MAX_INS;
    } else if (k == FLOW_IR_OP_XOR_VEC || k == FLOW_IR_OP_AND_VEC || k == FLOW_IR_OP_ADD ||
               k == FLOW_IR_OP_SUB || k == FLOW_IR_OP_MUL_FXP || k == FLOW_IR_OP_SHL_VAR ||
               k == FLOW_IR_OP_SHR_VAR) {
        return op->num_ins == 2U;
    } else if (k == FLOW_IR_OP_GF2M_MUL) {
        return op->num_ins == 2U && op->p0 >= UINT64_C(1) && op->p0 <= UINT64_C(32);
    } else if (k == FLOW_IR_OP_GF2M_INV) {
        return (op->num_ins == 1U || op->num_ins == 2U) && op->p0 >= UINT64_C(1) &&
               op->p0 <= UINT64_C(32);
    } else if (k == FLOW_IR_OP_LUT_ROM) {
        return op->num_ins == 1U && op->lut_len > 0U;
    } else if (k == FLOW_IR_OP_CTRL_SEL) {
        return op->num_ins == 3U;
    } else if (k == FLOW_IR_OP_STAGE_PIPE) {
        return op->num_ins <= FLOW_IR_MAX_INS;
    }
    return false;
}

static inline bool flow_ir_validate(const flow_ir_prog_t *p) {
    if (!p || p->name[0] == '\0') {
        return false;
    }
    if (p->mult != FLOW_IR_MULT_LINEAR && p->mult != FLOW_IR_MULT_CARTESIAN) {
        return false;
    }
    if (p->num_inputs == 0U || p->num_inputs > FLOW_IR_MAX_IO || p->num_outputs == 0U ||
        p->num_outputs > FLOW_IR_MAX_IO || p->num_vals == 0U || p->num_vals > FLOW_IR_MAX_VALS ||
        p->num_ops > FLOW_IR_MAX_OPS || p->num_lut_u64 > FLOW_IR_MAX_LUT_U64) {
        return false;
    }
    if (p->num_inputs + p->num_ops != p->num_vals) {
        return false;
    }
    uint16_t max_w = (p->mult == FLOW_IR_MULT_LINEAR) ? 256U : 512U;
    for (uint32_t i = 0U; i < p->num_vals; ++i) {
        if (p->vals[i].width == 0U || p->vals[i].width > max_w) {
            return false;
        }
    }
    for (uint32_t i = 0U; i < p->num_inputs; ++i) {
        if (p->inputs[i] != i) {
            return false;
        }
    }
    for (uint32_t i = 0U; i < p->num_outputs; ++i) {
        if (p->outputs[i] >= p->num_vals) {
            return false;
        }
    }
    uint32_t lut_acc = 0U;
    for (uint32_t i = 0U; i < p->num_ops; ++i) {
        const flow_ir_op_t *op = &p->ops[i];
        uint16_t k = op->kind;
        if (k < FLOW_IR_OP_CONST_U64 || k > FLOW_IR_OP_STAGE_PIPE) {
            return false;
        }
        if (p->mult == FLOW_IR_MULT_LINEAR && !flow_ir_op_is_linear_admissible(k)) {
            return false;
        }
        if (op->out_val != p->num_inputs + i || op->num_ins > FLOW_IR_MAX_INS) {
            return false;
        }
        if (!flow_ir__op_arity_ok(op)) {
            return false;
        }
        for (uint16_t j = 0U; j < op->num_ins; ++j) {
            if (op->ins[j] >= op->out_val) {
                return false;
            }
        }
        if ((uint64_t)op->lut_off + (uint64_t)op->lut_len > (uint64_t)p->num_lut_u64) {
            return false;
        }
        lut_acc += op->lut_len;
        if (lut_acc > FLOW_IR_MAX_LUT_U64) {
            return false;
        }
    }
    return true;
}

static inline bool flow_ir_encode_bytes(const flow_ir_prog_t *p, uint8_t *buf, size_t cap,
                                        size_t *out_len) {
    if (!flow_ir_validate(p) || !buf || !out_len) {
        return false;
    }
    uint32_t lut_total = 0U;
    for (uint32_t i = 0U; i < p->num_ops; ++i) {
        lut_total += p->ops[i].lut_len;
    }
    if (lut_total > FLOW_IR_MAX_LUT_U64) {
        return false;
    }

    size_t in_b = FLOW_IR_WIRE_PAD8_U32_BYTES(p->num_inputs);
    size_t out_b = FLOW_IR_WIRE_PAD8_U32_BYTES(p->num_outputs);
    size_t body_len =
        FLOW_IR_WIRE_BODY_BYTES(p->num_vals, p->num_inputs, p->num_outputs, p->num_ops, lut_total);
    if (body_len + 8U > cap) {
        return false;
    }

    flow_ir_wire_zero(buf, body_len + 8U);
    flow_ir_wire_write_header(buf, p->mult, p->flags, p->num_vals, p->num_inputs, p->num_outputs,
                              p->num_ops, lut_total, p->name);

    size_t off = FLOW_IR_HEADER_BYTES;
    for (uint32_t i = 0U; i < p->num_vals; ++i) {
        flow_ir_wire_write_val(buf + off, p->vals[i].width, p->vals[i].is_classical,
                               p->vals[i].name);
        off += FLOW_IR_VAL_WIRE_B;
    }
    for (uint32_t i = 0U; i < p->num_inputs; ++i) {
        flow_ir_wire_write_le32(buf + off + (size_t)i * 4U, p->inputs[i]);
    }
    off += in_b;
    for (uint32_t i = 0U; i < p->num_outputs; ++i) {
        flow_ir_wire_write_le32(buf + off + (size_t)i * 4U, p->outputs[i]);
    }
    off += out_b;

    uint32_t cur_lut_off = 0U;
    for (uint32_t i = 0U; i < p->num_ops; ++i) {
        const flow_ir_op_t *op = &p->ops[i];
        flow_ir_wire_write_op(buf + off, op->kind, op->num_ins, op->out_val, op->ins, cur_lut_off,
                              op->lut_len, op->p0, op->p1, op->p2, op->p3);
        cur_lut_off += op->lut_len;
        off += FLOW_IR_OP_WIRE_B;
    }
    for (uint32_t i = 0U; i < p->num_ops; ++i) {
        const flow_ir_op_t *op = &p->ops[i];
        for (uint32_t j = 0U; j < op->lut_len; ++j) {
            flow_ir_wire_write_le64(buf + off, p->lut_pool[op->lut_off + j]);
            off += 8U;
        }
    }
    *out_len = flow_ir_wire_seal_trailer(buf, body_len);
    return true;
}

#ifdef __cplusplus
}
#endif

#endif /* FLOW_IR_H */
