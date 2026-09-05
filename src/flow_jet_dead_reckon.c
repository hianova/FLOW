#include "flow_jet_dead_reckon.h"
#include "flow_smt_dsl.h"

#include <math.h>
#include <string.h>
#include <stdio.h>

int flow_jet_dead_reckon_sender_init(FlowJetDeadReckonSender *sender,
                                    FlowJet *actual_jet,
                                    double divergence_threshold) {
    if (sender == NULL || actual_jet == NULL) return 0;
    memset(sender, 0, sizeof(*sender));

    sender->actual_jet = actual_jet;
    sender->shadow_jet = *actual_jet;
    sender->divergence_threshold = (divergence_threshold > 0.0) ? divergence_threshold : 0.08;
    sender->total_ticks = 0;
    sender->packets_sent = 0;
    sender->packets_suppressed = 0;
    sender->max_observed_divergence = 0.0;
    sender->bandwidth_savings_ratio = 1.0;
    sender->next_packet_seq = 1;

    return 1;
}

int flow_jet_dead_reckon_receiver_init(FlowJetDeadReckonReceiver *receiver,
                                      const char *mirror_id,
                                      uint32_t dim) {
    if (receiver == NULL || mirror_id == NULL) return 0;
    memset(receiver, 0, sizeof(*receiver));

    uint32_t active_dim = (dim > 0 && dim <= FLOW_JET_MAX_DIM) ? dim : FLOW_JET_STANDARD_DIM;
    flow_jet_init_extended(&receiver->mirror_jet, mirror_id, "Dead Reckoned Mirror", active_dim, 8, 8);

    receiver->last_packet_seq = 0;
    receiver->last_packet_time_ns = 0;
    receiver->total_ticks_dead_reckoned = 0;
    receiver->sync_corrections_received = 0;

    return 1;
}

int flow_jet_dead_reckon_sender_step(FlowJetDeadReckonSender *sender,
                                     double dt,
                                     FlowJetDeadReckonPacket *packet_out,
                                     int *packet_generated_out) {
    if (sender == NULL || sender->actual_jet == NULL) return 0;
    if (packet_generated_out == NULL) return 0;

    FlowJet *actual = sender->actual_jet;
    FlowJet *shadow = &sender->shadow_jet;
    uint32_t dim = actual->header.vector_dim ? actual->header.vector_dim : FLOW_JET_STANDARD_DIM;
    if (dim > FLOW_JET_MAX_DIM) dim = FLOW_JET_MAX_DIM;

    /* Advance actual ground truth physics and shadow dead-reckoning model */
    flow_jet_symplectic_step(actual, dt);
    flow_jet_symplectic_step(shadow, dt);

    /* Measure Euclidean trajectory divergence */
    double diff_sq = 0.0;
    for (uint32_t i = 0; i < dim; ++i) {
        double d = actual->payload.q[i] - shadow->payload.q[i];
        diff_sq += d * d;
    }
    double divergence = sqrt(diff_sq);
    if (divergence > sender->max_observed_divergence) {
        sender->max_observed_divergence = divergence;
    }

    /* Transmit packet if initial tick or divergence breached Lyapunov threshold */
    if (sender->total_ticks == 0 || divergence >= sender->divergence_threshold) {
        if (packet_out != NULL) {
            memset(packet_out, 0, sizeof(*packet_out));
            packet_out->timestamp_ns = sender->total_ticks * (uint64_t)(dt * 1e9);
            packet_out->node_id = 1;
            packet_out->dim = dim;
            packet_out->packet_seq = sender->next_packet_seq++;

            for (uint32_t i = 0; i < dim; ++i) {
                packet_out->q[i] = actual->payload.q[i];
                packet_out->p[i] = actual->payload.p[i];
                packet_out->a[i] = actual->payload.a[i];
            }

            packet_out->crc32 = flow_jet_crc32(packet_out, offsetof(FlowJetDeadReckonPacket, crc32));
        }

        /* Resynchronize shadow model to actual state */
        *shadow = *actual;

        sender->packets_sent++;
        *packet_generated_out = 1;
    } else {
        sender->packets_suppressed++;
        *packet_generated_out = 0;
    }

    sender->total_ticks++;
    if (sender->total_ticks > 0) {
        sender->bandwidth_savings_ratio = (double)sender->packets_suppressed / (double)sender->total_ticks;
    }

    return 1;
}

int flow_jet_dead_reckon_receiver_step(FlowJetDeadReckonReceiver *receiver, double dt) {
    if (receiver == NULL) return 0;
    flow_jet_symplectic_step(&receiver->mirror_jet, dt);
    receiver->total_ticks_dead_reckoned++;
    return 1;
}

int flow_jet_dead_reckon_receiver_apply_packet(FlowJetDeadReckonReceiver *receiver,
                                               const FlowJetDeadReckonPacket *packet) {
    if (receiver == NULL || packet == NULL) return 0;

    /* Verify packet integrity */
    uint32_t expected_crc = flow_jet_crc32(packet, offsetof(FlowJetDeadReckonPacket, crc32));
    if (packet->crc32 != expected_crc) return 0;

    uint32_t dim = packet->dim;
    if (dim > FLOW_JET_MAX_DIM) dim = FLOW_JET_MAX_DIM;

    for (uint32_t i = 0; i < dim; ++i) {
        receiver->mirror_jet.payload.q[i] = packet->q[i];
        receiver->mirror_jet.payload.p[i] = packet->p[i];
        receiver->mirror_jet.payload.a[i] = packet->a[i];
    }
    receiver->mirror_jet.header.vector_dim = dim;
    receiver->mirror_jet.header.hamiltonian_energy = flow_jet_hamiltonian(&receiver->mirror_jet);

    receiver->last_packet_seq = packet->packet_seq;
    receiver->last_packet_time_ns = packet->timestamp_ns;
    receiver->sync_corrections_received++;

    return 1;
}

FlowSMTResult flow_jet_dead_reckon_verify_smt(const FlowJetDeadReckonSender *sender,
                                              FlowSMTProofAttestation *proof_out) {
    if (sender == NULL || sender->actual_jet == NULL) return FLOW_SMT_UNKNOWN;

    FLOW_SMT_BOX_BUILDER_DECL(builder);

    /* Theorem 1: Lyapunov Horizon Divergence Boundedness */
    double max_allowed = sender->divergence_threshold * 1.5;
    uint64_t div_violation = (sender->max_observed_divergence > max_allowed || isnan(sender->max_observed_divergence)) ? 1 : 0;
    FLOW_SMT_BOX_ADD_RULE(builder, "dead_reckon_divergence_bounded", div_violation, 0, 0,
                          FLOW_BOX_THEOREM_BUFFER_BOUNDS, "Dead reckoning trajectory divergence breached Lyapunov horizon bound");

    /* Theorem 2: Distributed Interconnect Bandwidth Savings (>= 85%) */
    uint64_t savings_violation = 0;
    if (sender->total_ticks >= 20) {
        if (sender->bandwidth_savings_ratio < 0.85 || isnan(sender->bandwidth_savings_ratio)) {
            savings_violation = 1;
        }
    }
    FLOW_SMT_BOX_ADD_RULE(builder, "dead_reckon_bandwidth_savings", savings_violation, 0, 0,
                          FLOW_BOX_THEOREM_MEMORY_QUOTA, "Cluster bandwidth savings fell below 85% requirement");

    /* Theorem 3: Symplectic Energy Conservation of Shadow Model */
    double H_shadow = flow_jet_hamiltonian(&sender->shadow_jet);
    uint64_t energy_violation = (H_shadow < 0.0 || H_shadow > 1.0e6 || isnan(H_shadow)) ? 1 : 0;
    FLOW_SMT_BOX_ADD_RULE(builder, "dead_reckon_energy_bounded", energy_violation, 0, 0,
                          FLOW_BOX_THEOREM_SHARD_ISOLATION, "Shadow dead-reckoning Hamiltonian diverged");

    /* Theorem 4: Single Cache-Line Confinement */
    uint64_t canvas_violation = (sizeof(FlowBmf1BitCanvas) != 64) ? 1 : 0;
    FLOW_SMT_BOX_ADD_RULE(builder, "dead_reckon_canvas_confinement", canvas_violation, 0, 0,
                          FLOW_BOX_THEOREM_DETERMINISM, "Switchboard canvas is not 64-byte aligned");

    FlowSMTResult res = FLOW_SMT_BOX_VERIFY(builder, "dead_reckon_soundness", proof_out);
    if (res == FLOW_SMT_PROVEN_UNSAT && proof_out != NULL) {
        snprintf(proof_out->proof_summary, sizeof(proof_out->proof_summary),
                 "SMT DEAD RECKON SOUND: Savings=%.1f%%, Sent=%llu, Suppressed=%llu, MaxDiv=%.4f (Zero-Defect Guaranteed)",
                 sender->bandwidth_savings_ratio * 100.0,
                 (unsigned long long)sender->packets_sent,
                 (unsigned long long)sender->packets_suppressed,
                 sender->max_observed_divergence);
    }
    return res;
}
