#include "registry.h"

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

static const FlowPlugin *PLUGINS[FLOW_PLUGIN_MAX];
static size_t plugin_count_value;
static int registry_initialized;

static int plugin_valid(const FlowPlugin *plugin) {
    size_t i;
    if (plugin == NULL || plugin->name == NULL || plugin->name[0] == '\0' ||
        plugin->version == NULL || plugin->components == NULL ||
        plugin->component_count == 0 || plugin->emit == NULL)
        return 0;
    for (i = 0; i < plugin->component_count; ++i) {
        const Component *component = &plugin->components[i];
        if (component->id == NULL || component->id[0] == '\0' ||
            component->kind == NULL || component->resource == NULL ||
            component->capability == NULL || component->domain_contract == NULL ||
            component->flow_binding == NULL)
            return 0;
    }
    return 1;
}

static int component_id_registered(const char *id) {
    size_t plugin_index;
    for (plugin_index = 0; plugin_index < plugin_count_value; ++plugin_index) {
        const FlowPlugin *plugin = PLUGINS[plugin_index];
        size_t component_index;
        for (component_index = 0; component_index < plugin->component_count;
             ++component_index)
            if (strcmp(id, plugin->components[component_index].id) == 0)
                return 1;
    }
    return 0;
}

static size_t registered_component_count(void) {
    size_t plugin_index;
    size_t count = 0;
    for (plugin_index = 0; plugin_index < plugin_count_value; ++plugin_index)
        count += PLUGINS[plugin_index]->component_count;
    return count;
}

static int add_plugin(const FlowPlugin *plugin) {
    size_t i;
    size_t registered;
    if (!plugin_valid(plugin) || plugin_count_value >= FLOW_PLUGIN_MAX)
        return 0;
    registered = registered_component_count();
    if (registered > FLOW_COMPONENT_MAX ||
        plugin->component_count > FLOW_COMPONENT_MAX - registered)
        return 0;
    for (i = 0; i < plugin_count_value; ++i)
        if (strcmp(PLUGINS[i]->name, plugin->name) == 0) return 0;
    for (i = 0; i < plugin->component_count; ++i) {
        size_t j;
        for (j = i + 1; j < plugin->component_count; ++j)
            if (strcmp(plugin->components[i].id,
                       plugin->components[j].id) == 0)
                return 0;
        if (component_id_registered(plugin->components[i].id)) return 0;
    }
    PLUGINS[plugin_count_value++] = plugin;
    return 1;
}

int flow_registry_init(void) {
    if (registry_initialized) return plugin_count_value != 0;
    registry_initialized = 1;
    if (!add_plugin(flow_builtin_plugin())) {
        plugin_count_value = 0;
        return 0;
    }
    return 1;
}

static void *DSO_HANDLES[FLOW_PLUGIN_MAX];
static size_t dso_handle_count = 0;

int flow_registry_load_dso(const char *so_path, char *err_msg, size_t err_size) {
    if (err_msg != NULL && err_size != 0) err_msg[0] = '\0';
    if (so_path == NULL) {
        if (err_msg && err_size) snprintf(err_msg, err_size, "null dso path");
        return 0;
    }
    void *handle = dlopen(so_path, RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL) {
        if (err_msg && err_size) snprintf(err_msg, err_size, "dlopen failed: %s", dlerror());
        return 0;
    }
    FlowPluginEntryFn entry_fn = (FlowPluginEntryFn)dlsym(handle, "flow_plugin_entry_v1");
    if (entry_fn == NULL) {
        if (err_msg && err_size) snprintf(err_msg, err_size, "missing entry symbol 'flow_plugin_entry_v1'");
        dlclose(handle);
        return 0;
    }
    const FlowPluginDescriptor *desc = entry_fn();
    if (desc == NULL) {
        if (err_msg && err_size) snprintf(err_msg, err_size, "flow_plugin_entry_v1 returned null descriptor");
        dlclose(handle);
        return 0;
    }
    if (desc->abi_major != FLOW_PLUGIN_ABI_MAJOR) {
        if (err_msg && err_size) {
            snprintf(err_msg, err_size, "abi major mismatch: plugin %u != host %u",
                     desc->abi_major, FLOW_PLUGIN_ABI_MAJOR);
        }
        dlclose(handle);
        return 0;
    }
    if (desc->descriptor_size != sizeof(FlowPluginDescriptor)) {
        if (err_msg && err_size) {
            snprintf(err_msg, err_size, "descriptor size mismatch: plugin %zu != host %zu",
                     desc->descriptor_size, sizeof(FlowPluginDescriptor));
        }
        dlclose(handle);
        return 0;
    }
    if (desc->plugin == NULL || desc->module_name == NULL) {
        if (err_msg && err_size) snprintf(err_msg, err_size, "invalid plugin descriptor payload");
        dlclose(handle);
        return 0;
    }
    if (!flow_registry_register(desc->plugin)) {
        if (err_msg && err_size) snprintf(err_msg, err_size, "failed to register plugin '%s'", desc->module_name);
        dlclose(handle);
        return 0;
    }
    if (dso_handle_count < FLOW_PLUGIN_MAX) {
        DSO_HANDLES[dso_handle_count++] = handle;
    }
    return 1;
}

int flow_registry_register(const FlowPlugin *plugin) {
    if (!flow_registry_init()) return 0;
    return add_plugin(plugin);
}

const FlowPlugin *flow_registry_lookup(const char *name) {
    size_t i;
    if (!flow_registry_init() || name == NULL || name[0] == '\0') return NULL;
    for (i = 0; i < plugin_count_value; ++i) {
        if (strcmp(PLUGINS[i]->name, name) == 0) return PLUGINS[i];
    }
    return NULL;
}

size_t flow_registry_plugin_count(void) {
    (void)flow_registry_init();
    return plugin_count_value;
}

const FlowPlugin *flow_registry_plugin_at(size_t index) {
    if (!flow_registry_init() || index >= plugin_count_value) return NULL;
    return PLUGINS[index];
}

const FlowPlugin *flow_component_plugin(const Component *component) {
    size_t plugin_index;
    if (!flow_registry_init() || component == NULL) return NULL;
    for (plugin_index = 0; plugin_index < plugin_count_value; ++plugin_index) {
        const FlowPlugin *plugin = PLUGINS[plugin_index];
        size_t component_index;
        for (component_index = 0; component_index < plugin->component_count;
             ++component_index)
            if (component == &plugin->components[component_index]) return plugin;
    }
    return NULL;
}

int flow_component_validate_contract(const SemanticIR *ir,
                                    const FlowPlugin *plugin,
                                    char *message, size_t message_size) {
    if (message != NULL && message_size != 0) message[0] = '\0';
    if (plugin == NULL) return 1;
    if (plugin->validate_contract != NULL)
        return plugin->validate_contract(ir, plugin, message, message_size);
    return 1;
}

void flow_plugin_lower_semantics(const FlowSpec *spec, SemanticIR *ir,
                                 const FlowPlugin *plugin) {
    if (plugin != NULL && plugin->lower_domain_semantics != NULL)
        plugin->lower_domain_semantics(spec, ir, plugin);
}

int flow_component_dimensions(const SemanticIR *ir, const Component *component,
                              FlowPlanDimensionSet *dims_out) {
    const FlowPlugin *plugin = flow_component_plugin(component);
    if (dims_out == NULL || component == NULL) return 0;
    memset(dims_out, 0, sizeof(*dims_out));
    if (plugin != NULL && plugin->enumerate_dimensions != NULL)
        return plugin->enumerate_dimensions(ir, component, dims_out);
    dims_out->count = 3;
    dims_out->dimensions[0] = (FlowPlanDimension){"capacity", FLOW_DIM_EXPONENT, FLOW_DIM_CLASS_STRUCTURAL_JIT, 1, 26, 1, 12, 500};
    dims_out->dimensions[1] = (FlowPlanDimension){"threads", FLOW_DIM_LINEAR, FLOW_DIM_CLASS_STRUCTURAL_JIT, 1, 64, 1, 1, 200};
    dims_out->dimensions[2] = (FlowPlanDimension){"shards", FLOW_DIM_LINEAR, FLOW_DIM_CLASS_STRUCTURAL_JIT, 1, 32, 1, 1, 200};
    return 1;
}

int flow_component_evaluate(const SemanticIR *ir, const Component *component,
                            const FlowPlanAssignment *plan,
                            FlowPlanMetrics *metrics_out) {
    const FlowPlugin *plugin = flow_component_plugin(component);
    if (metrics_out == NULL || component == NULL || plan == NULL) return 0;
    memset(metrics_out, 0, sizeof(*metrics_out));
    if (plugin != NULL && plugin->evaluate_plan != NULL)
        return plugin->evaluate_plan(ir, component, plan, metrics_out);
    {
        size_t capacity = plan->count > 0 ? (size_t)plan->values[0] : (size_t)ir->input_max_count;
        size_t threads = plan->count > 1 ? (size_t)plan->values[1] : 1;
        size_t shards = plan->count > 2 ? (size_t)plan->values[2] : 1;
        size_t estimated_bytes = 0;
        if (capacity == 0) capacity = 1;
        if (threads == 0) threads = 1;
        if (shards == 0) shards = 1;
        if (plugin != NULL && plugin->memory_model != NULL)
            plugin->memory_model(ir, component, capacity, shards, &estimated_bytes);
        metrics_out->capacity = capacity;
        metrics_out->threads = threads;
        metrics_out->shards = shards;
        metrics_out->memory_bytes = estimated_bytes;
        metrics_out->latency_score = component->latency_score;
        metrics_out->energy = (double)estimated_bytes / 1024.0 + (double)component->latency_score * 2.0;
        return 1;
    }
}

int flow_component_verify_plan(const SemanticIR *ir, const Component *component,
                               const FlowPlanAssignment *plan,
                               VerificationReport *report_out) {
    const FlowPlugin *plugin = flow_component_plugin(component);
    if (report_out == NULL || component == NULL || plan == NULL) return 0;
    memset(report_out, 0, sizeof(*report_out));
    if (plugin != NULL && plugin->verify_plan != NULL)
        return plugin->verify_plan(ir, component, plan, report_out);
    {
        size_t capacity = plan->count > 0 ? (size_t)plan->values[0] : (size_t)ir->input_max_count;
        size_t shards = plan->count > 2 ? (size_t)plan->values[2] : 1;
        if (capacity == 0) capacity = 1;
        if (shards == 0) shards = 1;
        return flow_component_verify(ir, component, capacity, shards,
                                     report_out->message, sizeof(report_out->message));
    }
}

uint64_t flow_component_benchmark(const SemanticIR *ir, const Component *component,
                                  const FlowPlanAssignment *plan) {
    const FlowPlugin *plugin = flow_component_plugin(component);
    if (plugin != NULL && plugin->benchmark != NULL)
        return plugin->benchmark(ir, component, plan);
    return 1000;
}

int flow_component_supports_reload(const Component *component) {
    if (component == NULL) return 0;
    return component->reload_capable;
}

void flow_ir_cleanup(SemanticIR *ir) {
    if (ir != NULL && ir->domain_ctx != NULL && ir->domain_ctx_free != NULL) {
        ir->domain_ctx_free(ir->domain_ctx);
        ir->domain_ctx = NULL;
    }
}

int flow_component_memory(const SemanticIR *ir, const Component *component,
                          size_t capacity, size_t shards,
                          size_t *estimated_bytes) {
    const FlowPlugin *plugin = flow_component_plugin(component);
    return plugin != NULL && plugin->memory_model != NULL &&
           plugin->memory_model(ir, component, capacity, shards,
                                estimated_bytes);
}

int flow_component_verify(const SemanticIR *ir, const Component *component,
                          size_t capacity, size_t shards,
                          char *message, size_t message_size) {
    const FlowPlugin *plugin = flow_component_plugin(component);
    if (message != NULL && message_size != 0) message[0] = '\0';
    return plugin != NULL &&
           (plugin->verify == NULL ||
            plugin->verify(ir, component, capacity, shards,
                           message, message_size));
}

int flow_component_preference(const SemanticIR *ir,
                              const Component *component) {
    const FlowPlugin *plugin = flow_component_plugin(component);
    if (plugin == NULL || plugin->preference == NULL) return 0;
    return plugin->preference(ir, component);
}

int flow_component_emit(FILE *output, const SemanticIR *ir,
                        const Component *component,
                        const struct FlowSearchResult *search,
                        const struct FlowVerificationReport *verification,
                        int reload_adapter) {
    const FlowPlugin *plugin = flow_component_plugin(component);
    return plugin != NULL && plugin->emit != NULL &&
           plugin->emit(output, ir, component, search, verification,
                        reload_adapter);
}

int flow_plugin_run_oracle(const FlowPlugin *plugin, const char *fixture_path,
                           char *message, size_t message_size) {
    if (message != NULL && message_size != 0) message[0] = '\0';
    if (plugin == NULL || plugin->oracle == NULL || fixture_path == NULL)
        return 0;
    return plugin->oracle(fixture_path, message, message_size);
}

int component_compatible(const SemanticIR *ir, const Component *component) {
    const FlowPlugin *plugin;
    if (ir == NULL || component == NULL) return 0;
    plugin = flow_component_plugin(component);
    if (plugin == NULL) return 0;

    if (ir->resource_name[0] != '\0' &&
        strcmp(ir->resource_name, component->resource) != 0) return 0;
    if (ir->capability_name[0] != '\0' &&
        strcmp(ir->capability_name, component->capability) != 0) return 0;

    if (ir->imported_module_count > 0) {
        int imported = 0;
        for (size_t m = 0; m < ir->imported_module_count; ++m) {
            if (strcmp(ir->imported_modules[m], plugin->name) == 0) {
                imported = 1;
                break;
            }
        }
        if (!imported) return 0;
    } else if (ir->plugin_name[0] != '\0' && strcmp(ir->plugin_name, plugin->name) != 0) {
        return 0;
    }

    if (plugin->validate_contract != NULL) {
        char err[128];
        if (!plugin->validate_contract(ir, plugin, err, sizeof(err)))
            return 0;
    } else if (ir->contract_name[0] != '\0' || component->domain_contract[0] != '\0') {
        if (strcmp(ir->contract_name, component->domain_contract) != 0)
            return 0;
    }
    return (plugin->compatible == NULL || plugin->compatible(ir, component));
}

const Component *select_component(const SemanticIR *ir) {
    const Component *best = NULL;
    int best_score = -1;
    size_t i;
    for (i = 0; i < component_count(); ++i) {
        const Component *candidate = component_at(i);
        int score = 0;
        if (!component_compatible(ir, candidate)) continue;
        if (ir->state_shared && candidate->supports_shared) score += 4;
        if (ir->state_read_heavy && candidate->supports_read_heavy) score += 4;
        if (strcmp(ir->flow_name, candidate->id) == 0) score += 100;
        if (!ir->fact_unordered && !candidate->supports_unordered) score += 5;
        score += ir->prefer_latency ? candidate->latency_score :
                                      candidate->memory_score;
        score += flow_component_preference(ir, candidate);
        if (best == NULL || score > best_score) {
            best = candidate;
            best_score = score;
        }
    }
    return best;
}

size_t compatible_component_count(const SemanticIR *ir) {
    size_t count = 0;
    size_t i;
    if (ir == NULL) return 0;
    for (i = 0; i < component_count(); ++i)
        if (component_compatible(ir, component_at(i))) ++count;
    return count;
}

const Component *compatible_component_at(const SemanticIR *ir,
                                         size_t compatible_index) {
    size_t i;
    if (ir == NULL) return NULL;
    for (i = 0; i < component_count(); ++i) {
        const Component *component = component_at(i);
        if (!component_compatible(ir, component)) continue;
        if (compatible_index == 0) return component;
        --compatible_index;
    }
    return NULL;
}

size_t component_count(void) {
    if (!flow_registry_init()) return 0;
    return registered_component_count();
}

const Component *component_at(size_t index) {
    size_t plugin_index;
    if (!flow_registry_init()) return NULL;
    for (plugin_index = 0; plugin_index < plugin_count_value; ++plugin_index) {
        const FlowPlugin *plugin = PLUGINS[plugin_index];
        if (index < plugin->component_count) return &plugin->components[index];
        index -= plugin->component_count;
    }
    return NULL;
}

size_t component_index(const Component *component) {
    size_t i;
    for (i = 0; i < component_count(); ++i)
        if (component_at(i) == component) return i;
    return 0;
}
