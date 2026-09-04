#include "simd_manifold.h"
#include <string.h>

FlowVector512 flow_v512_from_u64s(uint64_t w0, uint64_t w1, uint64_t w2, uint64_t w3,
                                  uint64_t w4, uint64_t w5, uint64_t w6, uint64_t w7) {
    FlowVector512 res;
    res.u64[0] = w0;
    res.u64[1] = w1;
    res.u64[2] = w2;
    res.u64[3] = w3;
    res.u64[4] = w4;
    res.u64[5] = w5;
    res.u64[6] = w6;
    res.u64[7] = w7;
    return res;
}

FlowVector512 flow_v512_broadcast(uint64_t scalar) {
    FlowVector512 res;
    for (int i = 0; i < 8; ++i) {
        res.u64[i] = scalar;
    }
    return res;
}

FlowVector512 flow_v512_zero(void) {
    FlowVector512 res;
    memset(&res, 0, sizeof(res));
    return res;
}

FlowVector512 flow_v512_all_ones(void) {
    FlowVector512 res;
    memset(&res, 0xFF, sizeof(res));
    return res;
}

FlowVector512 flow_v512_project(FlowVector512 genome,
                                FlowVector512 hard_mask,
                                FlowVector512 soft_bias) {
    FlowVector512 res;
    /* Native 512-bit vector bitwise operations in a single vector cycle */
    res.vec = (genome.vec & hard_mask.vec) | (soft_bias.vec & hard_mask.vec);
    return res;
}

FlowVector512 flow_v512_semilattice_join(FlowVector512 a,
                                         FlowVector512 b,
                                         FlowVector512 mask_a) {
    FlowVector512 res;
    /* Native 512-bit join-semilattice confluence (\sqcup) */
    res.vec = (a.vec & mask_a.vec) | (b.vec & ~mask_a.vec);
    return res;
}

FlowVector512 flow_v512_and(FlowVector512 a, FlowVector512 b) {
    FlowVector512 res;
    res.vec = a.vec & b.vec;
    return res;
}

FlowVector512 flow_v512_or(FlowVector512 a, FlowVector512 b) {
    FlowVector512 res;
    res.vec = a.vec | b.vec;
    return res;
}

FlowVector512 flow_v512_xor(FlowVector512 a, FlowVector512 b) {
    FlowVector512 res;
    res.vec = a.vec ^ b.vec;
    return res;
}

FlowVector512 flow_v512_not(FlowVector512 a) {
    FlowVector512 res;
    res.vec = ~a.vec;
    return res;
}

uint32_t flow_v512_popcount(FlowVector512 v) {
    uint32_t count = 0;
    for (int i = 0; i < 8; ++i) {
        count += (uint32_t)__builtin_popcountll(v.u64[i]);
    }
    return count;
}

uint64_t flow_v512_horizontal_or(FlowVector512 v) {
    return v.u64[0] | v.u64[1] | v.u64[2] | v.u64[3] |
           v.u64[4] | v.u64[5] | v.u64[6] | v.u64[7];
}

uint64_t flow_v512_horizontal_and(FlowVector512 v) {
    return v.u64[0] & v.u64[1] & v.u64[2] & v.u64[3] &
           v.u64[4] & v.u64[5] & v.u64[6] & v.u64[7];
}
