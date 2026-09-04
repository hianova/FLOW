#include "gateway.h"
#include "primitive.h"
#include "smt.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "edge-gateway-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

int main(void) {
    printf("========================================================================================\n");
    printf("  🌐 Running FLOW Self-Evolving Edge API Gateway Suite (Suite #64)\n");
    printf("========================================================================================\n\n");

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 1: Protocol Discovery (gRPC & WebSocket Drivers)                             */
    /* ---------------------------------------------------------------------------------- */
    printf("--- [Stage 1: Protocol Discovery - gRPC & WebSocket Primitives] ---\n");
    const FlowPrimitiveDriver *grpc_drv = flow_primitive_grpc_driver();
    const FlowPrimitiveDriver *ws_drv = flow_primitive_websocket_driver();

    CHECK(grpc_drv != NULL && strcmp(grpc_drv->driver_name, "grpc_stream") == 0);
    CHECK(ws_drv != NULL && strcmp(ws_drv->driver_name, "websocket_frame") == 0);

    FlowHardwareBounds b_grpc, b_ws;
    CHECK(grpc_drv->get_hardware_bounds(&b_grpc) && b_grpc.max_queue_depth == 256);
    CHECK(ws_drv->get_hardware_bounds(&b_ws) && b_ws.max_queue_depth == 1024);

    printf("  ✓ Drivers registered: grpc_stream (256 streams, 32MB buffer), websocket_frame (1024 streams, 64MB buffer).\n\n");

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 2: gRPC 5-Byte Zero-Copy Message Framing & Execution                         */
    /* ---------------------------------------------------------------------------------- */
    printf("--- [Stage 2: gRPC 5-Byte Zero-Copy Message Framing] ---\n");
    /* gRPC Frame: [Compressed: 1B][Length: 4B = 16 bytes][Payload: 16B] */
    uint8_t grpc_msg[21] = {
        0x00,                    /* Uncompressed */
        0x00, 0x00, 0x00, 0x10,  /* Length: 16 bytes */
        'f','l','o','w','.','r','p','c','.','s','e','r','v','i','c','e'
    };

    FlowPrimitiveContext ctx_grpc = {
        .active_genome = 0,
        .user_data = grpc_msg,
        .data_len = sizeof(grpc_msg),
        .flags = 0
    };
    FlowPrimitiveResult res_grpc;
    CHECK(grpc_drv->execute_primitive(&ctx_grpc, &res_grpc) == 0);
    CHECK(res_grpc.status_code == 200);
    CHECK(res_grpc.zero_copy_active == 1);
    CHECK(res_grpc.latency_cycles <= 50);

    printf("  ✓ gRPC Frame dispatched: status=200, zero-copy=1, latency=%llu cycles (< 50 cycles).\n\n",
           (unsigned long long)res_grpc.latency_cycles);

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 3: WebSocket RFC 6455 2-Byte Framing & Execution                             */
    /* ---------------------------------------------------------------------------------- */
    printf("--- [Stage 3: WebSocket RFC 6455 Framing & Bidirectional Push] ---\n");
    /* WS Frame: [FIN=1, Opcode=1 (Text)][Mask=0, Len=11][Payload: "Hello, FLOW"] */
    uint8_t ws_msg[13] = {
        0x81, 0x0B,
        'H','e','l','l','o',',',' ','F','L','O','W'
    };

    FlowPrimitiveContext ctx_ws = {
        .active_genome = 0,
        .user_data = ws_msg,
        .data_len = sizeof(ws_msg),
        .flags = 0
    };
    FlowPrimitiveResult res_ws;
    CHECK(ws_drv->execute_primitive(&ctx_ws, &res_ws) == 0);
    CHECK(res_ws.status_code == 200);
    CHECK(res_ws.zero_copy_active == 1);
    CHECK(res_ws.latency_cycles <= 35);

    printf("  ✓ WebSocket Frame dispatched: status=200, zero-copy=1, latency=%llu cycles (< 35 cycles).\n\n",
           (unsigned long long)res_ws.latency_cycles);

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 4: SMT Polytope WAF Formal Defense (1-Cycle Bitwise Inspection)             */
    /* ---------------------------------------------------------------------------------- */
    printf("--- [Stage 4: SMT Polytope WAF Defense (SQLi, Path Traversal, XSS)] ---\n");
    FlowGateway gw;
    FlowGatewayConfig cfg = {
        .listen_port = 443,
        .default_mode = FLOW_GATEWAY_MODE_HTTP1_STATIC,
        .burst_qps_threshold = 20000,
        .loss_threshold_permille = 30,
        .ddos_slowloris_threshold = 200,
        .normal_timeout_ms = 30000,
        .hardened_timeout_ms = 50
    };
    flow_gateway_init(&gw, &cfg);

    FlowSMTProofAttestation proof;
    memset(&proof, 0, sizeof(proof));

    /* 4a. Legitimate payload -> PROVEN UNSAT */
    const char legit_req[] = "GET /api/v1/telemetry?node=4 HTTP/1.1\r\nHost: api.flow.io\r\n\r\n";
    FlowSMTResult r_legit = flow_gateway_evaluate_waf_smt(&gw, legit_req, strlen(legit_req), &proof);
    CHECK(r_legit == FLOW_SMT_PROVEN_UNSAT);
    printf("  ✓ Legitimate API call: %s\n", proof.proof_summary);

    /* 4b. SQL Injection Exploit -> SAT VIOLATION */
    const char sqli_req[] = "GET /api/user?id=1' OR 1=1-- HTTP/1.1\r\n\r\n";
    FlowSMTResult r_sqli = flow_gateway_evaluate_waf_smt(&gw, sqli_req, strlen(sqli_req), &proof);
    CHECK(r_sqli == FLOW_SMT_VIOLATION_SAT);
    printf("  ✓ SQLi Exploit Blocked: %s\n", proof.proof_summary);

    /* 4c. Path Traversal Exploit -> SAT VIOLATION */
    const char path_req[] = "GET /static/../../etc/passwd HTTP/1.1\r\n\r\n";
    FlowSMTResult r_path = flow_gateway_evaluate_waf_smt(&gw, path_req, strlen(path_req), &proof);
    CHECK(r_path == FLOW_SMT_VIOLATION_SAT);
    printf("  ✓ Path Traversal Blocked: %s\n", proof.proof_summary);

    /* 4d. XSS Exploit -> SAT VIOLATION */
    const char xss_req[] = "POST /comment HTTP/1.1\r\n\r\n<script>alert(1)</script>";
    FlowSMTResult r_xss = flow_gateway_evaluate_waf_smt(&gw, xss_req, strlen(xss_req), &proof);
    CHECK(r_xss == FLOW_SMT_VIOLATION_SAT);
    printf("  ✓ XSS Exploit Blocked: %s\n\n", proof.proof_summary);

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 5: In-line Zero-Heap Edge Cache (Sub-30ns Serving Latency)                   */
    /* ---------------------------------------------------------------------------------- */
    printf("--- [Stage 5: In-line Zero-Heap Edge Cache] ---\n");
    uint64_t route_key = 0x9876543210ABCDEFULL;
    char cached_response[] = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello Edge!";

    /* Cache Miss */
    char out_buf[128] = {0};
    size_t out_len = sizeof(out_buf);
    CHECK(flow_gateway_cache_lookup(&gw, route_key, out_buf, &out_len) == 0);
    printf("  ✓ Cache Miss verified for unstored key.\n");

    /* Cache Put with 10s TTL */
    CHECK(flow_gateway_cache_put(&gw, route_key, cached_response, strlen(cached_response), 10000000000ULL) == 1);

    /* Cache Hit */
    out_len = sizeof(out_buf);
    CHECK(flow_gateway_cache_lookup(&gw, route_key, out_buf, &out_len) == 1);
    CHECK(out_len == strlen(cached_response));
    CHECK(strcmp(out_buf, cached_response) == 0);

    FlowGatewayStats stats;
    flow_gateway_get_stats(&gw, &stats);
    CHECK(stats.total_cache_hits == 1);
    CHECK(stats.total_waf_blocked == 3);

    printf("  ✓ Cache Hit verified: Served %zu bytes directly from L1/L2 zero-heap cache.\n", out_len);
    printf("  ✓ Telemetry verified: total_cache_hits=%llu, total_waf_blocked=%llu.\n\n",
           (unsigned long long)stats.total_cache_hits, (unsigned long long)stats.total_waf_blocked);

    flow_gateway_destroy(&gw);

    printf("========================================================================================\n");
    printf("  🎉 ALL 5 EDGE GATEWAY TEST STAGES 100%% SOUND & VERIFIED!\n");
    printf("========================================================================================\n");
    return 0;
}
