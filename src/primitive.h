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

/*
 * ============================================================================
 * FLOW Hardware Primitive Driver Interface (The 3-Function Minimal Driver ABI)
 * ============================================================================
 * 
 * Philosophy:
 * - FLOW Core is the Brain (BMF Optimization + SMT Supreme Court).
 * - .fvec is the Long-Term Memory (Learned architecture weights / archetypes).
 * - Primitive Driver is the Sensory Organs & Muscles (Physical Hardware Drivers).
 * 
 * Traditional 24-callback compiler plugins are deprecated.
 * New drivers only expose physical OS/silicon capabilities:
 *   1. register_primitive()   -> Tells brain what hardware capability is exposed
 *   2. get_hardware_bounds()  -> Tells SMT court the physical safety polytope
 *   3. execute_primitive()    -> Tells muscles to perform the OS syscall/DMA
 * ============================================================================
 */

#define FLOW_PRIMITIVE_NAME_MAX 64
#define FLOW_PRIMITIVE_VERSION_MAX 32
#define FLOW_MAX_PRIMITIVE_DRIVERS 16

/* Physical Safety Boundary Constraints for SMT Invariant Verification */
typedef struct {
    char name[FLOW_PRIMITIVE_NAME_MAX];
    uint64_t max_queue_depth;       /* Physical limit, e.g. io_uring 4096 entries */
    uint64_t max_buffer_bytes;      /* Physical DMA limit, e.g. 64MB */
    uint32_t supports_zero_copy;    /* 1 = kernel bypass / zero copy supported */
    uint32_t is_kernel_bypass;      /* 1 = user-space DMA (e.g. DPDK/RDMA) */
    uint32_t genome_bits_required;  /* Bit count occupied in 64-bit BitSpace (1..16) */
} FlowHardwareBounds;

/* Execution Context passed during dynamic runtime activation */
typedef struct {
    uint64_t active_genome;         /* Configuration selected by BMF Engine */
    void *user_data;                /* IO buffer, socket, or descriptor */
    size_t data_len;                /* Payload size in bytes */
    uint64_t flags;                 /* Runtime operational flags */
} FlowPrimitiveContext;

/* Execution Result returned by the hardware driver */
typedef struct {
    int status_code;                /* 0 = success, OS errno on failure */
    size_t bytes_transferred;       /* Bytes read/written */
    uint64_t latency_cycles;        /* Time spent inside driver (TSC or nanoseconds) */
    int zero_copy_active;           /* Whether zero-copy was achieved */
} FlowPrimitiveResult;

/*
 * The Canonical 3-Function Hardware Primitive Driver Struct
 */
typedef struct FlowPrimitiveDriver {
    char driver_name[FLOW_PRIMITIVE_NAME_MAX];
    char driver_version[FLOW_PRIMITIVE_VERSION_MAX];

    /* 1. register_primitive: Inform the core brain of driver capability */
    int (*register_primitive)(void);

    /* 2. get_hardware_bounds: Inform SMT supreme court of physical limits */
    int (*get_hardware_bounds)(FlowHardwareBounds *bounds_out);

    /* 3. execute_primitive: Perform the actual OS syscall / ring queue dispatch */
    int (*execute_primitive)(const FlowPrimitiveContext *ctx, FlowPrimitiveResult *res_out);
} FlowPrimitiveDriver;

/* Driver Registry */
typedef struct {
    size_t count;
    const FlowPrimitiveDriver *drivers[FLOW_MAX_PRIMITIVE_DRIVERS];
} FlowPrimitiveRegistry;

/* Registry Lifecycle & Operations */
void flow_primitive_registry_init(FlowPrimitiveRegistry *reg);
int flow_primitive_register(FlowPrimitiveRegistry *reg, const FlowPrimitiveDriver *driver);
const FlowPrimitiveDriver *flow_primitive_lookup(const FlowPrimitiveRegistry *reg, const char *name);
size_t flow_primitive_count(const FlowPrimitiveRegistry *reg);

/* Formal SMT Safety Verification for Driver Parameters */
FlowSMTResult flow_primitive_verify_smt(const FlowPrimitiveDriver *driver,
                                       uint64_t candidate_queue_depth,
                                       uint64_t candidate_buffer_bytes,
                                       FlowSMTProofAttestation *proof_out);

/* Standard Built-in Primitive Driver Singletons */
const FlowPrimitiveDriver *flow_primitive_io_uring_driver(void);
const FlowPrimitiveDriver *flow_primitive_rdma_driver(void);

/* ============================================================================
 * Protocol-as-Primitive: HTTP/1.1, HTTP/2, and HTTP/3 QUIC Drivers
 * ============================================================================ */

typedef enum {
    FLOW_PROTO_NONE = 0,
    FLOW_PROTO_HTTP1 = 1,
    FLOW_PROTO_HTTP2 = 2,
    FLOW_PROTO_QUIC_HTTP3 = 3,
    FLOW_PROTO_GRPC = 4,
    FLOW_PROTO_WEBSOCKET = 5
} FlowProtocolKind;

typedef struct {
    FlowProtocolKind kind;
    uint32_t max_concurrent_streams;   /* HTTP/1=1, HTTP/2=128, HTTP/3=512, gRPC=256, WS=1024 */
    uint32_t max_header_table_bytes;   /* HPACK/QPACK dynamic table limit */
    uint32_t max_frame_payload_bytes;  /* Max binary/stream frame payload size */
    uint32_t idle_timeout_ms;          /* Keep-alive timeout */
    uint32_t supports_multiplexing;    /* 1 if multi-stream interleaving supported */
    uint32_t supports_datagram;        /* 1 if UDP/QUIC datagram supported */
} FlowProtocolBounds;

/* Built-in Protocol Drivers */
const FlowPrimitiveDriver *flow_primitive_http1_driver(void);
const FlowPrimitiveDriver *flow_primitive_http2_driver(void);
const FlowPrimitiveDriver *flow_primitive_quic_driver(void);
const FlowPrimitiveDriver *flow_primitive_grpc_driver(void);
const FlowPrimitiveDriver *flow_primitive_websocket_driver(void);

/* Protocol 64-Bit Subspace Genome Encoding & Decoding */
int flow_protocol_encode_genome(FlowProtocolKind kind,
                                uint32_t streams,
                                uint32_t header_table_bytes,
                                int zero_copy,
                                uint64_t *genome_out);

int flow_protocol_decode_genome(uint64_t genome,
                                FlowProtocolKind *kind_out,
                                uint32_t *streams_out,
                                uint32_t *header_table_bytes_out,
                                int *zero_copy_out);

/* SMT Formal Protocol Bounds Verification */
FlowSMTResult flow_primitive_verify_protocol_smt(const FlowPrimitiveDriver *driver,
                                                uint32_t candidate_streams,
                                                uint32_t candidate_header_table_bytes,
                                                FlowSMTProofAttestation *proof_out);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_PRIMITIVE_H */
