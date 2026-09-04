#include "primitive.h"
#include "flow_smt_dsl.h"
#include "flow_str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void flow_primitive_registry_init(FlowPrimitiveRegistry *reg) {
    if (reg == NULL) return;
    memset(reg, 0, sizeof(*reg));
}

int flow_primitive_register(FlowPrimitiveRegistry *reg, const FlowPrimitiveDriver *driver) {
    if (reg == NULL || driver == NULL) return 0;
    if (reg->count >= FLOW_MAX_PRIMITIVE_DRIVERS) return 0;

    /* Execute driver registration hook if present */
    if (driver->register_primitive != NULL) {
        if (!driver->register_primitive()) {
            return 0; /* Hardware registration failed (e.g. device missing) */
        }
    }

    reg->drivers[reg->count++] = driver;
    return 1;
}

const FlowPrimitiveDriver *flow_primitive_lookup(const FlowPrimitiveRegistry *reg, const char *name) {
    if (reg == NULL || name == NULL) return NULL;
    for (size_t i = 0; i < reg->count; ++i) {
        if (reg->drivers[i] != NULL && strcmp(reg->drivers[i]->driver_name, name) == 0) {
            return reg->drivers[i];
        }
    }
    return NULL;
}

size_t flow_primitive_count(const FlowPrimitiveRegistry *reg) {
    return reg != NULL ? reg->count : 0;
}

FlowSMTResult flow_primitive_verify_smt(const FlowPrimitiveDriver *driver,
                                       uint64_t candidate_queue_depth,
                                       uint64_t candidate_buffer_bytes,
                                       FlowSMTProofAttestation *proof_out) {
    if (driver == NULL || driver->get_hardware_bounds == NULL) {
        if (proof_out) {
            proof_out->buffer_bounds_safety = FLOW_SMT_UNKNOWN;
            proof_out->memory_quota_bound = FLOW_SMT_UNKNOWN;
            proof_out->shard_non_aliasing = FLOW_SMT_UNKNOWN;
            proof_out->determinism_invariant = FLOW_SMT_UNKNOWN;
            snprintf(proof_out->proof_summary, sizeof(proof_out->proof_summary),
                     "SMT Driver Error: driver or bounds hook missing");
        }
        return FLOW_SMT_UNKNOWN;
    }

    FlowHardwareBounds bounds;
    memset(&bounds, 0, sizeof(bounds));
    if (!driver->get_hardware_bounds(&bounds)) {
        if (proof_out) {
            proof_out->buffer_bounds_safety = FLOW_SMT_UNKNOWN;
            snprintf(proof_out->proof_summary, sizeof(proof_out->proof_summary),
                     "SMT Driver Error: get_hardware_bounds rejected");
        }
        return FLOW_SMT_UNKNOWN;
    }

    /* Unified SMT Hyper-box Constraint Verification (QF_LIA) using flow_smt_dsl */
    FLOW_SMT_BOX_BUILDER_DECL(builder);
    FLOW_SMT_BOX_ADD_RULE(builder, "queue depth", candidate_queue_depth, 1, bounds.max_queue_depth,
                          FLOW_BOX_THEOREM_BUFFER_BOUNDS, "exceeds hardware physical limit");
    FLOW_SMT_BOX_ADD_RULE(builder, "buffer bytes", candidate_buffer_bytes, 0, bounds.max_buffer_bytes,
                          FLOW_BOX_THEOREM_MEMORY_QUOTA, "exceeds physical DMA limit");

    FlowSMTResult res = FLOW_SMT_BOX_VERIFY(builder, driver->driver_name, proof_out);
    if (res == FLOW_SMT_PROVEN_UNSAT && proof_out != NULL) {
        snprintf(proof_out->proof_summary, sizeof(proof_out->proof_summary),
                 "SMT PROVEN SOUND: queue_depth=%llu <= %llu, buffer=%llu <= %llu, zero_copy=%u",
                 (unsigned long long)candidate_queue_depth,
                 (unsigned long long)bounds.max_queue_depth,
                 (unsigned long long)candidate_buffer_bytes,
                 (unsigned long long)bounds.max_buffer_bytes,
                 bounds.supports_zero_copy);
    }
    return res;
}

/* ============================================================================
 * Standard Built-in & Protocol-as-Primitive Drivers (Vectorized Definition)
 * ============================================================================ */

static int default_primitive_register(void) {
    return 1;
}

#define FLOW_DEFINE_PRIMITIVE_DRIVER(id, name_str, ver_str, qdepth, buf_mb, zc, kb, gbits, lat, inspect_block) \
static int id##_bounds(FlowHardwareBounds *b) { \
    if (!b) return 0; \
    flow_str_copy(b->name, sizeof(b->name), name_str); \
    b->max_queue_depth = (qdepth); \
    b->max_buffer_bytes = (uint64_t)(buf_mb) * 1024ULL * 1024ULL; \
    b->supports_zero_copy = (zc); \
    b->is_kernel_bypass = (kb); \
    b->genome_bits_required = (gbits); \
    return 1; \
} \
static int id##_exec(const FlowPrimitiveContext *ctx, FlowPrimitiveResult *res) { \
    if (!ctx || !res) return -1; \
    res->status_code = 0; \
    res->bytes_transferred = ctx->data_len; \
    res->latency_cycles = (lat); \
    res->zero_copy_active = 1; \
    inspect_block; \
    return 0; \
} \
static const FlowPrimitiveDriver s_##id##_driver = { \
    .driver_name = name_str, \
    .driver_version = ver_str, \
    .register_primitive = default_primitive_register, \
    .get_hardware_bounds = id##_bounds, \
    .execute_primitive = id##_exec \
}; \
const FlowPrimitiveDriver *flow_primitive_##id##_driver(void) { \
    return &s_##id##_driver; \
}

/* Built-in Standard Reference Drivers */
FLOW_DEFINE_PRIMITIVE_DRIVER(io_uring, "io_uring", "v2.5", 4096, 64, 1, 0, 4, 120, (void)0)
FLOW_DEFINE_PRIMITIVE_DRIVER(rdma, "rdma_qp", "v1.2", 16384, 512, 1, 1, 6, 45, (void)0)

/* Built-in Protocol Drivers */
FLOW_DEFINE_PRIMITIVE_DRIVER(http1, "http1_stream", "v1.1", 1, 16, 1, 0, 4, 80, \
    if (ctx->user_data != NULL && ctx->data_len >= 4) { \
        const char *b = (const char *)ctx->user_data; \
        if (strncmp(b, "GET ", 4) == 0 || strncmp(b, "POST", 4) == 0 || \
            strncmp(b, "PUT ", 4) == 0 || strncmp(b, "HTTP", 4) == 0) res->status_code = 200; \
    })
FLOW_DEFINE_PRIMITIVE_DRIVER(http2, "http2_frame", "v2.0", 128, 64, 1, 0, 6, 50, \
    if (ctx->user_data != NULL && ctx->data_len >= 9) res->status_code = 200)
FLOW_DEFINE_PRIMITIVE_DRIVER(quic, "quic_datagram", "v3.0", 512, 128, 1, 1, 8, 25, \
    if (ctx->user_data != NULL && ctx->data_len >= 1 && (((const uint8_t *)ctx->user_data)[0] & 0x40) != 0) res->status_code = 200)
FLOW_DEFINE_PRIMITIVE_DRIVER(grpc, "grpc_stream", "v1.0", 256, 32, 1, 0, 6, 45, \
    if (ctx->user_data != NULL && ctx->data_len >= 5) res->status_code = 200)
FLOW_DEFINE_PRIMITIVE_DRIVER(websocket, "websocket_frame", "v1.0", 1024, 64, 1, 0, 6, 30, \
    if (ctx->user_data != NULL && ctx->data_len >= 2) res->status_code = 200)

/* ============================================================================
 * Protocol 64-Bit Subspace Genome Encoding & Decoding
 * ============================================================================ */

/* Protocol Genome Bitfield Subspace Layout (BitManifold BMF) */
#define FLOW_PROTO_OFF_KIND     0
#define FLOW_PROTO_LEN_KIND     3
#define FLOW_PROTO_OFF_STREAMS  3
#define FLOW_PROTO_LEN_STREAMS  9
#define FLOW_PROTO_OFF_HEADER   12
#define FLOW_PROTO_LEN_HEADER   7
#define FLOW_PROTO_OFF_ZCOPY    19
#define FLOW_PROTO_LEN_ZCOPY    1

int flow_protocol_encode_genome(FlowProtocolKind kind,
                                uint32_t streams,
                                uint32_t header_table_bytes,
                                int zero_copy,
                                uint64_t *genome_out) {
    if (genome_out == NULL) return 0;

    uint32_t st = (streams > 512) ? 512 : streams;
    uint32_t ht = (header_table_bytes >> 6) & 0x7F;

    /* Declarative BMF BitField Subspace Slicing */
    uint64_t g = FLOW_GENOME_PACK((uint64_t)kind, FLOW_PROTO_OFF_KIND, FLOW_PROTO_LEN_KIND)
               | FLOW_GENOME_PACK((uint64_t)st,   FLOW_PROTO_OFF_STREAMS, FLOW_PROTO_LEN_STREAMS)
               | FLOW_GENOME_PACK((uint64_t)ht,   FLOW_PROTO_OFF_HEADER, FLOW_PROTO_LEN_HEADER)
               | FLOW_GENOME_PACK(zero_copy ? 1ULL : 0ULL, FLOW_PROTO_OFF_ZCOPY, FLOW_PROTO_LEN_ZCOPY);

    *genome_out = g;
    return 1;
}

int flow_protocol_decode_genome(uint64_t genome,
                                FlowProtocolKind *kind_out,
                                uint32_t *streams_out,
                                uint32_t *header_table_bytes_out,
                                int *zero_copy_out) {
    if (kind_out) {
        *kind_out = (FlowProtocolKind)FLOW_GENOME_GET(genome, FLOW_PROTO_OFF_KIND, FLOW_PROTO_LEN_KIND);
    }
    if (streams_out) {
        *streams_out = (uint32_t)FLOW_GENOME_GET(genome, FLOW_PROTO_OFF_STREAMS, FLOW_PROTO_LEN_STREAMS);
    }
    if (header_table_bytes_out) {
        uint32_t ht = (uint32_t)FLOW_GENOME_GET(genome, FLOW_PROTO_OFF_HEADER, FLOW_PROTO_LEN_HEADER);
        *header_table_bytes_out = ht << 6;
    }
    if (zero_copy_out) {
        *zero_copy_out = (int)FLOW_GENOME_GET(genome, FLOW_PROTO_OFF_ZCOPY, FLOW_PROTO_LEN_ZCOPY);
    }
    return 1;
}

/* ============================================================================
 * SMT Formal Protocol Bounds Verification
 * ============================================================================ */

FlowSMTResult flow_primitive_verify_protocol_smt(const FlowPrimitiveDriver *driver,
                                                uint32_t candidate_streams,
                                                uint32_t candidate_header_table_bytes,
                                                FlowSMTProofAttestation *proof_out) {
    if (driver == NULL || driver->get_hardware_bounds == NULL) {
        if (proof_out) {
            proof_out->buffer_bounds_safety = FLOW_SMT_UNKNOWN;
            proof_out->memory_quota_bound = FLOW_SMT_UNKNOWN;
            proof_out->shard_non_aliasing = FLOW_SMT_UNKNOWN;
            proof_out->determinism_invariant = FLOW_SMT_UNKNOWN;
            snprintf(proof_out->proof_summary, sizeof(proof_out->proof_summary),
                     "SMT Protocol Error: driver or bounds hook missing");
        }
        return FLOW_SMT_UNKNOWN;
    }

    FlowHardwareBounds bounds;
    memset(&bounds, 0, sizeof(bounds));
    if (!driver->get_hardware_bounds(&bounds)) {
        if (proof_out) {
            proof_out->buffer_bounds_safety = FLOW_SMT_UNKNOWN;
            snprintf(proof_out->proof_summary, sizeof(proof_out->proof_summary),
                     "SMT Protocol Error: get_hardware_bounds rejected");
        }
        return FLOW_SMT_UNKNOWN;
    }

    const uint32_t max_allowed_header_table = 65536; /* 64 KB strict ceiling */

    /* Unified SMT Hyper-box Constraint Verification (QF_LIA) using flow_smt_dsl */
    FLOW_SMT_BOX_BUILDER_DECL(builder);
    FLOW_SMT_BOX_ADD_RULE(builder, "streams", candidate_streams, 1, bounds.max_queue_depth,
                          FLOW_BOX_THEOREM_BUFFER_BOUNDS, "exceeds protocol physical bound");
    FLOW_SMT_BOX_ADD_RULE(builder, "header table", candidate_header_table_bytes, 0, max_allowed_header_table,
                          FLOW_BOX_THEOREM_MEMORY_QUOTA, "exceeds safe ceiling");

    FlowSMTResult res = FLOW_SMT_BOX_VERIFY(builder, bounds.name, proof_out);
    if (res == FLOW_SMT_PROVEN_UNSAT && proof_out != NULL) {
        snprintf(proof_out->proof_summary, sizeof(proof_out->proof_summary),
                 "SMT PROTOCOL SOUND: protocol=%s, streams=%u <= %llu, header_table=%uB <= %uB",
                 bounds.name, candidate_streams, (unsigned long long)bounds.max_queue_depth,
                 candidate_header_table_bytes, max_allowed_header_table);
    }
    return res;
}
