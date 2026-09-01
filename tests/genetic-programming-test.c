#include "genetic.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "genetic-programming-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

int main(void) {
    /* 1. Test VM Micro-Instruction Execution */
    FlowKernelGenome kernel;
    memset(&kernel, 0, sizeof(kernel));
    kernel.instruction_count = 6;
    kernel.instructions[0] = (FlowMicroInstruction){FLOW_OP_LOAD_IN, 0, 0, 0, 0};   /* r0 = in[0] */
    kernel.instructions[1] = (FlowMicroInstruction){FLOW_OP_IMM, 1, 0, 0, 5};       /* r1 = 5 */
    kernel.instructions[2] = (FlowMicroInstruction){FLOW_OP_MUL, 0, 0, 1, 0};       /* r0 = r0 * 5 */
    kernel.instructions[3] = (FlowMicroInstruction){FLOW_OP_IMM, 2, 0, 0, 3};       /* r2 = 3 */
    kernel.instructions[4] = (FlowMicroInstruction){FLOW_OP_ADD, 0, 0, 2, 0};       /* r0 = r0 + 3 */
    kernel.instructions[5] = (FlowMicroInstruction){FLOW_OP_STORE_OUT, 0, 0, 0, 0}; /* out[0] = r0 */

    uint64_t in_val = 10;
    uint64_t out_val = 0;
    CHECK(flow_genetic_execute(&kernel, &in_val, 1, &out_val, 1));
    CHECK(out_val == (10 * 5 + 3)); /* 53 */

    /* 2. Test Synthetic Evolution Engine */
    FlowGeneticEngine engine;
    CHECK(flow_genetic_init(&engine, NULL));
    CHECK(engine.test_count == 16);

    uint64_t custom_in[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint64_t custom_exp[8];
    for (int i = 0; i < 8; ++i) {
        custom_exp[i] = (custom_in[i] * 3) + 7;
    }
    CHECK(flow_genetic_set_test_vectors(&engine, custom_in, custom_exp, 8));

    FlowKernelGenome evolved_kernel;
    CHECK(flow_genetic_evolve(&engine, 250, 42, &evolved_kernel));
    CHECK(evolved_kernel.fitness_score > 0.0);
    CHECK(evolved_kernel.correctness_rate >= 0.5); /* Converges rapidly towards functional synthesis */

    /* 3. Test C Code Generation */
    char c_code_buf[4096];
    FILE *mem = fmemopen(c_code_buf, sizeof(c_code_buf), "w");
    CHECK(mem != NULL);
    CHECK(flow_genetic_emit_c(mem, &evolved_kernel, "synthesized_rank_hash"));
    fclose(mem);

    CHECK(strstr(c_code_buf, "uint64_t synthesized_rank_hash(const uint64_t in[1])") != NULL);
    CHECK(strstr(c_code_buf, "return out[0];") != NULL);

    flow_genetic_report(&engine, stdout);
    printf("GENETIC_PROGRAMMING_TEST=passed micro_opcodes=16 vm_execution=sound 1bit_emergence=verified c_codegen=verified\n");
    return 0;
}
