/* fvec/fvec_immune.c - Autonomous Immune Promotion, Hebbian Strengthening & Senescence GC */
#include "../flowy_fvec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t fnv1a_bytes(uint64_t h, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    for (size_t i = 0; i < len; i++) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}

uint64_t flow_fvec_compute_content_hash(uint64_t genome, uint64_t hard_mask, uint64_t soft_bias,
                                        const FlowSMTProofAttestation *proof) {
    uint64_t h = 14695981039346656037ULL;
    h = fnv1a_bytes(h, &genome, sizeof(genome));
    h = fnv1a_bytes(h, &hard_mask, sizeof(hard_mask));
    h = fnv1a_bytes(h, &soft_bias, sizeof(soft_bias));
    if (proof) h = fnv1a_bytes(h, proof, sizeof(*proof));
    return h;
}

int flow_fvec_promote_or_strengthen(const char *target_dir, const FlowVecHeader *base_hdr,
                                    const FlowVecPayload *payload, uint64_t content_hash,
                                    char *out_filepath, size_t max_path_len, uint32_t *new_confidence_out) {
    if (!target_dir || !base_hdr || !payload) return 0;
    char hash_str[32], filepath[512];
    snprintf(hash_str, sizeof(hash_str), "%016llx", (unsigned long long)content_hash);
    snprintf(filepath, sizeof(filepath), "%s/auto_promoted_%s.fvec", target_dir, hash_str);
    if (out_filepath && max_path_len > 0) { strncpy(out_filepath, filepath, max_path_len - 1); out_filepath[max_path_len - 1] = '\0'; }

    uint64_t now_sec = (uint64_t)time(NULL);
    FlowVecHeader ex_hdr; FlowVecPayload ex_pl;
    if (flow_fvec_read_file(filepath, &ex_hdr, &ex_pl)) {
        ex_hdr.confidence_score++;
        ex_hdr.last_reinforced_unix = now_sec;
        if (new_confidence_out) *new_confidence_out = ex_hdr.confidence_score;
        return flow_fvec_write_file(filepath, &ex_hdr, &ex_pl);
    }

    FlowVecHeader nh = *base_hdr;
    nh.confidence_score = nh.is_auto_promoted = 1;
    nh.created_at_unix = nh.last_reinforced_unix = now_sec;
    strncpy(nh.content_hash, hash_str, sizeof(nh.content_hash) - 1);
    snprintf(nh.id, sizeof(nh.id), "auto_promoted_%s", hash_str);
    if (!nh.name[0]) snprintf(nh.name, sizeof(nh.name), "Auto-Promoted Antibody [%s]", hash_str);
    if (new_confidence_out) *new_confidence_out = 1;
    return flow_fvec_write_file(filepath, &nh, payload);
}

size_t flow_fvec_store_evict_senescent(FlowVecStore *store, uint64_t now_unix, uint64_t max_idle_seconds,
                                       char (*evicted_files_out)[256], size_t max_evictions) {
    if (!store) return 0;
    size_t evicted = 0, i = 0;
    while (i < store->count) {
        FlowVecRecord *rec = &store->records[i];
        uint64_t idle = (now_unix >= rec->header.last_reinforced_unix) ? (now_unix - rec->header.last_reinforced_unix) : 0;
        if (rec->header.is_auto_promoted && idle > max_idle_seconds) {
            if (rec->header.filepath[0]) {
                remove(rec->header.filepath);
                if (evicted_files_out && evicted < max_evictions) {
                    strncpy(evicted_files_out[evicted], rec->header.filepath, 255);
                    evicted_files_out[evicted][255] = '\0';
                }
            }
            evicted++;
            memmove(&store->records[i], &store->records[i + 1], sizeof(FlowVecRecord) * (store->count - i - 1));
            store->count--;
        } else {
            i++;
        }
    }
    return evicted;
}

void flow_immune_promoter_init(FlowImmunePromoter *promoter, uint64_t threshold) {
    if (!promoter) return;
    memset(promoter, 0, sizeof(*promoter));
    promoter->promotion_threshold = threshold > 0 ? threshold : FLOW_DEFAULT_IMMUNE_PROMOTION_THRESHOLD;
}

void flow_immune_promoter_set_active(FlowImmunePromoter *p, uint64_t genome, uint64_t hard_mask, uint64_t soft_bias,
                                     const FlowSMTProofAttestation *proof, const char *component_id,
                                     const char *intent, const char *hardware, double energy) {
    if (!p) return;
    p->active_genome = genome; p->active_mask = hard_mask; p->active_soft_bias = soft_bias;
    if (proof) p->active_proof = *proof;
    if (component_id) strncpy(p->active_component_id, component_id, sizeof(p->active_component_id) - 1);
    if (intent) strncpy(p->trigger_intent, intent, sizeof(p->trigger_intent) - 1);
    if (hardware) strncpy(p->origin_hardware, hardware, sizeof(p->origin_hardware) - 1);
    p->active_energy = energy;
    p->healthy_requests_count = p->anomaly_count = 0;
    p->is_promoted = false;
}

int flow_immune_promoter_record_request(FlowImmunePromoter *p, int is_healthy, int smt_sound) {
    if (!p) return 0;
    if (!is_healthy || !smt_sound) { p->anomaly_count++; p->healthy_requests_count = 0; return 0; }
    return (++p->healthy_requests_count >= p->promotion_threshold);
}

int flow_immune_promoter_check_and_promote(FlowImmunePromoter *p, const char *target_dir,
                                           char *promoted_path_out, size_t max_path_len,
                                           uint32_t *new_confidence_out, uint8_t lymph_packet_out[9]) {
    if (!p || !target_dir || p->anomaly_count > 0 || p->healthy_requests_count < p->promotion_threshold) return 0;
    uint64_t hash = flow_fvec_compute_content_hash(p->active_genome, p->active_mask, p->active_soft_bias, &p->active_proof);

    FlowVecHeader hdr; memset(&hdr, 0, sizeof(hdr));
    strncpy(hdr.magic, "FVEC_V1", sizeof(hdr.magic) - 1);
    snprintf(hdr.name, sizeof(hdr.name), "Auto-Promoted [%s]", p->trigger_intent);
    strncpy(hdr.origin_hardware, p->origin_hardware, sizeof(hdr.origin_hardware) - 1);
    strncpy(hdr.trigger_intent, p->trigger_intent, sizeof(hdr.trigger_intent) - 1);
    strncpy(hdr.category, "IMMUNE_AUTO", sizeof(hdr.category) - 1);
    strncpy(hdr.component_id, p->active_component_id, sizeof(hdr.component_id) - 1);
    strncpy(hdr.smt_signature, "BUFFER_UNSAT:MEM_UNSAT:SHARD_UNSAT:DET_UNSAT", sizeof(hdr.smt_signature) - 1);
    hdr.energy_score = p->active_energy;
    hdr.created_at_unix = (uint64_t)time(NULL);
    hdr.vector_dim = FLOW_VAULT_DIM;
    hdr.payload_size = sizeof(FlowVecPayload);

    FlowVecPayload payload; memset(&payload, 0, sizeof(payload));
    payload.pure_genome = p->active_genome;
    payload.hard_composite_mask = p->active_mask;
    payload.soft_composite_bias = p->active_soft_bias;
    payload.proof = p->active_proof;
    payload.crc32 = flow_fvec_crc32(&payload, sizeof(payload) - sizeof(uint32_t));

    if (!flow_fvec_promote_or_strengthen(target_dir, &hdr, &payload, hash, promoted_path_out, max_path_len, new_confidence_out))
        return 0;
    p->is_promoted = true;
    if (lymph_packet_out) {
        lymph_packet_out[0] = 0xAA;
        for (int i = 0; i < 8; i++) lymph_packet_out[1 + i] = (uint8_t)((hash >> (56 - i * 8)) & 0xFF);
    }
    return 1;
}
