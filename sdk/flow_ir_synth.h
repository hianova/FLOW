#ifndef FLOW_IR_SYNTH_H
#define FLOW_IR_SYNTH_H

#include "flow_ir.h"
#include "searu_mc_gf2.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * FLOW_IR_SYNTH — SEARU x FLOW_IR Multiplicative-Complexity Pass (`.fir`)
 * ============================================================================
 * Standalone SDK optimization pass with explicit header dependencies.
 * ============================================================================ */

typedef struct {
    const char *override_name;
    uint32_t max_depth;
    uint32_t max_and;
    uint32_t budget_scale;
    uint64_t seed;
    bool op_wise_only;
} flow_ir_synth_cfg_t;

typedef struct {
    uint32_t in_ops;
    uint32_t in_mult_bits;
    uint32_t in_mult_depth;
    uint32_t out_ops;
    uint32_t out_mult_bits;
    uint32_t out_mult_depth;
    uint32_t sub_ops_synthesized;
    uint32_t cache_hits;
    uint64_t total_searu_iters;
    uint32_t verified_states;
    bool whole_graph_mode;
} flow_ir_synth_stats_t;

typedef struct {
    uint16_t kind;
    uint16_t w0;
    uint16_t w1;
    uint16_t w_out;
    uint64_t p0;
    uint64_t p1;
    uint64_t p2;
    uint64_t lut_hash;
    searu_mc_plan plan;
    bool valid;
} flow_ir__synth_memo_t;

typedef struct {
    searu_mc_ws mc_ws;
    flow_ir_prog_t tmp_prog;
    flow_ir__synth_memo_t memo[16];
    uint32_t val_map[FLOW_IR_MAX_VALS];
    uint8_t val_live[FLOW_IR_MAX_VALS];
} flow_ir_synth_ws_t;

static inline void flow_ir__fmt_hex_name(char dst[24], const char *pfx, uint32_t id) {
    static const char k_hex[17] = "0123456789abcdef";
    flow_ir__zero(dst, 24U);
    size_t pos = 0U;
    if (pfx) {
        while (pos < 16U && pfx[pos] != '\0') {
            dst[pos] = pfx[pos];
            ++pos;
        }
    }
    dst[pos++] = '_';
    dst[pos++] = k_hex[(id >> 8U) & 0xFU];
    dst[pos++] = k_hex[(id >> 4U) & 0xFU];
    dst[pos++] = k_hex[id & 0xFU];
    dst[pos] = '\0';
}

static inline bool flow_ir__emit_op(flow_ir_prog_t *out, uint16_t kind, uint16_t width,
                                    uint16_t num_ins, const uint32_t ins[FLOW_IR_MAX_INS],
                                    uint64_t p0, uint64_t p1, uint64_t p2, uint32_t lut_off,
                                    uint32_t lut_len, const char *name_or_null, uint32_t *out_vid) {
    if (out->num_ops >= FLOW_IR_MAX_OPS || out->num_vals >= FLOW_IR_MAX_VALS) {
        return false;
    }
    uint32_t oid = out->num_vals;
    flow_ir_val_t *v = &out->vals[oid];
    flow_ir__zero(v, sizeof(*v));
    v->width = width;
    v->is_classical = 0U;
    if (name_or_null && name_or_null[0] != '\0') {
        flow_ir__write_name24((uint8_t *)v->name, name_or_null);
    } else {
        flow_ir__fmt_hex_name(v->name, "s", oid);
    }

    flow_ir_op_t *op = &out->ops[out->num_ops];
    flow_ir__zero(op, sizeof(*op));
    op->kind = kind;
    op->num_ins = num_ins;
    op->out_val = oid;
    for (uint16_t j = 0U; j < num_ins && j < FLOW_IR_MAX_INS; ++j) {
        op->ins[j] = ins[j];
    }
    op->lut_off = lut_off;
    op->lut_len = lut_len;
    op->p0 = p0;
    op->p1 = p1;
    op->p2 = p2;

    out->num_vals += 1U;
    out->num_ops += 1U;
    *out_vid = oid;
    return true;
}

static inline bool flow_ir__emit_const1(flow_ir_prog_t *out, uint64_t bit, uint32_t *out_vid) {
    uint32_t dummy[FLOW_IR_MAX_INS] = {0};
    return flow_ir__emit_op(out, FLOW_IR_OP_CONST_U64, 1U, 0U, dummy, bit & UINT64_C(1), 0ULL, 0ULL,
                            0U, 0U, NULL, out_vid);
}

static inline bool flow_ir__emit_xor2(flow_ir_prog_t *out, uint16_t width, uint32_t a, uint32_t b,
                                      uint32_t *out_vid) {
    uint32_t ins[FLOW_IR_MAX_INS] = {a, b, 0, 0, 0, 0};
    return flow_ir__emit_op(out, FLOW_IR_OP_XOR_VEC, width, 2U, ins, 0ULL, 0ULL, 0ULL, 0U, 0U, NULL,
                            out_vid);
}

static inline bool flow_ir__emit_and2(flow_ir_prog_t *out, uint16_t width, uint32_t a, uint32_t b,
                                      uint32_t *out_vid) {
    uint32_t ins[FLOW_IR_MAX_INS] = {a, b, 0, 0, 0, 0};
    return flow_ir__emit_op(out, FLOW_IR_OP_AND_VEC, width, 2U, ins, 0ULL, 0ULL, 0ULL, 0U, 0U, NULL,
                            out_vid);
}

static inline bool flow_ir__emit_not1(flow_ir_prog_t *out, uint32_t a, uint32_t *out_vid) {
    uint32_t ins[FLOW_IR_MAX_INS] = {a, 0, 0, 0, 0, 0};
    return flow_ir__emit_op(out, FLOW_IR_OP_NOT_VEC, 1U, 1U, ins, 0ULL, 0ULL, 0ULL, 0U, 0U, NULL,
                            out_vid);
}

static inline bool flow_ir__pack_chunks6(flow_ir_prog_t *out, const uint32_t vids[],
                                         const uint16_t widths[], uint32_t count,
                                         const char *final_name, uint32_t *out_vid) {
    if (count == 0U || count > 32U) {
        return false;
    }
    if (count == 1U) {
        if (final_name && final_name[0] != '\0' && vids[0] >= out->num_inputs) {
            flow_ir__write_name24((uint8_t *)out->vals[vids[0]].name, final_name);
        }
        *out_vid = vids[0];
        return true;
    }
    uint32_t cur_v[32];
    uint16_t cur_w[32];
    uint32_t cur_n = count;
    for (uint32_t i = 0U; i < count; ++i) {
        cur_v[i] = vids[i];
        cur_w[i] = widths[i];
    }
    while (cur_n > 1U) {
        uint32_t nxt_v[32];
        uint16_t nxt_w[32];
        uint32_t nxt_n = 0U;
        uint32_t idx = 0U;
        while (idx < cur_n) {
            uint32_t rem = cur_n - idx;
            uint16_t take = (rem >= 6U) ? 6U : (uint16_t)rem;
            if (take == 1U) {
                nxt_v[nxt_n] = cur_v[idx];
                nxt_w[nxt_n] = cur_w[idx];
                ++nxt_n;
                idx += 1U;
            } else {
                uint32_t cins[FLOW_IR_MAX_INS] = {0};
                uint16_t wsum = 0U;
                for (uint16_t j = 0U; j < take; ++j) {
                    cins[j] = cur_v[idx + j];
                    wsum = (uint16_t)(wsum + cur_w[idx + j]);
                }
                bool is_last = (cur_n <= 6U);
                const char *nm = is_last ? final_name : NULL;
                uint32_t cvid = 0U;
                if (!flow_ir__emit_op(out, FLOW_IR_OP_CONCAT, wsum, take, cins, 0ULL, 0ULL, 0ULL,
                                      0U, 0U, nm, &cvid)) {
                    return false;
                }
                nxt_v[nxt_n] = cvid;
                nxt_w[nxt_n] = wsum;
                ++nxt_n;
                idx += take;
            }
        }
        cur_n = nxt_n;
        for (uint32_t i = 0U; i < cur_n; ++i) {
            cur_v[i] = nxt_v[i];
            cur_w[i] = nxt_w[i];
        }
    }
    *out_vid = cur_v[0];
    return true;
}

static inline bool flow_ir__emit_paar_batch(flow_ir_prog_t *out, const uint32_t *base_wires,
                                            uint32_t n_base, const uint32_t *raw_masks,
                                            uint32_t batch_len, uint32_t *out_vids) {
    if (batch_len == 0U || batch_len > 32U || n_base > 64U) {
        return false;
    }
    uint32_t wires[64];
    for (uint32_t i = 0U; i < n_base; ++i) {
        wires[i] = base_wires[i];
    }
    uint32_t cur_c = n_base;
    uint64_t lin[32];
    uint8_t cbit[32];
    for (uint32_t r = 0U; r < batch_len; ++r) {
        cbit[r] = (uint8_t)(raw_masks[r] & 1U);
        lin[r] = (uint64_t)(raw_masks[r] >> 1U);
    }

    if (batch_len >= 2U) {
        while (cur_c < 64U && out->num_ops + 8U < FLOW_IR_MAX_OPS) {
            uint32_t best_cnt = 1U;
            uint32_t best_i = 0U;
            uint32_t best_j = 0U;
            for (uint32_t i = 0U; i < cur_c; ++i) {
                uint64_t mi = UINT64_C(1) << i;
                for (uint32_t j = i + 1U; j < cur_c; ++j) {
                    uint64_t mij = mi | (UINT64_C(1) << j);
                    uint32_t cnt = 0U;
                    for (uint32_t r = 0U; r < batch_len; ++r) {
                        if ((lin[r] & mij) == mij) {
                            ++cnt;
                        }
                    }
                    if (cnt > best_cnt) {
                        best_cnt = cnt;
                        best_i = i;
                        best_j = j;
                    }
                }
            }
            if (best_cnt < 2U) {
                break;
            }
            uint32_t w_new = 0U;
            if (!flow_ir__emit_xor2(out, 1U, wires[best_i], wires[best_j], &w_new)) {
                return false;
            }
            wires[cur_c] = w_new;
            uint64_t mij = (UINT64_C(1) << best_i) | (UINT64_C(1) << best_j);
            uint64_t bit_new = UINT64_C(1) << cur_c;
            for (uint32_t r = 0U; r < batch_len; ++r) {
                if ((lin[r] & mij) == mij) {
                    lin[r] = (lin[r] & ~mij) | bit_new;
                }
            }
            ++cur_c;
        }
    }

    for (uint32_t r = 0U; r < batch_len; ++r) {
        bool dup = false;
        for (uint32_t p = 0U; p < r; ++p) {
            if (raw_masks[p] == raw_masks[r]) {
                out_vids[r] = out_vids[p];
                dup = true;
                break;
            }
        }
        if (dup) {
            continue;
        }
        uint64_t lm = lin[r];
        uint32_t acc = 0U;
        if (lm == UINT64_C(0)) {
            if (!flow_ir__emit_const1(out, (uint64_t)cbit[r], &acc)) {
                return false;
            }
        } else {
            uint32_t b0 = searu_ctz64(lm);
            acc = wires[b0];
            lm &= (lm - UINT64_C(1));
            while (lm != UINT64_C(0)) {
                uint32_t bk = searu_ctz64(lm);
                uint32_t nxt = 0U;
                if (!flow_ir__emit_xor2(out, 1U, acc, wires[bk], &nxt)) {
                    return false;
                }
                acc = nxt;
                lm &= (lm - UINT64_C(1));
            }
            if (cbit[r] != 0U) {
                uint32_t nv = 0U;
                if (!flow_ir__emit_not1(out, acc, &nv)) {
                    return false;
                }
                acc = nv;
            }
        }
        out_vids[r] = acc;
    }
    return true;
}

static inline bool flow_ir__splice_mc_plan(flow_ir_prog_t *out, const uint32_t in_bit_vids[],
                                           uint32_t n_in, const searu_mc_plan *plan, uint32_t n_out,
                                           uint32_t out_bit_vids[]) {
    if (n_in + plan->num_and > 32U || n_out > SEARU_MC_MAX_OUT) {
        return false;
    }
    uint32_t wires[64];
    for (uint32_t i = 0U; i < n_in; ++i) {
        wires[i] = in_bit_vids[i];
    }

    uint32_t gate_depth[SEARU_MC_MAX_AND];
    uint32_t val_depth[SEARU_MC_MAX_BASIS];
    for (uint32_t i = 0U; i < 1U + n_in; ++i) {
        val_depth[i] = 0U;
    }
    uint32_t max_d = 0U;
    for (uint32_t k = 0U; k < plan->num_and; ++k) {
        uint32_t md = 0U;
        uint32_t m = plan->and_lhs_mask[k] | plan->and_rhs_mask[k];
        while (m != 0U) {
            uint32_t idx = searu_ctz64((uint64_t)m);
            if (idx < 1U + n_in + k && val_depth[idx] > md) {
                md = val_depth[idx];
            }
            m &= (m - 1U);
        }
        uint32_t kd = (plan->and_lhs_mask[k] != 0U && plan->and_rhs_mask[k] != 0U) ? (md + 1U) : 0U;
        gate_depth[k] = kd;
        val_depth[1U + n_in + k] = kd;
        if (kd > max_d) {
            max_d = kd;
        }
    }

    for (uint32_t d = 0U; d <= max_d; ++d) {
        uint32_t stage_gates[SEARU_MC_MAX_AND];
        uint32_t stage_cnt = 0U;
        uint32_t stage_masks[32];
        uint32_t stage_vids[32];
        for (uint32_t k = 0U; k < plan->num_and; ++k) {
            if (gate_depth[k] == d) {
                stage_gates[stage_cnt] = k;
                stage_masks[2U * stage_cnt] = plan->and_lhs_mask[k];
                stage_masks[2U * stage_cnt + 1U] = plan->and_rhs_mask[k];
                ++stage_cnt;
            }
        }
        if (stage_cnt == 0U) {
            continue;
        }
        if (!flow_ir__emit_paar_batch(out, wires, n_in + plan->num_and, stage_masks, 2U * stage_cnt,
                                      stage_vids)) {
            return false;
        }
        for (uint32_t s = 0U; s < stage_cnt; ++s) {
            uint32_t g = stage_gates[s];
            uint32_t av = 0U;
            if (!flow_ir__emit_and2(out, 1U, stage_vids[2U * s], stage_vids[2U * s + 1U], &av)) {
                return false;
            }
            wires[n_in + g] = av;
        }
    }

    return flow_ir__emit_paar_batch(out, wires, n_in + plan->num_and, plan->out_mask, n_out,
                                    out_bit_vids);
}

static inline bool flow_ir__slice_port_bits(flow_ir_prog_t *out, uint32_t src_vid, uint16_t width,
                                            uint32_t *dst_bits) {
    if (width == 1U) {
        dst_bits[0] = src_vid;
        return true;
    }
    for (uint16_t b = 0U; b < width; ++b) {
        uint32_t ins[FLOW_IR_MAX_INS] = {src_vid, 0, 0, 0, 0, 0};
        if (!flow_ir__emit_op(out, FLOW_IR_OP_SLICE, 1U, 1U, ins, (uint64_t)b, 0ULL, 0ULL, 0U, 0U,
                              NULL, &dst_bits[b])) {
            return false;
        }
    }
    return true;
}

static inline bool flow_ir__dce_compact(flow_ir_prog_t *p, flow_ir_synth_ws_t *ws) {
    flow_ir__zero(ws->val_live, sizeof(ws->val_live));
    for (uint32_t i = 0U; i < p->num_inputs; ++i) {
        ws->val_live[p->inputs[i]] = 1U;
    }
    for (uint32_t i = 0U; i < p->num_outputs; ++i) {
        ws->val_live[p->outputs[i]] = 1U;
    }
    for (int32_t i = (int32_t)p->num_ops - 1; i >= 0; --i) {
        const flow_ir_op_t *op = &p->ops[i];
        if (ws->val_live[op->out_val] != 0U) {
            for (uint16_t j = 0U; j < op->num_ins; ++j) {
                ws->val_live[op->ins[j]] = 1U;
            }
        }
    }

    flow_ir_prog_t *dst = &ws->tmp_prog;
    flow_ir__zero(dst, sizeof(*dst));
    dst->mult = p->mult;
    dst->flags = p->flags;
    flow_ir__write_name24((uint8_t *)dst->name, p->name);
    for (uint32_t i = 0U; i < p->num_inputs; ++i) {
        dst->vals[i] = p->vals[i];
        dst->inputs[i] = i;
        ws->val_map[i] = i;
    }
    dst->num_inputs = p->num_inputs;
    dst->num_vals = p->num_inputs;

    for (uint32_t i = 0U; i < p->num_ops; ++i) {
        const flow_ir_op_t *src_op = &p->ops[i];
        uint32_t old_oid = src_op->out_val;
        if (ws->val_live[old_oid] == 0U) {
            continue;
        }
        uint32_t new_oid = dst->num_vals;
        dst->vals[new_oid] = p->vals[old_oid];
        flow_ir_op_t *nop = &dst->ops[dst->num_ops];
        *nop = *src_op;
        nop->out_val = new_oid;
        for (uint16_t j = 0U; j < nop->num_ins; ++j) {
            nop->ins[j] = ws->val_map[src_op->ins[j]];
        }
        if (nop->lut_len > 0U) {
            nop->lut_off = dst->num_lut_u64;
            for (uint32_t k = 0U; k < nop->lut_len; ++k) {
                dst->lut_pool[dst->num_lut_u64++] = p->lut_pool[src_op->lut_off + k];
            }
        }
        ws->val_map[old_oid] = new_oid;
        dst->num_vals += 1U;
        dst->num_ops += 1U;
    }
    dst->num_outputs = p->num_outputs;
    for (uint32_t i = 0U; i < p->num_outputs; ++i) {
        dst->outputs[i] = ws->val_map[p->outputs[i]];
    }
    *p = *dst;
    return flow_ir_validate(p);
}

static inline bool flow_ir__verify_equiv(const flow_ir_prog_t *a, const flow_ir_prog_t *b,
                                         uint32_t *out_verified) {
    uint32_t tot_in = 0U;
    for (uint32_t i = 0U; i < a->num_inputs; ++i) {
        tot_in += a->vals[a->inputs[i]].width;
    }
    uint32_t n_vecs = (tot_in <= 12U) ? (UINT32_C(1) << tot_in) : 4096U;
    uint64_t rng = UINT64_C(0x517CC1B727220A95);
    uint64_t in_v[FLOW_IR_MAX_IO];
    uint64_t out_a[FLOW_IR_MAX_IO];
    uint64_t out_b[FLOW_IR_MAX_IO];

    for (uint32_t v = 0U; v < n_vecs; ++v) {
        if (tot_in <= 12U) {
            uint32_t sh = 0U;
            for (uint32_t i = 0U; i < a->num_inputs; ++i) {
                uint16_t w = a->vals[a->inputs[i]].width;
                in_v[i] = ((uint64_t)v >> sh) & flow_ir_mask_u64(w);
                sh += w;
            }
        } else {
            for (uint32_t i = 0U; i < a->num_inputs; ++i) {
                uint16_t w = a->vals[a->inputs[i]].width;
                in_v[i] = searu_rng_next(&rng) & flow_ir_mask_u64(w);
            }
        }
        if (!flow_ir_eval_u64(a, in_v, out_a) || !flow_ir_eval_u64(b, in_v, out_b)) {
            return false;
        }
        for (uint32_t o = 0U; o < a->num_outputs; ++o) {
            if (out_a[o] != out_b[o]) {
                return false;
            }
        }
    }
    if (out_verified) {
        *out_verified = n_vecs;
    }
    return true;
}

static inline bool flow_ir_synth_prog(flow_ir_prog_t *out, const flow_ir_prog_t *in,
                                      const flow_ir_synth_cfg_t *cfg, flow_ir_synth_ws_t *ws,
                                      flow_ir_synth_stats_t *stats) {
    if (!out || !in || !ws || !flow_ir_validate(in)) {
        return false;
    }
    flow_ir_synth_cfg_t dcfg = {NULL, 0U, 12U, 1U, UINT64_C(0xF12574742026), false};
    if (cfg) {
        dcfg = *cfg;
    }
    if (dcfg.max_and == 0U || dcfg.max_and > SEARU_MC_MAX_AND) {
        dcfg.max_and = 12U;
    }
    if (dcfg.budget_scale == 0U) {
        dcfg.budget_scale = 1U;
    }

    flow_ir_dag_stats_t in_st;
    if (!flow_ir_analyze_dag(in, &in_st)) {
        return false;
    }
    if (stats) {
        flow_ir__zero(stats, sizeof(*stats));
        stats->in_ops = in->num_ops;
        stats->in_mult_bits = in_st.mult_bits;
        stats->in_mult_depth = in_st.mult_depth;
    }
    flow_ir__zero(ws->memo, sizeof(ws->memo));

    const char *out_name =
        (dcfg.override_name && dcfg.override_name[0] != '\0') ? dcfg.override_name : in->name;

    uint32_t tot_in = 0U;
    for (uint32_t i = 0U; i < in->num_inputs; ++i) {
        tot_in += in->vals[in->inputs[i]].width;
    }
    uint32_t tot_out = 0U;
    for (uint32_t i = 0U; i < in->num_outputs; ++i) {
        tot_out += in->vals[in->outputs[i]].width;
    }

    bool synth_ok = false;
    /* Attempt Whole-Graph synthesis when compact (<= 6 input bits, <= 16 output bits) */
    if (!dcfg.op_wise_only && in->mult == FLOW_IR_MULT_LINEAR && tot_in >= 1U && tot_in <= 6U &&
        tot_out >= 1U && tot_out <= SEARU_MC_MAX_OUT) {
        searu_mc_spec spec;
        flow_ir__zero(&spec, sizeof(spec));
        spec.n_in = tot_in;
        spec.n_in_port0 = (in->num_inputs == 2U) ? in->vals[in->inputs[0]].width : 0U;
        spec.n_out = tot_out;
        spec.max_and = dcfg.max_and;
        spec.max_depth = dcfg.max_depth;
        spec.budget_scale = dcfg.budget_scale;
        spec.seed = dcfg.seed;

        uint32_t ns = UINT32_C(1) << tot_in;
        uint64_t in_v[FLOW_IR_MAX_IO];
        uint64_t out_v[FLOW_IR_MAX_IO];
        bool tt_ok = true;
        for (uint32_t s = 0U; s < ns; ++s) {
            uint32_t sh = 0U;
            for (uint32_t i = 0U; i < in->num_inputs; ++i) {
                uint16_t w = in->vals[in->inputs[i]].width;
                in_v[i] = ((uint64_t)s >> sh) & flow_ir_mask_u64(w);
                sh += w;
            }
            if (!flow_ir_eval_u64(in, in_v, out_v)) {
                tt_ok = false;
                break;
            }
            uint32_t osh = 0U;
            for (uint32_t o = 0U; o < in->num_outputs; ++o) {
                uint16_t ow = in->vals[in->outputs[o]].width;
                for (uint16_t b = 0U; b < ow; ++b) {
                    if (((out_v[o] >> b) & UINT64_C(1)) != UINT64_C(0)) {
                        spec.target_tt[osh + b][s >> 6U] |= (UINT64_C(1) << (s & 63U));
                    }
                }
                osh += ow;
            }
        }
        searu_mc_plan plan;
        if (tt_ok && searu_solve_mc_gf2(&plan, &spec, &ws->mc_ws)) {
            flow_ir__zero(out, sizeof(*out));
            out->mult = in->mult;
            out->flags = in->flags;
            flow_ir__write_name24((uint8_t *)out->name, out_name);
            out->num_inputs = in->num_inputs;
            out->num_vals = in->num_inputs;
            for (uint32_t i = 0U; i < in->num_inputs; ++i) {
                out->vals[i] = in->vals[i];
                out->inputs[i] = i;
            }
            uint32_t in_bits[SEARU_MC_MAX_IN];
            uint32_t bp = 0U;
            bool sl_ok = true;
            for (uint32_t i = 0U; i < in->num_inputs && sl_ok; ++i) {
                sl_ok =
                    flow_ir__slice_port_bits(out, out->inputs[i], out->vals[i].width, &in_bits[bp]);
                bp += out->vals[i].width;
            }
            uint32_t out_bits[SEARU_MC_MAX_OUT];
            if (sl_ok && flow_ir__splice_mc_plan(out, in_bits, tot_in, &plan, tot_out, out_bits)) {
                out->num_outputs = in->num_outputs;
                uint32_t obp = 0U;
                bool pk_ok = true;
                for (uint32_t o = 0U; o < in->num_outputs && pk_ok; ++o) {
                    uint16_t ow = in->vals[in->outputs[o]].width;
                    uint16_t ones[16];
                    for (uint16_t b = 0U; b < ow; ++b) {
                        ones[b] = 1U;
                    }
                    const char *oname = in->vals[in->outputs[o]].name;
                    pk_ok = flow_ir__pack_chunks6(out, &out_bits[obp], ones, ow, oname,
                                                  &out->outputs[o]);
                    obp += ow;
                }
                if (pk_ok && flow_ir__dce_compact(out, ws)) {
                    synth_ok = true;
                    if (stats) {
                        stats->whole_graph_mode = true;
                        stats->sub_ops_synthesized = 1U;
                        stats->total_searu_iters += plan.iterations_run;
                    }
                }
            }
        }
    }

    /* Compositional Sub-Op Synthesis Mode (handles arbitrary DAG widths like 8->8 Tower) */
    if (!synth_ok) {
        flow_ir__zero(out, sizeof(*out));
        out->mult = in->mult;
        out->flags = in->flags;
        flow_ir__write_name24((uint8_t *)out->name, out_name);
        out->num_inputs = in->num_inputs;
        out->num_vals = in->num_inputs;
        for (uint32_t i = 0U; i < in->num_inputs; ++i) {
            out->vals[i] = in->vals[i];
            out->inputs[i] = i;
            ws->val_map[i] = i;
        }
        for (uint32_t i = 0U; i < in->num_ops; ++i) {
            const flow_ir_op_t *op = &in->ops[i];
            uint32_t oid = op->out_val;
            uint16_t ow = in->vals[oid].width;
            uint32_t local_in_b = 0U;
            for (uint16_t j = 0U; j < op->num_ins; ++j) {
                local_in_b += in->vals[op->ins[j]].width;
            }
            bool already_atomic = flow_ir_op_is_affine_gf2(op->kind) ||
                                  (op->kind == FLOW_IR_OP_AND_VEC) ||
                                  (op->kind == FLOW_IR_OP_STAGE_PIPE);
            if (!already_atomic && in->mult == FLOW_IR_MULT_LINEAR && local_in_b >= 1U &&
                local_in_b <= SEARU_MC_MAX_IN && ow >= 1U && ow <= SEARU_MC_MAX_OUT) {
                uint64_t lhash = (op->lut_len > 0U)
                                     ? flow_ir__fnv1a64((const uint8_t *)&in->lut_pool[op->lut_off],
                                                        (size_t)op->lut_len * 8U)
                                     : UINT64_C(0);
                uint16_t w0 = (op->num_ins >= 1U) ? in->vals[op->ins[0]].width : 0U;
                uint16_t w1 = (op->num_ins >= 2U) ? in->vals[op->ins[1]].width : 0U;
                const searu_mc_plan *use_plan = NULL;
                searu_mc_plan fresh_plan;

                if (op->num_ins <= 2U) {
                    for (uint32_t m = 0U; m < 16U; ++m) {
                        if (ws->memo[m].valid && ws->memo[m].kind == op->kind &&
                            ws->memo[m].w0 == w0 && ws->memo[m].w1 == w1 &&
                            ws->memo[m].w_out == ow && ws->memo[m].p0 == op->p0 &&
                            ws->memo[m].p1 == op->p1 && ws->memo[m].p2 == op->p2 &&
                            ws->memo[m].lut_hash == lhash) {
                            use_plan = &ws->memo[m].plan;
                            if (stats) {
                                ++stats->cache_hits;
                            }
                            break;
                        }
                    }
                }
                if (!use_plan) {
                    searu_mc_spec spec;
                    flow_ir__zero(&spec, sizeof(spec));
                    spec.n_in = local_in_b;
                    spec.n_in_port0 = (op->num_ins == 2U) ? (uint32_t)w0 : 0U;
                    spec.n_out = ow;
                    spec.max_and = dcfg.max_and;
                    spec.max_depth = dcfg.max_depth;
                    spec.budget_scale = dcfg.budget_scale;
                    spec.seed = dcfg.seed ^ ((uint64_t)i * UINT64_C(0x9E3779B97F4A7C15));
                    uint32_t ns = UINT32_C(1) << local_in_b;
                    uint64_t omask = flow_ir_mask_u64(ow);
                    uint64_t args[FLOW_IR_MAX_INS] = {0};
                    for (uint32_t s = 0U; s < ns; ++s) {
                        uint32_t sh = 0U;
                        for (uint16_t j = 0U; j < op->num_ins; ++j) {
                            uint16_t jw = in->vals[op->ins[j]].width;
                            args[j] = ((uint64_t)s >> sh) & flow_ir_mask_u64(jw);
                            sh += jw;
                        }
                        uint64_t r = UINT64_C(0);
                        if (!flow_ir__eval_single_op(in, op, omask, args, &r)) {
                            return false;
                        }
                        for (uint16_t b = 0U; b < ow; ++b) {
                            if (((r >> b) & UINT64_C(1)) != UINT64_C(0)) {
                                spec.target_tt[b][s >> 6U] |= (UINT64_C(1) << (s & 63U));
                            }
                        }
                    }
                    if (!searu_solve_mc_gf2(&fresh_plan, &spec, &ws->mc_ws)) {
                        return false;
                    }
                    use_plan = &fresh_plan;
                    if (stats) {
                        ++stats->sub_ops_synthesized;
                        stats->total_searu_iters += fresh_plan.iterations_run;
                    }
                    if (op->num_ins <= 2U) {
                        for (uint32_t m = 0U; m < 16U; ++m) {
                            if (!ws->memo[m].valid) {
                                ws->memo[m].kind = op->kind;
                                ws->memo[m].w0 = w0;
                                ws->memo[m].w1 = w1;
                                ws->memo[m].w_out = ow;
                                ws->memo[m].p0 = op->p0;
                                ws->memo[m].p1 = op->p1;
                                ws->memo[m].p2 = op->p2;
                                ws->memo[m].lut_hash = lhash;
                                ws->memo[m].plan = fresh_plan;
                                ws->memo[m].valid = true;
                                break;
                            }
                        }
                    }
                }

                uint32_t in_bits[SEARU_MC_MAX_IN];
                uint32_t bp = 0U;
                for (uint16_t j = 0U; j < op->num_ins; ++j) {
                    uint32_t src_v = ws->val_map[op->ins[j]];
                    if (!flow_ir__slice_port_bits(out, src_v, in->vals[op->ins[j]].width,
                                                  &in_bits[bp])) {
                        return false;
                    }
                    bp += in->vals[op->ins[j]].width;
                }
                uint32_t out_bits[SEARU_MC_MAX_OUT];
                if (!flow_ir__splice_mc_plan(out, in_bits, local_in_b, use_plan, ow, out_bits)) {
                    return false;
                }
                uint16_t ones[16];
                for (uint16_t b = 0U; b < ow; ++b) {
                    ones[b] = 1U;
                }
                uint32_t new_vid = 0U;
                if (!flow_ir__pack_chunks6(out, out_bits, ones, ow, in->vals[oid].name, &new_vid)) {
                    return false;
                }
                ws->val_map[oid] = new_vid;
            } else {
                uint32_t c_ins[FLOW_IR_MAX_INS] = {0};
                for (uint16_t j = 0U; j < op->num_ins; ++j) {
                    c_ins[j] = ws->val_map[op->ins[j]];
                }
                uint32_t l_off = 0U;
                if (op->lut_len > 0U) {
                    if (out->num_lut_u64 + op->lut_len > FLOW_IR_MAX_LUT_U64) {
                        return false;
                    }
                    l_off = out->num_lut_u64;
                    for (uint32_t k = 0U; k < op->lut_len; ++k) {
                        out->lut_pool[out->num_lut_u64++] = in->lut_pool[op->lut_off + k];
                    }
                }
                uint32_t new_vid = 0U;
                if (!flow_ir__emit_op(out, op->kind, ow, op->num_ins, c_ins, op->p0, op->p1, op->p2,
                                      l_off, op->lut_len, in->vals[oid].name, &new_vid)) {
                    return false;
                }
                ws->val_map[oid] = new_vid;
            }
        }
        out->num_outputs = in->num_outputs;
        for (uint32_t o = 0U; o < in->num_outputs; ++o) {
            uint32_t dst_v = ws->val_map[in->outputs[o]];
            out->outputs[o] = dst_v;
            if (dst_v >= out->num_inputs) {
                flow_ir__write_name24((uint8_t *)out->vals[dst_v].name,
                                      in->vals[in->outputs[o]].name);
            }
        }
        if (!flow_ir__dce_compact(out, ws)) {
            return false;
        }
        synth_ok = true;
    }

    if (!synth_ok) {
        return false;
    }

    uint32_t vstates = 0U;
    if (!flow_ir__verify_equiv(in, out, &vstates)) {
        return false;
    }
    if (stats) {
        flow_ir_dag_stats_t out_st;
        if (!flow_ir_analyze_dag(out, &out_st)) {
            return false;
        }
        stats->out_ops = out->num_ops;
        stats->out_mult_bits = out_st.mult_bits;
        stats->out_mult_depth = out_st.mult_depth;
        stats->verified_states = vstates;
    }
    return true;
}

#ifdef __cplusplus
}
#endif

#endif /* FLOW_IR_SYNTH_H */
