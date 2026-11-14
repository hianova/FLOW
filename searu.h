#ifndef SEARU_H
#define SEARU_H

#include "flow.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * SEARU — Pure-GPR 1-Bit Chaos, CLZ Lévy-Flight & 2-Jet Symbiotic Co-Solver (v7.0.0)
 * ============================================================================
 * Zero-float, zero-heap, freestanding (-mgeneral-regs-only) combinatorial NP
 * engine paired symbiotically with FLOW's 128-bit 2-Jet manifold substrate:
 *   1. Pure-GPR Exact Power-Law Jump: hardware `CLZ` transforms uniform 64-bit
 *      entropy into an analytical Zipf / Pareto heavy tail (0 FPU, 0 powf, 0 udiv).
 *   2. Sparse-k Cardinality Mutation: cures full-dimension perturbation collapse
 *      by flipping k ~ Zipf(chaos) coupled variables while respecting Kan pins.
 *   3. Bilevel Symbiosis (SEARU <-> FLOW 2-Jet): SEARU hops across disconnected
 *      discrete topologies while FLOW 2-Jet collapses continuous Q16.16 slack
 *      via Citardauq/Lyapunov descent, feeding barrier residuals |j0| back as
 *      a frustration compass and committing breakthroughs via lock-free @swap.
 * ============================================================================ */

#define SEARU_CANVAS_WORDS     4U
#define SEARU_CANVAS_BITS      256U
#define SEARU_GRAPH_MAX_NODES  64U
#define SEARU_BILEVEL_MAX_VARS 64U

#define SEARU_CHAOS_MIN_Q8     128U /* Calm local 1-opt/2-opt (s = 3.00, ~0.02% swans) */
#define SEARU_CHAOS_DEFAULT_Q8 224U /* Balanced Levy flight   (s = 2.14, ~0.78% swans) */
#define SEARU_CHAOS_MAX_Q8     384U /* Extreme Zipf chaos     (s = 1.67, ~6.25% swans) */

/* Self-Evolved Hyperparameter Defaults (overridden by `flow_opt_params.h` via `make self-opt`) */
#ifndef SEARU_OPT_CHAOS_MIN_Q8
#define SEARU_OPT_CHAOS_MIN_Q8 128U
#endif
#ifndef SEARU_OPT_CHAOS_MAX_Q8
#define SEARU_OPT_CHAOS_MAX_Q8 384U
#endif
#ifndef SEARU_OPT_STAG_SHIFT
#define SEARU_OPT_STAG_SHIFT 1U
#endif
#ifndef SEARU_OPT_BARRIER_BOOST_Q8
#define SEARU_OPT_BARRIER_BOOST_Q8 48U
#endif
#ifndef SEARU_OPT_RAND_WALK_MASK
#define SEARU_OPT_RAND_WALK_MASK 3U
#endif
#ifndef SEARU_OPT_COL_WALK_MASK
#define SEARU_OPT_COL_WALK_MASK SEARU_OPT_RAND_WALK_MASK
#endif
#ifndef SEARU_OPT_BIL_WALK_MASK
#define SEARU_OPT_BIL_WALK_MASK SEARU_OPT_RAND_WALK_MASK
#endif
#ifndef SEARU_OPT_INL_WALK_MASK
#define SEARU_OPT_INL_WALK_MASK SEARU_OPT_RAND_WALK_MASK
#endif
#ifndef SEARU_OPT_SAT_INIT_TEMP_Q12
#define SEARU_OPT_SAT_INIT_TEMP_Q12 UINT64_C(8192)
#endif
#ifndef SEARU_OPT_SAT_COOL_Q16
#define SEARU_OPT_SAT_COOL_Q16 UINT64_C(65200)
#endif
#ifndef SEARU_OPT_SAT_REHEAT_STAG
#define SEARU_OPT_SAT_REHEAT_STAG 64U
#endif
#ifndef SEARU_OPT_SAT_REHEAT_Q12
#define SEARU_OPT_SAT_REHEAT_Q12 UINT64_C(2048)
#endif
#ifndef SEARU_OPT_COL_INIT_TEMP_Q12
#define SEARU_OPT_COL_INIT_TEMP_Q12 UINT64_C(6144)
#endif
#ifndef SEARU_OPT_COL_COOL_Q16
#define SEARU_OPT_COL_COOL_Q16 UINT64_C(65100)
#endif
#ifndef SEARU_OPT_COL_REHEAT_STAG
#define SEARU_OPT_COL_REHEAT_STAG 48U
#endif
#ifndef SEARU_OPT_COL_REHEAT_Q12
#define SEARU_OPT_COL_REHEAT_Q12 UINT64_C(2048)
#endif
#ifndef SEARU_OPT_BIL_INIT_TEMP_Q12
#define SEARU_OPT_BIL_INIT_TEMP_Q12 UINT64_C(4194304)
#endif
#ifndef SEARU_OPT_BIL_COOL_Q16
#define SEARU_OPT_BIL_COOL_Q16 UINT64_C(65000)
#endif
#ifndef SEARU_OPT_BIL_REHEAT_STAG
#define SEARU_OPT_BIL_REHEAT_STAG 48U
#endif
#ifndef SEARU_OPT_BIL_REHEAT_Q12
#define SEARU_OPT_BIL_REHEAT_Q12 UINT64_C(131072)
#endif
#ifndef SEARU_OPT_INL_INIT_TEMP_Q12
#define SEARU_OPT_INL_INIT_TEMP_Q12 UINT64_C(8192)
#endif
#ifndef SEARU_OPT_INL_COOL_Q16
#define SEARU_OPT_INL_COOL_Q16 UINT64_C(65200)
#endif
#ifndef SEARU_OPT_INL_REHEAT_STAG
#define SEARU_OPT_INL_REHEAT_STAG 64U
#endif
#ifndef SEARU_OPT_INL_REHEAT_Q12
#define SEARU_OPT_INL_REHEAT_Q12 UINT64_C(2048)
#endif

typedef struct {
    uint32_t chaos_min_q8;
    uint32_t chaos_max_q8;
    uint32_t stag_shift;
    uint32_t barrier_boost_q8;
    uint32_t rand_walk_mask;
    uint32_t col_walk_mask;
    uint32_t bil_walk_mask;
    uint32_t inl_walk_mask;
    uint64_t sat_init_temp_q12;
    uint64_t sat_cool_q16;
    uint32_t sat_reheat_stag;
    uint64_t sat_reheat_q12;
    uint64_t col_init_temp_q12;
    uint64_t col_cool_q16;
    uint32_t col_reheat_stag;
    uint64_t col_reheat_q12;
    uint64_t bil_init_temp_q12;
    uint64_t bil_cool_q16;
    uint32_t bil_reheat_stag;
    uint64_t bil_reheat_q12;
    uint64_t inl_init_temp_q12;
    uint64_t inl_cool_q16;
    uint32_t inl_reheat_stag;
    uint64_t inl_reheat_q12;
} searu_hyperparams;

#define SEARU_HUMAN_BASELINE_HP \
    {128U,                      \
     384U,                      \
     1U,                        \
     48U,                       \
     3U,                        \
     3U,                        \
     3U,                        \
     3U,                        \
     UINT64_C(8192),            \
     UINT64_C(65200),           \
     64U,                       \
     UINT64_C(2048),            \
     UINT64_C(6144),            \
     UINT64_C(65100),           \
     48U,                       \
     UINT64_C(2048),            \
     UINT64_C(4194304),         \
     UINT64_C(65000),           \
     48U,                       \
     UINT64_C(131072),          \
     UINT64_C(8192),            \
     UINT64_C(65200),           \
     64U,                       \
     UINT64_C(2048)}

#define SEARU_ACTIVE_OPT_HP                                                                 \
    {SEARU_OPT_CHAOS_MIN_Q8,      SEARU_OPT_CHAOS_MAX_Q8,      SEARU_OPT_STAG_SHIFT,        \
     SEARU_OPT_BARRIER_BOOST_Q8,  SEARU_OPT_RAND_WALK_MASK,    SEARU_OPT_COL_WALK_MASK,     \
     SEARU_OPT_BIL_WALK_MASK,     SEARU_OPT_INL_WALK_MASK,     SEARU_OPT_SAT_INIT_TEMP_Q12, \
     SEARU_OPT_SAT_COOL_Q16,      SEARU_OPT_SAT_REHEAT_STAG,   SEARU_OPT_SAT_REHEAT_Q12,    \
     SEARU_OPT_COL_INIT_TEMP_Q12, SEARU_OPT_COL_COOL_Q16,      SEARU_OPT_COL_REHEAT_STAG,   \
     SEARU_OPT_COL_REHEAT_Q12,    SEARU_OPT_BIL_INIT_TEMP_Q12, SEARU_OPT_BIL_COOL_Q16,      \
     SEARU_OPT_BIL_REHEAT_STAG,   SEARU_OPT_BIL_REHEAT_Q12,    SEARU_OPT_INL_INIT_TEMP_Q12, \
     SEARU_OPT_INL_COOL_Q16,      SEARU_OPT_INL_REHEAT_STAG,   SEARU_OPT_INL_REHEAT_Q12}

typedef struct {
    int64_t db0_q16;
    int64_t db1_q16;
    int64_t db2_q16;
    uint64_t weight_cost;
} searu_bilevel_term;

typedef struct {
    uint64_t canvas[SEARU_CANVAS_WORDS];
    uint64_t best_energy;
    uint64_t init_energy;
    int64_t best_x_q16;
    uint32_t iterations_run;
    uint32_t epiphanies;
    uint32_t black_swans;
    uint32_t converged_2jet;
    uint64_t replay_seed;
} searu_result;

#define SEARU_SAT3_CLAUSE(v0, neg0, v1, neg1, v2, neg2)                         \
    (((uint32_t)((v0) & 0xFFU)) | (((uint32_t)((v1) & 0xFFU)) << 8U) |          \
     (((uint32_t)((v2) & 0xFFU)) << 16U) | (((uint32_t)((neg0) & 1U)) << 24U) | \
     (((uint32_t)((neg1) & 1U)) << 25U) | (((uint32_t)((neg2) & 1U)) << 26U))

#define SEARU_EDGE_U16(u, v) ((uint16_t)((((uint16_t)((u) & 0x3FU)) << 8U) | ((v) & 0x3FU)))

#ifdef SEARU_EXPORT_RING0
uint64_t searu_ring0_step_1bit(uint64_t out_engram[8], const uint64_t in_engram[8],
                               const uint64_t kan_pin[4], const uint64_t frustration[4]);
uint64_t searu_ring0_bilevel_step(uint64_t out_state[8], const uint64_t cur_state[8],
                                  const uint64_t base_bound[8], const searu_bilevel_term terms[4],
                                  uint64_t *rng_io, uint64_t *bits_io);
#define SEARU_RING0_API
#else
#define SEARU_RING0_API static inline
#endif

#ifdef SEARU_EXPORT_SOLVERS
uint64_t searu_solve_3sat(searu_result *out, const uint32_t *clauses, uint32_t num_clauses,
                          uint32_t num_vars, const uint64_t init_canvas[4],
                          const uint64_t kan_pin[4], uint64_t seed, uint32_t max_iters);
uint64_t searu_solve_graph_color(searu_result *out, const uint16_t *edges, uint32_t num_edges,
                                 uint32_t num_nodes, uint32_t num_colors,
                                 const uint64_t init_canvas[4], uint64_t kan_node_pin,
                                 uint64_t seed, uint32_t max_iters);
uint64_t searu_solve_bilevel_2jet(searu_result *out, uint64_t out_best_chart[8],
                                  const uint64_t base_bound[8], const searu_bilevel_term *terms,
                                  uint32_t num_terms, uint64_t kan_bit_pin, int64_t init_x_q16,
                                  int64_t u_ext_q16, int64_t target_x_q16, uint32_t jet_steps,
                                  uint64_t seed, uint32_t max_iters);
bool searu_flow_commit_chart(flow_cell *arena, uint64_t slot, uint64_t cap_mask, uint32_t new_epoch,
                             const uint64_t best_chart[8]);
bool searu_heal_and_swap(flow_cell *arena, uint64_t cap_mask, const uint64_t state[8],
                         const searu_bilevel_term *terms, uint32_t num_terms, uint64_t kan_bit_pin,
                         int64_t u_ext_q16, int64_t target_x_q16, uint32_t jet_steps,
                         uint64_t *io_seed, uint32_t *io_epoch, uint32_t max_iters,
                         searu_result *opt_out);
#define SEARU_SOLVER_API
#else
#define SEARU_SOLVER_API static inline
#endif

/* --- Layer 1: Branchless Pure-GPR Entropy, CLZ Power-Law & SWAR Primitives --- */

FLOW_FORCE_INL uint32_t searu_clz64(uint64_t x) {
    uint64_t nz = flow_mask_bool(x != UINT64_C(0));
#if defined(__GNUC__) || defined(__clang__)
    uint64_t c = (uint64_t)(uint32_t)__builtin_clzll(x | UINT64_C(1));
#else
    uint64_t v = x | UINT64_C(1);
    uint64_t c = UINT64_C(0);
    for (uint32_t b = 63U; b > 0U; --b) {
        uint64_t top = flow_mask_bool((v >> 63U) == UINT64_C(0));
        c += top & UINT64_C(1);
        v = flow_mux_u64(top, v << 1U, v);
    }
#endif
    return (uint32_t)flow_mux_u64(nz, c, UINT64_C(64));
}

FLOW_FORCE_INL uint32_t searu_popcnt64(uint64_t x) {
    x -= (x >> 1U) & UINT64_C(0x5555555555555555);
    x = (x & UINT64_C(0x3333333333333333)) + ((x >> 2U) & UINT64_C(0x3333333333333333));
    x = (x + (x >> 4U)) & UINT64_C(0x0F0F0F0F0F0F0F0F);
    return (uint32_t)((x * UINT64_C(0x0101010101010101)) >> 56U);
}

FLOW_FORCE_INL uint64_t searu_rng_next(uint64_t *state) {
    uint64_t z = (*state += UINT64_C(0x9E3779B97F4A7C15));
    z = (z ^ (z >> 30U)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27U)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31U);
}

/* Branchless Lemire fast range reduction: maps 32-bit uniform `r` into `[0, bound)` (0 udiv!) */
FLOW_FORCE_INL uint32_t searu_Bounded_u32(uint64_t r, uint32_t bound) {
    uint64_t safe_b = flow_mux_u64(flow_mask_bool(bound > 0U), (uint64_t)bound, UINT64_C(1));
    return (uint32_t)(((uint64_t)(uint32_t)r * safe_b) >> 32U);
}

/* Exact Pure-GPR Zipf / Lévy Flight step via hardware CLZ:
 * Pr(clz(U) >= m) = 2^-m  =>  Pr(2^((lz * chaos_q8) >> 8) >= X) = X^{-(256 / chaos_q8)}.
 * Zero floats, zero powf(), zero division, zero branches. */
FLOW_FORCE_INL uint64_t searu_zipf_jump(uint64_t *rng, uint32_t chaos_q8,
                                        uint64_t *out_black_swan_mask) {
    uint64_t u = searu_rng_next(rng);
    uint64_t c_lo = flow_mux_u64(flow_mask_bool(chaos_q8 < SEARU_CHAOS_MIN_Q8),
                                 (uint64_t)SEARU_CHAOS_MIN_Q8, (uint64_t)chaos_q8);
    uint64_t c_q8 =
        flow_mux_u64(flow_mask_bool(c_lo > SEARU_CHAOS_MAX_Q8), (uint64_t)SEARU_CHAOS_MAX_Q8, c_lo);
    uint64_t lz = (uint64_t)searu_clz64(u);
    uint64_t raw_exp = (lz * c_q8) >> 8U;
    uint64_t capped_exp =
        flow_mux_u64(flow_mask_bool(raw_exp > UINT64_C(20)), UINT64_C(20), raw_exp);
    *out_black_swan_mask = flow_mask_bool(capped_exp >= UINT64_C(6));
    return UINT64_C(1) << (uint32_t)(capped_exp & UINT64_C(31));
}

/* --- Layer 2: Sparse-k Cardinality Mutation & Frustration-Guided Bit Flip --- */

/* Adapts chaos_q8 smoothly from stagnation count and 2-Jet barrier frustration */
FLOW_FORCE_INL uint32_t searu_adapt_chaos_q8_hp(uint32_t stagnation, uint64_t barrier_frustrated,
                                                uint32_t c_min, uint32_t c_max, uint32_t stag_sh,
                                                uint32_t bar_boost) {
    uint64_t base = (uint64_t)c_min + ((uint64_t)stagnation << (stag_sh & 7U));
    uint64_t boost = barrier_frustrated & (uint64_t)bar_boost;
    uint64_t raw = base + boost;
    return (uint32_t)flow_mux_u64(flow_mask_bool(raw > (uint64_t)c_max), (uint64_t)c_max, raw);
}

FLOW_FORCE_INL uint32_t searu_adapt_chaos_q8(uint32_t stagnation, uint64_t barrier_frustrated) {
    return searu_adapt_chaos_q8_hp(stagnation, barrier_frustrated, SEARU_OPT_CHAOS_MIN_Q8,
                                   SEARU_OPT_CHAOS_MAX_Q8, SEARU_OPT_STAG_SHIFT,
                                   SEARU_OPT_BARRIER_BOOST_Q8);
}

/* Branchless single-bit mask generator across a 256-bit `uint64_t[4]` canvas */
FLOW_FORCE_INL void searu_one_hot256(uint64_t out_mask[4], uint32_t bit_idx) {
    uint32_t b = bit_idx & 255U;
    uint32_t w = b >> 6U;
    uint64_t bit = UINT64_C(1) << (b & 63U);
    out_mask[0] = bit & flow_mask_bool(w == 0U);
    out_mask[1] = bit & flow_mask_bool(w == 1U);
    out_mask[2] = bit & flow_mask_bool(w == 2U);
    out_mask[3] = bit & flow_mask_bool(w == 3U);
}

/* Branchless 64-byte Engram / Canvas single-step pulse (Ring-0 safe, 0 branches):
 * Engram cell layout (`uint64_t[8]`):
 *   [0..3] : 256-bit 1-bit TopologyCanvas
 *   [4]    : current energy
 *   [5]    : [black_swan_count:32 | stagnation:16 | chaos_q8:16]
 *   [6]    : SplitMix64 PRNG state
 *   [7]    : coupled Q16.16 coordinate / auxiliary payload */
SEARU_RING0_API uint64_t searu_ring0_step_1bit(uint64_t out_engram[8], const uint64_t in_engram[8],
                                               const uint64_t kan_pin[4],
                                               const uint64_t frustration[4]) {
    uint64_t rng = in_engram[6];
    uint32_t meta_lo = (uint32_t)(in_engram[5] & UINT64_C(0xFFFF));
    uint32_t stag = (uint32_t)((in_engram[5] >> 16U) & UINT64_C(0xFFFF));
    uint32_t swans = (uint32_t)(in_engram[5] >> 32U);

    uint64_t has_frust =
        flow_mask_bool((frustration[0] | frustration[1] | frustration[2] | frustration[3]) != 0ULL);
    uint32_t chaos_q8 = searu_adapt_chaos_q8(stag, has_frust);
    chaos_q8 = (uint32_t)flow_mux_u64(flow_mask_bool(meta_lo >= SEARU_CHAOS_MIN_Q8),
                                      (uint64_t)meta_lo, (uint64_t)chaos_q8);

    uint64_t swan_mask = UINT64_C(0);
    uint64_t jump = searu_zipf_jump(&rng, chaos_q8, &swan_mask);
    uint64_t r1 = searu_rng_next(&rng);
    uint64_t r2 = searu_rng_next(&rng);

    uint64_t hot1[4];
    uint64_t hot2[4];
    searu_one_hot256(hot1, (uint32_t)r1);
    searu_one_hot256(hot2, (uint32_t)(r1 >> 16U));

    /* Sparse-k: 1-opt default; 2-opt when jump >= 4; Black-Swan frustration chord when swan_mask */
    uint64_t m2 = flow_mask_bool(jump >= UINT64_C(4));
    uint64_t chord0 = (r2 & frustration[0]) | (hot1[0] ^ (hot2[0] & m2));
    uint64_t chord1 = (searu_rng_next(&rng) & frustration[1]) | (hot1[1] ^ (hot2[1] & m2));
    uint64_t chord2 = (searu_rng_next(&rng) & frustration[2]) | (hot1[2] ^ (hot2[2] & m2));
    uint64_t chord3 = (searu_rng_next(&rng) & frustration[3]) | (hot1[3] ^ (hot2[3] & m2));

    uint64_t f0 = flow_mux_u64(swan_mask, chord0 ^ r1, hot1[0] | (hot2[0] & m2)) & ~kan_pin[0];
    uint64_t f1 =
        flow_mux_u64(swan_mask, chord1 ^ (r1 >> 7U), hot1[1] | (hot2[1] & m2)) & ~kan_pin[1];
    uint64_t f2 =
        flow_mux_u64(swan_mask, chord2 ^ (r2 >> 13U), hot1[2] | (hot2[2] & m2)) & ~kan_pin[2];
    uint64_t f3 =
        flow_mux_u64(swan_mask, chord3 ^ (r2 >> 29U), hot1[3] | (hot2[3] & m2)) & ~kan_pin[3];

    out_engram[0] = in_engram[0] ^ f0;
    out_engram[1] = in_engram[1] ^ f1;
    out_engram[2] = in_engram[2] ^ f2;
    out_engram[3] = in_engram[3] ^ f3;
    out_engram[4] = in_engram[4];
    swans += (uint32_t)(swan_mask & UINT64_C(1));
    out_engram[5] =
        ((uint64_t)swans << 32U) | ((uint64_t)(stag & 0xFFFFU) << 16U) | (uint64_t)chaos_q8;
    out_engram[6] = rng;
    out_engram[7] = in_engram[7];
    return jump;
}

/* Ring-0 single-step Bilevel Coupling (0 branches):
 * Mutates 4 discrete topology switches via SEARU and executes one exact FLOW 2-Jet step. */
SEARU_RING0_API uint64_t searu_ring0_bilevel_step(uint64_t out_state[8],
                                                  const uint64_t cur_state[8],
                                                  const uint64_t base_bound[8],
                                                  const searu_bilevel_term terms[4],
                                                  uint64_t *rng_io, uint64_t *bits_io) {
    uint64_t swan = UINT64_C(0);
    uint64_t jump = searu_zipf_jump(rng_io, SEARU_CHAOS_DEFAULT_Q8, &swan);
    uint64_t r = searu_rng_next(rng_io);
    uint64_t one_bit = UINT64_C(1) << (uint32_t)(r & UINT64_C(3));
    uint64_t flip = flow_mux_u64(swan, (r & UINT64_C(0xF)) | one_bit, one_bit);
    uint64_t cand_bits = (*bits_io ^ flip) & UINT64_C(0xF);

    int64_t b0 = (int64_t)base_bound[0];
    int64_t b1 = (int64_t)base_bound[1];
    int64_t b2 = (int64_t)base_bound[2];
    uint64_t w_cost = UINT64_C(0);

    for (uint32_t i = 0U; i < 4U; ++i) {
        uint64_t m = flow_mask_bool(((cand_bits >> i) & UINT64_C(1)) != UINT64_C(0));
        b0 += (int64_t)((uint64_t)terms[i].db0_q16 & m);
        b1 += (int64_t)((uint64_t)terms[i].db1_q16 & m);
        b2 += (int64_t)((uint64_t)terms[i].db2_q16 & m);
        w_cost += terms[i].weight_cost & m;
    }

    uint64_t chart[8];
    chart[0] = (uint64_t)flow_clamp_sym(b0, FLOW_MAX_Q16);
    chart[1] = (uint64_t)flow_clamp_sym(b1, FLOW_MAX_Q16);
    chart[2] = (uint64_t)flow_clamp_sym(b2, FLOW_MAX_Q16);
    chart[3] = base_bound[3];
    chart[4] = base_bound[4];
    chart[5] = base_bound[5];
    chart[6] = base_bound[6];
    chart[7] = UINT64_C(0);

    uint64_t cand_out[8];
    flow_step(cand_out, cur_state, chart);

    uint64_t cur_e = flow_abs_i64((int64_t)cur_state[0]);
    uint64_t cand_e = flow_abs_i64((int64_t)cand_out[0]) + w_cost + (jump & UINT64_C(0));
    uint64_t accept = flow_mask_bool(cand_e <= cur_e);

    *bits_io = flow_mux_u64(accept, cand_bits, *bits_io);
    out_state[0] = flow_mux_u64(accept, cand_out[0], cur_state[0]);
    out_state[1] = flow_mux_u64(accept, cand_out[1], cur_state[1]);
    out_state[2] = flow_mux_u64(accept, cand_out[2], cur_state[2]);
    out_state[3] = flow_mux_u64(accept, cand_out[3], cur_state[3]);
    out_state[4] = flow_mux_u64(accept, cand_out[4], cur_state[4]);
    out_state[5] = flow_mux_u64(accept, cand_out[5], cur_state[5]);
    out_state[6] = flow_mux_u64(accept, cand_out[6], cur_state[6]);
    out_state[7] = flow_mux_u64(accept, cand_out[7], cur_state[7]);
    return flow_mux_u64(accept, cand_e, cur_e);
}

/* --- Layer 3: Pure-GPR Crucible Acceptance & Frustration Bit Selection --- */

/* Selects a single bit index in `[0, num_vars)` biased toward `frustration[4]` */
FLOW_FORCE_INL uint32_t searu_pick_var_hp(uint64_t *rng, uint32_t num_vars,
                                          const uint64_t frustration[4], const uint64_t kan_pin[4],
                                          uint32_t rand_walk_mask) {
    uint64_t r = searu_rng_next(rng);
    uint32_t fallback = searu_Bounded_u32(r, num_vars);
    if ((r & (uint64_t)rand_walk_mask) == UINT64_C(0)) {
        return fallback;
    }
    uint32_t start_w = (uint32_t)((r >> 32U) & UINT64_C(3));
    for (uint32_t k = 0U; k < 4U; ++k) {
        uint32_t w = (start_w + k) & 3U;
        uint64_t f_word = frustration[w] & ~kan_pin[w];
        if (f_word != UINT64_C(0)) {
            uint32_t rot = (uint32_t)((r >> 40U) & UINT64_C(63));
            uint64_t rw = (f_word >> rot) | (f_word << ((64U - rot) & 63U));
            uint32_t lead = 63U - searu_clz64(rw);
            uint32_t bit_in_w = (lead + rot) & 63U;
            uint32_t idx = (w << 6U) | bit_in_w;
            if (idx < num_vars) {
                return idx;
            }
        }
    }
    return fallback;
}

FLOW_FORCE_INL uint32_t searu_pick_var(uint64_t *rng, uint32_t num_vars,
                                       const uint64_t frustration[4], const uint64_t kan_pin[4]) {
    return searu_pick_var_hp(rng, num_vars, frustration, kan_pin, SEARU_OPT_RAND_WALK_MASK);
}

/* Pure-GPR integer Metropolis-Tsallis acceptance with fixed-sign Epiphany reheating:
 * Minimizes `energy` strictly (`cand_e < best_e` = breakthrough!). */
FLOW_FORCE_INL bool searu_crucible_accept(uint64_t cand_e, uint64_t cur_e, uint64_t best_e,
                                          uint64_t *temp_q12, uint64_t init_temp_q12,
                                          uint64_t is_black_swan, uint64_t *rng,
                                          bool *out_epiphany) {
    *out_epiphany = false;
    if (cand_e < cur_e) {
        if (cand_e < best_e && is_black_swan != UINT64_C(0)) {
            *out_epiphany = true;
            uint64_t reheated = *temp_q12 << 2U;
            *temp_q12 =
                flow_mux_u64(flow_mask_bool(reheated > init_temp_q12), init_temp_q12, reheated);
        }
        return true;
    }
    uint64_t delta = cand_e - cur_e;
    if (*temp_q12 == UINT64_C(0) || delta > (*temp_q12 >> 4U) + UINT64_C(1)) {
        return false;
    }
    uint64_t u16 = searu_rng_next(rng) & UINT64_C(0xFFFF);
    /* Integer rational Boltzmann bound: 1 / (1 + (delta << 12) / temp_q12) > u16 / 65536 */
    uint64_t lhs = *temp_q12 << 16U;
    uint64_t rhs = (*temp_q12 + (delta << 12U)) * (u16 + UINT64_C(1));
    return lhs > rhs;
}

/* --- Layer 4: General Pure-GPR 3-SAT, Graph Coloring & Bilevel 2-Jet Solvers --- */

FLOW_FORCE_INL uint64_t searu_eval_3sat(const uint64_t canvas[4], const uint32_t *clauses,
                                        uint32_t num_clauses, uint64_t out_frust[4]) {
    out_frust[0] = UINT64_C(0);
    out_frust[1] = UINT64_C(0);
    out_frust[2] = UINT64_C(0);
    out_frust[3] = UINT64_C(0);
    uint64_t unsat = UINT64_C(0);

    for (uint32_t i = 0U; i < num_clauses; ++i) {
        uint32_t c = clauses[i];
        uint32_t v0 = c & 0xFFU;
        uint32_t v1 = (c >> 8U) & 0xFFU;
        uint32_t v2 = (c >> 16U) & 0xFFU;
        uint32_t n0 = (c >> 24U) & 1U;
        uint32_t n1 = (c >> 25U) & 1U;
        uint32_t n2 = (c >> 26U) & 1U;

        uint32_t b0 = (uint32_t)((canvas[v0 >> 6U] >> (v0 & 63U)) & UINT64_C(1)) ^ n0;
        uint32_t b1 = (uint32_t)((canvas[v1 >> 6U] >> (v1 & 63U)) & UINT64_C(1)) ^ n1;
        uint32_t b2 = (uint32_t)((canvas[v2 >> 6U] >> (v2 & 63U)) & UINT64_C(1)) ^ n2;

        if ((b0 | b1 | b2) == 0U) {
            ++unsat;
            out_frust[v0 >> 6U] |= UINT64_C(1) << (v0 & 63U);
            out_frust[v1 >> 6U] |= UINT64_C(1) << (v1 & 63U);
            out_frust[v2 >> 6U] |= UINT64_C(1) << (v2 & 63U);
        }
    }
    return unsat;
}

FLOW_INL uint64_t searu_solve_3sat_hp(searu_result *out, const uint32_t *clauses,
                                      uint32_t num_clauses, uint32_t num_vars,
                                      const uint64_t init_canvas[4], const uint64_t kan_pin[4],
                                      uint64_t seed, uint32_t max_iters,
                                      const searu_hyperparams *hp) {
    uint64_t pin[4];
    pin[0] = (kan_pin != NULL) ? kan_pin[0] : UINT64_C(0);
    pin[1] = (kan_pin != NULL) ? kan_pin[1] : UINT64_C(0);
    pin[2] = (kan_pin != NULL) ? kan_pin[2] : UINT64_C(0);
    pin[3] = (kan_pin != NULL) ? kan_pin[3] : UINT64_C(0);

    uint64_t cur[4];
    cur[0] = (init_canvas != NULL) ? init_canvas[0] : UINT64_C(0);
    cur[1] = (init_canvas != NULL) ? init_canvas[1] : UINT64_C(0);
    cur[2] = (init_canvas != NULL) ? init_canvas[2] : UINT64_C(0);
    cur[3] = (init_canvas != NULL) ? init_canvas[3] : UINT64_C(0);

    uint32_t nvars = (num_vars > SEARU_CANVAS_BITS) ? SEARU_CANVAS_BITS : num_vars;
    uint64_t rng = seed | UINT64_C(1);
    uint64_t frust[4];
    uint64_t cur_e = searu_eval_3sat(cur, clauses, num_clauses, frust);

    uint64_t best[4];
    best[0] = cur[0];
    best[1] = cur[1];
    best[2] = cur[2];
    best[3] = cur[3];
    uint64_t best_e = cur_e;
    uint64_t init_e = cur_e;

    uint64_t init_temp = hp->sat_init_temp_q12;
    uint64_t temp = init_temp;
    uint32_t stag = 0U;
    uint32_t epiphanies = 0U;
    uint32_t swans = 0U;
    uint32_t iter = 0U;

    for (; iter < max_iters && best_e > UINT64_C(0) && nvars > 0U; ++iter) {
        uint32_t chaos_q8 =
            searu_adapt_chaos_q8_hp(stag, UINT64_C(0), hp->chaos_min_q8, hp->chaos_max_q8,
                                    hp->stag_shift, hp->barrier_boost_q8);
        uint64_t swan_m = UINT64_C(0);
        uint64_t jump = searu_zipf_jump(&rng, chaos_q8, &swan_m);
        swans += (uint32_t)(swan_m & UINT64_C(1));

        /* Sparse-k cardinality: 1 flip normally, up to 6 coupled flips on Black Swan */
        uint32_t k_flips = 1U;
        if (swan_m != UINT64_C(0)) {
            k_flips = 2U + (uint32_t)(searu_clz64(jump) & 3U);
        } else if (jump >= UINT64_C(2)) {
            k_flips = 2U;
        }

        uint64_t cand[4];
        cand[0] = cur[0];
        cand[1] = cur[1];
        cand[2] = cur[2];
        cand[3] = cur[3];

        for (uint32_t f = 0U; f < k_flips; ++f) {
            uint32_t v = searu_pick_var_hp(&rng, nvars, frust, pin, hp->rand_walk_mask);
            uint32_t w = v >> 6U;
            uint64_t bit = (UINT64_C(1) << (v & 63U)) & ~pin[w];
            cand[w] ^= bit;
        }

        uint64_t cand_frust[4];
        uint64_t cand_e = searu_eval_3sat(cand, clauses, num_clauses, cand_frust);
        bool epi = false;
        if (searu_crucible_accept(cand_e, cur_e, best_e, &temp, init_temp, swan_m, &rng, &epi)) {
            cur[0] = cand[0];
            cur[1] = cand[1];
            cur[2] = cand[2];
            cur[3] = cand[3];
            frust[0] = cand_frust[0];
            frust[1] = cand_frust[1];
            frust[2] = cand_frust[2];
            frust[3] = cand_frust[3];
            cur_e = cand_e;
            if (epi) {
                ++epiphanies;
            }
            if (cand_e < best_e) {
                best[0] = cand[0];
                best[1] = cand[1];
                best[2] = cand[2];
                best[3] = cand[3];
                best_e = cand_e;
                stag = 0U;
            } else {
                ++stag;
            }
        } else {
            ++stag;
        }
        temp = (temp * hp->sat_cool_q16) >> 16U;
        if (temp < UINT64_C(64) && stag > hp->sat_reheat_stag) {
            temp = hp->sat_reheat_q12;
        }
    }

    if (out != NULL) {
        out->canvas[0] = best[0];
        out->canvas[1] = best[1];
        out->canvas[2] = best[2];
        out->canvas[3] = best[3];
        out->best_energy = best_e;
        out->init_energy = init_e;
        out->best_x_q16 = INT64_C(0);
        out->iterations_run = iter;
        out->epiphanies = epiphanies;
        out->black_swans = swans;
        out->converged_2jet = (best_e == UINT64_C(0)) ? 1U : 0U;
        out->replay_seed = seed | UINT64_C(1);
    }
    return best_e;
}

SEARU_SOLVER_API uint64_t searu_solve_3sat(searu_result *out, const uint32_t *clauses,
                                           uint32_t num_clauses, uint32_t num_vars,
                                           const uint64_t init_canvas[4], const uint64_t kan_pin[4],
                                           uint64_t seed, uint32_t max_iters) {
    const searu_hyperparams hp = SEARU_ACTIVE_OPT_HP;
    return searu_solve_3sat_hp(out, clauses, num_clauses, num_vars, init_canvas, kan_pin, seed,
                               max_iters, &hp);
}

FLOW_FORCE_INL uint32_t searu_color_get(const uint64_t canvas[4], uint32_t node) {
    uint32_t u = node & 63U;
    return (uint32_t)((canvas[u >> 4U] >> ((u & 15U) << 2U)) & UINT64_C(0xF));
}

FLOW_FORCE_INL void searu_color_set(uint64_t canvas[4], uint32_t node, uint32_t color) {
    uint32_t u = node & 63U;
    uint32_t w = u >> 4U;
    uint32_t sh = (u & 15U) << 2U;
    canvas[w] = (canvas[w] & ~(UINT64_C(0xF) << sh)) | ((uint64_t)(color & 0xFU) << sh);
}

FLOW_FORCE_INL uint64_t searu_eval_graph_color(const uint64_t canvas[4], const uint16_t *edges,
                                               uint32_t num_edges, uint32_t num_nodes,
                                               uint32_t num_colors, uint64_t *out_conflict_nodes) {
    uint64_t conflicts = UINT64_C(0);
    uint64_t bad_mask = UINT64_C(0);

    for (uint32_t u = 0U; u < num_nodes; ++u) {
        if (searu_color_get(canvas, u) >= num_colors) {
            ++conflicts;
            bad_mask |= UINT64_C(1) << u;
        }
    }
    for (uint32_t e = 0U; e < num_edges; ++e) {
        uint32_t u = ((uint32_t)edges[e] >> 8U) & 63U;
        uint32_t v = (uint32_t)edges[e] & 63U;
        if (u < num_nodes && v < num_nodes && u != v) {
            if (searu_color_get(canvas, u) == searu_color_get(canvas, v)) {
                ++conflicts;
                bad_mask |= (UINT64_C(1) << u) | (UINT64_C(1) << v);
            }
        }
    }
    *out_conflict_nodes = bad_mask;
    return conflicts;
}

FLOW_INL uint64_t searu_solve_graph_color_hp(searu_result *out, const uint16_t *edges,
                                             uint32_t num_edges, uint32_t num_nodes,
                                             uint32_t num_colors, const uint64_t init_canvas[4],
                                             uint64_t kan_node_pin, uint64_t seed,
                                             uint32_t max_iters, const searu_hyperparams *hp) {
    uint32_t nnodes = (num_nodes > SEARU_GRAPH_MAX_NODES) ? SEARU_GRAPH_MAX_NODES : num_nodes;
    uint32_t ncolors = (num_colors < 2U) ? 2U : ((num_colors > 16U) ? 16U : num_colors);

    uint64_t cur[4];
    cur[0] = (init_canvas != NULL) ? init_canvas[0] : UINT64_C(0);
    cur[1] = (init_canvas != NULL) ? init_canvas[1] : UINT64_C(0);
    cur[2] = (init_canvas != NULL) ? init_canvas[2] : UINT64_C(0);
    cur[3] = (init_canvas != NULL) ? init_canvas[3] : UINT64_C(0);

    for (uint32_t u = 0U; u < nnodes; ++u) {
        if (((kan_node_pin >> u) & UINT64_C(1)) == UINT64_C(0) &&
            searu_color_get(cur, u) >= ncolors) {
            searu_color_set(cur, u, 0U);
        }
    }

    uint64_t rng = seed | UINT64_C(1);
    uint64_t bad_nodes = UINT64_C(0);
    uint64_t cur_e = searu_eval_graph_color(cur, edges, num_edges, nnodes, ncolors, &bad_nodes);

    uint64_t best[4];
    best[0] = cur[0];
    best[1] = cur[1];
    best[2] = cur[2];
    best[3] = cur[3];
    uint64_t best_e = cur_e;
    uint64_t init_e = cur_e;

    uint64_t frust256[4] = {bad_nodes, UINT64_C(0), UINT64_C(0), UINT64_C(0)};
    uint64_t pin256[4] = {kan_node_pin, UINT64_C(0), UINT64_C(0), UINT64_C(0)};

    uint64_t init_temp = hp->col_init_temp_q12;
    uint64_t temp = init_temp;
    uint32_t stag = 0U;
    uint32_t epiphanies = 0U;
    uint32_t swans = 0U;
    uint32_t iter = 0U;

    for (; iter < max_iters && best_e > UINT64_C(0) && nnodes > 0U; ++iter) {
        uint32_t chaos_q8 =
            searu_adapt_chaos_q8_hp(stag, UINT64_C(0), hp->chaos_min_q8, hp->chaos_max_q8,
                                    hp->stag_shift, hp->barrier_boost_q8);
        uint64_t swan_m = UINT64_C(0);
        uint64_t jump = searu_zipf_jump(&rng, chaos_q8, &swan_m);
        swans += (uint32_t)(swan_m & UINT64_C(1));

        uint32_t k_recolor = (swan_m != UINT64_C(0)) ? 3U : ((jump >= UINT64_C(8)) ? 2U : 1U);
        uint64_t cand[4];
        cand[0] = cur[0];
        cand[1] = cur[1];
        cand[2] = cur[2];
        cand[3] = cur[3];

        for (uint32_t f = 0U; f < k_recolor; ++f) {
            frust256[0] = bad_nodes;
            uint32_t u = searu_pick_var_hp(&rng, nnodes, frust256, pin256, hp->col_walk_mask);
            if (((kan_node_pin >> u) & UINT64_C(1)) == UINT64_C(0)) {
                uint32_t old_c = searu_color_get(cand, u);
                uint32_t step_c = 1U + searu_Bounded_u32(searu_rng_next(&rng), ncolors - 1U);
                uint32_t new_c = old_c + step_c;
                if (new_c >= ncolors) {
                    new_c -= ncolors;
                }
                searu_color_set(cand, u, new_c);
            }
        }

        uint64_t cand_bad = UINT64_C(0);
        uint64_t cand_e =
            searu_eval_graph_color(cand, edges, num_edges, nnodes, ncolors, &cand_bad);
        bool epi = false;
        if (searu_crucible_accept(cand_e, cur_e, best_e, &temp, init_temp, swan_m, &rng, &epi)) {
            cur[0] = cand[0];
            cur[1] = cand[1];
            cur[2] = cand[2];
            cur[3] = cand[3];
            bad_nodes = cand_bad;
            cur_e = cand_e;
            if (epi) {
                ++epiphanies;
            }
            if (cand_e < best_e) {
                best[0] = cand[0];
                best[1] = cand[1];
                best[2] = cand[2];
                best[3] = cand[3];
                best_e = cand_e;
                stag = 0U;
            } else {
                ++stag;
            }
        } else {
            ++stag;
        }
        temp = (temp * hp->col_cool_q16) >> 16U;
        if (temp < UINT64_C(64) && stag > hp->col_reheat_stag) {
            temp = hp->col_reheat_q12;
        }
    }

    if (out != NULL) {
        out->canvas[0] = best[0];
        out->canvas[1] = best[1];
        out->canvas[2] = best[2];
        out->canvas[3] = best[3];
        out->best_energy = best_e;
        out->init_energy = init_e;
        out->best_x_q16 = INT64_C(0);
        out->iterations_run = iter;
        out->epiphanies = epiphanies;
        out->black_swans = swans;
        out->converged_2jet = (best_e == UINT64_C(0)) ? 1U : 0U;
        out->replay_seed = seed | UINT64_C(1);
    }
    return best_e;
}

SEARU_SOLVER_API uint64_t searu_solve_graph_color(searu_result *out, const uint16_t *edges,
                                                  uint32_t num_edges, uint32_t num_nodes,
                                                  uint32_t num_colors,
                                                  const uint64_t init_canvas[4],
                                                  uint64_t kan_node_pin, uint64_t seed,
                                                  uint32_t max_iters) {
    const searu_hyperparams hp = SEARU_ACTIVE_OPT_HP;
    return searu_solve_graph_color_hp(out, edges, num_edges, num_nodes, num_colors, init_canvas,
                                      kan_node_pin, seed, max_iters, &hp);
}

/* Evaluates one discrete bitvector `bits` by assembling the induced 2-Jet chart and running
 * FLOW's exact 128-bit Citardauq / Lyapunov solver for `jet_steps` deterministic steps. */
FLOW_FORCE_INL uint64_t searu_eval_bilevel_2jet(uint64_t bits, const uint64_t base_bound[8],
                                                const searu_bilevel_term *terms, uint32_t num_terms,
                                                int64_t init_x_q16, int64_t u_ext_q16,
                                                int64_t target_x_q16, uint32_t jet_steps,
                                                uint64_t out_chart[8], int64_t *out_x_q16,
                                                uint32_t *out_converged, uint64_t *out_frust_bits) {
    int64_t b0 = (int64_t)base_bound[0];
    int64_t b1 = (int64_t)base_bound[1];
    int64_t b2 = (int64_t)base_bound[2];
    uint64_t disc_cost = UINT64_C(0);

    for (uint32_t i = 0U; i < num_terms; ++i) {
        if (((bits >> i) & UINT64_C(1)) != UINT64_C(0)) {
            b0 += terms[i].db0_q16;
            b1 += terms[i].db1_q16;
            b2 += terms[i].db2_q16;
            disc_cost += terms[i].weight_cost;
        }
    }

    FLOW_ALIGN64 flow_cell arena[1];
    arena[0][0] = (uint64_t)flow_clamp_sym(b0, FLOW_MAX_Q16);
    arena[0][1] = (uint64_t)flow_clamp_sym(b1, FLOW_MAX_Q16);
    arena[0][2] = (uint64_t)flow_clamp_sym(b2, FLOW_MAX_Q16);
    arena[0][3] = base_bound[3];
    arena[0][4] = base_bound[4];
    arena[0][5] = base_bound[5] & UINT64_C(0xFFFF);
    arena[0][6] = UINT64_C(1) << 32U;
    arena[0][7] = UINT64_C(0);

    out_chart[0] = arena[0][0];
    out_chart[1] = arena[0][1];
    out_chart[2] = arena[0][2];
    out_chart[3] = arena[0][3];
    out_chart[4] = arena[0][4];
    out_chart[5] = base_bound[5];
    out_chart[6] = base_bound[6];
    out_chart[7] = UINT64_C(0);

    uint64_t st[8];
    st[0] = UINT64_C(0);
    st[1] = UINT64_C(0);
    st[2] = UINT64_C(0);
    st[3] = (uint64_t)flow_clamp_sym(init_x_q16, FLOW_MAX_Q16);
    st[4] = UINT64_C(0);
    st[5] = base_bound[5] & UINT64_C(0xFFFF);
    st[6] = UINT64_C(0);
    st[7] = UINT64_C(0);

    uint16_t mode_req = (uint16_t)(st[5] & UINT64_C(0xFFFF));
    uint32_t steps = (jet_steps == 0U) ? 1U : jet_steps;
    for (uint32_t s = 0U; s < steps; ++s) {
        flow_step_atlas(st, st, arena, UINT64_C(0), u_ext_q16, mode_req);
    }

    int64_t j0_post = (int64_t)st[0];
    int64_t x_post = (int64_t)st[3];
    uint32_t conv = (uint32_t)(st[6] & (uint64_t)FLOW_STATUS_CONVERGED);

    uint64_t res_mag = flow_abs_i64(j0_post);
    uint64_t x_gap = flow_abs_i64(x_post - target_x_q16);
    uint64_t barrier_pen = (conv == 0U) ? (UINT64_C(1) << 20U) : UINT64_C(0);

    /* 2-Jet + Implicit-Root Frustration Compass:
     * - Phase 1 (|j0_post| > 0, e.g. Delta < 0 barrier): j0_post sign pulls parabola to root locus.
     * - Phase 2 (j0_post == 0, converged root): Implicit Function Theorem sign (target_x - x_post)
     *   steers 2-opt swaps along the root manifold straight onto target_x_q16. */
    uint64_t f_bits = UINT64_C(0);
    int64_t signed_err = (j0_post != INT64_C(0)) ? j0_post : (x_post - target_x_q16);
    if (signed_err != INT64_C(0)) {
        for (uint32_t i = 0U; i < num_terms; ++i) {
            bool on = (((bits >> i) & UINT64_C(1)) != UINT64_C(0));
            bool helps = (signed_err > 0) ? (on ? (terms[i].db0_q16 > 0) : (terms[i].db0_q16 < 0))
                                          : (on ? (terms[i].db0_q16 < 0) : (terms[i].db0_q16 > 0));
            if (helps) {
                f_bits |= UINT64_C(1) << i;
            }
        }
    }

    *out_x_q16 = x_post;
    *out_converged = conv;
    *out_frust_bits = f_bits;
    return res_mag + x_gap + barrier_pen + disc_cost;
}

/* Symbiotic SEARU x FLOW 2-Jet Bilevel Co-Solver:
 * Outer SEARU navigates discrete 2^N topologies via Sparse-k CLZ Zipf jumps;
 * Inner FLOW 2-Jet collapses continuous Q16.16 coordinates to exact Citardauq roots. */
FLOW_INL uint64_t searu_solve_bilevel_2jet_hp(searu_result *out, uint64_t out_best_chart[8],
                                              const uint64_t base_bound[8],
                                              const searu_bilevel_term *terms, uint32_t num_terms,
                                              uint64_t kan_bit_pin, int64_t init_x_q16,
                                              int64_t u_ext_q16, int64_t target_x_q16,
                                              uint32_t jet_steps, uint64_t seed, uint32_t max_iters,
                                              const searu_hyperparams *hp) {
    uint32_t nterms = (num_terms > SEARU_BILEVEL_MAX_VARS) ? SEARU_BILEVEL_MAX_VARS : num_terms;
    uint64_t valid_mask = (nterms == 64U)
                              ? ~UINT64_C(0)
                              : ((nterms == 0U) ? UINT64_C(0) : ((UINT64_C(1) << nterms) - 1U));
    uint64_t cur_bits = kan_bit_pin & valid_mask;

    uint64_t cur_chart[8];
    int64_t cur_x = init_x_q16;
    uint32_t cur_conv = 0U;
    uint64_t frust_bits = UINT64_C(0);
    uint64_t cur_e =
        searu_eval_bilevel_2jet(cur_bits, base_bound, terms, nterms, init_x_q16, u_ext_q16,
                                target_x_q16, jet_steps, cur_chart, &cur_x, &cur_conv, &frust_bits);

    uint64_t best_bits = cur_bits;
    uint64_t best_e = cur_e;
    uint64_t init_e = cur_e;
    int64_t best_x = cur_x;
    uint32_t best_conv = cur_conv;
    uint64_t best_chart[8];
    for (uint32_t l = 0U; l < 8U; ++l) {
        best_chart[l] = cur_chart[l];
    }

    uint64_t frust256[4] = {frust_bits, UINT64_C(0), UINT64_C(0), UINT64_C(0)};
    uint64_t pin256[4] = {kan_bit_pin, UINT64_C(0), UINT64_C(0), UINT64_C(0)};

    uint64_t rng = seed | UINT64_C(1);
    uint64_t init_temp = hp->bil_init_temp_q12;
    uint64_t temp = init_temp;
    uint32_t stag = 0U;
    uint32_t epiphanies = 0U;
    uint32_t swans = 0U;
    uint32_t iter = 0U;

    for (; iter < max_iters && best_e > UINT64_C(0) && nterms > 0U; ++iter) {
        uint64_t barrier_frust = flow_mask_bool(cur_conv == 0U);
        uint32_t chaos_q8 =
            searu_adapt_chaos_q8_hp(stag, barrier_frust, hp->chaos_min_q8, hp->chaos_max_q8,
                                    hp->stag_shift, hp->barrier_boost_q8);
        uint64_t swan_m = UINT64_C(0);
        uint64_t jump = searu_zipf_jump(&rng, chaos_q8, &swan_m);
        swans += (uint32_t)(swan_m & UINT64_C(1));

        uint32_t k_flips = 1U;
        if (swan_m != UINT64_C(0)) {
            k_flips = 2U + (uint32_t)(searu_clz64(jump) & 3U);
        } else if (jump >= UINT64_C(2)) {
            k_flips = 2U;
        }

        uint64_t cand_bits = cur_bits;
        frust256[0] = frust_bits;
        for (uint32_t f = 0U; f < k_flips; ++f) {
            uint32_t v = searu_pick_var_hp(&rng, nterms, frust256, pin256, hp->bil_walk_mask);
            cand_bits ^= (UINT64_C(1) << v) & ~kan_bit_pin & valid_mask;
        }

        uint64_t cand_chart[8];
        int64_t cand_x = init_x_q16;
        uint32_t cand_conv = 0U;
        uint64_t cand_frust = UINT64_C(0);
        uint64_t cand_e = searu_eval_bilevel_2jet(cand_bits, base_bound, terms, nterms, init_x_q16,
                                                  u_ext_q16, target_x_q16, jet_steps, cand_chart,
                                                  &cand_x, &cand_conv, &cand_frust);

        bool epi = false;
        if (searu_crucible_accept(cand_e, cur_e, best_e, &temp, init_temp, swan_m, &rng, &epi)) {
            cur_bits = cand_bits;
            cur_e = cand_e;
            cur_x = cand_x;
            cur_conv = cand_conv;
            frust_bits = cand_frust;
            if (epi) {
                ++epiphanies;
            }
            if (cand_e < best_e) {
                best_bits = cand_bits;
                best_e = cand_e;
                best_x = cand_x;
                best_conv = cand_conv;
                for (uint32_t l = 0U; l < 8U; ++l) {
                    best_chart[l] = cand_chart[l];
                }
                stag = 0U;
            } else {
                ++stag;
            }
        } else {
            ++stag;
        }
        temp = (temp * hp->bil_cool_q16) >> 16U;
        if (temp < UINT64_C(1024) && stag > hp->bil_reheat_stag) {
            cur_bits = best_bits;
            cur_e = best_e;
            cur_x = best_x;
            cur_conv = best_conv;
            temp = hp->bil_reheat_q12;
            stag = 16U;
        }
    }

    if (out_best_chart != NULL) {
        for (uint32_t l = 0U; l < 8U; ++l) {
            out_best_chart[l] = best_chart[l];
        }
    }
    if (out != NULL) {
        out->canvas[0] = best_bits;
        out->canvas[1] = UINT64_C(0);
        out->canvas[2] = UINT64_C(0);
        out->canvas[3] = UINT64_C(0);
        out->best_energy = best_e;
        out->init_energy = init_e;
        out->best_x_q16 = best_x;
        out->iterations_run = iter;
        out->epiphanies = epiphanies;
        out->black_swans = swans;
        out->converged_2jet = best_conv;
        out->replay_seed = seed | UINT64_C(1);
    }
    return best_e;
}

SEARU_SOLVER_API uint64_t searu_solve_bilevel_2jet(searu_result *out, uint64_t out_best_chart[8],
                                                   const uint64_t base_bound[8],
                                                   const searu_bilevel_term *terms,
                                                   uint32_t num_terms, uint64_t kan_bit_pin,
                                                   int64_t init_x_q16, int64_t u_ext_q16,
                                                   int64_t target_x_q16, uint32_t jet_steps,
                                                   uint64_t seed, uint32_t max_iters) {
    const searu_hyperparams hp = SEARU_ACTIVE_OPT_HP;
    return searu_solve_bilevel_2jet_hp(out, out_best_chart, base_bound, terms, num_terms,
                                       kan_bit_pin, init_x_q16, u_ext_q16, target_x_q16, jet_steps,
                                       seed, max_iters, &hp);
}

/* Atomically publishes a SEARU-synthesized 2-Jet chart into a live multi-core FLOW arena */
SEARU_SOLVER_API bool searu_flow_commit_chart(flow_cell *arena, uint64_t slot, uint64_t cap_mask,
                                              uint32_t new_epoch, const uint64_t best_chart[8]) {
    return flow_arena_try_swap(arena, slot, cap_mask, new_epoch, best_chart);
}

/* --- Layer 5: Zero-Friction Application-Layer Dual-Solver Ergonomics --- */

/* Phase-Gated Orthogonal Self-Healing (`searu_heal_and_swap`):
 * - O(1) zero-chaos no-op whenever FLOW 2-Jet is converged (`FLOW_STATUS_CONVERGED`)
 *   OR actively moving (`state[4] == dt_resolved > 0`).
 * - Wakes SEARU x FLOW 2-Jet bilevel synthesis ONLY when trapped at a topological /
 *   discriminant barrier (`!CONVERGED && dt_resolved == 0`), and atomically commits
 *   the healed chart via lock-free `@swap` (`searu_flow_commit_chart`). */
SEARU_SOLVER_API bool searu_heal_and_swap(flow_cell *arena, uint64_t cap_mask,
                                          const uint64_t state[8], const searu_bilevel_term *terms,
                                          uint32_t num_terms, uint64_t kan_bit_pin,
                                          int64_t u_ext_q16, int64_t target_x_q16,
                                          uint32_t jet_steps, uint64_t *io_seed, uint32_t *io_epoch,
                                          uint32_t max_iters, searu_result *opt_out) {
    if (((state[6] & (uint64_t)FLOW_STATUS_CONVERGED) != UINT64_C(0)) ||
        (state[4] != UINT64_C(0))) {
        return false;
    }

    uint64_t slot = (state[5] >> 16U) & cap_mask;
    uint64_t base_bound[8];
    flow_arena_load_acq(base_bound, arena, slot, cap_mask);
    if ((uint32_t)(base_bound[6] & UINT64_C(0xFFFFFFFF)) == FLOW_SWAP_LOCK_TAG) {
        return false;
    }

    uint64_t seed = (io_seed != NULL) ? *io_seed : UINT64_C(0x9E3779B97F4A7C15);
    if (io_seed != NULL) {
        (void)searu_rng_next(io_seed);
    }

    searu_result local_res;
    searu_result *res_p = (opt_out != NULL) ? opt_out : &local_res;
    uint64_t best_chart[8];
    (void)searu_solve_bilevel_2jet(res_p, best_chart, base_bound, terms, num_terms, kan_bit_pin,
                                   (int64_t)state[3], u_ext_q16, target_x_q16, jet_steps, seed,
                                   max_iters);
    if (res_p->converged_2jet == 0U) {
        return false;
    }

    uint32_t cur_ep = (uint32_t)(base_bound[6] >> 32U);
    uint32_t next_ep = cur_ep + 1U;
    if (io_epoch != NULL) {
        if (*io_epoch >= next_ep) {
            next_ep = *io_epoch + 1U;
        }
        *io_epoch = next_ep;
    }
    return searu_flow_commit_chart(arena, slot, cap_mask, next_ep, best_chart);
}

FLOW_FORCE_INL void searu__copy_or_zero4(uint64_t dst[4], const uint64_t *src) {
    if (src != NULL) {
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = src[3];
    } else {
        dst[0] = UINT64_C(0);
        dst[1] = UINT64_C(0);
        dst[2] = UINT64_C(0);
        dst[3] = UINT64_C(0);
    }
}

FLOW_FORCE_INL void searu__write_inline_result(searu_result *out, const uint64_t best[4],
                                               uint64_t best_e, uint64_t init_e, uint32_t iter,
                                               uint32_t epiphanies, uint32_t swans, uint64_t seed) {
    if (out != NULL) {
        out->canvas[0] = best[0];
        out->canvas[1] = best[1];
        out->canvas[2] = best[2];
        out->canvas[3] = best[3];
        out->best_energy = best_e;
        out->init_energy = init_e;
        out->best_x_q16 = INT64_C(0);
        out->iterations_run = iter;
        out->epiphanies = epiphanies;
        out->black_swans = swans;
        out->converged_2jet = (best_e == UINT64_C(0)) ? 1U : 0U;
        out->replay_seed = seed | UINT64_C(1);
    }
}

/* Zero-`BLR` Inline Custom-Objective NP Solver Macro (`SEARU_SOLVE_INLINE_HP` /
 * `SEARU_SOLVE_INLINE`): Inlines a caller-supplied C17 energy/frustration block directly into
 * SEARU's CLZ Zipf Sparse-k + Kan-pin + Crucible loop (0 function pointers, 0 indirect calls, 0
 * heap, ROP-immune). Inside the trailing `{ ... }` body, read `const uint64_t cand_id[4]`,
 * optionally mark frustrated variable bits in `uint64_t frust_id[4]`, and set `uint64_t energy_id`.
 */
#define SEARU_SOLVE_INLINE_HP(out_res_ptr, num_vars_val, init_canvas_ptr, kan_pin_ptr, seed_val,   \
                              max_iters_val, hp_ptr, cand_id, frust_id, energy_id, ...)            \
    do {                                                                                           \
        const searu_hyperparams *searu_inl__hp = (hp_ptr);                                         \
        uint64_t searu_inl__pin[4];                                                                \
        uint64_t searu_inl__cur[4];                                                                \
        searu__copy_or_zero4(searu_inl__pin, (kan_pin_ptr));                                       \
        searu__copy_or_zero4(searu_inl__cur, (init_canvas_ptr));                                   \
        uint32_t searu_inl__nvars = (uint32_t)(num_vars_val);                                      \
        if (searu_inl__nvars > SEARU_CANVAS_BITS) {                                                \
            searu_inl__nvars = SEARU_CANVAS_BITS;                                                  \
        }                                                                                          \
        uint64_t searu_inl__seed = (uint64_t)(seed_val);                                           \
        uint32_t searu_inl__max_it = (uint32_t)(max_iters_val);                                    \
        uint64_t searu_inl__rng = searu_inl__seed | UINT64_C(1);                                   \
        uint64_t searu_inl__frust[4] = {UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0)};       \
        uint64_t searu_inl__cur_e = UINT64_C(0);                                                   \
        {                                                                                          \
            const uint64_t cand_id[4] = {searu_inl__cur[0], searu_inl__cur[1], searu_inl__cur[2],  \
                                         searu_inl__cur[3]};                                       \
            uint64_t frust_id[4] = {UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0)};           \
            uint64_t energy_id = UINT64_C(0);                                                      \
            __VA_ARGS__                                                                            \
            searu_inl__cur_e = energy_id;                                                          \
            searu_inl__frust[0] = frust_id[0];                                                     \
            searu_inl__frust[1] = frust_id[1];                                                     \
            searu_inl__frust[2] = frust_id[2];                                                     \
            searu_inl__frust[3] = frust_id[3];                                                     \
        }                                                                                          \
        uint64_t searu_inl__best[4] = {searu_inl__cur[0], searu_inl__cur[1], searu_inl__cur[2],    \
                                       searu_inl__cur[3]};                                         \
        uint64_t searu_inl__best_e = searu_inl__cur_e;                                             \
        uint64_t searu_inl__init_e = searu_inl__cur_e;                                             \
        uint64_t searu_inl__init_temp = searu_inl__hp->inl_init_temp_q12;                          \
        uint64_t searu_inl__temp = searu_inl__init_temp;                                           \
        uint32_t searu_inl__stag = 0U;                                                             \
        uint32_t searu_inl__epi_cnt = 0U;                                                          \
        uint32_t searu_inl__swans = 0U;                                                            \
        uint32_t searu_inl__iter = 0U;                                                             \
        for (; searu_inl__iter < searu_inl__max_it && searu_inl__best_e > UINT64_C(0) &&           \
               searu_inl__nvars > 0U;                                                              \
             ++searu_inl__iter) {                                                                  \
            uint32_t searu_inl__chaos =                                                            \
                searu_adapt_chaos_q8_hp(searu_inl__stag, UINT64_C(0), searu_inl__hp->chaos_min_q8, \
                                        searu_inl__hp->chaos_max_q8, searu_inl__hp->stag_shift,    \
                                        searu_inl__hp->barrier_boost_q8);                          \
            uint64_t searu_inl__swan_m = UINT64_C(0);                                              \
            uint64_t searu_inl__jump =                                                             \
                searu_zipf_jump(&searu_inl__rng, searu_inl__chaos, &searu_inl__swan_m);            \
            searu_inl__swans += (uint32_t)(searu_inl__swan_m & UINT64_C(1));                       \
            uint32_t searu_inl__k = 1U;                                                            \
            if (searu_inl__swan_m != UINT64_C(0)) {                                                \
                searu_inl__k = 2U + (uint32_t)(searu_clz64(searu_inl__jump) & 3U);                 \
            } else if (searu_inl__jump >= UINT64_C(2)) {                                           \
                searu_inl__k = 2U;                                                                 \
            }                                                                                      \
            uint64_t searu_inl__cand_mut[4] = {searu_inl__cur[0], searu_inl__cur[1],               \
                                               searu_inl__cur[2], searu_inl__cur[3]};              \
            for (uint32_t searu_inl__f = 0U; searu_inl__f < searu_inl__k; ++searu_inl__f) {        \
                uint32_t searu_inl__v =                                                            \
                    searu_pick_var_hp(&searu_inl__rng, searu_inl__nvars, searu_inl__frust,         \
                                      searu_inl__pin, searu_inl__hp->inl_walk_mask);               \
                uint32_t searu_inl__w = searu_inl__v >> 6U;                                        \
                uint64_t searu_inl__bit =                                                          \
                    (UINT64_C(1) << (searu_inl__v & 63U)) & ~searu_inl__pin[searu_inl__w];         \
                searu_inl__cand_mut[searu_inl__w] ^= searu_inl__bit;                               \
            }                                                                                      \
            const uint64_t cand_id[4] = {searu_inl__cand_mut[0], searu_inl__cand_mut[1],           \
                                         searu_inl__cand_mut[2], searu_inl__cand_mut[3]};          \
            uint64_t frust_id[4] = {UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0)};           \
            uint64_t energy_id = UINT64_C(0);                                                      \
            __VA_ARGS__                                                                            \
            bool searu_inl__epi = false;                                                           \
            if (searu_crucible_accept(energy_id, searu_inl__cur_e, searu_inl__best_e,              \
                                      &searu_inl__temp, searu_inl__init_temp, searu_inl__swan_m,   \
                                      &searu_inl__rng, &searu_inl__epi)) {                         \
                searu_inl__cur[0] = cand_id[0];                                                    \
                searu_inl__cur[1] = cand_id[1];                                                    \
                searu_inl__cur[2] = cand_id[2];                                                    \
                searu_inl__cur[3] = cand_id[3];                                                    \
                searu_inl__frust[0] = frust_id[0];                                                 \
                searu_inl__frust[1] = frust_id[1];                                                 \
                searu_inl__frust[2] = frust_id[2];                                                 \
                searu_inl__frust[3] = frust_id[3];                                                 \
                searu_inl__cur_e = energy_id;                                                      \
                if (searu_inl__epi) {                                                              \
                    ++searu_inl__epi_cnt;                                                          \
                }                                                                                  \
                if (energy_id < searu_inl__best_e) {                                               \
                    searu_inl__best[0] = cand_id[0];                                               \
                    searu_inl__best[1] = cand_id[1];                                               \
                    searu_inl__best[2] = cand_id[2];                                               \
                    searu_inl__best[3] = cand_id[3];                                               \
                    searu_inl__best_e = energy_id;                                                 \
                    searu_inl__stag = 0U;                                                          \
                } else {                                                                           \
                    ++searu_inl__stag;                                                             \
                }                                                                                  \
            } else {                                                                               \
                ++searu_inl__stag;                                                                 \
            }                                                                                      \
            searu_inl__temp = (searu_inl__temp * searu_inl__hp->inl_cool_q16) >> 16U;              \
            if (searu_inl__temp < UINT64_C(64) &&                                                  \
                searu_inl__stag > searu_inl__hp->inl_reheat_stag) {                                \
                searu_inl__temp = searu_inl__hp->inl_reheat_q12;                                   \
            }                                                                                      \
        }                                                                                          \
        searu__write_inline_result((out_res_ptr), searu_inl__best, searu_inl__best_e,              \
                                   searu_inl__init_e, searu_inl__iter, searu_inl__epi_cnt,         \
                                   searu_inl__swans, searu_inl__seed);                             \
    } while (0)

#define SEARU_SOLVE_INLINE(out_res_ptr, num_vars_val, init_canvas_ptr, kan_pin_ptr, seed_val,     \
                           max_iters_val, cand_id, frust_id, energy_id, ...)                      \
    do {                                                                                          \
        const searu_hyperparams searu_inl__def_hp = SEARU_ACTIVE_OPT_HP;                          \
        SEARU_SOLVE_INLINE_HP((out_res_ptr), (num_vars_val), (init_canvas_ptr), (kan_pin_ptr),    \
                              (seed_val), (max_iters_val), &searu_inl__def_hp, cand_id, frust_id, \
                              energy_id, __VA_ARGS__);                                            \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* SEARU_H */
