#ifndef FLOW_JET_LOB_H
#define FLOW_JET_LOB_H

#include "matching.h"
#include "flow_jet.h"
#include "smt.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW Jet-Based Phase-Space Liquidity Hydrodynamics (flow_jet_lob.h)
 * ============================================================================
 * Paradigm:
 * Continuous phase-space modeling of Limit Order Book (LOB) microstructures.
 *
 * Traditional order books track discrete price-time queues. They are vulnerable to:
 * - Microsecond liquidity vacuums / flash crashes.
 * - Latency sniping / toxic flow (adverse selection) where fast traders pick off
 *   stale resting orders before makers can cancel.
 *
 * Solution:
 * Uses Jet Bundles J^2(M) to track:
 * - Position q: Normalized mid-price and order flow imbalance (OFI).
 * - Momentum p = \dot{q}: Mid-price velocity and queue depletion rate.
 * - Acceleration a = \ddot{q}: Microstructure order shock and cancellation surge.
 *
 * Key Capabilities:
 * 1. Liquidity Collapse Pre-play: Kinematic extrapolation predicts exact microsecond
 *    time-to-exhaustion of top-of-book depth: (1/2) a t^2 + v t = Depth.
 * 2. Adaptive Dynamic Spread: Dynamically widens quote spreads during high acceleration
 *    spikes to neutralize latency arbitrage and toxic flow.
 * 3. Zero-Allocation C17: Executed in sub-microsecond matching cycles.
 * ============================================================================
 */

typedef struct {
    double time_to_collapse_us;
    bool is_collapse_imminent;
    double projected_depletion_rate;
    uint64_t predicted_breach_price;
} FlowLOBCollapseAlert;

typedef struct {
    uint32_t symbol_id;
    FlowJet jet;                            /* Continuous 16-D phase space representation */
    double mid_price;                       /* Continuous mid-price P_mid */
    double prev_mid_price;
    double price_velocity;                  /* \dot{P} (cents/microsecond) */
    double price_acceleration;              /* \ddot{P} (cents/microsecond^2) */
    double order_flow_imbalance;            /* OFI: \Delta Bid_vol - \Delta Ask_vol */
    double ofi_velocity;                    /* \dot{OFI} */
    uint64_t last_update_time_ns;
    uint64_t total_updates;
    uint64_t collapse_alerts_triggered;
    uint64_t adaptive_spread_activations;
    double depth_collapse_threshold;        /* Depth threshold below which collapse warning fires */
    double sniper_acceleration_threshold;   /* Acceleration threshold triggering spread widening */
} FlowLOBHydrodynamics;

/* Initialize LOB hydrodynamics tracker for a given symbol */
int flow_lob_hydrodynamics_init(FlowLOBHydrodynamics *hydro,
                                uint32_t symbol_id,
                                double depth_collapse_threshold,
                                double sniper_accel_threshold);

/* Update continuous phase space coordinates from discrete limit order book state */
int flow_lob_hydrodynamics_update_from_book(FlowLOBHydrodynamics *hydro,
                                           const FlowLimitOrderBook *book,
                                           uint64_t current_time_ns);

/* Predict liquidity collapse time-to-breach for the bid/ask book */
int flow_lob_hydrodynamics_predict_collapse(const FlowLOBHydrodynamics *hydro,
                                            const FlowLimitOrderBook *book,
                                            FlowOrderSide side,
                                            FlowLOBCollapseAlert *alert_out);

/* Compute adaptive protective spread widening offset against toxic latency sniping */
int flow_lob_hydrodynamics_compute_adaptive_spread(const FlowLOBHydrodynamics *hydro,
                                                   uint64_t base_spread,
                                                   uint64_t *widened_spread_out);

/* Formal SMT Supreme Court verification of LOB hydrodynamics & non-arbitrage */
FlowSMTResult flow_lob_hydrodynamics_verify_smt(const FlowLOBHydrodynamics *hydro,
                                                FlowSMTProofAttestation *proof_out);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_JET_LOB_H */
