#include "adaptive.h"

#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    atomic_int drops;
    FlowAdaptiveController *controller;
} AdaptiveHost;

typedef struct {
    atomic_int value;
} AdaptiveState;

static const FlowSchemaField V1_FIELDS[] = {
    {"value", "i32", FLOW_SCHEMA_FIELD_PERSISTENT}
};
static const FlowSchemaField V2_FIELDS[] = {
    {"value", "i32", FLOW_SCHEMA_FIELD_PERSISTENT},
    {"tuning", "u32", FLOW_SCHEMA_FIELD_DEFAULTABLE}
};
static const FlowSchema SCHEMA_V1 = {"adaptive_counter", 1, V1_FIELDS, 1};
static const FlowSchema SCHEMA_V2 = {"adaptive_counter", 2, V2_FIELDS, 2};

static int init_state(void *host_context, void **state_out) {
    AdaptiveState *state;
    (void)host_context;
    state = calloc(1, sizeof(*state));
    if (state == NULL) return -1;
    atomic_init(&state->value, 7);
    *state_out = state;
    return 0;
}

static int run_state(void *host_context, void *raw_state,
                     const void *input, void *output) {
    (void)host_context;
    (void)input;
    *(int *)output = atomic_load_explicit(&((AdaptiveState *)raw_state)->value,
                                          memory_order_relaxed);
    return 0;
}

static int apply_state(void *host_context, void *raw_state,
                       const FlowMutation *mutation) {
    AdaptiveState *state = raw_state;
    (void)host_context;
    if (mutation == NULL || mutation->kind != FLOW_MUTATION_UPSERT ||
        mutation->value == NULL || mutation->value_size != sizeof(int))
        return -1;
    atomic_store_explicit(&state->value, *(const int *)mutation->value,
                          memory_order_relaxed);
    return 0;
}

static int migrate_state(void *host_context, const void *raw_old,
                         void *raw_new) {
    const AdaptiveState *old_state = raw_old;
    AdaptiveState *new_state = raw_new;
    (void)host_context;
    atomic_store_explicit(&new_state->value,
                          atomic_load_explicit(&old_state->value,
                                               memory_order_relaxed),
                          memory_order_relaxed);
    return 0;
}

static void drop_state(void *raw_host, void *raw_state) {
    AdaptiveHost *host = raw_host;
    atomic_fetch_add_explicit(&host->drops, 1, memory_order_relaxed);
    free(raw_state);
}

static FlowUnit SLOW_UNIT = {
    .abi_version = FLOW_RELOAD_ABI_VERSION,
    .constraint_hash = UINT64_C(0xA001),
    .capability_hash = UINT64_C(0xA002),
    .name = "linear_array",
    .init = init_state,
    .run = run_state,
    .apply = apply_state,
    .drop = drop_state,
    .schema = &SCHEMA_V1
};

static FlowUnit FAST_UNIT = {
    .abi_version = FLOW_RELOAD_ABI_VERSION,
    .constraint_hash = UINT64_C(0xA001),
    .capability_hash = UINT64_C(0xA002),
    .name = "sharded_hash",
    .init = init_state,
    .run = run_state,
    .apply = apply_state,
    .migrate = migrate_state,
    .drop = drop_state,
    .schema = &SCHEMA_V2
};

static int probe(void *host_context,
                 const FlowAdaptiveCandidate *candidate,
                 const FlowAdaptiveMetrics *metrics, double *score_out) {
    AdaptiveHost *host = host_context;
    FlowAdaptiveMetrics observed;
    (void)metrics;
    assert(host->controller != NULL);
    assert(flow_adaptive_metrics(host->controller, &observed) ==
           FLOW_ADAPTIVE_OK);
    *score_out = strcmp(candidate->name, "sharded_hash") == 0 ? 1.0 : 100.0;
    return 0;
}

int main(void) {
    AdaptiveHost host;
    FlowReloadContext *reload;
    FlowReloadReader reader;
    FlowAdaptiveController *adaptive;
    FlowAdaptiveMetrics metrics;
    const FlowAdaptiveCandidate candidates[] = {
        {"linear_array", &SLOW_UNIT, "rank", "ranking", 5, 7, 0, 8,
         "cpu", "stdlib", 0, 0},
        {"sharded_hash", &FAST_UNIT, "rank", "ranking", 9, 9, 0, 12,
         "cpu", "pthread", 0, 0}
    };
    const FlowAdaptiveConfig config = {
        .sample_window = 8,
        .cooldown_calls = 8,
        .journal_capacity = 16,
        .min_improvement_percent = 10.0,
        .policy = {
            .flow_name = "rank",
            .domain_contract = "ranking",
            .input_capacity = 4096,
            .memory_limit_bytes = 65536,
            .resource = "cpu"
        }
    };
    int output = 0;
    int i;

    SLOW_UNIT.semantic_schema_hash = flow_schema_hash(&SCHEMA_V1);
    FAST_UNIT.semantic_schema_hash = flow_schema_hash(&SCHEMA_V2);
    atomic_init(&host.drops, 0);
    reload = flow_reload_create(&host);
    assert(reload != NULL);
    assert(flow_reload_reader_register(reload, &reader) == FLOW_RELOAD_OK);
    assert(flow_reload_activate(reload, &SLOW_UNIT) == FLOW_RELOAD_OK);
    {
        FlowAdaptiveCandidate invalid = candidates[0];
        invalid.domain_contract = "wrong_contract";
        assert(flow_adaptive_create(reload, &host, &config, &invalid, 1, 0,
                                     probe) == NULL);
    }
    {
        FlowAdaptiveConfig wrong_capability = config;
        wrong_capability.policy.capability = "gpu";
        assert(flow_adaptive_create(reload, &host, &wrong_capability,
                                    candidates, 2, 0, probe) == NULL);
    }
    {
        FlowAdaptiveConfig needs_parallel = config;
        needs_parallel.policy.require_parallelizable = 1;
        assert(flow_adaptive_create(reload, &host, &needs_parallel,
                                    candidates, 2, 0, probe) == NULL);
    }
    {
        FlowAdaptiveCandidate generic = candidates[0];
        FlowAdaptiveController *generic_controller;
        generic.flow_binding = NULL;
        generic_controller = flow_adaptive_create(reload, &host, &config,
                                                   &generic, 1, 0, probe);
        assert(generic_controller != NULL);
        assert(flow_adaptive_destroy(generic_controller) == FLOW_ADAPTIVE_OK);
    }
    {
        FlowAdaptiveCandidate invalid = candidates[0];
        FlowAdaptiveConfig fixed_limit = config;
        invalid.memory_fixed_bytes = 128;
        fixed_limit.policy.input_capacity = 0;
        fixed_limit.policy.memory_limit_bytes = 64;
        assert(flow_adaptive_create(reload, &host, &fixed_limit, &invalid, 1,
                                     0, probe) == NULL);
    }
    assert(flow_adaptive_create(reload, &host, &config, candidates, 2, 1,
                                probe) == NULL);
    adaptive = flow_adaptive_create(reload, &host, &config, candidates, 2, 0,
                                     probe);
    assert(adaptive != NULL);
    host.controller = adaptive;
    for (i = 0; i < 7; ++i)
        assert(flow_adaptive_call(adaptive, &reader, NULL, &output) ==
               FLOW_RELOAD_OK);
    assert(flow_adaptive_tick(adaptive) == FLOW_ADAPTIVE_NOT_READY);
    assert(flow_adaptive_call(adaptive, &reader, NULL, &output) ==
           FLOW_RELOAD_OK);
    assert(flow_adaptive_tick(adaptive) == FLOW_ADAPTIVE_OK);
    assert(flow_adaptive_current_index(adaptive) == 1);
    assert(flow_adaptive_metrics(adaptive, &metrics) == FLOW_ADAPTIVE_OK);
    assert(metrics.calls == 0 && metrics.failures == 0);
    assert(flow_adaptive_call(adaptive, &reader, NULL, &output) ==
           FLOW_RELOAD_OK);
    assert(output == 7);
    assert(flow_adaptive_destroy(adaptive) == FLOW_ADAPTIVE_OK);
    assert(flow_reload_reader_unregister(&reader) == FLOW_RELOAD_OK);
    assert(flow_reload_destroy(reload) == FLOW_RELOAD_OK);
    assert(atomic_load_explicit(&host.drops, memory_order_relaxed) == 2);
    puts("ADAPTIVE_TEST=passed selected=sharded_hash");
    return EXIT_SUCCESS;
}
