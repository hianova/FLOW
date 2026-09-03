#include "flowy_fvec.h"
#include "registry.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "vault-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

int main(void) {
    flow_registry_init();

    FlowVectorVault vault;
    flow_vault_init(&vault);
    CHECK(vault.count == 0);

    /* 1. Test Seeding Canonical Archetypes */
    int count = flow_vault_seed_canonical_archetypes(&vault);
    CHECK(count >= 8);
    CHECK(vault.count == (size_t)count);

    /* 2. Test Cosine Similarity Properties */
    double a[FLOW_VAULT_DIM] = {1, 0, 0, 0};
    double b[FLOW_VAULT_DIM] = {1, 0, 0, 0};
    double c[FLOW_VAULT_DIM] = {0, 1, 0, 0};
    CHECK(flow_vault_cosine_similarity(a, b, FLOW_VAULT_DIM) > 0.999);
    CHECK(flow_vault_cosine_similarity(a, c, FLOW_VAULT_DIM) < 0.001);

    /* 3. Test Retrieval by Features */
    double query[FLOW_VAULT_DIM] = {0};
    query[8] = 1.0; /* Extreme latency priority */
    query[7] = 0.9;
    query[12] = 1.0;
    size_t best_idx = 0;
    double best_sim = 0.0;
    CHECK(flow_vault_query_nearest(&vault, query, (FlowVaultCategory)-1, &best_idx, &best_sim));
    CHECK(best_sim > 0.6);
    CHECK(strcmp(vault.entries[best_idx].component_id, "bounded_queue") == 0);

    /* 4. Test Persistence Save & Load */
    const char *tmp_file = "/tmp/test_flow_hippocampus.vault";
    CHECK(flow_vault_save_file(&vault, tmp_file));

    FlowVectorVault loaded;
    flow_vault_init(&loaded);
    CHECK(flow_vault_load_file(&loaded, tmp_file));
    CHECK(loaded.count == vault.count);
    for (size_t i = 0; i < loaded.count; ++i) {
        CHECK(strcmp(loaded.entries[i].id, vault.entries[i].id) == 0);
        CHECK(loaded.entries[i].pure_genome == vault.entries[i].pure_genome);
    }
    remove(tmp_file);

    /* 5. Test Antibody Gossip Broadcast & Ingestion */
    const FlowVaultEntry *ab = flow_vault_lookup_by_id(&vault, "vec_antibody_slowloris_409");
    CHECK(ab != NULL);

    char packet[512];
    CHECK(flow_vault_broadcast_antibody(&vault, ab, packet, sizeof(packet)) > 0);
    CHECK(strstr(packet, "FLOW_ANTIBODY_V1|") != NULL);

    FlowVectorVault remote_node_vault;
    flow_vault_init(&remote_node_vault);
    size_t ingested_idx = 0;
    CHECK(flow_vault_ingest_antibody(&remote_node_vault, packet, &ingested_idx));
    CHECK(remote_node_vault.count == 1);
    CHECK(strcmp(remote_node_vault.entries[ingested_idx].id, "vec_antibody_slowloris_409") == 0);
    CHECK(remote_node_vault.entries[ingested_idx].pure_genome == ab->pure_genome);

    printf("VAULT_TEST=passed canonical_archetypes=%zu serialization=verified cosine_similarity=sound antibody_gossip=verified\n", vault.count);
    return 0;
}
