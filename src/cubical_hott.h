#ifndef FLOW_CUBICAL_HOTT_H
#define FLOW_CUBICAL_HOTT_H

#include "flow.h"
#include "plugin.h"
#include "bitspace.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW Discrete Cubical Homotopy Type Theory (HoTT) & Grothendieck Topos Engine
 * ============================================================================
 * Implements constructive Homotopy Type Theory on discrete cubical sets:
 * 1. Abstract Interval I = {0, 1}: De Morgan algebra with meet (AND), join (OR), NOT (~).
 * 2. 64-Bit Boundary Basis Sieve: 64 morphisms/dimensions in a single CPU register.
 * 3. Kan Condition & Open-Box Filling: Mismatch = (Mask_p ^ Mask_q) & BoundaryFilter == 0.
 * 4. Subobject Classifier Omega = 2 = {0, 1}: Hom(X, Omega) ~= Sub(X).
 * 5. Intensional Global Type Universe U_0 & Univalence (A ~= B => A = U_0 B).
 * ============================================================================
 */

#ifndef FLOW_BMF_1BIT_CANVAS_DEFINED
#define FLOW_BMF_1BIT_CANVAS_DEFINED
typedef struct __attribute__((aligned(64))) {
    uint32_t subspace_id;
    int32_t is_adjudicated_sound;
    uint64_t switchboard_bits, invariant_mask, malleable_mask, dynamic_bias;
    double energy;
    uint8_t _cacheline_pad[16];
} FlowBmf1BitCanvas;
_Static_assert(sizeof(FlowBmf1BitCanvas) == 64, "FlowBmf1BitCanvas must be 64 bytes");
#endif

/* De Morgan Abstract Interval I = {0, 1} */
typedef uint8_t FlowDeMorganInterval;
#define FLOW_DEMORGAN_ZERO 0
#define FLOW_DEMORGAN_ONE  1
#define FLOW_CUBICAL_MAX_DIM 64

static inline FlowDeMorganInterval flow_demorgan_not(FlowDeMorganInterval i) { return (FlowDeMorganInterval)(1 - (i & 1)); }
static inline FlowDeMorganInterval flow_demorgan_and(FlowDeMorganInterval a, FlowDeMorganInterval b) { return (FlowDeMorganInterval)((a & 1) & (b & 1)); }
static inline FlowDeMorganInterval flow_demorgan_or(FlowDeMorganInterval a, FlowDeMorganInterval b) { return (FlowDeMorganInterval)((a & 1) | (b & 1)); }

static inline uint64_t flow_demorgan_word_not(uint64_t w) { return ~w; }
static inline uint64_t flow_demorgan_word_and(uint64_t w1, uint64_t w2) { return w1 & w2; }
static inline uint64_t flow_demorgan_word_or(uint64_t w1, uint64_t w2) { return w1 | w2; }

/* Cubical Sieve & Covering */
typedef struct {
    uint64_t arrows;
    uint32_t dimension;
    uint8_t is_closed, reserved[3];
} FlowCubicalSieve;

uint64_t flow_cubical_face_proj(uint64_t sieve, uint32_t dim_k, uint8_t endpoint);
uint64_t flow_cubical_degeneracy(uint64_t sieve, uint32_t dim_k);
static inline bool flow_cubical_sieve_is_covering(uint64_t sieve, uint64_t target_basis) {
    return (sieve & target_basis) == target_basis;
}

/* Kan Open-Box Condition & Homotopy Equivalence */
typedef enum {
    FLOW_KAN_FILLED_HOMOTOPIC       = 0,
    FLOW_KAN_OBSTRUCTED_SINGULARITY = 1
} FlowKanStatus;

typedef struct {
    uint64_t mask_p, mask_q, boundary_filter, mismatch_mask;
    FlowKanStatus status;
} FlowKanBox;

FlowKanStatus flow_kan_check_homotopy(uint64_t mask_p, uint64_t mask_q, uint64_t boundary_filter, uint64_t *mismatch_out);

/* Grothendieck Topos Subobject Classifier Omega = {0, 1} */
uint8_t flow_topos_classify_subobject(uint64_t subobject_mask, uint64_t ambient_sieve);

/* Cubical Homotopy Type Sieve */
typedef struct {
    uint64_t mask;
    uint64_t boundary_filter;
} FlowCubicalTypeSieve;

FlowCubicalTypeSieve flow_cubical_sieve_from_ir(const SemanticIR *ir);
FlowCubicalTypeSieve flow_cubical_sieve_from_component(const Component *comp);
int flow_cubical_match_types(FlowCubicalTypeSieve intent, FlowCubicalTypeSieve candidate, uint64_t *mismatch_out);

/* Discrete Polytope Boundaries & Geometric Handover */
#define FLOW_POLY_MAX_DIM 4
#define FLOW_POLY_MAX_CONSTRAINTS 16

typedef struct {
    int64_t coeffs[FLOW_POLY_MAX_DIM];
    int64_t constant;
} FlowAffineConstraint;

typedef struct {
    double center[FLOW_POLY_MAX_DIM], hessian[FLOW_POLY_MAX_DIM][FLOW_POLY_MAX_DIM], gradient[FLOW_POLY_MAX_DIM], bound, semi_axes[FLOW_POLY_MAX_DIM];
    bool is_active;
} FlowQuadraticConstraint;

typedef struct {
    size_t dimension, constraint_count;
    FlowAffineConstraint constraints[FLOW_POLY_MAX_CONSTRAINTS];
    int64_t lower_bounds[FLOW_POLY_MAX_DIM], upper_bounds[FLOW_POLY_MAX_DIM];
    size_t quad_constraint_count;
    FlowQuadraticConstraint quad_constraints[FLOW_POLY_MAX_CONSTRAINTS];
} FlowPolyhedron;

typedef struct {
    size_t optimal_tile_size, optimal_simd_width;
    int64_t total_iterations;
    bool is_parallelizable, is_bounded;
    double quadratic_recovered_volume;
    bool has_quadratic_curvature;
} FlowPolyhedralSchedule;

int flow_polyhedral_init(FlowPolyhedron *poly, size_t dimension);
int flow_polyhedral_add_constraint(FlowPolyhedron *poly, const int64_t *coeffs, int64_t constant);
int flow_polyhedral_add_quadratic_constraint(FlowPolyhedron *poly, const double center[], const double hessian[FLOW_POLY_MAX_DIM][FLOW_POLY_MAX_DIM], const double gradient[], double bound);
int flow_polyhedral_set_box_bounds(FlowPolyhedron *poly, size_t dim_idx, int64_t lower, int64_t upper);
int flow_polyhedral_solve_schedule(const FlowPolyhedron *poly, size_t cache_line_bytes, size_t vector_register_bytes, FlowPolyhedralSchedule *schedule_out);
FlowSMTResult flow_polyhedral_verify_smt(const FlowPolyhedron *poly, const FlowPolyhedralSchedule *sched, FlowSMTProofAttestation *proof_out);
int flow_cubical_handover_polytope(uint64_t mask_p, uint64_t mask_q, uint64_t boundary_filter, FlowPolyhedron *poly, int64_t offset_out[FLOW_POLY_MAX_DIM]);

#define FLOW_POLYTOPE_MAX_CONSTRAINTS 32
#define FLOW_POLYTOPE_MAX_DIMS 16

typedef enum { FLOW_CONSTRAINT_LEQ = 0, FLOW_CONSTRAINT_GEQ = 1, FLOW_CONSTRAINT_EQ = 2, FLOW_CONSTRAINT_INTERVAL = 3 } FlowConstraintOp;
typedef struct { double coefficients[FLOW_POLYTOPE_MAX_DIMS]; FlowConstraintOp op; double rhs_min, rhs_max; char symbolic_tag[64]; } FlowLinearConstraint;
typedef struct {
    FlowLinearConstraint constraints[FLOW_POLYTOPE_MAX_CONSTRAINTS];
    size_t constraint_count, dimension_count;
    double lower_bounds[FLOW_POLYTOPE_MAX_DIMS], upper_bounds[FLOW_POLYTOPE_MAX_DIMS];
} FlowPolyhedronSystem;

void flow_polyhedron_init(FlowPolyhedronSystem *poly, size_t dim_count);
int flow_polyhedron_add_box_bounds(FlowPolyhedronSystem *poly, size_t dim_idx, double min_val, double max_val, const char *tag);
int flow_polyhedron_add_inequality(FlowPolyhedronSystem *poly, const double *coeffs, FlowConstraintOp op, double bound, const char *tag);
int flow_polyhedron_from_ir(const SemanticIR *ir, const Component *comp, const FlowPlanDimensionSet *dims, FlowPolyhedronSystem *poly);
uint64_t flow_polyhedron_project_mask(const FlowPolyhedronSystem *poly, const FlowPlanDimensionSet *dims, uint32_t total_bits);
int flow_polyhedron_from_affine(const FlowPolyhedron *affine_poly, FlowPolyhedronSystem *poly_out);

#define FLOW_HARDWIRED_MAX_PLANES 64
#define FLOW_HARDWIRED_MAX_DIM    8
typedef struct __attribute__((aligned(64))) FlowHardwiredPolyhedralTemplate {
    double coeffs[FLOW_HARDWIRED_MAX_PLANES][FLOW_HARDWIRED_MAX_DIM]; _Atomic double b_constants[FLOW_HARDWIRED_MAX_PLANES]; _Atomic uint64_t active_mask;
    _Atomic double translation[FLOW_HARDWIRED_MAX_DIM]; _Atomic double scale[FLOW_HARDWIRED_MAX_DIM]; size_t dimension, total_planes; _Atomic uint64_t generation;
    uint64_t total_evaluations, total_hot_updates, icache_flushes_avoided;
} FlowHardwiredPolyhedralTemplate;

typedef struct { bool is_inside; uint64_t violated_mask; double max_violation, projected_x[FLOW_HARDWIRED_MAX_DIM]; } FlowHardwiredEvalResult;
int flow_hardwired_template_init(FlowHardwiredPolyhedralTemplate *tpl, size_t dimension);
int flow_hardwired_template_from_polyhedron(FlowHardwiredPolyhedralTemplate *tpl, const FlowPolyhedron *poly);
int flow_hardwired_eval(FlowHardwiredPolyhedralTemplate *tpl, const double x[FLOW_HARDWIRED_MAX_DIM], FlowHardwiredEvalResult *res_out);

/* Verification Attestation */
FlowSMTResult flow_cubical_verify_smt(const FlowCubicalSieve *sieve, uint64_t mask_p, uint64_t mask_q, uint64_t boundary_filter, FlowSMTProofAttestation *proof_out);

/* Geometric Section & Fiber Bundle Connection */
#define FLOW_AXIOM_DIM 64
#define FLOW_AXIOM_LATTICE_DIM 4

#ifndef FLOW_THERMAL_STATE_DEFINED
#define FLOW_THERMAL_STATE_DEFINED
typedef struct FlowThermalState {
    double temp_c, temp_ambient_c, dtemp_dt, r_thermal, c_thermal, temp_throttle_c, temp_target_c, active_power_w;
    uint64_t throttle_events;
    uint8_t is_throttled, reserved[7];
} FlowThermalState;
#endif

typedef struct FlowUnifiedSection FlowUnifiedSection;
struct __attribute__((aligned(64))) FlowUnifiedSection {
    double q[FLOW_AXIOM_DIM], p[FLOW_AXIOM_DIM], a[FLOW_AXIOM_DIM];
    double s, transversality_margin;
    int64_t lattice_idx[FLOW_AXIOM_LATTICE_DIM];
    uint32_t optimal_tile_size, optimal_simd_width;
    uint64_t total_lattice_points, bmf_subspace_mask;
    FlowBmf1BitCanvas bmf_canvas;
    double curvature_spectrum[FLOW_AXIOM_DIM];
    FlowThermalState thermal;
    FlowSMTProofAttestation proof;
    uint64_t cubical_sieve;
    uint8_t kan_homotopy_status, omega_classifier_bit;
    uint32_t crc32;
    uint8_t is_transversal, is_compact, reserved[16];
};

int flow_axiom_init(FlowUnifiedSection *sec, const char *intent);
FlowUnifiedSection *flow_axiom_create(const char *intent);
void flow_axiom_destroy(FlowUnifiedSection *sec);
int flow_cubical_holonomy_twist(uint64_t loop_sieve, FlowUnifiedSection *sec);
int flow_axiom_eval_cubical_homotopy(FlowUnifiedSection *sec, uint64_t target_path, uint64_t boundary_filter);

/* 64-Bit Microarchitecture Axis Dictionary & Semantic Intent */
typedef enum {
    FLOW_SIEVE_BIT_EXTENT_ALIGN     = 5,
    FLOW_SIEVE_BIT_MEMORY_QUOTA     = 6,
    FLOW_SIEVE_BIT_PARALLEL         = 8,
    FLOW_SIEVE_BIT_SHARED_STATE     = 10,
    FLOW_SIEVE_BIT_READ_HEAVY       = 12,
    FLOW_SIEVE_BIT_UNORDERED        = 23,
    FLOW_SIEVE_BIT_COLUMNAR_MORPH   = 31,
    FLOW_SIEVE_BIT_ZERO_COPY_ABI    = 48,
    FLOW_SIEVE_BIT_BOUNDED_RANGE    = 49,
    FLOW_SIEVE_BIT_KAN_HOMOTOPY     = 50,
    FLOW_SIEVE_BIT_TRANSVERSALITY   = 51,
    FLOW_SIEVE_BIT_DETERMINISTIC    = 52,
    FLOW_SIEVE_BIT_QSBR_EPOCH_LEASE = 61,
    FLOW_SIEVE_BIT_LEAN_CERTIFIED   = 62
} FlowCubicalAxisBit;

#define FLOW_SIEVE_BASE_MASK 0x1FULL
#define FLOW_SIEVE_SECURITY_CANONICAL_MASK \
    ((1ULL << FLOW_SIEVE_BIT_ZERO_COPY_ABI) | \
     (1ULL << FLOW_SIEVE_BIT_BOUNDED_RANGE) | \
     (1ULL << FLOW_SIEVE_BIT_KAN_HOMOTOPY)  | \
     (1ULL << FLOW_SIEVE_BIT_TRANSVERSALITY)| \
     (1ULL << FLOW_SIEVE_BIT_DETERMINISTIC))

typedef enum {
    FLOW_CUBICAL_SUBSPACE_MEMORY      = 0,
    FLOW_CUBICAL_SUBSPACE_CONCURRENCY = 1,
    FLOW_CUBICAL_SUBSPACE_MICROARCH   = 2,
    FLOW_CUBICAL_SUBSPACE_LAYOUT      = 3,
    FLOW_CUBICAL_SUBSPACE_PHYSICAL    = 4,
    FLOW_CUBICAL_SUBSPACE_SECURITY    = 5
} FlowCubicalSubspaceType;

typedef struct {
    uint32_t axis_index;
    FlowCubicalSubspaceType subspace;
    const char *axis_name, *policy_contract, *module_id, *subspace_name, *canonical_explanation;
} FlowCubicalAxisInfo;

typedef struct {
    uint32_t flipped_axis;
    FlowCubicalSubspaceType subspace;
    const FlowCubicalAxisInfo *axis_info;
    uint64_t v_pre, v_post;
    int is_kan_homotopic;
    char explanation[512], pre_topology[64], post_topology[64];
} FlowCubicalTransitionReport;

const FlowCubicalAxisInfo *flow_cubical_get_axis_info(uint32_t axis_idx);
int flow_cubical_decode_transition(uint64_t v_pre, uint64_t v_post, uint32_t explicit_axis, FlowCubicalTransitionReport *report_out);
uint64_t flow_cubical_project_text_intent(const char *text);
uint64_t flow_cubical_get_module_mask(const char *module_id);

/*
 * ============================================================================
 * Intensional Global Type Universe U_0 & Univalence Customs (A ~= B => A = U_0 B)
 * ============================================================================
 */
#define FLOW_HOTT_UNIVERSE_MAX_TYPES 128
#define FLOW_SIMD_SOA_LANES 64U

typedef enum {
    FLOW_HOTT_LANG_LEAN     = 0,
    FLOW_HOTT_LANG_C17      = 1
} FlowHoTTLangOrigin;

typedef struct {
    char name[64];
    FlowHoTTLangOrigin origin;
    size_t field_count;
    size_t byte_size;
    size_t align_bytes;
    uint32_t fx_scale_bits;
    uint64_t sieve_fingerprint;
} FlowHoTTTypeEntry;

typedef struct {
    FlowHoTTTypeEntry entries[FLOW_HOTT_UNIVERSE_MAX_TYPES];
    size_t count;
    size_t distinct_type_count;
    size_t univalence_passed_count;
    size_t homotopy_broken_count;
    char broken_diagnostic[256];
} FlowHoTTUniverse;

const char *flow_hott_lang_origin_name(FlowHoTTLangOrigin origin);
FlowHoTTUniverse *flow_hott_universe_global(void);
void flow_hott_universe_reset(FlowHoTTUniverse *u);
int flow_hott_register_type(FlowHoTTUniverse *u, const char *name, FlowHoTTLangOrigin origin,
                            size_t field_count, size_t byte_size, size_t align_bytes,
                            uint32_t fx_scale_bits, uint64_t sieve_fingerprint);
FlowKanStatus flow_hott_verify_univalence(FlowHoTTUniverse *u, const char *type_name, uint64_t *path_hash_out);
int flow_hott_verify_universe_all(FlowHoTTUniverse *u);
int flow_hott_register_soa_equivalence(FlowHoTTUniverse *u, const char *aos_name, const char *soa_name,
                                       size_t field_count, size_t aos_byte_size, size_t simd_lanes,
                                       uint32_t fx_scale_bits, uint64_t base_sieve);
FlowKanStatus flow_hott_verify_soa_univalence(FlowHoTTUniverse *u, const char *aos_name, const char *soa_name,
                                              size_t simd_lanes, uint64_t *ua_path_hash_out);

/*
 * ============================================================================
 * Differential Cohesive HoTT: Homotopy Fiber (fib_{ev}(1)) & Discrete k-Jet (J^4 E)
 * ============================================================================
 */
typedef enum {
    FLOW_HLEVEL_CONTRACTIBLE      = -2, /* isContr(fib_{ev}(1)): Unique SAT, |pi_0| = 1, pi_1 = 0 */
    FLOW_HLEVEL_EMPTY_UNSAT       = -1, /* fib_{ev}(1) -> 0 (Empty Type / UNSAT) */
    FLOW_HLEVEL_SET_DISCRETE      =  0, /* 0-Type (Set): |pi_0| > 1, pi_k = 0 (k >= 1) */
    FLOW_HLEVEL_GROUPOID_SYMMETRY =  1  /* 1-Type (Groupoid A // G): non-trivial symmetry loop pi_1 != 0 */
} FlowHomotopyLevel;

typedef struct {
    FlowHomotopyLevel h_level;
    uint32_t pi0_components;            /* |pi_0(fib_f(d))|: Model count (#SAT / connected components) */
    uint32_t pi1_symmetry_generators;   /* Rank of fundamental group pi_1(fib_f(d)) from symmetry group G */
    uint64_t canonical_witness_sieve;   /* Selected base witness a_0 in A */
    uint64_t unconstrained_axes_mask;   /* Free axes causing |pi_0| > 1 solver oscillation */
    uint64_t symmetry_orbit_mask;       /* Free axes participating in G-symmetry involution */
    uint64_t recommended_pinning_mask;  /* Constraint mask required to collapse fib_f(d) to isContr */
    int is_contractible_after_quotient; /* 1 if quotienting A // G and pinning collapses |pi_0| -> 1 */
    char diagnostic_summary[256];
} FlowHomotopyFiberReport;

#define FLOW_JET_BUNDLE_MAX_SITES 64U

typedef struct {
    double j0_val;              /* Base value s(x) in Fiber E */
    double j1_grad;             /* 1st-order discrete finite difference (drift) */
    double j2_curv;             /* 2nd-order discrete Laplacian (diffusion/curvature) */
    double j3_skew;             /* 3rd-order discrete skewness tensor truncation (J^3 E) */
    double j4_kurt;             /* 4th-order discrete kurtosis tensor truncation (J^4 E) */
    uint8_t in_rk_submanifold;  /* 1 if local 4-jet j^4 s(x) lies inside constraint subbundle R_k */
} FlowDiscreteJetFiber;

typedef struct {
    size_t site_count;
    FlowDiscreteJetFiber jets[FLOW_JET_BUNDLE_MAX_SITES];
    double global_j1_mean;
    double global_j2_variance;
    double global_j3_skewness;
    double global_j4_kurtosis;
    double rk_bounds_min[5];    /* Lower bounds on J^0..J^4 defining subbundle R_k subset J^4 E */
    double rk_bounds_max[5];    /* Upper bounds on J^0..J^4 defining subbundle R_k subset J^4 E */
    uint64_t holonomic_mask;    /* Bit i = 1 iff j^4 s(x_i) in R_k */
    int is_holonomic_section;   /* 1 iff forall x in M, j^4 s(x) in R_k and Cartan-Kahler integrable */
    double cartan_kahler_defect;/* Max discrete mixed-difference commutator defect */
} FlowDiscreteJetBundle;

int flow_hott_analyze_homotopy_fiber(uint64_t target_mask, uint64_t feasible_sieve,
                                     uint64_t pinned_axes_mask, uint64_t symmetry_orbit_mask,
                                     FlowHomotopyFiberReport *report_out);
int flow_hott_collapse_fiber_contractible(const FlowHomotopyFiberReport *fiber,
                                          uint64_t *contracted_witness_out,
                                          uint64_t *augmented_pinned_mask_out);
int flow_jet_bundle_prolong_bmf(const double *section_values, size_t count,
                                const double rk_min[5], const double rk_max[5],
                                FlowDiscreteJetBundle *bundle_out);
int flow_hott_emit_commutative_diagram(FILE *out, const FlowDiscreteJetBundle *bundle,
                                       const FlowHomotopyFiberReport *fiber,
                                       const char *module_name);
int flow_hott_emit_lean_fiber_jet_spec(FILE *out, const FlowDiscreteJetBundle *bundle,
                                       const FlowHomotopyFiberReport *fiber,
                                       const char *namespace_name);

/*
 * ============================================================================
 * HIT Singularity Contraction (SpaceWithException) & Zero-Cost Boundary (dM)
 * ============================================================================
 * 1. Why Univalence Cannot Equate Normal and Exception States:
 *    - Interior states (M^\circ) satisfy system invariants: isContr(fib(spec)) (h-level -2).
 *    - Exception states rupture invariants into a 1-Type Groupoid (pi_1 != 0, h-level 1).
 *    - Because (-2)-Type !~= 1-Type, Univalence (A ~= B) ~= (A = B) is inapplicable.
 * 2. Higher Inductive Type (HIT) Point Contraction (`SpaceWithException`):
 *    - Quotients the entire exception submanifold U_exception subset M into a single
 *      0-dimensional boundary sink dM via `collapse : ExceptionZone x -> ExceptionZone y -> point x = point y`.
 * 3. Zero-Cost Hardware Pullback (`FlowResult` / IEEE-754 Quiet NaN Payload):
 *    - Encodes (fn_id, guard_idx, violation_kind, cubical_axis, user_code) into the
 *      51-bit mantissa payload of an IEEE-754 Quiet NaN (`0x7FF8400000000000ULL`),
 *      propagating branchlessly through hardware FPU pipelines along dM.
 * ============================================================================
 */
#define FLOW_BOUNDARY_QNAN_MAGIC        UINT64_C(0x7FF8400000000000)
#define FLOW_BOUNDARY_QNAN_PREFIX_MASK  UINT64_C(0x7FFCF00000000000)
#define FLOW_BOUNDARY_PAYLOAD_MASK      UINT64_C(0x00000FFFFFFFFFFF)

#ifndef FLOW_BOUNDARY_VIOLATION_KIND_DEFINED
#define FLOW_BOUNDARY_VIOLATION_KIND_DEFINED
typedef enum {
    FLOW_BOUNDARY_OK                 = 0, /* Interior point in M^\circ */
    FLOW_BOUNDARY_PRECOND            = 1, /* Precondition guard failed */
    FLOW_BOUNDARY_POSTCOND           = 2, /* Postcondition guard failed */
    FLOW_BOUNDARY_THM_IMPLIES        = 3, /* Theorem implication hyp => conc ruptured */
    FLOW_BOUNDARY_DOMAIN_SINGULARITY = 4, /* Non-finite / division-by-zero / NaN domain */
    FLOW_BOUNDARY_HIT_COLLAPSE       = 5, /* SpaceWithException HIT point collapse */
    FLOW_BOUNDARY_EXPLICIT_NONE      = 6  /* Explicit Option.none / FlowResult.boundary */
} FlowBoundaryViolationKind;
#endif

typedef struct {
    FlowHomotopyLevel normal_h_level;      /* (-2)-Type isContr in interior M^\circ */
    FlowHomotopyLevel ruptured_h_level;    /* 1-Type Groupoid when constraints rupture */
    int univalence_applicable;             /* Always 0: (-2)-Type !~= 1-Type */
    uint64_t ambient_sieve;                /* Ambient manifold M sieve */
    uint64_t exception_zone_mask;          /* Submanifold U_exception subset M */
    uint64_t interior_sieve;               /* M^\circ = M \ U_exception */
    uint32_t ambient_dof;                  /* popcount(ambient_sieve) */
    uint32_t exception_zone_dof;           /* popcount(exception_zone_mask) */
    uint32_t quotient_interior_dof;        /* popcount(interior_sieve) */
    uint32_t collapsed_boundary_points;    /* 1: all x,y in ExceptionZone collapsed to dM */
    uint64_t canonical_boundary_tag;       /* Canonical 0D sink tag on dM */
    char summary[256];
} FlowHITExceptionQuotient;

typedef struct {
    uint64_t value;
    uint64_t boundary_tag;
} FlowResultU64;

typedef struct {
    int is_on_boundary;
    uint64_t raw_bits;
    uint16_t fn_id;
    uint8_t guard_idx;
    FlowBoundaryViolationKind violation_kind;
    uint8_t cubical_axis;
    uint16_t user_code;
    const char *violation_kind_name;
    char fn_name[64];
    char theorem_name[64];
    char guard_expr[256];
    const FlowCubicalAxisInfo *axis_info;
    char diagnostic[512];
} FlowBoundaryCertificateReport;

#define FLOW_HOTT_BOUNDARY_HELPERS_C17 \
static inline __attribute__((unused)) uint64_t flowc_make_boundary_tag(uint16_t fn_id, uint8_t guard_idx, uint8_t kind, uint8_t axis, uint16_t code) { \
    uint64_t k4 = (uint64_t)((kind == 0U ? 1U : kind) & 0x0FU); \
    return FLOW_BOUNDARY_QNAN_MAGIC | \
           (((uint64_t)(fn_id & 0x0FFFU)) << 32U) | \
           (((uint64_t)guard_idx) << 24U) | \
           (k4 << 20U) | \
           (((uint64_t)(axis & 0x3FU)) << 12U) | \
           ((uint64_t)(code & 0x0FFFU)); \
} \
static inline __attribute__((unused)) bool flowc_is_boundary_tag(uint64_t raw_bits) { \
    return (raw_bits & FLOW_BOUNDARY_QNAN_PREFIX_MASK) == FLOW_BOUNDARY_QNAN_MAGIC; \
} \
static inline __attribute__((unused)) double flowc_make_boundary_nan(uint16_t fn_id, uint8_t guard_idx, uint8_t kind, uint8_t axis, uint16_t code) { \
    uint64_t bits = flowc_make_boundary_tag(fn_id, guard_idx, kind, axis, code); \
    double out = 0.0; \
    __builtin_memcpy(&out, &bits, sizeof(out)); \
    return out; \
} \
static inline __attribute__((unused)) bool flowc_is_boundary_nan(double val) { \
    uint64_t bits = 0ULL; \
    __builtin_memcpy(&bits, &val, sizeof(bits)); \
    if (flowc_is_boundary_tag(bits)) return true; \
    return !__builtin_isfinite(val); \
} \
static inline __attribute__((unused)) uint64_t flowc_extract_boundary_tag(double val) { \
    uint64_t bits = 0ULL; \
    __builtin_memcpy(&bits, &val, sizeof(bits)); \
    if (flowc_is_boundary_tag(bits)) return bits; \
    if (!__builtin_isfinite(val)) return flowc_make_boundary_tag(0U, 0U, (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY, 49U, 0U); \
    return 0ULL; \
} \
static inline __attribute__((unused)) double flowc_stamp_boundary_fn_id(double val, uint16_t fn_id) { \
    uint64_t bits = 0ULL; \
    __builtin_memcpy(&bits, &val, sizeof(bits)); \
    if (flowc_is_boundary_tag(bits)) { \
        if (((bits >> 32U) & 0x0FFFULL) == 0x0FFFULL) \
            bits = (bits & ~(0x0FFFULL << 32U)) | (((uint64_t)(fn_id & 0x0FFFU)) << 32U); \
    } else if (!__builtin_isfinite(val)) { \
        bits = flowc_make_boundary_tag(fn_id, 0U, (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY, 49U, 0U); \
    } \
    double out = 0.0; \
    __builtin_memcpy(&out, &bits, sizeof(out)); \
    return out; \
} \
static inline __attribute__((unused)) uint16_t flowc_boundary_code(double val) { \
    return (uint16_t)(flowc_extract_boundary_tag(val) & 0x0FFFULL); \
}

#ifndef FLOW_LEAN_BOUNDARY_HELPERS_DEFINED
#define FLOW_LEAN_BOUNDARY_HELPERS_DEFINED
FLOW_HOTT_BOUNDARY_HELPERS_C17
#endif

static inline uint64_t flow_hott_make_boundary_tag(uint16_t fn_id, uint8_t guard_idx,
                                                   uint8_t violation_kind, uint8_t cubical_axis,
                                                   uint16_t user_code) {
    return flowc_make_boundary_tag(fn_id, guard_idx, violation_kind, cubical_axis, user_code);
}

static inline bool flow_hott_is_boundary_tag(uint64_t raw_bits) {
    return flowc_is_boundary_tag(raw_bits);
}

static inline double flow_result_float_interior(double val) {
    return val;
}

static inline double flow_result_float_from_tag(uint64_t tag) {
    uint64_t bits = flowc_is_boundary_tag(tag)
        ? tag
        : flowc_make_boundary_tag(0U, 0U, (uint8_t)FLOW_BOUNDARY_EXPLICIT_NONE,
                                  (uint8_t)FLOW_SIEVE_BIT_LEAN_CERTIFIED, (uint16_t)(tag & 0x0FFFU));
    double out = 0.0;
    __builtin_memcpy(&out, &bits, sizeof(out));
    return out;
}

static inline double flow_result_float_boundary(uint16_t fn_id, uint8_t guard_idx,
                                                uint8_t violation_kind, uint8_t cubical_axis,
                                                uint16_t user_code) {
    return flowc_make_boundary_nan(fn_id, guard_idx, violation_kind, cubical_axis, user_code);
}

static inline bool flow_result_float_is_boundary(double val) {
    return flowc_is_boundary_nan(val);
}

static inline uint64_t flow_result_float_extract_tag(double val) {
    return flowc_extract_boundary_tag(val);
}

static inline FlowResultU64 flow_result_u64_interior(uint64_t val) {
    FlowResultU64 r = { val, 0ULL };
    return r;
}

static inline FlowResultU64 flow_result_u64_boundary(uint16_t fn_id, uint8_t guard_idx,
                                                     uint8_t violation_kind, uint8_t cubical_axis,
                                                     uint16_t user_code) {
    FlowResultU64 r = { 0ULL, flow_hott_make_boundary_tag(fn_id, guard_idx, violation_kind, cubical_axis, user_code) };
    return r;
}

static inline bool flow_result_u64_is_boundary(FlowResultU64 r) {
    return r.boundary_tag != 0ULL;
}

const char *flow_hott_boundary_kind_name(FlowBoundaryViolationKind kind);
int flow_hott_quotient_exception_zone(uint64_t ambient_sieve, uint64_t exception_zone_mask,
                                      uint16_t fn_id, FlowHITExceptionQuotient *quotient_out);
int flow_hott_decode_boundary_exception(uint64_t raw_bits, const SemanticIR *ir,
                                        FlowBoundaryCertificateReport *report_out);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_CUBICAL_HOTT_H */
