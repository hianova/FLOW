#ifndef FLOW_BENCHMARK_HARNESS_H
#define FLOW_BENCHMARK_HARNESS_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW Benchmark Statistical Measurement Harness (flow_benchmark_harness.h)
 * ============================================================================
 *
 * Provides pure C17 zero-dependency benchmark execution, latency distribution
 * sampling (P50, P90, P99, Min, Max), throughput (ops/sec) calculation,
 * CPU cache warm-up, and standardized Scorecard output.
 * ============================================================================
 */

#define FLOW_BENCHMARK_SAMPLE_CAP 1024

typedef struct {
    char name[64];
    uint64_t iterations;
    double elapsed_ms;
    double qps;
    double ns_per_op;
    double p50_ns;
    double p90_ns;
    double p99_ns;
    double min_ns;
    double max_ns;
} FlowBenchmarkResult;

static inline uint64_t flow_benchmark_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static inline int flow_benchmark_cmp_u64(const void *a, const void *b) {
    uint64_t va = *(const uint64_t *)a;
    uint64_t vb = *(const uint64_t *)b;
    return (va > vb) - (va < vb);
}

static inline void flow_benchmark_compute_stats(FlowBenchmarkResult *res,
                                                const char *name,
                                                uint64_t iterations,
                                                uint64_t total_ns,
                                                uint64_t *samples,
                                                size_t sample_count) {
    if (res == NULL) return;
    memset(res, 0, sizeof(*res));
    if (name) {
        strncpy(res->name, name, sizeof(res->name) - 1);
    }
    res->iterations = iterations;
    res->elapsed_ms = (double)total_ns / 1000000.0;
    if (res->elapsed_ms <= 0.0) {
        res->elapsed_ms = 0.000001;
    }
    res->qps = ((double)iterations / res->elapsed_ms) * 1000.0;
    res->ns_per_op = (iterations > 0) ? ((double)total_ns / (double)iterations) : 0.0;

    if (sample_count > 0 && samples != NULL) {
        qsort(samples, sample_count, sizeof(uint64_t), flow_benchmark_cmp_u64);
        res->min_ns = (double)samples[0];
        res->max_ns = (double)samples[sample_count - 1];
        size_t idx50 = (size_t)(sample_count * 0.50);
        size_t idx90 = (size_t)(sample_count * 0.90);
        size_t idx99 = (size_t)(sample_count * 0.99);
        if (idx50 >= sample_count) idx50 = sample_count - 1;
        if (idx90 >= sample_count) idx90 = sample_count - 1;
        if (idx99 >= sample_count) idx99 = sample_count - 1;
        res->p50_ns = (double)samples[idx50];
        res->p90_ns = (double)samples[idx90];
        res->p99_ns = (double)samples[idx99];
    } else {
        res->p50_ns = res->ns_per_op;
        res->p90_ns = res->ns_per_op;
        res->p99_ns = res->ns_per_op;
        res->min_ns = res->ns_per_op;
        res->max_ns = res->ns_per_op;
    }
}

static inline void flow_benchmark_print_header(void) {
    printf("====================================================================================================\n");
    printf("  BENCHMARK NAME          | ITERATIONS | TIME (ms) | THROUGHPUT (ops/s) | AVG (ns) | P50 (ns) | P99 (ns)\n");
    printf("--------------------------+------------+-----------+--------------------+----------+----------+---------\n");
}

static inline void flow_benchmark_print_row(const FlowBenchmarkResult *res) {
    if (res == NULL) return;
    printf("  %-23s | %10llu | %9.2f | %18.0f | %8.2f | %8.2f | %7.2f\n",
           res->name,
           (unsigned long long)res->iterations,
           res->elapsed_ms,
           res->qps,
           res->ns_per_op,
           res->p50_ns,
           res->p99_ns);
}

static inline void flow_benchmark_print_scorecard(const FlowBenchmarkResult *results, size_t count) {
    if (results == NULL || count == 0) return;
    flow_benchmark_print_header();
    for (size_t i = 0; i < count; ++i) {
        flow_benchmark_print_row(&results[i]);
    }
    printf("====================================================================================================\n\n");
}

/**
 * FLOW_BENCHMARK_RUN:
 * Executes automatic cache warm-up, total timing loop, latency distribution sampling,
 * and fills out a FlowBenchmarkResult.
 */
#define FLOW_BENCHMARK_RUN(name_str, iter_count, code_block, result_ptr) \
    do { \
        uint64_t _fb_iters = (uint64_t)(iter_count); \
        /* 1. CPU Cache & Branch Predictor Warm-up (up to 500 iterations) */ \
        uint64_t _fb_warmup = (_fb_iters / 20 > 500) ? 500 : (_fb_iters / 20); \
        if (_fb_warmup < 5 && _fb_iters >= 5) _fb_warmup = 5; \
        for (uint64_t _w = 0; _w < _fb_warmup; ++_w) { \
            code_block; \
        } \
        \
        /* 2. Main High-Throughput Measurement Loop */ \
        uint64_t _fb_t0 = flow_benchmark_now_ns(); \
        for (uint64_t _i = 0; _i < _fb_iters; ++_i) { \
            code_block; \
        } \
        uint64_t _fb_t1 = flow_benchmark_now_ns(); \
        uint64_t _fb_total_ns = (_fb_t1 > _fb_t0) ? (_fb_t1 - _fb_t0) : 1; \
        \
        /* 3. Latency Distribution Sampling (up to FLOW_BENCHMARK_SAMPLE_CAP) */ \
        uint64_t _fb_samples[FLOW_BENCHMARK_SAMPLE_CAP]; \
        size_t _fb_sample_target = (_fb_iters < FLOW_BENCHMARK_SAMPLE_CAP) ? (size_t)_fb_iters : FLOW_BENCHMARK_SAMPLE_CAP; \
        if (_fb_sample_target > 256) _fb_sample_target = 256; /* Keep sampling fast */ \
        for (size_t _s = 0; _s < _fb_sample_target; ++_s) { \
            uint64_t _st0 = flow_benchmark_now_ns(); \
            code_block; \
            uint64_t _st1 = flow_benchmark_now_ns(); \
            _fb_samples[_s] = (_st1 >= _st0) ? (_st1 - _st0) : 0; \
        } \
        \
        /* 4. Aggregate and Populate Statistics */ \
        flow_benchmark_compute_stats((result_ptr), (name_str), _fb_iters, _fb_total_ns, _fb_samples, _fb_sample_target); \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* FLOW_BENCHMARK_HARNESS_H */
