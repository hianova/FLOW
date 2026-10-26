#ifndef FLOW_JET_H
#define FLOW_JET_H
#include "flow.h"
#include "bitspace.h"
#include "cubical_hott.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
#define FLOW_JET_STANDARD_DIM 16
#define FLOW_JET_DIM 16
#define FLOW_JET_MAX_DIM 64
#define FLOW_JET_STANDARD_KOOPMAN_DIM 8
#define FLOW_JET_KOOPMAN_DIM 8
#define FLOW_JET_MAX_KOOPMAN_DIM 16
#define FLOW_JET_STANDARD_TAPS 8
#define FLOW_JET_MEMORY_KERNEL_TAPS 8
#define FLOW_JET_MAX_TAPS 16
#define FLOW_FJET_MAGIC "FJET_V1"
#define FLOW_FJET_HEADER_SIZE 2048
#define FLOW_FJET_DEFAULT_DIR ".flow/jets"
typedef struct { char magic[16], id[64], name[128], origin_hardware[128], trigger_intent[64], category[32], component_id[32], description[256], smt_signature[64];
    double hamiltonian_energy; uint64_t created_at_unix, last_reinforced_unix; uint32_t vector_dim, payload_size, confidence_score;
    uint8_t is_auto_promoted, koopman_dim, memory_taps, reserved_flags; char content_hash[32], filepath[256]; } FlowJetHeader;
_Static_assert(sizeof(FlowJetHeader) <= FLOW_FJET_HEADER_SIZE - sizeof(uint32_t), "FlowJetHeader must fit in FLOW_FJET_HEADER_SIZE with CRC32 trailer");
typedef struct { union {
        FlowUnifiedSection section; struct { double q[FLOW_JET_MAX_DIM], p[FLOW_JET_MAX_DIM], a[FLOW_JET_MAX_DIM]; double s, transversality_margin;
            int64_t lattice_idx[FLOW_AXIOM_LATTICE_DIM]; uint32_t optimal_tile_size, optimal_simd_width; uint64_t total_lattice_points, bmf_subspace_mask;
            FlowBmf1BitCanvas staged_canvas; double curvature_spectrum[FLOW_AXIOM_DIM]; FlowThermalState thermal; FlowSMTProofAttestation proof; }; };
    double memory_kernel[FLOW_JET_MAX_TAPS]; double koopman_matrix[FLOW_JET_MAX_KOOPMAN_DIM][FLOW_JET_MAX_KOOPMAN_DIM];
    uint64_t pure_genome, hard_composite_mask, soft_composite_bias; uint32_t crc32; } FlowJetPayload; typedef struct FlowJet { FlowJetHeader header; FlowJetPayload payload;
    double memory_integral[FLOW_JET_MAX_DIM]; uint64_t last_update_ns; } FlowJet; typedef struct { double P[FLOW_JET_MAX_KOOPMAN_DIM][FLOW_JET_MAX_KOOPMAN_DIM];
    double A[FLOW_JET_MAX_KOOPMAN_DIM][FLOW_JET_MAX_KOOPMAN_DIM]; double K[FLOW_JET_MAX_KOOPMAN_DIM][FLOW_JET_MAX_KOOPMAN_DIM]; double forgetting_factor; uint32_t dim;
    uint64_t update_count; } FlowJetStreamingEDMD; int flow_jet_edmd_init(FlowJetStreamingEDMD *edmd, uint32_t dim, double forgetting_factor);
int flow_jet_edmd_update(FlowJetStreamingEDMD *edmd, const double x_curr[], const double x_next[], double dt);
int flow_jet_apply_learned_koopman(FlowJet *jet, const FlowJetStreamingEDMD *edmd);
int flow_jet_stream_learn_step(FlowJet *jet, FlowJetStreamingEDMD *edmd, const double pmu_obs[], double dt); typedef struct FlowJetPotentialLandscape { uint32_t dim;
    double q_equilibrium[FLOW_JET_MAX_DIM], omega[FLOW_JET_MAX_DIM], q_saturation[FLOW_JET_MAX_DIM]; double barrier_mu;
    double moreau_low[FLOW_JET_MAX_DIM], moreau_high[FLOW_JET_MAX_DIM], moreau_kappa; } FlowJetPotentialLandscape;
int flow_jet_potential_init_default(FlowJetPotentialLandscape *landscape, uint32_t dim);
int flow_jet_potential_eval_gradient(const FlowJetPotentialLandscape *landscape, const double q[], double grad_out[]);
int flow_jet_symplectic_step_with_potential(FlowJet *jet, const FlowJetPotentialLandscape *landscape, double dt);
#define FLOW_THERMAL_DEFAULT_TEMP_C     45.0
#define FLOW_THERMAL_DEFAULT_AMBIENT_C  50.0
#define FLOW_THERMAL_DEFAULT_R_TH       6.0
#define FLOW_THERMAL_DEFAULT_C_TH       0.05
#define FLOW_THERMAL_DEFAULT_TARGET_C   85.0
#define FLOW_THERMAL_DEFAULT_THROTTLE_C 95.0
#define FLOW_THERMAL_DEFAULT_POWER_W    5.0

#define FLOW_JET_MZ_KERNEL_DECAY        0.4
#define FLOW_JET_MZ_STEP_DECAY          0.5
#define FLOW_JET_EDMD_DEFAULT_LAMBDA    0.98

void flow_verlet_step_harmonic(size_t dim, double *q, double *p, double *a, const double *omega_spectrum, double dt);
int flow_thermal_state_step(FlowThermalState *th, double p_active_watts, double dt_sec);
int flow_jet_init(FlowJet *jet, const char *id, const char *name);
int flow_jet_init_extended(FlowJet *jet, const char *id, const char *name, uint32_t dim, uint32_t koopman_dim, uint32_t taps); double flow_jet_hamiltonian(const FlowJet *jet);
int flow_jet_symplectic_step(FlowJet *jet, double dt); void flow_jet_thermal_init_default(FlowThermalState *th);
int flow_jet_thermal_step(FlowJet *jet, double p_active_watts, double dt_sec); double flow_jet_thermal_predict_horizon(const FlowJet *jet, double horizon_sec);
double flow_jet_thermal_prune_factor(const FlowJet *jet, double horizon_sec); int flow_jet_contact_step(FlowJet *jet, double dt, double p_active_watts);
int flow_jet_koopman_predict(const FlowJet *jet, double dt, double *observable_out); int flow_jet_mori_zwanzig_step(FlowJet *jet, double dt);
double flow_jet_phase_distance(const FlowJet *a, const FlowJet *b); int flow_jet_clamp_to_safety_envelope(FlowJet *jet, double max_q, double max_p, double max_a);
uint32_t flow_jet_crc32(const void *data, size_t length); int flow_jet_write_file(const FlowJet *jet, const char *filepath);
int flow_jet_read_file(const char *filepath, FlowJet *jet_out); int flow_jet_from_fvec(const FlowVecHeader *hdr, const FlowVecPayload *payload, FlowJet *jet_out);
int flow_jet_to_fvec(const FlowJet *jet, FlowVecHeader *hdr_out, FlowVecPayload *payload_out);
FlowSMTResult flow_jet_verify_symplectic_soundness_smt(const FlowJet *jet, FlowSMTProofAttestation *proof_out);
#define FLOW_JET_DEFINE_NAMED_VIEW(ViewName, CoordStruct) \
    _Static_assert(sizeof(CoordStruct) <= sizeof(double) * FLOW_JET_MAX_DIM, \
                   #CoordStruct " exceeds FlowJet phase-space dimension capacity"); \
    typedef struct { CoordStruct *q; CoordStruct *p; CoordStruct *a; } ViewName; \
    static inline __attribute__((unused)) ViewName ViewName##_bind(FlowJet *jet) { \
        ViewName v = { \
            .q = jet ? (CoordStruct *)(void *)jet->payload.q : NULL, \
            .p = jet ? (CoordStruct *)(void *)jet->payload.p : NULL, \
            .a = jet ? (CoordStruct *)(void *)jet->payload.a : NULL \
        }; \
        return v; \
    }
static inline int flow_jet_sanitize_finite(FlowJet *jet) {
    if (!jet) return 0;
    for (size_t i = 0; i < FLOW_JET_MAX_DIM; ++i) {
        if (!__builtin_isfinite(jet->payload.q[i])) jet->payload.q[i] = 0.0;
        if (!__builtin_isfinite(jet->payload.p[i])) jet->payload.p[i] = 0.0;
        if (!__builtin_isfinite(jet->payload.a[i])) jet->payload.a[i] = 0.0;
        if (!__builtin_isfinite(jet->memory_integral[i])) jet->memory_integral[i] = 0.0;
    }
    if (!__builtin_isfinite(jet->payload.s)) jet->payload.s = 0.0;
    if (!__builtin_isfinite(jet->payload.transversality_margin)) jet->payload.transversality_margin = 0.0;
    if (!__builtin_isfinite(jet->header.hamiltonian_energy)) jet->header.hamiltonian_energy = 0.0;
    return 1;
}
typedef struct { double threshold_low;
    double threshold_high;
    double current_value;
    int discrete_state;
    uint64_t total_updates; uint64_t state_transitions; uint64_t flutters_suppressed;
} FlowMoreauHysteresis;
int flow_moreau_init(FlowMoreauHysteresis *hys, double x_low, double x_high, int initial_state);
int flow_moreau_step(FlowMoreauHysteresis *hys, double input_signal);
FlowSMTResult flow_moreau_verify_smt(const FlowMoreauHysteresis *hys, FlowSMTProofAttestation *proof_out);
#ifdef __cplusplus
}
#endif
#endif /* FLOW_JET_H */
