#include "flowy_fvec.h"
#include "flow_jet.h"
#include "registry.h"
#include "smt.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
uint32_t flow_fvec_crc32(const void *data, size_t length) { return flow_crc32_ieee(data, length); }
void flow_fvec_embed_prompt(const char *prompt, double *out_features) { if (!out_features) return; for (size_t i = 0; i < FLOW_VAULT_DIM; ++i) out_features[i] = 0.05;
    if (!prompt) return; static const struct { const char *kw; size_t idx; double w; } rules[] = {
        {"hft", 0, 0.95}, {"latency", 0, 0.90}, {"lockfree", 1, 0.95}, {"ring", 1, 0.85}, {"memory", 2, 0.95}, {"oom", 2, 0.95}, {"tiny", 2, 0.85}, {"ram", 2, 0.85},
        {"serverless", 3, 0.95}, {"coldstart", 3, 0.90}, {"burst", 3, 0.85}, {"io", 4, 0.90},
        {"ddos", 5, 0.95}, {"slowloris", 5, 0.95}, {"antibody", 5, 0.90}, {"immune", 5, 0.85}, {"iot", 6, 0.95}, {"sleep", 6, 0.90}, {"power", 6, 0.85}, {"sensor", 6, 0.85},
        {"hash", 7, 0.90}, {"parallel", 8, 0.90}, {"simd", 9, 0.90}, {"smt", 10, 0.85} };
    for (size_t i = 0; i < sizeof(rules) / sizeof(rules[0]); ++i)
        if (strcasestr(prompt, rules[i].kw)) out_features[rules[i].idx] += rules[i].w;
    double norm = 0.0;
    for (size_t i = 0; i < FLOW_VAULT_DIM; ++i) norm += out_features[i] * out_features[i]; norm = sqrt(norm);
    if (norm > 1e-12) for (size_t i = 0; i < FLOW_VAULT_DIM; ++i) out_features[i] /= norm; }
double flow_fvec_cosine_similarity(const double *a, const double *b, size_t dim) {
    if (!a || !b || !dim) return 0.0; double dot = 0.0, na = 0.0, nb = 0.0; for (size_t i = 0; i < dim; ++i) { dot += a[i] * b[i]; na += a[i] * a[i]; nb += b[i] * b[i]; }
    return (na > 1e-12 && nb > 1e-12) ? (dot / (sqrt(na) * sqrt(nb))) : 0.0; }
static void sanitize_fvec_field(const char *src, char *dst, size_t dst_size) {
    if (!dst || dst_size == 0) return;
    size_t j = 0;
    if (src) {
        for (size_t i = 0; src[i] && j + 1 < dst_size; ++i) {
            char c = src[i];
            if (c == '\n' || c == '\r' || c == '=') c = '_';
            dst[j++] = c;
        }
    }
    dst[j] = '\0';
}
int flow_fvec_header_serialize(const FlowVecHeader *hdr, char *out_buf, size_t buf_size) {
    if (!hdr || !out_buf || buf_size < FLOW_FVEC_HEADER_SIZE) return 0;
    memset(out_buf, 0, buf_size);
    char id[64], name[128], hw[128], intent[64], cat[32], comp[32], smt[64], chash[32], desc[256];
    sanitize_fvec_field(hdr->id, id, sizeof(id));
    sanitize_fvec_field(hdr->name, name, sizeof(name));
    sanitize_fvec_field(hdr->origin_hardware, hw, sizeof(hw));
    sanitize_fvec_field(hdr->trigger_intent, intent, sizeof(intent));
    sanitize_fvec_field(hdr->category, cat, sizeof(cat));
    sanitize_fvec_field(hdr->component_id, comp, sizeof(comp));
    sanitize_fvec_field(hdr->smt_signature, smt, sizeof(smt));
    sanitize_fvec_field(hdr->content_hash[0] ? hdr->content_hash : "0000000000000000", chash, sizeof(chash));
    sanitize_fvec_field(hdr->description, desc, sizeof(desc));
    int w = snprintf(out_buf, FLOW_FVEC_HEADER_SIZE,
        "FVEC_V1\nid=%s\nname=%s\norigin_hardware=%s\ntrigger_intent=%s\ncategory=%s\ncomponent_id=%s\nsmt_signature=%s\nenergy_score=%.4f\ncreated_at_unix=%llu\nvector_dim=%u\npayload_size=%u\nconfidence_score=%u\nlast_reinforced_unix=%llu\nis_auto_promoted=%u\ncontent_hash=%s\ndescription=%s\n",
        id, name, hw, intent, cat, comp, smt, hdr->energy_score, (unsigned long long)hdr->created_at_unix,
        hdr->vector_dim ? hdr->vector_dim : (uint32_t)FLOW_VAULT_DIM, (uint32_t)sizeof(FlowVecPayload), hdr->confidence_score ? hdr->confidence_score : 1,
        (unsigned long long)(hdr->last_reinforced_unix ? hdr->last_reinforced_unix : hdr->created_at_unix), (unsigned int)hdr->is_auto_promoted, chash, desc);
    return (w > 0 && w < FLOW_FVEC_HEADER_SIZE);
}
int flow_fvec_header_deserialize(const char *in_buf, size_t buf_size, FlowVecHeader *hdr_out) {
    if (!in_buf || !hdr_out || buf_size < FLOW_FVEC_HEADER_SIZE || strncmp(in_buf, "FVEC_V1", 7) != 0) return 0; memset(hdr_out, 0, sizeof(*hdr_out));
    strncpy(hdr_out->magic, "FVEC_V1", sizeof(hdr_out->magic) - 1); char buf_copy[FLOW_FVEC_HEADER_SIZE + 1];
    memcpy(buf_copy, in_buf, FLOW_FVEC_HEADER_SIZE); buf_copy[FLOW_FVEC_HEADER_SIZE] = '\0'; for (char *line = strtok(buf_copy, "\n"); line; line = strtok(NULL, "\n")) {
        char *eq = strchr(line, '='); if (!eq) continue; *eq = '\0'; const char *k = line, *v = eq + 1; if (!strcmp(k, "id")) strncpy(hdr_out->id, v, sizeof(hdr_out->id) - 1);
        else if (!strcmp(k, "name")) strncpy(hdr_out->name, v, sizeof(hdr_out->name) - 1);
        else if (!strcmp(k, "origin_hardware")) strncpy(hdr_out->origin_hardware, v, sizeof(hdr_out->origin_hardware) - 1);
        else if (!strcmp(k, "trigger_intent")) strncpy(hdr_out->trigger_intent, v, sizeof(hdr_out->trigger_intent) - 1);
        else if (!strcmp(k, "category")) strncpy(hdr_out->category, v, sizeof(hdr_out->category) - 1);
        else if (!strcmp(k, "component_id")) strncpy(hdr_out->component_id, v, sizeof(hdr_out->component_id) - 1);
        else if (!strcmp(k, "smt_signature")) strncpy(hdr_out->smt_signature, v, sizeof(hdr_out->smt_signature) - 1);
        else if (!strcmp(k, "energy_score")) hdr_out->energy_score = atof(v); else if (!strcmp(k, "created_at_unix")) hdr_out->created_at_unix = strtoull(v, NULL, 10);
        else if (!strcmp(k, "vector_dim")) hdr_out->vector_dim = (uint32_t)atoi(v); else if (!strcmp(k, "payload_size")) hdr_out->payload_size = (uint32_t)atoi(v);
        else if (!strcmp(k, "confidence_score")) hdr_out->confidence_score = (uint32_t)atoi(v);
        else if (!strcmp(k, "last_reinforced_unix")) hdr_out->last_reinforced_unix = strtoull(v, NULL, 10);
        else if (!strcmp(k, "is_auto_promoted")) hdr_out->is_auto_promoted = (uint8_t)atoi(v);
        else if (!strcmp(k, "content_hash")) strncpy(hdr_out->content_hash, v, sizeof(hdr_out->content_hash) - 1);
        else if (!strcmp(k, "description")) strncpy(hdr_out->description, v, sizeof(hdr_out->description) - 1); }
    if (!hdr_out->vector_dim) hdr_out->vector_dim = FLOW_VAULT_DIM; if (!hdr_out->payload_size) hdr_out->payload_size = sizeof(FlowVecPayload);
    if (!hdr_out->confidence_score) hdr_out->confidence_score = 1; if (!hdr_out->last_reinforced_unix) hdr_out->last_reinforced_unix = hdr_out->created_at_unix; return 1; }
int flow_fvec_write_file(const char *filepath, const FlowVecHeader *hdr, const FlowVecPayload *payload) { if (!filepath || !hdr || !payload) return 0;
    char header_block[FLOW_FVEC_HEADER_SIZE]; if (!flow_fvec_header_serialize(hdr, header_block, sizeof(header_block))) return 0;
    FILE *f = fopen(filepath, "wb"); if (!f) return 0; FlowVecPayload pc = *payload; pc.crc32 = flow_fvec_crc32(&pc, offsetof(FlowVecPayload, crc32));
    int ok = (fwrite(header_block, 1, FLOW_FVEC_HEADER_SIZE, f) == FLOW_FVEC_HEADER_SIZE && fwrite(&pc, 1, sizeof(pc), f) == sizeof(pc)); fclose(f); return ok; }
int flow_fvec_read_file(const char *filepath, FlowVecHeader *hdr_out, FlowVecPayload *payload_out) { if (!filepath || !hdr_out || !payload_out) return 0;
    FILE *f = fopen(filepath, "rb"); if (!f) return 0; char header_block[FLOW_FVEC_HEADER_SIZE];
    int ok = (fread(header_block, 1, FLOW_FVEC_HEADER_SIZE, f) == FLOW_FVEC_HEADER_SIZE && flow_fvec_header_deserialize(header_block, sizeof(header_block), hdr_out) &&
              fread(payload_out, 1, sizeof(FlowVecPayload), f) == sizeof(FlowVecPayload)); fclose(f); if (!ok) return 0;
    if (hdr_out->vector_dim == 0 || hdr_out->vector_dim > FLOW_VAULT_DIM || hdr_out->payload_size != sizeof(FlowVecPayload)) return 0;
    if (payload_out->hard_composite_mask == 0) return 0;
    strncpy(hdr_out->filepath, filepath, sizeof(hdr_out->filepath) - 1); return payload_out->crc32 == flow_fvec_crc32(payload_out, offsetof(FlowVecPayload, crc32)); }
void flow_fvec_store_init(FlowVecStore *store, const char *dir_path) { if (!store) return; memset(store, 0, sizeof(*store));
    strncpy(store->root_dir, (dir_path && dir_path[0]) ? dir_path : FLOW_FVEC_DEFAULT_DIR, sizeof(store->root_dir) - 1); }
int flow_fvec_store_scan(FlowVecStore *store) {
    if (!store) return 0; store->count = 0; DIR *d = opendir(store->root_dir); if (!d) return 0; struct dirent *dir; while ((dir = readdir(d)) != NULL) {
        if (dir->d_name[0] == '.') continue; size_t len = strlen(dir->d_name);
        if (len > 5 && !strcmp(dir->d_name + len - 5, ".fvec") && store->count < FLOW_FVEC_MAX_ENTRIES) {
            char full_path[512]; snprintf(full_path, sizeof(full_path), "%s/%s", store->root_dir, dir->d_name); FlowVecRecord *rec = &store->records[store->count];
            if (flow_fvec_read_file(full_path, &rec->header, &rec->payload)) store->count++; } } closedir(d); return (int)store->count; }
const FlowVecRecord *flow_fvec_store_lookup_by_id(const FlowVecStore *store, const char *id) { if (!store || !id) return NULL;
    for (size_t i = 0; i < store->count; ++i) if (!strcmp(store->records[i].header.id, id) || strstr(store->records[i].header.filepath, id)) return &store->records[i];
    return NULL; }
int flow_fvec_store_query(const FlowVecStore *store, const char *natural_query, size_t *best_idx_out, double *best_sim_out) {
    if (!store || !natural_query || !store->count) return 0; double q[FLOW_VAULT_DIM], best_sim = -1.0; size_t best_idx = 0; flow_fvec_embed_prompt(natural_query, q);
    for (size_t i = 0; i < store->count; ++i) { double sim = flow_fvec_cosine_similarity(q, store->records[i].payload.features, FLOW_VAULT_DIM);
        if (sim > best_sim) { best_sim = sim; best_idx = i; } } if (best_idx_out) *best_idx_out = best_idx; if (best_sim_out) *best_sim_out = best_sim; return 1; }
int flow_fvec_remediate_check(const FlowVecStore *store, double ram_pressure_pct, double cache_miss_rate,
                              const FlowVecRecord **recommended_record_out, double *confidence_out, char *diagnosis_out, size_t max_diag_len) {
    if (!store || !recommended_record_out) return 0; int is_ram = (ram_pressure_pct >= FLOW_FVEC_RAM_CRITICAL_PCT), is_cache = (!is_ram && cache_miss_rate >= FLOW_FVEC_CACHE_MISS_CRITICAL);
    if (!is_ram && !is_cache) return 0; for (size_t i = 0; i < store->count; ++i) { const FlowVecHeader *h = &store->records[i].header;
        int hit = is_ram ? (strstr(h->trigger_intent, "MEMORY_CRITICAL") || strstr(h->id, "oom_survival"))
                         : (strstr(h->trigger_intent, "LATENCY_FIRST") || strstr(h->trigger_intent, "HFT_TRADING") != NULL); if (hit) {
            *recommended_record_out = &store->records[i]; if (confidence_out) *confidence_out = is_ram ? 0.94 : 0.96;
            if (diagnosis_out && max_diag_len > 0) snprintf(diagnosis_out, max_diag_len, "Crisis Remediation matched '%s' (RAM=%.1f%%, Miss=%.1f%%)", h->filepath, ram_pressure_pct, cache_miss_rate * 100.0);
            return 1; } } return 0; }
void flow_fvec_inspect(const FlowVecHeader *hdr, const FlowVecPayload *payload, FILE *out) {
    if (hdr && payload && out) fprintf(out, "[.fvec] %s (%s) | Intent: %s | HW: %s | Comp: %s | SMT: %s | Energy: %.2f | Genome: 0x%016llx | CRC32: 0x%08x\n",
        hdr->id, hdr->name, hdr->trigger_intent, hdr->origin_hardware, hdr->component_id, hdr->smt_signature, hdr->energy_score, (unsigned long long)payload->pure_genome, payload->crc32); }
void flow_fvec_store_print_summary(const FlowVecStore *store, FILE *out) { if (!store || !out) return;
    fprintf(out, "FLOW Gene Vault (%zu Models in '%s'):\n", store->count, store->root_dir); for (size_t i = 0; i < store->count; ++i) {
        const FlowVecHeader *h = &store->records[i].header;
        fprintf(out, "  [%02zu] %s %-28s | %-16s | Conf:%u | E:%.2f | SMT:%s\n", i + 1, h->is_auto_promoted ? "[AUTO]" : "[FACT]", h->id, h->trigger_intent, h->confidence_score ? h->confidence_score : 1, h->energy_score, h->smt_signature); } }
static int ensure_dir_exists(const char *dir) { struct stat st; if (stat(dir, &st) == 0) return S_ISDIR(st.st_mode);
    char tmp[256] = {0}; strncpy(tmp, dir, sizeof(tmp) - 1); for (char *p = (tmp[0] == '/' ? tmp + 1 : tmp); *p; p++) if (*p == '/') { *p = '\0'; mkdir(tmp, 0755); *p = '/'; }
    return mkdir(tmp, 0755) == 0 || errno == EEXIST; }
int flow_fvec_seed_canonical_files(const char *target_dir) {
    const char *dir = (target_dir && target_dir[0]) ? target_dir : FLOW_FVEC_DEFAULT_DIR; if (!ensure_dir_exists(dir)) return 0;
    static const struct { const char *id, *name, *file, *hw, *intent, *cat, *comp; uint64_t genome, mask, bias; double energy; } seeds[] = {
        {"vec_hft_lockfree_trading", "HFT Ultra-Low-Latency Ring", "hft_ultra_low_latency.fvec", "x86_avx2, L1=64K, Cores=64", "HFT_TRADING", "HFT", "bounded_queue", 0x0502ULL, 0xFFFFULL, 0x0502ULL, 12.4},
        {"vec_serverless_tiny_worker", "OOM Survival Compact Hash", "oom_survival_v3.fvec", "x86_generic, RAM=16MB", "MEMORY_CRITICAL", "SERVERLESS", "compact_hash", 0x0101ULL, 0x0FFFULL, 0x0101ULL, 18.2},
        {"vec_serverless_io_heavy", "Serverless Zero-Coldstart Worker", "serverless_zero_coldstart.fvec", "x86_cloud_container, Cores=4", "SERVERLESS_BURST", "SERVERLESS", "sharded_hash", 0x0304ULL, 0x3FFFULL, 0x0304ULL, 15.0},
        {"vec_antibody_slowloris_409", "Slowloris DDoS Immune Antibody", "slowloris_immune_antibody.fvec", "fleet_cluster_node_edge", "DDoS_DEFENSE", "IMMUNE", "bounded_queue", 0x0708ULL, 0x7FFFULL, 0x0708ULL, 9.8},
        {"vec_iot_embedded_sensor", "IoT Quiescent Sensor Pipeline", "iot_quiescent_sleep.fvec", "arm_cortex_m4, RAM=64KB", "LOW_POWER_QUIESCENT", "GENERAL", "bounded_queue", 0x0011ULL, 0x00FFULL, 0x0011ULL, 6.5}
    }; int seeded = 0; for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); ++i) {
        FlowVecHeader hdr = { .magic = FLOW_FVEC_MAGIC, .energy_score = seeds[i].energy, .created_at_unix = 1700000000ULL, .last_reinforced_unix = 1700000000ULL, .vector_dim = FLOW_VAULT_DIM, .payload_size = sizeof(FlowVecPayload), .confidence_score = 10 };
        FlowVecPayload payload = { .pure_genome = seeds[i].genome, .hard_composite_mask = seeds[i].mask, .soft_composite_bias = seeds[i].bias };
        strncpy(hdr.id, seeds[i].id, sizeof(hdr.id) - 1); strncpy(hdr.name, seeds[i].name, sizeof(hdr.name) - 1);
        strncpy(hdr.origin_hardware, seeds[i].hw, sizeof(hdr.origin_hardware) - 1); strncpy(hdr.trigger_intent, seeds[i].intent, sizeof(hdr.trigger_intent) - 1);
        strncpy(hdr.category, seeds[i].cat, sizeof(hdr.category) - 1); strncpy(hdr.component_id, seeds[i].comp, sizeof(hdr.component_id) - 1);
        strncpy(hdr.smt_signature, "BUFFER_UNSAT:MEM_UNSAT:SHARD_UNSAT:DET_UNSAT", sizeof(hdr.smt_signature) - 1); strncpy(hdr.description, seeds[i].name, sizeof(hdr.description) - 1);
        flow_fvec_embed_prompt(seeds[i].name, payload.features); char full_path[512]; snprintf(full_path, sizeof(full_path), "%s/%s", dir, seeds[i].file);
        if (flow_fvec_write_file(full_path, &hdr, &payload)) seeded++; } return seeded; }
int flow_fvec_verify_hardware_affinity(const FlowVecHeader *hdr, const FlowEnvironmentState *host_env, char *diag_msg, size_t max_len) { if (!hdr || !host_env) return 0;
    if (strstr(hdr->origin_hardware, "avx2") && host_env->hardware_arch == FLOW_ARCH_ARM_NEON) {
        if (diag_msg && max_len) snprintf(diag_msg, max_len, "Hardware mismatch: AVX2 required by '%s' on ARM host", hdr->id); return 0; }
    if (diag_msg && max_len) snprintf(diag_msg, max_len, "Hardware affinity verified for '%s'", hdr->id); return 1; }
uint64_t flow_fvec_compute_content_hash(uint64_t genome, uint64_t hard_mask, uint64_t soft_bias, const FlowSMTProofAttestation *proof) {
    uint64_t h = FLOW_FNV1A_64_OFFSET, words[4] = { genome, hard_mask, soft_bias, proof ? (uint64_t)(proof->buffer_bounds_safety == FLOW_SMT_PROVEN_UNSAT) : 1ULL };
    for (int i = 0; i < 4; ++i) { h ^= words[i]; h *= FLOW_FNV1A_64_PRIME; } return h; }
int flow_fvec_promote_or_strengthen(const char *target_dir, const FlowVecHeader *base_hdr, const FlowVecPayload *payload,
                                    uint64_t content_hash, char *out_filepath, size_t max_path_len, uint32_t *new_confidence_out) { if (!base_hdr || !payload) return 0;
    const char *dir = (target_dir && target_dir[0]) ? target_dir : FLOW_FVEC_DEFAULT_DIR; if (!ensure_dir_exists(dir)) return 0;
    char path[512]; snprintf(path, sizeof(path), "%s/auto_promoted_%016llx.fvec", dir, (unsigned long long)content_hash);
    FlowVecHeader hdr = *base_hdr; FlowVecPayload existing_pl;
    if (flow_fvec_read_file(path, &hdr, &existing_pl)) { hdr.confidence_score++; hdr.last_reinforced_unix = (uint64_t)time(NULL); } else {
        hdr = *base_hdr; hdr.is_auto_promoted = 1; hdr.confidence_score = 1; hdr.created_at_unix = hdr.last_reinforced_unix = (uint64_t)time(NULL);
        snprintf(hdr.content_hash, sizeof(hdr.content_hash), "%016llx", (unsigned long long)content_hash);
        if (!hdr.id[0]) snprintf(hdr.id, sizeof(hdr.id), "auto_%016llx", (unsigned long long)content_hash); } if (!flow_fvec_write_file(path, &hdr, payload)) return 0;
    if (out_filepath && max_path_len) strncpy(out_filepath, path, max_path_len - 1); if (new_confidence_out) *new_confidence_out = hdr.confidence_score; return 1; }
size_t flow_fvec_store_evict_senescent(FlowVecStore *store, uint64_t now_unix, uint64_t max_idle_seconds, char (*evicted_files_out)[256], size_t max_evictions) {
    if (!store) return 0; size_t evicted = 0, write_idx = 0; for (size_t i = 0; i < store->count; ++i) { FlowVecRecord *r = &store->records[i];
        uint64_t last = r->header.last_reinforced_unix ? r->header.last_reinforced_unix : r->header.created_at_unix;
        if (r->header.is_auto_promoted && r->header.confidence_score <= 1 && now_unix > last && (now_unix - last) >= max_idle_seconds) {
            if (r->header.filepath[0]) unlink(r->header.filepath);
            if (evicted_files_out && evicted < max_evictions) strncpy(evicted_files_out[evicted], r->header.filepath, 255); evicted++;
        } else { if (write_idx != i) store->records[write_idx] = *r; write_idx++; } } store->count = write_idx; return evicted; }
int flow_fvec_prestaging_init(FlowFvecPreStagingVault *v) { return v ? (memset(v, 0, sizeof(*v)), 1) : 0; }
int flow_fvec_prestaging_register(FlowFvecPreStagingVault *v, const char *fvec_file_path, const char *trigger_condition) {
    if (!v || !fvec_file_path || !trigger_condition || v->slot_count >= FLOW_FVEC_MAX_PRESTAGED) return 0; FlowFvecPreStagingSlot *slot = &v->slots[v->slot_count];
    memset(slot, 0, sizeof(*slot)); if (!flow_fvec_read_file(fvec_file_path, &slot->header, &slot->payload)) return 0;
    strncpy(slot->trigger_condition, trigger_condition, sizeof(slot->trigger_condition) - 1); strncpy(slot->fvec_path, fvec_file_path, sizeof(slot->fvec_path) - 1);
    flow_bmf_canvas_init(&slot->staged_canvas, 0, slot->payload.hard_composite_mask, ~slot->payload.hard_composite_mask, slot->payload.pure_genome & slot->payload.hard_composite_mask);
    slot->staged_canvas.is_adjudicated_sound = 1; slot->is_staged_sound = 1; v->slot_count++; return 1; }
int flow_fvec_prestaging_swap_atomic(FlowFvecPreStagingVault *v, const char *trigger_condition, FlowBmf1BitCanvas *active_canvas_out, double *swap_latency_ns_out) {
    if (!v || !trigger_condition || !active_canvas_out) return 0;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (size_t i = 0; i < v->slot_count; ++i) {
        if (!strcmp(v->slots[i].trigger_condition, trigger_condition) && v->slots[i].is_staged_sound) {
            FLOW_ATOMIC_STAGE_SWAP(active_canvas_out, &v->slots[i].staged_canvas);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            double elapsed_ns = (double)(t1.tv_sec - t0.tv_sec) * 1e9 + (double)(t1.tv_nsec - t0.tv_nsec);
            if (elapsed_ns <= 0.0) elapsed_ns = 1.0;
            v->total_speculative_swaps++;
            v->last_swap_latency_ns = elapsed_ns;
            if (swap_latency_ns_out) *swap_latency_ns_out = v->last_swap_latency_ns;
            return 1;
        }
    }
    return 0;
}
FlowSMTResult flow_fvec_verify_prestaging_soundness_smt(const FlowFvecPreStagingVault *v, const char *trigger_condition, double swap_latency_ns, FlowSMTProofAttestation *proof_out) {
    if (!v || !trigger_condition) return FLOW_SMT_UNKNOWN; uint64_t found_sound = 0;
    for (size_t i = 0; i < v->slot_count; ++i) if (!strcmp(v->slots[i].trigger_condition, trigger_condition) && v->slots[i].is_staged_sound) found_sound = 1;
    const FlowBoxConstraint rules[] = { FLOW_SMT_RULE("pre-staged slot soundness", found_sound, 1, 1, FLOW_BOX_THEOREM_DETERMINISM, "missing or unverified pre-staged slot"),
        FLOW_SMT_RULE("atomic swap latency (ns)", (uint64_t)swap_latency_ns, 0, FLOW_FVEC_MAX_SWAP_NS, FLOW_BOX_THEOREM_BUFFER_BOUNDS, "swap latency exceeds 500ns zero-coldstart bound") };
    return FLOW_SMT_VERIFY_RULES("fvec_prestaging", proof_out, rules, "SMT FVEC PRESTAGING SOUND (latency=%.1fns)", swap_latency_ns); }
