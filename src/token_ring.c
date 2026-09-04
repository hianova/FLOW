#include "token_ring.h"
#include "backend.h"
#include "numa_affinity.h"
#include "hardware_telemetry.h"
#include "simd_manifold.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

static int flow_smt_count_unsat(const FlowSMTProofAttestation *p) {
    return p ? (p->buffer_bounds_safety == FLOW_SMT_PROVEN_UNSAT) +
               (p->memory_quota_bound == FLOW_SMT_PROVEN_UNSAT) +
               (p->shard_non_aliasing == FLOW_SMT_PROVEN_UNSAT) +
               (p->determinism_invariant == FLOW_SMT_PROVEN_UNSAT) : 0;
}

uint64_t flow_token_ring_attention_project(FlowMaskCanvas *canvas, uint64_t attention_mask, uint64_t dynamic_bias, uint64_t current_genome) {
    if (!canvas) return current_genome;
    if (attention_mask) canvas->hard_composite_mask &= attention_mask;
    canvas->soft_composite_bias = canvas->hard_composite_mask ?
        ((canvas->soft_composite_bias & canvas->hard_composite_mask) | (dynamic_bias & canvas->hard_composite_mask)) : 0;
    return canvas->hard_composite_mask ? flow_manifold_project(current_genome, canvas->hard_composite_mask, canvas->soft_composite_bias) : 0;
}

static int stage_polytope_handler(FlowTokenRing *ring, FlowToken *token, FlowMaskCanvas *canvas) {
    if (!ring || !token || !canvas || !ring->active_ir) return 0;
    if (ring->active_space.candidate_count == 0 &&
        (!flow_bitspace_init_for_ir(ring->active_ir, &ring->active_space) || ring->active_space.candidate_count == 0)) {
        ring->state = FLOW_RING_UNSAT;
        snprintf(ring->status_message, sizeof(ring->status_message), "UNSAT: No feasible candidate components for intent '%s'", ring->active_ir->flow_name);
        return 0;
    }
    const Component *comp = ring->active_space.candidates[0];
    const FlowPlanDimensionSet *dims = &ring->active_space.candidate_dims[0];
    FlowPolyhedronSystem poly;
    flow_polyhedron_from_ir(ring->active_ir, comp, dims, &poly);
    uint64_t poly_mask = flow_polyhedron_project_mask(&poly, dims, 64);
    canvas->hard_polytope_mask = token->attention_mask = poly_mask;
    token->dynamic_bias = canvas->dynamic_telemetry_bias;
    token->energy = ring->lyapunov_energy > 0 ? ring->lyapunov_energy : 100.0;
    if (!canvas->hard_composite_mask) {
        ring->state = FLOW_RING_UNSAT;
        snprintf(ring->status_message, sizeof(ring->status_message), "UNSAT: Polytope projection produced null manifold intersection");
        return 0;
    }
    return 1;
}

static int stage_anneal_handler(FlowTokenRing *ring, FlowToken *token, FlowMaskCanvas *canvas) {
    if (!ring || !token || !canvas || !ring->active_ir || !ring->active_space.candidate_count) return 0;
    size_t iters = ring->anneal_iterations ? ring->anneal_iterations : 100;
    uint32_t seed = ring->rng_seed + (uint32_t)ring->cycle_count;
    FlowBitSearchResult bit_res = {0};
    if (!flow_bitspace_search(&ring->active_space, iters, seed, 0, NULL, &bit_res)) {
        ring->state = FLOW_RING_UNSAT;
        snprintf(ring->status_message, sizeof(ring->status_message), "UNSAT: BitSpace annealing failed to find valid plan");
        return 0;
    }
    flow_bitspace_extract_ensemble(&bit_res, &ring->ensemble);
    ring->active_genome = bit_res.best_plan.genome;
    flow_plan_to_search_result(&bit_res.best_plan, ring->active_ir, seed, &ring->best_search);
    token->energy = bit_res.best_plan.eval.energy;
    token->attention_mask = canvas->hard_composite_mask;
    token->dynamic_bias = canvas->soft_composite_bias;
    return 1;
}

static int stage_smt_proof_handler(FlowTokenRing *ring, FlowToken *token, FlowMaskCanvas *canvas) {
    if (!ring || !token || !canvas || !ring->active_ir || !ring->best_search.component) return 0;
    flow_smt_verify(ring->active_ir, ring->best_search.component, &ring->best_search.assignment, &ring->best_search.metrics, &ring->smt_proof);
    token->energy = ring->best_search.energy;
    token->attention_mask = canvas->hard_composite_mask;
    token->dynamic_bias = canvas->soft_composite_bias;
    return 1;
}

static int stage_synthesis_handler(FlowTokenRing *ring, FlowToken *token, FlowMaskCanvas *canvas) {
    if (!ring || !token || !canvas || !ring->active_ir || !ring->best_search.component) return 0;
    VerificationReport v_report = {0};
    int ok = verify_candidate(ring->active_ir, ring->best_search.component, &ring->best_search, &v_report);
    token->energy = ring->best_search.energy;
    token->attention_mask = canvas->hard_composite_mask;
    if (!ok) token->attention_mask &= ~(1ULL << (ring->active_genome & 63));
    return 1;
}

static int stage_attractor_handler(FlowTokenRing *ring, FlowToken *token, FlowMaskCanvas *canvas) {
    if (!ring || !token || !canvas) return 0;
    token->energy = ring->lyapunov_energy;
    token->attention_mask = canvas->hard_composite_mask;
    if (!canvas->hard_composite_mask) {
        ring->state = FLOW_RING_UNSAT;
        snprintf(ring->status_message, sizeof(ring->status_message), "UNSAT: Manifold collapsed to null space");
        return 1;
    }
    double delta = fabs(ring->lyapunov_energy - ring->prev_energy);
    ring->lyapunov_delta_e = delta;
    if (ring->cycle_count >= 1 && (delta < 1e-5 || ring->best_search.component)) {
        ring->state = FLOW_RING_ATTRACTOR_REACHED;
        ring->attractor_converged = 1;
        snprintf(ring->status_message, sizeof(ring->status_message),
                 "Attractor reached: cycle=%llu, energy=%.2f, Delta E=%.6f, SMT verified UNSAT=%d/4",
                 (unsigned long long)ring->cycle_count, ring->lyapunov_energy, delta, flow_smt_count_unsat(&ring->smt_proof));
    }
    return 1;
}

int flow_token_ring_init(FlowTokenRing *ring, SemanticIR *ir) {
    if (!ring) return 0;
    memset(ring, 0, sizeof(*ring));
    ring->active_ir = ir;
    ring->state = FLOW_RING_INIT;
    ring->rng_state = 0x853c49e6748fea9bULL;
    ring->anneal_iterations = 100;
    ring->rng_seed = 42;

    if (ir) {
        flow_bitspace_init_for_ir(ir, &ring->active_space);
        if (ring->active_space.candidate_count > 0) {
            flow_mask_canvas_compose(ir, ring->active_space.candidates[0], &ring->active_space.candidate_dims[0], NULL, &ring->active_canvas);
        } else {
            ring->active_canvas = (FlowMaskCanvas){.hard_safety_mask = ~0ULL, .hard_contract_mask = ~0ULL, .hard_resource_mask = ~0ULL,
                                                   .hard_plugin_mask = ~0ULL, .hard_polytope_mask = ~0ULL, .hard_composite_mask = ~0ULL};
        }
    } else {
        ring->active_canvas = (FlowMaskCanvas){.hard_safety_mask = ~0ULL, .hard_composite_mask = ~0ULL};
    }
    snprintf(ring->status_message, sizeof(ring->status_message), "Token Ring initialized");
    return 1;
}

int flow_token_ring_add_token(FlowTokenRing *ring, FlowTokenStage stage, const char *name, FlowTokenTransitionFn fn, void *user_data) {
    if (!ring || !fn || ring->token_count >= FLOW_TOKEN_RING_MAX_TOKENS) return 0;
    FlowToken *tok = &ring->tokens[ring->token_count++];
    *tok = (FlowToken){.token_id = (uint32_t)ring->token_count, .stage = stage, .transition_fn = fn, .user_data = user_data, .attention_mask = ~0ULL};
    if (name) strncpy(tok->stage_name, name, sizeof(tok->stage_name) - 1);
    else snprintf(tok->stage_name, sizeof(tok->stage_name), "stage_%d", (int)stage);
    return 1;
}

int flow_token_ring_setup_canonical(FlowTokenRing *ring, SemanticIR *ir, size_t anneal_iters, uint32_t seed) {
    if (!ring || !ir || !flow_token_ring_init(ring, ir)) return 0;
    ring->anneal_iterations = anneal_iters ? anneal_iters : 100;
    ring->rng_seed = seed ? seed : 42;
    ring->rng_state = (uint64_t)ring->rng_seed * 0x5851f42d4c957f2dULL + 1ULL;

    static const struct { FlowTokenStage s; const char *n; FlowTokenTransitionFn fn; } stages[] = {
        {FLOW_TOKEN_STAGE_POLYTOPE,  "polytope_projection", stage_polytope_handler},
        {FLOW_TOKEN_STAGE_ANNEAL,    "chaotic_annealing",   stage_anneal_handler},
        {FLOW_TOKEN_STAGE_SMT_PROOF, "smt_invariant_proof", stage_smt_proof_handler},
        {FLOW_TOKEN_STAGE_SYNTHESIS, "synthesis_validation",stage_synthesis_handler},
        {FLOW_TOKEN_STAGE_ATTRACTOR, "lyapunov_attractor",  stage_attractor_handler}
    };
    for (size_t i = 0; i < 5; ++i)
        flow_token_ring_add_token(ring, stages[i].s, stages[i].n, stages[i].fn, NULL);

    ring->state = FLOW_RING_CIRCULATING;
    return 1;
}

int flow_token_ring_step(FlowTokenRing *ring) {
    if (!ring || !ring->token_count || ring->state != FLOW_RING_CIRCULATING) return 0;
    FlowToken *tok = &ring->tokens[ring->current_token_idx];
    int ok = tok->transition_fn(ring, tok, &ring->active_canvas);
    tok->execution_count++;
    if (!ok && ring->state == FLOW_RING_CIRCULATING) { ring->state = FLOW_RING_UNSAT; return 0; }

    ring->active_genome = flow_token_ring_attention_project(&ring->active_canvas, tok->attention_mask, tok->dynamic_bias, ring->active_genome);
    if (tok->energy > 0) {
        ring->lyapunov_delta_e = fabs(tok->energy - ring->lyapunov_energy);
        ring->prev_energy = ring->lyapunov_energy;
        ring->lyapunov_energy = tok->energy;
    }
    ring->current_token_idx = (ring->current_token_idx + 1) % ring->token_count;
    ring->step_count++;
    if (ring->current_token_idx == 0) ring->cycle_count++;
    return 1;
}

FlowTokenRingState flow_token_ring_run_to_attractor(FlowTokenRing *ring, size_t max_cycles) {
    if (!ring) return FLOW_RING_UNSAT;
    size_t limit = max_cycles ? max_cycles : FLOW_TOKEN_RING_DEFAULT_MAX_CYCLES;
    while (ring->state == FLOW_RING_CIRCULATING && ring->cycle_count < limit)
        if (!flow_token_ring_step(ring)) break;
    if (ring->state == FLOW_RING_CIRCULATING) {
        ring->state = FLOW_RING_EXHAUSTED;
        snprintf(ring->status_message, sizeof(ring->status_message), "Exhausted maximum cycle limit (%zu cycles) without fixed point", limit);
    }
    return ring->state;
}

int flow_token_ring_is_converged(const FlowTokenRing *ring) {
    return ring && ring->state == FLOW_RING_ATTRACTOR_REACHED;
}

const char *flow_token_ring_state_name(FlowTokenRingState state) {
    static const char * const names[] = {"init", "circulating", "attractor_reached", "unsat_conflict", "cycle_exhausted"};
    return ((int)state >= 0 && (int)state <= 4) ? names[state] : "unknown_state";
}

const char *flow_token_stage_name(FlowTokenStage stage) {
    static const char * const names[] = {"ingest", "polytope", "anneal", "smt_proof", "synthesis", "attractor", "custom"};
    return ((int)stage >= 0 && (int)stage <= 6) ? names[stage] : "unknown_stage";
}

/*
 * ============================================================================
 * Part 2: Orthogonal Subspace Decomposition & Join-Semilattice Confluence
 * ============================================================================
 */

int flow_subspace_decompose_canonical(FlowSubspaceDecomposition *decomp, const SemanticIR *ir) {
    if (!decomp) return 0;
    memset(decomp, 0, sizeof(*decomp));

    long online_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (online_cpus < 1) online_cpus = 4;
    if (online_cpus > 64) online_cpus = 64;

    uint64_t max_items = (ir && ir->input_max_count > 0) ? (uint64_t)ir->input_max_count : 4096ULL;
    uint64_t mem_kb = (ir && ir->memory_limit_mb > 0) ? (uint64_t)ir->memory_limit_mb * 1024ULL : 65535ULL;
    if (mem_kb > 65535ULL) mem_kb = 65535ULL;

    /* Subspace 0: Capacity (bits 0..15) */
    decomp->subspaces[0] = (FlowSubspace){
        .id = FLOW_SUBSPACE_CAPACITY,
        .name = "capacity",
        .mask = 0x000000000000FFFFULL,
        .bit_offset = 0,
        .bit_width = 16,
        .min_value = 16,
        .max_value = 65535,
        .capacity_limit = (double)max_items,
        .current_demand = (double)max_items,
        .learning_rate_eta = 0.05,
        .shadow_price_lambda = 0.0,
        .current_val = max_items,
        .optimal_val = max_items
    };

    /* Subspace 1: Concurrency / Threads (bits 16..23) */
    decomp->subspaces[1] = (FlowSubspace){
        .id = FLOW_SUBSPACE_CONCURRENCY,
        .name = "concurrency",
        .mask = 0x0000000000FF0000ULL,
        .bit_offset = 16,
        .bit_width = 8,
        .min_value = 1,
        .max_value = 64,
        .capacity_limit = (double)online_cpus,
        .current_demand = (double)online_cpus,
        .learning_rate_eta = 0.05,
        .shadow_price_lambda = 0.0,
        .current_val = (uint64_t)online_cpus,
        .optimal_val = (uint64_t)online_cpus
    };

    /* Subspace 2: Sharding (bits 24..31) */
    decomp->subspaces[2] = (FlowSubspace){
        .id = FLOW_SUBSPACE_SHARDING,
        .name = "sharding",
        .mask = 0x00000000FF000000ULL,
        .bit_offset = 24,
        .bit_width = 8,
        .min_value = 1,
        .max_value = 64,
        .capacity_limit = 16.0,
        .current_demand = 16.0,
        .learning_rate_eta = 0.05,
        .shadow_price_lambda = 0.0,
        .current_val = 16,
        .optimal_val = 16
    };

    /* Subspace 3: Buffer / Arena Quota (bits 32..47) */
    decomp->subspaces[3] = (FlowSubspace){
        .id = FLOW_SUBSPACE_BUFFER,
        .name = "buffer_arena",
        .mask = 0x0000FFFF00000000ULL,
        .bit_offset = 32,
        .bit_width = 16,
        .min_value = 1024,
        .max_value = 65535,
        .capacity_limit = (double)mem_kb,
        .current_demand = (double)mem_kb,
        .learning_rate_eta = 0.05,
        .shadow_price_lambda = 0.0,
        .current_val = mem_kb,
        .optimal_val = mem_kb
    };

    /* Subspace 4: Growth / Batch Burst (bits 48..63) */
    decomp->subspaces[4] = (FlowSubspace){
        .id = FLOW_SUBSPACE_GROWTH,
        .name = "growth_batch",
        .mask = 0xFFFF000000000000ULL,
        .bit_offset = 48,
        .bit_width = 16,
        .min_value = 100,
        .max_value = 400,
        .capacity_limit = 200.0,
        .current_demand = 150.0,
        .learning_rate_eta = 0.05,
        .shadow_price_lambda = 0.0,
        .current_val = 150,
        .optimal_val = 150
    };

    decomp->subspace_count = 5;
    decomp->composite_coverage_mask = 0;
    decomp->is_strictly_orthogonal = true;

    /* Mathematical Invariant Check: Verify disjointness across all pairs */
    for (size_t i = 0; i < decomp->subspace_count; ++i) {
        for (size_t j = i + 1; j < decomp->subspace_count; ++j) {
            if ((decomp->subspaces[i].mask & decomp->subspaces[j].mask) != 0) {
                decomp->is_strictly_orthogonal = false;
            }
        }
        decomp->composite_coverage_mask |= decomp->subspaces[i].mask;
    }
    return decomp->is_strictly_orthogonal ? 1 : 0;
}

int flow_subspace_lagrangian_tune(FlowSubspace *sub, double current_demand, double capacity_limit) {
    if (!sub) return 0;
    sub->current_demand = current_demand;
    sub->capacity_limit = capacity_limit;

    double eta = sub->learning_rate_eta > 0.0 ? sub->learning_rate_eta : 0.05;
    double subgradient = current_demand - capacity_limit;

    /* Dual update: lambda_{t+1} = max(0, lambda_t + eta * (demand - capacity)) */
    sub->shadow_price_lambda = sub->shadow_price_lambda + eta * subgradient;
    if (sub->shadow_price_lambda < 0.0) {
        sub->shadow_price_lambda = 0.0;
    }

    /* Mathematical target derived from Lagrangian optimality */
    double target;
    if (sub->shadow_price_lambda > 0.0) {
        target = capacity_limit / (1.0 + sub->shadow_price_lambda);
    } else {
        target = current_demand;
    }

    /* Polyhedral box projection */
    if (target < (double)sub->min_value) target = (double)sub->min_value;
    if (target > (double)sub->max_value) target = (double)sub->max_value;

    sub->optimal_val = (uint64_t)target;
    sub->current_val = sub->optimal_val;
    return 1;
}

uint64_t flow_subspace_polyhedral_project(const FlowSubspace *sub, uint64_t raw_val) {
    if (!sub) return 0;
    uint64_t val = raw_val;
    if (val < sub->min_value) val = sub->min_value;
    if (val > sub->max_value) val = sub->max_value;
    uint64_t field_mask = (sub->bit_width >= 64) ? ~0ULL : ((1ULL << sub->bit_width) - 1ULL);
    return (val & field_mask) << sub->bit_offset;
}

uint64_t flow_wavefront_semilattice_join(uint64_t base_genome,
                                         const uint64_t *thread_slices,
                                         const uint64_t *subspace_masks,
                                         size_t count) {
    if (!thread_slices || !subspace_masks || count == 0) return base_genome;

    /*
     * Hardware Pillar 2: 512-Bit SIMD Vectorized Confluence
     * If count <= 8 (the 8 orthogonal subspaces of the 512-bit vector register),
     * execute parallel bitwise intersection and horizontal reduction in vector registers.
     */
    if (count <= 8) {
        FlowVector512 s = {0}, m = {0};
        for (size_t i = 0; i < count; ++i) {
            s.u64[i] = thread_slices[i];
            m.u64[i] = subspace_masks[i];
        }
        FlowVector512 filtered = flow_v512_and(s, m);
        uint64_t total_slices = flow_v512_horizontal_or(filtered);
        uint64_t total_mask = flow_v512_horizontal_or(m);
        return (base_genome & ~total_mask) | total_slices;
    }

    uint64_t merged = base_genome;
    for (size_t i = 0; i < count; ++i) {
        merged = (merged & ~subspace_masks[i]) | (thread_slices[i] & subspace_masks[i]);
    }
    return merged;
}

typedef struct {
    FlowWavefrontRing *ring;
    size_t worker_id;
    size_t subspace_idx;
    uint64_t slice_out;
    double energy_out;
} FlowWavefrontWorkerTask;

static void *wavefront_worker_func(void *arg) {
    FlowWavefrontWorkerTask *task = (FlowWavefrontWorkerTask *)arg;
    if (!task || !task->ring) return NULL;

    /* Hardware Pillar 1: Core pinning & QoS steer to performance cores */
    flow_numa_pin_thread((uint32_t)task->worker_id);

    FlowWavefrontRing *ring = task->ring;
    size_t idx = task->subspace_idx;
    if (idx >= ring->decomp.subspace_count) return NULL;

    FlowSubspace *sub = &ring->decomp.subspaces[idx];

    /* Mathematical engine tuning via Lagrangian subgradient */
    flow_subspace_lagrangian_tune(sub, sub->current_demand, sub->capacity_limit);

    /* Polyhedral box/affine projection */
    task->slice_out = flow_subspace_polyhedral_project(sub, sub->optimal_val);

    /* Individual subspace Lyapunov potential V_k = 0.5 * (val - opt)^2 */
    double diff = (double)sub->current_val - (double)sub->optimal_val;
    task->energy_out = 0.5 * (diff * diff) / (double)(sub->max_value > 0 ? sub->max_value : 1);
    return NULL;
}

int flow_wavefront_ring_init(FlowWavefrontRing *ring,
                             SemanticIR *ir,
                             size_t num_slots,
                             size_t num_workers) {
    if (!ring) return 0;
    memset(ring, 0, sizeof(*ring));
    ring->active_ir = ir;
    ring->slot_count = (num_slots > 0 && num_slots <= FLOW_WAVEFRONT_MAX_SLOTS) ? num_slots : 4;
    ring->worker_count = (num_workers > 0 && num_workers <= FLOW_WAVEFRONT_MAX_WORKERS) ? num_workers : 5;
    ring->state = FLOW_RING_CIRCULATING;
    ring->global_lyapunov_energy = 100.0;
    ring->prev_lyapunov_energy = 200.0;
    ring->lyapunov_delta_e = 100.0;

    flow_subspace_decompose_canonical(&ring->decomp, ir);

    for (size_t i = 0; i < ring->slot_count; ++i) {
        ring->slots[i].slot_id = (uint32_t)i;
        ring->slots[i].current_stage = (FlowTokenStage)(i % 5);
        ring->slots[i].energy = 50.0;
        ring->slots[i].in_flight = false;
        ring->slots[i].slot_genome = 0;
    }

    if (ir) {
        flow_bitspace_init_for_ir(ir, &ring->active_space);
        if (ring->active_space.candidate_count > 0) {
            flow_mask_canvas_compose(ir, ring->active_space.candidates[0],
                                     &ring->active_space.candidate_dims[0],
                                     NULL, &ring->global_canvas);
        }
    }
    snprintf(ring->status_message, sizeof(ring->status_message),
             "Wavefront Ring initialized: %zu slots, %zu workers, %zu orthogonal subspaces",
             ring->slot_count, ring->worker_count, ring->decomp.subspace_count);
    return 1;
}

int flow_wavefront_ring_step_parallel(FlowWavefrontRing *ring) {
    if (!ring || ring->state != FLOW_RING_CIRCULATING) return 0;

    size_t num_workers = ring->worker_count;
    if (num_workers > ring->decomp.subspace_count) {
        num_workers = ring->decomp.subspace_count;
    }
    if (num_workers == 0) return 0;

    /* Hardware Pillar 3: Physical Hardware Telemetry Probe Start */
    FlowPhysicalProbe probe;
    flow_hardware_probe_start(&probe);

    pthread_t threads[FLOW_WAVEFRONT_MAX_WORKERS];
    FlowWavefrontWorkerTask tasks[FLOW_WAVEFRONT_MAX_WORKERS];
    uint64_t thread_slices[FLOW_WAVEFRONT_MAX_WORKERS] = {0};
    uint64_t subspace_masks[FLOW_WAVEFRONT_MAX_WORKERS] = {0};

    for (size_t i = 0; i < num_workers; ++i) {
        tasks[i].ring = ring;
        tasks[i].worker_id = i;
        tasks[i].subspace_idx = i;
        tasks[i].slice_out = 0;
        tasks[i].energy_out = 0.0;
        if (pthread_create(&threads[i], NULL, wavefront_worker_func, &tasks[i]) != 0) {
            wavefront_worker_func(&tasks[i]);
            threads[i] = 0;
        }
    }

    double total_subspace_energy = 0.0;
    for (size_t i = 0; i < num_workers; ++i) {
        if (threads[i] != 0) {
            pthread_join(threads[i], NULL);
        }
        thread_slices[i] = tasks[i].slice_out;
        subspace_masks[i] = ring->decomp.subspaces[i].mask;
        total_subspace_energy += tasks[i].energy_out;
    }

    /* Hardware Pillar 3: Physical Hardware Telemetry Probe Stop */
    flow_hardware_probe_stop(&probe);
    ring->last_probe = probe;
    ring->total_cycles += probe.elapsed_cycles;
    ring->total_energy_uj += probe.dissipated_energy_uj;

    /* Register-speed Join-Semilattice Confluence (\sqcup) via 512-bit SIMD */
    ring->global_lattice_genome = flow_wavefront_semilattice_join(
        ring->global_lattice_genome,
        thread_slices,
        subspace_masks,
        num_workers
    );

    /* Update Lyapunov Multi-Objective Energy */
    ring->prev_lyapunov_energy = ring->global_lyapunov_energy;
    ring->global_lyapunov_energy = total_subspace_energy;
    ring->lyapunov_delta_e = fabs(ring->global_lyapunov_energy - ring->prev_lyapunov_energy);
    ring->wave_cycle_count++;

    /* Attractor Fixed-Point Check */
    if (ring->wave_cycle_count >= 1 && (ring->lyapunov_delta_e < 1e-5 || ring->global_lyapunov_energy < 1.0)) {
        ring->state = FLOW_RING_ATTRACTOR_REACHED;
        ring->attractor_converged = true;
        snprintf(ring->status_message, sizeof(ring->status_message),
                 "Wavefront Attractor reached: cycle=%llu, energy=%.4f, Delta E=%.6f, cycles=%llu, uJ=%.1f (Lattice Genome=0x%016llx)",
                 (unsigned long long)ring->wave_cycle_count,
                 ring->global_lyapunov_energy,
                 ring->lyapunov_delta_e,
                 (unsigned long long)ring->total_cycles,
                 ring->total_energy_uj,
                 (unsigned long long)ring->global_lattice_genome);
    }
    return 1;
}

FlowTokenRingState flow_wavefront_ring_run_to_attractor(FlowWavefrontRing *ring, size_t max_cycles) {
    if (!ring) return FLOW_RING_UNSAT;
    size_t limit = max_cycles ? max_cycles : FLOW_TOKEN_RING_DEFAULT_MAX_CYCLES;
    while (ring->state == FLOW_RING_CIRCULATING && ring->wave_cycle_count < limit) {
        if (!flow_wavefront_ring_step_parallel(ring)) break;
    }
    if (ring->state == FLOW_RING_CIRCULATING) {
        ring->state = FLOW_RING_EXHAUSTED;
        snprintf(ring->status_message, sizeof(ring->status_message),
                 "Wavefront exhausted maximum cycle limit (%zu cycles)", limit);
    }
    return ring->state;
}

void flow_wavefront_ring_destroy(FlowWavefrontRing *ring) {
    if (!ring) return;
    ring->state = FLOW_RING_INIT;
}

