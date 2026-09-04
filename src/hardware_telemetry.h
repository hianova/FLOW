#ifndef FLOW_HARDWARE_TELEMETRY_H
#define FLOW_HARDWARE_TELEMETRY_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW Physical Hardware Telemetry & Thermodynamic Closed-Loop (hardware_telemetry.h)
 * ============================================================================
 * Replaces abstract heuristic energy scores with bare-metal physical silicon metrics:
 * 1. Nanosecond/Cycle probe via RDTSC (x86) / CNTVCT_EL0 (ARM64 / Apple Silicon).
 * 2. Thermodynamic energy dissipation in microjoules (uJ) via Intel/AMD RAPL
 *    or calibrated physical power modeling.
 * 3. Closed-loop physical Lyapunov energy evaluation:
 *    V(x) = alpha * Cycles + beta * Microjoules + gamma * ConstraintViolation.
 * ============================================================================
 */

typedef struct {
    uint64_t start_cycles;
    uint64_t end_cycles;
    uint64_t elapsed_cycles;
    double elapsed_nanoseconds;
    double start_energy_uj;
    double end_energy_uj;
    double dissipated_energy_uj;
    double physical_power_watts;
} FlowPhysicalProbe;

/* Initialize hardware telemetry subsystem (probes timer frequency and power caps) */
int flow_hardware_telemetry_init(void);

/* Query raw hardware CPU cycle counter (RDTSC / CNTVCT_EL0) with 0 ns overhead */
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

/* Get timer frequency in Hz */
uint64_t flow_hardware_timer_frequency_hz(void);

/* Query cumulative thermodynamic energy consumed by package in microjoules (uJ) */
double flow_hardware_energy_uj(void);

/* Start physical telemetry probe */
void flow_hardware_probe_start(FlowPhysicalProbe *probe);

/* Stop physical telemetry probe and calculate elapsed cycles, ns, and energy */
void flow_hardware_probe_stop(FlowPhysicalProbe *probe);

/*
 * Closed-Loop Physical Lyapunov Metric:
 *     V_{phys} = w_cycles * (\Delta Cycles / 1e6) + w_energy * (\Delta uJ / 1e3) + V_{constraint}
 */
double flow_hardware_lyapunov_metric(const FlowPhysicalProbe *probe, double constraint_energy);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_HARDWARE_TELEMETRY_H */
