#ifndef FLOW_BITSPACE_H
#define FLOW_BITSPACE_H
#include "flow.h"
#include "plugin.h"
#include "reload.h"
#include "polyhedral.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#if !defined(__cplusplus)
#include <stdatomic.h>
#endif
#define FLOW_GENOME_MASK(width) ((width) >= 64 ? ~0ULL : ((1ULL << (width)) - 1ULL))
#define FLOW_GENOME_FIELD_MASK(offset, width) (FLOW_GENOME_MASK(width) << (offset))
#define FLOW_GENOME_GET(genome, offset, width) (((uint64_t)(genome) >> (offset)) & FLOW_GENOME_MASK(width))
#define FLOW_GENOME_PACK(val, offset, width) (((uint64_t)(val) & FLOW_GENOME_MASK(width)) << (offset))
static inline uint64_t flow_manifold_project(uint64_t genome, uint64_t hard_safety_mask, uint64_t dynamic_bias) {
    uint64_t projected = genome & hard_safety_mask;
    uint64_t malleable_bits = hard_safety_mask & ~genome;
    if (dynamic_bias != 0 && malleable_bits != 0) {
        projected |= (dynamic_bias & malleable_bits);
    }
    return projected;
}
static inline uint64_t flow_manifold_transition(uint64_t genome, uint64_t mask, uint64_t *rng_state, uint32_t *mutated_bit_out) {
    if (!mask) {
        if (mutated_bit_out) {
            *mutated_bit_out = 0;
        }
        return genome;
    }
    uint64_t s = (rng_state && *rng_state) ? *rng_state : 0x853c49e6748fea9bULL;
    s ^= s >> 12;
    s ^= s << 25;
    s ^= s >> 27;
    uint64_t r = s * 0x2545F4914F6CDD1DULL;
    if (rng_state) {
        *rng_state = s;
    }
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
#define FLOW_BMF_SW_HARD_SAFETY        (1ULL << 0)
#define FLOW_BMF_SW_CONTRACT_GATE      (1ULL << 1)
#define FLOW_BMF_SW_RESOURCE_QUOTA     (1ULL << 2)
#define FLOW_BMF_SW_POLYTOPE_FEASIBLE  (1ULL << 3)
#define FLOW_BMF_SW_STICK_SLIP_MODE    (1ULL << 4)
#define FLOW_BMF_SW_IMPACT_DAMPING     (1ULL << 5)
#define FLOW_BMF_SW_COOP_SYNC_LOCK     (1ULL << 6)
#define FLOW_BMF_SW_EMERGENCY_HALT     (1ULL << 7)
#define FLOW_BMF_SW_THERMAL_THROTTLE   (1ULL << 8)
#define FLOW_BMF_SW_ANTI_SPILL_TILT    (1ULL << 9)
#define FLOW_BMF_SW_GRIPPER_FORCE_SAFE (1ULL << 10)
#define FLOW_BMF_SW_ZMP_BALANCE        (1ULL << 11)
#define FLOW_BMF_SW_NUMA_FIRST_TOUCH   (1ULL << 12)
#define FLOW_BMF_SW_SIMD_VECTORIZED    (1ULL << 13)
#define FLOW_BMF_SW_QSBR_EPOCH_ADVANCE (1ULL << 14)
#define FLOW_BMF_SW_DETERMINISM_INVAR  (1ULL << 15)
#define FLOW_BMF_SW_IMU_ZERO_SAT       (1ULL << 17)
#define FLOW_BMF_SW_PREPLAY_CONE_SAFE  (1ULL << 22)
#define FLOW_BMF_SW_SWARM_IMMUNITY     (1ULL << 23)
#define FLOW_BMF_SW_TELEMETRY_BIAS_MASK (0xFFFFFFFF00000000ULL)
typedef struct __attribute__((aligned(64))) { uint32_t subspace_id; int32_t is_adjudicated_sound; uint64_t switchboard_bits, invariant_mask, malleable_mask, dynamic_bias;
    double energy; uint8_t _cacheline_pad[16]; } FlowBmf1BitCanvas; typedef FlowBmf1BitCanvas FlowBmfCanvas;
#define FLOW_ATOMIC_STAGE_SWAP(dst, src) do { if ((dst) && (src)) { \
    __atomic_store_n(&((dst)->subspace_id), __atomic_load_n(&((src)->subspace_id), __ATOMIC_RELAXED), __ATOMIC_RELAXED); \
    __atomic_store_n(&((dst)->invariant_mask), __atomic_load_n(&((src)->invariant_mask), __ATOMIC_RELAXED), __ATOMIC_RELAXED); \
    __atomic_store_n(&((dst)->malleable_mask), __atomic_load_n(&((src)->malleable_mask), __ATOMIC_RELAXED), __ATOMIC_RELAXED); \
    __atomic_store_n(&((dst)->dynamic_bias), __atomic_load_n(&((src)->dynamic_bias), __ATOMIC_RELAXED), __ATOMIC_RELAXED); \
    uint64_t _ebits = __atomic_load_n((const uint64_t *)(const void *)&((src)->energy), __ATOMIC_RELAXED); \
    __atomic_store_n((uint64_t *)(void *)&((dst)->energy), _ebits, __ATOMIC_RELAXED); \
    __atomic_store_n(&((dst)->is_adjudicated_sound), __atomic_load_n(&((src)->is_adjudicated_sound), __ATOMIC_RELAXED), __ATOMIC_RELEASE); \
    __atomic_store_n(&((dst)->switchboard_bits), __atomic_load_n(&((src)->switchboard_bits), __ATOMIC_ACQUIRE), __ATOMIC_RELEASE); \
} } while (0)
static inline FlowBmf1BitCanvas flow_atomic_stage_load_impl(const FlowBmf1BitCanvas *src) {
    FlowBmf1BitCanvas _out = {0};
    if (!src) return _out;
    _out.switchboard_bits = __atomic_load_n(&src->switchboard_bits, __ATOMIC_ACQUIRE);
    _out.is_adjudicated_sound = __atomic_load_n(&src->is_adjudicated_sound, __ATOMIC_ACQUIRE);
    _out.subspace_id = __atomic_load_n(&src->subspace_id, __ATOMIC_RELAXED);
    _out.invariant_mask = __atomic_load_n(&src->invariant_mask, __ATOMIC_RELAXED);
    _out.malleable_mask = __atomic_load_n(&src->malleable_mask, __ATOMIC_RELAXED);
    _out.dynamic_bias = __atomic_load_n(&src->dynamic_bias, __ATOMIC_RELAXED);
    union { uint64_t u; double d; } _eu;
    _eu.u = __atomic_load_n((const uint64_t *)(const void *)&src->energy, __ATOMIC_RELAXED);
    _out.energy = _eu.d;
    return _out;
}
#define FLOW_ATOMIC_STAGE_LOAD(src)      flow_atomic_stage_load_impl(src)
static inline void flow_bmf_canvas_init(FlowBmf1BitCanvas *canvas, uint32_t subspace_id, uint64_t invariant_mask, uint64_t malleable_mask, uint64_t initial_switches) {
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
static inline int flow_bmf_canvas_get_switch(const FlowBmf1BitCanvas *canvas, uint64_t switch_flag) {
    return canvas ? ((canvas->switchboard_bits & switch_flag) != 0) : 0;
}
static inline void flow_bmf_canvas_set_switch(FlowBmf1BitCanvas *canvas, uint64_t switch_flag, int value) {
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
    return canvas ? ((canvas->switchboard_bits & canvas->invariant_mask) == canvas->invariant_mask) : 0;
}
static inline uint64_t flow_bmf_canvas_flip_1bit(FlowBmf1BitCanvas *canvas, uint64_t *rng_state, uint32_t *mutated_bit_out) {
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
    canvas->switchboard_bits = flow_manifold_transition(canvas->switchboard_bits, flippable_mask, rng_state, mutated_bit_out);
    canvas->is_adjudicated_sound = 0;
    return canvas->switchboard_bits;
}
typedef uint64_t flow_v512_u64 __attribute__((vector_size(64)));
typedef union { flow_v512_u64 vec; uint64_t u64[8]; uint32_t u32[16]; uint8_t u8[64]; } FlowVector512;
static inline FlowVector512 flow_v512_from_u64s(uint64_t w0, uint64_t w1, uint64_t w2, uint64_t w3, uint64_t w4, uint64_t w5, uint64_t w6, uint64_t w7) {
    return (FlowVector512){ .u64 = {w0, w1, w2, w3, w4, w5, w6, w7} }; }
static inline FlowVector512 flow_v512_broadcast(uint64_t s) { return (FlowVector512){ .u64 = {s, s, s, s, s, s, s, s} }; }
static inline FlowVector512 flow_v512_zero(void) { return (FlowVector512){0}; }
static inline FlowVector512 flow_v512_all_ones(void) { return flow_v512_broadcast(~0ULL); }
static inline FlowVector512 flow_v512_project(FlowVector512 g, FlowVector512 h, FlowVector512 b) { return (FlowVector512){ .vec = (g.vec & h.vec) | (b.vec & h.vec) }; }
static inline FlowVector512 flow_v512_and(FlowVector512 a, FlowVector512 b) { return (FlowVector512){ .vec = a.vec & b.vec }; }
static inline uint32_t flow_v512_popcount(FlowVector512 v) {
    uint32_t c = 0;
    for (int i = 0; i < 8; ++i) {
        c += (uint32_t)__builtin_popcountll(v.u64[i]);
    }
    return c;
}
static inline uint64_t flow_v512_horizontal_or(FlowVector512 v) { return v.u64[0] | v.u64[1] | v.u64[2] | v.u64[3] | v.u64[4] | v.u64[5] | v.u64[6] | v.u64[7]; }
static inline uint64_t flow_v512_horizontal_and(FlowVector512 v) { return v.u64[0] & v.u64[1] & v.u64[2] & v.u64[3] & v.u64[4] & v.u64[5] & v.u64[6] & v.u64[7]; }
#define FLOW_PARETO_MAX 16
#define FLOW_BITSPACE_MAX_CANDIDATES 32
typedef struct { uint64_t hard_safety_mask, hard_contract_mask, hard_resource_mask, hard_plugin_mask, hard_polytope_mask, hard_composite_mask;
    uint64_t domain_preference_mask, dynamic_telemetry_bias, soft_composite_bias; } FlowMaskCanvas;
typedef enum { FLOW_BMF_SUBSPACE_GENERIC = 0, FLOW_BMF_SUBSPACE_SMOOTH_FETCH_LATTE = 1, FLOW_BMF_SUBSPACE_AGILE_SPRINT = 2,
    FLOW_BMF_SUBSPACE_COLLABORATIVE_HOLD = 3, FLOW_BMF_SUBSPACE_EMERGENCY_PROTECT = 4, FLOW_BMF_SUBSPACE_HFT_LATENCY = 5,
    FLOW_BMF_SUBSPACE_QUIESCENT_IOT = 6, FLOW_BMF_SUBSPACE_MAX = 16 } FlowBmfSubspaceId;
#define FLOW_BMF_SUBSPACE_FEATURE_DIM 16
typedef struct { uint32_t subspace_id; char name[48], description[96]; double centroid[FLOW_BMF_SUBSPACE_FEATURE_DIM]; uint64_t invariant_mask, malleable_mask, default_switches; } FlowBmfSubspace;
typedef struct { FlowBmfSubspace subspaces[FLOW_BMF_SUBSPACE_MAX]; size_t subspace_count; int is_initialized; } FlowBmfSubspaceRegistry;
void flow_bmf_subspace_init_registry(FlowBmfSubspaceRegistry *reg); const FlowBmfSubspace *flow_bmf_subspace_lookup(const FlowBmfSubspaceRegistry *reg, uint32_t subspace_id);
uint32_t flow_bmf_subspace_index_from_features(const FlowBmfSubspaceRegistry *reg, const double *features, size_t dim);
void flow_bmf_canvas_init_from_subspace(FlowBmf1BitCanvas *canvas, const FlowBmfSubspace *subspace); int flow_bmf_canvas_adjudicate_smt(FlowBmf1BitCanvas *canvas);
void flow_bmf_canvas_to_mask_canvas(const FlowBmf1BitCanvas *canvas, FlowMaskCanvas *mask_canvas_out);
void flow_bmf_canvas_from_mask_canvas(const FlowMaskCanvas *mask_canvas, uint32_t subspace_id, FlowBmf1BitCanvas *canvas_out);
typedef struct { double energy, latency_score, throughput_score; size_t memory_bytes, capacity; uint64_t benchmark_ns; int hard_gate_passed; char message[128]; } FlowEvaluation;
#define FLOW_GENOME_MAX_WORDS 16
typedef struct { uint64_t words[FLOW_GENOME_MAX_WORDS]; uint32_t active_words, total_bits; } FlowGenome;
void flow_genome_init(FlowGenome *g, uint32_t total_bits); void flow_genome_flip_bit(FlowGenome *g, uint32_t bit_idx);
void flow_genome_mutate_1bit(FlowGenome *g, uint64_t *rng_state, uint32_t *mutated_bit_out);
typedef struct { const Component *component; FlowPlanDimensionSet dimension_set; FlowPlanAssignment assignment; FlowEvaluation eval;
    uint64_t genome, schema_hash, contract_hash; uint32_t bit_count; } FlowPlan;
typedef struct FlowBitSpace FlowBitSpace;
struct FlowBitSpace { const SemanticIR *ir; const Component *candidates[FLOW_BITSPACE_MAX_CANDIDATES]; FlowPlanDimensionSet candidate_dims[FLOW_BITSPACE_MAX_CANDIDATES];
    uint32_t candidate_bits[FLOW_BITSPACE_MAX_CANDIDATES]; uint64_t candidate_masks[FLOW_BITSPACE_MAX_CANDIDATES];
    FlowMaskCanvas candidate_canvases[FLOW_BITSPACE_MAX_CANDIDATES]; FlowMaskCanvas global_canvas; size_t candidate_count; uint32_t selector_bits, bit_count;
    uint64_t contract_hash, schema_hash, env_mask; int (*decode)(const FlowBitSpace *space, uint64_t genome, FlowPlan *plan);
    int (*evaluate)(const FlowBitSpace *space, const FlowPlan *plan, FlowEvaluation *result);
    int (*hard_gate)(const FlowBitSpace *space, const FlowPlan *plan, const FlowEvaluation *result); };
typedef struct FlowPlanArtifact { char flow_name[64], module_name[64], module_version[32], component_id[64]; uint64_t contract_hash, plan_schema_hash, genome; uint32_t seed, bit_count;
    FlowPlanDimensionSet dimensions; FlowPlanAssignment plan; FlowEvaluation metrics; char verification_status[32], attestation_msg[128]; double jet_omega, jet_barrier_mu, jet_q_saturation; } FlowPlanArtifact;
uint64_t flow_compute_contract_hash(const SemanticIR *ir); uint64_t flow_bitspace_compute_schema_hash(const SemanticIR *ir, const Component *comp, const FlowPlanDimensionSet *dims);
int flow_bitspace_init_single(const SemanticIR *ir, const Component *comp, FlowBitSpace *space); int flow_bitspace_init_for_ir(const SemanticIR *ir, FlowBitSpace *space);
uint64_t flow_bitspace_mutate_1bit(const FlowBitSpace *space, uint64_t genome, uint64_t *rng_state, uint32_t *mutated_bit_out);
uint64_t flow_bitspace_encode(const FlowBitSpace *space, size_t cand_idx, const FlowPlanAssignment *plan); uint64_t flow_bitspace_default_genome(const FlowBitSpace *space);
typedef enum { FLOW_GATE_PASS = 0, FLOW_GATE_FAIL_MEMORY_LIMIT, FLOW_GATE_FAIL_VERIFIER_UNPROVEN, FLOW_GATE_FAIL_THREAD_AFFINITY, FLOW_GATE_FAIL_QUOTA_EXCEEDED,
    FLOW_GATE_FAIL_DIMENSION_BOUND, FLOW_GATE_FAIL_REENTRANCY, FLOW_GATE_FAIL_MUTATION_INVALID, FLOW_GATE_CATEGORY_MAX } FlowGateFailureReason;
typedef struct { uint64_t total_mutations, total_failures, failure_counts[FLOW_GATE_CATEGORY_MAX]; } FlowSearchHeatmap;
typedef struct { FlowPlan best_plan; size_t pareto_count, iterations; FlowPlan pareto_points[FLOW_PARETO_MAX];
    double best_latency, best_memory, latency_regret_percent, memory_regret_percent; uint32_t seed; int measured; FlowSearchHeatmap heatmap; FlowMaskCanvas mask_canvas; } FlowBitSearchResult;
int flow_mask_canvas_compose(const SemanticIR *ir, const Component *comp, const FlowPlanDimensionSet *dims, const FlowPMUTelemetry *pmu, FlowMaskCanvas *canvas_out);
uint64_t flow_bitspace_mutate_1bit_superposed(const FlowBitSpace *space, uint64_t genome, const FlowMaskCanvas *canvas, double bias_weight, uint64_t *rng_state, uint32_t *mutated_bit_out);
int flow_bitspace_search(const FlowBitSpace *space, size_t iterations, uint32_t seed, int measured, const FlowPlan *seed_plan, FlowBitSearchResult *result_out);
typedef enum { FLOW_TACTIC_SPEED = 0, FLOW_TACTIC_BALANCED = 1, FLOW_TACTIC_MEMORY = 2, FLOW_TACTIC_COUNT = 3 } FlowPlanTactic;
typedef struct { FlowPlan tactics[FLOW_TACTIC_COUNT]; int available[FLOW_TACTIC_COUNT]; size_t count; } FlowPlanEnsemble;
const char *flow_plan_tactic_name(FlowPlanTactic tactic);
int flow_plan_artifact_save(FILE *output, const FlowPlanArtifact *artifact); int flow_plan_artifact_load(FILE *input, FlowPlanArtifact *artifact);
int flow_plan_to_artifact(const FlowPlan *plan, const SemanticIR *ir, uint32_t seed, FlowPlanArtifact *art);
int flow_artifact_validate(const FlowPlanArtifact *art, const SemanticIR *ir, const FlowBitSpace *space, char *err_msg, size_t err_size);
typedef struct { size_t buffer_bytes, initial_capacity; unsigned growth_percent; size_t batch_size, arena_bytes; } FlowTuning;
typedef struct { const Component *component; FlowPlanAssignment assignment; FlowPlanMetrics metrics; double energy; } FlowParetoPoint;
typedef struct { size_t count; FlowParetoPoint points[FLOW_PARETO_MAX]; double best_latency, best_memory, latency_regret_percent, memory_regret_percent; } FlowParetoSummary;
typedef struct FlowSearchResult { const Component *component; uint64_t genome; FlowPlanDimensionSet dimension_set; FlowPlanAssignment assignment; FlowPlanMetrics metrics;
    double capacity, threads, shards, energy; uint64_t benchmark_ns; int measured; size_t iterations; uint32_t seed; FlowTuning tuning; FlowParetoSummary pareto;
    FlowSearchHeatmap heatmap; FlowMaskCanvas mask_canvas; } SearchResult;
typedef struct { int available; size_t component, capacity, threads, shards; uint64_t benchmark_ns; size_t workload_bytes; FlowTuning tuning; FlowPlanAssignment assignment; } ProfileSeed;
uint64_t flow_plan_get_value(const FlowPlanDimensionSet *dims, const FlowPlanAssignment *plan, const char *name, uint64_t default_val);
FlowTuning flow_default_tuning(const SemanticIR *ir, const Component *component);
int flow_polyhedral_solve_plan(const SemanticIR *ir, const FlowBitSpace *space, FlowPlan *plan_out);
SearchResult search_best(const SemanticIR *ir, size_t iterations, uint32_t seed, int measured, const ProfileSeed *profile);
void flow_plan_to_search_result(const FlowPlan *plan, const SemanticIR *ir, uint32_t seed, SearchResult *out);
int flow_artifact_to_profile_seed(const FlowPlanArtifact *art, ProfileSeed *seed_out);
#define FLOW_SWARM_MAX_PARTICLES 32
typedef struct { uint32_t id; uint64_t current_genome, best_genome; FlowPlan best_plan; double current_energy, best_energy; uint64_t local_mask; uint32_t stagnation_steps; } FlowSwarmParticle;
typedef struct { double bit_intensity[64]; uint64_t consensus_mask; double evaporation_rate, reinforcement_weight; } FlowSwarmPheromone;
typedef struct { const FlowBitSpace *space; size_t particle_count; FlowSwarmParticle particles[FLOW_SWARM_MAX_PARTICLES]; FlowSwarmPheromone pheromone; FlowPlan global_best_plan;
    double global_best_energy; size_t total_swarm_mutations, total_saddle_point_escapes; uint64_t rng_state; } FlowSwarmCluster;
int flow_swarm_init(FlowSwarmCluster *cluster, const FlowBitSpace *space, size_t particle_count, uint32_t base_seed);
int flow_swarm_step(FlowSwarmCluster *cluster, size_t steps_per_particle); int flow_swarm_diffuse_pheromone(FlowSwarmCluster *cluster);
int flow_swarm_search(const FlowBitSpace *space, size_t particle_count, size_t cycles, uint32_t seed, int measured, FlowBitSearchResult *result_out);

/* =========================================================================
 * FLOW Zero-Float Exact Numerics Engine (C17 port of SEARU no_std_tool::numerics)
 *   1. math: Q16.16 (exp_approx_q16, rsqrt_approx_i32, silu_approx_i8) &
 *            Q32.32 (flow_fx64_t) Lossless Fixed-Point ALU + Bitwise IEEE-754 Codec
 *   2. bignum: U256 ([uint64_t; 4]), 512-bit mul_wide, Montgomery Reduction, Miller-Rabin
 *   3. linalg: Stack-allocated i-k-j Fixed-Point Matrix, MSE Loss & Gradient Descent
 *   4. random: Pure bitwise Xoshiro256** + SplitMix64 PRNG
 * ========================================================================= */
#ifndef FLOW_EXACT_NUMERICS_DEFINED
#define FLOW_EXACT_NUMERICS_DEFINED
__extension__ typedef __int128 flow_i128_t;
__extension__ typedef unsigned __int128 flow_u128_t;

#define FLOW_FX16_SHIFT 16
#define FLOW_FX16_ONE   (1 << FLOW_FX16_SHIFT)

typedef int64_t flow_fx64_t;
#define FLOW_FX_SHIFT   32
#define FLOW_FX_ONE     (INT64_C(1) << FLOW_FX_SHIFT)
#define FLOW_FX64_NONE  INT64_MIN

static inline int flow_exp_approx_q16(int32_t x, int32_t *out) {
    if (!out) {
        return 0;
    }
    if (x < -10 * FLOW_FX16_ONE) {
        *out = 0;
        return 1;
    }
    if (x > 10 * FLOW_FX16_ONE) {
        return 0;
    }
    int32_t x_scaled = (int32_t)(((int64_t)x * INT64_C(94548)) >> FLOW_FX16_SHIFT);
    int32_t int_part = x_scaled >> FLOW_FX16_SHIFT;
    int32_t frac_part = x_scaled & (FLOW_FX16_ONE - 1);
    int32_t approx_frac = FLOW_FX16_ONE + frac_part;
    if (int_part >= 0) {
        *out = (int32_t)((uint32_t)approx_frac << int_part);
    } else {
        *out = approx_frac >> (-int_part);
    }
    return 1;
}

static inline int flow_rsqrt_approx_i32(uint32_t x, uint32_t *out) {
    if (!out || x == 0U) {
        return 0;
    }
    uint32_t msb = 31U - (uint32_t)__builtin_clz(x);
    uint32_t sqrt_approx = 1U << (msb / 2U);
    uint32_t one_q16 = 1U << 16U;
    *out = one_q16 / sqrt_approx;
    return 1;
}

static inline int flow_silu_approx_i8(int8_t x, int8_t *out) {
    if (!out) {
        return 0;
    }
    int32_t x_q16 = (int32_t)((uint32_t)(int32_t)x << FLOW_FX16_SHIFT);
    int32_t exp_neg_x = 0;
    if (!flow_exp_approx_q16(-x_q16, &exp_neg_x)) {
        return 0;
    }
    int32_t denom = FLOW_FX16_ONE + exp_neg_x;
    if (denom == 0) {
        return 0;
    }
    int64_t result = ((int64_t)x_q16 * (int64_t)FLOW_FX16_ONE) / (int64_t)denom;
    int32_t res_i32 = (int32_t)(result >> FLOW_FX16_SHIFT);
    if (res_i32 < -128) {
        res_i32 = -128;
    } else if (res_i32 > 127) {
        res_i32 = 127;
    }
    *out = (int8_t)res_i32;
    return 1;
}

static inline flow_fx64_t flow_fx64_from_double(double d) {
    uint64_t bits = 0;
    memcpy(&bits, &d, sizeof(bits));
    uint32_t raw_exp = (uint32_t)((bits >> 52U) & 0x7FFU);
    if (raw_exp == 0x7FFU) {
        return FLOW_FX64_NONE;
    }
    if ((bits & UINT64_C(0x7FFFFFFFFFFFFFFF)) == 0ULL || raw_exp == 0U) {
        return 0;
    }
    int sign = (int)(bits >> 63U);
    int32_t exp = (int32_t)raw_exp - 1023;
    if (exp >= 31) {
        return FLOW_FX64_NONE;
    }
    if (exp < -32) {
        return 0;
    }
    uint64_t mant = (bits & UINT64_C(0x000FFFFFFFFFFFFF)) | UINT64_C(0x0010000000000000);
    int32_t shift = 20 - exp;
    uint64_t mag = 0;
    if (shift > 0) {
        mag = (mant + (UINT64_C(1) << (shift - 1))) >> shift;
    } else {
        mag = mant << (-shift);
    }
    if (mag > (uint64_t)INT64_MAX) {
        return FLOW_FX64_NONE;
    }
    return sign ? -(flow_fx64_t)mag : (flow_fx64_t)mag;
}

static inline double flow_fx64_to_double(flow_fx64_t x) {
    if (x == 0 || x == FLOW_FX64_NONE) {
        return 0.0;
    }
    uint64_t sign = (x < 0) ? (UINT64_C(1) << 63U) : 0ULL;
    uint64_t mag = (x < 0) ? (0ULL - (uint64_t)x) : (uint64_t)x;
    int msb = 63 - __builtin_clzll(mag);
    int32_t exp = msb - 32;
    uint64_t mant53 = (msb <= 52)
        ? (mag << (52 - msb))
        : ((mag + (UINT64_C(1) << (msb - 53))) >> (msb - 52));
    if (mant53 & (UINT64_C(1) << 53U)) {
        mant53 >>= 1U;
        exp++;
    }
    uint64_t bits = sign | ((uint64_t)(exp + 1023) << 52U) | (mant53 & UINT64_C(0x000FFFFFFFFFFFFF));
    double out = 0.0;
    memcpy(&out, &bits, sizeof(out));
    return out;
}

static inline flow_fx64_t flow_fx64_add(flow_fx64_t a, flow_fx64_t b) {
    if (a == FLOW_FX64_NONE || b == FLOW_FX64_NONE) {
        return FLOW_FX64_NONE;
    }
    flow_i128_t s = (flow_i128_t)a + (flow_i128_t)b;
    if (s > (flow_i128_t)INT64_MAX || s < -(flow_i128_t)INT64_MAX) {
        return FLOW_FX64_NONE;
    }
    return (flow_fx64_t)s;
}

static inline flow_fx64_t flow_fx64_sub(flow_fx64_t a, flow_fx64_t b) {
    if (a == FLOW_FX64_NONE || b == FLOW_FX64_NONE) {
        return FLOW_FX64_NONE;
    }
    flow_i128_t d = (flow_i128_t)a - (flow_i128_t)b;
    if (d > (flow_i128_t)INT64_MAX || d < -(flow_i128_t)INT64_MAX) {
        return FLOW_FX64_NONE;
    }
    return (flow_fx64_t)d;
}

static inline flow_fx64_t flow_fx64_neg(flow_fx64_t x) {
    return (x == FLOW_FX64_NONE) ? FLOW_FX64_NONE : -x;
}

static inline flow_fx64_t flow_fx64_abs(flow_fx64_t x) {
    if (x == FLOW_FX64_NONE) {
        return FLOW_FX64_NONE;
    }
    return x < 0 ? -x : x;
}

static inline flow_fx64_t flow_fx64_mul(flow_fx64_t a, flow_fx64_t b) {
    if (a == FLOW_FX64_NONE || b == FLOW_FX64_NONE) {
        return FLOW_FX64_NONE;
    }
    flow_i128_t prod = (flow_i128_t)a * (flow_i128_t)b;
    flow_i128_t half = (flow_i128_t)1 << 31;
    flow_i128_t res = (prod >= 0) ? ((prod + half) >> FLOW_FX_SHIFT) : -(((-prod) + half) >> FLOW_FX_SHIFT);
    if (res > (flow_i128_t)INT64_MAX || res < -(flow_i128_t)INT64_MAX) {
        return FLOW_FX64_NONE;
    }
    return (flow_fx64_t)res;
}

static inline flow_fx64_t flow_fx64_div(flow_fx64_t a, flow_fx64_t b) {
    if (a == FLOW_FX64_NONE || b == FLOW_FX64_NONE || b == 0) {
        return 0;
    }
    int neg = (a < 0) ^ (b < 0);
    flow_u128_t ua = (flow_u128_t)(a < 0 ? (0ULL - (uint64_t)a) : (uint64_t)a);
    flow_u128_t ub = (flow_u128_t)(b < 0 ? (0ULL - (uint64_t)b) : (uint64_t)b);
    flow_u128_t q = ((ua << FLOW_FX_SHIFT) + (ub >> 1U)) / ub;
    if (q > (flow_u128_t)INT64_MAX) {
        return FLOW_FX64_NONE;
    }
    return neg ? -(flow_fx64_t)q : (flow_fx64_t)q;
}

static inline flow_fx64_t flow_fx64_floor(flow_fx64_t x) {
    if (x == FLOW_FX64_NONE) {
        return FLOW_FX64_NONE;
    }
    return (flow_fx64_t)((uint64_t)x & ~UINT64_C(0xFFFFFFFF));
}

static inline flow_fx64_t flow_fx64_ceil(flow_fx64_t x) {
    if (x == FLOW_FX64_NONE) {
        return FLOW_FX64_NONE;
    }
    return -flow_fx64_floor(-x);
}

static inline flow_fx64_t flow_fx64_round(flow_fx64_t x) {
    if (x == FLOW_FX64_NONE) {
        return FLOW_FX64_NONE;
    }
    return (x >= 0) ? flow_fx64_floor(x + INT64_C(0x80000000)) : -flow_fx64_floor((-x) + INT64_C(0x80000000));
}

static inline flow_fx64_t flow_fx64_max(flow_fx64_t a, flow_fx64_t b) {
    if (a == FLOW_FX64_NONE || b == FLOW_FX64_NONE) {
        return FLOW_FX64_NONE;
    }
    return a > b ? a : b;
}

static inline flow_fx64_t flow_fx64_min(flow_fx64_t a, flow_fx64_t b) {
    if (a == FLOW_FX64_NONE || b == FLOW_FX64_NONE) {
        return FLOW_FX64_NONE;
    }
    return a < b ? a : b;
}

static inline flow_fx64_t flow_fx64_sqrt(flow_fx64_t x) {
    if (x <= 0 || x == FLOW_FX64_NONE) {
        return 0;
    }
    uint64_t ux = (uint64_t)x;
    int msb = 63 - __builtin_clzll(ux);
    flow_u128_t target = ((flow_u128_t)ux) << FLOW_FX_SHIFT;
    uint64_t y = UINT64_C(1) << ((msb + 32) / 2);
    for (int i = 0; i < 6; ++i) {
        y = (uint64_t)(((flow_u128_t)y + target / y) >> 1U);
    }
    return (flow_fx64_t)y;
}

static inline flow_fx64_t flow_fx64_exp(flow_fx64_t x) {
    if (x == FLOW_FX64_NONE) {
        return FLOW_FX64_NONE;
    }
    if (x <= -22 * FLOW_FX_ONE) {
        return 0;
    }
    if (x >= 21 * FLOW_FX_ONE) {
        return FLOW_FX64_NONE;
    }
    /* Base-2 split: k = round(x * log2(e)), r = x - k * ln(2), |r| <= ln(2)/2 */
    int64_t x_log2e = (int64_t)(((flow_i128_t)x * INT64_C(6196328019)) >> FLOW_FX_SHIFT);
    int32_t k = (int32_t)((x_log2e >= 0 ? (x_log2e + INT64_C(0x80000000)) : (x_log2e - INT64_C(0x80000000))) >> FLOW_FX_SHIFT);
    flow_fx64_t r = x - (flow_fx64_t)k * INT64_C(2977044472);
    /* 7th-order Horner Taylor polynomial in Q32.32: 1 + r*(1 + r*(1/2 + r*(1/6 + r*(1/24 + r*(1/120 + r*(1/720 + r/5040)))))) */
    flow_fx64_t p = INT64_C(852176);                /* 1/5040 in Q32.32 */
    p = INT64_C(5965232) + flow_fx64_mul(r, p);     /* 1/720 */
    p = INT64_C(35791394) + flow_fx64_mul(r, p);    /* 1/120 */
    p = INT64_C(178956971) + flow_fx64_mul(r, p);   /* 1/24  */
    p = INT64_C(715827883) + flow_fx64_mul(r, p);   /* 1/6   */
    p = INT64_C(2147483648) + flow_fx64_mul(r, p);  /* 1/2   */
    p = FLOW_FX_ONE + flow_fx64_mul(r, p);          /* 1     */
    p = FLOW_FX_ONE + flow_fx64_mul(r, p);          /* 1     */
    if (k >= 0) {
        if (k >= 31 || (p >> (62 - k)) > 0) {
            return FLOW_FX64_NONE;
        }
        return (flow_fx64_t)((uint64_t)p << k);
    }
    if (k <= -62) {
        return 0;
    }
    return p >> (-k);
}

static inline flow_fx64_t flow_fx64_log(flow_fx64_t x) {
    if (x <= 0 || x == FLOW_FX64_NONE) {
        return 0;
    }
    uint64_t ux = (uint64_t)x;
    int msb = 63 - __builtin_clzll(ux);
    int32_t k = msb - 32;
    flow_fx64_t m = (msb >= 32) ? (flow_fx64_t)(ux >> (msb - 32)) : (flow_fx64_t)(ux << (32 - msb));
    flow_fx64_t z = flow_fx64_div(m - FLOW_FX_ONE, m + FLOW_FX_ONE);
    flow_fx64_t z2 = flow_fx64_mul(z, z);
    flow_fx64_t poly = INT64_C(613566757); /* 1/7 in Q32.32 */
    poly = INT64_C(858993459) + flow_fx64_mul(z2, poly);  /* 1/5 */
    poly = INT64_C(1431655765) + flow_fx64_mul(z2, poly); /* 1/3 */
    poly = FLOW_FX_ONE + flow_fx64_mul(z2, poly);         /* 1   */
    flow_fx64_t ln_m = flow_fx64_mul(2 * z, poly);
    return (flow_fx64_t)k * INT64_C(2977044472) + ln_m;
}

static inline flow_fx64_t flow_fx64_pow(flow_fx64_t base, flow_fx64_t exp) {
    if (base == FLOW_FX64_NONE || exp == FLOW_FX64_NONE) {
        return FLOW_FX64_NONE;
    }
    if (exp == 0) {
        return FLOW_FX_ONE;
    }
    if (((uint64_t)exp & UINT64_C(0xFFFFFFFF)) == 0ULL && exp > 0 && exp <= (64LL * FLOW_FX_ONE)) {
        uint64_t n = (uint64_t)(exp >> FLOW_FX_SHIFT);
        flow_fx64_t r = FLOW_FX_ONE, b = base;
        while (n > 0ULL) {
            if (n & 1ULL) {
                r = flow_fx64_mul(r, b);
            }
            b = flow_fx64_mul(b, b);
            n >>= 1U;
        }
        return r;
    }
    if (base <= 0) {
        return 0;
    }
    return flow_fx64_exp(flow_fx64_mul(exp, flow_fx64_log(base)));
}

static inline flow_fx64_t flow_fx64_sin(flow_fx64_t x) {
    if (x == FLOW_FX64_NONE) {
        return FLOW_FX64_NONE;
    }
    const flow_fx64_t TWO_PI = INT64_C(26986075409);
    const flow_fx64_t PI     = INT64_C(13493037705);
    const flow_fx64_t HALF_PI = INT64_C(6746518852);
    flow_fx64_t r = x % TWO_PI;
    if (r > PI) {
        r -= TWO_PI;
    } else if (r < -PI) {
        r += TWO_PI;
    }
    if (r > HALF_PI) {
        r = PI - r;
    } else if (r < -HALF_PI) {
        r = -PI - r;
    }
    if (r == HALF_PI) {
        return FLOW_FX_ONE;
    } else if (r == -HALF_PI) {
        return -FLOW_FX_ONE;
    }
    flow_fx64_t r2 = flow_fx64_mul(r, r);
    flow_fx64_t p = INT64_C(11836);                  /* +1/362880 in Q32.32 */
    p = -INT64_C(852176) + flow_fx64_mul(r2, p);     /* -1/5040 */
    p = INT64_C(35791394) + flow_fx64_mul(r2, p);    /* +1/120  */
    p = -INT64_C(715827883) + flow_fx64_mul(r2, p);  /* -1/6    */
    p = FLOW_FX_ONE + flow_fx64_mul(r2, p);
    return flow_fx64_mul(r, p);
}

static inline flow_fx64_t flow_fx64_cos(flow_fx64_t x) {
    if (x == FLOW_FX64_NONE) {
        return FLOW_FX64_NONE;
    }
    if (x == 0) {
        return FLOW_FX_ONE;
    }
    return flow_fx64_sin(x + INT64_C(6746518852));
}

static inline flow_fx64_t flow_fx64_atan2(flow_fx64_t y, flow_fx64_t x) {
    if (y == FLOW_FX64_NONE || x == FLOW_FX64_NONE || (y == 0 && x == 0)) {
        return 0;
    }
    const flow_fx64_t PI      = INT64_C(13493037705);
    const flow_fx64_t HALF_PI = INT64_C(6746518852);
    const flow_fx64_t QTR_PI  = INT64_C(3373259426);
    flow_fx64_t abs_y = flow_fx64_abs(y) + 1;
    flow_fx64_t angle = 0;
    if (x >= 0) {
        flow_fx64_t r = flow_fx64_div(x - abs_y, x + abs_y);
        flow_fx64_t r2 = flow_fx64_mul(r, r);
        angle = QTR_PI + flow_fx64_mul(flow_fx64_mul(INT64_C(843018725), r2) - INT64_C(4216574283), r);
    } else {
        flow_fx64_t r = flow_fx64_div(x + abs_y, abs_y - x);
        flow_fx64_t r2 = flow_fx64_mul(r, r);
        angle = (3 * QTR_PI) + flow_fx64_mul(flow_fx64_mul(INT64_C(843018725), r2) - INT64_C(4216574283), r);
    }
    if (x == 0) {
        angle = HALF_PI;
    } else if (y == 0) {
        return x < 0 ? PI : 0;
    }
    return y < 0 ? -angle : angle;
}

static inline flow_fx64_t flow_fx64_of_nat(uint64_t x) {
    if (x > UINT64_C(2147483647)) {
        return FLOW_FX64_NONE;
    }
    return (flow_fx64_t)(x << 32);
}

static inline flow_fx64_t flow_fx64_of_int(int64_t x) {
    if (x > INT64_C(2147483647) || x < INT64_C(-2147483647)) {
        return FLOW_FX64_NONE;
    }
    return (flow_fx64_t)((uint64_t)x << 32);
}

static inline flow_fx64_t flow_fx64_mod(flow_fx64_t a, flow_fx64_t b) {
    if (a == FLOW_FX64_NONE || b == FLOW_FX64_NONE || b == 0) {
        return 0;
    }
    return a % b;
}

static inline uint64_t flow_quantize_fx(flow_fx64_t val, flow_fx64_t min_v, flow_fx64_t max_v, uint32_t bits) {
    if (val == FLOW_FX64_NONE || min_v == FLOW_FX64_NONE || max_v == FLOW_FX64_NONE || max_v <= min_v || bits == 0 || bits > 32) {
        return 0ULL;
    }
    flow_fx64_t c = val < min_v ? min_v : (val > max_v ? max_v : val);
    uint64_t max_q = (1ULL << bits) - 1ULL;
    flow_u128_t num = (flow_u128_t)(uint64_t)(c - min_v) * (flow_u128_t)max_q;
    flow_u128_t den = (flow_u128_t)(uint64_t)(max_v - min_v);
    return (uint64_t)((num + (den >> 1)) / den);
}

static inline flow_fx64_t flow_dequantize_fx(uint64_t code, flow_fx64_t min_v, flow_fx64_t max_v, uint32_t bits) {
    if (min_v == FLOW_FX64_NONE || max_v == FLOW_FX64_NONE || max_v <= min_v || bits == 0 || bits > 32) {
        return 0;
    }
    uint64_t max_q = (1ULL << bits) - 1ULL;
    uint64_t c = code > max_q ? max_q : code;
    flow_u128_t span = (flow_u128_t)(uint64_t)(max_v - min_v);
    flow_fx64_t delta = (flow_fx64_t)(((flow_u128_t)c * span + (max_q >> 1)) / max_q);
    return min_v + delta;
}

typedef struct {
    uint64_t w[4];
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
void flow_u256_mul_wide(FlowU256 a, FlowU256 b, uint64_t out_512[8]);
FlowU256 flow_u256_mul(FlowU256 a, FlowU256 b);
int flow_u256_div_rem(FlowU256 a, FlowU256 b, FlowU256 *q_out, FlowU256 *r_out);
uint64_t flow_u256_inv_word(const FlowU256 *n);
FlowU256 flow_u256_mont_reduce(const uint64_t t_in[8], const FlowU256 *modulus, uint64_t n0_inv);
FlowU256 flow_u256_mod_pow(FlowU256 base, FlowU256 exp, FlowU256 modulus);
int flow_u256_is_probably_prime(const FlowU256 *n, const uint64_t *bases, size_t base_count);

typedef struct {
    uint64_t s[4];
} FlowXoshiro256SS;

void flow_xoshiro256ss_init(FlowXoshiro256SS *rng, uint64_t seed);
uint64_t flow_xoshiro256ss_next_u64(FlowXoshiro256SS *rng);
int flow_xoshiro256ss_next_bool(FlowXoshiro256SS *rng);

void flow_fx_mat_mul_ikj(size_t R, size_t K, size_t C, const flow_fx64_t *a, const flow_fx64_t *b, flow_fx64_t *out);
flow_fx64_t flow_fx_mse_loss(size_t N, size_t OUT, const flow_fx64_t *predictions, const flow_fx64_t *targets);
void flow_fx_gradient_descent_step(size_t N, size_t IN, size_t OUT,
                                   const flow_fx64_t *inputs, flow_fx64_t *weights,
                                   const flow_fx64_t *predictions, const flow_fx64_t *targets,
                                   flow_fx64_t learning_rate);
#endif /* FLOW_EXACT_NUMERICS_DEFINED */
#endif
