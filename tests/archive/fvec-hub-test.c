#include "flowy_fvec.h"
#include "registry.h"
#include "smt.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "fvec-hub-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

static void clean_dir(const char *dir) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    (void)system(cmd);
}

int main(void) {
    flow_registry_init();

    printf("========================================================================================\n");
    printf("  🌐 Running FLOW .fvec Ecosystem Hub & Community Sharing Suite\n");
    printf("========================================================================================\n\n");

    const char *hub_test_dir = "/tmp/flow_hub_test_sandbox";
    clean_dir(hub_test_dir);
    mkdir(hub_test_dir, 0755);

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 1: Hub Index Initialization & Community Models Discovery                    */
    /* ---------------------------------------------------------------------------------- */
    printf("--- [Stage 1: Hub Index Initialization & Discovery] ---\n");
    FlowHubIndex hub;
    CHECK(flow_hub_init_local_index(&hub) >= 5);
    printf("  ✓ Hub initialized with %zu community verified models.\n\n", hub.count);

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 2: Keyword & Intent-Based Search                                            */
    /* ---------------------------------------------------------------------------------- */
    printf("--- [Stage 2: Hub Search Queries] ---\n");
    FlowHubEntry matches[8];
    size_t found = 0;

    /* Search for io_uring */
    CHECK(flow_hub_search(&hub, "io_uring", matches, 8, &found));
    CHECK(found == 1);
    CHECK(strcmp(matches[0].model_id, "community/io_uring_edge_gateway") == 0);
    printf("  ✓ Search 'io_uring' -> Found: %s (Author: %s)\n", matches[0].name, matches[0].author);

    /* Search for HFT */
    CHECK(flow_hub_search(&hub, "HFT", matches, 8, &found));
    CHECK(found == 1);
    CHECK(strcmp(matches[0].model_id, "community/hft_lockfree_trading") == 0);
    printf("  ✓ Search 'HFT' -> Found: %s (Energy: %.2f)\n\n", matches[0].name, matches[0].energy_score);

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 3: Model Pull & CRC32 + SMT Soundness Verification Gate                     */
    /* ---------------------------------------------------------------------------------- */
    printf("--- [Stage 3: Pull Model with CRC32 & SMT Soundness Gate] ---\n");
    char pulled_path[512] = {0};
    CHECK(flow_hub_pull(&hub, "community/io_uring_edge_gateway", hub_test_dir, pulled_path, sizeof(pulled_path)));
    CHECK(access(pulled_path, F_OK) == 0);

    FlowVecHeader hdr;
    FlowVecPayload payload;
    CHECK(flow_fvec_read_file(pulled_path, &hdr, &payload));
    CHECK(strcmp(hdr.id, "community/io_uring_edge_gateway") == 0);
    CHECK(strstr(hdr.smt_signature, "BUFFER_UNSAT:MEM_UNSAT:SHARD_UNSAT:DET_UNSAT") != NULL);
    printf("  ✓ Pulled successfully to: '%s'\n", pulled_path);
    printf("    -> SMT Proof: %s\n", hdr.smt_signature);
    printf("    -> CRC32 Checksum: 0x%08x\n\n", payload.crc32);

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 4: Defense Against Tampered / SAT-Violating Corrupted Models                 */
    /* ---------------------------------------------------------------------------------- */
    printf("--- [Stage 4: Defense Against Malicious / Corrupted Models] ---\n");
    /* Write a valid one, then tamper with its payload byte */
    char corrupt_file[512];
    snprintf(corrupt_file, sizeof(corrupt_file), "%s/corrupt_model.fvec", hub_test_dir);
    CHECK(flow_fvec_write_file(corrupt_file, &hdr, &payload));

    FILE *f_tamper = fopen(corrupt_file, "r+b");
    CHECK(f_tamper != NULL);
    fseek(f_tamper, 1030, SEEK_SET); /* Into binary payload area */
    fputc(0xEE, f_tamper);
    fclose(f_tamper);

    /* Reading tampered file must FAIL due to CRC32 mismatch */
    FlowVecHeader t_hdr;
    FlowVecPayload t_payload;
    int read_tampered = flow_fvec_read_file(corrupt_file, &t_hdr, &t_payload);
    CHECK(read_tampered == 0);
    printf("  ✓ Security Gate: Tampered model caught by CRC32 check! Refused execution.\n\n");

    /* ---------------------------------------------------------------------------------- */
    /* STAGE 5: GitHub Release Manifest Packaging (flow_hub_push_package)                */
    /* ---------------------------------------------------------------------------------- */
    printf("--- [Stage 5: Packaging .fvec for GitHub Community Vault] ---\n");
    char pkg_meta[1024] = {0};
    CHECK(flow_hub_push_package(pulled_path, "community_engineer_42", pkg_meta, sizeof(pkg_meta)));
    CHECK(strstr(pkg_meta, "READY_FOR_GITHUB_GENE_VAULT") != NULL);
    CHECK(strstr(pkg_meta, "smt_certified\": true") != NULL);
    printf("  ✓ Package Manifest verified:\n%s\n\n", pkg_meta);

    printf("========================================================================================\n");
    printf("FVEC_HUB_TEST=passed search=verified pull_transplant=sound crc32_guard=verified github_pack=sound\n");
    printf("========================================================================================\n");

    clean_dir(hub_test_dir);
    return 0;
}
