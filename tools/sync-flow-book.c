/*
 * FLOW Native Doc-as-Topology Sync Generator (Slim English Only)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *chapter_ref;
    const char *title_en;
} ChapterInfo;

static const ChapterInfo ALL_CHAPTERS[] = {
    {"ch01_what_is_flow.md", "Chapter 1: What is FLOW? (Paradigm Shift from Static Compiler to Living System)"},
    {"ch02_intent_vs_implementation.md", "Chapter 2: Intent vs. Implementation (.flow: We Declare Invariants, Not Logic)"},
    {"ch03_topology_graph.md", "Chapter 3: Topology Graph (Dimensionality Reduction into Computable Invariants)"},
    {"ch04_bitmanifold_engine.md", "Chapter 4: BMF Optimization & BitManifold (BMF)"},
    {"ch05_smt_formal_verification.md", "Chapter 5: Formal Supreme Court (SMT UNSAT Proofs & 1-Cycle Pruning)"},
    {"ch06_jit_and_geometric_morphing.md", "Chapter 6: JIT Emission & Geometric Morphing (AoS to SoA)"},
    {"ch07_memory_order_and_qsbr.md", "Chapter 7: Lock-Free QSBR & Atomic Hot-Swapping (Zero-Copy Immune System)"},
    {"ch08_swarm_mesh.md", "Chapter 8: Swarm Mesh & Fleet Telemetry Gossip (Autonomous Pheromones)"},
    {"ch09_hardware_agnostic_genes.md", "Chapter 9: Hardware-Agnostic Gene Transfer (Cross-Platform Inheritance)"},
    {"ch10_predictive_time_travel.md", "Chapter 10: Predictive Time Travel (Proactive JIT & Kalman Trend Forecast)"},
    {"ch11_four_frontier_pillars.md", "Chapter 11: Four Frontier Pillars (Gateway, Swarm, Trading, CXL Fabric)"}
};
#define CHAPTER_COUNT (sizeof(ALL_CHAPTERS) / sizeof(ALL_CHAPTERS[0]))

typedef struct {
    const char *module_id;
    const char *chapter_ref;
} ModuleMap;

static const ModuleMap MODULE_BINDINGS[] = {
    {"bitspace", "ch04_bitmanifold_engine.md"},
    {"reload", "ch07_memory_order_and_qsbr.md"},
    {"orchestrator", "ch03_topology_graph.md"},
    {"embodied", "ch11_four_frontier_pillars.md"},
    {"cxl_fabric", "ch11_four_frontier_pillars.md"},
    {"gateway", "ch11_four_frontier_pillars.md"},
    {"swarm", "ch08_swarm_mesh.md"},
    {"matching", "ch11_four_frontier_pillars.md"},
    {"adaptive", "ch10_predictive_time_travel.md"},
    {"jit", "ch06_jit_and_geometric_morphing.md"},
    {"smt", "ch05_smt_formal_verification.md"},
    {"security", "ch05_smt_formal_verification.md"},
    {"topology", "ch03_topology_graph.md"},
    {"fvec", "ch09_hardware_agnostic_genes.md"},
    {"hub", "ch09_hardware_agnostic_genes.md"},
    {"backend", "ch06_jit_and_geometric_morphing.md"},
    {"primitive", "ch04_bitmanifold_engine.md"}
};
#define MODULE_COUNT (sizeof(MODULE_BINDINGS) / sizeof(MODULE_BINDINGS[0]))

static void escape_string(const char *src, FILE *out) {
    if (!src) return;
    while (*src) {
        if (*src == '"') fprintf(out, "\\\"");
        else if (*src == '\\') fprintf(out, "\\\\");
        else if (*src == '\n') fprintf(out, "\\n");
        else fputc(*src, out);
        src++;
    }
}

static const ChapterInfo *find_chapter(const char *ref) {
    for (size_t i = 0; i < CHAPTER_COUNT; ++i) {
        if (strcmp(ALL_CHAPTERS[i].chapter_ref, ref) == 0) return &ALL_CHAPTERS[i];
    }
    return &ALL_CHAPTERS[0];
}

int main(void) {
    const char *out_path = "src/generated_book_knowledge.h";
    FILE *f = fopen(out_path, "w");
    if (!f) return 1;

    fprintf(f, "/* AUTO-GENERATED - SLIM ENGLISH ONLY (ZH removed to save 3000+ lines) */\n");
    fprintf(f, "#ifndef FLOW_GENERATED_BOOK_KNOWLEDGE_H\n");
    fprintf(f, "#define FLOW_GENERATED_BOOK_KNOWLEDGE_H\n\n");
    fprintf(f, "#include \"flowy.h\"\n#include <string.h>\n\n");
    fprintf(f, "#define FLOW_BOOK_CHAPTER_COUNT %zu\n\n", CHAPTER_COUNT);

    fprintf(f, "typedef struct {\n");
    fprintf(f, "    const char *chapter_ref;\n");
    fprintf(f, "    const char *chapter_title;\n");
    fprintf(f, "} FlowBookChapterDoc;\n\n");

    fprintf(f, "static const FlowBookChapterDoc FLOW_BOOK_CHAPTERS_EN[FLOW_BOOK_CHAPTER_COUNT] = {\n");
    for (size_t i = 0; i < CHAPTER_COUNT; ++i) {
        const ChapterInfo *c = &ALL_CHAPTERS[i];
        fprintf(f, "    { \"%s\", \"", c->chapter_ref);
        escape_string(c->title_en, f);
        fprintf(f, "\" }%s\n", (i + 1 < CHAPTER_COUNT) ? "," : "");
    }
    fprintf(f, "};\n\n");

    fprintf(f, "typedef struct {\n");
    fprintf(f, "    const char *module_id;\n");
    fprintf(f, "    const char *chapter_ref;\n");
    fprintf(f, "    const char *chapter_title;\n");
    fprintf(f, "} FlowModuleBookBinding;\n\n");

    fprintf(f, "static const FlowModuleBookBinding FLOW_MODULE_BOOK_BINDINGS_EN[%zu] = {\n", MODULE_COUNT);
    for (size_t i = 0; i < MODULE_COUNT; ++i) {
        const ModuleMap *m = &MODULE_BINDINGS[i];
        const ChapterInfo *c = find_chapter(m->chapter_ref);
        fprintf(f, "    { \"%s\", \"%s\", \"", m->module_id, c->chapter_ref);
        escape_string(c->title_en, f);
        fprintf(f, "\" }%s\n", (i + 1 < MODULE_COUNT) ? "," : "");
    }
    fprintf(f, "};\n\n");

    fprintf(f, "static inline const FlowModuleBookBinding *flow_book_lookup_binding_lang(const char *module_id, FlowLanguage lang) {\n");
    fprintf(f, "    (void)lang; /* ZH removed */\n");
    fprintf(f, "    if (!module_id) return NULL;\n");
    fprintf(f, "    for (size_t i = 0; i < %zu; ++i) {\n", MODULE_COUNT);
    fprintf(f, "        if (strcmp(FLOW_MODULE_BOOK_BINDINGS_EN[i].module_id, module_id) == 0) return &FLOW_MODULE_BOOK_BINDINGS_EN[i];\n");
    fprintf(f, "    }\n    return NULL;\n}\n\n");

    fprintf(f, "static inline const FlowBookChapterDoc *flow_book_lookup_chapter_lang(const char *ref, FlowLanguage lang) {\n");
    fprintf(f, "    (void)lang; /* ZH removed */\n");
    fprintf(f, "    if (!ref) return NULL;\n");
    fprintf(f, "    for (size_t i = 0; i < FLOW_BOOK_CHAPTER_COUNT; ++i) {\n");
    fprintf(f, "        if (strcmp(FLOW_BOOK_CHAPTERS_EN[i].chapter_ref, ref) == 0) return &FLOW_BOOK_CHAPTERS_EN[i];\n");
    fprintf(f, "    }\n    return NULL;\n}\n\n");

    fprintf(f, "#endif\n");
    fclose(f);
    return 0;
}
