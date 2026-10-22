#include "registry.h"
#include "backend.h"
#include "cubical_hott.h"
#include "reload.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    if (!so_path) DSO_ERR("null dso path");
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
int flow_registry_register(const FlowPlugin *plugin) { return flow_registry_init() ? add_plugin(plugin) : 0; }
const FlowPlugin *flow_registry_lookup(const char *name) {
    if (!flow_registry_init() || !name || !name[0]) return NULL;
    for (size_t i = 0; i < plugin_count_value; ++i) if (!strcmp(PLUGINS[i]->name, name)) return PLUGINS[i];
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
    size_t cap = plan->count > 0 ? (size_t)plan->values[0] : (size_t)ir->input_max_count, shd = plan->count > 2 ? (size_t)plan->values[2] : 1;
    return flow_component_verify(ir, c, cap ? cap : 1, shd ? shd : 1, r->message, sizeof(r->message));
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
    return p->emit ? p->emit(out, ir, c, s, v, reload) : emit_default_unit_c(out, ir, c, v);
}
int component_compatible(const SemanticIR *ir, const Component *c) {
    if (!ir || !c) return 0;
    const FlowPlugin *p = flow_component_plugin(c);
    if (!p || (ir->resource_name[0] && strcmp(ir->resource_name, c->resource)) || (ir->capability_name[0] && strcmp(ir->capability_name, c->capability))) return 0;
    if (ir->imported_module_count > 0) {
        int imp = 0;
        for (size_t m = 0; m < ir->imported_module_count; ++m) if (!strcmp(ir->imported_modules[m], p->name) || !strcmp(p->name, "builtin")) { imp = 1; break; }
        if (!imp) return 0;
    } else if (ir->plugin_name[0] && strcmp(ir->plugin_name, p->name) && strcmp(p->name, "builtin")) return 0;
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
