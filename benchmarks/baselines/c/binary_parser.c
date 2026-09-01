/* FLOW benchmark baseline. Human decisions: 6 (header, length, offsets, bounds, decode, error). */
#include <stdio.h>
#ifdef FLOW_BENCHMARK
static volatile const char *flow_benchmark_last_format;
static void flow_benchmark_sink(const char *format, ...) { flow_benchmark_last_format = format; }
#define flow_printf(...) flow_benchmark_sink(__VA_ARGS__)
#else
#define flow_printf printf
#endif
int main(void) {
    const unsigned char p[] = {0xF1, 0x02, 0x07, 0x2A};
    if (sizeof(p) < 4 || p[0] != 0xF1 || (size_t)p[1] + 2 > sizeof(p)) return 1;
    flow_printf("packet_valid id=%u value=%u\n", p[2], p[3]); return 0;
}
