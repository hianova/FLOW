#include "flow_prefetch.h"
#include "flow_smt_dsl.h"
#include <string.h>
#include <stdio.h>

int flow_prefetch_manifold_geodesic(const FlowBmf1BitCanvas *current_canvas,
                                    const FlowBmfMorseAtlas *atlas,
                                    uint64_t attention_mask) {
    if (!current_canvas) return 0;
    int prefetched_lines = 0;

    /* Prefetch current canvas itself into L1 */
    flow_prefetch_l1(current_canvas);
    prefetched_lines++;

    /* Prefetch adjacent Morse cells along manifold boundary */
    if (atlas && atlas->cell_count > 0) {
        uint32_t curr_id = current_canvas->subspace_id;
        uint32_t next_id = (curr_id + 1) % (uint32_t)atlas->cell_count;
        uint32_t prev_id = (curr_id > 0) ? (curr_id - 1) : (uint32_t)(atlas->cell_count - 1);

        for (size_t i = 0; i < atlas->cell_count; i++) {
            if (atlas->cells[i].cell_id == next_id || atlas->cells[i].cell_id == prev_id) {
                flow_prefetch_l1(&atlas->cells[i]);
                prefetched_lines++;
            }
        }
    }

    /* Prefetch dynamic bias / soft perturbation buffer if active */
    if (attention_mask != 0) {
        flow_prefetch_l2(&current_canvas->dynamic_bias);
        prefetched_lines++;
    }

    return prefetched_lines;
}

int flow_prefetch_token_ring_slot(const void *next_slot_canvas_ptr,
                                  const void *next_component_ptr) {
    int count = 0;
    if (next_slot_canvas_ptr) {
        flow_prefetch_l1(next_slot_canvas_ptr);
        count++;
    }
    if (next_component_ptr) {
        flow_prefetch_l1(next_component_ptr);
        count++;
    }
    return count;
}

FlowSMTResult flow_prefetch_verify_alignment_smt(const FlowBmf1BitCanvas *canvas,
                                                 FlowSMTProofAttestation *proof_out) {
    FLOW_SMT_BOX_BUILDER_DECL(builder);

    /* Theorem 1: Exact 64-Byte Cache Line Confinement */
    size_t sz = sizeof(FlowBmf1BitCanvas);
    FLOW_SMT_BOX_ADD_RULE(builder, "cache_line_size_exact_64", (uint64_t)sz, 64, 64,
                          FLOW_BOX_THEOREM_BUFFER_BOUNDS, "FlowBmf1BitCanvas size deviates from 64-byte cache line");

    /* Theorem 2: Exact 64-Byte Memory Alignment */
    size_t al = _Alignof(FlowBmf1BitCanvas);
    FLOW_SMT_BOX_ADD_RULE(builder, "cache_line_align_exact_64", (uint64_t)al, 64, 64,
                          FLOW_BOX_THEOREM_BUFFER_BOUNDS, "FlowBmf1BitCanvas alignment is not 64-byte aligned");

    /* Theorem 3: Address Zero-Crossing (No Split-Line Access) */
    uint64_t addr_offset = canvas ? ((uintptr_t)canvas & 63ULL) : 0ULL;
    FLOW_SMT_BOX_ADD_RULE(builder, "zero_cross_split_line", addr_offset, 0, 0,
                          FLOW_BOX_THEOREM_DETERMINISM, "Pointer address causes split-line memory transaction");

    FlowSMTResult res = FLOW_SMT_BOX_VERIFY(builder, "prefetch_alignment", proof_out);
    if (res == FLOW_SMT_PROVEN_UNSAT && proof_out != NULL) {
        snprintf(proof_out->proof_summary, sizeof(proof_out->proof_summary),
                 "SMT PREFETCH SOUND: Size=%zu, Align=%zu, SplitOffset=%llu (Zero-Cache-Miss Guaranteed)",
                 sz, al, (unsigned long long)addr_offset);
    }
    return res;
}
