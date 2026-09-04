#ifndef FLOW_SWARM_H
#define FLOW_SWARM_H

#include "bitspace.h"
#include "smt.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define FLOW_SWARM_MAX_PARTICLES 32

typedef struct {
    uint32_t id;
    uint64_t current_genome;
    uint64_t best_genome;
    FlowPlan best_plan;
    double current_energy;
    double best_energy;
    uint64_t local_mask;
    uint32_t stagnation_steps;
} FlowSwarmParticle;

typedef struct {
    double bit_intensity[64];         /* Pheromone trace per BitSpace bit */
    uint64_t consensus_mask;          /* Topologically entangled mask */
    double evaporation_rate;          /* Decay rate per epoch (e.g. 0.92) */
    double reinforcement_weight;      /* Pheromone deposition weight (e.g. 2.5) */
} FlowSwarmPheromone;

typedef struct {
    const FlowBitSpace *space;
    size_t particle_count;
    FlowSwarmParticle particles[FLOW_SWARM_MAX_PARTICLES];
    FlowSwarmPheromone pheromone;
    FlowPlan global_best_plan;
    double global_best_energy;
    size_t total_swarm_mutations;
    size_t total_saddle_point_escapes;
    uint64_t rng_state;
} FlowSwarmCluster;

/* Swarm Intelligence & Federated Chaos APIs */
int flow_swarm_init(FlowSwarmCluster *cluster, const FlowBitSpace *space,
                    size_t particle_count, uint32_t base_seed);

int flow_swarm_step(FlowSwarmCluster *cluster, size_t steps_per_particle);
int flow_swarm_diffuse_pheromone(FlowSwarmCluster *cluster);

int flow_swarm_search(const FlowBitSpace *space, size_t particle_count,
                      size_t cycles, uint32_t seed, int measured,
                      FlowBitSearchResult *result_out);

void flow_swarm_report(const FlowSwarmCluster *cluster, FILE *out);

/* ------------------------------------------------------------------------- */
/* Swarm Lymphatic Broadcasting (9-Byte Fleet-Wide Antibody Propagation)     */
/* ------------------------------------------------------------------------- */
#define FLOW_SWARM_MSG_ANTIBODY 0xAA
#define FLOW_SWARM_LYMPH_PKT_SIZE 9

int flow_swarm_lymphatic_encode(uint64_t content_hash, uint8_t out_packet[FLOW_SWARM_LYMPH_PKT_SIZE]);
int flow_swarm_lymphatic_decode(const uint8_t in_packet[FLOW_SWARM_LYMPH_PKT_SIZE], uint64_t *out_content_hash);
int flow_swarm_lymphatic_assimilate(const char *local_vec_dir,
                                    const char *peer_vec_dir,
                                    uint64_t content_hash);

/* ------------------------------------------------------------------------- */
/* Heterogeneous Pheromone Mesh (Multi-Role 9-Byte Fluid Backpressure)       */
/* ------------------------------------------------------------------------- */
#define FLOW_SWARM_MSG_HETERO_PHEROMONE 0xBB
#define FLOW_SWARM_HETERO_PKT_SIZE 9
#define FLOW_HETERO_MESH_MAX_NODES 32

typedef enum {
    FLOW_SWARM_ROLE_GENERIC = 0,
    FLOW_SWARM_ROLE_INGRESS_GATEWAY = 1,  /* HTTP/QUIC Gateway */
    FLOW_SWARM_ROLE_COMPUTE_ROUTER = 2,   /* Parallel Map / Compute Worker */
    FLOW_SWARM_ROLE_STORAGE_INDEX = 3,    /* Sharded Hash / Storage Store */
    FLOW_SWARM_ROLE_EMBODIED_ACTUATOR = 4 /* 1kHz Spinal Reflex Node */
} FlowSwarmRole;

typedef struct {
    FlowSwarmRole role;
    uint8_t node_id;
    uint16_t backpressure_permille;  /* 0..1000 queue saturation */
    uint16_t latency_p99_us;         /* P99 latency in microseconds */
    uint16_t contract_crc16;         /* 16-bit CRC of the .flow contract */
} FlowHeteroPheromonePacket;

typedef struct {
    uint8_t node_id;
    FlowSwarmRole role;
    char name[32];
    uint64_t contract_hash;
    uint32_t capacity;               /* Max QPS / processing capability */
    uint16_t current_backpressure;   /* 0..1000 */
    uint16_t current_latency_us;     /* Microseconds */
    uint64_t total_routed_requests;
    int is_active;
} FlowHeteroMeshNode;

typedef struct {
    size_t node_count;
    FlowHeteroMeshNode nodes[FLOW_HETERO_MESH_MAX_NODES];
    uint64_t total_mesh_telemetry_packets;
    uint64_t total_mesh_routed;
} FlowHeteroMesh;

/* 9-Byte Heterogeneous Pheromone Packet Encoding & Decoding */
int flow_swarm_hetero_encode(const FlowHeteroPheromonePacket *pkt, uint8_t out[FLOW_SWARM_HETERO_PKT_SIZE]);
int flow_swarm_hetero_decode(const uint8_t in[FLOW_SWARM_HETERO_PKT_SIZE], FlowHeteroPheromonePacket *pkt_out);

/* Heterogeneous Mesh Lifecycle & Routing */
void flow_hetero_mesh_init(FlowHeteroMesh *mesh);
int flow_hetero_mesh_register_node(FlowHeteroMesh *mesh,
                                   uint8_t node_id,
                                   FlowSwarmRole role,
                                   const char *name,
                                   uint64_t contract_hash,
                                   uint32_t capacity);

int flow_hetero_mesh_receive_pheromone(FlowHeteroMesh *mesh, const uint8_t packet[FLOW_SWARM_HETERO_PKT_SIZE]);

/* Dynamic Fluid Routing: Select lowest energy downstream node */
int flow_hetero_mesh_route_target(const FlowHeteroMesh *mesh,
                                  FlowSwarmRole target_role,
                                  uint8_t *selected_node_id_out);

/* SMT Formal Flow Conservation Theorem Verification */
FlowSMTResult flow_hetero_mesh_verify_smt(const FlowHeteroMesh *mesh,
                                          uint32_t ingress_max_qps,
                                          FlowSMTProofAttestation *proof_out);

#endif
