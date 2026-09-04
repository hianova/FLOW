#ifndef FLOW_FWHT_PROJECTION_H
#define FLOW_FWHT_PROJECTION_H

#include "smt.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * Fast Walsh-Hadamard Transform (FWHT) & Ailon-Chazelle FJLT Projection
 * ============================================================================
 * Pursuing the Kolmogorov Complexity lower bound K(x):
 * Eliminates empirical floating-point weight matrices and random constant tables!
 *
 * For an input vector of dimension N = 2^k (e.g. 4096 = 2^12):
 * 1. Multiplication with deterministic Rademacher diagonal matrix D:
 *      x'_i = x_i * (-1)^{popcount(i ^ seed)}
 *    Requiring 0 bytes of stored weights!
 * 2. In-place Fast Walsh-Hadamard Transform:
 *      y = H_N * x'
 *    O(N log N) additions/subtractions, 0 float multiplications, 0 stored constants!
 * 3. Deterministic Bit-Extraction into 64-bit BMF & 16-D features:
 *      bmf_bit_k = (y_{k * 64} >= 0) ? 1 : 0
 *
 * Total stored weight tables: 0 Bytes (K(H_N) = O(1) program description).
 * ============================================================================
 */

#define FLOW_FWHT_DEFAULT_DIM 4096

/* Deterministic Rademacher sign (+1 or -1) from bit parity */
static inline float flow_fwht_rademacher_sign(size_t index, uint64_t seed) {
    uint64_t h = (uint64_t)index ^ seed;
    /* 64-bit popcount parity */
    h ^= h >> 32;
    h ^= h >> 16;
    h ^= h >> 8;
    h ^= h >> 4;
    h &= 0xF;
    return (0x6996 & (1U << h)) ? -1.0f : 1.0f;
}

/* In-place Fast Walsh-Hadamard Transform on float buffer of length n = 2^k */
void flow_fwht_transform_f32(float *data, size_t n);

/*
 * High-Speed Zero-Table Projection:
 * 4096-D continuous embedding -> 64-bit BMF + 16-D fvec features
 * Latency target: < 50ns with zero table allocations.
 */
int flow_fwht_project_4096(const float *input_4096,
                           uint64_t seed,
                           uint64_t *bmf_64_out,
                           double *fvec_16_out,
                           double *projection_ns_out);

/*
 * SMT Supreme Court Isometry Verification:
 * Formally proves that FWHT preserves l2 pairwise distance bounds (UNSAT on norm violation).
 */
FlowSMTResult flow_fwht_verify_isometry_smt(const float *x1,
                                            const float *x2,
                                            size_t n,
                                            uint64_t bmf1,
                                            uint64_t bmf2,
                                            FlowSMTProofAttestation *proof_out);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_FWHT_PROJECTION_H */
