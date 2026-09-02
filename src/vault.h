#ifndef FLOW_VAULT_H
#define FLOW_VAULT_H

#include "bitspace.h"
#include "smt.h"
#include "flow.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#define FLOW_VAULT_DIM 16
#define FLOW_VAULT_MAX_ENTRIES 64

typedef enum {
    FLOW_VAULT_CAT_GENERAL = 0,
    FLOW_VAULT_CAT_SERVERLESS = 1,        /* Zero-Cold-Start microservice profiles */
    FLOW_VAULT_CAT_IMMUNE_ANTIBODY = 2,    /* Fleet-wide defense against pathological attacks / DDoS */
    FLOW_VAULT_CAT_SEMANTIC_RAG = 3       /* Developer prompt-to-architecture archetypes */
} FlowVaultCategory;

typedef struct {
    char id[64];
    char name[128];
    char description[256];
    char semantic_keywords[256];
    FlowVaultCategory category;

    double features[FLOW_VAULT_DIM];
    /*
     * Feature Dimensions:
     * [0] log2(input_scale) normalized
     * [1] log2(memory_limit_mb) normalized
     * [2] shared_state (0 or 1)
     * [3] read_heavy (0 or 1)
     * [4] ordered (0 or 1)
     * [5] parallelizable (0 or 1)
     * [6] cache_miss_rate (0.0 to 1.0)
     * [7] ipc normalized (0.0 to 1.0)
     * [8] latency_priority (0.0=memory, 1.0=latency)
     * [9] thread_concurrency_norm (0.0 to 1.0)
     * [10] security_compliance_level (0.0=loose, 1.0=strict_prod)
     * [11] socket_descriptor_pressure (0.0 to 1.0)
     * [12..15] Semantic intent hash projections
     */

    uint64_t pure_genome;
    FlowMaskCanvas canvas;
    FlowSMTProofAttestation proof;
    char component_id[64];
    double baseline_energy;

    uint64_t creation_timestamp_ns;
    char origin_node_id[64];
    uint32_t times_recalled;
} FlowVaultEntry;

typedef struct {
    FlowVaultEntry entries[FLOW_VAULT_MAX_ENTRIES];
    size_t count;
    char vault_path[256];
    uint64_t total_lookups;
    uint64_t total_microsecond_savings;
} FlowVectorVault;

/* Lifecycle & Management */
void flow_vault_init(FlowVectorVault *vault);
int flow_vault_add_entry(FlowVectorVault *vault, const FlowVaultEntry *entry);
int flow_vault_seed_canonical_archetypes(FlowVectorVault *vault);
const FlowVaultEntry *flow_vault_get(const FlowVectorVault *vault, size_t index);
const FlowVaultEntry *flow_vault_lookup_by_id(const FlowVectorVault *vault, const char *id);

/* Vector & Semantic Retrieval */
double flow_vault_cosine_similarity(const double *a, const double *b, size_t dim);
int flow_vault_query_nearest(FlowVectorVault *vault, const double *query_features,
                             FlowVaultCategory category_filter, /* -1 for all */
                             size_t *best_idx_out, double *best_sim_out);

/* Semantic Topology RAG: Prompt -> Embedding -> Canva_Vec */
void flow_vault_embed_prompt(const char *prompt, double *out_features);
int flow_vault_query_semantic(FlowVectorVault *vault, const char *prompt,
                              size_t *best_idx_out, double *best_sim_out);

/* Scenario 1: Serverless Zero-Cold-Start Loader */
int flow_vault_serverless_coldstart(FlowVectorVault *vault,
                                    const SemanticIR *ir,
                                    double cache_miss, double ipc,
                                    FlowPlan *plan_out,
                                    FlowMaskCanvas *canvas_out,
                                    double *lookup_us_out);

/* Scenario 2: Fleet-Wide Immune Antibody Gossip Protocol */
int flow_vault_broadcast_antibody(const FlowVectorVault *vault,
                                  const FlowVaultEntry *antibody,
                                  char *packet_buffer, size_t max_buf_len);
int flow_vault_ingest_antibody(FlowVectorVault *vault,
                               const char *packet_buffer,
                               size_t *ingested_idx_out);

/* Advanced Paradigm 1: Vector Interpolation & Tidal Morphing */
int flow_vault_vector_interpolate(const double *vec_a, const double *vec_b, double alpha, double *out_interpolated);
int flow_vault_tidal_morph(const FlowVaultEntry *day_entry, const FlowVaultEntry *night_entry,
                            double alpha, FlowMaskCanvas *out_canvas, uint64_t *out_seed_genome);

/* Advanced Paradigm 2: Cross-Hardware Zero-Shot Transfer */
const char *flow_hardware_arch_name(FlowHardwareArch arch);
int flow_vault_export_dna(const FlowVaultEntry *entry, FlowHardwareArch source_arch, char *dna_buffer, size_t max_len);
int flow_vault_import_dna(FlowVectorVault *vault, const char *dna_buffer,
                          FlowHardwareArch target_arch, size_t *imported_idx_out,
                          double *adaptation_confidence_out);

/* Advanced Paradigm 3: Time-Series Prediction & Proactive JIT Pre-warming */
#define FLOW_PREDICTOR_MAX_HISTORY 32
typedef struct {
    uint64_t timestamps[FLOW_PREDICTOR_MAX_HISTORY];
    double history[FLOW_PREDICTOR_MAX_HISTORY][FLOW_VAULT_DIM];
    size_t count;
    double kalman_gain;
    double trend_slope[FLOW_VAULT_DIM];
} FlowTimeSeriesPredictor;

void flow_predictor_init(FlowTimeSeriesPredictor *p);
void flow_predictor_observe(FlowTimeSeriesPredictor *p, uint64_t timestamp_ns, const double *features);
int flow_predictor_forecast(const FlowTimeSeriesPredictor *p, uint64_t future_horizon_ns,
                            double *out_predicted_features, double *out_trend_slope);
int flow_vault_proactive_prewarm(FlowVectorVault *vault,
                                 const FlowTimeSeriesPredictor *predictor,
                                 uint64_t lookahead_ns,
                                 FlowPlan *prewarmed_plan_out,
                                 int *prewarm_triggered_out);

/* Advanced Paradigm 4: Generative Architecture Synthesis (Prompt-to-Architecture Latent Generator) */
int flow_vault_generative_synthesis(FlowVectorVault *vault,
                                    const char *radical_prompt,
                                    uint64_t random_seed,
                                    FlowVaultEntry *out_synthesized_species,
                                    FlowSMTProofAttestation *out_proof);

/* Persistence I/O */
int flow_vault_save_file(const FlowVectorVault *vault, const char *filepath);
int flow_vault_load_file(FlowVectorVault *vault, const char *filepath);

/* Formatting & Inspection */
void flow_vault_print_entry(const FlowVaultEntry *entry, FILE *out);
void flow_vault_print_summary(const FlowVectorVault *vault, FILE *out);

#endif
