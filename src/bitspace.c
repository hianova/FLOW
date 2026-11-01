#include "bitspace.h"
#include "reload.h"
#include "flow_jet.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* =========================================================================
 * C17 Implementation of SEARU no_std_tool::numerics (U256, Prime, Linalg, Random)
 * ========================================================================= */
FlowU256 flow_u256_zero(void) {
    FlowU256 z = {{0ULL, 0ULL, 0ULL, 0ULL}};
    return z;
}
FlowU256 flow_u256_one(void) {
    FlowU256 o = {{1ULL, 0ULL, 0ULL, 0ULL}};
    return o;
}
FlowU256 flow_u256_from_u64(uint64_t v) {
    FlowU256 u = {{v, 0ULL, 0ULL, 0ULL}};
    return u;
}
int flow_u256_from_hex_str(const char *hex, FlowU256 *out) {
    if (!hex || !out)
        return 0;
    if (hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X'))
        hex += 2;
    size_t len = strlen(hex);
    if (len == 0 || len > 64)
        return 0;
    *out = flow_u256_zero();
    size_t pos = len;
    for (size_t wi = 0; wi < 4 && pos > 0; ++wi) {
        uint64_t word = 0ULL;
        for (uint32_t j = 0; j < 16 && pos > 0; ++j) {
            char c = hex[--pos];
            uint64_t v = 0;
            if (c >= '0' && c <= '9')
                v = (uint64_t)(c - '0');
            else if (c >= 'a' && c <= 'f')
                v = (uint64_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                v = (uint64_t)(c - 'A' + 10);
            else
                return 0;
            word |= (v << (j * 4U));
        }
        out->w[wi] = word;
    }
    return 1;
}
int flow_u256_is_zero(const FlowU256 *a) {
    return a && (a->w[0] | a->w[1] | a->w[2] | a->w[3]) == 0ULL;
}
int flow_u256_is_even(const FlowU256 *a) {
    return a && ((a->w[0] & 1ULL) == 0ULL);
}
uint32_t flow_u256_leading_zeros(const FlowU256 *a) {
    if (!a)
        return 256U;
    uint32_t z = 0U;
    for (int i = 3; i >= 0; --i) {
        if (a->w[i] == 0ULL)
            z += 64U;
        else {
            z += (uint32_t)__builtin_clzll(a->w[i]);
            break;
        }
    }
    return z;
}
int flow_u256_cmp(const FlowU256 *a, const FlowU256 *b) {
    for (int i = 3; i >= 0; --i) {
        if (a->w[i] < b->w[i])
            return -1;
        if (a->w[i] > b->w[i])
            return 1;
    }
    return 0;
}
FlowU256 flow_u256_overflowing_add(FlowU256 a, FlowU256 b, int *overflow_out) {
    FlowU256 r = {{0}};
    uint64_t carry = 0ULL;
    for (int i = 0; i < 4; ++i) {
        flow_u128_t s = (flow_u128_t)a.w[i] + (flow_u128_t)b.w[i] + (flow_u128_t)carry;
        r.w[i] = (uint64_t)s;
        carry = (uint64_t)(s >> 64U);
    }
    if (overflow_out)
        *overflow_out = (carry > 0ULL);
    return r;
}
FlowU256 flow_u256_overflowing_sub(FlowU256 a, FlowU256 b, int *borrow_out) {
    FlowU256 r = {{0}};
    uint64_t borrow = 0ULL;
    for (int i = 0; i < 4; ++i) {
        uint64_t sub1 = a.w[i] - b.w[i];
        uint64_t ov1 = (a.w[i] < b.w[i]) ? 1ULL : 0ULL;
        uint64_t sub2 = sub1 - borrow;
        uint64_t ov2 = (sub1 < borrow) ? 1ULL : 0ULL;
        r.w[i] = sub2;
        borrow = ov1 | ov2;
    }
    if (borrow_out)
        *borrow_out = (borrow > 0ULL);
    return r;
}
FlowU256 flow_u256_shl1(FlowU256 a) {
    FlowU256 r = {{0}};
    uint64_t carry = 0ULL;
    for (int i = 0; i < 4; ++i) {
        r.w[i] = (a.w[i] << 1U) | carry;
        carry = a.w[i] >> 63U;
    }
    return r;
}
FlowU256 flow_u256_shr1(FlowU256 a) {
    FlowU256 r = {{0}};
    uint64_t carry = 0ULL;
    for (int i = 3; i >= 0; --i) {
        r.w[i] = (a.w[i] >> 1U) | carry;
        carry = a.w[i] << 63U;
    }
    return r;
}
void flow_u256_mul_wide(FlowU256 a, FlowU256 b, uint64_t out_512[8]) {
    memset(out_512, 0, sizeof(uint64_t) * 8);
    for (int i = 0; i < 4; ++i) {
        flow_u128_t carry = 0;
        for (int j = 0; j < 4; ++j) {
            flow_u128_t prod =
                (flow_u128_t)a.w[i] * (flow_u128_t)b.w[j] + (flow_u128_t)out_512[i + j] + carry;
            out_512[i + j] = (uint64_t)prod;
            carry = prod >> 64U;
        }
        int idx = i + 4;
        while (carry != 0 && idx < 8) {
            flow_u128_t sum = (flow_u128_t)out_512[idx] + carry;
            out_512[idx] = (uint64_t)sum;
            carry = sum >> 64U;
            idx++;
        }
    }
}
FlowU256 flow_u256_mul(FlowU256 a, FlowU256 b) {
    uint64_t wide[8];
    flow_u256_mul_wide(a, b, wide);
    FlowU256 r = {{wide[0], wide[1], wide[2], wide[3]}};
    return r;
}
int flow_u256_div_rem(FlowU256 a, FlowU256 b, FlowU256 *q_out, FlowU256 *r_out) {
    if (flow_u256_is_zero(&b))
        return 0;
    if (flow_u256_cmp(&a, &b) < 0) {
        if (q_out)
            *q_out = flow_u256_zero();
        if (r_out)
            *r_out = a;
        return 1;
    }
    FlowU256 q = flow_u256_zero(), r = flow_u256_zero();
    uint32_t bits = 256U - flow_u256_leading_zeros(&a);
    for (int32_t i = (int32_t)bits - 1; i >= 0; --i) {
        r = flow_u256_shl1(r);
        uint64_t bit = (a.w[i / 64] >> (i % 64)) & 1ULL;
        r.w[0] |= bit;
        if (flow_u256_cmp(&r, &b) >= 0) {
            r = flow_u256_overflowing_sub(r, b, NULL);
            q.w[i / 64] |= (1ULL << (i % 64));
        }
    }
    if (q_out)
        *q_out = q;
    if (r_out)
        *r_out = r;
    return 1;
}
uint64_t flow_u256_inv_word(const FlowU256 *n) {
    if (!n || (n->w[0] & 1ULL) == 0ULL)
        return 0ULL;
    uint64_t x = n->w[0], inv = x;
    for (int i = 0; i < FLOW_NEWTON_64BIT_ITERS; ++i)
        inv = inv * (2ULL - x * inv);
    return 0ULL - inv;
}
FlowU256 flow_u256_mont_reduce(const uint64_t t_in[FLOW_U512_LIMBS], const FlowU256 *modulus,
                               uint64_t n0_inv) {
    uint64_t t[FLOW_U512_LIMBS];
    memcpy(t, t_in, sizeof(t));
    uint64_t top_carry = 0ULL;
    for (int i = 0; i < FLOW_U256_LIMBS; ++i) {
        uint64_t m = t[i] * n0_inv;
        flow_u128_t carry = 0;
        for (int j = 0; j < FLOW_U256_LIMBS; ++j) {
            flow_u128_t prod =
                (flow_u128_t)m * (flow_u128_t)modulus->w[j] + (flow_u128_t)t[i + j] + carry;
            t[i + j] = (uint64_t)prod;
            carry = prod >> FLOW_WORD_BITS;
        }
        int j = i + FLOW_U256_LIMBS;
        while (carry > 0 && j < FLOW_U512_LIMBS) {
            flow_u128_t sum = (flow_u128_t)t[j] + carry;
            t[j] = (uint64_t)sum;
            carry = sum >> FLOW_WORD_BITS;
            j++;
        }
        if (carry > 0)
            top_carry += (uint64_t)carry;
    }
    FlowU256 res = {{t[4], t[5], t[6], t[7]}};
    if (top_carry > 0ULL || flow_u256_cmp(&res, modulus) >= 0)
        res = flow_u256_overflowing_sub(res, *modulus, NULL);
    return res;
}
static FlowU256 flow_u256_mul_mod_even(FlowU256 a, FlowU256 b, FlowU256 m) {
    FlowU256 res = flow_u256_zero();
    FlowU256 cur = flow_u256_zero();
    flow_u256_div_rem(a, m, NULL, &cur);
    while (!flow_u256_is_zero(&b)) {
        if (!flow_u256_is_even(&b)) {
            int ovf = 0;
            res = flow_u256_overflowing_add(res, cur, &ovf);
            if (ovf || flow_u256_cmp(&res, &m) >= 0)
                res = flow_u256_overflowing_sub(res, m, NULL);
        }
        uint64_t hi_bit = cur.w[3] >> 63U;
        cur = flow_u256_shl1(cur);
        if (hi_bit || flow_u256_cmp(&cur, &m) >= 0)
            cur = flow_u256_overflowing_sub(cur, m, NULL);
        b = flow_u256_shr1(b);
    }
    return res;
}
FlowU256 flow_u256_mod_pow(FlowU256 base, FlowU256 exp, FlowU256 modulus) {
    FlowU256 one = flow_u256_one();
    if (flow_u256_is_zero(&modulus) || flow_u256_cmp(&modulus, &one) == 0)
        return flow_u256_zero();
    if (flow_u256_is_even(&modulus)) {
        FlowU256 res = one, b = flow_u256_zero();
        flow_u256_div_rem(base, modulus, NULL, &b);
        while (!flow_u256_is_zero(&exp)) {
            if (!flow_u256_is_even(&exp))
                res = flow_u256_mul_mod_even(res, b, modulus);
            b = flow_u256_mul_mod_even(b, b, modulus);
            exp = flow_u256_shr1(exp);
        }
        return res;
    }
    if (flow_u256_cmp(&base, &modulus) >= 0) {
        flow_u256_div_rem(base, modulus, NULL, &base);
    }
    uint64_t n0_inv = flow_u256_inv_word(&modulus);
    FlowU256 r = one;
    for (uint32_t i = 0; i < FLOW_U256_BITS; ++i) {
        uint64_t hi_bit = r.w[3] >> 63U;
        r = flow_u256_shl1(r);
        if (hi_bit || flow_u256_cmp(&r, &modulus) >= 0)
            r = flow_u256_overflowing_sub(r, modulus, NULL);
    }
    FlowU256 r2 = r;
    for (uint32_t i = 0; i < FLOW_U256_BITS; ++i) {
        uint64_t hi_bit = r2.w[3] >> 63U;
        r2 = flow_u256_shl1(r2);
        if (hi_bit || flow_u256_cmp(&r2, &modulus) >= 0)
            r2 = flow_u256_overflowing_sub(r2, modulus, NULL);
    }
    uint64_t wide[FLOW_U512_LIMBS];
    flow_u256_mul_wide(base, r2, wide);
    FlowU256 b_mont = flow_u256_mont_reduce(wide, &modulus, n0_inv);
    FlowU256 res_mont = r;
    while (!flow_u256_is_zero(&exp)) {
        if (!flow_u256_is_even(&exp)) {
            flow_u256_mul_wide(res_mont, b_mont, wide);
            res_mont = flow_u256_mont_reduce(wide, &modulus, n0_inv);
        }
        flow_u256_mul_wide(b_mont, b_mont, wide);
        b_mont = flow_u256_mont_reduce(wide, &modulus, n0_inv);
        exp = flow_u256_shr1(exp);
    }
    flow_u256_mul_wide(res_mont, one, wide);
    return flow_u256_mont_reduce(wide, &modulus, n0_inv);
}
int flow_u256_is_probably_prime(const FlowU256 *n, const uint64_t *bases, size_t base_count) {
    if (!n)
        return 0;
    FlowU256 one = flow_u256_one(), two = flow_u256_from_u64(2ULL),
             three = flow_u256_from_u64(3ULL);
    if (flow_u256_is_zero(n) || flow_u256_cmp(n, &one) == 0)
        return 0;
    if (flow_u256_cmp(n, &two) == 0 || flow_u256_cmp(n, &three) == 0)
        return 1;
    if (flow_u256_is_even(n))
        return 0;
    FlowU256 n_minus_1 = flow_u256_overflowing_sub(*n, one, NULL);
    FlowU256 d = n_minus_1;
    uint32_t s = 0;
    while (flow_u256_is_even(&d)) {
        d = flow_u256_shr1(d);
        s++;
    }
    for (size_t bi = 0; bi < base_count; ++bi) {
        FlowU256 a = flow_u256_from_u64(bases[bi]);
        if (flow_u256_cmp(&a, n) >= 0)
            continue;
        FlowU256 x = flow_u256_mod_pow(a, d, *n);
        if (flow_u256_cmp(&x, &one) == 0 || flow_u256_cmp(&x, &n_minus_1) == 0)
            continue;
        int composite = 1;
        for (uint32_t r = 1; r < s; ++r) {
            x = flow_u256_mod_pow(x, two, *n);
            if (flow_u256_cmp(&x, &n_minus_1) == 0) {
                composite = 0;
                break;
            }
        }
        if (composite)
            return 0;
    }
    return 1;
}
