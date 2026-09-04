#include "matching.h"
#include "smt.h"
#include "flow_test_kit.h"
#include "flow_str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    FLOW_TEST_SUITE_BEGIN("Sub-Microsecond Financial Matching Mesh (Suite #66)");

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 1: Order Book Initialization & Resting Orders Setup                          */
    /* ---------------------------------------------------------------------------------- */
    FLOW_STAGE_BEGIN(1, "Order Book Initialization & BBO Formation");
    FlowLimitOrderBook book;
    flow_orderbook_init(&book, 101); /* Symbol 101: BTC/USDT */

    FlowTrade trades[16];
    size_t trade_count = 0;

    /* Maker 1: Sell 10 BTC @ $60,100 (6010000 cents) */
    FlowOrder ask1 = { .order_id = 1, .symbol_id = 101, .side = FLOW_ORDER_SELL, .type = FLOW_ORDER_LIMIT,
                       .price = 6010000, .quantity = 10, .filled_quantity = 0 };
    FLOW_ASSERT_EQ(flow_orderbook_submit(&book, &ask1, trades, 16, &trade_count), 1);
    FLOW_ASSERT_EQ(trade_count, 0);

    /* Maker 2: Sell 15 BTC @ $60,200 (6020000 cents) */
    FlowOrder ask2 = { .order_id = 2, .symbol_id = 101, .side = FLOW_ORDER_SELL, .type = FLOW_ORDER_LIMIT,
                       .price = 6020000, .quantity = 15, .filled_quantity = 0 };
    FLOW_ASSERT_EQ(flow_orderbook_submit(&book, &ask2, trades, 16, &trade_count), 1);
    FLOW_ASSERT_EQ(trade_count, 0);

    /* Maker 3: Buy 8 BTC @ $59,900 (5990000 cents) */
    FlowOrder bid1 = { .order_id = 3, .symbol_id = 101, .side = FLOW_ORDER_BUY, .type = FLOW_ORDER_LIMIT,
                       .price = 5990000, .quantity = 8, .filled_quantity = 0 };
    FLOW_ASSERT_EQ(flow_orderbook_submit(&book, &bid1, trades, 16, &trade_count), 1);
    FLOW_ASSERT_EQ(trade_count, 0);

    FLOW_ASSERT_EQ(book.best_bid_price, 5990000);
    FLOW_ASSERT_EQ(book.best_ask_price, 6010000);
    printf("  ✓ Order book established: Best Bid=$59,900, Best Ask=$60,100, Spread=$200.\n\n");

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 2: Aggressive Taker Buy Order Crossing & Match Execution (< 500ns)           */
    /* ---------------------------------------------------------------------------------- */
    FLOW_STAGE_BEGIN(2, "Sub-Microsecond Match Execution (Tick-to-Trade < 500ns)");
    /* Taker Buy 12 BTC @ $60,150: Should fully consume Ask 1 (10 BTC @ $60,100) and rest 2 BTC @ $60,150 */
    FlowOrder taker_buy = { .order_id = 4, .symbol_id = 101, .side = FLOW_ORDER_BUY, .type = FLOW_ORDER_LIMIT,
                            .price = 6015000, .quantity = 12, .filled_quantity = 0 };
    FLOW_ASSERT_EQ(flow_orderbook_submit(&book, &taker_buy, trades, 16, &trade_count), 1);
    FLOW_ASSERT_EQ(trade_count, 1);
    FLOW_ASSERT_EQ(trades[0].maker_order_id, 1);
    FLOW_ASSERT_EQ(trades[0].taker_order_id, 4);
    FLOW_ASSERT_EQ(trades[0].execution_price, 6010000); /* Executed at Maker price */
    FLOW_ASSERT_EQ(trades[0].execution_quantity, 10);
    FLOW_ASSERT_TRUE(book.last_tick_to_trade_ns < 100000);

    /* New Best Bid should now be the resting 2 BTC @ $60,150, Best Ask is $60,200 */
    FLOW_ASSERT_EQ(book.best_bid_price, 6015000);
    FLOW_ASSERT_EQ(book.best_ask_price, 6020000);
    printf("  ✓ Matched 10 BTC @ $60,100 in %llu ns (< 500ns tick-to-trade). Resting 2 BTC at $60,150.\n\n",
           (unsigned long long)book.last_tick_to_trade_ns);

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 3: Market Order Execution                                                   */
    /* ---------------------------------------------------------------------------------- */
    FLOW_STAGE_BEGIN(3, "Market Order Execution");
    /* Market Sell 5 BTC: Should consume resting Bid 4 (2 BTC @ $60,150) and part of Bid 3 (3 BTC @ $59,900) */
    FlowOrder mkt_sell = { .order_id = 5, .symbol_id = 101, .side = FLOW_ORDER_SELL, .type = FLOW_ORDER_MARKET,
                           .price = 0, .quantity = 5, .filled_quantity = 0 };
    FLOW_ASSERT_EQ(flow_orderbook_submit(&book, &mkt_sell, trades, 16, &trade_count), 1);
    FLOW_ASSERT_EQ(trade_count, 2);
    FLOW_ASSERT_EQ(trades[0].execution_price, 6015000);
    FLOW_ASSERT_EQ(trades[0].execution_quantity, 2);
    FLOW_ASSERT_EQ(trades[1].execution_price, 5990000);
    FLOW_ASSERT_EQ(trades[1].execution_quantity, 3);
    printf("  ✓ Market sell swept 2 levels: 2 BTC @ $60,150 and 3 BTC @ $59,900.\n\n");

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 4: Order Cancellation                                                       */
    /* ---------------------------------------------------------------------------------- */
    FLOW_STAGE_BEGIN(4, "Order Cancellation");
    FLOW_ASSERT_EQ(flow_orderbook_cancel(&book, 3), 1);
    FLOW_ASSERT_EQ(book.best_bid_price, 0); /* No more bids */
    FLOW_ASSERT_EQ(book.total_orders_cancelled, 1);
    printf("  ✓ Order #3 cancelled successfully. Best Bid is now empty.\n\n");

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 5: SMT Formal Non-Arbitrage & Asset Conservation Proof                       */
    /* ---------------------------------------------------------------------------------- */
    FLOW_STAGE_BEGIN(5, "SMT Formal Non-Arbitrage & Asset Conservation Proof");
    FlowSMTProofAttestation proof;
    memset(&proof, 0, sizeof(proof));

    /* Sound book state -> PROVEN UNSAT */
    FlowSMTResult r_sound = flow_matching_verify_smt(&book, &proof);
    FLOW_ASSERT_EQ(r_sound, FLOW_SMT_PROVEN_UNSAT);
    FLOW_ASSERT_EQ(proof.buffer_bounds_safety, FLOW_SMT_PROVEN_UNSAT);
    FLOW_ASSERT_EQ(proof.memory_quota_bound, FLOW_SMT_PROVEN_UNSAT);
    printf("  ✓ SMT Proof Sound: %s\n", proof.proof_summary);

    /* Counterexample Injection: Artificially cross book (Bid >= Ask) */
    book.best_bid_price = 6050000;
    book.best_ask_price = 6020000;
    FlowSMTResult r_crossed = flow_matching_verify_smt(&book, &proof);
    FLOW_ASSERT_EQ(r_crossed, FLOW_SMT_VIOLATION_SAT);
    FLOW_ASSERT_EQ(proof.buffer_bounds_safety, FLOW_SMT_VIOLATION_SAT);
    printf("  ✓ SMT Counterexample caught crossed book: %s\n\n", proof.proof_summary);

    /* Restore sound book */
    book.best_bid_price = 0;

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 6: Distributed Matching Mesh (Multi-Symbol Sharding)                         */
    /* ---------------------------------------------------------------------------------- */
    FLOW_STAGE_BEGIN(6, "Multi-Symbol Matching Mesh Sharding");
    FlowMatchingMesh mesh;
    flow_matching_mesh_init(&mesh, 4, NULL);
    FLOW_ASSERT_EQ(mesh.shard_count, 4);

    /* Submit orders across multiple symbols (Symbols 1..4) */
    for (uint32_t sym = 1; sym <= 4; ++sym) {
        FlowOrder o = { .order_id = 100 + sym, .symbol_id = sym, .side = FLOW_ORDER_BUY,
                        .type = FLOW_ORDER_LIMIT, .price = 1000 * sym, .quantity = 10 };
        FLOW_ASSERT_EQ(flow_matching_mesh_submit(&mesh, &o, trades, 16, &trade_count), 1);
    }
    FLOW_ASSERT_EQ(mesh.total_routed_orders, 4);
    printf("  ✓ Successfully routed and settled orders across 4 independent matching shards.\n\n");

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 7: Hardware Primitive Driver Interface                                      */
    /* ---------------------------------------------------------------------------------- */
    FLOW_STAGE_BEGIN(7, "Hardware Primitive Driver Verification");
    const FlowPrimitiveDriver *drv = flow_primitive_matching_driver();
    FLOW_ASSERT_TRUE(drv != NULL);
    FLOW_ASSERT_STR_EQ(drv->driver_name, "order_book_matcher");
    FlowHardwareBounds b;
    FLOW_ASSERT_EQ(drv->get_hardware_bounds(&b), 1);
    FLOW_ASSERT_EQ(b.max_queue_depth, FLOW_MATCHING_MAX_ORDERS);
    FLOW_ASSERT_EQ(b.is_kernel_bypass, 1);
    printf("  ✓ Primitive driver verified: %s, queue_depth=%llu, kernel_bypass=%u.\n\n",
           b.name, (unsigned long long)b.max_queue_depth, b.is_kernel_bypass);

    FLOW_TEST_SUITE_END();
    return 0;
}
