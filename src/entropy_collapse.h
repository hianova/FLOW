#ifndef FLOW_ENTROPY_COLLAPSE_H
#define FLOW_ENTROPY_COLLAPSE_H
#include "smt.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct FlowBumpQsbrArena { uint8_t *buffer; size_t capacity, cursor, total_allocs, total_folds; uint64_t generation, bound_wavefront_epoch; } FlowBumpQsbrArena;
int flow_bump_qsbr_init(FlowBumpQsbrArena *arena, void *backing_memory, size_t capacity); void *flow_bump_qsbr_alloc(FlowBumpQsbrArena *arena, size_t size_bytes);
int flow_bump_qsbr_quiescent_fold(FlowBumpQsbrArena *arena); FlowSMTResult flow_bump_qsbr_verify_smt(const FlowBumpQsbrArena *arena, FlowSMTProofAttestation *proof_out);
static inline void flow_bump_qsbr_sync_wavefront(FlowBumpQsbrArena *arena, uint64_t current_wavefront_cycle) {
    if (arena && current_wavefront_cycle > arena->bound_wavefront_epoch) { flow_bump_qsbr_quiescent_fold(arena); arena->bound_wavefront_epoch = current_wavefront_cycle; } }
FlowSMTResult flow_curry_howard_verify_precondition(const void *ptr, size_t len, size_t max_len, FlowSMTProofAttestation *proof_out); typedef struct __attribute__((packed)) {
    uint16_t opcode, sequence; uint32_t payload_len; uint64_t timestamp_ns, session_token; } FlowIsomorphicFrame;
static inline const FlowIsomorphicFrame *flow_isomorphic_slice_wire(const void *wire_bytes) { return (const FlowIsomorphicFrame *)wire_bytes; }
FlowSMTResult flow_isomorphic_verify_smt(const FlowIsomorphicFrame *frame, size_t wire_len, FlowSMTProofAttestation *proof_out); typedef struct {
    uint32_t threads, buffer_size_kb, timeout_ms, converged_epoch; double current_energy; bool is_converged; } FlowAutopoiesisEngine;
int flow_autopoiesis_init(FlowAutopoiesisEngine *eng); int flow_autopoiesis_converge(FlowAutopoiesisEngine *eng, size_t max_iterations);
FlowSMTResult flow_autopoiesis_verify_smt(const FlowAutopoiesisEngine *eng, FlowSMTProofAttestation *proof_out);
#define FLOW_SEM_EVT_BURST_INGRESS    0
#define FLOW_SEM_EVT_TIER_MIGRATED    1
#define FLOW_SEM_EVT_QUIC_SWITCHED    2
#define FLOW_SEM_EVT_OOM_THWARTED     3
#define FLOW_SEM_EVT_SMT_CERTIFIED    4
#define FLOW_SEM_EVT_WARDROP_LOCKED   5
#define FLOW_SEM_EVT_MOREAU_ABSORBED  6
#define FLOW_SEM_EVT_HOLE_UNCOVERED   7
typedef uint64_t FlowSemanticVector; static inline void flow_semantic_emit(FlowSemanticVector *vec, uint8_t event_bit) {
    if (vec && event_bit < 64) *vec |= (1ULL << event_bit); } const char *flow_semantic_resolve_name(uint8_t event_bit);
FlowSMTResult flow_semantic_verify_smt(FlowSemanticVector vec, FlowSMTProofAttestation *proof_out);
typedef void (*FlowGeodesicStageFn)(void *state_buffer, size_t len, size_t stage_idx); typedef struct { size_t stage_count, buffer_len;
    uint64_t total_pipeline_executions, destructors_eliminated; } FlowAffineGeodesic; int flow_geodesic_init(FlowAffineGeodesic *geo, size_t stage_count, size_t buffer_len);
int flow_geodesic_execute(FlowAffineGeodesic *geo, void *state_buffer, FlowGeodesicStageFn *stages);
FlowSMTResult flow_geodesic_verify_smt(const FlowAffineGeodesic *geo, FlowSMTProofAttestation *proof_out);
#ifdef __cplusplus
}
#endif
#endif /* FLOW_ENTROPY_COLLAPSE_H */
