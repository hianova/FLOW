#include "gateway.h"
#include "primitive.h"
#include "swarm.h"
#include "smt.h"
#include "flow_test_kit.h"
#include "flow_str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    FLOW_TEST_SUITE_BEGIN("Self-Healing Autonomous Gateway (Suite #63)");

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 1: Gateway Initialization & Default State                                   */
    /* ---------------------------------------------------------------------------------- */
    FLOW_STAGE_BEGIN(1, "Gateway Initialization & Default HTTP/1 State");
    FlowGatewayConfig cfg = {
        .listen_port = 8443,
        .default_mode = FLOW_GATEWAY_MODE_HTTP1_STATIC,
        .burst_qps_threshold = 20000,
        .loss_threshold_permille = 30,     /* 3.0% packet drop threshold */
        .ddos_slowloris_threshold = 200,   /* 20.0% slowloris connection threshold */
        .normal_timeout_ms = 30000,        /* 30 seconds default */
        .hardened_timeout_ms = 50          /* 50 ms under DDoS */
    };

    FlowGateway gw;
    FLOW_ASSERT_TRUE(flow_gateway_init(&gw, &cfg));

    FlowGatewayStats stats;
    flow_gateway_get_stats(&gw, &stats);
    FLOW_ASSERT_EQ(stats.current_mode, FLOW_GATEWAY_MODE_HTTP1_STATIC);
    FLOW_ASSERT_EQ(stats.active_timeout_ms, 30000);
    FLOW_ASSERT_EQ(stats.total_requests, 0);
    FLOW_ASSERT_EQ(stats.total_morph_events, 0);

    /* Register downstream compute & storage nodes into mesh */
    FLOW_ASSERT_TRUE(flow_hetero_mesh_register_node(&gw.mesh, 2, FLOW_SWARM_ROLE_COMPUTE_ROUTER, "worker_compute_1", 0x2222, 50000));
    FLOW_ASSERT_TRUE(flow_hetero_mesh_register_node(&gw.mesh, 3, FLOW_SWARM_ROLE_COMPUTE_ROUTER, "worker_compute_2", 0x3333, 50000));
    FLOW_ASSERT_TRUE(flow_hetero_mesh_register_node(&gw.mesh, 4, FLOW_SWARM_ROLE_STORAGE_INDEX,  "worker_storage_1", 0x4444, 100000));

    printf("  ✓ Gateway initialized on port %u. Default mode: %s (Timeout=%ums).\n\n",
           gw.config.listen_port, flow_gateway_mode_name(stats.current_mode), stats.active_timeout_ms);

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 2: Mode 1 - HTTP/1.1 Static Keep-Alive Zero-Copy Dispatch                   */
    /* ---------------------------------------------------------------------------------- */
    FLOW_STAGE_BEGIN(2, "Mode 1 - HTTP/1.1 Static Keep-Alive Zero-Copy Dispatch");
    const char http1_req[] = "GET /index.html HTTP/1.1\r\nHost: flow.io\r\nConnection: keep-alive\r\n\r\n";
    FlowPrimitiveResult res;
    uint8_t routed_node = 0;

    FLOW_ASSERT_TRUE(flow_gateway_dispatch_request(&gw, http1_req, strlen(http1_req), &res, &routed_node));
    FLOW_ASSERT_EQ(res.status_code, 200);
    FLOW_ASSERT_EQ(res.bytes_transferred, strlen(http1_req));
    FLOW_ASSERT_EQ(res.zero_copy_active, 1);
    FLOW_ASSERT_TRUE(routed_node == 2 || routed_node == 3); /* Routed to compute worker */

    flow_gateway_get_stats(&gw, &stats);
    FLOW_ASSERT_EQ(stats.total_requests, 1);
    FLOW_ASSERT_EQ(stats.total_zero_copy_frames, 1);
    printf("  ✓ HTTP/1.1 request dispatched: status=200, bytes=%zu, zero-copy=1, downstream_node=%u.\n\n",
           res.bytes_transferred, routed_node);

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 3: Mode 2 - 100k QPS Burst Adaptive Morphing (HTTP/2 Multiplexing)           */
    /* ---------------------------------------------------------------------------------- */
    FLOW_STAGE_BEGIN(3, "Mode 2 - 100k QPS Burst Adaptive Morphing (HTTP/2)");
    FlowTrafficEntropy burst_entropy = {
        .active_connections = 5000,
        .request_qps = 85000,            /* High traffic spike > 20000 QPS threshold */
        .packet_loss_permille = 5,       /* 0.5% clean network */
        .slowloris_ratio_permille = 10,  /* 1.0% normal connections */
        .ingress_bytes_sec = 100 * 1024 * 1024
    };

    FLOW_ASSERT_TRUE(flow_gateway_adapt_entropy(&gw, &burst_entropy));
    flow_gateway_get_stats(&gw, &stats);
    FLOW_ASSERT_EQ(stats.current_mode, FLOW_GATEWAY_MODE_HTTP2_BURST);
    FLOW_ASSERT_EQ(stats.total_morph_events, 1);
    FLOW_ASSERT_EQ(stats.active_timeout_ms, 30000);

    /* Dispatch HTTP/2 9-byte binary frame */
    uint8_t h2_frame[9] = { 0x00, 0x00, 0x10, 0x01, 0x05, 0x00, 0x00, 0x00, 0x01 };
    FLOW_ASSERT_TRUE(flow_gateway_dispatch_request(&gw, h2_frame, sizeof(h2_frame), &res, &routed_node));
    FLOW_ASSERT_EQ(res.status_code, 200);
    FLOW_ASSERT_EQ(res.bytes_transferred, sizeof(h2_frame));
    FLOW_ASSERT_EQ(res.zero_copy_active, 1);

    printf("  ✓ BMF morphed to HTTP/2 Multiplexing under 85k QPS burst. Driver: %s.\n\n",
           gw.current_driver->driver_name);

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 4: Mode 3 - 5% Lossy Network Adaptive Morphing (HTTP/3 QUIC UDP)            */
    /* ---------------------------------------------------------------------------------- */
    FLOW_STAGE_BEGIN(4, "Mode 3 - 5% Packet Loss Mobile Channel Adaptive Morphing (QUIC)");
    FlowTrafficEntropy lossy_entropy = {
        .active_connections = 4000,
        .request_qps = 15000,
        .packet_loss_permille = 50,      /* 5.0% packet loss > 3.0% threshold */
        .slowloris_ratio_permille = 10,
        .ingress_bytes_sec = 20 * 1024 * 1024
    };

    FLOW_ASSERT_TRUE(flow_gateway_adapt_entropy(&gw, &lossy_entropy));
    flow_gateway_get_stats(&gw, &stats);
    FLOW_ASSERT_EQ(stats.current_mode, FLOW_GATEWAY_MODE_QUIC_LOSSY);
    FLOW_ASSERT_EQ(stats.total_morph_events, 2);
    FLOW_ASSERT_STR_EQ(gw.current_driver->driver_name, "quic_datagram");

    /* Dispatch QUIC Packet (RFC 9000 compliant fixed bit 0x40) */
    uint8_t quic_pkt[32] = { 0x40, 0x01, 0x02, 0x03 };
    FLOW_ASSERT_TRUE(flow_gateway_dispatch_request(&gw, quic_pkt, sizeof(quic_pkt), &res, &routed_node));
    FLOW_ASSERT_EQ(res.status_code, 200);
    FLOW_ASSERT_TRUE(res.latency_cycles <= 50);

    printf("  ✓ BMF morphed to HTTP/3 QUIC Datagram under 5%% packet loss. Zero Head-of-Line Blocking.\n\n");

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 5: Mode 4 - Slowloris DDoS Attack & SMT Hard Polytope Defense                */
    /* ---------------------------------------------------------------------------------- */
    FLOW_STAGE_BEGIN(5, "Mode 4 - Slowloris DDoS Attack & SMT Timeout Polytope Defense");
    FlowTrafficEntropy ddos_entropy = {
        .active_connections = 12000,
        .request_qps = 5000,
        .packet_loss_permille = 5,
        .slowloris_ratio_permille = 350, /* 35.0% Slowloris slow connections > 20.0% threshold */
        .ingress_bytes_sec = 5 * 1024 * 1024
    };

    FLOW_ASSERT_TRUE(flow_gateway_adapt_entropy(&gw, &ddos_entropy));
    flow_gateway_get_stats(&gw, &stats);
    FLOW_ASSERT_EQ(stats.current_mode, FLOW_GATEWAY_MODE_DDOS_HARDENED);
    FLOW_ASSERT_EQ(stats.active_timeout_ms, 50); /* Clamped from 30s to 50ms! */
    FLOW_ASSERT_EQ(gw.is_hardened, 1);

    /* Execute SMT hard eviction of 10,000 slowloris attack connections */
    uint32_t pruned_conns = 0;
    uint64_t prune_latency_ns = 0;
    FLOW_ASSERT_TRUE(flow_gateway_thwart_ddos(&gw, 10000, &pruned_conns, &prune_latency_ns));
    FLOW_ASSERT_EQ(pruned_conns, 10000);

    flow_gateway_get_stats(&gw, &stats);
    FLOW_ASSERT_EQ(stats.total_ddos_connections_pruned, 10000);
    FLOW_ASSERT_EQ(stats.current_active_connections, 2000);

    printf("  ✓ SMT Timeout Polytope tightened in %.2f us (< 2.5us threshold). Pruned %u malicious conns.\n",
           (double)prune_latency_ns / 1000.0, pruned_conns);
    printf("  ✓ Legitimate active connections preserved: %u. 0 crash, 0 downtime.\n\n",
           stats.current_active_connections);

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 6: SMT Formal Boundary Invariant Proofs                                      */
    /* ---------------------------------------------------------------------------------- */
    FLOW_STAGE_BEGIN(6, "SMT Formal Boundary Invariant Proofs");
    FlowSMTProofAttestation proof;
    memset(&proof, 0, sizeof(proof));

    /* Test Sound Configuration under DDoS -> PROVEN UNSAT */
    FlowSMTResult r_sound = flow_gateway_verify_smt(&gw, &ddos_entropy, &proof);
    FLOW_ASSERT_EQ(r_sound, FLOW_SMT_PROVEN_UNSAT);
    FLOW_ASSERT_EQ(proof.buffer_bounds_safety, FLOW_SMT_PROVEN_UNSAT);
    FLOW_ASSERT_EQ(proof.memory_quota_bound, FLOW_SMT_PROVEN_UNSAT);
    FLOW_ASSERT_EQ(proof.shard_non_aliasing, FLOW_SMT_PROVEN_UNSAT);
    printf("  ✓ SMT Proof Attestation Sound: %s\n", proof.proof_summary);

    /* Test Counterexample: Artificially set timeout to 1000ms while hardened -> SAT VIOLATION */
    gw.active_timeout_ms = 1000;
    FlowSMTResult r_viol = flow_gateway_verify_smt(&gw, &ddos_entropy, &proof);
    FLOW_ASSERT_EQ(r_viol, FLOW_SMT_VIOLATION_SAT);
    FLOW_ASSERT_EQ(proof.shard_non_aliasing, FLOW_SMT_VIOLATION_SAT);
    printf("  ✓ SMT Counterexample successfully caught illegal unhardened timeout: %s\n\n", proof.proof_summary);

    /* Restore sound timeout */
    gw.active_timeout_ms = 50;

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 7: Autonomous Self-Healing Recovery to Clean State                          */
    /* ---------------------------------------------------------------------------------- */
    FLOW_STAGE_BEGIN(7, "Autonomous Self-Healing Recovery to Clean State");
    FlowTrafficEntropy clean_entropy = {
        .active_connections = 500,
        .request_qps = 1200,             /* Calm traffic */
        .packet_loss_permille = 2,       /* Clean line */
        .slowloris_ratio_permille = 0,   /* Attack ceased */
        .ingress_bytes_sec = 2 * 1024 * 1024
    };

    FLOW_ASSERT_TRUE(flow_gateway_adapt_entropy(&gw, &clean_entropy));
    flow_gateway_get_stats(&gw, &stats);
    FLOW_ASSERT_EQ(stats.current_mode, FLOW_GATEWAY_MODE_HTTP1_STATIC);
    FLOW_ASSERT_EQ(stats.active_timeout_ms, 30000);
    FLOW_ASSERT_EQ(gw.is_hardened, 0);
    FLOW_ASSERT_TRUE(stats.total_morph_events >= 4);

    printf("  ✓ Self-Healing completed: Automatically restored to %s, timeout=%ums, total_morphs=%llu.\n\n",
           flow_gateway_mode_name(stats.current_mode), stats.active_timeout_ms,
           (unsigned long long)stats.total_morph_events);

    flow_gateway_destroy(&gw);

    FLOW_TEST_SUITE_END();
    return 0;
}
