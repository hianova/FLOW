/* FLOW benchmark baseline. Human decisions: 7 (hash, slots, probing, layout, capacity, misses, update). */
#include <stdio.h>
#ifdef FLOW_BENCHMARK
static volatile const char *flow_benchmark_last_format;
static void flow_benchmark_sink(const char *format, ...) { flow_benchmark_last_format = format; }
#define flow_printf(...) flow_benchmark_sink(__VA_ARGS__)
#else
#define flow_printf printf
#endif
typedef struct { int used, key, value; } Entry;
int main(void) {
    Entry table[32] = {0};
    for (int i = 0; i < 5; ++i) { size_t slot = (unsigned)i % 32; table[slot] = (Entry){1, i + 1, 90 + i}; }
    int hits = 0;
    for (int i = 0; i < 5; ++i) for (size_t j = 0; j < 32; ++j)
        if (table[j].used && table[j].key == i + 1) { hits += table[j].value; break; }
    flow_printf("cache_hits %d\n", hits); return 0;
}
