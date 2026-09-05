#ifndef FLOW_TOKEN_RING_H
#define FLOW_TOKEN_RING_H

#include "flow.h"
#include "bitspace.h"
#include "bitmanifold.h"
#include "search.h"
#include "smt.h"
#include "hardware_telemetry.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct FlowBumpQsbrArena FlowBumpQsbrArena;

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
    FlowBmf1BitCanvas bmf_1bit_canvas;
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

    /* Wavefront Epoch & Implicit Quiescence */
    uint64_t wavefront_epoch;
    uint64_t quiescent_generation;
    FlowBumpQsbrArena *bound_arena;

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

uint64_t flow_token_ring_bmf_attention_project(FlowBmf1BitCanvas *canvas,
                                               uint64_t attention_mask,
                                               uint64_t dynamic_bias);

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

/*
 * ============================================================================
 * Part 2: Orthogonal Subspace Decomposition & Join-Semilattice Confluence
 * ============================================================================
 *
 * Direct Sum Decomposition:
 *     \mathcal{M} = \bigoplus_{k=1}^K \mathcal{S}_k
 *     \text{Mask}_j \ \& \ \text{Mask}_k = 0 \quad (\forall j \neq k)
 *
 * Join-Semilattice Merge (Lattice Least Upper Bound):
 *     Canvas_{\text{merged}} = \bigsqcup_{k=1}^K Canvas_k = \bigvee_{k=1}^K (Canvas_k \ \& \ \text{Mask}_k)
 *
 * Mathematical Engine Tuning (Zero Heuristics):
 * 1. Integer Polyhedral Affine Projection on bounds [min_val, max_val].
 * 2. Lagrangian Shadow Price Multiplier \lambda_{t+1} = \max(0, \lambda_t + \eta \cdot (\text{demand} - \text{capacity})).
 * 3. Lyapunov Negative Drift \dot{V} \le -\alpha V proving monotonic multi-threaded convergence.
 * ============================================================================
 */

#define FLOW_MAX_SUBSPACES 8
#define FLOW_WAVEFRONT_MAX_SLOTS 8
#define FLOW_WAVEFRONT_MAX_WORKERS 8

typedef enum {
    FLOW_SUBSPACE_CAPACITY = 0,    /* Bits 0..15: Element/Queue capacity */
    FLOW_SUBSPACE_CONCURRENCY = 1, /* Bits 16..23: Thread count & core affinity */
    FLOW_SUBSPACE_SHARDING = 2,    /* Bits 24..31: Partition/Shard count */
    FLOW_SUBSPACE_BUFFER = 3,      /* Bits 32..47: Buffer size & Arena quota */
    FLOW_SUBSPACE_GROWTH = 4,      /* Bits 48..63: Growth rate & batch size */
    FLOW_SUBSPACE_CUSTOM = 5
} FlowSubspaceId;

typedef struct {
    FlowSubspaceId id;
    char name[32];
    uint64_t mask;             /* Disjoint bitmask (Mask_j & Mask_k = 0) */
    uint8_t bit_offset;        /* Starting bit in 64-bit genome */
    uint8_t bit_width;         /* Width in bits */
    uint64_t min_value;        /* Polyhedral lower bound */
    uint64_t max_value;        /* Polyhedral upper bound */

    /* Mathematical Optimization State (Lagrangian Duality) */
    double shadow_price_lambda;/* Dual multiplier \lambda >= 0 */
    double learning_rate_eta;  /* Subgradient step size \eta */
    double current_demand;     /* Actual utilization / resource demand */
    double capacity_limit;     /* Hard constraint ceiling */
    uint64_t current_val;      /* Current integer parameter value */
    uint64_t optimal_val;      /* Mathematically tuned optimal value */
} FlowSubspace;

typedef struct {
    FlowSubspace subspaces[FLOW_MAX_SUBSPACES];
    size_t subspace_count;
    uint64_t composite_coverage_mask; /* Union of all subspace masks */
    bool is_strictly_orthogonal;      /* 1 if all pairwise intersections are empty */
} FlowSubspaceDecomposition;

/* Concurrent Pipeline Slot */
typedef struct {
    uint32_t slot_id;
    FlowTokenStage current_stage;
    uint64_t slot_genome;
    FlowMaskCanvas slot_canvas;
    double energy;
    bool in_flight;
} FlowWavefrontSlot;

/* Multi-Threaded Slotted Wavefront Ring */
typedef struct FlowWavefrontRing {
    FlowSubspaceDecomposition decomp;
    FlowWavefrontSlot slots[FLOW_WAVEFRONT_MAX_SLOTS];
    size_t slot_count;
    size_t worker_count;

    /* Global Join-Semilattice Canvas Accumulator */
    uint64_t global_lattice_genome;
    FlowMaskCanvas global_canvas;

    /* Lyapunov Multi-Objective Energy Tracking */
    double global_lyapunov_energy;
    double prev_lyapunov_energy;
    double lyapunov_delta_e;
    uint64_t wave_cycle_count;
    bool attractor_converged;
    FlowTokenRingState state;

    /* Domain Context */
    SemanticIR *active_ir;
    FlowBitSpace active_space;
    SearchResult best_search;
    FlowSMTProofAttestation smt_proof;

    /* Hardware Telemetry & Thermodynamic Monitoring */
    FlowPhysicalProbe last_probe;
    uint64_t total_cycles;
    double total_energy_uj;

    /* Wavefront Epoch & Implicit Quiescence */
    uint64_t wavefront_epoch;
    uint64_t quiescent_generation;
    FlowBumpQsbrArena *bound_arena;

    char status_message[128];
} FlowWavefrontRing;

/*
 * Mathematical Engine Subspace APIs
 */

/* Decompose 64-bit BitManifold into canonical orthogonal subspaces */
int flow_subspace_decompose_canonical(FlowSubspaceDecomposition *decomp,
                                      const SemanticIR *ir);

/* Update Lagrangian dual multiplier lambda and compute optimal subspace value without heuristics */
int flow_subspace_lagrangian_tune(FlowSubspace *sub,
                                  double current_demand,
                                  double capacity_limit);

/* Polyhedral box/affine projection for a subspace slice */
uint64_t flow_subspace_polyhedral_project(const FlowSubspace *sub,
                                          uint64_t raw_val);

/* Join-Semilattice Confluence: Commutative, associative, idempotent bitwise merge */
uint64_t flow_wavefront_semilattice_join(uint64_t base_genome,
                                         const uint64_t *thread_slices,
                                         const uint64_t *subspace_masks,
                                         size_t count);

/* Initialize Multi-Threaded Slotted Wavefront Ring */
int flow_wavefront_ring_init(FlowWavefrontRing *ring,
                             SemanticIR *ir,
                             size_t num_slots,
                             size_t num_workers);

/* Execute one concurrent parallel wave step across worker threads */
int flow_wavefront_ring_step_parallel(FlowWavefrontRing *ring);

/* Run Wavefront Ring until Lyapunov Attractor convergence (Delta E -> 0) */
FlowTokenRingState flow_wavefront_ring_run_to_attractor(FlowWavefrontRing *ring,
                                                        size_t max_cycles);

/* Destroy Wavefront Ring resources */
void flow_wavefront_ring_destroy(FlowWavefrontRing *ring);

/*
 * Wavefront-Coupled Implicit QSBR APIs
 * Binds a bump-pointer QSBR arena to the ring so it folds automatically on each wavefront rotation
 */
int flow_token_ring_bind_arena(FlowTokenRing *ring, FlowBumpQsbrArena *arena);
int flow_wavefront_ring_bind_arena(FlowWavefrontRing *ring, FlowBumpQsbrArena *arena);

/*
 * SMT Supreme Court Wavefront Temporal Safety Theorem
 * Mathematically proves:
 * 1. SWMR (Single-Writer Multi-Reader) Lock-Free Safety: non-blocking writer progress.
 * 2. 64-Byte Cache Line Confinement & Tear-Free Phase Shift (FLOW_ATOMIC_STAGE_SWAP).
 * 3. Bounded Evacuation Horizon: all readers evacuated within N_slots * tau_slot.
 * 4. Zero-Imperative-Cleanup: Bump-pointer reset at quiescent boundary has 0ns runtime overhead.
 */
FlowSMTResult flow_wavefront_verify_temporal_safety_smt(const FlowWavefrontRing *ring,
                                                        FlowSMTProofAttestation *proof_out);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_TOKEN_RING_H */
