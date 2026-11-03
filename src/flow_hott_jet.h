#ifndef FLOW_HOTT_JET_H
#define FLOW_HOTT_JET_H

#include "flow_asm.h"

#if !defined(__cplusplus)
#include <stdatomic.h>
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW v4: Universal External-Constraint HoTT & 2-Jet Engine
 * ============================================================================
 * De-abstracts HoTT and 2-Jet into pure compile-time invariants and branchless
 * 64-bit register algebra over external variables (u_ext, b_ext):
 *
 *   1. Compile-Time Univalence (`FLOW_STATIC_ASSERT_UNIVALENT`): 0 runtime bytes.
 *   2. 64-Bit Cubical Kan & Bitwise HIT Quotient: 1-cycle neighbor transition.
 *   3. 2-Jet Fiber (`FlowJet2` / `FlowQJet2`): Taylor evaluation, quadratic root
 *      solver (`flow_jet2_solve_root`), and ASM differential self-checking.
 *   4. Universal External-Constraint Polyhedron (`FLOW_QPOLY_DEFINE`): static
 *      normal matrix A + dynamic external bounds b_ext with 1-cycle branchless
 *      dual-threshold hysteresis (b_enter / b_exit).
 * ============================================================================
 */

/* ============================================================================
 * 1. Compile-Time Univalence (A ~= B => A = U_0 B, 0 Runtime Bytes)
 * ============================================================================ */

#define FLOW_STATIC_ASSERT_UNIVALENT(TypeA, TypeB)                                       \
    _Static_assert(sizeof(TypeA) == sizeof(TypeB) && _Alignof(TypeA) == _Alignof(TypeB), \
                   "HoTT Univalence violation: TypeA and TypeB are not layout-isomorphic")

/* ============================================================================
 * 2. Cubical De Morgan Algebra, Kan Homotopy & Bitwise HIT Quotient
 * ============================================================================ */

typedef uint8_t FlowDeMorganInterval;
#define FLOW_DEMORGAN_ZERO   0U
#define FLOW_DEMORGAN_ONE    1U
#define FLOW_CUBICAL_MAX_DIM 64U

static inline FlowDeMorganInterval flow_demorgan_not(FlowDeMorganInterval i) {
    return (FlowDeMorganInterval)(1U - (i & 1U));
}
static inline FlowDeMorganInterval flow_demorgan_and(FlowDeMorganInterval a,
                                                     FlowDeMorganInterval b) {
    return (FlowDeMorganInterval)((a & 1U) & (b & 1U));
}
static inline FlowDeMorganInterval flow_demorgan_or(FlowDeMorganInterval a,
                                                    FlowDeMorganInterval b) {
    return (FlowDeMorganInterval)((a & 1U) | (b & 1U));
}

static inline uint64_t flow_demorgan_word_not(uint64_t w) {
    return ~w;
}
static inline uint64_t flow_demorgan_word_and(uint64_t w1, uint64_t w2) {
    return w1 & w2;
}
static inline uint64_t flow_demorgan_word_or(uint64_t w1, uint64_t w2) {
    return w1 | w2;
}

static inline uint64_t flow_cubical_face_proj(uint64_t sieve, uint32_t dim_k, uint8_t endpoint) {
    if (dim_k >= FLOW_CUBICAL_MAX_DIM)
        return sieve;
    uint64_t bit = 1ULL << dim_k;
    return (endpoint == 0U) ? (sieve & ~bit) : (sieve | bit);
}

static inline uint64_t flow_cubical_degeneracy(uint64_t sieve, uint32_t dim_k) {
    if (dim_k >= FLOW_CUBICAL_MAX_DIM - 1U)
        return sieve & ((1ULL << (FLOW_CUBICAL_MAX_DIM - 1U)) - 1ULL);
    uint64_t lower_mask = (dim_k > 0U) ? ((1ULL << dim_k) - 1ULL) : 0ULL;
    return (sieve & lower_mask) | ((sieve & ~lower_mask) << 1U);
}

static inline bool flow_cubical_sieve_is_covering(uint64_t sieve, uint64_t target_basis) {
    return (sieve & target_basis) == target_basis;
}

static inline uint8_t flow_topos_classify_subobject(uint64_t subobject_mask,
                                                    uint64_t ambient_sieve) {
    return ((subobject_mask & ambient_sieve) == subobject_mask) ? 1U : 0U;
}

/* Pure bitwise HIT exception quotient: M^\circ = M \ U_exception (0 structs) */
static inline uint64_t flow_hott_quotient_sieve(uint64_t ambient_sieve,
                                                uint64_t exception_zone_mask) {
    return ambient_sieve & ~exception_zone_mask;
}

static inline uint32_t flow_hott_quotient_dof(uint64_t ambient_sieve,
                                              uint64_t exception_zone_mask) {
    return (uint32_t)__builtin_popcountll(ambient_sieve & ~exception_zone_mask);
}

typedef enum {
    FLOW_KAN_FILLED_HOMOTOPIC = 0,
    FLOW_KAN_OBSTRUCTED_SINGULARITY = 1
} FlowKanStatus;

static inline FlowKanStatus flow_kan_check_homotopy(uint64_t mask_p, uint64_t mask_q,
                                                    uint64_t boundary_filter,
                                                    uint64_t *mismatch_out) {
    uint64_t diff = (mask_p ^ mask_q) & boundary_filter;
    if (mismatch_out)
        *mismatch_out = diff;
    return (diff == 0ULL) ? FLOW_KAN_FILLED_HOMOTOPIC : FLOW_KAN_OBSTRUCTED_SINGULARITY;
}

static inline FlowKanStatus flow_cubical_step_neighbor(uint64_t mask, uint32_t bit,
                                                       uint64_t boundary_filter,
                                                       uint64_t *next_mask) {
    if (bit >= FLOW_CUBICAL_MAX_DIM) {
        if (next_mask)
            *next_mask = mask;
        return FLOW_KAN_OBSTRUCTED_SINGULARITY;
    }
    uint64_t cand = mask ^ (1ULL << bit);
    FlowKanStatus st = flow_kan_check_homotopy(mask, cand, boundary_filter, NULL);
    if (next_mask)
        *next_mask = (st == FLOW_KAN_FILLED_HOMOTOPIC) ? cand : mask;
    return st;
}

/* ============================================================================
 * 3. 2-Jet Fiber Algebra, Root Solver & Self-Bootstrapping Verifier
 * ============================================================================ */

static inline FlowJet2 flow_jet2_make(double v, double d1, double d2) {
    FlowJet2 r;
    r.v = __builtin_isfinite(v) ? v : 0.0;
    r.d1 = __builtin_isfinite(d1) ? d1 : 0.0;
    r.d2 = __builtin_isfinite(d2) ? d2 : 0.0;
    return r;
}

static inline FlowJet2 flow_jet2_var(double v) {
    return flow_jet2_make(v, 1.0, 0.0);
}

static inline FlowJet2 flow_jet2_const(double c) {
    return flow_jet2_make(c, 0.0, 0.0);
}

static inline FlowJet2 flow_jet2_add(FlowJet2 a, FlowJet2 b) {
    return flow_jet2_make(a.v + b.v, a.d1 + b.d1, a.d2 + b.d2);
}

static inline FlowJet2 flow_jet2_sub(FlowJet2 a, FlowJet2 b) {
    return flow_jet2_make(a.v - b.v, a.d1 - b.d1, a.d2 - b.d2);
}

static inline FlowJet2 flow_jet2_mul(FlowJet2 a, FlowJet2 b) {
    return flow_jet2_make(a.v * b.v, a.d1 * b.v + a.v * b.d1,
                          a.d2 * b.v + 2.0 * a.d1 * b.d1 + a.v * b.d2);
}

static inline FlowJet2 flow_jet2_div(FlowJet2 a, FlowJet2 b) {
    if (b.v == 0.0 || !__builtin_isfinite(b.v)) {
        return flow_jet2_const(0.0);
    }
    double v = a.v / b.v;
    double d1 = (a.d1 - v * b.d1) / b.v;
    double d2 = (a.d2 - 2.0 * d1 * b.d1 - v * b.d2) / b.v;
    return flow_jet2_make(v, d1, d2);
}

static inline FlowJet2 flow_jet2_neg(FlowJet2 x) {
    return flow_jet2_make(-x.v, -x.d1, -x.d2);
}

static inline FlowJet2 flow_jet2_abs(FlowJet2 x) {
    double s = (x.v > 0.0) ? 1.0 : ((x.v < 0.0) ? -1.0 : 0.0);
    return flow_jet2_make(__builtin_fabs(x.v), s * x.d1, s * x.d2);
}

static inline FlowJet2 flow_jet2_sqrt(FlowJet2 x) {
    if (x.v <= 0.0) {
        return flow_jet2_const(0.0);
    }
    double r = __builtin_sqrt(x.v);
    double d1 = x.d1 / (2.0 * r);
    double d2 = (x.d2 - 2.0 * d1 * d1) / (2.0 * r);
    return flow_jet2_make(r, d1, d2);
}

static inline FlowJet2 flow_jet2_sin(FlowJet2 x) {
    double s = __builtin_sin(x.v), c = __builtin_cos(x.v);
    return flow_jet2_make(s, c * x.d1, -s * x.d1 * x.d1 + c * x.d2);
}

static inline FlowJet2 flow_jet2_cos(FlowJet2 x) {
    double s = __builtin_sin(x.v), c = __builtin_cos(x.v);
    return flow_jet2_make(c, -s * x.d1, -c * x.d1 * x.d1 - s * x.d2);
}

static inline FlowJet2 flow_jet2_atan2(FlowJet2 y, FlowJet2 x) {
    double r2 = x.v * x.v + y.v * y.v;
    if (r2 <= 0.0) {
        return flow_jet2_const(0.0);
    }
    double d1 = (x.v * y.d1 - y.v * x.d1) / r2;
    double d2 = ((x.v * y.d2 - y.v * x.d2) - 2.0 * d1 * (x.v * x.d1 + y.v * y.d1)) / r2;
    return flow_jet2_make(__builtin_atan2(y.v, x.v), d1, d2);
}

static inline double flow_jet2_eval_taylor(FlowJet2 j, double dt) {
    return j.v + j.d1 * dt + 0.5 * j.d2 * dt * dt;
}

static inline bool flow_jet2_solve_root(FlowJet2 j, double t_max, double *out_t) {
    if (!__builtin_isfinite(j.v) || !__builtin_isfinite(j.d1) || !__builtin_isfinite(j.d2) ||
        !__builtin_isfinite(t_max) || t_max <= 0.0) {
        return false;
    }
    double a = 0.5 * j.d2;
    double b = j.d1;
    double c = j.v;
    if (__builtin_fabs(a) <= 1e-12) {
        if (__builtin_fabs(b) <= 1e-12) {
            return false;
        }
        double t = -c / b;
        if (t > 0.0 && t <= t_max) {
            if (out_t) {
                *out_t = t;
            }
            return true;
        }
        return false;
    }
    double disc = b * b - 4.0 * a * c;
    if (disc < 0.0) {
        return false;
    }
    double sqrt_disc = __builtin_sqrt(disc);
    double t1 = (-b - sqrt_disc) / (2.0 * a);
    double t2 = (-b + sqrt_disc) / (2.0 * a);
    double best_t = t_max + 1.0;
    bool found = false;
    if (t1 > 0.0 && t1 <= t_max) {
        best_t = t1;
        found = true;
    }
    if (t2 > 0.0 && t2 <= t_max && (!found || t2 < best_t)) {
        best_t = t2;
        found = true;
    }
    if (found && out_t) {
        *out_t = best_t;
    }
    return found;
}

static inline uint64_t flow_jet2_nondegenerate_ballot64(const FlowJet2 *jets, size_t count,
                                                        double eps_d1, double eps_d2) {
    if (!jets || count == 0U || !__builtin_isfinite(eps_d1) || !__builtin_isfinite(eps_d2) ||
        eps_d1 < 0.0 || eps_d2 < 0.0) {
        return 0ULL;
    }
    size_t n = (count > 64U) ? 64U : count;
    uint64_t ballot = 0ULL;
    for (size_t i = 0U; i < n; ++i) {
        if (__builtin_isfinite(jets[i].v) && __builtin_isfinite(jets[i].d1) &&
            __builtin_isfinite(jets[i].d2)) {
            if (__builtin_fabs(jets[i].d1) > eps_d1 || __builtin_fabs(jets[i].d2) > eps_d2) {
                ballot |= (1ULL << i);
            }
        }
    }
    return ballot;
}

static inline uint64_t flow_asm_jet2_self_check_step(FlowJet2 prev, FlowJet2 next, double dt,
                                                     int64_t tol_v_q16, int64_t tol_d1_q16,
                                                     uint64_t *inout_sieve) {
    uint64_t local_sieve = inout_sieve ? *inout_sieve : FLOW_ASM_SIEVE_ALL_SOUND;
    (void)flow_asm_fpu_check_jet2(prev, &local_sieve);
    (void)flow_asm_fpu_check_jet2(next, &local_sieve);

    double v_hat = flow_jet2_eval_taylor(prev, dt);
    double d1_hat = prev.d1 + 0.5 * (prev.d2 + next.d2) * dt;

    double err_v = __builtin_fabs(next.v - v_hat) * 65536.0;
    double err_d1 = __builtin_fabs(next.d1 - d1_hat) * 65536.0;

    bool fin_v = __builtin_isfinite(dt) && __builtin_isfinite(err_v) && (err_v < (double)INT64_MAX);
    bool fin_d1 =
        __builtin_isfinite(dt) && __builtin_isfinite(err_d1) && (err_d1 < (double)INT64_MAX);

    int64_t r0_q16 = fin_v ? (int64_t)err_v : INT64_MAX;
    int64_t r1_q16 = fin_d1 ? (int64_t)err_d1 : INT64_MAX;

    FlowResultU64 margin0 = flow_asm_qsub_i64(tol_v_q16, r0_q16, &local_sieve);
    FlowResultU64 margin1 = flow_asm_qsub_i64(tol_d1_q16, r1_q16, &local_sieve);

    uint64_t clear_bits =
        ((!fin_v || (int64_t)margin0.value < 0 || flow_result_u64_is_boundary(margin0))
             ? FLOW_ASM_SIEVE_JET2_TAYLOR_0
             : 0ULL) |
        ((!fin_d1 || (int64_t)margin1.value < 0 || flow_result_u64_is_boundary(margin1))
             ? FLOW_ASM_SIEVE_JET2_TAYLOR_1
             : 0ULL);
    local_sieve &= ~clear_bits;

    if (inout_sieve) {
        *inout_sieve = local_sieve;
    }
    return local_sieve;
}

/* ============================================================================
 * 4. Universal External-Constraint Q-Polyhedron (FlowQPoly)
 * ============================================================================
 * Static deterministic normal matrix A[64][DIM] paired with dynamic external
 * constraint bounds b[64] and optional branchless hysteresis bounds b_exit[64].
 * ============================================================================ */

#define FLOW_QPOLY_PLANES 64U

typedef struct {
    uint64_t violated_mask;  /* Sieve: bit i = 1 iff plane i is violated (a_i . x > b_active_i) */
    uint32_t worst_plane;    /* Plane index with largest positive violation */
    uint32_t tightest_plane; /* Interior plane index with minimum non-negative slack */
    int64_t worst_violation; /* Q-scaled maximum violation (>= 0) */
    int64_t min_slack;       /* Q-scaled minimum slack among satisfied active planes */
} FlowQPolyEval;

typedef struct {
    uint32_t plane;
    int64_t t_hit_q;
    bool will_exit;
} FlowQPolyForecast;

int flow_qpoly_quantize_inner(size_t plane_count, size_t dim, uint32_t shift, const int64_t *a_num,
                              const int64_t *b_num, int64_t denom, const int64_t *box_min,
                              const int64_t *box_max, int32_t *a_q_out, int64_t *b_q_out);

FlowQPolyForecast flow_qpoly_predict_next_face(const int32_t *A_rows, const int64_t *b,
                                               uint64_t active, size_t dim, const int32_t *x,
                                               const int32_t *dx, const int32_t *ddx);

static inline int64_t flow_asm_select_i64(uint64_t cond_bit, int64_t if_true, int64_t if_false) {
    int64_t out = 0;
#if !defined(FLOW_FORCE_C17_FALLBACK) && (defined(__aarch64__) || defined(__arm64__))
    __asm__("cmp  %[cond], #0\n\t"
            "csel %[out], %[t], %[f], ne"
            : [out] "=r"(out)
            : [cond] "r"(cond_bit), [t] "r"(if_true), [f] "r"(if_false)
            : "cc");
#else
    out = (cond_bit != 0ULL) ? if_true : if_false;
#endif
    return out;
}

#define FLOW_QPOLY_DEFINE(NAME, DIM, SHIFT)                                                        \
    enum {                                                                                         \
        NAME##_DIM = (DIM),                                                                        \
        NAME##_SHIFT = (SHIFT)                                                                     \
    };                                                                                             \
    typedef struct __attribute__((aligned(FLOW_HW_CACHE_LINE_BYTES))) {                            \
        int32_t A[FLOW_QPOLY_PLANES][DIM];         /* Static deterministic constraint matrix */    \
        _Atomic int64_t b[FLOW_QPOLY_PLANES];      /* Dynamic external bound b_enter */            \
        _Atomic int64_t b_exit[FLOW_QPOLY_PLANES]; /* Optional hysteresis recovery bound b_exit */ \
        _Atomic uint64_t active_mask;              /* Active constraint planes bitmask */          \
        _Atomic uint64_t hysteresis_mask;          /* Planes with dual-threshold hysteresis */     \
        _Atomic uint64_t generation;               /* Atomic external constraint epoch */          \
    } NAME;                                                                                        \
    _Static_assert(sizeof(NAME) <= FLOW_HW_PAGE_BYTES, #NAME " exceeds one hardware page");        \
                                                                                                   \
    static inline __attribute__((unused)) void NAME##_init(NAME *p) {                              \
        if (!p)                                                                                    \
            return;                                                                                \
        __builtin_memset(p, 0, sizeof(*p));                                                        \
        atomic_init(&p->active_mask, 0ULL);                                                        \
        atomic_init(&p->hysteresis_mask, 0ULL);                                                    \
        atomic_init(&p->generation, 1ULL);                                                         \
    }                                                                                              \
                                                                                                   \
    static inline __attribute__((unused)) int32_t NAME##_quantize_scalar(double v) {               \
        if (!__builtin_isfinite(v))                                                                \
            return 0;                                                                              \
        double scaled = v * (double)(1ULL << (SHIFT));                                             \
        if (scaled >= (double)INT32_MAX)                                                           \
            return INT32_MAX;                                                                      \
        if (scaled <= (double)INT32_MIN)                                                           \
            return INT32_MIN;                                                                      \
        return (int32_t)__builtin_llround(scaled);                                                 \
    }                                                                                              \
                                                                                                   \
    static inline __attribute__((unused)) void NAME##_set_hysteresis(                              \
        NAME *p, uint32_t plane, int64_t b_enter_val, int64_t b_exit_val) {                        \
        if (!p || plane >= FLOW_QPOLY_PLANES)                                                      \
            return;                                                                                \
        atomic_store_explicit(&p->b[plane], b_enter_val, memory_order_relaxed);                    \
        atomic_store_explicit(&p->b_exit[plane], b_exit_val, memory_order_relaxed);                \
        uint64_t bit = 1ULL << plane;                                                              \
        atomic_fetch_or_explicit(&p->hysteresis_mask, bit, memory_order_relaxed);                  \
        atomic_fetch_or_explicit(&p->active_mask, bit, memory_order_release);                      \
    }                                                                                              \
                                                                                                   \
    static inline __attribute__((unused)) void NAME##_update_external_bounds(                      \
        NAME *p, const int64_t *b_ext, uint64_t mask) {                                            \
        if (!p || !b_ext)                                                                          \
            return;                                                                                \
        uint64_t rem = mask;                                                                       \
        while (rem != 0ULL) {                                                                      \
            uint32_t i = (uint32_t)__builtin_ctzll(rem);                                           \
            rem &= (rem - 1ULL);                                                                   \
            atomic_store_explicit(&p->b[i], b_ext[i], memory_order_relaxed);                       \
        }                                                                                          \
        atomic_store_explicit(&p->active_mask, mask, memory_order_release);                        \
        atomic_fetch_add_explicit(&p->generation, 1ULL, memory_order_acq_rel);                     \
    }                                                                                              \
                                                                                                   \
    static inline __attribute__((unused)) FlowQPolyEval NAME##_eval_hysteresis(                    \
        const NAME *p, const int32_t x[DIM], uint64_t prev_violated_mask) {                        \
        if (!p || !x) {                                                                            \
            FlowQPolyEval empty = {0ULL, 0U, 0U, 0, 0};                                            \
            return empty;                                                                          \
        }                                                                                          \
        uint64_t rem = atomic_load_explicit(&p->active_mask, memory_order_acquire);                \
        uint64_t use_exit_mask =                                                                   \
            prev_violated_mask ? (prev_violated_mask &                                             \
                                  atomic_load_explicit(&p->hysteresis_mask, memory_order_relaxed)) \
                               : 0ULL;                                                             \
        int32_t x_local[DIM];                                                                      \
        for (size_t j = 0; j < (size_t)(DIM); ++j) {                                               \
            x_local[j] = x[j];                                                                     \
        }                                                                                          \
        uint64_t violated_mask = 0ULL;                                                             \
        uint32_t worst_plane = 0U;                                                                 \
        uint32_t tightest_plane = 0U;                                                              \
        int64_t worst_violation = 0;                                                               \
        int64_t min_slack = INT64_MAX;                                                             \
        bool any_violated = false;                                                                 \
        bool any_satisfied = false;                                                                \
        while (rem != 0ULL) {                                                                      \
            uint32_t i = (uint32_t)__builtin_ctzll(rem);                                           \
            rem &= (rem - 1ULL);                                                                   \
            FlowResultU64 dot_res = flow_asm_qdot_i32(p->A[i], x_local, (size_t)(DIM), NULL);      \
            int64_t dot = (int64_t)dot_res.value;                                                  \
            bool ovf = (dot_res.boundary_tag != 0ULL);                                             \
            int64_t b_enter_i = atomic_load_explicit(&p->b[i], memory_order_relaxed);              \
            int64_t bi = b_enter_i;                                                                \
            if (__builtin_expect(use_exit_mask != 0ULL, 0)) {                                      \
                int64_t b_exit_i = atomic_load_explicit(&p->b_exit[i], memory_order_relaxed);      \
                bi = flow_asm_select_i64((use_exit_mask >> i) & 1ULL, b_exit_i, b_enter_i);        \
            }                                                                                      \
            if (dot > bi || (ovf && dot == INT64_MAX)) {                                           \
                int64_t viol = ovf ? INT64_MAX : flow_asm_qsub_sat_pos_i64(dot, bi);               \
                violated_mask |= (1ULL << i);                                                      \
                if (!any_violated || viol > worst_violation) {                                     \
                    worst_violation = viol;                                                        \
                    worst_plane = i;                                                               \
                }                                                                                  \
                any_violated = true;                                                               \
            } else {                                                                               \
                int64_t slack = flow_asm_qsub_sat_pos_i64(bi, dot);                                \
                if (!any_satisfied || slack < min_slack) {                                         \
                    min_slack = slack;                                                             \
                    tightest_plane = i;                                                            \
                }                                                                                  \
                any_satisfied = true;                                                              \
            }                                                                                      \
        }                                                                                          \
        if (!any_satisfied)                                                                        \
            min_slack = 0;                                                                         \
        FlowQPolyEval res = {violated_mask, worst_plane, tightest_plane, worst_violation,          \
                             min_slack};                                                           \
        return res;                                                                                \
    }                                                                                              \
                                                                                                   \
    static inline __attribute__((unused)) FlowQPolyEval NAME##_eval(const NAME *p,                 \
                                                                    const int32_t x[DIM]) {        \
        return NAME##_eval_hysteresis(p, x, 0ULL);                                                 \
    }                                                                                              \
                                                                                                   \
    static inline __attribute__((unused)) int NAME##_quantize_inner(                               \
        NAME *p, size_t plane_count, const int64_t *a_num, const int64_t *b_num, int64_t denom,    \
        const int64_t box_min[DIM], const int64_t box_max[DIM]) {                                  \
        if (!p || plane_count == 0 || plane_count > FLOW_QPOLY_PLANES)                             \
            return 0;                                                                              \
        NAME##_init(p);                                                                            \
        int64_t b_tmp[FLOW_QPOLY_PLANES] = {0};                                                    \
        if (!flow_qpoly_quantize_inner(plane_count, (size_t)(DIM), (uint32_t)(SHIFT), a_num,       \
                                       b_num, denom, box_min, box_max, &p->A[0][0], b_tmp))        \
            return 0;                                                                              \
        for (size_t i = 0; i < plane_count; ++i) {                                                 \
            atomic_store_explicit(&p->b[i], b_tmp[i], memory_order_relaxed);                       \
            atomic_store_explicit(&p->b_exit[i], b_tmp[i], memory_order_relaxed);                  \
        }                                                                                          \
        uint64_t mask = (plane_count >= 64U) ? ~0ULL : ((1ULL << plane_count) - 1ULL);             \
        atomic_store_explicit(&p->active_mask, mask, memory_order_release);                        \
        return 1;                                                                                  \
    }

#ifdef __cplusplus
}
#endif

#endif /* FLOW_HOTT_JET_H */
