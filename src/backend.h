#ifndef BACKEND_H
#define BACKEND_H

#include "flow.h"
#include "registry.h"
#include "search.h"
#include "smt.h"

int emit_c(FILE *output, const SemanticIR *ir, const Component *component,
           const SearchResult *search, const VerificationReport *verification,
           int reload_adapter);

int flow_emit_mlir(FILE *output, const SemanticIR *ir, const Component *component,
                   const SearchResult *search, const VerificationReport *verification);

int flow_emit_llvm_ir(FILE *output, const SemanticIR *ir, const Component *component,
                      const SearchResult *search, const VerificationReport *verification);

/* Built-in plugins use the existing inspectable emitters through this hook.
 * External plugins provide their own FlowPluginEmitFn instead. */
int flow_emit_builtin_component(FILE *output, const SemanticIR *ir,
                                const Component *component,
                                const SearchResult *search,
                                const VerificationReport *verification,
                                int reload_adapter);

int flow_emit_file_template(FILE *output, const char *path);
void flow_emit_metadata(FILE *output, const SemanticIR *ir,
                        const Component *component,
                        const SearchResult *search,
                        const VerificationReport *verification);

#endif
