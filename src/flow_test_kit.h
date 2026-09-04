#ifndef FLOW_TEST_KIT_H
#define FLOW_TEST_KIT_H

#include "smt.h"

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW Unified Test Kit & SMT Theorem Assertions (flow_test_kit.h)
 * ============================================================================
 *
 * Provides pure C17 standardized test lifecycle management, assertion macros,
 * and high-level SMT formal theorem verification assertions (QF_LIA Soundness
 * and Violation detection).
 * ============================================================================
 */

typedef struct {
    const char *suite_name;
    uint32_t total_tests;
    uint32_t passed_tests;
    uint32_t failed_tests;
    uint32_t current_stage;
} FlowTestSuiteState;

#define FLOW_TEST_SUITE_BEGIN(name_str) \
    static FlowTestSuiteState _flow_test_state = { \
        .suite_name = (name_str), \
        .total_tests = 0, \
        .passed_tests = 0, \
        .failed_tests = 0, \
        .current_stage = 0 \
    }; \
    printf("========================================================================================\n"); \
    printf("  🧪 Running FLOW Test Suite: %s\n", (name_str)); \
    printf("========================================================================================\n\n");

#define FLOW_STAGE_BEGIN(stage_num, stage_name_str) \
    do { \
        _flow_test_state.current_stage = (stage_num); \
        printf("--- [Stage %u: %s] ---\n", (unsigned)(stage_num), (stage_name_str)); \
    } while (0)

#define FLOW_ASSERT_TRUE(cond) \
    do { \
        _flow_test_state.total_tests++; \
        if (!(cond)) { \
            _flow_test_state.failed_tests++; \
            fprintf(stderr, "❌ ASSERT_TRUE FAILED [%s:%d]: %s\n", __FILE__, __LINE__, #cond); \
        } else { \
            _flow_test_state.passed_tests++; \
        } \
    } while (0)

#define FLOW_ASSERT_FALSE(cond) \
    do { \
        _flow_test_state.total_tests++; \
        if (cond) { \
            _flow_test_state.failed_tests++; \
            fprintf(stderr, "❌ ASSERT_FALSE FAILED [%s:%d]: %s\n", __FILE__, __LINE__, #cond); \
        } else { \
            _flow_test_state.passed_tests++; \
        } \
    } while (0)

#define FLOW_ASSERT_EQ(a, b) \
    do { \
        _flow_test_state.total_tests++; \
        if ((a) != (b)) { \
            _flow_test_state.failed_tests++; \
            fprintf(stderr, "❌ ASSERT_EQ FAILED [%s:%d]: (%s == %lld) != (%s == %lld)\n", \
                    __FILE__, __LINE__, #a, (long long)(a), #b, (long long)(b)); \
        } else { \
            _flow_test_state.passed_tests++; \
        } \
    } while (0)

#define FLOW_ASSERT_NE(a, b) \
    do { \
        _flow_test_state.total_tests++; \
        if ((a) == (b)) { \
            _flow_test_state.failed_tests++; \
            fprintf(stderr, "❌ ASSERT_NE FAILED [%s:%d]: %s == %s (%lld)\n", \
                    __FILE__, __LINE__, #a, #b, (long long)(a)); \
        } else { \
            _flow_test_state.passed_tests++; \
        } \
    } while (0)

#define FLOW_ASSERT_STR_EQ(a, b) \
    do { \
        _flow_test_state.total_tests++; \
        const char *_s1 = (a); \
        const char *_s2 = (b); \
        if (_s1 == NULL || _s2 == NULL || strcmp(_s1, _s2) != 0) { \
            _flow_test_state.failed_tests++; \
            fprintf(stderr, "❌ ASSERT_STR_EQ FAILED [%s:%d]: \"%s\" != \"%s\"\n", \
                    __FILE__, __LINE__, _s1 ? _s1 : "<null>", _s2 ? _s2 : "<null>"); \
        } else { \
            _flow_test_state.passed_tests++; \
        } \
    } while (0)

#define FLOW_ASSERT_STR_CONTAINS(str, substr) \
    do { \
        _flow_test_state.total_tests++; \
        const char *_target_str = (str); \
        const char *_sub_str = (substr); \
        if (_target_str == NULL || _sub_str == NULL || strstr(_target_str, _sub_str) == NULL) { \
            _flow_test_state.failed_tests++; \
            fprintf(stderr, "❌ ASSERT_STR_CONTAINS FAILED [%s:%d]: substring \"%s\" not found in \"%s\"\n", \
                    __FILE__, __LINE__, _sub_str ? _sub_str : "<null>", _target_str ? _target_str : "<null>"); \
        } else { \
            _flow_test_state.passed_tests++; \
        } \
    } while (0)

/*
 * SMT Formal Assertions
 */

#define FLOW_ASSERT_SMT_SOUND(attestation) \
    do { \
        _flow_test_state.total_tests++; \
        int _is_sound = ((attestation).buffer_bounds_safety == FLOW_SMT_PROVEN_UNSAT && \
                         (attestation).memory_quota_bound   == FLOW_SMT_PROVEN_UNSAT && \
                         (attestation).shard_non_aliasing   == FLOW_SMT_PROVEN_UNSAT && \
                         (attestation).determinism_invariant == FLOW_SMT_PROVEN_UNSAT); \
        if (!_is_sound) { \
            _flow_test_state.failed_tests++; \
            fprintf(stderr, "❌ ASSERT_SMT_SOUND FAILED [%s:%d]: SMT Invariants violated! Summary: %s\n", \
                    __FILE__, __LINE__, (attestation).proof_summary); \
        } else { \
            _flow_test_state.passed_tests++; \
            printf("  ✓ SMT Proof Sound: %s\n", (attestation).proof_summary); \
        } \
    } while (0)

#define FLOW_ASSERT_SMT_VIOLATION(res, attestation) \
    do { \
        _flow_test_state.total_tests++; \
        int _is_violation = ((res) == FLOW_SMT_VIOLATION_SAT) && \
                            (((attestation).buffer_bounds_safety == FLOW_SMT_VIOLATION_SAT) || \
                             ((attestation).memory_quota_bound   == FLOW_SMT_VIOLATION_SAT) || \
                             ((attestation).shard_non_aliasing   == FLOW_SMT_VIOLATION_SAT) || \
                             ((attestation).determinism_invariant == FLOW_SMT_VIOLATION_SAT)); \
        if (!_is_violation) { \
            _flow_test_state.failed_tests++; \
            fprintf(stderr, "❌ ASSERT_SMT_VIOLATION FAILED [%s:%d]: Expected SAT violation but got UNSAT/Unknown!\n", \
                    __FILE__, __LINE__); \
        } else { \
            _flow_test_state.passed_tests++; \
            printf("  ✓ SMT Violation Correctly Caught: %s\n", (attestation).proof_summary); \
        } \
    } while (0)

#define FLOW_ASSERT_SMT_BOX_SOUND(tag, box_constraints, count) \
    do { \
        FlowSMTProofAttestation _box_proof; \
        memset(&_box_proof, 0, sizeof(_box_proof)); \
        FlowSMTResult _box_res = flow_smt_verify_box_invariants((tag), (box_constraints), (count), &_box_proof); \
        _flow_test_state.total_tests++; \
        if (_box_res != FLOW_SMT_PROVEN_UNSAT) { \
            _flow_test_state.failed_tests++; \
            fprintf(stderr, "❌ ASSERT_SMT_BOX_SOUND FAILED [%s:%d]: %s\n", __FILE__, __LINE__, _box_proof.proof_summary); \
        } else { \
            _flow_test_state.passed_tests++; \
            printf("  ✓ SMT Box Proof Sound [%s]: %s\n", (tag), _box_proof.proof_summary); \
        } \
    } while (0)


#define FLOW_TEST_CASE(name_str, spec_literal, ...) \
    do { \
        printf("--- [Test Case: %s] ---\n", name_str); \
        const char *spec_src = (spec_literal); \
        FILE *mem = tmpfile(); \
        if (mem) { \
            fputs(spec_src, mem); \
            rewind(mem); \
        } \
        FlowSpec spec; \
        memset(&spec, 0, sizeof(spec)); \
        if (!mem || !parse_spec(mem, &spec)) { \
            fprintf(stderr, "❌ parse_spec failed\n"); \
            if (mem) fclose(mem); \
            exit(1); \
        } \
        fclose(mem); \
        SemanticIR ir; \
        memset(&ir, 0, sizeof(ir)); \
        lower_to_ir(&spec, &ir); \
        __VA_ARGS__ \
    } while(0)

#define FLOW_TEST_SUITE_END() \
    do { \
        printf("\n========================================================================================\n"); \
        printf("  🏁 SUITE SUMMARY [%s]: %u PASSED, %u FAILED (Total %u Assertions)\n", \
               _flow_test_state.suite_name, \
               _flow_test_state.passed_tests, \
               _flow_test_state.failed_tests, \
               _flow_test_state.total_tests); \
        printf("========================================================================================\n\n"); \
        if (_flow_test_state.failed_tests > 0) { \
            exit(1); \
        } \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* FLOW_TEST_KIT_H */
