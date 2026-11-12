#include "flow_hott_jet.h"

/* ============================================================================
 * Freestanding L0 Q-Polyhedron Quantization & 2-Jet Next-Face Forecast
 * ============================================================================
 * Pure integer Ring-0 / bare-metal implementation (0 OS headers, 0 heap, 0 FPU,
 * 0 undefined runtime symbols on RV32IMAC / ARMv7-M / x86_64 / AArch64).
 * ============================================================================ */

int flow_qpoly_quantize_inner(size_t plane_count, size_t dim, uint32_t shift, const int64_t *a_num,
                              const int64_t *b_num, int64_t denom, const int64_t *box_min,
                              const int64_t *box_max, int32_t *a_q_out, int64_t *b_q_out) {
    if (!a_num || !b_num || !box_min || !box_max || !a_q_out || !b_q_out || plane_count == 0 ||
        plane_count > FLOW_QPOLY_PLANES || dim == 0 || shift > 30U || denom <= 0)
        return 0;

    for (size_t j = 0; j < dim; ++j) {
        if (box_min[j] > box_max[j])
            return 0;
    }

#if FLOW_HAS_INT128
    __extension__ typedef __int128 i128;
    __extension__ typedef unsigned __int128 u128;
    i128 D = (i128)denom;
    u128 uD = (u128)denom;
    i128 scale = (i128)1 << shift;

    for (size_t i = 0; i < plane_count; ++i) {
        u128 err_num = 0;
        for (size_t j = 0; j < dim; ++j) {
            i128 lo_abs = (box_min[j] < 0) ? -(i128)box_min[j] : (i128)box_min[j];
            i128 hi_abs = (box_max[j] < 0) ? -(i128)box_max[j] : (i128)box_max[j];
            u128 max_x = (u128)((lo_abs > hi_abs) ? lo_abs : hi_abs);

            i128 num = (i128)a_num[i * dim + j] * scale;
            i128 a_hat = (num >= 0) ? flow_i128_div_i128(num + (D >> 1U), D)
                                    : -flow_i128_div_i128((-num) + (D >> 1U), D);
            if (a_hat > (i128)INT32_MAX || a_hat < (i128)INT32_MIN)
                return 0;
            a_q_out[i * dim + j] = (int32_t)a_hat;

            i128 rem = a_hat * D - num;
            u128 urem = (rem < 0) ? (u128)(-rem) : (u128)rem;
            u128 term = 0;
            if (__builtin_mul_overflow(urem, max_x, &term) ||
                __builtin_add_overflow(err_num, term, &err_num))
                return 0;
        }
        u128 ceil_e_u = 0;
        if (__builtin_add_overflow(err_num, uD - 1U, &ceil_e_u))
            return 0;
        ceil_e_u = flow_u128_div_u128(ceil_e_u, uD);
        if (ceil_e_u > (u128)INT64_MAX)
            return 0;
        i128 ceil_e = (i128)ceil_e_u;
        i128 b_scaled = (i128)b_num[i] * scale;
        i128 floor_b = (b_scaled >= 0) ? flow_i128_div_i128(b_scaled, D)
                                       : -flow_i128_div_i128((-b_scaled) + D - 1, D);
        i128 b_hat = floor_b - ceil_e;
        if (b_hat > (i128)INT64_MAX || b_hat < (i128)INT64_MIN)
            return 0;
        b_q_out[i] = (int64_t)b_hat;
    }
#else
    FlowI128Pair D = flow_i128_from_i64(denom);
    FlowI128Pair D_half = flow_i128_from_i64(denom >> 1U);
    FlowI128Pair D_m1 = flow_i128_from_i64(denom - 1);
    FlowU128Pair uD = flow_u128_make(0ULL, (uint64_t)denom);
    int64_t scale64 = (int64_t)(1ULL << shift);

    for (size_t i = 0; i < plane_count; ++i) {
        FlowU128Pair err_num = {0ULL, 0ULL};
        for (size_t j = 0; j < dim; ++j) {
            uint64_t lo_abs =
                (box_min[j] < 0) ? (0ULL - (uint64_t)box_min[j]) : (uint64_t)box_min[j];
            uint64_t hi_abs =
                (box_max[j] < 0) ? (0ULL - (uint64_t)box_max[j]) : (uint64_t)box_max[j];
            uint64_t max_x = (lo_abs > hi_abs) ? lo_abs : hi_abs;

            FlowI128Pair num = flow_i128_mul_i64(a_num[i * dim + j], scale64);
            FlowI128Pair a_hat = (num.hi >= 0)
                                     ? flow_i128_div_pair(flow_i128_add_pair(num, D_half), D)
                                     : flow_i128_neg_pair(flow_i128_div_pair(
                                           flow_i128_add_pair(flow_i128_neg_pair(num), D_half), D));
            if (flow_i128_cmp(a_hat, flow_i128_from_i64(INT32_MAX)) > 0 ||
                flow_i128_cmp(a_hat, flow_i128_from_i64(INT32_MIN)) < 0)
                return 0;
            a_q_out[i * dim + j] = (int32_t)(int64_t)a_hat.lo;

            FlowI128Pair rem = flow_i128_sub_pair(flow_i128_mul_wrap(a_hat, D), num);
            FlowU128Pair urem = (rem.hi < 0) ? flow_u128_neg_pair((uint64_t)rem.hi, rem.lo)
                                             : flow_u128_make((uint64_t)rem.hi, rem.lo);
            FlowU128Pair term = {0ULL, 0ULL};
            if (flow_u128_mul_u64_checked(urem, max_x, &term) ||
                flow_u128_add_checked(err_num, term, &err_num))
                return 0;
        }
        FlowU128Pair ceil_e_u = {0ULL, 0ULL};
        if (flow_u128_add_checked(err_num, flow_u128_make(0ULL, (uint64_t)(denom - 1)), &ceil_e_u))
            return 0;
        ceil_e_u = flow_u128_div_pair(ceil_e_u, uD);
        if (ceil_e_u.hi != 0ULL || ceil_e_u.lo > (uint64_t)INT64_MAX)
            return 0;
        FlowI128Pair ceil_e = {0, ceil_e_u.lo};
        FlowI128Pair b_scaled = flow_i128_mul_i64(b_num[i], scale64);
        FlowI128Pair floor_b =
            (b_scaled.hi >= 0) ? flow_i128_div_pair(b_scaled, D)
                               : flow_i128_neg_pair(flow_i128_div_pair(
                                     flow_i128_add_pair(flow_i128_neg_pair(b_scaled), D_m1), D));
        FlowI128Pair b_hat = flow_i128_sub_pair(floor_b, ceil_e);
        if (flow_i128_cmp(b_hat, flow_i128_from_i64(INT64_MAX)) > 0 ||
            flow_i128_cmp(b_hat, flow_i128_from_i64(INT64_MIN)) < 0)
            return 0;
        b_q_out[i] = (int64_t)b_hat.lo;
    }
#endif
    return 1;
}

FlowQPolyForecast flow_qpoly_predict_next_face(const int32_t *A_rows, const int64_t *b,
                                               uint64_t active, size_t dim, const int32_t *x,
                                               const int32_t *dx, const int32_t *ddx) {
    FlowQPolyForecast fc = {0U, INT64_MAX, false};
    if (!A_rows || !b || !x || !dx || dim == 0 || active == 0ULL)
        return fc;

    int64_t best_t_q16 = INT64_MAX;
    uint64_t rem = active;
    while (rem != 0ULL) {
        uint32_t i = (uint32_t)__builtin_ctzll(rem);
        rem &= (rem - 1ULL);

        const int32_t *row = &A_rows[(size_t)i * dim];
        FlowResultU64 dot_x_res = flow_asm_qdot_i32(row, x, dim, NULL);
        int64_t dot_x = (int64_t)dot_x_res.value;
        bool dot_x_pos_ovf = flow_result_u64_is_boundary(dot_x_res) && (dot_x == INT64_MAX);
        int64_t r_i = (int64_t)flow_asm_qdot_i32(row, dx, dim, NULL).value;
        int64_t c_i = ddx ? (int64_t)flow_asm_qdot_i32(row, ddx, dim, NULL).value : 0;

        if (dot_x >= b[i] || dot_x_pos_ovf) {
            if (r_i > 0 || (r_i == 0 && c_i > 0)) {
                fc.plane = i;
                fc.t_hit_q = 0;
                fc.will_exit = true;
                return fc;
            }
            if (dot_x == b[i] && !dot_x_pos_ovf && r_i < 0 && c_i > 0) {
                uint64_t abs_r = 0ULL - (uint64_t)r_i;
                uint64_t den64 = (uint64_t)c_i;
#if FLOW_HAS_INT128
                __extension__ typedef unsigned __int128 u128;
                u128 num_q16 = ((u128)abs_r) << (FLOW_Q16_SHIFT + 1U);
                u128 den = (u128)den64;
                u128 t_rehit_u = flow_u128_div_u128(num_q16 + (den >> 1U), den);
                int64_t t_rehit_q16 =
                    (t_rehit_u >= (u128)INT64_MAX) ? INT64_MAX : (int64_t)t_rehit_u;
#else
                FlowU128Pair num_q16 = flow_u128_make(abs_r >> (64U - (FLOW_Q16_SHIFT + 1U)),
                                                      abs_r << (FLOW_Q16_SHIFT + 1U));
                FlowU128Pair rounded = {0ULL, 0ULL};
                (void)flow_u128_add_checked(num_q16, flow_u128_make(0ULL, den64 >> 1U), &rounded);
                FlowU128Pair t_rehit_u = flow_u128_div_pair(rounded, flow_u128_make(0ULL, den64));
                int64_t t_rehit_q16 = (t_rehit_u.hi != 0ULL || t_rehit_u.lo >= (uint64_t)INT64_MAX)
                                          ? INT64_MAX
                                          : (int64_t)t_rehit_u.lo;
#endif
                if (t_rehit_q16 > 0 && t_rehit_q16 < best_t_q16) {
                    best_t_q16 = t_rehit_q16;
                    fc.t_hit_q = t_rehit_q16;
                    fc.plane = i;
                    fc.will_exit = true;
                }
            }
            continue;
        }

        int64_t s_i = flow_asm_qsub_sat_pos_i64(b[i], dot_x);

        /*
         * Solve slack(t) = s_i - r_i * t - 0.5 * c_i * t^2 = 0 via pure Q16 2-Jet root primitive:
         * v = s_i, d1 = -r_i, d2 = -c_i
         */
        FlowJet2 slack_jet = flow_jet2_make(s_i, flow_q16_neg_sat(r_i), flow_q16_neg_sat(c_i));
        int64_t t_cand_q16 = 0;
        if (flow_jet2_solve_root(slack_jet, best_t_q16, &t_cand_q16) && t_cand_q16 > 0 &&
            t_cand_q16 < best_t_q16) {
            best_t_q16 = t_cand_q16;
            fc.t_hit_q = t_cand_q16;
            fc.plane = i;
            fc.will_exit = true;
        }
    }
    return fc;
}
