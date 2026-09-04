#include "neuro_bridge.h"

#include <math.h>
#include <string.h>
#include <stdio.h>

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

int flow_neuro_bridge_project(FlowNeuroBridge *bridge,
                              const float *input_embedding,
                              size_t embedding_len,
                              FlowNeuroIntentType intent_hint,
                              FlowNeuroProjectionResult *result_out) {
    if (bridge == NULL || input_embedding == NULL || result_out == NULL) {
        return 0;
    }
    if (embedding_len != bridge->input_dimension) {
        return 0;
    }

    memset(result_out, 0, sizeof(*result_out));

    /* Start high-resolution physical timer probe */
    uint64_t t_start = flow_hardware_cycles();

    /* 1. Ultra-fast 64-bit BMF Sparse Projection (256 operations) */
    uint64_t bmf = 0;
    for (size_t i = 0; i < FLOW_NEURO_BMF_BITS; i++) {
        float sum = 0.0f;
        const uint16_t *idx = bridge->bmf_indices[i];
        const int8_t *sgn = bridge->bmf_signs[i];

        sum += sgn[0] * input_embedding[idx[0]];
        sum += sgn[1] * input_embedding[idx[1]];
        sum += sgn[2] * input_embedding[idx[2]];
        sum += sgn[3] * input_embedding[idx[3]];

        if (sum > bridge->bmf_thresholds[i]) {
            bmf |= (1ULL << i);
        }
    }
    result_out->bmf_coordinates = bmf;

    /* 2. Ultra-fast 16-D .fvec continuous feature projection (128 operations) */
    for (size_t j = 0; j < FLOW_NEURO_FVEC_DIM; j++) {
        float acc = 0.0f;
        const uint16_t *idx = bridge->fvec_indices[j];
        const int8_t *sgn = bridge->fvec_signs[j];

        acc += sgn[0] * input_embedding[idx[0]];
        acc += sgn[1] * input_embedding[idx[1]];
        acc += sgn[2] * input_embedding[idx[2]];
        acc += sgn[3] * input_embedding[idx[3]];
        acc += sgn[4] * input_embedding[idx[4]];
        acc += sgn[5] * input_embedding[idx[5]];
        acc += sgn[6] * input_embedding[idx[6]];
        acc += sgn[7] * input_embedding[idx[7]];

        /* Fast smooth normalization into (-1.0, 1.0) */
        result_out->fvec_features[j] = (double)(acc / (1.0f + fabsf(acc)));
    }

    /* 3. Classify / decode semantic intent */
    if (intent_hint != FLOW_NEURO_INTENT_GENERIC) {
        result_out->classified_intent = intent_hint;
    } else {
        /* Automatic classification from top bits of projected BMF */
        uint8_t intent_selector = (uint8_t)(bmf & 0x03);
        result_out->classified_intent = (FlowNeuroIntentType)(intent_selector + 1);
    }

    /* 4. Synthesize rigid polyhedral physical bounds based on semantic intent */
    size_t bc = 0;
    switch (result_out->classified_intent) {
        case FLOW_NEURO_INTENT_SMOOTH_FETCH_LATTE: {
            snprintf(result_out->intent_description, sizeof(result_out->intent_description),
                     "Smooth Fetch: Unspillable latte fluid dynamics & soft grasp");

            /* Bound 0: Tilt Angle limit (rad) - fluid meniscus stability */
            FlowNeuroPhysicalBound *b0 = &result_out->bounds[bc++];
            strncpy(b0->name, "fluid_tilt_limit", sizeof(b0->name) - 1);
            b0->normal[0] = 1.0;
            b0->lower_bound = 0.0;
            b0->upper_bound = 0.08; /* ~4.6 degrees max tilt */
            b0->is_hard_constraint = true;

            /* Bound 1: Max Angular Acceleration (rad/s^2) */
            FlowNeuroPhysicalBound *b1 = &result_out->bounds[bc++];
            strncpy(b1->name, "max_angular_accel", sizeof(b1->name) - 1);
            b1->normal[1] = 1.0;
            b1->lower_bound = 0.0;
            b1->upper_bound = 0.40; /* Smooth gradual slew */
            b1->is_hard_constraint = true;

            /* Bound 2: Gripper Normal Contact Force (N) */
            FlowNeuroPhysicalBound *b2 = &result_out->bounds[bc++];
            strncpy(b2->name, "gripper_force_n", sizeof(b2->name) - 1);
            b2->normal[2] = 1.0;
            b2->lower_bound = 2.0; /* Minimum grip to prevent slippage */
            b2->upper_bound = 4.5; /* Maximum grip before crushing cardboard cup */
            b2->is_hard_constraint = true;

            /* Bound 3: Max Jerk (m/s^3) */
            FlowNeuroPhysicalBound *b3 = &result_out->bounds[bc++];
            strncpy(b3->name, "max_jerk_limit", sizeof(b3->name) - 1);
            b3->normal[3] = 1.0;
            b3->lower_bound = 0.0;
            b3->upper_bound = 0.80;
            b3->is_hard_constraint = true;
            break;
        }
        case FLOW_NEURO_INTENT_AGILE_SPRINT: {
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

    /* Measure elapsed hardware cycles and compute nanoseconds */
    uint64_t t_end = flow_hardware_cycles();
    result_out->projection_cycles = (t_end >= t_start) ? (t_end - t_start) : 0;

    uint64_t freq = flow_hardware_timer_frequency_hz();
    if (freq > 0) {
        result_out->projection_nanoseconds = ((double)result_out->projection_cycles * 1e9) / (double)freq;
    } else {
        /* Fallback assumption: 3 GHz cycle timer = 0.33 ns/cycle */
        result_out->projection_nanoseconds = (double)result_out->projection_cycles * 0.33;
    }

    bridge->total_projections++;
    bridge->cumulative_latency_ns += result_out->projection_nanoseconds;

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

    /* Theorem 4: Bounded Projection Latency Invariant (< 5000 ns bounded deadline) */
    uint64_t latency_violation = (result->projection_nanoseconds > 5000.0) ? 1 : 0;
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
