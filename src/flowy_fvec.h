#ifndef FLOW_FLOWY_FVEC_H
#define FLOW_FLOWY_FVEC_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "bitspace.h"
#include "smt.h"
#include "flow.h"

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

#define FLOW_PREDICTOR_MAX_HISTORY 32
typedef struct {
    uint64_t timestamps[FLOW_PREDICTOR_MAX_HISTORY];
    double history[FLOW_PREDICTOR_MAX_HISTORY][FLOW_VAULT_DIM];
    size_t count;
    double kalman_gain;
    double trend_slope[FLOW_VAULT_DIM];
} FlowTimeSeriesPredictor;

#define FLOW_FVEC_MAGIC "FVEC_V1"
#define FLOW_FVEC_HEADER_SIZE 1024
#define FLOW_FVEC_MAX_ENTRIES 128
#define FLOW_FVEC_DEFAULT_DIR ".flow/vecs"

/* ------------------------------------------------------------------------- */
/* 1. Semantic Metadata Header (1024 Bytes Fixed Padded Block)               */
/* ------------------------------------------------------------------------- */
typedef struct {
    char magic[16];                /* "FVEC_V1" */
    char id[64];                   /* e.g., "vec_hft_lockfree_trading" */
    char name[128];                /* Human readable title */
    char origin_hardware[128];     /* e.g., "x86_avx2, L1=64K, Cores=64" */
    char trigger_intent[64];       /* e.g., "HFT_TRADING", "MEMORY_CRITICAL", "SERVERLESS_BURST" */
    char category[32];             /* e.g., "HFT", "SERVERLESS", "IMMUNE", "GENERAL" */
    char component_id[32];         /* e.g., "bounded_queue", "sharded_hash" */
    char description[256];         /* Causal / evolutionary context */
    char smt_signature[64];        /* e.g., "BUFFER_UNSAT:MEM_UNSAT:SHARD_UNSAT:DET_UNSAT" */
    double energy_score;           /* Historical lowest Pareto energy (Delta E) */
    uint64_t created_at_unix;      /* Epoch timestamp */
    uint32_t vector_dim;           /* Always 16 in FLOW */
    uint32_t payload_size;         /* Size of binary payload in bytes */
    uint32_t confidence_score;     /* Hebbian learning weight (strengthened upon recurring events) */
    uint64_t last_reinforced_unix; /* Timestamp of last activation / reinforcement */
    uint8_t is_auto_promoted;      /* 1 if evolved online in battle; 0 if canonical built-in */
    char content_hash[32];         /* 16-hex content-addressable hash string */
    char filepath[256];            /* Filepath when loaded from disk */
} FlowVecHeader;

/* ------------------------------------------------------------------------- */
/* 2. Binary Payload (Highly Compressed for BMF & JIT Engine)        */
/* ------------------------------------------------------------------------- */
typedef struct {
    double features[FLOW_VAULT_DIM];           /* 16-D normalized continuous embedding (128 bytes) */
    uint64_t pure_genome;                      /* 64-bit physical architecture genome (8 bytes) */
    uint64_t hard_composite_mask;              /* 64-bit 1-cycle constraint mask (8 bytes) */
    uint64_t soft_composite_bias;              /* 64-bit Boltzmann probability manifold bias (8 bytes) */
    FlowSMTProofAttestation proof;             /* 4-theorem zero-defect formal status (16 bytes) */
    uint32_t crc32;                            /* Payload data integrity checksum (4 bytes) */
} FlowVecPayload;

/* ------------------------------------------------------------------------- */
/* 3. In-Memory Living Architecture Curator / Gene Vault Store               */
/* ------------------------------------------------------------------------- */
typedef struct {
    FlowVecHeader header;
    FlowVecPayload payload;
} FlowVecRecord;

typedef struct {
    FlowVecRecord records[FLOW_FVEC_MAX_ENTRIES];
    size_t count;
    char root_dir[256];
    uint64_t total_lookups;
} FlowVecStore;

/* ------------------------------------------------------------------------- */
/* 4. Core Serialization & Parsing APIs                                      */
/* ------------------------------------------------------------------------- */

/* CRC32 computation */
uint32_t flow_fvec_crc32(const void *data, size_t length);

/* Serialize Header into exact 1024-byte buffer */
int flow_fvec_header_serialize(const FlowVecHeader *hdr, char *out_buf, size_t buf_size);

/* Parse Header from 1024-byte buffer */
int flow_fvec_header_deserialize(const char *in_buf, size_t buf_size, FlowVecHeader *hdr_out);

/* File read / write */
int flow_fvec_write_file(const char *filepath, const FlowVecHeader *hdr, const FlowVecPayload *payload);
int flow_fvec_read_file(const char *filepath, FlowVecHeader *hdr_out, FlowVecPayload *payload_out);

/* Interop with FlowVaultEntry */
int flow_fvec_from_vault_entry(const FlowVaultEntry *entry,
                               const char *origin_hw,
                               const char *intent,
                               FlowVecHeader *hdr_out,
                               FlowVecPayload *payload_out);

int flow_fvec_to_vault_entry(const FlowVecHeader *hdr,
                             const FlowVecPayload *payload,
                             FlowVaultEntry *entry_out);

/* ------------------------------------------------------------------------- */
/* 5. Curator Store Management & Inverted Index                              */
/* ------------------------------------------------------------------------- */

void flow_fvec_store_init(FlowVecStore *store, const char *dir_path);
int flow_fvec_store_scan(FlowVecStore *store);
const FlowVecRecord *flow_fvec_store_lookup_by_id(const FlowVecStore *store, const char *id);

/* Query by semantic natural language prompt (Prompt-to-Vector) */
int flow_fvec_store_query(const FlowVecStore *store,
                          const char *natural_query,
                          size_t *best_idx_out,
                          double *best_sim_out);

/* Inverted index query by trigger intent */
size_t flow_fvec_store_find_by_intent(const FlowVecStore *store,
                                      const char *intent_keyword,
                                      const FlowVecRecord **out_records,
                                      size_t max_results);

/* Disaster Crisis Remediation: Antibody / Survival Injection Matching */
int flow_fvec_remediate_check(const FlowVecStore *store,
                              double ram_pressure_pct,
                              double cache_miss_rate,
                              const FlowVecRecord **recommended_record_out,
                              double *confidence_out,
                              char *diagnosis_out,
                              size_t max_diag_len);

/* Print and inspect */
void flow_fvec_inspect(const FlowVecHeader *hdr, const FlowVecPayload *payload, FILE *out);
void flow_fvec_store_print_summary(const FlowVecStore *store, FILE *out);

/* Seed default canonical .fvec files into directory */
int flow_fvec_seed_canonical_files(const char *target_dir);

/* ------------------------------------------------------------------------- */
/* 6. Autonomous Immune Promotion, Hebbian Strengthening & Senescence GC    */
/* ------------------------------------------------------------------------- */

/* Content-Addressable Hash over Mask Topology + SMT Theorem State */
uint64_t flow_fvec_compute_content_hash(uint64_t genome,
                                        uint64_t hard_mask,
                                        uint64_t soft_bias,
                                        const FlowSMTProofAttestation *proof);

/* Promote new antibody or Hebbian-strengthen existing one */
int flow_fvec_promote_or_strengthen(const char *target_dir,
                                    const FlowVecHeader *base_hdr,
                                    const FlowVecPayload *payload,
                                    uint64_t content_hash,
                                    char *out_filepath,
                                    size_t max_path_len,
                                    uint32_t *new_confidence_out);

/* Immune Senescence: LRU Eviction of idle auto-promoted models */
size_t flow_fvec_store_evict_senescent(FlowVecStore *store,
                                       uint64_t now_unix,
                                       uint64_t max_idle_seconds,
                                       char (*evicted_files_out)[256],
                                       size_t max_evictions);

/* ------------------------------------------------------------------------- */
/* 7. Subconscious Telemetry Immune Promotion Tracker                        */
/* ------------------------------------------------------------------------- */
#define FLOW_DEFAULT_IMMUNE_PROMOTION_THRESHOLD 1000000ULL

typedef struct {
    uint64_t active_genome;
    uint64_t active_mask;
    uint64_t active_soft_bias;
    FlowSMTProofAttestation active_proof;
    char active_component_id[32];
    char trigger_intent[64];
    char origin_hardware[128];
    double active_energy;

    uint64_t healthy_requests_count;
    uint64_t anomaly_count;
    uint64_t promotion_threshold; /* e.g. 1,000,000 */
    bool is_promoted;
} FlowImmunePromoter;

void flow_immune_promoter_init(FlowImmunePromoter *promoter, uint64_t threshold);
void flow_immune_promoter_set_active(FlowImmunePromoter *promoter,
                                     uint64_t genome,
                                     uint64_t hard_mask,
                                     uint64_t soft_bias,
                                     const FlowSMTProofAttestation *proof,
                                     const char *component_id,
                                     const char *intent,
                                     const char *hardware,
                                     double energy);
int flow_immune_promoter_record_request(FlowImmunePromoter *promoter, int is_healthy, int smt_sound);
int flow_immune_promoter_check_and_promote(FlowImmunePromoter *promoter,
                                          const char *target_dir,
                                          char *promoted_path_out,
                                          size_t max_path_len,
                                          uint32_t *new_confidence_out,
                                          uint8_t lymph_packet_out[9]);

/* ------------------------------------------------------------------------- */
/* 8. Flowy Hub: Ecosystem .fvec Community Sharing & Gene Vault Repository  */
/* ------------------------------------------------------------------------- */
#define FLOW_HUB_MAX_ENTRIES 32

typedef struct {
    char model_id[64];
    char name[128];
    char author[64];
    char category[32];
    char origin_hardware[64];
    char smt_signature[64];
    uint32_t confidence_score;
    char description[256];
    uint64_t pure_genome;
    double energy_score;
} FlowHubEntry;

typedef struct {
    size_t count;
    FlowHubEntry entries[FLOW_HUB_MAX_ENTRIES];
} FlowHubIndex;

int flow_hub_init_local_index(FlowHubIndex *idx);
int flow_hub_search(const FlowHubIndex *idx, const char *query,
                    FlowHubEntry *matches_out, size_t max_matches, size_t *found_count);
const FlowHubEntry *flow_hub_lookup(const FlowHubIndex *idx, const char *model_id);
int flow_hub_pull(const FlowHubIndex *idx, const char *model_id,
                  const char *dest_dir, char *saved_path_out, size_t max_path_len);
int flow_hub_push_package(const char *fvec_path, const char *author,
                          char *out_package_meta, size_t max_meta_len);

/* ------------------------------------------------------------------------- */
/* 9. Universal Lockfile & Hardware Affinity Enforcement                     */
/* ------------------------------------------------------------------------- */

int flow_fvec_verify_hardware_affinity(const FlowVecHeader *hdr,
                                       const FlowEnvironmentState *host_env,
                                       char *diag_msg, size_t max_len);

/* ------------------------------------------------------------------------- */
/* 10. Vector Manifold, Prompt Embedding & Dynamic Morphing Operations       */
/* ------------------------------------------------------------------------- */

void flow_vault_init(FlowVectorVault *vault);
int flow_vault_add_entry(FlowVectorVault *vault, const FlowVaultEntry *entry);
int flow_vault_seed_canonical_archetypes(FlowVectorVault *vault);
const FlowVaultEntry *flow_vault_get(const FlowVectorVault *vault, size_t index);
const FlowVaultEntry *flow_vault_lookup_by_id(const FlowVectorVault *vault, const char *id);

double flow_vault_cosine_similarity(const double *a, const double *b, size_t dim);
int flow_vault_query_nearest(FlowVectorVault *vault, const double *query_features,
                             FlowVaultCategory category_filter,
                             size_t *best_idx_out, double *best_sim_out);

void flow_vault_embed_prompt(const char *prompt, double *out_features);
int flow_vault_query_semantic(FlowVectorVault *vault, const char *prompt,
                              size_t *best_idx_out, double *best_sim_out);

int flow_vault_serverless_coldstart(FlowVectorVault *vault,
                                    const SemanticIR *ir,
                                    double cache_miss, double ipc,
                                    FlowPlan *plan_out,
                                    FlowMaskCanvas *canvas_out,
                                    double *lookup_us_out);

int flow_vault_broadcast_antibody(const FlowVectorVault *vault,
                                  const FlowVaultEntry *antibody,
                                  char *packet_buffer, size_t max_buf_len);
int flow_vault_ingest_antibody(FlowVectorVault *vault,
                               const char *packet_buffer,
                               size_t *ingested_idx_out);

int flow_vault_vector_interpolate(const double *vec_a, const double *vec_b, double alpha, double *out_interpolated);
int flow_vault_tidal_morph(const FlowVaultEntry *day_entry, const FlowVaultEntry *night_entry,
                            double alpha, FlowMaskCanvas *out_canvas, uint64_t *out_seed_genome);

const char *flow_hardware_arch_name(FlowHardwareArch arch);
int flow_vault_export_dna(const FlowVaultEntry *entry, FlowHardwareArch source_arch, char *dna_buffer, size_t max_len);
int flow_vault_import_dna(FlowVectorVault *vault, const char *dna_buffer,
                          FlowHardwareArch target_arch, size_t *imported_idx_out,
                          double *adaptation_confidence_out);

void flow_predictor_init(FlowTimeSeriesPredictor *p);
void flow_predictor_observe(FlowTimeSeriesPredictor *p, uint64_t timestamp_ns, const double *features);
int flow_predictor_forecast(const FlowTimeSeriesPredictor *p, uint64_t future_horizon_ns,
                            double *out_predicted_features, double *out_trend_slope);
int flow_vault_proactive_prewarm(FlowVectorVault *vault,
                                 const FlowTimeSeriesPredictor *predictor,
                                 uint64_t lookahead_ns,
                                 FlowPlan *prewarmed_plan_out,
                                 int *prewarm_triggered_out);

int flow_vault_generative_synthesis(FlowVectorVault *vault,
                                    const char *radical_prompt,
                                    uint64_t random_seed,
                                    FlowVaultEntry *out_synthesized_species,
                                    FlowSMTProofAttestation *out_proof);

int flow_vault_save_file(const FlowVectorVault *vault, const char *filepath);
int flow_vault_load_file(FlowVectorVault *vault, const char *filepath);

int flow_vault_sync_from_dir(FlowVectorVault *vault, const char *dirpath);
int flow_vault_sync_to_dir(const FlowVectorVault *vault, const char *dirpath);

void flow_vault_print_entry(const FlowVaultEntry *entry, FILE *out);
void flow_vault_print_summary(const FlowVectorVault *vault, FILE *out);

#endif /* FLOW_FLOWY_FVEC_H */
