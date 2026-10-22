#ifndef BACKEND_H
#define BACKEND_H
#include "flow.h"
#include "registry.h"
#include "search.h"
#include "smt.h"
int emit_c(FILE *output, const SemanticIR *ir, const Component *component, const SearchResult *search, const VerificationReport *verification, int reload_adapter);
int flow_emit_mlir(FILE *output, const SemanticIR *ir, const Component *component, const SearchResult *search, const VerificationReport *verification);
int flow_emit_llvm_ir(FILE *output, const SemanticIR *ir, const Component *component, const SearchResult *search, const VerificationReport *verification);
void flow_emit_abi_exports(FILE *output, const SemanticIR *ir, const Component *c, size_t cap);
int flow_emit_builtin_component(FILE *output, const SemanticIR *ir, const Component *component, const SearchResult *search, const VerificationReport *verification, int reload_adapter);
int flow_backend_run_in_memory(const SemanticIR *ir, const Component *component, const SearchResult *search, const VerificationReport *verification, FILE *out);
#endif
