#include "primitive.h"
#include "smt.h"
#include "flow_mock_driver.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "primitive-driver-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

/* Custom 3-Function Developer Driver: eBPF XDP Packet Filter Driver (25 lines of C!) */
static int custom_xdp_register(void) {
    return 1;
}

static int custom_xdp_get_bounds(FlowHardwareBounds *bounds_out) {
    if (!bounds_out) return 0;
    strncpy(bounds_out->name, "ebpf_xdp_filter", sizeof(bounds_out->name) - 1);
    bounds_out->max_queue_depth = 8192;
    bounds_out->max_buffer_bytes = 16ULL * 1024ULL * 1024ULL; /* 16 MB packet ring */
    bounds_out->supports_zero_copy = 1;
    bounds_out->is_kernel_bypass = 1;
    bounds_out->genome_bits_required = 3;
    return 1;
}

static int custom_xdp_execute(const FlowPrimitiveContext *ctx, FlowPrimitiveResult *res_out) {
    if (!ctx || !res_out) return -1;
    res_out->status_code = 0;
    res_out->bytes_transferred = ctx->data_len;
    res_out->latency_cycles = 15; /* XDP line-rate processing */
    res_out->zero_copy_active = 1;
    return 0;
}

static const FlowPrimitiveDriver s_custom_xdp_driver = {
    .driver_name = "ebpf_xdp_filter",
    .driver_version = "v1.0",
    .register_primitive = custom_xdp_register,
    .get_hardware_bounds = custom_xdp_get_bounds,
    .execute_primitive = custom_xdp_execute
};

FLOW_DECLARE_MOCK_DRIVER(mock_nvme_driver,
                         .queue_depth = 2048,
                         .buffer_bytes = 64 * 1024 * 1024,
                         .zero_copy = 1,
                         .is_kernel_bypass = 1,
                         .simulated_latency_cycles = 30);

int main(void) {
    printf("========================================================================================\n");
    printf("  🦾 Running FLOW Strategic Plugin Demotion: 3-Function Primitive Driver Suite\n");
    printf("========================================================================================\n\n");

    FlowPrimitiveRegistry registry;
    flow_primitive_registry_init(&registry);
    CHECK(flow_primitive_count(&registry) == 0);

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 1: Standard Driver Registration & Discovery                                 */
    /* ---------------------------------------------------------------------------------- */
    printf("--- [Stage 1: Register Built-in Primitive Drivers (io_uring, RDMA)] ---\n");
    CHECK(flow_primitive_register(&registry, flow_primitive_io_uring_driver()));
    CHECK(flow_primitive_register(&registry, flow_primitive_rdma_driver()));
    CHECK(flow_primitive_count(&registry) == 2);

    const FlowPrimitiveDriver *io_drv = flow_primitive_lookup(&registry, "io_uring");
    CHECK(io_drv != NULL);
    CHECK(strcmp(io_drv->driver_name, "io_uring") == 0);

    const FlowPrimitiveDriver *rdma_drv = flow_primitive_lookup(&registry, "rdma_qp");
    CHECK(rdma_drv != NULL);
    CHECK(strcmp(rdma_drv->driver_name, "rdma_qp") == 0);
    printf("  ✓ Drivers registered: 'io_uring' (v2.5) and 'rdma_qp' (v1.2) active in registry.\n\n");

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 2: SMT Physical Polytope Boundary Safety Audit                               */
    /* ---------------------------------------------------------------------------------- */
    printf("--- [Stage 2: SMT Formal Proof of Hardware Boundary Polytope] ---\n");
    FlowSMTProofAttestation proof;
    memset(&proof, 0, sizeof(proof));

    /* Case 2a: Safe parameters (queue_depth=1024, buffer=8MB) must be PROVEN UNSAT */
    FlowSMTResult res_safe = flow_primitive_verify_smt(io_drv, 1024, 8 * 1024 * 1024, &proof);
    CHECK(res_safe == FLOW_SMT_PROVEN_UNSAT);
    CHECK(proof.buffer_bounds_safety == FLOW_SMT_PROVEN_UNSAT);
    CHECK(proof.memory_quota_bound == FLOW_SMT_PROVEN_UNSAT);
    printf("  ✓ SMT Proof Sound: Candidate queue_depth=1024 <= 4096 physical bound (UNSAT Zero-Defect).\n");

    /* Case 2b: Excessive queue depth (5000 > 4096) must be REJECTED with SAT counterexample */
    FlowSMTResult res_viol_q = flow_primitive_verify_smt(io_drv, 5000, 8 * 1024 * 1024, &proof);
    CHECK(res_viol_q == FLOW_SMT_VIOLATION_SAT);
    CHECK(proof.buffer_bounds_safety == FLOW_SMT_VIOLATION_SAT);
    CHECK(strstr(proof.proof_summary, "exceeds hardware physical limit") != NULL);
    printf("  ✓ SMT Proof Violation: Candidate queue_depth=5000 > 4096 caught: '%s'\n", proof.proof_summary);

    /* Case 2c: Excessive DMA buffer (128MB > 64MB) must be REJECTED with SAT counterexample */
    FlowSMTResult res_viol_buf = flow_primitive_verify_smt(io_drv, 1024, 128ULL * 1024ULL * 1024ULL, &proof);
    CHECK(res_viol_buf == FLOW_SMT_VIOLATION_SAT);
    CHECK(proof.memory_quota_bound == FLOW_SMT_VIOLATION_SAT);
    CHECK(strstr(proof.proof_summary, "exceeds physical DMA limit") != NULL);
    printf("  ✓ SMT Proof Violation: Candidate buffer=128MB > 64MB caught: '%s'\n\n", proof.proof_summary);

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 3: Muscle Actuation: execute_primitive Dispatch                              */
    /* ---------------------------------------------------------------------------------- */
    printf("--- [Stage 3: Muscle Actuation: execute_primitive Hardware Invocation] ---\n");
    char test_payload[256];
    memset(test_payload, 'A', sizeof(test_payload));

    FlowPrimitiveContext ctx = {
        .active_genome = 0x00000004ULL,
        .user_data = test_payload,
        .data_len = sizeof(test_payload),
        .flags = 0
    };
    FlowPrimitiveResult result;
    memset(&result, 0, sizeof(result));

    CHECK(io_drv->execute_primitive(&ctx, &result) == 0);
    CHECK(result.status_code == 0);
    CHECK(result.bytes_transferred == sizeof(test_payload));
    CHECK(result.zero_copy_active == 1);
    CHECK(result.latency_cycles > 0);
    printf("  ✓ io_uring execution verified: Transferred %zu bytes, zero-copy=1, latency=%llu cycles.\n\n",
           result.bytes_transferred, (unsigned long long)result.latency_cycles);

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 4: Custom Developer Driver (3-Function ABI in 25 lines of C)                 */
    /* ---------------------------------------------------------------------------------- */
    printf("--- [Stage 4: 25-Line Developer Driver (eBPF XDP Filter)] ---\n");
    CHECK(flow_primitive_register(&registry, &s_custom_xdp_driver));
    CHECK(flow_primitive_count(&registry) == 3);

    const FlowPrimitiveDriver *xdp_drv = flow_primitive_lookup(&registry, "ebpf_xdp_filter");
    CHECK(xdp_drv != NULL);

    FlowSMTResult xdp_smt = flow_primitive_verify_smt(xdp_drv, 4096, 4 * 1024 * 1024, &proof);
    CHECK(xdp_smt == FLOW_SMT_PROVEN_UNSAT);

    FlowPrimitiveResult xdp_res;
    memset(&xdp_res, 0, sizeof(xdp_res));
    CHECK(xdp_drv->execute_primitive(&ctx, &xdp_res) == 0);
    CHECK(xdp_res.latency_cycles == 15);
    printf("  ✓ Custom eBPF XDP Driver verified: 3 functions, 0 AST callbacks, latency=%llu cycles!\n\n",
           (unsigned long long)xdp_res.latency_cycles);

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 5: Declarative Mock Driver (1-Line Driver Prototyping)                      */
    /* ---------------------------------------------------------------------------------- */
    printf("--- [Stage 5: Declarative Mock Driver (flow_mock_driver.h)] ---\n");
    CHECK(flow_primitive_register(&registry, &mock_nvme_driver));
    CHECK(flow_primitive_count(&registry) == 4);

    const FlowPrimitiveDriver *nvme_drv = flow_primitive_lookup(&registry, "mock_nvme_driver");
    CHECK(nvme_drv != NULL);

    FlowSMTResult nvme_smt = flow_primitive_verify_smt(nvme_drv, 2048, 64 * 1024 * 1024, &proof);
    CHECK(nvme_smt == FLOW_SMT_PROVEN_UNSAT);

    FlowPrimitiveResult nvme_res;
    memset(&nvme_res, 0, sizeof(nvme_res));
    CHECK(nvme_drv->execute_primitive(&ctx, &nvme_res) == 0);
    CHECK(nvme_res.latency_cycles == 30);
    printf("  ✓ Declarative Mock Driver verified: 1-line definition, 0 boilerplate, latency=%llu cycles!\n\n",
           (unsigned long long)nvme_res.latency_cycles);

    printf("========================================================================================\n");
    printf("PRIMITIVE_DRIVER_TEST=passed 3_function_abi=verified smt_polytope_guard=sound zero_copy=sound\n");
    printf("========================================================================================\n");
    return 0;
}
