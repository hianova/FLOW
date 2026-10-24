#ifndef FLOW_TOKEN_RING_H
#define FLOW_TOKEN_RING_H
#include "flow.h"
#include "bitspace.h"
#include "smt.h"
#include "hardware_telemetry.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>
typedef struct FlowBumpQsbrArena FlowBumpQsbrArena;
#ifdef __cplusplus
extern "C" {
#endif
#define FLOW_WIRE_FRAME9_SIZE 9
#define FLOW_WIRE_OP_HASH_SYNC    0xAA
#define FLOW_WIRE_OP_ANTIBODY     0xAA
#define FLOW_WIRE_OP_HETERO_MESH  0xBB
#define FLOW_WIRE_OP_FLEET_SYNC   0xCC
#pragma pack(push, 1)
typedef struct { uint8_t opcode; union { struct { uint8_t hash_bytes[8]; } antibody;
    struct { uint8_t role, node_id, bp_hi, bp_lo, lat_hi, lat_lo, crc_hi, crc_lo; } hetero_mesh;
    struct { uint8_t agent_id, role, x_hi, x_lo, y_hi, y_lo, crc_hi, crc_lo; } fleet; uint8_t raw[8]; } payload; } FlowWireFrame9;
#pragma pack(pop)
static inline uint16_t flow_wire_crc16(const uint8_t *data, size_t len) { uint16_t crc = 0xFFFF; if (!data) return 0; for (size_t i = 0; i < len; ++i) {
    crc ^= (uint16_t)data[i] << 8; for (int b = 0; b < 8; ++b) crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1); } return crc; }
static inline int flow_wire_frame9_pack(const FlowWireFrame9 *frame, uint8_t out[FLOW_WIRE_FRAME9_SIZE]) {
    return (frame && out) ? (memcpy(out, frame, FLOW_WIRE_FRAME9_SIZE), 1) : 0; }
static inline int flow_wire_frame9_unpack(const uint8_t in[FLOW_WIRE_FRAME9_SIZE], FlowWireFrame9 *frame_out) {
    return (in && frame_out) ? (memcpy(frame_out, in, FLOW_WIRE_FRAME9_SIZE), 1) : 0; }
static inline int flow_wire_hash_encode(uint64_t content_hash, uint8_t out_packet[FLOW_WIRE_FRAME9_SIZE]) { if (!out_packet) return 0;
    FlowWireFrame9 f = { .opcode = FLOW_WIRE_OP_HASH_SYNC }; for (int i = 0; i < 8; ++i) f.payload.antibody.hash_bytes[i] = (uint8_t)((content_hash >> (56 - i * 8)) & 0xFF);
    return flow_wire_frame9_pack(&f, out_packet); }
static inline int flow_wire_hash_decode(const uint8_t in_packet[FLOW_WIRE_FRAME9_SIZE], uint64_t *out_content_hash) { if (!in_packet || !out_content_hash) return 0;
    FlowWireFrame9 f; if (!flow_wire_frame9_unpack(in_packet, &f) || f.opcode != FLOW_WIRE_OP_HASH_SYNC) return 0; uint64_t h = 0;
    for (int i = 0; i < 8; ++i) h = (h << 8) | f.payload.antibody.hash_bytes[i]; *out_content_hash = h; return 1; }
#define FLOW_SWARM_MSG_ANTIBODY       FLOW_WIRE_OP_HASH_SYNC
#define FLOW_SWARM_LYMPH_PKT_SIZE     FLOW_WIRE_FRAME9_SIZE
#define flow_swarm_lymphatic_encode   flow_wire_hash_encode
#define flow_swarm_lymphatic_decode   flow_wire_hash_decode
#define FLOW_TOKEN_RING_MAX_TOKENS 16
#define FLOW_TOKEN_RING_DEFAULT_MAX_CYCLES 32
typedef enum { FLOW_TOKEN_STAGE_INGEST = 0, FLOW_TOKEN_STAGE_POLYTOPE = 1, FLOW_TOKEN_STAGE_ANNEAL = 2,
    FLOW_TOKEN_STAGE_SMT_PROOF = 3, FLOW_TOKEN_STAGE_SYNTHESIS = 4, FLOW_TOKEN_STAGE_ATTRACTOR = 5, FLOW_TOKEN_STAGE_CUSTOM = 6 } FlowTokenStage;
typedef enum { FLOW_RING_INIT = 0, FLOW_RING_CIRCULATING = 1, FLOW_RING_ATTRACTOR_REACHED = 2, FLOW_RING_UNSAT = 3, FLOW_RING_EXHAUSTED = 4 } FlowTokenRingState;
struct FlowTokenRing; typedef struct FlowToken FlowToken;
typedef int (*FlowTokenTransitionFn)(struct FlowTokenRing *ring, FlowToken *token, FlowMaskCanvas *canvas);
struct FlowToken { uint32_t token_id; FlowTokenStage stage; char stage_name[32]; uint64_t attention_mask, dynamic_bias, execution_count;
    double energy; FlowTokenTransitionFn transition_fn; void *user_data; };
typedef struct FlowTokenRing { FlowToken tokens[FLOW_TOKEN_RING_MAX_TOKENS]; size_t token_count, current_token_idx, step_count, anneal_iterations;
    FlowMaskCanvas active_canvas; FlowBmf1BitCanvas bmf_1bit_canvas; uint64_t active_genome, cycle_count, rng_state, wavefront_epoch, quiescent_generation;
    double lyapunov_energy, prev_energy, lyapunov_delta_e; FlowTokenRingState state; int attractor_converged; SemanticIR *active_ir; FlowBitSpace active_space;
    SearchResult best_search; FlowSMTProofAttestation smt_proof; FlowPlanEnsemble ensemble; uint32_t rng_seed; FlowBumpQsbrArena *bound_arena; char status_message[128]; } FlowTokenRing;
int flow_token_ring_init(FlowTokenRing *ring, SemanticIR *ir);
int flow_token_ring_setup_canonical(FlowTokenRing *ring, SemanticIR *ir, size_t anneal_iters, uint32_t seed);
int flow_token_ring_add_token(FlowTokenRing *ring, FlowTokenStage stage, const char *name, FlowTokenTransitionFn fn, void *user_data);
uint64_t flow_token_ring_attention_project(FlowMaskCanvas *canvas, uint64_t attention_mask, uint64_t dynamic_bias, uint64_t current_genome);
uint64_t flow_token_ring_bmf_attention_project(FlowBmf1BitCanvas *canvas, uint64_t attention_mask, uint64_t dynamic_bias);
int flow_token_ring_step(FlowTokenRing *ring);
FlowTokenRingState flow_token_ring_run_to_attractor(FlowTokenRing *ring, size_t max_cycles);
int flow_token_ring_converge_default(FlowTokenRing *ring, const SemanticIR *opt_ir, const char *flow_name);

#define FLOW_SUBSPACE_MASK_CAPACITY     0x000000000000FFFFULL
#define FLOW_SUBSPACE_MASK_CONCURRENCY  0x0000000000FF0000ULL
#define FLOW_SUBSPACE_MASK_SHARDING     0x00000000FF000000ULL
#define FLOW_SUBSPACE_MASK_BUFFER       0x0000FFFF00000000ULL
#define FLOW_SUBSPACE_MASK_GROWTH       0xFFFF000000000000ULL
#define FLOW_LYAPUNOV_CONVERGE_EPS      1e-5
#define FLOW_LAGRANGIAN_DEFAULT_ETA     0.05
#define FLOW_MESH_SATURATION_LIMIT_PERMILLE 949U

#define FLOW_MAX_SUBSPACES 8
#define FLOW_WAVEFRONT_MAX_SLOTS 8
#define FLOW_WAVEFRONT_MAX_WORKERS 8
typedef enum { FLOW_SUBSPACE_CAPACITY = 0, FLOW_SUBSPACE_CONCURRENCY = 1, FLOW_SUBSPACE_SHARDING = 2,
    FLOW_SUBSPACE_BUFFER = 3, FLOW_SUBSPACE_GROWTH = 4, FLOW_SUBSPACE_CUSTOM = 5 } FlowSubspaceId;
typedef struct { FlowSubspaceId id; char name[32]; uint64_t mask; uint8_t bit_offset, bit_width; uint64_t min_value, max_value;
    double shadow_price_lambda, learning_rate_eta, current_demand, capacity_limit; uint64_t current_val, optimal_val; } FlowSubspace;
typedef struct { FlowSubspace subspaces[FLOW_MAX_SUBSPACES]; size_t subspace_count; uint64_t composite_coverage_mask; bool is_strictly_orthogonal; } FlowSubspaceDecomposition;
typedef struct __attribute__((aligned(64))) { uint32_t slot_id; FlowTokenStage current_stage; uint64_t slot_genome; FlowBmf1BitCanvas bmf_canvas; FlowMaskCanvas slot_canvas;
    double energy; bool in_flight; } FlowWavefrontSlot;
typedef struct FlowWavefrontRing { FlowSubspaceDecomposition decomp; FlowWavefrontSlot slots[FLOW_WAVEFRONT_MAX_SLOTS]; size_t slot_count, worker_count;
    uint64_t global_lattice_genome, wave_cycle_count, total_cycles, wavefront_epoch, quiescent_generation; FlowMaskCanvas global_canvas;
    double global_lyapunov_energy, prev_lyapunov_energy, lyapunov_delta_e, total_energy_uj; bool attractor_converged; FlowTokenRingState state; SemanticIR *active_ir;
    FlowBitSpace active_space; SearchResult best_search; FlowSMTProofAttestation smt_proof; FlowPhysicalProbe last_probe; FlowBumpQsbrArena *bound_arena; char status_message[128]; } FlowWavefrontRing;
int flow_subspace_decompose_canonical(FlowSubspaceDecomposition *decomp, const SemanticIR *ir);
int flow_subspace_lagrangian_tune(FlowSubspace *sub, double current_demand, double capacity_limit);
uint64_t flow_subspace_polyhedral_project(const FlowSubspace *sub, uint64_t raw_val);
uint64_t flow_wavefront_semilattice_join(uint64_t base_genome, const uint64_t *thread_slices, const uint64_t *subspace_masks, size_t count);
int flow_wavefront_ring_init(FlowWavefrontRing *ring, SemanticIR *ir, size_t num_slots, size_t num_workers);
int flow_wavefront_ring_step_parallel(FlowWavefrontRing *ring);
void flow_wavefront_ring_destroy(FlowWavefrontRing *ring);
int flow_wavefront_ring_bind_arena(FlowWavefrontRing *ring, FlowBumpQsbrArena *arena);
FlowSMTResult flow_wavefront_verify_temporal_safety_smt(const FlowWavefrontRing *ring, FlowSMTProofAttestation *proof_out);
#define FLOW_SWARM_MSG_HETERO_PHEROMONE 0xBB
#define FLOW_SWARM_HETERO_PKT_SIZE 9
#define FLOW_HETERO_MESH_MAX_NODES 64
typedef enum { FLOW_SWARM_ROLE_GENERIC = 0, FLOW_SWARM_ROLE_INGRESS_GATEWAY = 1, FLOW_SWARM_ROLE_COMPUTE_ROUTER = 2, FLOW_SWARM_ROLE_STORAGE_INDEX = 3,
    FLOW_SWARM_ROLE_EMBODIED_ACTUATOR = 4 } FlowSwarmRole;
typedef struct { uint8_t node_id; FlowSwarmRole role; char name[32]; uint64_t contract_hash; uint32_t capacity; uint16_t current_backpressure, current_latency_us;
    uint64_t total_routed_requests; int is_active; } FlowHeteroMeshNode;
typedef struct { size_t node_count; FlowHeteroMeshNode nodes[FLOW_HETERO_MESH_MAX_NODES]; uint64_t total_mesh_telemetry_packets, total_mesh_routed; } FlowHeteroMesh;
void flow_hetero_mesh_init(FlowHeteroMesh *mesh);
int flow_hetero_mesh_register_node(FlowHeteroMesh *mesh, uint8_t node_id, FlowSwarmRole role, const char *name, uint64_t contract_hash, uint32_t capacity);
int flow_hetero_mesh_route_target(const FlowHeteroMesh *mesh, FlowSwarmRole target_role, uint8_t *selected_node_id_out);
FlowSMTResult flow_hetero_mesh_verify_smt(const FlowHeteroMesh *mesh, uint32_t ingress_max_qps, FlowSMTProofAttestation *proof_out);
#define FLOW_HETERO_RING_MAX_NODES 64
typedef struct __attribute__((aligned(64))) { _Atomic uint64_t seq; uint8_t node_id, role; uint16_t backpressure_permille, latency_p99_us, contract_crc16;
    uint32_t capacity_qps; uint64_t timestamp_ns; double q_snapshot[16]; uint8_t reserved[36]; } FlowSwarmRingSlot;
typedef struct { FlowSwarmRingSlot slots[FLOW_HETERO_RING_MAX_NODES]; size_t active_node_count; uint64_t total_ring_updates, total_ring_samples; } FlowSwarmRingMesh;
int flow_swarm_ring_mesh_init(FlowSwarmRingMesh *mesh);
int flow_swarm_ring_publish(FlowSwarmRingMesh *mesh, uint8_t node_id, FlowSwarmRole role, uint16_t backpressure, uint16_t latency_p99_us, uint16_t contract_crc16, uint32_t capacity_qps, const double q[16]);
int flow_swarm_ring_sample(const FlowSwarmRingMesh *mesh, uint8_t node_id, FlowSwarmRingSlot *slot_out);
int flow_swarm_ring_route_lowest_energy(const FlowSwarmRingMesh *mesh, FlowSwarmRole target_role, uint8_t *selected_node_out);
#ifdef __cplusplus
}
#endif
#endif /* FLOW_TOKEN_RING_H */
