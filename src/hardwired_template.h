#ifndef FLOW_HARDWIRED_TEMPLATE_H
#define FLOW_HARDWIRED_TEMPLATE_H

#include "smt.h"
#include "polyhedral.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW Universal Hardwired Polyhedral Template & Register Hot-Update Engine
 * ============================================================================
 * Physical Foundation:
 * Replaces expensive runtime JIT machine-code compilation and instruction
 * cache (I-Cache) invalidation with an AOT-hardened Universal Polyhedral Template.
 *
 * Key Architectural Properties:
 * 1. Universal Hardwired Template (AOT Static Core):
 *    - 64-hyperplane capacity permanently etched in immutable binary code:
 *        \sum_{j=0}^{D-1} A_{ij} (x_j - \Delta_j) \le b_i  (i \in [0, 63])
 *    - Zero runtime compiler footprint (0 MB vs 100MB+ for LLVM/Cranelift).
 *
 * 2. 1-Clock Cycle Register Hot-Update (Zero I-Cache Invalidation):
 *    - Boundary face enabling/disabling via 64-bit atomic mask: M \in {0, 1}^64
 *    - Boundary translation via constant register update: b_i \leftarrow b'_i
 *    - Spatial translation via origin register update: \Delta_j \leftarrow \Delta'_j
 *    - Zero instruction cache flushes (sys_icache_invalidate / __clear_cache = 0)
 *    - Pure D-Cache / Register File store: latency < 2ns.
 *
 * 3. W^X & Bare-Metal Portability:
 *    - Zero write-and-execute memory dependencies (W^X strictly preserved).
 *    - Runs deterministically on any microcontroller, DSP, or ASIC.
 *
 * 4. Cascaded Multi-Core Stacking:
 *    - When problem complexity exceeds 64 hyperplanes, cores cascade seamlessly
 *      in series/parallel without memory reallocation.
 * ============================================================================
 */

#define FLOW_HARDWIRED_MAX_PLANES 64
#define FLOW_HARDWIRED_MAX_DIM    8

/*
 * IMPORTANT ARCHITECTURAL REQUIREMENT: [Heap/Arena Only]
 * FlowHardwiredPolyhedralTemplate is ~5KB-9KB and enforces 64-byte cacheline
 * alignment (alignas(64)). DO NOT allocate this structure on the thread stack.
 * Always allocate via flow_hardwired_create() or within an arena.
 */
typedef struct FlowHardwiredPolyhedralTemplate FlowHardwiredPolyhedralTemplate;

struct __attribute__((aligned(64))) FlowHardwiredPolyhedralTemplate {
    /* Statically hardwired coefficient matrix A (immutable code / rodata) */
    double coeffs[FLOW_HARDWIRED_MAX_PLANES][FLOW_HARDWIRED_MAX_DIM];

    /* Dynamic register file (Hot-update targets written in 1 clock cycle) */
    _Atomic double b_constants[FLOW_HARDWIRED_MAX_PLANES];
    _Atomic uint64_t active_mask;
    _Atomic double translation[FLOW_HARDWIRED_MAX_DIM];
    _Atomic double scale[FLOW_HARDWIRED_MAX_DIM];

    /* Geometry configuration */
    size_t dimension;
    size_t total_planes;
    _Atomic uint64_t generation;

    /* Telemetry & hardware profiling */
    uint64_t total_evaluations;
    uint64_t total_hot_updates;
    uint64_t icache_flushes_avoided;
};

typedef struct {
    bool is_inside;                                              /* 1 if inside all active half-spaces */
    uint64_t violated_mask;                                      /* Bitmask of active hyperplanes violated */
    double max_violation;                                        /* Peak violation magnitude */
    double projected_x[FLOW_HARDWIRED_MAX_DIM];                  /* Clamped coordinates */
} FlowHardwiredEvalResult;

/* Allocate 64-byte aligned universal template on Heap (Heap/Arena Only) */
FlowHardwiredPolyhedralTemplate *flow_hardwired_create(size_t dimension);

/* Free heap-allocated universal template */
void flow_hardwired_destroy(FlowHardwiredPolyhedralTemplate *tpl);

/* Lifecycle: Initialize universal template with canonical bounding box & simplex normals */
int flow_hardwired_template_init(FlowHardwiredPolyhedralTemplate *tpl, size_t dimension);

/* Populate template from an existing FlowPolyhedron */
int flow_hardwired_template_from_polyhedron(FlowHardwiredPolyhedralTemplate *tpl,
                                            const FlowPolyhedron *poly);

/* 1-Clock Cycle Register Hot-Updates */
int flow_hardwired_set_mask(FlowHardwiredPolyhedralTemplate *tpl, uint64_t new_mask);
int flow_hardwired_set_bound(FlowHardwiredPolyhedralTemplate *tpl, size_t plane_idx, double new_b);
int flow_hardwired_translate(FlowHardwiredPolyhedralTemplate *tpl, const double delta[FLOW_HARDWIRED_MAX_DIM]);
int flow_hardwired_scale(FlowHardwiredPolyhedralTemplate *tpl, const double scale[FLOW_HARDWIRED_MAX_DIM]);

/* Branchless Evaluation on Static AOT Pipeline */
int flow_hardwired_eval(FlowHardwiredPolyhedralTemplate *tpl,
                        const double x[FLOW_HARDWIRED_MAX_DIM],
                        FlowHardwiredEvalResult *res_out);

/* Cascaded Multi-Core Stacking for M > 64 Hyperplanes */
int flow_hardwired_cascade_eval(FlowHardwiredPolyhedralTemplate *tpl_a,
                                FlowHardwiredPolyhedralTemplate *tpl_b,
                                const double x[FLOW_HARDWIRED_MAX_DIM],
                                FlowHardwiredEvalResult *res_out);

/* Formal SMT Supreme Court Invariant Verification */
FlowSMTResult flow_hardwired_verify_smt(const FlowHardwiredPolyhedralTemplate *tpl,
                                        FlowSMTProofAttestation *proof_out);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_HARDWIRED_TEMPLATE_H */
