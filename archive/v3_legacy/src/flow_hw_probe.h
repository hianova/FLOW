#ifndef FLOW_HW_PROBE_H
#define FLOW_HW_PROBE_H

#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE 1
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Hardware Telemetry, Cycle Probes & NUMA Topology
 * ============================================================================ */
#define FLOW_HW_FALLBACK_X86_HZ         3000000000ULL
#define FLOW_HW_FALLBACK_ARM_HZ         24000000ULL
#define FLOW_HW_FALLBACK_TDP_WATTS      5.0
#define FLOW_HW_DEFAULT_CACHELINE_BYTES 64U
#define FLOW_HW_DEFAULT_L1_BYTES        32768U
#define FLOW_HW_DEFAULT_L2_BYTES        4194304U
#define FLOW_OS_PAGE_BYTES              4096U

typedef struct {
    uint64_t start_cycles, end_cycles, elapsed_cycles;
    double elapsed_nanoseconds, start_energy_uj, end_energy_uj, dissipated_energy_uj,
        physical_power_watts;
    int is_estimated_power;
} FlowPhysicalProbe;

int flow_hardware_telemetry_init(void);

static inline uint64_t flow_hardware_cycles(void) {
#if defined(__aarch64__) || defined(__arm64__)
    uint64_t val;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(val));
    return val;
#elif defined(__x86_64__) || defined(_M_X64)
    return __builtin_ia32_rdtsc();
#else
    return 0;
#endif
}

static inline uint64_t flow_clock_monotonic_ns(void) {
    struct timespec ts;
    return clock_gettime(CLOCK_MONOTONIC, &ts) == 0
               ? (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec
               : 0;
}

uint64_t flow_hardware_timer_frequency_hz(void);
uint64_t flow_hardware_frequency_hz(void);
double flow_hardware_energy_uj(void);
void flow_hardware_probe_start(FlowPhysicalProbe *probe);
void flow_hardware_probe_stop(FlowPhysicalProbe *probe);

#define FLOW_MAX_NUMA_NODES     8
#define FLOW_MAX_PHYSICAL_CORES 256

typedef struct {
    uint32_t node_id, core_count, core_ids[FLOW_MAX_PHYSICAL_CORES];
    size_t local_memory_bytes;
} FlowNumaNode;

typedef struct {
    uint32_t total_logical_cores, total_physical_cores, performance_cores, efficiency_cores,
        numa_node_count;
    FlowNumaNode nodes[FLOW_MAX_NUMA_NODES];
    size_t cache_line_size, l1_cache_size, l2_cache_size;
    bool is_numa_available;
} FlowHardwareTopology;

typedef FlowHardwareTopology FlowNumaTopology;

int flow_numa_topology_discover(FlowHardwareTopology *topo);
FlowHardwareTopology flow_hardware_topology(void);
int flow_numa_pin_thread(uint32_t core_id);
void *flow_numa_alloc_local(size_t bytes);
void flow_numa_free_local(void *ptr, size_t bytes);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_HW_PROBE_H */
