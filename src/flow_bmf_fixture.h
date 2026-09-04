#ifndef FLOW_BMF_FIXTURE_H
#define FLOW_BMF_FIXTURE_H

#include "bitmanifold.h"

#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW BitManifold (BMF) Energy Fixture & Annealing Template (flow_bmf_fixture.h)
 * ============================================================================
 *
 * Provides a pure C17 fixture to evaluate domain energy landscapes over 64-bit
 * discrete manifolds, automating 1-bit chaotic mutation, Boltzmann exploration,
 * and greedy descent with zero heap allocation.
 * ============================================================================
 */

typedef double (*FlowBMFEnergyFn)(uint64_t candidate_genome, void *user_context);

typedef struct {
    uint64_t initial_genome;
    uint64_t best_genome;
    double initial_energy;
    double best_energy;
    uint64_t total_iterations;
    uint64_t transitions_accepted;
    uint64_t transitions_rejected;
} FlowBMFAnnealResult;

/**
 * flow_bmf_anneal_step:
 * Executes a single 1-bit chaotic mutation on the legal manifold, evaluates the
 * domain energy landscape, and accepts or rejects the state based on Delta E and temperature.
 */
static inline int flow_bmf_anneal_step(uint64_t current_genome,
                                       uint64_t hard_safety_mask,
                                       FlowBMFEnergyFn energy_fn,
                                       void *user_ctx,
                                       uint64_t *rng_state,
                                       double current_energy,
                                       double temperature,
                                       uint64_t *new_genome_out,
                                       double *new_energy_out) {
    if (energy_fn == NULL || new_genome_out == NULL) return 0;

    uint32_t mutated_bit = 0;
    uint64_t candidate = flow_manifold_transition(current_genome, hard_safety_mask, rng_state, &mutated_bit);
    double cand_energy = energy_fn(candidate, user_ctx);
    double delta_e = cand_energy - current_energy;

    int accept = 0;
    if (delta_e <= 0.0) {
        accept = 1;
    } else if (temperature > 1e-6) {
        double p = exp(-delta_e / temperature);
        uint64_t r = rng_state ? *rng_state : 0x853c49e6748fea9bULL;
        if (r == 0) r = 0x853c49e6748fea9bULL;
        r ^= r >> 12; r ^= r << 25; r ^= r >> 27;
        if (rng_state) *rng_state = r;
        double random_0_to_1 = (double)(r & 0xFFFFULL) / 65535.0;
        if (random_0_to_1 < p) {
            accept = 1;
        }
    }

    if (accept) {
        *new_genome_out = candidate;
        if (new_energy_out) *new_energy_out = cand_energy;
        return 1;
    } else {
        *new_genome_out = current_genome;
        if (new_energy_out) *new_energy_out = current_energy;
        return 0;
    }
}

/**
 * flow_bmf_anneal_transition:
 * Direct 1-line helper: mutates current_genome, evaluates energy, and returns accepted state.
 */
static inline uint64_t flow_bmf_anneal_transition(uint64_t current_genome,
                                                  uint64_t hard_safety_mask,
                                                  FlowBMFEnergyFn energy_fn,
                                                  void *user_ctx,
                                                  uint64_t *rng_state,
                                                  double temperature) {
    if (energy_fn == NULL) return current_genome;
    double curr_e = energy_fn(current_genome, user_ctx);
    uint64_t next_g = current_genome;
    flow_bmf_anneal_step(current_genome, hard_safety_mask, energy_fn, user_ctx, rng_state, curr_e, temperature, &next_g, NULL);
    return next_g;
}

/**
 * flow_bmf_anneal_loop:
 * Complete closed-loop annealing schedule on the discrete BitManifold.
 */
static inline void flow_bmf_anneal_loop(uint64_t initial_genome,
                                        uint64_t hard_safety_mask,
                                        FlowBMFEnergyFn energy_fn,
                                        void *user_ctx,
                                        size_t iterations,
                                        double t_start,
                                        double t_end,
                                        uint64_t rng_seed,
                                        FlowBMFAnnealResult *result_out) {
    if (result_out == NULL) return;
    memset(result_out, 0, sizeof(*result_out));
    if (energy_fn == NULL || iterations == 0) return;

    uint64_t rng = (rng_seed != 0) ? rng_seed : 0x853c49e6748fea9bULL;
    uint64_t current_g = flow_manifold_project(initial_genome, hard_safety_mask, 0);
    double current_e = energy_fn(current_g, user_ctx);

    uint64_t best_g = current_g;
    double best_e = current_e;

    result_out->initial_genome = current_g;
    result_out->initial_energy = current_e;
    result_out->total_iterations = iterations;

    double temp_decay = 1.0;
    if (iterations > 1 && t_start > 0.0 && t_end > 0.0) {
        temp_decay = pow(t_end / t_start, 1.0 / (double)(iterations - 1));
    }
    double temp = t_start;

    for (size_t i = 0; i < iterations; ++i) {
        uint64_t next_g = current_g;
        double next_e = current_e;
        int accepted = flow_bmf_anneal_step(current_g, hard_safety_mask, energy_fn, user_ctx, &rng,
                                            current_e, temp, &next_g, &next_e);
        if (accepted) {
            result_out->transitions_accepted++;
            current_g = next_g;
            current_e = next_e;
            if (current_e < best_e) {
                best_e = current_e;
                best_g = current_g;
            }
        } else {
            result_out->transitions_rejected++;
        }
        temp *= temp_decay;
    }

    result_out->best_genome = best_g;
    result_out->best_energy = best_e;
}

#ifdef __cplusplus
}
#endif

#endif /* FLOW_BMF_FIXTURE_H */
