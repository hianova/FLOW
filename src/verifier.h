#ifndef VERIFIER_H
#define VERIFIER_H

#include "flow.h"
#include "plugin.h"
#include "search.h"

int verify_candidate(const SemanticIR *ir, const Component *component,
                     const SearchResult *search, VerificationReport *report);
const char *verification_status_name(VerificationStatus status);

#endif
