#ifndef FLOW_CUBICAL_HOTT_H
#define FLOW_CUBICAL_HOTT_H
#include "bitspace.h"
#include "smt.h"
#include "polyhedral.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct FlowJet FlowJet;
typedef struct FlowThermalState {
    double temp_c, temp_ambient_c, dtemp_dt, r_thermal, c_thermal, temp_throttle_c, temp_target_c, active_power_w; uint64_t throttle_events; uint8_t is_throttled, reserved[7];
} FlowThermalState;
#define FLOW_AXIOM_DIM 64
#define FLOW_AXIOM_LATTICE_DIM 4
typedef struct FlowUnifiedSection FlowUnifiedSection;
struct __attribute__((aligned(64))) FlowUnifiedSection { double q[FLOW_AXIOM_DIM], p[FLOW_AXIOM_DIM], a[FLOW_AXIOM_DIM];
    double s, transversality_margin; int64_t lattice_idx[FLOW_AXIOM_LATTICE_DIM]; uint32_t optimal_tile_size, optimal_simd_width;
    uint64_t total_lattice_points, bmf_subspace_mask; FlowBmf1BitCanvas bmf_canvas; double curvature_spectrum[FLOW_AXIOM_DIM]; FlowThermalState thermal;
    FlowSMTProofAttestation proof; uint64_t cubical_sieve; uint8_t kan_homotopy_status, omega_classifier_bit; uint32_t crc32; uint8_t is_transversal, is_compact, reserved[16]; };
FlowUnifiedSection *flow_axiom_create(const char *intent); void flow_axiom_destroy(FlowUnifiedSection *sec);
int flow_axiom_init(FlowUnifiedSection *sec, const char *intent); int flow_axiom_eval_transversality(FlowUnifiedSection *sec, const FlowPolyhedron *poly);
int flow_axiom_project_bmf(FlowUnifiedSection *sec); int flow_axiom_step(FlowUnifiedSection *sec, double dt, double p_active_watts);
int flow_axiom_from_jet(FlowUnifiedSection *sec, const FlowJet *jet); int flow_axiom_to_jet(const FlowUnifiedSection *sec, FlowJet *jet_out);
int flow_axiom_eval_cubical_homotopy(FlowUnifiedSection *sec, uint64_t target_path, uint64_t boundary_filter);
FlowSMTResult flow_axiom_verify_smt(const FlowUnifiedSection *sec, FlowSMTProofAttestation *proof_out);
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

typedef enum {
    FLOW_SIEVE_BIT_EXTENT_ALIGN    = 5,
    FLOW_SIEVE_BIT_MEMORY_QUOTA    = 6,
    FLOW_SIEVE_BIT_PARALLEL        = 8,
    FLOW_SIEVE_BIT_SHARED_STATE    = 10,
    FLOW_SIEVE_BIT_READ_HEAVY      = 12,
    FLOW_SIEVE_BIT_UNORDERED       = 23,
    FLOW_SIEVE_BIT_COLUMNAR_MORPH  = 31,
    FLOW_SIEVE_BIT_ZERO_COPY_ABI   = 48,
    FLOW_SIEVE_BIT_BOUNDED_RANGE   = 49,
    FLOW_SIEVE_BIT_KAN_HOMOTOPY    = 50,
    FLOW_SIEVE_BIT_TRANSVERSALITY  = 51,
    FLOW_SIEVE_BIT_DETERMINISTIC   = 52,
    FLOW_SIEVE_BIT_QSBR_EPOCH_LEASE = 61,
    FLOW_SIEVE_BIT_LEAN_CERTIFIED  = 62
} FlowCubicalAxisBit;

#define FLOW_SIEVE_BASE_MASK 0x1FULL
#define FLOW_SIEVE_SECURITY_CANONICAL_MASK \
    ((1ULL << FLOW_SIEVE_BIT_ZERO_COPY_ABI) | \
     (1ULL << FLOW_SIEVE_BIT_BOUNDED_RANGE) | \
     (1ULL << FLOW_SIEVE_BIT_KAN_HOMOTOPY)  | \
     (1ULL << FLOW_SIEVE_BIT_TRANSVERSALITY)| \
     (1ULL << FLOW_SIEVE_BIT_DETERMINISTIC))

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
