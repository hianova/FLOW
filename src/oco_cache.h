#ifndef FLOW_OCO_CACHE_H
#define FLOW_OCO_CACHE_H

#include "smt.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW OCO Cache: Online Convex Optimization & Lagrangian Duality
 * ============================================================================
 * Replaces empirical cache eviction heuristics (LRU, LFU, ARC, 2Q) with
 * Online Convex Optimization (OCO) and subgradient descent on Lagrangian
 * shadow prices lambda.
 *
 * Each item i has utility u_i and size s_i.
 * Capacity constraint: sum_{i} s_i * x_i <= Capacity.
 * Shadow price update: lambda_{t+1} = max(0, lambda_t + eta * (TotalDemand - Capacity)).
 * Residency decision: x_i = 1 iff u_i >= lambda * s_i.
 * ============================================================================
 */

#define FLOW_OCO_MAX_ITEMS 64

typedef struct {
    uint64_t item_id;
    double utility;             /* Marginal access frequency / entropy value */
    size_t size_bytes;          /* Size in bytes / pages */
    bool is_resident;           /* 1 if kept in fast tier, 0 if evicted */
} FlowOcoItem;

typedef struct {
    size_t capacity_bytes;
    size_t current_usage_bytes;
    double shadow_price_lambda; /* Dual multiplier lambda >= 0 */
    double learning_rate_eta;   /* Subgradient step size eta */
    uint64_t total_decisions;
    uint64_t total_evictions;
    uint64_t bmf_residency_mask;/* 64-bit BitManifold orthogonal projection */
    FlowOcoItem items[FLOW_OCO_MAX_ITEMS];
    size_t item_count;
} FlowOcoCache;

/* Initialize OCO cache governor */
int flow_oco_cache_init(FlowOcoCache *cache, size_t capacity_bytes, double initial_eta);

/* Register or update item utility and size */
int flow_oco_cache_upsert_item(FlowOcoCache *cache, uint64_t item_id, double utility, size_t size_bytes);

/* Execute one online subgradient descent step to update shadow price and residency */
int flow_oco_cache_step_optimization(FlowOcoCache *cache);

/* Query whether an item is resident in fast tier under current hyperplane projection */
int flow_oco_cache_is_resident(const FlowOcoCache *cache, uint64_t item_id);

/* SMT Formal Capacity & Regret Bound Proof */
FlowSMTResult flow_oco_cache_verify_smt(const FlowOcoCache *cache, FlowSMTProofAttestation *proof_out);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_OCO_CACHE_H */
