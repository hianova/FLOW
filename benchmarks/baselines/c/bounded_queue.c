/* FLOW benchmark baseline. Human decisions: 5 (capacity, layout, indices, API, overflow). */
#include <stdio.h>
#ifdef FLOW_BENCHMARK
static volatile const char *flow_benchmark_last_format;
static void flow_benchmark_sink(const char *format, ...) { flow_benchmark_last_format = format; }
#define flow_printf(...) flow_benchmark_sink(__VA_ARGS__)
#define flow_puts(text) flow_benchmark_sink(text)
#else
#define flow_printf printf
#define flow_puts puts
#endif
int main(void) {
    int queue[1024], head = 0, tail = 0, count = 0;
    for (int i = 0; i < 5; ++i) { queue[tail] = i; tail = (tail + 1) % 1024; ++count; }
    while (count > 0) { flow_printf("%d ", queue[head]); head = (head + 1) % 1024; --count; }
    flow_puts(""); return 0;
}
