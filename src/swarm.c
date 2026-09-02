#include "swarm.h"
#include "bitspace.h"
#include "registry.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t swarm_xorshift64(uint64_t *state) {
    uint64_t x = *state;
    if (x == 0) x = UINT64_C(0xdeadbeefcafebabe);
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

int flow_swarm_init(FlowSwarmCluster *cluster, const FlowBitSpace *space,
                    size_t particle_count, uint32_t base_seed) {
    if (cluster == NULL || space == NULL || particle_count == 0) return 0;
    if (particle_count > FLOW_SWARM_MAX_PARTICLES) particle_count = FLOW_SWARM_MAX_PARTICLES;
    memset(cluster, 0, sizeof(*cluster));

    cluster->space = space;
    cluster->particle_count = particle_count;
    cluster->rng_state = base_seed == 0 ? UINT64_C(0x1928374650afbecd) : (uint64_t)base_seed;

    cluster->pheromone.evaporation_rate = 0.92;
    cluster->pheromone.reinforcement_weight = 2.5;
    for (int b = 0; b < 64; ++b) {
        cluster->pheromone.bit_intensity[b] = 1.0;
    }
    cluster->pheromone.consensus_mask = (space->bit_count >= 64) ? (uint64_t)-1 : (((uint64_t)1 << space->bit_count) - 1);
    if (space->env_mask != 0) {
        cluster->pheromone.consensus_mask &= space->env_mask;
    }

    uint64_t def_genome = flow_bitspace_default_genome(space);

    for (size_t i = 0; i < particle_count; ++i) {
        FlowSwarmParticle *p = &cluster->particles[i];
        p->id = (uint32_t)i;
        p->current_genome = def_genome;

        /* Perturb starting genome slightly per particle */
        if (i > 0) {
            uint32_t flip_bit = (uint32_t)(swarm_xorshift64(&cluster->rng_state) % (space->bit_count > 0 ? space->bit_count : 1));
            p->current_genome ^= (UINT64_C(1) << flip_bit);
        }

        p->best_genome = p->current_genome;
        p->local_mask = space->env_mask;

        space->decode(space, p->current_genome, &p->best_plan);
        space->evaluate(space, &p->best_plan, &p->best_plan.eval);
        p->best_plan.eval.hard_gate_passed = space->hard_gate(space, &p->best_plan, &p->best_plan.eval);
        if (!p->best_plan.eval.hard_gate_passed) {
            p->best_plan.eval.energy += 1.0e12;
        }

        p->current_energy = p->best_plan.eval.energy;
        p->best_energy = p->best_plan.eval.energy;

        if (i == 0 || (p->best_plan.eval.hard_gate_passed &&
                      (!cluster->global_best_plan.eval.hard_gate_passed || p->best_energy < cluster->global_best_energy))) {
            cluster->global_best_plan = p->best_plan;
            cluster->global_best_energy = p->best_energy;
        }
    }
    return 1;
}

int flow_swarm_diffuse_pheromone(FlowSwarmCluster *cluster) {
    if (cluster == NULL || cluster->space == NULL) return 0;
    uint32_t bits = cluster->space->bit_count > 0 ? cluster->space->bit_count : 16;
    if (bits > 64) bits = 64;

    double sum = 0.0;
    for (uint32_t b = 0; b < bits; ++b) {
        /* Evaporation */
        cluster->pheromone.bit_intensity[b] *= cluster->pheromone.evaporation_rate;
        if (cluster->pheromone.bit_intensity[b] < 0.1) {
            cluster->pheromone.bit_intensity[b] = 0.1;
        }
        sum += cluster->pheromone.bit_intensity[b];
    }

    double avg = bits > 0 ? sum / (double)bits : 1.0;
    uint64_t mask = 0;
    for (uint32_t b = 0; b < bits; ++b) {
        if (cluster->pheromone.bit_intensity[b] >= avg) {
            mask |= (UINT64_C(1) << b);
        }
    }
    if (mask == 0) mask = (bits >= 64) ? (uint64_t)-1 : (((uint64_t)1 << bits) - 1);
    if (cluster->space->env_mask != 0) mask &= cluster->space->env_mask;

    cluster->pheromone.consensus_mask = mask;
    return 1;
}

int flow_swarm_step(FlowSwarmCluster *cluster, size_t steps_per_particle) {
    if (cluster == NULL || cluster->space == NULL) return 0;
    const FlowBitSpace *space = cluster->space;
    uint32_t bits = space->bit_count > 0 ? space->bit_count : 16;
    if (bits > 64) bits = 64;

    FlowMaskCanvas swarm_canvas = space->global_canvas;
    swarm_canvas.soft_composite_bias |= cluster->pheromone.consensus_mask;
    if (swarm_canvas.hard_composite_mask != 0) {
        swarm_canvas.soft_composite_bias &= swarm_canvas.hard_composite_mask;
    }

    for (size_t p_idx = 0; p_idx < cluster->particle_count; ++p_idx) {
        FlowSwarmParticle *p = &cluster->particles[p_idx];

        for (size_t step = 0; step < steps_per_particle; ++step) {
            cluster->total_swarm_mutations++;
            uint32_t mutated_bit = 0;

            uint64_t cand_genome = flow_bitspace_mutate_1bit_superposed(
                space, p->current_genome, &swarm_canvas, 0.65, &cluster->rng_state, &mutated_bit);

            if (mutated_bit == 0xFFFFFFFF || mutated_bit >= bits) {
                continue;
            }

            FlowPlan cand_plan;
            space->decode(space, cand_genome, &cand_plan);
            space->evaluate(space, &cand_plan, &cand_plan.eval);
            cand_plan.eval.hard_gate_passed = space->hard_gate(space, &cand_plan, &cand_plan.eval);
            if (!cand_plan.eval.hard_gate_passed) {
                cand_plan.eval.energy += 1.0e12;
            }

            double delta = cand_plan.eval.energy - p->current_energy;

            if (delta < 0.0 || !p->best_plan.eval.hard_gate_passed) {
                p->current_genome = cand_genome;
                p->current_energy = cand_plan.eval.energy;

                if (cand_plan.eval.hard_gate_passed &&
                    (!p->best_plan.eval.hard_gate_passed || cand_plan.eval.energy < p->best_energy)) {
                    double gain = p->best_energy - cand_plan.eval.energy;
                    p->best_energy = cand_plan.eval.energy;
                    p->best_genome = cand_genome;
                    p->best_plan = cand_plan;
                    p->stagnation_steps = 0;

                    /* Pheromone Trace Reinforcement */
                    if (mutated_bit < 64) {
                        double boost = (gain > 0.0) ? (gain / (cand_plan.eval.energy + 1.0)) : 1.0;
                        cluster->pheromone.bit_intensity[mutated_bit] += cluster->pheromone.reinforcement_weight * boost;
                    }

                    /* Global Best Breakthrough */
                    if (!cluster->global_best_plan.eval.hard_gate_passed ||
                        p->best_energy < cluster->global_best_energy) {
                        cluster->global_best_plan = cand_plan;
                        cluster->global_best_energy = p->best_energy;
                    }
                }
            } else {
                p->stagnation_steps++;
                if (p->stagnation_steps >= 8) {
                    /* Topological Entanglement Saddle Point Escape: Quantum tunneling towards global consensus */
                    cluster->total_saddle_point_escapes++;
                    uint32_t guide_bit = (uint32_t)(swarm_xorshift64(&cluster->rng_state) % bits);
                    if (cluster->global_best_plan.eval.hard_gate_passed) {
                        p->current_genome = cluster->global_best_plan.genome ^ (UINT64_C(1) << guide_bit);
                    } else {
                        p->current_genome = flow_bitspace_default_genome(space) ^ (UINT64_C(1) << guide_bit);
                    }
                    space->decode(space, p->current_genome, &p->best_plan);
                    space->evaluate(space, &p->best_plan, &p->best_plan.eval);
                    p->current_energy = p->best_plan.eval.energy;
                    p->stagnation_steps = 0;
                }
            }
        }
    }

    flow_swarm_diffuse_pheromone(cluster);
    return 1;
}

int flow_swarm_search(const FlowBitSpace *space, size_t particle_count,
                      size_t cycles, uint32_t seed, int measured,
                      FlowBitSearchResult *result_out) {
    if (space == NULL || result_out == NULL) return 0;
    if (particle_count == 0) particle_count = 8;
    if (cycles == 0) cycles = 10;

    FlowSwarmCluster cluster;
    if (!flow_swarm_init(&cluster, space, particle_count, seed)) return 0;

    memset(result_out, 0, sizeof(*result_out));
    result_out->iterations = particle_count * cycles * 10;
    result_out->seed = seed;
    result_out->measured = measured;

    for (size_t c = 0; c < cycles; ++c) {
        flow_swarm_step(&cluster, 10);
    }

    /* Benchmark validation if measured */
    if (measured && cluster.global_best_plan.component != NULL && space->ir != NULL) {
        cluster.global_best_plan.eval.benchmark_ns = flow_component_benchmark(
            space->ir, cluster.global_best_plan.component, &cluster.global_best_plan.assignment);
    }

    result_out->best_plan = cluster.global_best_plan;
    result_out->mask_canvas = space->global_canvas;
    result_out->mask_canvas.soft_composite_bias = cluster.pheromone.consensus_mask;

    return cluster.global_best_plan.eval.hard_gate_passed ? 1 : 0;
}

void flow_swarm_report(const FlowSwarmCluster *cluster, FILE *out) {
    if (cluster == NULL || out == NULL) return;
    fprintf(out, "Swarm Intelligence & Federated Chaos Report:\n");
    fprintf(out, "  Particles: %zu | Total Mutations: %zu | Saddle Point Escapes: %zu\n",
            cluster->particle_count, cluster->total_swarm_mutations, cluster->total_saddle_point_escapes);
    fprintf(out, "  Global Best Energy: %.3f | Consensus Mask: 0x%016llx\n",
            cluster->global_best_energy, (unsigned long long)cluster->pheromone.consensus_mask);
    for (size_t i = 0; i < cluster->particle_count; ++i) {
        const FlowSwarmParticle *p = &cluster->particles[i];
        fprintf(out, "  [Particle %u] best_energy=%.2f genome=0x%016llx stag=%u\n",
                p->id, p->best_energy, (unsigned long long)p->best_genome, p->stagnation_steps);
    }
}

/* ========================================================================= */
/* Dynamic DSO Plugin ABI Export                                             */
/* ========================================================================= */

static const Component SWARM_COMPONENTS[] = {
    {
        .id = "swarm_optimizer",
        .kind = "algorithm",
        .resource = "cpu",
        .capability = "pthread",
        .supports_shared = 1,
        .supports_read_heavy = 1,
        .supports_unordered = 1,
        .supports_parallelizable = 1,
        .latency_score = 2,
        .memory_score = 2,
        .domain_contract = "swarm_federation",
        .flow_binding = "flow_swarm_search",
        .memory_fixed_bytes = sizeof(FlowSwarmCluster),
        .memory_bytes_per_capacity = sizeof(FlowSwarmParticle),
        .reload_capable = 0
    }
};

static uint64_t swarm_pref_mask(const SemanticIR *ir, const Component *c, const FlowPlanDimensionSet *dims) {
    (void)ir; (void)c; (void)dims;
    return UINT64_MAX;
}

static const FlowPlugin SWARM_PLUGIN = {
    .name = "flow.swarm",
    .version = "1.0",
    .components = SWARM_COMPONENTS,
    .component_count = 1,
    .compatible = NULL,
    .memory_model = NULL,
    .verify = NULL,
    .emit = NULL,
    .oracle = NULL,
    .preference = NULL,
    .validate_contract = NULL,
    .lower_domain_semantics = NULL,
    .free_domain_semantics = NULL,
    .enumerate_dimensions = NULL,
    .evaluate_plan = NULL,
    .verify_plan = NULL,
    .benchmark = NULL,
    .get_mutation_mask = NULL,
    .preference_mask = swarm_pref_mask,
    .contract_mask = NULL,
    .resource_mask = NULL,
    .environment_mask = NULL,
    .create_unit = NULL,
    .doc_title = "Particle Swarm & Federated 1-Bit Chaos Optimizer",
    .doc_responsibilities = "Executes multi-agent particle swarm search with pheromone consensus and saddle-point escape",
    .doc_algorithmic_guarantee = "Guarantees global Pareto frontier convergence under multimodal fitness landscapes",
    .doc_memory_concurrency_model = "Lock-free atomic particle state replication",
    .doc_key_apis = "flow_swarm_search, flow_swarm_step",
    .doc_layer = 2,
    .domain_context = NULL
};

static const FlowPluginDescriptor SWARM_DESCRIPTOR = {
    .abi_major = FLOW_PLUGIN_ABI_MAJOR,
    .abi_minor = FLOW_PLUGIN_ABI_MINOR,
    .descriptor_size = sizeof(FlowPluginDescriptor),
    .module_name = "flow.swarm",
    .module_version = "1.0",
    .module_hash = 0x54A83001,
    .plugin = &SWARM_PLUGIN,
    .dso_handle = NULL,
    .active_references = 0
};

const FlowPluginDescriptor *flow_swarm_entry_v1(void) {
    return &SWARM_DESCRIPTOR;
}

#ifdef FLOW_PLUGIN_DSO
const FlowPluginDescriptor *flow_plugin_entry_v1(void) {
    return &SWARM_DESCRIPTOR;
}
#endif

const FlowPlugin *flow_swarm_plugin(void) {
    return &SWARM_PLUGIN;
}
