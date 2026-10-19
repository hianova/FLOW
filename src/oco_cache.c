#include "flow_smt_dsl.h"
#include "oco_cache.h"
#include <math.h>
#include <string.h>

int flow_oco_cache_init(FlowOcoCache *cache, size_t capacity_bytes, double initial_eta) {
    if (!cache || !capacity_bytes) return 0;
    *cache = (FlowOcoCache){
        .capacity_bytes = capacity_bytes,
        .learning_rate_eta = (initial_eta > 0.0) ? initial_eta : 0.01,
        .shadow_price_lambda = 0.1
    };
    return 1;
}

int flow_oco_cache_upsert_item(FlowOcoCache *cache, uint64_t item_id, double utility, size_t size_bytes) {
    if (!cache || !item_id || !size_bytes) return 0;
    for (size_t i = 0; i < cache->item_count; ++i) {
        if (cache->items[i].item_id == item_id) {
            cache->items[i].utility = utility;
            cache->items[i].size_bytes = size_bytes;
            return 1;
        }
    }
    if (cache->item_count >= FLOW_OCO_MAX_ITEMS) return 0;
    cache->items[cache->item_count++] = (FlowOcoItem){ .item_id = item_id, .utility = utility, .size_bytes = size_bytes };
    return 1;
}

int flow_oco_cache_step_optimization(FlowOcoCache *cache) {
    if (!cache) return 0;
    cache->total_decisions++;
    size_t total_demand = 0;
    uint64_t mask = 0;

    for (size_t i = 0; i < cache->item_count && i < 64; ++i) {
        FlowOcoItem *it = &cache->items[i];
        if (it->utility >= cache->shadow_price_lambda * (double)it->size_bytes) {
            it->is_resident = true;
            total_demand += it->size_bytes;
            mask |= (1ULL << i);
        } else {
            if (it->is_resident) cache->total_evictions++;
            it->is_resident = false;
        }
    }

    while (total_demand > cache->capacity_bytes) {
        double min_density = 1e18;
        int victim = -1;
        for (size_t i = 0; i < cache->item_count; ++i) {
            if (cache->items[i].is_resident) {
                double d = cache->items[i].utility / (double)cache->items[i].size_bytes;
                if (d < min_density) { min_density = d; victim = (int)i; }
            }
        }
        if (victim < 0) break;
        cache->items[victim].is_resident = false;
        total_demand -= cache->items[victim].size_bytes;
        mask &= ~(1ULL << victim);
        cache->total_evictions++;
    }

    cache->current_usage_bytes = total_demand;
    cache->bmf_residency_mask = mask;
    cache->shadow_price_lambda = fmax(0.0, cache->shadow_price_lambda + cache->learning_rate_eta * ((double)total_demand - (double)cache->capacity_bytes));
    return 1;
}

int flow_oco_cache_is_resident(const FlowOcoCache *cache, uint64_t item_id) {
    if (!cache || !item_id) return 0;
    for (size_t i = 0; i < cache->item_count; ++i)
        if (cache->items[i].item_id == item_id) return cache->items[i].is_resident ? 1 : 0;
    return 0;
}

FlowSMTResult flow_oco_cache_verify_smt(const FlowOcoCache *cache, FlowSMTProofAttestation *proof_out) {
    if (!cache) return FLOW_SMT_UNKNOWN;
    uint64_t cap_viol = (cache->current_usage_bytes > cache->capacity_bytes) ? (cache->current_usage_bytes - cache->capacity_bytes) : 0;
    const FlowBoxConstraint rules[] = {
        FLOW_SMT_RULE("cache capacity bound", cap_viol, 0, 0, FLOW_BOX_THEOREM_MEMORY_QUOTA, "Active resident cache exceeds physical capacity"),
        FLOW_SMT_RULE("shadow price non-negative", cache->shadow_price_lambda < 0.0, 0, 0, FLOW_BOX_THEOREM_BUFFER_BOUNDS, "Lagrangian dual multiplier is negative")
    };
    return FLOW_SMT_VERIFY_RULES("oco_cache_optimization", proof_out, rules,
        "SMT OCO SOUND: Usage=%zu/%zu bytes, Lambda=%.4f, ResidentMask=0x%016llx (Zero-Defect Soundness)",
        cache->current_usage_bytes, cache->capacity_bytes, cache->shadow_price_lambda, (unsigned long long)cache->bmf_residency_mask);
}
