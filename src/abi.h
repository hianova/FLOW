#ifndef FLOW_ABI_H
#define FLOW_ABI_H

#include "flow.h"
#include "bitspace.h"
#include "llvm_lifter.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

int flow_abi_emit_c_header(FILE *output, const SemanticIR *ir);
int flow_abi_emit_mex_dispatcher(FILE *output, const SemanticIR *ir);
int flow_abi_emit_lean_adapter(FILE *output, const SemanticIR *ir);
int flow_abi_audit_lean_certificate(const char *lean_source, char *err_msg, size_t err_size);
int flow_abi_verify_lean_certificate(const char *lean_path, char *err_msg, size_t err_size);

#endif /* FLOW_ABI_H */
