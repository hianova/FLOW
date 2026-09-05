#ifndef FLOW_PREFETCH_H
#define FLOW_PREFETCH_H

#include "bitmanifold.h"
#include "bitspace.h"
#include "morse_atlas.h"
#include "smt.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW Hardware Cache Prefetch & Manifold-Guided Speculative Warmer (flow_prefetch.h)
 * ============================================================================
 * Micro-Scale Optimization (1 ~ 50 ns):
 * Eliminates the Von Neumann Memory Wall and CPU cache miss stalls (20~100ns)
 * during sub-microsecond state transitions.
 *
 * Capabilities:
 * 1. Low-Level Hardware Prefetching:
 *    - L1 Data Cache Prefetch (temporal locality 3)
 *    - L2 Data Cache Prefetch (temporal locality 2)
 *    - Write-Intent Prefetch (RFO - Request For Ownership)
 *    - Instruction Cache Prefetch
 * 2. 64-Byte Cache Line Confinement:
 *    - Guarantees FlowBmf1BitCanvas is precisely 64 bytes with 64-byte alignment.
 * 3. Manifold Geodesic Topo-Prefetching:
 *    - Follows Riemannian state space geodesics; prefetches adjacent Morse cells
 *      and 1-bit Hamming neighbors prior to state collapse.
 * ============================================================================
 */

/* Hardware Cache Line Constants */
#define FLOW_CACHE_LINE_BYTES 64

/*
 * Low-level hardware cache prefetch primitives
 */
static inline void flow_prefetch_l1(const void *ptr) {
#if defined(__GNUC__) || defined(__clang__)
    __builtin_prefetch(ptr, 0, 3); /* Read, high temporal locality -> L1 */
#endif
}

static inline void flow_prefetch_l2(const void *ptr) {
#if defined(__GNUC__) || defined(__clang__)
    __builtin_prefetch(ptr, 0, 2); /* Read, moderate locality -> L2 */
#endif
}

static inline void flow_prefetch_write(const void *ptr) {
#if defined(__GNUC__) || defined(__clang__)
    __builtin_prefetch(ptr, 1, 3); /* Prepare for write (RFO) -> L1 */
#endif
}

/*
 * Manifold Geodesic Prefetching:
 * Inspects current 1-bit canvas state, computes adjacent Morse cell attractors
 * and Hamming-1 neighbors, and issues non-blocking L1 cache line loads ahead of time.
 */
int flow_prefetch_manifold_geodesic(const FlowBmf1BitCanvas *current_canvas,
                                    const FlowBmfMorseAtlas *atlas,
                                    uint64_t attention_mask);

/*
 * Token Ring Slot Prefetching:
 * Warm up the next slot's canvas, genome, and active component descriptor.
 */
int flow_prefetch_token_ring_slot(const void *next_slot_canvas_ptr,
                                  const void *next_component_ptr);

/*
 * SMT Supreme Court Prefetch Alignment Theorem:
 * Mathematically verifies that:
 * 1. sizeof(FlowBmf1BitCanvas) == 64 bytes (exact single cache line).
 * 2. alignof(FlowBmf1BitCanvas) == 64 bytes (zero cross-cache-line split).
 * 3. Prefetch execution latency is non-blocking (UNSAT on pipeline hazard).
 */
FlowSMTResult flow_prefetch_verify_alignment_smt(const FlowBmf1BitCanvas *canvas,
                                                 FlowSMTProofAttestation *proof_out);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_PREFETCH_H */
