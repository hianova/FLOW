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

#define FLOW_STR_INNER(...) #__VA_ARGS__
#define FLOW_STR(...)       FLOW_STR_INNER(__VA_ARGS__)

#define FLOW_FX16_SHIFT      16
#define FLOW_FX16_ONE        (1 << FLOW_FX16_SHIFT)
#define FLOW_FX16_EXP_BOUND  (10 * FLOW_FX16_ONE)
#define FLOW_FX16_LOG2E      INT64_C(94548)

typedef int64_t flow_fx64_t;
#define FLOW_FX_SHIFT        32
#define FLOW_FX_ONE          (INT64_C(1) << FLOW_FX_SHIFT)
#define FLOW_FX_HALF         (INT64_C(1) << (FLOW_FX_SHIFT - 1))
#define FLOW_FX_FRAC_MASK    (FLOW_FX_ONE - 1)
#define FLOW_FX64_NONE       INT64_MIN
#define FLOW_FX_PI           INT64_C(13493037705)
#define FLOW_FX_TWO_PI       INT64_C(26986075409)
#define FLOW_FX_HALF_PI      INT64_C(6746518852)
#define FLOW_FX_QTR_PI       INT64_C(3373259426)
#define FLOW_FX_LN2          INT64_C(2977044472)
#define FLOW_FX_INV_LN2      INT64_C(6196328019)
#define FLOW_FX_EXP_MIN      (-22 * FLOW_FX_ONE)
#define FLOW_FX_EXP_MAX      (21 * FLOW_FX_ONE)
#define FLOW_FX_INV_FACT3    INT64_C(715827883)
#define FLOW_FX_INV_FACT4    INT64_C(178956971)
#define FLOW_FX_INV_FACT5    INT64_C(35791394)
#define FLOW_FX_INV_FACT6    INT64_C(5965232)
#define FLOW_FX_INV_FACT7    INT64_C(852176)
#define FLOW_FX_INV_FACT9    INT64_C(11836)
#define FLOW_FX_INV_3        INT64_C(1431655765)
#define FLOW_FX_INV_5        INT64_C(858993459)
#define FLOW_FX_INV_7        INT64_C(613566757)
#define FLOW_FX_ATAN_C0      INT64_C(4216574283)
#define FLOW_FX_ATAN_C1      INT64_C(843018725)
#define FLOW_NEWTON_64BIT_ITERS 6

#define FLOW_IEEE754_SIGN_SHIFT   63U
#define FLOW_IEEE754_MANT_BITS    52U
#define FLOW_IEEE754_EXP_MASK     UINT64_C(0x7FF)
#define FLOW_IEEE754_EXP_BIAS     1023
#define FLOW_IEEE754_MANT_MASK    UINT64_C(0x000FFFFFFFFFFFFF)
#define FLOW_IEEE754_IMPLICIT_BIT (UINT64_C(1) << FLOW_IEEE754_MANT_BITS)
#define FLOW_IEEE754_ABS_MASK     UINT64_C(0x7FFFFFFFFFFFFFFF)
#define FLOW_FX_MAX_INT_EXP       31
#define FLOW_FX_MIN_SUBNORM_EXP   (-32)
#define FLOW_FX_MANT_TO_Q32_SHIFT 20

#define FLOW_U256_LIMBS           4
#define FLOW_U512_LIMBS           8
#define FLOW_U256_BITS            256U
#define FLOW_WORD_BITS            64U
#define FLOW_SPLITMIX64_MUL1      UINT64_C(0xbf58476d1ce4e5b9)
#define FLOW_SPLITMIX64_MUL2      UINT64_C(0x94d049bb133111eb)

#define FLOW_EXACT_CODEC_C17 \
static inline __attribute__((unused)) int flow_exp_approx_q16(int32_t x, int32_t *out) { \
    if (!out) { return 0; } \
    if (x < -FLOW_FX16_EXP_BOUND) { *out = 0; return 1; } \
    if (x > FLOW_FX16_EXP_BOUND) { return 0; } \
    int32_t x_scaled = (int32_t)(((int64_t)x * FLOW_FX16_LOG2E) >> FLOW_FX16_SHIFT); \
    int32_t int_part = x_scaled >> FLOW_FX16_SHIFT; \
    int32_t frac_part = x_scaled & (FLOW_FX16_ONE - 1); \
    int32_t approx_frac = FLOW_FX16_ONE + frac_part; \
    if (int_part >= 0) { *out = (int32_t)((uint32_t)approx_frac << int_part); } \
    else { *out = approx_frac >> (-int_part); } \
    return 1; \
} \
static inline __attribute__((unused)) int flow_rsqrt_approx_i32(uint32_t x, uint32_t *out) { \
    if (!out || x == 0U) { return 0; } \
    uint32_t msb = 31U - (uint32_t)__builtin_clz(x); \
    uint32_t sqrt_approx = 1U << (msb >> 1U); \
    *out = (uint32_t)FLOW_FX16_ONE / sqrt_approx; \
    return 1; \
} \
static inline __attribute__((unused)) int flow_silu_approx_i8(int8_t x, int8_t *out) { \
    if (!out) { return 0; } \
    int32_t x_q16 = (int32_t)((uint32_t)(int32_t)x << FLOW_FX16_SHIFT); \
    int32_t exp_neg_x = 0; \
    if (!flow_exp_approx_q16(-x_q16, &exp_neg_x)) { return 0; } \
    int32_t denom = FLOW_FX16_ONE + exp_neg_x; \
    if (denom == 0) { return 0; } \
    int64_t result = ((int64_t)x_q16 * (int64_t)FLOW_FX16_ONE) / (int64_t)denom; \
    int32_t res_i32 = (int32_t)(result >> FLOW_FX16_SHIFT); \
    if (res_i32 < INT8_MIN) { res_i32 = INT8_MIN; } \
    else if (res_i32 > INT8_MAX) { res_i32 = INT8_MAX; } \
    *out = (int8_t)res_i32; \
    return 1; \
} \
static inline __attribute__((unused)) flow_fx64_t flow_fx64_from_double(double d) { \
    uint64_t bits = 0; \
    memcpy(&bits, &d, sizeof(bits)); \
    uint32_t raw_exp = (uint32_t)((bits >> FLOW_IEEE754_MANT_BITS) & FLOW_IEEE754_EXP_MASK); \
    if (raw_exp == FLOW_IEEE754_EXP_MASK) { return FLOW_FX64_NONE; } \
    if ((bits & FLOW_IEEE754_ABS_MASK) == 0ULL || raw_exp == 0U) { return 0; } \
    int sign = (int)(bits >> FLOW_IEEE754_SIGN_SHIFT); \
    int32_t exp = (int32_t)raw_exp - FLOW_IEEE754_EXP_BIAS; \
    if (exp >= FLOW_FX_MAX_INT_EXP) { return FLOW_FX64_NONE; } \
    if (exp < FLOW_FX_MIN_SUBNORM_EXP) { return 0; } \
    uint64_t mant = (bits & FLOW_IEEE754_MANT_MASK) | FLOW_IEEE754_IMPLICIT_BIT; \
    int32_t shift = FLOW_FX_MANT_TO_Q32_SHIFT - exp; \
    uint64_t mag = (shift > 0) ? ((mant + (UINT64_C(1) << (shift - 1))) >> shift) : (mant << (-shift)); \
    if (mag > (uint64_t)INT64_MAX) { return FLOW_FX64_NONE; } \
    return sign ? -(flow_fx64_t)mag : (flow_fx64_t)mag; \
} \
static inline __attribute__((unused)) double flow_fx64_to_double(flow_fx64_t x) { \
    if (x == 0 || x == FLOW_FX64_NONE) { return 0.0; } \
    uint64_t sign = (x < 0) ? (UINT64_C(1) << FLOW_IEEE754_SIGN_SHIFT) : 0ULL; \
    uint64_t mag = (x < 0) ? (0ULL - (uint64_t)x) : (uint64_t)x; \
    int msb = (int)(FLOW_WORD_BITS - 1U) - __builtin_clzll(mag); \
    int32_t exp = msb - FLOW_FX_SHIFT; \
    uint64_t mant53 = (msb <= (int)FLOW_IEEE754_MANT_BITS) \
        ? (mag << ((int)FLOW_IEEE754_MANT_BITS - msb)) \
        : ((mag + (UINT64_C(1) << (msb - ((int)FLOW_IEEE754_MANT_BITS + 1)))) >> (msb - (int)FLOW_IEEE754_MANT_BITS)); \
    if (mant53 & (UINT64_C(1) << (FLOW_IEEE754_MANT_BITS + 1U))) { mant53 >>= 1U; exp++; } \
    uint64_t bits = sign | ((uint64_t)(exp + FLOW_IEEE754_EXP_BIAS) << FLOW_IEEE754_MANT_BITS) | (mant53 & FLOW_IEEE754_MANT_MASK); \
    double out = 0.0; \
    memcpy(&out, &bits, sizeof(out)); \
    return out; \
}

#define FLOW_EXACT_ALU_C17 \
static inline __attribute__((unused)) flow_fx64_t flow_fx64_add(flow_fx64_t a, flow_fx64_t b) { \
    if (a == FLOW_FX64_NONE || b == FLOW_FX64_NONE) { return FLOW_FX64_NONE; } \
    flow_i128_t s = (flow_i128_t)a + (flow_i128_t)b; \
    if (s > (flow_i128_t)INT64_MAX || s < -(flow_i128_t)INT64_MAX) { return FLOW_FX64_NONE; } \
    return (flow_fx64_t)s; \
} \
static inline __attribute__((unused)) flow_fx64_t flow_fx64_sub(flow_fx64_t a, flow_fx64_t b) { \
    if (a == FLOW_FX64_NONE || b == FLOW_FX64_NONE) { return FLOW_FX64_NONE; } \
    flow_i128_t d = (flow_i128_t)a - (flow_i128_t)b; \
    if (d > (flow_i128_t)INT64_MAX || d < -(flow_i128_t)INT64_MAX) { return FLOW_FX64_NONE; } \
    return (flow_fx64_t)d; \
} \
static inline __attribute__((unused)) flow_fx64_t flow_fx64_neg(flow_fx64_t x) { \
    return (x == FLOW_FX64_NONE) ? FLOW_FX64_NONE : -x; \
} \
static inline __attribute__((unused)) flow_fx64_t flow_fx64_abs(flow_fx64_t x) { \
    if (x == FLOW_FX64_NONE) { return FLOW_FX64_NONE; } \
    return x < 0 ? -x : x; \
} \
static inline __attribute__((unused)) flow_fx64_t flow_fx64_mul(flow_fx64_t a, flow_fx64_t b) { \
    if (a == FLOW_FX64_NONE || b == FLOW_FX64_NONE) { return FLOW_FX64_NONE; } \
    flow_i128_t prod = (flow_i128_t)a * (flow_i128_t)b; \
    flow_i128_t half = (flow_i128_t)FLOW_FX_HALF; \
    flow_i128_t res = (prod >= 0) ? ((prod + half) >> FLOW_FX_SHIFT) : -(((-prod) + half) >> FLOW_FX_SHIFT); \
    if (res > (flow_i128_t)INT64_MAX || res < -(flow_i128_t)INT64_MAX) { return FLOW_FX64_NONE; } \
    return (flow_fx64_t)res; \
} \
static inline __attribute__((unused)) flow_fx64_t flow_fx64_div(flow_fx64_t a, flow_fx64_t b) { \
    if (a == FLOW_FX64_NONE || b == FLOW_FX64_NONE || b == 0) { return 0; } \
    int neg = (a < 0) ^ (b < 0); \
    flow_u128_t ua = (flow_u128_t)(a < 0 ? (0ULL - (uint64_t)a) : (uint64_t)a); \
    flow_u128_t ub = (flow_u128_t)(b < 0 ? (0ULL - (uint64_t)b) : (uint64_t)b); \
    flow_u128_t q = ((ua << FLOW_FX_SHIFT) + (ub >> 1U)) / ub; \
    if (q > (flow_u128_t)INT64_MAX) { return FLOW_FX64_NONE; } \
    return neg ? -(flow_fx64_t)q : (flow_fx64_t)q; \
} \
static inline __attribute__((unused)) flow_fx64_t flow_fx64_mod(flow_fx64_t a, flow_fx64_t b) { \
    if (a == FLOW_FX64_NONE || b == FLOW_FX64_NONE || b == 0) { return 0; } \
    return a % b; \
} \
static inline __attribute__((unused)) flow_fx64_t flow_fx64_floor(flow_fx64_t x) { \
    if (x == FLOW_FX64_NONE) { return FLOW_FX64_NONE; } \
    return (flow_fx64_t)((uint64_t)x & ~(uint64_t)FLOW_FX_FRAC_MASK); \
} \
static inline __attribute__((unused)) flow_fx64_t flow_fx64_ceil(flow_fx64_t x) { \
    if (x == FLOW_FX64_NONE) { return FLOW_FX64_NONE; } \
    return -flow_fx64_floor(-x); \
} \
static inline __attribute__((unused)) flow_fx64_t flow_fx64_round(flow_fx64_t x) { \
    if (x == FLOW_FX64_NONE) { return FLOW_FX64_NONE; } \
    return (x >= 0) ? flow_fx64_floor(x + FLOW_FX_HALF) : -flow_fx64_floor((-x) + FLOW_FX_HALF); \
} \
static inline __attribute__((unused)) flow_fx64_t flow_fx64_max(flow_fx64_t a, flow_fx64_t b) { \
    if (a == FLOW_FX64_NONE || b == FLOW_FX64_NONE) { return FLOW_FX64_NONE; } \
    return a > b ? a : b; \
} \
static inline __attribute__((unused)) flow_fx64_t flow_fx64_min(flow_fx64_t a, flow_fx64_t b) { \
    if (a == FLOW_FX64_NONE || b == FLOW_FX64_NONE) { return FLOW_FX64_NONE; } \
    return a < b ? a : b; \
} \
static inline __attribute__((unused)) flow_fx64_t flow_fx64_of_nat(uint64_t x) { \
    if (x > (uint64_t)INT32_MAX) { return FLOW_FX64_NONE; } \
    return (flow_fx64_t)(x << FLOW_FX_SHIFT); \
} \
static inline __attribute__((unused)) flow_fx64_t flow_fx64_of_int(int64_t x) { \
    if (x > (int64_t)INT32_MAX || x < -(int64_t)INT32_MAX) { return FLOW_FX64_NONE; } \
    return (flow_fx64_t)((uint64_t)x << FLOW_FX_SHIFT); \
}

#define FLOW_EXACT_EXP_LOG_C17 \
static inline __attribute__((unused)) flow_fx64_t flow_fx64_sqrt(flow_fx64_t x) { \
    if (x <= 0 || x == FLOW_FX64_NONE) { return 0; } \
    uint64_t ux = (uint64_t)x; \
    int msb = (int)(FLOW_WORD_BITS - 1U) - __builtin_clzll(ux); \
    flow_u128_t target = ((flow_u128_t)ux) << FLOW_FX_SHIFT; \
    uint64_t y = UINT64_C(1) << ((msb + FLOW_FX_SHIFT) / 2); \
    for (int i = 0; i < FLOW_NEWTON_64BIT_ITERS; ++i) { \
        y = (uint64_t)(((flow_u128_t)y + target / y) >> 1U); \
    } \
    return (flow_fx64_t)y; \
} \
static inline __attribute__((unused)) flow_fx64_t flow_fx64_exp(flow_fx64_t x) { \
    if (x == FLOW_FX64_NONE) { return FLOW_FX64_NONE; } \
    if (x <= FLOW_FX_EXP_MIN) { return 0; } \
    if (x >= FLOW_FX_EXP_MAX) { return FLOW_FX64_NONE; } \
    int64_t x_log2e = (int64_t)(((flow_i128_t)x * FLOW_FX_INV_LN2) >> FLOW_FX_SHIFT); \
    int32_t k = (int32_t)((x_log2e >= 0 ? (x_log2e + FLOW_FX_HALF) : (x_log2e - FLOW_FX_HALF)) >> FLOW_FX_SHIFT); \
    flow_fx64_t r = x - (flow_fx64_t)k * FLOW_FX_LN2; \
    flow_fx64_t p = FLOW_FX_INV_FACT7; \
    p = FLOW_FX_INV_FACT6 + flow_fx64_mul(r, p); \
    p = FLOW_FX_INV_FACT5 + flow_fx64_mul(r, p); \
    p = FLOW_FX_INV_FACT4 + flow_fx64_mul(r, p); \
    p = FLOW_FX_INV_FACT3 + flow_fx64_mul(r, p); \
    p = FLOW_FX_HALF + flow_fx64_mul(r, p); \
    p = FLOW_FX_ONE + flow_fx64_mul(r, p); \
    p = FLOW_FX_ONE + flow_fx64_mul(r, p); \
    if (k >= 0) { \
        if (k >= FLOW_FX_MAX_INT_EXP || (p >> ((int)(FLOW_WORD_BITS - 2U) - k)) > 0) { return FLOW_FX64_NONE; } \
        return (flow_fx64_t)((uint64_t)p << k); \
    } \
    if (k <= -(int)(FLOW_WORD_BITS - 2U)) { return 0; } \
    return p >> (-k); \
} \
static inline __attribute__((unused)) flow_fx64_t flow_fx64_log(flow_fx64_t x) { \
    if (x <= 0 || x == FLOW_FX64_NONE) { return 0; } \
    uint64_t ux = (uint64_t)x; \
    int msb = (int)(FLOW_WORD_BITS - 1U) - __builtin_clzll(ux); \
    int32_t k = msb - FLOW_FX_SHIFT; \
    flow_fx64_t m = (msb >= FLOW_FX_SHIFT) ? (flow_fx64_t)(ux >> (msb - FLOW_FX_SHIFT)) : (flow_fx64_t)(ux << (FLOW_FX_SHIFT - msb)); \
    flow_fx64_t z = flow_fx64_div(m - FLOW_FX_ONE, m + FLOW_FX_ONE); \
    flow_fx64_t z2 = flow_fx64_mul(z, z); \
    flow_fx64_t poly = FLOW_FX_INV_7; \
    poly = FLOW_FX_INV_5 + flow_fx64_mul(z2, poly); \
    poly = FLOW_FX_INV_3 + flow_fx64_mul(z2, poly); \
    poly = FLOW_FX_ONE + flow_fx64_mul(z2, poly); \
    flow_fx64_t ln_m = flow_fx64_mul(2 * z, poly); \
    return (flow_fx64_t)k * FLOW_FX_LN2 + ln_m; \
} \
static inline __attribute__((unused)) flow_fx64_t flow_fx64_pow(flow_fx64_t base, flow_fx64_t exp) { \
    if (base == FLOW_FX64_NONE || exp == FLOW_FX64_NONE) { return FLOW_FX64_NONE; } \
    if (exp == 0) { return FLOW_FX_ONE; } \
    if (((uint64_t)exp & (uint64_t)FLOW_FX_FRAC_MASK) == 0ULL && exp > 0 && exp <= ((flow_fx64_t)FLOW_WORD_BITS * FLOW_FX_ONE)) { \
        uint64_t n = (uint64_t)(exp >> FLOW_FX_SHIFT); \
        flow_fx64_t r = FLOW_FX_ONE, b = base; \
        while (n > 0ULL) { \
            if (n & 1ULL) { r = flow_fx64_mul(r, b); } \
            b = flow_fx64_mul(b, b); \
            n >>= 1U; \
        } \
        return r; \
    } \
    if (base <= 0) { return 0; } \
    return flow_fx64_exp(flow_fx64_mul(exp, flow_fx64_log(base))); \
}

#define FLOW_EXACT_TRIG_QUANT_C17 \
static inline __attribute__((unused)) flow_fx64_t flow_fx64_sin(flow_fx64_t x) { \
    if (x == FLOW_FX64_NONE) { return FLOW_FX64_NONE; } \
    flow_fx64_t r = x % FLOW_FX_TWO_PI; \
    if (r > FLOW_FX_PI) { r -= FLOW_FX_TWO_PI; } \
    else if (r < -FLOW_FX_PI) { r += FLOW_FX_TWO_PI; } \
    if (r > FLOW_FX_HALF_PI) { r = FLOW_FX_PI - r; } \
    else if (r < -FLOW_FX_HALF_PI) { r = -FLOW_FX_PI - r; } \
    if (r == FLOW_FX_HALF_PI) { return FLOW_FX_ONE; } \
    else if (r == -FLOW_FX_HALF_PI) { return -FLOW_FX_ONE; } \
    flow_fx64_t r2 = flow_fx64_mul(r, r); \
    flow_fx64_t p = FLOW_FX_INV_FACT9; \
    p = -FLOW_FX_INV_FACT7 + flow_fx64_mul(r2, p); \
    p = FLOW_FX_INV_FACT5 + flow_fx64_mul(r2, p); \
    p = -FLOW_FX_INV_FACT3 + flow_fx64_mul(r2, p); \
    p = FLOW_FX_ONE + flow_fx64_mul(r2, p); \
    return flow_fx64_mul(r, p); \
} \
static inline __attribute__((unused)) flow_fx64_t flow_fx64_cos(flow_fx64_t x) { \
    if (x == FLOW_FX64_NONE) { return FLOW_FX64_NONE; } \
    if (x == 0) { return FLOW_FX_ONE; } \
    return flow_fx64_sin(x + FLOW_FX_HALF_PI); \
} \
static inline __attribute__((unused)) flow_fx64_t flow_fx64_atan2(flow_fx64_t y, flow_fx64_t x) { \
    if (y == FLOW_FX64_NONE || x == FLOW_FX64_NONE || (y == 0 && x == 0)) { return 0; } \
    flow_fx64_t abs_y = flow_fx64_abs(y) + 1; \
    flow_fx64_t angle = 0; \
    if (x >= 0) { \
        flow_fx64_t r = flow_fx64_div(x - abs_y, x + abs_y); \
        flow_fx64_t r2 = flow_fx64_mul(r, r); \
        angle = FLOW_FX_QTR_PI + flow_fx64_mul(flow_fx64_mul(FLOW_FX_ATAN_C1, r2) - FLOW_FX_ATAN_C0, r); \
    } else { \
        flow_fx64_t r = flow_fx64_div(x + abs_y, abs_y - x); \
        flow_fx64_t r2 = flow_fx64_mul(r, r); \
        angle = (3 * FLOW_FX_QTR_PI) + flow_fx64_mul(flow_fx64_mul(FLOW_FX_ATAN_C1, r2) - FLOW_FX_ATAN_C0, r); \
    } \
    if (x == 0) { angle = FLOW_FX_HALF_PI; } \
    else if (y == 0) { return x < 0 ? FLOW_FX_PI : 0; } \
    return y < 0 ? -angle : angle; \
} \
static inline __attribute__((unused)) uint64_t flow_quantize_fx(flow_fx64_t val, flow_fx64_t min_v, flow_fx64_t max_v, uint32_t bits) { \
    if (val == FLOW_FX64_NONE || min_v == FLOW_FX64_NONE || max_v == FLOW_FX64_NONE || max_v <= min_v || bits == 0 || bits > (uint32_t)FLOW_FX_SHIFT) { return 0ULL; } \
    flow_fx64_t c = val < min_v ? min_v : (val > max_v ? max_v : val); \
    uint64_t max_q = (1ULL << bits) - 1ULL; \
    flow_u128_t num = (flow_u128_t)(uint64_t)(c - min_v) * (flow_u128_t)max_q; \
    flow_u128_t den = (flow_u128_t)(uint64_t)(max_v - min_v); \
    return (uint64_t)((num + (den >> 1)) / den); \
} \
static inline __attribute__((unused)) flow_fx64_t flow_dequantize_fx(uint64_t code, flow_fx64_t min_v, flow_fx64_t max_v, uint32_t bits) { \
    if (min_v == FLOW_FX64_NONE || max_v == FLOW_FX64_NONE || max_v <= min_v || bits == 0 || bits > (uint32_t)FLOW_FX_SHIFT) { return 0; } \
    uint64_t max_q = (1ULL << bits) - 1ULL; \
    uint64_t c = code > max_q ? max_q : code; \
    flow_u128_t span = (flow_u128_t)(uint64_t)(max_v - min_v); \
    flow_fx64_t delta = (flow_fx64_t)(((flow_u128_t)c * span + (max_q >> 1)) / max_q); \
    return min_v + delta; \
}

#define FLOW_LEAN_NAT_OPS_C17 \
static inline __attribute__((unused)) uint64_t flow_nat_sub(uint64_t a, uint64_t b) { return a >= b ? (a - b) : 0ULL; } \
static inline __attribute__((unused)) uint64_t flow_nat_div(uint64_t a, uint64_t b) { return b == 0ULL ? 0ULL : (a / b); } \
static inline __attribute__((unused)) uint64_t flow_nat_mod(uint64_t a, uint64_t b) { return b == 0ULL ? a : (a % b); } \
static inline __attribute__((unused)) uint64_t flow_nat_pow(uint64_t base, uint64_t exp) { uint64_t r = 1ULL; while (exp > 0ULL) { if (exp & 1ULL) r *= base; base *= base; exp >>= 1U; } return r; } \
static inline __attribute__((unused)) uint64_t flow_nat_min(uint64_t a, uint64_t b) { return a < b ? a : b; } \
static inline __attribute__((unused)) uint64_t flow_nat_max(uint64_t a, uint64_t b) { return a > b ? a : b; } \
static inline __attribute__((unused)) int64_t flow_int_div(int64_t a, int64_t b) { return b == 0 ? 0 : ((a == INT64_MIN && b == -1) ? INT64_MIN : (a / b)); } \
static inline __attribute__((unused)) int64_t flow_int_mod(int64_t a, int64_t b) { return b == 0 ? a : ((a == INT64_MIN && b == -1) ? 0 : (a % b)); } \
static inline __attribute__((unused)) int64_t flow_int_pow(int64_t base, int64_t exp) { if (exp < 0) return 0; int64_t r = 1; while (exp > 0) { if (exp & 1) r *= base; base *= base; exp >>= 1; } return r; }

#define FLOW_LEAN_FLOAT_OPS_C17 \
static inline __attribute__((unused)) double flow_float_add(double a, double b) { return flow_fx64_to_double(flow_fx64_add(flow_fx64_from_double(a), flow_fx64_from_double(b))); } \
static inline __attribute__((unused)) double flow_float_sub(double a, double b) { return flow_fx64_to_double(flow_fx64_sub(flow_fx64_from_double(a), flow_fx64_from_double(b))); } \
static inline __attribute__((unused)) double flow_float_mul(double a, double b) { return flow_fx64_to_double(flow_fx64_mul(flow_fx64_from_double(a), flow_fx64_from_double(b))); } \
static inline __attribute__((unused)) double flow_float_div(double a, double b) { return flow_fx64_to_double(flow_fx64_div(flow_fx64_from_double(a), flow_fx64_from_double(b))); } \
static inline __attribute__((unused)) double flow_float_mod(double a, double b) { return flow_fx64_to_double(flow_fx64_mod(flow_fx64_from_double(a), flow_fx64_from_double(b))); } \
static inline __attribute__((unused)) double flow_float_neg(double x) { return flow_fx64_to_double(flow_fx64_neg(flow_fx64_from_double(x))); } \
static inline __attribute__((unused)) double flow_float_pow(double base, double exp) { return flow_fx64_to_double(flow_fx64_pow(flow_fx64_from_double(base), flow_fx64_from_double(exp))); } \
static inline __attribute__((unused)) double flow_float_abs(double x) { return flow_fx64_to_double(flow_fx64_abs(flow_fx64_from_double(x))); } \
static inline __attribute__((unused)) double flow_float_floor(double x) { return flow_fx64_to_double(flow_fx64_floor(flow_fx64_from_double(x))); } \
static inline __attribute__((unused)) double flow_float_ceil(double x) { return flow_fx64_to_double(flow_fx64_ceil(flow_fx64_from_double(x))); } \
static inline __attribute__((unused)) double flow_float_round(double x) { return flow_fx64_to_double(flow_fx64_round(flow_fx64_from_double(x))); } \
static inline __attribute__((unused)) double flow_float_sqrt(double x) { return flow_fx64_to_double(flow_fx64_sqrt(flow_fx64_from_double(x))); } \
static inline __attribute__((unused)) double flow_float_exp(double x) { return flow_fx64_to_double(flow_fx64_exp(flow_fx64_from_double(x))); } \
static inline __attribute__((unused)) double flow_float_log(double x) { return flow_fx64_to_double(flow_fx64_log(flow_fx64_from_double(x))); } \
static inline __attribute__((unused)) double flow_float_sin(double x) { return flow_fx64_to_double(flow_fx64_sin(flow_fx64_from_double(x))); } \
static inline __attribute__((unused)) double flow_float_cos(double x) { return flow_fx64_to_double(flow_fx64_cos(flow_fx64_from_double(x))); } \
static inline __attribute__((unused)) double flow_float_atan2(double y, double x) { return flow_fx64_to_double(flow_fx64_atan2(flow_fx64_from_double(y), flow_fx64_from_double(x))); } \
static inline __attribute__((unused)) double flow_float_max(double a, double b) { return flow_fx64_to_double(flow_fx64_max(flow_fx64_from_double(a), flow_fx64_from_double(b))); } \
static inline __attribute__((unused)) double flow_float_min(double a, double b) { return flow_fx64_to_double(flow_fx64_min(flow_fx64_from_double(a), flow_fx64_from_double(b))); } \
static inline __attribute__((unused)) double flow_float_of_nat(uint64_t x) { return flow_fx64_to_double(flow_fx64_of_nat(x)); } \
static inline __attribute__((unused)) double flow_float_of_int(int64_t x) { return flow_fx64_to_double(flow_fx64_of_int(x)); } \
static inline __attribute__((unused)) uint64_t flow_quantize(double val, double min_v, double max_v, uint32_t bits) { \
    if (!__builtin_isfinite(val) || !__builtin_isfinite(min_v) || !__builtin_isfinite(max_v)) return 0ULL; \
    return flow_quantize_fx(flow_fx64_from_double(val), flow_fx64_from_double(min_v), flow_fx64_from_double(max_v), bits); \
} \
static inline __attribute__((unused)) double flow_dequantize(uint64_t code, double min_v, double max_v, uint32_t bits) { \
    if (!__builtin_isfinite(min_v) || !__builtin_isfinite(max_v)) return 0.0; \
    return flow_fx64_to_double(flow_dequantize_fx(code, flow_fx64_from_double(min_v), flow_fx64_from_double(max_v), bits)); \
}

FLOW_EXACT_CODEC_C17
FLOW_EXACT_ALU_C17
FLOW_EXACT_EXP_LOG_C17
FLOW_EXACT_TRIG_QUANT_C17
#ifndef FLOW_LEAN_NAT_OPS_DEFINED
#define FLOW_LEAN_NAT_OPS_DEFINED
FLOW_LEAN_NAT_OPS_C17
FLOW_LEAN_FLOAT_OPS_C17
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
FlowU256 flow_u256_mont_reduce(const uint64_t t_in[FLOW_U512_LIMBS], const FlowU256 *modulus, uint64_t n0_inv);
FlowU256 flow_u256_mod_pow(FlowU256 base, FlowU256 exp, FlowU256 modulus);
int flow_u256_is_probably_prime(const FlowU256 *n, const uint64_t *bases, size_t base_count);

typedef struct {
    uint64_t s[FLOW_U256_LIMBS];
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
