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
        "第二章：意圖 vs. 實作 (.flow 檔案的本質：我們只宣告約束，不寫邏輯)",
        "Chapter 2: Intent vs. Implementation (.flow: We Declare Invariants, Not Logic)",
        "在 FLOW 的哲學中，撰寫程式碼不是告訴 CPU 如何一步一步執行指令，而是向宇宙宣告系統必須服從的幾何邊界與不變量。",
        "In FLOW philosophy, writing code is not telling the CPU step-by-step instructions, but declaring the geometric boundaries and mathematical invariants the universe must obey.",
        "一個標準的 .flow 描述檔是純宣告式的，不包含命令式迴圈或指標操作。開發者只宣告 input 規模、flow 流水線與物理約束，實作由引擎自動坍縮合成。",
        "A standard .flow spec is strictly declarative with no imperative loops or manual pointers. Developers declare input bounds and pipeline topology, letting the engine synthesize optimal implementations."
    },
    {
        "ch03_hello_chaos.md",
        "第三章：Hello, Chaos (第一個 FLOW 專案的運作原理與生命週期)",
        "Chapter 3: Hello, Chaos (Lifecycle and Mechanics of Your First FLOW Project)",
        "我們從一個最純粹的範例出發，親眼見證一份宣告式意圖如何透過 1-Bit 混沌引擎，在千分之五秒內自發坍縮成高效能的 Native C 程式碼。",
        "Starting from the purest example, witness how declarative intent spontaneously collapses into high-performance Native C code within 5 milliseconds via the 1-bit chaotic engine.",
        "flowc 編譯器以極速掃描 .flow 規格，生成抽象語意 IR，透過 1-bit 混沌退火在 12.96ns 步長下鎖定全局最優參數，並發射可執行的高質量 C 程式碼。",
        "flowc scans .flow specifications instantly into SemanticIR, locks the global optimal parameters via 1-bit chaotic annealing at 12.96ns/step, and emits production-ready Native C code."
    },
    {
        "ch04_topology_graph.md",
        "第四章：拓樸圖譜 (Topology Graph) (將程式碼降維成可推算的依賴約束)",
        "Chapter 4: Topology Graph (Dimensionality Reduction into Computable Invariants)",
        "軟體的架構不是文字檔目錄的堆疊，而是一張高維拓樸圖。在 FLOW 中，程式碼被降維成可直接進行圖論運算、親和性分析與遙測附著的活體神經圖譜。",
        "Software architecture is not a stack of directory text files, but a high-dimensional topology graph computed via graph theory, shard affinity, and neural telemetry.",
        "FLOW 將編譯器核心、外掛、元件與意圖統一建模為 FlowTopologyGraph，實現 Layer 0 到 Layer 4 嚴格邊界防火牆審計與零跨層滲漏保證。",
        "FLOW models compiler core, plugins, components, and intent as a unified FlowTopologyGraph, enforcing strict Layer 0 to Layer 4 firewalls with 0 cross-layer architectural leaks."
    },
    {
        "ch05_1bit_chaos_engine.md",
        "第五章：1-Bit 混沌退火引擎 (Xorshift、Mask Canva 與能量坍縮的數學原理)",
        "Chapter 5: 1-Bit Chaos Annealing Engine (Xorshift, Mask Canvas & Energy Collapse)",
        "在維度的詛咒面前，窮舉搜尋是死路一條。FLOW 採用 1-Bit 混沌退火，以 12.96 奈秒的極致步長，在 2^N 超維幾何流形中精確坍縮出全局最優解。",
        "In face of the curse of dimensionality, exhaustive search is futile. FLOW employs 1-bit chaotic annealing with 12.96ns step-size to collapse Pareto optima in 2^N hypercubes.",
        "1-Bit 狀態脊椎將系統維度壓縮為離散布林向量，透過三層遮罩畫布（安全硬閘門、遙測偏置、領域偏好）在 1 個時鐘週期內剔除 99.9% 非法狀態。",
        "The 1-Bit state spine encodes dimensions into discrete bit vectors, pruning 99.9% illegal states in a single clock cycle via the 3-Tier Mask Canvas."
    },
    {
        "ch06_smt_formal_verification.md",
        "第六章：SMT 形式化驗證 (編譯期的「最高法院」，Bit-Blasting 實作與硬約束拒絕)",
        "Chapter 6: SMT Formal Verification (The Supreme Court of Compilation & Bit-Blasting)",
        "啟發式搜尋可以天馬行空，但發射出的每一行機器碼必須擁有無可爭辯的數學證明。SMT 定理證明器是 FLOW 宇宙的最高法院，凡無證明者，一律否決。",
        "Heuristic search can explore freely, but every emitted line of machine code must have indisputable mathematical proofs. The SMT solver is the Supreme Court: unproven plans are vetoed.",
        "SMT 最高法院透過 QF_LIA 理論驗證緩衝區邊界、記憶體配額上限、分片隔離與確定性定理。任何違反 invariants 的候選遮罩一律被判定 UNSAT 並歸零機率偏置。",
        "SMT proves buffer bounds, memory quotas, shard isolation, and determinism via QF_LIA theories. Any candidate mask violating invariants is vetoed with zeroed probability bias."
    },
    {
        "ch07_qsbr_lockfree_hotswap.md",
        "第七章：QSBR 無鎖熱替換 (微秒級 Zero-Downtime 遷移的秘密)",
        "Chapter 7: QSBR Lock-Free Hot-Swap (The Secret to Sub-Microsecond Zero-Downtime Migration)",
        "在每秒數千萬次請求的高並發伺服器中，獲取哪怕一把讀寫鎖（RWLock）都會造成災難性的快取一致性風暴。FLOW 採用統一 QSBR 無鎖架構，實現了讀取路徑零原子寫入、熱替換微秒級無損遷移。",
        "In servers processing millions of requests/sec, acquiring even one RWLock triggers catastrophic cache-line bouncing. FLOW QSBR achieves zero atomic writes on read paths and sub-microsecond migration.",
        "QSBR 透過靜止狀態檢測與 64 位元世代演進實現 356M ops/s 讀取吞吐量。看門狗以 mprotect 隔離掉隊者並以客製化 sigaction 攔截 SIGSEGV 達成優雅降級與 0 丟失遷移。",
        "QSBR achieves 356M ops/s read throughput. Epoch watchdogs isolate stragglers via mprotect, with custom sigaction catching SIGSEGV to guarantee graceful degradation and 0 dropped requests."
    },
    {
        "ch08_memory_high_watermark_survival.md",
        "第八章：記憶體高水位與生存模式 (對抗 OOM 的背壓機制與 Static Survival 避難所)",
        "Chapter 8: Memory High-Watermark Survival (OOM Backpressure & Static Survival Refuge)",
        "當系統可用記憶體在 1 微秒內自 16GB 暴跌 99% 至 16MB，同時並發量飆升 10,000 倍時，愚蠢的系統會嘗試 JIT 編譯並被 Linux OOM-Killer 擊斃；有智慧的活體系統會自我否決 JIT，遁入零分配的靜態生存避難所。",
        "When available RAM plummets 99% to 16MB during a 10,000x traffic spike, naive systems attempt JIT and get killed by OOM-Killer. Living systems veto JIT and retreat to static survival shelters.",
        "自我意識 JIT 動態根據 AST 複雜度推算記憶體門檻。記憶體不足時自動否決編譯並路由指針至零分配靜態生存避難所，施密特觸發器遲滯區間有效防止邊界震盪。",
        "Self-aware JIT derives RAM requirements from AST complexity, vetoing compilation under stress and routing traffic to static zero-allocation modes with Schmitt-trigger anti-flapping."
    },
    {
        "ch09_geometric_morphing_aos_soa.md",
        "第九章：幾何變形 (AoS 到 SoA 的即時記憶體視圖切換與 mremap 應用)",
        "Chapter 9: Geometric Morphing (Instant AoS-to-SoA Memory View Switching via mremap)",
        "資料結構的本質，不是記憶體中的具體位元組排列，而是對物理空間的幾何投影。FLOW 透過虛擬記憶體重映射技術，實現微秒級的 AoS 到 SoA 零拷貝即時幾何變形。",
        "Data structures are not fixed byte layouts, but geometric projections onto physical space. FLOW achieves sub-microsecond zero-copy AoS <-> SoA geometric morphing via virtual memory remapping.",
        "在高並發隨機寫入時採用 AoS 獲得高局部性，在記憶體緊繃或批次掃描時瞬間重映射為 SoA 欄位壓縮，達成 96.9% 記憶體佔用縮減且零 TLB Shootdown 開銷。",
        "AoS offers write locality under high concurrency, while instant morphing to SoA columnar view delivers 96.9% RAM compression during memory pressure with zero TLB shootdowns."
    },
    {
        "ch10_meet_flowy.md",
        "第十章：認識 Flowy (拋棄 Chatbot，擁抱決定論式的 Codebase Reasoner)",
        "Chapter 10: Meet Flowy (Ditching Probabilistic Chatbots for Deterministic Codebase Reasoners)",
        "我們不需要一個會胡言亂語的機率型 Chatbot 來解釋系統架構。FLOW 打造了 Flowy——一個內建於二進位中、100% 決定論、零幻覺的代碼庫因果推論器。",
        "We do not need a hallucinating probabilistic chatbot to explain system architecture. FLOW built Flowy: a 100% deterministic, zero-hallucination causal codebase reasoner in pure C.",
        "Flowy 直接構建於 FlowTopologyGraph 與因果決策日誌之上。每次詢問均以純圖論遍歷與 SMT 不變量為依據，輸出具備完整形式化證明的代碼庫審計報告。",
        "Flowy operates directly on the FlowTopologyGraph and deterministic causal decision logs, producing formal architectural audit reports without relying on external cloud APIs."
    },
    {
        "ch11_semantic_reasoning_sandbox.md",
        "第十一章：語意推論與沙盤推演 (如何讓 Flowy 模擬與解釋架構變化)",
        "Chapter 11: Semantic Reasoning Sandbox (How Flowy Simulates and Explains Morphing)",
        "活體系統不僅要能自動適應，更要具備向人類解釋『為什麼做此決定』的因果表達能力，以及在不改動生產環境的前提下進行『如果...會怎樣』的反事實沙盤推演。",
        "Living systems must not only adapt autonomously, but possess causal explainability ('Why was this decision made?') and counterfactual what-if simulation capabilities.",
        "因果決策記錄器精確記錄每一次突變之觸發源、遙測極值與熱替換耗時。沙盤推演模組支援在零生產干擾下模擬記憶體限額調整後的拓樸坍縮效應與 Pareto 邊界偏移。",
        "The decision logger captures timestamps, telemetry anomalies, and hot-swap latency. Counterfactual sandbox simulates topological shifts and Pareto frontier changes under hypothetical memory limits."
    },
    {
        "ch12_dynamic_plugins_abi.md",
        "第十二章：動態外掛與 ABI 契約 (如何撰寫您的第一個 FLOW Plugin，從宣告到發射 LLVM IR)",
        "Chapter 12: Dynamic Plugins & ABI Contracts (Writing Your First FLOW Plugin & Emitting IR)",
        "一個偉大的活體系統必須具備無限擴展的生態。FLOW 定義了極簡的 Standardized Plugin ABI v2，將策略與機制徹底分離，讓任何 C/Rust 模組都能在 4 個函數內無縫融入活體超立方體。",
        "A great living system must possess boundless extensibility. FLOW ABI v2 decouples Mechanism from Policy, enabling any C/Rust module to integrate seamlessly via 4 pure functions.",
        "Plugin ABI v2 包含能力枚舉、成本模型評估、SMT 契約驗證與 IR 發射 4 個核心進入點。FLOW 核心提供退火搜尋機制，外掛只需定義自身領域策略。",
        "Plugin ABI v2 features 4 core function hooks: dimension enumeration, cost modeling, SMT contract verification, and IR emission, keeping domain policies cleanly decoupled from core mechanics."
    },
    {
        "ch13_overcoming_epistasis.md",
        "第十三章：跨越上位效應壁壘 (Epistasis、萊維飛行與動態機率偏移)",
        "Chapter 13: Overcoming Epistasis Barriers (Epistasis, Lévy Flights & Dynamic Probability Bias)",
        "當多個參數之間存在強烈的非線性耦合時，單獨改變任何一個參數只會讓系統變得更糟。FLOW 透過 SMT 基因連鎖圖譜與萊維飛行量子穿隧，徹底擊碎遺傳學中的上位效應壁壘。",
        "When parameters exhibit severe nonlinear coupling, changing any single parameter degrades fitness. FLOW shatters epistasis barriers via SMT linkage maps and Lévy flight quantum tunneling.",
        "在 AoS-1-Core 邁向 SoA-64-Core 的演化峽谷中，單點變異必然引發活結。FLOW 透過 SMT 形式化分析識別基因連鎖群，以超級位元原子多點翻轉與萊維飛行大步長跳躍瞬間穿隧鞍點。",
        "In the epistatic canyon between AoS-1-Core and SoA-64-Core, single-bit mutations cause livelocks. FLOW detects linkage groups via SMT and tunnels through saddles via multi-bit atomic flips."
    },
    {
        "ch14_swarm_intelligence.md",
        "第十四章：群體智能 (Swarm) (9-Byte UDP 拓樸費洛蒙與分散式尋優)",
        "Chapter 14: Swarm Intelligence (9-Byte UDP Topology Pheromones & Federated Search)",
        "分散式節點不需要龐大沉重的共識協議。FLOW 採用 9-Byte UDP 拓樸費洛蒙廣播，讓數千台邊緣節點如同蟻群般，自發協同湧現出全局最優拓樸。",
        "Distributed nodes do not need heavyweight consensus protocols. FLOW broadcasts 9-byte UDP pheromones, allowing thousands of edge nodes to spontaneously converge on global Pareto optima.",
        "9-Byte 費洛蒙結構包含 8 位元組 Genome 掩碼與 1 位元組標準化能量。費洛蒙在網狀網路中以指數蒸發與加權強化傳播，粒子群透過量子穿隧躍遷實現跨節點分散式尋優。",
        "The 9-byte pheromone embeds an 8-byte genome and 1-byte fitness. Pheromones propagate across mesh nodes with exponential evaporation, achieving fast decentralized Pareto convergence."
    },
    {
        "ch15_embodied_physical_gates.md",
        "第十五章：具身智能與物理閘門 (Embodied) (相延遲、史密斯預測器與 Sim-to-Real 降級)",
        "Chapter 15: Embodied Physical Intelligence & Gates (Phase Lag, Smith Predictors & Sim-to-Real)",
        "當軟體進駐機器人軀體，任何一個演算法錯誤都可能導致幾十公斤的鋼鐵軀體失去平衡摔毀。FLOW 建立了微物理零倒地保證、雙速率頻率分離與史密斯死區補償，築起堅不可摧的物理安全閘門。",
        "When software inhabits robotic bodies, algorithmic bugs cause real physical destruction. FLOW establishes Zero-Moment Point (ZMP) stability gates and dual-rate spinal reflexes to guarantee zero tip-over.",
        "具身模組採用 1kHz 脊髓反射與 1Hz 皮質重構之雙速率分離。卡爾曼濾波器與史密斯預測器消除關節相延遲與感測器震盪，熱過載時自發觸發零功耗安全休眠。",
        "Embodied intelligence separates high-speed 1kHz spinal reflexes from 1Hz cortical reconfiguration. Kalman filtering and Smith predictors compensate for actuator delay and thermal overloads."
    },
    {
        "ch16_level5_crucible_test.md",
        "第十六章：Level 5 絕對死局測試 (解析 Kobayashi Maru 壓測：並發風暴與 OOM 雙重束縛下的生存實錄)",
        "Chapter 16: Level 5 Crucible Audit (Kobayashi Maru: Surviving Concurrency & OOM Double-Bind)",
        "在雙重束縛的絕境中，系統同時面臨 99% 記憶體崩跌與 10,000 倍流量衝擊。FLOW 透過 SMT 否決、JIT 避難所、QSBR 50ms 零丟失熱換與遲滯復原，展示了 Level 5 自主生命的堅韌。",
        "In double-bind crises where memory drops 99% amid 10,000x traffic surges, FLOW demonstrates Level-5 resilience via SMT veto, JIT shelter, QSBR 50ms hot-swap, and Schmitt recovery.",
        "Level-5 熔爐壓測驗證了系統在極端惡意環境下的 5 階段自癒過程。全部決策均由圖論不變量與物理限制自主推導，全程 0 丟失請求、0 OOM 崩潰、0 人工參數介入。",
        "The Level-5 crucible test validates autonomous self-healing across 5 harsh stages. All decisions are mathematically derived with 0 dropped requests, 0 OOM panics, and 0 human knobs."
    },
    {
        "ch17_performance_benchmarks.md",
        "第十七章：效能評測 (為何 FLOW 的 JIT 產出能與 Firefox 引擎持平)",
        "Chapter 17: Performance Benchmarks (Why FLOW JIT Native Output Matches Top Industry Engines)",
        "數據是檢驗架構的唯一真理。在 6 大標準計算核心與極限並發吞吐測試中，FLOW JIT 產出的機器代碼展現了與手寫 C/Rust 及 Firefox SpiderMonkey 持平的極致效能。",
        "Empirical telemetry is the ultimate judge. Across 6 standard benchmarks, FLOW JIT emitted code matches handcrafted C/Rust and Firefox SpiderMonkey with near-zero branch miss rates.",
        "FLOW JIT 的優勢源於 1-Bit 混沌退火在編譯期精確鎖定硬體快取行與暫存器拓樸，徹底消除了多態分發開銷與冷快取行換入，實現真正的零抽象成本。",
        "FLOW JIT achieves zero-cost abstraction because 1-bit chaotic annealing precisely locks hardware cache-lines and register layouts at compile time, eliminating polymorphic dispatch."
    },
    {
        "ch18_hippocampus_long_term_memory.md",
        "第十八章：海馬迴長期記憶與 Canva-to-Vec (向量化、餘弦相似度與肌肉記憶的誕生)",
        "Chapter 18: Hippocampus Long-Term Memory & Canva-to-Vec (Vectorization, Cosine Similarity & Muscle Memory)",
        "過去的混沌引擎每次遇到新環境都必須從零開始盲目退火並繳納混沌稅；現在，FLOW 將遮罩畫布壓縮為 16 維潛在特徵向量，讓系統具備開箱即用的肌肉記憶。",
        "The 1-bit chaos engine once paid heavy chaos taxes solving cold starts from scratch; FLOW compresses canvases into 16-D embedding vectors, granting instant muscle memory.",
        "FLOW 建立前額葉（1-Bit 混沌）、海馬迴（16 維特徵庫）與脊髓神經（QSBR <100ns 熱換）三重架構。38 奈秒餘弦比對瞬間喚醒 SMT 認證之純 State，使 Serverless 具備 AOT 極速，並實現跨機隊 98.5% 算力節約的抗體共享。",
        "FLOW establishes a 3-tier cognitive model: prefrontal chaos, hippocampus 16-D vault, and spinal reflex. 38ns cosine matching retrieves SMT-certified pure states, slashing Serverless cold starts by 89.4% and sharing fleet-wide antibodies."
    },
    {
        "ch19_advanced_manifold_paradigms.md",
        "第十九章：高維流形四大進階典範 (潮汐形變、跨硬體移植、時序預熱與架構物種生成)",
        "Chapter 19: Advanced Manifold Paradigms (Tidal Morphing, Cross-Hardware Transfer, Predictive Pre-warming & Generative Architecture)",
        "架構不是靜止的石碑，而是高維連續流形上的流動曲面。FLOW 讓系統具備潮汐般的呼吸、跨硬體的遺傳、未卜先知的預熱，以及自我繁衍全新架構物種的生命特質。",
        "Architecture is not a static monolith, but a dynamic surface on high-dimensional manifolds. FLOW brings tidal breathing, cross-hardware genetics, anticipatory pre-warming, and generative species synthesis.",
        "向量空間內插結合公共硬安全多面體實現零斷崖日夜潮汐過渡；FLOW_DNA_V1 跨 ISA 自適應實現 95% 置信度零次學習；卡爾曼預測器提前 300 秒預熱 JIT 消除突發延遲；朗之萬 5 步擴散去噪採樣合成帶有 4/4 SMT UNSAT 形式化證明的全新拓樸物種。",
        "Vector interpolation on safety polytopes enables cliff-free tidal transitions; FLOW_DNA_V1 enables 95% confidence zero-shot ISA transfer; Kalman forecasting eliminates latency spikes via proactive JIT pre-warming; Langevin diffusion synthesizes novel SMT-certified species."
    }
};

static const size_t CHAPTER_COUNT = sizeof(ALL_CHAPTERS) / sizeof(ALL_CHAPTERS[0]);

typedef struct {
    const char *module_id;
    const char *chapter_ref;
} ModuleMap;

static const ModuleMap MODULE_BINDINGS[] = {
    { "bitspace", "ch05_1bit_chaos_engine.md" },
    { "reload", "ch07_qsbr_lockfree_hotswap.md" },
    { "orchestrator", "ch04_topology_graph.md" },
    { "embodied", "ch15_embodied_physical_gates.md" },
    { "smt", "ch06_smt_formal_verification.md" },
    { "jit", "ch08_memory_high_watermark_survival.md" },
    { "adaptive", "ch09_geometric_morphing_aos_soa.md" },
    { "security", "ch12_dynamic_plugins_abi.md" },
    { "swarm", "ch14_swarm_intelligence.md" },
    { "genetic", "ch13_overcoming_epistasis.md" },
    { "registry", "ch12_dynamic_plugins_abi.md" },
    { "abi", "ch12_dynamic_plugins_abi.md" },
    { "topology", "ch04_topology_graph.md" },
    { "flowy", "ch10_meet_flowy.md" },
    { "parser", "ch02_intent_vs_implementation.md" },
    { "semantic", "ch02_intent_vs_implementation.md" },
    { "verifier", "ch06_smt_formal_verification.md" },
    { "backend", "ch03_hello_chaos.md" },
    { "benchmark", "ch17_performance_benchmarks.md" },
    { "flowc", "ch01_what_is_flow.md" },
    { "vault", "ch18_hippocampus_long_term_memory.md" },
    { "fvec", "ch18_hippocampus_long_term_memory.md" }
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

    fprintf(f, "/* AUTO-GENERATED BY tools/sync-flow-book.c (Pure C17 Autopoiesis) - DO NOT EDIT DIRECTLY */\n");
    fprintf(f, "#ifndef FLOW_GENERATED_BOOK_KNOWLEDGE_H\n");
    fprintf(f, "#define FLOW_GENERATED_BOOK_KNOWLEDGE_H\n\n");
    fprintf(f, "#include <stddef.h>\n");
    fprintf(f, "#include <string.h>\n\n");

    fprintf(f, "#ifndef FLOW_LANG_ENUM_DEFINED\n");
    fprintf(f, "#define FLOW_LANG_ENUM_DEFINED\n");
    fprintf(f, "typedef enum {\n");
    fprintf(f, "    FLOW_LANG_ZH = 0, /* Traditional Chinese (預設 / Default) */\n");
    fprintf(f, "    FLOW_LANG_EN = 1  /* English */\n");
    fprintf(f, "} FlowLanguage;\n");
    fprintf(f, "#endif\n\n");

    fprintf(f, "typedef struct {\n");
    fprintf(f, "    const char *chapter_ref;\n");
    fprintf(f, "    const char *chapter_title;\n");
    fprintf(f, "    const char *philosophy_why;\n");
    fprintf(f, "    const char *book_excerpt;\n");
    fprintf(f, "} FlowBookChapterDoc;\n\n");

    fprintf(f, "typedef struct {\n");
    fprintf(f, "    const char *module_id;\n");
    fprintf(f, "    const char *chapter_ref;\n");
    fprintf(f, "    const char *chapter_title;\n");
    fprintf(f, "    const char *philosophy_why;\n");
    fprintf(f, "    const char *book_excerpt;\n");
    fprintf(f, "} FlowModuleBookBinding;\n\n");

    fprintf(f, "#define FLOW_BOOK_CHAPTER_COUNT %zu\n", CHAPTER_COUNT);
    fprintf(f, "#define FLOW_MODULE_BOOK_BINDING_COUNT %zu\n\n", MODULE_COUNT);

    /* Traditional Chinese (ZH) Chapters */
    fprintf(f, "static const FlowBookChapterDoc FLOW_BOOK_CHAPTERS_ZH[FLOW_BOOK_CHAPTER_COUNT] = {\n");
    for (size_t i = 0; i < CHAPTER_COUNT; ++i) {
        const ChapterInfo *c = &ALL_CHAPTERS[i];
        fprintf(f, "    {\n");
        fprintf(f, "        .chapter_ref = \"%s\",\n", c->chapter_ref);
        fprintf(f, "        .chapter_title = \"");
        escape_string(c->title_zh, f);
        fprintf(f, "\",\n        .philosophy_why = \"");
        escape_string(c->why_zh, f);
        fprintf(f, "\",\n        .book_excerpt = \"");
        escape_string(c->excerpt_zh, f);
        fprintf(f, "\"\n    }%s\n", (i + 1 < CHAPTER_COUNT) ? "," : "");
    }
    fprintf(f, "};\n\n");

    /* English (EN) Chapters */
    fprintf(f, "static const FlowBookChapterDoc FLOW_BOOK_CHAPTERS_EN[FLOW_BOOK_CHAPTER_COUNT] = {\n");
    for (size_t i = 0; i < CHAPTER_COUNT; ++i) {
        const ChapterInfo *c = &ALL_CHAPTERS[i];
        fprintf(f, "    {\n");
        fprintf(f, "        .chapter_ref = \"%s\",\n", c->chapter_ref);
        fprintf(f, "        .chapter_title = \"");
        escape_string(c->title_en, f);
        fprintf(f, "\",\n        .philosophy_why = \"");
        escape_string(c->why_en, f);
        fprintf(f, "\",\n        .book_excerpt = \"");
        escape_string(c->excerpt_en, f);
        fprintf(f, "\"\n    }%s\n", (i + 1 < CHAPTER_COUNT) ? "," : "");
    }
    fprintf(f, "};\n\n");

    fprintf(f, "#define FLOW_BOOK_CHAPTERS FLOW_BOOK_CHAPTERS_ZH\n\n");

    /* Traditional Chinese (ZH) Module Bindings */
    fprintf(f, "static const FlowModuleBookBinding FLOW_MODULE_BOOK_BINDINGS_ZH[FLOW_MODULE_BOOK_BINDING_COUNT] = {\n");
    for (size_t i = 0; i < MODULE_COUNT; ++i) {
        const ModuleMap *m = &MODULE_BINDINGS[i];
        const ChapterInfo *c = find_chapter(m->chapter_ref);
        fprintf(f, "    {\n");
        fprintf(f, "        .module_id = \"%s\",\n", m->module_id);
        fprintf(f, "        .chapter_ref = \"%s\",\n", m->chapter_ref);
        fprintf(f, "        .chapter_title = \"");
        escape_string(c->title_zh, f);
        fprintf(f, "\",\n        .philosophy_why = \"");
        escape_string(c->why_zh, f);
        fprintf(f, "\",\n        .book_excerpt = \"");
        escape_string(c->excerpt_zh, f);
        fprintf(f, "\"\n    }%s\n", (i + 1 < MODULE_COUNT) ? "," : "");
    }
    fprintf(f, "};\n\n");

    /* English (EN) Module Bindings */
    fprintf(f, "static const FlowModuleBookBinding FLOW_MODULE_BOOK_BINDINGS_EN[FLOW_MODULE_BOOK_BINDING_COUNT] = {\n");
    for (size_t i = 0; i < MODULE_COUNT; ++i) {
        const ModuleMap *m = &MODULE_BINDINGS[i];
        const ChapterInfo *c = find_chapter(m->chapter_ref);
        fprintf(f, "    {\n");
        fprintf(f, "        .module_id = \"%s\",\n", m->module_id);
        fprintf(f, "        .chapter_ref = \"%s\",\n", m->chapter_ref);
        fprintf(f, "        .chapter_title = \"");
        escape_string(c->title_en, f);
        fprintf(f, "\",\n        .philosophy_why = \"");
        escape_string(c->why_en, f);
        fprintf(f, "\",\n        .book_excerpt = \"");
        escape_string(c->excerpt_en, f);
        fprintf(f, "\"\n    }%s\n", (i + 1 < MODULE_COUNT) ? "," : "");
    }
    fprintf(f, "};\n\n");

    fprintf(f, "#define FLOW_MODULE_BOOK_BINDINGS FLOW_MODULE_BOOK_BINDINGS_ZH\n\n");

    /* Helper Lookup Functions */
    fprintf(f, "static inline const FlowModuleBookBinding *flow_book_lookup_binding_lang(const char *module_id, FlowLanguage lang) {\n");
    fprintf(f, "    if (module_id == NULL) return NULL;\n");
    fprintf(f, "    const FlowModuleBookBinding *bindings = (lang == FLOW_LANG_EN) ? FLOW_MODULE_BOOK_BINDINGS_EN : FLOW_MODULE_BOOK_BINDINGS_ZH;\n");
    fprintf(f, "    for (size_t i = 0; i < FLOW_MODULE_BOOK_BINDING_COUNT; ++i) {\n");
    fprintf(f, "        if (strcmp(bindings[i].module_id, module_id) == 0) {\n");
    fprintf(f, "            return &bindings[i];\n");
    fprintf(f, "        }\n");
    fprintf(f, "    }\n");
    fprintf(f, "    return NULL;\n");
    fprintf(f, "}\n\n");

    fprintf(f, "static inline const FlowModuleBookBinding *flow_book_lookup_binding(const char *module_id) {\n");
    fprintf(f, "    return flow_book_lookup_binding_lang(module_id, FLOW_LANG_ZH);\n");
    fprintf(f, "}\n\n");

    fprintf(f, "static inline const FlowBookChapterDoc *flow_book_lookup_chapter_lang(const char *ref_or_index, FlowLanguage lang) {\n");
    fprintf(f, "    if (ref_or_index == NULL) return NULL;\n");
    fprintf(f, "    const FlowBookChapterDoc *chapters = (lang == FLOW_LANG_EN) ? FLOW_BOOK_CHAPTERS_EN : FLOW_BOOK_CHAPTERS_ZH;\n");
    fprintf(f, "    for (size_t i = 0; i < FLOW_BOOK_CHAPTER_COUNT; ++i) {\n");
    fprintf(f, "        if (strcmp(chapters[i].chapter_ref, ref_or_index) == 0 ||\n");
    fprintf(f, "            strstr(chapters[i].chapter_title, ref_or_index) != NULL) {\n");
    fprintf(f, "            return &chapters[i];\n");
    fprintf(f, "        }\n");
    fprintf(f, "    }\n");
    fprintf(f, "    return NULL;\n");
    fprintf(f, "}\n\n");

    fprintf(f, "static inline const FlowBookChapterDoc *flow_book_lookup_chapter(const char *ref_or_index) {\n");
    fprintf(f, "    return flow_book_lookup_chapter_lang(ref_or_index, FLOW_LANG_ZH);\n");
    fprintf(f, "}\n\n");

    fprintf(f, "#endif /* FLOW_GENERATED_BOOK_KNOWLEDGE_H */\n");
    fclose(f);

    printf("Generated bilingual %s with %zu chapters (ZH/EN) in 100%% pure native C.\n", out_path, CHAPTER_COUNT);
    return 0;
}
