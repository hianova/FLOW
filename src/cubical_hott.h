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
 * 1. Abstract Interval I = {0, 1}: De Morgan algebra with meet (AND), join (OR), NOT (~).
 * 2. 64-Bit Boundary Basis Sieve: 64 morphisms/dimensions in a single CPU register.
 * 3. Kan Condition & Open-Box Filling: Mismatch = (Mask_p ^ Mask_q) & BoundaryFilter == 0.
 * 4. Subobject Classifier Omega = 2 = {0, 1}: Hom(X, Omega) ~= Sub(X).
 * 5. Unified Homotopy Type Matching for SemanticIR, Component & Codebase Topology.
 * ============================================================================
 */
typedef uint8_t FlowDeMorganInterval;
#define FLOW_DEMORGAN_ZERO 0
#define FLOW_DEMORGAN_ONE  1
#define FLOW_CUBICAL_MAX_DIM 64
static inline FlowDeMorganInterval flow_demorgan_not(FlowDeMorganInterval i) { return (FlowDeMorganInterval)(1 - (i & 1)); }
static inline FlowDeMorganInterval flow_demorgan_and(FlowDeMorganInterval a, FlowDeMorganInterval b) { return (FlowDeMorganInterval)((a & 1) & (b & 1)); }
static inline FlowDeMorganInterval flow_demorgan_or(FlowDeMorganInterval a, FlowDeMorganInterval b) { return (FlowDeMorganInterval)((a & 1) | (b & 1)); }
static inline uint64_t flow_demorgan_word_not(uint64_t w) { return ~w; } static inline uint64_t flow_demorgan_word_and(uint64_t w1, uint64_t w2) { return w1 & w2; }
static inline uint64_t flow_demorgan_word_or(uint64_t w1, uint64_t w2) { return w1 | w2; } typedef struct { uint64_t arrows; uint32_t dimension;
    uint8_t is_closed, reserved[3]; } FlowCubicalSieve; uint64_t flow_cubical_face_proj(uint64_t sieve, uint32_t dim_k, uint8_t endpoint);
uint64_t flow_cubical_degeneracy(uint64_t sieve, uint32_t dim_k);
static inline bool flow_cubical_sieve_is_covering(uint64_t sieve, uint64_t target_basis) { return (sieve & target_basis) == target_basis; } typedef enum {
    FLOW_KAN_FILLED_HOMOTOPIC       = 0, FLOW_KAN_OBSTRUCTED_SINGULARITY = 1
} FlowKanStatus; typedef struct { uint64_t mask_p, mask_q, boundary_filter, mismatch_mask; FlowKanStatus status; } FlowKanBox;
FlowKanStatus flow_kan_check_homotopy(uint64_t mask_p, uint64_t mask_q, uint64_t boundary_filter, uint64_t *mismatch_out);
uint8_t flow_topos_classify_subobject(uint64_t subobject_mask, uint64_t ambient_sieve);
int flow_cubical_handover_polytope(uint64_t mask_p, uint64_t mask_q, uint64_t boundary_filter, FlowPolyhedron *poly, int64_t offset_out[FLOW_POLY_MAX_DIM]);
int flow_cubical_holonomy_twist(uint64_t loop_sieve, FlowUnifiedSection *sec);
FlowSMTResult flow_cubical_verify_smt(const FlowCubicalSieve *sieve, uint64_t mask_p, uint64_t mask_q, uint64_t boundary_filter, FlowSMTProofAttestation *proof_out);
typedef struct { uint64_t mask;            /* 64-bit capability / intent face in I^64 */
    uint64_t boundary_filter; /* Clamped invariant boundary dimensions */
} FlowCubicalTypeSieve; FlowCubicalTypeSieve flow_cubical_sieve_from_ir(const SemanticIR *ir); FlowCubicalTypeSieve flow_cubical_sieve_from_component(const Component *comp);
int flow_cubical_match_types(FlowCubicalTypeSieve intent, FlowCubicalTypeSieve candidate, uint64_t *mismatch_out); typedef enum { FLOW_CUBICAL_SUBSPACE_MEMORY      = 0,
    FLOW_CUBICAL_SUBSPACE_CONCURRENCY = 1, FLOW_CUBICAL_SUBSPACE_MICROARCH   = 2, FLOW_CUBICAL_SUBSPACE_LAYOUT      = 3, FLOW_CUBICAL_SUBSPACE_PHYSICAL    = 4,
    FLOW_CUBICAL_SUBSPACE_SECURITY    = 5
} FlowCubicalSubspaceType; typedef struct { uint32_t axis_index; FlowCubicalSubspaceType subspace;
    const char *axis_name, *policy_contract, *module_id, *subspace_name, *canonical_explanation; } FlowCubicalAxisInfo; typedef struct { uint32_t flipped_axis;
    FlowCubicalSubspaceType subspace; const FlowCubicalAxisInfo *axis_info; uint64_t v_pre, v_post; int is_kan_homotopic;
    char explanation[512], pre_topology[64], post_topology[64]; } FlowCubicalTransitionReport; const FlowCubicalAxisInfo *flow_cubical_get_axis_info(uint32_t axis_idx);
int flow_cubical_decode_transition(uint64_t v_pre, uint64_t v_post, uint32_t explicit_axis, FlowCubicalTransitionReport *report_out);
uint64_t flow_cubical_project_text_intent(const char *text); uint64_t flow_cubical_get_module_mask(const char *module_id);
#ifdef __cplusplus
}
#endif
#endif /* FLOW_CUBICAL_HOTT_H */
