#include "simd_manifold.h"
#include <string.h>
FlowVector512 flow_v512_from_u64s(uint64_t w0, uint64_t w1, uint64_t w2, uint64_t w3, uint64_t w4, uint64_t w5, uint64_t w6, uint64_t w7) {
    return (FlowVector512){ .u64 = {w0, w1, w2, w3, w4, w5, w6, w7} }; }
FlowVector512 flow_v512_broadcast(uint64_t s) { return (FlowVector512){ .u64 = {s, s, s, s, s, s, s, s} }; }
FlowVector512 flow_v512_zero(void) { return (FlowVector512){0}; }
FlowVector512 flow_v512_all_ones(void) { return flow_v512_broadcast(~0ULL); }
FlowVector512 flow_v512_project(FlowVector512 g, FlowVector512 h, FlowVector512 b) { return (FlowVector512){ .vec = (g.vec & h.vec) | (b.vec & h.vec) }; }
FlowVector512 flow_v512_and(FlowVector512 a, FlowVector512 b) { return (FlowVector512){ .vec = a.vec & b.vec }; }
uint32_t flow_v512_popcount(FlowVector512 v) { uint32_t c = 0; for (int i = 0; i < 8; ++i) c += (uint32_t)__builtin_popcountll(v.u64[i]); return c; }
uint64_t flow_v512_horizontal_or(FlowVector512 v) { return v.u64[0] | v.u64[1] | v.u64[2] | v.u64[3] | v.u64[4] | v.u64[5] | v.u64[6] | v.u64[7]; }
uint64_t flow_v512_horizontal_and(FlowVector512 v) { return v.u64[0] & v.u64[1] & v.u64[2] & v.u64[3] & v.u64[4] & v.u64[5] & v.u64[6] & v.u64[7]; }
