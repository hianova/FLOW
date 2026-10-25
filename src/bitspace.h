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
#define FLOW_FX64_MAX        INT64_MAX
#define FLOW_FX64_MIN        (-INT64_MAX)
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
    if (a == FLOW_FX64_NONE || b == FLOW_FX64_NONE) return FLOW_FX64_NONE; \
    flow_i128_t s = (flow_i128_t)a + (flow_i128_t)b; \
    return (s > (flow_i128_t)INT64_MAX || s < -(flow_i128_t)INT64_MAX) ? FLOW_FX64_NONE : (flow_fx64_t)s; \
} \
static inline __attribute__((unused)) flow_fx64_t flow_fx64_sub(flow_fx64_t a, flow_fx64_t b) { \
    if (a == FLOW_FX64_NONE || b == FLOW_FX64_NONE) return FLOW_FX64_NONE; \
    flow_i128_t d = (flow_i128_t)a - (flow_i128_t)b; \
    return (d > (flow_i128_t)INT64_MAX || d < -(flow_i128_t)INT64_MAX) ? FLOW_FX64_NONE : (flow_fx64_t)d; \
} \
static inline __attribute__((unused)) flow_fx64_t flow_fx64_neg(flow_fx64_t x) { \
    return (x == FLOW_FX64_NONE) ? FLOW_FX64_NONE : -x; \
} \
static inline __attribute__((unused)) flow_fx64_t flow_fx64_abs(flow_fx64_t x) { \
    return (x == FLOW_FX64_NONE) ? FLOW_FX64_NONE : (x < 0 ? -x : x); \
} \
static inline __attribute__((unused)) flow_fx64_t flow_fx64_mul(flow_fx64_t a, flow_fx64_t b) { \
    if (a == FLOW_FX64_NONE || b == FLOW_FX64_NONE) return FLOW_FX64_NONE; \
    flow_i128_t prod = (flow_i128_t)a * (flow_i128_t)b, half = (flow_i128_t)FLOW_FX_HALF; \
    flow_i128_t res = (prod >= 0) ? ((prod + half) >> FLOW_FX_SHIFT) : -(((-prod) + half) >> FLOW_FX_SHIFT); \
    return (res > (flow_i128_t)INT64_MAX || res < -(flow_i128_t)INT64_MAX) ? FLOW_FX64_NONE : (flow_fx64_t)res; \
} \
static inline __attribute__((unused)) flow_fx64_t flow_fx64_div(flow_fx64_t a, flow_fx64_t b) { \
    if (a == FLOW_FX64_NONE || b == FLOW_FX64_NONE || b == 0) return 0; \
    int neg = (a < 0) ^ (b < 0); \
    flow_u128_t ua = (flow_u128_t)(a < 0 ? (0ULL - (uint64_t)a) : (uint64_t)a); \
    flow_u128_t ub = (flow_u128_t)(b < 0 ? (0ULL - (uint64_t)b) : (uint64_t)b); \
    flow_u128_t q = ((ua << FLOW_FX_SHIFT) + (ub >> 1U)) / ub; \
    return (q > (flow_u128_t)INT64_MAX) ? FLOW_FX64_NONE : (neg ? -(flow_fx64_t)q : (flow_fx64_t)q); \
} \
static inline __attribute__((unused)) flow_fx64_t flow_fx64_mod(flow_fx64_t a, flow_fx64_t b) { \
    return (a == FLOW_FX64_NONE || b == FLOW_FX64_NONE || b == 0) ? 0 : (a % b); \
} \
static inline __attribute__((unused)) flow_fx64_t flow_fx64_floor(flow_fx64_t x) { \
    return (x == FLOW_FX64_NONE) ? FLOW_FX64_NONE : (flow_fx64_t)((uint64_t)x & ~(uint64_t)FLOW_FX_FRAC_MASK); \
} \
static inline __attribute__((unused)) flow_fx64_t flow_fx64_ceil(flow_fx64_t x) { \
    if (x == FLOW_FX64_NONE || ((uint64_t)x & (uint64_t)FLOW_FX_FRAC_MASK) == 0ULL) return x; \
    return flow_fx64_add(flow_fx64_floor(x), FLOW_FX_ONE); \
} \
static inline __attribute__((unused)) flow_fx64_t flow_fx64_round(flow_fx64_t x) { \
    if (x == FLOW_FX64_NONE) return FLOW_FX64_NONE; \
    flow_fx64_t s = flow_fx64_add(x >= 0 ? x : -x, FLOW_FX_HALF); \
    return (s == FLOW_FX64_NONE) ? FLOW_FX64_NONE : (x >= 0 ? flow_fx64_floor(s) : -flow_fx64_floor(s)); \
} \
static inline __attribute__((unused)) flow_fx64_t flow_fx64_max(flow_fx64_t a, flow_fx64_t b) { \
    return (a == FLOW_FX64_NONE || b == FLOW_FX64_NONE) ? FLOW_FX64_NONE : (a > b ? a : b); \
} \
static inline __attribute__((unused)) flow_fx64_t flow_fx64_min(flow_fx64_t a, flow_fx64_t b) { \
    return (a == FLOW_FX64_NONE || b == FLOW_FX64_NONE) ? FLOW_FX64_NONE : (a < b ? a : b); \
} \
static inline __attribute__((unused)) flow_fx64_t flow_fx64_of_nat(uint64_t x) { \
    return (x > (uint64_t)INT32_MAX) ? FLOW_FX64_NONE : (flow_fx64_t)(x << FLOW_FX_SHIFT); \
} \
static inline __attribute__((unused)) flow_fx64_t flow_fx64_of_int(int64_t x) { \
    return (x > (int64_t)INT32_MAX || x < -(int64_t)INT32_MAX) ? FLOW_FX64_NONE : (flow_fx64_t)((uint64_t)x << FLOW_FX_SHIFT); \
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
    flow_fx64_t r = x % FLOW_FX_TWO_PI; \
    return flow_fx64_sin(r + FLOW_FX_HALF_PI); \
} \
static inline __attribute__((unused)) flow_fx64_t flow_fx64_atan2(flow_fx64_t y, flow_fx64_t x) { \
    if (y == FLOW_FX64_NONE || x == FLOW_FX64_NONE || (y == 0 && x == 0)) { return 0; } \
    if (x == 0) { return y < 0 ? -FLOW_FX_HALF_PI : FLOW_FX_HALF_PI; } \
    if (y == 0) { return x < 0 ? FLOW_FX_PI : 0; } \
    flow_fx64_t sx = x, sy = y; \
    if (flow_fx64_abs(sx) > (INT64_MAX >> 2) || flow_fx64_abs(sy) > (INT64_MAX >> 2)) { \
        sx >>= 2; \
        sy >>= 2; \
    } \
    flow_fx64_t abs_y = flow_fx64_abs(sy) + 1; \
    flow_fx64_t angle = 0; \
    if (sx >= 0) { \
        flow_fx64_t r = flow_fx64_div(sx - abs_y, sx + abs_y); \
        flow_fx64_t r2 = flow_fx64_mul(r, r); \
        angle = FLOW_FX_QTR_PI + flow_fx64_mul(flow_fx64_mul(FLOW_FX_ATAN_C1, r2) - FLOW_FX_ATAN_C0, r); \
    } else { \
        flow_fx64_t r = flow_fx64_div(sx + abs_y, abs_y - sx); \
        flow_fx64_t r2 = flow_fx64_mul(r, r); \
        angle = (3 * FLOW_FX_QTR_PI) + flow_fx64_mul(flow_fx64_mul(FLOW_FX_ATAN_C1, r2) - FLOW_FX_ATAN_C0, r); \
    } \
    return y < 0 ? -angle : angle; \
} \
static inline __attribute__((unused)) uint64_t flow_quantize_fx(flow_fx64_t val, flow_fx64_t min_v, flow_fx64_t max_v, uint32_t bits) { \
    if (val == FLOW_FX64_NONE || min_v == FLOW_FX64_NONE || max_v == FLOW_FX64_NONE || max_v <= min_v || bits == 0 || bits > (uint32_t)FLOW_FX_SHIFT) { return 0ULL; } \
    flow_fx64_t c = val < min_v ? min_v : (val > max_v ? max_v : val); \
    uint64_t max_q = (1ULL << bits) - 1ULL; \
    flow_u128_t diff_c = (flow_u128_t)((flow_i128_t)c - (flow_i128_t)min_v); \
    flow_u128_t den = (flow_u128_t)((flow_i128_t)max_v - (flow_i128_t)min_v); \
    flow_u128_t num = diff_c * (flow_u128_t)max_q; \
    return (uint64_t)((num + (den >> 1)) / den); \
} \
static inline __attribute__((unused)) flow_fx64_t flow_dequantize_fx(uint64_t code, flow_fx64_t min_v, flow_fx64_t max_v, uint32_t bits) { \
    if (min_v == FLOW_FX64_NONE || max_v == FLOW_FX64_NONE || max_v <= min_v || bits == 0 || bits > (uint32_t)FLOW_FX_SHIFT) { return 0; } \
    uint64_t max_q = (1ULL << bits) - 1ULL; \
    uint64_t c = code > max_q ? max_q : code; \
    flow_u128_t span = (flow_u128_t)((flow_i128_t)max_v - (flow_i128_t)min_v); \
    flow_i128_t delta = (flow_i128_t)(((flow_u128_t)c * span + (max_q >> 1)) / max_q); \
    flow_i128_t res = (flow_i128_t)min_v + delta; \
    if (res > (flow_i128_t)INT64_MAX) { return INT64_MAX; } \
    if (res < -(flow_i128_t)INT64_MAX) { return -INT64_MAX; } \
    return (flow_fx64_t)res; \
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

/* ============================================================================
 * Architectural Experience Memory (.fvec) (folded into bitspace.h)
 * ============================================================================ */
#define FLOW_VAULT_DIM 16
#define FLOW_FVEC_MAGIC "FVEC_V1"
#define FLOW_FVEC_HEADER_SIZE 1024
#define FLOW_FVEC_MAX_ENTRIES 128
#define FLOW_FVEC_DEFAULT_DIR ".flow/vecs"
#define FLOW_FVEC_RAM_CRITICAL_PCT (90.0)
#define FLOW_FVEC_CACHE_MISS_CRITICAL (0.15)
#define FLOW_FVEC_MAX_SWAP_NS (500ULL)
typedef struct { char magic[16], id[64], name[128], origin_hardware[128], trigger_intent[64], category[32], component_id[32], description[256], smt_signature[64];
    double energy_score; uint64_t created_at_unix; uint32_t vector_dim, payload_size, confidence_score; uint64_t last_reinforced_unix; uint8_t is_auto_promoted;
    char content_hash[32], filepath[256]; } FlowVecHeader;
typedef struct { double features[FLOW_VAULT_DIM]; uint64_t pure_genome, hard_composite_mask, soft_composite_bias; FlowSMTProofAttestation proof; uint32_t crc32; } FlowVecPayload;
typedef struct { FlowVecHeader header; FlowVecPayload payload; } FlowVecRecord;
typedef struct { FlowVecRecord records[FLOW_FVEC_MAX_ENTRIES]; size_t count; char root_dir[256]; uint64_t total_lookups; } FlowVecStore;
uint32_t flow_fvec_crc32(const void *data, size_t length);
int flow_fvec_header_serialize(const FlowVecHeader *hdr, char *out_buf, size_t buf_size);
int flow_fvec_header_deserialize(const char *in_buf, size_t buf_size, FlowVecHeader *hdr_out);
int flow_fvec_write_file(const char *filepath, const FlowVecHeader *hdr, const FlowVecPayload *payload);
int flow_fvec_read_file(const char *filepath, FlowVecHeader *hdr_out, FlowVecPayload *payload_out);
void flow_fvec_embed_prompt(const char *prompt, double *out_features);
double flow_fvec_cosine_similarity(const double *a, const double *b, size_t dim);
void flow_fvec_store_init(FlowVecStore *store, const char *dir_path);
int flow_fvec_store_scan(FlowVecStore *store);
const FlowVecRecord *flow_fvec_store_lookup_by_id(const FlowVecStore *store, const char *id);
int flow_fvec_store_query(const FlowVecStore *store, const char *natural_query, size_t *best_idx_out, double *best_sim_out);
int flow_fvec_remediate_check(const FlowVecStore *store, double ram_pressure_pct, double cache_miss_rate, const FlowVecRecord **recommended_record_out, double *confidence_out, char *diagnosis_out, size_t max_diag_len);
void flow_fvec_inspect(const FlowVecHeader *hdr, const FlowVecPayload *payload, FILE *out);
void flow_fvec_store_print_summary(const FlowVecStore *store, FILE *out);
int flow_fvec_seed_canonical_files(const char *target_dir);
int flow_fvec_verify_hardware_affinity(const FlowVecHeader *hdr, const FlowEnvironmentState *host_env, char *diag_msg, size_t max_len);
uint64_t flow_fvec_compute_content_hash(uint64_t genome, uint64_t hard_mask, uint64_t soft_bias, const FlowSMTProofAttestation *proof);
int flow_fvec_promote_or_strengthen(const char *target_dir, const FlowVecHeader *base_hdr, const FlowVecPayload *payload, uint64_t content_hash, char *out_filepath, size_t max_path_len, uint32_t *new_confidence_out);
size_t flow_fvec_store_evict_senescent(FlowVecStore *store, uint64_t now_unix, uint64_t max_idle_seconds, char (*evicted_files_out)[256], size_t max_evictions);
#define FLOW_FVEC_MAX_PRESTAGED 8
typedef struct { char trigger_condition[64], fvec_path[256]; FlowVecHeader header; FlowVecPayload payload; FlowBmf1BitCanvas staged_canvas; int is_staged_sound; } FlowFvecPreStagingSlot;
typedef struct { FlowFvecPreStagingSlot slots[FLOW_FVEC_MAX_PRESTAGED]; size_t slot_count; uint64_t total_speculative_swaps; double last_swap_latency_ns; } FlowFvecPreStagingVault;
int flow_fvec_prestaging_init(FlowFvecPreStagingVault *vault);
int flow_fvec_prestaging_register(FlowFvecPreStagingVault *vault, const char *fvec_file_path, const char *trigger_condition);
int flow_fvec_prestaging_swap_atomic(FlowFvecPreStagingVault *vault, const char *trigger_condition, FlowBmf1BitCanvas *active_canvas_out, double *swap_latency_ns_out);
FlowSMTResult flow_fvec_verify_prestaging_soundness_smt(const FlowFvecPreStagingVault *vault, const char *trigger_condition, double swap_latency_ns, FlowSMTProofAttestation *proof_out);

/* ============================================================================
 * 5-Stage Token Ring, Wavefront Semilattice & Swarm Mesh (folded into bitspace.h)
 * ============================================================================ */
#define FLOW_WIRE_FRAME9_SIZE 9
#define FLOW_WIRE_OP_HASH_SYNC    0xAA
#define FLOW_WIRE_OP_ANTIBODY     0xAA
#define FLOW_WIRE_OP_HETERO_MESH  0xBB
#define FLOW_WIRE_OP_FLEET_SYNC   0xCC
#pragma pack(push, 1)
typedef struct { uint8_t opcode; union { struct { uint8_t hash_bytes[8]; } antibody;
    struct { uint8_t role, node_id, bp_hi, bp_lo, lat_hi, lat_lo, crc_hi, crc_lo; } hetero_mesh;
    struct { uint8_t agent_id, role, x_hi, x_lo, y_hi, y_lo, crc_hi, crc_lo; } fleet; uint8_t raw[8]; } payload; } FlowWireFrame9;
#pragma pack(pop)
static inline uint16_t flow_wire_crc16(const uint8_t *data, size_t len) { uint16_t crc = 0xFFFF; if (!data) return 0; for (size_t i = 0; i < len; ++i) {
    crc ^= (uint16_t)data[i] << 8; for (int b = 0; b < 8; ++b) crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1); } return crc; }
static inline int flow_wire_frame9_pack(const FlowWireFrame9 *frame, uint8_t out[FLOW_WIRE_FRAME9_SIZE]) {
    return (frame && out) ? (memcpy(out, frame, FLOW_WIRE_FRAME9_SIZE), 1) : 0; }
static inline int flow_wire_frame9_unpack(const uint8_t in[FLOW_WIRE_FRAME9_SIZE], FlowWireFrame9 *frame_out) {
    return (in && frame_out) ? (memcpy(frame_out, in, FLOW_WIRE_FRAME9_SIZE), 1) : 0; }
static inline int flow_wire_hash_encode(uint64_t content_hash, uint8_t out_packet[FLOW_WIRE_FRAME9_SIZE]) { if (!out_packet) return 0;
    FlowWireFrame9 f = { .opcode = FLOW_WIRE_OP_HASH_SYNC }; for (int i = 0; i < 8; ++i) f.payload.antibody.hash_bytes[i] = (uint8_t)((content_hash >> (56 - i * 8)) & 0xFF);
    return flow_wire_frame9_pack(&f, out_packet); }
static inline int flow_wire_hash_decode(const uint8_t in_packet[FLOW_WIRE_FRAME9_SIZE], uint64_t *out_content_hash) { if (!in_packet || !out_content_hash) return 0;
    FlowWireFrame9 f; if (!flow_wire_frame9_unpack(in_packet, &f) || f.opcode != FLOW_WIRE_OP_HASH_SYNC) return 0; uint64_t h = 0;
    for (int i = 0; i < 8; ++i) h = (h << 8) | f.payload.antibody.hash_bytes[i]; *out_content_hash = h; return 1; }
#define FLOW_SWARM_MSG_ANTIBODY       FLOW_WIRE_OP_HASH_SYNC
#define FLOW_SWARM_LYMPH_PKT_SIZE     FLOW_WIRE_FRAME9_SIZE
#define flow_swarm_lymphatic_encode   flow_wire_hash_encode
#define flow_swarm_lymphatic_decode   flow_wire_hash_decode
#define FLOW_TOKEN_RING_MAX_TOKENS 16
#define FLOW_TOKEN_RING_DEFAULT_MAX_CYCLES 32
typedef enum { FLOW_TOKEN_STAGE_INGEST = 0, FLOW_TOKEN_STAGE_POLYTOPE = 1, FLOW_TOKEN_STAGE_ANNEAL = 2,
    FLOW_TOKEN_STAGE_SMT_PROOF = 3, FLOW_TOKEN_STAGE_SYNTHESIS = 4, FLOW_TOKEN_STAGE_ATTRACTOR = 5, FLOW_TOKEN_STAGE_CUSTOM = 6 } FlowTokenStage;
typedef enum { FLOW_RING_INIT = 0, FLOW_RING_CIRCULATING = 1, FLOW_RING_ATTRACTOR_REACHED = 2, FLOW_RING_UNSAT = 3, FLOW_RING_EXHAUSTED = 4 } FlowTokenRingState;
struct FlowTokenRing; typedef struct FlowToken FlowToken;
typedef int (*FlowTokenTransitionFn)(struct FlowTokenRing *ring, FlowToken *token, FlowMaskCanvas *canvas);
struct FlowToken { uint32_t token_id; FlowTokenStage stage; char stage_name[32]; uint64_t attention_mask, dynamic_bias, execution_count;
    double energy; FlowTokenTransitionFn transition_fn; void *user_data; };
typedef struct FlowTokenRing { FlowToken tokens[FLOW_TOKEN_RING_MAX_TOKENS]; size_t token_count, current_token_idx, step_count, anneal_iterations;
    FlowMaskCanvas active_canvas; FlowBmf1BitCanvas bmf_1bit_canvas; uint64_t active_genome, cycle_count, rng_state, wavefront_epoch, quiescent_generation;
    double lyapunov_energy, prev_energy, lyapunov_delta_e; FlowTokenRingState state; int attractor_converged; SemanticIR *active_ir; FlowBitSpace active_space;
    SearchResult best_search; FlowSMTProofAttestation smt_proof; FlowPlanEnsemble ensemble; uint32_t rng_seed; FlowBumpQsbrArena *bound_arena; char status_message[128]; } FlowTokenRing;
int flow_token_ring_init(FlowTokenRing *ring, SemanticIR *ir);
int flow_token_ring_setup_canonical(FlowTokenRing *ring, SemanticIR *ir, size_t anneal_iters, uint32_t seed);
int flow_token_ring_add_token(FlowTokenRing *ring, FlowTokenStage stage, const char *name, FlowTokenTransitionFn fn, void *user_data);
uint64_t flow_token_ring_attention_project(FlowMaskCanvas *canvas, uint64_t attention_mask, uint64_t dynamic_bias, uint64_t current_genome);
uint64_t flow_token_ring_bmf_attention_project(FlowBmf1BitCanvas *canvas, uint64_t attention_mask, uint64_t dynamic_bias);
int flow_token_ring_step(FlowTokenRing *ring);
FlowTokenRingState flow_token_ring_run_to_attractor(FlowTokenRing *ring, size_t max_cycles);
int flow_token_ring_converge_default(FlowTokenRing *ring, const SemanticIR *opt_ir, const char *flow_name);

#define FLOW_SUBSPACE_MASK_CAPACITY     0x000000000000FFFFULL
#define FLOW_SUBSPACE_MASK_CONCURRENCY  0x0000000000FF0000ULL
#define FLOW_SUBSPACE_MASK_SHARDING     0x00000000FF000000ULL
#define FLOW_SUBSPACE_MASK_BUFFER       0x0000FFFF00000000ULL
#define FLOW_SUBSPACE_MASK_GROWTH       0xFFFF000000000000ULL
#define FLOW_LYAPUNOV_CONVERGE_EPS      1e-5
#define FLOW_LAGRANGIAN_DEFAULT_ETA     0.05
#define FLOW_MESH_SATURATION_LIMIT_PERMILLE 949U

#define FLOW_MAX_SUBSPACES 8
#define FLOW_WAVEFRONT_MAX_SLOTS 8
#define FLOW_WAVEFRONT_MAX_WORKERS 8
typedef enum { FLOW_SUBSPACE_CAPACITY = 0, FLOW_SUBSPACE_CONCURRENCY = 1, FLOW_SUBSPACE_SHARDING = 2,
    FLOW_SUBSPACE_BUFFER = 3, FLOW_SUBSPACE_GROWTH = 4, FLOW_SUBSPACE_CUSTOM = 5 } FlowSubspaceId;
typedef struct { FlowSubspaceId id; char name[32]; uint64_t mask; uint8_t bit_offset, bit_width; uint64_t min_value, max_value;
    double shadow_price_lambda, learning_rate_eta, current_demand, capacity_limit; uint64_t current_val, optimal_val; } FlowSubspace;
typedef struct { FlowSubspace subspaces[FLOW_MAX_SUBSPACES]; size_t subspace_count; uint64_t composite_coverage_mask; bool is_strictly_orthogonal; } FlowSubspaceDecomposition;
typedef struct __attribute__((aligned(64))) { uint32_t slot_id; FlowTokenStage current_stage; uint64_t slot_genome; FlowBmf1BitCanvas bmf_canvas; FlowMaskCanvas slot_canvas;
    double energy; bool in_flight; } FlowWavefrontSlot;
typedef struct FlowWavefrontRing { FlowSubspaceDecomposition decomp; FlowWavefrontSlot slots[FLOW_WAVEFRONT_MAX_SLOTS]; size_t slot_count, worker_count;
    uint64_t global_lattice_genome, wave_cycle_count, total_cycles, wavefront_epoch, quiescent_generation; FlowMaskCanvas global_canvas;
    double global_lyapunov_energy, prev_lyapunov_energy, lyapunov_delta_e, total_energy_uj; bool attractor_converged; FlowTokenRingState state; SemanticIR *active_ir;
    FlowBitSpace active_space; SearchResult best_search; FlowSMTProofAttestation smt_proof; FlowPhysicalProbe last_probe; FlowBumpQsbrArena *bound_arena; char status_message[128]; } FlowWavefrontRing;
int flow_subspace_decompose_canonical(FlowSubspaceDecomposition *decomp, const SemanticIR *ir);
int flow_subspace_lagrangian_tune(FlowSubspace *sub, double current_demand, double capacity_limit);
uint64_t flow_subspace_polyhedral_project(const FlowSubspace *sub, uint64_t raw_val);
uint64_t flow_wavefront_semilattice_join(uint64_t base_genome, const uint64_t *thread_slices, const uint64_t *subspace_masks, size_t count);
int flow_wavefront_ring_init(FlowWavefrontRing *ring, SemanticIR *ir, size_t num_slots, size_t num_workers);
int flow_wavefront_ring_step_parallel(FlowWavefrontRing *ring);
void flow_wavefront_ring_destroy(FlowWavefrontRing *ring);
int flow_wavefront_ring_bind_arena(FlowWavefrontRing *ring, FlowBumpQsbrArena *arena);
FlowSMTResult flow_wavefront_verify_temporal_safety_smt(const FlowWavefrontRing *ring, FlowSMTProofAttestation *proof_out);
#define FLOW_SWARM_MSG_HETERO_PHEROMONE 0xBB
#define FLOW_SWARM_HETERO_PKT_SIZE 9
#define FLOW_HETERO_MESH_MAX_NODES 64
typedef enum { FLOW_SWARM_ROLE_GENERIC = 0, FLOW_SWARM_ROLE_INGRESS_GATEWAY = 1, FLOW_SWARM_ROLE_COMPUTE_ROUTER = 2, FLOW_SWARM_ROLE_STORAGE_INDEX = 3,
    FLOW_SWARM_ROLE_EMBODIED_ACTUATOR = 4 } FlowSwarmRole;
typedef struct { uint8_t node_id; FlowSwarmRole role; char name[32]; uint64_t contract_hash; uint32_t capacity; uint16_t current_backpressure, current_latency_us;
    uint64_t total_routed_requests; int is_active; } FlowHeteroMeshNode;
typedef struct { size_t node_count; FlowHeteroMeshNode nodes[FLOW_HETERO_MESH_MAX_NODES]; uint64_t total_mesh_telemetry_packets, total_mesh_routed; } FlowHeteroMesh;
void flow_hetero_mesh_init(FlowHeteroMesh *mesh);
int flow_hetero_mesh_register_node(FlowHeteroMesh *mesh, uint8_t node_id, FlowSwarmRole role, const char *name, uint64_t contract_hash, uint32_t capacity);
int flow_hetero_mesh_route_target(const FlowHeteroMesh *mesh, FlowSwarmRole target_role, uint8_t *selected_node_id_out);
FlowSMTResult flow_hetero_mesh_verify_smt(const FlowHeteroMesh *mesh, uint32_t ingress_max_qps, FlowSMTProofAttestation *proof_out);
#define FLOW_HETERO_RING_MAX_NODES 64
typedef struct __attribute__((aligned(64))) { _Atomic uint64_t seq; uint8_t node_id, role; uint16_t backpressure_permille, latency_p99_us, contract_crc16;
    uint32_t capacity_qps; uint64_t timestamp_ns; double q_snapshot[16]; uint8_t reserved[36]; } FlowSwarmRingSlot;
typedef struct { FlowSwarmRingSlot slots[FLOW_HETERO_RING_MAX_NODES]; size_t active_node_count; uint64_t total_ring_updates, total_ring_samples; } FlowSwarmRingMesh;
int flow_swarm_ring_mesh_init(FlowSwarmRingMesh *mesh);
int flow_swarm_ring_publish(FlowSwarmRingMesh *mesh, uint8_t node_id, FlowSwarmRole role, uint16_t backpressure, uint16_t latency_p99_us, uint16_t contract_crc16, uint32_t capacity_qps, const double q[16]);
int flow_swarm_ring_sample(const FlowSwarmRingMesh *mesh, uint8_t node_id, FlowSwarmRingSlot *slot_out);
int flow_swarm_ring_route_lowest_energy(const FlowSwarmRingMesh *mesh, FlowSwarmRole target_role, uint8_t *selected_node_out);
#endif
