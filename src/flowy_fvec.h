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
#define FLOW_FVEC_MAGIC "FVEC_V1"
#define FLOW_FVEC_HEADER_SIZE 1024
#define FLOW_FVEC_MAX_ENTRIES 128
#define FLOW_FVEC_DEFAULT_DIR ".flow/vecs"
typedef struct { char magic[16]; char id[64]; char name[128]; char origin_hardware[128]; char trigger_intent[64]; char category[32]; char component_id[32];
    char description[256]; char smt_signature[64]; double energy_score; uint64_t created_at_unix; uint32_t vector_dim; uint32_t payload_size; uint32_t confidence_score;
    uint64_t last_reinforced_unix; uint8_t is_auto_promoted; char content_hash[32]; char filepath[256]; } FlowVecHeader; typedef struct { double features[FLOW_VAULT_DIM];
    uint64_t pure_genome; uint64_t hard_composite_mask; uint64_t soft_composite_bias; FlowSMTProofAttestation proof; uint32_t crc32; } FlowVecPayload; typedef struct {
    FlowVecHeader header; FlowVecPayload payload; } FlowVecRecord; typedef struct { FlowVecRecord records[FLOW_FVEC_MAX_ENTRIES]; size_t count; char root_dir[256];
    uint64_t total_lookups; } FlowVecStore; uint32_t flow_fvec_crc32(const void *data, size_t length);
int flow_fvec_header_serialize(const FlowVecHeader *hdr, char *out_buf, size_t buf_size);
int flow_fvec_header_deserialize(const char *in_buf, size_t buf_size, FlowVecHeader *hdr_out);
int flow_fvec_write_file(const char *filepath, const FlowVecHeader *hdr, const FlowVecPayload *payload);
int flow_fvec_read_file(const char *filepath, FlowVecHeader *hdr_out, FlowVecPayload *payload_out); void flow_fvec_embed_prompt(const char *prompt, double *out_features);
double flow_fvec_cosine_similarity(const double *a, const double *b, size_t dim); void flow_fvec_store_init(FlowVecStore *store, const char *dir_path);
int flow_fvec_store_scan(FlowVecStore *store); const FlowVecRecord *flow_fvec_store_lookup_by_id(const FlowVecStore *store, const char *id);
int flow_fvec_store_query(const FlowVecStore *store, const char *natural_query, size_t *best_idx_out, double *best_sim_out);
size_t flow_fvec_store_find_by_intent(const FlowVecStore *store, const char *intent_keyword, const FlowVecRecord **out_records, size_t max_results);
int flow_fvec_remediate_check(const FlowVecStore *store, double ram_pressure_pct, double cache_miss_rate, const FlowVecRecord **recommended_record_out, double *confidence_out,
                              char *diagnosis_out, size_t max_diag_len); void flow_fvec_inspect(const FlowVecHeader *hdr, const FlowVecPayload *payload, FILE *out);
void flow_fvec_store_print_summary(const FlowVecStore *store, FILE *out); int flow_fvec_seed_canonical_files(const char *target_dir);
int flow_fvec_verify_hardware_affinity(const FlowVecHeader *hdr, const FlowEnvironmentState *host_env, char *diag_msg, size_t max_len);
#define FLOW_DEFAULT_IMMUNE_PROMOTION_THRESHOLD 1000000ULL
typedef struct { uint64_t active_genome; uint64_t active_mask; uint64_t active_soft_bias; FlowSMTProofAttestation active_proof; char active_component_id[32];
    char trigger_intent[64]; char origin_hardware[128]; double active_energy; uint64_t healthy_requests_count; uint64_t anomaly_count; uint64_t promotion_threshold;
    bool is_promoted; } FlowImmunePromoter;
uint64_t flow_fvec_compute_content_hash(uint64_t genome, uint64_t hard_mask, uint64_t soft_bias, const FlowSMTProofAttestation *proof);
int flow_fvec_promote_or_strengthen(const char *target_dir, const FlowVecHeader *base_hdr, const FlowVecPayload *payload,
                                    uint64_t content_hash, char *out_filepath, size_t max_path_len, uint32_t *new_confidence_out);
size_t flow_fvec_store_evict_senescent(FlowVecStore *store, uint64_t now_unix, uint64_t max_idle_seconds, char (*evicted_files_out)[256], size_t max_evictions);
void flow_immune_promoter_init(FlowImmunePromoter *promoter, uint64_t threshold);
void flow_immune_promoter_set_active(FlowImmunePromoter *promoter, uint64_t genome, uint64_t hard_mask, uint64_t soft_bias,
                                     const FlowSMTProofAttestation *proof, const char *component_id, const char *intent, const char *hardware, double energy);
int flow_immune_promoter_record_request(FlowImmunePromoter *promoter, int is_healthy, int smt_sound);
int flow_immune_promoter_check_and_promote(FlowImmunePromoter *promoter, const char *target_dir, char *promoted_path_out,
                                           size_t max_path_len, uint32_t *new_confidence_out, uint8_t lymph_packet_out[9]);
#define FLOW_FVEC_MAX_PRESTAGED 8
typedef struct { char trigger_condition[64]; char fvec_path[256]; FlowVecHeader header; FlowVecPayload payload; FlowBmf1BitCanvas staged_canvas; int is_staged_sound;
} FlowFvecPreStagingSlot; typedef struct { FlowFvecPreStagingSlot slots[FLOW_FVEC_MAX_PRESTAGED]; size_t slot_count; uint64_t total_speculative_swaps;
    double last_swap_latency_ns; } FlowFvecPreStagingVault; int flow_fvec_prestaging_init(FlowFvecPreStagingVault *vault);
int flow_fvec_prestaging_register(FlowFvecPreStagingVault *vault, const char *fvec_file_path, const char *trigger_condition);
int flow_fvec_prestaging_swap_atomic(FlowFvecPreStagingVault *vault, const char *trigger_condition, FlowBmf1BitCanvas *active_canvas_out, double *swap_latency_ns_out);
FlowSMTResult flow_fvec_verify_prestaging_soundness_smt(const FlowFvecPreStagingVault *vault, const char *trigger_condition,
                                                        double swap_latency_ns, FlowSMTProofAttestation *proof_out);
#endif /* FLOW_FLOWY_FVEC_H */
