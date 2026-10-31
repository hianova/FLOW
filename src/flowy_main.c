#include "flowy.h"
#include "registry.h"
#include "backend.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int run_argv_sync(const char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0)
        return EXIT_FAILURE;
    if (pid == 0) {
        union {
            const char *const *c;
            char *const *m;
        } u = {argv};
        execvp(argv[0], u.m);
        _exit(127);
    }
    int st = 0;
    if (waitpid(pid, &st, 0) < 0)
        return EXIT_FAILURE;
    return WIFEXITED(st) ? WEXITSTATUS(st) : EXIT_FAILURE;
}

static int is_help_flag(const char *s) {
    return s && (!strcmp(s, "-h") || !strcmp(s, "--help") || !strcmp(s, "help"));
}

static int match_any(const char *cmd, const char *const *list) {
    for (; *list; ++list) {
        if (!strcmp(cmd, *list))
            return 1;
    }
    return 0;
}

static int flowy_build_cmd(int argc, char **argv, int and_run) {
    if (argc < 3 || is_help_flag(argv[2])) {
        printf("Usage: flowy %s [--shared] <spec.lean> [-o <binary|so>] [args...]\n",
               and_run ? "run" : "build");
        return (argc < 3) ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    const char *spec_file = NULL;
    const char *out_bin = NULL;
    int extra_arg_start = argc;
    int is_shared = 0;

    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--shared") || !strcmp(argv[i], "--sdk")) {
            is_shared = 1;
        } else if (!strcmp(argv[i], "-o") && i + 1 < argc) {
            out_bin = argv[++i];
        } else if (!spec_file && argv[i][0] != '-') {
            spec_file = argv[i];
        } else {
            extra_arg_start = i;
            break;
        }
    }

    if (!spec_file) {
        fprintf(stderr, "flowy %s: missing specification file\n", and_run ? "run" : "build");
        return EXIT_FAILURE;
    }

    const char *slash = strrchr(spec_file, '/');
    const char *base = slash ? slash + 1 : spec_file;
    const char *dot = strrchr(base, '.');
    char stem[128] = {0};
    snprintf(stem, sizeof(stem), "%.*s", (int)(dot ? (size_t)(dot - base) : strlen(base)), base);

    char auto_bin[512] = {0};
    char temp_c[512] = {0};

    if (!out_bin) {
        if (is_shared) {
            snprintf(auto_bin, sizeof(auto_bin), "build/lib%s.so", stem);
        } else {
            snprintf(auto_bin, sizeof(auto_bin), "build/%s", stem);
        }
        out_bin = auto_bin;
    }

    (void)mkdir("build", 0755);
    snprintf(temp_c, sizeof(temp_c), "/tmp/flow_XXXXXX.c");
    int tmp_fd = mkstemps(temp_c, 2);
    if (tmp_fd >= 0) {
        close(tmp_fd);
    } else {
        snprintf(temp_c, sizeof(temp_c), "/tmp/flow_%d_%lu.c", (int)getpid(),
                 (unsigned long)time(NULL));
    }

    char sibling_flowc[512] = {0};
    const char *flowc_bin =
        flowy_resolve_flowc_bin(argc > 0 ? argv[0] : NULL, sibling_flowc, sizeof(sibling_flowc));
    char h_path[256] = {0};

    if (is_shared) {
        snprintf(h_path, sizeof(h_path), "build/%s.h", stem);
        const char *fc_sh_argv[] = {flowc_bin,           spec_file, "-o", temp_c,
                                    "--target-c-header", h_path,    NULL};
        if (run_argv_sync(fc_sh_argv) != 0) {
            fprintf(stderr, "flowy build --shared: failed to compile %s\n", spec_file);
            remove(temp_c);
            return EXIT_FAILURE;
        }
    } else {
        const char *fc_argv[] = {flowc_bin, spec_file, "-o", temp_c, NULL};
        if (run_argv_sync(fc_argv) != 0) {
            fprintf(stderr, "flowy %s: failed to compile %s\n", and_run ? "run" : "build",
                    spec_file);
            remove(temp_c);
            return EXIT_FAILURE;
        }
    }

    const char *cc = getenv("CC");
    if (!cc || !cc[0])
        cc = "clang";
    int ret = 0;

    if (is_shared) {
        const char *cc_sh1[] = {
            cc,         "-std=c17", "-O3", "-shared", "-fPIC", "-DFLOW_SHARED_LIB",
            "-pthread", temp_c,     "-o",  out_bin,   "-lm",   NULL};
        ret = run_argv_sync(cc_sh1);
        remove(temp_c);
        if (ret != 0) {
            fprintf(stderr, "flowy build --shared: failed to generate %s\n", out_bin);
            return EXIT_FAILURE;
        }
        printf("  ✓ [FLOW C17 SDK] Built %s + %s\n", out_bin, h_path);
        return EXIT_SUCCESS;
    }

    const char *cc_argv1[] = {cc, "-O3", temp_c, "-o", out_bin, "-lpthread", "-lm", NULL};
    ret = run_argv_sync(cc_argv1);
    if (ret != 0) {
        const char *cc_argv2[] = {cc, "-O3", temp_c, "-o", out_bin, "-lm", NULL};
        ret = run_argv_sync(cc_argv2);
    }
    remove(temp_c);
    if (ret != 0) {
        fprintf(stderr, "flowy %s: failed to generate binary %s\n", and_run ? "run" : "build",
                out_bin);
        return EXIT_FAILURE;
    }
    printf("  ✓ Built %s (standalone native binary)\n", out_bin);

    if (and_run) {
        char exec_path[512];
        snprintf(exec_path, sizeof(exec_path), "%s%s",
                 (out_bin[0] == '/' || (out_bin[0] == '.' && out_bin[1] == '/')) ? "" : "./",
                 out_bin);
        const char *run_argv[128];
        int r_argc = 0;
        run_argv[r_argc++] = exec_path;
        for (int i = extra_arg_start; i < argc && r_argc < 127; ++i) {
            run_argv[r_argc++] = argv[i];
        }
        run_argv[r_argc] = NULL;
        printf("▶ Executing %s:\n", exec_path);
        int run_rc = run_argv_sync(run_argv);
        return (run_rc == 0) ? EXIT_SUCCESS : run_rc;
    }
    return EXIT_SUCCESS;
}

static int flowy_run_jit_direct(const char *spec_file) {
    flow_registry_init();
    FILE *input = fopen(spec_file, "r");
    if (!input) {
        fprintf(stderr, "flowy run: cannot open spec file '%s': %s\n", spec_file, strerror(errno));
        return EXIT_FAILURE;
    }
    static FlowSpec spec;
    static SemanticIR ir;
    memset(&spec, 0, sizeof(spec));
    memset(&ir, 0, sizeof(ir));

    if (!parse_spec(input, &spec)) {
        fprintf(stderr, "flowy run: failed to parse spec %s\n", spec_file);
        fclose(input);
        return EXIT_FAILURE;
    }
    fclose(input);

    lower_to_ir(&spec, &ir);
    if (ir.imported_module_count > 0) {
        for (size_t m = 0; m < ir.imported_module_count; ++m) {
            flow_registry_resolve_plugin(ir.imported_modules[m]);
            const FlowPlugin *mod = flow_registry_lookup(ir.imported_modules[m]);
            if (mod)
                flow_plugin_lower_semantics(&spec, &ir, mod);
        }
    } else {
        const char *mname = spec.plugin_name[0] ? spec.plugin_name : "builtin";
        flow_registry_resolve_plugin(mname);
        const FlowPlugin *mod = flow_registry_lookup(mname);
        if (mod)
            flow_plugin_lower_semantics(&spec, &ir, mod);
    }

    const Component *component = select_component(&ir);
    if (!component) {
        flow_ir_cleanup(&ir);
        return -1;
    }
    VerificationReport verification;
    if (!verify_candidate(&ir, component, NULL, &verification)) {
        fprintf(stderr, "flowy run: formal verification failed: %s\n", verification.message);
        flow_ir_cleanup(&ir);
        return EXIT_FAILURE;
    }
    printf("[JIT Direct Track] Executing %s in-memory...\n",
           ir.flow_name[0] ? ir.flow_name : spec_file);
    int ok = flow_backend_run_in_memory(&ir, component, NULL, &verification, stdout);
    flow_ir_cleanup(&ir);
    return ok ? EXIT_SUCCESS : -1;
}

static int flowy_run_cmd(int argc, char **argv) {
    if (argc < 3 || is_help_flag(argv[2])) {
        printf("Usage: flowy run [--aot] <spec.lean> [args...]\n");
        return (argc < 3) ? EXIT_FAILURE : EXIT_SUCCESS;
    }
    int use_aot = 0;
    const char *spec_file = NULL;
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--aot")) {
            use_aot = 1;
        } else if (argv[i][0] != '-' && !spec_file) {
            spec_file = argv[i];
        }
    }
    if (!spec_file) {
        fprintf(stderr, "flowy run: missing .lean specification\n");
        return EXIT_FAILURE;
    }
    if (use_aot) {
        char *aot_argv[128];
        int aot_argc = 0;
        aot_argv[aot_argc++] = argv[0];
        aot_argv[aot_argc++] = argv[1];
        for (int i = 2; i < argc && aot_argc < 127; ++i) {
            if (strcmp(argv[i], "--aot") != 0) {
                aot_argv[aot_argc++] = argv[i];
            }
        }
        aot_argv[aot_argc] = NULL;
        return flowy_build_cmd(aot_argc, aot_argv, 1);
    }
    int ret = flowy_run_jit_direct(spec_file);
    if (ret == -1) {
        printf("[Fallback] External plugin link required; falling back to AOT...\n");
        return flowy_build_cmd(argc, argv, 1);
    }
    return ret;
}

static void flowy_print_usage(FILE *out) {
    fprintf(
        out,
        "FLOW 2.0 Living System & C17 Toolchain (flowy) v" FLOW_VERSION_STRING
        " (git: " FLOW_GIT_PROVENANCE ")\n"
        "Usage: flowy <command> [options...]\n\n"
        "Primary Commands:\n"
        "  induce / discover [--benchmark|<file.csv>]  Induce Lean 4 invariants, SMM params & PRNG seed audit\n"
        "  patch <base.lean> [csv] [-b 0x...] -o <out> Synthesize & verify white-box .lean patch from dM boundary & data\n"
        "  topos / hott                                Inspect 64-Bit Cubical HoTT Sieve & Type Universe U_0\n"
        "  audit                                       Run HoTT U_0 univalence & C17 ABI soundness audit\n"
        "  build [--shared] <spec.lean>                Build standalone AOT binary or C17 shared library (--shared)\n"
        "  run [--aot] <spec.lean> [args...]           Run instantly via In-Memory JIT or standalone AOT\n"
        "  inspect <why|timeline|audit|topos>          Introspection and decision explanation\n"
        "  why                                         Explain real-time scheduling / hardware decision\n"
        "  timeline                                    Show real-time decision event timeline\n"
        "  lang [en|zh]                                Show or switch active display language\n"
        "  -v, --version                               Show version information\n"
        "  -h, --help                                  Show this help message\n");
}

static int flowy_build_entry(int argc, char **argv) {
    return flowy_build_cmd(argc, argv, 0);
}

static int flowy_induce_entry(int argc, char **argv) {
    return cmd_induce_run(argc - 2, argv + 2);
}

static int flowy_patch_entry(int argc, char **argv) {
    flow_registry_init();
    return cmd_patch_run(argc - 2, argv + 2);
}

static int flowy_inspect_entry(int argc, char **argv) {
    flow_registry_init();
    if (argc < 3 || is_help_flag(argv[2])) {
        cmd_inspect_print_usage(stdout);
        return EXIT_SUCCESS;
    }
    return cmd_inspect_run(argc - 2, argv + 2);
}

static int flowy_inspect_alias_entry(int argc, char **argv) {
    flow_registry_init();
    return cmd_inspect_run(argc - 1, argv + 1);
}

static int flowy_lang_entry(int argc, char **argv) {
    if (argc >= 3) {
        FlowLanguage nl = flowy_parse_language(argv[2]);
        flowy_set_language(nl);
        printf("FLOW language switched to: %s\n", flowy_language_name(nl));
    } else {
        printf("Active FLOW language: %s\n", flowy_language_name(flowy_get_language()));
    }
    return EXIT_SUCCESS;
}

typedef struct {
    const char *const *names;
    int (*handler)(int argc, char **argv);
} FlowCliCommand;

int main(int argc, char **argv) {
    if (argc < 2 || is_help_flag(argv[1])) {
        flowy_print_usage(stdout);
        return EXIT_SUCCESS;
    }

    if (!strcmp(argv[1], "-v") || !strcmp(argv[1], "--version") || !strcmp(argv[1], "version")) {
        printf("FLOW System Framework (flowy) v" FLOW_VERSION_STRING " (git: " FLOW_GIT_PROVENANCE
               ") | Cubical HoTT & Topos U_0\n");
        return EXIT_SUCCESS;
    }

    flowy_set_language(flowy_detect_system_language());
    for (int i = 1; i < argc; ++i) {
        if ((!strcmp(argv[i], "--lang") || !strcmp(argv[i], "-l")) && i + 1 < argc) {
            flowy_set_language(flowy_parse_language(argv[i + 1]));
            for (int j = i; j + 2 < argc; ++j) {
                argv[j] = argv[j + 2];
            }
            argc -= 2;
            i--;
        }
    }

    static const char *const run_names[] = {"run", NULL};
    static const char *const build_names[] = {"build", "compile", NULL};
    static const char *const induce_names[] = {"induce", "--induce", "discover", "--discover",
                                               NULL};
    static const char *const patch_names[] = {"patch", "--patch", NULL};
    static const char *const inspect_names[] = {"inspect", NULL};
    static const char *const alias_names[] = {
        "why",      "--why",      "boundary",          "--boundary",
        "timeline", "--timeline", "explain-decisions", "audit",
        "--audit",  "topos",      "--topos",           "hott",
        "--hott",   "cubical",    "--cubical",         NULL};
    static const char *const lang_names[] = {"lang", "language", NULL};

    static const FlowCliCommand FLOW_CLI_COMMANDS[] = {
        {run_names, flowy_run_cmd},           {build_names, flowy_build_entry},
        {induce_names, flowy_induce_entry},   {patch_names, flowy_patch_entry},
        {inspect_names, flowy_inspect_entry}, {alias_names, flowy_inspect_alias_entry},
        {lang_names, flowy_lang_entry}};

    for (size_t ci = 0; ci < sizeof(FLOW_CLI_COMMANDS) / sizeof(FLOW_CLI_COMMANDS[0]); ++ci) {
        if (match_any(argv[1], FLOW_CLI_COMMANDS[ci].names)) {
            return FLOW_CLI_COMMANDS[ci].handler(argc, argv);
        }
    }

    fprintf(stderr, "Unknown command: %s\n\n", argv[1]);
    flowy_print_usage(stderr);
    return EXIT_FAILURE;
}
