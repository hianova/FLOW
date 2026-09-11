#ifndef FLOW_SIMPLICIAL_HOMOLOGY_H
#define FLOW_SIMPLICIAL_HOMOLOGY_H

#include "smt.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW Simplicial Homology & Algebraic Topology Defect Mining
 * ============================================================================
 * Replaces empirical genetic fuzzing (AFL branch guessing) with Sheaf Theory
 * and Simplicial Homology on the program's control-flow state complex K:
 *
 *   C_2 --partial_2--> C_1 --partial_1--> C_0
 *   partial_1 o partial_2 = 0  (The boundary of a boundary is empty)
 *
 * Cycles:     Z_1 = ker(partial_1)
 * Boundaries: B_1 = im(partial_2)
 * Homology:   H_1 = Z_1 / B_1
 * Betti No:   b_1 = dim(H_1)
 *
 * When b_1 > 0, non-trivial "topological holes" exist (uncovered boundary
 * execution cycles). Directed topological ray casting guides BMF mutations
 * specifically toward these holes.
 * ============================================================================
 */

#define FLOW_HOMOLOGY_MAX_VERTICES 32
#define FLOW_HOMOLOGY_MAX_EDGES    64
#define FLOW_HOMOLOGY_MAX_FACES    32

typedef struct {
    uint32_t u;
    uint32_t v;
} FlowSimplex1D; /* Edge */

typedef struct {
    uint32_t u;
    uint32_t v;
    uint32_t w;
} FlowSimplex2D; /* Triangle face */

typedef struct {
    size_t vertex_count;
    size_t edge_count;
    size_t face_count;
    FlowSimplex1D edges[FLOW_HOMOLOGY_MAX_EDGES];
    FlowSimplex2D faces[FLOW_HOMOLOGY_MAX_FACES];
    size_t betti_0;               /* Number of connected components */
    size_t betti_1;               /* Number of 1D topological holes */
    uint64_t total_topological_rays;
    uint64_t holes_uncovered;
} FlowSimplicialComplex;

/* Initialize Simplicial Complex with N vertices (basic blocks / states) */
int flow_homology_init(FlowSimplicialComplex *complex, size_t vertex_count);

/* Add 1-simplex (state transition edge) */
int flow_homology_add_edge(FlowSimplicialComplex *complex, uint32_t u, uint32_t v);

/* Add 2-simplex (commuting triangle face) */
int flow_homology_add_face(FlowSimplicialComplex *complex, uint32_t u, uint32_t v, uint32_t w);

/* Compute homology groups and Betti numbers (b_0, b_1) */
int flow_homology_compute_betti(FlowSimplicialComplex *complex, size_t *b0_out, size_t *b1_out);

/* Directed Topological Ray Casting: generates bitwise mutation targeting uncovered hole */
int flow_homology_guide_mutation(FlowSimplicialComplex *complex,
                                 uint64_t base_genome,
                                 uint64_t *guided_genome_out,
                                 uint32_t *target_simplex_out);

/* SMT Formal Algebraic Topology Theorem Proof (partial_1 o partial_2 = 0) */
FlowSMTResult flow_homology_verify_smt(const FlowSimplicialComplex *complex, FlowSMTProofAttestation *proof_out);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_SIMPLICIAL_HOMOLOGY_H */
