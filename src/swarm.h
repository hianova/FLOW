#ifndef FLOW_SWARM_H
#define FLOW_SWARM_H

#include "bitspace.h"
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

#endif
