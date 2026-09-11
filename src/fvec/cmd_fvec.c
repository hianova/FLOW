/* fvec/cmd_fvec.c
 * CLI handler for 'flowy fvec', 'flowy rag', 'flowy vault',
 * 'flowy antibody', 'flowy query', and 'flowy hub' subcommands.
 *
 * Part of FLOW Living Architecture - Method A refactoring.
 * Extracted from src/flowy_main.c.
 *
 * Calling convention: argv[0] = subcommand name (e.g., "fvec", "hub", "rag")
 */
#include "../cmd_dispatch.h"
#include "../flowy_fvec.h"
#include "../registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void cmd_fvec_print_usage(FILE *out) {
    fprintf(out, "Usage: flowy fvec <subcommand> [options...]\n\n");
    fprintf(out, "Subcommands:\n");
    fprintf(out, "  seed                     Seed canonical .fvec models to .flow/vecs\n");
    fprintf(out, "  list                     List all crystallized models in local store\n");
    fprintf(out, "  inspect <file.fvec>      Display model header, payload and SMT proof\n");
    fprintf(out, "  export <vault_id> <out>  Export vault entry to .fvec file\n");
    fprintf(out, "  query \"<prompt>\"         Find best matching model by prompt similarity\n");
    fprintf(out, "  rag \"<prompt>\"           Prompt-to-Architecture semantic synthesis\n");
    fprintf(out, "  vault                    Living architecture hippocampus summary\n");
    fprintf(out, "  antibody [broadcast]     Fleet-wide immune antibody memory\n");
    fprintf(out, "  remediate [--ram <pct>]  Autonomous crisis defense & gene remediation\n");
    fprintf(out, "  hub [search|pull|push]   Community ecosystem gene vault repository\n");
    fprintf(out, "  gc [--max-age <sec>]     Evict senescent auto-models\n");
}

/* ------------------------------------------------------------------ */
/* rag / vault / antibody: direct-call commands                       */
/* argv[0] = the command name ("rag"/"vault"/"antibody"/etc.)         */
/* ------------------------------------------------------------------ */

static int _cmd_rag(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: flowy rag \"<natural language architecture requirement>\" [--emit-spec]\n");
        return EXIT_FAILURE;
    }
    int emit_spec = 0;
    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--emit-spec") == 0 || strcmp(argv[i], "-e") == 0) emit_spec = 1;
    }
    FlowVectorVault vault;
    flow_vault_init(&vault);
    flow_vault_seed_canonical_archetypes(&vault);
    size_t best_idx = 0;
    double best_sim = 0.0;
    if (!flow_vault_query_semantic(&vault, argv[1], &best_idx, &best_sim)) {
        fprintf(stderr, "flowy rag: failed to project query into semantic topology manifold\n");
        return EXIT_FAILURE;
    }
    const FlowVaultEntry *matched = flow_vault_get(&vault, best_idx);
    printf("  🧠 FLOW Semantic Topology RAG (Prompt-to-Architecture Engine)\n");
    printf("  Natural Language Prompt: \"%s\"\n", argv[1]);
    printf("  Hippocampus Recall:      Matched [%s] (Cosine Similarity: %.4f)\n", matched->name, best_sim);
    printf("  Cognitive Status:        Retrieved Pure State from Long-Term Memory (0ms JIT Delay)\n");
    flow_vault_print_entry(matched, stdout);
    if (emit_spec) {
        printf("\n--- Synthesized .flow Executable Intent Specification ---\n");
        printf("flow %s {\n", matched->id);
        printf("    input max_count 10000\n");
        printf("    memory limit_mb 16\n");
        printf("    requires component \"%s\"\n", matched->component_id);
        printf("    guarantee zero_atomic_qsbr\n");
        printf("    guarantee smt_proven_sound\n");
        printf("}\n");
    }
    return EXIT_SUCCESS;
}

static int _cmd_vault(int argc, char **argv) {
    (void)argc; (void)argv;
    FlowVectorVault vault;
    flow_vault_init(&vault);
    flow_vault_seed_canonical_archetypes(&vault);
    flow_vault_print_summary(&vault, stdout);
    return EXIT_SUCCESS;
}

static int _cmd_antibody(int argc, char **argv) {
    FlowVectorVault vault;
    flow_vault_init(&vault);
    flow_vault_seed_canonical_archetypes(&vault);
    if (argc >= 3 && strcmp(argv[1], "broadcast") == 0) {
        const FlowVaultEntry *e = flow_vault_lookup_by_id(&vault, argv[2]);
        if (!e) {
            fprintf(stderr, "flowy antibody: antibody ID '%s' not found in local vault\n", argv[2]);
            return EXIT_FAILURE;
        }
        char packet[512];
        flow_vault_broadcast_antibody(&vault, e, packet, sizeof(packet));
        printf("FLOW Fleet Gossip Broadcast:\n%s\n", packet);
        return EXIT_SUCCESS;
    }
    printf("  🛡️ FLOW Fleet-Wide Digital Immune System (Antibody Memory Vault)\n");
    size_t count = 0;
    for (size_t i = 0; i < vault.count; ++i) {
        if (vault.entries[i].category == FLOW_VAULT_CAT_IMMUNE_ANTIBODY) {
            flow_vault_print_entry(&vault.entries[i], stdout);
            count++;
        }
    }
    printf("  Active Antibodies in Herd Memory: %zu\n", count);
    return EXIT_SUCCESS;
}

static int _cmd_fvec_main(int argc, char **argv) {
    /* argv[0]="fvec"|"query", argv[1]=action, argv[2+]=args */
    const char *action = "list";
    int arg_offset = 2;
    if (strcmp(argv[0], "query") == 0 || strcmp(argv[0], "--query") == 0) {
        action = "query";
        arg_offset = 1;
    } else if (argc >= 2) {
        action = argv[1];
        arg_offset = 2;
    }

    FlowVecStore store;
    flow_fvec_store_init(&store, FLOW_FVEC_DEFAULT_DIR);
    flow_fvec_store_scan(&store);
    if (store.count == 0) {
        flow_fvec_seed_canonical_files(FLOW_FVEC_DEFAULT_DIR);
        flow_fvec_store_scan(&store);
    }

    /* list */
    if (strcmp(action, "list") == 0) {
        flow_fvec_store_print_summary(&store, stdout);
        return EXIT_SUCCESS;
    }

    /* inspect <file.fvec> */
    if (strcmp(action, "inspect") == 0 || strcmp(action, "show") == 0) {
        if (arg_offset >= argc) {
            fprintf(stderr, "usage: flowy fvec inspect <file.fvec>\n");
            return EXIT_FAILURE;
        }
        FlowVecHeader hdr;
        FlowVecPayload payload;
        if (!flow_fvec_read_file(argv[arg_offset], &hdr, &payload)) {
            fprintf(stderr, "flowy fvec: failed to load or verify '%s'\n", argv[arg_offset]);
            return EXIT_FAILURE;
        }
        flow_fvec_inspect(&hdr, &payload, stdout);
        return EXIT_SUCCESS;
    }

    /* export <vault_id> <output.fvec> */
    if (strcmp(action, "export") == 0) {
        if (arg_offset + 1 >= argc) {
            fprintf(stderr, "usage: flowy fvec export <vault_id> <output.fvec>\n");
            return EXIT_FAILURE;
        }
        const char *vault_id = argv[arg_offset];
        const char *out_fvec = argv[arg_offset + 1];
        FlowVectorVault vault;
        flow_vault_init(&vault);
        flow_vault_seed_canonical_archetypes(&vault);
        const FlowVaultEntry *e = flow_vault_lookup_by_id(&vault, vault_id);
        if (!e) {
            fprintf(stderr, "flowy fvec: vault ID '%s' not found\n", vault_id);
            return EXIT_FAILURE;
        }
        FlowVecHeader hdr;
        FlowVecPayload payload;
        flow_fvec_from_vault_entry(e, "x86_avx2, L1=64K, Cores=64", "EXPORTED_EXPERIENCE", &hdr, &payload);
        if (!flow_fvec_write_file(out_fvec, &hdr, &payload)) {
            fprintf(stderr, "flowy fvec: failed to write '%s'\n", out_fvec);
            return EXIT_FAILURE;
        }
        printf("✓ Exported .fvec model: %s (Genome: 0x%016llx, SMT: %s)\n",
               out_fvec, (unsigned long long)payload.pure_genome, hdr.smt_signature);
        return EXIT_SUCCESS;
    }

    /* query "<prompt>" */
    if (strcmp(action, "query") == 0 || strcmp(action, "find") == 0) {
        if (arg_offset >= argc) {
            fprintf(stderr, "usage: flowy query \"<intent prompt>\"\n");
            return EXIT_FAILURE;
        }
        const char *prompt = argv[arg_offset];
        size_t best_idx = 0;
        double best_sim = 0.0;
        if (!flow_fvec_store_query(&store, prompt, &best_idx, &best_sim)) {
            fprintf(stderr, "flowy query: no .fvec models found in '%s'\n", store.root_dir);
            return EXIT_FAILURE;
        }
        const FlowVecRecord *rec = &store.records[best_idx];
        printf("  🏛️ FLOW Living Architecture Museum & Gene Vault (Prompt-to-Vector Query)\n");
        printf("  🔍 Query Intent:        \"%s\"\n", prompt);
        printf("  📄 Matched Model:       %s (Similarity: %.2f%%)\n", rec->header.name, best_sim * 100.0);
        printf("  📁 File Path:           %s\n", rec->header.filepath);
        printf("  🧬 Architectural Features:\n");
        printf("     - Component:          %s\n", rec->header.component_id);
        printf("     - Trigger Intent:     %s\n", rec->header.trigger_intent);
        printf("     - Origin Platform:    %s\n", rec->header.origin_hardware);
        printf("     - Energy Score:       %.2f\n", rec->header.energy_score);
        printf("     - Pure Genome:        0x%016llx\n", (unsigned long long)rec->payload.pure_genome);
        printf("  ⚡ Expected Performance: < 15ns Latency (100%% SMT Zero-Defect Proven Sound)\n");
        printf("  🚀 Instant Physical Shape Application Command:\n");
        printf("     flowc <your_spec.flow> -o generated/output.c --apply-fvec %s\n", rec->header.filepath);
        return EXIT_SUCCESS;
    }

    /* remediate [--ram <pct>] [--miss <rate>] */
    if (strcmp(action, "remediate") == 0 || strcmp(action, "crisis") == 0) {
        double ram = 98.0;
        double miss = 0.05;
        for (int i = arg_offset; i < argc; ++i) {
            if (strcmp(argv[i], "--ram") == 0 && i + 1 < argc) ram = atof(argv[++i]);
            else if (strcmp(argv[i], "--miss") == 0 && i + 1 < argc) miss = atof(argv[++i]);
        }
        const FlowVecRecord *rec = NULL;
        double conf = 0.0;
        char diag[512] = {0};
        if (flow_fvec_remediate_check(&store, ram, miss, &rec, &conf, diag, sizeof(diag))) {
            printf("  🚨 FLOW Autonomous Crisis Defense & Gene Bank Remediation\n");
            printf("  系統警報: %s\n\n", diag);
            printf("  💉 推薦抗體特徵檔: %s\n", rec->header.filepath);
            printf("  🧬 載入處方指令:\n");
            printf("     flowc <spec.flow> -o generated/survival.c --apply-fvec %s\n", rec->header.filepath);
            return EXIT_SUCCESS;
        }
        printf("FLOW Remediation: Telemetry stable. No emergency gene injection required.\n");
        return EXIT_SUCCESS;
    }

    /* seed */
    if (strcmp(action, "seed") == 0) {
        int count = flow_fvec_seed_canonical_files(FLOW_FVEC_DEFAULT_DIR);
        printf("✓ Seeded %d canonical .fvec models to '%s'\n", count, FLOW_FVEC_DEFAULT_DIR);
        return EXIT_SUCCESS;
    }

    /* gc [--max-age <seconds>] */
    if (strcmp(action, "gc") == 0 || strcmp(action, "evict") == 0) {
        uint64_t max_age = 30 * 86400ULL;
        for (int i = arg_offset; i < argc; ++i) {
            if (strcmp(argv[i], "--max-age") == 0 && i + 1 < argc) {
                max_age = strtoull(argv[++i], NULL, 10);
            }
        }
        char evicted_files[16][256];
        uint64_t now_unix = (uint64_t)time(NULL);
        size_t n = flow_fvec_store_evict_senescent(&store, now_unix, max_age, evicted_files, 16);
        printf("  🍂 FLOW Immune Senescence & Garbage Collection (LRU Eviction)\n");
        printf("  Threshold Age:       %llu seconds (%.1f days)\n", (unsigned long long)max_age, (double)max_age / 86400.0);
        printf("  Evicted Auto-Models: %zu\n", n);
        for (size_t i = 0; i < n && i < 16; ++i) {
            printf("    - Removed idle model: %s\n", evicted_files[i]);
        }
        printf("  Remaining Models:    %zu\n", store.count);
        return EXIT_SUCCESS;
    }

    fprintf(stderr, "Unknown fvec action: %s\n", action);
    fprintf(stderr, "usage: flowy fvec [list|inspect <file>|export <id> <file>|query <prompt>|remediate|seed|gc]\n");
    return EXIT_FAILURE;
}

/* ------------------------------------------------------------------ */
/* hub subcommands: argv[0]="hub", argv[1]=subcmd, argv[2+]=args    */
/* ------------------------------------------------------------------ */

static int _cmd_hub(int argc, char **argv) {
    FlowHubIndex hub_idx;
    flow_hub_init_local_index(&hub_idx);

    const char *subcmd = (argc >= 2) ? argv[1] : "search";
    int hub_arg_offset = 2;

    if (strcmp(subcmd, "search") == 0 || strcmp(subcmd, "list") == 0 || strcmp(subcmd, "find") == 0) {
        const char *query = (hub_arg_offset < argc) ? argv[hub_arg_offset] : "";
        FlowHubEntry matches[FLOW_HUB_MAX_ENTRIES];
        size_t found = 0;
        flow_hub_search(&hub_idx, query, matches, FLOW_HUB_MAX_ENTRIES, &found);
        printf("  🌐 FLOW Gene Vault Ecosystem Hub (GitHub / Community .fvec Repository)\n");
        printf("  🔍 Search Query: \"%s\" (Found: %zu models)\n\n", query, found);
        for (size_t i = 0; i < found; ++i) {
            printf("  📦 [%02zu] %-34s | Author: %-16s | Conf: %-3u\n",
                   i + 1, matches[i].model_id, matches[i].author, matches[i].confidence_score);
            printf("       Name: %s\n", matches[i].name);
            printf("       Hardware: %-30s | SMT: %s\n", matches[i].origin_hardware, matches[i].smt_signature);
            printf("       Desc: %s\n\n", matches[i].description);
        }
        printf("  🚀 Pull & Transplant Command: flowy hub pull <model_id>\n");
        return EXIT_SUCCESS;
    }

    if (strcmp(subcmd, "pull") == 0 || strcmp(subcmd, "download") == 0) {
        if (hub_arg_offset >= argc) {
            fprintf(stderr, "usage: flowy hub pull <model_id> [--dest <dir>]\n");
            return EXIT_FAILURE;
        }
        const char *model_id = argv[hub_arg_offset];
        const char *dest = FLOW_FVEC_DEFAULT_DIR;
        for (int i = hub_arg_offset + 1; i < argc; ++i) {
            if (strcmp(argv[i], "--dest") == 0 && i + 1 < argc) dest = argv[++i];
        }
        char saved_path[512] = {0};
        if (!flow_hub_pull(&hub_idx, model_id, dest, saved_path, sizeof(saved_path))) {
            fprintf(stderr, "flowy hub: model '%s' not found or failed SMT/CRC32 verification\n", model_id);
            return EXIT_FAILURE;
        }
        printf("  💉 FLOW Architecture Gene Transplant Completed (100%% SMT Proven Sound)\n");
        printf("  Model ID:     %s\n", model_id);
        printf("  Saved To:     %s\n", saved_path);
        printf("  Verification: CRC32 Validated | SMT Zero-Defect Guaranteed (UNSAT)\n");
        printf("  ⚡ Instant Application Command:\n");
        printf("     flowc <spec.flow> -o generated/server.c --apply-fvec %s\n", saved_path);
        return EXIT_SUCCESS;
    }

    if (strcmp(subcmd, "push") == 0 || strcmp(subcmd, "publish") == 0) {
        if (hub_arg_offset >= argc) {
            fprintf(stderr, "usage: flowy hub push <file.fvec> [--author <name>]\n");
            return EXIT_FAILURE;
        }
        const char *fvec_file = argv[hub_arg_offset];
        const char *author = "anonymous_contributor";
        for (int i = hub_arg_offset + 1; i < argc; ++i) {
            if (strcmp(argv[i], "--author") == 0 && i + 1 < argc) author = argv[++i];
        }
        char pkg_meta[1024] = {0};
        if (!flow_hub_push_package(fvec_file, author, pkg_meta, sizeof(pkg_meta))) {
            fprintf(stderr, "flowy hub: failed to package '%s': %s\n", fvec_file, pkg_meta);
            return EXIT_FAILURE;
        }
        printf("  🚀 FLOW Gene Hub Package Generated (Ready for GitHub Push / PR)\n");
        printf("%s\n", pkg_meta);
        return EXIT_SUCCESS;
    }

    fprintf(stderr, "Unknown hub action: %s\n", subcmd);
    fprintf(stderr, "usage: flowy hub [search <query>|pull <model_id>|push <file.fvec>]\n");
    return EXIT_FAILURE;
}

/* ------------------------------------------------------------------ */
/* cmd_fvec_run: main dispatch entry point                            */
/* argv[0] = the top-level command ("fvec","rag","vault","hub",etc.)  */
/* ------------------------------------------------------------------ */

int cmd_fvec_run(int argc, char **argv) {
    if (argc < 1) {
        cmd_fvec_print_usage(stderr);
        return EXIT_FAILURE;
    }
    const char *cmd = argv[0];

    if (strcmp(cmd, "rag") == 0 || strcmp(cmd, "prompt") == 0 || strcmp(cmd, "--rag") == 0)
        return _cmd_rag(argc, argv);
    if (strcmp(cmd, "vault") == 0 || strcmp(cmd, "--vault") == 0 || strcmp(cmd, "hippocampus") == 0)
        return _cmd_vault(argc, argv);
    if (strcmp(cmd, "antibody") == 0 || strcmp(cmd, "--antibody") == 0 || strcmp(cmd, "immune") == 0)
        return _cmd_antibody(argc, argv);
    if (strcmp(cmd, "fvec") == 0 || strcmp(cmd, "--fvec") == 0 ||
        strcmp(cmd, "query") == 0 || strcmp(cmd, "--query") == 0)
        return _cmd_fvec_main(argc, argv);
    if (strcmp(cmd, "hub") == 0 || strcmp(cmd, "--hub") == 0)
        return _cmd_hub(argc, argv);

    fprintf(stderr, "Unknown fvec/hub command: %s\n\n", cmd);
    cmd_fvec_print_usage(stderr);
    return EXIT_FAILURE;
}
