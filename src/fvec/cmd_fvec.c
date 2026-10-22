#include "../cmd_dispatch.h"
#include "../flowy_fvec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
void cmd_fvec_print_usage(FILE *out) { fprintf(out, "Usage: flowy fvec <seed|list|inspect|query|rag|remediate|gc> [options...]\n"); }
static void ensure_seeded_store(FlowVecStore *store) { flow_fvec_store_init(store, FLOW_FVEC_DEFAULT_DIR);
    if (!flow_fvec_store_scan(store)) { flow_fvec_seed_canonical_files(FLOW_FVEC_DEFAULT_DIR); flow_fvec_store_scan(store); } }
int cmd_fvec_run(int argc, char **argv) { if (argc < 1) { cmd_fvec_print_usage(stderr); return EXIT_FAILURE; }
    FlowVecStore store; ensure_seeded_store(&store); const char *cmd = argv[0];
    const char *action = (!strcmp(cmd, "fvec") || !strcmp(cmd, "--fvec")) ? (argc >= 2 ? argv[1] : "list") : cmd;
    int off = (!strcmp(cmd, "fvec") || !strcmp(cmd, "--fvec")) ? 2 : 1;
    if (!strcmp(action, "list") || !strcmp(action, "vault") || !strcmp(action, "--vault") || !strcmp(action, "antibody")) {
        flow_fvec_store_print_summary(&store, stdout); return EXIT_SUCCESS; }
    if (!strcmp(action, "seed")) { printf("✓ Seeded %d canonical .fvec models to '%s'\n", flow_fvec_seed_canonical_files(FLOW_FVEC_DEFAULT_DIR), FLOW_FVEC_DEFAULT_DIR); return EXIT_SUCCESS; }
    if (!strcmp(action, "inspect") || !strcmp(action, "show")) { FlowVecHeader hdr; FlowVecPayload pl;
        if (off >= argc || !flow_fvec_read_file(argv[off], &hdr, &pl)) return EXIT_FAILURE;
        flow_fvec_inspect(&hdr, &pl, stdout); return EXIT_SUCCESS; }
    if (!strcmp(action, "query") || !strcmp(action, "--query") || !strcmp(action, "rag") || !strcmp(action, "--rag") || !strcmp(action, "find")) {
        size_t best_idx = 0; double best_sim = 0.0; if (off >= argc || !flow_fvec_store_query(&store, argv[off], &best_idx, &best_sim)) return EXIT_FAILURE;
        const FlowVecRecord *r = &store.records[best_idx];
        printf("[.fvec Match] %s (Sim: %.2f%%) | Path: %s | Comp: %s | Genome: 0x%016llx\n", r->header.name, best_sim * 100.0, r->header.filepath, r->header.component_id, (unsigned long long)r->payload.pure_genome);
        return EXIT_SUCCESS; }
    if (!strcmp(action, "remediate") || !strcmp(action, "crisis")) { double ram = 98.0, miss = 0.05, conf = 0.0;
        for (int i = off; i < argc; ++i) if (!strcmp(argv[i], "--ram") && i + 1 < argc) ram = atof(argv[++i]); else if (!strcmp(argv[i], "--miss") && i + 1 < argc) miss = atof(argv[++i]);
        const FlowVecRecord *rec = NULL; char diag[512] = {0};
        if (flow_fvec_remediate_check(&store, ram, miss, &rec, &conf, diag, sizeof(diag))) printf("[Remediation] %s -> Apply: %s\n", diag, rec->header.filepath);
        else printf("[Remediation] Telemetry stable.\n");
        return EXIT_SUCCESS; }
    if (!strcmp(action, "gc") || !strcmp(action, "evict")) { uint64_t max_age = 30 * 86400ULL;
        for (int i = off; i < argc; ++i) if (!strcmp(argv[i], "--max-age") && i + 1 < argc) max_age = strtoull(argv[++i], NULL, 10);
        char evicted[16][256]; size_t n = flow_fvec_store_evict_senescent(&store, (uint64_t)time(NULL), max_age, evicted, 16);
        printf("[GC] Evicted %zu idle .fvec models (Remaining: %zu)\n", n, store.count); return EXIT_SUCCESS; }
    cmd_fvec_print_usage(stderr); return EXIT_FAILURE; }
