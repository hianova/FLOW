#ifndef FLOW_PRIMITIVE_H
#define FLOW_PRIMITIVE_H
#include "smt.h"
#include "bitmanifold.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
#define FLOW_PRIMITIVE_NAME_MAX 64
#define FLOW_PRIMITIVE_VERSION_MAX 32
#define FLOW_MAX_PRIMITIVE_DRIVERS 16
typedef struct { char name[FLOW_PRIMITIVE_NAME_MAX]; uint64_t max_queue_depth, max_buffer_bytes; uint32_t supports_zero_copy, is_kernel_bypass, genome_bits_required;
} FlowHardwareBounds; typedef struct { uint64_t active_genome; void *user_data; size_t data_len; uint64_t flags; } FlowPrimitiveContext; typedef struct { int status_code;
    size_t bytes_transferred; uint64_t latency_cycles; int zero_copy_active; } FlowPrimitiveResult; typedef struct FlowPrimitiveDriver {
    char driver_name[FLOW_PRIMITIVE_NAME_MAX]; char driver_version[FLOW_PRIMITIVE_VERSION_MAX]; int (*register_primitive)(void);
    int (*get_hardware_bounds)(FlowHardwareBounds *bounds_out); int (*execute_primitive)(const FlowPrimitiveContext *ctx, FlowPrimitiveResult *res_out); } FlowPrimitiveDriver;
typedef struct { size_t count; const FlowPrimitiveDriver *drivers[FLOW_MAX_PRIMITIVE_DRIVERS]; } FlowPrimitiveRegistry;
void flow_primitive_registry_init(FlowPrimitiveRegistry *reg); int flow_primitive_register(FlowPrimitiveRegistry *reg, const FlowPrimitiveDriver *driver);
const FlowPrimitiveDriver *flow_primitive_lookup(const FlowPrimitiveRegistry *reg, const char *name); size_t flow_primitive_count(const FlowPrimitiveRegistry *reg);
FlowSMTResult flow_primitive_verify_smt(const FlowPrimitiveDriver *driver, uint64_t candidate_queue_depth, uint64_t candidate_buffer_bytes, FlowSMTProofAttestation *proof_out);
const FlowPrimitiveDriver *flow_primitive_io_uring_driver(void); const FlowPrimitiveDriver *flow_primitive_rdma_driver(void);
const FlowPrimitiveDriver *flow_primitive_accelerator_driver(void); typedef enum { FLOW_PROTO_NONE = 0, FLOW_PROTO_HTTP1 = 1, FLOW_PROTO_HTTP2 = 2, FLOW_PROTO_QUIC_HTTP3 = 3,
    FLOW_PROTO_GRPC = 4, FLOW_PROTO_WEBSOCKET = 5
} FlowProtocolKind; typedef struct { FlowProtocolKind kind;
    uint32_t max_concurrent_streams, max_header_table_bytes, max_frame_payload_bytes, idle_timeout_ms, supports_multiplexing, supports_datagram; } FlowProtocolBounds;
const FlowPrimitiveDriver *flow_primitive_http1_driver(void); const FlowPrimitiveDriver *flow_primitive_http2_driver(void);
const FlowPrimitiveDriver *flow_primitive_quic_driver(void); const FlowPrimitiveDriver *flow_primitive_grpc_driver(void);
const FlowPrimitiveDriver *flow_primitive_websocket_driver(void);
int flow_protocol_encode_genome(FlowProtocolKind kind, uint32_t streams, uint32_t header_table_bytes, int zero_copy, uint64_t *genome_out);
int flow_protocol_decode_genome(uint64_t genome, FlowProtocolKind *kind_out, uint32_t *streams_out, uint32_t *header_table_bytes_out, int *zero_copy_out);
FlowSMTResult flow_primitive_verify_protocol_smt(const FlowPrimitiveDriver *driver, uint32_t candidate_streams, uint32_t candidate_header_table_bytes, FlowSMTProofAttestation *proof_out);
#ifdef __cplusplus
}
#endif
#endif /* FLOW_PRIMITIVE_H */
