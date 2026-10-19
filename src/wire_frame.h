#ifndef FLOW_WIRE_FRAME_H
#define FLOW_WIRE_FRAME_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#ifdef __cplusplus
extern "C" {
#endif
#define FLOW_WIRE_FRAME9_SIZE 9
#define FLOW_WIRE_OP_ANTIBODY     0xAA
#define FLOW_WIRE_OP_HETERO_MESH  0xBB
#define FLOW_WIRE_OP_FLEET_SYNC   0xCC
#pragma pack(push, 1)
typedef struct { uint8_t opcode; union { struct { uint8_t hash_bytes[8]; } antibody;
        struct { uint8_t role, node_id, bp_hi, bp_lo, lat_hi, lat_lo, crc_hi, crc_lo; } hetero_mesh;
        struct { uint8_t agent_id, role, x_hi, x_lo, y_hi, y_lo, crc_hi, crc_lo; } fleet; uint8_t raw[8]; } payload; } FlowWireFrame9;
#pragma pack(pop)
static inline uint16_t flow_wire_crc16(const uint8_t *data, size_t len) { uint16_t crc = 0xFFFF; if (!data) return 0; for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i] << 8; for (int b = 0; b < 8; ++b) crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1); } return crc; }
static inline int flow_wire_frame9_pack(const FlowWireFrame9 *frame, uint8_t out[FLOW_WIRE_FRAME9_SIZE]) { if (!frame || !out) return 0;
    memcpy(out, frame, FLOW_WIRE_FRAME9_SIZE); return 1; } static inline int flow_wire_frame9_unpack(const uint8_t in[FLOW_WIRE_FRAME9_SIZE], FlowWireFrame9 *frame_out) {
    if (!in || !frame_out) return 0; memcpy(frame_out, in, FLOW_WIRE_FRAME9_SIZE); return 1; }
static inline int flow_wire_frame9_pack_antibody(uint64_t content_hash, uint8_t out[FLOW_WIRE_FRAME9_SIZE]) { if (!out) return 0;
    FlowWireFrame9 f = { .opcode = FLOW_WIRE_OP_ANTIBODY }; for (int i = 0; i < 8; ++i) f.payload.antibody.hash_bytes[i] = (uint8_t)((content_hash >> (56 - i * 8)) & 0xFF);
    return flow_wire_frame9_pack(&f, out); } static inline int flow_wire_frame9_unpack_antibody(const uint8_t in[FLOW_WIRE_FRAME9_SIZE], uint64_t *hash_out) {
    if (!in || !hash_out) return 0; FlowWireFrame9 f; if (!flow_wire_frame9_unpack(in, &f) || f.opcode != FLOW_WIRE_OP_ANTIBODY) return 0; uint64_t h = 0;
    for (int i = 0; i < 8; ++i) h = (h << 8) | f.payload.antibody.hash_bytes[i]; *hash_out = h; return 1; }
static inline int flow_wire_frame9_pack_hetero(uint8_t role, uint8_t node_id, uint16_t bp, uint16_t lat, uint16_t crc, uint8_t out[FLOW_WIRE_FRAME9_SIZE]) {
    if (!out) return 0;
    FlowWireFrame9 f = { .opcode = FLOW_WIRE_OP_HETERO_MESH, .payload.hetero_mesh = { role, node_id, (uint8_t)(bp >> 8), (uint8_t)bp, (uint8_t)(lat >> 8), (uint8_t)lat, (uint8_t)(crc >> 8), (uint8_t)crc } };
    return flow_wire_frame9_pack(&f, out); }
static inline int flow_wire_frame9_unpack_hetero(const uint8_t in[FLOW_WIRE_FRAME9_SIZE], uint8_t *role_out, uint8_t *node_id_out, uint16_t *bp_out, uint16_t *lat_out, uint16_t *crc_out) {
    if (!in) return 0; FlowWireFrame9 f; if (!flow_wire_frame9_unpack(in, &f) || f.opcode != FLOW_WIRE_OP_HETERO_MESH) return 0;
    if (role_out) *role_out = f.payload.hetero_mesh.role; if (node_id_out) *node_id_out = f.payload.hetero_mesh.node_id;
    if (bp_out) *bp_out = ((uint16_t)f.payload.hetero_mesh.bp_hi << 8) | f.payload.hetero_mesh.bp_lo;
    if (lat_out) *lat_out = ((uint16_t)f.payload.hetero_mesh.lat_hi << 8) | f.payload.hetero_mesh.lat_lo;
    if (crc_out) *crc_out = ((uint16_t)f.payload.hetero_mesh.crc_hi << 8) | f.payload.hetero_mesh.crc_lo; return 1; }
static inline int flow_wire_frame9_pack_fleet(uint8_t agent_id, uint8_t role, int16_t x_mm, int16_t y_mm, uint8_t out[FLOW_WIRE_FRAME9_SIZE]) { if (!out) return 0;
    uint16_t ux = (uint16_t)x_mm, uy = (uint16_t)y_mm;
    FlowWireFrame9 f = { .opcode = FLOW_WIRE_OP_FLEET_SYNC, .payload.fleet = { agent_id, role, (uint8_t)(ux >> 8), (uint8_t)ux, (uint8_t)(uy >> 8), (uint8_t)uy, 0, 0 } };
    uint16_t crc = flow_wire_crc16((const uint8_t *)&f, 7); f.payload.fleet.crc_hi = (uint8_t)(crc >> 8); f.payload.fleet.crc_lo = (uint8_t)crc;
    return flow_wire_frame9_pack(&f, out); }
static inline int flow_wire_frame9_unpack_fleet(const uint8_t in[FLOW_WIRE_FRAME9_SIZE], uint8_t *agent_id_out, uint8_t *role_out, int16_t *x_mm_out, int16_t *y_mm_out) {
    if (!in) return 0; FlowWireFrame9 f; if (!flow_wire_frame9_unpack(in, &f) || f.opcode != FLOW_WIRE_OP_FLEET_SYNC) return 0;
    if ((((uint16_t)f.payload.fleet.crc_hi << 8) | f.payload.fleet.crc_lo) != flow_wire_crc16(in, 7)) return 0; if (agent_id_out) *agent_id_out = f.payload.fleet.agent_id;
    if (role_out) *role_out = f.payload.fleet.role; if (x_mm_out) *x_mm_out = (int16_t)(((uint16_t)f.payload.fleet.x_hi << 8) | f.payload.fleet.x_lo);
    if (y_mm_out) *y_mm_out = (int16_t)(((uint16_t)f.payload.fleet.y_hi << 8) | f.payload.fleet.y_lo); return 1; }
#ifdef __cplusplus
}
#endif
#endif /* FLOW_WIRE_FRAME_H */
