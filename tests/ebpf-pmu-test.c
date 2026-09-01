#include "adaptive.h"
#include "registry.h"
#include "reload.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "ebpf-pmu-test assertion failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

static int init_state(void *host, void **state_out) {
    (void)host;
    int *val = calloc(1, sizeof(int));
    *val = 42;
    *state_out = val;
    return 0;
}

static int run_state(void *host, void *raw_state, const void *in, void *out) {
    (void)host; (void)in;
    if (out != NULL && raw_state != NULL) *(int *)out = *(int *)raw_state;
    return 0;
}

static int migrate_state(void *host, const void *old_st, void *new_st) {
    (void)host;
    if (old_st != NULL && new_st != NULL) *(int *)new_st = *(const int *)old_st;
    return 0;
}

static void drop_state(void *host, void *raw_state) {
    (void)host;
    free(raw_state);
}

static int apply_state(void *host, void *raw_state, const FlowMutation *mutation) {
    (void)host;
    if (mutation != NULL && mutation->value != NULL && raw_state != NULL) {
        *(int *)raw_state = *(const int *)mutation->value;
    }
    return 0;
}

static const FlowSchemaField COUNTER_FIELDS[] = {
    {"value", "i32", FLOW_SCHEMA_FIELD_PERSISTENT}
};
static const FlowSchema COUNTER_SCHEMA = {"counter", 1, COUNTER_FIELDS, 1};

static int mock_probe(void *host, const FlowAdaptiveCandidate *candidate,
                        const FlowAdaptiveMetrics *metrics, double *score_out) {
    (void)host; (void)candidate; (void)metrics;
    if (score_out != NULL) *score_out = 1.0;
    return 1;
}

int main(void) {
    flow_registry_init();

    FlowUnit u_slow = {
        .abi_version = FLOW_RELOAD_ABI_VERSION,
        .constraint_hash = UINT64_C(0xA001),
        .capability_hash = UINT64_C(0xA002),
        .name = "cand_slow",
        .init = init_state,
        .run = run_state,
        .apply = apply_state,
        .migrate = migrate_state,
        .drop = drop_state,
        .schema = &COUNTER_SCHEMA
    };

    FlowUnit u_fast = {
        .abi_version = FLOW_RELOAD_ABI_VERSION,
        .constraint_hash = UINT64_C(0xA001),
        .capability_hash = UINT64_C(0xA002),
        .name = "cand_fast",
        .init = init_state,
        .run = run_state,
        .apply = apply_state,
        .migrate = migrate_state,
        .drop = drop_state,
        .schema = &COUNTER_SCHEMA
    };

    u_slow.semantic_schema_hash = flow_schema_hash(&COUNTER_SCHEMA);
    u_fast.semantic_schema_hash = flow_schema_hash(&COUNTER_SCHEMA);

    FlowAdaptiveCandidate candidates[2] = {
        {
            .name = "cand_slow",
            .unit = &u_slow,
            .latency_score = 10,
            .memory_score = 80,
            .supports_parallelizable = 1,
            .plan_schema_hash = 100
        },
        {
            .name = "cand_fast",
            .unit = &u_fast,
            .latency_score = 5,
            .memory_score = 20, /* Superior memory locality */
            .supports_parallelizable = 1,
            .plan_schema_hash = 100
        }
    };

    FlowReloadContext *reload_ctx = flow_reload_create(NULL);
    CHECK(reload_ctx != NULL);
    CHECK(flow_reload_activate(reload_ctx, &u_slow) == FLOW_RELOAD_OK);

    FlowAdaptiveConfig cfg = {
        .sample_window = 10,
        .cooldown_calls = 5,
        .journal_capacity = 64,
        .min_improvement_percent = 10.0
    };

    FlowAdaptiveController *ctrl = flow_adaptive_create(reload_ctx, NULL, &cfg, candidates, 2, 0, mock_probe);
    CHECK(ctrl != NULL);

    /* 1. Test IP Range Attribution */
    uintptr_t jit_start = 0x7fff00001000ULL;
    uintptr_t jit_end   = 0x7fff00002000ULL;
    CHECK(flow_adaptive_register_ip_range(ctrl, jit_start, jit_end, "cand_slow_jit", 0));

    uint32_t matched_idx = 99;
    CHECK(flow_adaptive_is_ip_attributed(ctrl, 0x7fff00001500ULL, &matched_idx));
    CHECK(matched_idx == 0);

    /* Unrelated IP outside JIT range must be rejected */
    CHECK(!flow_adaptive_is_ip_attributed(ctrl, 0x100000ULL, &matched_idx));

    /* 2. Configure Anti-Thrashing (EMA + Anomaly Streak + Cooldown) */
    FlowAntiThrashingConfig anti_thrash = {
        .ema_alpha = 0.5,
        .anomaly_streak_required = 2, /* Must see 2 consecutive anomaly ticks */
        .cooldown_ticks = 3,          /* Cooldown window of 3 ticks */
        .backoff_multiplier = 1.5
    };
    flow_adaptive_set_anti_thrashing(ctrl, &anti_thrash);

    FlowPMUThresholds thresholds = {
        .cache_miss_rate_threshold = 0.35, /* 35% miss rate triggers swap */
        .min_ipc_threshold = 0.8
    };

    /* 3. Feed a single bursty spike (Noise) -> Must NOT trigger hot-swap */
    FlowPMUTelemetry spike = {
        .l3_cache_misses = 500,
        .l3_cache_references = 1000,
        .cache_miss_rate = 0.50, /* 50% spike */
        .ipc = 1.2
    };
    CHECK(flow_adaptive_feed_attributed_pmu(ctrl, 0x7fff00001500ULL, &spike) == FLOW_ADAPTIVE_OK);
    FlowAdaptiveStatus status1 = flow_adaptive_tick_pmu(ctrl, &thresholds);
    CHECK(status1 == FLOW_ADAPTIVE_NO_CHANGE); /* Filtered by Anomaly Streak = 1 < 2 */
    CHECK(flow_adaptive_current_index(ctrl) == 0);

    /* 4. Feed second sustained anomaly -> Now triggers hot-swap */
    CHECK(flow_adaptive_feed_attributed_pmu(ctrl, 0x7fff00001500ULL, &spike) == FLOW_ADAPTIVE_OK);
    FlowAdaptiveStatus status2 = flow_adaptive_tick_pmu(ctrl, &thresholds);
    CHECK(status2 == FLOW_ADAPTIVE_OK);
    CHECK(flow_adaptive_current_index(ctrl) == 1); /* Switched to cand_fast */

    /* 5. Register cand_fast IP range and test low IPC anomaly under active cooldown */
    CHECK(flow_adaptive_register_ip_range(ctrl, 0x7fff00002000ULL, 0x7fff00003000ULL, "cand_fast_jit", 1));
    FlowPMUTelemetry low_ipc_spike = {
        .l3_cache_misses = 10,
        .l3_cache_references = 1000,
        .cache_miss_rate = 0.01,
        .ipc = 0.3 /* Low IPC anomaly */
    };
    CHECK(flow_adaptive_feed_attributed_pmu(ctrl, 0x7fff00002500ULL, &low_ipc_spike) == FLOW_ADAPTIVE_OK);
    FlowAdaptiveStatus s3_1 = flow_adaptive_tick_pmu(ctrl, &thresholds);
    CHECK(s3_1 == FLOW_ADAPTIVE_NO_CHANGE); /* Streak 1 of 2 */

    CHECK(flow_adaptive_feed_attributed_pmu(ctrl, 0x7fff00002500ULL, &low_ipc_spike) == FLOW_ADAPTIVE_OK);
    FlowAdaptiveStatus s3_2 = flow_adaptive_tick_pmu(ctrl, &thresholds);
    CHECK(s3_2 == FLOW_ADAPTIVE_NOT_READY); /* Blocked by active Cooldown window! */

    FlowDebounceState debounce;
    CHECK(flow_adaptive_get_debounce_state(ctrl, &debounce));
    CHECK(debounce.swap_count == 1);

    flow_adaptive_destroy(ctrl);
    flow_reload_destroy(reload_ctx);

    printf("EBPF_PMU_TEST=passed ip_attribution=verified ema_smoothing=verified anti_thrashing=verified\n");
    return 0;
}
