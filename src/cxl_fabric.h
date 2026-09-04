#ifndef FLOW_CXL_FABRIC_H
#define FLOW_CXL_FABRIC_H

#include "primitive.h"
#include "reload.h"
#include "smt.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW LLM Distributed Inference & CXL Memory Fabric
 * ============================================================================
 * 
 * Philosophy:
 * Modern LLM inference (e.g. DeepSeek, LLaMA, MoE) is memory-capacity bound.
 * GPU HBM memory fills up quickly with KV cache, triggering Out-Of-Memory (OOM).
 * 
 * FlowCxlFabric provides:
 * 1. Disaggregated 3-tier memory model:
 *    - Tier 0: GPU HBM (~5ns, ultra-fast, scarce capacity)
 *    - Tier 1: Host DDR5 (~50ns, high bandwidth, medium capacity)
 *    - Tier 2: Remote CXL 3.0 Pool (~180ns, disaggregated pooled rack memory)
 * 2. 1-Bit Chaotic KV-Cache dynamic eviction / demotion based on attention entropy.
 * 3. QSBR lock-free pointer migration with zero inference generation stalls.
 * 4. SMT formal proofs for memory quotas and multi-session non-aliasing.
 * ============================================================================
 */

#define FLOW_CXL_MAX_PAGES 512
#define FLOW_CXL_PAGE_SIZE 4096

typedef enum {
    FLOW_CXL_TIER_HBM = 0,         /* Tier 0: Ultra-low latency GPU HBM */
    FLOW_CXL_TIER_DDR5 = 1,        /* Tier 1: Local Host DDR5 */
    FLOW_CXL_TIER_REMOTE_CXL = 2   /* Tier 2: Remote Disaggregated CXL 3.0 Pooled Fabric */
} FlowCxlTier;

typedef struct {
    uint64_t page_id;
    uint32_t session_id;
    uint32_t token_start_idx;
    uint32_t token_count;
    FlowCxlTier current_tier;
    double attention_entropy;      /* 0.0..1.0: Low = predictable/evictable, High = vital */
    uint64_t last_accessed_ns;
    uint8_t data[FLOW_CXL_PAGE_SIZE];
    int is_active;
} FlowKvPage;

typedef struct {
    FlowKvPage pages[FLOW_CXL_MAX_PAGES];
    size_t total_pages;
    
    /* Tier capacity limits */
    size_t cap_hbm;                /* e.g. 32 pages */
    size_t cap_ddr5;               /* e.g. 128 pages */
    size_t cap_cxl;                /* e.g. 352 pages */

    /* Active page counts per tier */
    size_t count_hbm;
    size_t count_ddr5;
    size_t count_cxl;

    /* Statistics */
    uint64_t total_page_accesses;
    uint64_t total_tier_migrations;
    uint64_t total_chaotic_evictions;
    uint64_t total_qsbr_checkpoints;

    FlowReloadContext *reload_ctx;
    FlowReloadReader *reload_reader;
} FlowCxlFabric;

/* Lifecycle */
int flow_cxl_init(FlowCxlFabric *fabric);
void flow_cxl_destroy(FlowCxlFabric *fabric);

/* KV Cache Allocation & Access */
int flow_cxl_allocate_kv_page(FlowCxlFabric *fabric,
                              uint32_t session_id,
                              uint32_t token_start_idx,
                              uint32_t token_count,
                              double initial_entropy,
                              uint64_t *page_id_out);

int flow_cxl_access_kv_page(FlowCxlFabric *fabric,
                            uint64_t page_id,
                            void *data_out,
                            uint64_t *latency_ns_out);

/* QSBR Zero-Downtime Page Migration Across Tiers */
int flow_cxl_migrate_page(FlowCxlFabric *fabric, uint64_t page_id, FlowCxlTier target_tier);

/* 1-Bit Chaos Dynamic KV Eviction / Demotion under Memory Pressure */
int flow_cxl_adapt_eviction_chaos(FlowCxlFabric *fabric, double memory_pressure_ratio);

/* SMT Formal Verification of Memory Quotas and Multi-Session Non-Aliasing */
FlowSMTResult flow_cxl_verify_smt(const FlowCxlFabric *fabric,
                                  uint32_t active_sessions,
                                  FlowSMTProofAttestation *proof_out);

/* Standard Hardware Primitive Driver */
const FlowPrimitiveDriver *flow_primitive_cxl_driver(void);

const char *flow_cxl_tier_name(FlowCxlTier tier);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_CXL_FABRIC_H */
