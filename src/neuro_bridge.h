#ifndef FLOW_NEURO_BRIDGE_H
#define FLOW_NEURO_BRIDGE_H

#include "flow.h"
#include "bitspace.h"
#include "smt.h"
#include "flow_smt_dsl.h"
#include "hardware_telemetry.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW Neuro-Bit Manifold Bridge (neuro_bridge.h)
 * ============================================================================
 * Bridges continuous high-dimensional semantic embeddings from Large Multimodal
 * Models (e.g. 512, 1024, 2048, 4096-D LLM/VLM embeddings) into FLOW's
 * rigid, deterministic 64-Bit BMF coordinates and polyhedral safety bounds in:
 *
 *     SUB-100 NANOSECONDS (< 100 ns)
 *
 * Division of Labor:
 * - LLM/VLM: Under-constrained, ambiguous semantic understanding ("拿快灑出來的拿鐵").
 * - FLOW Bridge: Structured Sparse Johnson-Lindenstrauss Projection into 64-bit BMF
 *   coordinates + 16-D .fvec continuous manifold + SMT-verified physical polyhedra.
 * ============================================================================
 */

#define FLOW_NEURO_MAX_INPUT_DIM 4096
#define FLOW_NEURO_BMF_BITS 64
#define FLOW_NEURO_FVEC_DIM 16
#define FLOW_NEURO_SPARSITY_BMF 4
#define FLOW_NEURO_SPARSITY_FVEC 8
#define FLOW_NEURO_MAX_CONSTRAINTS 8

typedef enum {
    FLOW_NEURO_INTENT_GENERIC = 0,
    FLOW_NEURO_INTENT_SMOOTH_FETCH_LATTE = 1,   /* "拿桌上快灑出來的拿鐵" */
    FLOW_NEURO_INTENT_AGILE_SPRINT = 2,         /* "敏捷衝刺避障" */
    FLOW_NEURO_INTENT_COLLABORATIVE_HOLD = 3,   /* "雙臂協同平穩搬運" */
    FLOW_NEURO_INTENT_EMERGENCY_PROTECT = 4     /* "防跌倒緊急制動" */
} FlowNeuroIntentType;

typedef struct {
    char name[32];
    double normal[FLOW_NEURO_FVEC_DIM];
    double lower_bound;
    double upper_bound;
    bool is_hard_constraint;
} FlowNeuroPhysicalBound;

typedef struct {
    uint64_t bmf_coordinates;                    /* 64-bit discrete BMF state */
    double fvec_features[FLOW_NEURO_FVEC_DIM];   /* 16-D normalized continuous embedding */
    FlowNeuroIntentType classified_intent;       /* Decoded semantic intent */
    char intent_description[128];
    FlowNeuroPhysicalBound bounds[FLOW_NEURO_MAX_CONSTRAINTS];
    size_t bound_count;
    uint64_t projection_cycles;                  /* RDTSC / CNTVCT_EL0 elapsed cycles */
    double projection_nanoseconds;               /* Elapsed time in nanoseconds (< 100 ns target) */
} FlowNeuroProjectionResult;

typedef struct {
    size_t input_dimension;                      /* e.g. 512, 1024, 2048, 4096 */
    uint16_t bmf_indices[FLOW_NEURO_BMF_BITS][FLOW_NEURO_SPARSITY_BMF];
    int8_t   bmf_signs[FLOW_NEURO_BMF_BITS][FLOW_NEURO_SPARSITY_BMF];
    float    bmf_thresholds[FLOW_NEURO_BMF_BITS];

    uint16_t fvec_indices[FLOW_NEURO_FVEC_DIM][FLOW_NEURO_SPARSITY_FVEC];
    int8_t   fvec_signs[FLOW_NEURO_FVEC_DIM][FLOW_NEURO_SPARSITY_FVEC];

    uint64_t total_projections;
    double cumulative_latency_ns;
} FlowNeuroBridge;

/* Initialize Neuro-Bit Manifold Bridge for a given embedding dimension */
int flow_neuro_bridge_init(FlowNeuroBridge *bridge, size_t input_dim, uint32_t seed);

/*
 * Ultra-Fast Projection:
 * Projects a high-dimensional continuous embedding into 64-bit BMF coordinates,
 * 16-D .fvec features, and synthesized polyhedral bounds.
 * Guarantees execution latency < 100 ns on modern hardware.
 */
int flow_neuro_bridge_project(FlowNeuroBridge *bridge,
                              const float *input_embedding,
                              size_t embedding_len,
                              FlowNeuroIntentType intent_hint,
                              FlowNeuroProjectionResult *result_out);

/*
 * SMT Formal Verification of Synthesized Physical Bounds:
 * Proves:
 * 1. Feasibility: lower_bound <= upper_bound
 * 2. Motor Safety: torque/force <= physical hardware limit
 * 3. Anti-Spill Fluid Dynamics: tilt angle <= spill threshold
 * 4. Determinism: projection latency < 1000 ns bounded deadline
 */
FlowSMTResult flow_neuro_bridge_verify_smt(const FlowNeuroProjectionResult *result,
                                           FlowSMTProofAttestation *proof_out);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_NEURO_BRIDGE_H */
