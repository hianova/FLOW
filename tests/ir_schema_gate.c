#include "flow_ir.h"
#include "flow_ir_io.h"
#include "synth_arena_abi.h"
#include "synth_bundle_abi.h"

#include <limits.h>
#include <stdio.h>

#ifndef IR_GATE_DIR
#define IR_GATE_DIR "build/ir_gate"
#endif

static flow_ir_prog_t g_p;
static flow_ir_prog_t g_q;
static flow_ir_prog_t g_bad;
static uint8_t g_buf[FLOW_IR_MAX_FILE_BYTES];
static uint8_t g_raw[SYNTH_BUNDLE_ABI_BUNDLE_BYTES];
static flow_cell g_arena[SYNTH_BUNDLE_ABI_CHART_COUNT];

static int check_synth_equiv(const char *spec_p, const char *opt_p, uint32_t max_mb,
                             uint32_t max_md) {
    if (!flow_ir_load_file(&g_p, spec_p, g_buf, sizeof(g_buf))) {
        return 1;
    }
    if (!flow_ir_load_file(&g_q, opt_p, g_buf, sizeof(g_buf))) {
        return 2;
    }
    flow_ir_dag_stats_t st;
    if (!flow_ir_analyze_dag(&g_q, &st)) {
        return 3;
    }
    if (st.mult_bits > max_mb || st.mult_depth > max_md || st.non_affine_ops != st.mult_bits) {
        fprintf(stderr, "SYNTH BOUND FAIL %s: mb=%u (<=%u) md=%u (<=%u) nao=%u\n", opt_p,
                st.mult_bits, max_mb, st.mult_depth, max_md, st.non_affine_ops);
        return 4;
    }
    uint32_t tot = 0U;
    for (uint32_t i = 0U; i < g_p.num_inputs; ++i) {
        tot += g_p.vals[i].width;
    }
    uint32_t ns = (1U << tot);
    for (uint32_t s = 0U; s < ns; ++s) {
        uint64_t inv[FLOW_IR_MAX_IO] = {0};
        uint64_t o1[FLOW_IR_MAX_IO] = {0};
        uint64_t o2[FLOW_IR_MAX_IO] = {0};
        uint32_t sh = 0U;
        for (uint32_t i = 0U; i < g_p.num_inputs; ++i) {
            uint16_t w = g_p.vals[i].width;
            inv[i] = ((uint64_t)s >> sh) & flow_ir_mask_u64(w);
            sh += w;
        }
        if (!flow_ir_eval_u64(&g_p, inv, o1) || !flow_ir_eval_u64(&g_q, inv, o2)) {
            return 5;
        }
        if (o1[0] != o2[0]) {
            return 6;
        }
    }
    return 0;
}

static void init_min_valid_prog(flow_ir_prog_t *p) {
    flow_ir_wire_zero(p, sizeof(*p));
    flow_ir_wire_write_name24((uint8_t *)p->name, "neg_probe");
    p->mult = FLOW_IR_MULT_LINEAR;
    p->flags = FLOW_IR_FLAG_CLEAN;
    p->num_inputs = 3U;
    p->num_outputs = 1U;
    p->num_ops = 1U;
    p->num_vals = 4U;
    p->vals[0].width = 8U;
    p->vals[0].name[0] = 'a';
    p->vals[1].width = 8U;
    p->vals[1].name[0] = 'b';
    p->vals[2].width = 1U;
    p->vals[2].name[0] = 's';
    p->vals[3].width = 8U;
    p->vals[3].name[0] = 'y';
    p->inputs[0] = 0U;
    p->inputs[1] = 1U;
    p->inputs[2] = 2U;
    p->outputs[0] = 3U;
    p->ops[0].out_val = 3U;
    p->ops[0].ins[0] = 0U;
    p->ops[0].ins[1] = 1U;
    p->ops[0].ins[2] = 2U;
}

static int check_negative_validate_cases(void) {
    init_min_valid_prog(&g_bad);
    g_bad.ops[0].kind = FLOW_IR_OP_XOR_VEC;
    g_bad.ops[0].num_ins = 2U;
    if (!flow_ir_validate(&g_bad)) {
        return 1;
    }

    g_bad.ops[0].num_ins = 1U;
    if (flow_ir_validate(&g_bad)) {
        return 2;
    }

    g_bad.ops[0].kind = FLOW_IR_OP_GF2M_MUL;
    g_bad.ops[0].num_ins = 2U;
    g_bad.ops[0].p0 = UINT64_C(0);
    g_bad.ops[0].p1 = UINT64_C(0x11B);
    if (flow_ir_validate(&g_bad)) {
        return 3;
    }
    g_bad.ops[0].p0 = UINT64_C(8);
    if (!flow_ir_validate(&g_bad)) {
        return 4;
    }

    g_bad.ops[0].kind = FLOW_IR_OP_GF2M_INV;
    g_bad.ops[0].num_ins = 1U;
    g_bad.ops[0].p0 = UINT64_C(33);
    if (flow_ir_validate(&g_bad)) {
        return 5;
    }
    g_bad.ops[0].p0 = UINT64_C(8);
    if (!flow_ir_validate(&g_bad)) {
        return 6;
    }

    g_bad.ops[0].kind = FLOW_IR_OP_LUT_ROM;
    g_bad.ops[0].num_ins = 1U;
    g_bad.ops[0].p0 = UINT64_C(0);
    g_bad.ops[0].lut_len = 0U;
    if (flow_ir_validate(&g_bad)) {
        return 7;
    }

    g_bad.ops[0].kind = FLOW_IR_OP_CTRL_SEL;
    g_bad.ops[0].num_ins = 2U;
    if (flow_ir_validate(&g_bad)) {
        return 8;
    }
    g_bad.ops[0].num_ins = 3U;
    if (!flow_ir_validate(&g_bad)) {
        return 9;
    }
    return 0;
}

static int check_fiber_discrimination_cases(void) {
    static uint8_t fir_raw[4096];
    FILE *fp2 = fopen(IR_GATE_DIR "/synth_lin.fir", "rb");
    if (!fp2) {
        return 27;
    }
    size_t fir_sz = fread(fir_raw, 1U, sizeof(fir_raw), fp2);
    fclose(fp2);
    if (fir_sz <= FLOW_IR_HEADER_BYTES + 8U) {
        return 28;
    }
    if (flow_ir_load_bytes_fiber(&g_p, fir_raw, fir_sz) != FLOW_IR_FIBER_COMMITTED) {
        return 29;
    }
    fir_raw[fir_sz - 1U] ^= UINT8_C(0x01);
    if (flow_ir_load_bytes_fiber(&g_p, fir_raw, fir_sz) != FLOW_IR_FIBER_FALLBACK_MAC) {
        return 30;
    }
    fir_raw[fir_sz - 1U] ^= UINT8_C(0x01);
    fir_raw[0] ^= UINT8_C(0x01);
    if (flow_ir_load_bytes_fiber(&g_p, fir_raw, fir_sz) != FLOW_IR_FIBER_FALLBACK_FORMAT) {
        return 31;
    }
    fir_raw[0] ^= UINT8_C(0x01);
    if (flow_ir_load_bytes_fiber(&g_p, fir_raw, fir_sz) != FLOW_IR_FIBER_COMMITTED) {
        return 32;
    }
    g_bad = g_p;
    g_bad.ops[0].kind = FLOW_IR_OP_STAGE_PIPE;
    g_bad.ops[0].num_ins = 2U;
    if (flow_ir_validate_fiber(&g_bad) != FLOW_IR_FIBER_FALLBACK_MULT) {
        return 33;
    }
    g_bad = g_p;
    g_bad.ops[0].num_ins = 5U;
    if (flow_ir_validate_fiber(&g_bad) != FLOW_IR_FIBER_FALLBACK_ARITY) {
        return 34;
    }
    g_bad = g_p;
    if (g_bad.num_ops > 0U && g_bad.ops[0].num_ins > 0U) {
        g_bad.ops[0].ins[0] = g_bad.ops[0].out_val;
        if (flow_ir_validate_fiber(&g_bad) != FLOW_IR_FIBER_FALLBACK_SSA) {
            return 35;
        }
    }
    return 0;
}

int main(void) {
    synth_pkt_t pkt_in = {0xABU,
                          0x01U,
                          0x1234U,
                          0xCAFEBABEU,
                          -424242,
                          0U,
                          {UINT64_C(0x0123456789ABCDEF), 2ULL, 3ULL, 4ULL},
                          {-9876543210LL, -1LL}};
    uint8_t wire[64];
    synth_arena_pkt_pack_bytes(wire, &pkt_in);
    synth_pkt_t pkt_out = {0};
    synth_arena_pkt_unpack_bytes(wire, &pkt_out);
    if (pkt_out.tag_u8 != pkt_in.tag_u8 || pkt_out.flag_u16 != pkt_in.flag_u16 ||
        pkt_out.len_u32 != pkt_in.len_u32 || pkt_out.val_i32 != pkt_in.val_i32 ||
        pkt_out.seq_u64[0] != pkt_in.seq_u64[0] || pkt_out.acc_i64[1] != pkt_in.acc_i64[1]) {
        return 1;
    }

    synth_pkt_p_t pp_in = {
        {65, 66, 67, 68}, {-10, 20, -30, 40}, {1U, 2U, 3U, 4U}, 0U, {10ULL, 20ULL, 30ULL, 40ULL}};
    synth_arena_pkt_p_pack_bytes(wire, &pp_in);
    synth_pkt_p_t pp_out = {0};
    synth_arena_pkt_p_unpack_bytes(wire, &pp_out);
    if (pp_out.vals_i16[2] != -30 || pp_out.acc_u64[3] != 40ULL) {
        return 12;
    }

    FILE *fp = fopen(IR_GATE_DIR "/synth_bundle.flow", "rb");
    if (!fp || fread(g_raw, 1, sizeof(g_raw), fp) != sizeof(g_raw)) {
        return 2;
    }
    fclose(fp);
    for (size_t i = 0; i < SYNTH_BUNDLE_ABI_CHART_COUNT; ++i) {
        flow_cell_from_bytes(g_arena[i], g_raw + (i + 1) * 64U);
    }
    synth_bundle_abi_t b_out;
    if (!synth_bundle_abi_unpack(g_arena, SYNTH_BUNDLE_ABI_CHART_COUNT, &b_out)) {
        return 3;
    }
    if (b_out.alpha != 42 || b_out.beta != -123 || b_out.gamma != 777) {
        return 4;
    }
    g_arena[SYNTH_BUNDLE_ABI_TAG_CHART][7] ^= 1ULL;
    if (synth_bundle_abi_unpack(g_arena, SYNTH_BUNDLE_ABI_CHART_COUNT, &b_out)) {
        return 5;
    }
    if (!flow_ir_load_file(&g_p, IR_GATE_DIR "/synth_cart.fir", g_buf, sizeof(g_buf))) {
        return 6;
    }
    if (g_p.mult != FLOW_IR_MULT_CARTESIAN || flow_ir_admits_unitary(&g_p) || g_p.num_ops != 1U) {
        return 7;
    }
    if (!flow_ir_load_file(&g_p, IR_GATE_DIR "/synth_lin.fir", g_buf, sizeof(g_buf))) {
        return 8;
    }
    if (!flow_ir_admits_unitary(&g_p)) {
        return 9;
    }
    uint64_t inv_in[3] = {0x53ULL, 0x05ULL, 0ULL};
    uint64_t inv_out[1] = {0};
    if (!flow_ir_eval_u64(&g_p, inv_in, inv_out) ||
        flow_ir__gf2m_mul(inv_out[0], 0x53ULL, 8, 0x11B) != 1ULL) {
        return 10;
    }
    uint64_t zero_in[3] = {0ULL, 0x05ULL, 0ULL};
    uint64_t zero_out[1] = {0};
    if (!flow_ir_eval_u64(&g_p, zero_in, zero_out) || zero_out[0] != 0x05ULL) {
        return 11;
    }
    if (!flow_ir_load_file(&g_p, IR_GATE_DIR "/synth_linmap.fir", g_buf, sizeof(g_buf))) {
        return 13;
    }
    flow_ir_dag_stats_t dag_st;
    if (!flow_ir_analyze_dag(&g_p, &dag_st) || dag_st.mult_bits != 9U || dag_st.mult_depth != 1U ||
        dag_st.non_affine_ops != 1U) {
        return 14;
    }
    uint64_t lm_in[1] = {0x103ULL};
    uint64_t lm_out[1] = {0};
    if (!flow_ir_eval_u64(&g_p, lm_in, lm_out) || lm_out[0] != 0x102ULL) {
        return 15;
    }
    if (!flow_ir_save_file(&g_p, IR_GATE_DIR "/rt.fir", g_buf, sizeof(g_buf))) {
        return 16;
    }
    if (!flow_ir_load_file(&g_q, IR_GATE_DIR "/rt.fir", g_buf, sizeof(g_buf)) ||
        g_q.fnv1a != g_p.fnv1a) {
        return 17;
    }
    if (check_synth_equiv(IR_GATE_DIR "/synth_mul4.fir", IR_GATE_DIR "/synth_mul4_opt.fir", 9U,
                          1U) != 0) {
        return 18;
    }
    if (check_synth_equiv(IR_GATE_DIR "/synth_inv4.fir", IR_GATE_DIR "/synth_inv4_a5.fir", 5U,
                          3U) != 0) {
        return 19;
    }
    if (check_synth_equiv(IR_GATE_DIR "/synth_inv4.fir", IR_GATE_DIR "/synth_inv4_d2.fir", 6U,
                          2U) != 0) {
        return 20;
    }
    if (check_synth_equiv(IR_GATE_DIR "/synth_lut5.fir", IR_GATE_DIR "/synth_lut5_opt.fir", 5U,
                          1U) != 0) {
        return 21;
    }
    if (check_synth_equiv(IR_GATE_DIR "/synth_quad_inv.fir", IR_GATE_DIR "/synth_quad_a32.fir", 32U,
                          5U) != 0) {
        return 22;
    }
    if (check_synth_equiv(IR_GATE_DIR "/synth_quad_inv.fir", IR_GATE_DIR "/synth_quad_d4.fir", 33U,
                          4U) != 0) {
        return 23;
    }

    /* Check 24: Extreme signed-value round-trips without <string.h> / memcpy */
    synth_pkt_t pkt_ext_in = {0xFFU,
                              0x7FU,
                              0xFFFFU,
                              UINT32_MAX,
                              INT32_MIN,
                              UINT32_MAX,
                              {UINT64_MAX, 0ULL, 1ULL, UINT64_MAX},
                              {INT64_MIN, INT64_MAX}};
    synth_arena_pkt_pack_bytes(wire, &pkt_ext_in);
    synth_pkt_t pkt_ext_out = {0};
    synth_arena_pkt_unpack_bytes(wire, &pkt_ext_out);
    if (pkt_ext_out.val_i32 != INT32_MIN || pkt_ext_out.acc_i64[0] != INT64_MIN ||
        pkt_ext_out.acc_i64[1] != INT64_MAX) {
        return 24;
    }

    synth_pkt_p_t pp_ext_in = {{(char)CHAR_MIN, (char)-1, (char)0, (char)CHAR_MAX},
                               {INT16_MIN, -1, 0, INT16_MAX},
                               {1U, 2U, 3U, UINT32_MAX},
                               0xDEADBEEFU,
                               {0ULL, 1ULL, 2ULL, UINT64_MAX}};
    synth_arena_pkt_p_pack_bytes(wire, &pp_ext_in);
    synth_pkt_p_t pp_ext_out = {0};
    synth_arena_pkt_p_unpack_bytes(wire, &pp_ext_out);
    if (pp_ext_out.tag_ch[0] != (char)CHAR_MIN || pp_ext_out.tag_ch[1] != (char)-1 ||
        pp_ext_out.tag_ch[3] != (char)CHAR_MAX || pp_ext_out.vals_i16[0] != INT16_MIN ||
        pp_ext_out.vals_i16[1] != -1 || pp_ext_out.vals_i16[3] != INT16_MAX) {
        return 25;
    }

    /* Check 25: Negative flow_ir_validate arity & gf2m range enforcement */
    if (check_negative_validate_cases() != 0) {
        return 26;
    }

    /* Check 26: HoTT Fallback Fiber discrimination (flow_ir_load_bytes_fiber & validate_fiber) */
    int rc_fiber = check_fiber_discrimination_cases();
    if (rc_fiber != 0) {
        return rc_fiber;
    }
    return 0;
}
