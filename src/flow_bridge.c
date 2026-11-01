#include "flow_bridge.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define FLOW_BRIDGE_STACK_BUF_MAX 512U

const char *flow_bridge_status_name(FlowBridgeStatusCode code) {
    switch (code) {
    case FLOW_BRIDGE_SUCCESS:
        return "FLOW_BRIDGE_SUCCESS";
    case FLOW_BRIDGE_ERR_NULL_PARAM:
        return "FLOW_BRIDGE_ERR_NULL_PARAM";
    case FLOW_BRIDGE_ERR_QUANTIZE:
        return "FLOW_BRIDGE_ERR_QUANTIZE";
    case FLOW_BRIDGE_ERR_PRECONDITION:
        return "FLOW_BRIDGE_ERR_PRECONDITION";
    case FLOW_BRIDGE_ERR_LATENCY:
        return "FLOW_BRIDGE_ERR_LATENCY";
    case FLOW_BRIDGE_ERR_OPERATOR:
        return "FLOW_BRIDGE_ERR_OPERATOR";
    case FLOW_BRIDGE_ERR_POSTCONDITION:
        return "FLOW_BRIDGE_ERR_POSTCONDITION";
    case FLOW_BRIDGE_ERR_ENVELOPE:
        return "FLOW_BRIDGE_ERR_ENVELOPE";
    case FLOW_BRIDGE_ERR_DEQUANTIZE:
        return "FLOW_BRIDGE_ERR_DEQUANTIZE";
    case FLOW_BRIDGE_ERR_JET_SINGULAR:
        return "FLOW_BRIDGE_ERR_JET_SINGULAR";
    default:
        return "FLOW_BRIDGE_ERR_UNKNOWN";
    }
}

uint64_t flow_bridge_project_jet2_sieve(FlowJetScalar2 jet_sample,
                                        const FlowJetManifoldEnvelope *jet_env,
                                        FlowKanStatus *kan_status_out, uint64_t *kan_mismatch_out) {
    uint64_t sieve = 0ULL;
    bool finite_ok = isfinite(jet_sample.v) && isfinite(jet_sample.d1) && isfinite(jet_sample.d2);
    if (finite_ok) {
        sieve |= FLOW_JET_SIEVE_FINITE;
    }

    if (jet_env == NULL) {
        if (finite_ok) {
            sieve |= FLOW_JET_SIEVE_CANONICAL_ALL;
        }
        if (kan_status_out) {
            *kan_status_out =
                finite_ok ? FLOW_KAN_FILLED_HOMOTOPIC : FLOW_KAN_OBSTRUCTED_SINGULARITY;
        }
        if (kan_mismatch_out) {
            *kan_mismatch_out = finite_ok ? 0ULL : FLOW_JET_SIEVE_FINITE;
        }
        return sieve;
    }

    if (finite_ok) {
        /* 0-Order Value Box: v in [v_min, v_max] */
        if (jet_env->v_max <= jet_env->v_min ||
            (jet_sample.v >= jet_env->v_min && jet_sample.v <= jet_env->v_max)) {
            sieve |= FLOW_JET_SIEVE_ZERO_ORDER_BOX;
        }

        /* 1-Order Tangent Space / Jacobian Lipschitz Bound: |df/dx| <= max_lipschitz_d1 */
        if (jet_env->max_lipschitz_d1 <= 0.0 || fabs(jet_sample.d1) <= jet_env->max_lipschitz_d1) {
            sieve |= FLOW_JET_SIEVE_FIRST_ORDER_LIPSCHITZ;
        }

        /* 2-Order Hessian Curvature Bound: |d^2f/dx^2| <= max_curvature_d2 */
        if (jet_env->max_curvature_d2 <= 0.0 || fabs(jet_sample.d2) <= jet_env->max_curvature_d2) {
            sieve |= FLOW_JET_SIEVE_SECOND_ORDER_CURVATURE;
        }

        /* 2-Jet Lyapunov Dissipation: V(v) = 1/2 v^2 => L_f V = v * d1 <= -gamma * v^2 + 1e-6 */
        double v_sq = jet_sample.v * jet_sample.v;
        double lie_v = jet_sample.v * jet_sample.d1;
        double gamma = jet_env->lyapunov_decay_rate > 0.0 ? jet_env->lyapunov_decay_rate : 0.0;
        if (lie_v <= -gamma * v_sq + 1e-6) {
            sieve |= FLOW_JET_SIEVE_LYAPUNOV_DISSIPATIVE;
        }

        /* Thom Transversality near Boundary dM: prevent tangential grazing singularities */
        if (jet_env->transversality_floor <= 0.0) {
            sieve |= FLOW_JET_SIEVE_TRANSVERSALITY;
        } else {
            double span =
                (jet_env->v_max > jet_env->v_min) ? (jet_env->v_max - jet_env->v_min) : 1.0;
            double dist_lo = fabs(jet_sample.v - jet_env->v_min);
            double dist_hi = fabs(jet_env->v_max - jet_sample.v);
            double boundary_band = 0.05 * span;
            if ((dist_lo > boundary_band && dist_hi > boundary_band) ||
                fabs(jet_sample.d1) >= jet_env->transversality_floor) {
                sieve |= FLOW_JET_SIEVE_TRANSVERSALITY;
            }
        }
    }

    uint64_t req_mask =
        jet_env->required_kan_sieve ? jet_env->required_kan_sieve : FLOW_JET_SIEVE_CANONICAL_ALL;
    uint64_t filter = jet_env->kan_boundary_filter ? jet_env->kan_boundary_filter : req_mask;
    uint64_t mismatch = 0ULL;
    FlowKanStatus kst = flow_kan_check_homotopy(sieve, req_mask, filter, &mismatch);
    if (kan_status_out) {
        *kan_status_out = kst;
    }
    if (kan_mismatch_out) {
        *kan_mismatch_out = mismatch;
    }
    return sieve;
}

uint64_t flow_bridge_project_jet_bundle_sieve(const FlowJetScalar2 *fibers, size_t dim,
                                              const FlowJetManifoldEnvelope *jet_env,
                                              FlowKanStatus *kan_status_out,
                                              uint64_t *kan_mismatch_out) {
    if (fibers == NULL || dim == 0U) {
        if (kan_status_out) {
            *kan_status_out = FLOW_KAN_OBSTRUCTED_SINGULARITY;
        }
        if (kan_mismatch_out) {
            *kan_mismatch_out = FLOW_JET_SIEVE_CANONICAL_ALL;
        }
        return 0ULL;
    }

    uint64_t bundle_sieve = ~0ULL;
    for (size_t i = 0; i < dim; ++i) {
        uint64_t fiber_sieve = flow_bridge_project_jet2_sieve(fibers[i], jet_env, NULL, NULL);
        bundle_sieve &= fiber_sieve;
    }

    uint64_t req_mask = (jet_env && jet_env->required_kan_sieve) ? jet_env->required_kan_sieve
                                                                 : FLOW_JET_SIEVE_CANONICAL_ALL;
    uint64_t filter =
        (jet_env && jet_env->kan_boundary_filter) ? jet_env->kan_boundary_filter : req_mask;
    uint64_t mismatch = 0ULL;
    FlowKanStatus kst = flow_kan_check_homotopy(bundle_sieve, req_mask, filter, &mismatch);
    if (kan_status_out) {
        *kan_status_out = kst;
    }
    if (kan_mismatch_out) {
        *kan_mismatch_out = mismatch;
    }
    return bundle_sieve;
}

int flow_bridge_execute_step(const FlowPhysicalTransducer *transducer,
                             const FlowFormalContract *contract,
                             const FlowPhysicalEnvelope *envelope,
                             int (*engineering_operator)(const void *in, void *out),
                             const void *raw_obs_in, void *raw_act_out,
                             uint64_t observed_condition_mask, uint64_t *boundary_tag_out) {
    /* Step 1: Parameter checks */
    if (boundary_tag_out != NULL) {
        *boundary_tag_out = 0ULL;
    }

    if (transducer == NULL || contract == NULL || envelope == NULL ||
        engineering_operator == NULL || raw_obs_in == NULL || raw_act_out == NULL) {
        if (boundary_tag_out != NULL) {
            *boundary_tag_out = flow_hott_make_boundary_tag(
                0U, 0U, (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY, 0U, 5U);
        }
        return FLOW_BRIDGE_ERR_NULL_PARAM;
    }

    if (transducer->quantize_observation == NULL || transducer->dequantize_actuation == NULL ||
        envelope->fallback_operator == NULL) {
        if (boundary_tag_out != NULL) {
            *boundary_tag_out = flow_hott_make_boundary_tag(
                0U, 0U, (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY, 0U, 5U);
        }
        return FLOW_BRIDGE_ERR_NULL_PARAM;
    }

    size_t in_bytes = transducer->discrete_in_bytes > 0U ? transducer->discrete_in_bytes
                                                         : FLOW_BRIDGE_STACK_BUF_MAX;
    size_t out_bytes = transducer->discrete_out_bytes > 0U ? transducer->discrete_out_bytes
                                                           : FLOW_BRIDGE_STACK_BUF_MAX;

    /* Step 2: Buffer allocations (hybrid stack / heap) */
    uint8_t stack_in[FLOW_BRIDGE_STACK_BUF_MAX];
    uint8_t stack_out[FLOW_BRIDGE_STACK_BUF_MAX];
    void *heap_in = NULL;
    void *heap_out = NULL;
    void *discrete_in = stack_in;
    void *discrete_out = stack_out;

    if (in_bytes > FLOW_BRIDGE_STACK_BUF_MAX) {
        heap_in = calloc(1U, in_bytes);
        if (heap_in == NULL) {
            if (boundary_tag_out != NULL) {
                *boundary_tag_out = flow_hott_make_boundary_tag(
                    0U, 0U, (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY, 0U, 0x0FFFU);
            }
            return FLOW_BRIDGE_ERR_NULL_PARAM;
        }
        discrete_in = heap_in;
    } else {
        memset(stack_in, 0, in_bytes);
    }

    if (out_bytes > FLOW_BRIDGE_STACK_BUF_MAX) {
        heap_out = calloc(1U, out_bytes);
        if (heap_out == NULL) {
            free(heap_in);
            if (boundary_tag_out != NULL) {
                *boundary_tag_out = flow_hott_make_boundary_tag(
                    0U, 0U, (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY, 0U, 0x0FFFU);
            }
            return FLOW_BRIDGE_ERR_NULL_PARAM;
        }
        discrete_out = heap_out;
    } else {
        memset(stack_out, 0, out_bytes);
    }

    int ret_status = FLOW_BRIDGE_SUCCESS;

    /* Step 3: Transducer Quantization */
    int q_res = transducer->quantize_observation(raw_obs_in, discrete_in);
    if (q_res != 0) {
        if (boundary_tag_out != NULL) {
            *boundary_tag_out = flow_hott_make_boundary_tag(
                0U, 0U, (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY, 0U, 1U);
        }
        (void)envelope->fallback_operator(discrete_in, discrete_out);
        (void)transducer->dequantize_actuation(discrete_out, raw_act_out);
        ret_status = FLOW_BRIDGE_ERR_QUANTIZE;
        goto cleanup;
    }

    /* Step 4: Physical Envelope & Topos Sieve Monitoring */
    if ((observed_condition_mask & envelope->ambient_sieve_mask) != envelope->ambient_sieve_mask) {
        if (boundary_tag_out != NULL) {
            *boundary_tag_out =
                flow_hott_make_boundary_tag(0U, 0U, (uint8_t)FLOW_BOUNDARY_HIT_COLLAPSE, 0U, 3U);
        }
        (void)envelope->fallback_operator(discrete_in, discrete_out);
        (void)transducer->dequantize_actuation(discrete_out, raw_act_out);
        ret_status = FLOW_BRIDGE_ERR_ENVELOPE;
        goto cleanup;
    }

    /* Step 5: Formal Precondition Guard */
    if (contract->check_precondition != NULL) {
        uint64_t vtag = 0ULL;
        int pre_res = contract->check_precondition(discrete_in, &vtag);
        if (pre_res < 0 || vtag != 0ULL) {
            if (vtag == 0ULL) {
                vtag = flow_hott_make_boundary_tag(0U, 0U, (uint8_t)FLOW_BOUNDARY_PRECOND, 0U, 1U);
            }
            if (boundary_tag_out != NULL) {
                *boundary_tag_out = vtag;
            }
            (void)envelope->fallback_operator(discrete_in, discrete_out);
            (void)transducer->dequantize_actuation(discrete_out, raw_act_out);
            ret_status = FLOW_BRIDGE_ERR_PRECONDITION;
            goto cleanup;
        }
    }

    /* Step 6: Operator Execution & Cycle Envelope Monitoring */
    uint64_t t0 = flow_hardware_cycles();
    int op_res = engineering_operator(discrete_in, discrete_out);
    uint64_t t1 = flow_hardware_cycles();
    uint64_t elapsed_cycles = (t1 >= t0) ? (t1 - t0) : 0ULL;

    if (envelope->max_latency_cycles > 0ULL && elapsed_cycles > envelope->max_latency_cycles) {
        if (boundary_tag_out != NULL) {
            *boundary_tag_out = flow_hott_make_boundary_tag(
                0U, 0U, (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY, 0U, 4U);
        }
        (void)envelope->fallback_operator(discrete_in, discrete_out);
        (void)transducer->dequantize_actuation(discrete_out, raw_act_out);
        ret_status = FLOW_BRIDGE_ERR_LATENCY;
        goto cleanup;
    }

    if (op_res != 0) {
        if (boundary_tag_out != NULL) {
            *boundary_tag_out = flow_hott_make_boundary_tag(
                0U, 0U, (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY, 0U, 4U);
        }
        (void)envelope->fallback_operator(discrete_in, discrete_out);
        (void)transducer->dequantize_actuation(discrete_out, raw_act_out);
        ret_status = FLOW_BRIDGE_ERR_OPERATOR;
        goto cleanup;
    }

    /* Step 7: Formal Postcondition Guard */
    if (contract->check_postcondition != NULL) {
        uint64_t vtag = 0ULL;
        int post_res = contract->check_postcondition(discrete_in, discrete_out, &vtag);
        if (post_res < 0 || vtag != 0ULL) {
            if (vtag == 0ULL) {
                vtag = flow_hott_make_boundary_tag(0U, 0U, (uint8_t)FLOW_BOUNDARY_POSTCOND, 0U, 2U);
            }
            if (boundary_tag_out != NULL) {
                *boundary_tag_out = vtag;
            }
            (void)envelope->fallback_operator(discrete_in, discrete_out);
            (void)transducer->dequantize_actuation(discrete_out, raw_act_out);
            ret_status = FLOW_BRIDGE_ERR_POSTCONDITION;
            goto cleanup;
        }
    }

    /* Step 8: Transducer Dequantization / Actuation */
    int deq_res = transducer->dequantize_actuation(discrete_out, raw_act_out);
    if (deq_res != 0) {
        if (boundary_tag_out != NULL) {
            *boundary_tag_out = flow_hott_make_boundary_tag(
                0U, 0U, (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY, 0U, 5U);
        }
        ret_status = FLOW_BRIDGE_ERR_DEQUANTIZE;
        goto cleanup;
    }

    if (boundary_tag_out != NULL) {
        *boundary_tag_out = 0ULL;
    }
    ret_status = FLOW_BRIDGE_SUCCESS;

cleanup:
    if (heap_in != NULL) {
        free(heap_in);
    }
    if (heap_out != NULL) {
        free(heap_out);
    }
    return ret_status;
}

int flow_bridge_execute_hott_jet_step(const FlowPhysicalBridge *bridge,
                                      const FlowJetScalar2 *jet_fibers, size_t jet_dim,
                                      const void *raw_obs_in, void *raw_act_out,
                                      uint64_t ambient_sieve_bits, uint64_t *boundary_tag_out) {
    if (boundary_tag_out != NULL) {
        *boundary_tag_out = 0ULL;
    }
    if (bridge == NULL || raw_obs_in == NULL || raw_act_out == NULL) {
        if (boundary_tag_out != NULL) {
            *boundary_tag_out = flow_hott_make_boundary_tag(
                0U, 0U, (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY, 0U, 5U);
        }
        return FLOW_BRIDGE_ERR_NULL_PARAM;
    }

    /* 1. Evaluate 2-Jet Differential Manifold Bundle & Kan Open-Box Homotopy */
    uint64_t jet_sieve = FLOW_JET_SIEVE_CANONICAL_ALL;
    if (jet_fibers != NULL && jet_dim > 0U) {
        FlowKanStatus kan_status = FLOW_KAN_FILLED_HOMOTOPIC;
        uint64_t kan_mismatch = 0ULL;
        jet_sieve = flow_bridge_project_jet_bundle_sieve(jet_fibers, jet_dim, &bridge->jet_envelope,
                                                         &kan_status, &kan_mismatch);
        if (kan_status != FLOW_KAN_FILLED_HOMOTOPIC) {
            uint8_t ruptured_axis =
                (kan_mismatch != 0ULL) ? (uint8_t)__builtin_ctzll(kan_mismatch) : 50U;
            if (boundary_tag_out != NULL) {
                *boundary_tag_out = flow_hott_make_boundary_tag(
                    0U, 0U, (uint8_t)FLOW_BOUNDARY_HIT_COLLAPSE, ruptured_axis, 3U);
            }
            /* Execute safe homotopy fallback along boundary dM */
            if (bridge->transducer.quantize_observation != NULL &&
                bridge->envelope.fallback_operator != NULL &&
                bridge->transducer.dequantize_actuation != NULL) {
                uint8_t stack_in[FLOW_BRIDGE_STACK_BUF_MAX];
                uint8_t stack_out[FLOW_BRIDGE_STACK_BUF_MAX];
                memset(stack_in, 0, sizeof(stack_in));
                memset(stack_out, 0, sizeof(stack_out));
                (void)bridge->transducer.quantize_observation(raw_obs_in, stack_in);
                (void)bridge->envelope.fallback_operator(stack_in, stack_out);
                (void)bridge->transducer.dequantize_actuation(stack_out, raw_act_out);
            }
            return FLOW_BRIDGE_ERR_JET_SINGULAR;
        }
    }

    uint64_t combined_sieve = ambient_sieve_bits | jet_sieve;
    return flow_bridge_execute_step(&bridge->transducer, &bridge->contract, &bridge->envelope,
                                    bridge->engineering_operator, raw_obs_in, raw_act_out,
                                    combined_sieve, boundary_tag_out);
}
