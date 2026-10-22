#ifndef FLOW_SIMD_MANIFOLD_H
#define FLOW_SIMD_MANIFOLD_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
typedef uint64_t flow_v512_u64 __attribute__((vector_size(64)));
typedef union { flow_v512_u64 vec; uint64_t u64[8]; uint32_t u32[16]; uint8_t u8[64]; } FlowVector512;
FlowVector512 flow_v512_from_u64s(uint64_t w0, uint64_t w1, uint64_t w2, uint64_t w3, uint64_t w4, uint64_t w5, uint64_t w6, uint64_t w7);
FlowVector512 flow_v512_broadcast(uint64_t scalar); FlowVector512 flow_v512_zero(void); FlowVector512 flow_v512_all_ones(void);
FlowVector512 flow_v512_project(FlowVector512 genome, FlowVector512 hard_mask, FlowVector512 soft_bias);
FlowVector512 flow_v512_and(FlowVector512 a, FlowVector512 b);
uint32_t flow_v512_popcount(FlowVector512 v); uint64_t flow_v512_horizontal_or(FlowVector512 v); uint64_t flow_v512_horizontal_and(FlowVector512 v);
#endif /* FLOW_SIMD_MANIFOLD_H */
