#include "vault.h"
#include "registry.h"
#include "smt.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "generative-architecture-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

int main(void) {
    flow_registry_init();

    printf("========================================================================================\n");
    printf("  ✨ SCENARIO 4: Generative AI Architecture Synthesis (Novel Species Generation)\n");
    printf("  (Diffusion Latent Sampling of Never-Before-Seen Topological Configurations)\n");
    printf("========================================================================================\n\n");

    FlowVectorVault vault;
    flow_vault_init(&vault);
    flow_vault_seed_canonical_archetypes(&vault);
    size_t canonical_count = vault.count;

    const char *radical_prompts[] = {
        "hybrid lock-free ring buffer with SoA columnar layout and SIMD batching for ultra-high throughput telemetry",
        "energy-neutral neuromorphic event queue with sparse updates and low memory footprint"
    };
    size_t num_prompts = sizeof(radical_prompts) / sizeof(radical_prompts[0]);

    for (size_t i = 0; i < num_prompts; ++i) {
        const char *prompt = radical_prompts[i];
        printf("  [Experiment %zu/%zu: Synthesizing Novel Architectural Species]\n", i + 1, num_prompts);
        printf("    -> Radical Input Prompt: \"%s\"\n", prompt);

        FlowVaultEntry synthesized;
        FlowSMTProofAttestation proof;
        uint64_t seed = 0xcafe1234ULL + (uint64_t)i * 0x5678ULL;

        CHECK(flow_vault_generative_synthesis(&vault, prompt, seed, &synthesized, &proof));

        printf("    -> Synthesized Species ID:  %s\n", synthesized.id);
        printf("    -> Generated Novel Genome:  0x%016llx\n", (unsigned long long)synthesized.pure_genome);
        printf("    -> Synthesized Component:   %s\n", synthesized.component_id);
        printf("    -> Baseline Energy:         %.2f\n", synthesized.baseline_energy);

        /* Verify that this is indeed a newly generated species */
        CHECK(strncmp(synthesized.id, "vec_gen_species_", 16) == 0);
        CHECK(synthesized.pure_genome != 0);

        /* Verify 100% SMT Zero-Defect Soundness */
        CHECK(proof.buffer_bounds_safety == FLOW_SMT_PROVEN_UNSAT);
        CHECK(proof.memory_quota_bound == FLOW_SMT_PROVEN_UNSAT);
        CHECK(proof.shard_non_aliasing == FLOW_SMT_PROVEN_UNSAT);
        CHECK(proof.determinism_invariant == FLOW_SMT_PROVEN_UNSAT);
        printf("    -> SMT Theorems Attested:   4/4 UNSAT (BufferBounds, MemoryQuota, ShardIsolation, Determinism)\n");
        printf("    ✓ PASSED (Novel Species Successfully Born into Hippocampus Vault)\n\n");
    }

    CHECK(vault.count == canonical_count + num_prompts);

    /* Test that newly synthesized species can be queried back via semantic RAG */
    size_t recalled_idx = 0;
    double recalled_sim = 0.0;
    CHECK(flow_vault_query_semantic(&vault, radical_prompts[0], &recalled_idx, &recalled_sim));
    printf("  [Recall Test of Synthesized Species]:\n");
    printf("    -> Query:    \"%s\"\n", radical_prompts[0]);
    printf("    -> Recalled: [%s] (Cosine Sim: %.4f)\n", vault.entries[recalled_idx].name, recalled_sim);
    CHECK(recalled_sim > 0.70);

    printf("\nGENERATIVE_ARCHITECTURE_TEST=passed synthesized_species=%zu diffusion_sampling=sound smt_certified=100%%\n",
           num_prompts);
    return 0;
}
