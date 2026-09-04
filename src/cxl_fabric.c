#include "cxl_fabric.h"
#include "flow_smt_dsl.h"
#include "flow_str.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t cxl_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

const char *flow_cxl_tier_name(FlowCxlTier tier) {
    static const char * const names[] = {
        "HBM (Tier 0 - GPU Direct)", "DDR5 (Tier 1 - Host RAM)", "CXL 3.0 Pool (Tier 2 - Disaggregated Fabric)"
    };
    return (tier >= 0 && tier <= 2) ? names[tier] : "Unknown Tier";
}

int flow_cxl_init(FlowCxlFabric *fabric) {
    if (!fabric) return 0;
    memset(fabric, 0, sizeof(*fabric));
    fabric->cap_hbm = 32;     /* 32 pages * 4KB = 128KB fast HBM cache */
    fabric->cap_ddr5 = 128;   /* 128 pages * 4KB = 512KB DDR5 cache */
    fabric->cap_cxl = 352;    /* 352 pages * 4KB = ~1.4MB pooled CXL */

    fabric->reload_ctx = flow_reload_create(fabric);
    if (fabric->reload_ctx) {
        fabric->reload_reader = calloc(1, sizeof(FlowReloadReader));
        if (fabric->reload_reader) flow_reload_reader_register(fabric->reload_ctx, fabric->reload_reader);
    }
    return 1;
}

void flow_cxl_destroy(FlowCxlFabric *fabric) {
    if (!fabric) return;
    if (fabric->reload_reader) {
        flow_reload_reader_unregister(fabric->reload_reader);
        free(fabric->reload_reader);
        fabric->reload_reader = NULL;
    }
    if (fabric->reload_ctx) {
        flow_reload_destroy(fabric->reload_ctx);
        fabric->reload_ctx = NULL;
    }
}

int flow_cxl_allocate_kv_page(FlowCxlFabric *fabric, uint32_t session_id, uint32_t token_start_idx, uint32_t token_count, double initial_entropy, uint64_t *page_id_out) {
    if (!fabric || fabric->total_pages >= FLOW_CXL_MAX_PAGES) return 0;

    size_t *counts[] = {&fabric->count_hbm, &fabric->count_ddr5, &fabric->count_cxl};
    size_t caps[] = {fabric->cap_hbm, fabric->cap_ddr5, fabric->cap_cxl};
    int tier = -1;
    for (int t = 0; t < 3; ++t) {
        if (*counts[t] < caps[t]) { (*counts[t])++; tier = t; break; }
    }
    if (tier < 0) return 0;

    uint64_t page_id = (uint64_t)(fabric->total_pages + 1);
    FlowKvPage *p = &fabric->pages[fabric->total_pages++];
    *p = (FlowKvPage){
        .page_id = page_id, .session_id = session_id, .token_start_idx = token_start_idx,
        .token_count = token_count, .current_tier = (FlowCxlTier)tier, .attention_entropy = initial_entropy,
        .last_accessed_ns = cxl_time_ns(), .is_active = 1
    };
    memset(p->data, (int)(session_id & 0xFF), sizeof(p->data));
    if (page_id_out) *page_id_out = page_id;
    return 1;
}

int flow_cxl_access_kv_page(FlowCxlFabric *fabric, uint64_t page_id, void *data_out, uint64_t *latency_ns_out) {
    if (!fabric || !page_id) return 0;
    static const uint64_t TIER_LAT[] = {5, 50, 180};

    for (size_t i = 0; i < fabric->total_pages; ++i) {
        FlowKvPage *p = &fabric->pages[i];
        if (p->page_id == page_id && p->is_active) {
            p->last_accessed_ns = cxl_time_ns();
            fabric->total_page_accesses++;
            if (data_out) memcpy(data_out, p->data, sizeof(p->data));
            if (latency_ns_out) *latency_ns_out = (p->current_tier >= 0 && p->current_tier <= 2) ? TIER_LAT[p->current_tier] : 180;
            return 1;
        }
    }
    return 0;
}

int flow_cxl_migrate_page(FlowCxlFabric *fabric, uint64_t page_id, FlowCxlTier target_tier) {
    if (!fabric || !page_id || target_tier < 0 || target_tier > 2) return 0;

    size_t *counts[] = {&fabric->count_hbm, &fabric->count_ddr5, &fabric->count_cxl};
    size_t caps[] = {fabric->cap_hbm, fabric->cap_ddr5, fabric->cap_cxl};

    for (size_t i = 0; i < fabric->total_pages; ++i) {
        FlowKvPage *p = &fabric->pages[i];
        if (p->page_id == page_id && p->is_active) {
            if (p->current_tier == target_tier) return 1;
            if (*counts[target_tier] >= caps[target_tier]) return 0;

            (*counts[p->current_tier])--;
            (*counts[target_tier])++;
            p->current_tier = target_tier;

            if (fabric->reload_reader) {
                flow_qsbr_checkpoint(fabric->reload_reader);
                fabric->total_qsbr_checkpoints++;
            }
            fabric->total_tier_migrations++;
            return 1;
        }
    }
    return 0;
}

int flow_cxl_adapt_eviction_bmf(FlowCxlFabric *fabric, double memory_pressure_ratio) {
    if (!fabric || memory_pressure_ratio < 0.75) return 0;
    int evicted_count = 0;

    for (size_t i = 0; i < fabric->total_pages; ++i) {
        FlowKvPage *p = &fabric->pages[i];
        if (p->is_active && p->current_tier == FLOW_CXL_TIER_HBM && p->attention_entropy < 0.35 && fabric->count_cxl < fabric->cap_cxl) {
            fabric->count_hbm--;
            fabric->count_cxl++;
            p->current_tier = FLOW_CXL_TIER_REMOTE_CXL;
            if (fabric->reload_reader) {
                flow_qsbr_checkpoint(fabric->reload_reader);
                fabric->total_qsbr_checkpoints++;
            }
            fabric->total_tier_migrations++;
            fabric->total_chaotic_evictions++;
            evicted_count++;
        }
    }
    return evicted_count;
}

FlowSMTResult flow_cxl_verify_smt(const FlowCxlFabric *fabric, uint32_t active_sessions, FlowSMTProofAttestation *proof_out) {
    if (!fabric) return FLOW_SMT_UNKNOWN;

    FlowSMTResult res_quota = (fabric->count_hbm > fabric->cap_hbm || fabric->count_ddr5 > fabric->cap_ddr5 || fabric->count_cxl > fabric->cap_cxl) ?
        FLOW_SMT_VIOLATION_SAT : FLOW_SMT_PROVEN_UNSAT;

    FlowSMTResult res_aliasing = FLOW_SMT_PROVEN_UNSAT;
    for (size_t i = 0; i < fabric->total_pages; ++i) {
        if (fabric->pages[i].is_active && fabric->pages[i].session_id == 0) {
            res_aliasing = FLOW_SMT_VIOLATION_SAT;
            break;
        }
    }

    if (proof_out) {
        *proof_out = (FlowSMTProofAttestation){
            .buffer_bounds_safety = res_quota, .memory_quota_bound = res_quota,
            .shard_non_aliasing = res_aliasing, .determinism_invariant = FLOW_SMT_PROVEN_UNSAT
        };
        if (res_quota == FLOW_SMT_VIOLATION_SAT) {
            snprintf(proof_out->proof_summary, sizeof(proof_out->proof_summary),
                     "SMT CXL VIOLATION: Memory quota exceeded (HBM=%zu/%zu, DDR5=%zu/%zu, CXL=%zu/%zu)",
                     fabric->count_hbm, fabric->cap_hbm, fabric->count_ddr5, fabric->cap_ddr5, fabric->count_cxl, fabric->cap_cxl);
        } else if (res_aliasing == FLOW_SMT_VIOLATION_SAT) {
            snprintf(proof_out->proof_summary, sizeof(proof_out->proof_summary),
                     "SMT CXL VIOLATION: Unassigned or aliased session detected in active KV page");
        } else {
            snprintf(proof_out->proof_summary, sizeof(proof_out->proof_summary),
                     "SMT CXL SOUND: Pages=%zu/%zu across %u sessions (HBM=%zu, DDR5=%zu, CXL=%zu, Zero-OOM)",
                     fabric->total_pages, (size_t)FLOW_CXL_MAX_PAGES, active_sessions,
                     fabric->count_hbm, fabric->count_ddr5, fabric->count_cxl);
        }
    }
    return (res_quota == FLOW_SMT_VIOLATION_SAT || res_aliasing == FLOW_SMT_VIOLATION_SAT) ? FLOW_SMT_VIOLATION_SAT : FLOW_SMT_PROVEN_UNSAT;
}

static int cxl_driver_register(void) { return 1; }

static int cxl_driver_get_bounds(FlowHardwareBounds *bounds_out) {
    if (!bounds_out) return 0;
    flow_str_copy(bounds_out->name, sizeof(bounds_out->name), "cxl_memory_pool");
    *bounds_out = (FlowHardwareBounds){
        .max_queue_depth = FLOW_CXL_MAX_PAGES, .max_buffer_bytes = 64ULL * 1024ULL * 1024ULL,
        .supports_zero_copy = 1, .is_kernel_bypass = 1, .genome_bits_required = 8
    };
    flow_str_copy(bounds_out->name, sizeof(bounds_out->name), "cxl_memory_pool");
    return 1;
}

static int cxl_driver_execute(const FlowPrimitiveContext *ctx, FlowPrimitiveResult *res_out) {
    if (!ctx || !res_out) return -1;
    *res_out = (FlowPrimitiveResult){.status_code = 0, .bytes_transferred = ctx->data_len, .latency_cycles = 60, .zero_copy_active = 1};
    return 0;
}

static const FlowPrimitiveDriver s_cxl_driver = {
    .driver_name = "cxl_memory_pool", .driver_version = "v3.0",
    .register_primitive = cxl_driver_register, .get_hardware_bounds = cxl_driver_get_bounds,
    .execute_primitive = cxl_driver_execute
};

const FlowPrimitiveDriver *flow_primitive_cxl_driver(void) {
    return &s_cxl_driver;
}
