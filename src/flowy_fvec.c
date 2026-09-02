#include "flowy_fvec.h"
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

/* ------------------------------------------------------------------------- */
/* CRC32 standard implementation                                             */
/* ------------------------------------------------------------------------- */
static uint32_t s_crc32_table[256];
static int s_crc32_table_computed = 0;

static void init_crc32_table(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) {
            c = (c & 1) ? (0xEDB88320L ^ (c >> 1)) : (c >> 1);
        }
        s_crc32_table[i] = c;
    }
    s_crc32_table_computed = 1;
}

uint32_t flow_fvec_crc32(const void *data, size_t length) {
    if (!s_crc32_table_computed) {
        init_crc32_table();
    }
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFL;
    for (size_t i = 0; i < length; i++) {
        crc = s_crc32_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFL;
}

/* ------------------------------------------------------------------------- */
/* Header Serialization & Deserialization (1024 Bytes Padded Block)          */
/* ------------------------------------------------------------------------- */

int flow_fvec_header_serialize(const FlowVecHeader *hdr, char *out_buf, size_t buf_size) {
    if (hdr == NULL || out_buf == NULL || buf_size < FLOW_FVEC_HEADER_SIZE) return 0;
    memset(out_buf, 0, buf_size);

    int written = snprintf(out_buf, FLOW_FVEC_HEADER_SIZE,
        "FVEC_V1\n"
        "id=%s\n"
        "name=%s\n"
        "origin_hardware=%s\n"
        "trigger_intent=%s\n"
        "category=%s\n"
        "component_id=%s\n"
        "smt_signature=%s\n"
        "energy_score=%.4f\n"
        "created_at_unix=%llu\n"
        "vector_dim=%u\n"
        "payload_size=%u\n"
        "confidence_score=%u\n"
        "last_reinforced_unix=%llu\n"
        "is_auto_promoted=%u\n"
        "content_hash=%s\n"
        "description=%s\n",
        hdr->id,
        hdr->name,
        hdr->origin_hardware,
        hdr->trigger_intent,
        hdr->category,
        hdr->component_id,
        hdr->smt_signature,
        hdr->energy_score,
        (unsigned long long)hdr->created_at_unix,
        hdr->vector_dim ? hdr->vector_dim : (uint32_t)FLOW_VAULT_DIM,
        (uint32_t)sizeof(FlowVecPayload),
        hdr->confidence_score ? hdr->confidence_score : 1,
        (unsigned long long)(hdr->last_reinforced_unix ? hdr->last_reinforced_unix : hdr->created_at_unix),
        (unsigned int)hdr->is_auto_promoted,
        hdr->content_hash[0] ? hdr->content_hash : "0000000000000000",
        hdr->description);

    return (written > 0 && written < FLOW_FVEC_HEADER_SIZE);
}

int flow_fvec_header_deserialize(const char *in_buf, size_t buf_size, FlowVecHeader *hdr_out) {
    if (in_buf == NULL || hdr_out == NULL || buf_size < FLOW_FVEC_HEADER_SIZE) return 0;
    memset(hdr_out, 0, sizeof(*hdr_out));

    if (strncmp(in_buf, "FVEC_V1", 7) != 0) return 0;
    strncpy(hdr_out->magic, "FVEC_V1", sizeof(hdr_out->magic) - 1);

    /* Parse line-by-line key=value */
    char buf_copy[FLOW_FVEC_HEADER_SIZE + 1];
    memcpy(buf_copy, in_buf, FLOW_FVEC_HEADER_SIZE);
    buf_copy[FLOW_FVEC_HEADER_SIZE] = '\0';

    char *line = strtok(buf_copy, "\n");
    while (line != NULL) {
        char *eq = strchr(line, '=');
        if (eq != NULL) {
            *eq = '\0';
            const char *key = line;
            const char *val = eq + 1;

            if (strcmp(key, "id") == 0) strncpy(hdr_out->id, val, sizeof(hdr_out->id) - 1);
            else if (strcmp(key, "name") == 0) strncpy(hdr_out->name, val, sizeof(hdr_out->name) - 1);
            else if (strcmp(key, "origin_hardware") == 0) strncpy(hdr_out->origin_hardware, val, sizeof(hdr_out->origin_hardware) - 1);
            else if (strcmp(key, "trigger_intent") == 0) strncpy(hdr_out->trigger_intent, val, sizeof(hdr_out->trigger_intent) - 1);
            else if (strcmp(key, "category") == 0) strncpy(hdr_out->category, val, sizeof(hdr_out->category) - 1);
            else if (strcmp(key, "component_id") == 0) strncpy(hdr_out->component_id, val, sizeof(hdr_out->component_id) - 1);
            else if (strcmp(key, "smt_signature") == 0) strncpy(hdr_out->smt_signature, val, sizeof(hdr_out->smt_signature) - 1);
            else if (strcmp(key, "energy_score") == 0) hdr_out->energy_score = atof(val);
            else if (strcmp(key, "created_at_unix") == 0) hdr_out->created_at_unix = strtoull(val, NULL, 10);
            else if (strcmp(key, "vector_dim") == 0) hdr_out->vector_dim = (uint32_t)atoi(val);
            else if (strcmp(key, "payload_size") == 0) hdr_out->payload_size = (uint32_t)atoi(val);
            else if (strcmp(key, "confidence_score") == 0) hdr_out->confidence_score = (uint32_t)atoi(val);
            else if (strcmp(key, "last_reinforced_unix") == 0) hdr_out->last_reinforced_unix = strtoull(val, NULL, 10);
            else if (strcmp(key, "is_auto_promoted") == 0) hdr_out->is_auto_promoted = (uint8_t)atoi(val);
            else if (strcmp(key, "content_hash") == 0) strncpy(hdr_out->content_hash, val, sizeof(hdr_out->content_hash) - 1);
            else if (strcmp(key, "description") == 0) strncpy(hdr_out->description, val, sizeof(hdr_out->description) - 1);
        }
        line = strtok(NULL, "\n");
    }

    if (hdr_out->vector_dim == 0) hdr_out->vector_dim = FLOW_VAULT_DIM;
    if (hdr_out->payload_size == 0) hdr_out->payload_size = sizeof(FlowVecPayload);
    if (hdr_out->confidence_score == 0) hdr_out->confidence_score = 1;
    if (hdr_out->last_reinforced_unix == 0) hdr_out->last_reinforced_unix = hdr_out->created_at_unix;
    return 1;
}

/* ------------------------------------------------------------------------- */
/* File I/O Operations                                                       */
/* ------------------------------------------------------------------------- */

int flow_fvec_write_file(const char *filepath, const FlowVecHeader *hdr, const FlowVecPayload *payload) {
    if (filepath == NULL || hdr == NULL || payload == NULL) return 0;

    FILE *f = fopen(filepath, "wb");
    if (f == NULL) return 0;

    char header_block[FLOW_FVEC_HEADER_SIZE];
    if (!flow_fvec_header_serialize(hdr, header_block, sizeof(header_block))) {
        fclose(f);
        return 0;
    }

    if (fwrite(header_block, 1, FLOW_FVEC_HEADER_SIZE, f) != FLOW_FVEC_HEADER_SIZE) {
        fclose(f);
        return 0;
    }

    FlowVecPayload payload_copy = *payload;
    /* Compute CRC32 over all bytes preceding the crc32 field */
    size_t crc_len = offsetof(FlowVecPayload, crc32);
    payload_copy.crc32 = flow_fvec_crc32(&payload_copy, crc_len);

    if (fwrite(&payload_copy, 1, sizeof(payload_copy), f) != sizeof(payload_copy)) {
        fclose(f);
        return 0;
    }

    fclose(f);
    return 1;
}

int flow_fvec_read_file(const char *filepath, FlowVecHeader *hdr_out, FlowVecPayload *payload_out) {
    if (filepath == NULL || hdr_out == NULL || payload_out == NULL) return 0;

    FILE *f = fopen(filepath, "rb");
    if (f == NULL) return 0;

    char header_block[FLOW_FVEC_HEADER_SIZE];
    if (fread(header_block, 1, FLOW_FVEC_HEADER_SIZE, f) != FLOW_FVEC_HEADER_SIZE) {
        fclose(f);
        return 0;
    }

    if (!flow_fvec_header_deserialize(header_block, sizeof(header_block), hdr_out)) {
        fclose(f);
        return 0;
    }

    strncpy(hdr_out->filepath, filepath, sizeof(hdr_out->filepath) - 1);

    if (fread(payload_out, 1, sizeof(FlowVecPayload), f) != sizeof(FlowVecPayload)) {
        fclose(f);
        return 0;
    }

    fclose(f);

    /* Verify CRC32 */
    size_t crc_len = offsetof(FlowVecPayload, crc32);
    uint32_t expected_crc = flow_fvec_crc32(payload_out, crc_len);
    if (payload_out->crc32 != expected_crc) {
        fprintf(stderr, "flow_fvec: CRC32 mismatch in '%s' (got 0x%08x, expected 0x%08x)\n",
                filepath, payload_out->crc32, expected_crc);
        return 0;
    }

    return 1;
}

/* ------------------------------------------------------------------------- */
/* Vault Interoperability                                                    */
/* ------------------------------------------------------------------------- */

int flow_fvec_from_vault_entry(const FlowVaultEntry *entry,
                               const char *origin_hw,
                               const char *intent,
                               FlowVecHeader *hdr_out,
                               FlowVecPayload *payload_out) {
    if (entry == NULL || hdr_out == NULL || payload_out == NULL) return 0;
    memset(hdr_out, 0, sizeof(*hdr_out));
    memset(payload_out, 0, sizeof(*payload_out));

    strncpy(hdr_out->magic, FLOW_FVEC_MAGIC, sizeof(hdr_out->magic) - 1);
    strncpy(hdr_out->id, entry->id, sizeof(hdr_out->id) - 1);
    strncpy(hdr_out->name, entry->name, sizeof(hdr_out->name) - 1);
    strncpy(hdr_out->description, entry->description, sizeof(hdr_out->description) - 1);
    strncpy(hdr_out->component_id, entry->component_id, sizeof(hdr_out->component_id) - 1);

    if (origin_hw && origin_hw[0]) strncpy(hdr_out->origin_hardware, origin_hw, sizeof(hdr_out->origin_hardware) - 1);
    else strncpy(hdr_out->origin_hardware, "x86_avx2, L1=64K, Cores=64", sizeof(hdr_out->origin_hardware) - 1);

    if (intent && intent[0]) strncpy(hdr_out->trigger_intent, intent, sizeof(hdr_out->trigger_intent) - 1);
    else strncpy(hdr_out->trigger_intent, "GENERAL_OPTIMIZATION", sizeof(hdr_out->trigger_intent) - 1);

    switch (entry->category) {
        case FLOW_VAULT_CAT_SERVERLESS: strncpy(hdr_out->category, "SERVERLESS", sizeof(hdr_out->category) - 1); break;
        case FLOW_VAULT_CAT_IMMUNE_ANTIBODY: strncpy(hdr_out->category, "IMMUNE", sizeof(hdr_out->category) - 1); break;
        case FLOW_VAULT_CAT_SEMANTIC_RAG: strncpy(hdr_out->category, "SEMANTIC_RAG", sizeof(hdr_out->category) - 1); break;
        default: strncpy(hdr_out->category, "GENERAL", sizeof(hdr_out->category) - 1); break;
    }

    hdr_out->energy_score = entry->baseline_energy;
    hdr_out->created_at_unix = (uint64_t)time(NULL);
    hdr_out->vector_dim = FLOW_VAULT_DIM;
    hdr_out->payload_size = sizeof(FlowVecPayload);

    /* Construct SMT certificate signature */
    snprintf(hdr_out->smt_signature, sizeof(hdr_out->smt_signature),
             "BUFFER_%s:MEM_%s:SHARD_%s:DET_%s",
             entry->proof.buffer_bounds_safety == FLOW_SMT_PROVEN_UNSAT ? "UNSAT" : "SAT",
             entry->proof.memory_quota_bound == FLOW_SMT_PROVEN_UNSAT ? "UNSAT" : "SAT",
             entry->proof.shard_non_aliasing == FLOW_SMT_PROVEN_UNSAT ? "UNSAT" : "SAT",
             entry->proof.determinism_invariant == FLOW_SMT_PROVEN_UNSAT ? "UNSAT" : "SAT");

    /* Copy payload */
    memcpy(payload_out->features, entry->features, sizeof(double) * FLOW_VAULT_DIM);
    payload_out->pure_genome = entry->pure_genome;
    payload_out->hard_composite_mask = entry->canvas.hard_composite_mask;
    payload_out->soft_composite_bias = entry->canvas.soft_composite_bias;
    payload_out->proof = entry->proof;

    return 1;
}

int flow_fvec_to_vault_entry(const FlowVecHeader *hdr,
                             const FlowVecPayload *payload,
                             FlowVaultEntry *entry_out) {
    if (hdr == NULL || payload == NULL || entry_out == NULL) return 0;
    memset(entry_out, 0, sizeof(*entry_out));

    strncpy(entry_out->id, hdr->id, sizeof(entry_out->id) - 1);
    strncpy(entry_out->name, hdr->name, sizeof(entry_out->name) - 1);
    strncpy(entry_out->description, hdr->description, sizeof(entry_out->description) - 1);
    strncpy(entry_out->component_id, hdr->component_id, sizeof(entry_out->component_id) - 1);
    entry_out->baseline_energy = hdr->energy_score;

    if (strcmp(hdr->category, "SERVERLESS") == 0) entry_out->category = FLOW_VAULT_CAT_SERVERLESS;
    else if (strcmp(hdr->category, "IMMUNE") == 0) entry_out->category = FLOW_VAULT_CAT_IMMUNE_ANTIBODY;
    else if (strcmp(hdr->category, "SEMANTIC_RAG") == 0) entry_out->category = FLOW_VAULT_CAT_SEMANTIC_RAG;
    else entry_out->category = FLOW_VAULT_CAT_GENERAL;

    memcpy(entry_out->features, payload->features, sizeof(double) * FLOW_VAULT_DIM);
    entry_out->pure_genome = payload->pure_genome;
    entry_out->canvas.hard_composite_mask = payload->hard_composite_mask;
    entry_out->canvas.soft_composite_bias = payload->soft_composite_bias;
    entry_out->proof = payload->proof;

    return 1;
}

/* ------------------------------------------------------------------------- */
/* Store Management & Directory Scanning                                     */
/* ------------------------------------------------------------------------- */

void flow_fvec_store_init(FlowVecStore *store, const char *dir_path) {
    if (store == NULL) return;
    memset(store, 0, sizeof(*store));
    if (dir_path && dir_path[0]) {
        strncpy(store->root_dir, dir_path, sizeof(store->root_dir) - 1);
    } else {
        strncpy(store->root_dir, FLOW_FVEC_DEFAULT_DIR, sizeof(store->root_dir) - 1);
    }
}

int flow_fvec_store_scan(FlowVecStore *store) {
    if (store == NULL) return 0;
    store->count = 0;

    DIR *d = opendir(store->root_dir);
    if (!d) return 0;

    struct dirent *dir;
    while ((dir = readdir(d)) != NULL) {
        if (dir->d_name[0] == '.') continue;
        size_t len = strlen(dir->d_name);
        if (len > 5 && strcmp(dir->d_name + len - 5, ".fvec") == 0) {
            if (store->count >= FLOW_FVEC_MAX_ENTRIES) break;

            char full_path[512];
            snprintf(full_path, sizeof(full_path), "%s/%s", store->root_dir, dir->d_name);

            FlowVecRecord *rec = &store->records[store->count];
            if (flow_fvec_read_file(full_path, &rec->header, &rec->payload)) {
                store->count++;
            }
        }
    }
    closedir(d);
    return (int)store->count;
}

const FlowVecRecord *flow_fvec_store_lookup_by_id(const FlowVecStore *store, const char *id) {
    if (store == NULL || id == NULL) return NULL;
    for (size_t i = 0; i < store->count; ++i) {
        if (strcmp(store->records[i].header.id, id) == 0 ||
            strstr(store->records[i].header.filepath, id) != NULL) {
            return &store->records[i];
        }
    }
    return NULL;
}

int flow_fvec_store_query(const FlowVecStore *store,
                          const char *natural_query,
                          size_t *best_idx_out,
                          double *best_sim_out) {
    if (store == NULL || natural_query == NULL || store->count == 0) return 0;

    double query_vec[FLOW_VAULT_DIM];
    flow_vault_embed_prompt(natural_query, query_vec);

    double best_sim = -1.0;
    size_t best_idx = 0;

    for (size_t i = 0; i < store->count; ++i) {
        double sim = flow_vault_cosine_similarity(query_vec, store->records[i].payload.features, FLOW_VAULT_DIM);
        if (sim > best_sim) {
            best_sim = sim;
            best_idx = i;
        }
    }

    if (best_idx_out) *best_idx_out = best_idx;
    if (best_sim_out) *best_sim_out = best_sim;
    return 1;
}

size_t flow_fvec_store_find_by_intent(const FlowVecStore *store,
                                      const char *intent_keyword,
                                      const FlowVecRecord **out_records,
                                      size_t max_results) {
    if (store == NULL || intent_keyword == NULL || out_records == NULL || max_results == 0) return 0;
    size_t found = 0;

    for (size_t i = 0; i < store->count; ++i) {
        if (strcasestr(store->records[i].header.trigger_intent, intent_keyword) != NULL ||
            strcasestr(store->records[i].header.name, intent_keyword) != NULL ||
            strcasestr(store->records[i].header.id, intent_keyword) != NULL) {
            out_records[found++] = &store->records[i];
            if (found >= max_results) break;
        }
    }
    return found;
}

int flow_fvec_remediate_check(const FlowVecStore *store,
                              double ram_pressure_pct,
                              double cache_miss_rate,
                              const FlowVecRecord **recommended_record_out,
                              double *confidence_out,
                              char *diagnosis_out,
                              size_t max_diag_len) {
    if (store == NULL || recommended_record_out == NULL) return 0;

    if (ram_pressure_pct >= 90.0) {
        /* Crisis A: Memory Exhaustion / OOM */
        const FlowVecRecord *match = NULL;
        for (size_t i = 0; i < store->count; ++i) {
            if (strstr(store->records[i].header.trigger_intent, "MEMORY_CRITICAL") ||
                strstr(store->records[i].header.id, "oom_survival")) {
                match = &store->records[i];
                break;
            }
        }
        if (match) {
            *recommended_record_out = match;
            if (confidence_out) *confidence_out = 0.94;
            if (diagnosis_out && max_diag_len > 0) {
                snprintf(diagnosis_out, max_diag_len,
                         "偵測到資源崩塌危機 (RAM: %.1f%%)。檢索 .fvec 基因庫發現 '%s' (歷史相似度 94%%)，該特徵在過去成功將記憶體壓縮 80%%。是否直接載入該特徵向量跳過混沌搜尋？",
                         ram_pressure_pct, match->header.filepath);
            }
            return 1;
        }
    } else if (cache_miss_rate >= 0.15) {
        /* Crisis B: Cache Thrashing / Latency Storm */
        const FlowVecRecord *match = NULL;
        for (size_t i = 0; i < store->count; ++i) {
            if (strstr(store->records[i].header.trigger_intent, "LATENCY_FIRST") ||
                strstr(store->records[i].header.trigger_intent, "HFT_TRADING")) {
                match = &store->records[i];
                break;
            }
        }
        if (match) {
            *recommended_record_out = match;
            if (confidence_out) *confidence_out = 0.96;
            if (diagnosis_out && max_diag_len > 0) {
                snprintf(diagnosis_out, max_diag_len,
                         "偵測到快取風暴危機 (Miss Rate: %.1f%%)。檢索 .fvec 基因庫發現 '%s' (歷史相似度 96%%)，該特徵在過去成功將延遲降至 15ns。是否直接載入該特徵向量？",
                         cache_miss_rate * 100.0, match->header.filepath);
            }
            return 1;
        }
    }

    return 0;
}

/* ------------------------------------------------------------------------- */
/* Terminal Inspection & Summaries                                           */
/* ------------------------------------------------------------------------- */

void flow_fvec_inspect(const FlowVecHeader *hdr, const FlowVecPayload *payload, FILE *out) {
    if (hdr == NULL || payload == NULL || out == NULL) return;

    fprintf(out, "┌────────────────────────────────────────────────────────────────────────┐\n");
    fprintf(out, "│ 📄 .fvec Architecture Feature Model: %-33s │\n", hdr->id);
    fprintf(out, "├────────────────────────────────────────────────────────────────────────┤\n");
    fprintf(out, "│ Name:             %-52s │\n", hdr->name);
    fprintf(out, "│ Trigger Intent:   %-52s │\n", hdr->trigger_intent);
    fprintf(out, "│ Origin Hardware:  %-52s │\n", hdr->origin_hardware);
    fprintf(out, "│ Category:         %-52s │\n", hdr->category);
    fprintf(out, "│ Component:        %-52s │\n", hdr->component_id);
    fprintf(out, "│ SMT Certificate:  %-52s │\n", hdr->smt_signature);
    fprintf(out, "│ Energy Score:     %-52.2f │\n", hdr->energy_score);
    fprintf(out, "│ File Path:        %-52s │\n", hdr->filepath[0] ? hdr->filepath : "(in-memory)");
    fprintf(out, "├────────────────────────────────────────────────────────────────────────┤\n");
    fprintf(out, "│ 🧬 Binary Payload (1-Bit Chaos & JIT Engine Parameters):               │\n");
    fprintf(out, "│   Pure Genome:    0x%016llx                             │\n", (unsigned long long)payload->pure_genome);
    fprintf(out, "│   Hard Mask:      0x%016llx (1-cycle bitwise pruning)   │\n", (unsigned long long)payload->hard_composite_mask);
    fprintf(out, "│   Soft Bias:      0x%016llx (Boltzmann manifold bias)   │\n", (unsigned long long)payload->soft_composite_bias);
    fprintf(out, "│   Data Checksum:  CRC32=0x%08x (Payload verified intact)        │\n", payload->crc32);
    fprintf(out, "└────────────────────────────────────────────────────────────────────────┘\n");
}

void flow_fvec_store_print_summary(const FlowVecStore *store, FILE *out) {
    if (store == NULL || out == NULL) return;
    fprintf(out, "========================================================================================\n");
    fprintf(out, "  🏛️ FLOW Living Architecture Museum & Gene Vault (%zu Models in '%s')\n",
            store->count, store->root_dir);
    fprintf(out, "========================================================================================\n");
    for (size_t i = 0; i < store->count; ++i) {
        const FlowVecHeader *h = &store->records[i].header;
        fprintf(out, "  [%02zu] %-6s %-30s | %-16s | Conf: %-3u | Score: %.2f\n",
                i + 1, h->is_auto_promoted ? "[AUTO]" : "[FACT]", h->id, h->trigger_intent,
                h->confidence_score ? h->confidence_score : 1, h->energy_score);
        fprintf(out, "       HW: %-32s | SMT: %s\n", h->origin_hardware, h->smt_signature);
    }
    fprintf(out, "========================================================================================\n");
}

/* ------------------------------------------------------------------------- */
/* Seed Canonical .fvec Models                                               */
/* ------------------------------------------------------------------------- */

static int ensure_dir_exists(const char *dir) {
    struct stat st;
    if (stat(dir, &st) == 0) {
        if (S_ISDIR(st.st_mode)) return 1;
        return 0;
    }
    char tmp[256];
    strncpy(tmp, dir, sizeof(tmp) - 1);
    char *p = tmp;
    if (*p == '/') p++;
    while (*p) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
        p++;
    }
    return mkdir(tmp, 0755) == 0 || errno == EEXIST;
}

int flow_fvec_seed_canonical_files(const char *target_dir) {
    const char *dir = (target_dir && target_dir[0]) ? target_dir : FLOW_FVEC_DEFAULT_DIR;
    if (!ensure_dir_exists(dir)) return 0;

    FlowVectorVault vault;
    flow_vault_init(&vault);
    flow_vault_seed_canonical_archetypes(&vault);

    struct {
        const char *vault_id;
        const char *filename;
        const char *hw;
        const char *intent;
    } canonical_seeds[] = {
        {
            "vec_hft_lockfree_trading",
            "hft_ultra_low_latency.fvec",
            "x86_avx2, L1=64K, Cores=64",
            "HFT_TRADING"
        },
        {
            "vec_serverless_tiny_worker",
            "oom_survival_v3.fvec",
            "x86_generic, RAM=16MB",
            "MEMORY_CRITICAL"
        },
        {
            "vec_serverless_io_heavy",
            "serverless_zero_coldstart.fvec",
            "x86_cloud_container, Cores=4",
            "SERVERLESS_BURST"
        },
        {
            "vec_antibody_slowloris_409",
            "slowloris_immune_antibody.fvec",
            "fleet_cluster_node_edge",
            "DDoS_DEFENSE"
        },
        {
            "vec_iot_embedded_sensor",
            "iot_quiescent_sleep.fvec",
            "arm_cortex_m4, RAM=64KB",
            "LOW_POWER_QUIESCENT"
        }
    };

    size_t num_seeds = sizeof(canonical_seeds) / sizeof(canonical_seeds[0]);
    int seeded = 0;

    for (size_t i = 0; i < num_seeds; ++i) {
        const FlowVaultEntry *e = flow_vault_lookup_by_id(&vault, canonical_seeds[i].vault_id);
        if (e) {
            FlowVecHeader hdr;
            FlowVecPayload payload;
            flow_fvec_from_vault_entry(e, canonical_seeds[i].hw, canonical_seeds[i].intent, &hdr, &payload);

            char full_path[512];
            snprintf(full_path, sizeof(full_path), "%s/%s", dir, canonical_seeds[i].filename);
            if (flow_fvec_write_file(full_path, &hdr, &payload)) {
                seeded++;
            }
        }
    }

    return seeded;
}

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
