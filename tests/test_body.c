#include "flow_test_kit.h"
#include "numa_affinity.h"
#include "simd_manifold.h"
#include "hardware_telemetry.h"
#include "embodied.h"
#include "primitive.h"
#include "flow_mock_driver.h"
#include "bus_hybrid_poll.h"
#include "cxl_fabric.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Custom 3-Function Developer Driver: eBPF XDP Packet Filter Driver */
static int custom_xdp_register(void) {
    return 1;
}

static int custom_xdp_get_bounds(FlowHardwareBounds *bounds_out) {
    if (!bounds_out) return 0;
    strncpy(bounds_out->name, "ebpf_xdp_filter", sizeof(bounds_out->name) - 1);
    bounds_out->max_queue_depth = 8192;
    bounds_out->max_buffer_bytes = 16ULL * 1024ULL * 1024ULL; /* 16 MB packet ring */
    bounds_out->supports_zero_copy = 1;
    bounds_out->is_kernel_bypass = 1;
    bounds_out->genome_bits_required = 3;
    return 1;
}

static int custom_xdp_execute(const FlowPrimitiveContext *ctx, FlowPrimitiveResult *res_out) {
    if (!ctx || !res_out) return -1;
    res_out->status_code = 0;
    res_out->bytes_transferred = ctx->data_len;
    res_out->latency_cycles = 15; /* XDP line-rate processing */
    res_out->zero_copy_active = 1;
    return 0;
}

static const FlowPrimitiveDriver s_custom_xdp_driver = {
    .driver_name = "ebpf_xdp_filter",
    .driver_version = "v1.0",
    .register_primitive = custom_xdp_register,
    .get_hardware_bounds = custom_xdp_get_bounds,
    .execute_primitive = custom_xdp_execute
};

int main(void) {
    FLOW_TEST_SUITE_BEGIN("Body: Physical Substrate, Telemetry, Drivers & Hardware Control");

    /* ========================================================================= */
    /* STAGE 1: NUMA Discovery, Core Pinning & Local Arenas                      */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(1, "NUMA Discovery, Core Pinning & Local Memory Arenas");
    {
        FlowNumaTopology topo;
        FLOW_ASSERT_EQ(flow_numa_topology_discover(&topo), 1);
        FLOW_ASSERT_TRUE(topo.total_logical_cores >= 1);
        FLOW_ASSERT_TRUE(topo.total_physical_cores >= 1);
        FLOW_ASSERT_TRUE(topo.cache_line_size >= 32);

        /* Test Thread Pinning across available cores */
        for (uint32_t c = 0; c < topo.total_logical_cores && c < 4; ++c) {
            FLOW_ASSERT_EQ(flow_numa_pin_thread(c), 1);
        }

        /* NUMA-Local Memory Allocation with 64-byte alignment */
        size_t arena_sz = 128 * 1024;
        void *arena = flow_numa_alloc_local(arena_sz);
        FLOW_ASSERT_TRUE(arena != NULL);
        FLOW_ASSERT_EQ(((uintptr_t)arena & 63), 0ULL);

        uint64_t *words = (uint64_t *)arena;
        for (size_t i = 0; i < arena_sz / sizeof(uint64_t); ++i) {
            words[i] = (uint64_t)i ^ 0xFEEDCAFEULL;
        }
        FLOW_ASSERT_EQ(words[0], 0xFEEDCAFEULL);
        FLOW_ASSERT_EQ(words[10], (10 ^ 0xFEEDCAFEULL));

        flow_numa_free_local(arena, arena_sz);
        printf("  ✓ Stage 1 Passed: NUMA topology discovered, cores pinned, 64-byte aligned arena verified.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 2: 512-Bit SIMD Vector Manifold Operations                          */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(2, "512-Bit SIMD Vector Manifold (8 x 64-Bit Lanes)");
    {
        FlowVector512 zero = flow_v512_zero();
        FlowVector512 ones = flow_v512_all_ones();
        FlowVector512 bcast = flow_v512_broadcast(0x0123456789ABCDEFULL);

        for (int i = 0; i < 8; ++i) {
            FLOW_ASSERT_EQ(zero.u64[i], 0ULL);
            FLOW_ASSERT_EQ(ones.u64[i], 0xFFFFFFFFFFFFFFFFULL);
            FLOW_ASSERT_EQ(bcast.u64[i], 0x0123456789ABCDEFULL);
        }

        FLOW_ASSERT_EQ(flow_v512_popcount(zero), 0);
        FLOW_ASSERT_EQ(flow_v512_popcount(ones), 512);

        FlowVector512 custom = flow_v512_from_u64s(
            1ULL, 3ULL, 7ULL, 15ULL,
            31ULL, 63ULL, 127ULL, 255ULL
        );
        FLOW_ASSERT_EQ(flow_v512_popcount(custom), 36);
        FLOW_ASSERT_EQ(flow_v512_horizontal_or(custom), 255ULL);
        FLOW_ASSERT_EQ(flow_v512_horizontal_and(custom), 1ULL);

        /* Discrete Attention Projection */
        FlowVector512 genome = flow_v512_broadcast(0xAAAAAAAAAAAAAAAAULL);
        FlowVector512 hard_mask = flow_v512_from_u64s(
            0xFFFFFFFFFFFFFFFFULL, 0x00000000FFFFFFFFULL, 0xFFFFFFFF00000000ULL, 0x0ULL,
            0xF0F0F0F0F0F0F0F0ULL, 0x0F0F0F0F0F0F0F0FULL, 0xAAAAAAAAAAAAAAAAULL, 0x5555555555555555ULL
        );
        FlowVector512 soft_bias = flow_v512_broadcast(0x5555555555555555ULL);
        FlowVector512 projected = flow_v512_project(genome, hard_mask, soft_bias);

        for (int i = 0; i < 8; ++i) {
            uint64_t expected = (genome.u64[i] | (soft_bias.u64[i] & hard_mask.u64[i])) & hard_mask.u64[i];
            FLOW_ASSERT_EQ(projected.u64[i], expected);
            FLOW_ASSERT_EQ((projected.u64[i] & ~hard_mask.u64[i]), 0ULL);
        }

        printf("  ✓ Stage 2 Passed: 512-bit SIMD vector manifold & attention projection sound.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 3: Physical Telemetry & Closed-Loop Thermodynamic Probing           */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(3, "Physical Telemetry & Hardware Cycle Probing");
    {
        FLOW_ASSERT_EQ(flow_hardware_telemetry_init(), 1);

        uint64_t freq = flow_hardware_timer_frequency_hz();
        FLOW_ASSERT_TRUE(freq > 1000000ULL);

        uint64_t c1 = flow_hardware_cycles();
        volatile uint64_t acc = 0;
        for (int i = 0; i < 50000; ++i) {
            acc += (uint64_t)i;
        }
        uint64_t c2 = flow_hardware_cycles();
        FLOW_ASSERT_TRUE(c2 >= c1);

        FlowPhysicalProbe probe;
        flow_hardware_probe_start(&probe);
        FLOW_ASSERT_TRUE(probe.start_cycles > 0);

        for (int i = 0; i < 100000; ++i) {
            acc = (acc * 6364136223846793005ULL) + 1ULL;
        }

        flow_hardware_probe_stop(&probe);
        FLOW_ASSERT_TRUE(probe.end_cycles >= probe.start_cycles);
        FLOW_ASSERT_TRUE(probe.elapsed_cycles > 0);
        FLOW_ASSERT_TRUE(probe.elapsed_nanoseconds > 0.0);

        printf("  ✓ Stage 3 Passed: Physical hardware timer (%llu Hz) & cycle probe (elapsed: %.1fns) active.\n\n",
               (unsigned long long)freq, probe.elapsed_nanoseconds);
    }

    /* ========================================================================= */
    /* STAGE 4: Embodied Dual-Rate Physics & CoM Stability                       */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(4, "Embodied Dual-Rate Physics: Joint Limits & ZMP Invariant");
    {
        FlowPhysicsEngine phys;
        FLOW_ASSERT_EQ(flow_physics_init(&phys, 6, 25.0), 1);
        FLOW_ASSERT_EQ(phys.current_state.joint_count, 6);
        FLOW_ASSERT_EQ((int)phys.current_state.mass_kg, 25);

        /* Safe torques test */
        double safe_torques[6] = { 10.0, -15.0, 20.0, -5.0, 8.0, 12.0 };
        FLOW_ASSERT_TRUE(flow_physics_is_torque_safe(&phys.current_state, safe_torques));
        FLOW_ASSERT_EQ(flow_physics_simulate_step(&phys, safe_torques), 1);
        FLOW_ASSERT_EQ(phys.simulated_steps_total, 1);
        FLOW_ASSERT_EQ(phys.violations_prevented_total, 0);

        /* Over-torque burnout prevention: 120 N*m exceeds 80 N*m limit */
        double dangerous_torques[6] = { 120.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
        FLOW_ASSERT_FALSE(flow_physics_is_torque_safe(&phys.current_state, dangerous_torques));
        FLOW_ASSERT_EQ(flow_physics_simulate_step(&phys, dangerous_torques), 0);
        FLOW_ASSERT_EQ(phys.violations_prevented_total, 1);

        /* ZMP tip-over polygon verification */
        phys.current_state.zmp_position[0] = 0.50; /* 0.50m outside 0.15m polygon */
        phys.current_state.zmp_position[1] = 0.0;
        FLOW_ASSERT_FALSE(flow_physics_is_zmp_stable(&phys.current_state));

        phys.current_state.zmp_position[0] = 0.0;
        phys.current_state.zmp_position[1] = 0.0;
        FLOW_ASSERT_TRUE(flow_physics_is_zmp_stable(&phys.current_state));

        printf("  ✓ Stage 4 Passed: Joint torque safe limits verified & ZMP tip-over violation prevented.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 5: Minimalist 3-Function Primitive Drivers                          */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(5, "Minimalist 3-Function Driver ABI & Hardware Registry");
    {
        FlowPrimitiveRegistry registry;
        flow_primitive_registry_init(&registry);
        FLOW_ASSERT_EQ(flow_primitive_count(&registry), 0);

        FLOW_ASSERT_EQ(flow_primitive_register(&registry, &s_custom_xdp_driver), 1);
        FLOW_ASSERT_EQ(flow_primitive_count(&registry), 1);

        const FlowPrimitiveDriver *d = flow_primitive_lookup(&registry, "ebpf_xdp_filter");
        FLOW_ASSERT_TRUE(d != NULL);

        FlowHardwareBounds bounds;
        FLOW_ASSERT_EQ(d->get_hardware_bounds(&bounds), 1);
        FLOW_ASSERT_EQ(bounds.max_queue_depth, 8192);
        FLOW_ASSERT_TRUE(bounds.supports_zero_copy);
        FLOW_ASSERT_TRUE(bounds.is_kernel_bypass);

        uint8_t payload[64] = "FLOW_XDP_PAYLOAD_TEST";
        FlowPrimitiveContext ctx = {
            .active_genome = 0x1ULL,
            .user_data = payload,
            .data_len = sizeof(payload),
            .flags = 0
        };
        FlowPrimitiveResult res;
        FLOW_ASSERT_EQ(d->execute_primitive(&ctx, &res), 0);
        FLOW_ASSERT_EQ(res.status_code, 0);
        FLOW_ASSERT_EQ(res.bytes_transferred, sizeof(payload));
        FLOW_ASSERT_TRUE(res.zero_copy_active);

        printf("  ✓ Stage 5 Passed: 3-function primitive driver registered and executed line-rate.\n\n");
    }

    /* ========================================================================= */
    /* STAGE 6: Bus Hybrid Polling & Moreau Normal Cone Hysteresis               */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(6, "Bus Hybrid Polling & Non-Smooth Anti-Flapping Hysteresis");
    {
        FlowBusHybridPoll bus;
        FLOW_ASSERT_EQ(flow_bus_hybrid_init(&bus, "gpu_command_bus", 4.0, 16.0, 2000.0), 1);
        FLOW_ASSERT_EQ(bus.current_mode, FLOW_BUS_MODE_INTERRUPT);

        /* Low traffic -> INTERRUPT */
        for (int q = 1; q < 16; ++q) {
            FlowBusMode mode = flow_bus_hybrid_evaluate_phase(&bus, (double)q);
            FLOW_ASSERT_EQ(mode, FLOW_BUS_MODE_INTERRUPT);
        }

        /* Crossing threshold (q >= 16) -> BUSY_POLL */
        FlowBusMode mode_enter = flow_bus_hybrid_evaluate_phase(&bus, 16.0);
        FLOW_ASSERT_EQ(mode_enter, FLOW_BUS_MODE_BUSY_POLL);
        FLOW_ASSERT_EQ(bus.current_mode, FLOW_BUS_MODE_BUSY_POLL);

        /* Oscillations inside [4, 16] must NOT flap */
        for (int i = 0; i < 50; ++i) {
            double noise_q = 6.0 + (double)(i % 8);
            FlowBusMode m = flow_bus_hybrid_evaluate_phase(&bus, noise_q);
            FLOW_ASSERT_EQ(m, FLOW_BUS_MODE_BUSY_POLL);
        }
        FLOW_ASSERT_TRUE(bus.flutters_suppressed >= 40);

        /* Draining below exit threshold (q <= 4) -> Fallback to INTERRUPT */
        FlowBusMode mode_exit = flow_bus_hybrid_evaluate_phase(&bus, 3.0);
        FLOW_ASSERT_EQ(mode_exit, FLOW_BUS_MODE_INTERRUPT);

        FlowSMTProofAttestation bus_proof;
        memset(&bus_proof, 0, sizeof(bus_proof));
        FLOW_ASSERT_EQ(flow_bus_hybrid_verify_smt(&bus, &bus_proof), FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_SMT_SOUND(bus_proof);

        printf("  ✓ Stage 6 Passed: Hybrid polling switched seamlessly; %llu flutters suppressed.\n\n",
               (unsigned long long)bus.flutters_suppressed);
    }

    /* ========================================================================= */
    /* STAGE 7: CXL Heterogeneous Memory Fabric & LLM KV Cache Tiering           */
    /* ========================================================================= */
    FLOW_STAGE_BEGIN(7, "CXL Heterogeneous Memory Fabric: Tiered KV Cache");
    {
        FlowCxlFabric fabric;
        FLOW_ASSERT_EQ(flow_cxl_init(&fabric), 1);
        FLOW_ASSERT_EQ(fabric.cap_hbm, 32);
        FLOW_ASSERT_EQ(fabric.cap_ddr5, 128);
        FLOW_ASSERT_EQ(fabric.cap_cxl, 352);

        uint64_t pid1, pid33;
        /* Session 1: HBM allocation */
        FLOW_ASSERT_EQ(flow_cxl_allocate_kv_page(&fabric, 1, 0, 128, 0.85, &pid1), 1);
        FLOW_ASSERT_EQ(fabric.pages[0].current_tier, FLOW_CXL_TIER_HBM);

        /* Saturate Tier 0 HBM */
        for (int i = 1; i < 32; ++i) {
            uint64_t pid;
            FLOW_ASSERT_EQ(flow_cxl_allocate_kv_page(&fabric, 1, i * 128, 128, 0.80, &pid), 1);
        }
        FLOW_ASSERT_EQ(fabric.count_hbm, 32);

        /* Page 33 overflows into Tier 1 DDR5 */
        FLOW_ASSERT_EQ(flow_cxl_allocate_kv_page(&fabric, 2, 0, 128, 0.75, &pid33), 1);
        FLOW_ASSERT_EQ(fabric.pages[32].current_tier, FLOW_CXL_TIER_DDR5);
        FLOW_ASSERT_EQ(fabric.count_ddr5, 1);

        /* Access page */
        uint8_t buffer[FLOW_CXL_PAGE_SIZE];
        uint64_t lat_ns = 0;
        FLOW_ASSERT_EQ(flow_cxl_access_kv_page(&fabric, pid1, buffer, &lat_ns), 1);
        FLOW_ASSERT_TRUE(lat_ns <= 50); /* HBM latency <= 50ns */

        FlowSMTProofAttestation cxl_proof;
        memset(&cxl_proof, 0, sizeof(cxl_proof));
        FLOW_ASSERT_EQ(flow_cxl_verify_smt(&fabric, 2, &cxl_proof), FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_SMT_SOUND(cxl_proof);

        printf("  ✓ Stage 7 Passed: CXL multi-tier KV cache allocation & sub-microsecond access verified.\n\n");
    }

    FLOW_TEST_SUITE_END();
    return 0;
}
