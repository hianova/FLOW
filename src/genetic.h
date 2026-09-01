#ifndef FLOW_GENETIC_H
#define FLOW_GENETIC_H

#include "flow.h"
#include "bitspace.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define FLOW_GENETIC_MAX_INSTRUCTIONS 16
#define FLOW_GENETIC_NUM_REGISTERS 8

typedef enum {
    FLOW_OP_NOP = 0,
    FLOW_OP_LOAD_IN,       /* r[dst] = in[src1] */
    FLOW_OP_STORE_OUT,     /* out[dst] = r[src1] */
    FLOW_OP_IMM,           /* r[dst] = imm */
    FLOW_OP_ADD,           /* r[dst] = r[src1] + r[src2] */
    FLOW_OP_SUB,           /* r[dst] = r[src1] - r[src2] */
    FLOW_OP_MUL,           /* r[dst] = r[src1] * r[src2] */
    FLOW_OP_SHL,           /* r[dst] = r[src1] << (r[src2] & 31) */
    FLOW_OP_SHR,           /* r[dst] = r[src1] >> (r[src2] & 31) */
    FLOW_OP_AND,           /* r[dst] = r[src1] & r[src2] */
    FLOW_OP_OR,            /* r[dst] = r[src1] | r[src2] */
    FLOW_OP_XOR,           /* r[dst] = r[src1] ^ r[src2] */
    FLOW_OP_MIN,           /* r[dst] = r[src1] < r[src2] ? r[src1] : r[src2] */
    FLOW_OP_MAX,           /* r[dst] = r[src1] > r[src2] ? r[src1] : r[src2] */
    FLOW_OP_SELECT,        /* r[dst] = r[src1] != 0 ? r[src2] : imm */
    FLOW_OP_HASH_STEP,     /* r[dst] = (r[src1] ^ (r[src1] >> 16)) * 0x45d9f3b */
    FLOW_OP_COUNT
} FlowOpcode;

typedef struct {
    FlowOpcode op;
    uint8_t dst_reg;
    uint8_t src_reg1;
    uint8_t src_reg2;
    int32_t imm;
} FlowMicroInstruction;

typedef struct {
    size_t instruction_count;
    FlowMicroInstruction instructions[FLOW_GENETIC_MAX_INSTRUCTIONS];
    uint64_t words[6]; /* 384-bit BitSpace genome representation */
    double fitness_score;
    double correctness_rate;
    int is_verified;
} FlowKernelGenome;

typedef struct {
    const SemanticIR *ir;
    uint64_t test_inputs[32];
    uint64_t expected_outputs[32];
    size_t test_count;
    FlowKernelGenome best_kernel;
    size_t total_evolutions;
    size_t zero_defect_pruned;
} FlowGeneticEngine;

/* Genetic Programming & Micro-Opcode Evolution APIs */
int flow_genetic_init(FlowGeneticEngine *engine, const SemanticIR *ir);
int flow_genetic_set_test_vectors(FlowGeneticEngine *engine, const uint64_t *inputs,
                                 const uint64_t *expected, size_t count);

int flow_genetic_evolve(FlowGeneticEngine *engine, size_t generations,
                        uint32_t seed, FlowKernelGenome *best_kernel_out);

int flow_genetic_execute(const FlowKernelGenome *kernel, const uint64_t *in,
                         size_t in_count, uint64_t *out, size_t out_count);

int flow_genetic_verify_kernel(const FlowGeneticEngine *engine, const FlowKernelGenome *kernel);
int flow_genetic_emit_c(FILE *out, const FlowKernelGenome *kernel, const char *func_name);
void flow_genetic_report(const FlowGeneticEngine *engine, FILE *out);

#endif
