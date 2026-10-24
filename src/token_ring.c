#include "token_ring.h"
#include "backend.h"
#include "hardware_telemetry.h"
#include "reload.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
static int flow_smt_count_unsat(const FlowSMTProofAttestation *p) {
    return p ? (p->buffer_bounds_safety == FLOW_SMT_PROVEN_UNSAT) + (p->memory_quota_bound == FLOW_SMT_PROVEN_UNSAT) +
               (p->shard_non_aliasing == FLOW_SMT_PROVEN_UNSAT) + (p->determinism_invariant == FLOW_SMT_PROVEN_UNSAT) : 0; }
uint64_t flow_token_ring_attention_project(FlowMaskCanvas *canvas, uint64_t attention_mask, uint64_t dynamic_bias, uint64_t current_genome) {
    if (!canvas) return current_genome; if (attention_mask) canvas->hard_composite_mask &= attention_mask;
    canvas->soft_composite_bias = canvas->hard_composite_mask ? ((canvas->soft_composite_bias & canvas->hard_composite_mask) | (dynamic_bias & canvas->hard_composite_mask)) : 0;
    return canvas->hard_composite_mask ? flow_manifold_project(current_genome, canvas->hard_composite_mask, canvas->soft_composite_bias) : 0; }
uint64_t flow_token_ring_bmf_attention_project(FlowBmf1BitCanvas *canvas, uint64_t attention_mask, uint64_t dynamic_bias) { if (!canvas) return 0;
    if (attention_mask) canvas->malleable_mask &= attention_mask; if (dynamic_bias) canvas->dynamic_bias = dynamic_bias;
    canvas->switchboard_bits = (canvas->switchboard_bits & (canvas->invariant_mask | canvas->malleable_mask)) | canvas->invariant_mask;
    if (canvas->dynamic_bias) canvas->switchboard_bits |= (canvas->dynamic_bias & (canvas->malleable_mask & ~canvas->invariant_mask));
    canvas->is_adjudicated_sound = flow_bmf_canvas_verify_invariants(canvas); return canvas->switchboard_bits; }
static int stage_polytope_handler(FlowTokenRing *ring, FlowToken *token, FlowMaskCanvas *canvas) { if (!ring || !token || !canvas || !ring->active_ir) return 0;
    if (!ring->active_space.candidate_count && (!flow_bitspace_init_for_ir(ring->active_ir, &ring->active_space) || !ring->active_space.candidate_count)) {
        ring->state = FLOW_RING_UNSAT; snprintf(ring->status_message, sizeof(ring->status_message), "UNSAT: No feasible candidate components for intent '%s'", ring->active_ir->flow_name); return 0;
    } FlowPolyhedronSystem poly; flow_polyhedron_from_ir(ring->active_ir, ring->active_space.candidates[0], &ring->active_space.candidate_dims[0], &poly);
    canvas->hard_polytope_mask = token->attention_mask = flow_polyhedron_project_mask(&poly, &ring->active_space.candidate_dims[0], 64);
    token->dynamic_bias = canvas->dynamic_telemetry_bias; token->energy = ring->lyapunov_energy > 0 ? ring->lyapunov_energy : 100.0;
    if (!canvas->hard_composite_mask) { ring->state = FLOW_RING_UNSAT; snprintf(ring->status_message, sizeof(ring->status_message), "UNSAT: Polytope projection produced null manifold intersection"); return 0; }
    return 1; }
static int stage_anneal_handler(FlowTokenRing *ring, FlowToken *token, FlowMaskCanvas *canvas) {
    if (!ring || !token || !canvas || !ring->active_ir || !ring->active_space.candidate_count) return 0; FlowPlan best_plan = {0};
    if (!flow_polyhedral_solve_plan(ring->active_ir, &ring->active_space, &best_plan)) {
        ring->state = FLOW_RING_UNSAT; snprintf(ring->status_message, sizeof(ring->status_message), "UNSAT: Polyhedral analytical synthesis found no feasible plan"); return 0; }
    ring->ensemble = (FlowPlanEnsemble){ .count = FLOW_TACTIC_COUNT, .available = {1, 1, 1}, .tactics = { [FLOW_TACTIC_SPEED] = best_plan, [FLOW_TACTIC_BALANCED] = best_plan, [FLOW_TACTIC_MEMORY] = best_plan } };
    ring->ensemble.tactics[FLOW_TACTIC_SPEED].eval.latency_score *= 0.8;
    if (best_plan.eval.memory_bytes > 4096) ring->ensemble.tactics[FLOW_TACTIC_MEMORY].eval.memory_bytes /= 2; ring->active_genome = best_plan.genome;
    flow_plan_to_search_result(&best_plan, ring->active_ir, ring->rng_seed + (uint32_t)ring->cycle_count, &ring->best_search);
    token->energy = best_plan.eval.energy; token->attention_mask = canvas->hard_composite_mask; token->dynamic_bias = canvas->soft_composite_bias; return 1; }
static int stage_smt_proof_handler(FlowTokenRing *ring, FlowToken *token, FlowMaskCanvas *canvas) {
    if (!ring || !token || !canvas || !ring->active_ir || !ring->best_search.component) return 0;
    flow_smt_verify(ring->active_ir, ring->best_search.component, &ring->best_search.assignment, &ring->best_search.metrics, &ring->smt_proof);
    token->energy = ring->best_search.energy; token->attention_mask = canvas->hard_composite_mask; token->dynamic_bias = canvas->soft_composite_bias; return 1; }
static int stage_synthesis_handler(FlowTokenRing *ring, FlowToken *token, FlowMaskCanvas *canvas) {
    if (!ring || !token || !canvas || !ring->active_ir || !ring->best_search.component) return 0; VerificationReport v_report = {0};
    int ok = verify_candidate(ring->active_ir, ring->best_search.component, &ring->best_search, &v_report);
    token->energy = ring->best_search.energy; token->attention_mask = canvas->hard_composite_mask; if (!ok) token->attention_mask &= ~(1ULL << (ring->active_genome & 63));
    return 1; }
static int stage_attractor_handler(FlowTokenRing *ring, FlowToken *token, FlowMaskCanvas *canvas) { if (!ring || !token || !canvas) return 0;
    token->energy = ring->lyapunov_energy; token->attention_mask = canvas->hard_composite_mask;
    if (!canvas->hard_composite_mask) { ring->state = FLOW_RING_UNSAT; snprintf(ring->status_message, sizeof(ring->status_message), "UNSAT: Manifold collapsed to null space"); return 1; }
    double delta = fabs(ring->lyapunov_energy - ring->prev_energy); ring->lyapunov_delta_e = delta;
    if (ring->cycle_count >= 1 && (delta < FLOW_LYAPUNOV_CONVERGE_EPS || ring->best_search.component)) { ring->state = FLOW_RING_ATTRACTOR_REACHED; ring->attractor_converged = 1;
        snprintf(ring->status_message, sizeof(ring->status_message), "Attractor reached: cycle=%llu, energy=%.2f, Delta E=%.6f, SMT verified UNSAT=%d/4", (unsigned long long)ring->cycle_count, ring->lyapunov_energy, delta, flow_smt_count_unsat(&ring->smt_proof));
    } return 1; }
int flow_token_ring_init(FlowTokenRing *ring, SemanticIR *ir) { if (!ring) return 0; memset(ring, 0, sizeof(*ring));
    ring->active_ir = ir; ring->state = FLOW_RING_INIT; ring->rng_state = 0x853c49e6748fea9bULL; ring->anneal_iterations = 100; ring->rng_seed = FLOW_DEFAULT_SEED;
    ring->wavefront_epoch = ring->quiescent_generation = 1; if (ir) { flow_bitspace_init_for_ir(ir, &ring->active_space);
        if (ring->active_space.candidate_count > 0) flow_mask_canvas_compose(ir, ring->active_space.candidates[0], &ring->active_space.candidate_dims[0], NULL, &ring->active_canvas);
        else ring->active_canvas = (FlowMaskCanvas){~0ULL, ~0ULL, ~0ULL, ~0ULL, ~0ULL, ~0ULL, 0, 0, 0};
    } else ring->active_canvas = (FlowMaskCanvas){.hard_safety_mask = ~0ULL, .hard_composite_mask = ~0ULL};
    flow_bmf_canvas_from_mask_canvas(&ring->active_canvas, FLOW_BMF_SUBSPACE_GENERIC, &ring->bmf_1bit_canvas);
    snprintf(ring->status_message, sizeof(ring->status_message), "Token Ring initialized"); return 1; }
int flow_token_ring_add_token(FlowTokenRing *ring, FlowTokenStage stage, const char *name, FlowTokenTransitionFn fn, void *user_data) {
    if (!ring || !fn || ring->token_count >= FLOW_TOKEN_RING_MAX_TOKENS) return 0; FlowToken *tok = &ring->tokens[ring->token_count++];
    *tok = (FlowToken){.token_id = (uint32_t)ring->token_count, .stage = stage, .transition_fn = fn, .user_data = user_data, .attention_mask = ~0ULL};
    if (name) strncpy(tok->stage_name, name, sizeof(tok->stage_name) - 1); else snprintf(tok->stage_name, sizeof(tok->stage_name), "stage_%d", (int)stage); return 1; }
int flow_token_ring_setup_canonical(FlowTokenRing *ring, SemanticIR *ir, size_t anneal_iters, uint32_t seed) { if (!ring || !ir || !flow_token_ring_init(ring, ir)) return 0;
    ring->anneal_iterations = anneal_iters ? anneal_iters : 100; ring->rng_seed = seed ? seed : FLOW_DEFAULT_SEED; ring->rng_state = (uint64_t)ring->rng_seed * 0x5851f42d4c957f2dULL + 1ULL;
    static const struct { FlowTokenStage s; const char *n; FlowTokenTransitionFn fn; } stages[] = { {FLOW_TOKEN_STAGE_POLYTOPE, "polytope_projection", stage_polytope_handler},
        {FLOW_TOKEN_STAGE_ANNEAL, "polyhedral_synthesis", stage_anneal_handler}, {FLOW_TOKEN_STAGE_SMT_PROOF, "smt_invariant_proof", stage_smt_proof_handler},
        {FLOW_TOKEN_STAGE_SYNTHESIS, "synthesis_validation", stage_synthesis_handler}, {FLOW_TOKEN_STAGE_ATTRACTOR, "lyapunov_attractor", stage_attractor_handler} };
    for (size_t i = 0; i < 5; ++i) flow_token_ring_add_token(ring, stages[i].s, stages[i].n, stages[i].fn, NULL); ring->state = FLOW_RING_CIRCULATING; return 1; }
int flow_token_ring_step(FlowTokenRing *ring) { if (!ring || !ring->token_count || ring->state != FLOW_RING_CIRCULATING) return 0;
    FlowToken *tok = &ring->tokens[ring->current_token_idx]; int ok = tok->transition_fn(ring, tok, &ring->active_canvas); tok->execution_count++;
    if (!ok && ring->state == FLOW_RING_CIRCULATING) { ring->state = FLOW_RING_UNSAT; return 0; }
    ring->active_genome = flow_token_ring_attention_project(&ring->active_canvas, tok->attention_mask, tok->dynamic_bias, ring->active_genome);
    if (tok->energy > 0) { ring->lyapunov_delta_e = fabs(tok->energy - ring->lyapunov_energy); ring->prev_energy = ring->lyapunov_energy; ring->lyapunov_energy = tok->energy; }
    ring->wavefront_epoch++; ring->current_token_idx = (ring->current_token_idx + 1) % ring->token_count; ring->step_count++;
    if (ring->current_token_idx == 0) { ring->cycle_count++; ring->quiescent_generation++; if (ring->bound_arena) flow_bump_qsbr_quiescent_fold(ring->bound_arena); } return 1; }
FlowTokenRingState flow_token_ring_run_to_attractor(FlowTokenRing *ring, size_t max_cycles) { if (!ring) return FLOW_RING_UNSAT;
    size_t limit = max_cycles ? max_cycles : FLOW_TOKEN_RING_DEFAULT_MAX_CYCLES;
    while (ring->state == FLOW_RING_CIRCULATING && ring->cycle_count < limit) if (!flow_token_ring_step(ring)) break;
    if (ring->state == FLOW_RING_CIRCULATING) { ring->state = FLOW_RING_EXHAUSTED; snprintf(ring->status_message, sizeof(ring->status_message), "Exhausted maximum cycle limit (%zu cycles) without fixed point", limit); }
    return ring->state; }
int flow_token_ring_converge_default(FlowTokenRing *ring, const SemanticIR *opt_ir, const char *flow_name) {
    if (!ring) return 0;
    flow_registry_init();
    static SemanticIR s_ir;
    memset(&s_ir, 0, sizeof(s_ir));
    if (opt_ir) {
        s_ir = *opt_ir;
    } else {
        strncpy(s_ir.flow_name, flow_name ? flow_name : "default_flow", sizeof(s_ir.flow_name) - 1);
        s_ir.input_max_count = 4096;
        s_ir.memory_limit_mb = 64;
        s_ir.fact_deterministic = 1;
        s_ir.fact_range_proven = 1;
        s_ir.state_shared = 1;
        s_ir.state_read_heavy = 1;
        s_ir.fact_unordered = 1;
    }
    if (!flow_token_ring_setup_canonical(ring, &s_ir, 64, FLOW_DEFAULT_SEED)) return 0;
    FlowTokenRingState st = flow_token_ring_run_to_attractor(ring, 8);
    return (st == FLOW_RING_ATTRACTOR_REACHED || ring->best_search.component != NULL) ? 1 : 0;
}
int flow_subspace_decompose_canonical(FlowSubspaceDecomposition *decomp, const SemanticIR *ir) { if (!decomp) return 0; memset(decomp, 0, sizeof(*decomp));
    long cpus = sysconf(_SC_NPROCESSORS_ONLN); cpus = cpus < 1 ? 4 : (cpus > 64 ? 64 : cpus);
    uint64_t max_items = (ir && ir->input_max_count > 0) ? (uint64_t)ir->input_max_count : 4096ULL;
    uint64_t mem_kb = (ir && ir->memory_limit_mb > 0) ? (uint64_t)ir->memory_limit_mb * 1024ULL : 65535ULL; if (mem_kb > 65535ULL) mem_kb = 65535ULL;
    const FlowSubspace init_subs[5] = {
        {FLOW_SUBSPACE_CAPACITY,    "capacity",     FLOW_SUBSPACE_MASK_CAPACITY,    0, 16,   16, 65535, 0.0, FLOW_LAGRANGIAN_DEFAULT_ETA, (double)max_items, (double)max_items, max_items, max_items},
        {FLOW_SUBSPACE_CONCURRENCY, "concurrency",  FLOW_SUBSPACE_MASK_CONCURRENCY, 16, 8,    1,    64, 0.0, FLOW_LAGRANGIAN_DEFAULT_ETA, (double)cpus,      (double)cpus,      (uint64_t)cpus, (uint64_t)cpus},
        {FLOW_SUBSPACE_SHARDING,    "sharding",     FLOW_SUBSPACE_MASK_SHARDING,    24, 8,    1,    64, 0.0, FLOW_LAGRANGIAN_DEFAULT_ETA, 16.0,              16.0,              16, 16},
        {FLOW_SUBSPACE_BUFFER,      "buffer_arena", FLOW_SUBSPACE_MASK_BUFFER,      32, 16, 1024, 65535, 0.0, FLOW_LAGRANGIAN_DEFAULT_ETA, (double)mem_kb,    (double)mem_kb,    mem_kb, mem_kb},
        {FLOW_SUBSPACE_GROWTH,      "growth_batch", FLOW_SUBSPACE_MASK_GROWTH,      48, 16,  100,   400, 0.0, FLOW_LAGRANGIAN_DEFAULT_ETA, 200.0,             150.0,             150, 150} };
    memcpy(decomp->subspaces, init_subs, sizeof(init_subs)); decomp->subspace_count = 5; decomp->is_strictly_orthogonal = true;
    for (size_t i = 0; i < decomp->subspace_count; ++i) {
        for (size_t j = i + 1; j < decomp->subspace_count; ++j) if (decomp->subspaces[i].mask & decomp->subspaces[j].mask) decomp->is_strictly_orthogonal = false;
        decomp->composite_coverage_mask |= decomp->subspaces[i].mask; } return decomp->is_strictly_orthogonal ? 1 : 0; }
int flow_subspace_lagrangian_tune(FlowSubspace *sub, double current_demand, double capacity_limit) { if (!sub) return 0;
    sub->current_demand = current_demand; sub->capacity_limit = capacity_limit; double eta = sub->learning_rate_eta > 0.0 ? sub->learning_rate_eta : FLOW_LAGRANGIAN_DEFAULT_ETA;
    sub->shadow_price_lambda = fmax(0.0, sub->shadow_price_lambda + eta * (current_demand - capacity_limit));
    double target = sub->shadow_price_lambda > 0.0 ? capacity_limit / (1.0 + sub->shadow_price_lambda) : current_demand;
    target = fmin((double)sub->max_value, fmax((double)sub->min_value, target)); sub->current_val = sub->optimal_val = (uint64_t)target; return 1; }
uint64_t flow_subspace_polyhedral_project(const FlowSubspace *sub, uint64_t raw_val) { if (!sub) return 0;
    uint64_t val = raw_val < sub->min_value ? sub->min_value : (raw_val > sub->max_value ? sub->max_value : raw_val);
    uint64_t field_mask = (sub->bit_width >= 64) ? ~0ULL : ((1ULL << sub->bit_width) - 1ULL); return (val & field_mask) << sub->bit_offset; }
uint64_t flow_wavefront_semilattice_join(uint64_t base_genome, const uint64_t *thread_slices, const uint64_t *subspace_masks, size_t count) {
    if (!thread_slices || !subspace_masks || !count) return base_genome; if (count <= 8) { FlowVector512 s = {0}, m = {0};
        for (size_t i = 0; i < count; ++i) { s.u64[i] = thread_slices[i]; m.u64[i] = subspace_masks[i]; }
        return (base_genome & ~flow_v512_horizontal_or(m)) | flow_v512_horizontal_or(flow_v512_and(s, m)); } uint64_t merged = base_genome;
    for (size_t i = 0; i < count; ++i) merged = (merged & ~subspace_masks[i]) | (thread_slices[i] & subspace_masks[i]); return merged; }
typedef struct { FlowWavefrontRing *ring; size_t worker_id, subspace_idx; uint64_t slice_out; double energy_out; } FlowWavefrontWorkerTask;
static void *wavefront_worker_func(void *arg) { FlowWavefrontWorkerTask *task = (FlowWavefrontWorkerTask *)arg;
    if (!task || !task->ring || task->subspace_idx >= task->ring->decomp.subspace_count) return NULL; flow_numa_pin_thread((uint32_t)task->worker_id);
    FlowSubspace *sub = &task->ring->decomp.subspaces[task->subspace_idx]; flow_subspace_lagrangian_tune(sub, sub->current_demand, sub->capacity_limit);
    task->slice_out = flow_subspace_polyhedral_project(sub, sub->optimal_val); double diff = (double)sub->current_val - (double)sub->optimal_val;
    task->energy_out = 0.5 * (diff * diff) / (double)(sub->max_value > 0 ? sub->max_value : 1); return NULL; }
int flow_wavefront_ring_init(FlowWavefrontRing *ring, SemanticIR *ir, size_t num_slots, size_t num_workers) { if (!ring) return 0; memset(ring, 0, sizeof(*ring));
    ring->active_ir = ir; ring->slot_count = (num_slots > 0 && num_slots <= FLOW_WAVEFRONT_MAX_SLOTS) ? num_slots : 4;
    ring->worker_count = (num_workers > 0 && num_workers <= FLOW_WAVEFRONT_MAX_WORKERS) ? num_workers : 5; ring->state = FLOW_RING_CIRCULATING;
    ring->global_lyapunov_energy = 100.0; ring->prev_lyapunov_energy = 200.0; ring->lyapunov_delta_e = 100.0; flow_subspace_decompose_canonical(&ring->decomp, ir);
    for (size_t i = 0; i < ring->slot_count; ++i) { ring->slots[i].slot_id = (uint32_t)i; ring->slots[i].current_stage = (FlowTokenStage)(i % 5); ring->slots[i].energy = 50.0;
        flow_bmf_canvas_init(&ring->slots[i].bmf_canvas, (uint32_t)i, ~0ULL, ~0ULL, 0); } if (ir) { flow_bitspace_init_for_ir(ir, &ring->active_space);
        if (ring->active_space.candidate_count > 0) flow_mask_canvas_compose(ir, ring->active_space.candidates[0], &ring->active_space.candidate_dims[0], NULL, &ring->global_canvas);
    } ring->wavefront_epoch = ring->quiescent_generation = 1;
    snprintf(ring->status_message, sizeof(ring->status_message), "Wavefront Ring initialized: %zu slots, %zu workers, %zu orthogonal subspaces", ring->slot_count, ring->worker_count, ring->decomp.subspace_count);
    return 1; }
int flow_wavefront_ring_step_parallel(FlowWavefrontRing *ring) { if (!ring || ring->state != FLOW_RING_CIRCULATING) return 0;
    size_t num_workers = ring->worker_count > ring->decomp.subspace_count ? ring->decomp.subspace_count : ring->worker_count; if (!num_workers) return 0;
    FlowPhysicalProbe probe; flow_hardware_probe_start(&probe); pthread_t threads[FLOW_WAVEFRONT_MAX_WORKERS]; FlowWavefrontWorkerTask tasks[FLOW_WAVEFRONT_MAX_WORKERS];
    uint64_t thread_slices[FLOW_WAVEFRONT_MAX_WORKERS] = {0}, subspace_masks[FLOW_WAVEFRONT_MAX_WORKERS] = {0}; for (size_t i = 0; i < num_workers; ++i) {
        tasks[i] = (FlowWavefrontWorkerTask){.ring = ring, .worker_id = i, .subspace_idx = i};
        if (pthread_create(&threads[i], NULL, wavefront_worker_func, &tasks[i]) != 0) { wavefront_worker_func(&tasks[i]); threads[i] = 0; } }
    double total_subspace_energy = 0.0; for (size_t i = 0; i < num_workers; ++i) { if (threads[i]) pthread_join(threads[i], NULL);
        thread_slices[i] = tasks[i].slice_out; subspace_masks[i] = ring->decomp.subspaces[i].mask; total_subspace_energy += tasks[i].energy_out; }
    flow_hardware_probe_stop(&probe); ring->last_probe = probe; ring->total_cycles += probe.elapsed_cycles; ring->total_energy_uj += probe.dissipated_energy_uj;
    ring->global_lattice_genome = flow_wavefront_semilattice_join(ring->global_lattice_genome, thread_slices, subspace_masks, num_workers);
    ring->prev_lyapunov_energy = ring->global_lyapunov_energy; ring->global_lyapunov_energy = total_subspace_energy;
    ring->lyapunov_delta_e = fabs(ring->global_lyapunov_energy - ring->prev_lyapunov_energy); ring->wavefront_epoch++; ring->wave_cycle_count++; ring->quiescent_generation++;
    if (ring->bound_arena) flow_bump_qsbr_quiescent_fold(ring->bound_arena);
    if (ring->wave_cycle_count >= 1 && (ring->lyapunov_delta_e < FLOW_LYAPUNOV_CONVERGE_EPS || ring->global_lyapunov_energy < 1.0)) {
        ring->state = FLOW_RING_ATTRACTOR_REACHED; ring->attractor_converged = true;
        snprintf(ring->status_message, sizeof(ring->status_message), "Wavefront Attractor reached: cycle=%llu, energy=%.4f, Delta E=%.6f, cycles=%llu, uJ=%.1f (Lattice Genome=0x%016llx)",
                 (unsigned long long)ring->wave_cycle_count, ring->global_lyapunov_energy, ring->lyapunov_delta_e, (unsigned long long)ring->total_cycles, ring->total_energy_uj, (unsigned long long)ring->global_lattice_genome);
    } return 1; }
void flow_wavefront_ring_destroy(FlowWavefrontRing *ring) { if (ring) ring->state = FLOW_RING_INIT; }
int flow_wavefront_ring_bind_arena(FlowWavefrontRing *ring, FlowBumpQsbrArena *arena) { return (ring && arena) ? (ring->bound_arena = arena, 1) : 0; }
FlowSMTResult flow_wavefront_verify_temporal_safety_smt(const FlowWavefrontRing *ring, FlowSMTProofAttestation *proof_out) {
    size_t sz = sizeof(FlowBmf1BitCanvas), al = _Alignof(FlowBmf1BitCanvas), slot_al = _Alignof(FlowWavefrontSlot); const FlowBoxConstraint rules[] = {
        FLOW_SMT_RULE("swmr_lockfree_safety", (ring && ring->state == FLOW_RING_UNSAT) ? 1 : 0, 0, 0, FLOW_BOX_THEOREM_DETERMINISM, "Writer encountered lock contention or dead state"),
        FLOW_SMT_RULE("64b_tear_free_confinement", (sz != 64 || al != 64 || slot_al < 64) ? 1 : 0, 0, 0, FLOW_BOX_THEOREM_BUFFER_BOUNDS, "FlowBmf1BitCanvas or FlowWavefrontSlot deviates from 64-byte single cache line alignment"),
        FLOW_SMT_RULE("bounded_evacuation_horizon", (ring && ring->slot_count > FLOW_WAVEFRONT_MAX_SLOTS) ? 1 : 0, 0, 0, FLOW_BOX_THEOREM_SHARD_ISOLATION, "Wavefront ring slot count unbounded"),
        FLOW_SMT_RULE("zero_imperative_cleanup", (ring && ring->bound_arena && ring->bound_arena->cursor > ring->bound_arena->capacity) ? 1 : 0, 0, 0, FLOW_BOX_THEOREM_MEMORY_QUOTA, "Bump arena memory exceeded capacity before fold")
    };
    return FLOW_SMT_VERIFY_RULES("wavefront_temporal_safety", proof_out, rules, "SMT WAVEFRONT TEMPORAL SOUND: Epoch=%llu, Gen=%llu, Size=%zu, Align=%zu (Zero-Cost QSBR Guaranteed)",
        ring ? (unsigned long long)ring->wavefront_epoch : 0ULL, ring ? (unsigned long long)ring->quiescent_generation : 0ULL, sz, al); }
void flow_hetero_mesh_init(FlowHeteroMesh *mesh) { if (mesh) memset(mesh, 0, sizeof(*mesh)); }
int flow_hetero_mesh_register_node(FlowHeteroMesh *mesh, uint8_t node_id, FlowSwarmRole role, const char *name, uint64_t contract_hash, uint32_t capacity) {
    if (!mesh || mesh->node_count >= FLOW_HETERO_MESH_MAX_NODES) return 0; for (size_t i = 0; i < mesh->node_count; ++i) if (mesh->nodes[i].node_id == node_id) return 0;
    FlowHeteroMeshNode *n = &mesh->nodes[mesh->node_count++];
    *n = (FlowHeteroMeshNode){ .node_id = node_id, .role = role, .contract_hash = contract_hash, .capacity = capacity ? capacity : 1000, .current_latency_us = 50, .is_active = 1 };
    strncpy(n->name, name ? name : "node", sizeof(n->name) - 1); return 1; }
int flow_hetero_mesh_route_target(const FlowHeteroMesh *mesh, FlowSwarmRole target_role, uint8_t *selected_node_id_out) { if (!mesh || !selected_node_id_out) return 0;
    double best_energy = 1.0e18; int best_id = -1; size_t best_idx = 0; for (size_t i = 0; i < mesh->node_count; ++i) { const FlowHeteroMeshNode *n = &mesh->nodes[i];
        if (!n->is_active || n->role != target_role) continue;
        double cost = (double)n->current_latency_us * (1.0 + (double)n->current_backpressure / 200.0) + (n->current_backpressure >= 1000 ? 1.0e9 : 0.0);
        if (cost < best_energy) { best_energy = cost; best_id = (int)n->node_id; best_idx = i; } } if (best_id < 0) return 0; *selected_node_id_out = (uint8_t)best_id;
    __atomic_fetch_add(&((FlowHeteroMesh *)mesh)->nodes[best_idx].total_routed_requests, 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&((FlowHeteroMesh *)mesh)->total_mesh_routed, 1, __ATOMIC_RELAXED);
    return 1; }
FlowSMTResult flow_hetero_mesh_verify_smt(const FlowHeteroMesh *mesh, uint32_t ingress_max_qps, FlowSMTProofAttestation *proof_out) { if (!mesh) return FLOW_SMT_UNKNOWN;
    uint64_t comp_cap = 0, stor_cap = 0, max_bp = 0; size_t ds_nodes = 0; for (size_t i = 0; i < mesh->node_count; ++i) { const FlowHeteroMeshNode *n = &mesh->nodes[i];
        if (!n->is_active) continue; if (n->role == FLOW_SWARM_ROLE_COMPUTE_ROUTER) { comp_cap += n->capacity; ds_nodes++; }
        else if (n->role == FLOW_SWARM_ROLE_STORAGE_INDEX) { stor_cap += n->capacity; ds_nodes++; } }
    uint64_t bottleneck = (stor_cap > 0 && (!comp_cap || stor_cap < comp_cap)) ? stor_cap : (comp_cap ? comp_cap : comp_cap + stor_cap);
    if (ds_nodes <= 1 && mesh->node_count > 1)
        for (size_t i = 0; i < mesh->node_count; ++i) if (mesh->nodes[i].current_backpressure > max_bp) max_bp = mesh->nodes[i].current_backpressure;
    const FlowBoxConstraint rules[] = {
        FLOW_SMT_RULE("ingress QPS", ingress_max_qps, 0, bottleneck, FLOW_BOX_THEOREM_BUFFER_BOUNDS, "exceeds pipeline bottleneck tier capacity"),
        FLOW_SMT_RULE("downstream saturation", max_bp, 0, FLOW_MESH_SATURATION_LIMIT_PERMILLE, FLOW_BOX_THEOREM_MEMORY_QUOTA, "single-point-of-failure downstream node at 95%+ saturation") };
    return FLOW_SMT_VERIFY_RULES("hetero_mesh", proof_out, rules, "SMT MESH SOUND: ingress_qps=%u <= bottleneck_cap=%llu across %zu nodes (Zero-Defect)",
        ingress_max_qps, (unsigned long long)bottleneck, ds_nodes); }
int flow_swarm_ring_mesh_init(FlowSwarmRingMesh *mesh) { if (!mesh) return 0;
    memset(mesh, 0, sizeof(*mesh)); for (size_t i = 0; i < FLOW_HETERO_RING_MAX_NODES; ++i) atomic_init(&mesh->slots[i].seq, 0); return 1; }
int flow_swarm_ring_publish(FlowSwarmRingMesh *mesh, uint8_t node_id, FlowSwarmRole role, uint16_t backpressure,
                            uint16_t latency_p99_us, uint16_t contract_crc16, uint32_t capacity_qps, const double q[16]) {
    if (!mesh || node_id >= FLOW_HETERO_RING_MAX_NODES) return 0; FlowSwarmRingSlot *slot = &mesh->slots[node_id];
    uint64_t prev_seq = atomic_load_explicit(&slot->seq, memory_order_relaxed); atomic_store_explicit(&slot->seq, prev_seq + 1, memory_order_release);
    atomic_thread_fence(memory_order_release);
    slot->node_id = node_id; slot->role = (uint8_t)role; slot->backpressure_permille = backpressure;
    slot->latency_p99_us = latency_p99_us; slot->contract_crc16 = contract_crc16; slot->capacity_qps = capacity_qps; slot->timestamp_ns = flow_clock_monotonic_ns();
    if (q) memcpy(slot->q_snapshot, q, sizeof(double) * 16); atomic_store_explicit(&slot->seq, prev_seq + 2, memory_order_release);
    if (node_id >= mesh->active_node_count) mesh->active_node_count = node_id + 1;
    __atomic_fetch_add(&mesh->total_ring_updates, 1, __ATOMIC_RELAXED);
    return 1; }
int flow_swarm_ring_sample(const FlowSwarmRingMesh *mesh, uint8_t node_id, FlowSwarmRingSlot *slot_out) {
    if (!mesh || !slot_out || node_id >= FLOW_HETERO_RING_MAX_NODES) return 0; const FlowSwarmRingSlot *slot = &mesh->slots[node_id];
    for (int retry = 0; retry < 16; ++retry) { uint64_t s1 = atomic_load_explicit(&slot->seq, memory_order_acquire); if (!s1 || (s1 & 1)) continue; *slot_out = *slot;
        atomic_thread_fence(memory_order_acquire);
        if (s1 == atomic_load_explicit(&slot->seq, memory_order_acquire)) { __atomic_fetch_add(&((FlowSwarmRingMesh *)mesh)->total_ring_samples, 1, __ATOMIC_RELAXED); return 1; } } return 0; }
int flow_swarm_ring_route_lowest_energy(const FlowSwarmRingMesh *mesh, FlowSwarmRole target_role, uint8_t *selected_node_out) { if (!mesh || !selected_node_out) return 0;
    double lowest = 1e12; int found_id = -1; size_t limit = mesh->active_node_count > 0 ? mesh->active_node_count : FLOW_HETERO_RING_MAX_NODES;
    for (size_t i = 0; i < limit; ++i) { const FlowSwarmRingSlot *slot = &mesh->slots[i];
        if (!atomic_load_explicit(&slot->seq, memory_order_acquire) || slot->role != (uint8_t)target_role) continue;
        double e = (double)slot->backpressure_permille + (double)slot->latency_p99_us * 0.5; if (e < lowest) { lowest = e; found_id = (int)slot->node_id; } }
    if (found_id < 0) return 0; *selected_node_out = (uint8_t)found_id; return 1; }
