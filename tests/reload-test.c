#include "reload.h"
#include "bitspace.h"
#include "registry.h"

#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    atomic_int v1_runs;
    atomic_int v2_runs;
    atomic_int drops;
    atomic_int failures;
} TestHost;

typedef struct {
    int version;
    int migrated;
} TestState;

static const FlowSchemaField SCHEMA_V1_FIELDS[] = {
    {"key", "u64", FLOW_SCHEMA_FIELD_PERSISTENT},
    {"score", "i32", FLOW_SCHEMA_FIELD_PERSISTENT}
};

static const FlowSchemaField SCHEMA_V2_FIELDS[] = {
    {"key", "u64", FLOW_SCHEMA_FIELD_PERSISTENT},
    {"score", "i32", FLOW_SCHEMA_FIELD_PERSISTENT},
    {"ttl", "u64", FLOW_SCHEMA_FIELD_DEFAULTABLE | FLOW_SCHEMA_FIELD_PERSISTENT}
};

static const FlowSchemaField SCHEMA_V3_FIELDS[] = {
    {"key", "u64", FLOW_SCHEMA_FIELD_PERSISTENT},
    {"score", "i32", FLOW_SCHEMA_FIELD_PERSISTENT},
    {"ttl", "u64", FLOW_SCHEMA_FIELD_DEFAULTABLE | FLOW_SCHEMA_FIELD_PERSISTENT},
    {"region", "u32", FLOW_SCHEMA_FIELD_DEFAULTABLE}
};

static const FlowSchemaField SCHEMA_BAD_FIELDS[] = {
    {"key", "string", FLOW_SCHEMA_FIELD_PERSISTENT},
    {"score", "i32", FLOW_SCHEMA_FIELD_PERSISTENT}
};

static const FlowSchema SCHEMA_V1 = {"cache", 1, SCHEMA_V1_FIELDS, 2};
static const FlowSchema SCHEMA_V2 = {"cache", 2, SCHEMA_V2_FIELDS, 3};
static const FlowSchema SCHEMA_V3 = {"cache", 3, SCHEMA_V3_FIELDS, 4};
static const FlowSchema SCHEMA_BAD = {"cache", 2, SCHEMA_BAD_FIELDS, 2};

static TestState *new_state(int version) {
    TestState *state = malloc(sizeof(*state));
    assert(state != NULL);
    state->version = version;
    state->migrated = 0;
    return state;
}

static int init_state(void *host_context, void **state_out) {
    (void)host_context;
    *state_out = new_state(2);
    return 0;
}

static int run_v1(void *host_context, void *state,
                  const void *input, void *output) {
    TestHost *host = host_context;
    (void)input;
    *(int *)output = ((TestState *)state)->version;
    atomic_fetch_add_explicit(&host->v1_runs, 1, memory_order_relaxed);
    return 0;
}

static int run_v2(void *host_context, void *state,
                  const void *input, void *output) {
    TestHost *host = host_context;
    (void)input;
    *(int *)output = ((TestState *)state)->version +
                     (((TestState *)state)->migrated ? 10 : 0);
    atomic_fetch_add_explicit(&host->v2_runs, 1, memory_order_relaxed);
    return 0;
}

static int migrate_state(void *host_context, const void *old_state,
                         void *new_state) {
    TestState *destination = new_state;
    (void)host_context;
    destination->version = ((const TestState *)old_state)->version;
    destination->migrated = 1;
    return 0;
}

static int migrate_fail(void *host_context, const void *old_state,
                        void *new_state) {
    (void)host_context;
    (void)old_state;
    (void)new_state;
    return -1;
}

static void drop_state(void *host_context, void *state) {
    TestHost *host = host_context;
    atomic_fetch_add_explicit(&host->drops, 1, memory_order_relaxed);
    free(state);
}

static FlowUnit UNIT_V1 = {
    .abi_version = FLOW_RELOAD_ABI_VERSION,
    .constraint_hash = 0x22,
    .capability_hash = 0x33,
    .name = "test-v1",
    .run = run_v1,
    .drop = drop_state,
    .schema = &SCHEMA_V1
};

static FlowUnit UNIT_V2_DIRECT = {
    .abi_version = FLOW_RELOAD_ABI_VERSION,
    .constraint_hash = 0x22,
    .capability_hash = 0x33,
    .name = "test-v2-direct",
    .init = init_state,
    .run = run_v2,
    .drop = drop_state,
    .schema = &SCHEMA_V1
};

static FlowUnit UNIT_V2 = {
    .abi_version = FLOW_RELOAD_ABI_VERSION,
    .constraint_hash = 0x22,
    .capability_hash = 0x33,
    .name = "test-v2-migrated",
    .init = init_state,
    .run = run_v2,
    .migrate = migrate_state,
    .drop = drop_state,
    .schema = &SCHEMA_V2
};

static FlowUnit UNIT_FAIL = {
    .abi_version = FLOW_RELOAD_ABI_VERSION,
    .constraint_hash = 0x22,
    .capability_hash = 0x33,
    .name = "test-fail-migration",
    .init = init_state,
    .run = run_v2,
    .migrate = migrate_fail,
    .drop = drop_state,
    .schema = &SCHEMA_V3
};

static FlowUnit UNIT_BAD = {
    .abi_version = FLOW_RELOAD_ABI_VERSION,
    .constraint_hash = 0x22,
    .capability_hash = 0x33,
    .name = "bad-schema",
    .run = run_v2,
    .drop = drop_state,
    .schema = &SCHEMA_BAD
};

typedef struct {
    FlowReloadContext *context;
    TestHost *host;
    int iterations;
} WorkerArgs;

static void *worker_main(void *argument) {
    WorkerArgs *args = argument;
    FlowReloadReader reader;
    int output = 0;
    int i;
    assert(flow_reload_reader_register(args->context, &reader) == FLOW_RELOAD_OK);
    for (i = 0; i < args->iterations; ++i) {
        if (flow_reload_call(args->context, &reader, NULL, &output) !=
                FLOW_RELOAD_OK ||
            (output != 1 && output != 2 && output != 12)) {
            atomic_fetch_add_explicit(&args->host->failures, 1,
                                      memory_order_relaxed);
        }
    }
    assert(flow_reload_reader_unregister(&reader) == FLOW_RELOAD_OK);
    return NULL;
}

typedef struct {
    FlowReloadContext *context;
    const FlowUnit *candidate;
    atomic_int started;
    atomic_int done;
    int result;
} MigrationArgs;

static void *migration_main(void *argument) {
    MigrationArgs *args = argument;
    atomic_store_explicit(&args->started, 1, memory_order_release);
    args->result = flow_reload_migrate(args->context, args->candidate);
    atomic_store_explicit(&args->done, 1, memory_order_release);
    return NULL;
}

static void initialize_unit_hashes(void) {
    UNIT_V1.semantic_schema_hash = flow_schema_hash(&SCHEMA_V1);
    UNIT_V2_DIRECT.semantic_schema_hash = flow_schema_hash(&SCHEMA_V1);
    UNIT_V2.semantic_schema_hash = flow_schema_hash(&SCHEMA_V2);
    UNIT_FAIL.semantic_schema_hash = flow_schema_hash(&SCHEMA_V3);
    UNIT_BAD.semantic_schema_hash = flow_schema_hash(&SCHEMA_BAD);
}

int main(void) {
    enum { WORKERS = 4, CALLS = 20000, PUBLISHES = 64 };
    TestHost host;
    FlowReloadContext *context;
    FlowReloadReader held;
    FlowInvocation invocation = {0};
    pthread_t threads[WORKERS];
    WorkerArgs args;
    TestState *state;
    int output = 0;
    int i;

    initialize_unit_hashes();
    atomic_init(&host.v1_runs, 0);
    atomic_init(&host.v2_runs, 0);
    atomic_init(&host.drops, 0);
    atomic_init(&host.failures, 0);
    context = flow_reload_create(&host);
    assert(context != NULL);
    assert(flow_reload_reader_register(context, &held) == FLOW_RELOAD_OK);
    assert(flow_schema_hash(&SCHEMA_V1) == UNIT_V1.semantic_schema_hash);
    assert(flow_schema_migration_compatible(&SCHEMA_V1, &SCHEMA_V2));
    assert(!flow_schema_migration_compatible(&SCHEMA_V1, &SCHEMA_BAD));

    assert(flow_reload_publish(context, &UNIT_V1, new_state(1)) == FLOW_RELOAD_OK);
    assert(flow_reload_begin(context, &held, &invocation) == FLOW_RELOAD_OK);
    assert(invocation.generation == 1);
    assert(invocation.unit == &UNIT_V1);
    assert(invocation.unit->run(&host, invocation.state, NULL, &output) == 0);
    assert(output == 1);

    assert(flow_reload_publish(context, &UNIT_V2_DIRECT, new_state(2)) ==
           FLOW_RELOAD_OK);
    assert(flow_reload_reclaim(context) == 0);
    assert(atomic_load_explicit(&host.drops, memory_order_relaxed) == 0);
    flow_reload_end(&invocation);
    assert(flow_reload_reclaim(context) == 1);
    assert(atomic_load_explicit(&host.drops, memory_order_relaxed) == 1);
    assert(flow_reload_call(context, &held, NULL, &output) == FLOW_RELOAD_OK);
    assert(output == 2);

    state = new_state(9);
    assert(flow_reload_publish(context, &UNIT_BAD, state) ==
           FLOW_RELOAD_INCOMPATIBLE);
    free(state);

    assert(flow_reload_begin(context, &held, &invocation) == FLOW_RELOAD_OK);
    {
        MigrationArgs migration = {0};
        pthread_t migration_thread;
        migration.context = context;
        migration.candidate = &UNIT_V2;
        migration.result = FLOW_RELOAD_INVALID;
        atomic_init(&migration.started, 0);
        atomic_init(&migration.done, 0);
        assert(pthread_create(&migration_thread, NULL, migration_main,
                              &migration) == 0);
        while (!atomic_load_explicit(&migration.started, memory_order_acquire))
            sched_yield();
        sched_yield();
        assert(!atomic_load_explicit(&migration.done, memory_order_acquire));
        flow_reload_end(&invocation);
        assert(pthread_join(migration_thread, NULL) == 0);
        assert(migration.result == FLOW_RELOAD_OK);
    }
    assert(flow_reload_reclaim(context) == 1);
    assert(flow_reload_call(context, &held, NULL, &output) == FLOW_RELOAD_OK);
    assert(output == 12);
    assert(flow_reload_migrate(context, &UNIT_FAIL) == FLOW_RELOAD_INVALID);
    assert(flow_reload_call(context, &held, NULL, &output) == FLOW_RELOAD_OK);
    assert(output == 12);

    assert(flow_reload_reader_unregister(&held) == FLOW_RELOAD_OK);
    args.context = context;
    args.host = &host;
    args.iterations = CALLS;
    for (i = 0; i < WORKERS; ++i)
        assert(pthread_create(&threads[i], NULL, worker_main, &args) == 0);
    for (i = 0; i < PUBLISHES; ++i) {
        assert(flow_reload_publish(context, &UNIT_V2, new_state(2)) ==
               FLOW_RELOAD_OK);
        (void)flow_reload_reclaim(context);
    }
    for (i = 0; i < WORKERS; ++i) assert(pthread_join(threads[i], NULL) == 0);
    while (flow_reload_reclaim(context) != 0) { }
    assert(atomic_load_explicit(&host.failures, memory_order_relaxed) == 0);
    assert(atomic_load_explicit(&host.v1_runs, memory_order_relaxed) > 0);
    assert(atomic_load_explicit(&host.v2_runs, memory_order_relaxed) > 0);
    assert(flow_reload_destroy(context) == FLOW_RELOAD_OK);

    /* Test FlowPlanArtifact direct reload integration */
    {
        flow_registry_init();
        SemanticIR ir;
        memset(&ir, 0, sizeof(ir));
        strncpy(ir.flow_name, "test_plan_flow", sizeof(ir.flow_name) - 1);
        ir.input_max_count = 100;
        ir.state_shared = 1;
        ir.fact_unordered = 1;

        FlowBitSpace space;
        assert(flow_bitspace_init_for_ir(&ir, &space));

        FlowPlan plan;
        space.decode(&space, 0, &plan);

        FlowPlanArtifact art;
        assert(flow_plan_to_artifact(&plan, &ir, 42, &art));

        FlowReloadContext *ctx = flow_reload_create(NULL);
        assert(ctx != NULL);

        assert(flow_reload_plan(ctx, &art, &ir, FLOW_MIGRATE_AUTO) == FLOW_RELOAD_OK);
        /* Test execution on the active plan */
        FlowReloadReader reader;
        assert(flow_reload_reader_register(ctx, &reader) == FLOW_RELOAD_OK);

        int input_val = 1234;
        int output_val = 0;
        assert(flow_reload_call(ctx, &reader, &input_val, &output_val) == FLOW_RELOAD_OK);
        assert(output_val == 1);

        int input_val2 = 5678;
        assert(flow_reload_call(ctx, &reader, &input_val2, &output_val) == FLOW_RELOAD_OK);
        assert(output_val == 2);

        /* Apply a mutation */
        int mut_val = 9999;
        FlowMutation mutation = {
            .kind = FLOW_MUTATION_UPSERT,
            .value = &mut_val,
            .value_size = sizeof(mut_val)
        };
        assert(flow_reload_apply(ctx, &reader, &mutation) == FLOW_RELOAD_OK);
        assert(flow_reload_call(ctx, &reader, NULL, &output_val) == FLOW_RELOAD_OK);
        assert(output_val == 3);

        /* Migrate to a second plan artifact with higher capacity */
        FlowPlan plan2;
        space.decode(&space, 1, &plan2);
        FlowPlanArtifact art2;
        assert(flow_plan_to_artifact(&plan2, &ir, 99, &art2));
        assert(flow_reload_plan(ctx, &art2, &ir, FLOW_MIGRATE_AUTO) == FLOW_RELOAD_OK);

        /* Verify that migrated state preserves the 3 items and accepts new calls */
        assert(flow_reload_call(ctx, &reader, NULL, &output_val) == FLOW_RELOAD_OK);
        assert(output_val == 3);

        /* Negative test: corrupted contract hash */
        FlowPlanArtifact bad_art = art;
        bad_art.contract_hash ^= 0x123456;
        assert(flow_reload_plan(ctx, &bad_art, &ir, FLOW_MIGRATE_AUTO) == FLOW_RELOAD_INCOMPATIBLE);

        assert(flow_reload_reader_unregister(&reader) == FLOW_RELOAD_OK);
        assert(flow_reload_destroy(ctx) == FLOW_RELOAD_OK);
    }

    printf("RELOAD_TEST=passed generations=%d drops=%d plan_reload=verified\n", PUBLISHES + 3,
           atomic_load_explicit(&host.drops, memory_order_relaxed));
    return EXIT_SUCCESS;
}
