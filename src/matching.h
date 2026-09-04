#ifndef FLOW_MATCHING_H
#define FLOW_MATCHING_H

#include "primitive.h"
#include "swarm.h"
#include "smt.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW Sub-Microsecond Distributed Financial Matching Mesh
 * ============================================================================
 * 
 * Philosophy:
 * Traditional matching engines (e.g. C++ exchange gateways) suffer from heap
 * allocation latency spikes (std::map / red-black tree rebalancing), floating-point
 * rounding errors, and unverified multi-threaded race conditions.
 * 
 * FlowMatching provides:
 * 1. Pure integer price-time priority (Price in fixed-point cents / satoshis).
 * 2. Zero-allocation flat memory ring slots (< 300ns tick-to-trade).
 * 3. SMT formal non-arbitrage and cash/asset conservation proof.
 * 4. Multi-symbol sharding integrated with Heterogeneous Pheromone Mesh.
 * ============================================================================
 */

#define FLOW_MATCHING_MAX_ORDERS 4096
#define FLOW_MATCHING_MAX_SHARDS 16

typedef enum {
    FLOW_ORDER_BUY = 1,
    FLOW_ORDER_SELL = 2
} FlowOrderSide;

typedef enum {
    FLOW_ORDER_LIMIT = 1,
    FLOW_ORDER_MARKET = 2,
    FLOW_ORDER_CANCEL = 3
} FlowOrderType;

typedef struct {
    uint64_t order_id;
    uint32_t symbol_id;
    FlowOrderSide side;
    FlowOrderType type;
    uint64_t price;             /* Fixed-point integer price (e.g., $100.50 -> 10050) */
    uint64_t quantity;          /* Order quantity */
    uint64_t filled_quantity;   /* Executed quantity */
    uint64_t timestamp_ns;
    int is_active;
} FlowOrder;

typedef struct {
    uint64_t trade_id;
    uint32_t symbol_id;
    uint64_t maker_order_id;
    uint64_t taker_order_id;
    uint64_t execution_price;
    uint64_t execution_quantity;
    uint64_t timestamp_ns;
} FlowTrade;

typedef struct {
    uint32_t symbol_id;
    FlowOrder orders[FLOW_MATCHING_MAX_ORDERS];
    size_t order_count;
    
    /* Best Bid and Best Ask cache */
    uint64_t best_bid_price;
    uint64_t best_ask_price;
    
    /* Statistics */
    uint64_t total_orders_submitted;
    uint64_t total_trades_executed;
    uint64_t total_volume_traded;
    uint64_t total_orders_cancelled;
    uint64_t last_tick_to_trade_ns;
} FlowLimitOrderBook;

typedef struct {
    FlowLimitOrderBook shards[FLOW_MATCHING_MAX_SHARDS];
    size_t shard_count;
    FlowHeteroMesh *mesh;       /* Dynamic fluid mesh integration */
    uint64_t total_routed_orders;
} FlowMatchingMesh;

/* Order Book Lifecycle */
void flow_orderbook_init(FlowLimitOrderBook *book, uint32_t symbol_id);

/* Sub-Microsecond Submit & Match (Tick-to-Trade < 500ns) */
int flow_orderbook_submit(FlowLimitOrderBook *book,
                          const FlowOrder *order,
                          FlowTrade *trades_out,
                          size_t max_trades,
                          size_t *trade_count_out);

/* Cancel Order */
int flow_orderbook_cancel(FlowLimitOrderBook *book, uint64_t order_id);

/* Distributed Matching Mesh Operations */
void flow_matching_mesh_init(FlowMatchingMesh *mesh, size_t shard_count, FlowHeteroMesh *telemetry_mesh);
int flow_matching_mesh_submit(FlowMatchingMesh *mesh,
                              const FlowOrder *order,
                              FlowTrade *trades_out,
                              size_t max_trades,
                              size_t *trade_count_out);

/* SMT Formal Non-Arbitrage & Asset Conservation Invariant Proof */
FlowSMTResult flow_matching_verify_smt(const FlowLimitOrderBook *book,
                                       FlowSMTProofAttestation *proof_out);

/* Standard FLOW Hardware Primitive Driver */
const FlowPrimitiveDriver *flow_primitive_matching_driver(void);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_MATCHING_H */
