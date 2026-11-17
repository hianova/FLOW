#ifndef FLOW_IR_H
#define FLOW_IR_H

#include "flow.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

#define FLOW_IR_MAGIC        UINT32_C(0x31524946) /* "FIR1" */
#define FLOW_IR_VERSION      UINT32_C(1)
#define FLOW_IR_HEADER_BYTES 64U
#define FLOW_IR_VAL_WIRE_B   32U
#define FLOW_IR_OP_WIRE_B    72U
#define FLOW_IR_MAX_VALS     128U
#define FLOW_IR_MAX_OPS      128U
#define FLOW_IR_MAX_IO       32U
#define FLOW_IR_MAX_INS      6U
#define FLOW_IR_MAX_LUT_U64  4096U
#define FLOW_IR_FLAG_CLEAN   UINT32_C(1)

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
    return (uint16_t)(((uint16_t)p[0]) | ((uint16_t)p[1] << 8U));
}

static inline uint32_t flow_ir__read_le32(const uint8_t *p) {
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}

static inline uint64_t flow_ir__read_le64(const uint8_t *p) {
    return ((uint64_t)p[0]) | ((uint64_t)p[1] << 8U) | ((uint64_t)p[2] << 16U) |
           ((uint64_t)p[3] << 24U) | ((uint64_t)p[4] << 32U) | ((uint64_t)p[5] << 40U) |
           ((uint64_t)p[6] << 48U) | ((uint64_t)p[7] << 56U);
}

static inline uint64_t flow_ir__fnv1a64(const uint8_t *buf, size_t len) {
    uint64_t h = UINT64_C(0xCBF29CE484222325);
    for (size_t i = 0U; i < len; ++i) {
        h ^= (uint64_t)buf[i];
        h *= UINT64_C(0x00000100000001B3);
    }
    return h;
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

static inline bool flow_ir_load_bytes(flow_ir_prog_t *out, const uint8_t *buf, size_t len) {
    if (!out || !buf || len < FLOW_IR_HEADER_BYTES + 8U) {
        return false;
    }
    memset(out, 0, sizeof(*out));

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

    size_t vals_b = (size_t)out->num_vals * FLOW_IR_VAL_WIRE_B;
    size_t in_b = ((size_t)out->num_inputs + 1U) / 2U * 8U;
    size_t out_b = ((size_t)out->num_outputs + 1U) / 2U * 8U;
    size_t ops_b = (size_t)out->num_ops * FLOW_IR_OP_WIRE_B;
    size_t lut_b = (size_t)out->num_lut_u64 * 8U;
    size_t body_len = FLOW_IR_HEADER_BYTES + vals_b + in_b + out_b + ops_b + lut_b;
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
    return true;
}

#define FLOW_IR_MAX_FILE_BYTES                                                                \
    (FLOW_IR_HEADER_BYTES + (FLOW_IR_MAX_VALS * FLOW_IR_VAL_WIRE_B) + (FLOW_IR_MAX_IO * 8U) + \
     (FLOW_IR_MAX_OPS * FLOW_IR_OP_WIRE_B) + (FLOW_IR_MAX_LUT_U64 * 8U) + 16U)

static inline bool flow_ir_load_file(flow_ir_prog_t *out, const char *path, uint8_t *scratch_buf,
                                     size_t scratch_cap) {
    if (!out || !path || !scratch_buf || scratch_cap < FLOW_IR_HEADER_BYTES + 8U) {
        return false;
    }
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return false;
    }
    size_t n = fread(scratch_buf, 1U, scratch_cap, fp);
    int extra = fgetc(fp);
    fclose(fp);
    if (extra != EOF) {
        return false;
    }
    return flow_ir_load_bytes(out, scratch_buf, n);
}

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
        uint16_t k = op->kind;
        uint64_t r = UINT64_C(0);

        if (k == FLOW_IR_OP_CONST_U64) {
            r = op->p0 & omask;
        } else if (k == FLOW_IR_OP_SLICE) {
            if (op->num_ins < 1U || op->p0 >= 64ULL) {
                return false;
            }
            r = (env[op->ins[0]] >> (uint32_t)op->p0) & omask;
        } else if (k == FLOW_IR_OP_CONCAT) {
            uint32_t pos = 0U;
            for (uint16_t j = 0U; j < op->num_ins; ++j) {
                uint32_t iv = op->ins[j];
                uint16_t iw = p->vals[iv].width;
                if (pos < 64U) {
                    r |= (env[iv] & flow_ir_mask_u64(iw)) << pos;
                }
                pos += iw;
            }
            r &= omask;
        } else if (k == FLOW_IR_OP_XOR_VEC) {
            if (op->num_ins < 2U) {
                return false;
            }
            r = (env[op->ins[0]] ^ env[op->ins[1]]) & omask;
        } else if (k == FLOW_IR_OP_AND_VEC) {
            if (op->num_ins < 2U) {
                return false;
            }
            r = (env[op->ins[0]] & env[op->ins[1]]) & omask;
        } else if (k == FLOW_IR_OP_NOT_VEC) {
            if (op->num_ins < 1U) {
                return false;
            }
            r = (~env[op->ins[0]]) & omask;
        } else if (k == FLOW_IR_OP_OR_REDUCE) {
            if (op->num_ins < 1U) {
                return false;
            }
            r = (env[op->ins[0]] != UINT64_C(0)) ? UINT64_C(1) : UINT64_C(0);
        } else if (k == FLOW_IR_OP_ADD) {
            if (op->num_ins < 2U) {
                return false;
            }
            r = (env[op->ins[0]] + env[op->ins[1]]) & omask;
        } else if (k == FLOW_IR_OP_SUB) {
            if (op->num_ins < 2U) {
                return false;
            }
            r = (env[op->ins[0]] - env[op->ins[1]]) & omask;
        } else if (k == FLOW_IR_OP_MUL_FXP) {
            if (op->num_ins < 2U) {
                return false;
            }
            uint64_t sh = (op->p0 + op->p1 > op->p2) ? (op->p0 + op->p1 - op->p2) : UINT64_C(0);
            flow_u128 p12 = (flow_u128)env[op->ins[0]] * (flow_u128)env[op->ins[1]];
            r = ((sh < 128ULL) ? (uint64_t)(p12 >> (uint32_t)sh) : UINT64_C(0)) & omask;
        } else if (k == FLOW_IR_OP_SQR_FXP) {
            if (op->num_ins < 1U) {
                return false;
            }
            uint64_t sh = (2ULL * op->p0 > op->p2) ? (2ULL * op->p0 - op->p2) : UINT64_C(0);
            flow_u128 p12 = (flow_u128)env[op->ins[0]] * (flow_u128)env[op->ins[0]];
            r = ((sh < 128ULL) ? (uint64_t)(p12 >> (uint32_t)sh) : UINT64_C(0)) & omask;
        } else if (k == FLOW_IR_OP_GF2M_MUL) {
            if (op->num_ins < 2U) {
                return false;
            }
            r = flow_ir__gf2m_mul(env[op->ins[0]], env[op->ins[1]], (uint32_t)op->p0, op->p1) &
                omask;
        } else if (k == FLOW_IR_OP_GF2M_INV) {
            if (op->num_ins < 1U) {
                return false;
            }
            r = flow_ir__gf2m_inv(env[op->ins[0]], (uint32_t)op->p0, op->p1) & omask;
        } else if (k == FLOW_IR_OP_LUT_ROM) {
            if (op->num_ins < 1U) {
                return false;
            }
            uint64_t addr = env[op->ins[0]];
            if (addr < (uint64_t)op->lut_len) {
                r = p->lut_pool[op->lut_off + (uint32_t)addr] & omask;
            }
        } else if (k == FLOW_IR_OP_CTRL_SEL) {
            if (op->num_ins < 3U) {
                return false;
            }
            r = (((env[op->ins[0]] & UINT64_C(1)) != UINT64_C(0)) ? env[op->ins[2]]
                                                                  : env[op->ins[1]]) &
                omask;
        } else if (k == FLOW_IR_OP_SHL_VAR) {
            if (op->num_ins < 2U) {
                return false;
            }
            uint64_t sh = env[op->ins[1]];
            r = (sh < 64ULL) ? ((env[op->ins[0]] << (uint32_t)sh) & omask) : UINT64_C(0);
        } else if (k == FLOW_IR_OP_SHR_VAR) {
            if (op->num_ins < 2U) {
                return false;
            }
            uint64_t sh = env[op->ins[1]];
            r = (sh < 64ULL) ? ((env[op->ins[0]] >> (uint32_t)sh) & omask) : UINT64_C(0);
        } else if (k == FLOW_IR_OP_EQ_ZERO) {
            if (op->num_ins < 1U) {
                return false;
            }
            r = (env[op->ins[0]] == UINT64_C(0)) ? UINT64_C(1) : UINT64_C(0);
        } else {
            return false;
        }
        env[oid] = r;
    }

    for (uint32_t i = 0U; i < p->num_outputs; ++i) {
        out_vals[i] = env[p->outputs[i]];
    }
    return true;
}

#ifdef __cplusplus
}
#endif

#endif /* FLOW_IR_H */
