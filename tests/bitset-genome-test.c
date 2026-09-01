#include "bitspace.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "bitset-genome-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

int main(void) {
    /* 1. Test 1024-bit initialization */
    FlowGenome g;
    flow_genome_init(&g, 1024);
    CHECK(g.total_bits == 1024);
    CHECK(g.active_words == 16);
    for (int i = 0; i < 16; ++i) {
        CHECK(g.words[i] == 0);
    }

    /* 2. Test bit manipulation across all 16 words */
    flow_genome_set_bit(&g, 0, 1);
    flow_genome_set_bit(&g, 63, 1);
    flow_genome_set_bit(&g, 64, 1);
    flow_genome_set_bit(&g, 512, 1);
    flow_genome_set_bit(&g, 1023, 1);

    CHECK(flow_genome_get_bit(&g, 0) == 1);
    CHECK(flow_genome_get_bit(&g, 1) == 0);
    CHECK(flow_genome_get_bit(&g, 63) == 1);
    CHECK(flow_genome_get_bit(&g, 64) == 1);
    CHECK(flow_genome_get_bit(&g, 512) == 1);
    CHECK(flow_genome_get_bit(&g, 1023) == 1);
    CHECK(flow_genome_get_bit(&g, 1022) == 0);

    /* Flip bit */
    flow_genome_flip_bit(&g, 512);
    CHECK(flow_genome_get_bit(&g, 512) == 0);
    flow_genome_flip_bit(&g, 512);
    CHECK(flow_genome_get_bit(&g, 512) == 1);

    /* 3. Test O(1) 1-bit chaotic mutation correctness across 1024-bit space */
    uint64_t rng = 0xabcdef12345678ULL;
    uint32_t word_hits[16] = {0};

    const size_t VERIFY_MUTATIONS = 50000;
    for (size_t i = 0; i < VERIFY_MUTATIONS; ++i) {
        FlowGenome prev = g;
        uint32_t mutated_bit = 0;
        flow_genome_mutate_1bit(&g, &rng, &mutated_bit);

        CHECK(mutated_bit < 1024);
        word_hits[mutated_bit / 64]++;

        /* Verify exact 1-bit hamming distance */
        int diff_count = 0;
        for (int w = 0; w < 16; ++w) {
            uint64_t xor_diff = g.words[w] ^ prev.words[w];
            if (xor_diff != 0) {
                CHECK((xor_diff & (xor_diff - 1)) == 0);
                diff_count++;
            }
        }
        CHECK(diff_count == 1);
    }

    /* Verify all 16 words were mutated (uniform distribution across 1024 bits) */
    for (int w = 0; w < 16; ++w) {
        CHECK(word_hits[w] > (VERIFY_MUTATIONS / 32)); /* Substantial hits in every chunk */
    }

    /* 4. Benchmark Pure O(1) 1-Bit Mutation Speed */
    const size_t BENCH_MUTATIONS = 1000000;
    uint64_t start_ns = get_time_ns();
    for (size_t i = 0; i < BENCH_MUTATIONS; ++i) {
        uint32_t bit = 0;
        flow_genome_mutate_1bit(&g, &rng, &bit);
    }
    uint64_t elapsed_ns = get_time_ns() - start_ns;
    double ns_per_mutation = (double)elapsed_ns / (double)BENCH_MUTATIONS;

    /* Pure O(1) 1-bit mutation takes only a few nanoseconds */
    CHECK(ns_per_mutation < 50.0);

    printf("BITSET_GENOME_TEST=passed total_bits=1024 active_words=16 bench_mutations=%zu speed=%.2f_ns/mutation hamming_dist_1=verified\n",
           BENCH_MUTATIONS, ns_per_mutation);
    return 0;
}
