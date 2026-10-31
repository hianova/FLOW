#ifndef FLOW_BITSPACE_H
#define FLOW_BITSPACE_H
#include "flow.h"
#include "plugin.h"
#include "reload.h"
#include "cubical_hott.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#if !defined(__cplusplus)
#include <stdatomic.h>
#endif
#define FLOW_GENOME_MASK(width)               ((width) >= 64 ? ~0ULL : ((1ULL << (width)) - 1ULL))
#define FLOW_GENOME_FIELD_MASK(offset, width) (FLOW_GENOME_MASK(width) << (offset))
#define FLOW_GENOME_GET(genome, offset, width) \
    (((uint64_t)(genome) >> (offset)) & FLOW_GENOME_MASK(width))
#define FLOW_GENOME_PACK(val, offset, width) \
    (((uint64_t)(val) & FLOW_GENOME_MASK(width)) << (offset))
static inline uint64_t flow_manifold_project(uint64_t genome, uint64_t hard_safety_mask,
                                             uint64_t dynamic_bias) {
    uint64_t projected = genome & hard_safety_mask;
    uint64_t malleable_bits = hard_safety_mask & ~genome;
    if (dynamic_bias != 0 && malleable_bits != 0) {
        projected |= (dynamic_bias & malleable_bits);
    }
    return projected;
}
static inline uint64_t flow_manifold_transition(uint64_t genome, uint64_t mask, uint64_t *rng_state,
                                                uint32_t *mutated_bit_out) {
    if (!mask) {
        if (mutated_bit_out) {
            *mutated_bit_out = 0;
        }
        return genome;
    }
    uint64_t r = flow_prng_next(rng_state);
    int bit_positions[64], count = 0;
    for (int i = 0; i < 64; ++i) {
        if ((mask >> i) & 1ULL) {
            bit_positions[count++] = i;
        }
    }
    if (!count) {
        if (mutated_bit_out) {
            *mutated_bit_out = 0;
        }
        return genome;
    }
    int chosen_idx = bit_positions[r % (uint64_t)count];
    if (mutated_bit_out) {
        *mutated_bit_out = (uint32_t)chosen_idx;
    }
    return genome ^ (1ULL << chosen_idx);
}
#define FLOW_BMF_SW_HARD_SAFETY         (1ULL << 0)
#define FLOW_BMF_SW_CONTRACT_GATE       (1ULL << 1)
#define FLOW_BMF_SW_RESOURCE_QUOTA      (1ULL << 2)
#define FLOW_BMF_SW_POLYTOPE_FEASIBLE   (1ULL << 3)
#define FLOW_BMF_SW_STICK_SLIP_MODE     (1ULL << 4)
#define FLOW_BMF_SW_IMPACT_DAMPING      (1ULL << 5)
#define FLOW_BMF_SW_COOP_SYNC_LOCK      (1ULL << 6)
#define FLOW_BMF_SW_EMERGENCY_HALT      (1ULL << 7)
#define FLOW_BMF_SW_THERMAL_THROTTLE    (1ULL << 8)
#define FLOW_BMF_SW_ANTI_SPILL_TILT     (1ULL << 9)
#define FLOW_BMF_SW_GRIPPER_FORCE_SAFE  (1ULL << 10)
#define FLOW_BMF_SW_ZMP_BALANCE         (1ULL << 11)
#define FLOW_BMF_SW_NUMA_FIRST_TOUCH    (1ULL << 12)
#define FLOW_BMF_SW_SIMD_VECTORIZED     (1ULL << 13)
#define FLOW_BMF_SW_QSBR_EPOCH_ADVANCE  (1ULL << 14)
#define FLOW_BMF_SW_DETERMINISM_INVAR   (1ULL << 15)
#define FLOW_BMF_SW_IMU_ZERO_SAT        (1ULL << 17)
#define FLOW_BMF_SW_PREPLAY_CONE_SAFE   (1ULL << 22)
#define FLOW_BMF_SW_SWARM_IMMUNITY      (1ULL << 23)
#define FLOW_BMF_SW_TELEMETRY_BIAS_MASK (0xFFFFFFFF00000000ULL)
#ifndef FLOW_BMF_1BIT_CANVAS_DEFINED
#define FLOW_BMF_1BIT_CANVAS_DEFINED
typedef struct __attribute__((aligned(64))) {
    uint32_t subspace_id;
    int32_t is_adjudicated_sound;
    uint64_t switchboard_bits, invariant_mask, malleable_mask, dynamic_bias;
    double energy;
    uint8_t _cacheline_pad[16];
} FlowBmf1BitCanvas;
#endif
typedef FlowBmf1BitCanvas FlowBmfCanvas;
#define FLOW_ATOMIC_STAGE_SWAP(dst, src)                                                        \
    do {                                                                                        \
        if ((dst) && (src)) {                                                                   \
            __atomic_store_n(&((dst)->subspace_id),                                             \
                             __atomic_load_n(&((src)->subspace_id), __ATOMIC_RELAXED),          \
                             __ATOMIC_RELAXED);                                                 \
            __atomic_store_n(&((dst)->invariant_mask),                                          \
                             __atomic_load_n(&((src)->invariant_mask), __ATOMIC_RELAXED),       \
                             __ATOMIC_RELAXED);                                                 \
            __atomic_store_n(&((dst)->malleable_mask),                                          \
                             __atomic_load_n(&((src)->malleable_mask), __ATOMIC_RELAXED),       \
                             __ATOMIC_RELAXED);                                                 \
            __atomic_store_n(&((dst)->dynamic_bias),                                            \
                             __atomic_load_n(&((src)->dynamic_bias), __ATOMIC_RELAXED),         \
                             __ATOMIC_RELAXED);                                                 \
            uint64_t _ebits = __atomic_load_n((const uint64_t *)(const void *)&((src)->energy), \
                                              __ATOMIC_RELAXED);                                \
            __atomic_store_n((uint64_t *)(void *)&((dst)->energy), _ebits, __ATOMIC_RELAXED);   \
            __atomic_store_n(&((dst)->is_adjudicated_sound),                                    \
                             __atomic_load_n(&((src)->is_adjudicated_sound), __ATOMIC_RELAXED), \
                             __ATOMIC_RELEASE);                                                 \
            __atomic_store_n(&((dst)->switchboard_bits),                                        \
                             __atomic_load_n(&((src)->switchboard_bits), __ATOMIC_ACQUIRE),     \
                             __ATOMIC_RELEASE);                                                 \
        }                                                                                       \
    } while (0)
static inline FlowBmf1BitCanvas flow_atomic_stage_load_impl(const FlowBmf1BitCanvas *src) {
    FlowBmf1BitCanvas _out = {0};
    if (!src)
        return _out;
    _out.switchboard_bits = __atomic_load_n(&src->switchboard_bits, __ATOMIC_ACQUIRE);
    _out.is_adjudicated_sound = __atomic_load_n(&src->is_adjudicated_sound, __ATOMIC_ACQUIRE);
    _out.subspace_id = __atomic_load_n(&src->subspace_id, __ATOMIC_RELAXED);
    _out.invariant_mask = __atomic_load_n(&src->invariant_mask, __ATOMIC_RELAXED);
    _out.malleable_mask = __atomic_load_n(&src->malleable_mask, __ATOMIC_RELAXED);
    _out.dynamic_bias = __atomic_load_n(&src->dynamic_bias, __ATOMIC_RELAXED);
    union {
        uint64_t u;
        double d;
    } _eu;
    _eu.u = __atomic_load_n((const uint64_t *)(const void *)&src->energy, __ATOMIC_RELAXED);
    _out.energy = _eu.d;
    return _out;
}
#define FLOW_ATOMIC_STAGE_LOAD(src) flow_atomic_stage_load_impl(src)
static inline void flow_bmf_canvas_init(FlowBmf1BitCanvas *canvas, uint32_t subspace_id,
                                        uint64_t invariant_mask, uint64_t malleable_mask,
                                        uint64_t initial_switches) {
    if (!canvas) {
        return;
    }
    canvas->subspace_id = subspace_id;
    canvas->invariant_mask = invariant_mask;
    canvas->malleable_mask = malleable_mask;
    canvas->switchboard_bits = (initial_switches | invariant_mask);
    canvas->dynamic_bias = 0;
    canvas->energy = 0.0;
    canvas->is_adjudicated_sound = 0;
}
static inline int flow_bmf_canvas_get_switch(const FlowBmf1BitCanvas *canvas,
                                             uint64_t switch_flag) {
    return canvas ? ((canvas->switchboard_bits & switch_flag) != 0) : 0;
}
static inline void flow_bmf_canvas_set_switch(FlowBmf1BitCanvas *canvas, uint64_t switch_flag,
                                              int value) {
    if (!canvas || (!value && (canvas->invariant_mask & switch_flag))) {
        return;
    }
    if (value) {
        canvas->switchboard_bits |= switch_flag;
    } else {
        canvas->switchboard_bits &= ~switch_flag;
    }
    canvas->is_adjudicated_sound = 0;
}
static inline int flow_bmf_canvas_verify_invariants(const FlowBmf1BitCanvas *canvas) {
    return canvas ? ((canvas->switchboard_bits & canvas->invariant_mask) == canvas->invariant_mask)
                  : 0;
}
static inline uint64_t flow_bmf_canvas_flip_1bit(FlowBmf1BitCanvas *canvas, uint64_t *rng_state,
                                                 uint32_t *mutated_bit_out) {
    if (!canvas) {
        return 0;
    }
    uint64_t flippable_mask = canvas->malleable_mask & ~canvas->invariant_mask;
    if (!flippable_mask) {
        if (mutated_bit_out) {
            *mutated_bit_out = 0;
        }
        return canvas->switchboard_bits;
    }
    canvas->switchboard_bits = flow_manifold_transition(canvas->switchboard_bits, flippable_mask,
                                                        rng_state, mutated_bit_out);
    canvas->is_adjudicated_sound = 0;
    return canvas->switchboard_bits;
}
typedef uint64_t flow_v512_u64 __attribute__((vector_size(64)));
typedef union {
    flow_v512_u64 vec;
    uint64_t u64[8];
    uint32_t u32[16];
    uint8_t u8[64];
} FlowVector512;
static inline FlowVector512 flow_v512_from_u64s(uint64_t w0, uint64_t w1, uint64_t w2, uint64_t w3,
                                                uint64_t w4, uint64_t w5, uint64_t w6,
                                                uint64_t w7) {
    return (FlowVector512){.u64 = {w0, w1, w2, w3, w4, w5, w6, w7}};
}
static inline FlowVector512 flow_v512_broadcast(uint64_t s) {
    return (FlowVector512){.u64 = {s, s, s, s, s, s, s, s}};
}
static inline FlowVector512 flow_v512_zero(void) {
    return (FlowVector512){0};
}
static inline FlowVector512 flow_v512_all_ones(void) {
    return flow_v512_broadcast(~0ULL);
}
static inline FlowVector512 flow_v512_project(FlowVector512 g, FlowVector512 h, FlowVector512 b) {
    return (FlowVector512){.vec = (g.vec & h.vec) | (b.vec & h.vec)};
}
static inline FlowVector512 flow_v512_and(FlowVector512 a, FlowVector512 b) {
    return (FlowVector512){.vec = a.vec & b.vec};
}
static inline uint32_t flow_v512_popcount(FlowVector512 v) {
    uint32_t c = 0;
    for (int i = 0; i < 8; ++i) {
        c += (uint32_t)__builtin_popcountll(v.u64[i]);
    }
    return c;
}
static inline uint64_t flow_v512_horizontal_or(FlowVector512 v) {
    return v.u64[0] | v.u64[1] | v.u64[2] | v.u64[3] | v.u64[4] | v.u64[5] | v.u64[6] | v.u64[7];
}
static inline uint64_t flow_v512_horizontal_and(FlowVector512 v) {
    return v.u64[0] & v.u64[1] & v.u64[2] & v.u64[3] & v.u64[4] & v.u64[5] & v.u64[6] & v.u64[7];
}
#define FLOW_PARETO_MAX              16
#define FLOW_BITSPACE_MAX_CANDIDATES 32
typedef struct {
    uint64_t hard_safety_mask, hard_contract_mask, hard_resource_mask, hard_plugin_mask,
        hard_polytope_mask, hard_composite_mask;
    uint64_t domain_preference_mask, dynamic_telemetry_bias, soft_composite_bias;
} FlowMaskCanvas;
typedef struct {
    double energy, latency_score, throughput_score;
    size_t memory_bytes, capacity;
    uint64_t benchmark_ns;
    int hard_gate_passed;
    char message[128];
} FlowEvaluation;
#define FLOW_GENOME_MAX_WORDS 16
typedef struct {
    uint64_t words[FLOW_GENOME_MAX_WORDS];
    uint32_t active_words, total_bits;
} FlowGenome;
void flow_genome_init(FlowGenome *g, uint32_t total_bits);
void flow_genome_flip_bit(FlowGenome *g, uint32_t bit_idx);
void flow_genome_mutate_1bit(FlowGenome *g, uint64_t *rng_state, uint32_t *mutated_bit_out);
typedef struct {
    const Component *component;
    FlowPlanDimensionSet dimension_set;
    FlowPlanAssignment assignment;
    FlowEvaluation eval;
    uint64_t genome, schema_hash, contract_hash;
    uint32_t bit_count;
} FlowPlan;
typedef struct FlowBitSpace FlowBitSpace;
struct FlowBitSpace {
    const SemanticIR *ir;
    const Component *candidates[FLOW_BITSPACE_MAX_CANDIDATES];
    FlowPlanDimensionSet candidate_dims[FLOW_BITSPACE_MAX_CANDIDATES];
    uint32_t candidate_bits[FLOW_BITSPACE_MAX_CANDIDATES];
    uint64_t candidate_masks[FLOW_BITSPACE_MAX_CANDIDATES];
    FlowMaskCanvas candidate_canvases[FLOW_BITSPACE_MAX_CANDIDATES];
    FlowMaskCanvas global_canvas;
    size_t candidate_count;
    uint32_t selector_bits, bit_count;
    uint64_t contract_hash, schema_hash, env_mask;
    int (*decode)(const FlowBitSpace *space, uint64_t genome, FlowPlan *plan);
    int (*evaluate)(const FlowBitSpace *space, const FlowPlan *plan, FlowEvaluation *result);
    int (*hard_gate)(const FlowBitSpace *space, const FlowPlan *plan, const FlowEvaluation *result);
};
typedef struct FlowPlanArtifact {
    char flow_name[64], module_name[64], module_version[32], component_id[64];
    uint64_t contract_hash, plan_schema_hash, genome;
    uint32_t seed, bit_count;
    FlowPlanDimensionSet dimensions;
    FlowPlanAssignment plan;
    FlowEvaluation metrics;
    char verification_status[32], attestation_msg[128];
    double jet_omega, jet_barrier_mu, jet_q_saturation;
} FlowPlanArtifact;
uint64_t flow_compute_contract_hash(const SemanticIR *ir);
uint64_t flow_bitspace_compute_schema_hash(const SemanticIR *ir, const Component *comp,
                                           const FlowPlanDimensionSet *dims);
int flow_bitspace_init_single(const SemanticIR *ir, const Component *comp, FlowBitSpace *space);
int flow_bitspace_init_for_ir(const SemanticIR *ir, FlowBitSpace *space);
uint64_t flow_bitspace_mutate_1bit(const FlowBitSpace *space, uint64_t genome, uint64_t *rng_state,
                                   uint32_t *mutated_bit_out);
uint64_t flow_bitspace_encode(const FlowBitSpace *space, size_t cand_idx,
                              const FlowPlanAssignment *plan);
uint64_t flow_bitspace_default_genome(const FlowBitSpace *space);
typedef enum {
    FLOW_GATE_PASS = 0,
    FLOW_GATE_FAIL_MEMORY_LIMIT,
    FLOW_GATE_FAIL_VERIFIER_UNPROVEN,
    FLOW_GATE_FAIL_THREAD_AFFINITY,
    FLOW_GATE_FAIL_QUOTA_EXCEEDED,
    FLOW_GATE_FAIL_DIMENSION_BOUND,
    FLOW_GATE_FAIL_REENTRANCY,
    FLOW_GATE_FAIL_MUTATION_INVALID,
    FLOW_GATE_CATEGORY_MAX
} FlowGateFailureReason;
typedef struct {
    uint64_t total_mutations, total_failures, failure_counts[FLOW_GATE_CATEGORY_MAX];
} FlowSearchHeatmap;
typedef struct {
    FlowPlan best_plan;
    size_t pareto_count, iterations;
    FlowPlan pareto_points[FLOW_PARETO_MAX];
    double best_latency, best_memory, latency_regret_percent, memory_regret_percent;
    uint32_t seed;
    int measured;
    FlowSearchHeatmap heatmap;
    FlowMaskCanvas mask_canvas;
} FlowBitSearchResult;
int flow_mask_canvas_compose(const SemanticIR *ir, const Component *comp,
                             const FlowPlanDimensionSet *dims, const FlowPMUTelemetry *pmu,
                             FlowMaskCanvas *canvas_out);
uint64_t flow_bitspace_mutate_1bit_superposed(const FlowBitSpace *space, uint64_t genome,
                                              const FlowMaskCanvas *canvas, double bias_weight,
                                              uint64_t *rng_state, uint32_t *mutated_bit_out);
int flow_bitspace_search(const FlowBitSpace *space, size_t iterations, uint32_t seed, int measured,
                         const FlowPlan *seed_plan, FlowBitSearchResult *result_out);
typedef enum {
    FLOW_TACTIC_SPEED = 0,
    FLOW_TACTIC_BALANCED = 1,
    FLOW_TACTIC_MEMORY = 2,
    FLOW_TACTIC_COUNT = 3
} FlowPlanTactic;
typedef struct {
    FlowPlan tactics[FLOW_TACTIC_COUNT];
    int available[FLOW_TACTIC_COUNT];
    size_t count;
} FlowPlanEnsemble;
const char *flow_plan_tactic_name(FlowPlanTactic tactic);
int flow_plan_artifact_save(FILE *output, const FlowPlanArtifact *artifact);
int flow_plan_artifact_load(FILE *input, FlowPlanArtifact *artifact);
int flow_plan_to_artifact(const FlowPlan *plan, const SemanticIR *ir, uint32_t seed,
                          FlowPlanArtifact *art);
int flow_artifact_validate(const FlowPlanArtifact *art, const SemanticIR *ir,
                           const FlowBitSpace *space, char *err_msg, size_t err_size);
typedef struct {
    size_t buffer_bytes, initial_capacity;
    unsigned growth_percent;
    size_t batch_size, arena_bytes;
} FlowTuning;
typedef struct {
    const Component *component;
    FlowPlanAssignment assignment;
    FlowPlanMetrics metrics;
    double energy;
} FlowParetoPoint;
typedef struct {
    size_t count;
    FlowParetoPoint points[FLOW_PARETO_MAX];
    double best_latency, best_memory, latency_regret_percent, memory_regret_percent;
} FlowParetoSummary;
typedef struct FlowSearchResult {
    const Component *component;
    uint64_t genome;
    FlowPlanDimensionSet dimension_set;
    FlowPlanAssignment assignment;
    FlowPlanMetrics metrics;
    double capacity, threads, shards, energy;
    uint64_t benchmark_ns;
    int measured;
    size_t iterations;
    uint32_t seed;
    FlowTuning tuning;
    FlowParetoSummary pareto;
    FlowSearchHeatmap heatmap;
    FlowMaskCanvas mask_canvas;
} SearchResult;
typedef struct {
    int available;
    size_t component, capacity, threads, shards;
    uint64_t benchmark_ns;
    size_t workload_bytes;
    FlowTuning tuning;
    FlowPlanAssignment assignment;
} ProfileSeed;
uint64_t flow_plan_get_value(const FlowPlanDimensionSet *dims, const FlowPlanAssignment *plan,
                             const char *name, uint64_t default_val);
FlowTuning flow_default_tuning(const SemanticIR *ir, const Component *component);
int flow_polyhedral_solve_plan(const SemanticIR *ir, const FlowBitSpace *space, FlowPlan *plan_out);
SearchResult search_best(const SemanticIR *ir, size_t iterations, uint32_t seed, int measured,
                         const ProfileSeed *profile);
void flow_plan_to_search_result(const FlowPlan *plan, const SemanticIR *ir, uint32_t seed,
                                SearchResult *out);
int flow_artifact_to_profile_seed(const FlowPlanArtifact *art, ProfileSeed *seed_out);

/* =========================================================================
 * FLOW Exact Numerics & IEEE-754 / 2-Jet / BigInt Engine
 *   1. math: Fast Integer Approximations (exp_approx_q16, rsqrt_approx_i32, silu_approx_i8)
 *            & Hardware IEEE-754 Double + 2-Jet Forward AD
 *   2. bignum: U256 ([uint64_t; 4]), 512-bit mul_wide, Montgomery Reduction, Miller-Rabin
 *   3. linalg: Stack-allocated i-k-j IEEE-754 Matrix, MSE Loss & Gradient Descent
 *   4. random: Pure bitwise Xoshiro256** + SplitMix64 PRNG
 * ========================================================================= */
#ifndef FLOW_EXACT_NUMERICS_DEFINED
#define FLOW_EXACT_NUMERICS_DEFINED
__extension__ typedef __int128 flow_i128_t;
__extension__ typedef unsigned __int128 flow_u128_t;

#define FLOW_STR_INNER(...)     #__VA_ARGS__
#define FLOW_STR(...)           FLOW_STR_INNER(__VA_ARGS__)

#define FLOW_FX16_SHIFT         16
#define FLOW_FX16_ONE           (1 << FLOW_FX16_SHIFT)
#define FLOW_FX16_EXP_BOUND     (10 * FLOW_FX16_ONE)
#define FLOW_FX16_LOG2E         INT64_C(94548)

#define FLOW_U256_LIMBS         4
#define FLOW_U512_LIMBS         8
#define FLOW_U256_BITS          256U
#define FLOW_WORD_BITS          64U
#define FLOW_NEWTON_64BIT_ITERS 5
#define FLOW_SPLITMIX64_MUL1    UINT64_C(0xbf58476d1ce4e5b9)
#define FLOW_SPLITMIX64_MUL2    UINT64_C(0x94d049bb133111eb)

#define FLOW_EXACT_CODEC_C17                                                                     \
    static inline __attribute__((unused)) int flow_exp_approx_q16(int32_t x, int32_t *out) {     \
        if (!out) {                                                                              \
            return 0;                                                                            \
        }                                                                                        \
        if (x < -FLOW_FX16_EXP_BOUND) {                                                          \
            *out = 0;                                                                            \
            return 1;                                                                            \
        }                                                                                        \
        if (x > FLOW_FX16_EXP_BOUND) {                                                           \
            return 0;                                                                            \
        }                                                                                        \
        int32_t x_scaled = (int32_t)(((int64_t)x * FLOW_FX16_LOG2E) >> FLOW_FX16_SHIFT);         \
        int32_t int_part = x_scaled >> FLOW_FX16_SHIFT;                                          \
        int32_t frac_part = x_scaled & (FLOW_FX16_ONE - 1);                                      \
        int32_t approx_frac = FLOW_FX16_ONE + frac_part;                                         \
        if (int_part >= 0) {                                                                     \
            *out = (int32_t)((uint32_t)approx_frac << int_part);                                 \
        } else {                                                                                 \
            *out = approx_frac >> (-int_part);                                                   \
        }                                                                                        \
        return 1;                                                                                \
    }                                                                                            \
    static inline __attribute__((unused)) int flow_rsqrt_approx_i32(uint32_t x, uint32_t *out) { \
        if (!out || x == 0U) {                                                                   \
            return 0;                                                                            \
        }                                                                                        \
        uint32_t msb = 31U - (uint32_t)__builtin_clz(x);                                         \
        uint32_t sqrt_approx = 1U << (msb >> 1U);                                                \
        *out = (uint32_t)FLOW_FX16_ONE / sqrt_approx;                                            \
        return 1;                                                                                \
    }                                                                                            \
    static inline __attribute__((unused)) int flow_silu_approx_i8(int8_t x, int8_t *out) {       \
        if (!out) {                                                                              \
            return 0;                                                                            \
        }                                                                                        \
        int32_t x_q16 = (int32_t)((uint32_t)(int32_t)x << FLOW_FX16_SHIFT);                      \
        int32_t exp_neg_x = 0;                                                                   \
        if (!flow_exp_approx_q16(-x_q16, &exp_neg_x)) {                                          \
            return 0;                                                                            \
        }                                                                                        \
        int32_t denom = FLOW_FX16_ONE + exp_neg_x;                                               \
        if (denom == 0) {                                                                        \
            return 0;                                                                            \
        }                                                                                        \
        int64_t result = ((int64_t)x_q16 * (int64_t)FLOW_FX16_ONE) / (int64_t)denom;             \
        int32_t res_i32 = (int32_t)(result >> FLOW_FX16_SHIFT);                                  \
        if (res_i32 < INT8_MIN) {                                                                \
            res_i32 = INT8_MIN;                                                                  \
        } else if (res_i32 > INT8_MAX) {                                                         \
            res_i32 = INT8_MAX;                                                                  \
        }                                                                                        \
        *out = (int8_t)res_i32;                                                                  \
        return 1;                                                                                \
    }

#define FLOW_LEAN_NAT_OPS_C17                                                                  \
    static inline __attribute__((unused)) uint64_t flow_nat_sub(uint64_t a, uint64_t b) {      \
        return a >= b ? (a - b) : 0ULL;                                                        \
    }                                                                                          \
    static inline __attribute__((unused)) uint64_t flow_nat_div(uint64_t a, uint64_t b) {      \
        return b == 0ULL ? 0ULL : (a / b);                                                     \
    }                                                                                          \
    static inline __attribute__((unused)) uint64_t flow_nat_mod(uint64_t a, uint64_t b) {      \
        return b == 0ULL ? a : (a % b);                                                        \
    }                                                                                          \
    static inline __attribute__((unused)) uint64_t flow_nat_pow(uint64_t base, uint64_t exp) { \
        uint64_t r = 1ULL;                                                                     \
        while (exp > 0ULL) {                                                                   \
            if (exp & 1ULL) {                                                                  \
                r *= base;                                                                     \
            }                                                                                  \
            base *= base;                                                                      \
            exp >>= 1U;                                                                        \
        }                                                                                      \
        return r;                                                                              \
    }                                                                                          \
    static inline __attribute__((unused)) uint64_t flow_nat_min(uint64_t a, uint64_t b) {      \
        return a < b ? a : b;                                                                  \
    }                                                                                          \
    static inline __attribute__((unused)) uint64_t flow_nat_max(uint64_t a, uint64_t b) {      \
        return a > b ? a : b;                                                                  \
    }                                                                                          \
    static inline __attribute__((unused)) int64_t flow_int_div(int64_t a, int64_t b) {         \
        return b == 0 ? 0 : ((a == INT64_MIN && b == -1) ? INT64_MIN : (a / b));               \
    }                                                                                          \
    static inline __attribute__((unused)) int64_t flow_int_mod(int64_t a, int64_t b) {         \
        return b == 0 ? a : ((a == INT64_MIN && b == -1) ? 0 : (a % b));                       \
    }                                                                                          \
    static inline __attribute__((unused)) int64_t flow_int_pow(int64_t base, int64_t exp) {    \
        if (exp < 0) {                                                                         \
            return 0;                                                                          \
        }                                                                                      \
        int64_t r = 1;                                                                         \
        while (exp > 0) {                                                                      \
            if (exp & 1) {                                                                     \
                r *= base;                                                                     \
            }                                                                                  \
            base *= base;                                                                      \
            exp >>= 1;                                                                         \
        }                                                                                      \
        return r;                                                                              \
    }

#define FLOW_LEAN_FLOAT_OPS_C17                                                                 \
    static inline __attribute__((unused)) double flow_float_add(double a, double b) {           \
        return a + b;                                                                           \
    }                                                                                           \
    static inline __attribute__((unused)) double flow_float_sub(double a, double b) {           \
        return a - b;                                                                           \
    }                                                                                           \
    static inline __attribute__((unused)) double flow_float_mul(double a, double b) {           \
        return a * b;                                                                           \
    }                                                                                           \
    static inline __attribute__((unused)) double flow_float_div(double a, double b) {           \
        return b == 0.0 ? 0.0 : (a / b);                                                        \
    }                                                                                           \
    static inline __attribute__((unused)) double flow_float_mod(double a, double b) {           \
        return b == 0.0 ? 0.0 : __builtin_fmod(a, b);                                           \
    }                                                                                           \
    static inline __attribute__((unused)) double flow_float_neg(double x) {                     \
        return -x;                                                                              \
    }                                                                                           \
    static inline __attribute__((unused)) double flow_float_pow(double base, double exp) {      \
        return __builtin_pow(base, exp);                                                        \
    }                                                                                           \
    static inline __attribute__((unused)) double flow_float_abs(double x) {                     \
        return __builtin_fabs(x);                                                               \
    }                                                                                           \
    static inline __attribute__((unused)) double flow_float_floor(double x) {                   \
        return __builtin_floor(x);                                                              \
    }                                                                                           \
    static inline __attribute__((unused)) double flow_float_ceil(double x) {                    \
        return __builtin_ceil(x);                                                               \
    }                                                                                           \
    static inline __attribute__((unused)) double flow_float_round(double x) {                   \
        return __builtin_round(x);                                                              \
    }                                                                                           \
    static inline __attribute__((unused)) double flow_float_sqrt(double x) {                    \
        return x <= 0.0 ? 0.0 : __builtin_sqrt(x);                                              \
    }                                                                                           \
    static inline __attribute__((unused)) double flow_float_exp(double x) {                     \
        return __builtin_exp(x);                                                                \
    }                                                                                           \
    static inline __attribute__((unused)) double flow_float_log(double x) {                     \
        return x <= 0.0 ? 0.0 : __builtin_log(x);                                               \
    }                                                                                           \
    static inline __attribute__((unused)) double flow_float_sin(double x) {                     \
        return __builtin_sin(x);                                                                \
    }                                                                                           \
    static inline __attribute__((unused)) double flow_float_cos(double x) {                     \
        return __builtin_cos(x);                                                                \
    }                                                                                           \
    static inline __attribute__((unused)) double flow_float_atan2(double y, double x) {         \
        return __builtin_atan2(y, x);                                                           \
    }                                                                                           \
    static inline __attribute__((unused)) double flow_float_max(double a, double b) {           \
        return __builtin_isnan(a) ? a : (__builtin_isnan(b) ? b : (a > b ? a : b));             \
    }                                                                                           \
    static inline __attribute__((unused)) double flow_float_min(double a, double b) {           \
        return __builtin_isnan(a) ? a : (__builtin_isnan(b) ? b : (a < b ? a : b));             \
    }                                                                                           \
    static inline __attribute__((unused)) double flow_float_of_nat(uint64_t x) {                \
        return (double)x;                                                                       \
    }                                                                                           \
    static inline __attribute__((unused)) double flow_float_of_int(int64_t x) {                 \
        return (double)x;                                                                       \
    }                                                                                           \
    static inline __attribute__((unused)) uint64_t flow_quantize(double val, double min_v,      \
                                                                 double max_v, uint32_t bits) { \
        if (!__builtin_isfinite(val) || !__builtin_isfinite(min_v) ||                           \
            !__builtin_isfinite(max_v) || max_v <= min_v || bits == 0 || bits > 32U)            \
            return 0ULL;                                                                        \
        uint64_t max_q = (bits == 32U) ? 0xFFFFFFFFULL : ((1ULL << bits) - 1ULL);               \
        double c = val < min_v ? min_v : (val > max_v ? max_v : val);                           \
        return (uint64_t)(((c - min_v) / (max_v - min_v)) * (double)max_q + 0.5);               \
    }                                                                                           \
    static inline __attribute__((unused)) double flow_dequantize(uint64_t code, double min_v,   \
                                                                 double max_v, uint32_t bits) { \
        if (!__builtin_isfinite(min_v) || !__builtin_isfinite(max_v) || max_v <= min_v ||       \
            bits == 0 || bits > 32U)                                                            \
            return 0.0;                                                                         \
        uint64_t max_q = (bits == 32U) ? 0xFFFFFFFFULL : ((1ULL << bits) - 1ULL);               \
        uint64_t c = code > max_q ? max_q : code;                                               \
        return min_v + ((double)c / (double)max_q) * (max_v - min_v);                           \
    }

#define FLOW_JET_SCALAR2_TYPEDEF_C17 \
    typedef struct {                 \
        double v;                    \
        double d1;                   \
        double d2;                   \
    } FlowJetScalar2;

#ifndef FLOW_JET_SCALAR2_DEFINED
#define FLOW_JET_SCALAR2_DEFINED
FLOW_JET_SCALAR2_TYPEDEF_C17
#endif

#define FLOW_ABI_CORE_TYPES_C17                                    \
    typedef enum {                                                 \
        FLOW_OK = 0,                                               \
        FLOW_ERR_INVALID_ARG = -1,                                 \
        FLOW_ERR_QUOTA_OR_STRIDE = -2,                             \
        FLOW_ERR_KERNEL_UNBOUND = -3,                              \
        FLOW_ERR_WATCHDOG_ESTOP = -4                               \
    } FlowStatusCode;                                              \
    typedef struct {                                               \
        uint64_t base_offset, byte_length, qsbr_epoch;             \
        uint32_t stride_bytes, align_bytes, schema_version, flags; \
        uint64_t reserved[3];                                      \
    } FlowExtentDescriptor;

#ifndef FLOW_ABI_CORE_TYPES_DEFINED
#define FLOW_ABI_CORE_TYPES_DEFINED
FLOW_ABI_CORE_TYPES_C17
#endif

#define FLOW_JET2_ALU_C17                                                                    \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_make(double v, double d1, \
                                                                        double d2) {         \
        FlowJetScalar2 r;                                                                    \
        r.v = __builtin_isfinite(v) ? v : 0.0;                                               \
        r.d1 = __builtin_isfinite(d1) ? d1 : 0.0;                                            \
        r.d2 = __builtin_isfinite(d2) ? d2 : 0.0;                                            \
        return r;                                                                            \
    }                                                                                        \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_var(double v) {           \
        return flow_jet2_make(v, 1.0, 0.0);                                                  \
    }                                                                                        \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_const(double c) {         \
        return flow_jet2_make(c, 0.0, 0.0);                                                  \
    }                                                                                        \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_of_nat(uint64_t x) {      \
        return flow_jet2_make((double)x, 0.0, 0.0);                                          \
    }                                                                                        \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_of_int(int64_t x) {       \
        return flow_jet2_make((double)x, 0.0, 0.0);                                          \
    }                                                                                        \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_add(FlowJetScalar2 a,     \
                                                                       FlowJetScalar2 b) {   \
        return flow_jet2_make(a.v + b.v, a.d1 + b.d1, a.d2 + b.d2);                          \
    }                                                                                        \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_sub(FlowJetScalar2 a,     \
                                                                       FlowJetScalar2 b) {   \
        return flow_jet2_make(a.v - b.v, a.d1 - b.d1, a.d2 - b.d2);                          \
    }                                                                                        \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_mul(FlowJetScalar2 a,     \
                                                                       FlowJetScalar2 b) {   \
        return flow_jet2_make(a.v * b.v, a.d1 * b.v + a.v * b.d1,                            \
                              a.d2 * b.v + 2.0 * a.d1 * b.d1 + a.v * b.d2);                  \
    }                                                                                        \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_div(FlowJetScalar2 a,     \
                                                                       FlowJetScalar2 b) {   \
        if (b.v == 0.0 || !__builtin_isfinite(b.v)) {                                        \
            return flow_jet2_const(0.0);                                                     \
        }                                                                                    \
        double v = a.v / b.v;                                                                \
        double d1 = (a.d1 - v * b.d1) / b.v;                                                 \
        double d2 = (a.d2 - 2.0 * d1 * b.d1 - v * b.d2) / b.v;                               \
        return flow_jet2_make(v, d1, d2);                                                    \
    }                                                                                        \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_mod(FlowJetScalar2 a,     \
                                                                       FlowJetScalar2 b) {   \
        if (b.v == 0.0 || !__builtin_isfinite(b.v)) {                                        \
            return flow_jet2_const(0.0);                                                     \
        }                                                                                    \
        double q = __builtin_floor(a.v / b.v);                                               \
        return flow_jet2_make(__builtin_fmod(a.v, b.v), a.d1 - q * b.d1, a.d2 - q * b.d2);   \
    }                                                                                        \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_neg(FlowJetScalar2 x) {   \
        return flow_jet2_make(-x.v, -x.d1, -x.d2);                                           \
    }                                                                                        \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_abs(FlowJetScalar2 x) {   \
        double s = (x.v > 0.0) ? 1.0 : ((x.v < 0.0) ? -1.0 : 0.0);                           \
        return flow_jet2_make(__builtin_fabs(x.v), s * x.d1, s * x.d2);                      \
    }                                                                                        \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_floor(FlowJetScalar2 x) { \
        return flow_jet2_make(__builtin_floor(x.v), 0.0, 0.0);                               \
    }                                                                                        \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_ceil(FlowJetScalar2 x) {  \
        return flow_jet2_make(__builtin_ceil(x.v), 0.0, 0.0);                                \
    }                                                                                        \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_round(FlowJetScalar2 x) { \
        return flow_jet2_make(__builtin_round(x.v), 0.0, 0.0);                               \
    }

#define FLOW_JET2_MATH_C17                                                                    \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_sqrt(FlowJetScalar2 x) {   \
        if (x.v <= 0.0) {                                                                     \
            return flow_jet2_const(0.0);                                                      \
        }                                                                                     \
        double r = __builtin_sqrt(x.v);                                                       \
        double d1 = x.d1 / (2.0 * r);                                                         \
        double d2 = (x.d2 - 2.0 * d1 * d1) / (2.0 * r);                                       \
        return flow_jet2_make(r, d1, d2);                                                     \
    }                                                                                         \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_exp(FlowJetScalar2 x) {    \
        double e = __builtin_exp(x.v);                                                        \
        return flow_jet2_make(e, e * x.d1, e * (x.d1 * x.d1 + x.d2));                         \
    }                                                                                         \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_log(FlowJetScalar2 x) {    \
        if (x.v <= 0.0) {                                                                     \
            return flow_jet2_const(0.0);                                                      \
        }                                                                                     \
        double d1 = x.d1 / x.v;                                                               \
        double d2 = (x.d2 - d1 * x.d1) / x.v;                                                 \
        return flow_jet2_make(__builtin_log(x.v), d1, d2);                                    \
    }                                                                                         \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_sin(FlowJetScalar2 x) {    \
        double s = __builtin_sin(x.v), c = __builtin_cos(x.v);                                \
        return flow_jet2_make(s, c * x.d1, -s * x.d1 * x.d1 + c * x.d2);                      \
    }                                                                                         \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_cos(FlowJetScalar2 x) {    \
        double s = __builtin_sin(x.v), c = __builtin_cos(x.v);                                \
        return flow_jet2_make(c, -s * x.d1, -c * x.d1 * x.d1 - s * x.d2);                     \
    }                                                                                         \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_atan2(FlowJetScalar2 y,    \
                                                                         FlowJetScalar2 x) {  \
        double r2 = x.v * x.v + y.v * y.v;                                                    \
        if (r2 <= 0.0) {                                                                      \
            return flow_jet2_const(0.0);                                                      \
        }                                                                                     \
        double d1 = (x.v * y.d1 - y.v * x.d1) / r2;                                           \
        double d2 = ((x.v * y.d2 - y.v * x.d2) - 2.0 * d1 * (x.v * x.d1 + y.v * y.d1)) / r2;  \
        return flow_jet2_make(__builtin_atan2(y.v, x.v), d1, d2);                             \
    }                                                                                         \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_pow(FlowJetScalar2 b,      \
                                                                       FlowJetScalar2 e) {    \
        if (e.d1 == 0.0 && e.d2 == 0.0) {                                                     \
            double p = e.v;                                                                   \
            double v = __builtin_pow(b.v, p);                                                 \
            double gp = (p == 0.0) ? 0.0 : (p * __builtin_pow(b.v, p - 1.0));                 \
            double gpp =                                                                      \
                (p == 0.0 || p == 1.0) ? 0.0 : (p * (p - 1.0) * __builtin_pow(b.v, p - 2.0)); \
            return flow_jet2_make(v, gp * b.d1, gpp * b.d1 * b.d1 + gp * b.d2);               \
        }                                                                                     \
        if (b.v <= 0.0) {                                                                     \
            return flow_jet2_const(0.0);                                                      \
        }                                                                                     \
        return flow_jet2_exp(flow_jet2_mul(e, flow_jet2_log(b)));                             \
    }                                                                                         \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_max(FlowJetScalar2 a,      \
                                                                       FlowJetScalar2 b) {    \
        return (a.v >= b.v) ? a : b;                                                          \
    }                                                                                         \
    static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_min(FlowJetScalar2 a,      \
                                                                       FlowJetScalar2 b) {    \
        return (a.v <= b.v) ? a : b;                                                          \
    }

#define FLOW_JET2_OPS_C17 FLOW_JET2_ALU_C17 FLOW_JET2_MATH_C17

FLOW_EXACT_CODEC_C17
#ifndef FLOW_LEAN_NAT_OPS_DEFINED
#define FLOW_LEAN_NAT_OPS_DEFINED
FLOW_LEAN_NAT_OPS_C17
FLOW_LEAN_FLOAT_OPS_C17
FLOW_JET2_OPS_C17
#endif

typedef struct {
    uint64_t w[FLOW_U256_LIMBS];
} FlowU256;

FlowU256 flow_u256_zero(void);
FlowU256 flow_u256_one(void);
FlowU256 flow_u256_from_u64(uint64_t v);
int flow_u256_from_hex_str(const char *hex, FlowU256 *out);
int flow_u256_is_zero(const FlowU256 *a);
int flow_u256_is_even(const FlowU256 *a);
uint32_t flow_u256_leading_zeros(const FlowU256 *a);
int flow_u256_cmp(const FlowU256 *a, const FlowU256 *b);
FlowU256 flow_u256_overflowing_add(FlowU256 a, FlowU256 b, int *overflow_out);
FlowU256 flow_u256_overflowing_sub(FlowU256 a, FlowU256 b, int *borrow_out);
FlowU256 flow_u256_shl1(FlowU256 a);
FlowU256 flow_u256_shr1(FlowU256 a);
void flow_u256_mul_wide(FlowU256 a, FlowU256 b, uint64_t out_512[FLOW_U512_LIMBS]);
FlowU256 flow_u256_mul(FlowU256 a, FlowU256 b);
int flow_u256_div_rem(FlowU256 a, FlowU256 b, FlowU256 *q_out, FlowU256 *r_out);
uint64_t flow_u256_inv_word(const FlowU256 *n);
FlowU256 flow_u256_mont_reduce(const uint64_t t_in[FLOW_U512_LIMBS], const FlowU256 *modulus,
                               uint64_t n0_inv);
FlowU256 flow_u256_mod_pow(FlowU256 base, FlowU256 exp, FlowU256 modulus);
int flow_u256_is_probably_prime(const FlowU256 *n, const uint64_t *bases, size_t base_count);

typedef struct {
    uint64_t s[FLOW_U256_LIMBS];
} FlowXoshiro256SS;

void flow_xoshiro256ss_init(FlowXoshiro256SS *rng, uint64_t seed);
uint64_t flow_xoshiro256ss_next_u64(FlowXoshiro256SS *rng);
int flow_xoshiro256ss_next_bool(FlowXoshiro256SS *rng);

void flow_mat_mul_ikj(size_t R, size_t K, size_t C, const double *a, const double *b, double *out);
double flow_mse_loss(size_t N, size_t OUT, const double *predictions, const double *targets);
void flow_gradient_descent_step(size_t N, size_t IN, size_t OUT, const double *inputs,
                                double *weights, const double *predictions, const double *targets,
                                double learning_rate);
#endif /* FLOW_EXACT_NUMERICS_DEFINED */
#endif /* FLOW_BITSPACE_H */
