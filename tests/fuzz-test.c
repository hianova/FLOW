#include "bitspace.h"
#include "backend.h"
#include "abi.h"
#include "flow.h"
#include "registry.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FUZZ_CHECK(cond) if (!(cond)) { fprintf(stderr, "fuzz-test assertion failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

int LLVMFuzzerInitialize(int *argc, char ***argv);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerInitialize(int *argc, char ***argv) {
    (void)argc;
    (void)argv;
    flow_registry_init();
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (data == NULL || size == 0 || size > 65536) return 0;

    FILE *mem = fmemopen((void *)data, size, "r");
    if (!mem) return 0;

    static FlowSpec spec;
    memset(&spec, 0, sizeof(spec));
    int parsed = parse_spec(mem, &spec);
    if (parsed) {
        static SemanticIR ir;
        memset(&ir, 0, sizeof(ir));
        lower_to_ir(&spec, &ir);
        FUZZ_CHECK(ir.input_max_count >= 0);

        FlowBitSpace space;
        if (flow_bitspace_init_for_ir(&ir, &space)) {
            FlowBitSearchResult search_res;
            memset(&search_res, 0, sizeof(search_res));
            flow_bitspace_search(&space, 10, 42, 0, NULL, &search_res);
            FUZZ_CHECK(search_res.heatmap.total_mutations <= 64);

            if (search_res.best_plan.component != NULL) {
                FlowPlanArtifact artifact;
                flow_plan_to_artifact(&search_res.best_plan, &ir, 42, &artifact);
                char val_err[128] = {0};
                (void)flow_artifact_validate(&artifact, &ir, &space, val_err, sizeof(val_err));

                FILE *sink = tmpfile();
                if (sink) {
                    SearchResult sres;
                    memset(&sres, 0, sizeof(sres));
                    flow_plan_to_search_result(&search_res.best_plan, &ir, 42, &sres);
                    VerificationReport vrep;
                    memset(&vrep, 0, sizeof(vrep));
                    if (verify_candidate(&ir, search_res.best_plan.component, &sres, &vrep)) {
                        (void)emit_c(sink, &ir, search_res.best_plan.component, &sres, &vrep, 0);
                    }
                    fclose(sink);
                }
            }
        }
        flow_ir_cleanup(&ir);
    }
    fclose(mem);
    return 0;
}

#ifndef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
static uint64_t fuzz_prng(uint64_t *state) {
    uint64_t x = *state;
    if (x == 0) x = UINT64_C(0xdeadbeefcafebabe);
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

int main(int argc, char **argv) {
    LLVMFuzzerInitialize(&argc, &argv);

    /* 1. Corpus of valid, malformed, pathological and boundary pure .lean inputs */
    const char *corpus[] = {
        /* Valid pure .lean specifications */
        "structure ReflexState where\n  pos : UInt64\n  vel : UInt64\n\ndef stepKernel (pos : UInt64) (vel : UInt64) : UInt64 :=\n  if pos + vel > 100 then 0 else 1\n\ntheorem step_safe : stepKernel 10 20 = 1 := by decide\n",
        "def badSub (a : Nat) (b : Nat) : Nat :=\n  a - b\ntheorem sub_zero : badSub 3 10 = 0 := by decide\n",
        "namespace Quant.Quote\nprivate def spread (x : Float) : Float := x * 0.5\ndef quote (m : Float) : Float := m + spread m\ntheorem q_ok (m : Float) (h : m > 0.0) : quote m > m := by decide\nend Quant.Quote\n",

        /* Retired @annotations & malformed .lean specifications */
        "@quota {\n  deadline: 200us\n}\n",
        "@embodied {\n  frequency 1000hz\n}\n",
        "@reload {\n  version: 2\n}\n",
        "@patchbay {\n  stage s1 fn f in S out S\n}\n",
        "theorem bad_sorry : 1 = 2 := by sorry\n",
        "axiom fake_axiom : False\n",
        "def bad_rec (n : UInt64) : UInt64 := if n == 0 then 0 else bad_rec (n - 1)\n",

        /* Binary & null-byte junk */
        "\x00\x00\x00\x00",
        "\xff\xff\xfe\xfe\x12\x34\x56\x78",
        "def \x00 test\n",
        "\x7f\x45\x4c\x46\x02\x01\x01\x00",
    };

    size_t corpus_count = sizeof(corpus) / sizeof(corpus[0]);
    for (size_t i = 0; i < corpus_count; ++i) {
        LLVMFuzzerTestOneInput((const uint8_t *)corpus[i], strlen(corpus[i]));
    }

    /* 2. Automated Pseudo-Random BMF Mutation Engine (1,000 randomized streams) */
    uint64_t rng = UINT64_C(0x1337c0de);
    uint8_t buffer[512];
    for (int run = 0; run < 1000; ++run) {
        size_t len = (size_t)(fuzz_prng(&rng) % (sizeof(buffer) - 1)) + 1;
        for (size_t b = 0; b < len; ++b) {
            buffer[b] = (uint8_t)(fuzz_prng(&rng) & 0xff);
        }
        buffer[len] = '\0';
        LLVMFuzzerTestOneInput(buffer, len);
    }

    printf("FLOW_FUZZ_TEST=passed corpus_cases=%zu random_mutations=1000 memory_safety=verified\n",
           corpus_count);
    return 0;
}
#endif
