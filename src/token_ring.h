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
#define FLOW_TOKEN_RING_MAX_TOKENS 16
#define FLOW_TOKEN_RING_DEFAULT_MAX_CYCLES 32
typedef enum { FLOW_TOKEN_STAGE_INGEST = 0, FLOW_TOKEN_STAGE_POLYTOPE = 1, FLOW_TOKEN_STAGE_ANNEAL = 2,
    FLOW_TOKEN_STAGE_SMT_PROOF = 3, FLOW_TOKEN_STAGE_SYNTHESIS = 4, FLOW_TOKEN_STAGE_ATTRACTOR = 5, FLOW_TOKEN_STAGE_CUSTOM = 6
} FlowTokenStage; typedef enum { FLOW_RING_INIT = 0, FLOW_RING_CIRCULATING = 1, FLOW_RING_ATTRACTOR_REACHED = 2, FLOW_RING_UNSAT = 3, FLOW_RING_EXHAUSTED = 4
} FlowTokenRingState; struct FlowTokenRing; typedef struct FlowToken FlowToken;
typedef int (*FlowTokenTransitionFn)(struct FlowTokenRing *ring, FlowToken *token, FlowMaskCanvas *canvas); struct FlowToken { uint32_t token_id; FlowTokenStage stage;
    char stage_name[32]; uint64_t attention_mask, dynamic_bias, execution_count; double energy; FlowTokenTransitionFn transition_fn; void *user_data; };
typedef struct FlowTokenRing { FlowToken tokens[FLOW_TOKEN_RING_MAX_TOKENS]; size_t token_count, current_token_idx, step_count, anneal_iterations;
    FlowMaskCanvas active_canvas; FlowBmf1BitCanvas bmf_1bit_canvas; uint64_t active_genome, cycle_count, rng_state, wavefront_epoch, quiescent_generation;
    double lyapunov_energy, prev_energy, lyapunov_delta_e; FlowTokenRingState state; int attractor_converged; SemanticIR *active_ir; FlowBitSpace active_space;
    SearchResult best_search; FlowSMTProofAttestation smt_proof; FlowPlanEnsemble ensemble; uint32_t rng_seed; FlowBumpQsbrArena *bound_arena; char status_message[128];
} FlowTokenRing; int flow_token_ring_init(FlowTokenRing *ring, SemanticIR *ir);
int flow_token_ring_setup_canonical(FlowTokenRing *ring, SemanticIR *ir, size_t anneal_iters, uint32_t seed);
int flow_token_ring_add_token(FlowTokenRing *ring, FlowTokenStage stage, const char *name, FlowTokenTransitionFn fn, void *user_data);
uint64_t flow_token_ring_attention_project(FlowMaskCanvas *canvas, uint64_t attention_mask, uint64_t dynamic_bias, uint64_t current_genome);
uint64_t flow_token_ring_bmf_attention_project(FlowBmf1BitCanvas *canvas, uint64_t attention_mask, uint64_t dynamic_bias); int flow_token_ring_step(FlowTokenRing *ring);
FlowTokenRingState flow_token_ring_run_to_attractor(FlowTokenRing *ring, size_t max_cycles); int flow_token_ring_is_converged(const FlowTokenRing *ring);
const char *flow_token_ring_state_name(FlowTokenRingState state); const char *flow_token_stage_name(FlowTokenStage stage);
#define FLOW_MAX_SUBSPACES 8
#define FLOW_WAVEFRONT_MAX_SLOTS 8
#define FLOW_WAVEFRONT_MAX_WORKERS 8
typedef enum { FLOW_SUBSPACE_CAPACITY = 0, FLOW_SUBSPACE_CONCURRENCY = 1, FLOW_SUBSPACE_SHARDING = 2,
    FLOW_SUBSPACE_BUFFER = 3, FLOW_SUBSPACE_GROWTH = 4, FLOW_SUBSPACE_CUSTOM = 5
} FlowSubspaceId; typedef struct { FlowSubspaceId id; char name[32]; uint64_t mask; uint8_t bit_offset, bit_width; uint64_t min_value, max_value;
    double shadow_price_lambda, learning_rate_eta, current_demand, capacity_limit; uint64_t current_val, optimal_val; } FlowSubspace; typedef struct {
    FlowSubspace subspaces[FLOW_MAX_SUBSPACES]; size_t subspace_count; uint64_t composite_coverage_mask; bool is_strictly_orthogonal; } FlowSubspaceDecomposition;
typedef struct __attribute__((aligned(64))) { uint32_t slot_id; FlowTokenStage current_stage; uint64_t slot_genome; FlowBmf1BitCanvas bmf_canvas; FlowMaskCanvas slot_canvas;
    double energy; bool in_flight; } FlowWavefrontSlot; typedef struct FlowWavefrontRing { FlowSubspaceDecomposition decomp; FlowWavefrontSlot slots[FLOW_WAVEFRONT_MAX_SLOTS];
    size_t slot_count, worker_count; uint64_t global_lattice_genome, wave_cycle_count, total_cycles, wavefront_epoch, quiescent_generation; FlowMaskCanvas global_canvas;
    double global_lyapunov_energy, prev_lyapunov_energy, lyapunov_delta_e, total_energy_uj; bool attractor_converged; FlowTokenRingState state; SemanticIR *active_ir;
    FlowBitSpace active_space; SearchResult best_search; FlowSMTProofAttestation smt_proof; FlowPhysicalProbe last_probe; FlowBumpQsbrArena *bound_arena;
    char status_message[128]; } FlowWavefrontRing; int flow_subspace_decompose_canonical(FlowSubspaceDecomposition *decomp, const SemanticIR *ir);
int flow_subspace_lagrangian_tune(FlowSubspace *sub, double current_demand, double capacity_limit);
uint64_t flow_subspace_polyhedral_project(const FlowSubspace *sub, uint64_t raw_val);
uint64_t flow_wavefront_semilattice_join(uint64_t base_genome, const uint64_t *thread_slices, const uint64_t *subspace_masks, size_t count);
int flow_wavefront_ring_init(FlowWavefrontRing *ring, SemanticIR *ir, size_t num_slots, size_t num_workers); int flow_wavefront_ring_step_parallel(FlowWavefrontRing *ring);
FlowTokenRingState flow_wavefront_ring_run_to_attractor(FlowWavefrontRing *ring, size_t max_cycles); void flow_wavefront_ring_destroy(FlowWavefrontRing *ring);
int flow_token_ring_bind_arena(FlowTokenRing *ring, FlowBumpQsbrArena *arena); int flow_wavefront_ring_bind_arena(FlowWavefrontRing *ring, FlowBumpQsbrArena *arena);
FlowSMTResult flow_wavefront_verify_temporal_safety_smt(const FlowWavefrontRing *ring, FlowSMTProofAttestation *proof_out);
#ifdef __cplusplus
}
#endif
#endif /* FLOW_TOKEN_RING_H */
