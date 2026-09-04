#include "token_ring.h"
#include "backend.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
