#ifndef FLOW_HARDWARE_TELEMETRY_H
#define FLOW_HARDWARE_TELEMETRY_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint64_t start_cycles, end_cycles, elapsed_cycles; double elapsed_nanoseconds, start_energy_uj, end_energy_uj, dissipated_energy_uj, physical_power_watts;
} FlowPhysicalProbe; int flow_hardware_telemetry_init(void); static inline uint64_t flow_hardware_cycles(void) {
#if defined(__aarch64__) || defined(__arm64__)
    uint64_t val; __asm__ volatile("mrs %0, cntvct_el0" : "=r"(val)); return val;
#elif defined(__x86_64__) || defined(_M_X64)
    return __builtin_ia32_rdtsc();
#else
    return 0;
#endif
} static inline uint64_t flow_clock_monotonic_ns(void) { struct timespec ts;
    return clock_gettime(CLOCK_MONOTONIC, &ts) == 0 ? (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec : 0; }
static inline uint64_t flow_prng_next(uint64_t *state) { uint64_t x = (state && *state) ? *state : UINT64_C(0x9e3779b97f4a7c15); x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    if (state) *state = x; return x; } uint64_t flow_hardware_timer_frequency_hz(void); double flow_hardware_energy_uj(void);
void flow_hardware_probe_start(FlowPhysicalProbe *probe); void flow_hardware_probe_stop(FlowPhysicalProbe *probe);
double flow_hardware_lyapunov_metric(const FlowPhysicalProbe *probe, double constraint_energy); struct FlowThermalState;
void flow_hardware_probe_update_thermal(const FlowPhysicalProbe *probe, struct FlowThermalState *thermal, double dt_sec);
#ifdef __cplusplus
}
#endif
#endif /* FLOW_HARDWARE_TELEMETRY_H */
