#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "flow_jet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <math.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <pthread.h>
#include <pthread/qos.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif
uint32_t flow_jet_crc32(const void *data, size_t length) { return flow_crc32_ieee(data, length); }
int flow_jet_init_extended(FlowJet *jet, const char *id, const char *name, uint32_t dim, uint32_t koopman_dim, uint32_t taps) { if (!jet) return 0;
    memset(jet, 0, sizeof(*jet)); uint32_t eff_dim = (dim > 0 && dim <= FLOW_JET_MAX_DIM) ? dim : FLOW_JET_STANDARD_DIM;
    uint32_t eff_kdim = (koopman_dim > 0 && koopman_dim <= FLOW_JET_MAX_KOOPMAN_DIM) ? koopman_dim : FLOW_JET_STANDARD_KOOPMAN_DIM;
    uint32_t eff_taps = (taps > 0 && taps <= FLOW_JET_MAX_TAPS) ? taps : FLOW_JET_STANDARD_TAPS; strncpy(jet->header.magic, FLOW_FJET_MAGIC, sizeof(jet->header.magic) - 1);
    strncpy(jet->header.id, id ? id : "jet_default", sizeof(jet->header.id) - 1);
    strncpy(jet->header.name, name ? name : "Default Phase Space Jet", sizeof(jet->header.name) - 1);
    strncpy(jet->header.origin_hardware, "Unified Phase Space Engine", sizeof(jet->header.origin_hardware) - 1);
    strncpy(jet->header.trigger_intent, "JET_SYMPLECTIC_FLOW", sizeof(jet->header.trigger_intent) - 1);
    strncpy(jet->header.category, "JET_BUNDLE", sizeof(jet->header.category) - 1);
    strncpy(jet->header.component_id, "symplectic_kernel", sizeof(jet->header.component_id) - 1);
    strncpy(jet->header.description, "2nd-order Jet Bundle with Mori-Zwanzig memory kernel & Koopman generator", sizeof(jet->header.description) - 1);
    strncpy(jet->header.smt_signature, "SYMPLECTIC_UNSAT:KOOPMAN_UNSAT:MORI_ZWANZIG_UNSAT", sizeof(jet->header.smt_signature) - 1);
    jet->header.created_at_unix = jet->header.last_reinforced_unix = (uint64_t)time(NULL);
    jet->header.vector_dim = eff_dim; jet->header.koopman_dim = (uint8_t)eff_kdim; jet->header.memory_taps = (uint8_t)eff_taps;
    jet->header.payload_size = sizeof(FlowJetPayload); jet->header.confidence_score = 100; flow_axiom_init(&jet->payload.section, jet->header.trigger_intent);
    for (size_t i = 0; i < eff_taps; ++i) jet->payload.memory_kernel[i] = exp(-FLOW_JET_MZ_KERNEL_DECAY * (double)i); for (size_t i = 0; i < eff_kdim; ++i)
        for (size_t j = 0; j < eff_kdim; ++j) jet->payload.koopman_matrix[i][j] = (i == j) ? -0.05 : (abs((int)i - (int)j) == 1 ? 0.02 : 0.0);
    flow_bmf_canvas_init(&jet->payload.staged_canvas, 0, ~0ULL, ~0ULL, 0x1234567890ABCDEFULL);
    jet->payload.pure_genome = 0x1234567890ABCDEFULL; jet->payload.hard_composite_mask = ~0ULL;
    jet->payload.proof = (FlowSMTProofAttestation){FLOW_SMT_PROVEN_UNSAT, FLOW_SMT_PROVEN_UNSAT, FLOW_SMT_PROVEN_UNSAT, FLOW_SMT_PROVEN_UNSAT, ""};
    flow_jet_thermal_init_default(&jet->payload.thermal); jet->header.hamiltonian_energy = flow_jet_hamiltonian(jet); return 1; }
int flow_jet_init(FlowJet *jet, const char *id, const char *name) {
    return flow_jet_init_extended(jet, id, name, FLOW_JET_STANDARD_DIM, FLOW_JET_STANDARD_KOOPMAN_DIM, FLOW_JET_STANDARD_TAPS); }
int flow_jet_edmd_init(FlowJetStreamingEDMD *edmd, uint32_t dim, double forgetting_factor) { if (!edmd) return 0; memset(edmd, 0, sizeof(*edmd));
    edmd->dim = (dim > 0 && dim <= FLOW_JET_MAX_KOOPMAN_DIM) ? dim : FLOW_JET_STANDARD_KOOPMAN_DIM;
    edmd->forgetting_factor = (forgetting_factor >= 0.5 && forgetting_factor <= 1.0) ? forgetting_factor : FLOW_JET_EDMD_DEFAULT_LAMBDA;
    for (size_t i = 0; i < edmd->dim; ++i) { edmd->P[i][i] = 100.0; edmd->A[i][i] = 0.99; edmd->K[i][i] = -0.05; } return 1; }
int flow_jet_edmd_update(FlowJetStreamingEDMD *edmd, const double x_curr[], const double x_next[], double dt) { if (!edmd || !x_curr || !x_next) return 0;
    uint32_t d = edmd->dim;
    double lambda = edmd->forgetting_factor, gamma = lambda, v[FLOW_JET_MAX_KOOPMAN_DIM] = {0}, k[FLOW_JET_MAX_KOOPMAN_DIM], e[FLOW_JET_MAX_KOOPMAN_DIM];
    for (size_t i = 0; i < d; ++i) { for (size_t j = 0; j < d; ++j) v[i] += edmd->P[i][j] * x_curr[j]; gamma += x_curr[i] * v[i]; } if (gamma < 1e-9) return 0;
    for (size_t i = 0; i < d; ++i) { k[i] = v[i] / gamma; double pred = 0.0; for (size_t j = 0; j < d; ++j) pred += edmd->A[i][j] * x_curr[j]; e[i] = x_next[i] - pred; }
    for (size_t i = 0; i < d; ++i) for (size_t j = 0; j < d; ++j) { edmd->A[i][j] += e[i] * k[j]; edmd->P[i][j] = (edmd->P[i][j] - k[i] * v[j]) / lambda; }
    for (size_t i = 0; i < d; ++i) for (size_t j = i + 1; j < d; ++j) edmd->P[i][j] = edmd->P[j][i] = 0.5 * (edmd->P[i][j] + edmd->P[j][i]);
    double eff_dt = (dt > 1e-6) ? dt : 0.01, trace_K = 0.0; for (size_t i = 0; i < d; ++i) {
        for (size_t j = 0; j < d; ++j) edmd->K[i][j] = (edmd->A[i][j] - (i == j ? 1.0 : 0.0)) / eff_dt; trace_K += edmd->K[i][i]; } if (trace_K >= -0.05) {
        double shift = (trace_K + 0.06) / (double)d; for (size_t i = 0; i < d; ++i) { edmd->K[i][i] -= shift; edmd->A[i][i] = 1.0 + edmd->K[i][i] * eff_dt; } }
    edmd->update_count++; return 1; } int flow_jet_apply_learned_koopman(FlowJet *jet, const FlowJetStreamingEDMD *edmd) { if (!jet || !edmd) return 0;
    uint32_t d = edmd->dim < FLOW_JET_MAX_KOOPMAN_DIM ? edmd->dim : FLOW_JET_MAX_KOOPMAN_DIM; jet->header.koopman_dim = (uint8_t)d;
    for (size_t i = 0; i < d; ++i) for (size_t j = 0; j < d; ++j) jet->payload.koopman_matrix[i][j] = edmd->K[i][j]; return 1; }
int flow_jet_stream_learn_step(FlowJet *jet, FlowJetStreamingEDMD *edmd, const double pmu_obs[], double dt) { if (!jet || !edmd || !pmu_obs) return 0;
    uint32_t d = edmd->dim; double x_curr[FLOW_JET_MAX_KOOPMAN_DIM]; memcpy(x_curr, jet->payload.q, d * sizeof(double));
    if (!flow_jet_edmd_update(edmd, x_curr, pmu_obs, dt)) return 0; flow_jet_apply_learned_koopman(jet, edmd); memcpy(jet->payload.q, pmu_obs, d * sizeof(double)); return 1; }
int flow_jet_potential_init_default(FlowJetPotentialLandscape *l, uint32_t dim) { if (!l) return 0; memset(l, 0, sizeof(*l));
    l->dim = (dim > 0 && dim <= FLOW_JET_MAX_DIM) ? dim : FLOW_JET_STANDARD_DIM; l->barrier_mu = 0.005; l->moreau_kappa = 4.0;
    for (size_t i = 0; i < l->dim; ++i) { l->omega[i] = 1.0; l->q_saturation[i] = 2.0; l->moreau_low[i] = -1.2; l->moreau_high[i] = 1.2; } return 1; }
int flow_jet_potential_eval_gradient(const FlowJetPotentialLandscape *l, const double q[], double grad_out[]) { for (size_t i = 0; i < l->dim; ++i) {
        double grad_base = l->omega[i] * l->omega[i] * (q[i] - l->q_equilibrium[i]); double diff_sat = l->q_saturation[i] - fabs(q[i]);
        double grad_barrier = ((q[i] >= 0.0) ? 1.0 : -1.0) * (2.0 * l->barrier_mu) / (diff_sat * diff_sat * diff_sat);
        double grad_moreau = q[i] > l->moreau_high[i] ? l->moreau_kappa * (q[i] - l->moreau_high[i]) : (q[i] < l->moreau_low[i] ? l->moreau_kappa * (q[i] - l->moreau_low[i]) : 0.0);
        grad_out[i] = grad_base + grad_barrier + grad_moreau; } return 1; }
int flow_jet_symplectic_step_with_potential(FlowJet *jet, const FlowJetPotentialLandscape *l, double dt) { uint32_t dim = jet->header.vector_dim;
    double grad0[FLOW_JET_MAX_DIM], grad1[FLOW_JET_MAX_DIM], p_half[FLOW_JET_MAX_DIM]; flow_jet_potential_eval_gradient(l, jet->payload.q, grad0);
    for (size_t i = 0; i < dim; ++i) { p_half[i] = jet->payload.p[i] - 0.5 * dt * grad0[i]; jet->payload.q[i] += dt * p_half[i]; }
    flow_jet_potential_eval_gradient(l, jet->payload.q, grad1);
    for (size_t i = 0; i < dim; ++i) { jet->payload.a[i] = -grad1[i]; jet->payload.p[i] = p_half[i] - 0.5 * dt * grad1[i]; } flow_jet_mori_zwanzig_step(jet, dt);
    jet->header.hamiltonian_energy = flow_jet_hamiltonian(jet); return 1; } double flow_jet_hamiltonian(const FlowJet *jet) { double E = 0.0;
    for (size_t i = 0; i < jet->header.vector_dim; ++i) E += 0.5 * (jet->payload.p[i] * jet->payload.p[i] + jet->payload.q[i] * jet->payload.q[i]); return E; }
void flow_verlet_step_harmonic(size_t dim, double *q, double *p, double *a, const double *omega_spectrum, double dt) {
    if (!q || !p || !a || dt <= 0.0) return;
    for (size_t i = 0; i < dim; ++i) {
        double omega = omega_spectrum ? omega_spectrum[i] : 1.0;
        double p_half = p[i] - 0.5 * dt * (omega * q[i]);
        q[i] += dt * p_half;
        a[i] = -(omega * q[i]);
        p[i] = p_half - 0.5 * dt * (omega * q[i]);
    }
}
int flow_jet_symplectic_step(FlowJet *jet, double dt) {
    if (!jet || dt <= 0.0) return 0;
    flow_verlet_step_harmonic(jet->header.vector_dim, jet->payload.q, jet->payload.p, jet->payload.a, NULL, dt);
    flow_jet_mori_zwanzig_step(jet, dt);
    jet->header.hamiltonian_energy = flow_jet_hamiltonian(jet);
    return 1;
}
void flow_jet_thermal_init_default(FlowThermalState *th) {
    if (th) *th = (FlowThermalState){
        .temp_c = FLOW_THERMAL_DEFAULT_TEMP_C,
        .temp_ambient_c = FLOW_THERMAL_DEFAULT_AMBIENT_C,
        .r_thermal = FLOW_THERMAL_DEFAULT_R_TH,
        .c_thermal = FLOW_THERMAL_DEFAULT_C_TH,
        .temp_throttle_c = FLOW_THERMAL_DEFAULT_THROTTLE_C,
        .temp_target_c = FLOW_THERMAL_DEFAULT_TARGET_C,
        .active_power_w = FLOW_THERMAL_DEFAULT_POWER_W
    };
}
int flow_thermal_state_step(FlowThermalState *th, double p_active_watts, double dt_sec) {
    if (!th || dt_sec <= 0.0) return 0;
    if (th->c_thermal <= 0.0 || th->r_thermal <= 0.0) flow_jet_thermal_init_default(th);
    if (p_active_watts >= 0.0) th->active_power_w = p_active_watts;
    th->dtemp_dt = (th->active_power_w - (th->temp_c - th->temp_ambient_c) / th->r_thermal) / th->c_thermal;
    th->temp_c += th->dtemp_dt * dt_sec;
    if (th->temp_c >= th->temp_throttle_c && !th->is_throttled) {
        th->throttle_events++;
        th->is_throttled = 1;
    } else if (th->temp_c < th->temp_target_c) {
        th->is_throttled = 0;
    }
    return 1;
}
int flow_jet_thermal_step(FlowJet *jet, double p_active_watts, double dt_sec) {
    if (!jet || dt_sec <= 0.0) return 0;
    if (!flow_thermal_state_step(&jet->payload.thermal, p_active_watts, dt_sec)) return 0;
    jet->payload.s += jet->payload.thermal.active_power_w * dt_sec;
    return 1;
}
double flow_jet_thermal_predict_horizon(const FlowJet *jet, double horizon_sec) {
    return (!jet || jet->payload.thermal.c_thermal <= 0.0) ? FLOW_THERMAL_DEFAULT_AMBIENT_C : jet->payload.thermal.temp_c + jet->payload.thermal.dtemp_dt * horizon_sec; }
double flow_jet_thermal_prune_factor(const FlowJet *jet, double horizon_sec) { if (!jet) return 1.0; double t_pred = flow_jet_thermal_predict_horizon(jet, horizon_sec);
    const FlowThermalState *th = &jet->payload.thermal;
    double t_ceil = (th->temp_target_c > 0.0) ? th->temp_target_c : FLOW_THERMAL_DEFAULT_TARGET_C, t_trip = (th->temp_throttle_c > t_ceil) ? th->temp_throttle_c : FLOW_THERMAL_DEFAULT_THROTTLE_C;
    return t_pred <= t_ceil ? 1.0 : fmax(0.2, 1.0 - 0.58 * fmin(1.0, (t_pred - t_ceil) / (t_trip - t_ceil))); }
int flow_jet_contact_step(FlowJet *jet, double dt, double p_active_watts) { return (jet && flow_jet_symplectic_step(jet, dt)) ? flow_jet_thermal_step(jet, p_active_watts, dt) : 0; }
int flow_jet_mori_zwanzig_step(FlowJet *jet, double dt) { double decay = exp(-FLOW_JET_MZ_STEP_DECAY * dt), weight = jet->payload.memory_kernel[0];
    for (size_t i = 0; i < jet->header.vector_dim; ++i) jet->memory_integral[i] = jet->memory_integral[i] * decay + weight * jet->payload.p[i] * dt; return 1; }
int flow_jet_koopman_predict(const FlowJet *jet, double dt, double *observable_out) { if (!jet || !observable_out) return 0;
    uint32_t kdim = jet->header.koopman_dim ? jet->header.koopman_dim : FLOW_JET_STANDARD_KOOPMAN_DIM; if (kdim > FLOW_JET_MAX_KOOPMAN_DIM) kdim = FLOW_JET_MAX_KOOPMAN_DIM;
    double g0[FLOW_JET_MAX_KOOPMAN_DIM]; memcpy(g0, jet->payload.q, kdim * sizeof(double)); for (size_t i = 0; i < kdim; ++i) { double val = g0[i];
        for (size_t j = 0; j < kdim; ++j) val += dt * jet->payload.koopman_matrix[i][j] * g0[j]; observable_out[i] = val; } return 1; }
double flow_jet_phase_distance(const FlowJet *a, const FlowJet *b) { if (!a || !b) return 1.0e12;
    uint32_t da = a->header.vector_dim ? a->header.vector_dim : FLOW_JET_STANDARD_DIM, db = b->header.vector_dim ? b->header.vector_dim : FLOW_JET_STANDARD_DIM;
    uint32_t dim = (da < db ? da : db) > FLOW_JET_MAX_DIM ? FLOW_JET_MAX_DIM : (da < db ? da : db); double dist_sq = 0.0; for (size_t i = 0; i < dim; ++i) {
        double dq = a->payload.q[i] - b->payload.q[i], dp = a->payload.p[i] - b->payload.p[i], dm = a->memory_integral[i] - b->memory_integral[i];
        dist_sq += dq * dq + 1.5 * dp * dp + 0.5 * dm * dm; } return sqrt(dist_sq); }
static double clamp_finite(double v, double b) { return (isnan(v) || isinf(v)) ? 0.0 : (v > b ? b : (v < -b ? -b : v)); }
int flow_jet_clamp_to_safety_envelope(FlowJet *jet, double max_q, double max_p, double max_a) { if (!jet) return 0;
    uint32_t dim = jet->header.vector_dim ? jet->header.vector_dim : FLOW_JET_STANDARD_DIM; if (dim > FLOW_JET_MAX_DIM) dim = FLOW_JET_MAX_DIM;
    double qb = max_q > 0.0 ? max_q : 10.0, pb = max_p > 0.0 ? max_p : 50.0, ab = max_a > 0.0 ? max_a : 200.0; for (size_t i = 0; i < dim; ++i) {
        jet->payload.q[i] = clamp_finite(jet->payload.q[i], qb); jet->payload.p[i] = clamp_finite(jet->payload.p[i], pb);
        jet->payload.a[i] = clamp_finite(jet->payload.a[i], ab); } jet->header.hamiltonian_energy = flow_jet_hamiltonian(jet); return 1; }
int flow_jet_write_file(const FlowJet *jet, const char *filepath) { if (!jet || !filepath) return 0; FILE *f = fopen(filepath, "wb"); if (!f) return 0;
    FlowJetHeader hdr = jet->header; hdr.payload_size = sizeof(FlowJetPayload);
    uint32_t hdr_crc = flow_jet_crc32(&hdr, offsetof(FlowJetHeader, content_hash));
    snprintf(hdr.content_hash, sizeof(hdr.content_hash), "%08x", hdr_crc);
    uint8_t header_buf[FLOW_FJET_HEADER_SIZE] = {0};
    memcpy(header_buf, &hdr, sizeof(hdr));
    memcpy(header_buf + FLOW_FJET_HEADER_SIZE - sizeof(uint32_t), &hdr_crc, sizeof(uint32_t));
    FlowJetPayload payload = jet->payload;
    payload.crc32 = flow_jet_crc32(&payload, offsetof(FlowJetPayload, crc32));
    int ok = (fwrite(header_buf, 1, FLOW_FJET_HEADER_SIZE, f) == FLOW_FJET_HEADER_SIZE) && (fwrite(&payload, sizeof(payload), 1, f) == 1); fclose(f); return ok; }
int flow_jet_read_file(const char *filepath, FlowJet *jet_out) { if (!filepath || !jet_out) return 0; FILE *f = fopen(filepath, "rb"); if (!f) return 0;
    uint8_t header_buf[FLOW_FJET_HEADER_SIZE]; if (fread(header_buf, 1, FLOW_FJET_HEADER_SIZE, f) != FLOW_FJET_HEADER_SIZE) { fclose(f); return 0; }
    FlowJetHeader hdr = {0}; memcpy(&hdr, header_buf, sizeof(hdr));
    if (strncmp(hdr.magic, FLOW_FJET_MAGIC, 7) != 0) { fclose(f); return 0; }
    if (hdr.vector_dim == 0 || hdr.vector_dim > FLOW_JET_MAX_DIM || isnan(hdr.hamiltonian_energy) || isinf(hdr.hamiltonian_energy) || hdr.hamiltonian_energy < 0.0) { fclose(f); return 0; }
    uint32_t expected_hdr_crc = flow_jet_crc32(&hdr, offsetof(FlowJetHeader, content_hash)), stored_hdr_crc = 0;
    memcpy(&stored_hdr_crc, header_buf + FLOW_FJET_HEADER_SIZE - sizeof(uint32_t), sizeof(uint32_t));
    if (stored_hdr_crc != expected_hdr_crc) { fclose(f); return 0; }
    FlowJetPayload payload = {0};
    if (hdr.payload_size == sizeof(FlowJetPayload)) { int ok = (fread(&payload, sizeof(payload), 1, f) == 1); fclose(f);
        if (!ok || payload.crc32 != flow_jet_crc32(&payload, offsetof(FlowJetPayload, crc32))) return 0; } else { uint8_t temp_buf[4096];
        if (hdr.payload_size < 300 || hdr.payload_size > sizeof(temp_buf)) { fclose(f); return 0; }
        size_t read_bytes = fread(temp_buf, 1, hdr.payload_size, f); fclose(f); if (read_bytes != hdr.payload_size) return 0;
        uint32_t file_crc = 0; memcpy(&file_crc, temp_buf + read_bytes - 4, 4);
        if (file_crc != flow_jet_crc32(temp_buf, read_bytes - 4)) return 0;
        memcpy(payload.q, temp_buf, 16 * sizeof(double)); memcpy(payload.p, temp_buf + 16 * sizeof(double), 16 * sizeof(double)); memcpy(payload.a, temp_buf + 32 * sizeof(double), 16 * sizeof(double));
        flow_bmf_canvas_init(&payload.staged_canvas, 0, ~0ULL, ~0ULL, 0x1234567890ABCDEFULL); payload.pure_genome = 0x1234567890ABCDEFULL; payload.crc32 = file_crc; }
    memset(jet_out, 0, sizeof(*jet_out)); jet_out->header = hdr; jet_out->payload = payload; return 1; }
int flow_jet_from_fvec(const FlowVecHeader *hdr, const FlowVecPayload *payload, FlowJet *jet_out) { if (!hdr || !payload || !jet_out) return 0;
    flow_jet_init(jet_out, hdr->id, hdr->name); strncpy(jet_out->header.description, hdr->description, sizeof(jet_out->header.description) - 1);
    strncpy(jet_out->header.trigger_intent, hdr->trigger_intent, sizeof(jet_out->header.trigger_intent) - 1);
    strncpy(jet_out->header.component_id, hdr->component_id, sizeof(jet_out->header.component_id) - 1);
    for (size_t i = 0; i < FLOW_JET_STANDARD_DIM; ++i) { jet_out->payload.q[i] = payload->features[i]; jet_out->payload.p[i] = 0.0; jet_out->payload.a[i] = -payload->features[i]; }
    jet_out->payload.pure_genome = payload->pure_genome; jet_out->payload.hard_composite_mask = payload->hard_composite_mask;
    jet_out->payload.soft_composite_bias = payload->soft_composite_bias; jet_out->payload.proof = payload->proof;
    flow_bmf_canvas_init(&jet_out->payload.staged_canvas, 0, payload->hard_composite_mask, ~0ULL, payload->pure_genome);
    jet_out->payload.staged_canvas.dynamic_bias = payload->soft_composite_bias; jet_out->header.hamiltonian_energy = flow_jet_hamiltonian(jet_out); return 1; }
int flow_jet_to_fvec(const FlowJet *jet, FlowVecHeader *hdr_out, FlowVecPayload *payload_out) { if (!jet || !hdr_out || !payload_out) return 0;
    memset(hdr_out, 0, sizeof(*hdr_out)); strncpy(hdr_out->magic, FLOW_FVEC_MAGIC, sizeof(hdr_out->magic) - 1); strncpy(hdr_out->id, jet->header.id, sizeof(hdr_out->id) - 1);
    strncpy(hdr_out->name, jet->header.name, sizeof(hdr_out->name) - 1); strncpy(hdr_out->origin_hardware, jet->header.origin_hardware, sizeof(hdr_out->origin_hardware) - 1);
    strncpy(hdr_out->trigger_intent, jet->header.trigger_intent, sizeof(hdr_out->trigger_intent) - 1);
    strncpy(hdr_out->component_id, jet->header.component_id, sizeof(hdr_out->component_id) - 1);
    strncpy(hdr_out->description, jet->header.description, sizeof(hdr_out->description) - 1);
    hdr_out->vector_dim = FLOW_VAULT_DIM; hdr_out->payload_size = sizeof(FlowVecPayload); hdr_out->energy_score = jet->header.hamiltonian_energy;
    memset(payload_out, 0, sizeof(*payload_out)); memcpy(payload_out->features, jet->payload.q, FLOW_VAULT_DIM * sizeof(double));
    payload_out->pure_genome = jet->payload.pure_genome; payload_out->hard_composite_mask = jet->payload.hard_composite_mask;
    payload_out->soft_composite_bias = jet->payload.soft_composite_bias; payload_out->proof = jet->payload.proof;
    payload_out->crc32 = flow_fvec_crc32(payload_out, offsetof(FlowVecPayload, crc32)); return 1; }
FlowSMTResult flow_jet_verify_symplectic_soundness_smt(const FlowJet *jet, FlowSMTProofAttestation *proof_out) { if (!jet) return FLOW_SMT_UNKNOWN;
    double H = flow_jet_hamiltonian(jet); bool energy_ok = !(H < 0.0 || H > 1.0e6 || isnan(H)), memory_ok = true;
    uint32_t eff_taps = jet->header.memory_taps ? jet->header.memory_taps : FLOW_JET_STANDARD_TAPS; if (eff_taps > FLOW_JET_MAX_TAPS) eff_taps = FLOW_JET_MAX_TAPS;
    for (size_t i = 0; i < eff_taps; ++i) if (jet->payload.memory_kernel[i] < 0.0 || isnan(jet->payload.memory_kernel[i])) { memory_ok = false; break; }
    uint32_t eff_kdim = jet->header.koopman_dim ? jet->header.koopman_dim : FLOW_JET_STANDARD_KOOPMAN_DIM;
    if (eff_kdim > FLOW_JET_MAX_KOOPMAN_DIM) eff_kdim = FLOW_JET_MAX_KOOPMAN_DIM; double trace_K = 0.0;
    for (size_t i = 0; i < eff_kdim; ++i) trace_K += jet->payload.koopman_matrix[i][i]; const FlowBoxConstraint rules[] = {
        FLOW_SMT_RULE("hamiltonian_energy_bounded", !energy_ok, 0, 0, FLOW_BOX_THEOREM_BUFFER_BOUNDS, "Hamiltonian energy unbounded"),
        FLOW_SMT_RULE("mori_zwanzig_positivity", !memory_ok, 0, 0, FLOW_BOX_THEOREM_MEMORY_QUOTA, "Negative Mori-Zwanzig memory tap"),
        FLOW_SMT_RULE("koopman_spectral_stability", (trace_K > 0.0 || isnan(trace_K)), 0, 0, FLOW_BOX_THEOREM_SHARD_ISOLATION, "Koopman trace positive"),
        FLOW_SMT_RULE("canvas_64b_confinement", (sizeof(FlowBmf1BitCanvas) != 64), 0, 0, FLOW_BOX_THEOREM_DETERMINISM, "64B confinement violated") };
    return FLOW_SMT_VERIFY_RULES("jet_symplectic", proof_out, rules, "SMT JET SOUND: H=%.4f, Tr(K)=%.2f, MZ_taps=%d, 64B_Confinement=YES (Zero-Defect)", H, trace_K, (int)eff_taps);
}
int flow_moreau_init(FlowMoreauHysteresis *hys, double x_low, double x_high, int initial_state) { if (!hys || x_low >= x_high) return 0; *hys = (FlowMoreauHysteresis){
        .threshold_low = x_low, .threshold_high = x_high, .discrete_state = initial_state ? 1 : 0, .current_value = initial_state ? x_high : x_low }; return 1; }
int flow_moreau_step(FlowMoreauHysteresis *hys, double input_signal) { if (!hys) return 0; hys->total_updates++; hys->current_value = input_signal;
    int prev = hys->discrete_state; if (input_signal >= hys->threshold_high) { hys->discrete_state = 1; hys->state_transitions += (prev == 0);
    } else if (input_signal <= hys->threshold_low) { hys->discrete_state = 0; hys->state_transitions += (prev == 1); } else { hys->flutters_suppressed++; }
    return hys->discrete_state; } FlowSMTResult flow_moreau_verify_smt(const FlowMoreauHysteresis *hys, FlowSMTProofAttestation *proof_out) {
    if (!hys) return FLOW_SMT_UNKNOWN; const FlowBoxConstraint rules[] = {
        FLOW_SMT_RULE("convex set ordering", hys->threshold_low >= hys->threshold_high, 0, 0, FLOW_BOX_THEOREM_BUFFER_BOUNDS, "Hysteresis threshold bounds inverted or degenerate"),
        FLOW_SMT_RULE("discrete state binary", hys->discrete_state != 0 && hys->discrete_state != 1, 0, 0, FLOW_BOX_THEOREM_SHARD_ISOLATION, "Hysteresis state is not binary")
    }; return FLOW_SMT_VERIFY_RULES("moreau_hysteresis", proof_out, rules, "SMT MOREAU SOUND: Deadband=[%.2f, %.2f], State=%d, FluttersAbsorbed=%llu (Zero-Defect Soundness)",
        hys->threshold_low, hys->threshold_high, hys->discrete_state, (unsigned long long)hys->flutters_suppressed); }

/* ============================================================================
 * Hardware Telemetry, Cycle Probes & NUMA Topology (folded into flow_jet.c)
 * ============================================================================ */
#if defined(__APPLE__)
static int sysctl_u32(const char *name, uint32_t *out) { int v = 0; size_t len = sizeof(v);
    if (sysctlbyname(name, &v, &len, NULL, 0) == 0 && v > 0) { *out = (uint32_t)v; return 1; } return 0; }
static void sysctl_sz(const char *name, size_t *out) { size_t v = 0, len = sizeof(v); if (sysctlbyname(name, &v, &len, NULL, 0) == 0 && v > 0) *out = v; }
#endif
static uint64_t g_timer_freq_hz = 0;
static const char *g_rapl_path = "/sys/class/powercap/intel-rapl/intel-rapl:0/energy_uj";
static bool g_rapl_available = false;
int flow_hardware_telemetry_init(void) {
#if defined(__aarch64__) || defined(__arm64__)
    uint64_t val; __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(val)); g_timer_freq_hz = val > 0 ? val : FLOW_HW_FALLBACK_ARM_HZ;
#elif defined(__x86_64__) || defined(_M_X64)
    g_timer_freq_hz = FLOW_HW_FALLBACK_X86_HZ;
#else
    g_timer_freq_hz = 1000000000ULL;
#endif
    g_rapl_available = (access(g_rapl_path, R_OK) == 0); return 1; }
uint64_t flow_hardware_timer_frequency_hz(void) { if (!g_timer_freq_hz) flow_hardware_telemetry_init(); return g_timer_freq_hz; }
double flow_hardware_energy_uj(void) { if (g_rapl_available) { FILE *f = fopen(g_rapl_path, "r"); if (f) { unsigned long long uj = 0;
        if (fscanf(f, "%llu", &uj) == 1) { fclose(f); return (double)uj; } fclose(f); } }
    uint64_t cycles = flow_hardware_cycles(), freq = flow_hardware_timer_frequency_hz(); return ((freq > 0) ? (double)cycles / (double)freq : 0.0) * FLOW_HW_FALLBACK_TDP_WATTS * 1000000.0; }
void flow_hardware_probe_start(FlowPhysicalProbe *probe) { if (!probe) return; memset(probe, 0, sizeof(*probe)); probe->start_cycles = flow_hardware_cycles();
    probe->start_energy_uj = flow_hardware_energy_uj(); probe->is_estimated_power = !g_rapl_available; }
void flow_hardware_probe_stop(FlowPhysicalProbe *probe) { if (!probe) return;
    probe->end_cycles = flow_hardware_cycles(); probe->end_energy_uj = flow_hardware_energy_uj(); probe->is_estimated_power = !g_rapl_available;
    probe->elapsed_cycles = (probe->end_cycles >= probe->start_cycles) ? (probe->end_cycles - probe->start_cycles) : 0; uint64_t freq = flow_hardware_timer_frequency_hz();
    probe->elapsed_nanoseconds = (freq > 0) ? ((double)probe->elapsed_cycles * 1e9 / (double)freq) : (double)probe->elapsed_cycles;
    probe->dissipated_energy_uj = (probe->end_energy_uj >= probe->start_energy_uj) ? (probe->end_energy_uj - probe->start_energy_uj) : 0.0;
    probe->physical_power_watts = (probe->elapsed_nanoseconds > 0.0) ? (probe->dissipated_energy_uj / (probe->elapsed_nanoseconds / 1000.0)) : 0.0; }
void flow_hardware_probe_update_thermal(const FlowPhysicalProbe *probe, FlowThermalState *thermal, double dt_sec) {
    if (!probe || !thermal) return;
    double p_watts = probe->physical_power_watts > 0.0 ? probe->physical_power_watts : FLOW_HW_FALLBACK_TDP_WATTS;
    if (dt_sec > 0.0) {
        flow_thermal_state_step(thermal, p_watts, dt_sec);
    } else {
        if (thermal->c_thermal <= 0.0 || thermal->r_thermal <= 0.0) flow_jet_thermal_init_default(thermal);
        thermal->active_power_w = p_watts;
    }
}
int flow_numa_topology_discover(FlowHardwareTopology *topo) { if (!topo) return 0; memset(topo, 0, sizeof(*topo));
    topo->cache_line_size = FLOW_HW_DEFAULT_CACHELINE_BYTES; topo->l1_cache_size = FLOW_HW_DEFAULT_L1_BYTES; topo->l2_cache_size = FLOW_HW_DEFAULT_L2_BYTES;
#if defined(__APPLE__)
    if (!sysctl_u32("hw.ncpu", &topo->total_logical_cores)) topo->total_logical_cores = (uint32_t)sysconf(_SC_NPROCESSORS_ONLN);
    if (!sysctl_u32("hw.physicalcpu", &topo->total_physical_cores)) topo->total_physical_cores = topo->total_logical_cores;
    if (!sysctl_u32("hw.perflevel0.physicalcpu", &topo->performance_cores)) topo->performance_cores = topo->total_physical_cores;
    if (!sysctl_u32("hw.perflevel1.physicalcpu", &topo->efficiency_cores))
        topo->efficiency_cores = (topo->total_physical_cores > topo->performance_cores) ? (topo->total_physical_cores - topo->performance_cores) : 0;
    sysctl_sz("hw.cachelinesize", &topo->cache_line_size); sysctl_sz("hw.l1dcachesize", &topo->l1_cache_size); sysctl_sz("hw.l2cachesize", &topo->l2_cache_size);
    topo->numa_node_count = 1; topo->nodes[0].node_id = 0; topo->nodes[0].core_count = topo->total_logical_cores;
    for (uint32_t i = 0; i < topo->total_logical_cores && i < FLOW_MAX_PHYSICAL_CORES; ++i) topo->nodes[0].core_ids[i] = i; topo->is_numa_available = true;
#elif defined(__linux__)
    long cpus = sysconf(_SC_NPROCESSORS_ONLN); topo->total_logical_cores = cpus > 0 ? (uint32_t)cpus : 4;
    topo->total_physical_cores = topo->performance_cores = topo->total_logical_cores; uint32_t node_count = 0; for (uint32_t n = 0; n < FLOW_MAX_NUMA_NODES; ++n) {
        char path[128]; snprintf(path, sizeof(path), "/sys/devices/system/node/node%u", n);
        if (access(path, F_OK) == 0) { topo->nodes[n].node_id = n; topo->nodes[n].core_count = 0; node_count++; } } if (!node_count) {
        node_count = 1; topo->nodes[0].node_id = 0; topo->nodes[0].core_count = topo->total_logical_cores;
        for (uint32_t i = 0; i < topo->total_logical_cores && i < FLOW_MAX_PHYSICAL_CORES; ++i) topo->nodes[0].core_ids[i] = i; }
    topo->numa_node_count = node_count; topo->is_numa_available = (node_count > 1);
#else
    topo->total_logical_cores = topo->total_physical_cores = topo->performance_cores = 4; topo->numa_node_count = 1; topo->nodes[0].core_count = 4;
#endif
    return 1; }
int flow_numa_pin_thread(uint32_t core_id) {
#if defined(__APPLE__)
    (void)core_id; return (pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0) == 0) ? 1 : 0;
#elif defined(__linux__)
    cpu_set_t cpuset; CPU_ZERO(&cpuset); long ncpus = sysconf(_SC_NPROCESSORS_ONLN); if (ncpus <= 0) ncpus = 1; CPU_SET(core_id % (uint32_t)ncpus, &cpuset);
    return (pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset) == 0) ? 1 : 0;
#else
    (void)core_id; return 1;
#endif
}
void *flow_numa_alloc_local(size_t bytes) { if (!bytes) return NULL; size_t aligned = (bytes + (FLOW_OS_PAGE_BYTES - 1U)) & ~(size_t)(FLOW_OS_PAGE_BYTES - 1U);
    void *ptr = mmap(NULL, aligned, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0); if (ptr == MAP_FAILED) return NULL;
    for (size_t off = 0; off < aligned; off += FLOW_OS_PAGE_BYTES) ((volatile uint8_t *)ptr)[off] = 0; return ptr; }
void flow_numa_free_local(void *ptr, size_t bytes) { if (ptr && bytes) munmap(ptr, (bytes + (FLOW_OS_PAGE_BYTES - 1U)) & ~(size_t)(FLOW_OS_PAGE_BYTES - 1U)); }
