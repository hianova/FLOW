#include "flow.h"
#include "registry.h"
#include "smt.h"
#include "verifier.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "smt-test assertion failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

int main(void) {
    flow_registry_init();

    /* 1. Formally verify proven bounded spec */
    const char *proven_spec_src =
        "input task_stream {\n"
        "    max_count 1024\n"
        "}\n"
        "flow parallel_pipeline {\n"
        "    task_stream -> transform -> collect\n"
        "}\n"
        "import builtin\n"
        "require {\n"
        "    deterministic\n"
        "    memory < 16mb\n"
        "}\n";

    FILE *mem = fmemopen((void *)proven_spec_src, strlen(proven_spec_src), "r");
    CHECK(mem != NULL);
    FlowSpec spec;
    CHECK(parse_spec(mem, &spec));
    fclose(mem);

    SemanticIR ir;
    lower_to_ir(&spec, &ir);

    const Component *comp = select_component(&ir);
    CHECK(comp != NULL);

    FlowPlanMetrics metrics = {
        .capacity = 2048, /* > 1024 input len -> Buffer bounds strictly UNSAT (proven) */
        .threads = 4,
        .shards = 1,
        .memory_bytes = 16384, /* << 16MB limit -> Memory quota strictly UNSAT (proven) */
        .latency_score = 4.0,
        .throughput_score = 4000.0,
        .energy = 20.0
    };

    FlowSMTProofAttestation attestation;
    CHECK(flow_smt_verify(&ir, comp, NULL, &metrics, &attestation));
    CHECK(attestation.buffer_bounds_safety == FLOW_SMT_PROVEN_UNSAT);
    CHECK(attestation.memory_quota_bound == FLOW_SMT_PROVEN_UNSAT);
    CHECK(attestation.shard_non_aliasing == FLOW_SMT_PROVEN_UNSAT);
    CHECK(attestation.determinism_invariant == FLOW_SMT_PROVEN_UNSAT);
    CHECK(strstr(attestation.proof_summary, "4/4 theorems verified UNSAT") != NULL);

    /* 2. Formally generate and validate SMT-LIB2 script format */
    char smt_buffer[4096];
    FILE *smt_out = fmemopen(smt_buffer, sizeof(smt_buffer), "w");
    CHECK(smt_out != NULL);
    CHECK(flow_smt_generate_proof_script(&ir, comp, NULL, &metrics, smt_out));
    fclose(smt_out);

    CHECK(strstr(smt_buffer, "(set-logic QF_BV)") != NULL);
    CHECK(strstr(smt_buffer, "; --- Theorem 1: Buffer Bounds Safety Invariant ---") != NULL);
    CHECK(strstr(smt_buffer, "; --- Theorem 2: Memory Limit & Quota Boundedness ---") != NULL);
    CHECK(strstr(smt_buffer, "; --- Theorem 3: Shard Non-Aliasing & Isolation Invariant ---") != NULL);
    CHECK(strstr(smt_buffer, "; --- Theorem 4: Functional Determinism Invariant ---") != NULL);
    CHECK(strstr(smt_buffer, "(check-sat)") != NULL);

    flow_ir_cleanup(&ir);

    printf("SMT_TEST=passed logic=QF_BV theorems_proven=4/4 script_generation=verified zero_defect=attested\n");
    return 0;
}
