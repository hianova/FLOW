/* FLOW benchmark baseline. Human decisions: 6 (worker model, split, join, output, transform, scheduling). */
#include <stdio.h>
#ifdef FLOW_BENCHMARK
static volatile const char *flow_benchmark_last_format;
static void flow_benchmark_sink(const char *format, ...) { flow_benchmark_last_format = format; }
#define flow_printf(...) flow_benchmark_sink(__VA_ARGS__)
#else
#define flow_printf printf
#endif
int main(void) {
    const int input[] = {91, 74, 99, 86, 95}; int output[5];
    for (int i = 0; i < 5; ++i) output[i] = input[i] * 2;
    flow_printf("mapped %d\n", output[0]); return 0;
}
