#ifndef FLOW_WIRE_FRAME_H
#define FLOW_WIRE_FRAME_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
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
static inline int flow_wire_frame9_pack(const FlowWireFrame9 *frame, uint8_t out[FLOW_WIRE_FRAME9_SIZE]) {
    return (frame && out) ? (memcpy(out, frame, FLOW_WIRE_FRAME9_SIZE), 1) : 0; }
static inline int flow_wire_frame9_unpack(const uint8_t in[FLOW_WIRE_FRAME9_SIZE], FlowWireFrame9 *frame_out) {
    return (in && frame_out) ? (memcpy(frame_out, in, FLOW_WIRE_FRAME9_SIZE), 1) : 0; }
#endif /* FLOW_WIRE_FRAME_H */
