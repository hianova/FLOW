#ifndef FLOW_TOKEN_RING_H
#define FLOW_TOKEN_RING_H

#include "flow.h"
#include "bitspace.h"
#include "bitmanifold.h"
#include "search.h"
#include "smt.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW BMF Token Ring Architecture & Discrete Attention State Machine
 * ============================================================================
 *
 * Replaces imperative procedural compilation (absorb -> anneal -> morph)
 * with a pure topological state machine.
 *
 * Discrete BMF Attention Operator:
 *     Canvas_{t+1} = \Phi(Canvas_t \otimes Mask_{Attn(t)})
 *
 * Each token on the circulating ring executes its stage, generates an Attention
 * Mask, projects onto the multi-tier BitSpace canvas in O(1) bitwise operations,
 * and passes the updated constraint canvas to the next token on the ring until
 * convergence to a Lyapunov Attractor Fixed Point (Delta E -> 0, SMT UNSAT closure)
 * or detection of an UNSAT constraint conflict.
 * ============================================================================
 */

#define FLOW_TOKEN_RING_MAX_TOKENS 16
#define FLOW_TOKEN_RING_DEFAULT_MAX_CYCLES 32

typedef enum {
    FLOW_TOKEN_STAGE_INGEST = 0,     /* Parse intent & establish initial manifold */
    FLOW_TOKEN_STAGE_POLYTOPE = 1,   /* Polyhedral inequality projection P = {Ax <= b} */
    FLOW_TOKEN_STAGE_ANNEAL = 2,     /* 1-bit chaotic manifold transition & linkage */
    FLOW_TOKEN_STAGE_SMT_PROOF = 3,  /* 4/4 SMT theorem proofs (buffer, quota, shard, det) */
    FLOW_TOKEN_STAGE_SYNTHESIS = 4,  /* Target candidate selection & validation */
    FLOW_TOKEN_STAGE_ATTRACTOR = 5,  /* Fixed-point attractor & Lyapunov stability */
    FLOW_TOKEN_STAGE_CUSTOM = 6      /* User-defined topological stage */
} FlowTokenStage;

typedef enum {
    FLOW_RING_INIT = 0,
    FLOW_RING_CIRCULATING = 1,
    FLOW_RING_ATTRACTOR_REACHED = 2, /* Fixed point reached: Delta E = 0, SMT proven */
    FLOW_RING_UNSAT = 3,              /* Topological mutex violation / empty manifold */
    FLOW_RING_EXHAUSTED = 4           /* Reached max cycle limit without convergence */
} FlowTokenRingState;

struct FlowTokenRing;
typedef struct FlowToken FlowToken;

/* Transition callback function for each token in the ring */
typedef int (*FlowTokenTransitionFn)(struct FlowTokenRing *ring,
                                     FlowToken *token,
                                     FlowMaskCanvas *canvas);

struct FlowToken {
    uint32_t token_id;
    FlowTokenStage stage;
    char stage_name[32];
    uint64_t attention_mask;  /* Mask_Attn(t) emitted by this token */
    uint64_t dynamic_bias;   /* Soft telemetry/preference bias */
    double energy;           /* Energy contribution at this stage */
    uint64_t execution_count;/* Number of times this token has executed */
    FlowTokenTransitionFn transition_fn;
    void *user_data;
};

typedef struct FlowTokenRing {
    FlowToken tokens[FLOW_TOKEN_RING_MAX_TOKENS];
    size_t token_count;
    size_t current_token_idx;

    /* Circulating Constraint Canvas & Manifold Genome */
    FlowMaskCanvas active_canvas;
    uint64_t active_genome;

    /* Lyapunov Energy Tracking */
    double lyapunov_energy;
    double prev_energy;
    double lyapunov_delta_e;

    /* Trajectory & Convergence Metrics */
    uint64_t cycle_count;
    size_t step_count;
    FlowTokenRingState state;
    int attractor_converged;

    /* Active Domain Context */
    SemanticIR *active_ir;
    FlowBitSpace active_space;
    SearchResult best_search;
    FlowSMTProofAttestation smt_proof;
    FlowPlanEnsemble ensemble;

    /* Annealing Hyperparameters */
    size_t anneal_iterations;
    uint32_t rng_seed;
    uint64_t rng_state;

    char status_message[128];
} FlowTokenRing;

/*
 * Lifecycle & Configuration
 */
int flow_token_ring_init(FlowTokenRing *ring, SemanticIR *ir);

int flow_token_ring_setup_canonical(FlowTokenRing *ring,
                                    SemanticIR *ir,
                                    size_t anneal_iters,
                                    uint32_t seed);

int flow_token_ring_add_token(FlowTokenRing *ring,
                              FlowTokenStage stage,
                              const char *name,
                              FlowTokenTransitionFn fn,
                              void *user_data);

/*
 * BMF Discrete Attention Operator:
 *     Canvas_{t+1} = \Phi(Canvas_t \otimes Mask_{Attn(t)})
 *
 * Updates canvas hard composite mask, superposes soft bias, and projects
 * current_genome onto the legal discrete hypercube manifold \Pi_P({0,1}^64).
 * Returns the projected genome in O(1) bitwise operations.
 */
uint64_t flow_token_ring_attention_project(FlowMaskCanvas *canvas,
                                           uint64_t attention_mask,
                                           uint64_t dynamic_bias,
                                           uint64_t current_genome);

/*
 * State Machine Execution
 */
int flow_token_ring_step(FlowTokenRing *ring);

FlowTokenRingState flow_token_ring_run_to_attractor(FlowTokenRing *ring,
                                                    size_t max_cycles);

int flow_token_ring_is_converged(const FlowTokenRing *ring);

/*
 * Diagnostic & Reflection Helpers
 */
const char *flow_token_ring_state_name(FlowTokenRingState state);
const char *flow_token_stage_name(FlowTokenStage stage);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_TOKEN_RING_H */
