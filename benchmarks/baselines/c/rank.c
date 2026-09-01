/* FLOW benchmark baseline. Human decisions: 7 (record, comparator, sort, capacity, top-k, input, output). */
#include <stdio.h>
#include <stdlib.h>
#ifdef FLOW_BENCHMARK
static volatile const char *flow_benchmark_last_format;
static void flow_benchmark_sink(const char *format, ...) { flow_benchmark_last_format = format; }
#define flow_printf(...) flow_benchmark_sink(__VA_ARGS__)
#else
#define flow_printf printf
#endif
typedef struct { int id, score; } Item;
static int compare(const void *a, const void *b) { return ((const Item *)b)->score - ((const Item *)a)->score; }
int main(void) {
    Item items[] = {{1,91},{2,74},{3,99},{4,86},{5,95}};
    qsort(items, 5, sizeof(items[0]), compare);
    for (int i = 0; i < 3; ++i) flow_printf("%d %d\n", items[i].id, items[i].score);
    return 0;
}
