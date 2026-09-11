/* fvec/fvec_immune.c
 * Autonomous Immune Promotion, Hebbian Strengthening & Senescence GC,
 * and Subconscious Telemetry Immune Promotion Tracker.
 *
 * Part of the FLOW Living Architecture system.
 * Split from src/flowy_fvec.c for maintainability (Method A: cmd co-located with feature).
 */
#include "../flowy_fvec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------------- */
/* 6. Autonomous Immune Promotion, Hebbian Strengthening & Senescence GC    */
/* ------------------------------------------------------------------------- */

uint64_t flow_fvec_compute_content_hash(uint64_t genome,
                                        uint64_t hard_mask,
                                        uint64_t soft_bias,
                                        const FlowSMTProofAttestation *proof) {
    uint64_t hash = 14695981039346656037ULL;
    const uint8_t *p;
    size_t i;

    p = (const uint8_t *)&genome;
    for (i = 0; i < sizeof(genome); i++) {
        hash ^= p[i];
        hash *= 1099511628211ULL;
    }
    p = (const uint8_t *)&hard_mask;
    for (i = 0; i < sizeof(hard_mask); i++) {
        hash ^= p[i];
        hash *= 1099511628211ULL;
    }
    p = (const uint8_t *)&soft_bias;
    for (i = 0; i < sizeof(soft_bias); i++) {
        hash ^= p[i];
        hash *= 1099511628211ULL;
    }
    if (proof != NULL) {
        p = (const uint8_t *)proof;
        for (i = 0; i < sizeof(*proof); i++) {
            hash ^= p[i];
            hash *= 1099511628211ULL;
        }
    }
    return hash;
}

int flow_fvec_promote_or_strengthen(const char *target_dir,
                                    const FlowVecHeader *base_hdr,
                                    const FlowVecPayload *payload,
                                    uint64_t content_hash,
                                    char *out_filepath,
                                    size_t max_path_len,
                                    uint32_t *new_confidence_out) {
    if (target_dir == NULL || base_hdr == NULL || payload == NULL) return 0;

    char hash_str[32];
    snprintf(hash_str, sizeof(hash_str), "%016llx", (unsigned long long)content_hash);

    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/auto_promoted_%s.fvec", target_dir, hash_str);
    if (out_filepath && max_path_len > 0) {
        strncpy(out_filepath, filepath, max_path_len - 1);
        out_filepath[max_path_len - 1] = '\0';
    }

    uint64_t now_sec = (uint64_t)time(NULL);

    /* Check if file already exists -> Hebbian Strengthening */
    FlowVecHeader existing_hdr;
    FlowVecPayload existing_payload;
    if (flow_fvec_read_file(filepath, &existing_hdr, &existing_payload)) {
        existing_hdr.confidence_score++;
        existing_hdr.last_reinforced_unix = now_sec;
        if (new_confidence_out) *new_confidence_out = existing_hdr.confidence_score;
        return flow_fvec_write_file(filepath, &existing_hdr, &existing_payload);
    }

    /* Mint brand new auto-promoted model */
    FlowVecHeader new_hdr = *base_hdr;
    new_hdr.confidence_score = 1;
    new_hdr.is_auto_promoted = 1;
    new_hdr.created_at_unix = now_sec;
    new_hdr.last_reinforced_unix = now_sec;
    strncpy(new_hdr.content_hash, hash_str, sizeof(new_hdr.content_hash) - 1);
    snprintf(new_hdr.id, sizeof(new_hdr.id), "auto_promoted_%s", hash_str);
    if (new_hdr.name[0] == '\0') {
        snprintf(new_hdr.name, sizeof(new_hdr.name), "Auto-Promoted Antibody [%s]", hash_str);
    }
    if (new_confidence_out) *new_confidence_out = 1;
    return flow_fvec_write_file(filepath, &new_hdr, payload);
}

size_t flow_fvec_store_evict_senescent(FlowVecStore *store,
                                       uint64_t now_unix,
                                       uint64_t max_idle_seconds,
                                       char (*evicted_files_out)[256],
                                       size_t max_evictions) {
    if (store == NULL) return 0;
    size_t evicted_count = 0;
    size_t i = 0;

    while (i < store->count) {
        FlowVecRecord *rec = &store->records[i];
        if (rec->header.is_auto_promoted) {
            uint64_t idle_time = (now_unix >= rec->header.last_reinforced_unix) ?
                                 (now_unix - rec->header.last_reinforced_unix) : 0;
            if (idle_time > max_idle_seconds) {
                if (rec->header.filepath[0] != '\0') {
                    remove(rec->header.filepath);
                    if (evicted_files_out && evicted_count < max_evictions) {
                        strncpy(evicted_files_out[evicted_count], rec->header.filepath, 255);
                        evicted_files_out[evicted_count][255] = '\0';
                    }
                }
                evicted_count++;
                for (size_t j = i; j + 1 < store->count; j++) {
                    store->records[j] = store->records[j + 1];
                }
                store->count--;
                continue;
            }
        }
        i++;
    }
    return evicted_count;
}

/* ------------------------------------------------------------------------- */
/* 7. Subconscious Telemetry Immune Promotion Tracker                        */
/* ------------------------------------------------------------------------- */

void flow_immune_promoter_init(FlowImmunePromoter *promoter, uint64_t threshold) {
    if (promoter == NULL) return;
    memset(promoter, 0, sizeof(*promoter));
    promoter->promotion_threshold = (threshold > 0) ? threshold : FLOW_DEFAULT_IMMUNE_PROMOTION_THRESHOLD;
}

void flow_immune_promoter_set_active(FlowImmunePromoter *promoter,
                                     uint64_t genome,
                                     uint64_t hard_mask,
                                     uint64_t soft_bias,
                                     const FlowSMTProofAttestation *proof,
                                     const char *component_id,
                                     const char *intent,
                                     const char *hardware,
                                     double energy) {
    if (promoter == NULL) return;
    promoter->active_genome = genome;
    promoter->active_mask = hard_mask;
    promoter->active_soft_bias = soft_bias;
    if (proof) promoter->active_proof = *proof;
    if (component_id) strncpy(promoter->active_component_id, component_id, sizeof(promoter->active_component_id) - 1);
    if (intent) strncpy(promoter->trigger_intent, intent, sizeof(promoter->trigger_intent) - 1);
    if (hardware) strncpy(promoter->origin_hardware, hardware, sizeof(promoter->origin_hardware) - 1);
    promoter->active_energy = energy;
    promoter->healthy_requests_count = 0;
    promoter->anomaly_count = 0;
    promoter->is_promoted = false;
}

int flow_immune_promoter_record_request(FlowImmunePromoter *promoter, int is_healthy, int smt_sound) {
    if (promoter == NULL) return 0;
    if (!is_healthy || !smt_sound) {
        promoter->anomaly_count++;
        promoter->healthy_requests_count = 0; /* Reset continuous streak */
        return 0;
    }
    promoter->healthy_requests_count++;
    return (promoter->healthy_requests_count >= promoter->promotion_threshold);
}

int flow_immune_promoter_check_and_promote(FlowImmunePromoter *promoter,
                                          const char *target_dir,
                                          char *promoted_path_out,
                                          size_t max_path_len,
                                          uint32_t *new_confidence_out,
                                          uint8_t lymph_packet_out[9]) {
    if (promoter == NULL || target_dir == NULL) return 0;
    if (promoter->anomaly_count > 0 ||
        promoter->healthy_requests_count < promoter->promotion_threshold) {
        return 0;
    }

    uint64_t hash = flow_fvec_compute_content_hash(promoter->active_genome,
                                                   promoter->active_mask,
                                                   promoter->active_soft_bias,
                                                   &promoter->active_proof);

    FlowVecHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    strncpy(hdr.magic, "FVEC_V1", sizeof(hdr.magic) - 1);
    snprintf(hdr.name, sizeof(hdr.name), "Auto-Promoted [%s]", promoter->trigger_intent);
    strncpy(hdr.origin_hardware, promoter->origin_hardware, sizeof(hdr.origin_hardware) - 1);
    strncpy(hdr.trigger_intent, promoter->trigger_intent, sizeof(hdr.trigger_intent) - 1);
    strncpy(hdr.category, "IMMUNE_AUTO", sizeof(hdr.category) - 1);
    strncpy(hdr.component_id, promoter->active_component_id, sizeof(hdr.component_id) - 1);
    strncpy(hdr.smt_signature, "BUFFER_UNSAT:MEM_UNSAT:SHARD_UNSAT:DET_UNSAT", sizeof(hdr.smt_signature) - 1);
    hdr.energy_score = promoter->active_energy;
    hdr.created_at_unix = (uint64_t)time(NULL);
    hdr.vector_dim = FLOW_VAULT_DIM;
    hdr.payload_size = sizeof(FlowVecPayload);

    FlowVecPayload payload;
    memset(&payload, 0, sizeof(payload));
    payload.pure_genome = promoter->active_genome;
    payload.hard_composite_mask = promoter->active_mask;
    payload.soft_composite_bias = promoter->active_soft_bias;
    payload.proof = promoter->active_proof;
    payload.crc32 = flow_fvec_crc32(&payload, sizeof(payload) - sizeof(uint32_t));

    int ok = flow_fvec_promote_or_strengthen(target_dir, &hdr, &payload, hash,
                                            promoted_path_out, max_path_len, new_confidence_out);
    if (ok) {
        promoter->is_promoted = true;
        if (lymph_packet_out != NULL) {
            lymph_packet_out[0] = 0xAA; /* FLOW_SWARM_MSG_ANTIBODY */
            for (int i = 0; i < 8; i++) {
                lymph_packet_out[1 + i] = (uint8_t)((hash >> (56 - i * 8)) & 0xFF);
            }
        }
    }
    return ok;
}

