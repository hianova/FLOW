#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "hardware_telemetry.h"
#include "flow_jet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <pthread.h>
#include <pthread/qos.h>
#include <sys/sysctl.h>
#include <sys/types.h>
static int sysctl_u32(const char *name, uint32_t *out) { int v = 0; size_t len = sizeof(v);
    if (sysctlbyname(name, &v, &len, NULL, 0) == 0 && v > 0) { *out = (uint32_t)v; return 1; } return 0; }
static void sysctl_sz(const char *name, size_t *out) { size_t v = 0, len = sizeof(v); if (sysctlbyname(name, &v, &len, NULL, 0) == 0 && v > 0) *out = v; }
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif
static uint64_t g_timer_freq_hz = 0; static const char *g_rapl_path = "/sys/class/powercap/intel-rapl/intel-rapl:0/energy_uj"; static bool g_rapl_available = false;
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
