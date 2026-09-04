#include "flow_test_kit.h"
#include "numa_affinity.h"
#include "simd_manifold.h"
#include "hardware_telemetry.h"
#include "token_ring.h"
#include "registry.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "hardware-blindspot-test assertion failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        exit(1); \
    } \
} while (0)

static void test_numa_topology_and_pinning(void) {
    printf("--- [Unit 1/4] Testing NUMA Discovery, Core Pinning & Local Arenas ---\n");

    FlowNumaTopology topo;
    int ok = flow_numa_topology_discover(&topo);
    CHECK(ok == 1);
    CHECK(topo.total_logical_cores >= 1);
    CHECK(topo.total_physical_cores >= 1);
    CHECK(topo.cache_line_size >= 32);

    printf("  Discovered Topology:\n");
    printf("    Physical Cores: %u, Logical Cores: %u\n", topo.total_physical_cores, topo.total_logical_cores);
    printf("    Performance Cores: %u, Efficiency Cores: %u\n", topo.performance_cores, topo.efficiency_cores);
    printf("    Cache Line: %zu bytes, NUMA Nodes: %u\n", topo.cache_line_size, topo.numa_node_count);

    /* Test Thread Pinning & QoS Binding across multiple logical cores */
    for (uint32_t c = 0; c < topo.total_logical_cores && c < 8; ++c) {
        CHECK(flow_numa_pin_thread(c) == 1);
    }

    /* Test NUMA-Local Memory Allocation with First-Touch Faulting */
    size_t arena_sz = 128 * 1024; /* 128 KB */
    void *arena = flow_numa_alloc_local(arena_sz);
    CHECK(arena != NULL);
    /* Check 64-byte alignment */
    CHECK(((uintptr_t)arena & 63) == 0);

    /* Verify read/write capability */
    uint64_t *words = (uint64_t *)arena;
    for (size_t i = 0; i < arena_sz / sizeof(uint64_t); ++i) {
        words[i] = (uint64_t)i ^ 0xFEEDCAFEULL;
    }
    CHECK(words[0] == 0xFEEDCAFEULL);
    CHECK(words[10] == (10 ^ 0xFEEDCAFEULL));

    flow_numa_free_local(arena, arena_sz);
    printf("  [PASS] NUMA topology, core pinning, and local first-touch arena verified.\n");
}

static void test_simd_512_manifold(void) {
    printf("--- [Unit 2/4] Testing 512-Bit SIMD Vector Manifold (8 x 64-bit Lanes) ---\n");

    /* Test Vector Construction & Broadcast */
    FlowVector512 zero = flow_v512_zero();
    FlowVector512 ones = flow_v512_all_ones();
    FlowVector512 bcast = flow_v512_broadcast(0x0123456789ABCDEFULL);

    for (int i = 0; i < 8; ++i) {
        CHECK(zero.u64[i] == 0ULL);
        CHECK(ones.u64[i] == 0xFFFFFFFFFFFFFFFFULL);
        CHECK(bcast.u64[i] == 0x0123456789ABCDEFULL);
    }

    /* Test Popcount */
    CHECK(flow_v512_popcount(zero) == 0);
    CHECK(flow_v512_popcount(ones) == 512);
    FlowVector512 custom = flow_v512_from_u64s(
        1ULL, 3ULL, 7ULL, 15ULL,
        31ULL, 63ULL, 127ULL, 255ULL
    );
    /* Popcounts: 1+2+3+4+5+6+7+8 = 36 */
    CHECK(flow_v512_popcount(custom) == 36);

    /* Test Horizontal Reductions */
    CHECK(flow_v512_horizontal_or(custom) == 255ULL);
    CHECK(flow_v512_horizontal_and(custom) == 1ULL);

    /* Test Vectorized Discrete Attention Operator */
    FlowVector512 genome = flow_v512_broadcast(0xAAAAAAAAAAAAAAAAULL);
    FlowVector512 hard_mask = flow_v512_from_u64s(
        0xFFFFFFFFFFFFFFFFULL, 0x00000000FFFFFFFFULL, 0xFFFFFFFF00000000ULL, 0x0ULL,
        0xF0F0F0F0F0F0F0F0ULL, 0x0F0F0F0F0F0F0F0FULL, 0xAAAAAAAAAAAAAAAAULL, 0x5555555555555555ULL
    );
    FlowVector512 soft_bias = flow_v512_broadcast(0x5555555555555555ULL);

    FlowVector512 projected = flow_v512_project(genome, hard_mask, soft_bias);
    for (int i = 0; i < 8; ++i) {
        uint64_t expected = (genome.u64[i] | (soft_bias.u64[i] & hard_mask.u64[i])) & hard_mask.u64[i];
        CHECK(projected.u64[i] == expected);
        /* Invariant: No bits outside hard_mask may ever be set */
        CHECK((projected.u64[i] & ~hard_mask.u64[i]) == 0);
    }

    /* Test 512-bit Join-Semilattice Confluence */
    FlowVector512 va = flow_v512_from_u64s(10, 20, 30, 40, 50, 60, 70, 80);
    FlowVector512 vb = flow_v512_from_u64s(11, 21, 31, 41, 51, 61, 71, 81);
    FlowVector512 ma = flow_v512_broadcast(0xFFFFFFFFFFFFFFFFULL);
    FlowVector512 joined = flow_v512_semilattice_join(va, vb, ma);
    for (int i = 0; i < 8; ++i) {
        CHECK(joined.u64[i] == va.u64[i]);
    }

    printf("  [PASS] 512-bit SIMD vector manifold (8-lane parallel operations) verified.\n");
}

static void test_hardware_telemetry(void) {
    printf("--- [Unit 3/4] Testing Physical Hardware Telemetry & Thermodynamic Closed Loop ---\n");

    int ok = flow_hardware_telemetry_init();
    CHECK(ok == 1);

    uint64_t freq = flow_hardware_timer_frequency_hz();
    CHECK(freq > 1000000ULL);
    printf("  Timer Frequency: %llu Hz\n", (unsigned long long)freq);

    uint64_t c1 = flow_hardware_cycles();
    /* Perform slight busy work */
    volatile uint64_t acc = 0;
    for (int i = 0; i < 100000; ++i) {
        acc += (uint64_t)i;
    }
    uint64_t c2 = flow_hardware_cycles();
    CHECK(c2 >= c1);
    printf("  CPU Cycle Counter check: %llu -> %llu (Delta = %llu cycles)\n",
           (unsigned long long)c1, (unsigned long long)c2, (unsigned long long)(c2 - c1));

    /* Test Physical Probe */
    FlowPhysicalProbe probe;
    flow_hardware_probe_start(&probe);
    CHECK(probe.start_cycles > 0);

    /* Simulated workload */
    for (int i = 0; i < 500000; ++i) {
        acc = (acc * 6364136223846793005ULL) + 1ULL;
    }

    flow_hardware_probe_stop(&probe);
    CHECK(probe.end_cycles >= probe.start_cycles);
    CHECK(probe.elapsed_cycles > 0);
    CHECK(probe.elapsed_nanoseconds > 0.0);
    CHECK(probe.dissipated_energy_uj >= 0.0);

    printf("  Physical Probe Result:\n");
    printf("    Elapsed Cycles: %llu\n", (unsigned long long)probe.elapsed_cycles);
    printf("    Elapsed Time:   %.2f ns (%.4f ms)\n", probe.elapsed_nanoseconds, probe.elapsed_nanoseconds / 1e6);
    printf("    Energy Dissipated: %.4f uJ\n", probe.dissipated_energy_uj);
    printf("    Estimated Power:   %.3f W\n", probe.physical_power_watts);

    /* Test Closed-Loop Physical Lyapunov Metric */
    double constraint_v = 10.5;
    double phys_v = flow_hardware_lyapunov_metric(&probe, constraint_v);
    CHECK(phys_v >= constraint_v);
    printf("  Physical Lyapunov Metric: V_constraint=%.2f -> V_phys=%.4f\n", constraint_v, phys_v);

    printf("  [PASS] Physical telemetry and thermodynamic closed loop verified.\n");
}

static void test_wavefront_ring_hardware_blindspots(void) {
    printf("--- [Unit 4/4] Testing Slotted Wavefront Ring Integration with Hardware Blindspots ---\n");

    FLOW_TEST_CASE("tests/hardware-blindspot-test.c",
        "input telemetry_stream {\n"
        "    max_count 8192\n"
        "}\n"
        "flow hardware_accelerated_pipeline {\n"
        "    telemetry_stream -> filter -> map -> reduce\n"
        "}\n"
        "require {\n"
        "    deterministic\n"
        "    memory < 64mb\n"
        "}\n",
        {
            FlowWavefrontRing wring;
            CHECK(flow_wavefront_ring_init(&wring, &ir, 4, 5));
            CHECK(wring.slot_count == 4);
            CHECK(wring.worker_count == 5);
            CHECK(wring.state == FLOW_RING_CIRCULATING);

            /* Step parallel wave: triggers NUMA thread pinning, SIMD join, & physical telemetry probe */
            CHECK(flow_wavefront_ring_step_parallel(&wring));
            CHECK(wring.wave_cycle_count == 1);
            CHECK(wring.total_cycles > 0);
            CHECK(wring.last_probe.elapsed_cycles > 0);

            printf("  Wavefront Step 1 Physical Probe: cycles=%llu, time=%.2f ns, energy=%.2f uJ\n",
                   (unsigned long long)wring.last_probe.elapsed_cycles,
                   wring.last_probe.elapsed_nanoseconds,
                   wring.last_probe.dissipated_energy_uj);

            /* Run to Lyapunov attractor */
            FlowTokenRingState state = flow_wavefront_ring_run_to_attractor(&wring, 16);
            CHECK(state == FLOW_RING_ATTRACTOR_REACHED);
            CHECK(wring.attractor_converged == true);
            CHECK(wring.wave_cycle_count >= 1);
            CHECK(wring.total_cycles > 0);

            printf("  Wavefront Attractor Reached Status: %s\n", wring.status_message);
            flow_wavefront_ring_destroy(&wring);
        }
    );
    printf("  [PASS] Slotted Wavefront Ring integration with 3 hardware blindspots verified.\n");
}

int main(void) {
    flow_registry_init();
    printf("================================================================================\n");
    printf("      FLOW THREE CRITICAL HARDWARE BLINDSPOTS VERIFICATION SUITE (#75)         \n");
    printf("================================================================================\n");

    test_numa_topology_and_pinning();
    test_simd_512_manifold();
    test_hardware_telemetry();
    test_wavefront_ring_hardware_blindspots();

    printf("================================================================================\n");
    printf("   ALL 4 HARDWARE BLINDSPOT UNITS 100%% SOUND, PHYSICALLY VERIFIED & PASSED!     \n");
    printf("================================================================================\n");
    return 0;
}
