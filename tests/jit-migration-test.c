#include "jit.h"
#include "adaptive.h"
#include "registry.h"
#include "reload.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "jit-migration-test assertion failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

typedef struct {
    uint32_t user_id;
    uint32_t score;
    uint32_t timestamp;
} UserRecordAoS;

int main(void) {
    flow_registry_init();

    /* 1. In-Memory Zero-I/O JIT Engine Compilation */
    FlowJITConfig jit_cfg = {
        .enable_lto = 1,
        .opt_level = 3,
        .initial_code_heap_bytes = 64 * 1024
    };
    FlowJITEngine *jit = flow_jit_create(&jit_cfg);
    CHECK(jit != NULL);

    const char *mock_llvm_ir =
        "; ModuleID = 'flow_jit_kernel'\n"
        "source_filename = \"flow_jit.c\"\n"
        "target datalayout = \"e-m:o-i64:64-i128:128-n32:64-S128\"\n"
        "target triple = \"arm64-apple-macosx\"\n"
        "define void @flow_run() alwaysinline nounwind ssp {\n"
        "    ret void\n"
        "}\n";

    FlowUnit jit_unit;
    FlowJITCodeBlock code_block;
    CHECK(flow_jit_compile_llvm_ir(jit, mock_llvm_ir, "jit_soa_kernel", FLOW_LAYOUT_SOA, &jit_unit, &code_block));
    CHECK(code_block.start_ip < code_block.end_ip);
    CHECK(code_block.code_bytes > 0);
    CHECK(jit_unit.layout == FLOW_LAYOUT_SOA);

    /* 2. AoS -> SoA Dynamic State Migration */
    size_t N = 100;
    UserRecordAoS *aos_data = calloc(N, sizeof(UserRecordAoS));
    CHECK(aos_data != NULL);
    for (size_t i = 0; i < N; ++i) {
        aos_data[i].user_id = (uint32_t)(1000 + i);
        aos_data[i].score = (uint32_t)(i * 10);
        aos_data[i].timestamp = 1600000000u + (uint32_t)i;
    }

    uint32_t *soa_user_ids = calloc(N, sizeof(uint32_t));
    uint32_t *soa_scores = calloc(N, sizeof(uint32_t));
    uint32_t *soa_timestamps = calloc(N, sizeof(uint32_t));
    uint8_t *soa_cols[3] = { (uint8_t *)soa_user_ids, (uint8_t *)soa_scores, (uint8_t *)soa_timestamps };

    FlowLayoutMigrationSpec aos_to_soa_spec = {
        .item_count = N,
        .field_count = 3,
        .field_sizes = { sizeof(uint32_t), sizeof(uint32_t), sizeof(uint32_t) },
        .from_layout = FLOW_LAYOUT_AOS,
        .to_layout = FLOW_LAYOUT_SOA
    };

    size_t copied = 0, transformed = 0;
    CHECK(flow_jit_migrate_state_layout(&aos_to_soa_spec, aos_data, soa_cols, &copied, &transformed));
    CHECK(transformed == N * 3 * sizeof(uint32_t));

    /* Verify Data Integrity in SoA columns */
    for (size_t i = 0; i < N; ++i) {
        CHECK(soa_user_ids[i] == (uint32_t)(1000 + i));
        CHECK(soa_scores[i] == (uint32_t)(i * 10));
        CHECK(soa_timestamps[i] == 1600000000u + (uint32_t)i);
    }

    /* 3. SoA -> AoS Dynamic State Migration */
    UserRecordAoS *aos_roundtrip = calloc(N, sizeof(UserRecordAoS));
    FlowLayoutMigrationSpec soa_to_aos_spec = {
        .item_count = N,
        .field_count = 3,
        .field_sizes = { sizeof(uint32_t), sizeof(uint32_t), sizeof(uint32_t) },
        .from_layout = FLOW_LAYOUT_SOA,
        .to_layout = FLOW_LAYOUT_AOS
    };
    CHECK(flow_jit_migrate_state_layout(&soa_to_aos_spec, soa_cols, aos_roundtrip, &copied, &transformed));
    for (size_t i = 0; i < N; ++i) {
        CHECK(aos_roundtrip[i].user_id == aos_data[i].user_id);
        CHECK(aos_roundtrip[i].score == aos_data[i].score);
        CHECK(aos_roundtrip[i].timestamp == aos_data[i].timestamp);
    }

    /* 4. Columnar Zero-Copy Partial Migration */
    uint32_t *new_scores = calloc(N, sizeof(uint32_t));
    uint8_t *new_cols[3] = { NULL, (uint8_t *)new_scores, NULL };

    FlowLayoutMigrationSpec columnar_spec = {
        .item_count = N,
        .field_count = 3,
        .field_sizes = { sizeof(uint32_t), sizeof(uint32_t), sizeof(uint32_t) },
        .from_layout = FLOW_LAYOUT_COLUMNAR,
        .to_layout = FLOW_LAYOUT_COLUMNAR,
        .field_changed = { 0, 1, 0 } /* Only scores column changed */
    };
    CHECK(flow_jit_migrate_state_layout(&columnar_spec, soa_cols, new_cols, &copied, &transformed));
    /* Column 0 & Column 2 must be pointer-shared (Zero-Copy) */
    CHECK(new_cols[0] == soa_cols[0]);
    CHECK(new_cols[2] == soa_cols[2]);
    CHECK(copied == 2 * N * sizeof(uint32_t));
    CHECK(transformed == 1 * N * sizeof(uint32_t));

    /* 5. Migration Cost & Amortization Payback Calculation */
    double cost_ns = 0.0, payback_calls = 0.0;
    CHECK(flow_jit_calculate_migration_cost(&columnar_spec, 5.0 /* 5ns steady-state gain/call */,
                                            &cost_ns, &payback_calls));
    CHECK(cost_ns > 0.0);
    CHECK(payback_calls > 0.0);

    /* Cleanup */
    free(aos_data);
    free(aos_roundtrip);
    free(soa_user_ids);
    free(soa_scores);
    free(soa_timestamps);
    free(new_scores);
    flow_jit_destroy(jit);

    printf("JIT_MIGRATION_TEST=passed in_memory_jit=verified aos_soa_transform=verified columnar_zero_copy=sound payback_model=verified\n");
    return 0;
}
