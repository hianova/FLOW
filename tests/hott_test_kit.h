#ifndef HOTT_TEST_KIT_H
#define HOTT_TEST_KIT_H

#include "flow_io.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *suite_name;
    uint32_t total_tests;
    uint32_t passed_tests;
    uint32_t failed_tests;
    uint32_t skipped_tests;
    uint32_t current_stage;
} HottTestSuiteState;

#define FLOW_TEST_SUITE_BEGIN(name_str)                                                                \
    static HottTestSuiteState _flow_test_state = {.suite_name = (name_str),                            \
                                                  .total_tests = 0,                                    \
                                                  .passed_tests = 0,                                   \
                                                  .failed_tests = 0,                                   \
                                                  .skipped_tests = 0,                                  \
                                                  .current_stage = 0};                                 \
    printf(                                                                                            \
        "========================================================================================\n"); \
    printf("  🧪 Running Test Suite: %s\n", (name_str));                                               \
    printf(                                                                                            \
        "========================================================================================\n\n");

#define FLOW_STAGE_BEGIN(stage_num, stage_name_str)                                  \
    do {                                                                             \
        _flow_test_state.current_stage = (stage_num);                                \
        printf("--- [Stage %u: %s] ---\n", (unsigned)(stage_num), (stage_name_str)); \
    } while (0)

#define FLOW_ASSERT_TRUE(cond)                                                                 \
    do {                                                                                       \
        _flow_test_state.total_tests++;                                                        \
        if (!(cond)) {                                                                         \
            _flow_test_state.failed_tests++;                                                   \
            fprintf(stderr, "❌ ASSERT_TRUE FAILED [%s:%d]: %s\n", __FILE__, __LINE__, #cond); \
        } else {                                                                               \
            _flow_test_state.passed_tests++;                                                   \
        }                                                                                      \
    } while (0)

#define FLOW_ASSERT_FALSE(cond)                                                                 \
    do {                                                                                        \
        _flow_test_state.total_tests++;                                                         \
        if (cond) {                                                                             \
            _flow_test_state.failed_tests++;                                                    \
            fprintf(stderr, "❌ ASSERT_FALSE FAILED [%s:%d]: %s\n", __FILE__, __LINE__, #cond); \
        } else {                                                                                \
            _flow_test_state.passed_tests++;                                                    \
        }                                                                                       \
    } while (0)

#define FLOW_ASSERT_EQ(a, b)                                                               \
    do {                                                                                   \
        long long _flow_a = (long long)(a);                                                \
        long long _flow_b = (long long)(b);                                                \
        _flow_test_state.total_tests++;                                                    \
        if (_flow_a != _flow_b) {                                                          \
            _flow_test_state.failed_tests++;                                               \
            fprintf(stderr, "❌ ASSERT_EQ FAILED [%s:%d]: (%s == %lld) != (%s == %lld)\n", \
                    __FILE__, __LINE__, #a, _flow_a, #b, _flow_b);                         \
        } else {                                                                           \
            _flow_test_state.passed_tests++;                                               \
        }                                                                                  \
    } while (0)

#define FLOW_ASSERT_NE(a, b)                                                                      \
    do {                                                                                          \
        long long _flow_a = (long long)(a);                                                       \
        long long _flow_b = (long long)(b);                                                       \
        _flow_test_state.total_tests++;                                                           \
        if (_flow_a == _flow_b) {                                                                 \
            _flow_test_state.failed_tests++;                                                      \
            fprintf(stderr, "❌ ASSERT_NE FAILED [%s:%d]: %s == %s (%lld)\n", __FILE__, __LINE__, \
                    #a, #b, _flow_a);                                                             \
        } else {                                                                                  \
            _flow_test_state.passed_tests++;                                                      \
        }                                                                                         \
    } while (0)

#define FLOW_ASSERT_FLOAT_EQ(a, b, eps)                                                           \
    do {                                                                                          \
        double _flow_fa = (double)(a);                                                            \
        double _flow_fb = (double)(b);                                                            \
        double _flow_eps = (double)(eps);                                                         \
        _flow_test_state.total_tests++;                                                           \
        double _diff = fabs(_flow_fa - _flow_fb);                                                 \
        if (!(_diff <= _flow_eps)) {                                                              \
            _flow_test_state.failed_tests++;                                                      \
            fprintf(stderr, "❌ ASSERT_FLOAT_EQ FAILED [%s:%d]: |%s - %s| = %g > %g\n", __FILE__, \
                    __LINE__, #a, #b, _diff, _flow_eps);                                          \
        } else {                                                                                  \
            _flow_test_state.passed_tests++;                                                      \
        }                                                                                         \
    } while (0)

#define FLOW_TEST_SUITE_END()                                                                                \
    do {                                                                                                     \
        printf(                                                                                              \
            "\n========================================================================================\n"); \
        printf(                                                                                              \
            "  🏁 SUITE SUMMARY [%s]: %u PASSED, %u FAILED, %u SKIPPED (Total %u Assertions)\n",             \
            _flow_test_state.suite_name, _flow_test_state.passed_tests,                                      \
            _flow_test_state.failed_tests, _flow_test_state.skipped_tests,                                   \
            _flow_test_state.total_tests);                                                                   \
        printf(                                                                                              \
            "========================================================================================\n\n"); \
        if (_flow_test_state.failed_tests > 0)                                                               \
            return 1;                                                                                        \
        return 0;                                                                                            \
    } while (0)

#endif /* HOTT_TEST_KIT_H */
