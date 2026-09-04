#ifndef FLOW_GENERATED_KNOWLEDGE_H
#define FLOW_GENERATED_KNOWLEDGE_H

#include "flowy.h"

/* ------------------------------------------------------------------------- */
/* 1. Static Introspective Codebase Knowledge Base (Modules 0..16)           */
/* ------------------------------------------------------------------------- */

static const FlowModuleKnowledge CODEBASE_KNOWLEDGE[] = {
    {
        .module_id = "bitspace",
        .title = "BitManifold (BMF) / BitSpace & 3-Tier Mask Canvas",
        .header_file = "src/bitspace.h",
        .source_file = "src/bitspace.c",
        .layer = 0,
        .responsibilities = "Manages BitManifold (BMF) discrete manifolds, orthogonal polytope hypercube projections Pi_P({0,1}^N), constant-time O(1) 1-bit chaotic manifold transitions, 64-bit BitField Subspace Slicing (FLOW_GENOME_FIELD), and 3-Tier Dynamic Mask Canvas.",
        .algorithmic_guarantee = "O(1) 1-bit mutation (<2.5 ns/op) exploring Pareto frontiers on discrete manifolds; 1-cycle bitwise pruning eliminates 99.9% illegal states.",
        .memory_concurrency_model = "Native uint64_t register genome; zero heap allocation on search fast-path.",
        .key_apis = "flow_manifold_project, flow_manifold_transition, flow_polyhedron_project_mask, flow_mask_canvas_compose",
        .keywords = "bitmanifold bmf bitspace genome 1bit BMF mutation mask canvas polytope projection flow_manifold_project flow_manifold_transition 混沌 流形 幾何 遮罩 突變 退火"
    },
    {
        .module_id = "reload",
        .title = "Unified QSBR (Quiescent-State Based Reclamation) & Live Hot Reload",
        .header_file = "src/reload.h",
        .source_file = "src/reload.c",
        .layer = 0,
        .responsibilities = "Provides lock-free, zero-atomic-write RCU memory reclamation, sub-microsecond atomic pointer live migration, circular mutation audit trail, and offline reader immunity.",
        .algorithmic_guarantee = "Read throughput > 356M ops/s across 16 cores (24.1x faster than pthread_rwlock); hot-swap pointer switch < 1us (70-200ns).",
        .memory_concurrency_model = "64-byte cache-line aligned FlowReloadReader structs with false-sharing isolation buffer; memory_order_acquire/release atomics.",
        .key_apis = "flow_reload_call, flow_reload_begin, flow_reload_end, flow_reload_publish, flow_audit_replay",
        .keywords = "qsbr reload rcu lock-free lockfree atomic hot-swap hotswap audit trail snapshot cache-aligned quiescent reclamation 無鎖 无锁 讀寫 換熱 熱換 熱替換 热替换 記憶體回收 内存回收"
    },
    {
        .module_id = "orchestrator",
        .title = "Living Topology Orchestrator & State/Constraint Synthesizer",
        .header_file = "src/orchestrator.h",
        .source_file = "src/orchestrator.c",
        .layer = 0,
        .responsibilities = "Orchestrates the living codebase suite: Semantic Merge (flow absorb), Global Chaotic Annealing (flow anneal), Continuous Entropy Reduction (flow refactor), and State Time-Travel (flow morph).",
        .algorithmic_guarantee = "Mathematically detects intent mutual exclusions via SMT; minimizes global constraint energy and topological entropy continuously.",
        .memory_concurrency_model = "Global orchestrator state encapsulating multi-spec SemanticIR registry and epoch snapshots.",
        .key_apis = "flow_orchestrator_absorb, flow_orchestrator_anneal, flow_orchestrator_refactor_entropy, flow_orchestrator_landscape",
        .keywords = "orchestrator absorb anneal refactor landscape morph semantic merge living codebase 拓樸 吸收 退火 熵減 重構"
    },
    {
        .module_id = "embodied",
        .title = "Embodied Physical Intelligence & Robotics Plugin Interface",
        .header_file = "src/embodied.h",
        .source_file = "src/embodied.c",
        .layer = 0,
        .responsibilities = "Separates Mechanism from Policy: Ingests external physical policies (torque limits, ZMP stability, latency constraints) and translates them into BitSpace Dynamic Mask Canvases for the 1-bit chaotic engine.",
        .algorithmic_guarantee = "Newton-Euler dynamics simulation verifies torque limits and ZMP stability in < 2.5us; Kalman filter rejects sensor vibration spikes; 0W steady-state sleep.",
        .memory_concurrency_model = "Dual-rate frequency separation: high-speed 1kHz synchronous spinal tick + 1Hz asynchronous cortical reconfiguration.",
        .key_apis = "flow_physics_simulate_step, flow_physics_get_safety_mask, flow_dual_rate_spinal_tick, flow_sensor_fusion_update_imu, flow_energy_governor_check_wakeup",
        .keywords = "embodied physics sim-to-real robot robotics torque zmp spinal reflex kalman sensor fusion thermal energy 具身 機器人 物理 質心 關節 力矩 步態 抗震 防護"
    },
    {
        .module_id = "smt",
        .title = "Formal SMT-LIB2 Mathematical Proof Engine",
        .header_file = "src/smt.h",
        .source_file = "src/smt.c",
        .layer = 0,
        .responsibilities = "Generates and verifies mathematical theorems for Zero-Defect guarantees: Buffer Bounds Safety, Memory Quota Limits, Shard Non-Aliasing, and Functional Determinism.",
        .algorithmic_guarantee = "Outputs formal SMT-LIB2 QF_LIA theories; UNSAT guarantees zero runtime buffer overflows and strict shard isolation.",
        .memory_concurrency_model = "Deterministic string theorem synthesizer operating on SemanticIR constraints.",
        .key_apis = "flow_smt_prove, flow_smt_generate_proof_text, flow_smt_verify_theorem",
        .keywords = "smt proof prove proofs theorem theorems formal math z3 unsat formal verification 形式化 證明 數學 定理 最高法院 零缺陷"
    },
    {
        .module_id = "jit",
        .title = "Asynchronous JIT Compilation Pool",
        .header_file = "src/jit.h",
        .source_file = "src/jit.c",
        .layer = 0,
        .responsibilities = "Asynchronously compiles specialized native code in background worker threads without stalling the main execution thread. Physical constraint: requires_ram_mb > 100.",
        .algorithmic_guarantee = "Main-thread P99 call latency < 34us during live compilation (1029x lower than 35ms synchronous JIT blocking); forking compiler disabled when RAM < 100MB to prevent OOM.",
        .memory_concurrency_model = "Thread pool with lock-free job queue and condition-variable task dispatch; dual-mapped W^X pages.",
        .key_apis = "flow_jit_pool_create, flow_jit_pool_submit, flow_jit_compile_llvm_ir",
        .keywords = "jit async background compile worker pool latency thread requires_ram_mb watermark survival oom shelter backpressure 即時編譯 非同步 延遲 高水位 生存模式 避難所"
    },
    {
        .module_id = "adaptive",
        .title = "Dynamic Adaptation & Layout Morphing",
        .header_file = "src/adaptive.h",
        .source_file = "src/adaptive.c",
        .layer = 0,
        .responsibilities = "Collects eBPF/PMU telemetry and triggers instant zero-downtime memory layout morphing (AoS <-> SoA <-> Columnar) under memory pressure, with Golden Baseline fallback.",
        .algorithmic_guarantee = "96.9% RAM reduction under memory pressure (128MB -> 3.9MB); sub-microsecond fallback on 3 consecutive OOD errors.",
        .memory_concurrency_model = "Atomic telemetry counters and atomic golden baseline function pointer switch.",
        .key_apis = "flow_adaptive_observe_telemetry, flow_adaptive_check_morph, flow_adaptive_set_golden_baseline, flow_adaptive_fallback_to_golden_baseline",
        .keywords = "adaptive morph morphing soa aos columnar layout memory reduction golden baseline pmu ebpf 自適應 佈局 降解 遙測 幾何變形 變形 重映射 快取"
    },
    {
        .module_id = "security",
        .title = "Bounded Chaos Security & Moving Target Defense (MTD)",
        .header_file = "src/security.h",
        .source_file = "src/security.c",
        .layer = 0,
        .responsibilities = "Enforces Bounded Chaos compliance modes (FLOW_COMPLIANCE_STRICT_PROD), verifies cryptographic immutable mutation snapshots, and generates MTD polymorphic struct layouts.",
        .algorithmic_guarantee = "> 2.46 bits of Shannon layout entropy disrupts ROP gadget attacks with 0% runtime overhead; locks structural bits in production.",
        .memory_concurrency_model = "Cryptographic PRNG field permutation and canary offset verification.",
        .key_apis = "flow_security_get_compliance_mask, flow_security_verify_snapshot, flow_mtd_generate_polymorphic_layout",
        .keywords = "security mtd compliance audit snapshot polymorphic rpc rop safety defense attack randomization 動態靶標 防禦 合規 混淆 攻擊 隨機化"
    },
    {
        .module_id = "swarm",
        .title = "Swarm Intelligence & Federated Chaos Search",
        .header_file = "src/swarm.h",
        .source_file = "src/swarm.c",
        .layer = 0,
        .responsibilities = "Coordinates federated multi-particle chaotic exploration with pheromone diffusion and quantum-tunneling 2-bit jumps.",
        .algorithmic_guarantee = "Escapes deep Lorenz saddle points with > 92% escape rate; achieves consensus global Pareto optimum.",
        .memory_concurrency_model = "Array of independent particle genomes with asynchronous pheromone matrix diffusion.",
        .key_apis = "flow_swarm_init, flow_swarm_step, flow_swarm_get_consensus_mask",
        .keywords = "swarm particle federation pheromone saddle point quantum tunneling 粒子群 聯邦 費洛蒙 鞍點 群體智能"
    },
    {
        .module_id = "primitive",
        .title = "Hardware Primitive Drivers & Minimalist ABI",
        .header_file = "src/primitive.h",
        .source_file = "src/primitive.c",
        .layer = 1,
        .responsibilities = "Provides minimalist 3-function hardware primitive driver hooks (register_primitive, get_hardware_bounds, execute_primitive) interfacing to io_uring, RDMA, and eBPF Maps.",
        .algorithmic_guarantee = "Zero-overhead direct hardware and syscall dispatch; strict physical boundaries verified by SMT.",
        .memory_concurrency_model = "Pure hardware-boundary abstraction with zero compiler callback bloat.",
        .key_apis = "flow_primitive_register, flow_primitive_get_bounds, flow_primitive_execute",
        .keywords = "primitive driver io_uring rdma ebpf hardware bounds abi 原語 驅動 硬體 邊界"
    },
    {
        .module_id = "registry",
        .title = "Plugin Registry & Declarative Contracts",
        .header_file = "src/registry.h",
        .source_file = "src/registry.c",
        .layer = 1,
        .responsibilities = "Maintains domain component registry and synthesizes zero-C-callback extensions from FlowPluginContract descriptors.",
        .algorithmic_guarantee = "Auto-synthesizes dimension enumeration, verification, cost models, and hardware capabilities without manual C function pointers.",
        .memory_concurrency_model = "Thread-safe plugin lookup table and dynamic DSO module loader.",
        .key_apis = "flow_registry_register, flow_registry_lookup, flow_plugin_create_from_contract, flow_registry_load_dso",
        .keywords = "registry plugin declarative contract dso component extension 註冊 外掛 宣告式 合約 插件 模組"
    },
    {
        .module_id = "abi",
        .title = "Cross-Language Zero-Copy ABI Emitter",
        .header_file = "src/abi.h",
        .source_file = "src/abi.c",
        .layer = 1,
        .responsibilities = "Generates synchronized multi-language Zero-Copy ABI bindings: ANSI C headers, Safe Rust repr(C) crates, and Python memoryview wrappers.",
        .algorithmic_guarantee = "Zero memory copies across language boundaries with identical struct field alignments.",
        .memory_concurrency_model = "Standard C99/C11 struct layout matching Rust/Python FFI ABIs.",
        .key_apis = "flow_abi_emit_c_header, flow_abi_emit_rust_binding, flow_abi_emit_python_binding",
        .keywords = "abi ffi rust python zero-copy cross-language c bindings layout 接口 跨語言 綁定 介面 契約"
    },
    {
        .module_id = "topology",
        .title = "Codebase Architecture Knowledge Graph & Firewall Audit",
        .header_file = "src/topology.h",
        .source_file = "src/topology.c",
        .layer = 0,
        .responsibilities = "Maintains the 22-node architectural dependency graph, enforces clean layer separation firewalls, and audits modularity scores.",
        .algorithmic_guarantee = "Modularity = 1.00, Cross-Layer Leaks = 0, Upward Dependency Violations = 0.",
        .memory_concurrency_model = "Adjacency list directed acyclic graph representation.",
        .key_apis = "flow_topology_build_codebase_graph, flow_topology_audit_codebase, flow_topology_export_dot",
        .keywords = "topology graph architecture modularity leaks firewalls layers 拓樸 圖譜 架構 模組化 防火牆"
    },
    {
        .module_id = "audit",
        .title = "Global Decision Audit Logger & Telemetry Infrastructure",
        .header_file = "src/audit.h",
        .source_file = "src/audit.c",
        .layer = 0,
        .responsibilities = "Global ring-buffer decision audit logger recording timestamps, triggers, metrics, flipped genome bits, and QSBR grace periods across chaos, QSBR, and autopilot subsystems.",
        .algorithmic_guarantee = "Zero allocation on logging fast path; thread-safe circular event retention.",
        .memory_concurrency_model = "Circular buffer of 64 FlowDecisionEvent records with atomic monotonic head counter.",
        .key_apis = "flow_decision_logger_init, flow_decision_logger_record, flow_decision_logger_latest",
        .keywords = "audit logger decision timeline causal telemetry event 審計 日誌 決策 時間線 因果 遙測"
    },
    {
        .module_id = "flowy_fvec",
        .title = "Universal .fvec Architecture Repository & Vector Manifold",
        .header_file = "src/flowy_fvec.h",
        .source_file = "src/flowy_fvec.c",
        .layer = 2,
        .responsibilities = "Dual-layer .fvec format (1024-byte ASCII header + CRC32 binary payload), 16-D cosine similarity retrieval, 1ms hardware affinity gate, tidal morphing, cross-hardware transfer, and immune promotion.",
        .algorithmic_guarantee = "38ns cosine matching, 1ms preflight hardware affinity rejection, 100% SMT zero-defect theorem preservation.",
        .memory_concurrency_model = "Fixed-memory inverted index store with read-only shared payload mappings.",
        .key_apis = "flow_fvec_write_file, flow_fvec_read_file, flow_fvec_store_scan, flow_fvec_store_query, flow_fvec_auto_promote",
        .keywords = "fvec vector manifold lockfile affinity antibody tidal transfer immune promotion 特徵 向量 鎖定檔 親和度 抗體 肌肉記憶"
    },
    {
        .module_id = "flowy_cli",
        .title = "Flowy Presentation Layer, Formatters & Interactive REPL",
        .header_file = "src/flowy_cli.h",
        .source_file = "src/flowy_cli.c",
        .layer = 1,
        .responsibilities = "Decoupled presentation layer for terminal rendering, causal explanation formatters, The FLOW Book viewer, and interactive REPL shell loop.",
        .algorithmic_guarantee = "Data-Template separation, zero business logic contamination, pure formatting.",
        .memory_concurrency_model = "Stateless formatters writing to caller-provided FILE streams.",
        .key_apis = "flowy_print_answer, flowy_print_decision_explanation, flowy_show_book, flowy_interactive_loop",
        .keywords = "cli presentation format render repl shell interactive book 表現層 格式化 渲染 交互 終端 電子書"
    },
    {
        .module_id = "gateway",
        .title = "Self-Healing Autonomous Gateway (4-Mode Morphing Quartet)",
        .header_file = "src/gateway.h",
        .source_file = "src/gateway.c",
        .layer = 1,
        .responsibilities = "Integrates protocol primitives (HTTP/1, HTTP/2, HTTP/3 QUIC) with heterogeneous swarm mesh backpressure routing. Executes 4-mode online adaptive morphing and SMT timeout polytope DDoS mitigation.",
        .algorithmic_guarantee = "Zero-downtime QSBR protocol morphing in <200ns; SMT Slowloris connection pruning in <2.5us; 0 packet loss under 100k QPS bursts and 5% packet drop.",
        .memory_concurrency_model = "Lock-free atomic state transition with QSBR quiescent state checkpoints; zero heap allocation on request fast-path.",
        .key_apis = "flow_gateway_init, flow_gateway_adapt_entropy, flow_gateway_dispatch_request, flow_gateway_thwart_ddos, flow_gateway_verify_smt",
        .keywords = "gateway autonomous self-healing http1 http2 http3 quic ddos slowloris timeout polytope morphing 網關 自治 自愈 變形 防禦 零丟包"
    },
    {
        .module_id = "matching",
        .title = "Sub-Microsecond Financial Matching Engine & Price-Time Priority LOB",
        .header_file = "src/matching.h",
        .source_file = "src/matching.c",
        .layer = 0,
        .responsibilities = "High-frequency limit order book (LOB) executing FIFO price-time priority matching with pure fixed-point integer pricing (1e8 multiplier), zero heap allocations, non-arbitrage enforcement, and SMT conservation proofs.",
        .algorithmic_guarantee = "Tick-to-trade latency < 500ns (<50ns hot cache); SMT QF_LIA formal proofs for order book volume balance and non-arbitrage.",
        .memory_concurrency_model = "Contiguous ring-buffered pre-allocated slots; cache-line friendly layout; lock-free single-writer state.",
        .key_apis = "flow_matching_engine_init, flow_matching_submit_order, flow_matching_verify_smt, flow_primitive_matching_driver",
        .keywords = "matching orderbook lob hft price-time fifo fixed-point non-arbitrage financial 金融 撮合 訂單簿 限價單 微秒 無套利"
    },
    {
        .module_id = "cxl_fabric",
        .title = "Distributed LLM KV-Cache & 3-Tier CXL Memory Fabric",
        .header_file = "src/cxl_fabric.h",
        .source_file = "src/cxl_fabric.c",
        .layer = 0,
        .responsibilities = "Manages 3-tier disaggregated memory topology (Tier 0 HBM, Tier 1 DDR5, Tier 2 CXL 3.0 Pool) for LLM distributed inference. Implements 1-bit chaotic KV-cache eviction via attention entropy, zero-stall QSBR page migration, and SMT quota enforcement.",
        .algorithmic_guarantee = "HBM read < 10ns, DDR5 < 60ns, CXL pool < 200ns; QSBR page demotion without stalling active token generation; zero cross-session memory leaks verified by SMT.",
        .memory_concurrency_model = "Tier-tagged page directories with lock-free pointer migration under QSBR quiescent epochs.",
        .key_apis = "flow_cxl_fabric_init, flow_cxl_allocate_session, flow_cxl_read_page, flow_cxl_evict_chaos, flow_cxl_verify_smt, flow_primitive_cxl_driver",
        .keywords = "cxl hbm ddr5 kv-cache llm attention entropy eviction memory tiering qsbr 大模型 記憶體 記憶體池 階層 遷移 熵減"
    }
};

static const size_t KNOWLEDGE_COUNT = sizeof(CODEBASE_KNOWLEDGE) / sizeof(CODEBASE_KNOWLEDGE[0]);

/* ------------------------------------------------------------------------- */
/* 2. Bilingual Locale Templates (Data-Template Separation)                  */
/* ------------------------------------------------------------------------- */

typedef struct {
    const char *report_header;
    const char *label_module;
    const char *label_source_files;
    const char *label_title;
    const char *sec1_title;
    const char *sec2_title;
    const char *sec3_title;
    const char *sec4_title;
    const char *sec5_title;
    const char *sec6_title;
    const char *decision_header;
    const char *decision_causal_reasoning_title;
    const char *decision_book_title;
    const char *bottleneck_header;
    const char *bottleneck_sec1_title;
    const char *bottleneck_sec2_title;
    const char *bottleneck_sec3_title;
    const char *book_toc_header;
    const char *book_toc_footer;
    const char *book_doc_header;
    const char *book_doc_path;
    const char *book_doc_why;
    const char *book_doc_excerpt;
} FlowyLocaleTemplate;

static const FlowyLocaleTemplate LOCALE_TEMPLATES[2] = {
    [FLOW_LANG_ZH] = {
        .report_header = "=== FLOW 代碼庫內省式架構推論報告 ===",
        .label_module = "核心模組",
        .label_source_files = "原始碼檔案",
        .label_title = "子系統標題",
        .sec1_title = "1. 核心系統職責 (Core Responsibilities):",
        .sec2_title = "2. 演算法與形式化保證 (Algorithmic & Theoretical Guarantees):",
        .sec3_title = "3. 記憶體佈局與並發模型 (Memory Layout & Concurrency Model):",
        .sec4_title = "4. 關鍵權威 API (Key Authoritative APIs):",
        .sec5_title = "5. 💡 設計哲學與成因 (Why - 摘自《The FLOW Book》):",
        .sec6_title = "6. 📖 文檔拓樸章節索引與精華 (Book Chapter Reference & Excerpt):",
        .decision_header = "=== FLOW 即時決策與因果解釋 (Real-Time Causal Explanation) ===",
        .decision_causal_reasoning_title = "確定性因果推論 (打破物理黑盒子 / Deterministic Causal Reasoning):",
        .decision_book_title = "💡 《The FLOW Book》延伸閱讀與設計哲學 (Design Philosophy):",
        .bottleneck_header = "=== FLOW 下意識神經遙測與效能瓶頸報告 (Subconscious Telemetry) ===",
        .bottleneck_sec1_title = "1. 架構角色與定位 (Architectural Role):",
        .bottleneck_sec2_title = "2. 確定性診斷與成因推論 (Deterministic Diagnosis):",
        .bottleneck_sec3_title = "3. 自主修復機制 (Autonomous Remedy):",
        .book_toc_header = "               《The FLOW Book: 意圖驅動的活體系統》 全書目錄                     ",
        .book_toc_footer = "使用 'flowy book <chapter_number|module_name>' 閱讀特定章節摘錄與設計哲學。",
        .book_doc_header = "《The FLOW Book》 知識庫索引",
        .book_doc_path = "檔案位置",
        .book_doc_why = "💡 設計哲學 (Why):",
        .book_doc_excerpt = "📖 核心段落摘要 (Excerpt):"
    },
    [FLOW_LANG_EN] = {
        .report_header = "=== FLOW INTROSPECTIVE CODEBASE ARCHITECTURE REPORT ===",
        .label_module = "Module",
        .label_source_files = "Source Files",
        .label_title = "Title",
        .sec1_title = "1. CORE RESPONSIBILITIES:",
        .sec2_title = "2. ALGORITHMIC & THEORETICAL GUARANTEES:",
        .sec3_title = "3. MEMORY LAYOUT & CONCURRENCY MODEL:",
        .sec4_title = "4. KEY AUTHORITATIVE APIS:",
        .sec5_title = "5. 💡 DESIGN PHILOSOPHY & WHY (From 《The FLOW Book》):",
        .sec6_title = "6. 📖 BOOK CHAPTER REFERENCE & EXCERPT:",
        .decision_header = "=== FLOW INTROSPECTIVE REAL-TIME DECISION & CAUSAL EXPLANATION ===",
        .decision_causal_reasoning_title = "DETERMINISTIC CAUSAL REASONING (Breaking Physical Black-Box):",
        .decision_book_title = "💡 《The FLOW Book》 Further Reading & Design Philosophy:",
        .bottleneck_header = "=== FLOW SUBCONSCIOUS NEURAL TELEMETRY & BOTTLENECK REPORT ===",
        .bottleneck_sec1_title = "1. ARCHITECTURAL ROLE (From Codebase Knowledge):",
        .bottleneck_sec2_title = "2. DETERMINISTIC DIAGNOSIS & REASONING (For Humans):",
        .bottleneck_sec3_title = "3. AUTONOMOUS REMEDY (Zero Human Knobs Required):",
        .book_toc_header = "               《The FLOW Book: Autopoietic Living Systems》 Table of Contents    ",
        .book_toc_footer = "Use 'flowy book <chapter_number|module_name>' to view chapter excerpts and design philosophy.",
        .book_doc_header = "《The FLOW Book》 Knowledge Graph Index",
        .book_doc_path = "File Path",
        .book_doc_why = "💡 Design Philosophy (Why):",
        .book_doc_excerpt = "📖 Core Paragraph Excerpt:"
    }
};

#endif /* FLOW_GENERATED_KNOWLEDGE_H */
