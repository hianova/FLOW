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

#ifdef __cplusplus
}
#endif

#endif /* FLOW_BITMANIFOLD_H */
