#include "registry.h"
#include "cubical_hott.h"
#include "flowy.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const FlowPlugin *PLUGINS[FLOW_PLUGIN_MAX]; static size_t plugin_count_value; static int registry_initialized; static void *DSO_HANDLES[FLOW_PLUGIN_MAX];
static size_t dso_handle_count = 0; static int plugin_valid(const FlowPlugin *p) {
    if (!p || !p->name || !p->name[0] || !p->version || (p->component_count > 0 && !p->components)) return 0;
    for (size_t i = 0; i < p->component_count; ++i) if (!p->components[i].id || !p->components[i].id[0]) return 0; return 1; }
static int component_id_registered(const char *id) { for (size_t p = 0; p < plugin_count_value; ++p)
        for (size_t c = 0; c < PLUGINS[p]->component_count; ++c) if (!strcmp(id, PLUGINS[p]->components[c].id)) return 1; return 0; }
static size_t registered_component_count(void) { size_t count = 0; for (size_t p = 0; p < plugin_count_value; ++p) count += PLUGINS[p]->component_count; return count; }
static int add_plugin(const FlowPlugin *plugin) { if (!plugin_valid(plugin) || plugin_count_value >= FLOW_PLUGIN_MAX) return 0; size_t reg = registered_component_count();
    if (reg > FLOW_COMPONENT_MAX || plugin->component_count > FLOW_COMPONENT_MAX - reg) return 0;
    for (size_t i = 0; i < plugin_count_value; ++i) if (!strcmp(PLUGINS[i]->name, plugin->name)) return 0; for (size_t i = 0; i < plugin->component_count; ++i) {
        for (size_t j = i + 1; j < plugin->component_count; ++j) if (!strcmp(plugin->components[i].id, plugin->components[j].id)) return 0;
        if (component_id_registered(plugin->components[i].id)) return 0; } PLUGINS[plugin_count_value++] = plugin; if (plugin->name) { FlowModuleKnowledge dyn_k = {
            .module_id = plugin->name, .title = plugin->doc_title ? plugin->doc_title : plugin->name,
            .header_file = "src/plugin.h", .source_file = "dynamic_dso", .layer = plugin->doc_layer > 0 ? plugin->doc_layer : 2,
            .responsibilities = plugin->doc_responsibilities ? plugin->doc_responsibilities : "Dynamic domain plugin component",
            .algorithmic_guarantee = plugin->doc_algorithmic_guarantee ? plugin->doc_algorithmic_guarantee : "Verified plugin contract",
            .memory_concurrency_model = plugin->doc_memory_concurrency_model ? plugin->doc_memory_concurrency_model : "Plugin managed memory",
            .key_apis = plugin->doc_key_apis ? plugin->doc_key_apis : "flow_component_evaluate, flow_component_verify_plan", .keywords = plugin->name };
        flowy_register_dynamic_module(&dyn_k); } return 1; } int flow_registry_register_contract(const FlowPluginContract *c) { if (!c || !c->module_name) return 0;
    FlowModuleKnowledge dyn_k = { .module_id = c->module_name, .title = c->doc_title ? c->doc_title : c->module_name,
        .header_file = "declarative_contract", .source_file = "contract.flow", .layer = 2,
        .responsibilities = c->doc_responsibilities ? c->doc_responsibilities : "Declarative zero-C plugin contract",
        .algorithmic_guarantee = c->doc_algorithmic_guarantee ? c->doc_algorithmic_guarantee : "Declarative QF_LIA verified bounds",
        .memory_concurrency_model = c->doc_memory_model ? c->doc_memory_model : "Contract bounded memory allocation",
        .key_apis = c->doc_key_apis ? c->doc_key_apis : "declarative_eval, declarative_verify", .keywords = c->module_name }; return flowy_register_dynamic_module(&dyn_k); }
int flow_registry_init(void) { if (registry_initialized) return plugin_count_value != 0; registry_initialized = 1;
    if (!add_plugin(flow_builtin_plugin())) { plugin_count_value = 0; return 0; } return 1; } int flow_registry_load_dso(const char *so_path, char *err_msg, size_t err_size) {
    #define DSO_ERR(...) do { if (err_msg && err_size) snprintf(err_msg, err_size, __VA_ARGS__); if (handle) dlclose(handle); return 0; } while (0)
    if (err_msg && err_size) err_msg[0] = '\0'; void *handle = NULL; if (!so_path) DSO_ERR("null dso path");
    if (!(handle = dlopen(so_path, RTLD_NOW | RTLD_LOCAL))) DSO_ERR("dlopen failed: %s", dlerror());
    static const char *const SYMS[] = {"flow_plugin_entry_v1", "flow_embodied_entry_v1", "flow_swarm_entry_v1"}; FlowPluginEntryFn entry_fn = NULL;
    for (size_t i = 0; i < 3 && !entry_fn; ++i) entry_fn = (FlowPluginEntryFn)dlsym(handle, SYMS[i]); if (!entry_fn) DSO_ERR("missing entry symbol 'flow_plugin_entry_v1'");
    const FlowPluginDescriptor *desc = entry_fn(); if (!desc) DSO_ERR("flow_plugin_entry_v1 returned null descriptor");
    if (desc->abi_major != FLOW_PLUGIN_ABI_MAJOR) DSO_ERR("abi major mismatch: plugin %u != host %u", desc->abi_major, FLOW_PLUGIN_ABI_MAJOR);
    if (desc->descriptor_size < 64) DSO_ERR("descriptor size mismatch: plugin %zu < min 64", desc->descriptor_size);
    if (!desc->plugin || !desc->module_name || !flow_registry_register(desc->plugin)) DSO_ERR("failed to register plugin");
    if (dso_handle_count < FLOW_PLUGIN_MAX) DSO_HANDLES[dso_handle_count++] = handle;
    #undef DSO_ERR
    return 1; } int flow_registry_register(const FlowPlugin *plugin) { return flow_registry_init() ? add_plugin(plugin) : 0; }
const FlowPlugin *flow_registry_lookup(const char *name) { if (!flow_registry_init() || !name || !name[0]) return NULL;
    for (size_t i = 0; i < plugin_count_value; ++i) if (!strcmp(PLUGINS[i]->name, name)) return PLUGINS[i]; return NULL; }
size_t flow_registry_plugin_count(void) { (void)flow_registry_init(); return plugin_count_value; }
const FlowPlugin *flow_registry_plugin_at(size_t index) { return (flow_registry_init() && index < plugin_count_value) ? PLUGINS[index] : NULL; }
const FlowPlugin *flow_component_plugin(const Component *component) { if (!flow_registry_init() || !component) return NULL; for (size_t p = 0; p < plugin_count_value; ++p)
        for (size_t c = 0; c < PLUGINS[p]->component_count; ++c) if (component == &PLUGINS[p]->components[c]) return PLUGINS[p]; return NULL; }
int flow_component_validate_contract(const SemanticIR *ir, const FlowPlugin *p, char *msg, size_t sz) {
    if (msg && sz) msg[0] = '\0'; return (!p || !p->validate_contract) ? 1 : p->validate_contract(ir, p, msg, sz); }
void flow_plugin_lower_semantics(const FlowSpec *spec, SemanticIR *ir, const FlowPlugin *p) { if (p && p->lower_domain_semantics) p->lower_domain_semantics(spec, ir, p); }
int flow_component_dimensions(const SemanticIR *ir, const Component *c, FlowPlanDimensionSet *dims_out) { if (!dims_out || !c) return 0;
    memset(dims_out, 0, sizeof(*dims_out)); const FlowPlugin *p = flow_component_plugin(c); if (p && p->enumerate_dimensions) return p->enumerate_dimensions(ir, c, dims_out);
    dims_out->count = 3; dims_out->dimensions[0] = (FlowPlanDimension){"capacity", FLOW_DIM_EXPONENT, FLOW_DIM_CLASS_STRUCTURAL_JIT, 1, 26, 1, 12, 500};
    dims_out->dimensions[1] = (FlowPlanDimension){"threads", FLOW_DIM_LINEAR, FLOW_DIM_CLASS_STRUCTURAL_JIT, 1, 64, 1, 1, 200};
    dims_out->dimensions[2] = (FlowPlanDimension){"shards", FLOW_DIM_LINEAR, FLOW_DIM_CLASS_STRUCTURAL_JIT, 1, 32, 1, 1, 200}; return 1; }
int flow_component_evaluate(const SemanticIR *ir, const Component *c, const FlowPlanAssignment *plan, FlowPlanMetrics *m) { if (!m || !c || !plan) return 0;
    memset(m, 0, sizeof(*m)); const FlowPlugin *p = flow_component_plugin(c); if (p && p->evaluate_plan) return p->evaluate_plan(ir, c, plan, m);
    size_t cap = plan->count > 0 ? (size_t)plan->values[0] : (size_t)ir->input_max_count, thr = plan->count > 1 ? (size_t)plan->values[1] : 1, shd = plan->count > 2 ? (size_t)plan->values[2] : 1, est = 0;
    if (!cap) cap = 1; if (!thr) thr = 1; if (!shd) shd = 1; if (p && p->memory_model) p->memory_model(ir, c, cap, shd, &est);
    *m = (FlowPlanMetrics){.capacity = cap, .threads = thr, .shards = shd, .memory_bytes = est, .latency_score = c->latency_score, .energy = (double)est / 1024.0 + (double)c->latency_score * 2.0};
    return 1; } int flow_component_verify_plan(const SemanticIR *ir, const Component *c, const FlowPlanAssignment *plan, VerificationReport *r) {
    if (!r || !c || !plan) return 0; memset(r, 0, sizeof(*r)); const FlowPlugin *p = flow_component_plugin(c); if (p && p->verify_plan) return p->verify_plan(ir, c, plan, r);
    size_t cap = plan->count > 0 ? (size_t)plan->values[0] : (size_t)ir->input_max_count, shd = plan->count > 2 ? (size_t)plan->values[2] : 1;
    return flow_component_verify(ir, c, cap ? cap : 1, shd ? shd : 1, r->message, sizeof(r->message)); }
uint64_t flow_component_benchmark(const SemanticIR *ir, const Component *c, const FlowPlanAssignment *plan) { const FlowPlugin *p = flow_component_plugin(c); return (p && p->benchmark) ? p->benchmark(ir, c, plan) : 1000; }
int flow_component_supports_reload(const Component *c) { return c ? c->reload_capable : 0; }
void flow_ir_cleanup(SemanticIR *ir) { if (ir && ir->domain_ctx && ir->domain_ctx_free) { ir->domain_ctx_free(ir->domain_ctx); ir->domain_ctx = NULL; } }
int flow_component_memory(const SemanticIR *ir, const Component *c, size_t cap, size_t shd, size_t *est) { const FlowPlugin *p = flow_component_plugin(c); return p && p->memory_model && p->memory_model(ir, c, cap, shd, est); }
int flow_component_verify(const SemanticIR *ir, const Component *c, size_t cap, size_t shd, char *msg, size_t sz) { const FlowPlugin *p = flow_component_plugin(c); if (msg && sz) msg[0] = '\0'; return p && (!p->verify || p->verify(ir, c, cap, shd, msg, sz)); }
int flow_component_preference(const SemanticIR *ir, const Component *c) { const FlowPlugin *p = flow_component_plugin(c); return (p && p->preference) ? p->preference(ir, c) : 0; }
uint64_t flow_component_mutation_mask(const SemanticIR *ir, const Component *c, const FlowPlanDimensionSet *dims) { const FlowPlugin *p = flow_component_plugin(c); return (p && p->get_mutation_mask) ? p->get_mutation_mask(ir, c, dims) : (uint64_t)-1; }
uint64_t flow_component_preference_mask(const SemanticIR *ir, const Component *c, const FlowPlanDimensionSet *dims) { const FlowPlugin *p = flow_component_plugin(c); return (p && p->preference_mask) ? p->preference_mask(ir, c, dims) : 0; }
uint64_t flow_component_contract_mask(const SemanticIR *ir, const Component *c, const FlowPlanDimensionSet *dims) { const FlowPlugin *p = flow_component_plugin(c); return (p && p->contract_mask) ? p->contract_mask(ir, c, dims) : (uint64_t)-1; }
uint64_t flow_component_resource_mask(const SemanticIR *ir, const Component *c, const FlowPlanDimensionSet *dims, size_t lim) { const FlowPlugin *p = flow_component_plugin(c); return (p && p->resource_mask) ? p->resource_mask(ir, c, dims, lim) : (uint64_t)-1; }
uint64_t flow_component_environment_mask(const SemanticIR *ir, const Component *c, const FlowPlanDimensionSet *dims, const FlowEnvironmentState *env) { const FlowPlugin *p = flow_component_plugin(c); return (p && p->environment_mask) ? p->environment_mask(ir, c, dims, env) : (uint64_t)-1; }
int flow_component_emit(FILE *out, const SemanticIR *ir, const Component *c, const struct FlowSearchResult *s, const struct FlowVerificationReport *v, int reload) { const FlowPlugin *p = flow_component_plugin(c); return p && p->emit && p->emit(out, ir, c, s, v, reload); }
int flow_plugin_run_oracle(const FlowPlugin *p, const char *path, char *msg, size_t sz) { if (msg && sz) msg[0] = '\0'; return (p && p->oracle && path) ? p->oracle(path, msg, sz) : 0; }
int component_compatible(const SemanticIR *ir, const Component *c) { if (!ir || !c) return 0; const FlowPlugin *p = flow_component_plugin(c);
    if (!p || (ir->resource_name[0] && strcmp(ir->resource_name, c->resource)) || (ir->capability_name[0] && strcmp(ir->capability_name, c->capability))) return 0;
    if (ir->imported_module_count > 0) { int imp = 0;
        for (size_t m = 0; m < ir->imported_module_count; ++m) if (!strcmp(ir->imported_modules[m], p->name) || !strcmp(p->name, "builtin")) { imp = 1; break; }
        if (!imp) return 0; } else if (ir->plugin_name[0] && strcmp(ir->plugin_name, p->name) && strcmp(p->name, "builtin")) return 0;
    if (p->validate_contract) { char err[128]; if (!p->validate_contract(ir, p, err, sizeof(err))) return 0; }
    else if (ir->contract_name[0] && (!c->domain_contract || strcmp(ir->contract_name, c->domain_contract))) return 0;
    FlowCubicalTypeSieve ir_sieve = flow_cubical_sieve_from_ir(ir), comp_sieve = flow_cubical_sieve_from_component(c);
    if (c->cubical_type_mask) comp_sieve.mask |= c->cubical_type_mask; if (!flow_cubical_match_types(ir_sieve, comp_sieve, NULL)) return 0;
    return (!p->compatible || p->compatible(ir, c)); } const Component *select_component(const SemanticIR *ir) { const Component *best = NULL; int best_score = -1;
    FlowCubicalTypeSieve ir_sieve = flow_cubical_sieve_from_ir(ir); for (size_t i = 0; i < component_count(); ++i) { const Component *cand = component_at(i);
        if (!component_compatible(ir, cand)) continue; FlowCubicalTypeSieve cand_sieve = flow_cubical_sieve_from_component(cand);
        int h_score = flow_cubical_match_types(ir_sieve, cand_sieve, NULL) ? (int)(__builtin_popcountll(ir_sieve.mask & cand_sieve.mask) * 10) : 0;
        int score = h_score + (!strcmp(ir->flow_name, cand->id) ? 100 : 0) + (!ir->fact_unordered && !cand->supports_unordered ? 5 : 0) +
                    (ir->prefer_latency ? cand->latency_score : cand->memory_score) + flow_component_preference(ir, cand);
        if (!best || score > best_score) { best = cand; best_score = score; } } return best; } size_t compatible_component_count(const SemanticIR *ir) { if (!ir) return 0;
    size_t count = 0; for (size_t i = 0; i < component_count(); ++i) if (component_compatible(ir, component_at(i))) ++count; return count; }
const Component *compatible_component_at(const SemanticIR *ir, size_t idx) { if (!ir) return NULL;
    for (size_t i = 0; i < component_count(); ++i) { const Component *c = component_at(i); if (!component_compatible(ir, c)) continue; if (idx == 0) return c; --idx; }
    return NULL; } size_t component_count(void) { return flow_registry_init() ? registered_component_count() : 0; } const Component *component_at(size_t index) {
    if (!flow_registry_init()) return NULL;
    for (size_t p = 0; p < plugin_count_value; ++p) { if (index < PLUGINS[p]->component_count) return &PLUGINS[p]->components[index]; index -= PLUGINS[p]->component_count; }
    return NULL; } size_t component_index(const Component *c) { for (size_t i = 0; i < component_count(); ++i) if (component_at(i) == c) return i; return 0; }
typedef struct { FlowPluginContract contract; } FlowDeclarativeContext; static int decl_validate_contract(const SemanticIR *ir, const FlowPlugin *p, char *msg, size_t sz) {
    if (!p || !p->domain_context || !ir) return 1; const FlowDeclarativeContext *ctx = p->domain_context;
    if (ctx->contract.target_domain && ctx->contract.target_domain[0] && ir->domain_name[0] && strcmp(ir->domain_name, ctx->contract.target_domain)) { if (msg && sz) snprintf(msg, sz, "domain mismatch with contract %s", ctx->contract.target_domain); return 0; }
    if (ctx->contract.target_contract && ctx->contract.target_contract[0] && ir->contract_name[0] && strcmp(ir->contract_name, ctx->contract.target_contract)) { if (msg && sz) snprintf(msg, sz, "contract mismatch with contract %s", ctx->contract.target_contract); return 0; }
    return 1; } static int decl_compatible(const SemanticIR *ir, const Component *c) {
    return ir && c && (!ir->state_shared || c->supports_shared) && (!ir->state_read_heavy || c->supports_read_heavy) && (!ir->flow_parallelizable || c->supports_parallelizable) && (!ir->fact_ordered || !c->supports_unordered);
} static int decl_emit(FILE *out, const SemanticIR *ir, const Component *c, const struct FlowSearchResult *s, const struct FlowVerificationReport *v, int r) {
    (void)s; (void)v; (void)r; return (out && ir && c) ? (fprintf(out, "/* Auto-Generated Declarative FLOW Component: %s */\n", c->id) > 0) : 0; }
static int decl_enumerate_dimensions(const SemanticIR *ir, const Component *c, FlowPlanDimensionSet *dims_out) { (void)ir; if (!c || !dims_out) return 0;
    const FlowPlugin *p = flow_component_plugin(c); if (!p || !p->domain_context) return 0; const FlowDeclarativeContext *ctx = p->domain_context;
    for (size_t i = 0; i < ctx->contract.component_count; ++i) { if (!strcmp(ctx->contract.components[i].component_id, c->id)) {
            const FlowContractComponent *cc = &ctx->contract.components[i]; dims_out->count = cc->dimension_count;
            for (size_t d = 0; d < cc->dimension_count; ++d) dims_out->dimensions[d] = cc->dimensions[d]; return 1; } } return 0; }
static int decl_evaluate_plan(const SemanticIR *ir, const Component *c, const FlowPlanAssignment *plan, FlowPlanMetrics *m) { if (!ir || !c || !plan || !m) return 0;
    const FlowPlugin *p = flow_component_plugin(c); if (!p || !p->domain_context) return 0; const FlowDeclarativeContext *ctx = p->domain_context;
    size_t cap = 1024, thr = 1, shd = 1; FlowPlanDimensionSet dims; if (decl_enumerate_dimensions(ir, c, &dims)) { for (size_t d = 0; d < dims.count && d < plan->count; ++d) {
            size_t v = (dims.dimensions[d].kind == FLOW_DIM_EXPONENT) ? ((size_t)1 << plan->values[d]) : (size_t)plan->values[d];
            if (!strcmp(dims.dimensions[d].name, "capacity")) cap = v; else if (!strcmp(dims.dimensions[d].name, "threads")) thr = v;
            else if (!strcmp(dims.dimensions[d].name, "shards")) shd = v; } } double lat_w = 1.0, tp_w = 1.0; size_t bps = 16, fix = 64;
    for (size_t i = 0; i < ctx->contract.component_count; ++i) { if (!strcmp(ctx->contract.components[i].component_id, c->id)) {
            lat_w = ctx->contract.components[i].base_latency_weight; tp_w = ctx->contract.components[i].base_throughput_weight;
            bps = ctx->contract.components[i].memory_bytes_per_slot; fix = ctx->contract.components[i].memory_fixed_overhead; break; } }
    *m = (FlowPlanMetrics){ .capacity = cap, .threads = thr, .shards = shd, .memory_bytes = fix + cap * bps, .latency_score = lat_w * (10.0 + (double)cap / 1000.0), .throughput_score = tp_w * (double)thr * 100000.0 };
    m->energy = m->latency_score * 5.0 + (double)m->memory_bytes / 1024.0; return 1; }
static int decl_verify_plan(const SemanticIR *ir, const Component *c, const FlowPlanAssignment *plan, VerificationReport *r) { if (!r) return 0; FlowPlanMetrics m;
    if (!decl_evaluate_plan(ir, c, plan, &m)) { r->status = VERIFIER_COMPILE_ERROR; snprintf(r->message, sizeof(r->message), "failed to evaluate declarative plan"); return 0; }
    r->capacity = m.capacity; r->estimated_bytes = m.memory_bytes; if (ir && ir->input_max_count > 0 && m.capacity < (size_t)ir->input_max_count) {
        r->status = VERIFIER_COMPILE_ERROR; snprintf(r->message, sizeof(r->message), "capacity %zu below required max_count %d", m.capacity, ir->input_max_count); return 0; }
    r->status = VERIFIER_PROVEN; r->max_count_proven = 1; snprintf(r->message, sizeof(r->message), "declarative contract verified proven"); return 1; }
FlowPlugin *flow_plugin_create_from_contract(const FlowPluginContract *contract) { if (!contract || !contract->component_count) return NULL;
    FlowPlugin *p = calloc(1, sizeof(FlowPlugin)); FlowDeclarativeContext *ctx = calloc(1, sizeof(FlowDeclarativeContext));
    Component *comps = calloc(contract->component_count, sizeof(Component)); if (!p || !ctx || !comps) { free(comps); free(ctx); free(p); return NULL; }
    ctx->contract = *contract; for (size_t c = 0; c < contract->component_count; ++c) { const FlowContractComponent *cc = &contract->components[c]; comps[c] = (Component){
            .id = strdup(cc->component_id), .kind = strdup(cc->kind[0] ? cc->kind : "collection"), .resource = "cpu", .capability = "declarative",
            .domain_contract = contract->target_contract ? strdup(contract->target_contract) : "", .flow_binding = "",
            .supports_shared = cc->supports_shared, .supports_read_heavy = cc->supports_read_heavy, .supports_unordered = !cc->supports_ordered,
            .supports_parallelizable = cc->supports_parallel, .memory_fixed_bytes = cc->memory_fixed_overhead, .memory_bytes_per_capacity = cc->memory_bytes_per_slot,
            .latency_score = (int)cc->base_latency_weight, .memory_score = 10 }; } *p = (FlowPlugin){
        .name = strdup(contract->module_name), .version = strdup(contract->module_version ? contract->module_version : "1.0.0"),
        .components = comps, .component_count = contract->component_count, .domain_context = ctx, .emit = decl_emit,
        .validate_contract = decl_validate_contract, .compatible = decl_compatible, .enumerate_dimensions = decl_enumerate_dimensions,
        .evaluate_plan = decl_evaluate_plan, .verify_plan = decl_verify_plan }; return p; } void flow_plugin_free_contract_plugin(FlowPlugin *plugin) { if (!plugin) return;
    if (plugin->components) { for (size_t i = 0; i < plugin->component_count; ++i) { free((void *)plugin->components[i].id); free((void *)plugin->components[i].kind); }
        free((void *)plugin->components); } free((void *)plugin->name); free((void *)plugin->version); free(plugin->domain_context); free(plugin); }
