#include "flow_smt_dsl.h"
#include "oco_cache.h"
#include <string.h>
#include <math.h>

int flow_oco_cache_init(FlowOcoCache *cache, size_t capacity_bytes, double initial_eta) {
    if (cache == NULL || capacity_bytes == 0) return 0;
    memset(cache, 0, sizeof(*cache));
    cache->capacity_bytes = capacity_bytes;
    cache->learning_rate_eta = (initial_eta > 0.0) ? initial_eta : 0.01;
    cache->shadow_price_lambda = 0.1; /* Initial positive shadow price */
    return 1;
}

int flow_oco_cache_upsert_item(FlowOcoCache *cache, uint64_t item_id, double utility, size_t size_bytes) {
    if (cache == NULL || item_id == 0 || size_bytes == 0) return 0;

    for (size_t i = 0; i < cache->item_count; ++i) {
        if (cache->items[i].item_id == item_id) {
            cache->items[i].utility = utility;
            cache->items[i].size_bytes = size_bytes;
            return 1;
        }
    }

    if (cache->item_count >= FLOW_OCO_MAX_ITEMS) return 0;

    FlowOcoItem *item = &cache->items[cache->item_count++];
    item->item_id = item_id;
    item->utility = utility;
    item->size_bytes = size_bytes;
    item->is_resident = false;
    return 1;
}

int flow_oco_cache_step_optimization(FlowOcoCache *cache) {
    if (cache == NULL) return 0;
    cache->total_decisions++;

    /* 1. Primal Decision: x_i = 1 iff u_i >= lambda * s_i */
    size_t total_demand_bytes = 0;
    uint64_t mask = 0;

    for (size_t i = 0; i < cache->item_count && i < 64; ++i) {
        FlowOcoItem *item = &cache->items[i];
        double cutoff = cache->shadow_price_lambda * (double)item->size_bytes;
        if (item->utility >= cutoff) {
            item->is_resident = true;
            total_demand_bytes += item->size_bytes;
            mask |= (1ULL << i);
        } else {
            if (item->is_resident) {
                cache->total_evictions++;
            }
            item->is_resident = false;
        }
    }

    /* If total demand exceeds capacity, prune items in reverse order of utility-density */
    while (total_demand_bytes > cache->capacity_bytes) {
        double min_density = 1e18;
        int victim_idx = -1;
        for (size_t i = 0; i < cache->item_count; ++i) {
            FlowOcoItem *item = &cache->items[i];
            if (item->is_resident) {
                double density = item->utility / (double)item->size_bytes;
                if (density < min_density) {
                    min_density = density;
                    victim_idx = (int)i;
                }
            }
        }
        if (victim_idx >= 0) {
            cache->items[victim_idx].is_resident = false;
            total_demand_bytes -= cache->items[victim_idx].size_bytes;
            mask &= ~(1ULL << victim_idx);
            cache->total_evictions++;
        } else {
            break;
        }
    }

    cache->current_usage_bytes = total_demand_bytes;
    cache->bmf_residency_mask = mask;

    /* 2. Dual Subgradient Descent: lambda_{t+1} = max(0, lambda_t + eta * (Demand - Capacity)) */
    double subgradient = (double)total_demand_bytes - (double)cache->capacity_bytes;
    cache->shadow_price_lambda += cache->learning_rate_eta * subgradient;
    if (cache->shadow_price_lambda < 0.0) {
        cache->shadow_price_lambda = 0.0;
    }

    return 1;
}

int flow_oco_cache_is_resident(const FlowOcoCache *cache, uint64_t item_id) {
    if (cache == NULL || item_id == 0) return 0;
    for (size_t i = 0; i < cache->item_count; ++i) {
        if (cache->items[i].item_id == item_id) {
            return cache->items[i].is_resident ? 1 : 0;
        }
    }
    return 0;
}

FlowSMTResult flow_oco_cache_verify_smt(const FlowOcoCache *cache, FlowSMTProofAttestation *proof_out) {
    if (cache == NULL) return FLOW_SMT_UNKNOWN;

    FLOW_SMT_BOX_BUILDER_DECL(builder);

    /* Theorem 1: Strict Capacity Polytope Bound */
    uint64_t capacity_violation = 0;
    if (cache->current_usage_bytes > cache->capacity_bytes) {
        capacity_violation = (uint64_t)(cache->current_usage_bytes - cache->capacity_bytes);
    }
    FLOW_SMT_BOX_ADD_RULE(builder, "cache capacity bound", capacity_violation, 0, 0,
                          FLOW_BOX_THEOREM_MEMORY_QUOTA, "Active resident cache exceeds physical capacity");

    /* Theorem 2: Dual Shadow Price Non-Negativity */
    uint64_t shadow_price_negativity = (cache->shadow_price_lambda < 0.0) ? 1 : 0;
    FLOW_SMT_BOX_ADD_RULE(builder, "shadow price non-negative", shadow_price_negativity, 0, 0,
                          FLOW_BOX_THEOREM_BUFFER_BOUNDS, "Lagrangian dual multiplier is negative");

    FlowSMTResult res = FLOW_SMT_BOX_VERIFY(builder, "oco_cache_optimization", proof_out);
    if (res == FLOW_SMT_PROVEN_UNSAT && proof_out != NULL) {
        snprintf(proof_out->proof_summary, sizeof(proof_out->proof_summary),
                 "SMT OCO SOUND: Usage=%zu/%zu bytes, Lambda=%.4f, ResidentMask=0x%016llx (Zero-Defect Soundness)",
                 cache->current_usage_bytes, cache->capacity_bytes,
                 cache->shadow_price_lambda, (unsigned long long)cache->bmf_residency_mask);
    }
    return res;
}
