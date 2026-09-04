#ifndef FLOW_WIRE_FRAME_H
#define FLOW_WIRE_FRAME_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW 9-Byte Wire Pheromone Framing (Zero-Copy Header & Wire Skeleton)
 * ============================================================================
 *
 * Compact 9-byte packet format designed to fit into a single UDP datagram or
 * 64-bit + 8-bit register pair with zero heap allocation and zero-copy packing:
 *
 * ┌────────────────────────────────────────────────────────────────────────┐
 * │                     FLOW 9-Byte Wire Frame                             │
 * ├─────────┬──────────────────────────────────────────────────────────────┤
 * │ OpCode  │ 8-Byte Typed Payload (Antibody / HeteroMesh / Fleet / Raw)   │
 * │ (1 Byte)│                                                              │
 * └─────────┴──────────────────────────────────────────────────────────────┘
 * ============================================================================
 */

#define FLOW_WIRE_FRAME9_SIZE 9

#define FLOW_WIRE_OP_ANTIBODY     0xAA
#define FLOW_WIRE_OP_HETERO_MESH  0xBB
#define FLOW_WIRE_OP_FLEET_SYNC   0xCC

#pragma pack(push, 1)
typedef struct {
    uint8_t opcode;
    union {
        /* Opcode 0xAA: Swarm Lymphatic Antibody Broadcast (8-byte hash) */
        struct {
            uint8_t hash_bytes[8];
        } antibody;

        /* Opcode 0xBB: Heterogeneous Mesh Fluid Backpressure (6-byte payload + 2-byte CRC16) */
        struct {
            uint8_t role;
            uint8_t node_id;
            uint8_t bp_hi;
            uint8_t bp_lo;
            uint8_t lat_hi;
            uint8_t lat_lo;
            uint8_t crc_hi;
            uint8_t crc_lo;
        } hetero_mesh;

        /* Opcode 0xCC: Embodied Multi-Agent Fleet Telemetry */
        struct {
            uint8_t agent_id;
            uint8_t role;
            uint8_t x_hi;
            uint8_t x_lo;
            uint8_t y_hi;
            uint8_t y_lo;
            uint8_t crc_hi;
            uint8_t crc_lo;
        } fleet;

        /* Generic raw 8-byte payload */
        uint8_t raw[8];
    } payload;
} FlowWireFrame9;
#pragma pack(pop)

/* Fast CRC-16-CCITT for 9-byte packet validation */
static inline uint16_t flow_wire_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    if (data == NULL) return 0;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; ++b) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

/* Pack a generic FlowWireFrame9 into 9-byte buffer */
static inline int flow_wire_frame9_pack(const FlowWireFrame9 *frame, uint8_t out[FLOW_WIRE_FRAME9_SIZE]) {
    if (frame == NULL || out == NULL) return 0;
    memcpy(out, frame, FLOW_WIRE_FRAME9_SIZE);
    return 1;
}

/* Unpack 9-byte buffer into FlowWireFrame9 */
static inline int flow_wire_frame9_unpack(const uint8_t in[FLOW_WIRE_FRAME9_SIZE], FlowWireFrame9 *frame_out) {
    if (in == NULL || frame_out == NULL) return 0;
    memcpy(frame_out, in, FLOW_WIRE_FRAME9_SIZE);
    return 1;
}

/* Specialized Helper: Pack Lymphatic Antibody (0xAA) */
static inline int flow_wire_frame9_pack_antibody(uint64_t content_hash, uint8_t out[FLOW_WIRE_FRAME9_SIZE]) {
    if (out == NULL) return 0;
    FlowWireFrame9 f;
    f.opcode = FLOW_WIRE_OP_ANTIBODY;
    for (int i = 0; i < 8; ++i) {
        f.payload.antibody.hash_bytes[i] = (uint8_t)((content_hash >> (56 - i * 8)) & 0xFF);
    }
    return flow_wire_frame9_pack(&f, out);
}

/* Specialized Helper: Unpack Lymphatic Antibody (0xAA) */
static inline int flow_wire_frame9_unpack_antibody(const uint8_t in[FLOW_WIRE_FRAME9_SIZE], uint64_t *hash_out) {
    if (in == NULL || hash_out == NULL) return 0;
    FlowWireFrame9 f;
    if (!flow_wire_frame9_unpack(in, &f) || f.opcode != FLOW_WIRE_OP_ANTIBODY) return 0;
    uint64_t h = 0;
    for (int i = 0; i < 8; ++i) {
        h = (h << 8) | f.payload.antibody.hash_bytes[i];
    }
    *hash_out = h;
    return 1;
}

/* Specialized Helper: Pack Heterogeneous Mesh Pheromone (0xBB) */
static inline int flow_wire_frame9_pack_hetero(uint8_t role, uint8_t node_id,
                                              uint16_t backpressure_permille,
                                              uint16_t latency_p99_us,
                                              uint16_t contract_crc16,
                                              uint8_t out[FLOW_WIRE_FRAME9_SIZE]) {
    if (out == NULL) return 0;
    FlowWireFrame9 f;
    f.opcode = FLOW_WIRE_OP_HETERO_MESH;
    f.payload.hetero_mesh.role = role;
    f.payload.hetero_mesh.node_id = node_id;
    f.payload.hetero_mesh.bp_hi = (uint8_t)((backpressure_permille >> 8) & 0xFF);
    f.payload.hetero_mesh.bp_lo = (uint8_t)(backpressure_permille & 0xFF);
    f.payload.hetero_mesh.lat_hi = (uint8_t)((latency_p99_us >> 8) & 0xFF);
    f.payload.hetero_mesh.lat_lo = (uint8_t)(latency_p99_us & 0xFF);
    f.payload.hetero_mesh.crc_hi = (uint8_t)((contract_crc16 >> 8) & 0xFF);
    f.payload.hetero_mesh.crc_lo = (uint8_t)(contract_crc16 & 0xFF);
    return flow_wire_frame9_pack(&f, out);
}

/* Specialized Helper: Unpack Heterogeneous Mesh Pheromone (0xBB) */
static inline int flow_wire_frame9_unpack_hetero(const uint8_t in[FLOW_WIRE_FRAME9_SIZE],
                                                uint8_t *role_out, uint8_t *node_id_out,
                                                uint16_t *bp_out, uint16_t *lat_out,
                                                uint16_t *crc_out) {
    if (in == NULL) return 0;
    FlowWireFrame9 f;
    if (!flow_wire_frame9_unpack(in, &f) || f.opcode != FLOW_WIRE_OP_HETERO_MESH) return 0;
    if (role_out) *role_out = f.payload.hetero_mesh.role;
    if (node_id_out) *node_id_out = f.payload.hetero_mesh.node_id;
    if (bp_out) *bp_out = ((uint16_t)f.payload.hetero_mesh.bp_hi << 8) | f.payload.hetero_mesh.bp_lo;
    if (lat_out) *lat_out = ((uint16_t)f.payload.hetero_mesh.lat_hi << 8) | f.payload.hetero_mesh.lat_lo;
    if (crc_out) *crc_out = ((uint16_t)f.payload.hetero_mesh.crc_hi << 8) | f.payload.hetero_mesh.crc_lo;
    return 1;
}

/* Specialized Helper: Pack Fleet Telemetry (0xCC) */
static inline int flow_wire_frame9_pack_fleet(uint8_t agent_id, uint8_t role,
                                             int16_t x_mm, int16_t y_mm,
                                             uint8_t out[FLOW_WIRE_FRAME9_SIZE]) {
    if (out == NULL) return 0;
    FlowWireFrame9 f;
    f.opcode = FLOW_WIRE_OP_FLEET_SYNC;
    f.payload.fleet.agent_id = agent_id;
    f.payload.fleet.role = role;
    uint16_t ux = (uint16_t)x_mm;
    uint16_t uy = (uint16_t)y_mm;
    f.payload.fleet.x_hi = (uint8_t)((ux >> 8) & 0xFF);
    f.payload.fleet.x_lo = (uint8_t)(ux & 0xFF);
    f.payload.fleet.y_hi = (uint8_t)((uy >> 8) & 0xFF);
    f.payload.fleet.y_lo = (uint8_t)(uy & 0xFF);

    /* Calculate CRC16 over first 7 bytes */
    uint16_t crc = flow_wire_crc16((const uint8_t *)&f, 7);
    f.payload.fleet.crc_hi = (uint8_t)((crc >> 8) & 0xFF);
    f.payload.fleet.crc_lo = (uint8_t)(crc & 0xFF);
    return flow_wire_frame9_pack(&f, out);
}

/* Specialized Helper: Unpack Fleet Telemetry (0xCC) */
static inline int flow_wire_frame9_unpack_fleet(const uint8_t in[FLOW_WIRE_FRAME9_SIZE],
                                               uint8_t *agent_id_out, uint8_t *role_out,
                                               int16_t *x_mm_out, int16_t *y_mm_out) {
    if (in == NULL) return 0;
    FlowWireFrame9 f;
    if (!flow_wire_frame9_unpack(in, &f) || f.opcode != FLOW_WIRE_OP_FLEET_SYNC) return 0;

    /* Verify CRC16 */
    uint16_t expected_crc = ((uint16_t)f.payload.fleet.crc_hi << 8) | f.payload.fleet.crc_lo;
    uint16_t actual_crc = flow_wire_crc16(in, 7);
    if (expected_crc != actual_crc) return 0;

    if (agent_id_out) *agent_id_out = f.payload.fleet.agent_id;
    if (role_out) *role_out = f.payload.fleet.role;
    if (x_mm_out) {
        uint16_t ux = ((uint16_t)f.payload.fleet.x_hi << 8) | f.payload.fleet.x_lo;
        *x_mm_out = (int16_t)ux;
    }
    if (y_mm_out) {
        uint16_t uy = ((uint16_t)f.payload.fleet.y_hi << 8) | f.payload.fleet.y_lo;
        *y_mm_out = (int16_t)uy;
    }
    return 1;
}

#ifdef __cplusplus
}
#endif

#endif /* FLOW_WIRE_FRAME_H */
