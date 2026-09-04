/*
 * FLOW Native Doc-as-Topology Bilingual Sync Generator
 * Pure C17 implementation - 100% Zero External Interpreter Dependency (Autopoiesis / 自舉)
 *
 * Emits src/generated_book_knowledge.h with Language-Agnostic Bindings + ZH/EN Render Masks.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *chapter_ref;
    const char *title_zh;
    const char *title_en;
    const char *why_zh;
    const char *why_en;
    const char *excerpt_zh;
    const char *excerpt_en;
} ChapterInfo;

static const ChapterInfo ALL_CHAPTERS[] = {
    {
        "ch01_what_is_flow.md",
        "第一章：什麼是 FLOW？ (從靜態編譯到動態活體系統的典範轉移)",
        "Chapter 1: What is FLOW? (Paradigm Shift from Static Compiler to Living System)",
        "我們不是在編譯一份死沉沉的程式碼，而是在孕育一個能夠感知環境、自我退火、在毀滅性壓力下自我蛻變的拓樸活體。",
        "We are not compiling dead code, but nurturing an autopoietic topological organism that senses environment, anneals autonomously, and morphs under destructive stress.",
        "自 1957 年 Fortran 編譯器問世以來，編譯器架構的核心假設始終如一：原始碼是唯一真理。FLOW 徹底顛覆此一假設，將代碼視為可感知環境與持續演化的活體幾何流形。",
        "Since the 1957 Fortran compiler, the assumption has been that source code is static truth. FLOW inverts this: code declares immutable geometric boundaries, while the runtime organism evolves on manifolds."
    },
    {
        "ch02_intent_vs_implementation.md",
        "第二章：意圖規格 .flow (我們只向宇宙宣告幾何約束，不寫邏輯)",
        "Chapter 2: Intent vs. Implementation (.flow: We Declare Invariants, Not Logic)",
        "在 FLOW 的哲學中，撰寫程式碼不是告訴 CPU 如何一步一步執行指令，而是向宇宙宣告系統必須服從的幾何邊界與不變量。",
        "In FLOW philosophy, writing code is not telling the CPU step-by-step instructions, but declaring the geometric boundaries and mathematical invariants the universe must obey.",
        "一個標準的 .flow 描述檔是純宣告式的，不包含命令式迴圈或指標操作。開發者只宣告 input 規模、flow 流水線與物理約束，實作由引擎自動坍縮合成。",
        "A standard .flow spec is strictly declarative with no imperative loops or manual pointers. Developers declare input bounds and pipeline topology, letting the engine synthesize optimal implementations."
    },
    {
        "ch03_topology_graph.md",
        "第三章：拓樸圖譜 (將程式碼降維成可推算的依賴約束流形)",
        "Chapter 3: Topology Graph (Dimensionality Reduction into Computable Invariants)",
        "軟體的架構不是文字檔目錄的堆疊，而是一張高維拓樸圖。在 FLOW 中，程式碼被降維成可直接進行圖論運算、親和性分析與遙測附著的活體神經圖譜。",
        "Software architecture is not a stack of directory text files, but a high-dimensional topology graph computed via graph theory, shard affinity, and neural telemetry.",
        "FLOW 將核心模組、驅動、元件與意圖統構為 FlowTopologyGraph，實施嚴格的分層防火牆審計、動態神經遙測附著與零跨層滲漏保證。",
        "FLOW models compiler core, drivers, components, and intent as a unified FlowTopologyGraph, enforcing strict layer firewalls with 0 cross-layer architectural leaks."
    },
    {
        "ch04_1bit_chaos_engine.md",
        "第四章：1-Bit 混沌退火與 BitManifold (BMF) (暫存器位元翻轉、連鎖群與量子漂移)",
        "Chapter 4: 1-Bit Chaotic Annealing & BitManifold (BMF) (Bit Flips, Linkage Groups & Quantum Drift)",
        "在維度的詛咒與上位效應壁壘面前，傳統遺傳交叉必然撕裂拓樸；FLOW 以 12.96 奈秒純暫存器 1-Bit 混沌微步與 SMT 超級位元原子翻轉，突破局部鞍點。",
        "In face of epistasis barriers, traditional GA crossover tears topology. FLOW employs 12.96ns 1-bit chaotic mutations with SMT super-bit atomic flips to tunnel through saddle points.",
        "1-Bit 狀態脊椎將系統維度壓縮為 64-bit 離散基因組，透過三層動態遮罩畫布（SMT 硬安全閘門、遙測偏置、領域偏好）在 1 個時鐘週期內修剪 99.9% 非法狀態，並以 9-Byte UDP 費洛蒙實現群體尋優。",
        "The 1-Bit state spine encodes dimensions into 64-bit genomes, pruning 99.9% illegal states in a single clock cycle via 3-Tier Mask Canvases, sharing convergence via 9-byte UDP pheromones."
    },
    {
        "ch05_smt_formal_verification.md",
        "第五章：形式化最高法院 (SMT 4 大定理 UNSAT 證明與 1-Cycle 修剪)",
        "Chapter 5: Formal Supreme Court (SMT 4-Theorem UNSAT Proofs & 1-Cycle Pruning)",
        "啟發式搜尋可以天馬行空，但發射出的每一行機器碼必須擁有無可爭辯的數學證明。SMT 定理證明器是 FLOW 宇宙的最高法院，凡無證明者，一律否決。",
        "Heuristic search can explore freely, but every emitted line of machine code must have indisputable mathematical proofs. The SMT solver is the Supreme Court: unproven plans are vetoed.",
        "SMT 最高法院透過 QF_LIA 理論驗證緩衝區邊界、記憶體配額上限、分片隔離與確定性 4 大定理。合約檢查與硬體邊界在編譯期即刻生成 1-Cycle 多面體位元修剪遮罩。",
        "SMT proves buffer bounds, memory quotas, shard isolation, and determinism via QF_LIA. Contract and resource bounds immediately synthesize 1-cycle bitwise pruning masks."
    },
    {
        "ch06_jit_and_geometric_morphing.md",
        "第六章：JIT 代碼發射與幾何變形 (AoS 到 SoA 即時重映射與生存模式)",
        "Chapter 6: JIT Emission & Geometric Morphing (AoS to SoA Remapping & Survival Mode)",
        "程式碼不是雕刻在石頭上的死文字，而是能在記憶體中自發變形的黏土。當記憶體即將崩潰時，系統在微秒內完成拓樸幾何變形，躲過 OS OOM Killer 的致命屠刀。",
        "Code is not carved in stone, but clay morphing in memory. When RAM collapses, the system executes microsecond geometric morphing to escape the OS OOM Killer.",
        "JIT 發射器支援 C、Rust、Python 與 LLVM IR 多目標。透過 mremap 實現 AoS 與 SoA 零拷貝即時切換；在極端低記憶體下自我否決 JIT，遁入零分配 Static Survival 避難所。",
        "JIT emitter targets C, Rust, Python, and LLVM IR. mremap enables zero-copy AoS <-> SoA morphing. Under stress, self-aware JIT vetoes compilation and routes traffic to Static Survival shelters."
    },
    {
        "ch07_qsbr_lockfree_hotswap.md",
        "第七章：QSBR 零鎖熱替換 (微秒級世代指針遷移與 Watchdog 隔離)",
        "Chapter 7: QSBR Lock-Free Hot-Swap (Sub-Microsecond Generational Migration)",
        "在每秒數千萬次請求的高並發伺服器中，獲取哪怕一把讀寫鎖（RWLock）都會造成災難性的快取一致性風暴。FLOW 採用統一 QSBR 無鎖架構，實現了讀取路徑零原子寫入、熱替換微秒級無損遷移。",
        "In servers processing millions of requests/sec, acquiring even one RWLock triggers catastrophic cache-line bouncing. FLOW QSBR achieves zero atomic writes on read paths and sub-microsecond migration.",
        "QSBR 透過靜態世代演進實現 >390M ops/s 讀取吞吐量。看門狗以 mprotect 隔離掉隊讀者執行緒，以客製化信號捕捉保證優雅降級與 0 丟失請求熱替換。",
        "QSBR achieves >390M ops/s read throughput. Epoch watchdogs isolate stragglers via mprotect, guaranteeing graceful degradation and 0 dropped requests during hot-swap."
    },
    {
        "ch08_hardware_primitive_drivers.md",
        "第八章：硬體原語驅動 (奧坎剃刀下的 3-Function 極簡 ABI 與具身物理閘門)",
        "Chapter 8: Hardware Primitive Drivers (3-Function Minimalist ABI & Embodied Gates)",
        "Plugin 不該是沉重的編譯器外掛，而只是大腦接在物理世界的視神經與肌肉。奧坎剃刀切除了一切非必要的 24 個回呼實體，只留下極簡的 3 個硬體原語驅動介面。",
        "Plugins are not bloated compiler extensions, but sensory organs and muscles. Occam's razor purges 24 callbacks, retaining 3 minimalist primitive driver hooks.",
        "極簡驅動 ABI 只需宣告硬體原語、呈報 SMT 物理邊界與執行調度。具身模組具備 1kHz 脊髓反射與 1Hz 皮層重構雙速率分離，ZMP 零力矩點物理閘門確保機器人永不倒地。",
        "Minimalist driver ABI declares primitives, reports SMT bounds, and executes syscalls. Embodied intelligence features dual-rate 1kHz/1Hz reflexes, with ZMP stability gates preventing robot tip-overs."
    },
    {
        "ch09_fvec_universal_lockfile.md",
        "第九章：大一統 .fvec 與 Universal Lockfile (架構權重庫、零秒冷啟動與抗體昇華)",
        "Chapter 9: Universal .fvec & Lockfiles (Architecture Weights, Zero-Cold-Start & Antibodies)",
        "大腦負責即時推論，而 .fvec 是大腦裡的長期記憶神經權重。它既是具備 SMT 物理簽章的 Universal Lockfile，也是在戰火中自主長出肌肉記憶的抗體庫。",
        "The brain infers in real time, while .fvec stores long-term architecture weights. It serves as a Universal Lockfile with SMT signatures and an immune antibody repository.",
        "大一統 .fvec 採用 1024-Byte 明文 ASCII 表頭 + CRC32 載荷。1ms 硬體親和度門禁杜絕跨架構無效套用；在線 100 萬次請求零錯誤與 SMT 認證觸發抗體昇華與赫布強化。",
        "Universal .fvec pairs a 1024-byte ASCII header with CRC32 binary payload. A 1ms affinity gate rejects incompatible architectures; 1M zero-error online requests trigger autonomous antibody promotion."
    },
    {
        "ch10_deterministic_flowy_reasoner.md",
        "第十章：決定論因果推論大腦 (0% 幻覺的架構自省、神經遙測熱點與職責分離)",
        "Chapter 10: Deterministic Flowy Reasoner (0% Hallucination Introspection & Decoupling)",
        "我們不需要一個會胡言亂語的機率型 Chatbot 來解釋系統架構。FLOW 打造了 Flowy——一個內建於二進位中、100% 決定論、零幻覺的代碼庫因果推論大腦。",
        "We do not need a hallucinating probabilistic chatbot to explain system architecture. FLOW built Flowy: a 100% deterministic, zero-hallucination causal codebase reasoner in pure C.",
        "Flowy 嚴格遵循大腦（flowy.c）、審計設施（audit.c）與前端表現層（flowy_cli.c）三層分離。即時解析決策因果（flowy why）、神經遙測熱點（flowy bottleneck）與反事實模擬。",
        "Flowy decouples Brain (flowy.c), Audit Logger (audit.c), and CLI Presentation (flowy_cli.c). It provides deterministic causal explanations (flowy why), telemetry diagnosis, and what-if simulation."
    },
    {
        "ch11_level5_crucible_and_benchmarks.md",
        "第十一章：Level 5 絕對死局壓測與效能基準 (並發風暴與 OOM 雙重束縛下的生存實錄)",
        "Chapter 11: Level 5 Crucible & Benchmarks (Kobayashi Maru & Production Replay)",
        "在科林丸號（Kobayashi Maru）測試中，學員面臨註定毀滅的死局。FLOW 打造了 Level 5 絕對死局壓測：當記憶體暴跌 99%、連線暴增 10,000 倍時，系統在死局中自我蛻變求生。",
        "In Kobayashi Maru double-bind crises where memory drops 99% amid 10,000x traffic surges, FLOW demonstrates Level-5 resilience via SMT veto, JIT shelter, and QSBR zero-downtime morphing.",
        "Level-5 熔爐壓測驗證了系統在雙重束縛下的 5 階段自癒過程，維持 0 封包丟失與 0 崩潰；QSBR 讀取吞吐達到 390M ops/s，通用鎖定檔套用耗時縮短至 37 微秒。",
        "The Level-5 crucible validates 5-stage autonomous self-healing with 0 dropped requests and 0 panics. QSBR achieves >390M ops/s read throughput and 37us instant cold-start application."
    }
};

static const size_t CHAPTER_COUNT = sizeof(ALL_CHAPTERS) / sizeof(ALL_CHAPTERS[0]);

typedef struct {
    const char *module_id;
    const char *chapter_ref;
} ModuleMap;

static const ModuleMap MODULE_BINDINGS[] = {
    { "flowc", "ch01_what_is_flow.md" },
    { "parser", "ch02_intent_vs_implementation.md" },
    { "semantic", "ch02_intent_vs_implementation.md" },
    { "topology", "ch03_topology_graph.md" },
    { "orchestrator", "ch03_topology_graph.md" },
    { "bitspace", "ch04_1bit_chaos_engine.md" },
    { "search", "ch04_1bit_chaos_engine.md" },
    { "swarm", "ch04_1bit_chaos_engine.md" },
    { "genetic", "ch04_1bit_chaos_engine.md" },
    { "smt", "ch05_smt_formal_verification.md" },
    { "verifier", "ch05_smt_formal_verification.md" },
    { "jit", "ch06_jit_and_geometric_morphing.md" },
    { "adaptive", "ch06_jit_and_geometric_morphing.md" },
    { "backend", "ch06_jit_and_geometric_morphing.md" },
    { "reload", "ch07_qsbr_lockfree_hotswap.md" },
    { "primitive", "ch08_hardware_primitive_drivers.md" },
    { "abi", "ch08_hardware_primitive_drivers.md" },
    { "registry", "ch08_hardware_primitive_drivers.md" },
    { "security", "ch08_hardware_primitive_drivers.md" },
    { "embodied", "ch08_hardware_primitive_drivers.md" },
    { "flowy_fvec", "ch09_fvec_universal_lockfile.md" },
    { "fvec", "ch09_fvec_universal_lockfile.md" },
    { "vault", "ch09_fvec_universal_lockfile.md" },
    { "flowy", "ch10_deterministic_flowy_reasoner.md" },
    { "flowy_cli", "ch10_deterministic_flowy_reasoner.md" },
    { "audit", "ch10_deterministic_flowy_reasoner.md" },
    { "benchmark", "ch11_level5_crucible_and_benchmarks.md" },
    { "gateway", "ch11_level5_crucible_and_benchmarks.md" },
    { "matching", "ch08_hardware_primitive_drivers.md" },
    { "cxl_fabric", "ch08_hardware_primitive_drivers.md" }
};

static const size_t MODULE_COUNT = sizeof(MODULE_BINDINGS) / sizeof(MODULE_BINDINGS[0]);

static const ChapterInfo *find_chapter(const char *ref) {
    for (size_t i = 0; i < CHAPTER_COUNT; ++i) {
        if (strcmp(ALL_CHAPTERS[i].chapter_ref, ref) == 0) {
            return &ALL_CHAPTERS[i];
        }
    }
    return &ALL_CHAPTERS[0];
}

static void escape_string(const char *src, FILE *out) {
    while (*src) {
        if (*src == '\\') {
            fputs("\\\\", out);
        } else if (*src == '"') {
            fputs("\\\"", out);
        } else if (*src == '\n') {
            fputs("\\n", out);
        } else {
            fputc(*src, out);
        }
        src++;
    }
}

int main(int argc, char **argv) {
    const char *out_path = (argc >= 2) ? argv[1] : "src/generated_book_knowledge.h";
    FILE *f = fopen(out_path, "w");
    if (!f) {
        fprintf(stderr, "Error opening %s for write\n", out_path);
        return 1;
    }

    fprintf(f, "/* AUTO-GENERATED BY tools/sync-flow-book.c - DO NOT EDIT MANUALLY */\n");
    fprintf(f, "#ifndef FLOW_GENERATED_BOOK_KNOWLEDGE_H\n");
    fprintf(f, "#define FLOW_GENERATED_BOOK_KNOWLEDGE_H\n\n");
    fprintf(f, "#include \"flowy.h\"\n");
    fprintf(f, "#include <string.h>\n\n");

    /* 1. Chapter Count */
    fprintf(f, "#define FLOW_BOOK_CHAPTER_COUNT %zu\n\n", CHAPTER_COUNT);

    /* 2. Structured Chapter Documentation */
    fprintf(f, "typedef struct {\n");
    fprintf(f, "    const char *chapter_ref;\n");
    fprintf(f, "    const char *chapter_title;\n");
    fprintf(f, "    const char *philosophy_why;\n");
    fprintf(f, "    const char *book_excerpt;\n");
    fprintf(f, "} FlowBookChapterDoc;\n\n");

    /* Chinese Chapters */
    fprintf(f, "static const FlowBookChapterDoc FLOW_BOOK_CHAPTERS_ZH[FLOW_BOOK_CHAPTER_COUNT] = {\n");
    for (size_t i = 0; i < CHAPTER_COUNT; ++i) {
        const ChapterInfo *c = &ALL_CHAPTERS[i];
        fprintf(f, "    {\n");
        fprintf(f, "        .chapter_ref = \"%s\",\n", c->chapter_ref);
        fprintf(f, "        .chapter_title = \""); escape_string(c->title_zh, f); fprintf(f, "\",\n");
        fprintf(f, "        .philosophy_why = \""); escape_string(c->why_zh, f); fprintf(f, "\",\n");
        fprintf(f, "        .book_excerpt = \""); escape_string(c->excerpt_zh, f); fprintf(f, "\"\n");
        fprintf(f, "    }%s\n", (i + 1 < CHAPTER_COUNT) ? "," : "");
    }
    fprintf(f, "};\n\n");

    /* English Chapters */
    fprintf(f, "static const FlowBookChapterDoc FLOW_BOOK_CHAPTERS_EN[FLOW_BOOK_CHAPTER_COUNT] = {\n");
    for (size_t i = 0; i < CHAPTER_COUNT; ++i) {
        const ChapterInfo *c = &ALL_CHAPTERS[i];
        fprintf(f, "    {\n");
        fprintf(f, "        .chapter_ref = \"%s\",\n", c->chapter_ref);
        fprintf(f, "        .chapter_title = \""); escape_string(c->title_en, f); fprintf(f, "\",\n");
        fprintf(f, "        .philosophy_why = \""); escape_string(c->why_en, f); fprintf(f, "\",\n");
        fprintf(f, "        .book_excerpt = \""); escape_string(c->excerpt_en, f); fprintf(f, "\"\n");
        fprintf(f, "    }%s\n", (i + 1 < CHAPTER_COUNT) ? "," : "");
    }
    fprintf(f, "};\n\n");

    /* 3. Module to Book Chapter Static Bindings */
    fprintf(f, "typedef struct {\n");
    fprintf(f, "    const char *module_id;\n");
    fprintf(f, "    const char *chapter_ref;\n");
    fprintf(f, "    const char *chapter_title;\n");
    fprintf(f, "    const char *philosophy_why;\n");
    fprintf(f, "    const char *book_excerpt;\n");
    fprintf(f, "} FlowModuleBookBinding;\n\n");

    /* Chinese Bindings */
    fprintf(f, "static const FlowModuleBookBinding FLOW_MODULE_BOOK_BINDINGS_ZH[%zu] = {\n", MODULE_COUNT);
    for (size_t i = 0; i < MODULE_COUNT; ++i) {
        const ModuleMap *m = &MODULE_BINDINGS[i];
        const ChapterInfo *c = find_chapter(m->chapter_ref);
        fprintf(f, "    {\n");
        fprintf(f, "        .module_id = \"%s\",\n", m->module_id);
        fprintf(f, "        .chapter_ref = \"%s\",\n", c->chapter_ref);
        fprintf(f, "        .chapter_title = \""); escape_string(c->title_zh, f); fprintf(f, "\",\n");
        fprintf(f, "        .philosophy_why = \""); escape_string(c->why_zh, f); fprintf(f, "\",\n");
        fprintf(f, "        .book_excerpt = \""); escape_string(c->excerpt_zh, f); fprintf(f, "\"\n");
        fprintf(f, "    }%s\n", (i + 1 < MODULE_COUNT) ? "," : "");
    }
    fprintf(f, "};\n\n");

    /* English Bindings */
    fprintf(f, "static const FlowModuleBookBinding FLOW_MODULE_BOOK_BINDINGS_EN[%zu] = {\n", MODULE_COUNT);
    for (size_t i = 0; i < MODULE_COUNT; ++i) {
        const ModuleMap *m = &MODULE_BINDINGS[i];
        const ChapterInfo *c = find_chapter(m->chapter_ref);
        fprintf(f, "    {\n");
        fprintf(f, "        .module_id = \"%s\",\n", m->module_id);
        fprintf(f, "        .chapter_ref = \"%s\",\n", c->chapter_ref);
        fprintf(f, "        .chapter_title = \""); escape_string(c->title_en, f); fprintf(f, "\",\n");
        fprintf(f, "        .philosophy_why = \""); escape_string(c->why_en, f); fprintf(f, "\",\n");
        fprintf(f, "        .book_excerpt = \""); escape_string(c->excerpt_en, f); fprintf(f, "\"\n");
        fprintf(f, "    }%s\n", (i + 1 < MODULE_COUNT) ? "," : "");
    }
    fprintf(f, "};\n\n");

    /* Helper Lookup Functions */
    fprintf(f, "static inline const FlowModuleBookBinding *flow_book_lookup_binding_lang(const char *module_id, FlowLanguage lang) {\n");
    fprintf(f, "    if (module_id == NULL) return NULL;\n");
    fprintf(f, "    const FlowModuleBookBinding *bindings = (lang == FLOW_LANG_EN) ? FLOW_MODULE_BOOK_BINDINGS_EN : FLOW_MODULE_BOOK_BINDINGS_ZH;\n");
    fprintf(f, "    for (size_t i = 0; i < %zu; ++i) {\n", MODULE_COUNT);
    fprintf(f, "        if (strcmp(bindings[i].module_id, module_id) == 0) return &bindings[i];\n");
    fprintf(f, "    }\n");
    fprintf(f, "    return NULL;\n");
    fprintf(f, "}\n\n");

    fprintf(f, "static inline const FlowModuleBookBinding *flow_book_lookup_binding(const char *module_id) {\n");
    fprintf(f, "    return flow_book_lookup_binding_lang(module_id, FLOW_LANG_ZH);\n");
    fprintf(f, "}\n\n");

    fprintf(f, "static inline const FlowBookChapterDoc *flow_book_lookup_chapter_lang(const char *ref_or_name, FlowLanguage lang) {\n");
    fprintf(f, "    if (ref_or_name == NULL) return NULL;\n");
    fprintf(f, "    const FlowBookChapterDoc *chapters = (lang == FLOW_LANG_EN) ? FLOW_BOOK_CHAPTERS_EN : FLOW_BOOK_CHAPTERS_ZH;\n");
    fprintf(f, "    for (size_t i = 0; i < FLOW_BOOK_CHAPTER_COUNT; ++i) {\n");
    fprintf(f, "        if (strcmp(chapters[i].chapter_ref, ref_or_name) == 0 || strstr(chapters[i].chapter_title, ref_or_name) != NULL) {\n");
    fprintf(f, "            return &chapters[i];\n");
    fprintf(f, "        }\n");
    fprintf(f, "    }\n");
    fprintf(f, "    return NULL;\n");
    fprintf(f, "}\n\n");

    fprintf(f, "static inline const FlowBookChapterDoc *flow_book_lookup_chapter(const char *ref_or_name) {\n");
    fprintf(f, "    return flow_book_lookup_chapter_lang(ref_or_name, FLOW_LANG_ZH);\n");
    fprintf(f, "}\n\n");

    fprintf(f, "#endif /* FLOW_GENERATED_BOOK_KNOWLEDGE_H */\n");
    fclose(f);
    printf("Generated bilingual %s with %zu chapters (ZH/EN) in 100%% pure native C.\n", out_path, CHAPTER_COUNT);
    return 0;
}
