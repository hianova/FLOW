#ifndef VERIFIER_H
#define VERIFIER_H

#include "flow.h"
#include "plugin.h"
#include "search.h"

int verify_candidate(const SemanticIR *ir, const Component *component,
                     const SearchResult *search, VerificationReport *report);
const char *verification_status_name(VerificationStatus status);

/* Pre-emptive Verifier Masks (Hard Constraints / Contract & Quota 1-Cycle Pruning) */
uint64_t flow_verifier_get_contract_mask(const SemanticIR *ir,
                                         const Component *comp,
                                         const FlowPlanDimensionSet *dims);
uint64_t flow_verifier_get_resource_mask(const SemanticIR *ir,
                                         const Component *comp,
                                         const FlowPlanDimensionSet *dims);

#endif
