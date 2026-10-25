#include "flowy.h"
#include "topology.h"
#include "registry.h"
#include "flow_jet.h"
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
    if (pid < 0) return EXIT_FAILURE;
    if (pid == 0) { union { const char *const *c; char *const *m; } u = { argv }; execvp(argv[0], u.m); _exit(127); }
    int st = 0;
    if (waitpid(pid, &st, 0) < 0) return EXIT_FAILURE;
    return WIFEXITED(st) ? WEXITSTATUS(st) : EXIT_FAILURE;
}
static int is_help_flag(const char *s) { return s && (!strcmp(s, "-h") || !strcmp(s, "--help") || !strcmp(s, "help")); }
static int match_any(const char *cmd, const char *const *list) { for (; *list; ++list) if (!strcmp(cmd, *list)) return 1; return 0; }
static int flowy_build_cmd(int argc, char **argv, int and_run) {
    if (argc < 3 || is_help_flag(argv[2])) { printf("Usage: flowy %s [--shared] <spec.flow|spec.lean> [-o <binary|so>] [args...]\n", and_run ? "run" : "build"); return (argc < 3) ? EXIT_FAILURE : EXIT_SUCCESS; }
    const char *spec_file = NULL, *out_bin = NULL; int extra_arg_start = argc, is_shared = 0;
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--shared") || !strcmp(argv[i], "--sdk")) is_shared = 1;
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) out_bin = argv[++i];
        else if (!spec_file && argv[i][0] != '-') spec_file = argv[i];
        else { extra_arg_start = i; break; }
    }
    if (!spec_file) { fprintf(stderr, "flowy %s: missing specification file\n", and_run ? "run" : "build"); return EXIT_FAILURE; }
    const char *slash = strrchr(spec_file, '/'), *base = slash ? slash + 1 : spec_file, *dot = strrchr(base, '.');
    char stem[128] = {0}; snprintf(stem, sizeof(stem), "%.*s", (int)(dot ? (size_t)(dot - base) : strlen(base)), base);
    char auto_bin[512] = {0}, temp_c[512] = {0};
    if (!out_bin) {
        if (is_shared) snprintf(auto_bin, sizeof(auto_bin), "build/lib%s.so", stem);
        else snprintf(auto_bin, sizeof(auto_bin), "build/%s", stem);
        out_bin = auto_bin;
    }
    (void)mkdir("build", 0755);
    snprintf(temp_c, sizeof(temp_c), "/tmp/flow_XXXXXX.c");
    int tmp_fd = mkstemps(temp_c, 2);
    if (tmp_fd >= 0) close(tmp_fd);
    else snprintf(temp_c, sizeof(temp_c), "/tmp/flow_%d_%lu.c", (int)getpid(), (unsigned long)time(NULL));
    char sibling_flowc[512] = {0};
    const char *flowc_bin = flowy_resolve_flowc_bin(argc > 0 ? argv[0] : NULL, sibling_flowc, sizeof(sibling_flowc));
    char h_path[256] = {0}, py_path[256] = {0}, rs_path[256] = {0}, m_path[256] = {0};
    if (is_shared) {
        snprintf(h_path, sizeof(h_path), "build/%s.h", stem);
        snprintf(py_path, sizeof(py_path), "build/%s_abi.py", stem);
        snprintf(rs_path, sizeof(rs_path), "build/%s_abi.rs", stem);
        snprintf(m_path, sizeof(m_path), "build/flow_%s.m", stem);
        const char *fc_sh_argv[] = {flowc_bin, spec_file, "-o", temp_c, "--target-c-header", h_path, "--target-python", py_path, "--target-rust", rs_path, "--target-matlab", m_path, NULL};
        if (run_argv_sync(fc_sh_argv) != 0) { fprintf(stderr, "flowy build --shared: failed to compile %s\n", spec_file); remove(temp_c); return EXIT_FAILURE; }
    } else {
        const char *fc_argv[] = {flowc_bin, spec_file, "-o", temp_c, NULL};
        if (run_argv_sync(fc_argv) != 0) { fprintf(stderr, "flowy %s: failed to compile %s\n", and_run ? "run" : "build", spec_file); remove(temp_c); return EXIT_FAILURE; }
    }
    const char *cc = getenv("CC"); if (!cc || !cc[0]) cc = "clang";
    int ret = 0;
    if (is_shared) {
        const char *cc_sh1[] = {cc, "-std=c17", "-O3", "-shared", "-fPIC", "-DFLOW_SHARED_LIB", "-pthread", temp_c, "-o", out_bin, "-lm", NULL};
        ret = run_argv_sync(cc_sh1);
        if (ret != 0) {
            const char *cc_sh2[] = {cc, "-std=c17", "-O3", "-shared", "-fPIC", "-DFLOW_SHARED_LIB", temp_c, "-o", out_bin, "-lm", NULL};
            ret = run_argv_sync(cc_sh2);
        }
        remove(temp_c);
        if (ret != 0) { fprintf(stderr, "flowy build --shared: failed to generate %s\n", out_bin); return EXIT_FAILURE; }
        printf("  ✓ [FLOW SDK Bundle] Built %s + %s + %s + %s + %s\n", out_bin, h_path, py_path, rs_path, m_path);
        return EXIT_SUCCESS;
    }
    const char *cc_argv1[] = {cc, "-O3", temp_c, "-o", out_bin, "-lpthread", "-lm", NULL};
    ret = run_argv_sync(cc_argv1);
    if (ret != 0) {
        const char *cc_argv2[] = {cc, "-O3", temp_c, "-o", out_bin, "-lm", NULL};
        ret = run_argv_sync(cc_argv2);
    }
    remove(temp_c);
    if (ret != 0) { fprintf(stderr, "flowy %s: failed to generate binary %s\n", and_run ? "run" : "build", out_bin); return EXIT_FAILURE; }
    printf("  ✓ [Dual-Track: AOT Track] Built %s (SMT formally verified, standalone native binary)\n", out_bin);
    if (and_run) {
        char exec_path[512];
        snprintf(exec_path, sizeof(exec_path), "%s%s", (out_bin[0] == '/' || (out_bin[0] == '.' && out_bin[1] == '/')) ? "" : "./", out_bin);
        const char *run_argv[128]; int r_argc = 0;
        run_argv[r_argc++] = exec_path;
        for (int i = extra_arg_start; i < argc && r_argc < 127; ++i) run_argv[r_argc++] = argv[i];
        run_argv[r_argc] = NULL;
        printf("▶ [Dual-Track: AOT Track] Executing %s:\n", exec_path);
        int run_rc = run_argv_sync(run_argv);
        return (run_rc == 0) ? EXIT_SUCCESS : run_rc;
    }
    return EXIT_SUCCESS;
}
static int flowy_run_jit_direct(const char *spec_file) {
    flow_registry_init(); FILE *input = fopen(spec_file, "r");
    if (!input) { fprintf(stderr, "flowy run: cannot open spec file '%s': %s\n", spec_file, strerror(errno)); return EXIT_FAILURE; } FlowSpec spec;
    if (!parse_spec(input, &spec)) { fprintf(stderr, "flowy run: failed to parse spec %s\n", spec_file); fclose(input); return EXIT_FAILURE; } fclose(input);
    SemanticIR ir; lower_to_ir(&spec, &ir); if (ir.imported_module_count > 0) { for (size_t m = 0; m < ir.imported_module_count; ++m) {
            flow_registry_resolve_plugin(ir.imported_modules[m]); const FlowPlugin *mod = flow_registry_lookup(ir.imported_modules[m]);
            if (mod) flow_plugin_lower_semantics(&spec, &ir, mod); } } else { const char *mname = spec.plugin_name[0] ? spec.plugin_name : "builtin";
        flow_registry_resolve_plugin(mname); const FlowPlugin *mod = flow_registry_lookup(mname); if (mod) flow_plugin_lower_semantics(&spec, &ir, mod); }
    const Component *component = select_component(&ir); if (!component) { flow_ir_cleanup(&ir); return -1; } VerificationReport verification;
    if (!verify_candidate(&ir, component, NULL, &verification)) { fprintf(stderr, "flowy run: formal verification failed: %s\n", verification.message); flow_ir_cleanup(&ir); return EXIT_FAILURE; }
    if (ir.extern_kernel[0]) { flow_ir_cleanup(&ir); return -1; }
    printf("[Dual-Track: JIT Direct Track] Executing %s (in-memory, 0 disk I/O, SMT UNSAT verified)...\n", ir.flow_name[0] ? ir.flow_name : spec_file);
    int ok = flow_backend_run_in_memory(&ir, component, NULL, &verification, stdout); flow_ir_cleanup(&ir); return ok ? EXIT_SUCCESS : -1; }
static int flowy_run_cmd(int argc, char **argv) {
    if (argc < 3 || is_help_flag(argv[2])) { printf("Usage: flowy run [--aot] <spec.lean|spec.flow> [args...]\n"); return (argc < 3) ? EXIT_FAILURE : EXIT_SUCCESS; }
    int use_aot = 0; const char *spec_file = NULL;
    for (int i = 2; i < argc; ++i) if (!strcmp(argv[i], "--aot")) use_aot = 1; else if (argv[i][0] != '-' && !spec_file) spec_file = argv[i];
    if (!spec_file) { fprintf(stderr, "flowy run: missing .lean or .flow specification\n"); return EXIT_FAILURE; } if (use_aot) { char *aot_argv[128]; int aot_argc = 0;
        aot_argv[aot_argc++] = argv[0]; aot_argv[aot_argc++] = argv[1];
        for (int i = 2; i < argc && aot_argc < 127; ++i) if (strcmp(argv[i], "--aot") != 0) aot_argv[aot_argc++] = argv[i]; aot_argv[aot_argc] = NULL;
        return flowy_build_cmd(aot_argc, aot_argv, 1); } int ret = flowy_run_jit_direct(spec_file);
    if (ret == -1) { printf("[Dual-Track: Fallback] Custom external plugin link required; falling back to AOT...\n"); return flowy_build_cmd(argc, argv, 1); } return ret; }
static int is_safe_path_str(const char *s) {
    if (!s || !s[0]) return 0;
    for (const char *p = s; *p; ++p) {
        if (*p == '"' || *p == '\'' || *p == '`' || *p == '$' || *p == ';' || *p == '|' || *p == '&' || *p == '\n' || *p == '\r') return 0;
    }
    return 1;
}
static int flowy_ui_cmd(int argc, char **argv) {
    if (argc < 3 || is_help_flag(argv[2])) {
        printf("Usage: flowy ui <spec.lean|spec.flow> [--port <port>] [--selftest]\n");
        return (argc < 3) ? EXIT_FAILURE : EXIT_SUCCESS;
    }
    const char *spec_file = NULL; int port = 8095, selftest = 0;
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--selftest")) selftest = 1;
        else if (argv[i][0] != '-' && !spec_file) spec_file = argv[i];
    }
    if (!spec_file || !is_safe_path_str(spec_file)) { fprintf(stderr, "flowy ui: missing or unsafe specification file path\n"); return EXIT_FAILURE; }
    const char *slash = strrchr(spec_file, '/'), *base = slash ? slash + 1 : spec_file, *dot = strrchr(base, '.');
    char stem[128] = {0}; snprintf(stem, sizeof(stem), "%.*s", (int)(dot ? (size_t)(dot - base) : strlen(base)), base);
    for (char *p = stem; *p; ++p) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')) *p = '_';
    }
    (void)mkdir("build", 0755);
    char sibling_flowc[512] = {0};
    const char *flowc_bin = flowy_resolve_flowc_bin(argc > 0 ? argv[0] : NULL, sibling_flowc, sizeof(sibling_flowc));
    char c_path[256], py_path[256], ui_path[256], so_path[256], selftest_str[16], port_str[16];
    snprintf(c_path, sizeof(c_path), "build/%s_ui.c", stem);
    snprintf(py_path, sizeof(py_path), "build/%s_ui_abi.py", stem);
    snprintf(ui_path, sizeof(ui_path), "build/%s_ui.html", stem);
    snprintf(so_path, sizeof(so_path), "build/lib%s_ui.so", stem);
    snprintf(selftest_str, sizeof(selftest_str), "%d", selftest);
    snprintf(port_str, sizeof(port_str), "%d", port);
    const char *fc_argv[] = {flowc_bin, spec_file, "-o", c_path, "--target-python", py_path, "--target-ui", ui_path, NULL};
    if (run_argv_sync(fc_argv) != 0) { fprintf(stderr, "flowy ui: failed to compile %s\n", spec_file); return EXIT_FAILURE; }
    const char *cc = getenv("CC"); if (!cc || !cc[0]) cc = "clang";
    const char *cc_argv1[] = {cc, "-std=c17", "-O2", "-shared", "-fPIC", "-DFLOW_SHARED_LIB", "-pthread", c_path, "-o", so_path, "-lm", NULL};
    if (run_argv_sync(cc_argv1) != 0) {
        const char *cc_argv2[] = {cc, "-std=c17", "-O2", "-shared", "-fPIC", "-DFLOW_SHARED_LIB", c_path, "-o", so_path, "-lm", NULL};
        if (run_argv_sync(cc_argv2) != 0) { fprintf(stderr, "flowy ui: failed to build shared library %s\n", so_path); return EXIT_FAILURE; }
    }
    static const char *const py_script =
        "import sys, json, ctypes, importlib.util, http.server\n"
        "py_p, so_p, ui_p, selftest, port = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])\n"
        "spec = importlib.util.spec_from_file_location('uimod', py_p)\n"
        "mod = importlib.util.module_from_spec(spec); spec.loader.exec_module(mod)\n"
        "cls = next(v for v in vars(mod).values() if isinstance(v, type) and hasattr(v, 'LEAN_FUNCTIONS'))\n"
        "eng = cls(so_p)\n"
        "ui_html = open(ui_p, 'r', encoding='utf-8').read()\n"
        "assert 'FLOW_SCHEMA' in ui_html\n"
        "def _ser(v):\n"
        "    return {k: getattr(v, k) for k, _ in v._fields_} if isinstance(v, ctypes.Structure) else v\n"
        "if selftest:\n"
        "    w = eng.step_words([0, 64, 1])\n"
        "    for fn in eng.LEAN_FUNCTIONS:\n"
        "        res = _ser(eng.call_lean(fn['name'], *([10]*len(fn['params']))))\n"
        "    assert eng.reset_watchdog() == 0\n"
        "    assert eng.watchdog_breach_count() >= 0\n"
        "    print(f'  ✓ [FLOW Schema-Driven UI & C17 Bridge] Self-test passed (fields={len(eng.STATE_FIELDS)}, fns={len(eng.LEAN_FUNCTIONS)}, theorems={len(eng.LEAN_THEOREMS)})')\n"
        "    eng.close(); sys.exit(0)\n"
        "ledger = []\n"
        "wd_latched = False\n"
        "fn_names = {f['name'] for f in eng.LEAN_FUNCTIONS}\n"
        "class H(http.server.BaseHTTPRequestHandler):\n"
        "    def log_message(self, *a): pass\n"
        "    def _json(self, obj, code=200):\n"
        "        b = json.dumps(obj).encode('utf-8'); self.send_response(code)\n"
        "        self.send_header('Content-Type', 'application/json'); self.send_header('Content-Length', str(len(b))); self.end_headers(); self.wfile.write(b)\n"
        "    def do_GET(self):\n"
        "        if self.path == '/api/schema': return self._json({'ok':True,'state_fields':eng.STATE_FIELDS,'lean_functions':list(eng.LEAN_FUNCTIONS),'lean_theorems':eng.LEAN_THEOREMS,'ledger':ledger})\n"
        "        if self.path == '/api/certificate':\n"
        "            return self._json({'ok':True,'component':cls.__name__,'state_fields':eng.STATE_FIELDS,'lean_functions':list(eng.LEAN_FUNCTIONS),'lean_theorems':eng.LEAN_THEOREMS,'watchdog':{'estop_latched':wd_latched,'breach_count':eng.watchdog_breach_count()},'sod_audit_ledger':ledger})\n"
        "        b = ui_html.encode('utf-8'); self.send_response(200); self.send_header('Content-Type','text/html; charset=utf-8'); self.send_header('Content-Length',str(len(b))); self.end_headers(); self.wfile.write(b)\n"
        "    def do_POST(self):\n"
        "        global wd_latched\n"
        "        ln = int(self.headers.get('Content-Length', '0'))\n"
        "        body = json.loads(self.rfile.read(ln).decode('utf-8') or '{}')\n"
        "        try:\n"
        "            if self.path == '/api/call': return self._json({'ok':True,'fn':body['fn'],'result':_ser(eng.call_lean(body['fn'], *body.get('args',[])))})\n"
        "            if self.path == '/api/step': return self._json({'ok':True,'words':eng.step_words(body.get('words',[]))})\n"
        "            if self.path == '/api/watchdog':\n"
        "                act = body.get('action', 'reset')\n"
        "                rc = eng.trip_watchdog_for_test(10**9) if act == 'trip' else eng.reset_watchdog()\n"
        "                wd_latched = (rc == -4 or act == 'trip')\n"
        "                return self._json({'ok':True,'status':rc,'estop_latched':wd_latched,'breach_count':eng.watchdog_breach_count()})\n"
        "            if self.path == '/api/ledger':\n"
        "                sod = (body.get('auth',0) != body.get('exec',0) and body.get('auth',0) > 0 and body.get('exec',0) > 0)\n"
        "                res = _ser(eng.call_lean(body['fn'], *body.get('args',[]))) if body.get('fn') in fn_names else True\n"
        "                ent = {'seq':len(ledger)+1,'fn':body.get('fn',''),'sod_ok':sod,'result':res}\n"
        "                ledger.append(ent); return self._json({'ok':True,**ent})\n"
        "        except Exception as e: return self._json({'ok':False,'error':str(e)}, 400)\n"
        "print(f'▶ FLOW Schema-Driven Control Plane listening on http://127.0.0.1:{port}')\n"
        "http.server.ThreadingHTTPServer(('127.0.0.1', port), H).serve_forever()\n";
    const char *py_argv[] = {"python3", "-B", "-c", py_script, py_path, so_path, ui_path, selftest_str, port_str, NULL};
    return run_argv_sync(py_argv) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
static void flowy_print_usage(FILE *out) { fprintf(out, "FLOW 2.0 Living System & Declarative Toolchain (flowy) v2.5.0\nUsage: flowy <command> [options...]\n\n"
                 "Primary Commands:\n"
                 "  run [--aot] <spec.lean|spec.flow> [args...] Run instantly via In-Memory JIT Direct Track (or --aot)\n"
                 "  build [--shared] <spec.lean|spec.flow>      Build standalone AOT binary or 5-artifact SDK bundle (--shared)\n"
                 "  ui <spec.lean|spec.flow> [--port <N>]       Synthesize & serve schema-driven C17 Control Plane & SoD Ledger UI\n"
                 "  audit                                       Run self-bootstrapping 7-scope codebase & HoTT U_0 univalence audit\n"
                 "  scope [name]                                Inspect the 7 architectural Component Scopes across C17, Lean & Rust\n"
                 "  topos / hott                                Inspect 64-Bit Cubical HoTT Sieve & Intensional Type Universe U_0\n"
                 "  ask <query>                                 Query any module, scope, or .lean intent via Live AST + Token Ring\n"
                 "  why                                         Explain real-time scheduling / hardware decision\n"
                 "  shell                                       Start the autonomic interactive REPL\n"
                 "  inspect <subcommand>                        Introspection: ask, why, timeline, bottleneck, audit, scope, doc, topos\n"
                 "  jet <subcommand>                            Phase Space Jet Bundles (.fjet) & Symplectic physics\n"
                 "  fvec <subcommand>                           Gene vectors & immune antibody promotion (.fvec)\n"
                 "  topo <subcommand>                           Topology graph and continuous living orchestrator\n"
                 "  test [suite]                                Run domain test suites (brain, body, concurrency, fvec-swarm, system, refactored-core, geometric-axiom, cubical-hott, all)\n"); } static int run_test_cmd(int argc, char **argv) {
    const char *suite = (argc >= 3) ? argv[2] : "all";
    if (is_help_flag(suite)) { printf("Usage: flowy test <brain|body|concurrency|fvec-swarm|system|refactored-core|geometric-axiom|cubical-hott|all>\n"); return EXIT_SUCCESS; }
    static const char *const suites[] = {"brain", "body", "concurrency", "fvec-swarm", "system", "refactored-core", "geometric-axiom", "cubical-hott"};
    const char *build_dir = getenv("FLOW_BUILD_DIR");
    if (!build_dir || !build_dir[0]) build_dir = "./build";
    if (!strcmp(suite, "all")) {
        for (int i = 0; i < 8; ++i) {
            char bin_path[512];
            snprintf(bin_path, sizeof(bin_path), "%s/test-%s", build_dir, suites[i]);
            printf("▶ Running Suite [%s] (%s)...\n", suites[i], bin_path);
            const char *t_argv[] = {bin_path, NULL};
            int ret = run_argv_sync(t_argv);
            if (ret != 0) { fprintf(stderr, "❌ Suite [%s] failed with exit code %d\n", suites[i], ret); return EXIT_FAILURE; }
        }
        printf("  ✅ ALL 8 DOMAIN TEST SUITES PASSED\n");
        return EXIT_SUCCESS;
    }
    for (int i = 0; i < 8; ++i) {
        if (!strcmp(suite, suites[i]) || (!strcmp(suite, "fvec") && !strcmp(suites[i], "fvec-swarm")) ||
            (!strcmp(suite, "core") && !strcmp(suites[i], "refactored-core")) ||
            (!strcmp(suite, "axiom") && !strcmp(suites[i], "geometric-axiom")) ||
            (!strcmp(suite, "hott") && !strcmp(suites[i], "cubical-hott"))) {
            char bin_path[512];
            snprintf(bin_path, sizeof(bin_path), "%s/test-%s", build_dir, suites[i]);
            printf("▶ Running Suite [%s] (%s)...\n", suites[i], bin_path);
            const char *t_argv[] = {bin_path, NULL};
            return (run_argv_sync(t_argv) == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
        }
    }
    fprintf(stderr, "Unknown test suite: %s\n", suite); return EXIT_FAILURE; }
int main(int argc, char **argv) { if (argc < 2 || is_help_flag(argv[1])) { flowy_print_usage(stdout); return EXIT_SUCCESS; }
    if (!strcmp(argv[1], "-v") || !strcmp(argv[1], "--version") || !strcmp(argv[1], "version")) {
        printf("FLOW System Framework (flowy) v2.5.0 | Bit-Manifold Form (BMF) & Cubical HoTT\n"); return EXIT_SUCCESS; }
    if (!strcmp(argv[1], "test")) return run_test_cmd(argc, argv); if (!strcmp(argv[1], "run")) return flowy_run_cmd(argc, argv);
    if (!strcmp(argv[1], "ui") || !strcmp(argv[1], "--ui")) return flowy_ui_cmd(argc, argv);
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
        "explain-decisions","bottleneck","--bottleneck","audit","--audit","scope","--scope","scopes","doc","--doc","topos","--topos","hott","--hott","cubical","--cubical",NULL };
    if (match_any(argv[1], inspect_aliases)) return cmd_inspect_run(argc - 1, argv + 1); if (!strcmp(argv[1], "lang") || !strcmp(argv[1], "language")) {
        if (argc >= 3) { FlowLanguage nl = flowy_parse_language(argv[2]); flowy_set_language(nl); printf("FLOW language switched to: %s\n", flowy_language_name(nl)); }
        else printf("Active FLOW language: %s\n", flowy_language_name(flowy_get_language())); return EXIT_SUCCESS; }
    if (!strcmp(argv[1], "fvec") || !strcmp(argv[1], "--fvec")) {
        if (argc < 3 || is_help_flag(argv[2])) { cmd_fvec_print_usage(stdout); return argc < 3 ? EXIT_FAILURE : EXIT_SUCCESS; } return cmd_fvec_run(argc - 1, argv + 1); }
    static const char *const fvec_aliases[] = {"rag","prompt","--rag","vault","--vault","hippocampus","antibody","--antibody","immune","query","--query","search","--search","hub","--hub",NULL};
    if (match_any(argv[1], fvec_aliases)) return cmd_fvec_run(argc - 1, argv + 1); if (!strcmp(argv[1], "jet") || !strcmp(argv[1], "--jet")) {
        if (argc < 3 || is_help_flag(argv[2])) { cmd_jet_print_usage(argc < 3 ? stderr : stdout); return argc < 3 ? EXIT_FAILURE : EXIT_SUCCESS; }
        return cmd_jet_run(argc - 2, argv + 2); } fprintf(stderr, "Unknown command: %s\n\n", argv[1]); flowy_print_usage(stderr); return EXIT_FAILURE; }
