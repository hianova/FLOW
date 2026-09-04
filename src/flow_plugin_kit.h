#ifndef FLOW_PLUGIN_KIT_H
#define FLOW_PLUGIN_KIT_H

#include "plugin.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * FLOW Plugin Kit: Declarative Pure C17 Plugin Infrastructure & Scaffolding
 * ============================================================================
 * 
 * Provides declarative macros for:
 * 1. Standard Canonical Plugin ABI v2 (4-function pure contract)
 * 2. Plugin Descriptors & Dynamic Shared Object (DSO) entry points
 * 3. Default safe implementations for contract validation & memory modeling
 * ============================================================================ */

/* ----------------------------------------------------------------------------
 * Default Safe Plugin Hook Implementations
 * ---------------------------------------------------------------------------- */

static inline int flow_plugin_default_validate_contract(const SemanticIR *ir,
                                                        const FlowPlugin *plugin,
                                                        char *message,
                                                        size_t message_size) {
    (void)plugin;
    if (message != NULL && message_size > 0) {
        message[0] = '\0';
    }
    return (ir != NULL) ? 1 : 0;
}

static inline int flow_plugin_default_memory_model(const SemanticIR *ir,
                                                   const Component *component,
                                                   size_t capacity,
                                                   size_t shards,
                                                   size_t *estimated_bytes) {
    (void)ir;
    (void)shards;
    if (component == NULL || estimated_bytes == NULL) return 0;
    size_t var_bytes = capacity * component->memory_bytes_per_capacity;
    *estimated_bytes = component->memory_fixed_bytes + var_bytes;
    return 1;
}

/* ----------------------------------------------------------------------------
 * Declarative Canonical ABI v2 Generation Macro
 * ---------------------------------------------------------------------------- */

#define FLOW_PLUGIN_ABI_DECLARE(Prefix, BitSize, ValidMaskFn, EnergyFn, EmitFn) \
    static const FlowPluginABI Prefix##_abi_v2 = {                              \
        .get_genome_bit_size = (BitSize),                                       \
        .get_valid_mask = (ValidMaskFn),                                        \
        .evaluate_energy = (EnergyFn),                                          \
        .emit_llvm_ir = (EmitFn)                                                \
    };                                                                          \
    static inline const FlowPluginABI *flow_##Prefix##_abi_v2(void) {           \
        return &Prefix##_abi_v2;                                                \
    }

/* ----------------------------------------------------------------------------
 * Declarative Plugin Descriptor & Entry Point Macro
 * ---------------------------------------------------------------------------- */

#define FLOW_PLUGIN_DESCRIPTOR_DECLARE(Prefix, ModName, ModVersion, PluginPtr, AbiPtr) \
    static const FlowPluginDescriptor Prefix##_descriptor = {                           \
        .abi_major = FLOW_PLUGIN_ABI_MAJOR,                                             \
        .abi_minor = FLOW_PLUGIN_ABI_MINOR,                                             \
        .descriptor_size = sizeof(FlowPluginDescriptor),                                \
        .module_name = (ModName),                                                       \
        .module_version = (ModVersion),                                                 \
        .module_hash = 0,                                                               \
        .plugin = (PluginPtr),                                                          \
        .abi_v2 = (AbiPtr),                                                             \
        .dso_handle = NULL,                                                             \
        .active_references = 0                                                          \
    };                                                                                  \
    static inline const FlowPluginDescriptor *flow_##Prefix##_descriptor(void) {        \
        return &Prefix##_descriptor;                                                    \
    }

#ifdef __cplusplus
}
#endif

#endif /* FLOW_PLUGIN_KIT_H */
