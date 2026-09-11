#ifndef FLOW_MORSE_ATLAS_H
#define FLOW_MORSE_ATLAS_H

#include "bitspace.h"
#include "bitmanifold.h"
#include "smt.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * Topological Morse-Smale Subspace Atlas (FlowBmfMorseAtlas)
 * ============================================================================
 * Pursuing the Kolmogorov Complexity lower bound:
 * Eliminates human-centric symbolic strings and manual subspace nomenclature!
 *
 * In pure algorithmic information theory, names like "smooth_fetch_latte" are
 * fluff. The physical manifold decomposes naturally into Morse cells:
 *
 *     M = \bigcup_{k} \mathcal{W}^s(p_k)
 *
 * where p_k are critical points (\nabla E(p_k) = 0) of the system's Lyapunov
 * energy functional, and \mathcal{W}^s(p_k) is the stable attraction basin.
 *
 * Each Morse cell is strictly an integer index k with:
 * 1. Critical Point coordinates (Attractor Center in R^16)
 * 2. Morse Index (Dimension of unstable tangent bundle: 0=attractor, 1=saddle)
 * 3. Topological Invariants (b_0, b_1)
 * 4. 1-Bit Invariant Mask (Hardware/Physical Presburger constraints)
 * 5. 1-Bit Malleable Mask (Free degrees of freedom for annealing)
 *
 * When an environmental bifurcation occurs, cells split/merge without manual
 * developer intervention.
 * ============================================================================
 */

#define FLOW_MORSE_MAX_CELLS 16
#define FLOW_MORSE_DIM       FLOW_BMF_SUBSPACE_FEATURE_DIM

typedef struct {
    uint32_t cell_id;                     /* Pure integer index, zero string fluff */
    double   critical_point[FLOW_MORSE_DIM]; /* Gradient vanishing attractor in phase space */
    uint32_t morse_index;                 /* Number of negative Hessian eigenvalues */
    uint32_t betti_0;                     /* Connected components */
    uint32_t betti_1;                     /* 1D cyclic holes */
    uint64_t invariant_mask;              /* Presburger hard 1-bit boundary constraints */
    uint64_t malleable_mask;              /* 1-bit chaotic annealing degrees of freedom */
    uint64_t default_switches;            /* Resting 1-bit state */
    double   basin_radius;                /* Radius of stable attraction basin */
} FlowBmfMorseCell;

typedef struct {
    FlowBmfMorseCell cells[FLOW_MORSE_MAX_CELLS];
    size_t cell_count;
    uint32_t global_betti_0;
    uint32_t global_betti_1;
    bool is_topologically_closed;
} FlowBmfMorseAtlas;

/* Initialize an empty Morse Atlas */
void flow_morse_atlas_init(FlowBmfMorseAtlas *atlas);

/* Populate canonical Morse cells from foundational physical basins */
void flow_morse_atlas_seed_canonical(FlowBmfMorseAtlas *atlas);

/* Add a topologically discovered Morse cell */
int flow_morse_atlas_add_cell(FlowBmfMorseAtlas *atlas,
                              uint32_t cell_id,
                              const double *critical_pt,
                              size_t dim,
                              uint32_t morse_index,
                              uint64_t invariant_mask,
                              uint64_t malleable_mask,
                              uint64_t default_switches,
                              double basin_radius);

/*
 * Label-Free Topological Subspace Routing:
 * Direct distance metric in phase space: returns nearest Morse cell index in < 15ns.
 */
uint32_t flow_morse_atlas_route(const FlowBmfMorseAtlas *atlas, const double *features, size_t dim);

/*
 * Autonomous Bifurcation:
 * Splits a Morse cell along an unstable eigenvector when environmental entropy shifts.
 */
int flow_morse_atlas_bifurcate(FlowBmfMorseAtlas *atlas,
                               uint32_t source_cell_id,
                               double bifurcation_drift,
                               uint32_t *new_cell_id_out);

/*
 * SMT Supreme Court Topological Completeness Proof:
 * Proves that the atlas forms a complete covering (Atlas Partition of Unity)
 * with zero unreachable dead zones across the operational hypercube (UNSAT).
 */
FlowSMTResult flow_morse_verify_partition_completeness_smt(const FlowBmfMorseAtlas *atlas,
                                                           FlowSMTProofAttestation *proof_out);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_MORSE_ATLAS_H */
