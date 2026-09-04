#include "flow_test_kit.h"
#include "flow_str.h"
#include "flow_fixed_vec.h"
#include "flow_smt_dsl.h"
#include "flow_bmf_schema.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * ============================================================================
 * TEST SUITE #70: FLOW Developer Velocity & Ergonomic Polish Kits Suite
 * ============================================================================
 */

/* ------------------------------------------------------------------------- */
/* Declarations for Test Vectors and BitManifold Schema                      */
/* ------------------------------------------------------------------------- */

typedef struct {
    uint32_t id;
    int32_t score;
} TestScoreItem;

FLOW_FIXED_VEC_DEFINE(TestScoreVec, TestScoreItem, 16);

FLOW_BMF_FIELD_DECLARE(TestProtoKind, 0, 3);
FLOW_BMF_FIELD_DECLARE(TestProtoStreams, 3, 9);
FLOW_BMF_FIELD_DECLARE(TestProtoTable, 12, 7);
FLOW_BMF_FIELD_DECLARE(TestProtoZeroCopy, 19, 1);

int main(void) {
    FLOW_TEST_SUITE_BEGIN("Developer Velocity & Ergonomic Polish Kits (#70)");

    /* ===================================================================== */
    /* STAGE 1: Safe String & Fast 64-Bit Hashing (flow_str.h)               */
    /* ===================================================================== */
    FLOW_STAGE_BEGIN(1, "Safe String & Fast 64-Bit Hashing (flow_str.h)");
    {
        char buf[16];

        /* 1a: Bounded string copy */
        size_t n1 = flow_str_copy(buf, sizeof(buf), "HelloWorld");
        FLOW_ASSERT_EQ(n1, 10);
        FLOW_ASSERT_STR_EQ(buf, "HelloWorld");

        /* Copy with truncation */
        size_t n2 = flow_str_copy(buf, 6, "123456789");
        FLOW_ASSERT_EQ(n2, 5);
        FLOW_ASSERT_STR_EQ(buf, "12345");

        /* Null safety */
        size_t n3 = flow_str_copy(buf, sizeof(buf), NULL);
        FLOW_ASSERT_EQ(n3, 0);
        FLOW_ASSERT_STR_EQ(buf, "");

        /* 1b: Bounded formatting */
        flow_str_fmt(buf, sizeof(buf), "ID:%d-%s", 42, "OK");
        FLOW_ASSERT_STR_EQ(buf, "ID:42-OK");

        /* 1c: Fast 64-bit hashing */
        uint64_t h1 = flow_hash64_str("route/orders/create");
        uint64_t h2 = flow_hash64_str("route/orders/create");
        uint64_t h3 = flow_hash64_str("route/orders/cancel");
        FLOW_ASSERT_EQ(h1, h2);
        FLOW_ASSERT_NE(h1, h3);
        FLOW_ASSERT_NE(h1, 0);

        /* 1d: Integer hash */
        uint64_t ih1 = flow_hash64_u64(1001);
        uint64_t ih2 = flow_hash64_u64(1002);
        FLOW_ASSERT_NE(ih1, ih2);

        /* 1e: Equality & Prefix */
        FLOW_ASSERT_TRUE(flow_str_eq("abc", "abc"));
        FLOW_ASSERT_FALSE(flow_str_eq("abc", "def"));
        FLOW_ASSERT_TRUE(flow_str_eq(NULL, NULL));
        FLOW_ASSERT_FALSE(flow_str_eq("abc", NULL));
        FLOW_ASSERT_TRUE(flow_str_starts_with("FLOW_CORE", "FLOW"));
        FLOW_ASSERT_FALSE(flow_str_starts_with("FLOW_CORE", "CORE"));
        FLOW_ASSERT_TRUE(flow_str_contains("BitManifold Architecture", "Manifold"));
        FLOW_ASSERT_FALSE(flow_str_contains("BitManifold Architecture", "BMFQuantum"));

        printf("  ✓ Safe string bounded copying, formatting, and 64-bit hashing verified.\n\n");
    }

    /* ===================================================================== */
    /* STAGE 2: Zero-Heap Fixed Flat Vector (flow_fixed_vec.h)               */
    /* ===================================================================== */
    FLOW_STAGE_BEGIN(2, "Zero-Heap Fixed Flat Vector (flow_fixed_vec.h)");
    {
        TestScoreVec vec;
        TestScoreVec_init(&vec);

        FLOW_ASSERT_TRUE(TestScoreVec_is_empty(&vec));
        FLOW_ASSERT_FALSE(TestScoreVec_is_full(&vec));
        FLOW_ASSERT_EQ(TestScoreVec_count(&vec), 0);

        /* Push 16 items */
        for (uint32_t i = 0; i < 16; ++i) {
            TestScoreItem it = { .id = 100 + i, .score = (int32_t)(i * 5) };
            FLOW_ASSERT_TRUE(TestScoreVec_push(&vec, &it));
        }

        FLOW_ASSERT_TRUE(TestScoreVec_is_full(&vec));
        FLOW_ASSERT_EQ(TestScoreVec_count(&vec), 16);

        /* 17th push must fail (no overflow) */
        TestScoreItem overflow_it = { .id = 999, .score = -1 };
        FLOW_ASSERT_FALSE(TestScoreVec_push(&vec, &overflow_it));

        /* Access items */
        TestScoreItem *item5 = TestScoreVec_get(&vec, 5);
        FLOW_ASSERT_TRUE(item5 != NULL);
        FLOW_ASSERT_EQ(item5->id, 105);
        FLOW_ASSERT_EQ(item5->score, 25);

        /* Modify in-place via pointer */
        item5->score = 999;
        FLOW_ASSERT_EQ(TestScoreVec_get(&vec, 5)->score, 999);

        /* O(1) unordered remove at index 2 (swaps with last element) */
        FLOW_ASSERT_TRUE(TestScoreVec_remove_unordered(&vec, 2));
        FLOW_ASSERT_EQ(TestScoreVec_count(&vec), 15);
        FLOW_ASSERT_EQ(TestScoreVec_get(&vec, 2)->id, 115); /* Last item was 115 */

        /* Pop */
        TestScoreItem popped;
        FLOW_ASSERT_TRUE(TestScoreVec_pop(&vec, &popped));
        FLOW_ASSERT_EQ(TestScoreVec_count(&vec), 14);

        /* Clear */
        TestScoreVec_clear(&vec);
        FLOW_ASSERT_TRUE(TestScoreVec_is_empty(&vec));
        FLOW_ASSERT_EQ(TestScoreVec_count(&vec), 0);

        printf("  ✓ Fixed Flat Vector push/pop/remove_unordered and bounds enforcement sound.\n\n");
    }

    /* ===================================================================== */
    /* STAGE 3: SMT Hyper-Box Polytope DSL (flow_smt_dsl.h)                  */
    /* ===================================================================== */
    FLOW_STAGE_BEGIN(3, "SMT Hyper-Box Polytope DSL (flow_smt_dsl.h)");
    {
        FLOW_SMT_BOX_BUILDER_DECL(builder);

        /* Add valid rules */
        FLOW_SMT_BOX_ADD_RULE(builder, "queue_depth", 1024, 1, 4096, FLOW_BOX_THEOREM_BUFFER_BOUNDS, "exceeds queue bound");
        FLOW_SMT_BOX_ADD_RULE(builder, "buffer_bytes", 32 * 1024 * 1024, 0, 64 * 1024 * 1024, FLOW_BOX_THEOREM_MEMORY_QUOTA, "exceeds buffer quota");
        FLOW_SMT_BOX_ADD_RULE(builder, "shard_id", 3, 0, 15, FLOW_BOX_THEOREM_SHARD_ISOLATION, "exceeds shard range");

        FlowSMTProofAttestation proof;
        FlowSMTResult res = FLOW_SMT_BOX_VERIFY(builder, "DSL_Test", &proof);
        FLOW_ASSERT_EQ(res, FLOW_SMT_PROVEN_UNSAT);
        FLOW_ASSERT_SMT_SOUND(proof);

        /* Add violating rule */
        FLOW_SMT_BOX_BUILDER_DECL(viol_builder);
        FLOW_SMT_BOX_ADD_RULE(viol_builder, "queue_depth", 8192, 1, 4096, FLOW_BOX_THEOREM_BUFFER_BOUNDS, "exceeds physical ring limit");
        FlowSMTProofAttestation viol_proof;
        FlowSMTResult viol_res = FLOW_SMT_BOX_VERIFY(viol_builder, "DSL_Violation", &viol_proof);
        FLOW_ASSERT_EQ(viol_res, FLOW_SMT_VIOLATION_SAT);
        FLOW_ASSERT_SMT_VIOLATION(viol_res, viol_proof);

        printf("  ✓ SMT Box DSL builder and declarative verification sound.\n\n");
    }

    /* ===================================================================== */
    /* STAGE 4: BitManifold Declarative Field Schema (flow_bmf_schema.h)     */
    /* ===================================================================== */
    FLOW_STAGE_BEGIN(4, "BitManifold Declarative Field Schema (flow_bmf_schema.h)");
    {
        uint64_t genome = 0;

        /* Pack fields declaratively */
        genome |= TestProtoKind_pack(2);        /* HTTP/2 (2) */
        genome |= TestProtoStreams_pack(128);   /* 128 multiplexed streams */
        genome |= TestProtoTable_pack(64);      /* 64 entries in HPACK table */
        genome |= TestProtoZeroCopy_pack(1);    /* Zero-copy active */

        /* Verify extraction */
        FLOW_ASSERT_EQ(TestProtoKind_get(genome), 2);
        FLOW_ASSERT_EQ(TestProtoStreams_get(genome), 128);
        FLOW_ASSERT_EQ(TestProtoTable_get(genome), 64);
        FLOW_ASSERT_EQ(TestProtoZeroCopy_get(genome), 1);

        /* Verify in-place setter */
        genome = TestProtoStreams_set(genome, 256);
        FLOW_ASSERT_EQ(TestProtoStreams_get(genome), 256);
        FLOW_ASSERT_EQ(TestProtoKind_get(genome), 2); /* Other fields untouched */
        FLOW_ASSERT_EQ(TestProtoTable_get(genome), 64);
        FLOW_ASSERT_EQ(TestProtoZeroCopy_get(genome), 1);

        /* Verify field mask */
        FLOW_ASSERT_EQ(TestProtoKind_mask(), 0x07ULL);
        FLOW_ASSERT_EQ(TestProtoZeroCopy_mask(), 1ULL << 19);

        printf("  ✓ BMF Field Schema declarative getters, setters, and masks verified.\n\n");
    }

    FLOW_TEST_SUITE_END();
    return 0;
}
