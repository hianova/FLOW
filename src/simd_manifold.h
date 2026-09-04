#ifndef FLOW_SIMD_MANIFOLD_H
#define FLOW_SIMD_MANIFOLD_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW 512-Bit SIMD Vector Manifold (simd_manifold.h)
 * ============================================================================
 * Upgrades the 64-bit BitManifold to native 512-bit vector registers.
 * A 512-bit vector register holds 8 x 64-bit orthogonal subspaces, executing
 * discrete attention projections, bounds filtering, and join-semilattice
 * confluence in a single vector instruction cycle.
 * ============================================================================
 */

typedef uint64_t flow_v512_u64 __attribute__((vector_size(64)));

typedef union {
    flow_v512_u64 vec;
    uint64_t u64[8];
    uint32_t u32[16];
    uint8_t  u8[64];
} FlowVector512;

/* Initialize 512-bit vector from 8 scalar 64-bit words */
FlowVector512 flow_v512_from_u64s(uint64_t w0, uint64_t w1, uint64_t w2, uint64_t w3,
                                  uint64_t w4, uint64_t w5, uint64_t w6, uint64_t w7);

/* Broadcast a single 64-bit scalar word to all 8 lanes */
FlowVector512 flow_v512_broadcast(uint64_t scalar);

/* Return zero vector */
FlowVector512 flow_v512_zero(void);

/* Return all-ones vector */
FlowVector512 flow_v512_all_ones(void);

/*
 * Vectorized BMF Discrete Attention Operator:
 *     Canvas_{t+1} = \Phi(Canvas_t \otimes Mask_{Attn(t)})
 * Simultaneously projects all 8 orthogonal subspaces in 1 vector cycle!
 */
FlowVector512 flow_v512_project(FlowVector512 genome,
                                FlowVector512 hard_mask,
                                FlowVector512 soft_bias);

/* 512-bit Join-Semilattice Confluence: a \sqcup b across all lanes */
FlowVector512 flow_v512_semilattice_join(FlowVector512 a,
                                         FlowVector512 b,
                                         FlowVector512 mask_a);

/* Vectorized bitwise AND */
FlowVector512 flow_v512_and(FlowVector512 a, FlowVector512 b);

/* Vectorized bitwise OR */
FlowVector512 flow_v512_or(FlowVector512 a, FlowVector512 b);

/* Vectorized bitwise XOR */
FlowVector512 flow_v512_xor(FlowVector512 a, FlowVector512 b);

/* Vectorized bitwise NOT */
FlowVector512 flow_v512_not(FlowVector512 a);

/* Total population count (number of set bits) across all 512 bits */
uint32_t flow_v512_popcount(FlowVector512 v);

/* Horizontal bitwise reduction (OR of all 8 lanes into a single 64-bit word) */
uint64_t flow_v512_horizontal_or(FlowVector512 v);

/* Horizontal bitwise reduction (AND of all 8 lanes) */
uint64_t flow_v512_horizontal_and(FlowVector512 v);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_SIMD_MANIFOLD_H */
