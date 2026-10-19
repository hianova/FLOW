#include "primitive.h"
#include "flow_smt_dsl.h"
#include "flow_str.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void flow_primitive_registry_init(FlowPrimitiveRegistry *reg) {
    if (reg) memset(reg, 0, sizeof(*reg));
}

int flow_primitive_register(FlowPrimitiveRegistry *reg, const FlowPrimitiveDriver *driver) {
    if (!reg || !driver || reg->count >= FLOW_MAX_PRIMITIVE_DRIVERS) return 0;
    if (driver->register_primitive && !driver->register_primitive()) return 0;
    reg->drivers[reg->count++] = driver;
    return 1;
}

const FlowPrimitiveDriver *flow_primitive_lookup(const FlowPrimitiveRegistry *reg, const char *name) {
    if (!reg || !name) return NULL;
    for (size_t i = 0; i < reg->count; ++i)
        if (reg->drivers[i] && strcmp(reg->drivers[i]->driver_name, name) == 0) return reg->drivers[i];
    return NULL;
}

size_t flow_primitive_count(const FlowPrimitiveRegistry *reg) {
    return reg ? reg->count : 0;
}

static int query_driver_bounds(const FlowPrimitiveDriver *driver, FlowHardwareBounds *bounds, const char *err_prefix, FlowSMTProofAttestation *proof_out) {
    if (!driver || !driver->get_hardware_bounds) {
        if (proof_out) {
            *proof_out = (FlowSMTProofAttestation){FLOW_SMT_UNKNOWN, FLOW_SMT_UNKNOWN, FLOW_SMT_UNKNOWN, FLOW_SMT_UNKNOWN, {0}, {0}};
            snprintf(proof_out->proof_summary, sizeof(proof_out->proof_summary), "%s: driver or bounds hook missing", err_prefix);
        }
        return 0;
    }
    memset(bounds, 0, sizeof(*bounds));
    if (!driver->get_hardware_bounds(bounds)) {
        if (proof_out) {
            proof_out->buffer_bounds_safety = FLOW_SMT_UNKNOWN;
            snprintf(proof_out->proof_summary, sizeof(proof_out->proof_summary), "%s: get_hardware_bounds rejected", err_prefix);
        }
        return 0;
    }
    return 1;
}

FlowSMTResult flow_primitive_verify_smt(const FlowPrimitiveDriver *driver, uint64_t candidate_queue_depth, uint64_t candidate_buffer_bytes, FlowSMTProofAttestation *proof_out) {
    FlowHardwareBounds b;
    if (!query_driver_bounds(driver, &b, "SMT Driver Error", proof_out)) return FLOW_SMT_UNKNOWN;
    const FlowBoxConstraint rules[] = {
        FLOW_SMT_RULE("queue depth", candidate_queue_depth, 1, b.max_queue_depth, FLOW_BOX_THEOREM_BUFFER_BOUNDS, "exceeds hardware physical limit"),
        FLOW_SMT_RULE("buffer bytes", candidate_buffer_bytes, 0, b.max_buffer_bytes, FLOW_BOX_THEOREM_MEMORY_QUOTA, "exceeds physical DMA limit")
    };
    return FLOW_SMT_VERIFY_RULES(driver->driver_name, proof_out, rules,
        "SMT PROVEN SOUND: queue_depth=%llu <= %llu, buffer=%llu <= %llu, zero_copy=%u",
        (unsigned long long)candidate_queue_depth, (unsigned long long)b.max_queue_depth,
        (unsigned long long)candidate_buffer_bytes, (unsigned long long)b.max_buffer_bytes, b.supports_zero_copy);
}

static int default_primitive_register(void) { return 1; }

#define FLOW_DEFINE_PRIMITIVE_DRIVER(id, name_str, ver_str, qdepth, buf_mb, zc, kb, gbits, lat, inspect_block) \
static int id##_bounds(FlowHardwareBounds *b) { \
    if (!b) return 0; \
    *b = (FlowHardwareBounds){ .max_queue_depth = (qdepth), .max_buffer_bytes = (uint64_t)(buf_mb) << 20, .supports_zero_copy = (zc), .is_kernel_bypass = (kb), .genome_bits_required = (gbits) }; \
    flow_str_copy(b->name, sizeof(b->name), name_str); \
    return 1; \
} \
static int id##_exec(const FlowPrimitiveContext *ctx, FlowPrimitiveResult *res) { \
    if (!ctx || !res) return -1; \
    *res = (FlowPrimitiveResult){ .status_code = 0, .bytes_transferred = ctx->data_len, .latency_cycles = (lat), .zero_copy_active = 1 }; \
    inspect_block; \
    return 0; \
} \
static const FlowPrimitiveDriver s_##id##_driver = { name_str, ver_str, default_primitive_register, id##_bounds, id##_exec }; \
const FlowPrimitiveDriver *flow_primitive_##id##_driver(void) { return &s_##id##_driver; }

FLOW_DEFINE_PRIMITIVE_DRIVER(io_uring, "io_uring", "v2.5", 4096, 64, 1, 0, 4, 120, (void)0)
FLOW_DEFINE_PRIMITIVE_DRIVER(rdma, "rdma_qp", "v1.2", 16384, 512, 1, 1, 6, 45, (void)0)
FLOW_DEFINE_PRIMITIVE_DRIVER(accelerator, "accelerator", "v1.0", 8192, 1024, 1, 1, 8, 25, (void)0)
FLOW_DEFINE_PRIMITIVE_DRIVER(http1, "http1_stream", "v1.1", 1, 16, 1, 0, 4, 80, \
    if (ctx->user_data && ctx->data_len >= 4) { \
        const char *p = (const char *)ctx->user_data; \
        if (!strncmp(p, "GET ", 4) || !strncmp(p, "POST", 4) || !strncmp(p, "PUT ", 4) || !strncmp(p, "HTTP", 4)) res->status_code = 200; \
    })
FLOW_DEFINE_PRIMITIVE_DRIVER(http2, "http2_frame", "v2.0", 128, 64, 1, 0, 6, 50, if (ctx->user_data && ctx->data_len >= 9) res->status_code = 200)
FLOW_DEFINE_PRIMITIVE_DRIVER(quic, "quic_datagram", "v3.0", 512, 128, 1, 1, 8, 25, if (ctx->user_data && ctx->data_len >= 1 && (((const uint8_t *)ctx->user_data)[0] & 0x40)) res->status_code = 200)
FLOW_DEFINE_PRIMITIVE_DRIVER(grpc, "grpc_stream", "v1.0", 256, 32, 1, 0, 6, 45, if (ctx->user_data && ctx->data_len >= 5) res->status_code = 200)
FLOW_DEFINE_PRIMITIVE_DRIVER(websocket, "websocket_frame", "v1.0", 1024, 64, 1, 0, 6, 30, if (ctx->user_data && ctx->data_len >= 2) res->status_code = 200)

#define FLOW_PROTO_OFF_KIND    0
#define FLOW_PROTO_LEN_KIND    3
#define FLOW_PROTO_OFF_STREAMS 3
#define FLOW_PROTO_LEN_STREAMS 9
#define FLOW_PROTO_OFF_HEADER  12
#define FLOW_PROTO_LEN_HEADER  7
#define FLOW_PROTO_OFF_ZCOPY   19
#define FLOW_PROTO_LEN_ZCOPY   1

int flow_protocol_encode_genome(FlowProtocolKind kind, uint32_t streams, uint32_t header_table_bytes, int zero_copy, uint64_t *genome_out) {
    if (!genome_out) return 0;
    uint32_t st = (streams > 512) ? 512 : streams, ht = (header_table_bytes >> 6) & 0x7F;
    *genome_out = FLOW_GENOME_PACK((uint64_t)kind, FLOW_PROTO_OFF_KIND, FLOW_PROTO_LEN_KIND)
                | FLOW_GENOME_PACK((uint64_t)st,   FLOW_PROTO_OFF_STREAMS, FLOW_PROTO_LEN_STREAMS)
                | FLOW_GENOME_PACK((uint64_t)ht,   FLOW_PROTO_OFF_HEADER, FLOW_PROTO_LEN_HEADER)
                | FLOW_GENOME_PACK(zero_copy ? 1ULL : 0ULL, FLOW_PROTO_OFF_ZCOPY, FLOW_PROTO_LEN_ZCOPY);
    return 1;
}

int flow_protocol_decode_genome(uint64_t genome, FlowProtocolKind *kind_out, uint32_t *streams_out, uint32_t *header_table_bytes_out, int *zero_copy_out) {
    if (kind_out) *kind_out = (FlowProtocolKind)FLOW_GENOME_GET(genome, FLOW_PROTO_OFF_KIND, FLOW_PROTO_LEN_KIND);
    if (streams_out) *streams_out = (uint32_t)FLOW_GENOME_GET(genome, FLOW_PROTO_OFF_STREAMS, FLOW_PROTO_LEN_STREAMS);
    if (header_table_bytes_out) *header_table_bytes_out = (uint32_t)FLOW_GENOME_GET(genome, FLOW_PROTO_OFF_HEADER, FLOW_PROTO_LEN_HEADER) << 6;
    if (zero_copy_out) *zero_copy_out = (int)FLOW_GENOME_GET(genome, FLOW_PROTO_OFF_ZCOPY, FLOW_PROTO_LEN_ZCOPY);
    return 1;
}

FlowSMTResult flow_primitive_verify_protocol_smt(const FlowPrimitiveDriver *driver, uint32_t candidate_streams, uint32_t candidate_header_table_bytes, FlowSMTProofAttestation *proof_out) {
    FlowHardwareBounds b;
    if (!query_driver_bounds(driver, &b, "SMT Protocol Error", proof_out)) return FLOW_SMT_UNKNOWN;
    const FlowBoxConstraint rules[] = {
        FLOW_SMT_RULE("streams", candidate_streams, 1, b.max_queue_depth, FLOW_BOX_THEOREM_BUFFER_BOUNDS, "exceeds protocol physical bound"),
        FLOW_SMT_RULE("header table", candidate_header_table_bytes, 0, 65536, FLOW_BOX_THEOREM_MEMORY_QUOTA, "exceeds safe ceiling")
    };
    return FLOW_SMT_VERIFY_RULES(b.name, proof_out, rules,
        "SMT PROTOCOL SOUND: protocol=%s, streams=%u <= %llu, header_table=%uB <= 65536B",
        b.name, candidate_streams, (unsigned long long)b.max_queue_depth, candidate_header_table_bytes);
}
