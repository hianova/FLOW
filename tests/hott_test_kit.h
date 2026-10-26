#ifndef HOTT_TEST_KIT_H
#define HOTT_TEST_KIT_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include "flow.h"

typedef struct {
    const char *suite_name;
    uint32_t total_tests;
    uint32_t passed_tests;
    uint32_t failed_tests;
    uint32_t skipped_tests;
    uint32_t current_stage;
} HottTestSuiteState;

#define FLOW_TEST_SUITE_BEGIN(name_str) \
    static HottTestSuiteState _flow_test_state = { \
        .suite_name = (name_str), \
        .total_tests = 0, \
        .passed_tests = 0, \
        .failed_tests = 0, \
        .skipped_tests = 0, \
        .current_stage = 0 \
    }; \
    printf("========================================================================================\n"); \
    printf("  🧪 Running Test Suite: %s\n", (name_str)); \
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
        long long _flow_a = (long long)(a); \
        long long _flow_b = (long long)(b); \
        _flow_test_state.total_tests++; \
        if (_flow_a != _flow_b) { \
            _flow_test_state.failed_tests++; \
            fprintf(stderr, "❌ ASSERT_EQ FAILED [%s:%d]: (%s == %lld) != (%s == %lld)\n", \
                    __FILE__, __LINE__, #a, _flow_a, #b, _flow_b); \
        } else { \
            _flow_test_state.passed_tests++; \
        } \
    } while (0)

#define FLOW_ASSERT_NE(a, b) \
    do { \
        long long _flow_a = (long long)(a); \
        long long _flow_b = (long long)(b); \
        _flow_test_state.total_tests++; \
        if (_flow_a == _flow_b) { \
            _flow_test_state.failed_tests++; \
            fprintf(stderr, "❌ ASSERT_NE FAILED [%s:%d]: %s == %s (%lld)\n", \
                    __FILE__, __LINE__, #a, #b, _flow_a); \
        } else { \
            _flow_test_state.passed_tests++; \
        } \
    } while (0)

#define FLOW_ASSERT_STR_EQ(a, b) \
    do { \
        const char *_flow_sa = (const char *)(a); \
        const char *_flow_sb = (const char *)(b); \
        _flow_test_state.total_tests++; \
        if (!_flow_sa || !_flow_sb || strcmp(_flow_sa, _flow_sb) != 0) { \
            _flow_test_state.failed_tests++; \
            fprintf(stderr, "❌ ASSERT_STR_EQ FAILED [%s:%d]: \"%s\" != \"%s\"\n", \
                    __FILE__, __LINE__, _flow_sa ? _flow_sa : "(null)", _flow_sb ? _flow_sb : "(null)"); \
        } else { \
            _flow_test_state.passed_tests++; \
        } \
    } while (0)

#define FLOW_ASSERT_FLOAT_EQ(a, b, eps) \
    do { \
        double _flow_fa = (double)(a); \
        double _flow_fb = (double)(b); \
        double _flow_eps = (double)(eps); \
        _flow_test_state.total_tests++; \
        double _diff = fabs(_flow_fa - _flow_fb); \
        if (!(_diff <= _flow_eps)) { \
            _flow_test_state.failed_tests++; \
            fprintf(stderr, "❌ ASSERT_FLOAT_EQ FAILED [%s:%d]: |%s - %s| = %g > %g\n", \
                    __FILE__, __LINE__, #a, #b, _diff, _flow_eps); \
        } else { \
            _flow_test_state.passed_tests++; \
        } \
    } while (0)

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

#define FLOW_TEST_SUITE_END() \
    do { \
        printf("\n========================================================================================\n"); \
        printf("  🏁 SUITE SUMMARY [%s]: %u PASSED, %u FAILED, %u SKIPPED (Total %u Assertions)\n", \
               _flow_test_state.suite_name, _flow_test_state.passed_tests, \
               _flow_test_state.failed_tests, _flow_test_state.skipped_tests, \
               _flow_test_state.total_tests); \
        printf("========================================================================================\n\n"); \
        if (_flow_test_state.failed_tests > 0) return 1; \
        return 0; \
    } while (0)

#define FLOW_SMT_PROOF_POISON(p) do { \
    (p)->buffer_bounds_safety = FLOW_SMT_VIOLATION_SAT; \
    (p)->memory_quota_bound = FLOW_SMT_VIOLATION_SAT; \
    (p)->shard_non_aliasing = FLOW_SMT_VIOLATION_SAT; \
    (p)->determinism_invariant = FLOW_SMT_VIOLATION_SAT; \
    (p)->proof_summary[0] = '\0'; \
} while (0)

#endif /* HOTT_TEST_KIT_H */
