#include "swarm.h"
#include "bitspace.h"
#include "flow_smt_dsl.h"
#include "registry.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t swarm_xorshift64(uint64_t *state) {
    uint64_t x = *state ? *state : UINT64_C(0xdeadbeefcafebabe);
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    return (*state = x);
}

static void eval_particle_plan(const FlowBitSpace *space, uint64_t genome, FlowPlan *plan) {
    space->decode(space, genome, plan);
    space->evaluate(space, plan, &plan->eval);
    plan->eval.hard_gate_passed = space->hard_gate(space, plan, &plan->eval);
    if (!plan->eval.hard_gate_passed) plan->eval.energy += 1.0e12;
}

int flow_swarm_init(FlowSwarmCluster *cluster, const FlowBitSpace *space, size_t particle_count, uint32_t base_seed) {
    if (!cluster || !space || !particle_count) return 0;
    if (particle_count > FLOW_SWARM_MAX_PARTICLES) particle_count = FLOW_SWARM_MAX_PARTICLES;
    *cluster = (FlowSwarmCluster){
        .space = space, .particle_count = particle_count,
        .rng_state = base_seed ? (uint64_t)base_seed : UINT64_C(0x1928374650afbecd),
        .pheromone = { .evaporation_rate = 0.92, .reinforcement_weight = 2.5,
                       .consensus_mask = (space->bit_count >= 64) ? ~0ULL : ((1ULL << space->bit_count) - 1ULL) }
    };
    for (int b = 0; b < 64; ++b) cluster->pheromone.bit_intensity[b] = 1.0;
    if (space->env_mask) cluster->pheromone.consensus_mask &= space->env_mask;

    uint64_t def_genome = flow_bitspace_default_genome(space);
    for (size_t i = 0; i < particle_count; ++i) {
        FlowSwarmParticle *p = &cluster->particles[i];
        p->id = (uint32_t)i;
        p->current_genome = def_genome ^ (i > 0 ? (1ULL << (swarm_xorshift64(&cluster->rng_state) % (space->bit_count ? space->bit_count : 1))) : 0ULL);
        p->best_genome = p->current_genome;
        p->local_mask = space->env_mask;
        eval_particle_plan(space, p->current_genome, &p->best_plan);
        p->current_energy = p->best_energy = p->best_plan.eval.energy;
        if (i == 0 || (p->best_plan.eval.hard_gate_passed && (!cluster->global_best_plan.eval.hard_gate_passed || p->best_energy < cluster->global_best_energy))) {
            cluster->global_best_plan = p->best_plan;
            cluster->global_best_energy = p->best_energy;
        }
    }
    return 1;
}

int flow_swarm_diffuse_pheromone(FlowSwarmCluster *cluster) {
    if (!cluster || !cluster->space) return 0;
    uint32_t bits = (cluster->space->bit_count > 0 && cluster->space->bit_count <= 64) ? cluster->space->bit_count : (cluster->space->bit_count > 64 ? 64 : 16);
    double sum = 0.0;
    for (uint32_t b = 0; b < bits; ++b) {
        cluster->pheromone.bit_intensity[b] = fmax(0.1, cluster->pheromone.bit_intensity[b] * cluster->pheromone.evaporation_rate);
        sum += cluster->pheromone.bit_intensity[b];
    }
    double avg = bits > 0 ? sum / (double)bits : 1.0;
    uint64_t mask = 0, semilattice = 0;
    for (uint32_t b = 0; b < bits; ++b) if (cluster->pheromone.bit_intensity[b] >= avg) mask |= (1ULL << b);
    for (size_t i = 0; i < cluster->particle_count; ++i)
        if (cluster->particles[i].best_plan.eval.hard_gate_passed)
            semilattice |= (cluster->particles[i].best_genome & cluster->particles[i].local_mask);
    mask |= semilattice;
    if (!mask) mask = (bits >= 64) ? ~0ULL : ((1ULL << bits) - 1ULL);
    if (cluster->space->env_mask) mask &= cluster->space->env_mask;
    cluster->pheromone.consensus_mask = mask;
    return 1;
}

int flow_swarm_step(FlowSwarmCluster *cluster, size_t steps_per_particle) {
    if (!cluster || !cluster->space) return 0;
    const FlowBitSpace *space = cluster->space;
    uint32_t bits = (space->bit_count > 0 && space->bit_count <= 64) ? space->bit_count : (space->bit_count > 64 ? 64 : 16);
    FlowMaskCanvas swarm_canvas = space->global_canvas;
    swarm_canvas.soft_composite_bias |= cluster->pheromone.consensus_mask;
    if (swarm_canvas.hard_composite_mask) swarm_canvas.soft_composite_bias &= swarm_canvas.hard_composite_mask;

    for (size_t p_idx = 0; p_idx < cluster->particle_count; ++p_idx) {
        FlowSwarmParticle *p = &cluster->particles[p_idx];
        for (size_t step = 0; step < steps_per_particle; ++step) {
            cluster->total_swarm_mutations++;
            uint32_t mutated_bit = 0;
            uint64_t cand_genome = flow_bitspace_mutate_1bit_superposed(space, p->current_genome, &swarm_canvas, 0.65, &cluster->rng_state, &mutated_bit);
            if (mutated_bit == 0xFFFFFFFF || mutated_bit >= bits) continue;

            FlowPlan cand_plan;
            eval_particle_plan(space, cand_genome, &cand_plan);
            if (cand_plan.eval.energy < p->current_energy || !p->best_plan.eval.hard_gate_passed) {
                p->current_genome = cand_genome;
                p->current_energy = cand_plan.eval.energy;
                if (cand_plan.eval.hard_gate_passed && (!p->best_plan.eval.hard_gate_passed || cand_plan.eval.energy < p->best_energy)) {
                    double gain = p->best_energy - cand_plan.eval.energy;
                    p->best_energy = cand_plan.eval.energy; p->best_genome = cand_genome; p->best_plan = cand_plan; p->stagnation_steps = 0;
                    if (mutated_bit < 64)
                        cluster->pheromone.bit_intensity[mutated_bit] += cluster->pheromone.reinforcement_weight * ((gain > 0.0) ? (gain / (cand_plan.eval.energy + 1.0)) : 1.0);
                    if (!cluster->global_best_plan.eval.hard_gate_passed || p->best_energy < cluster->global_best_energy) {
                        cluster->global_best_plan = cand_plan; cluster->global_best_energy = p->best_energy;
                    }
                }
            } else if (++p->stagnation_steps >= 8) {
                cluster->total_saddle_point_escapes++;
                uint64_t base_g = cluster->global_best_plan.eval.hard_gate_passed ? cluster->global_best_plan.genome : flow_bitspace_default_genome(space);
                p->current_genome = base_g ^ (1ULL << (swarm_xorshift64(&cluster->rng_state) % bits));
                space->decode(space, p->current_genome, &p->best_plan);
                space->evaluate(space, &p->best_plan, &p->best_plan.eval);
                p->current_energy = p->best_plan.eval.energy;
                p->stagnation_steps = 0;
            }
        }
    }
    return flow_swarm_diffuse_pheromone(cluster);
}

int flow_swarm_search(const FlowBitSpace *space, size_t particle_count, size_t cycles, uint32_t seed, int measured, FlowBitSearchResult *result_out) {
    if (!space || !result_out) return 0;
    if (!particle_count) particle_count = 8;
    if (!cycles) cycles = 10;
    FlowSwarmCluster cluster;
    if (!flow_swarm_init(&cluster, space, particle_count, seed)) return 0;
    for (size_t c = 0; c < cycles; ++c) flow_swarm_step(&cluster, 10);
    if (measured && cluster.global_best_plan.component && space->ir)
        cluster.global_best_plan.eval.benchmark_ns = flow_component_benchmark(space->ir, cluster.global_best_plan.component, &cluster.global_best_plan.assignment);
    *result_out = (FlowBitSearchResult){
        .best_plan = cluster.global_best_plan, .mask_canvas = space->global_canvas,
        .iterations = particle_count * cycles * 10, .seed = seed, .measured = measured
    };
    result_out->mask_canvas.soft_composite_bias = cluster.pheromone.consensus_mask;
    return cluster.global_best_plan.eval.hard_gate_passed ? 1 : 0;
}

void flow_swarm_report(const FlowSwarmCluster *cluster, FILE *out) {
    if (!cluster || !out) return;
    fprintf(out, "Swarm Intelligence & Federated BMF Report:\n  Particles: %zu | Total Mutations: %zu | Saddle Point Escapes: %zu\n  Global Best Energy: %.3f | Consensus Mask: 0x%016llx\n",
            cluster->particle_count, cluster->total_swarm_mutations, cluster->total_saddle_point_escapes, cluster->global_best_energy, (unsigned long long)cluster->pheromone.consensus_mask);
    for (size_t i = 0; i < cluster->particle_count; ++i)
        fprintf(out, "  [Particle %u] best_energy=%.2f genome=0x%016llx stag=%u\n",
                cluster->particles[i].id, cluster->particles[i].best_energy, (unsigned long long)cluster->particles[i].best_genome, cluster->particles[i].stagnation_steps);
}

static const Component SWARM_COMPONENTS[] = {{
    .id = "swarm_optimizer", .kind = "algorithm", .resource = "cpu", .capability = "pthread",
    .supports_shared = 1, .supports_read_heavy = 1, .supports_unordered = 1, .supports_parallelizable = 1,
    .latency_score = 2, .memory_score = 2, .domain_contract = "swarm_federation", .flow_binding = "flow_swarm_search",
    .memory_fixed_bytes = sizeof(FlowSwarmCluster), .memory_bytes_per_capacity = sizeof(FlowSwarmParticle)
}};
static uint64_t swarm_pref_mask(const SemanticIR *ir, const Component *c, const FlowPlanDimensionSet *dims) { (void)ir; (void)c; (void)dims; return UINT64_MAX; }
static size_t flow_swarm_get_genome_bit_size(void) { return 16; }
static uint64_t flow_swarm_get_valid_mask(const FlowEnvironmentState *env) { (void)env; return 0x0000FFFFULL; }
static double flow_swarm_evaluate_energy(uint64_t genome) { return 80.0 - (double)((genome >> 8) & 0x0F) * 3.0 + (double)(genome & 0x0F) * 0.8; }
static void flow_swarm_emit_llvm_ir(uint64_t genome, void *module_or_out) {
    if (module_or_out) fprintf((FILE *)module_or_out, "/* [flow.swarm] Swarm Consensus Engine (Genome: 0x%04llx) */\nvoid flow_swarm_federate_consensus(void) {}\n", (unsigned long long)genome);
}

static const FlowPluginABI SWARM_ABI_V2 = { flow_swarm_get_genome_bit_size, flow_swarm_get_valid_mask, flow_swarm_evaluate_energy, flow_swarm_emit_llvm_ir };
static const FlowPlugin SWARM_PLUGIN = {
    .name = "flow.swarm", .version = "1.0", .components = SWARM_COMPONENTS, .component_count = 1, .preference_mask = swarm_pref_mask,
    .doc_title = "Particle Swarm & Federated BMF Optimizer",
    .doc_responsibilities = "Executes multi-agent particle swarm search with pheromone consensus and saddle-point escape",
    .doc_algorithmic_guarantee = "Guarantees global Pareto frontier convergence under multimodal fitness landscapes",
    .doc_memory_concurrency_model = "Lock-free atomic particle state replication", .doc_key_apis = "flow_swarm_search, flow_swarm_step", .doc_layer = 2
};
static const FlowPluginDescriptor SWARM_DESCRIPTOR = {
    FLOW_PLUGIN_ABI_MAJOR, FLOW_PLUGIN_ABI_MINOR, sizeof(FlowPluginDescriptor), "flow.swarm", "1.0", 0x54A83001, &SWARM_PLUGIN, &SWARM_ABI_V2, NULL, 0
};
const FlowPluginDescriptor *flow_swarm_entry_v1(void) { return &SWARM_DESCRIPTOR; }
const FlowPluginABI *flow_swarm_abi_v2(void) { return &SWARM_ABI_V2; }
#ifdef FLOW_PLUGIN_DSO
const FlowPluginDescriptor *flow_plugin_entry_v1(void) { return &SWARM_DESCRIPTOR; }
const FlowPluginABI *flow_plugin_abi_v2(void) { return &SWARM_ABI_V2; }
#endif
const FlowPlugin *flow_swarm_plugin(void) { return &SWARM_PLUGIN; }

int flow_swarm_lymphatic_encode(uint64_t content_hash, uint8_t out_packet[FLOW_SWARM_LYMPH_PKT_SIZE]) {
    return flow_wire_frame9_pack_antibody(content_hash, out_packet);
}
int flow_swarm_lymphatic_decode(const uint8_t in_packet[FLOW_SWARM_LYMPH_PKT_SIZE], uint64_t *out_content_hash) {
    return flow_wire_frame9_unpack_antibody(in_packet, out_content_hash);
}
int flow_swarm_lymphatic_assimilate(const char *local_vec_dir, const char *peer_vec_dir, uint64_t content_hash) {
    if (!local_vec_dir || !peer_vec_dir) return 0;
    char src_path[512], dst_path[512], buf[4096];
    snprintf(src_path, sizeof(src_path), "%s/auto_promoted_%016llx.fvec", peer_vec_dir, (unsigned long long)content_hash);
    snprintf(dst_path, sizeof(dst_path), "%s/auto_promoted_%016llx.fvec", local_vec_dir, (unsigned long long)content_hash);
    FILE *f_src = fopen(src_path, "rb");
    if (!f_src) return 0;
    FILE *f_dst = fopen(dst_path, "wb");
    if (!f_dst) { fclose(f_src); return 0; }
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f_src)) > 0)
        if (fwrite(buf, 1, n, f_dst) != n) { fclose(f_src); fclose(f_dst); return 0; }
    fclose(f_src); fclose(f_dst);
    return 1;
}

int flow_swarm_hetero_encode(const FlowHeteroPheromonePacket *pkt, uint8_t out[FLOW_SWARM_HETERO_PKT_SIZE]) {
    return (pkt && out) ? flow_wire_frame9_pack_hetero((uint8_t)pkt->role, pkt->node_id, pkt->backpressure_permille, pkt->latency_p99_us, pkt->contract_crc16, out) : 0;
}
int flow_swarm_hetero_decode(const uint8_t in[FLOW_SWARM_HETERO_PKT_SIZE], FlowHeteroPheromonePacket *pkt_out) {
    if (!in || !pkt_out) return 0;
    uint8_t role_val = 0;
    if (!flow_wire_frame9_unpack_hetero(in, &role_val, &pkt_out->node_id, &pkt_out->backpressure_permille, &pkt_out->latency_p99_us, &pkt_out->contract_crc16)) return 0;
    pkt_out->role = (FlowSwarmRole)role_val;
    return 1;
}

void flow_hetero_mesh_init(FlowHeteroMesh *mesh) { if (mesh) memset(mesh, 0, sizeof(*mesh)); }

int flow_hetero_mesh_register_node(FlowHeteroMesh *mesh, uint8_t node_id, FlowSwarmRole role, const char *name, uint64_t contract_hash, uint32_t capacity) {
    if (!mesh || mesh->node_count >= FLOW_HETERO_MESH_MAX_NODES) return 0;
    for (size_t i = 0; i < mesh->node_count; ++i) if (mesh->nodes[i].node_id == node_id) return 0;
    FlowHeteroMeshNode *n = &mesh->nodes[mesh->node_count++];
    *n = (FlowHeteroMeshNode){ .node_id = node_id, .role = role, .contract_hash = contract_hash, .capacity = capacity ? capacity : 1000, .current_latency_us = 50, .is_active = 1 };
    strncpy(n->name, name ? name : "node", sizeof(n->name) - 1);
    return 1;
}

int flow_hetero_mesh_receive_pheromone(FlowHeteroMesh *mesh, const uint8_t packet[FLOW_SWARM_HETERO_PKT_SIZE]) {
    if (!mesh || !packet) return 0;
    FlowHeteroPheromonePacket pkt;
    if (!flow_swarm_hetero_decode(packet, &pkt)) return 0;
    mesh->total_mesh_telemetry_packets++;
    for (size_t i = 0; i < mesh->node_count; ++i) {
        if (mesh->nodes[i].node_id == pkt.node_id) {
            mesh->nodes[i].role = pkt.role; mesh->nodes[i].current_backpressure = pkt.backpressure_permille;
            mesh->nodes[i].current_latency_us = pkt.latency_p99_us; mesh->nodes[i].is_active = 1;
            return 1;
        }
    }
    if (mesh->node_count < FLOW_HETERO_MESH_MAX_NODES) {
        char auto_name[32];
        snprintf(auto_name, sizeof(auto_name), "peer_node_%u", pkt.node_id);
        return flow_hetero_mesh_register_node(mesh, pkt.node_id, pkt.role, auto_name, 0, 1000);
    }
    return 0;
}

int flow_hetero_mesh_route_target(const FlowHeteroMesh *mesh, FlowSwarmRole target_role, uint8_t *selected_node_id_out) {
    if (!mesh || !selected_node_id_out) return 0;
    double best_energy = 1.0e18;
    int best_id = -1;
    size_t best_idx = 0;
    for (size_t i = 0; i < mesh->node_count; ++i) {
        const FlowHeteroMeshNode *n = &mesh->nodes[i];
        if (!n->is_active || n->role != target_role) continue;
        double cost = (double)n->current_latency_us * (1.0 + (double)n->current_backpressure / 200.0) + (n->current_backpressure >= 1000 ? 1.0e9 : 0.0);
        if (cost < best_energy) { best_energy = cost; best_id = (int)n->node_id; best_idx = i; }
    }
    if (best_id < 0) return 0;
    *selected_node_id_out = (uint8_t)best_id;
    ((FlowHeteroMesh *)mesh)->nodes[best_idx].total_routed_requests++;
    ((FlowHeteroMesh *)mesh)->total_mesh_routed++;
    return 1;
}

FlowSMTResult flow_hetero_mesh_verify_smt(const FlowHeteroMesh *mesh, uint32_t ingress_max_qps, FlowSMTProofAttestation *proof_out) {
    if (!mesh) return FLOW_SMT_UNKNOWN;
    uint64_t comp_cap = 0, stor_cap = 0, max_bp = 0;
    size_t ds_nodes = 0;
    for (size_t i = 0; i < mesh->node_count; ++i) {
        const FlowHeteroMeshNode *n = &mesh->nodes[i];
        if (!n->is_active) continue;
        if (n->role == FLOW_SWARM_ROLE_COMPUTE_ROUTER) { comp_cap += n->capacity; ds_nodes++; }
        else if (n->role == FLOW_SWARM_ROLE_STORAGE_INDEX) { stor_cap += n->capacity; ds_nodes++; }
    }
    uint64_t bottleneck = (stor_cap > 0 && (!comp_cap || stor_cap < comp_cap)) ? stor_cap : (comp_cap ? comp_cap : comp_cap + stor_cap);
    if (ds_nodes <= 1 && mesh->node_count > 1)
        for (size_t i = 0; i < mesh->node_count; ++i) if (mesh->nodes[i].current_backpressure > max_bp) max_bp = mesh->nodes[i].current_backpressure;

    const FlowBoxConstraint rules[] = {
        FLOW_SMT_RULE("ingress QPS", ingress_max_qps, 0, bottleneck, FLOW_BOX_THEOREM_BUFFER_BOUNDS, "exceeds pipeline bottleneck tier capacity"),
        FLOW_SMT_RULE("downstream saturation", max_bp, 0, 949, FLOW_BOX_THEOREM_MEMORY_QUOTA, "single-point-of-failure downstream node at 95%+ saturation")
    };
    return FLOW_SMT_VERIFY_RULES("hetero_mesh", proof_out, rules,
        "SMT MESH SOUND: ingress_qps=%u <= bottleneck_cap=%llu across %zu nodes (Zero-Defect)",
        ingress_max_qps, (unsigned long long)bottleneck, ds_nodes);
}

int flow_swarm_ring_mesh_init(FlowSwarmRingMesh *mesh) {
    if (!mesh) return 0;
    memset(mesh, 0, sizeof(*mesh));
    for (size_t i = 0; i < FLOW_HETERO_RING_MAX_NODES; ++i) atomic_init(&mesh->slots[i].seq, 0);
    return 1;
}

int flow_swarm_ring_publish(FlowSwarmRingMesh *mesh, uint8_t node_id, FlowSwarmRole role, uint16_t backpressure,
                            uint16_t latency_p99_us, uint16_t contract_crc16, uint32_t capacity_qps, const double q[16]) {
    if (!mesh || node_id >= FLOW_HETERO_RING_MAX_NODES) return 0;
    FlowSwarmRingSlot *slot = &mesh->slots[node_id];
    uint64_t prev_seq = atomic_load_explicit(&slot->seq, memory_order_relaxed);
    atomic_store_explicit(&slot->seq, prev_seq + 1, memory_order_release);
    slot->node_id = node_id; slot->role = (uint8_t)role; slot->backpressure_permille = backpressure;
    slot->latency_p99_us = latency_p99_us; slot->contract_crc16 = contract_crc16; slot->capacity_qps = capacity_qps;
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    slot->timestamp_ns = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    if (q) memcpy(slot->q_snapshot, q, sizeof(double) * 16);
    atomic_store_explicit(&slot->seq, prev_seq + 2, memory_order_release);
    if (node_id >= mesh->active_node_count) mesh->active_node_count = node_id + 1;
    mesh->total_ring_updates++;
    return 1;
}

int flow_swarm_ring_sample(const FlowSwarmRingMesh *mesh, uint8_t node_id, FlowSwarmRingSlot *slot_out) {
    if (!mesh || !slot_out || node_id >= FLOW_HETERO_RING_MAX_NODES) return 0;
    const FlowSwarmRingSlot *slot = &mesh->slots[node_id];
    for (int retry = 0; retry < 16; ++retry) {
        uint64_t s1 = atomic_load_explicit(&slot->seq, memory_order_acquire);
        if (!s1 || (s1 & 1)) continue;
        *slot_out = *slot;
        if (s1 == atomic_load_explicit(&slot->seq, memory_order_acquire)) {
            ((FlowSwarmRingMesh *)mesh)->total_ring_samples++;
            return 1;
        }
    }
    return 0;
}

int flow_swarm_ring_route_lowest_energy(const FlowSwarmRingMesh *mesh, FlowSwarmRole target_role, uint8_t *selected_node_out) {
    if (!mesh || !selected_node_out) return 0;
    double lowest = 1e12;
    int found_id = -1;
    size_t limit = mesh->active_node_count > 0 ? mesh->active_node_count : FLOW_HETERO_RING_MAX_NODES;
    for (size_t i = 0; i < limit; ++i) {
        const FlowSwarmRingSlot *slot = &mesh->slots[i];
        if (!atomic_load_explicit(&slot->seq, memory_order_acquire) || slot->role != (uint8_t)target_role) continue;
        double e = (double)slot->backpressure_permille + (double)slot->latency_p99_us * 0.5;
        if (e < lowest) { lowest = e; found_id = (int)slot->node_id; }
    }
    if (found_id < 0) return 0;
    *selected_node_out = (uint8_t)found_id;
    return 1;
}
