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
typedef uint64_t flow_v512_u64 __attribute__((vector_size(64))); typedef union { flow_v512_u64 vec; uint64_t u64[8]; uint32_t u32[16]; uint8_t  u8[64]; } FlowVector512;
FlowVector512 flow_v512_from_u64s(uint64_t w0, uint64_t w1, uint64_t w2, uint64_t w3, uint64_t w4, uint64_t w5, uint64_t w6, uint64_t w7);
FlowVector512 flow_v512_broadcast(uint64_t scalar); FlowVector512 flow_v512_zero(void); FlowVector512 flow_v512_all_ones(void);
/*
 * Vectorized BMF Discrete Attention Operator:
 *     Canvas_{t+1} = \Phi(Canvas_t \otimes Mask_{Attn(t)})
 * Simultaneously projects all 8 orthogonal subspaces in 1 vector cycle!
 */
FlowVector512 flow_v512_project(FlowVector512 genome, FlowVector512 hard_mask, FlowVector512 soft_bias); FlowVector512 flow_v512_semilattice_join(FlowVector512 a,
                                         FlowVector512 b, FlowVector512 mask_a); FlowVector512 flow_v512_and(FlowVector512 a, FlowVector512 b);
FlowVector512 flow_v512_or(FlowVector512 a, FlowVector512 b); FlowVector512 flow_v512_xor(FlowVector512 a, FlowVector512 b); FlowVector512 flow_v512_not(FlowVector512 a);
uint32_t flow_v512_popcount(FlowVector512 v); uint64_t flow_v512_horizontal_or(FlowVector512 v); uint64_t flow_v512_horizontal_and(FlowVector512 v);
#ifdef __cplusplus
}
#endif
#endif /* FLOW_SIMD_MANIFOLD_H */
