#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t bench_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static inline uint64_t xorshift64(uint64_t *state) {
    uint64_t x = *state;
    if (x == 0) x = UINT64_C(0x9e3779b97f4a7c15);
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

static inline double eval_landscape_f64(uint32_t x, uint32_t y) {
    if (x == 1 && y == 1) return -500.0;
    if (x == 0 && y == 0) return 50.0;
    return 150.0;
}

#define Q16_SCALE 65536
static inline int32_t eval_landscape_int_q16(uint32_t x, uint32_t y) {
    if (x == 1 && y == 1) return -500 * Q16_SCALE;
    if (x == 0 && y == 0) return 50 * Q16_SCALE;
    return 150 * Q16_SCALE;
}

static uint16_t INT_EXP_LUT[2048];
static void init_int_exp_lut(void) {
    for (int i = 0; i < 2048; ++i) {
        double val = (double)i / 256.0;
        double p = exp(-val);
        INT_EXP_LUT[i] = (uint16_t)(p * 65535.0);
    }
}

/* Method A: Standard Float64 with libm exp() */
static uint64_t run_bench_f64(size_t total_ops, uint64_t seed, double *elapsed_ms_out) {
    uint64_t rng = seed;
    uint32_t x = 0, y = 0;
    double curr_e = eval_landscape_f64(x, y);
    double temp = 80.0;
    size_t stagnation = 0;
    uint64_t checksum = 0;

    uint64_t t0 = bench_time_ns();
    for (size_t i = 0; i < total_ops; ++i) {
        uint32_t cx = x, cy = y;
        if (xorshift64(&rng) & 1) cx ^= 1;
        else cy ^= 1;

        double ce = eval_landscape_f64(cx, cy);
        double delta = ce - curr_e;

        if (delta < 0.0) {
            x = cx; y = cy; curr_e = ce; stagnation = 0;
            checksum += x + y;
        } else {
            double r = (double)(xorshift64(&rng) % 10000) / 10000.0;
            if (temp > 0.001 && r < exp(-delta / temp)) {
                x = cx; y = cy; curr_e = ce; stagnation = 0;
                checksum += x + y;
            } else {
                if (++stagnation >= 5) { temp = 80.0 * 0.6; stagnation = 0; }
            }
        }
        temp *= 0.95;
        if (temp < 0.0001) temp = 0.0001;
    }
    uint64_t t1 = bench_time_ns();
    *elapsed_ms_out = (double)(t1 - t0) / 1000000.0;
    return checksum + x + y;
}

/* Method B: Hybrid Float Energy + Integer L1D LUT Acceptance */
static uint64_t run_bench_hybrid(size_t total_ops, uint64_t seed, double *elapsed_ms_out) {
    uint64_t rng = seed;
    uint32_t x = 0, y = 0;
    double curr_e = eval_landscape_f64(x, y);
    double temp = 80.0;
    size_t stagnation = 0;
    uint64_t checksum = 0;

    uint64_t t0 = bench_time_ns();
    for (size_t i = 0; i < total_ops; ++i) {
        uint32_t cx = x, cy = y;
        if (xorshift64(&rng) & 1) cx ^= 1;
        else cy ^= 1;

        double ce = eval_landscape_f64(cx, cy);
        double delta = ce - curr_e;

        if (delta <= 0.0) {
            x = cx; y = cy; curr_e = ce; stagnation = 0;
            checksum += x + y;
        } else {
            if (temp > 0.001) {
                double ratio = (delta * 256.0) / temp;
                if (ratio < 2048.0) {
                    uint16_t p = INT_EXP_LUT[(uint32_t)ratio];
                    uint16_t r = (uint16_t)xorshift64(&rng);
                    if (r < p) {
                        x = cx; y = cy; curr_e = ce; stagnation = 0;
                        checksum += x + y;
                        goto cooled_hybrid;
                    }
                }
            }
            if (++stagnation >= 5) { temp = 80.0 * 0.6; stagnation = 0; }
        }
cooled_hybrid:
        temp *= 0.95;
        if (temp < 0.0001) temp = 0.0001;
    }
    uint64_t t1 = bench_time_ns();
    *elapsed_ms_out = (double)(t1 - t0) / 1000000.0;
    return checksum + x + y;
}

/* Method C: Pure Integer Fixed-Point Q16.16 */
static uint64_t run_bench_int_q16(size_t total_ops, uint64_t seed, double *elapsed_ms_out) {
    uint64_t rng = seed;
    uint32_t x = 0, y = 0;
    int32_t curr_e = eval_landscape_int_q16(x, y);
    int32_t temp = 80 * Q16_SCALE;
    size_t stagnation = 0;
    uint64_t checksum = 0;

    uint64_t t0 = bench_time_ns();
    for (size_t i = 0; i < total_ops; ++i) {
        uint32_t cx = x, cy = y;
        if (xorshift64(&rng) & 1) cx ^= 1;
        else cy ^= 1;

        int32_t ce = eval_landscape_int_q16(cx, cy);
        int32_t delta = ce - curr_e;

        if (delta <= 0) {
            x = cx; y = cy; curr_e = ce; stagnation = 0;
            checksum += x + y;
        } else {
            if (temp > 65) {
                int64_t ratio_idx = ((int64_t)delta << 8) / temp;
                if (ratio_idx < 2048) {
                    uint16_t p = INT_EXP_LUT[ratio_idx];
                    uint16_t r = (uint16_t)xorshift64(&rng);
                    if (r < p) {
                        x = cx; y = cy; curr_e = ce; stagnation = 0;
                        checksum += x + y;
                        goto cooled_int;
                    }
                }
            }
            if (++stagnation >= 5) { temp = (int32_t)(((int64_t)(80 * Q16_SCALE) * 39321) >> 16); stagnation = 0; }
        }
cooled_int:
        temp = (int32_t)(((int64_t)temp * 62259) >> 16);
        if (temp < 6) temp = 6;
    }
    uint64_t t1 = bench_time_ns();
    *elapsed_ms_out = (double)(t1 - t0) / 1000000.0;
    return checksum + x + y;
}

int main(void) {
    init_int_exp_lut();
    const size_t N = 10000000;
    double t_a = 0, t_b = 0, t_c = 0;
    volatile uint64_t sink = 0;

    sink += run_bench_f64(100000, 42, &t_a);
    sink += run_bench_hybrid(100000, 42, &t_b);
    sink += run_bench_int_q16(100000, 42, &t_c);

    sink += run_bench_f64(N, 12345, &t_a);
    sink += run_bench_hybrid(N, 12345, &t_b);
    sink += run_bench_int_q16(N, 12345, &t_c);

    printf("10 Million Iterations Benchmark (sink=%llu):\n", (unsigned long long)sink);
    printf("  Method A (Double + libm exp):    %6.2f ms | %5.2f ns/op | %6.2f Mops/s\n", t_a, (t_a*1e6)/N, (N/t_a)/1e3);
    printf("  Method B (Hybrid + Integer LUT): %6.2f ms | %5.2f ns/op | %6.2f Mops/s\n", t_b, (t_b*1e6)/N, (N/t_b)/1e3);
    printf("  Method C (Pure Integer Q16):     %6.2f ms | %5.2f ns/op | %6.2f Mops/s\n", t_c, (t_c*1e6)/N, (N/t_c)/1e3);

    return 0;
}
