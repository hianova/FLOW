#include "flow_test_kit.h"
#include "backend.h"
#include "flow.h"
#include "registry.h"
#include "smt.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "mlir-llvm-test assertion failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

int main(void) {
    flow_registry_init();

    FLOW_TEST_CASE("tests/mlir-llvm-test.c",
"input task_stream {\n"
        "    max_count 4096\n"
        "}\n"
        "flow parallel_pipeline {\n"
        "    task_stream -> transform -> collect\n"
        "}\n"
        "import builtin\n"
        "require {\n"
        "    deterministic\n"
        "    memory < 32mb\n"
        "}\n",
{
const Component *comp = select_component(&ir);
    CHECK(comp != NULL);

    SearchResult search = {0};
    search.component = comp;
    search.capacity = 4096;
    search.threads = 4;
    search.shards = 1;

    VerificationReport verification = {0};
    verify_candidate(&ir, comp, &search, &verification);

    /* 1. Test MLIR Dialect Emitter */
    char mlir_buffer[4096];
    FILE *mlir_out = fmemopen(mlir_buffer, sizeof(mlir_buffer), "w");
    CHECK(mlir_out != NULL);
    CHECK(flow_emit_mlir(mlir_out, &ir, comp, &search, &verification));
    fclose(mlir_out);

    CHECK(strstr(mlir_buffer, "module @parallel_pipeline") != NULL);
    CHECK(strstr(mlir_buffer, "flow.intent @parallel_pipeline") != NULL);
    CHECK(strstr(mlir_buffer, "flow.constraint.capacity 4096 : index") != NULL);
    CHECK(strstr(mlir_buffer, "flow.constraint.memory_limit 33554432 : i64") != NULL);
    CHECK(strstr(mlir_buffer, "func.func @flow_kernel") != NULL);
    CHECK(strstr(mlir_buffer, "scf.for %iv = %c0 to %cap step %c1") != NULL);

    /* 2. Test LLVM IR Emitter */
    char ll_buffer[4096];
    FILE *ll_out = fmemopen(ll_buffer, sizeof(ll_buffer), "w");
    CHECK(ll_out != NULL);
    CHECK(flow_emit_llvm_ir(ll_out, &ir, comp, &search, &verification));
    fclose(ll_out);

    CHECK(strstr(ll_buffer, "%struct.flow_item = type { i32, i32 }") != NULL);
    CHECK(strstr(ll_buffer, "define i32 @flow_init(ptr %state_out)") != NULL);
    CHECK(strstr(ll_buffer, "define i32 @flow_run(ptr %state, ptr %input, ptr %output)") != NULL);
    CHECK(strstr(ll_buffer, "define void @flow_drop(ptr %state)") != NULL);

    


    printf("MLIR_LLVM_TEST=passed mlir_dialect=flow.intent llvm_ir=lto_ready\n");
    return 0;

});
}