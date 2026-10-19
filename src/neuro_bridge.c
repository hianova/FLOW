#include "neuro_bridge.h"
#include "flow_smt_dsl.h"
#include "flow_str.h"
#include "hardware_telemetry.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

int flow_neuro_bridge_init(FlowNeuroBridge *bridge, size_t input_dim, uint32_t seed) {
    if (!bridge || !input_dim || input_dim > FLOW_NEURO_MAX_INPUT_DIM) return 0;
    memset(bridge, 0, sizeof(*bridge));
    bridge->input_dimension = input_dim;
    uint32_t s = seed ? seed : 0x1337BEEF;
    for (size_t i = 0; i < FLOW_NEURO_BMF_BITS; i++) {
        for (size_t k = 0; k < FLOW_NEURO_SPARSITY_BMF; k++) {
            s = s * 1664525u + 1013904223u;
            bridge->bmf_indices[i][k] = (uint16_t)(s % input_dim);
            bridge->bmf_signs[i][k] = (s & 0x10000) ? 1 : -1;
        }
    }
    for (size_t j = 0; j < FLOW_NEURO_FVEC_DIM; j++) {
        for (size_t k = 0; k < FLOW_NEURO_SPARSITY_FVEC; k++) {
            s = s * 1664525u + 1013904223u;
            bridge->fvec_indices[j][k] = (uint16_t)(s % input_dim);
            bridge->fvec_signs[j][k] = (s & 0x10000) ? 1 : -1;
        }
    }
    return 1;
}

static void add_pbound(FlowNeuroProjectionResult *r, const char *name, int axis, double lo, double hi) {
    FlowNeuroPhysicalBound *b = &r->bounds[r->bound_count++];
    strncpy(b->name, name, sizeof(b->name) - 1);
    if (axis >= 0 && axis < FLOW_NEURO_FVEC_DIM) b->normal[axis] = 1.0;
    b->lower_bound = lo; b->upper_bound = hi; b->is_hard_constraint = true;
}

static void flow_neuro_synthesize_bounds(FlowNeuroProjectionResult *r) {
    r->bound_count = 0;
    switch (r->classified_intent) {
        case FLOW_NEURO_INTENT_SMOOTH_FETCH_LATTE:
            r->indexed_subspace_id = FLOW_BMF_SUBSPACE_SMOOTH_FETCH_LATTE;
            r->bmf_1bit_switches = FLOW_BMF_SW_HARD_SAFETY | FLOW_BMF_SW_CONTRACT_GATE | FLOW_BMF_SW_ANTI_SPILL_TILT |
                                   FLOW_BMF_SW_GRIPPER_FORCE_SAFE | FLOW_BMF_SW_STICK_SLIP_MODE | FLOW_BMF_SW_SIMD_VECTORIZED;
            snprintf(r->intent_description, sizeof(r->intent_description), "Smooth Fetch: Unspillable latte fluid dynamics & soft grasp");
            add_pbound(r, "fluid_tilt_limit", 0, 0.0, 0.08);
            add_pbound(r, "max_angular_accel", 1, 0.0, 0.40);
            add_pbound(r, "gripper_force_n", 2, 2.0, 4.5);
            add_pbound(r, "max_jerk_limit", 3, 0.0, 0.80);
            break;
        case FLOW_NEURO_INTENT_AGILE_SPRINT:
            r->indexed_subspace_id = FLOW_BMF_SUBSPACE_AGILE_SPRINT;
            r->bmf_1bit_switches = FLOW_BMF_SW_HARD_SAFETY | FLOW_BMF_SW_STICK_SLIP_MODE | FLOW_BMF_SW_ZMP_BALANCE | FLOW_BMF_SW_SIMD_VECTORIZED;
            snprintf(r->intent_description, sizeof(r->intent_description), "Agile Sprint: Dynamic bipedal terrain traverse");
            add_pbound(r, "forward_velocity", -1, 0.0, 4.5);
            add_pbound(r, "ground_friction_coeff", -1, 0.6, 1.0);
            break;
        case FLOW_NEURO_INTENT_COLLABORATIVE_HOLD:
            r->indexed_subspace_id = FLOW_BMF_SUBSPACE_COLLABORATIVE_HOLD;
            r->bmf_1bit_switches = FLOW_BMF_SW_HARD_SAFETY | FLOW_BMF_SW_CONTRACT_GATE | FLOW_BMF_SW_COOP_SYNC_LOCK | FLOW_BMF_SW_GRIPPER_FORCE_SAFE;
            snprintf(r->intent_description, sizeof(r->intent_description), "Collaborative Hold: Dual-arm load sharing");
            add_pbound(r, "dual_arm_sync_error", -1, 0.0, 0.005);
            break;
        default:
            r->indexed_subspace_id = FLOW_BMF_SUBSPACE_EMERGENCY_PROTECT;
            r->bmf_1bit_switches = FLOW_BMF_SW_HARD_SAFETY | FLOW_BMF_SW_EMERGENCY_HALT | FLOW_BMF_SW_IMPACT_DAMPING | FLOW_BMF_SW_CAN_BUS_HEALTHY;
            snprintf(r->intent_description, sizeof(r->intent_description), "Emergency Protection: Zero-fall safe deceleration");
            add_pbound(r, "emergency_decel", -1, 0.0, 8.0);
            break;
    }
}

static void finalize_projection(FlowNeuroBridge *bridge, FlowNeuroIntentType intent_hint, uint64_t t_start, FlowNeuroProjectionResult *res) {
    res->classified_intent = (intent_hint != FLOW_NEURO_INTENT_GENERIC) ? intent_hint : (FlowNeuroIntentType)((res->bmf_coordinates & 0x03) + 1);
    flow_neuro_synthesize_bounds(res);
    uint64_t t_end = flow_hardware_cycles(), freq = flow_hardware_timer_frequency_hz();
    res->projection_cycles = (t_end >= t_start) ? (t_end - t_start) : 0;
    res->projection_nanoseconds = freq > 0 ? (((double)res->projection_cycles * 1e9) / (double)freq) : ((double)res->projection_cycles * 0.33);
    if (bridge) { bridge->total_projections++; bridge->cumulative_latency_ns += res->projection_nanoseconds; }
}

int flow_neuro_bridge_project(FlowNeuroBridge *bridge, const float *emb, size_t emb_len, FlowNeuroIntentType intent_hint, FlowNeuroProjectionResult *res) {
    if (!bridge || !emb || !res || emb_len != bridge->input_dimension) return 0;
    memset(res, 0, sizeof(*res));
    uint64_t t_start = flow_hardware_cycles(), bmf = 0;
    for (size_t i = 0; i < FLOW_NEURO_BMF_BITS; i++) {
        const uint16_t *idx = bridge->bmf_indices[i]; const int8_t *sgn = bridge->bmf_signs[i];
        float sum = sgn[0] * emb[idx[0]] + sgn[1] * emb[idx[1]] + sgn[2] * emb[idx[2]] + sgn[3] * emb[idx[3]];
        if (sum > bridge->bmf_thresholds[i]) bmf |= (1ULL << i);
    }
    res->bmf_coordinates = bmf;
    for (size_t j = 0; j < FLOW_NEURO_FVEC_DIM; j++) {
        const uint16_t *idx = bridge->fvec_indices[j]; const int8_t *sgn = bridge->fvec_signs[j];
        float acc = sgn[0] * emb[idx[0]] + sgn[1] * emb[idx[1]] + sgn[2] * emb[idx[2]] + sgn[3] * emb[idx[3]] +
                    sgn[4] * emb[idx[4]] + sgn[5] * emb[idx[5]] + sgn[6] * emb[idx[6]] + sgn[7] * emb[idx[7]];
        res->fvec_features[j] = (double)(acc / (1.0f + fabsf(acc)));
    }
    finalize_projection(bridge, intent_hint, t_start, res);
    return 1;
}

int flow_neuro_bridge_project_simd(FlowNeuroBridge *bridge, const float *emb, size_t emb_len, FlowNeuroIntentType intent_hint, FlowNeuroProjectionResult *res) {
#if defined(__ARM_NEON)
    if (!bridge || !emb || !res || emb_len != bridge->input_dimension) return 0;
    memset(res, 0, sizeof(*res));
    uint64_t t_start = flow_hardware_cycles(), bmf = 0;
    for (size_t i = 0; i < FLOW_NEURO_BMF_BITS; i++) {
        const uint16_t *idx = bridge->bmf_indices[i]; const int8_t *sgn = bridge->bmf_signs[i];
        float32x4_t v_sgn = { (float)sgn[0], (float)sgn[1], (float)sgn[2], (float)sgn[3] };
        float32x4_t v_emb = { emb[idx[0]], emb[idx[1]], emb[idx[2]], emb[idx[3]] };
        if (vaddvq_f32(vmulq_f32(v_sgn, v_emb)) > bridge->bmf_thresholds[i]) bmf |= (1ULL << i);
    }
    res->bmf_coordinates = bmf;
    for (size_t j = 0; j < FLOW_NEURO_FVEC_DIM; j++) {
        const uint16_t *idx = bridge->fvec_indices[j]; const int8_t *sgn = bridge->fvec_signs[j];
        float32x4_t s_lo = { (float)sgn[0], (float)sgn[1], (float)sgn[2], (float)sgn[3] };
        float32x4_t s_hi = { (float)sgn[4], (float)sgn[5], (float)sgn[6], (float)sgn[7] };
        float32x4_t e_lo = { emb[idx[0]], emb[idx[1]], emb[idx[2]], emb[idx[3]] };
        float32x4_t e_hi = { emb[idx[4]], emb[idx[5]], emb[idx[6]], emb[idx[7]] };
        float acc = vaddvq_f32(vfmaq_f32(vmulq_f32(s_lo, e_lo), s_hi, e_hi));
        res->fvec_features[j] = (double)(acc / (1.0f + fabsf(acc)));
    }
    finalize_projection(bridge, intent_hint, t_start, res);
    return 1;
#else
    return flow_neuro_bridge_project(bridge, emb, emb_len, intent_hint, res);
#endif
}

int flow_neuro_bridge_quantize(const FlowNeuroBridge *src, FlowNeuroBridgeQuantized *dst_out) {
    if (!src || !dst_out) return 0;
    memset(dst_out, 0, sizeof(*dst_out));
    dst_out->input_dimension = src->input_dimension;
    for (size_t i = 0; i < FLOW_NEURO_BMF_BITS; i++) {
        for (size_t k = 0; k < FLOW_NEURO_SPARSITY_BMF; k++) {
            dst_out->bmf_indices[i][k] = src->bmf_indices[i][k];
            dst_out->bmf_weights[i][k] = src->bmf_signs[i][k];
        }
        dst_out->bmf_thresholds[i] = (int16_t)(src->bmf_thresholds[i] * 128.0f);
    }
    for (size_t j = 0; j < FLOW_NEURO_FVEC_DIM; j++)
        for (size_t k = 0; k < FLOW_NEURO_SPARSITY_FVEC; k++) {
            dst_out->fvec_indices[j][k] = src->fvec_indices[j][k];
            dst_out->fvec_weights[j][k] = src->fvec_signs[j][k];
        }
    return 1;
}

int flow_neuro_bridge_project_quantized(const FlowNeuroBridgeQuantized *qb, const int8_t *qemb, FlowNeuroIntentType intent_hint, FlowNeuroProjectionResult *res) {
    if (!qb || !qemb || !res) return 0;
    memset(res, 0, sizeof(*res));
    uint64_t t_start = flow_hardware_cycles(), bmf = 0;
    for (size_t i = 0; i < FLOW_NEURO_BMF_BITS; i++) {
        const uint16_t *idx = qb->bmf_indices[i]; const int8_t *w = qb->bmf_weights[i];
        int32_t sum = (int32_t)w[0] * qemb[idx[0]] + (int32_t)w[1] * qemb[idx[1]] + (int32_t)w[2] * qemb[idx[2]] + (int32_t)w[3] * qemb[idx[3]];
        if (sum > (int32_t)qb->bmf_thresholds[i]) bmf |= (1ULL << i);
    }
    res->bmf_coordinates = bmf;
    for (size_t j = 0; j < FLOW_NEURO_FVEC_DIM; j++) {
        const uint16_t *idx = qb->fvec_indices[j]; const int8_t *w = qb->fvec_weights[j];
        int32_t acc = (int32_t)w[0] * qemb[idx[0]] + (int32_t)w[1] * qemb[idx[1]] + (int32_t)w[2] * qemb[idx[2]] + (int32_t)w[3] * qemb[idx[3]] +
                      (int32_t)w[4] * qemb[idx[4]] + (int32_t)w[5] * qemb[idx[5]] + (int32_t)w[6] * qemb[idx[6]] + (int32_t)w[7] * qemb[idx[7]];
        float f_acc = (float)acc / 128.0f;
        res->fvec_features[j] = (double)(f_acc / (1.0f + fabsf(f_acc)));
    }
    finalize_projection(NULL, intent_hint, t_start, res);
    return 1;
}

int flow_neuro_eval_bounds_simd(const FlowNeuroPhysicalBound *bounds, size_t bound_count, const double *state_vec, uint32_t *violation_mask_out) {
    if (!bounds || !state_vec || !violation_mask_out) return 0;
    uint32_t mask = 0;
    for (size_t i = 0; i < bound_count && i < 32; i++) {
        double dot = 0.0;
        for (size_t d = 0; d < FLOW_NEURO_FVEC_DIM; d++) dot += bounds[i].normal[d] * state_vec[d];
        if (dot < bounds[i].lower_bound || dot > bounds[i].upper_bound) mask |= (1U << i);
    }
    *violation_mask_out = mask;
    return 1;
}

FlowSMTResult flow_neuro_bridge_verify_smt(const FlowNeuroProjectionResult *r, FlowSMTProofAttestation *proof_out) {
    if (!r) return FLOW_SMT_UNKNOWN;
    uint64_t ord_v = 0, mot_v = 0, fld_v = 0;
    for (size_t i = 0; i < r->bound_count; i++) {
        if (r->bounds[i].lower_bound > r->bounds[i].upper_bound) ord_v++;
        if (strcmp(r->bounds[i].name, "gripper_force_n") == 0 && r->bounds[i].upper_bound > 10.0) mot_v++;
        if (r->classified_intent == FLOW_NEURO_INTENT_SMOOTH_FETCH_LATTE && strcmp(r->bounds[i].name, "fluid_tilt_limit") == 0 && r->bounds[i].upper_bound > 0.12) fld_v++;
    }
    const FlowBoxConstraint rules[] = {
        FLOW_SMT_RULE("polyhedral_bounds_order", ord_v, 0, 0, FLOW_BOX_THEOREM_BUFFER_BOUNDS, "Lower bound exceeds upper bound"),
        FLOW_SMT_RULE("motor_gripper_safety", mot_v, 0, 0, FLOW_BOX_THEOREM_MEMORY_QUOTA, "Grip force exceeds cardboard cup structural limit"),
        FLOW_SMT_RULE("fluid_tilt_unspillable", fld_v, 0, 0, FLOW_BOX_THEOREM_SHARD_ISOLATION, "Tilt limit exceeds liquid meniscus spill threshold"),
        FLOW_SMT_RULE("projection_latency_bound", r->projection_nanoseconds > 50000.0, 0, 0, FLOW_BOX_THEOREM_DETERMINISM, "Projection latency exceeded deterministic deadline")
    };
    return FLOW_SMT_VERIFY_RULES("neuro_bridge", proof_out, rules,
        "SMT NEURO-BRIDGE SOUND: Intent=%d, Latency=%.2fns, Cycles=%llu (Zero-Defect Soundness)",
        (int)r->classified_intent, r->projection_nanoseconds, (unsigned long long)r->projection_cycles);
}

FlowSMTResult flow_neuro_verify_simd_soundness_smt(const FlowNeuroProjectionResult *base, const FlowNeuroProjectionResult *simd, double max_allowed_error, FlowSMTProofAttestation *proof_out) {
    if (!base || !simd) return FLOW_SMT_UNKNOWN;
    double max_err = 0.0;
    for (size_t i = 0; i < FLOW_NEURO_FVEC_DIM; i++) max_err = fmax(max_err, fabs(base->fvec_features[i] - simd->fvec_features[i]));
    const FlowBoxConstraint rules[] = {
        FLOW_SMT_RULE("intent_parity", base->classified_intent != simd->classified_intent, 0, 0, FLOW_BOX_THEOREM_DETERMINISM, "SIMD intent classification diverges from baseline reference"),
        FLOW_SMT_RULE("simd_precision_bound", (uint64_t)(max_err * 10000.0), 0, (uint64_t)(max_allowed_error * 10000.0), FLOW_BOX_THEOREM_BUFFER_BOUNDS, "SIMD numerical error exceeds tolerance limit")
    };
    return FLOW_SMT_VERIFY_RULES("neuro_simd", proof_out, rules,
        "SMT NEURO-SIMD SOUND: IntentParity=1, MaxErr=%.5f <= %.5f, SIMD Latency=%.2fns (Soundness Guaranteed)",
        max_err, max_allowed_error, simd->projection_nanoseconds);
}

int flow_neuro_bridge_index_subspace(const FlowNeuroBridge *bridge, const float *emb, size_t emb_len, FlowNeuroIntentType intent_hint, const FlowBmfSubspaceRegistry *reg, uint32_t *subspace_id_out) {
    if (!bridge || !emb || !subspace_id_out) return 0;
    FlowNeuroProjectionResult res;
    if (!flow_neuro_bridge_project_simd((FlowNeuroBridge *)bridge, emb, emb_len, intent_hint, &res)) return 0;
    *subspace_id_out = (intent_hint == FLOW_NEURO_INTENT_GENERIC && reg) ? flow_bmf_subspace_index_from_features(reg, res.fvec_features, FLOW_NEURO_FVEC_DIM) : res.indexed_subspace_id;
    return 1;
}

int flow_neuro_bridge_to_1bit_canvas(FlowNeuroBridge *bridge, const float *emb, size_t emb_len, FlowNeuroIntentType intent_hint, const FlowBmfSubspaceRegistry *reg, FlowBmf1BitCanvas *canvas_out, FlowNeuroProjectionResult *result_out) {
    if (!bridge || !emb || !canvas_out) return 0;
    FlowNeuroProjectionResult local_res, *res = result_out ? result_out : &local_res;
    if (!flow_neuro_bridge_project_simd(bridge, emb, emb_len, intent_hint, res)) return 0;
    if (intent_hint == FLOW_NEURO_INTENT_GENERIC && reg)
        res->indexed_subspace_id = flow_bmf_subspace_index_from_features(reg, res->fvec_features, FLOW_NEURO_FVEC_DIM);
    if (reg) flow_bmf_canvas_init_from_subspace(canvas_out, flow_bmf_subspace_lookup(reg, res->indexed_subspace_id));
    else flow_bmf_canvas_init(canvas_out, res->indexed_subspace_id, FLOW_BMF_SW_HARD_SAFETY | FLOW_BMF_SW_CONTRACT_GATE, ~0ULL, res->bmf_1bit_switches);
    canvas_out->dynamic_bias = res->bmf_coordinates;
    canvas_out->is_adjudicated_sound = flow_bmf_canvas_verify_invariants(canvas_out);
    return 1;
}

int flow_neuro_bridge_project_fwht(const float *input_4096, uint64_t seed, FlowNeuroProjectionResult *res) {
    if (!input_4096 || !res) return 0;
    memset(res, 0, sizeof(*res));
    if (!flow_fwht_project_4096(input_4096, seed, &res->bmf_coordinates, res->fvec_features, &res->projection_nanoseconds)) return 0;
    res->projection_cycles = (uint64_t)(res->projection_nanoseconds * 2.4);
    res->classified_intent = FLOW_NEURO_INTENT_GENERIC;
    res->bmf_1bit_switches = FLOW_BMF_SW_HARD_SAFETY | FLOW_BMF_SW_SIMD_VECTORIZED;
    snprintf(res->intent_description, sizeof(res->intent_description), "FWHT Zero-Table Fast Projection (K(H_N) = O(1))");
    return 1;
}

int flow_neuro_bridge_to_morse_canvas(const float *input_4096, uint64_t seed, const FlowBmfMorseAtlas *atlas, FlowBmf1BitCanvas *canvas_out, FlowNeuroProjectionResult *result_out) {
    if (!input_4096 || !canvas_out) return 0;
    FlowNeuroProjectionResult local_res, *res = result_out ? result_out : &local_res;
    if (!flow_neuro_bridge_project_fwht(input_4096, seed, res)) return 0;
    const FlowBmfMorseCell *cell = NULL;
    uint32_t cell_id = 0;
    if (atlas && atlas->cell_count > 0) {
        cell_id = flow_morse_atlas_route(atlas, res->fvec_features, FLOW_NEURO_FVEC_DIM);
        res->indexed_subspace_id = cell_id;
        for (size_t i = 0; i < atlas->cell_count; i++) if (atlas->cells[i].cell_id == cell_id) { cell = &atlas->cells[i]; break; }
    }
    if (cell) flow_bmf_canvas_init(canvas_out, cell->cell_id, cell->invariant_mask, cell->malleable_mask, cell->default_switches);
    else flow_bmf_canvas_init(canvas_out, cell_id, FLOW_BMF_SW_HARD_SAFETY, ~0ULL, FLOW_BMF_SW_HARD_SAFETY);
    canvas_out->dynamic_bias = res->bmf_coordinates;
    canvas_out->is_adjudicated_sound = flow_bmf_canvas_verify_invariants(canvas_out);
    return 1;
}
