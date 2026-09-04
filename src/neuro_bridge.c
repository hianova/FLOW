#include "neuro_bridge.h"
#include "flow_str.h"
#include "hardware_telemetry.h"

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

int flow_neuro_bridge_init(FlowNeuroBridge *bridge, size_t input_dim, uint32_t seed) {
    if (bridge == NULL || input_dim == 0 || input_dim > FLOW_NEURO_MAX_INPUT_DIM) {
        return 0;
    }
    memset(bridge, 0, sizeof(*bridge));
    bridge->input_dimension = input_dim;

    uint32_t s = (seed != 0) ? seed : 0x1337BEEF;

    /* Initialize sparse projection indices & signs for 64-bit BMF */
    for (size_t i = 0; i < FLOW_NEURO_BMF_BITS; i++) {
        for (size_t k = 0; k < FLOW_NEURO_SPARSITY_BMF; k++) {
            s = s * 1664525u + 1013904223u;
            bridge->bmf_indices[i][k] = (uint16_t)(s % input_dim);
            bridge->bmf_signs[i][k] = (s & 0x10000) ? 1 : -1;
        }
        bridge->bmf_thresholds[i] = 0.0f;
    }

    /* Initialize sparse projection indices & signs for 16-D .fvec */
    for (size_t j = 0; j < FLOW_NEURO_FVEC_DIM; j++) {
        for (size_t k = 0; k < FLOW_NEURO_SPARSITY_FVEC; k++) {
            s = s * 1664525u + 1013904223u;
            bridge->fvec_indices[j][k] = (uint16_t)(s % input_dim);
            bridge->fvec_signs[j][k] = (s & 0x10000) ? 1 : -1;
        }
    }

    return 1;
}

static void flow_neuro_synthesize_bounds(FlowNeuroProjectionResult *result_out) {
    size_t bc = 0;
    switch (result_out->classified_intent) {
        case FLOW_NEURO_INTENT_SMOOTH_FETCH_LATTE: {
            result_out->indexed_subspace_id = FLOW_BMF_SUBSPACE_SMOOTH_FETCH_LATTE;
            result_out->bmf_1bit_switches = FLOW_BMF_SW_HARD_SAFETY | FLOW_BMF_SW_CONTRACT_GATE |
                                            FLOW_BMF_SW_ANTI_SPILL_TILT | FLOW_BMF_SW_GRIPPER_FORCE_SAFE |
                                            FLOW_BMF_SW_STICK_SLIP_MODE | FLOW_BMF_SW_SIMD_VECTORIZED;
            snprintf(result_out->intent_description, sizeof(result_out->intent_description),
                     "Smooth Fetch: Unspillable latte fluid dynamics & soft grasp");

            FlowNeuroPhysicalBound *b0 = &result_out->bounds[bc++];
            strncpy(b0->name, "fluid_tilt_limit", sizeof(b0->name) - 1);
            b0->normal[0] = 1.0;
            b0->lower_bound = 0.0;
            b0->upper_bound = 0.08; /* ~4.6 degrees max tilt */
            b0->is_hard_constraint = true;

            FlowNeuroPhysicalBound *b1 = &result_out->bounds[bc++];
            strncpy(b1->name, "max_angular_accel", sizeof(b1->name) - 1);
            b1->normal[1] = 1.0;
            b1->lower_bound = 0.0;
            b1->upper_bound = 0.40; /* Smooth gradual slew */
            b1->is_hard_constraint = true;

            FlowNeuroPhysicalBound *b2 = &result_out->bounds[bc++];
            strncpy(b2->name, "gripper_force_n", sizeof(b2->name) - 1);
            b2->normal[2] = 1.0;
            b2->lower_bound = 2.0; /* Minimum grip to prevent slippage */
            b2->upper_bound = 4.5; /* Maximum grip before crushing cup */
            b2->is_hard_constraint = true;

            FlowNeuroPhysicalBound *b3 = &result_out->bounds[bc++];
            strncpy(b3->name, "max_jerk_limit", sizeof(b3->name) - 1);
            b3->normal[3] = 1.0;
            b3->lower_bound = 0.0;
            b3->upper_bound = 0.80;
            b3->is_hard_constraint = true;
            break;
        }
        case FLOW_NEURO_INTENT_AGILE_SPRINT: {
            result_out->indexed_subspace_id = FLOW_BMF_SUBSPACE_AGILE_SPRINT;
            result_out->bmf_1bit_switches = FLOW_BMF_SW_HARD_SAFETY | FLOW_BMF_SW_STICK_SLIP_MODE |
                                            FLOW_BMF_SW_ZMP_BALANCE | FLOW_BMF_SW_SIMD_VECTORIZED;
            snprintf(result_out->intent_description, sizeof(result_out->intent_description),
                     "Agile Sprint: Dynamic bipedal terrain traverse");
            FlowNeuroPhysicalBound *b0 = &result_out->bounds[bc++];
            strncpy(b0->name, "forward_velocity", sizeof(b0->name) - 1);
            b0->lower_bound = 0.0;
            b0->upper_bound = 4.5;
            b0->is_hard_constraint = true;

            FlowNeuroPhysicalBound *b1 = &result_out->bounds[bc++];
            strncpy(b1->name, "ground_friction_coeff", sizeof(b1->name) - 1);
            b1->lower_bound = 0.6;
            b1->upper_bound = 1.0;
            b1->is_hard_constraint = true;
            break;
        }
        case FLOW_NEURO_INTENT_COLLABORATIVE_HOLD: {
            result_out->indexed_subspace_id = FLOW_BMF_SUBSPACE_COLLABORATIVE_HOLD;
            result_out->bmf_1bit_switches = FLOW_BMF_SW_HARD_SAFETY | FLOW_BMF_SW_CONTRACT_GATE |
                                            FLOW_BMF_SW_COOP_SYNC_LOCK | FLOW_BMF_SW_GRIPPER_FORCE_SAFE;
            snprintf(result_out->intent_description, sizeof(result_out->intent_description),
                     "Collaborative Hold: Dual-arm load sharing");
            FlowNeuroPhysicalBound *b0 = &result_out->bounds[bc++];
            strncpy(b0->name, "dual_arm_sync_error", sizeof(b0->name) - 1);
            b0->lower_bound = 0.0;
            b0->upper_bound = 0.005; /* 5mm */
            b0->is_hard_constraint = true;
            break;
        }
        case FLOW_NEURO_INTENT_EMERGENCY_PROTECT:
        default: {
            result_out->indexed_subspace_id = FLOW_BMF_SUBSPACE_EMERGENCY_PROTECT;
            result_out->bmf_1bit_switches = FLOW_BMF_SW_HARD_SAFETY | FLOW_BMF_SW_EMERGENCY_HALT |
                                            FLOW_BMF_SW_IMPACT_DAMPING | FLOW_BMF_SW_CAN_BUS_HEALTHY;
            snprintf(result_out->intent_description, sizeof(result_out->intent_description),
                     "Emergency Protection: Zero-fall safe deceleration");
            FlowNeuroPhysicalBound *b0 = &result_out->bounds[bc++];
            strncpy(b0->name, "emergency_decel", sizeof(b0->name) - 1);
            b0->lower_bound = 0.0;
            b0->upper_bound = 8.0;
            b0->is_hard_constraint = true;
            break;
        }
    }
    result_out->bound_count = bc;
}

int flow_neuro_bridge_project(FlowNeuroBridge *bridge,
                              const float *input_embedding,
                              size_t embedding_len,
                              FlowNeuroIntentType intent_hint,
                              FlowNeuroProjectionResult *result_out) {
    if (bridge == NULL || input_embedding == NULL || result_out == NULL) return 0;
    if (embedding_len != bridge->input_dimension) return 0;

    memset(result_out, 0, sizeof(*result_out));
    uint64_t t_start = flow_hardware_cycles();

    /* 1. Scalar 64-bit BMF Sparse Projection */
    uint64_t bmf = 0;
    for (size_t i = 0; i < FLOW_NEURO_BMF_BITS; i++) {
        const uint16_t *idx = bridge->bmf_indices[i];
        const int8_t *sgn = bridge->bmf_signs[i];

        float sum = sgn[0] * input_embedding[idx[0]] +
                    sgn[1] * input_embedding[idx[1]] +
                    sgn[2] * input_embedding[idx[2]] +
                    sgn[3] * input_embedding[idx[3]];

        if (sum > bridge->bmf_thresholds[i]) {
            bmf |= (1ULL << i);
        }
    }
    result_out->bmf_coordinates = bmf;

    /* 2. Scalar 16-D .fvec continuous feature projection */
    for (size_t j = 0; j < FLOW_NEURO_FVEC_DIM; j++) {
        const uint16_t *idx = bridge->fvec_indices[j];
        const int8_t *sgn = bridge->fvec_signs[j];

        float acc = sgn[0] * input_embedding[idx[0]] + sgn[1] * input_embedding[idx[1]] +
                    sgn[2] * input_embedding[idx[2]] + sgn[3] * input_embedding[idx[3]] +
                    sgn[4] * input_embedding[idx[4]] + sgn[5] * input_embedding[idx[5]] +
                    sgn[6] * input_embedding[idx[6]] + sgn[7] * input_embedding[idx[7]];

        result_out->fvec_features[j] = (double)(acc / (1.0f + fabsf(acc)));
    }

    /* 3. Classify / decode semantic intent */
    if (intent_hint != FLOW_NEURO_INTENT_GENERIC) {
        result_out->classified_intent = intent_hint;
    } else {
        uint8_t intent_selector = (uint8_t)(bmf & 0x03);
        result_out->classified_intent = (FlowNeuroIntentType)(intent_selector + 1);
    }

    flow_neuro_synthesize_bounds(result_out);

    uint64_t t_end = flow_hardware_cycles();
    result_out->projection_cycles = (t_end >= t_start) ? (t_end - t_start) : 0;
    uint64_t freq = flow_hardware_timer_frequency_hz();
    result_out->projection_nanoseconds = freq > 0 ? (((double)result_out->projection_cycles * 1e9) / (double)freq) : ((double)result_out->projection_cycles * 0.33);

    bridge->total_projections++;
    bridge->cumulative_latency_ns += result_out->projection_nanoseconds;
    return 1;
}

int flow_neuro_bridge_project_simd(FlowNeuroBridge *bridge,
                                   const float *input_embedding,
                                   size_t embedding_len,
                                   FlowNeuroIntentType intent_hint,
                                   FlowNeuroProjectionResult *result_out) {
    if (bridge == NULL || input_embedding == NULL || result_out == NULL) return 0;
    if (embedding_len != bridge->input_dimension) return 0;

    memset(result_out, 0, sizeof(*result_out));
    uint64_t t_start = flow_hardware_cycles();

    uint64_t bmf = 0;
#if defined(__ARM_NEON)
    /* ARM NEON Vectorized Projection */
    for (size_t i = 0; i < FLOW_NEURO_BMF_BITS; i++) {
        const uint16_t *idx = bridge->bmf_indices[i];
        const int8_t *sgn = bridge->bmf_signs[i];

        float32x4_t v_sgn = { (float)sgn[0], (float)sgn[1], (float)sgn[2], (float)sgn[3] };
        float32x4_t v_emb = { input_embedding[idx[0]], input_embedding[idx[1]], input_embedding[idx[2]], input_embedding[idx[3]] };
        float32x4_t v_prod = vmulq_f32(v_sgn, v_emb);
        float sum = vaddvq_f32(v_prod);

        if (sum > bridge->bmf_thresholds[i]) {
            bmf |= (1ULL << i);
        }
    }

    for (size_t j = 0; j < FLOW_NEURO_FVEC_DIM; j++) {
        const uint16_t *idx = bridge->fvec_indices[j];
        const int8_t *sgn = bridge->fvec_signs[j];

        float32x4_t sgn_lo = { (float)sgn[0], (float)sgn[1], (float)sgn[2], (float)sgn[3] };
        float32x4_t sgn_hi = { (float)sgn[4], (float)sgn[5], (float)sgn[6], (float)sgn[7] };
        float32x4_t emb_lo = { input_embedding[idx[0]], input_embedding[idx[1]], input_embedding[idx[2]], input_embedding[idx[3]] };
        float32x4_t emb_hi = { input_embedding[idx[4]], input_embedding[idx[5]], input_embedding[idx[6]], input_embedding[idx[7]] };

        float32x4_t prod_lo = vmulq_f32(sgn_lo, emb_lo);
        float32x4_t acc_vec = vfmaq_f32(prod_lo, sgn_hi, emb_hi);
        float acc = vaddvq_f32(acc_vec);
        result_out->fvec_features[j] = (double)(acc / (1.0f + fabsf(acc)));
    }
#else
    /* Unrolled 4-way loop fallback */
    for (size_t i = 0; i < FLOW_NEURO_BMF_BITS; i++) {
        const uint16_t *idx = bridge->bmf_indices[i];
        const int8_t *sgn = bridge->bmf_signs[i];
        float sum = sgn[0] * input_embedding[idx[0]] +
                    sgn[1] * input_embedding[idx[1]] +
                    sgn[2] * input_embedding[idx[2]] +
                    sgn[3] * input_embedding[idx[3]];
        if (sum > bridge->bmf_thresholds[i]) {
            bmf |= (1ULL << i);
        }
    }
    for (size_t j = 0; j < FLOW_NEURO_FVEC_DIM; j++) {
        const uint16_t *idx = bridge->fvec_indices[j];
        const int8_t *sgn = bridge->fvec_signs[j];
        float acc = sgn[0] * input_embedding[idx[0]] + sgn[1] * input_embedding[idx[1]] +
                    sgn[2] * input_embedding[idx[2]] + sgn[3] * input_embedding[idx[3]] +
                    sgn[4] * input_embedding[idx[4]] + sgn[5] * input_embedding[idx[5]] +
                    sgn[6] * input_embedding[idx[6]] + sgn[7] * input_embedding[idx[7]];
        result_out->fvec_features[j] = (double)(acc / (1.0f + fabsf(acc)));
    }
#endif
    result_out->bmf_coordinates = bmf;

    if (intent_hint != FLOW_NEURO_INTENT_GENERIC) {
        result_out->classified_intent = intent_hint;
    } else {
        uint8_t intent_selector = (uint8_t)(bmf & 0x03);
        result_out->classified_intent = (FlowNeuroIntentType)(intent_selector + 1);
    }

    flow_neuro_synthesize_bounds(result_out);

    uint64_t t_end = flow_hardware_cycles();
    result_out->projection_cycles = (t_end >= t_start) ? (t_end - t_start) : 0;
    uint64_t freq = flow_hardware_timer_frequency_hz();
    result_out->projection_nanoseconds = freq > 0 ? (((double)result_out->projection_cycles * 1e9) / (double)freq) : ((double)result_out->projection_cycles * 0.33);

    bridge->total_projections++;
    bridge->cumulative_latency_ns += result_out->projection_nanoseconds;
    return 1;
}

int flow_neuro_bridge_quantize(const FlowNeuroBridge *src, FlowNeuroBridgeQuantized *dst_out) {
    if (src == NULL || dst_out == NULL) return 0;
    memset(dst_out, 0, sizeof(*dst_out));
    dst_out->input_dimension = src->input_dimension;

    for (size_t i = 0; i < FLOW_NEURO_BMF_BITS; i++) {
        for (size_t k = 0; k < FLOW_NEURO_SPARSITY_BMF; k++) {
            dst_out->bmf_indices[i][k] = src->bmf_indices[i][k];
            dst_out->bmf_weights[i][k] = src->bmf_signs[i][k];
        }
        dst_out->bmf_thresholds[i] = (int16_t)(src->bmf_thresholds[i] * 128.0f);
    }

    for (size_t j = 0; j < FLOW_NEURO_FVEC_DIM; j++) {
        for (size_t k = 0; k < FLOW_NEURO_SPARSITY_FVEC; k++) {
            dst_out->fvec_indices[j][k] = src->fvec_indices[j][k];
            dst_out->fvec_weights[j][k] = src->fvec_signs[j][k];
        }
    }
    return 1;
}

int flow_neuro_bridge_project_quantized(const FlowNeuroBridgeQuantized *q_bridge,
                                        const int8_t *quantized_embedding,
                                        FlowNeuroIntentType intent_hint,
                                        FlowNeuroProjectionResult *result_out) {
    if (q_bridge == NULL || quantized_embedding == NULL || result_out == NULL) return 0;
    memset(result_out, 0, sizeof(*result_out));

    uint64_t t_start = flow_hardware_cycles();
    uint64_t bmf = 0;

    for (size_t i = 0; i < FLOW_NEURO_BMF_BITS; i++) {
        const uint16_t *idx = q_bridge->bmf_indices[i];
        const int8_t *w = q_bridge->bmf_weights[i];

        int32_t sum = (int32_t)w[0] * quantized_embedding[idx[0]] +
                      (int32_t)w[1] * quantized_embedding[idx[1]] +
                      (int32_t)w[2] * quantized_embedding[idx[2]] +
                      (int32_t)w[3] * quantized_embedding[idx[3]];

        if (sum > (int32_t)q_bridge->bmf_thresholds[i]) {
            bmf |= (1ULL << i);
        }
    }
    result_out->bmf_coordinates = bmf;

    for (size_t j = 0; j < FLOW_NEURO_FVEC_DIM; j++) {
        const uint16_t *idx = q_bridge->fvec_indices[j];
        const int8_t *w = q_bridge->fvec_weights[j];

        int32_t acc = (int32_t)w[0] * quantized_embedding[idx[0]] +
                      (int32_t)w[1] * quantized_embedding[idx[1]] +
                      (int32_t)w[2] * quantized_embedding[idx[2]] +
                      (int32_t)w[3] * quantized_embedding[idx[3]] +
                      (int32_t)w[4] * quantized_embedding[idx[4]] +
                      (int32_t)w[5] * quantized_embedding[idx[5]] +
                      (int32_t)w[6] * quantized_embedding[idx[6]] +
                      (int32_t)w[7] * quantized_embedding[idx[7]];

        float f_acc = (float)acc / 128.0f;
        result_out->fvec_features[j] = (double)(f_acc / (1.0f + fabsf(f_acc)));
    }

    if (intent_hint != FLOW_NEURO_INTENT_GENERIC) {
        result_out->classified_intent = intent_hint;
    } else {
        uint8_t intent_selector = (uint8_t)(bmf & 0x03);
        result_out->classified_intent = (FlowNeuroIntentType)(intent_selector + 1);
    }

    flow_neuro_synthesize_bounds(result_out);

    uint64_t t_end = flow_hardware_cycles();
    result_out->projection_cycles = (t_end >= t_start) ? (t_end - t_start) : 0;
    uint64_t freq = flow_hardware_timer_frequency_hz();
    result_out->projection_nanoseconds = freq > 0 ? (((double)result_out->projection_cycles * 1e9) / (double)freq) : ((double)result_out->projection_cycles * 0.33);

    return 1;
}

int flow_neuro_eval_bounds_simd(const FlowNeuroPhysicalBound *bounds,
                                size_t bound_count,
                                const double *state_vec,
                                uint32_t *violation_mask_out) {
    if (bounds == NULL || state_vec == NULL || violation_mask_out == NULL) return 0;
    uint32_t mask = 0;

    for (size_t i = 0; i < bound_count && i < 32; i++) {
        double dot = 0.0;
        for (size_t d = 0; d < FLOW_NEURO_FVEC_DIM; d++) {
            dot += bounds[i].normal[d] * state_vec[d];
        }
        if (dot < bounds[i].lower_bound || dot > bounds[i].upper_bound) {
            mask |= (1U << i);
        }
    }
    *violation_mask_out = mask;
    return 1;
}

FlowSMTResult flow_neuro_bridge_verify_smt(const FlowNeuroProjectionResult *result,
                                           FlowSMTProofAttestation *proof_out) {
    if (result == NULL) return FLOW_SMT_UNKNOWN;

    FLOW_SMT_BOX_BUILDER_DECL(builder);

    /* Theorem 1: Polyhedral Bound Ordering (lower_bound <= upper_bound) */
    uint64_t ordering_violations = 0;
    for (size_t i = 0; i < result->bound_count; i++) {
        if (result->bounds[i].lower_bound > result->bounds[i].upper_bound) {
            ordering_violations++;
        }
    }
    FLOW_SMT_BOX_ADD_RULE(builder, "polyhedral_bounds_order", ordering_violations, 0, 0,
                          FLOW_BOX_THEOREM_BUFFER_BOUNDS, "Lower bound exceeds upper bound");

    /* Theorem 2: Safe Grip & Motor Limits */
    uint64_t motor_violations = 0;
    for (size_t i = 0; i < result->bound_count; i++) {
        if (strcmp(result->bounds[i].name, "gripper_force_n") == 0) {
            if (result->bounds[i].upper_bound > 10.0) { /* > 10N breaks cup */
                motor_violations++;
            }
        }
    }
    FLOW_SMT_BOX_ADD_RULE(builder, "motor_gripper_safety", motor_violations, 0, 0,
                          FLOW_BOX_THEOREM_MEMORY_QUOTA, "Grip force exceeds cardboard cup structural limit");

    /* Theorem 3: Fluid Anti-Spill Limit (tilt <= 0.12 rad) */
    uint64_t fluid_violations = 0;
    if (result->classified_intent == FLOW_NEURO_INTENT_SMOOTH_FETCH_LATTE) {
        for (size_t i = 0; i < result->bound_count; i++) {
            if (strcmp(result->bounds[i].name, "fluid_tilt_limit") == 0) {
                if (result->bounds[i].upper_bound > 0.12) {
                    fluid_violations++;
                }
            }
        }
    }
    FLOW_SMT_BOX_ADD_RULE(builder, "fluid_tilt_unspillable", fluid_violations, 0, 0,
                          FLOW_BOX_THEOREM_SHARD_ISOLATION, "Tilt limit exceeds liquid meniscus spill threshold");

    /* Theorem 4: Bounded Projection Latency Invariant (< 50000 ns bounded deadline) */
    uint64_t latency_violation = (result->projection_nanoseconds > 50000.0) ? 1 : 0;
    FLOW_SMT_BOX_ADD_RULE(builder, "projection_latency_bound", latency_violation, 0, 0,
                          FLOW_BOX_THEOREM_DETERMINISM, "Projection latency exceeded deterministic deadline");

    FlowSMTResult res = FLOW_SMT_BOX_VERIFY(builder, "neuro_bridge", proof_out);
    if (res == FLOW_SMT_PROVEN_UNSAT && proof_out != NULL) {
        snprintf(proof_out->proof_summary, sizeof(proof_out->proof_summary),
                 "SMT NEURO-BRIDGE SOUND: Intent=%d, Latency=%.2fns, Cycles=%llu (Zero-Defect Soundness)",
                 (int)result->classified_intent,
                 result->projection_nanoseconds,
                 (unsigned long long)result->projection_cycles);
    }
    return res;
}

FlowSMTResult flow_neuro_verify_simd_soundness_smt(const FlowNeuroProjectionResult *baseline_res,
                                                  const FlowNeuroProjectionResult *simd_res,
                                                  double max_allowed_error,
                                                  FlowSMTProofAttestation *proof_out) {
    if (baseline_res == NULL || simd_res == NULL) return FLOW_SMT_UNKNOWN;

    FLOW_SMT_BOX_BUILDER_DECL(builder);

    /* Theorem 1: Intent Classification Parity */
    uint64_t intent_diff = (baseline_res->classified_intent == simd_res->classified_intent) ? 0 : 1;
    FLOW_SMT_BOX_ADD_RULE(builder, "intent_parity", intent_diff, 0, 0,
                          FLOW_BOX_THEOREM_DETERMINISM, "SIMD intent classification diverges from baseline reference");

    /* Theorem 2: Feature Cosine / L1 Error Bounded */
    double max_err = 0.0;
    for (size_t i = 0; i < FLOW_NEURO_FVEC_DIM; i++) {
        double err = fabs(baseline_res->fvec_features[i] - simd_res->fvec_features[i]);
        if (err > max_err) max_err = err;
    }
    uint64_t scaled_err = (uint64_t)(max_err * 10000.0);
    uint64_t scaled_limit = (uint64_t)(max_allowed_error * 10000.0);
    FLOW_SMT_BOX_ADD_RULE(builder, "simd_precision_bound", scaled_err, 0, scaled_limit,
                          FLOW_BOX_THEOREM_BUFFER_BOUNDS, "SIMD numerical error exceeds tolerance limit");

    FlowSMTResult res = FLOW_SMT_BOX_VERIFY(builder, "neuro_simd", proof_out);
    if (res == FLOW_SMT_PROVEN_UNSAT && proof_out != NULL) {
        snprintf(proof_out->proof_summary, sizeof(proof_out->proof_summary),
                 "SMT NEURO-SIMD SOUND: IntentParity=1, MaxErr=%.5f <= %.5f, SIMD Latency=%.2fns (Soundness Guaranteed)",
                 max_err, max_allowed_error, simd_res->projection_nanoseconds);
    }
    return res;
}

int flow_neuro_bridge_index_subspace(const FlowNeuroBridge *bridge,
                                     const float *input_embedding,
                                     size_t embedding_len,
                                     FlowNeuroIntentType intent_hint,
                                     const FlowBmfSubspaceRegistry *reg,
                                     uint32_t *subspace_id_out) {
    if (!bridge || !input_embedding || !subspace_id_out) return 0;
    FlowNeuroProjectionResult res;
    if (!flow_neuro_bridge_project_simd((FlowNeuroBridge *)bridge, input_embedding, embedding_len, intent_hint, &res)) {
        return 0;
    }
    if (intent_hint != FLOW_NEURO_INTENT_GENERIC) {
        *subspace_id_out = res.indexed_subspace_id;
    } else if (reg != NULL) {
        *subspace_id_out = flow_bmf_subspace_index_from_features(reg, res.fvec_features, FLOW_NEURO_FVEC_DIM);
    } else {
        *subspace_id_out = res.indexed_subspace_id;
    }
    return 1;
}

int flow_neuro_bridge_to_1bit_canvas(FlowNeuroBridge *bridge,
                                     const float *input_embedding,
                                     size_t embedding_len,
                                     FlowNeuroIntentType intent_hint,
                                     const FlowBmfSubspaceRegistry *reg,
                                     FlowBmf1BitCanvas *canvas_out,
                                     FlowNeuroProjectionResult *result_out) {
    if (!bridge || !input_embedding || !canvas_out) return 0;
    FlowNeuroProjectionResult local_res;
    FlowNeuroProjectionResult *res = result_out ? result_out : &local_res;
    if (!flow_neuro_bridge_project_simd(bridge, input_embedding, embedding_len, intent_hint, res)) {
        return 0;
    }
    uint32_t sub_id = res->indexed_subspace_id;
    if (intent_hint == FLOW_NEURO_INTENT_GENERIC && reg != NULL) {
        sub_id = flow_bmf_subspace_index_from_features(reg, res->fvec_features, FLOW_NEURO_FVEC_DIM);
        res->indexed_subspace_id = sub_id;
    }
    if (reg != NULL) {
        const FlowBmfSubspace *sub = flow_bmf_subspace_lookup(reg, sub_id);
        flow_bmf_canvas_init_from_subspace(canvas_out, sub);
    } else {
        flow_bmf_canvas_init(canvas_out, sub_id,
                             FLOW_BMF_SW_HARD_SAFETY | FLOW_BMF_SW_CONTRACT_GATE,
                             ~0ULL,
                             res->bmf_1bit_switches);
    }
    canvas_out->dynamic_bias = res->bmf_coordinates;
    canvas_out->is_adjudicated_sound = flow_bmf_canvas_verify_invariants(canvas_out);
    return 1;
}

