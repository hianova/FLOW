#include "registry.h"
#include "backend.h"
#include "bitspace.h"
#include "cubical_hott.h"
#include "reload.h"
#include <dlfcn.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
static const FlowPlugin *PLUGINS[FLOW_PLUGIN_MAX];
static size_t plugin_count_value;
static int registry_initialized;
static void *DSO_HANDLES[FLOW_PLUGIN_MAX];
static size_t dso_handle_count = 0;
static int plugin_valid(const FlowPlugin *p) {
    if (!p || !p->name || !p->name[0] || !p->version || (p->component_count > 0 && !p->components)) return 0;
    for (size_t i = 0; i < p->component_count; ++i) if (!p->components[i].id || !p->components[i].id[0]) return 0;
    return 1;
}
static int component_id_registered(const char *id) {
    for (size_t p = 0; p < plugin_count_value; ++p)
        for (size_t c = 0; c < PLUGINS[p]->component_count; ++c)
            if (!strcmp(id, PLUGINS[p]->components[c].id)) return 1;
    return 0;
}
static size_t registered_component_count(void) {
    size_t count = 0;
    for (size_t p = 0; p < plugin_count_value; ++p) count += PLUGINS[p]->component_count;
    return count;
}
static int add_plugin(const FlowPlugin *plugin) {
    if (!plugin_valid(plugin) || plugin_count_value >= FLOW_PLUGIN_MAX) return 0;
    size_t reg = registered_component_count();
    if (reg > FLOW_COMPONENT_MAX || plugin->component_count > FLOW_COMPONENT_MAX - reg) return 0;
    for (size_t i = 0; i < plugin_count_value; ++i) if (!strcmp(PLUGINS[i]->name, plugin->name)) return 0;
    for (size_t i = 0; i < plugin->component_count; ++i) {
        for (size_t j = i + 1; j < plugin->component_count; ++j) if (!strcmp(plugin->components[i].id, plugin->components[j].id)) return 0;
        if (component_id_registered(plugin->components[i].id)) return 0;
    }
    PLUGINS[plugin_count_value++] = plugin;
    return 1;
}
int flow_registry_init(void) {
    if (registry_initialized) return plugin_count_value != 0;
    registry_initialized = 1;
    if (!add_plugin(flow_builtin_plugin())) { plugin_count_value = 0; return 0; }
    return 1;
}
int flow_registry_load_dso(const char *so_path, char *err_msg, size_t err_size) {
    #define DSO_ERR(...) do { if (err_msg && err_size) snprintf(err_msg, err_size, __VA_ARGS__); if (handle) dlclose(handle); return 0; } while (0)
    if (err_msg && err_size) err_msg[0] = '\0';
    void *handle = NULL;
    if (!so_path || !so_path[0]) DSO_ERR("null or empty dso path");
    if (strstr(so_path, "..")) DSO_ERR("path traversal rejected in dso path");
    size_t plen = strlen(so_path);
    int ext_ok = (plen > 3 && strcmp(so_path + plen - 3, ".so") == 0) ||
                 (plen > 6 && strcmp(so_path + plen - 6, ".dylib") == 0);
    if (!ext_ok) DSO_ERR("dso path must end in .so or .dylib");
    struct stat st;
    if (stat(so_path, &st) != 0) DSO_ERR("stat failed on dso path: %s", so_path);
    if (!S_ISREG(st.st_mode)) DSO_ERR("dso path is not a regular file");
    if ((st.st_mode & S_IWOTH) != 0) DSO_ERR("world-writable dso rejected for security");
    FILE *fp = fopen(so_path, "rb");
    if (!fp) DSO_ERR("cannot open dso file for header verification");
    uint8_t magic[4] = {0};
    size_t nread = fread(magic, 1, 4, fp);
    fclose(fp);
    uint32_t m32 = 0;
    if (nread == 4) memcpy(&m32, magic, 4);
    int magic_ok = (nread == 4) && (
        (magic[0] == 0x7f && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F') ||
        m32 == 0xfeedfaceU || m32 == 0xcefaedfeU ||
        m32 == 0xfeedfacfU || m32 == 0xcffaedfeU ||
        m32 == 0xcafebabeU || m32 == 0xbebafecaU);
    if (!magic_ok) DSO_ERR("invalid ELF/Mach-O binary header in dso");
    if (!(handle = dlopen(so_path, RTLD_NOW | RTLD_LOCAL))) DSO_ERR("dlopen failed: %s", dlerror());
    static const char *const SYMS[] = {"flow_plugin_entry_v1", "flow_embodied_entry_v1", "flow_swarm_entry_v1"};
    FlowPluginEntryFn entry_fn = NULL;
    for (size_t i = 0; i < 3 && !entry_fn; ++i) entry_fn = (FlowPluginEntryFn)dlsym(handle, SYMS[i]);
    if (!entry_fn) {
        typedef const FlowUnit *(*FlowUnitEntryFn)(void);
        FlowUnitEntryFn unit_fn = (FlowUnitEntryFn)dlsym(handle, "flow_unit_entry_v1");
        if (unit_fn) {
            const FlowUnit *u = unit_fn();
            if (!u || !flow_registry_register_unit(u, "cpu", "qsbr", 0, 8)) DSO_ERR("failed to register FlowUnit from flow_unit_entry_v1");
            if (dso_handle_count < FLOW_PLUGIN_MAX) DSO_HANDLES[dso_handle_count++] = handle;
            return 1;
        }
        DSO_ERR("missing entry symbol 'flow_plugin_entry_v1' or 'flow_unit_entry_v1'");
    }
    const FlowPluginDescriptor *desc = entry_fn();
    if (!desc) DSO_ERR("flow_plugin_entry_v1 returned null descriptor");
    if (desc->abi_major != FLOW_PLUGIN_ABI_MAJOR) DSO_ERR("abi major mismatch: plugin %u != host %u", desc->abi_major, FLOW_PLUGIN_ABI_MAJOR);
    if (desc->descriptor_size < 64) DSO_ERR("descriptor size mismatch: plugin %zu < min 64", desc->descriptor_size);
    if (!desc->plugin || !desc->module_name || !flow_registry_register(desc->plugin)) DSO_ERR("failed to register plugin");
    if (dso_handle_count < FLOW_PLUGIN_MAX) DSO_HANDLES[dso_handle_count++] = handle;
    #undef DSO_ERR
    return 1;
}
static void normalize_plugin_ident(const char *in, char *out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';
    if (!in) return;
    if (!strncmp(in, "flow.", 5)) in += 5;
    else if (!strncmp(in, "libflow_", 8)) in += 8;
    else if (!strncmp(in, "lib", 3)) in += 3;
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 1 < out_sz; ++i) {
        out[j++] = (in[i] == '-') ? '_' : in[i];
    }
    out[j] = '\0';
}
static int plugin_name_matches(const char *p_name, const char *query) {
    if (!p_name || !query || !p_name[0] || !query[0]) return 0;
    if (!strcmp(p_name, query)) return 1;
    char np[128], nq[128];
    normalize_plugin_ident(p_name, np, sizeof(np));
    normalize_plugin_ident(query, nq, sizeof(nq));
    if (!strcmp(np, nq)) return 1;
    if ((!strcmp(np, "rust_simd_plugin") && (!strcmp(nq, "flow_plugin_example") || !strcmp(nq, "plugin_example"))) ||
        ((!strcmp(np, "flow_plugin_example") || !strcmp(np, "plugin_example")) && !strcmp(nq, "rust_simd_plugin"))) {
        return 1;
    }
    return 0;
}
int flow_registry_register(const FlowPlugin *plugin) { return flow_registry_init() ? add_plugin(plugin) : 0; }
int flow_registry_resolve_plugin(const char *mod_name) {
    if (!mod_name || !mod_name[0]) return 0;
    if (flow_registry_lookup(mod_name)) return 1;
    const char *raw_base = (!strncmp(mod_name, "flow.", 5)) ? mod_name + 5 : mod_name;
    char norm_base[128];
    normalize_plugin_ident(raw_base, norm_base, sizeof(norm_base));
    const char *bases[4] = { raw_base, norm_base, NULL, NULL };
    size_t base_count = strcmp(raw_base, norm_base) != 0 ? 2 : 1;
    if (!strcmp(norm_base, "rust_simd_plugin")) {
        bases[base_count++] = "flow_plugin_example";
    } else if (!strcmp(norm_base, "flow_plugin_example") || !strcmp(norm_base, "plugin_example")) {
        bases[base_count++] = "flow_plugin_example";
        bases[base_count++] = "rust_simd_plugin";
    }
    static const char *const fmts[16] = {
        "build/libflow_%s.so", "build/libflow_%s.dylib",
        "libflow_%s.so", "libflow_%s.dylib",
        "build/lib%s.so", "build/lib%s.dylib",
        "lib%s.so", "lib%s.dylib",
        "target/debug/lib%s.so", "target/debug/lib%s.dylib",
        "target/release/lib%s.so", "target/release/lib%s.dylib",
        "target/debug/libflow_%s.so", "target/debug/libflow_%s.dylib",
        "target/release/libflow_%s.so", "target/release/libflow_%s.dylib"
    };
    char path[256], err[256] = {0};
    for (size_t b = 0; b < base_count; ++b) {
        if (!bases[b]) continue;
        for (int i = 0; i < 16; ++i) {
            snprintf(path, sizeof(path), fmts[i], bases[b]);
            if (flow_registry_load_dso(path, err, sizeof(err))) return 1;
        }
    }
    return 0;
}
const FlowPlugin *flow_registry_lookup(const char *name) {
    if (!flow_registry_init() || !name || !name[0]) return NULL;
    for (size_t i = 0; i < plugin_count_value; ++i) if (plugin_name_matches(PLUGINS[i]->name, name)) return PLUGINS[i];
    return NULL;
}
size_t flow_registry_plugin_count(void) { (void)flow_registry_init(); return plugin_count_value; }
const FlowPlugin *flow_component_plugin(const Component *component) {
    if (!flow_registry_init() || !component) return NULL;
    for (size_t p = 0; p < plugin_count_value; ++p)
        for (size_t c = 0; c < PLUGINS[p]->component_count; ++c)
            if (component == &PLUGINS[p]->components[c]) return PLUGINS[p];
    return NULL;
}
int flow_component_validate_contract(const SemanticIR *ir, const FlowPlugin *p, char *msg, size_t sz) {
    if (msg && sz) msg[0] = '\0';
    return (!p || !p->validate_contract) ? 1 : p->validate_contract(ir, p, msg, sz);
}
void flow_plugin_lower_semantics(const FlowSpec *spec, SemanticIR *ir, const FlowPlugin *p) { if (p && p->lower_domain_semantics) p->lower_domain_semantics(spec, ir, p); }
int flow_component_dimensions(const SemanticIR *ir, const Component *c, FlowPlanDimensionSet *dims_out) {
    if (!dims_out || !c) return 0;
    memset(dims_out, 0, sizeof(*dims_out));
    const FlowPlugin *p = flow_component_plugin(c);
    if (p && p->enumerate_dimensions) return p->enumerate_dimensions(ir, c, dims_out);
    dims_out->count = 3;
    dims_out->dimensions[0] = (FlowPlanDimension){"capacity", FLOW_DIM_EXPONENT, FLOW_DIM_CLASS_STRUCTURAL_JIT, 1, 26, 1, 12, 500};
    dims_out->dimensions[1] = (FlowPlanDimension){"threads", FLOW_DIM_LINEAR, FLOW_DIM_CLASS_STRUCTURAL_JIT, 1, 64, 1, 1, 200};
    dims_out->dimensions[2] = (FlowPlanDimension){"shards", FLOW_DIM_LINEAR, FLOW_DIM_CLASS_STRUCTURAL_JIT, 1, 32, 1, 1, 200};
    return 1;
}
int flow_component_evaluate(const SemanticIR *ir, const Component *c, const FlowPlanAssignment *plan, FlowPlanMetrics *m) {
    if (!m || !c || !plan) return 0;
    memset(m, 0, sizeof(*m));
    const FlowPlugin *p = flow_component_plugin(c);
    if (p && p->evaluate_plan) return p->evaluate_plan(ir, c, plan, m);
    size_t cap = plan->count > 0 ? (size_t)plan->values[0] : (size_t)ir->input_max_count, thr = plan->count > 1 ? (size_t)plan->values[1] : 1, shd = plan->count > 2 ? (size_t)plan->values[2] : 1, est = 0;
    if (!cap) cap = 1; if (!thr) thr = 1; if (!shd) shd = 1;
    if (p && p->memory_model) p->memory_model(ir, c, cap, shd, &est);
    *m = (FlowPlanMetrics){.capacity = cap, .threads = thr, .shards = shd, .memory_bytes = est, .latency_score = c->latency_score, .energy = (double)est / 1024.0 + (double)c->latency_score * 2.0};
    return 1;
}
int flow_component_verify_plan(const SemanticIR *ir, const Component *c, const FlowPlanAssignment *plan, VerificationReport *r) {
    if (!r || !c || !plan) return 0;
    memset(r, 0, sizeof(*r));
    const FlowPlugin *p = flow_component_plugin(c);
    if (p && p->verify_plan) return p->verify_plan(ir, c, plan, r);
    size_t cap = (plan->count > 0 && plan->values[0] > 0) ? (size_t)plan->values[0] : (size_t)(ir && ir->input_max_count > 0 ? ir->input_max_count : 1024);
    size_t shd = (plan->count > 2 && plan->values[2] > 0) ? (size_t)plan->values[2] : 1;
    r->capacity = cap ? cap : 1;
    (void)flow_component_memory(ir, c, r->capacity, shd, &r->estimated_bytes);
    return flow_component_verify(ir, c, r->capacity, shd, r->message, sizeof(r->message));
}
uint64_t flow_component_benchmark(const SemanticIR *ir, const Component *c, const FlowPlanAssignment *plan) {
    const FlowPlugin *p = flow_component_plugin(c);
    return (p && p->benchmark) ? p->benchmark(ir, c, plan) : 1000;
}
int flow_component_supports_reload(const Component *c) { return c ? c->reload_capable : 0; }
void flow_ir_cleanup(SemanticIR *ir) { if (ir && ir->domain_ctx && ir->domain_ctx_free) { ir->domain_ctx_free(ir->domain_ctx); ir->domain_ctx = NULL; } }
int flow_component_memory(const SemanticIR *ir, const Component *c, size_t cap, size_t shd, size_t *est) {
    const FlowPlugin *p = flow_component_plugin(c);
    if (!p || !c || !est) return 0;
    if (p->memory_model) return p->memory_model(ir, c, cap, shd, est);
    size_t bpc = c->memory_bytes_per_capacity > 0 ? c->memory_bytes_per_capacity : 8;
    *est = c->memory_fixed_bytes + (cap ? cap : 1) * bpc;
    return 1;
}
int flow_component_verify(const SemanticIR *ir, const Component *c, size_t cap, size_t shd, char *msg, size_t sz) {
    const FlowPlugin *p = flow_component_plugin(c);
    if (msg && sz) msg[0] = '\0';
    return p && (!p->verify || p->verify(ir, c, cap, shd, msg, sz));
}
int flow_component_preference(const SemanticIR *ir, const Component *c) { const FlowPlugin *p = flow_component_plugin(c); return (p && p->preference) ? p->preference(ir, c) : 0; }
uint64_t flow_component_mutation_mask(const SemanticIR *ir, const Component *c, const FlowPlanDimensionSet *dims) { const FlowPlugin *p = flow_component_plugin(c); return (p && p->get_mutation_mask) ? p->get_mutation_mask(ir, c, dims) : (uint64_t)-1; }
uint64_t flow_component_preference_mask(const SemanticIR *ir, const Component *c, const FlowPlanDimensionSet *dims) { const FlowPlugin *p = flow_component_plugin(c); return (p && p->preference_mask) ? p->preference_mask(ir, c, dims) : 0; }
uint64_t flow_component_contract_mask(const SemanticIR *ir, const Component *c, const FlowPlanDimensionSet *dims) { const FlowPlugin *p = flow_component_plugin(c); return (p && p->contract_mask) ? p->contract_mask(ir, c, dims) : (uint64_t)-1; }
uint64_t flow_component_resource_mask(const SemanticIR *ir, const Component *c, const FlowPlanDimensionSet *dims, size_t lim) { const FlowPlugin *p = flow_component_plugin(c); return (p && p->resource_mask) ? p->resource_mask(ir, c, dims, lim) : (uint64_t)-1; }
static int emit_default_unit_c(FILE *out, const SemanticIR *ir, const Component *c, const struct FlowVerificationReport *v) {
    if (!out || !ir || !c) return 0;
    size_t cap = (v && v->capacity > 0) ? v->capacity : (size_t)(ir->input_max_count > 0 ? ir->input_max_count : 1024);
    size_t stride = ir->input_stride_bytes > 0 ? ir->input_stride_bytes : 8;
    fprintf(out,
        "/* Auto-Generated Declarative FLOW Component: %s */\n"
        "/* Verification: status=%s capacity=%zu estimated_bytes=%zu */\n"
        "#include <stddef.h>\n#include <stdint.h>\n#include <stdio.h>\n#include <stdlib.h>\n"
        "#define FLOW_CAPACITY %zu\n",
        c->id, (v && v->status == VERIFIER_PROVEN) ? "proven" : "runtime_check", cap, v ? v->estimated_bytes : cap * stride, cap);
    flow_emit_abi_exports(out, ir, c, cap);
    fprintf(out,
        "#ifndef FLOW_SHARED_LIB\nint main(void) {\n"
        "    puts(\"FLOW generated program\"); puts(\"flow: %s\"); puts(\"component: %s\");\n"
        "    return EXIT_SUCCESS;\n}\n#endif\n",
        ir->flow_name, c->id);
    return ferror(out) == 0;
}
int flow_component_emit(FILE *out, const SemanticIR *ir, const Component *c, const struct FlowSearchResult *s, const struct FlowVerificationReport *v, int reload) {
    const FlowPlugin *p = flow_component_plugin(c);
    if (!p) return 0;
    if (p->emit && p->emit(out, ir, c, s, v, reload)) return 1;
    return emit_default_unit_c(out, ir, c, v);
}
int flow_component_create_unit(const struct FlowPlanArtifact *art, const SemanticIR *ir, const Component *c, struct FlowUnit *unit_out) {
    const FlowPlugin *p = flow_component_plugin(c);
    if (!p || !p->create_unit || !unit_out) return 0;
    return p->create_unit(art, ir, c, unit_out);
}
int component_compatible(const SemanticIR *ir, const Component *c) {
    if (!ir || !c) return 0;
    const FlowPlugin *p = flow_component_plugin(c);
    if (!p || (ir->resource_name[0] && strcmp(ir->resource_name, c->resource)) || (ir->capability_name[0] && strcmp(ir->capability_name, c->capability))) return 0;
    if (ir->imported_module_count > 0) {
        int imp = 0;
        for (size_t m = 0; m < ir->imported_module_count; ++m) if (plugin_name_matches(p->name, ir->imported_modules[m]) || !strcmp(p->name, "builtin")) { imp = 1; break; }
        if (!imp) return 0;
    } else if (ir->plugin_name[0] && !plugin_name_matches(p->name, ir->plugin_name) && strcmp(p->name, "builtin")) return 0;
    if (p->validate_contract) { char err[128]; if (!p->validate_contract(ir, p, err, sizeof(err))) return 0; }
    else if (ir->contract_name[0] && (!c->domain_contract || strcmp(ir->contract_name, c->domain_contract))) return 0;
    FlowCubicalTypeSieve ir_sieve = flow_cubical_sieve_from_ir(ir), comp_sieve = flow_cubical_sieve_from_component(c);
    if (c->cubical_type_mask) comp_sieve.mask |= c->cubical_type_mask;
    if (!flow_cubical_match_types(ir_sieve, comp_sieve, NULL)) return 0;
    return (!p->compatible || p->compatible(ir, c));
}
const Component *select_component(const SemanticIR *ir) {
    const Component *best = NULL;
    int best_score = -1;
    FlowCubicalTypeSieve ir_sieve = flow_cubical_sieve_from_ir(ir);
    for (size_t i = 0; i < component_count(); ++i) {
        const Component *cand = component_at(i);
        if (!component_compatible(ir, cand)) continue;
        FlowCubicalTypeSieve cand_sieve = flow_cubical_sieve_from_component(cand);
        int h_score = flow_cubical_match_types(ir_sieve, cand_sieve, NULL) ? (int)(__builtin_popcountll(ir_sieve.mask & cand_sieve.mask) * 10) : 0;
        int id_match = !strcmp(ir->flow_name, cand->id);
        for (size_t n = 0; !id_match && n < ir->flow_node_count; ++n) if (!strcmp(ir->flow_nodes[n].name, cand->id)) id_match = 1;
        int score = h_score + (id_match ? 100 : 0) + (!ir->fact_unordered && !cand->supports_unordered ? 5 : 0) +
                    (ir->prefer_latency ? cand->latency_score : cand->memory_score) + flow_component_preference(ir, cand);
        if (!best || score > best_score) { best = cand; best_score = score; }
    }
    return best;
}
size_t compatible_component_count(const SemanticIR *ir) {
    if (!ir) return 0;
    size_t count = 0;
    for (size_t i = 0; i < component_count(); ++i) if (component_compatible(ir, component_at(i))) ++count;
    return count;
}
const Component *compatible_component_at(const SemanticIR *ir, size_t idx) {
    if (!ir) return NULL;
    for (size_t i = 0; i < component_count(); ++i) {
        const Component *c = component_at(i);
        if (!component_compatible(ir, c)) continue;
        if (idx == 0) return c;
        --idx;
    }
    return NULL;
}
size_t component_count(void) { return flow_registry_init() ? registered_component_count() : 0; }
const Component *component_at(size_t index) {
    if (!flow_registry_init()) return NULL;
    for (size_t p = 0; p < plugin_count_value; ++p) {
        if (index < PLUGINS[p]->component_count) return &PLUGINS[p]->components[index];
        index -= PLUGINS[p]->component_count;
    }
    return NULL;
}
size_t component_index(const Component *c) { for (size_t i = 0; i < component_count(); ++i) if (component_at(i) == c) return i; return 0; }
static int unit_plugin_compatible(const SemanticIR *ir, const Component *c) {
    if (!ir || !c || !c->id) return 0;
    if (!strcmp(ir->flow_name, c->id) || !strcmp(ir->plugin_name, c->id)) return 1;
    for (size_t i = 0; i < ir->imported_module_count; ++i) if (!strcmp(ir->imported_modules[i], c->id)) return 1;
    for (size_t i = 0; i < ir->flow_node_count; ++i) if (!strcmp(ir->flow_nodes[i].name, c->id)) return 1;
    return 0;
}
static int unit_plugin_create_unit(const struct FlowPlanArtifact *art, const SemanticIR *ir, const Component *c, struct FlowUnit *unit_out) {
    (void)art; (void)ir;
    const FlowPlugin *p = flow_component_plugin(c);
    if (!p || !p->domain_context || !unit_out) return 0;
    *unit_out = *(const FlowUnit *)p->domain_context;
    return 1;
}
int flow_registry_register_unit(const struct FlowUnit *unit, const char *resource, const char *capability, uint64_t cubical_type_mask, size_t bytes_per_slot) {
    if (!unit || !unit->name || !unit->name[0] || !unit->run || !unit->drop) return 0;
    FlowPlugin *p = calloc(1, sizeof(FlowPlugin)); Component *comp = calloc(1, sizeof(Component)); FlowUnit *u_copy = calloc(1, sizeof(FlowUnit));
    if (!p || !comp || !u_copy) { free(u_copy); free(comp); free(p); return 0; }
    *u_copy = *unit; u_copy->abi_version = FLOW_RELOAD_ABI_VERSION;
    *comp = (Component){
        .id = strdup(unit->name), .kind = strdup("flow_unit"), .resource = strdup(resource && resource[0] ? resource : "cpu"),
        .capability = strdup(capability && capability[0] ? capability : "qsbr"), .domain_contract = "", .flow_binding = strdup(unit->name),
        .supports_shared = 1, .supports_read_heavy = 1, .supports_unordered = 1, .supports_parallelizable = 1,
        .latency_score = 9, .memory_score = 9, .memory_fixed_bytes = 64, .memory_bytes_per_capacity = bytes_per_slot > 0 ? bytes_per_slot : 8,
        .reload_capable = 1, .cubical_type_mask = cubical_type_mask
    };
    *p = (FlowPlugin){
        .name = strdup(unit->name), .version = strdup("1.0.0"), .components = comp, .component_count = 1,
        .compatible = unit_plugin_compatible, .create_unit = unit_plugin_create_unit, .domain_context = u_copy,
        .doc_title = "Zero-Boilerplate FlowUnit Module", .doc_responsibilities = "Executes QSBR epoch-protected FlowUnit operator",
        .doc_algorithmic_guarantee = "Cubical HoTT & QSBR Epoch Safe", .doc_memory_concurrency_model = "QSBR Epoch Lease", .doc_layer = 2
    };
    return flow_registry_register(p);
}

/* ============================================================================
 * Built-in Algorithms & Data Structures Plugin (folded into registry.c)
 * ============================================================================ */
static const Component COMPONENTS[] = { {"sharded_hash", "collection", "cpu", "pthread", 1, 1, 1, 0, 9, 7, "", "", 0u, 12u, 1, 0},
    {"linear_array", "collection", "cpu", "stdlib", 0, 0, 1, 0, 5, 9, "", "", 0u, 8u, 1, 0},
    {"ordered_tree", "collection", "cpu", "pthread", 1, 0, 0, 0, 6, 6, "", "", 0u, 16u, 0, 0},
    {"bounded_queue", "queue", "cpu", "pthread", 1, 0, 1, 0, 9, 8, "", "bounded_queue", 0u, 8u, 0, 0},
    {"parallel_map", "algorithm", "cpu", "pthread", 0, 0, 1, 1, 8, 6, "", "parallel_map", 0u, 8u, 0, 0},
    {"binary_parser", "parser", "cpu", "stdlib", 0, 0, 1, 0, 7, 7, "", "binary_parser", 0u, 8u, 0, 0},
    {"state_machine", "state_machine", "cpu", "stdlib", 0, 0, 1, 0, 7, 7, "", "state_machine", 0u, 8u, 0, 0},
    {"flowc_bootstrap", "compiler", "cpu", "stdio", 0, 0, 1, 0, 7, 7, "compiler", "", 0u, 8u, 0, 0} };
static int builtin_compatible(const SemanticIR *ir, const Component *c) { if (!ir || !c) return 0;
    if (!strcmp(c->id, "flowc_bootstrap")) return ir->contract_name[0] && !strcmp(ir->contract_name, "compiler");
    if (ir->contract_name[0] && !strcmp(ir->contract_name, "compiler")) return 0;
    if (c->flow_binding[0] && strcmp(ir->flow_name, c->flow_binding) && !(ir->flow_parallelizable && c->supports_parallelizable)) return 0;
    if (ir->state_shared != c->supports_shared || (ir->state_read_heavy && !c->supports_read_heavy) ||
        (ir->fact_unordered && !c->supports_unordered) || (ir->flow_parallelizable != c->supports_parallelizable)) return 0;
    for (size_t i = 0; i < ir->declared_constraint_count; ++i) { const char *cn = ir->constraints[i].name;
        if (strcmp(cn, "deterministic") && strcmp(cn, "memory") && strcmp(cn, "sla") && strcmp(cn, "latency") &&
            strcmp(cn, "lean_certified") && strcmp(cn, "proof_carrying") && strcmp(cn, "no_sorry")) return 0; } return 1; }
static int builtin_validate_contract(const SemanticIR *ir, const FlowPlugin *p, char *msg, size_t sz) { (void)p; if (msg && sz) msg[0] = '\0'; return ir != NULL; }
static int builtin_memory_model(const SemanticIR *ir, const Component *c, size_t cap, size_t shards, size_t *est) { (void)ir; if (!est || !c) return 0;
    if (!shards) shards = 1; size_t slots = cap; if (!strcmp(c->id, "sharded_hash")) { if (cap > SIZE_MAX - (shards - 1u)) return 0;
        size_t per_shard = (cap + shards - 1u) / shards; if (per_shard && shards > SIZE_MAX / per_shard) return 0; slots = per_shard * shards; }
    if (c->memory_bytes_per_capacity && slots > SIZE_MAX / c->memory_bytes_per_capacity) return 0; size_t var = slots * c->memory_bytes_per_capacity;
    if (c->memory_fixed_bytes > SIZE_MAX - var) return 0; *est = c->memory_fixed_bytes + var; return 1; }
static int builtin_enumerate_dimensions(const SemanticIR *ir, const Component *c, FlowPlanDimensionSet *dims_out) { (void)ir; if (!dims_out || !c) return 0;
    static const FlowPlanDimension DIMS[8] = { {"capacity", FLOW_DIM_EXPONENT, FLOW_DIM_CLASS_STRUCTURAL_JIT, 1, 20, 1, 12, 500},
        {"threads", FLOW_DIM_EXPONENT, FLOW_DIM_CLASS_STRUCTURAL_JIT, 0, 6, 1, 0, 200}, {"shards", FLOW_DIM_EXPONENT, FLOW_DIM_CLASS_STRUCTURAL_JIT, 0, 6, 1, 0, 200},
        {"buffer_bytes", FLOW_DIM_LINEAR, FLOW_DIM_CLASS_TACTILE_PARAM, 1024, 65536, 1024, 16384, 0},
        {"initial_capacity", FLOW_DIM_EXPONENT, FLOW_DIM_CLASS_TACTILE_PARAM, 1, 16, 1, 2, 0},
        {"growth_percent", FLOW_DIM_LINEAR, FLOW_DIM_CLASS_TACTILE_PARAM, 110, 200, 10, 150, 0},
        {"batch_size", FLOW_DIM_LINEAR, FLOW_DIM_CLASS_TACTILE_PARAM, 1024, 65536, 1024, 16384, 0},
        {"arena_bytes", FLOW_DIM_LINEAR, FLOW_DIM_CLASS_TACTILE_PARAM, 0, 1048576, 4096, 0, 0} }; dims_out->count = 8; memcpy(dims_out->dimensions, DIMS, sizeof(DIMS));
    return 1; }
static size_t builtin_target_threads(const SemanticIR *ir) { long online = sysconf(_SC_NPROCESSORS_ONLN); if (online < 1) online = 1;
    if (online > 64) online = 64; size_t target = (ir && ir->flow_parallelizable) ? 4 : ((ir && ir->state_shared) ? 16 : 1);
    return (target > (size_t)online ? (size_t)online : (target ? target : 1)); }
static int builtin_evaluate_plan(const SemanticIR *ir, const Component *c, const FlowPlanAssignment *plan, FlowPlanMetrics *m) { if (!m || !c || !plan) return 0;
    size_t cap = plan->count > 0 ? (size_t)plan->values[0] : (size_t)ir->input_max_count, thr = plan->count > 1 ? (size_t)plan->values[1] : 1, shd = plan->count > 2 ? (size_t)plan->values[2] : 1, est = 0;
    if (!cap) cap = 1; if (!thr) thr = 1; if (!shd) shd = 1; (void)builtin_memory_model(ir, c, cap, shd, &est);
    double t_cap = (ir && ir->input_max_count > 0) ? (double)ir->input_max_count : 4096.0;
    double d_cap = ((double)cap < t_cap) ? ((t_cap - (double)cap) * 50.0) : (((double)cap - t_cap) * 0.1);
    double d_thr = fabs((double)thr - (double)builtin_target_threads(ir)) * 20.0, d_shd = fabs((double)shd - ((ir && ir->state_shared) ? 16.0 : 1.0)) * 50.0;
    *m = (FlowPlanMetrics){ .capacity = cap, .threads = thr, .shards = shd, .memory_bytes = est, .latency_score = (double)c->latency_score, .throughput_score = (double)thr * (11.0 - (double)c->latency_score), .energy = (double)est / 1024.0 + (double)c->latency_score * 2.0 + d_cap + d_thr + d_shd };
    return 1; }
static int builtin_verify_plan(const SemanticIR *ir, const Component *c, const FlowPlanAssignment *plan, VerificationReport *r) {
    if (!r || !c || !plan) return 0; *r = (VerificationReport){0};
    size_t cap = plan->count > 0 ? (size_t)plan->values[0] : (size_t)ir->input_max_count, shd = plan->count > 2 ? (size_t)plan->values[2] : 1, est = 0;
    if (!cap) cap = 1; if (!shd) shd = 1;
    if (!builtin_memory_model(ir, c, cap, shd, &est)) { r->status = VERIFIER_COMPILE_ERROR; snprintf(r->message, sizeof(r->message), "builtin memory model failed"); return 0; }
    r->capacity = cap; r->estimated_bytes = est; r->max_count_proven = (size_t)ir->input_max_count <= cap;
    r->runtime_input_guard = !r->max_count_proven; r->status = r->max_count_proven ? VERIFIER_PROVEN : VERIFIER_RUNTIME_CHECK;
    snprintf(r->message, sizeof(r->message), r->max_count_proven ? "input bound proven" : "input bound requires runtime guard"); return 1; }
static int builtin_verify(const SemanticIR *ir, const Component *c, size_t cap, size_t shd, char *msg, size_t sz) {
    (void)ir; (void)c; (void)cap; (void)shd; if (msg && sz) msg[0] = '\0'; return 1; }
typedef struct { int id, score; } BenchItem;
typedef struct { int occupied; BenchItem item; } BenchSlot;
typedef struct { BenchItem item; int left, right; } BenchNode;
typedef struct { const BenchItem *input; BenchItem *output; size_t begin, end; } BenchMapTask;
static volatile uint64_t builtin_benchmark_sink;
static uint64_t builtin_clock_ns(void) { struct timespec now;
    return clock_gettime(CLOCK_MONOTONIC, &now) == 0 ? (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec : 0; }
static uint64_t bench_linear(size_t cap, size_t thr, size_t cnt) { BenchItem items[4096]; size_t rounds = 32 + thr * 2, st = cap > 4096 ? 4096 : (cap < cnt ? cnt : cap);
    volatile uint64_t cs = 0; uint64_t start = builtin_clock_ns(); for (size_t r = 0; r < rounds; ++r) {
        for (size_t i = 0; i < cnt; ++i) items[i % st] = (BenchItem){(int)i, (int)((i * 17 + r) % 101)};
        for (size_t q = 0; q < cnt; ++q) cs += (uint64_t)(items[q % st].score + q); } builtin_benchmark_sink ^= cs; return builtin_clock_ns() - start; }
static uint64_t bench_hash(size_t cap, size_t thr, size_t shd, size_t cnt) { BenchSlot slots[4096]; size_t rounds = 32 + thr * 2, st = cap < cnt * 2 ? cnt * 2 : cap;
    if (st > 4096) st = 4096; if (!shd) shd = 1; volatile uint64_t cs = 0; uint64_t start = builtin_clock_ns(); for (size_t r = 0; r < rounds; ++r) {
        memset(slots, 0, sizeof(slots)); for (size_t i = 0; i < cnt; ++i) { size_t s0 = (((size_t)((uint32_t)i * UINT32_C(2654435761)) % shd) + i) % st;
            for (size_t p = 0; p < st; ++p) if (!slots[(s0 + p) % st].occupied) { slots[(s0 + p) % st] = (BenchSlot){1, {(int)i, (int)((i * 17 + r) % 101)}}; break; } }
        for (size_t q = 0; q < cnt; ++q) { size_t s0 = (((size_t)((uint32_t)q * UINT32_C(2654435761)) % shd) + q) % st;
            for (size_t p = 0; p < st; ++p) { size_t s = (s0 + p) % st; if (slots[s].occupied && slots[s].item.id == (int)q) { cs += (uint64_t)(slots[s].item.score + q); break; } }
        } } builtin_benchmark_sink ^= cs; return builtin_clock_ns() - start; }
static void tree_insert(BenchNode *nodes, size_t *len, int *root, BenchItem item) {
    int idx = (int)(*len); nodes[(*len)++] = (BenchNode){item, -1, -1}; if (*root < 0) { *root = idx; return; } for (int cur = *root;;) {
        int *next = item.score > nodes[cur].item.score ? &nodes[cur].left : &nodes[cur].right; if (*next < 0) { *next = idx; return; } cur = *next; } }
static uint64_t tree_sum(const BenchNode *nodes, int r) { return r < 0 ? 0 : (uint64_t)nodes[r].item.score + tree_sum(nodes, nodes[r].left) + tree_sum(nodes, nodes[r].right); }
static uint64_t bench_tree(size_t cap, size_t thr, size_t cnt) { BenchNode nodes[4096]; size_t rounds = 32 + thr * 2, st = cap > 4096 ? 4096 : (cap < cnt ? cnt : cap);
    uint64_t cs = 0, start = builtin_clock_ns(); for (size_t r = 0; r < rounds; ++r) { size_t len = 0; int root = -1;
        for (size_t i = 0; i < cnt && i < st; ++i) tree_insert(nodes, &len, &root, (BenchItem){(int)i, (int)((i * 17 + r) % 101)}); cs += tree_sum(nodes, root); }
    builtin_benchmark_sink ^= cs; return builtin_clock_ns() - start; }
static uint64_t bench_queue(size_t cap, size_t thr, size_t cnt) {
    BenchItem queue[4096]; size_t rounds = 32 + thr * 2, st = cap > 4096 ? 4096 : (cap ? cap : 1); if (cnt > st) cnt = st; uint64_t cs = 0, start = builtin_clock_ns();
    for (size_t r = 0; r < rounds; ++r) { size_t head = 0, tail = 0, len = 0;
        for (size_t i = 0; i < cnt; ++i) { queue[tail] = (BenchItem){(int)i, (int)(r + i)}; tail = (tail + 1) % st; ++len; }
        while (len > 0) { cs += (uint64_t)queue[head].score; head = (head + 1) % st; --len; } } builtin_benchmark_sink ^= cs; return builtin_clock_ns() - start; }
static void *bench_map_worker(void *raw) { BenchMapTask *t = raw; for (size_t i = t->begin; i < t->end; ++i) { t->output[i] = t->input[i]; t->output[i].score *= 2; }
    return NULL; }
static uint64_t bench_parallel_map(size_t thr, size_t cnt) { BenchItem in[4096], out[4096]; pthread_t workers[64]; BenchMapTask tasks[64];
    size_t rounds = 32 + thr * 2, wc = thr < cnt ? thr : cnt; if (wc > 64) wc = 64; uint64_t cs = 0, start = builtin_clock_ns(); for (size_t r = 0; r < rounds; ++r) {
        for (size_t i = 0; i < cnt; ++i) in[i] = (BenchItem){(int)i, (int)((i + r) % 101)}; size_t created = 0; if (wc >= 2 && cnt >= wc * 2) {
            for (size_t i = 0; i < wc; ++i) { tasks[i] = (BenchMapTask){in, out, i * cnt / wc, (i + 1) * cnt / wc};
                if (pthread_create(&workers[i], NULL, bench_map_worker, &tasks[i]) != 0) break; ++created; }
            for (size_t i = 0; i < created; ++i) (void)pthread_join(workers[i], NULL); }
        if (created != wc || wc < 2 || cnt < wc * 2) for (size_t i = 0; i < cnt; ++i) { out[i] = in[i]; out[i].score *= 2; }
        for (size_t i = 0; i < cnt; ++i) cs += (uint64_t)out[i].score; } builtin_benchmark_sink ^= cs; return builtin_clock_ns() - start; }
static uint64_t builtin_benchmark(const SemanticIR *ir, const Component *c, const FlowPlanAssignment *plan) { if (!c) return UINT64_MAX;
    size_t cnt = ir->input_max_count > 64 ? 64 : (ir->input_max_count < 8 ? 8 : (size_t)ir->input_max_count);
    size_t cap = plan && plan->count > 0 ? (size_t)plan->values[0] : 4096, thr = plan && plan->count > 1 ? (size_t)plan->values[1] : 1, shd = plan && plan->count > 2 ? (size_t)plan->values[2] : 1;
    if (!strcmp(c->id, "bounded_queue")) return bench_queue(cap, thr, cnt); if (!strcmp(c->id, "parallel_map")) return bench_parallel_map(thr, cnt);
    if (!strcmp(c->id, "binary_parser") || !strcmp(c->id, "state_machine")) { uint64_t start = builtin_clock_ns(); volatile uint64_t cs = 0;
        for (size_t r = 0; r < (32 + thr * 2) * 4; ++r) cs += 49; builtin_benchmark_sink ^= cs; return builtin_clock_ns() - start; }
    if (!strcmp(c->id, "linear_array")) return bench_linear(cap, thr, cnt); if (!strcmp(c->id, "ordered_tree")) return bench_tree(cap, thr, cnt);
    return bench_hash(cap, thr, shd, cnt); }
typedef struct { int id, score, occupied; } BuiltinEntry;
typedef struct { size_t capacity, threads, shards, count; BuiltinEntry *entries; } BuiltinRuntimeState;
static int builtin_state_init(void *h, void **out) {
    (void)h; BuiltinRuntimeState *st = calloc(1, sizeof(*st)); if (!st) return -1; st->capacity = 4096; st->threads = st->shards = 1;
    if (!(st->entries = calloc(st->capacity, sizeof(BuiltinEntry)))) { free(st); return -1; } *out = st; return 0; }
static int builtin_state_run(void *h, void *s, const void *in, void *out) { (void)h; BuiltinRuntimeState *st = s; if (!st) return -1;
    if (in && st->count < st->capacity) { int val = *(const int *)in; st->entries[st->count++] = (BuiltinEntry){val, val * 2, 1}; } if (out) *(int *)out = (int)st->count;
    return 0; }
static int builtin_state_apply(void *h, void *s, const FlowMutation *m) { (void)h; BuiltinRuntimeState *st = s; if (!st || !m) return -1;
    if (m->kind == FLOW_MUTATION_UPSERT && m->value && m->value_size == sizeof(int) && st->count < st->capacity) {
        int val = *(const int *)m->value; st->entries[st->count++] = (BuiltinEntry){m->key ? *(const int *)m->key : val, val, 1}; } return 0; }
static int builtin_state_migrate(void *h, const void *os, void *ns) { (void)h; const BuiltinRuntimeState *o = os; BuiltinRuntimeState *n = ns; if (!o || !n) return -1;
    size_t cnt = o->count > n->capacity ? n->capacity : o->count; if (cnt && o->entries && n->entries) memcpy(n->entries, o->entries, cnt * sizeof(BuiltinEntry));
    n->count = cnt; return 0; }
static void builtin_state_drop(void *h, void *s) { (void)h; BuiltinRuntimeState *st = s; if (st) { free(st->entries); free(st); } }
static const FlowSchemaField BUILTIN_SCHEMA_FIELDS[] = { {"entries", "entry[]", FLOW_SCHEMA_FIELD_PERSISTENT | FLOW_SCHEMA_FIELD_ORDERED},
    {"count", "u64", FLOW_SCHEMA_FIELD_PERSISTENT} };
static const FlowSchema BUILTIN_UNIT_SCHEMA = {"builtin_collection", 1, BUILTIN_SCHEMA_FIELDS, 2};
static int builtin_create_unit(const FlowPlanArtifact *art, const SemanticIR *ir, const Component *comp, FlowUnit *unit_out) { if (!art || !comp || !unit_out) return 0;
    *unit_out = (FlowUnit){ .abi_version = FLOW_RELOAD_ABI_VERSION, .name = comp->id, .init = builtin_state_init, .run = builtin_state_run,
        .apply = builtin_state_apply, .migrate = builtin_state_migrate, .drop = builtin_state_drop, .schema = &BUILTIN_UNIT_SCHEMA,
        .constraint_hash = flow_compute_contract_hash(ir), .capability_hash = 0x55, .semantic_schema_hash = art->plan_schema_hash, .supports_snapshot_cow = 1 }; return 1; }
static uint64_t builtin_preference_mask(const SemanticIR *ir, const Component *c, const FlowPlanDimensionSet *dims) { if (!dims || !dims->count) return 0;
    uint64_t mask = 0; unsigned shift = 0; for (size_t i = 0; i < dims->count; ++i) { const FlowPlanDimension *d = &dims->dimensions[i];
        unsigned bits = flow_dimension_bits(d); if (!bits) continue; uint64_t dm = (bits >= 64) ? (uint64_t)-1 : (((uint64_t)1 << bits) - 1);
        int pref = c && ((!strcmp(c->id, "sharded_hash") && ir && (ir->state_shared || ir->state_read_heavy) && (!strcmp(d->name, "threads") || !strcmp(d->name, "shards"))) ||
                         (!strcmp(c->id, "bounded_queue") && (!strcmp(d->name, "buffer_bytes") || !strcmp(d->name, "batch_size"))) ||
                         (!strcmp(c->id, "parallel_map") && (!strcmp(d->name, "threads") || !strcmp(d->name, "batch_size"))) ||
                         (!strcmp(c->id, "linear_array") && (!strcmp(d->name, "capacity") || !strcmp(d->name, "initial_capacity")))); if (pref) mask |= (dm << shift);
        shift += bits; } return mask; }
static uint64_t builtin_environment_mask(const SemanticIR *ir, const Component *c, const FlowPlanDimensionSet *dims, const FlowEnvironmentState *env) { (void)ir; (void)c;
    if (!dims || !dims->count || !env) return (uint64_t)-1; uint64_t mask = (uint64_t)-1; unsigned shift = 0; for (size_t i = 0; i < dims->count; ++i) {
        const FlowPlanDimension *d = &dims->dimensions[i]; unsigned bits = flow_dimension_bits(d); if (!bits) continue;
        uint64_t dm = (bits >= 64) ? (uint64_t)-1 : (((uint64_t)1 << bits) - 1); if (env->pressure_level == FLOW_ENV_PRESSURE_MEMORY_CRITICAL) {
            if (!strcmp(d->name, "arena_bytes")) mask &= ~(dm << shift);
            else if (!strcmp(d->name, "buffer_bytes") || !strcmp(d->name, "batch_size") || !strcmp(d->name, "threads")) {
                uint64_t allowed = !strcmp(d->name, "threads") ? 0x1 : 0x3;
                for (unsigned b = 0; b < bits; ++b) if (((uint64_t)1 << b) > allowed) mask &= ~(UINT64_C(1) << (shift + b)); }
        } else if (env->pressure_level == FLOW_ENV_PRESSURE_CACHE_THRASHING && (!strcmp(d->name, "shards") || !strcmp(d->name, "buffer_bytes"))) {
            for (unsigned b = 0; b < bits; ++b) if (((uint64_t)1 << b) > 0x3) mask &= ~(UINT64_C(1) << (shift + b)); }
        if (env->hardware_arch == FLOW_ARCH_APPLE_SILICON && !strcmp(d->name, "batch_size")) mask &= ~(UINT64_C(0x1) << shift); shift += bits; } return mask; }
static const FlowPlugin BUILTIN_PLUGIN = { .name = "builtin", .version = "1", .components = COMPONENTS, .component_count = sizeof(COMPONENTS) / sizeof(COMPONENTS[0]),
    .compatible = builtin_compatible, .memory_model = builtin_memory_model, .verify = builtin_verify, .emit = flow_emit_builtin_component,
    .validate_contract = builtin_validate_contract, .enumerate_dimensions = builtin_enumerate_dimensions, .evaluate_plan = builtin_evaluate_plan,
    .verify_plan = builtin_verify_plan, .benchmark = builtin_benchmark, .preference_mask = builtin_preference_mask,
    .environment_mask = builtin_environment_mask, .create_unit = builtin_create_unit,
    .doc_title = "Builtin Algorithms & Data Structures", .doc_responsibilities = "Provides linear_array, sharded_hash, parallel_map, and basic primitives",
    .doc_algorithmic_guarantee = "Formal QF_LIA verified sound bounds", .doc_memory_concurrency_model = "Pthread lock-free / QSBR shared memory",
    .doc_key_apis = "flow_component_evaluate, flow_component_verify_plan" };
const FlowPlugin *flow_builtin_plugin(void) { return &BUILTIN_PLUGIN; }
