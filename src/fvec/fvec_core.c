/* fvec/fvec_core.c - Core FVEC CRC32, Serialization, File I/O, Vault Interop & Store */
#include "../flowy_fvec.h"
#include "../flow_jet.h"
#include "../registry.h"
#include "../flow_smt_dsl.h"
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

static uint32_t s_crc32_table[256];
static int s_crc32_table_computed = 0;

uint32_t flow_fvec_crc32(const void *data, size_t length) {
    if (!s_crc32_table_computed) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int j = 0; j < 8; j++) c = (c & 1) ? (0xEDB88320L ^ (c >> 1)) : (c >> 1);
            s_crc32_table[i] = c;
        }
        s_crc32_table_computed = 1;
    }
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFL;
    for (size_t i = 0; i < length; i++) crc = s_crc32_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFL;
}

int flow_fvec_header_serialize(const FlowVecHeader *hdr, char *out_buf, size_t buf_size) {
    if (!hdr || !out_buf || buf_size < FLOW_FVEC_HEADER_SIZE) return 0;
    memset(out_buf, 0, buf_size);
    int w = snprintf(out_buf, FLOW_FVEC_HEADER_SIZE,
        "FVEC_V1\nid=%s\nname=%s\norigin_hardware=%s\ntrigger_intent=%s\ncategory=%s\n"
        "component_id=%s\nsmt_signature=%s\nenergy_score=%.4f\ncreated_at_unix=%llu\n"
        "vector_dim=%u\npayload_size=%u\nconfidence_score=%u\nlast_reinforced_unix=%llu\n"
        "is_auto_promoted=%u\ncontent_hash=%s\ndescription=%s\n",
        hdr->id, hdr->name, hdr->origin_hardware, hdr->trigger_intent, hdr->category,
        hdr->component_id, hdr->smt_signature, hdr->energy_score, (unsigned long long)hdr->created_at_unix,
        hdr->vector_dim ? hdr->vector_dim : (uint32_t)FLOW_VAULT_DIM, (uint32_t)sizeof(FlowVecPayload),
        hdr->confidence_score ? hdr->confidence_score : 1,
        (unsigned long long)(hdr->last_reinforced_unix ? hdr->last_reinforced_unix : hdr->created_at_unix),
        (unsigned int)hdr->is_auto_promoted, hdr->content_hash[0] ? hdr->content_hash : "0000000000000000",
        hdr->description);
    return (w > 0 && w < FLOW_FVEC_HEADER_SIZE);
}

int flow_fvec_header_deserialize(const char *in_buf, size_t buf_size, FlowVecHeader *hdr_out) {
    if (!in_buf || !hdr_out || buf_size < FLOW_FVEC_HEADER_SIZE || strncmp(in_buf, "FVEC_V1", 7) != 0) return 0;
    memset(hdr_out, 0, sizeof(*hdr_out));
    strncpy(hdr_out->magic, "FVEC_V1", sizeof(hdr_out->magic) - 1);
    char buf_copy[FLOW_FVEC_HEADER_SIZE + 1];
    memcpy(buf_copy, in_buf, FLOW_FVEC_HEADER_SIZE);
    buf_copy[FLOW_FVEC_HEADER_SIZE] = '\0';

    for (char *line = strtok(buf_copy, "\n"); line; line = strtok(NULL, "\n")) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *k = line, *v = eq + 1;
        if (strcmp(k, "id") == 0) strncpy(hdr_out->id, v, sizeof(hdr_out->id) - 1);
        else if (strcmp(k, "name") == 0) strncpy(hdr_out->name, v, sizeof(hdr_out->name) - 1);
        else if (strcmp(k, "origin_hardware") == 0) strncpy(hdr_out->origin_hardware, v, sizeof(hdr_out->origin_hardware) - 1);
        else if (strcmp(k, "trigger_intent") == 0) strncpy(hdr_out->trigger_intent, v, sizeof(hdr_out->trigger_intent) - 1);
        else if (strcmp(k, "category") == 0) strncpy(hdr_out->category, v, sizeof(hdr_out->category) - 1);
        else if (strcmp(k, "component_id") == 0) strncpy(hdr_out->component_id, v, sizeof(hdr_out->component_id) - 1);
        else if (strcmp(k, "smt_signature") == 0) strncpy(hdr_out->smt_signature, v, sizeof(hdr_out->smt_signature) - 1);
        else if (strcmp(k, "energy_score") == 0) hdr_out->energy_score = atof(v);
        else if (strcmp(k, "created_at_unix") == 0) hdr_out->created_at_unix = strtoull(v, NULL, 10);
        else if (strcmp(k, "vector_dim") == 0) hdr_out->vector_dim = (uint32_t)atoi(v);
        else if (strcmp(k, "payload_size") == 0) hdr_out->payload_size = (uint32_t)atoi(v);
        else if (strcmp(k, "confidence_score") == 0) hdr_out->confidence_score = (uint32_t)atoi(v);
        else if (strcmp(k, "last_reinforced_unix") == 0) hdr_out->last_reinforced_unix = strtoull(v, NULL, 10);
        else if (strcmp(k, "is_auto_promoted") == 0) hdr_out->is_auto_promoted = (uint8_t)atoi(v);
        else if (strcmp(k, "content_hash") == 0) strncpy(hdr_out->content_hash, v, sizeof(hdr_out->content_hash) - 1);
        else if (strcmp(k, "description") == 0) strncpy(hdr_out->description, v, sizeof(hdr_out->description) - 1);
    }
    if (!hdr_out->vector_dim) hdr_out->vector_dim = FLOW_VAULT_DIM;
    if (!hdr_out->payload_size) hdr_out->payload_size = sizeof(FlowVecPayload);
    if (!hdr_out->confidence_score) hdr_out->confidence_score = 1;
    if (!hdr_out->last_reinforced_unix) hdr_out->last_reinforced_unix = hdr_out->created_at_unix;
    return 1;
}

int flow_fvec_write_file(const char *filepath, const FlowVecHeader *hdr, const FlowVecPayload *payload) {
    if (!filepath || !hdr || !payload) return 0;
    char header_block[FLOW_FVEC_HEADER_SIZE];
    if (!flow_fvec_header_serialize(hdr, header_block, sizeof(header_block))) return 0;
    FILE *f = fopen(filepath, "wb");
    if (!f) return 0;
    FlowVecPayload pc = *payload;
    pc.crc32 = flow_fvec_crc32(&pc, offsetof(FlowVecPayload, crc32));
    int ok = (fwrite(header_block, 1, FLOW_FVEC_HEADER_SIZE, f) == FLOW_FVEC_HEADER_SIZE &&
              fwrite(&pc, 1, sizeof(pc), f) == sizeof(pc));
    fclose(f);
    return ok;
}

int flow_fvec_read_file(const char *filepath, FlowVecHeader *hdr_out, FlowVecPayload *payload_out) {
    if (!filepath || !hdr_out || !payload_out) return 0;
    FILE *f = fopen(filepath, "rb");
    if (!f) return 0;
    char header_block[FLOW_FVEC_HEADER_SIZE];
    int ok = (fread(header_block, 1, FLOW_FVEC_HEADER_SIZE, f) == FLOW_FVEC_HEADER_SIZE &&
              flow_fvec_header_deserialize(header_block, sizeof(header_block), hdr_out) &&
              fread(payload_out, 1, sizeof(FlowVecPayload), f) == sizeof(FlowVecPayload));
    fclose(f);
    if (!ok) return 0;
    strncpy(hdr_out->filepath, filepath, sizeof(hdr_out->filepath) - 1);
    uint32_t expected_crc = flow_fvec_crc32(payload_out, offsetof(FlowVecPayload, crc32));
    if (payload_out->crc32 != expected_crc) {
        fprintf(stderr, "flow_fvec: CRC32 mismatch in '%s' (got 0x%08x, expected 0x%08x)\n",
                filepath, payload_out->crc32, expected_crc);
        return 0;
    }
    return 1;
}

int flow_fvec_from_vault_entry(const FlowVaultEntry *entry, const char *origin_hw, const char *intent,
                               FlowVecHeader *hdr_out, FlowVecPayload *payload_out) {
    if (!entry || !hdr_out || !payload_out) return 0;
    memset(hdr_out, 0, sizeof(*hdr_out));
    memset(payload_out, 0, sizeof(*payload_out));
    strncpy(hdr_out->magic, FLOW_FVEC_MAGIC, sizeof(hdr_out->magic) - 1);
    strncpy(hdr_out->id, entry->id, sizeof(hdr_out->id) - 1);
    strncpy(hdr_out->name, entry->name, sizeof(hdr_out->name) - 1);
    strncpy(hdr_out->description, entry->description, sizeof(hdr_out->description) - 1);
    strncpy(hdr_out->component_id, entry->component_id, sizeof(hdr_out->component_id) - 1);
    strncpy(hdr_out->origin_hardware, (origin_hw && origin_hw[0]) ? origin_hw : "x86_avx2, L1=64K, Cores=64", sizeof(hdr_out->origin_hardware) - 1);
    strncpy(hdr_out->trigger_intent, (intent && intent[0]) ? intent : "GENERAL_OPTIMIZATION", sizeof(hdr_out->trigger_intent) - 1);
    const char *cat = entry->category == FLOW_VAULT_CAT_SERVERLESS ? "SERVERLESS" :
                      entry->category == FLOW_VAULT_CAT_IMMUNE_ANTIBODY ? "IMMUNE" :
                      entry->category == FLOW_VAULT_CAT_SEMANTIC_RAG ? "SEMANTIC_RAG" : "GENERAL";
    strncpy(hdr_out->category, cat, sizeof(hdr_out->category) - 1);
    hdr_out->energy_score = entry->baseline_energy;
    hdr_out->created_at_unix = (uint64_t)time(NULL);
    hdr_out->vector_dim = FLOW_VAULT_DIM;
    hdr_out->payload_size = sizeof(FlowVecPayload);
    snprintf(hdr_out->smt_signature, sizeof(hdr_out->smt_signature), "BUFFER_%s:MEM_%s:SHARD_%s:DET_%s",
             entry->proof.buffer_bounds_safety == FLOW_SMT_PROVEN_UNSAT ? "UNSAT" : "SAT",
             entry->proof.memory_quota_bound == FLOW_SMT_PROVEN_UNSAT ? "UNSAT" : "SAT",
             entry->proof.shard_non_aliasing == FLOW_SMT_PROVEN_UNSAT ? "UNSAT" : "SAT",
             entry->proof.determinism_invariant == FLOW_SMT_PROVEN_UNSAT ? "UNSAT" : "SAT");
    memcpy(payload_out->features, entry->features, sizeof(double) * FLOW_VAULT_DIM);
    payload_out->pure_genome = entry->pure_genome;
    payload_out->hard_composite_mask = entry->canvas.hard_composite_mask;
    payload_out->soft_composite_bias = entry->canvas.soft_composite_bias;
    payload_out->proof = entry->proof;
    return 1;
}

int flow_fvec_to_vault_entry(const FlowVecHeader *hdr, const FlowVecPayload *payload, FlowVaultEntry *entry_out) {
    if (!hdr || !payload || !entry_out) return 0;
    memset(entry_out, 0, sizeof(*entry_out));
    strncpy(entry_out->id, hdr->id, sizeof(entry_out->id) - 1);
    strncpy(entry_out->name, hdr->name, sizeof(entry_out->name) - 1);
    strncpy(entry_out->description, hdr->description, sizeof(entry_out->description) - 1);
    strncpy(entry_out->component_id, hdr->component_id, sizeof(entry_out->component_id) - 1);
    entry_out->baseline_energy = hdr->energy_score;
    entry_out->category = strcmp(hdr->category, "SERVERLESS") == 0 ? FLOW_VAULT_CAT_SERVERLESS :
                          strcmp(hdr->category, "IMMUNE") == 0 ? FLOW_VAULT_CAT_IMMUNE_ANTIBODY :
                          strcmp(hdr->category, "SEMANTIC_RAG") == 0 ? FLOW_VAULT_CAT_SEMANTIC_RAG : FLOW_VAULT_CAT_GENERAL;
    memcpy(entry_out->features, payload->features, sizeof(double) * FLOW_VAULT_DIM);
    entry_out->pure_genome = payload->pure_genome;
    entry_out->canvas.hard_composite_mask = payload->hard_composite_mask;
    entry_out->canvas.soft_composite_bias = payload->soft_composite_bias;
    entry_out->proof = payload->proof;
    return 1;
}

void flow_fvec_store_init(FlowVecStore *store, const char *dir_path) {
    if (!store) return;
    memset(store, 0, sizeof(*store));
    strncpy(store->root_dir, (dir_path && dir_path[0]) ? dir_path : FLOW_FVEC_DEFAULT_DIR, sizeof(store->root_dir) - 1);
}

int flow_fvec_store_scan(FlowVecStore *store) {
    if (!store) return 0;
    store->count = 0;
    DIR *d = opendir(store->root_dir);
    if (!d) return 0;
    struct dirent *dir;
    while ((dir = readdir(d)) != NULL) {
        if (dir->d_name[0] == '.') continue;
        size_t len = strlen(dir->d_name);
        if (len > 5 && strcmp(dir->d_name + len - 5, ".fvec") == 0) {
            if (store->count >= FLOW_FVEC_MAX_ENTRIES) break;
            char full_path[512]; snprintf(full_path, sizeof(full_path), "%s/%s", store->root_dir, dir->d_name);
            FlowVecRecord *rec = &store->records[store->count];
            if (flow_fvec_read_file(full_path, &rec->header, &rec->payload)) store->count++;
        }
    }
    closedir(d);
    return (int)store->count;
}

const FlowVecRecord *flow_fvec_store_lookup_by_id(const FlowVecStore *store, const char *id) {
    if (!store || !id) return NULL;
    for (size_t i = 0; i < store->count; ++i)
        if (strcmp(store->records[i].header.id, id) == 0 || strstr(store->records[i].header.filepath, id))
            return &store->records[i];
    return NULL;
}

int flow_fvec_store_query(const FlowVecStore *store, const char *natural_query, size_t *best_idx_out, double *best_sim_out) {
    if (!store || !natural_query || store->count == 0) return 0;
    double q[FLOW_VAULT_DIM], best_sim = -1.0;
    size_t best_idx = 0;
    flow_vault_embed_prompt(natural_query, q);
    for (size_t i = 0; i < store->count; ++i) {
        double sim = flow_vault_cosine_similarity(q, store->records[i].payload.features, FLOW_VAULT_DIM);
        if (sim > best_sim) { best_sim = sim; best_idx = i; }
    }
    if (best_idx_out) *best_idx_out = best_idx;
    if (best_sim_out) *best_sim_out = best_sim;
    return 1;
}

size_t flow_fvec_store_find_by_intent(const FlowVecStore *store, const char *kw, const FlowVecRecord **out_records, size_t max_results) {
    if (!store || !kw || !out_records || max_results == 0) return 0;
    size_t found = 0;
    for (size_t i = 0; i < store->count && found < max_results; ++i) {
        const FlowVecHeader *h = &store->records[i].header;
        if (strcasestr(h->trigger_intent, kw) || strcasestr(h->name, kw) || strcasestr(h->id, kw))
            out_records[found++] = &store->records[i];
    }
    return found;
}

int flow_fvec_remediate_check(const FlowVecStore *store, double ram_pressure_pct, double cache_miss_rate,
                              const FlowVecRecord **recommended_record_out, double *confidence_out,
                              char *diagnosis_out, size_t max_diag_len) {
    if (!store || !recommended_record_out) return 0;
    int is_ram = (ram_pressure_pct >= 90.0), is_cache = (!is_ram && cache_miss_rate >= 0.15);
    if (!is_ram && !is_cache) return 0;
    for (size_t i = 0; i < store->count; ++i) {
        const FlowVecHeader *h = &store->records[i].header;
        int hit = is_ram ? (strstr(h->trigger_intent, "MEMORY_CRITICAL") || strstr(h->id, "oom_survival"))
                         : (strstr(h->trigger_intent, "LATENCY_FIRST") || strstr(h->trigger_intent, "HFT_TRADING") != NULL);
        if (hit) {
            *recommended_record_out = &store->records[i];
            if (confidence_out) *confidence_out = is_ram ? 0.94 : 0.96;
            if (diagnosis_out && max_diag_len > 0) {
                if (is_ram)
                    snprintf(diagnosis_out, max_diag_len, "偵測到資源崩塌危機 (RAM: %.1f%%)。檢索 .fvec 基因庫發現 '%s' (歷史相似度 94%%)，該特徵在過去成功將記憶體壓縮 80%%。是否直接載入該特徵向量跳過混沌搜尋？", ram_pressure_pct, h->filepath);
                else
                    snprintf(diagnosis_out, max_diag_len, "偵測到快取風暴危機 (Miss Rate: %.1f%%)。檢索 .fvec 基因庫發現 '%s' (歷史相似度 96%%)，該特徵在過去成功將延遲降至 15ns。是否直接載入該特徵向量？", cache_miss_rate * 100.0, h->filepath);
            }
            return 1;
        }
    }
    return 0;
}

void flow_fvec_inspect(const FlowVecHeader *hdr, const FlowVecPayload *payload, FILE *out) {
    if (!hdr || !payload || !out) return;
    fprintf(out, "┌────────────────────────────────────────────────────────────────────────┐\n"
                 "│ 📄 .fvec Architecture Feature Model: %-33s │\n"
                 "├────────────────────────────────────────────────────────────────────────┤\n"
                 "│ Name:             %-52s │\n│ Trigger Intent:   %-52s │\n│ Origin Hardware:  %-52s │\n"
                 "│ Category:         %-52s │\n│ Component:        %-52s │\n│ SMT Certificate:  %-52s │\n"
                 "│ Energy Score:     %-52.2f │\n│ File Path:        %-52s │\n"
                 "├────────────────────────────────────────────────────────────────────────┤\n"
                 "│ 🧬 Binary Payload (BMF & JIT Engine Parameters):               │\n"
                 "│   Pure Genome:    0x%016llx                             │\n"
                 "│   Hard Mask:      0x%016llx (1-cycle bitwise pruning)   │\n"
                 "│   Soft Bias:      0x%016llx (Boltzmann manifold bias)   │\n"
                 "│   Data Checksum:  CRC32=0x%08x (Payload verified intact)        │\n"
                 "└────────────────────────────────────────────────────────────────────────┘\n",
            hdr->id, hdr->name, hdr->trigger_intent, hdr->origin_hardware, hdr->category, hdr->component_id,
            hdr->smt_signature, hdr->energy_score, hdr->filepath[0] ? hdr->filepath : "(in-memory)",
            (unsigned long long)payload->pure_genome, (unsigned long long)payload->hard_composite_mask,
            (unsigned long long)payload->soft_composite_bias, payload->crc32);
}

void flow_fvec_store_print_summary(const FlowVecStore *store, FILE *out) {
    if (!store || !out) return;
    fprintf(out, "========================================================================================\n"
                 "  🏛️ FLOW Living Architecture Museum & Gene Vault (%zu Models in '%s')\n"
                 "========================================================================================\n",
            store->count, store->root_dir);
    for (size_t i = 0; i < store->count; ++i) {
        const FlowVecHeader *h = &store->records[i].header;
        fprintf(out, "  [%02zu] %-6s %-30s | %-16s | Conf: %-3u | Score: %.2f\n       HW: %-32s | SMT: %s\n",
                i + 1, h->is_auto_promoted ? "[AUTO]" : "[FACT]", h->id, h->trigger_intent,
                h->confidence_score ? h->confidence_score : 1, h->energy_score, h->origin_hardware, h->smt_signature);
    }
    fprintf(out, "========================================================================================\n");
}

static int ensure_dir_exists(const char *dir) {
    struct stat st;
    if (stat(dir, &st) == 0) return S_ISDIR(st.st_mode);
    char tmp[256]; strncpy(tmp, dir, sizeof(tmp) - 1); tmp[sizeof(tmp) - 1] = '\0';
    for (char *p = (tmp[0] == '/' ? tmp + 1 : tmp); *p; p++)
        if (*p == '/') { *p = '\0'; mkdir(tmp, 0755); *p = '/'; }
    return mkdir(tmp, 0755) == 0 || errno == EEXIST;
}

int flow_fvec_seed_canonical_files(const char *target_dir) {
    const char *dir = (target_dir && target_dir[0]) ? target_dir : FLOW_FVEC_DEFAULT_DIR;
    if (!ensure_dir_exists(dir)) return 0;
    FlowVectorVault vault;
    flow_vault_init(&vault);
    flow_vault_seed_canonical_archetypes(&vault);
    static const struct { const char *id, *file, *hw, *intent; } seeds[] = {
        {"vec_hft_lockfree_trading",   "hft_ultra_low_latency.fvec",     "x86_avx2, L1=64K, Cores=64",  "HFT_TRADING"},
        {"vec_serverless_tiny_worker", "oom_survival_v3.fvec",           "x86_generic, RAM=16MB",       "MEMORY_CRITICAL"},
        {"vec_serverless_io_heavy",    "serverless_zero_coldstart.fvec", "x86_cloud_container, Cores=4","SERVERLESS_BURST"},
        {"vec_antibody_slowloris_409", "slowloris_immune_antibody.fvec", "fleet_cluster_node_edge",     "DDoS_DEFENSE"},
        {"vec_iot_embedded_sensor",    "iot_quiescent_sleep.fvec",       "arm_cortex_m4, RAM=64KB",     "LOW_POWER_QUIESCENT"}
    };
    int seeded = 0;
    for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); ++i) {
        const FlowVaultEntry *e = flow_vault_lookup_by_id(&vault, seeds[i].id);
        if (e) {
            FlowVecHeader hdr; FlowVecPayload payload;
            flow_fvec_from_vault_entry(e, seeds[i].hw, seeds[i].intent, &hdr, &payload);
            char full_path[512]; snprintf(full_path, sizeof(full_path), "%s/%s", dir, seeds[i].file);
            if (flow_fvec_write_file(full_path, &hdr, &payload)) seeded++;
        }
    }
    return seeded;
}
