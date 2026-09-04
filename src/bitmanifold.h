#ifndef FLOW_BITMANIFOLD_H
#define FLOW_BITMANIFOLD_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW BitManifold (BMF): 64-bit Genome Subspace Slicing & Manifold Transitions
 * ============================================================================
 *
 * Provides zero-cost, branchless macros and static inline primitives for:
 * 1. Declarative bitfield subspace slicing on 64-bit discrete manifolds.
 * 2. Polytope manifold projection (flow_manifold_project).
 * 3. 1-Bit chaotic state transition on constrained manifolds (flow_manifold_transition).
 * ============================================================================
 */

/* Compile-time bitmask for given width (up to 64 bits) */
#define FLOW_GENOME_MASK(width) \
    ((width) >= 64 ? ~0ULL : ((1ULL << (width)) - 1ULL))

/* Positioned field mask */
#define FLOW_GENOME_FIELD_MASK(offset, width) \
    (FLOW_GENOME_MASK(width) << (offset))

/* Extract field value from genome */
#define FLOW_GENOME_GET(genome, offset, width) \
    (((uint64_t)(genome) >> (offset)) & FLOW_GENOME_MASK(width))

/* Set field value into genome without affecting other bits */
#define FLOW_GENOME_SET(genome, offset, width, val) \
    (((uint64_t)(genome) & ~FLOW_GENOME_FIELD_MASK(offset, width)) | \
     (((uint64_t)(val) & FLOW_GENOME_MASK(width)) << (offset)))

/* Pack a value into a specific field offset */
#define FLOW_GENOME_PACK(val, offset, width) \
    (((uint64_t)(val) & FLOW_GENOME_MASK(width)) << (offset))

/* Declarative field descriptor */
typedef struct {
    const char *name;
    uint8_t offset;
    uint8_t width;
} FlowGenomeField;

#define FLOW_GENOME_FIELD_DEF(name_str, off, w) \
    { .name = (name_str), .offset = (off), .width = (w) }

/* Branchless Inline Slicing Accessors */
static inline uint64_t flow_genome_extract(uint64_t genome, uint8_t offset, uint8_t width) {
    return (genome >> offset) & FLOW_GENOME_MASK(width);
}

static inline uint64_t flow_genome_insert(uint64_t genome, uint8_t offset, uint8_t width, uint64_t val) {
    uint64_t mask = FLOW_GENOME_FIELD_MASK(offset, width);
    return (genome & ~mask) | ((val & FLOW_GENOME_MASK(width)) << offset);
}

/*
 * Canonical BitManifold (BMF) Functions
 */

/**
 * flow_manifold_project:
 * Projects a raw 64-bit candidate genome onto the legal discrete hypercube manifold \Pi_P({0,1}^64).
 * Enforces hard safety masks (bits constrained to 0 or 1) and applies dynamic telemetry bias.
 */
static inline uint64_t flow_manifold_project(uint64_t genome, uint64_t hard_safety_mask, uint64_t dynamic_bias) {
    /* Hard safety mask bits must not be violated: keep only legal manifold bits */
    uint64_t projected = genome & hard_safety_mask;

    /* Where hard safety allows exploration, softly apply dynamic telemetry bias */
    uint64_t malleable_bits = hard_safety_mask & ~genome;
    if (dynamic_bias != 0 && malleable_bits != 0) {
        projected |= (dynamic_bias & malleable_bits);
    }
    return projected;
}

/**
 * flow_manifold_transition:
 * Executes an O(1) single-cycle 1-bit chaotic mutation transition on the manifold.
 * Flips exactly one bit among the allowable mask bits using high-speed XorShift64* PRNG.
 */
static inline uint64_t flow_manifold_transition(uint64_t genome, uint64_t mask,
                                                uint64_t *rng_state, uint32_t *mutated_bit_out) {
    if (mask == 0) {
        if (mutated_bit_out) *mutated_bit_out = 0;
        return genome;
    }

    /* Advance PRNG state (XorShift64*) */
    uint64_t s = rng_state ? *rng_state : 0x853c49e6748fea9bULL;
    if (s == 0) s = 0x853c49e6748fea9bULL;
    s ^= s >> 12;
    s ^= s << 25;
    s ^= s >> 27;
    uint64_t r = s * 0x2545F4914F6CDD1DULL;
    if (rng_state) *rng_state = s;

    /* Count available bits in mask */
    int bit_positions[64];
    int count = 0;
    for (int i = 0; i < 64; ++i) {
        if ((mask >> i) & 1ULL) {
            bit_positions[count++] = i;
        }
    }

    if (count == 0) {
        if (mutated_bit_out) *mutated_bit_out = 0;
        return genome;
    }

    int chosen_idx = bit_positions[r % (uint64_t)count];
    if (mutated_bit_out) *mutated_bit_out = (uint32_t)chosen_idx;

    /* Single-cycle bit flip transition on manifold */
    return genome ^ (1ULL << chosen_idx);
}

/*
 * ============================================================================
 * 1-Bit Rigid Switchboard BMF Canvas (FlowBmf1BitCanvas / FlowBmfCanvas)
 * ============================================================================
 *
 * Instead of ballooning the state space with 4096-D continuous variables, the
 * continuous neural embedding is strictly used by the neuro-bridge to index
 * the active Subspace (chart). Within the selected Subspace, the FLOW core
 * retreats to this pure 1-bit switchboard canvas for sub-microsecond SMT
 * adjudication and single-cycle Markovian 1-bit chaotic annealing.
 * ============================================================================
 */

/* Core Physical & Formal Invariant 1-Bit Switches (Bits 0..15: Hard Invariants) */
#define FLOW_BMF_SW_HARD_SAFETY        (1ULL << 0)  /* SMT Buffer Bounds / Memory Safety */
#define FLOW_BMF_SW_CONTRACT_GATE      (1ULL << 1)  /* SMT IR Contract & Ordering Valid */
#define FLOW_BMF_SW_RESOURCE_QUOTA     (1ULL << 2)  /* SMT Memory Quota Ceiling Bound */
#define FLOW_BMF_SW_POLYTOPE_FEASIBLE  (1ULL << 3)  /* State inside Presburger Polyhedron */
#define FLOW_BMF_SW_STICK_SLIP_MODE    (1ULL << 4)  /* Coulomb friction: 1=Stick (inside cone), 0=Slip */
#define FLOW_BMF_SW_IMPACT_DAMPING     (1ULL << 5)  /* Moreau Touchdown Contact critical damping active */
#define FLOW_BMF_SW_COOP_SYNC_LOCK     (1ULL << 6)  /* Dual-Robot distance synchronization invariant locked */
#define FLOW_BMF_SW_EMERGENCY_HALT     (1ULL << 7)  /* Emergency frame preemption / priority interrupt active */
#define FLOW_BMF_SW_THERMAL_THROTTLE   (1ULL << 8)  /* PMU thermal envelope guard (energy < limit) */
#define FLOW_BMF_SW_ANTI_SPILL_TILT    (1ULL << 9)  /* Latte/liquid anti-spill tilt clamp (theta <= 0.08 rad) */
#define FLOW_BMF_SW_GRIPPER_FORCE_SAFE (1ULL << 10) /* Gripper normal force clamp ([2.0, 4.5] N) */
#define FLOW_BMF_SW_ZMP_BALANCE        (1ULL << 11) /* Zero Moment Point within convex support polygon */
#define FLOW_BMF_SW_NUMA_FIRST_TOUCH   (1ULL << 12) /* First-touch local NUMA affinity locked */
#define FLOW_BMF_SW_SIMD_VECTORIZED    (1ULL << 13) /* SIMD vectorization path enabled */
#define FLOW_BMF_SW_QSBR_EPOCH_ADVANCE (1ULL << 14) /* QSBR quiescent state observed, safe to reclaim */
#define FLOW_BMF_SW_DETERMINISM_INVAR  (1ULL << 15) /* SMT Functional determinism invariant holds */

/* Extended Hardware Substrate & Execution Switches (Bits 16..31) */
#define FLOW_BMF_SW_CAN_BUS_HEALTHY    (1ULL << 16) /* SocketCAN / CAN-FD bus state operational */
#define FLOW_BMF_SW_IMU_ZERO_SAT       (1ULL << 17) /* IMU streaming driver zero saturation verified */
#define FLOW_BMF_SW_CXL_FABRIC_READY   (1ULL << 18) /* CXL memory fabric demotion channel available */
#define FLOW_BMF_SW_HYBRID_POLL_ACTIVE (1ULL << 19) /* Accelerator Moreau hybrid poll hysteresis active */
#define FLOW_BMF_SW_MTD_DIVERSITY_LOCK (1ULL << 20) /* Moving Target Defense layout randomized */
#define FLOW_BMF_SW_EPIGENETIC_MASK    (1ULL << 21) /* Epigenetic environmental adaptation active */
#define FLOW_BMF_SW_PREPLAY_CONE_SAFE  (1ULL << 22) /* Spacetime pre-play 3.0s future light cone verified safe */
#define FLOW_BMF_SW_SWARM_IMMUNITY     (1ULL << 23) /* Fleet lymphatic antibody network active */

/* Telemetry, Biasing & Dynamic Annealing Exploration Bits (Bits 32..63) */
#define FLOW_BMF_SW_TELEMETRY_BIAS_MASK (0xFFFFFFFF00000000ULL)

typedef struct {
    uint32_t subspace_id;          /* Active Subspace / Chart ID (indexed from 4096-D embedding) */
    uint64_t switchboard_bits;     /* 1-bit rigid physical switch states */
    uint64_t invariant_mask;       /* Invariant mask: bits that must strictly remain 1 for physical safety */
    uint64_t malleable_mask;       /* Malleable mask: bits allowed to undergo 1-bit chaotic mutation/annealing */
    uint64_t dynamic_bias;         /* Continuous telemetry / external soft perturbation bias */
    double   energy;               /* Physical energy / Lyapunov potential of current 1-bit configuration */
    int      is_adjudicated_sound; /* True (1) if SMT Supreme Court has verified all invariant switches */
} FlowBmf1BitCanvas;

typedef FlowBmf1BitCanvas FlowBmfCanvas;

static inline void flow_bmf_canvas_init(FlowBmf1BitCanvas *canvas, uint32_t subspace_id,
                                        uint64_t invariant_mask, uint64_t malleable_mask,
                                        uint64_t initial_switches) {
    if (!canvas) return;
    canvas->subspace_id = subspace_id;
    canvas->invariant_mask = invariant_mask;
    canvas->malleable_mask = malleable_mask;
    /* Invariants must always be enforced (must be 1) */
    canvas->switchboard_bits = (initial_switches | invariant_mask);
    canvas->dynamic_bias = 0;
    canvas->energy = 0.0;
    canvas->is_adjudicated_sound = 0;
}

static inline int flow_bmf_canvas_get_switch(const FlowBmf1BitCanvas *canvas, uint64_t switch_flag) {
    if (!canvas) return 0;
    return (canvas->switchboard_bits & switch_flag) != 0;
}

static inline void flow_bmf_canvas_set_switch(FlowBmf1BitCanvas *canvas, uint64_t switch_flag, int value) {
    if (!canvas) return;
    /* If switch is part of invariant mask, it cannot be cleared to 0 */
    if (!value && (canvas->invariant_mask & switch_flag)) {
        return; /* Hard invariant protected: refusal to violate physical safety */
    }
    if (value) {
        canvas->switchboard_bits |= switch_flag;
    } else {
        canvas->switchboard_bits &= ~switch_flag;
    }
    canvas->is_adjudicated_sound = 0; /* Invalidate proof upon state mutation */
}

static inline int flow_bmf_canvas_verify_invariants(const FlowBmf1BitCanvas *canvas) {
    if (!canvas) return 0;
    return (canvas->switchboard_bits & canvas->invariant_mask) == canvas->invariant_mask;
}

static inline uint64_t flow_bmf_canvas_flip_1bit(FlowBmf1BitCanvas *canvas,
                                                 uint64_t *rng_state,
                                                 uint32_t *mutated_bit_out) {
    if (!canvas) return 0;
    /* Flip is strictly allowed only within malleable bits that are not invariant */
    uint64_t flippable_mask = canvas->malleable_mask & ~canvas->invariant_mask;
    if (flippable_mask == 0) {
        if (mutated_bit_out) *mutated_bit_out = 0;
        return canvas->switchboard_bits;
    }
    canvas->switchboard_bits = flow_manifold_transition(canvas->switchboard_bits, flippable_mask, rng_state, mutated_bit_out);
    canvas->is_adjudicated_sound = 0;
    return canvas->switchboard_bits;
}

#ifdef __cplusplus
}
#endif

#endif /* FLOW_BITMANIFOLD_H */
