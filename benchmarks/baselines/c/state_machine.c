/* FLOW benchmark baseline. Human decisions: 5 (states, events, initial, transitions, terminal). */
#include <stdio.h>
#ifdef FLOW_BENCHMARK
static volatile const char *flow_benchmark_last_format;
static void flow_benchmark_sink(const char *format, ...) { flow_benchmark_last_format = format; }
#define flow_printf(...) flow_benchmark_sink(__VA_ARGS__)
#else
#define flow_printf printf
#endif
int main(void) {
    enum State { IDLE, RUNNING, DONE }; enum State state = IDLE; int events[] = {1,1,2,2};
    for (int i = 0; i < 4; ++i) { if (state == IDLE && events[i] == 1) state = RUNNING; else if (state == RUNNING && events[i] == 2) state = DONE; }
    flow_printf("final_state %d\n", state); return state == DONE ? 0 : 1;
}
