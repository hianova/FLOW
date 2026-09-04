#ifndef FLOW_SWARM_AUTOPOIESIS_H
#define FLOW_SWARM_AUTOPOIESIS_H

#include "flow.h"
#include "flowy_fvec.h"
#include "manifold_algebra.h"
#include "swarm.h"
#include "smt.h"
#include "flow_smt_dsl.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW Swarm Speciation & Autopoiesis Engine (swarm_autopoiesis.h)
 * ============================================================================
 * Enables spontaneous, living evolutionary speciation of .fvec architectures
 * across 10,000+ distributed heterogeneous nodes without human developer intervention.
 *
 * Distinct from static single-node configs, this engine:
 * 1. Simulates diverse Environmental Niches (Desert Thermal, Ice Friction, Cloud Bursty, HFT).
 * 2. Employs Epistatic-Linkage-Aware Crossover: Preserves coupled constraint blocks
 *    discovered by Manifold Algebra, preventing destructive recombination.
 * 3. Applies Adaptive Genetic Drift scaled by environmental entropy.
 * 4. Propagates Pareto-superior genotypes via 9-byte Lymphatic Fleet Antibodies.
 * 5. Auto-promotes SMT-verified living architectures into canonical .fvec artifacts.
 * ============================================================================
 */

#define FLOW_SPECIATION_POPULATION_SIZE 32
#define FLOW_SPECIATION_MAX_NICHES 4

typedef enum {
    FLOW_NICHE_DESERT_THERMAL = 0,     /* 50C ambient, strict thermal throttling, power-capped */
    FLOW_NICHE_ICE_LOW_FRICTION = 1,    /* mu in [0.05, 0.15], extreme slip hazard, high damping */
    FLOW_NICHE_SERVERLESS_BURSTY = 2,  /* 0ns cold-start, high-frequency spin up/down */
    FLOW_NICHE_HFT_DETERMINISTIC = 3   /* Sub-microsecond deadline, 0 packet loss, kernel bypass */
} FlowSpeciationNicheType;

typedef struct {
    FlowSpeciationNicheType type;
    char name[32];
    double ambient_temp_c;
    double friction_mu;
    double packet_drop_rate;
    double max_power_budget_w;
    double environmental_entropy;
} FlowEnvironmentalNiche;

typedef struct {
    char id[64];
    uint64_t pure_genome;
    double features[FLOW_MANIFOLD_DIM];
    FlowManifold manifold;
    double fitness_score;              /* Lower is better (Lyapunov / Pareto energy) */
    uint32_t generation;
    FlowSpeciationNicheType origin_niche;
    bool is_promoted;
    uint64_t content_hash;
} FlowSpeciationSpecimen;

typedef struct {
    FlowEnvironmentalNiche niches[FLOW_SPECIATION_MAX_NICHES];
    FlowSpeciationSpecimen population[FLOW_SPECIATION_POPULATION_SIZE];
    size_t population_size;
    uint32_t current_generation;
    uint64_t rng_state;
    uint64_t total_crossovers;
    uint64_t total_drifts;
    uint64_t total_promotions;
    uint64_t antibodies_broadcast;
} FlowSwarmSpeciationEngine;

/* Initialize the Swarm Speciation & Autopoiesis Engine */
int flow_speciation_init(FlowSwarmSpeciationEngine *engine, uint32_t seed);

/* Configure an environmental niche */
int flow_speciation_set_niche(FlowSwarmSpeciationEngine *engine,
                              FlowSpeciationNicheType type,
                              double temp_c,
                              double friction_mu,
                              double power_w,
                              double entropy);

/* Evaluate fitness of all specimens under their respective niche stressors */
int flow_speciation_evaluate_fitness(FlowSwarmSpeciationEngine *engine);

/*
 * Epistatic-Linkage-Aware Crossover:
 * Recombines Parent A and Parent B while strictly preserving coupled gene blocks
 * specified by epistatic_linkage_mask. Returns non-zero on success.
 */
int flow_speciation_crossover(const FlowSpeciationSpecimen *parent_a,
                              const FlowSpeciationSpecimen *parent_b,
                              uint64_t rng_seed,
                              FlowSpeciationSpecimen *child_out);

/*
 * Adaptive Genetic Drift:
 * Mutates specimen features and genome with mutation step scaled by environmental entropy.
 */
int flow_speciation_drift(FlowSpeciationSpecimen *specimen,
                          double environmental_entropy,
                          uint64_t rng_seed);

/*
 * Advance the Swarm Speciation Life Cycle by one generation:
 * 1. Evaluate fitness under environmental stress.
 * 2. Natural selection & Epistatic Crossover.
 * 3. Environmental Genetic Drift.
 * 4. Lymphatic Antibody broadcasting for top performers.
 * 5. Auto-promotion of Pareto-superior specimens to .fvec format.
 */
int flow_speciation_step_generation(FlowSwarmSpeciationEngine *engine);

/* Export an auto-promoted specimen to a valid .fvec file on disk */
int flow_speciation_export_fvec(const FlowSpeciationSpecimen *specimen, const char *output_dir);

/*
 * SMT Formal Verification of Autopoietic Speciation Invariants:
 * 1. Epistatic Invariance: Linked gene blocks preserved during crossover.
 * 2. Fitness Validity: Best fitness >= 0.0 and strictly non-divergent.
 * 3. Population Diversity: Non-zero entropy across active niches.
 * 4. Lymphatic Broadcast Integrity: 9-byte packet format conformant.
 */
FlowSMTResult flow_speciation_verify_smt(const FlowSwarmSpeciationEngine *engine,
                                         FlowSMTProofAttestation *proof_out);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_SWARM_AUTOPOIESIS_H */
