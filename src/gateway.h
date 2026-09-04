#ifndef FLOW_GATEWAY_H
#define FLOW_GATEWAY_H

#include "primitive.h"
#include "swarm.h"
#include "reload.h"
#include "smt.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW Self-Healing Autonomous Gateway (4-Mode Morphing Quartet)
 * ============================================================================
 * 
 * Philosophy:
 * Traditional gateways (Nginx, Envoy, HAProxy) rely on static configuration files.
 * When traffic spikes 100x, networks experience packet loss, or Slowloris attacks
 * strike, static gateways suffer head-of-line blocking, connection exhaustion,
 * and service crashes.
 * 
 * FlowGateway is an autopoietic topological organism:
 * - Mode 1 (Static Low-Load): HTTP/1.1 Keep-Alive + zero-alloc static memory.
 * - Mode 2 (100k QPS Burst):  1-Bit Chaos flip -> HTTP/2 binary multiplexing.
 * - Mode 3 (5% Lossy Network): 1-Bit Chaos flip -> HTTP/3 QUIC UDP datagrams.
 * - Mode 4 (Slowloris DDoS):   SMT hard timeout tightening (30s -> 50ms) in 2.5us.
 * ============================================================================
 */

typedef enum {
    FLOW_GATEWAY_MODE_HTTP1_STATIC = 0,    /* Mode 1: Low concurrency HTTP/1.1 Keep-Alive */
    FLOW_GATEWAY_MODE_HTTP2_BURST = 1,     /* Mode 2: 100k QPS burst -> HTTP/2 binary multiplexing */
    FLOW_GATEWAY_MODE_QUIC_LOSSY = 2,      /* Mode 3: Lossy/mobile network -> HTTP/3 QUIC UDP datagram */
    FLOW_GATEWAY_MODE_DDOS_HARDENED = 3    /* Mode 4: Slowloris DDoS attack -> SMT timeout polytope tightening */
} FlowGatewayMode;

typedef struct {
    uint32_t active_connections;          /* Current concurrent TCP/UDP sessions */
    uint32_t request_qps;                 /* Requests per second */
    uint16_t packet_loss_permille;        /* 0..1000 (e.g. 50 = 5.0% packet drop rate) */
    uint16_t slowloris_ratio_permille;    /* 0..1000 (e.g. 350 = 35% slow/hanging connections) */
    uint64_t ingress_bytes_sec;           /* Ingress bandwidth in bytes/sec */
} FlowTrafficEntropy;

typedef struct {
    uint16_t listen_port;
    FlowGatewayMode default_mode;
    uint32_t burst_qps_threshold;         /* QPS threshold to trigger HTTP/2 multiplexing (e.g. 20000) */
    uint16_t loss_threshold_permille;     /* Packet loss to trigger QUIC (e.g. 30 = 3%) */
    uint16_t ddos_slowloris_threshold;    /* Ratio of slowloris conns to trigger SMT tightening (e.g. 200 = 20%) */
    uint32_t normal_timeout_ms;           /* Default idle timeout (e.g. 30000 ms) */
    uint32_t hardened_timeout_ms;         /* SMT-tightened DDoS timeout (e.g. 50 ms) */
} FlowGatewayConfig;

typedef struct {
    uint64_t total_requests;
    uint64_t total_bytes_transferred;
    uint64_t total_morph_events;
    uint64_t total_ddos_connections_pruned;
    uint64_t total_zero_copy_frames;
    uint32_t current_active_connections;
    uint64_t p99_latency_ns;
    FlowGatewayMode current_mode;
    uint64_t active_genome;
    uint32_t active_timeout_ms;
} FlowGatewayStats;

typedef struct FlowGateway {
    FlowGatewayConfig config;
    FlowPrimitiveRegistry registry;
    const FlowPrimitiveDriver *current_driver;
    FlowHeteroMesh mesh;
    FlowReloadContext *reload_ctx;
    FlowReloadReader *reload_reader;
    
    _Atomic FlowGatewayMode active_mode;
    _Atomic uint32_t active_timeout_ms;
    _Atomic uint64_t active_genome;
    _Atomic int is_hardened;
    
    /* Statistics counters */
    _Atomic uint64_t total_requests;
    _Atomic uint64_t total_bytes_transferred;
    _Atomic uint64_t total_morph_events;
    _Atomic uint64_t total_ddos_connections_pruned;
    _Atomic uint64_t total_zero_copy_frames;
    _Atomic uint32_t current_connections;
    _Atomic uint64_t p99_latency_ns;
    
    uint64_t last_morph_timestamp_ns;
} FlowGateway;

/* Gateway Lifecycle */
int flow_gateway_init(FlowGateway *gw, const FlowGatewayConfig *config);
void flow_gateway_destroy(FlowGateway *gw);

/* In-line Adaptive Morphing driven by traffic entropy (Mode 1 <-> Mode 2 <-> Mode 3 <-> Mode 4) */
int flow_gateway_adapt_entropy(FlowGateway *gw, const FlowTrafficEntropy *entropy);

/* High-speed wire dispatch: zero-copy framing, driver execution, and fluid backpressure routing */
int flow_gateway_dispatch_request(FlowGateway *gw,
                                  const void *req_data,
                                  size_t req_len,
                                  FlowPrimitiveResult *res_out,
                                  uint8_t *routed_node_id_out);

/* SMT Hard Polytope Slowloris DDoS Defense: Prunes dangling connections within microseconds */
int flow_gateway_thwart_ddos(FlowGateway *gw,
                             uint32_t slowloris_conns,
                             uint32_t *pruned_conns_out,
                             uint64_t *prune_latency_ns_out);

/* SMT Formal Boundary Invariant Proof */
FlowSMTResult flow_gateway_verify_smt(const FlowGateway *gw,
                                      const FlowTrafficEntropy *entropy,
                                      FlowSMTProofAttestation *proof_out);

/* Snapshot Stats */
void flow_gateway_get_stats(const FlowGateway *gw, FlowGatewayStats *stats_out);

/* Human-readable Mode Name */
const char *flow_gateway_mode_name(FlowGatewayMode mode);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_GATEWAY_H */
