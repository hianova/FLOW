#ifndef BACKEND_H
#define BACKEND_H

#include "flow.h"
#include "bitspace.h"
#include <stdio.h>

int emit_c(FILE *output, const SemanticIR *ir);
void flow_emit_abi_exports(FILE *output, const SemanticIR *ir, size_t cap);
void flow_emit_inline_lean_kernels(FILE *output, const SemanticIR *ir);
void flow_emit_ir_enums_and_structs(FILE *output, const SemanticIR *ir);

#endif /* BACKEND_H */
