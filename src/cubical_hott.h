#ifndef FLOW_CUBICAL_HOTT_H
#define FLOW_CUBICAL_HOTT_H

#include "smt.h"
#include "polyhedral.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FlowUnifiedSection FlowUnifiedSection;

/*
 * ============================================================================
 * FLOW Discrete Cubical Homotopy Type Theory (HoTT) & Grothendieck Topos Engine
 * ============================================================================
 * Implements constructive Homotopy Type Theory on discrete cubical sets:
 * 
 * 1. Abstract Interval I = {0, 1}: De Morgan algebra with meet (AND),
 *    join (OR), and involution ~ (NOT).
 * 2. 64-Bit Boundary Basis Sieve: Represents 64 morphisms/dimensions in a single
 *    CPU register, enabling single-cycle cubical face operators and degeneracies.
 * 3. Kan Condition & Open-Box Filling:
 *      Mismatch = (Mask_p ^ Mask_q) & BoundaryFilter
 *      Mismatch == 0  <=>  Kan filler exists (p ~ q are homotopic).
 * 4. Subobject Classifier Omega = 2 = {0, 1}:
 *      Hom(X, Omega) ~= Sub(X). Pure Ising spin {-1, +1} -> {0, 1}.
 * 5. Four-Layer Vertical Integration:
 *      1-bit Canva (Omega) -> 64-bit Subset (Kan) -> Polytope -> Fiber Bundle.
 * ============================================================================
 */

/* De Morgan Abstract Interval I = {0, 1} */
typedef uint8_t FlowDeMorganInterval;

#define FLOW_DEMORGAN_ZERO 0
#define FLOW_DEMORGAN_ONE  1

/* De Morgan Algebra Elementary Involutive Operations */
static inline FlowDeMorganInterval flow_demorgan_not(FlowDeMorganInterval i) {
    return (FlowDeMorganInterval)(1 - (i & 1));
}

static inline FlowDeMorganInterval flow_demorgan_and(FlowDeMorganInterval a, FlowDeMorganInterval b) {
    return (FlowDeMorganInterval)((a & 1) & (b & 1));
}

static inline FlowDeMorganInterval flow_demorgan_or(FlowDeMorganInterval a, FlowDeMorganInterval b) {
    return (FlowDeMorganInterval)((a & 1) | (b & 1));
}

/* 64-bit Parallel Word De Morgan Operations (64 Dimensions in 1 CPU Cycle) */
static inline uint64_t flow_demorgan_word_not(uint64_t w) {
    return ~w;
}

static inline uint64_t flow_demorgan_word_and(uint64_t w1, uint64_t w2) {
    return w1 & w2;
}

static inline uint64_t flow_demorgan_word_or(uint64_t w1, uint64_t w2) {
    return w1 | w2;
}

/* ------------------------------------------------------------------------- */
/* Cubical Sets: Face Operators, Degeneracies & Covering Sieves              */
/* ------------------------------------------------------------------------- */

#define FLOW_CUBICAL_MAX_DIM 64

typedef struct {
    uint64_t arrows;             /* Bitmask of active morphisms / boundary basis directions */
    uint32_t dimension;          /* Active dimensionality (<= 64) */
    uint8_t is_closed;           /* 1 if boundary is closed (partial o partial = 0) */
    uint8_t reserved[3];
} FlowCubicalSieve;

/* Face Operator: Project dimension k to endpoint epsilon in {0, 1} */
uint64_t flow_cubical_face_proj(uint64_t sieve, uint32_t dim_k, uint8_t endpoint);

/* Degeneracy Operator: Insert degenerate identity dimension at index k */
uint64_t flow_cubical_degeneracy(uint64_t sieve, uint32_t dim_k);

/* Sieve Meet (Intersection) & Join (Union) */
static inline uint64_t flow_cubical_sieve_intersection(uint64_t s1, uint64_t s2) {
    return s1 & s2;
}

static inline uint64_t flow_cubical_sieve_union(uint64_t s1, uint64_t s2) {
    return s1 | s2;
}

/* Check if Sieve s covers target basis T: (s & T) == T */
static inline bool flow_cubical_sieve_is_covering(uint64_t sieve, uint64_t target_basis) {
    return (sieve & target_basis) == target_basis;
}

/* ------------------------------------------------------------------------- */
/* Kan Open-Box Condition & Homotopy Equivalence Proof                       */
/* ------------------------------------------------------------------------- */

typedef enum {
    FLOW_KAN_FILLED_HOMOTOPIC       = 0, /* Kan filler exists; paths homotopic */
    FLOW_KAN_OBSTRUCTED_SINGULARITY = 1  /* Boundary conflict; topological singularity */
} FlowKanStatus;

typedef struct {
    uint64_t mask_p;             /* Path p boundary face encoding */
    uint64_t mask_q;             /* Path q boundary face encoding */
    uint64_t boundary_filter;    /* Clamped boundary dimensions */
    uint64_t mismatch_mask;      /* (mask_p ^ mask_q) & boundary_filter */
    FlowKanStatus status;        /* Verification verdict */
} FlowKanBox;

/*
 * Kan Homotopy Equivalence Check:
 * Computes:
 *   Mismatch = (mask_p ^ mask_q) & boundary_filter
 * If Mismatch == 0, returns FLOW_KAN_FILLED_HOMOTOPIC, meaning 2-cell filler exists.
 */
FlowKanStatus flow_kan_check_homotopy(uint64_t mask_p,
                                      uint64_t mask_q,
                                      uint64_t boundary_filter,
                                      uint64_t *mismatch_out);

/* ------------------------------------------------------------------------- */
/* Grothendieck Topos Subobject Classifier Omega = 2 = {0, 1}                */
/* ------------------------------------------------------------------------- */

/*
 * Characteristic Function: Sub(X) ~= Hom(X, Omega)
 * Classifies whether subobject S is validly embedded in ambient sieve A.
 * Returns 1 (True / Preserved) or 0 (False / Breached).
 */
uint8_t flow_topos_classify_subobject(uint64_t subobject_mask, uint64_t ambient_sieve);

/* ------------------------------------------------------------------------- */
/* Polytope Handover Protocol                                                */
/* ------------------------------------------------------------------------- */

/*
 * Bridges 64-bit subset cubical layer to Polyhedral Convex Engine:
 * 1. Checks Kan homotopy between path p and path q under boundary_filter.
 * 2. If obstructed, immediately aborts (returns 0) to protect Polytope from
 *    degenerate singular numerical space.
 * 3. If filled, computes integer displacement offset Delta_j for each lattice
 *    dimension and shifts the Polytope bounding hyperplanes.
 */
int flow_cubical_handover_polytope(uint64_t mask_p,
                                   uint64_t mask_q,
                                   uint64_t boundary_filter,
                                   FlowPolyhedron *poly,
                                   int64_t offset_out[FLOW_POLY_MAX_DIM]);

/* ------------------------------------------------------------------------- */
/* Fiber Bundle Connection & Holonomy Integration                            */
/* ------------------------------------------------------------------------- */

/*
 * Computes Z_2 holonomy around closed cycle in base manifold.
 * If holonomy is odd parity (topologically non-trivial loop), applies
 * discrete spin flip p_k <- -p_k on fiber momentum to preserve exact
 * symplectic Hamiltonian conservation.
 */
int flow_cubical_holonomy_twist(uint64_t loop_sieve, FlowUnifiedSection *sec);

/* ------------------------------------------------------------------------- */
/* Formal SMT Supreme Court Attestation for Cubical HoTT Engine               */
/* ------------------------------------------------------------------------- */

FlowSMTResult flow_cubical_verify_smt(const FlowCubicalSieve *sieve,
                                      uint64_t mask_p,
                                      uint64_t mask_q,
                                      uint64_t boundary_filter,
                                      FlowSMTProofAttestation *proof_out);

/* ========================================================================= */
/* 64-Axis Geometric Basis Dictionary & BitSpace Semantic Decoder             */
/* Hypercube Topology: \mathbb{I}^{64} = {0, 1}^{64}                         */
/* Unifying Semantics (Face), Action (Path), and Decision (Kan Pullback)     */
/* ========================================================================= */

typedef enum {
    FLOW_CUBICAL_SUBSPACE_MEMORY      = 0,  /* Bits 0–7:   Memory Quota, Capacity, Bump Arena, Columnar */
    FLOW_CUBICAL_SUBSPACE_CONCURRENCY = 1,  /* Bits 8–15:  Threads, Shards, NUMA Affinity, QSBR Epoch */
    FLOW_CUBICAL_SUBSPACE_MICROARCH   = 2,  /* Bits 16–23: SIMD V*, Loop Tile T*, L1/L2 Prefetch, Branchless */
    FLOW_CUBICAL_SUBSPACE_LAYOUT      = 3,  /* Bits 24–31: AoS vs SoA, Columnar Partition, Shannon MTD */
    FLOW_CUBICAL_SUBSPACE_PHYSICAL    = 4,  /* Bits 32–47: Embodied Reflex, Joint Torques, ZMP Polygon, Friction Cone */
    FLOW_CUBICAL_SUBSPACE_SECURITY    = 5   /* Bits 48–63: W^X JIT, Cap Bounds, Straggler Quarantine, Fail-Safe Fallback */
} FlowCubicalSubspaceType;

typedef struct {
    uint32_t axis_index;               /* 0 to 63 */
    FlowCubicalSubspaceType subspace;
    const char *axis_name;             /* e.g. "capacity_exponent", "zmp_stability", "memory_quota" */
    const char *policy_contract;       /* e.g. "Global Memory Quota Ceiling", "ZMP Tip-over Boundary" */
    const char *module_id;             /* Primary bound module e.g. "jit", "reload", "embodied", "smt" */
    const char *subspace_name;         /* Subspace title */
    const char *canonical_explanation; /* Deterministic causal rationale when this bit flips */
} FlowCubicalAxisInfo;

typedef struct {
    uint32_t flipped_axis;
    FlowCubicalSubspaceType subspace;
    const FlowCubicalAxisInfo *axis_info;
    uint64_t v_pre;
    uint64_t v_post;
    int is_kan_homotopic;
    char explanation[512];
    char pre_topology[64];
    char post_topology[64];
} FlowCubicalTransitionReport;

/* Query 64-Axis Basis Info */
const FlowCubicalAxisInfo *flow_cubical_get_axis_info(uint32_t axis_idx);

/* Geometric Decoder: Decode a transition between two BitSpace vertices */
int flow_cubical_decode_transition(uint64_t v_pre,
                                   uint64_t v_post,
                                   uint32_t explicit_axis,
                                   FlowCubicalTransitionReport *report_out);

/* Text Intent Projection: Project query text to a 64-bit BitSpace Intent Mask */
uint64_t flow_cubical_project_text_intent(const char *text);

/* Module Signature Mask: Get 64-bit BitSpace characteristic mask for a module */
uint64_t flow_cubical_get_module_mask(const char *module_id);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_CUBICAL_HOTT_H */
