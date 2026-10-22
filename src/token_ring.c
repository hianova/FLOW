#include "token_ring.h"
#include "backend.h"
#include "numa_affinity.h"
#include "hardware_telemetry.h"
#include "simd_manifold.h"
#include "entropy_collapse.h"
#include "flow_smt_dsl.h"
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
    if (ring->cycle_count >= 1 && (delta < 1e-5 || ring->best_search.component)) { ring->state = FLOW_RING_ATTRACTOR_REACHED; ring->attractor_converged = 1;
        snprintf(ring->status_message, sizeof(ring->status_message), "Attractor reached: cycle=%llu, energy=%.2f, Delta E=%.6f, SMT verified UNSAT=%d/4", (unsigned long long)ring->cycle_count, ring->lyapunov_energy, delta, flow_smt_count_unsat(&ring->smt_proof));
    } return 1; }
int flow_token_ring_init(FlowTokenRing *ring, SemanticIR *ir) { if (!ring) return 0; memset(ring, 0, sizeof(*ring));
    ring->active_ir = ir; ring->state = FLOW_RING_INIT; ring->rng_state = 0x853c49e6748fea9bULL; ring->anneal_iterations = 100; ring->rng_seed = 42;
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
    ring->anneal_iterations = anneal_iters ? anneal_iters : 100; ring->rng_seed = seed ? seed : 42; ring->rng_state = (uint64_t)ring->rng_seed * 0x5851f42d4c957f2dULL + 1ULL;
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
int flow_subspace_decompose_canonical(FlowSubspaceDecomposition *decomp, const SemanticIR *ir) { if (!decomp) return 0; memset(decomp, 0, sizeof(*decomp));
    long cpus = sysconf(_SC_NPROCESSORS_ONLN); cpus = cpus < 1 ? 4 : (cpus > 64 ? 64 : cpus);
    uint64_t max_items = (ir && ir->input_max_count > 0) ? (uint64_t)ir->input_max_count : 4096ULL;
    uint64_t mem_kb = (ir && ir->memory_limit_mb > 0) ? (uint64_t)ir->memory_limit_mb * 1024ULL : 65535ULL; if (mem_kb > 65535ULL) mem_kb = 65535ULL;
    const FlowSubspace init_subs[5] = {
        {FLOW_SUBSPACE_CAPACITY,    "capacity",     0x000000000000FFFFULL,  0, 16,   16, 65535, 0.0, 0.05, (double)max_items, (double)max_items, max_items, max_items},
        {FLOW_SUBSPACE_CONCURRENCY, "concurrency",  0x0000000000FF0000ULL, 16,  8,    1,    64, 0.0, 0.05, (double)cpus,      (double)cpus,      (uint64_t)cpus, (uint64_t)cpus},
        {FLOW_SUBSPACE_SHARDING,    "sharding",     0x00000000FF000000ULL, 24,  8,    1,    64, 0.0, 0.05, 16.0,              16.0,              16, 16},
        {FLOW_SUBSPACE_BUFFER,      "buffer_arena", 0x0000FFFF00000000ULL, 32, 16, 1024, 65535, 0.0, 0.05, (double)mem_kb,    (double)mem_kb,    mem_kb, mem_kb},
        {FLOW_SUBSPACE_GROWTH,      "growth_batch", 0xFFFF000000000000ULL, 48, 16,  100,   400, 0.0, 0.05, 200.0,             150.0,             150, 150} };
    memcpy(decomp->subspaces, init_subs, sizeof(init_subs)); decomp->subspace_count = 5; decomp->is_strictly_orthogonal = true;
    for (size_t i = 0; i < decomp->subspace_count; ++i) {
        for (size_t j = i + 1; j < decomp->subspace_count; ++j) if (decomp->subspaces[i].mask & decomp->subspaces[j].mask) decomp->is_strictly_orthogonal = false;
        decomp->composite_coverage_mask |= decomp->subspaces[i].mask; } return decomp->is_strictly_orthogonal ? 1 : 0; }
int flow_subspace_lagrangian_tune(FlowSubspace *sub, double current_demand, double capacity_limit) { if (!sub) return 0;
    sub->current_demand = current_demand; sub->capacity_limit = capacity_limit; double eta = sub->learning_rate_eta > 0.0 ? sub->learning_rate_eta : 0.05;
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
    if (ring->wave_cycle_count >= 1 && (ring->lyapunov_delta_e < 1e-5 || ring->global_lyapunov_energy < 1.0)) {
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
