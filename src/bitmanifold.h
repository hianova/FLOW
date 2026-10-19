#ifndef FLOW_BITMANIFOLD_H
#define FLOW_BITMANIFOLD_H
#include <stdint.h>
#include <stddef.h>
#if !defined(__cplusplus)
#include <stdatomic.h>
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define FLOW_GENOME_MASK(width) ((width) >= 64 ? ~0ULL : ((1ULL << (width)) - 1ULL))
#define FLOW_GENOME_FIELD_MASK(offset, width) (FLOW_GENOME_MASK(width) << (offset))
#define FLOW_GENOME_GET(genome, offset, width) (((uint64_t)(genome) >> (offset)) & FLOW_GENOME_MASK(width))
#define FLOW_GENOME_SET(genome, offset, width, val) (((uint64_t)(genome) & ~FLOW_GENOME_FIELD_MASK(offset, width)) | (((uint64_t)(val) & FLOW_GENOME_MASK(width)) << (offset)))
#define FLOW_GENOME_PACK(val, offset, width) (((uint64_t)(val) & FLOW_GENOME_MASK(width)) << (offset))
typedef struct { const char *name; uint8_t offset, width; } FlowGenomeField;
#define FLOW_GENOME_FIELD_DEF(name_str, off, w) { .name = (name_str), .offset = (off), .width = (w) }
static inline uint64_t flow_genome_extract(uint64_t genome, uint8_t offset, uint8_t width) { return (genome >> offset) & FLOW_GENOME_MASK(width); }
static inline uint64_t flow_genome_insert(uint64_t genome, uint8_t offset, uint8_t width, uint64_t val) { uint64_t mask = FLOW_GENOME_FIELD_MASK(offset, width);
    return (genome & ~mask) | ((val & FLOW_GENOME_MASK(width)) << offset); }
static inline uint64_t flow_manifold_project(uint64_t genome, uint64_t hard_safety_mask, uint64_t dynamic_bias) { uint64_t projected = genome & hard_safety_mask;
    uint64_t malleable_bits = hard_safety_mask & ~genome; if (dynamic_bias != 0 && malleable_bits != 0) projected |= (dynamic_bias & malleable_bits); return projected; }
static inline uint64_t flow_manifold_transition(uint64_t genome, uint64_t mask, uint64_t *rng_state, uint32_t *mutated_bit_out) {
    if (!mask) { if (mutated_bit_out) *mutated_bit_out = 0; return genome; } uint64_t s = rng_state && *rng_state ? *rng_state : 0x853c49e6748fea9bULL;
    s ^= s >> 12; s ^= s << 25; s ^= s >> 27; uint64_t r = s * 0x2545F4914F6CDD1DULL; if (rng_state) *rng_state = s; int bit_positions[64], count = 0;
    for (int i = 0; i < 64; ++i) if ((mask >> i) & 1ULL) bit_positions[count++] = i; if (!count) { if (mutated_bit_out) *mutated_bit_out = 0; return genome; }
    int chosen_idx = bit_positions[r % (uint64_t)count]; if (mutated_bit_out) *mutated_bit_out = (uint32_t)chosen_idx; return genome ^ (1ULL << chosen_idx); }
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
#define FLOW_BMF_SW_CAN_BUS_HEALTHY    (1ULL << 16)
#define FLOW_BMF_SW_IMU_ZERO_SAT       (1ULL << 17)
#define FLOW_BMF_SW_CXL_FABRIC_READY   (1ULL << 18)
#define FLOW_BMF_SW_HYBRID_POLL_ACTIVE (1ULL << 19)
#define FLOW_BMF_SW_MTD_DIVERSITY_LOCK (1ULL << 20)
#define FLOW_BMF_SW_EPIGENETIC_MASK    (1ULL << 21)
#define FLOW_BMF_SW_PREPLAY_CONE_SAFE  (1ULL << 22)
#define FLOW_BMF_SW_SWARM_IMMUNITY     (1ULL << 23)
#define FLOW_BMF_SW_TELEMETRY_BIAS_MASK (0xFFFFFFFF00000000ULL)
typedef struct __attribute__((aligned(64))) { uint32_t subspace_id; int32_t  is_adjudicated_sound; uint64_t switchboard_bits, invariant_mask, malleable_mask, dynamic_bias;
    double   energy; uint8_t  _cacheline_pad[16]; } FlowBmf1BitCanvas; typedef FlowBmf1BitCanvas FlowBmfCanvas;
static inline void flow_atomic_stage_swap(FlowBmf1BitCanvas *dst, const FlowBmf1BitCanvas *src) { if (!dst || !src) return;
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_ATOMICS__) && !defined(__cplusplus)
    atomic_thread_fence(memory_order_release);
#endif
    *dst = *src;
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_ATOMICS__) && !defined(__cplusplus)
    atomic_thread_fence(memory_order_release);
#endif
} static inline FlowBmf1BitCanvas flow_atomic_stage_load(const FlowBmf1BitCanvas *src) { FlowBmf1BitCanvas canvas = {0}; if (!src) return canvas;
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_ATOMICS__) && !defined(__cplusplus)
    atomic_thread_fence(memory_order_acquire);
#endif
    canvas = *src;
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_ATOMICS__) && !defined(__cplusplus)
    atomic_thread_fence(memory_order_acquire);
#endif
    return canvas; }
#define FLOW_ATOMIC_STAGE_SWAP(dst, src) flow_atomic_stage_swap((dst), (src))
#define FLOW_ATOMIC_STAGE_LOAD(src)      flow_atomic_stage_load((src))
static inline void flow_bmf_canvas_init(FlowBmf1BitCanvas *canvas, uint32_t subspace_id, uint64_t invariant_mask, uint64_t malleable_mask, uint64_t initial_switches) {
    if (!canvas) return; canvas->subspace_id = subspace_id; canvas->invariant_mask = invariant_mask; canvas->malleable_mask = malleable_mask;
    canvas->switchboard_bits = (initial_switches | invariant_mask); canvas->dynamic_bias = 0; canvas->energy = 0.0; canvas->is_adjudicated_sound = 0; }
static inline int flow_bmf_canvas_get_switch(const FlowBmf1BitCanvas *canvas, uint64_t switch_flag) { return canvas ? ((canvas->switchboard_bits & switch_flag) != 0) : 0; }
static inline void flow_bmf_canvas_set_switch(FlowBmf1BitCanvas *canvas, uint64_t switch_flag, int value) {
    if (!canvas || (!value && (canvas->invariant_mask & switch_flag))) return; if (value) canvas->switchboard_bits |= switch_flag;
    else canvas->switchboard_bits &= ~switch_flag; canvas->is_adjudicated_sound = 0; } static inline int flow_bmf_canvas_verify_invariants(const FlowBmf1BitCanvas *canvas) {
    return canvas ? ((canvas->switchboard_bits & canvas->invariant_mask) == canvas->invariant_mask) : 0; }
static inline uint64_t flow_bmf_canvas_flip_1bit(FlowBmf1BitCanvas *canvas, uint64_t *rng_state, uint32_t *mutated_bit_out) { if (!canvas) return 0;
    uint64_t flippable_mask = canvas->malleable_mask & ~canvas->invariant_mask;
    if (!flippable_mask) { if (mutated_bit_out) *mutated_bit_out = 0; return canvas->switchboard_bits; }
    canvas->switchboard_bits = flow_manifold_transition(canvas->switchboard_bits, flippable_mask, rng_state, mutated_bit_out); canvas->is_adjudicated_sound = 0;
    return canvas->switchboard_bits; }
#ifdef __cplusplus
}
#endif
#endif /* FLOW_BITMANIFOLD_H */
