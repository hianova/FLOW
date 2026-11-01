#ifndef FLOW_BRIDGE_H
#define FLOW_BRIDGE_H

#include "flow.h"
#include "bitspace.h"
#include "cubical_hott.h"
#include "flow_jet.h"
#include "reload.h"
#include "flow_hw_probe.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW HoTT x 2-Jet Mathematical-to-Code Grounding Interface
 * ============================================================================
 * Connects continuous differential geometry (2-Jet tangent/curvature bundles
 * J^2(M, N): value f(x), Jacobian df, Hessian d^2f, Lie derivative L_f V)
 * with discrete Cubical Homotopy Type Theory (64-bit Grothendieck Topos Sieve,
 * 1-cycle Kan Open-Box filling, and Univalence Dual-Mapped mmap transport).
 * ============================================================================
 */

/*
 * Execution status codes for flow_bridge_execute_step & flow_bridge_execute_hott_jet_step.
 */
typedef enum {
    FLOW_BRIDGE_SUCCESS = 0,
    FLOW_BRIDGE_ERR_NULL_PARAM = -1,
    FLOW_BRIDGE_ERR_QUANTIZE = -2,
    FLOW_BRIDGE_ERR_PRECONDITION = -3,
    FLOW_BRIDGE_ERR_LATENCY = -4,
    FLOW_BRIDGE_ERR_OPERATOR = -5,
    FLOW_BRIDGE_ERR_POSTCONDITION = -6,
    FLOW_BRIDGE_ERR_ENVELOPE = -7,
    FLOW_BRIDGE_ERR_DEQUANTIZE = -8,
    FLOW_BRIDGE_ERR_JET_SINGULAR = -9
} FlowBridgeStatusCode;

/*
 * Canonical 64-bit Grothendieck Topos Sieve Axes for 2-Jet Differential Invariants.
 */
typedef enum {
    FLOW_JET_SIEVE_FINITE = (1ULL << 0),                /* j^2 f(x) in R^3 (no NaN / Inf) */
    FLOW_JET_SIEVE_ZERO_ORDER_BOX = (1ULL << 1),        /* 0-Jet state v in [v_min, v_max] */
    FLOW_JET_SIEVE_FIRST_ORDER_LIPSCHITZ = (1ULL << 2), /* 1-Jet tangent |d1| <= max_lipschitz_d1 */
    FLOW_JET_SIEVE_SECOND_ORDER_CURVATURE =
        (1ULL << 3),                                   /* 2-Jet Hessian |d2| <= max_curvature_d2 */
    FLOW_JET_SIEVE_LYAPUNOV_DISSIPATIVE = (1ULL << 4), /* Lie derivative L_f V <= -gamma * V */
    FLOW_JET_SIEVE_TRANSVERSALITY = (1ULL << 5), /* Thom transversality |d1| >= floor near dM */
    FLOW_JET_SIEVE_CANONICAL_ALL = 0x3FULL
} FlowJetSieveAxis;

/*
 * Continuous 2-Jet Differential Manifold Envelope (J^2 E Subbundle Contract).
 */
typedef struct FlowJetManifoldEnvelope {
    double v_min;               /* 0-Jet lower bound */
    double v_max;               /* 0-Jet upper bound */
    double max_lipschitz_d1;    /* 1-Jet tangent / Jacobian Lipschitz bound |df/dx| <= L_max */
    double max_curvature_d2;    /* 2-Jet Hessian curvature bound |d^2f/dx^2| <= kappa_max */
    double lyapunov_decay_rate; /* Exponential dissipation rate gamma >= 0 */
    double
        transversality_floor; /* Minimum |df/dx| near boundary dM to prevent grazing singularity */
    uint64_t required_kan_sieve;  /* Required 64-bit Cubical HoTT Topos Sieve mask */
    uint64_t kan_boundary_filter; /* Protected homotopy axes for 1-cycle Kan Open-Box filling */
} FlowJetManifoldEnvelope;

#ifndef FLOW_FORMAL_CONTRACT_DEFINED
#define FLOW_FORMAL_CONTRACT_DEFINED

/*
 * 1. Formal Constraint Guard Contract ABI.
 * Holds formal Pre/Post and relational invariant checks synthesized from Lean 4.
 */
typedef struct FlowFormalContract {
    const char *contract_name;
    uint64_t schema_hash;
    int (*check_precondition)(const void *discrete_in, uint64_t *violation_tag_out);
    int (*check_postcondition)(const void *discrete_in, const void *discrete_out,
                               uint64_t *violation_tag_out);
    int (*check_relational_invariant)(const void *state_prev, const void *state_curr,
                                      uint64_t *violation_tag_out);
} FlowFormalContract;

/*
 * 2. Physical Transducer Interface (Continuous Physics <-> Discrete Lean Domain).
 * Handles bi-directional quantization between continuous raw sensor readings
 * and discrete verified integer types, and dequantization back to raw actuation.
 */
typedef struct FlowPhysicalTransducer {
    const char *transducer_name;
    size_t raw_obs_bytes;
    size_t discrete_in_bytes;
    size_t discrete_out_bytes;
    size_t raw_act_bytes;
    int (*quantize_observation)(const void *raw_obs, void *discrete_in_out);
    int (*dequantize_actuation)(const void *discrete_out, void *raw_act_out);
} FlowPhysicalTransducer;

/*
 * 3. Physical Envelope & Homotopy Fallback Interface.
 * Defines runtime execution constraints: cycle budget, 64-bit Topos ambient sieve,
 * and deterministic safe fallback operator to trigger on any contract or envelope failure.
 */
typedef struct FlowPhysicalEnvelope {
    uint64_t max_latency_cycles;
    uint64_t ambient_sieve_mask; /* 64-bit Topos Sieve defining theorem preconditions */
    int (*fallback_operator)(const void *discrete_in, void *safe_discrete_out);
} FlowPhysicalEnvelope;

#endif /* FLOW_FORMAL_CONTRACT_DEFINED */

/*
 * 4. Standardized HoTT x Jet Formal-Physical Bridge Descriptor.
 */
typedef struct FlowPhysicalBridge {
    FlowPhysicalTransducer transducer;
    FlowFormalContract contract;
    FlowPhysicalEnvelope envelope;
    int (*engineering_operator)(const void *in, void *out);
    FlowJetManifoldEnvelope jet_envelope;
    FlowReloadContext *univalence_ctx; /* Optional Univalence Dual-Mapped mmap / QSBR context */
} FlowPhysicalBridge;

/*
 * 5. O(1) 2-Jet to Cubical HoTT Topos Sieve Projection.
 * Evaluates a 2-Jet scalar (v, d1, d2) against a differential manifold envelope,
 * returning the 64-bit Topos Sieve mask and checking 1-cycle Kan Open-Box homotopy.
 */
uint64_t flow_bridge_project_jet2_sieve(FlowJetScalar2 jet_sample,
                                        const FlowJetManifoldEnvelope *jet_env,
                                        FlowKanStatus *kan_status_out, uint64_t *kan_mismatch_out);

/*
 * Projects an N-dimensional 2-Jet fiber bundle (intersection of Topos sieves across fibers).
 */
uint64_t flow_bridge_project_jet_bundle_sieve(const FlowJetScalar2 *fibers, size_t dim,
                                              const FlowJetManifoldEnvelope *jet_env,
                                              FlowKanStatus *kan_status_out,
                                              uint64_t *kan_mismatch_out);

/*
 * 6. Unified Formal-Physical Bridge Step Executor.
 */
int flow_bridge_execute_step(const FlowPhysicalTransducer *transducer,
                             const FlowFormalContract *contract,
                             const FlowPhysicalEnvelope *envelope,
                             int (*engineering_operator)(const void *in, void *out),
                             const void *raw_obs_in, void *raw_act_out,
                             uint64_t observed_condition_mask, uint64_t *boundary_tag_out);

/*
 * 7. Full HoTT x 2-Jet Grounded Step Executor.
 * Projects 2-Jet differential invariants into the 64-bit HoTT Topos Sieve,
 * verifies 1-cycle Kan Open-Box homotopy, and executes the formal-physical bridge
 * (with optional Univalence Dual-Mapped mmap QSBR dispatch).
 */
int flow_bridge_execute_hott_jet_step(const FlowPhysicalBridge *bridge,
                                      const FlowJetScalar2 *jet_fibers, size_t jet_dim,
                                      const void *raw_obs_in, void *raw_act_out,
                                      uint64_t ambient_sieve_bits, uint64_t *boundary_tag_out);

/*
 * String representation of a FlowBridgeStatusCode.
 */
const char *flow_bridge_status_name(FlowBridgeStatusCode code);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_BRIDGE_H */
