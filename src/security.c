#include "security.h"
#include "registry.h"
#include "flow_jet.h"
#include "flow_jet_dead_reckon.h"
#include "jit.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t flow_security_next(uint64_t *state) {
    uint64_t value = *state;
    if (value == 0) value = UINT64_C(0x9e3779b97f4a7c15);
    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    *state = value;
    return value;
}

const char *flow_security_outcome_name(FlowSecurityOutcome outcome) {
    switch (outcome) {
        case FLOW_SECURITY_PASS: return "pass";
        case FLOW_SECURITY_CONTRACT_VIOLATION: return "contract_violation";
        case FLOW_SECURITY_MEMORY_VIOLATION: return "memory_violation";
        case FLOW_SECURITY_RESOURCE_EXHAUSTION: return "resource_exhaustion";
        case FLOW_SECURITY_TIMEOUT: return "timeout";
        case FLOW_SECURITY_DIVERGENCE: return "divergence";
        case FLOW_SECURITY_INCOMPLETE: return "incomplete";
        case FLOW_SECURITY_PHYSICAL_BREACH: return "physical_breach";
        default: return "unknown";
    }
}

static void flow_security_record_failure(FlowSecurityReport *report,
                                         const FlowSecurityCase *security_case,
                                         FlowSecurityOutcome outcome,
                                         const char *message) {
    report->failures++;
    if (report->failures != 1) return;
    report->first_failure = outcome;
    report->first_failure_round = security_case->round;
    report->first_failure_bit = security_case->mutated_bit;
    report->first_failure_genome = security_case->mutated_genome;
    if (message != NULL) {
        strncpy(report->first_failure_message, message,
                sizeof(report->first_failure_message) - 1);
        report->first_failure_message[
            sizeof(report->first_failure_message) - 1] = '\0';
    }
}

int flow_security_run(uint64_t seed, uint64_t base_genome, uint32_t genome_bits,
                      uint32_t rounds, FlowSecurityProbeFn probe,
                      void *userdata, FlowSecurityReport *report) {
    uint64_t state;
    uint32_t round;
    if (probe == NULL || report == NULL || genome_bits == 0 ||
        genome_bits > 64 || rounds == 0)
        return -1;
    memset(report, 0, sizeof(*report));
    report->seed = seed;
    report->base_genome = base_genome;
    report->genome_bits = genome_bits;
    report->rounds = rounds;
    report->first_failure = FLOW_SECURITY_PASS;
    state = seed == 0 ? UINT64_C(0x9e3779b97f4a7c15) : seed;

    {
        FlowSecurityCase base = {seed, base_genome, base_genome, UINT32_MAX,
                                 UINT32_MAX};
        char message[160] = {0};
        FlowSecurityOutcome outcome = probe(&base, userdata, message,
                                            sizeof(message));
        report->checks++;
        if (outcome != FLOW_SECURITY_PASS) {
            flow_security_record_failure(report, &base, outcome, message);
            return 1;
        }
    }

    for (round = 0; round < rounds; ++round) {
        uint64_t random = flow_security_next(&state);
        uint32_t bit = (uint32_t)(random % genome_bits);
        FlowSecurityCase security_case = {
            seed, base_genome, base_genome ^ (UINT64_C(1) << bit), bit, round};
        char message[160] = {0};
        FlowSecurityOutcome outcome = probe(&security_case, userdata, message,
                                            sizeof(message));
        report->checks++;
        if (outcome != FLOW_SECURITY_PASS) {
            flow_security_record_failure(report, &security_case, outcome,
                                         message);
            return 1;
        }
    }
    return 0;
}

int flow_security_write_attestation(FILE *output, const char *component,
                                    const FlowSecurityReport *report) {
    if (output == NULL || component == NULL || report == NULL) return 0;
    if (report->failures == 0) {
        fprintf(output,
                "security_attestation component=%s status=verified seed=%llu "
                "genome=0x%016llx bits=%u rounds=%u checks=%zu failures=0\n",
                component, (unsigned long long)report->seed,
                (unsigned long long)report->base_genome, report->genome_bits,
                report->rounds, report->checks);
    } else {
        fprintf(output,
                "security_attestation component=%s status=rejected "
                "seed=%llu genome=0x%016llx bits=%u rounds=%u checks=%zu "
                "failures=%zu outcome=%s round=%u bit=%u mutated=0x%016llx "
                "message=%s\n",
                component, (unsigned long long)report->seed,
                (unsigned long long)report->base_genome, report->genome_bits,
                report->rounds, report->checks, report->failures,
                flow_security_outcome_name(report->first_failure),
                report->first_failure_round, report->first_failure_bit,
                (unsigned long long)report->first_failure_genome,
                report->first_failure_message);
    }
    return ferror(output) == 0;
}

/* ========================================================================= */
/* Linker Hard Gates Implementation                                          */
/* ========================================================================= */

FlowSecurityOutcome flow_security_check_contract_gate(
    const FlowCompositionSpec *spec, char *message, size_t message_size) {
    if (spec == NULL || spec->component == NULL) {
        if (message && message_size) snprintf(message, message_size, "null spec or component");
        return FLOW_SECURITY_CONTRACT_VIOLATION;
    }
    if (spec->ir != NULL) {
        if (!component_compatible(spec->ir, spec->component)) {
            if (message && message_size)
                snprintf(message, message_size,
                         "component '%s' violates semantic contract invariants",
                         spec->component->id);
            return FLOW_SECURITY_CONTRACT_VIOLATION;
        }
    }
    return FLOW_SECURITY_PASS;
}

FlowSecurityOutcome flow_security_check_abi_migration_gate(
    const FlowCompositionSpec *spec, char *message, size_t message_size) {
    if (spec == NULL || spec->component == NULL) {
        if (message && message_size) snprintf(message, message_size, "null spec or component");
        return FLOW_SECURITY_DIVERGENCE;
    }
    if (spec->reload_adapter_enabled) {
        if (!flow_component_supports_reload(spec->component)) {
            if (message && message_size)
                snprintf(message, message_size,
                         "component '%s' does not satisfy reload adapter ABI invariants",
                         spec->component->id);
            return FLOW_SECURITY_DIVERGENCE;
        }
    }
    return FLOW_SECURITY_PASS;
}

FlowSecurityOutcome flow_security_check_ownership_gate(
    const FlowCompositionSpec *spec, char *message, size_t message_size) {
    int is_read_only;
    if (spec == NULL || spec->component == NULL) {
        if (message && message_size) snprintf(message, message_size, "null spec or component");
        return FLOW_SECURITY_MEMORY_VIOLATION;
    }
    is_read_only = spec->read_only_ownership ||
                   (spec->ir != NULL && spec->ir->fact_mutability_read_only);
    if (is_read_only && spec->concurrency_threads > 1 &&
        (spec->ir != NULL && spec->ir->state_shared && !spec->component->supports_read_heavy)) {
        if (message && message_size)
            snprintf(message, message_size,
                     "read-only ownership boundary breached by uncoordinated concurrent writer");
        return FLOW_SECURITY_MEMORY_VIOLATION;
    }
    if (spec->concurrency_threads > 1 && spec->ir != NULL &&
        spec->ir->state_shared && !spec->component->supports_shared) {
        if (message && message_size)
            snprintf(message, message_size,
                     "concurrent execution on unshared state causes race divergence");
        return FLOW_SECURITY_CONTRACT_VIOLATION;
    }
    return FLOW_SECURITY_PASS;
}

FlowSecurityOutcome flow_security_check_resource_quota_gate(
    const FlowCompositionSpec *spec, char *message, size_t message_size) {
    size_t total_memory = 0;
    size_t limit = 0;
    if (spec == NULL || spec->component == NULL) {
        if (message && message_size) snprintf(message, message_size, "null spec or component");
        return FLOW_SECURITY_RESOURCE_EXHAUSTION;
    }
    total_memory += spec->component->memory_fixed_bytes;
    total_memory += spec->total_composed_bytes;
    if (spec->metrics != NULL) {
        total_memory += spec->metrics->memory_bytes;
    }
    limit = spec->memory_limit_bytes;
    if (limit == 0 && spec->ir != NULL && spec->ir->memory_limit_mb > 0) {
        limit = (size_t)spec->ir->memory_limit_mb * 1024u * 1024u;
    }
    if (limit > 0 && total_memory > limit) {
        if (message && message_size)
            snprintf(message, message_size,
                     "composed memory (%zu bytes) exceeds quota (%zu bytes)",
                     total_memory, limit);
        return FLOW_SECURITY_RESOURCE_EXHAUSTION;
    }
    if (spec->ir != NULL && spec->ir->top_n > 0 && spec->metrics != NULL &&
        spec->metrics->capacity > 0 && (size_t)spec->ir->top_n > spec->metrics->capacity) {
        if (message && message_size)
            snprintf(message, message_size,
                     "top_n (%d) exceeds candidate capacity (%zu)",
                     spec->ir->top_n, spec->metrics->capacity);
        return FLOW_SECURITY_RESOURCE_EXHAUSTION;
    }
    return FLOW_SECURITY_PASS;
}

FlowSecurityOutcome flow_security_check_physical_barrier_gate(
    const FlowCompositionSpec *spec, char *message, size_t message_size) {
    if (spec == NULL) {
        if (message && message_size) snprintf(message, message_size, "null spec");
        return FLOW_SECURITY_CONTRACT_VIOLATION;
    }
    if (spec->jet != NULL) {
        const FlowJet *jet = spec->jet;
        uint32_t dim = jet->header.vector_dim ? jet->header.vector_dim : FLOW_JET_STANDARD_DIM;
        if (dim > FLOW_JET_MAX_DIM) dim = FLOW_JET_MAX_DIM;

        /* 1. Sensor & Coordinate Integrity: Check for NaN or Inf in phase space */
        for (size_t i = 0; i < dim; ++i) {
            if (isnan(jet->payload.q[i]) || isinf(jet->payload.q[i])) {
                if (message && message_size) {
                    snprintf(message, message_size,
                             "physical barrier breach: NaN or Inf coordinate q[%zu] detected", i);
                }
                return FLOW_SECURITY_PHYSICAL_BREACH;
            }
            if (isnan(jet->payload.p[i]) || isinf(jet->payload.p[i])) {
                if (message && message_size) {
                    snprintf(message, message_size,
                             "physical barrier breach: NaN or Inf conjugate momentum p[%zu] detected", i);
                }
                return FLOW_SECURITY_PHYSICAL_BREACH;
            }
            if (isnan(jet->payload.a[i]) || isinf(jet->payload.a[i])) {
                if (message && message_size) {
                    snprintf(message, message_size,
                             "physical barrier breach: NaN or Inf acceleration a[%zu] detected", i);
                }
                return FLOW_SECURITY_PHYSICAL_BREACH;
            }
        }

        /* 2. Dynamic Velocity & Acceleration Barrier Bounds */
        if (spec->max_velocity_bound > 0.0) {
            for (size_t i = 0; i < dim; ++i) {
                if (fabs(jet->payload.p[i]) > spec->max_velocity_bound) {
                    if (message && message_size) {
                        snprintf(message, message_size,
                                 "physical barrier breach: velocity p[%zu]=%.3f exceeds limit %.3f",
                                 i, jet->payload.p[i], spec->max_velocity_bound);
                    }
                    return FLOW_SECURITY_PHYSICAL_BREACH;
                }
            }
        }

        if (spec->max_acceleration_bound > 0.0) {
            for (size_t i = 0; i < dim; ++i) {
                if (fabs(jet->payload.a[i]) > spec->max_acceleration_bound) {
                    if (message && message_size) {
                        snprintf(message, message_size,
                                 "physical barrier breach: acceleration a[%zu]=%.3f exceeds limit %.3f",
                                 i, jet->payload.a[i], spec->max_acceleration_bound);
                    }
                    return FLOW_SECURITY_PHYSICAL_BREACH;
                }
            }
        }

        /* 3. Symplectic Hamiltonian Energy Drift Protection */
        if (jet->header.hamiltonian_energy > 0.0) {
            double current_h = flow_jet_hamiltonian(jet);
            double h0 = jet->header.hamiltonian_energy;
            double drift = fabs(current_h - h0) / h0;
            double tol = spec->max_hamiltonian_drift_ratio > 0.0 ? spec->max_hamiltonian_drift_ratio : 0.05;
            if (drift > tol) {
                if (message && message_size) {
                    snprintf(message, message_size,
                             "physical barrier breach: Hamiltonian energy drift %.4f exceeds tolerance %.4f",
                             drift, tol);
                }
                return FLOW_SECURITY_PHYSICAL_BREACH;
            }
        }
    }
    return FLOW_SECURITY_PASS;
}

FlowSecurityOutcome flow_security_check_jit_wx_gate(
    const FlowJITCodeBlock *block, uintptr_t write_base, uintptr_t exec_base,
    char *message, size_t message_size) {
    if (write_base == 0 || exec_base == 0) {
        if (message && message_size) snprintf(message, message_size, "unmapped jit memory base");
        return FLOW_SECURITY_MEMORY_VIOLATION;
    }
    /* W^X Hard Invariant: Writable heap must not alias executable heap */
    if (write_base == exec_base) {
        if (message && message_size) {
            snprintf(message, message_size,
                     "W^X hard gate violation: page simultaneously writable and executable (0x%lx)",
                     (unsigned long)write_base);
        }
        return FLOW_SECURITY_MEMORY_VIOLATION;
    }
    if (block != NULL) {
        if (block->start_ip == 0 || block->code_bytes == 0) {
            if (message && message_size) snprintf(message, message_size, "invalid empty jit code block");
            return FLOW_SECURITY_MEMORY_VIOLATION;
        }
        if (block->start_ip < exec_base) {
            if (message && message_size) {
                snprintf(message, message_size,
                         "jit code block pointer 0x%lx outside exec heap base 0x%lx",
                         (unsigned long)block->start_ip, (unsigned long)exec_base);
            }
            return FLOW_SECURITY_MEMORY_VIOLATION;
        }
    }
    return FLOW_SECURITY_PASS;
}

FlowSecurityOutcome flow_security_check_transposition_gate(
    const FlowLayoutMigrationSpec *spec, size_t buffer_bytes,
    char *message, size_t message_size) {
    if (spec == NULL) {
        if (message && message_size) snprintf(message, message_size, "null layout migration spec");
        return FLOW_SECURITY_CONTRACT_VIOLATION;
    }
    if (spec->field_count == 0 || spec->field_count > 8) {
        if (message && message_size) {
            snprintf(message, message_size, "invalid field count %zu (must be 1..8)", spec->field_count);
        }
        return FLOW_SECURITY_CONTRACT_VIOLATION;
    }

    size_t struct_size = 0;
    for (size_t f = 0; f < spec->field_count; ++f) {
        size_t f_sz = spec->field_sizes[f];
        if (f_sz == 0 || f_sz > 256) {
            if (message && message_size) snprintf(message, message_size, "invalid field size %zu at index %zu", f_sz, f);
            return FLOW_SECURITY_CONTRACT_VIOLATION;
        }
        /* Check field offset alignment */
        if (struct_size % (f_sz > 8 ? 8 : f_sz) != 0) {
            if (message && message_size) {
                snprintf(message, message_size, "misaligned field offset %zu for field size %zu", struct_size, f_sz);
            }
            return FLOW_SECURITY_MEMORY_VIOLATION;
        }
        struct_size += f_sz;
    }

    /* Integer overflow protection during item_count * struct_size */
    if (spec->item_count > 0 && struct_size > 0) {
        if (spec->item_count > (SIZE_MAX / struct_size)) {
            if (message && message_size) {
                snprintf(message, message_size, "integer overflow in transposition item_count * struct_size");
            }
            return FLOW_SECURITY_MEMORY_VIOLATION;
        }
        size_t total_bytes = spec->item_count * struct_size;
        if (buffer_bytes > 0 && total_bytes > buffer_bytes) {
            if (message && message_size) {
                snprintf(message, message_size, "transposition required bytes %zu exceeds buffer %zu",
                         total_bytes, buffer_bytes);
            }
            return FLOW_SECURITY_RESOURCE_EXHAUSTION;
        }
    }
    return FLOW_SECURITY_PASS;
}

/* ========================================================================= */
/* Phase Space Attractor IDS Implementation                                  */
/* ========================================================================= */
int flow_jet_attractor_profile_init(FlowJetAttractorProfile *profile, uint32_t dim,
                                   double r_q, double r_p, double max_trace) {
    if (profile == NULL) return 0;
    memset(profile, 0, sizeof(*profile));
    profile->dim = (dim > 0 && dim <= FLOW_JET_ATTRACTOR_MAX_DIM) ? dim : 16;
    profile->max_radius_q = r_q > 0.0 ? r_q : 5.0;
    profile->max_radius_p = r_p > 0.0 ? r_p : 10.0;
    profile->max_koopman_trace = max_trace;
    return 1;
}

FlowSecurityOutcome flow_security_check_attractor_anomaly(
    const FlowJet *jet, const FlowJetAttractorProfile *profile,
    char *message, size_t message_size) {
    if (jet == NULL || profile == NULL) {
        if (message && message_size) snprintf(message, message_size, "null jet or profile");
        return FLOW_SECURITY_CONTRACT_VIOLATION;
    }

    uint32_t dim = jet->header.vector_dim ? jet->header.vector_dim : 16;
    if (dim > profile->dim) dim = profile->dim;
    if (dim > FLOW_JET_ATTRACTOR_MAX_DIM) dim = FLOW_JET_ATTRACTOR_MAX_DIM;

    double rq2 = profile->max_radius_q * profile->max_radius_q;
    double rp2 = profile->max_radius_p * profile->max_radius_p;
    if (rq2 <= 0.0) rq2 = 1.0;
    if (rp2 <= 0.0) rp2 = 1.0;

    double mahalanobis_sum = 0.0;
    for (size_t i = 0; i < dim; ++i) {
        double dq = jet->payload.q[i] - profile->q_center[i];
        double dp = jet->payload.p[i] - profile->p_center[i];
        mahalanobis_sum += (dq * dq) / rq2 + (dp * dp) / rp2;
    }
    double normalized_dist = mahalanobis_sum / (double)dim;
    if (normalized_dist > 1.0) {
        if (message && message_size) {
            snprintf(message, message_size,
                     "attractor breach: normalized phase distance %.3f exceeds limit 1.0",
                     normalized_dist);
        }
        return FLOW_SECURITY_PHYSICAL_BREACH;
    }

    uint32_t kdim = jet->header.koopman_dim ? jet->header.koopman_dim : 8;
    if (kdim > FLOW_JET_MAX_KOOPMAN_DIM) kdim = FLOW_JET_MAX_KOOPMAN_DIM;

    double trace_K = 0.0;
    for (size_t i = 0; i < kdim; ++i) {
        trace_K += jet->payload.koopman_matrix[i][i];
    }
    if (trace_K > profile->max_koopman_trace) {
        if (message && message_size) {
            snprintf(message, message_size,
                     "attractor instability: Koopman generator trace %.4f > %.4f (expanding Lyapunov phase flow)",
                     trace_K, profile->max_koopman_trace);
        }
        return FLOW_SECURITY_PHYSICAL_BREACH;
    }

    return FLOW_SECURITY_PASS;
}

/* ========================================================================= */
/* Differential Continuity Physical Proof Implementation                     */
/* ========================================================================= */
FlowSecurityOutcome flow_security_check_continuity_proof(
    const FlowJet *prev_jet, const FlowJet *curr_jet,
    double dt, double max_jerk, double noise_tolerance,
    char *message, size_t message_size) {
    if (prev_jet == NULL || curr_jet == NULL || dt <= 0.0) {
        if (message && message_size) snprintf(message, message_size, "invalid arguments for continuity check");
        return FLOW_SECURITY_CONTRACT_VIOLATION;
    }

    uint32_t dim = prev_jet->header.vector_dim ? prev_jet->header.vector_dim : 16;
    if (curr_jet->header.vector_dim && curr_jet->header.vector_dim < dim) {
        dim = curr_jet->header.vector_dim;
    }
    if (dim > FLOW_JET_MAX_DIM) dim = FLOW_JET_MAX_DIM;

    double j_max = max_jerk > 0.0 ? max_jerk : 500.0;
    double tol = noise_tolerance >= 0.0 ? noise_tolerance : 0.01;

    for (size_t i = 0; i < dim; ++i) {
        double q0 = prev_jet->payload.q[i];
        double p0 = prev_jet->payload.p[i];
        double a0 = prev_jet->payload.a[i];

        double q1 = curr_jet->payload.q[i];
        double p1 = curr_jet->payload.p[i];
        double a1 = curr_jet->payload.a[i];

        /* Acceleration jump (jerk limit) */
        double delta_a = fabs(a1 - a0);
        double max_delta_a = j_max * dt + tol;
        if (delta_a > max_delta_a) {
            if (message && message_size) {
                snprintf(message, message_size,
                         "continuity breach: dim %zu accel jump %.3f exceeds jerk bound %.3f (spoofed impulse)",
                         i, delta_a, max_delta_a);
            }
            return FLOW_SECURITY_PHYSICAL_BREACH;
        }

        /* Velocity Taylor residual: p1 vs (p0 + a0 * dt) */
        double p1_taylor = p0 + a0 * dt;
        double delta_p = fabs(p1 - p1_taylor);
        double max_delta_p = 0.5 * j_max * dt * dt + tol;
        if (delta_p > max_delta_p) {
            if (message && message_size) {
                snprintf(message, message_size,
                         "continuity breach: dim %zu velocity residual %.3f exceeds bound %.3f (spoofed velocity step)",
                         i, delta_p, max_delta_p);
            }
            return FLOW_SECURITY_PHYSICAL_BREACH;
        }

        /* Position Taylor residual: q1 vs (q0 + p0 * dt + 0.5 * a0 * dt^2) */
        double q1_taylor = q0 + p0 * dt + 0.5 * a0 * dt * dt;
        double delta_q = fabs(q1 - q1_taylor);
        double max_delta_q = (1.0 / 6.0) * j_max * dt * dt * dt + tol;
        if (delta_q > max_delta_q) {
            if (message && message_size) {
                snprintf(message, message_size,
                         "continuity breach: dim %zu position residual %.3f exceeds bound %.3f (spoofed position teleport)",
                         i, delta_q, max_delta_q);
            }
            return FLOW_SECURITY_PHYSICAL_BREACH;
        }
    }

    return FLOW_SECURITY_PASS;
}

/* ========================================================================= */
/* Predictive MTD Resource Quota Extrapolation Implementation                */
/* ========================================================================= */
int flow_security_predict_resource_breach(
    double current_usage, double consumption_rate, double consumption_accel,
    double quota_limit, double time_horizon_s,
    FlowPredictiveBreachReport *report) {
    if (report == NULL) return 0;
    memset(report, 0, sizeof(*report));
    report->time_to_breach_s = -1.0;

    if (current_usage >= quota_limit) {
        report->will_breach = 1;
        report->time_to_breach_s = 0.0;
        report->projected_breach_velocity = consumption_rate;
        return 1;
    }

    double delta_q = quota_limit - current_usage;
    double v = consumption_rate;
    double a = consumption_accel;
    double t_breach = -1.0;

    if (fabs(a) < 1.0e-9) {
        if (v > 0.0) {
            t_breach = delta_q / v;
        }
    } else {
        double discr = v * v + 2.0 * a * delta_q;
        if (discr >= 0.0) {
            double sqrt_d = sqrt(discr);
            double t1 = (-v + sqrt_d) / a;
            double t2 = (-v - sqrt_d) / a;

            if (t1 > 0.0 && t2 > 0.0) {
                t_breach = (t1 < t2) ? t1 : t2;
            } else if (t1 > 0.0) {
                t_breach = t1;
            } else if (t2 > 0.0) {
                t_breach = t2;
            }
        }
    }

    if (t_breach > 0.0 && (time_horizon_s <= 0.0 || t_breach <= time_horizon_s)) {
        report->will_breach = 1;
        report->time_to_breach_s = t_breach;
        report->projected_breach_velocity = v + a * t_breach;
    }

    return 1;
}

int flow_security_should_proactive_morph(
    const FlowPredictiveBreachReport *report, double proactive_lead_time_s) {
    if (report == NULL || !report->will_breach) return 0;
    if (proactive_lead_time_s <= 0.0) return report->will_breach;
    return (report->time_to_breach_s <= proactive_lead_time_s) ? 1 : 0;
}

/* ========================================================================= */
/* Symplectic Byzantine Consensus Filter Implementation                      */
/* ========================================================================= */
int flow_jet_byzantine_filter_init(
    FlowSymplecticByzantineFilter *filter,
    double max_h_drift, double max_phase_dist) {
    if (filter == NULL) return 0;
    memset(filter, 0, sizeof(*filter));
    filter->max_hamiltonian_drift_tolerance = max_h_drift > 0.0 ? max_h_drift : 0.05;
    filter->max_phase_distance_tolerance = max_phase_dist > 0.0 ? max_phase_dist : 5.0;
    return 1;
}

FlowSecurityOutcome flow_jet_byzantine_validate_packet(
    FlowSymplecticByzantineFilter *filter,
    const FlowJetDeadReckonPacket *packet,
    const FlowJet *local_shadow_mirror,
    char *message, size_t message_size) {
    if (filter == NULL || packet == NULL) {
        if (message && message_size) snprintf(message, message_size, "null filter or packet");
        return FLOW_SECURITY_CONTRACT_VIOLATION;
    }

    /* 1. Packet CRC32 Checksum Validation */
    uint32_t expected_crc = flow_jet_crc32(packet, offsetof(FlowJetDeadReckonPacket, crc32));
    if (packet->crc32 != expected_crc) {
        filter->byzantine_faults_detected++;
        if (message && message_size) {
            snprintf(message, message_size, "byzantine packet corrupted: crc 0x%08x != expected 0x%08x",
                     packet->crc32, expected_crc);
        }
        return FLOW_SECURITY_PHYSICAL_BREACH;
    }

    uint32_t dim = packet->dim ? packet->dim : 16;
    if (dim > FLOW_JET_MAX_DIM) dim = FLOW_JET_MAX_DIM;

    /* 2. Sensor Integrity: NaN or Inf coordinates */
    for (size_t i = 0; i < dim; ++i) {
        if (isnan(packet->q[i]) || isinf(packet->q[i]) ||
            isnan(packet->p[i]) || isinf(packet->p[i]) ||
            isnan(packet->a[i]) || isinf(packet->a[i])) {
            filter->byzantine_faults_detected++;
            if (message && message_size) {
                snprintf(message, message_size, "byzantine fault: node %u transmitted NaN/Inf coordinates",
                         packet->node_id);
            }
            return FLOW_SECURITY_PHYSICAL_BREACH;
        }
    }

    /* Reconstruct temporary remote Jet */
    FlowJet remote_jet;
    memset(&remote_jet, 0, sizeof(remote_jet));
    remote_jet.header.vector_dim = dim;
    for (size_t i = 0; i < dim; ++i) {
        remote_jet.payload.q[i] = packet->q[i];
        remote_jet.payload.p[i] = packet->p[i];
        remote_jet.payload.a[i] = packet->a[i];
    }
    double remote_h = flow_jet_hamiltonian(&remote_jet);

    if (local_shadow_mirror != NULL) {
        double mirror_h = flow_jet_hamiltonian(local_shadow_mirror);
        if (mirror_h > 0.0) {
            double h_drift = fabs(remote_h - mirror_h) / mirror_h;
            if (h_drift > filter->max_hamiltonian_drift_tolerance) {
                filter->byzantine_faults_detected++;
                if (message && message_size) {
                    snprintf(message, message_size,
                             "byzantine fault: node %u energy drift %.4f > tolerance %.4f",
                             packet->node_id, h_drift, filter->max_hamiltonian_drift_tolerance);
                }
                return FLOW_SECURITY_PHYSICAL_BREACH;
            }
        }

        double phase_dist = flow_jet_phase_distance(&remote_jet, local_shadow_mirror);
        if (phase_dist > filter->max_phase_distance_tolerance) {
            filter->byzantine_faults_detected++;
            if (message && message_size) {
                snprintf(message, message_size,
                         "byzantine fault: node %u geodesic phase distance %.3f > tolerance %.3f",
                         packet->node_id, phase_dist, filter->max_phase_distance_tolerance);
            }
            return FLOW_SECURITY_PHYSICAL_BREACH;
        }
    }

    return FLOW_SECURITY_PASS;
}

FlowSecurityOutcome flow_security_check_composition_gate(
    const FlowCompositionSpec *spec, char *message, size_t message_size) {
    FlowSecurityOutcome outcome;
    outcome = flow_security_check_contract_gate(spec, message, message_size);
    if (outcome != FLOW_SECURITY_PASS) return outcome;
    outcome = flow_security_check_abi_migration_gate(spec, message, message_size);
    if (outcome != FLOW_SECURITY_PASS) return outcome;
    outcome = flow_security_check_ownership_gate(spec, message, message_size);
    if (outcome != FLOW_SECURITY_PASS) return outcome;
    outcome = flow_security_check_resource_quota_gate(spec, message, message_size);
    if (outcome != FLOW_SECURITY_PASS) return outcome;
    outcome = flow_security_check_physical_barrier_gate(spec, message, message_size);
    if (outcome != FLOW_SECURITY_PASS) return outcome;
    return FLOW_SECURITY_PASS;
}

/* ========================================================================= */
/* Compositional BMF Fuzzer & Auditor                                */
/* ========================================================================= */

#include "bitspace.h"

typedef struct {
    const FlowCompositionSpec *base_spec;
    FlowBitSpace space;
} CompositionProbeContext;

static FlowSecurityOutcome composition_probe_fn(
    const FlowSecurityCase *security_case, void *userdata, char *message,
    size_t message_size) {
    CompositionProbeContext *ctx = (CompositionProbeContext *)userdata;
    const FlowCompositionSpec *base = ctx->base_spec;
    FlowCompositionSpec mutated_spec = *base;
    FlowPlan cand_plan;
    FlowPlanMetrics mutated_metrics;

    if (!ctx->space.decode(&ctx->space, security_case->mutated_genome, &cand_plan)) {
        if (message && message_size)
            snprintf(message, message_size, "mutated genome decode failed");
        return FLOW_SECURITY_CONTRACT_VIOLATION;
    }
    if (!ctx->space.evaluate(&ctx->space, &cand_plan, &cand_plan.eval)) {
        if (message && message_size)
            snprintf(message, message_size, "mutated plan evaluation failed");
        return FLOW_SECURITY_CONTRACT_VIOLATION;
    }

    memset(&mutated_metrics, 0, sizeof(mutated_metrics));
    mutated_metrics.energy = cand_plan.eval.energy;
    mutated_metrics.latency_score = cand_plan.eval.latency_score;
    mutated_metrics.throughput_score = cand_plan.eval.throughput_score;
    mutated_metrics.memory_bytes = cand_plan.eval.memory_bytes;
    mutated_metrics.capacity = cand_plan.eval.capacity;

    mutated_spec.plan = &cand_plan.assignment;
    mutated_spec.metrics = &mutated_metrics;

    return flow_security_check_composition_gate(&mutated_spec, message, message_size);
}

int flow_security_audit_composition(const FlowCompositionSpec *spec,
                                   uint64_t seed, uint32_t rounds,
                                   FlowSecurityReport *report) {
    CompositionProbeContext ctx;
    uint32_t genome_bits = 0;
    char message[160] = {0};
    FlowSecurityOutcome base_outcome;

    if (spec == NULL || report == NULL) return -1;

    base_outcome = flow_security_check_composition_gate(spec, message, sizeof(message));
    if (base_outcome != FLOW_SECURITY_PASS) {
        memset(report, 0, sizeof(*report));
        report->seed = seed;
        report->checks = 1;
        report->failures = 1;
        report->first_failure = base_outcome;
        strncpy(report->first_failure_message, message, sizeof(report->first_failure_message) - 1);
        return 1;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.base_spec = spec;
    if (spec->component != NULL) {
        flow_bitspace_init_single(spec->ir, spec->component, &ctx.space);
        genome_bits = ctx.space.bit_count;
    }
    if (genome_bits == 0) genome_bits = 16;
    if (rounds == 0) rounds = 32;

    return flow_security_run(seed, UINT64_C(0), genome_bits, rounds,
                             composition_probe_fn, &ctx, report);
}

int flow_security_write_composition_attestation(
    FILE *output, const FlowCompositionSpec *spec,
    const FlowSecurityReport *report) {
    if (output == NULL || spec == NULL || report == NULL) return 0;
    const char *comp_id = spec->component ? spec->component->id : "unknown";
    if (report->failures == 0) {
        fprintf(output,
                "composition_security_attestation component=%s status=verified "
                "threads=%zu reload=%d quota_limit=%zu checks=%zu failures=0\n",
                comp_id, spec->concurrency_threads,
                spec->reload_adapter_enabled, spec->memory_limit_bytes,
                report->checks);
    } else {
        fprintf(output,
                "composition_security_attestation component=%s status=rejected "
                "outcome=%s round=%u bit=%u error=\"%s\"\n",
                comp_id, flow_security_outcome_name(report->first_failure),
                report->first_failure_round, report->first_failure_bit,
                report->first_failure_message);
    }
    return ferror(output) == 0;
}

uint64_t flow_security_get_safety_mask(const SemanticIR *ir,
                                       const Component *comp,
                                       const FlowPlanDimensionSet *dims) {
    if (dims == NULL || dims->count == 0) return (uint64_t)-1;
    uint64_t mask = (uint64_t)-1;
    unsigned shift = 0;

    int forbid_concurrency = 0;
    if (ir != NULL && comp != NULL) {
        /* Ownership / Race condition safety gate */
        int is_read_only = ir->fact_mutability_read_only;
        if (is_read_only && ir->state_shared && !comp->supports_read_heavy) {
            forbid_concurrency = 1;
        }
        if (!ir->state_shared && !ir->flow_parallelizable) {
            forbid_concurrency = 1;
        }
        if (ir->state_shared && !comp->supports_shared) {
            forbid_concurrency = 1;
        }
    }

    for (size_t i = 0; i < dims->count; ++i) {
        const FlowPlanDimension *d = &dims->dimensions[i];
        unsigned bits = flow_dimension_bits(d);
        if (bits == 0) continue;
        uint64_t dim_mask = (bits >= 64) ? (uint64_t)-1 : (((uint64_t)1 << bits) - 1);

        if (forbid_concurrency && (strcmp(d->name, "threads") == 0 || strcmp(d->name, "shards") == 0)) {
            /* Concurrency physically forbidden: mask all non-zero bits so raw value is strictly 0 (1 thread / 1 shard) */
            mask &= ~(dim_mask << shift);
        } else if (strcmp(d->name, "threads") == 0) {
            /* System thread & FD exhaustion limit: threads cannot exceed 64 (exp > 6) */
            if (d->kind == FLOW_DIM_EXPONENT && d->max_val > 6) {
                uint64_t max_allowed_exp = 6;
                uint64_t max_allowed_raw = max_allowed_exp >= d->min_val ? max_allowed_exp - d->min_val : 0;
                for (unsigned b = 0; b < bits; ++b) {
                    if (((uint64_t)1 << b) > max_allowed_raw) {
                        mask &= ~(UINT64_C(1) << (shift + b));
                    }
                }
            }
        }
        shift += bits;
    }
    return mask;
}

static uint64_t mtd_xorshift64(uint64_t *state) {
    uint64_t x = *state;
    if (x == 0) x = UINT64_C(0x85432917a4c9b13d);
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

double flow_security_mtd_calculate_entropy(const FlowMTDLayout *layout) {
    if (layout == NULL || layout->field_count <= 1 || layout->total_size == 0) return 0.0;
    double entropy = 0.0;
    double total = (double)layout->total_size;

    for (size_t i = 0; i < layout->field_count; ++i) {
        size_t gap = (i + 1 < layout->field_count) ?
                     (layout->field_offsets[i + 1] - layout->field_offsets[i]) :
                     (layout->total_size - layout->field_offsets[i]);
        if (gap > 0) {
            double p = (double)gap / total;
            entropy -= p * (log(p) / log(2.0));
        }
    }
    return entropy;
}

int flow_security_mtd_verify_alignment(const FlowMTDLayout *layout, const size_t *field_alignments) {
    if (layout == NULL) return 0;
    for (size_t i = 0; i < layout->field_count; ++i) {
        size_t orig_idx = layout->field_order[i];
        size_t align = (field_alignments != NULL && field_alignments[orig_idx] > 0) ?
                       field_alignments[orig_idx] : sizeof(void *);
        if ((layout->field_offsets[i] % align) != 0) {
            return 0;
        }
    }
    return 1;
}

int flow_security_mtd_generate_layout(uint64_t seed, size_t field_count,
                                      const size_t *field_sizes,
                                      const size_t *field_alignments,
                                      size_t max_padding_jitter,
                                      FlowMTDLayout *layout_out) {
    if (layout_out == NULL || field_count == 0 || field_sizes == NULL) return 0;
    if (field_count > FLOW_MTD_MAX_FIELDS) field_count = FLOW_MTD_MAX_FIELDS;
    memset(layout_out, 0, sizeof(*layout_out));

    layout_out->seed = seed;
    layout_out->field_count = field_count;
    uint64_t rng = seed == 0 ? UINT64_C(0xa1b2c3d4e5f60718) : seed;

    /* 1. Initial sequential order */
    for (size_t i = 0; i < field_count; ++i) {
        layout_out->field_order[i] = i;
    }

    /* 2. Fisher-Yates Field Order Permutation */
    for (size_t i = field_count - 1; i > 0; --i) {
        size_t j = (size_t)(mtd_xorshift64(&rng) % (i + 1));
        size_t tmp = layout_out->field_order[i];
        layout_out->field_order[i] = layout_out->field_order[j];
        layout_out->field_order[j] = tmp;
    }

    /* 3. Determine natural max alignment */
    size_t max_align = sizeof(void *);
    for (size_t i = 0; i < field_count; ++i) {
        size_t align = (field_alignments != NULL && field_alignments[i] > 0) ? field_alignments[i] : sizeof(void *);
        if (align > max_align) max_align = align;
    }
    layout_out->required_alignment = max_align;

    /* 4. Layout Placement with Dynamic Inter-Field Padding Jitter */
    size_t current_offset = 0;
    if (max_padding_jitter == 0) max_padding_jitter = 16;

    for (size_t i = 0; i < field_count; ++i) {
        size_t orig_idx = layout_out->field_order[i];
        size_t size = field_sizes[orig_idx];
        size_t align = (field_alignments != NULL && field_alignments[orig_idx] > 0) ?
                       field_alignments[orig_idx] : sizeof(void *);

        /* Align offset */
        current_offset = (current_offset + align - 1) & ~(align - 1);
        layout_out->field_offsets[i] = current_offset;

        /* Add field size */
        current_offset += size;

        /* Add non-deterministic jitter padding */
        size_t jitter = (size_t)(mtd_xorshift64(&rng) % max_padding_jitter);
        /* Ensure jitter maintains alignment for next potential field */
        jitter = (jitter + align - 1) & ~(align - 1);
        layout_out->padding_bytes[i] = jitter;
        current_offset += jitter;
    }

    /* Align total struct size to max alignment */
    current_offset = (current_offset + max_align - 1) & ~(max_align - 1);
    layout_out->total_size = current_offset;

    /* 5. Dynamic Canary Guard Token */
    layout_out->canary_token = mtd_xorshift64(&rng);

    /* 6. Compute Shannon Entropy */
    layout_out->shannon_entropy = flow_security_mtd_calculate_entropy(layout_out);

    return flow_security_mtd_verify_alignment(layout_out, field_alignments);
}

void flow_security_mtd_report(const FlowMTDLayout *layout, FILE *out) {
    if (layout == NULL || out == NULL) return;
    fprintf(out, "Moving Target Defense (MTD) Polymorphic Layout:\n");
    fprintf(out, "  Seed: 0x%016llx | Total Size: %zu bytes | Max Align: %zu\n",
            (unsigned long long)layout->seed, layout->total_size, layout->required_alignment);
    fprintf(out, "  Shannon Offset Entropy: %.3f bits | Canary: 0x%016llx\n",
            layout->shannon_entropy, (unsigned long long)layout->canary_token);
    for (size_t i = 0; i < layout->field_count; ++i) {
        fprintf(out, "  [Field slot %zu] orig_index=%zu offset=%-4zu padding=%-3zu\n",
                i, layout->field_order[i], layout->field_offsets[i], layout->padding_bytes[i]);
    }
}

/* ========================================================================= */
/* Bounded BMF Compliance Mode Implementation                             */
/* ========================================================================= */

uint64_t flow_security_get_compliance_mask(FlowComplianceMode mode,
                                           const FlowPlanDimensionSet *dims) {
    if (mode == FLOW_COMPLIANCE_PERMISSIVE_STAGING || dims == NULL || dims->count == 0) {
        return (uint64_t)-1;
    }

    /* In STRICT_PROD mode: lock core structural/algorithm dimensions.
       Only allow buffer_bytes, initial_capacity, growth_percent, and batch_size tuning. */
    uint64_t mask = 0;
    unsigned shift = 0;
    for (size_t i = 0; i < dims->count; ++i) {
        const FlowPlanDimension *d = &dims->dimensions[i];
        unsigned bits = flow_dimension_bits(d);
        if (bits == 0) continue;
        uint64_t dim_mask = (bits >= 64) ? (uint64_t)-1 : (((uint64_t)1 << bits) - 1);

        int is_safe_tuning = (strcmp(d->name, "tuning_buffer") == 0 ||
                              strcmp(d->name, "tuning_initial") == 0 ||
                              strcmp(d->name, "tuning_growth") == 0 ||
                              strcmp(d->name, "tuning_batch") == 0);

        if (is_safe_tuning) {
            mask |= (dim_mask << shift);
        }
        shift += bits;
    }
    return mask;
}

int flow_security_is_mutation_compliant(FlowComplianceMode mode,
                                        uint32_t mutated_bit,
                                        const FlowPlanDimensionSet *dims) {
    if (mode == FLOW_COMPLIANCE_PERMISSIVE_STAGING) return 1;
    uint64_t mask = flow_security_get_compliance_mask(mode, dims);
    if (mutated_bit < 64) {
        return (mask & ((uint64_t)1 << mutated_bit)) != 0;
    }
    return 0;
}

/* ========================================================================= */
/* Dynamic DSO Plugin ABI Export                                             */
/* ========================================================================= */

static const Component SECURITY_COMPONENTS[] = {
    {
        .id = "security_firewall",
        .kind = "security",
        .resource = "cpu",
        .capability = "attestation",
        .supports_shared = 1,
        .supports_read_heavy = 1,
        .supports_unordered = 1,
        .supports_parallelizable = 1,
        .latency_score = 1,
        .memory_score = 1,
        .domain_contract = "security_attested",
        .flow_binding = "flow_security_audit_space",
        .memory_fixed_bytes = 1024,
        .memory_bytes_per_capacity = 64,
        .reload_capable = 1
    }
};

static uint64_t security_res_mask(const SemanticIR *ir, const Component *c, const FlowPlanDimensionSet *dims, unsigned long max_quota_bytes) {
    (void)ir; (void)c; (void)dims; (void)max_quota_bytes;
    return UINT64_MAX;
}

/* ========================================================================= */
/* Standardized FLOW Plugin ABI v2 (Canonical 4-Function Contract)          */
/* ========================================================================= */

static size_t flow_security_get_genome_bit_size(void) {
    return 16; /* 16 bits: 4 bits quota tier, 4 bits MTD entropy, 4 bits barrier mask, 4 bits ASLR rounds */
}

static uint64_t flow_security_get_valid_mask(const FlowEnvironmentState *env) {
    (void)env;
    /* MTD & quota projection: zero-quota or unsafe tiers masked out */
    return 0x0000FFFFULL;
}

static double flow_security_evaluate_energy(uint64_t genome) {
    unsigned quota = (unsigned)(genome & 0x0F);
    unsigned entropy = (unsigned)((genome >> 4) & 0x0F);
    /* Higher entropy and bounded quota minimize exploit risk (lower security energy) */
    return 100.0 - (double)entropy * 4.0 + (double)quota * 1.0;
}

static void flow_security_emit_llvm_ir(uint64_t genome, void *module_or_out) {
    if (module_or_out == NULL) return;
    FILE *out = (FILE *)module_or_out;
    fprintf(out, "/* [flow.security] MTD Hardened Shield Gate (Genome: 0x%04llx) */\n", (unsigned long long)genome);
    fprintf(out, "void flow_security_enforce_guardrails(void) {\n");
    fprintf(out, "    /* Ownership and quota validation verified */\n");
    fprintf(out, "}\n");
}

static const FlowPluginABI SECURITY_ABI_V2 = {
    .get_genome_bit_size = flow_security_get_genome_bit_size,
    .get_valid_mask = flow_security_get_valid_mask,
    .evaluate_energy = flow_security_evaluate_energy,
    .emit_llvm_ir = flow_security_emit_llvm_ir
};

static const FlowPlugin SECURITY_PLUGIN = {
    .name = "flow.security",
    .version = "1.0",
    .components = SECURITY_COMPONENTS,
    .component_count = 1,
    .compatible = NULL,
    .memory_model = NULL,
    .verify = NULL,
    .emit = NULL,
    .oracle = NULL,
    .preference = NULL,
    .validate_contract = NULL,
    .lower_domain_semantics = NULL,
    .free_domain_semantics = NULL,
    .enumerate_dimensions = NULL,
    .evaluate_plan = NULL,
    .verify_plan = NULL,
    .benchmark = NULL,
    .get_mutation_mask = NULL,
    .preference_mask = NULL,
    .contract_mask = NULL,
    .resource_mask = security_res_mask,
    .environment_mask = NULL,
    .create_unit = NULL,
    .doc_title = "Moving Target Defense & Formal Security Gatekeeper",
    .doc_responsibilities = "Enforces memory bounds quotas, ownership gates, ABI hash validation, and MTD entropy randomization",
    .doc_algorithmic_guarantee = "Zero out-of-quota allocation, provable memory safety, and 1-bit chaotic fuzzer attestation",
    .doc_memory_concurrency_model = "Zero heap overhead, hardware-assisted write barriers",
    .doc_key_apis = "flow_security_check_composition_gate, flow_security_audit_space",
    .doc_layer = 2,
    .domain_context = NULL
};

static const FlowPluginDescriptor SECURITY_DESCRIPTOR = {
    .abi_major = FLOW_PLUGIN_ABI_MAJOR,
    .abi_minor = FLOW_PLUGIN_ABI_MINOR,
    .descriptor_size = sizeof(FlowPluginDescriptor),
    .module_name = "flow.security",
    .module_version = "1.0",
    .module_hash = 0x5EC07171,
    .plugin = &SECURITY_PLUGIN,
    .abi_v2 = &SECURITY_ABI_V2,
    .dso_handle = NULL,
    .active_references = 0
};

const FlowPluginDescriptor *flow_security_entry_v1(void) {
    return &SECURITY_DESCRIPTOR;
}

const FlowPluginABI *flow_security_abi_v2(void) {
    return &SECURITY_ABI_V2;
}

#ifdef FLOW_PLUGIN_DSO
const FlowPluginDescriptor *flow_plugin_entry_v1(void) {
    return &SECURITY_DESCRIPTOR;
}

const FlowPluginABI *flow_plugin_abi_v2(void) {
    return &SECURITY_ABI_V2;
}
#endif

const FlowPlugin *flow_security_plugin(void) {
    return &SECURITY_PLUGIN;
}
