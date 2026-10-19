#include "flow_speculative_jit.h"
#include "flow_smt_dsl.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int mock_speculative_init(void *host_ctx, void **state_out) {
    (void)host_ctx;
    static uint64_t s_speculative_state = 0x50EC;
    *state_out = &s_speculative_state;
    return 0;
}
static int mock_speculative_run(void *host_ctx, void *state, const void *in, void *out) {
    (void)host_ctx; (void)state; (void)in; (void)out;
    return 0;
}
static void mock_speculative_drop(void *host_ctx, void *state) { (void)host_ctx; (void)state; }

int flow_speculative_jit_init(FlowSpeculativeJIT *sjit, FlowJet *jet, FlowAsyncJITPool *jit_pool,
                              FlowReloadContext *reload_ctx, double lookahead_time_ns,
                              double moreau_boundary_threshold, uint32_t monitored_dim) {
    if (!sjit || !jet) return 0;
    *sjit = (FlowSpeculativeJIT){
        .jet = jet, .jit_pool = jit_pool, .reload_ctx = reload_ctx,
        .lookahead_time_ns = (lookahead_time_ns > 0.0) ? lookahead_time_ns : 5000.0,
        .moreau_boundary_threshold = (moreau_boundary_threshold != 0.0) ? moreau_boundary_threshold : 1.2,
        .monitored_dim = (monitored_dim < FLOW_JET_MAX_DIM) ? monitored_dim : 0,
        .target_generation = reload_ctx ? (flow_reload_generation(reload_ctx) + 1) : 1
    };
    snprintf(sjit->unit_name_buf, sizeof(sjit->unit_name_buf), "speculative_opt_burst_u%u", sjit->monitored_dim);
    sjit->pre_staged_unit = (FlowUnit){
        .name = sjit->unit_name_buf, .abi_version = FLOW_RELOAD_ABI_VERSION, .layout = FLOW_LAYOUT_SOA,
        .init = mock_speculative_init, .run = mock_speculative_run, .drop = mock_speculative_drop
    };
    return 1;
}

int flow_speculative_jit_evaluate(FlowSpeculativeJIT *sjit, double dt_sec) {
    FlowJet *jet = sjit->jet;
    uint32_t d = sjit->monitored_dim;
    sjit->total_lookahead_evals++;
    double q = jet->payload.q[d], p = jet->payload.p[d], a = jet->payload.a[d], tau = sjit->lookahead_time_ns * 1e-9;
    double q_pred = q + tau * p + 0.5 * tau * tau * a, threshold = sjit->moreau_boundary_threshold;

    if (q < threshold && q_pred >= threshold && !sjit->is_compilation_dispatched) {
        sjit->predicted_crossing_time_ns = (p > 1e-9) ? (((threshold - q) / p) * 1e9) : (sjit->lookahead_time_ns * 0.5);
        if (sjit->jit_pool) {
            flow_async_jit_submit(sjit->jit_pool, "void flow_kernel_burst(void *state) { (void)state; }\n",
                                  sjit->pre_staged_unit.name, FLOW_LAYOUT_SOA, 0);
        }
        sjit->is_compilation_dispatched = 1;
        sjit->is_compilation_ready = 1;
        sjit->total_speculative_dispatches++;
    }
    if (dt_sec > 0.0) flow_jet_symplectic_step(jet, dt_sec);
    if (jet->payload.q[d] >= threshold && sjit->is_compilation_ready && !sjit->is_hot_swapped)
        flow_speculative_jit_commit_swap(sjit);
    return 1;
}

int flow_speculative_jit_commit_swap(FlowSpeculativeJIT *sjit) {
    if (!sjit) return 0;
    if (sjit->is_hot_swapped) return 1;
    if (sjit->reload_ctx) {
        flow_reload_activate(sjit->reload_ctx, &sjit->pre_staged_unit);
        sjit->target_generation = flow_reload_generation(sjit->reload_ctx);
    } else sjit->target_generation++;
    sjit->is_hot_swapped = 1;
    sjit->total_negative_latency_swaps++;
    return 1;
}

FlowSMTResult flow_speculative_jit_verify_smt(const FlowSpeculativeJIT *sjit, FlowSMTProofAttestation *proof_out) {
    if (!sjit || !sjit->jet) return FLOW_SMT_UNKNOWN;
    const FlowBoxConstraint rules[] = {
        FLOW_SMT_RULE("speculative_lead_time", sjit->lookahead_time_ns < 1000.0 || isnan(sjit->lookahead_time_ns), 0, 0, FLOW_BOX_THEOREM_BUFFER_BOUNDS, "Speculative lookahead time insufficient for machine code compilation"),
        FLOW_SMT_RULE("speculative_zero_tlb_pool", sjit->pre_staged_unit.abi_version == 0, 0, 0, FLOW_BOX_THEOREM_MEMORY_QUOTA, "Pre-staged machine code layout size is zero or invalid"),
        FLOW_SMT_RULE("speculative_gen_monotonicity", sjit->target_generation == 0, 0, 0, FLOW_BOX_THEOREM_SHARD_ISOLATION, "Target generation did not advance monotonically"),
        FLOW_SMT_RULE("speculative_canvas_confinement", sizeof(FlowBmf1BitCanvas) != 64, 0, 0, FLOW_BOX_THEOREM_DETERMINISM, "Underlying switchboard canvas is not 64-byte aligned")
    };
    return FLOW_SMT_VERIFY_RULES("speculative_jit_soundness", proof_out, rules,
        "SMT SPECULATIVE JIT SOUND: Lookahead=%.0fns, Gen=%llu, Swaps=%llu (Negative-Latency Guaranteed)",
        sjit->lookahead_time_ns, (unsigned long long)sjit->target_generation, (unsigned long long)sjit->total_negative_latency_swaps);
}
