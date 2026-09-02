#ifndef REGISTRY_H
#define REGISTRY_H

#include "plugin.h"

#define FLOW_PLUGIN_MAX 64
#define FLOW_COMPONENT_MAX 16

/* The compiler executable initializes the built-ins first. Embedders can then
 * register domain plugins without changing FLOW core or registry.c. The
 * descriptor and all pointed-to data must remain alive for the registry's
 * lifetime. */
int flow_registry_init(void);
int flow_registry_register(const FlowPlugin *plugin);
int flow_registry_register_contract(const FlowPluginContract *contract);
int flow_registry_load_dso(const char *so_path, char *err_msg, size_t err_size);
const FlowPlugin *flow_registry_lookup(const char *name);
size_t flow_registry_plugin_count(void);
const FlowPlugin *flow_registry_plugin_at(size_t index);
const FlowPlugin *flow_component_plugin(const Component *component);

/* Domain module hooks & contract validation */
int flow_component_validate_contract(const SemanticIR *ir,
                                    const FlowPlugin *plugin,
                                    char *message, size_t message_size);
void flow_plugin_lower_semantics(const FlowSpec *spec, SemanticIR *ir,
                                 const FlowPlugin *plugin);
int flow_component_dimensions(const SemanticIR *ir, const Component *component,
                              FlowPlanDimensionSet *dims_out);
int flow_component_evaluate(const SemanticIR *ir, const Component *component,
                            const FlowPlanAssignment *plan,
                            FlowPlanMetrics *metrics_out);
int flow_component_verify_plan(const SemanticIR *ir, const Component *component,
                               const FlowPlanAssignment *plan,
                               VerificationReport *report_out);
uint64_t flow_component_benchmark(const SemanticIR *ir, const Component *component,
                                  const FlowPlanAssignment *plan);
int flow_component_supports_reload(const Component *component);
void flow_ir_cleanup(SemanticIR *ir);

/* Legacy / fallback hooks */
int flow_component_memory(const SemanticIR *ir, const Component *component,
                          size_t capacity, size_t shards,
                          size_t *estimated_bytes);
int flow_component_verify(const SemanticIR *ir, const Component *component,
                          size_t capacity, size_t shards,
                          char *message, size_t message_size);
int flow_component_preference(const SemanticIR *ir,
                              const Component *component);
uint64_t flow_component_mutation_mask(const SemanticIR *ir,
                                      const Component *component,
                                      const FlowPlanDimensionSet *dims);
uint64_t flow_component_preference_mask(const SemanticIR *ir,
                                        const Component *component,
                                        const FlowPlanDimensionSet *dims);
uint64_t flow_component_contract_mask(const SemanticIR *ir,
                                      const Component *component,
                                      const FlowPlanDimensionSet *dims);
uint64_t flow_component_resource_mask(const SemanticIR *ir,
                                      const Component *component,
                                      const FlowPlanDimensionSet *dims,
                                      size_t memory_limit_bytes);
uint64_t flow_component_environment_mask(const SemanticIR *ir,
                                         const Component *component,
                                         const FlowPlanDimensionSet *dims,
                                         const FlowEnvironmentState *env);
int flow_component_emit(FILE *output, const SemanticIR *ir,
                        const Component *component,
                        const struct FlowSearchResult *search,
                        const struct FlowVerificationReport *verification,
                        int reload_adapter);
int flow_plugin_run_oracle(const FlowPlugin *plugin, const char *fixture_path,
                           char *message, size_t message_size);

const Component *select_component(const SemanticIR *ir);
int component_compatible(const SemanticIR *ir, const Component *component);
size_t compatible_component_count(const SemanticIR *ir);
const Component *compatible_component_at(const SemanticIR *ir,
                                         size_t compatible_index);
size_t component_count(void);
const Component *component_at(size_t index);
size_t component_index(const Component *component);

const FlowPlugin *flow_builtin_plugin(void);

/* Declarative Contract Synthesizer (Auto-generates all component callbacks) */
FlowPlugin *flow_plugin_create_from_contract(const FlowPluginContract *contract);
void flow_plugin_free_contract_plugin(FlowPlugin *plugin);

#endif
