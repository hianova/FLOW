#include "flowy_fvec.h"
#include "registry.h"
#include "smt.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "semantic-rag-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

typedef struct {
    const char *prompt;
    const char *expected_id;
    const char *expected_component;
    double min_similarity;
} RAGTestCase;

int main(void) {
    flow_registry_init();

    printf("========================================================================================\n");
    printf("  🧠 SCENARIO 3: Semantic Topology RAG (Prompt-to-Architecture) Test Matrix\n");
    printf("========================================================================================\n\n");

    FlowVectorVault vault;
    flow_vault_init(&vault);
    flow_vault_seed_canonical_archetypes(&vault);

    RAGTestCase test_cases[] = {
        {
            .prompt = "high-frequency trading ultra-low latency lock-free ring buffer",
            .expected_id = "vec_hft_lockfree_trading",
            .expected_component = "bounded_queue",
            .min_similarity = 0.75
        },
        {
            .prompt = "serverless cloud lambda function with bursty batch compute",
            .expected_id = "vec_serverless_cpu_burst",
            .expected_component = "parallel_map",
            .min_similarity = 0.70
        },
        {
            .prompt = "embedded IoT sensor logging telemetry on 1MB RAM and battery",
            .expected_id = "vec_iot_embedded_sensor",
            .expected_component = "linear_array",
            .min_similarity = 0.75
        },
        {
            .prompt = "distributed Slowloris attack DDoS quarantine defense firewall",
            .expected_id = "vec_antibody_slowloris_409",
            .expected_component = "bounded_queue",
            .min_similarity = 0.75
        },
        {
            .prompt = "relational database sorted search index with monotonic keys",
            .expected_id = "vec_ordered_relational_index",
            .expected_component = "ordered_tree",
            .min_similarity = 0.75
        }
    };

    size_t num_cases = sizeof(test_cases) / sizeof(test_cases[0]);
    size_t passed_count = 0;

    for (size_t i = 0; i < num_cases; ++i) {
        const RAGTestCase *tc = &test_cases[i];
        size_t best_idx = 0;
        double best_sim = 0.0;

        CHECK(flow_vault_query_semantic(&vault, tc->prompt, &best_idx, &best_sim));
        const FlowVaultEntry *matched = flow_vault_get(&vault, best_idx);
        CHECK(matched != NULL);

        printf("  [Test %zu/%zu] Prompt: \"%s\"\n", i + 1, num_cases, tc->prompt);
        printf("    -> Recalled: [%s] (ID: %s, Component: %s)\n", matched->name, matched->id, matched->component_id);
        printf("    -> Cosine Similarity: %.4f (Min required: %.2f)\n", best_sim, tc->min_similarity);
        printf("    -> Pure Genome:       0x%016llx (SMT Zero-Defect Attested)\n", (unsigned long long)matched->pure_genome);

        CHECK(strcmp(matched->id, tc->expected_id) == 0);
        CHECK(strcmp(matched->component_id, tc->expected_component) == 0);
        CHECK(best_sim >= tc->min_similarity);
        CHECK(matched->proof.buffer_bounds_safety == FLOW_SMT_PROVEN_UNSAT);
        CHECK(matched->proof.memory_quota_bound == FLOW_SMT_PROVEN_UNSAT);

        passed_count++;
        printf("    ✓ PASSED (Accurate Prompt-to-Architecture Mapping)\n\n");
    }

    printf("========================================================================================\n");
    printf("SEMANTIC_RAG_TEST=passed total_cases=%zu passed=%zu prompt_to_architecture=verified smt_certified=100%%\n",
           num_cases, passed_count);
    return 0;
}
