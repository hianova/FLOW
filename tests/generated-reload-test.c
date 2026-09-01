#include "reload.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

const FlowUnit *flow_generated_unit(void);

static uint64_t elapsed_nanoseconds(const struct timespec *start,
                                    const struct timespec *end) {
    const int64_t seconds = (int64_t)end->tv_sec - (int64_t)start->tv_sec;
    const int64_t nanoseconds = (int64_t)end->tv_nsec -
                                (int64_t)start->tv_nsec;
    return (uint64_t)(seconds * INT64_C(1000000000) + nanoseconds);
}

static void seed_unit(const FlowUnit *unit, void *state, int count) {
    int i;
    for (i = 0; i < count; ++i) {
        const FlowMutation mutation = {
            .kind = FLOW_MUTATION_UPSERT,
            .key = &i,
            .key_size = sizeof(i),
            .value = &i,
            .value_size = sizeof(i)
        };
        assert(unit->apply(NULL, state, &mutation) == 0);
    }
}

int main(void) {
    const FlowUnit *unit = flow_generated_unit();
    FlowReloadContext *context;
    FlowReloadReader reader;
    void *direct_state = NULL;
    int output = 0;
    int key = 7;
    int i;
    struct timespec start;
    struct timespec end;
    uint64_t direct_ns;
    uint64_t reload_ns;
    const size_t iterations = 100000;

    assert(unit != NULL && unit->name != NULL);
    assert(unit->init(NULL, &direct_state) == 0);
    seed_unit(unit, direct_state, 128);
    assert(clock_gettime(CLOCK_MONOTONIC, &start) == 0);
    for (i = 0; i < (int)iterations; ++i)
        assert(unit->run(NULL, direct_state, &key, &output) == 0);
    assert(clock_gettime(CLOCK_MONOTONIC, &end) == 0);
    direct_ns = elapsed_nanoseconds(&start, &end);
    unit->drop(NULL, direct_state);

    context = flow_reload_create(NULL);
    assert(context != NULL);
    assert(flow_reload_reader_register(context, &reader) == FLOW_RELOAD_OK);
    assert(flow_reload_activate(context, unit) == FLOW_RELOAD_OK);
    for (i = 0; i < 128; ++i) {
        const FlowMutation mutation = {
            .kind = FLOW_MUTATION_UPSERT,
            .key = &i,
            .key_size = sizeof(i),
            .value = &i,
            .value_size = sizeof(i)
        };
        assert(flow_reload_apply(context, &reader, &mutation) == FLOW_RELOAD_OK);
    }
    assert(clock_gettime(CLOCK_MONOTONIC, &start) == 0);
    for (i = 0; i < (int)iterations; ++i)
        assert(flow_reload_call(context, &reader, &key, &output) ==
               FLOW_RELOAD_OK);
    assert(clock_gettime(CLOCK_MONOTONIC, &end) == 0);
    reload_ns = elapsed_nanoseconds(&start, &end);
    assert(output == key);

    {
        const FlowMutation deletion = {
            .kind = FLOW_MUTATION_DELETE,
            .key = &key,
            .key_size = sizeof(key)
        };
        assert(flow_reload_apply(context, &reader, &deletion) == FLOW_RELOAD_OK);
        assert(flow_reload_call(context, &reader, &key, &output) !=
               FLOW_RELOAD_OK);
    }
    assert(flow_reload_reader_unregister(&reader) == FLOW_RELOAD_OK);
    assert(flow_reload_destroy(context) == FLOW_RELOAD_OK);
    printf("GENERATED_RELOAD_TEST=passed unit=%s direct_ns=%llu reload_ns=%llu\n",
           unit->name, (unsigned long long)direct_ns,
           (unsigned long long)reload_ns);
    return EXIT_SUCCESS;
}
