/* AUTO-GENERATED - SLIM ENGLISH ONLY (ZH removed to save 3000+ lines) */
#ifndef FLOW_GENERATED_BOOK_KNOWLEDGE_H
#define FLOW_GENERATED_BOOK_KNOWLEDGE_H

#include "flowy.h"
#include <string.h>

#define FLOW_BOOK_CHAPTER_COUNT 11

typedef struct {
    const char *chapter_ref;
    const char *chapter_title;
} FlowBookChapterDoc;

static const FlowBookChapterDoc FLOW_BOOK_CHAPTERS_EN[FLOW_BOOK_CHAPTER_COUNT] = {
    { "ch01_what_is_flow.md", "Chapter 1: What is FLOW? (Paradigm Shift from Static Compiler to Living System)" },
    { "ch02_intent_vs_implementation.md", "Chapter 2: Intent vs. Implementation (.flow: We Declare Invariants, Not Logic)" },
    { "ch03_topology_graph.md", "Chapter 3: Topology Graph (Dimensionality Reduction into Computable Invariants)" },
    { "ch04_bitmanifold_engine.md", "Chapter 4: BMF Optimization & BitManifold (BMF)" },
    { "ch05_smt_formal_verification.md", "Chapter 5: Formal Supreme Court (SMT UNSAT Proofs & 1-Cycle Pruning)" },
    { "ch06_jit_and_geometric_morphing.md", "Chapter 6: JIT Emission & Geometric Morphing (AoS to SoA)" },
    { "ch07_memory_order_and_qsbr.md", "Chapter 7: Lock-Free QSBR & Atomic Hot-Swapping (Zero-Copy Immune System)" },
    { "ch08_swarm_mesh.md", "Chapter 8: Swarm Mesh & Fleet Telemetry Gossip (Autonomous Pheromones)" },
    { "ch09_hardware_agnostic_genes.md", "Chapter 9: Hardware-Agnostic Gene Transfer (Cross-Platform Inheritance)" },
    { "ch10_predictive_time_travel.md", "Chapter 10: Predictive Time Travel (Proactive JIT & Kalman Trend Forecast)" },
    { "ch11_four_frontier_pillars.md", "Chapter 11: Four Frontier Pillars (Gateway, Swarm, Trading, CXL Fabric)" }
};

typedef struct {
    const char *module_id;
    const char *chapter_ref;
    const char *chapter_title;
} FlowModuleBookBinding;

static const FlowModuleBookBinding FLOW_MODULE_BOOK_BINDINGS_EN[17] = {
    { "bitspace", "ch04_bitmanifold_engine.md", "Chapter 4: BMF Optimization & BitManifold (BMF)" },
    { "reload", "ch07_memory_order_and_qsbr.md", "Chapter 7: Lock-Free QSBR & Atomic Hot-Swapping (Zero-Copy Immune System)" },
    { "orchestrator", "ch03_topology_graph.md", "Chapter 3: Topology Graph (Dimensionality Reduction into Computable Invariants)" },
    { "embodied", "ch11_four_frontier_pillars.md", "Chapter 11: Four Frontier Pillars (Gateway, Swarm, Trading, CXL Fabric)" },
    { "cxl_fabric", "ch11_four_frontier_pillars.md", "Chapter 11: Four Frontier Pillars (Gateway, Swarm, Trading, CXL Fabric)" },
    { "gateway", "ch11_four_frontier_pillars.md", "Chapter 11: Four Frontier Pillars (Gateway, Swarm, Trading, CXL Fabric)" },
    { "swarm", "ch08_swarm_mesh.md", "Chapter 8: Swarm Mesh & Fleet Telemetry Gossip (Autonomous Pheromones)" },
    { "matching", "ch11_four_frontier_pillars.md", "Chapter 11: Four Frontier Pillars (Gateway, Swarm, Trading, CXL Fabric)" },
    { "adaptive", "ch10_predictive_time_travel.md", "Chapter 10: Predictive Time Travel (Proactive JIT & Kalman Trend Forecast)" },
    { "jit", "ch06_jit_and_geometric_morphing.md", "Chapter 6: JIT Emission & Geometric Morphing (AoS to SoA)" },
    { "smt", "ch05_smt_formal_verification.md", "Chapter 5: Formal Supreme Court (SMT UNSAT Proofs & 1-Cycle Pruning)" },
    { "security", "ch05_smt_formal_verification.md", "Chapter 5: Formal Supreme Court (SMT UNSAT Proofs & 1-Cycle Pruning)" },
    { "topology", "ch03_topology_graph.md", "Chapter 3: Topology Graph (Dimensionality Reduction into Computable Invariants)" },
    { "fvec", "ch09_hardware_agnostic_genes.md", "Chapter 9: Hardware-Agnostic Gene Transfer (Cross-Platform Inheritance)" },
    { "hub", "ch09_hardware_agnostic_genes.md", "Chapter 9: Hardware-Agnostic Gene Transfer (Cross-Platform Inheritance)" },
    { "backend", "ch06_jit_and_geometric_morphing.md", "Chapter 6: JIT Emission & Geometric Morphing (AoS to SoA)" },
    { "primitive", "ch04_bitmanifold_engine.md", "Chapter 4: BMF Optimization & BitManifold (BMF)" }
};

static inline const FlowModuleBookBinding *flow_book_lookup_binding_lang(const char *module_id, FlowLanguage lang) {
    (void)lang; /* ZH removed */
    if (!module_id) return NULL;
    for (size_t i = 0; i < 17; ++i) {
        if (strcmp(FLOW_MODULE_BOOK_BINDINGS_EN[i].module_id, module_id) == 0) return &FLOW_MODULE_BOOK_BINDINGS_EN[i];
    }
    return NULL;
}

static inline const FlowBookChapterDoc *flow_book_lookup_chapter_lang(const char *ref, FlowLanguage lang) {
    (void)lang; /* ZH removed */
    if (!ref) return NULL;
    for (size_t i = 0; i < FLOW_BOOK_CHAPTER_COUNT; ++i) {
        if (strcmp(FLOW_BOOK_CHAPTERS_EN[i].chapter_ref, ref) == 0) return &FLOW_BOOK_CHAPTERS_EN[i];
    }
    return NULL;
}

#endif
