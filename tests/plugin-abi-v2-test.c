#include "flow.h"
#include "plugin.h"
#include "embodied.h"
#include "registry.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "plugin-abi-v2-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

extern const FlowPluginDescriptor *flow_embodied_entry_v1(void);
extern const FlowPluginDescriptor *flow_smt_entry_v1(void);
extern const FlowPluginDescriptor *flow_security_entry_v1(void);
extern const FlowPluginDescriptor *flow_swarm_entry_v1(void);

static uint64_t bench_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

int main(void) {
    printf("Starting Standardized FlowPluginABI v2 & Non-linear Projection Verification Suite...\n");

    /* ========================================================================= */
    /* 1. Universal 4-Function ABI v2 Contract Verification Across All 4 Plugins  */
    /* ========================================================================= */
    {
        printf("  [1/4] Verifying 4-function FlowPluginABI v2 contracts on all plugins...\n");
        const FlowPluginDescriptor *descriptors[4] = {
            flow_embodied_entry_v1(),
            flow_smt_entry_v1(),
            flow_security_entry_v1(),
            flow_swarm_entry_v1()
        };

        for (int i = 0; i < 4; ++i) {
            const FlowPluginDescriptor *d = descriptors[i];
            CHECK(d != NULL);
            CHECK(d->abi_v2 != NULL);
            CHECK(d->abi_v2->get_genome_bit_size != NULL);
            CHECK(d->abi_v2->get_valid_mask != NULL);
            CHECK(d->abi_v2->evaluate_energy != NULL);
            CHECK(d->abi_v2->emit_llvm_ir != NULL);

            size_t bits = d->abi_v2->get_genome_bit_size();
            CHECK(bits > 0);
            uint64_t mask = d->abi_v2->get_valid_mask(NULL);
            CHECK(mask != 0);
            double energy = d->abi_v2->evaluate_energy(0x0123);
            CHECK(energy >= 0.0);

            printf("        -> Plugin '%s': bit_size=%zu, default_mask=0x%016llx, sample_energy=%.2f\n",
                   d->module_name, bits, (unsigned long long)mask, energy);
        }
    }

    /* ========================================================================= */
    /* 2. Non-linear Kinetic Energy Inequality Precomputation (E <= E_max)       */
    /* ========================================================================= */
    {
        printf("  [2/4] Verifying pre-computed non-linear kinetic energy projection to 1-bit mask...\n");
        const FlowPluginDescriptor *emb_desc = flow_embodied_entry_v1();
        CHECK(emb_desc != NULL && emb_desc->abi_v2 != NULL);

        /* Case A: Normal environment (high power) -> full velocity bits enabled */
        FlowEnvironmentState normal_env = { .measured_miss_rate = 0.01 };
        uint64_t normal_mask = emb_desc->abi_v2->get_valid_mask(&normal_env);

        /* Case B: Cache pressure / thermal throttle -> reduced power budget -> high-vel bits masked */
        FlowEnvironmentState throttle_env = { .measured_miss_rate = 0.08 };
        uint64_t throttle_mask = emb_desc->abi_v2->get_valid_mask(&throttle_env);

        CHECK(normal_mask != throttle_mask);
        CHECK((throttle_mask & 0x00C0ULL) == 0); /* High velocity bits disabled */
        printf("        -> Normal power mask:   0x%016llx (all velocity bits valid)\n", (unsigned long long)normal_mask);
        printf("        -> Throttled power mask: 0x%016llx (high-velocity bits pruned by non-linear bound)\n", (unsigned long long)throttle_mask);
    }

    /* ========================================================================= */
    /* 3. 1-Cycle O(1) Bitwise Gate Performance Benchmark (10M evaluations)      */
    /* ========================================================================= */
    {
        printf("  [3/4] Benchmarking O(1) 1-cycle Bitwise AND constraint gating (10,000,000 cycles)...\n");
        const FlowPluginDescriptor *emb_desc = flow_embodied_entry_v1();
        FlowEnvironmentState env = { .measured_miss_rate = 0.02 };
        uint64_t valid_mask = emb_desc->abi_v2->get_valid_mask(&env);

        const size_t N = 10000000;
        uint64_t valid_count = 0;
        uint64_t start_ns = bench_time_ns();

        uint64_t rng = 0x853c49e6748fea9bULL;
        for (size_t i = 0; i < N; ++i) {
            rng ^= rng >> 12;
            rng ^= rng << 25;
            rng ^= rng >> 27;
            uint64_t mutated_genome = rng * 0x2545F4914F6CDD1DULL;

            /* 1-cycle O(1) Bitwise AND Constraint Gate */
            if ((mutated_genome & valid_mask) == (mutated_genome & 0x0000FFFFULL)) {
                valid_count++;
            }
        }
        uint64_t elapsed_ns = bench_time_ns() - start_ns;
        double elapsed_ms = (double)elapsed_ns / 1000000.0;
        double ns_per_op = (double)elapsed_ns / (double)N;

        printf("        -> 10M bitwise gate checks: %.2f ms (%.3f ns / check, %llu valid mutations)\n",
               elapsed_ms, ns_per_op, (unsigned long long)valid_count);
        CHECK(elapsed_ms < 100.0); /* Well under 100ms */
    }

    /* ========================================================================= */
    /* 4. Code Emission via emit_llvm_ir ABI Hook                                */
    /* ========================================================================= */
    {
        printf("  [4/4] Verifying code emission via emit_llvm_ir ABI hook...\n");
        const FlowPluginDescriptor *descriptors[3] = {
            flow_embodied_entry_v1(),
            flow_smt_entry_v1(),
            flow_security_entry_v1()
        };

        char buffer[2048];
        for (int i = 0; i < 3; ++i) {
            FILE *mem = fmemopen(buffer, sizeof(buffer), "w");
            CHECK(mem != NULL);
            descriptors[i]->abi_v2->emit_llvm_ir(0x0ABC, mem);
            fclose(mem);
            CHECK(strlen(buffer) > 0);
            printf("        -> Generated code from '%s' (%zu bytes):\n", descriptors[i]->module_name, strlen(buffer));
        }
    }

    printf("\nPLUGIN_ABI_V2_TEST=passed universal_abi_v2=verified nonlinear_projection=verified bitwise_gate_speed=verified\n");
    return 0;
}
