#include "flowy.h"
#include "flowy_cli.h"
#include "topology.h"
#include "registry.h"
#include "orchestrator.h"
#include "flowy_fvec.h"
#include "flow_jet.h"
#include "backend.h"
#include "cmd_dispatch.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static int flowy_resolve_and_load_plugin(const char *mod_name) { if (!mod_name || !mod_name[0]) return 0; if (flow_registry_lookup(mod_name)) return 1;
    const char *base = (!strncmp(mod_name, "flow.", 5)) ? mod_name + 5 : mod_name;
    static const char *const fmts[] = {"build/libflow_%s.so", "build/libflow_%s.dylib", "libflow_%s.so", "libflow_%s.dylib", "build/lib%s.so", "build/lib%s.dylib"};
    char path[256], err[256] = {0};
    for (size_t i = 0; i < 6; ++i) { snprintf(path, sizeof(path), fmts[i], base); if (flow_registry_load_dso(path, err, sizeof(err))) return 1; } return 0; }
static int is_help_flag(const char *s) { return s && (!strcmp(s, "-h") || !strcmp(s, "--help") || !strcmp(s, "help")); }
static int match_any(const char *cmd, const char *const *list) { for (; *list; ++list) if (!strcmp(cmd, *list)) return 1; return 0; }
static int flowy_build_cmd(int argc, char **argv, int and_run) {
    if (argc < 3 || is_help_flag(argv[2])) { printf("Usage: flowy %s <spec.flow|spec.lean> [-o <binary>] [args...]\n", and_run ? "run" : "build"); return (argc < 3) ? EXIT_FAILURE : EXIT_SUCCESS; }
    const char *spec_file = argv[2], *out_bin = NULL; int extra_arg_start = argc;
    for (int i = 3; i < argc; ++i) if (!strcmp(argv[i], "-o") && i + 1 < argc) out_bin = argv[++i]; else { extra_arg_start = i; break; }
    char auto_bin[512] = {0}, temp_c[512] = {0}, cmd[1024]; if (!out_bin) {
        const char *slash = strrchr(spec_file, '/'), *base = slash ? slash + 1 : spec_file, *dot = strrchr(base, '.');
        snprintf(auto_bin, sizeof(auto_bin), "build/%.*s", (int)(dot ? (size_t)(dot - base) : strlen(base)), base); out_bin = auto_bin; } system("mkdir -p build 2>/dev/null");
    snprintf(temp_c, sizeof(temp_c), "/tmp/flow_%d_%lu.c", (int)getpid(), (unsigned long)time(NULL));
    char sibling_flowc[512] = {0}; const char *flowc_bin = getenv("FLOWC");
    if (!flowc_bin || !flowc_bin[0]) {
        const char * last_slash = (argc > 0 && argv[0]) ? strrchr(argv[0], '/') : NULL;
        if (last_slash) {
            snprintf(sibling_flowc, sizeof(sibling_flowc), "%.*s/flowc", (int)(last_slash - argv[0]), argv[0]);
            FILE *fs = fopen(sibling_flowc, "rb"); if (fs) { fclose(fs); flowc_bin = sibling_flowc; }
        }
    }
    if (!flowc_bin || !flowc_bin[0]) {
        FILE *ft = fopen("./build/flowc", "rb"); if (ft) { fclose(ft); flowc_bin = "./build/flowc"; } else flowc_bin = "flowc";
    }
    snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\" -o \"%s\"", flowc_bin, spec_file, temp_c);
    if (system(cmd) != 0) { fprintf(stderr, "flowy %s: failed to compile %s\n", and_run ? "run" : "build", spec_file); remove(temp_c); return EXIT_FAILURE; }
    const char *cc = getenv("CC"); if (!cc || !cc[0]) cc = "clang";
    snprintf(cmd, sizeof(cmd), "%s -O3 \"%s\" -o \"%s\" -lpthread -lm 2>/dev/null || %s -O3 \"%s\" -o \"%s\" -lm", cc, temp_c, out_bin, cc, temp_c, out_bin);
    int ret = system(cmd); remove(temp_c);
    if (ret != 0) { fprintf(stderr, "flowy %s: failed to generate binary %s\n", and_run ? "run" : "build", out_bin); return EXIT_FAILURE; }
    printf("  ✓ [Dual-Track: AOT Track] Built %s (SMT formally verified, standalone native binary)\n", out_bin); if (and_run) { char run_cmd[2048];
        int pos = snprintf(run_cmd, sizeof(run_cmd), "%s%s", (out_bin[0] == '/' || (out_bin[0] == '.' && out_bin[1] == '/')) ? "" : "./", out_bin);
        for (int i = extra_arg_start; i < argc; ++i) pos += snprintf(run_cmd + pos, sizeof(run_cmd) - pos, " \"%s\"", argv[i]);
        printf("▶ [Dual-Track: AOT Track] Executing %s:\n", run_cmd); return system(run_cmd); } return EXIT_SUCCESS; } static int flowy_run_jit_direct(const char *spec_file) {
    flow_registry_init(); FILE *input = fopen(spec_file, "r");
    if (!input) { fprintf(stderr, "flowy run: cannot open spec file '%s': %s\n", spec_file, strerror(errno)); return EXIT_FAILURE; } FlowSpec spec;
    if (!parse_spec(input, &spec)) { fprintf(stderr, "flowy run: failed to parse spec %s\n", spec_file); fclose(input); return EXIT_FAILURE; } fclose(input);
    SemanticIR ir; lower_to_ir(&spec, &ir); if (ir.imported_module_count > 0) { for (size_t m = 0; m < ir.imported_module_count; ++m) {
            flowy_resolve_and_load_plugin(ir.imported_modules[m]); const FlowPlugin *mod = flow_registry_lookup(ir.imported_modules[m]);
            if (mod) flow_plugin_lower_semantics(&spec, &ir, mod); } } else { const char *mname = spec.plugin_name[0] ? spec.plugin_name : "builtin";
        flowy_resolve_and_load_plugin(mname); const FlowPlugin *mod = flow_registry_lookup(mname); if (mod) flow_plugin_lower_semantics(&spec, &ir, mod); }
    const Component *component = select_component(&ir); if (!component) { flow_ir_cleanup(&ir); return -1; } VerificationReport verification;
    if (!verify_candidate(&ir, component, NULL, &verification)) { fprintf(stderr, "flowy run: formal verification failed: %s\n", verification.message); flow_ir_cleanup(&ir); return EXIT_FAILURE; }
    if (ir.extern_kernel[0]) { flow_ir_cleanup(&ir); return -1; }
    printf("[Dual-Track: JIT Direct Track] Executing %s (in-memory, 0 disk I/O, SMT UNSAT verified)...\n", ir.flow_name[0] ? ir.flow_name : spec_file);
    int ok = flow_backend_run_in_memory(&ir, component, NULL, &verification, stdout); flow_ir_cleanup(&ir); return ok ? EXIT_SUCCESS : -1; }
static int flowy_run_cmd(int argc, char **argv) {
    if (argc < 3 || is_help_flag(argv[2])) { printf("Usage: flowy run [--aot] <spec.flow> [args...]\n"); return (argc < 3) ? EXIT_FAILURE : EXIT_SUCCESS; }
    int use_aot = 0; const char *spec_file = NULL;
    for (int i = 2; i < argc; ++i) if (!strcmp(argv[i], "--aot")) use_aot = 1; else if (argv[i][0] != '-' && !spec_file) spec_file = argv[i];
    if (!spec_file) { fprintf(stderr, "flowy run: missing .flow specification\n"); return EXIT_FAILURE; } if (use_aot) { char *aot_argv[128]; int aot_argc = 0;
        aot_argv[aot_argc++] = argv[0]; aot_argv[aot_argc++] = argv[1];
        for (int i = 2; i < argc && aot_argc < 127; ++i) if (strcmp(argv[i], "--aot") != 0) aot_argv[aot_argc++] = argv[i]; aot_argv[aot_argc] = NULL;
        return flowy_build_cmd(aot_argc, aot_argv, 1); } int ret = flowy_run_jit_direct(spec_file);
    if (ret == -1) { printf("[Dual-Track: Fallback] Custom external plugin link required; falling back to AOT...\n"); return flowy_build_cmd(argc, argv, 1); } return ret; }
static void flowy_print_usage(FILE *out) { fprintf(out, "FLOW 2.0 Living System & Declarative Toolchain (flowy) v2.5.0\nUsage: flowy <command> [options...]\n\n"
                 "Primary Commands:\n"
                 "  run [--aot] <spec.flow> [args...] Run instantly via In-Memory JIT Direct Track (or --aot)\n"
                 "  build <spec.flow> [-o <bin>]   Build standalone AOT native binary\n"
                 "  why                            Explain real-time scheduling / hardware decision\n"
                 "  audit                          Run formal invariant & layer separation audit\n"
                 "  shell                          Start the autonomic interactive REPL\n"
                 "  inspect <subcommand>           Introspection: ask, why, timeline, bottleneck, audit, doc, topos\n"
                 "  jet <subcommand>               Phase Space Jet Bundles (.fjet) & Symplectic physics\n"
                 "  fvec <subcommand>              Gene vectors & immune antibody promotion (.fvec)\n"
                 "  topo <subcommand>              Topology graph and continuous living orchestrator\n"
                 "  test [suite]                   Run domain test suites (brain, body, concurrency, fvec-swarm, system, refactored-core, geometric-axiom, cubical-hott, all)\n"); } static int run_test_cmd(int argc, char **argv) {
    const char *suite = (argc >= 3) ? argv[2] : "all";
    if (is_help_flag(suite)) { printf("Usage: flowy test <brain|body|concurrency|fvec-swarm|system|refactored-core|geometric-axiom|cubical-hott|all>\n"); return EXIT_SUCCESS; }
    static const char *const suites[] = {"brain", "body", "concurrency", "fvec-swarm", "system", "refactored-core", "geometric-axiom", "cubical-hott"};
    static const char *const bins[] = {"./build/test-brain", "./build/test-body", "./build/test-concurrency", "./build/test-fvec-swarm", "./build/test-system", "./build/test-refactored-core", "./build/test-geometric-axiom", "./build/test-cubical-hott"};
    if (!strcmp(suite, "all")) { for (int i = 0; i < 8; ++i) { printf("▶ Running Suite [%s] (%s)...\n", suites[i], bins[i]); int ret = system(bins[i]);
            if (ret != 0) { fprintf(stderr, "❌ Suite [%s] failed with exit code %d\n", suites[i], ret); return EXIT_FAILURE; } }
        printf("  ✅ ALL 8 DOMAIN TEST SUITES PASSED\n"); return EXIT_SUCCESS; }
    for (int i = 0; i < 8; ++i) {
        if (!strcmp(suite, suites[i]) || (!strcmp(suite, "fvec") && !strcmp(suites[i], "fvec-swarm")) ||
            (!strcmp(suite, "core") && !strcmp(suites[i], "refactored-core")) ||
            (!strcmp(suite, "axiom") && !strcmp(suites[i], "geometric-axiom")) ||
            (!strcmp(suite, "hott") && !strcmp(suites[i], "cubical-hott"))) {
            printf("▶ Running Suite [%s] (%s)...\n", suites[i], bins[i]);
            return (system(bins[i]) == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
        }
    }
    fprintf(stderr, "Unknown test suite: %s\n", suite); return EXIT_FAILURE; }
int main(int argc, char **argv) { if (argc < 2 || is_help_flag(argv[1])) { flowy_print_usage(stdout); return EXIT_SUCCESS; }
    if (!strcmp(argv[1], "-v") || !strcmp(argv[1], "--version") || !strcmp(argv[1], "version")) {
        printf("FLOW System Framework (flowy) v2.5.0 | Bit-Manifold Form (BMF) & Cubical HoTT\n"); return EXIT_SUCCESS; }
    if (!strcmp(argv[1], "test")) return run_test_cmd(argc, argv); if (!strcmp(argv[1], "run")) return flowy_run_cmd(argc, argv);
    if (!strcmp(argv[1], "build") || !strcmp(argv[1], "compile")) return flowy_build_cmd(argc, argv, 0); if (!strcmp(argv[1], "topo")) {
        if (argc < 3 || is_help_flag(argv[2])) { cmd_topo_print_usage(stdout); return EXIT_SUCCESS; } return cmd_topo_run(argc - 1, argv + 1); }
    static const char *const topo_aliases[] = { "shell","--shell","absorb","--absorb","anneal","--anneal","landscape","--landscape",
        "refactor","--refactor","morph","--morph","what-if","--what-if","whatif","remediate", "--remediate","autopilot","--autopilot","daemon","--daemon",NULL };
    if (match_any(argv[1], topo_aliases)) return cmd_topo_run(argc, argv); flow_registry_init(); flowy_set_language(flowy_detect_system_language());
    for (int i = 1; i < argc; ++i) {
        if ((!strcmp(argv[i], "--lang") || !strcmp(argv[i], "-l")) && i + 1 < argc) {
            flowy_set_language(flowy_parse_language(argv[i + 1]));
            for (int j = i; j + 2 < argc; ++j) argv[j] = argv[j + 2];
            argc -= 2; i--;
        }
    }
    if (!strcmp(argv[1], "inspect")) {
        if (argc < 3 || is_help_flag(argv[2])) { cmd_inspect_print_usage(stdout); return EXIT_SUCCESS; } return cmd_inspect_run(argc - 2, argv + 2); }
    static const char *const inspect_aliases[] = { "ask","--ask","why","--why","timeline","--timeline",
        "explain-decisions","bottleneck","--bottleneck","audit","--audit","doc","--doc","topos","--topos",NULL };
    if (match_any(argv[1], inspect_aliases)) return cmd_inspect_run(argc - 1, argv + 1); if (!strcmp(argv[1], "lang") || !strcmp(argv[1], "language")) {
        if (argc >= 3) { FlowLanguage nl = flowy_parse_language(argv[2]); flowy_set_language(nl); printf("FLOW language switched to: %s\n", flowy_language_name(nl)); }
        else printf("Active FLOW language: %s\n", flowy_language_name(flowy_get_language())); return EXIT_SUCCESS; }
    if (!strcmp(argv[1], "fvec") || !strcmp(argv[1], "--fvec")) {
        if (argc < 3 || is_help_flag(argv[2])) { cmd_fvec_print_usage(stdout); return argc < 3 ? EXIT_FAILURE : EXIT_SUCCESS; } return cmd_fvec_run(argc - 1, argv + 1); }
    static const char *const fvec_aliases[] = {"rag","prompt","--rag","vault","--vault","hippocampus","antibody","--antibody","immune","query","--query","hub","--hub",NULL};
    if (match_any(argv[1], fvec_aliases)) return cmd_fvec_run(argc - 1, argv + 1); if (!strcmp(argv[1], "jet") || !strcmp(argv[1], "--jet")) {
        if (argc < 3 || is_help_flag(argv[2])) { cmd_jet_print_usage(argc < 3 ? stderr : stdout); return argc < 3 ? EXIT_FAILURE : EXIT_SUCCESS; }
        return cmd_jet_run(argc - 2, argv + 2); } fprintf(stderr, "Unknown command: %s\n\n", argv[1]); flowy_print_usage(stderr); return EXIT_FAILURE; }
