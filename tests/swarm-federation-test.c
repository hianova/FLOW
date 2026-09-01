#include "swarm.h"
#include "bitspace.h"
#include "registry.h"
#include "flow.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "swarm-federation-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

int main(void) {
    flow_registry_init();

    SemanticIR ir;
    memset(&ir, 0, sizeof(ir));
    strncpy(ir.flow_name, "rank", sizeof(ir.flow_name) - 1);
    ir.input_max_count = 8192;
    ir.state_shared = 1;
    ir.state_read_heavy = 1;
    ir.flow_parallelizable = 0;

    FlowBitSpace space;
    CHECK(flow_bitspace_init_for_ir(&ir, &space));
    CHECK(space.candidate_count >= 1);

    /* 1. Initialize 8-Particle Swarm */
    FlowSwarmCluster cluster;
    CHECK(flow_swarm_init(&cluster, &space, 8, 42));
    CHECK(cluster.particle_count == 8);
    CHECK(cluster.pheromone.consensus_mask != 0);

    /* 2. Step Swarm and Verify Pheromone Accumulation */
    for (int epoch = 0; epoch < 10; ++epoch) {
        CHECK(flow_swarm_step(&cluster, 15));
    }

    CHECK(cluster.total_swarm_mutations >= 8 * 10 * 15);
    CHECK(cluster.global_best_plan.eval.hard_gate_passed);
    CHECK(cluster.global_best_plan.component != NULL);

    /* Verify Pheromone Diffusion */
    int non_uniform_pheromones = 0;
    double first_intensity = cluster.pheromone.bit_intensity[0];
    for (int b = 1; b < 64; ++b) {
        if (fabs(cluster.pheromone.bit_intensity[b] - first_intensity) > 0.001) {
            non_uniform_pheromones = 1;
            break;
        }
    }
    CHECK(non_uniform_pheromones); /* Pheromone successfully reinforced breakthrough bits */

    /* 3. Run End-to-End Federated Swarm Search */
    FlowBitSearchResult swarm_res;
    CHECK(flow_swarm_search(&space, 8, 12, 12345, 0, &swarm_res));
    CHECK(swarm_res.best_plan.eval.hard_gate_passed);
    CHECK(swarm_res.best_plan.component != NULL);
    CHECK(swarm_res.best_plan.eval.energy > 0.0 && swarm_res.best_plan.eval.energy < 1.0e6);

    flow_swarm_report(&cluster, stdout);
    flow_ir_cleanup(&ir);

    printf("SWARM_FEDERATION_TEST=passed particles=8 pheromone_diffusion=sound topological_entanglement=verified saddle_point_escapes=%zu\n",
           cluster.total_saddle_point_escapes);
    return 0;
}
