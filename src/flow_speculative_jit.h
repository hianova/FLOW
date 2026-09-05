#ifndef FLOW_SPECULATIVE_JIT_H
#define FLOW_SPECULATIVE_JIT_H

#include "flow_jet.h"
#include "jit.h"
#include "reload.h"
#include "smt.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW Speculative JIT Native Machine Code Hot-Swap (flow_speculative_jit.h)
 * ============================================================================
 * Paradigm:
 * From "Hardware Cache Prefetching" to "Speculative Native Machine Code Synthesis".
 *
 * When the Koopman linear transfer operator predicts that the phase trajectory
 * will cross a Moreau convex boundary / phase transition threshold within lookahead
 * horizon \tau (e.g. 1~10 \mu s), the background JIT pool synthesizes and optimizes
 * target machine code into dual-mapped zero-TLB heap pages.
 *
 * Upon actual crossing of the critical boundary, an atomic pointer swap occurs
 * with 0ms compilation latency and 0ns TLB shootdowns ("Negative Latency" hot swap).
 * ============================================================================
 */

typedef struct {
    FlowJet *jet;                           /* Monitored phase space trajectory */
    FlowAsyncJITPool *jit_pool;             /* Background non-blocking compilation engine */
    FlowReloadContext *reload_ctx;          /* Hot-swap QSBR generation manager */
    double lookahead_time_ns;               /* Extrapolation lookahead window (e.g. 5000 ns) */
    double moreau_boundary_threshold;       /* Critical coordinate boundary q_crit */
    uint32_t monitored_dim;                 /* Coordinate index triggering phase transition */
    int is_compilation_dispatched;          /* 1 if speculative background compilation is running */
    int is_compilation_ready;               /* 1 if specialized machine code is pre-assembled */
    int is_hot_swapped;                     /* 1 if generation swap has been committed */
    uint64_t target_generation;             /* Projected generation counter */
    uint64_t total_lookahead_evals;         /* Evaluation counter */
    uint64_t total_speculative_dispatches;  /* Background compilation dispatches */
    uint64_t total_negative_latency_swaps;  /* Zero-stall hot-swaps completed */
    double predicted_crossing_time_ns;      /* Estimated nanoseconds until boundary impact */
    char unit_name_buf[64];                 /* Buffer for pre-staged unit name */
    FlowUnit pre_staged_unit;               /* Pre-compiled unit in dual-mapped memory */
} FlowSpeculativeJIT;

/* Initialize speculative JIT engine */
int flow_speculative_jit_init(FlowSpeculativeJIT *sjit,
                             FlowJet *jet,
                             FlowAsyncJITPool *jit_pool,
                             FlowReloadContext *reload_ctx,
                             double lookahead_time_ns,
                             double moreau_boundary_threshold,
                             uint32_t monitored_dim);

/* Predict trajectory forward using Koopman/symplectic projection; dispatch JIT compilation if crossing is imminent */
int flow_speculative_jit_evaluate(FlowSpeculativeJIT *sjit, double dt_sec);

/* Execute zero-downtime atomic generation swap when the actual trajectory hits the boundary */
int flow_speculative_jit_commit_swap(FlowSpeculativeJIT *sjit);

/* Formal SMT Supreme Court verification of Speculative JIT safety & non-blocking invariants */
FlowSMTResult flow_speculative_jit_verify_smt(const FlowSpeculativeJIT *sjit,
                                              FlowSMTProofAttestation *proof_out);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_SPECULATIVE_JIT_H */
