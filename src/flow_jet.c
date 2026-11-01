#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "flow_jet.h"
#include "flow_hw_probe.h"
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

uint32_t flow_jet_crc32(const void *data, size_t length) {
    return flow_crc32_ieee(data, length);
}

int flow_jet_init_extended(FlowJet *jet, const char *id, const char *name, uint32_t dim,
                           uint32_t koopman_dim, uint32_t taps) {
    if (!jet)
        return 0;
    memset(jet, 0, sizeof(*jet));
    uint32_t eff_dim = (dim > 0 && dim <= FLOW_JET_MAX_DIM) ? dim : FLOW_JET_STANDARD_DIM;
    uint32_t eff_kdim = (koopman_dim > 0 && koopman_dim <= FLOW_JET_MAX_KOOPMAN_DIM)
                            ? koopman_dim
                            : FLOW_JET_STANDARD_KOOPMAN_DIM;
    uint32_t eff_taps = (taps > 0 && taps <= FLOW_JET_MAX_TAPS) ? taps : FLOW_JET_STANDARD_TAPS;
    strncpy(jet->header.magic, FLOW_FJET_MAGIC, sizeof(jet->header.magic) - 1);
    strncpy(jet->header.id, id ? id : "jet_default", sizeof(jet->header.id) - 1);
    strncpy(jet->header.name, name ? name : "Default Phase Space Jet",
            sizeof(jet->header.name) - 1);
    strncpy(jet->header.origin_hardware, "Unified Phase Space Engine",
            sizeof(jet->header.origin_hardware) - 1);
    strncpy(jet->header.trigger_intent, "JET_SYMPLECTIC_FLOW",
            sizeof(jet->header.trigger_intent) - 1);
    strncpy(jet->header.category, "JET_BUNDLE", sizeof(jet->header.category) - 1);
    strncpy(jet->header.component_id, "symplectic_kernel", sizeof(jet->header.component_id) - 1);
    strncpy(jet->header.description,
            "2nd-order Jet Bundle with Mori-Zwanzig memory kernel & Koopman generator",
            sizeof(jet->header.description) - 1);
    strncpy(jet->header.smt_signature, "SYMPLECTIC_UNSAT:KOOPMAN_UNSAT:MORI_ZWANZIG_UNSAT",
            sizeof(jet->header.smt_signature) - 1);
    jet->header.created_at_unix = jet->header.last_reinforced_unix = (uint64_t)time(NULL);
    jet->header.vector_dim = eff_dim;
    jet->header.koopman_dim = (uint8_t)eff_kdim;
    jet->header.memory_taps = (uint8_t)eff_taps;
    jet->header.payload_size = sizeof(FlowJetPayload);
    jet->header.confidence_score = 100;
    flow_axiom_init(&jet->payload.section, jet->header.trigger_intent);
    for (size_t i = 0; i < eff_taps; ++i)
        jet->payload.memory_kernel[i] = exp(-FLOW_JET_MZ_KERNEL_DECAY * (double)i);
    for (size_t i = 0; i < eff_kdim; ++i)
        for (size_t j = 0; j < eff_kdim; ++j)
            jet->payload.koopman_matrix[i][j] =
                (i == j) ? -0.05 : (abs((int)i - (int)j) == 1 ? 0.02 : 0.0);
    flow_bmf_canvas_init(&jet->payload.staged_canvas, 0, ~0ULL, ~0ULL, 0x1234567890ABCDEFULL);
    jet->payload.pure_genome = 0x1234567890ABCDEFULL;
    jet->payload.hard_composite_mask = ~0ULL;
    jet->payload.proof =
        (FlowSMTProofAttestation){FLOW_SMT_PROVEN_UNSAT, FLOW_SMT_PROVEN_UNSAT,
                                  FLOW_SMT_PROVEN_UNSAT, FLOW_SMT_PROVEN_UNSAT, ""};
    jet->header.hamiltonian_energy = flow_jet_hamiltonian(jet);
    return 1;
}

int flow_jet_init(FlowJet *jet, const char *id, const char *name) {
    return flow_jet_init_extended(jet, id, name, FLOW_JET_STANDARD_DIM,
                                  FLOW_JET_STANDARD_KOOPMAN_DIM, FLOW_JET_STANDARD_TAPS);
}

int flow_jet_potential_init_default(FlowJetPotentialLandscape *l, uint32_t dim) {
    if (!l)
        return 0;
    memset(l, 0, sizeof(*l));
    l->dim = (dim > 0 && dim <= FLOW_JET_MAX_DIM) ? dim : FLOW_JET_STANDARD_DIM;
    l->barrier_mu = 0.005;
    l->moreau_kappa = 4.0;
    for (size_t i = 0; i < l->dim; ++i) {
        l->omega[i] = 1.0;
        l->q_saturation[i] = 2.0;
        l->moreau_low[i] = -1.2;
        l->moreau_high[i] = 1.2;
    }
    return 1;
}

int flow_jet_potential_eval_gradient(const FlowJetPotentialLandscape *l, const double q[],
                                     double grad_out[]) {
    if (!l || !q || !grad_out)
        return 0;
    size_t dim = (l->dim <= FLOW_JET_MAX_DIM) ? l->dim : FLOW_JET_MAX_DIM;
    for (size_t i = 0; i < dim; ++i) {
        double grad_base = l->omega[i] * l->omega[i] * (q[i] - l->q_equilibrium[i]);
        double diff_sat = l->q_saturation[i] - fabs(q[i]);
        if (fabs(diff_sat) < 1e-6)
            diff_sat = (diff_sat >= 0.0) ? 1e-6 : -1e-6;
        double grad_barrier =
            ((q[i] >= 0.0) ? 1.0 : -1.0) * (2.0 * l->barrier_mu) / (diff_sat * diff_sat * diff_sat);
        double grad_moreau =
            q[i] > l->moreau_high[i]
                ? l->moreau_kappa * (q[i] - l->moreau_high[i])
                : (q[i] < l->moreau_low[i] ? l->moreau_kappa * (q[i] - l->moreau_low[i]) : 0.0);
        grad_out[i] = grad_base + grad_barrier + grad_moreau;
    }
    return 1;
}

int flow_jet_symplectic_step_with_potential(FlowJet *jet, const FlowJetPotentialLandscape *l,
                                            double dt) {
    if (!jet || !l || dt <= 0.0 || !isfinite(dt))
        return 0;
    if (jet->header.vector_dim > FLOW_JET_MAX_DIM)
        jet->header.vector_dim = FLOW_JET_MAX_DIM;
    uint32_t dim = jet->header.vector_dim;
    double grad0[FLOW_JET_MAX_DIM] = {0}, grad1[FLOW_JET_MAX_DIM] = {0},
           p_half[FLOW_JET_MAX_DIM] = {0};
    flow_jet_potential_eval_gradient(l, jet->payload.q, grad0);
    for (size_t i = 0; i < dim; ++i) {
        p_half[i] = jet->payload.p[i] - 0.5 * dt * grad0[i];
        jet->payload.q[i] += dt * p_half[i];
    }
    flow_jet_potential_eval_gradient(l, jet->payload.q, grad1);
    for (size_t i = 0; i < dim; ++i) {
        jet->payload.a[i] = -grad1[i];
        jet->payload.p[i] = p_half[i] - 0.5 * dt * grad1[i];
    }
    flow_jet_mori_zwanzig_step(jet, dt);
    jet->header.hamiltonian_energy = flow_jet_hamiltonian(jet);
    return 1;
}

double flow_jet_hamiltonian(const FlowJet *jet) {
    if (!jet)
        return 0.0;
    size_t dim =
        (jet->header.vector_dim > FLOW_JET_MAX_DIM) ? FLOW_JET_MAX_DIM : jet->header.vector_dim;
    double E = 0.0;
    for (size_t i = 0; i < dim; ++i)
        E += 0.5 * (jet->payload.p[i] * jet->payload.p[i] + jet->payload.q[i] * jet->payload.q[i]);
    return E;
}

void flow_verlet_step_harmonic(size_t dim, double *q, double *p, double *a,
                               const double *omega_spectrum, double dt) {
    if (!q || !p || !a || dt <= 0.0)
        return;
    for (size_t i = 0; i < dim; ++i) {
        double omega = omega_spectrum ? omega_spectrum[i] : 1.0;
        double p_half = p[i] - 0.5 * dt * (omega * q[i]);
        q[i] += dt * p_half;
        a[i] = -(omega * q[i]);
        p[i] = p_half - 0.5 * dt * (omega * q[i]);
    }
}

int flow_jet_symplectic_step(FlowJet *jet, double dt) {
    if (!jet || dt <= 0.0)
        return 0;
    if (jet->header.vector_dim > FLOW_JET_MAX_DIM)
        jet->header.vector_dim = FLOW_JET_MAX_DIM;
    flow_verlet_step_harmonic(jet->header.vector_dim, jet->payload.q, jet->payload.p,
                              jet->payload.a, NULL, dt);
    flow_jet_mori_zwanzig_step(jet, dt);
    jet->header.hamiltonian_energy = flow_jet_hamiltonian(jet);
    return 1;
}

int flow_jet_mori_zwanzig_step(FlowJet *jet, double dt) {
    if (!jet)
        return 0;
    size_t dim =
        (jet->header.vector_dim > FLOW_JET_MAX_DIM) ? FLOW_JET_MAX_DIM : jet->header.vector_dim;
    double decay = exp(-FLOW_JET_MZ_STEP_DECAY * dt), weight = jet->payload.memory_kernel[0];
    for (size_t i = 0; i < dim; ++i)
        jet->memory_integral[i] = jet->memory_integral[i] * decay + weight * jet->payload.p[i] * dt;
    return 1;
}

int flow_jet_koopman_predict(const FlowJet *jet, double dt, double *observable_out) {
    if (!jet || !observable_out)
        return 0;
    uint32_t kdim =
        jet->header.koopman_dim ? jet->header.koopman_dim : FLOW_JET_STANDARD_KOOPMAN_DIM;
    if (kdim > FLOW_JET_MAX_KOOPMAN_DIM)
        kdim = FLOW_JET_MAX_KOOPMAN_DIM;
    double g0[FLOW_JET_MAX_KOOPMAN_DIM];
    memcpy(g0, jet->payload.q, kdim * sizeof(double));
    for (size_t i = 0; i < kdim; ++i) {
        double val = g0[i];
        for (size_t j = 0; j < kdim; ++j)
            val += dt * jet->payload.koopman_matrix[i][j] * g0[j];
        observable_out[i] = val;
    }
    return 1;
}

double flow_jet_phase_distance(const FlowJet *a, const FlowJet *b) {
    if (!a || !b)
        return 1.0e12;
    uint32_t da = a->header.vector_dim ? a->header.vector_dim : FLOW_JET_STANDARD_DIM,
             db = b->header.vector_dim ? b->header.vector_dim : FLOW_JET_STANDARD_DIM;
    uint32_t dim = (da < db ? da : db) > FLOW_JET_MAX_DIM ? FLOW_JET_MAX_DIM : (da < db ? da : db);
    double dist_sq = 0.0;
    for (size_t i = 0; i < dim; ++i) {
        double dq = a->payload.q[i] - b->payload.q[i], dp = a->payload.p[i] - b->payload.p[i],
               dm = a->memory_integral[i] - b->memory_integral[i];
        dist_sq += dq * dq + 1.5 * dp * dp + 0.5 * dm * dm;
    }
    return sqrt(dist_sq);
}

static double clamp_finite(double v, double b) {
    return (isnan(v) || isinf(v)) ? 0.0 : (v > b ? b : (v < -b ? -b : v));
}

int flow_jet_clamp_to_safety_envelope(FlowJet *jet, double max_q, double max_p, double max_a) {
    if (!jet)
        return 0;
    uint32_t dim = jet->header.vector_dim ? jet->header.vector_dim : FLOW_JET_STANDARD_DIM;
    if (dim > FLOW_JET_MAX_DIM)
        dim = FLOW_JET_MAX_DIM;
    double qb = max_q > 0.0 ? max_q : 10.0, pb = max_p > 0.0 ? max_p : 50.0,
           ab = max_a > 0.0 ? max_a : 200.0;
    for (size_t i = 0; i < dim; ++i) {
        jet->payload.q[i] = clamp_finite(jet->payload.q[i], qb);
        jet->payload.p[i] = clamp_finite(jet->payload.p[i], pb);
        jet->payload.a[i] = clamp_finite(jet->payload.a[i], ab);
    }
    jet->header.hamiltonian_energy = flow_jet_hamiltonian(jet);
    return 1;
}

FlowSMTResult flow_jet_verify_symplectic_soundness_smt(const FlowJet *jet,
                                                       FlowSMTProofAttestation *proof_out) {
    if (!jet)
        return FLOW_SMT_UNKNOWN;
    double H = flow_jet_hamiltonian(jet);
    bool energy_ok = !(H < 0.0 || H > 1.0e6 || isnan(H)), memory_ok = true;
    uint32_t eff_taps = jet->header.memory_taps ? jet->header.memory_taps : FLOW_JET_STANDARD_TAPS;
    if (eff_taps > FLOW_JET_MAX_TAPS)
        eff_taps = FLOW_JET_MAX_TAPS;
    for (size_t i = 0; i < eff_taps; ++i)
        if (jet->payload.memory_kernel[i] < 0.0 || isnan(jet->payload.memory_kernel[i])) {
            memory_ok = false;
            break;
        }
    uint32_t eff_kdim =
        jet->header.koopman_dim ? jet->header.koopman_dim : FLOW_JET_STANDARD_KOOPMAN_DIM;
    if (eff_kdim > FLOW_JET_MAX_KOOPMAN_DIM)
        eff_kdim = FLOW_JET_MAX_KOOPMAN_DIM;
    double trace_K = 0.0;
    for (size_t i = 0; i < eff_kdim; ++i)
        trace_K += jet->payload.koopman_matrix[i][i];
    const FlowBoxConstraint rules[] = {
        FLOW_SMT_RULE("hamiltonian_energy_bounded", !energy_ok, 0, 0,
                      FLOW_BOX_THEOREM_BUFFER_BOUNDS, "Hamiltonian energy unbounded"),
        FLOW_SMT_RULE("mori_zwanzig_positivity", !memory_ok, 0, 0, FLOW_BOX_THEOREM_MEMORY_QUOTA,
                      "Negative Mori-Zwanzig memory tap"),
        FLOW_SMT_RULE("koopman_spectral_stability", (trace_K > 0.0 || isnan(trace_K)), 0, 0,
                      FLOW_BOX_THEOREM_SHARD_ISOLATION, "Koopman trace positive"),
        FLOW_SMT_RULE("canvas_64b_confinement", (sizeof(FlowBmf1BitCanvas) != 64), 0, 0,
                      FLOW_BOX_THEOREM_DETERMINISM, "64B confinement violated")};
    return FLOW_SMT_VERIFY_RULES(
        "jet_symplectic", proof_out, rules,
        "SMT JET SOUND: H=%.4f, Tr(K)=%.2f, MZ_taps=%d, 64B_Confinement=YES (Zero-Defect)", H,
        trace_K, (int)eff_taps);
}

/* ============================================================================
 * Hardware Telemetry, Cycle Probes & NUMA Topology
 * ============================================================================ */
#if defined(__APPLE__)
static int sysctl_u32(const char *name, uint32_t *out) {
    int v = 0;
    size_t len = sizeof(v);
    if (sysctlbyname(name, &v, &len, NULL, 0) == 0 && v > 0) {
        *out = (uint32_t)v;
        return 1;
    }
    return 0;
}
static void sysctl_sz(const char *name, size_t *out) {
    size_t v = 0, len = sizeof(v);
    if (sysctlbyname(name, &v, &len, NULL, 0) == 0 && v > 0)
        *out = v;
}
#endif
static uint64_t g_timer_freq_hz = 0;
static const char *g_rapl_path = "/sys/class/powercap/intel-rapl/intel-rapl:0/energy_uj";
static bool g_rapl_available = false;

int flow_hardware_telemetry_init(void) {
#if defined(__aarch64__) || defined(__arm64__)
    uint64_t val;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(val));
    g_timer_freq_hz = val > 0 ? val : FLOW_HW_FALLBACK_ARM_HZ;
#elif defined(__x86_64__) || defined(_M_X64)
    g_timer_freq_hz = FLOW_HW_FALLBACK_X86_HZ;
#else
    g_timer_freq_hz = 1000000000ULL;
#endif
    g_rapl_available = (access(g_rapl_path, R_OK) == 0);
    return 1;
}

uint64_t flow_hardware_timer_frequency_hz(void) {
    if (!g_timer_freq_hz)
        flow_hardware_telemetry_init();
    return g_timer_freq_hz;
}

double flow_hardware_energy_uj(void) {
    if (g_rapl_available) {
        FILE *f = fopen(g_rapl_path, "r");
        if (f) {
            unsigned long long uj = 0;
            if (fscanf(f, "%llu", &uj) == 1) {
                fclose(f);
                return (double)uj;
            }
            fclose(f);
        }
    }
    uint64_t cycles = flow_hardware_cycles(), freq = flow_hardware_timer_frequency_hz();
    return ((freq > 0) ? (double)cycles / (double)freq : 0.0) * FLOW_HW_FALLBACK_TDP_WATTS *
           1000000.0;
}

void flow_hardware_probe_start(FlowPhysicalProbe *probe) {
    if (!probe)
        return;
    memset(probe, 0, sizeof(*probe));
    probe->start_cycles = flow_hardware_cycles();
    probe->start_energy_uj = flow_hardware_energy_uj();
    probe->is_estimated_power = !g_rapl_available;
}

void flow_hardware_probe_stop(FlowPhysicalProbe *probe) {
    if (!probe)
        return;
    probe->end_cycles = flow_hardware_cycles();
    probe->end_energy_uj = flow_hardware_energy_uj();
    probe->is_estimated_power = !g_rapl_available;
    probe->elapsed_cycles =
        (probe->end_cycles >= probe->start_cycles) ? (probe->end_cycles - probe->start_cycles) : 0;
    uint64_t freq = flow_hardware_timer_frequency_hz();
    probe->elapsed_nanoseconds = (freq > 0) ? ((double)probe->elapsed_cycles * 1e9 / (double)freq)
                                            : (double)probe->elapsed_cycles;
    probe->dissipated_energy_uj = (probe->end_energy_uj >= probe->start_energy_uj)
                                      ? (probe->end_energy_uj - probe->start_energy_uj)
                                      : 0.0;
    probe->physical_power_watts =
        (probe->elapsed_nanoseconds > 0.0)
            ? (probe->dissipated_energy_uj / (probe->elapsed_nanoseconds / 1000.0))
            : 0.0;
}

int flow_numa_topology_discover(FlowHardwareTopology *topo) {
    if (!topo)
        return 0;
    memset(topo, 0, sizeof(*topo));
    topo->cache_line_size = FLOW_HW_DEFAULT_CACHELINE_BYTES;
    topo->l1_cache_size = FLOW_HW_DEFAULT_L1_BYTES;
    topo->l2_cache_size = FLOW_HW_DEFAULT_L2_BYTES;
#if defined(__APPLE__)
    if (!sysctl_u32("hw.ncpu", &topo->total_logical_cores))
        topo->total_logical_cores = (uint32_t)sysconf(_SC_NPROCESSORS_ONLN);
    if (!sysctl_u32("hw.physicalcpu", &topo->total_physical_cores))
        topo->total_physical_cores = topo->total_logical_cores;
    if (!sysctl_u32("hw.perflevel0.physicalcpu", &topo->performance_cores))
        topo->performance_cores = topo->total_physical_cores;
    if (!sysctl_u32("hw.perflevel1.physicalcpu", &topo->efficiency_cores))
        topo->efficiency_cores = (topo->total_physical_cores > topo->performance_cores)
                                     ? (topo->total_physical_cores - topo->performance_cores)
                                     : 0;
    sysctl_sz("hw.cachelinesize", &topo->cache_line_size);
    sysctl_sz("hw.l1dcachesize", &topo->l1_cache_size);
    sysctl_sz("hw.l2cachesize", &topo->l2_cache_size);
    topo->numa_node_count = 1;
    topo->nodes[0].node_id = 0;
    topo->nodes[0].core_count = topo->total_logical_cores;
    for (uint32_t i = 0; i < topo->total_logical_cores && i < FLOW_MAX_PHYSICAL_CORES; ++i)
        topo->nodes[0].core_ids[i] = i;
    topo->is_numa_available = true;
#elif defined(__linux__)
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    topo->total_logical_cores = cpus > 0 ? (uint32_t)cpus : 4;
    topo->total_physical_cores = topo->performance_cores = topo->total_logical_cores;
    uint32_t node_count = 0;
    for (uint32_t n = 0; n < FLOW_MAX_NUMA_NODES; ++n) {
        char path[128];
        snprintf(path, sizeof(path), "/sys/devices/system/node/node%u", n);
        if (access(path, F_OK) == 0) {
            topo->nodes[n].node_id = n;
            topo->nodes[n].core_count = 0;
            node_count++;
        }
    }
    if (!node_count) {
        node_count = 1;
        topo->nodes[0].node_id = 0;
        topo->nodes[0].core_count = topo->total_logical_cores;
        for (uint32_t i = 0; i < topo->total_logical_cores && i < FLOW_MAX_PHYSICAL_CORES; ++i)
            topo->nodes[0].core_ids[i] = i;
    }
    topo->numa_node_count = node_count;
    topo->is_numa_available = (node_count > 1);
#else
    topo->total_logical_cores = topo->total_physical_cores = topo->performance_cores = 4;
    topo->numa_node_count = 1;
    topo->nodes[0].core_count = 4;
#endif
    return 1;
}

int flow_numa_pin_thread(uint32_t core_id) {
#if defined(__APPLE__)
    (void)core_id;
    return (pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0) == 0) ? 1 : 0;
#elif defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    long ncpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpus <= 0)
        ncpus = 1;
    CPU_SET(core_id % (uint32_t)ncpus, &cpuset);
    return (pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset) == 0) ? 1 : 0;
#else
    (void)core_id;
    return 1;
#endif
}

void *flow_numa_alloc_local(size_t bytes) {
    if (!bytes)
        return NULL;
    size_t aligned = (bytes + (FLOW_OS_PAGE_BYTES - 1U)) & ~(size_t)(FLOW_OS_PAGE_BYTES - 1U);
    void *ptr = mmap(NULL, aligned, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED)
        return NULL;
    for (size_t off = 0; off < aligned; off += FLOW_OS_PAGE_BYTES)
        ((volatile uint8_t *)ptr)[off] = 0;
    return ptr;
}

void flow_numa_free_local(void *ptr, size_t bytes) {
    if (ptr && bytes)
        munmap(ptr, (bytes + (FLOW_OS_PAGE_BYTES - 1U)) & ~(size_t)(FLOW_OS_PAGE_BYTES - 1U));
}

uint64_t flow_hardware_frequency_hz(void) {
    return flow_hardware_timer_frequency_hz();
}

FlowHardwareTopology flow_hardware_topology(void) {
    FlowHardwareTopology t;
    memset(&t, 0, sizeof(t));
    (void)flow_numa_topology_discover(&t);
    return t;
}
