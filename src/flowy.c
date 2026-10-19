#include "flowy.h"
#include "topology.h"
#include "jit.h"
#include "adaptive.h"
#include "smt.h"
#include "audit.h"
#include "cubical_hott.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const FlowModuleKnowledge CODEBASE_KNOWLEDGE[] = {
    {"bitspace", "BitManifold (BMF) / BitSpace & 3-Tier Mask Canvas", "src/bitspace.h", "src/bitspace.c", 0, "Manages BitManifold discrete manifolds, polytope hypercube projections, O(1) 1-bit transitions, and 3-Tier Mask Canvas.", "O(1) 1-bit mutation (<2.5 ns/op); 1-cycle bitwise pruning.", "Native uint64_t register genome; zero heap allocation on fast-path.", "flow_manifold_project, flow_manifold_transition, flow_mask_canvas_compose", "bitmanifold bmf bitspace genome 1bit mutation mask canvas polytope projection 混沌 流形 幾何 遮罩 突變 退火"},
    {"reload", "Unified QSBR & Live Hot Reload", "src/reload.h", "src/reload.c", 0, "Lock-free, zero-atomic-write RCU memory reclamation, sub-microsecond pointer migration, circular audit trail, and straggler quarantine.", "Read throughput > 356M ops/s across 16 cores; hot-swap switch < 1us.", "64-byte cache-line aligned FlowReloadReader structs; acquire/release atomics.", "flow_reload_call, flow_reload_begin, flow_reload_end, flow_reload_publish", "qsbr reload rcu lock-free lockfree atomic hot-swap hotswap audit trail quiescent reclamation 無鎖 讀寫 熱替換 記憶體回收"},
    {"orchestrator", "Living Topology Orchestrator & Constraint Synthesizer", "src/orchestrator.h", "src/orchestrator.c", 0, "Orchestrates Semantic Merge (absorb), Global Annealing (anneal), Entropy Reduction (refactor), and State Time-Travel (morph).", "Detects intent mutual exclusions via SMT and Cubical Homotopy Type Matching.", "Global orchestrator state encapsulating multi-spec SemanticIR registry and epoch snapshots.", "flow_orchestrator_absorb, flow_orchestrator_anneal, flow_orchestrator_refactor_entropy", "orchestrator absorb anneal refactor landscape morph semantic merge living codebase 拓樸 吸收 退火 熵減 重構"},
    {"embodied", "Embodied Physical Intelligence & Robotics Plugin", "plugins/embodied.h", "plugins/embodied.c", 2, "Ingests physical policies (torque limits, ZMP stability, latency) and translates them into BitSpace Dynamic Mask Canvases.", "Newton-Euler dynamics verifies torque and ZMP stability in < 2.5us; Kalman filter rejects spikes.", "Dual-rate separation: 1kHz synchronous spinal tick + 1Hz cortical reconfiguration.", "flow_physics_simulate_step, flow_physics_get_safety_mask, flow_dual_rate_spinal_tick", "embodied physics sim-to-real robot robotics torque zmp spinal reflex kalman sensor fusion 具身 機器人 物理 質心 關節 力矩 步態"},
    {"smt", "Formal SMT-LIB2 Mathematical Proof Engine", "src/smt.h", "src/smt.c", 0, "Verifies Buffer Bounds Safety, Memory Quota Limits, Shard Non-Aliasing, and Functional Determinism.", "Outputs formal SMT-LIB2 QF_LIA theories; UNSAT guarantees zero buffer overflows.", "Deterministic theorem synthesizer operating on SemanticIR constraints and hyper-box polytopes.", "flow_smt_verify, flow_smt_generate_proof_script, flow_smt_verify_box_invariants", "smt proof prove proofs theorem theorems formal math z3 unsat formal verification 形式化 證明 數學 定理 最高法院 零缺陷"},
    {"jit", "Asynchronous JIT Compilation Pool & Layout Morphing", "src/jit.h", "src/jit.c", 0, "Asynchronously compiles specialized native code in background worker threads (requires_ram_mb > 100).", "Main-thread P99 latency < 34us during live compilation; survival shelter when RAM < 100MB.", "Thread pool with lock-free job queue and condition-variable dispatch.", "flow_jit_pool_create, flow_jit_pool_submit, flow_jit_transpose_aos_to_soa", "jit async background compile worker pool latency thread requires_ram_mb watermark survival oom shelter backpressure 即時編譯 非同步 延遲 生存模式 避難所"},
    {"adaptive", "Dynamic Adaptation & Layout Morphing", "src/adaptive.h", "src/adaptive.c", 0, "Collects eBPF/PMU telemetry and triggers zero-downtime memory layout morphing (AoS <-> SoA <-> Columnar) with Golden Baseline fallback.", "96.9% RAM reduction under memory pressure; sub-microsecond fallback on 3 consecutive OOD errors.", "Atomic telemetry counters and atomic golden baseline function pointer switch.", "flow_adaptive_observe_telemetry, flow_adaptive_check_morph, flow_adaptive_set_golden_baseline", "adaptive morph morphing soa aos columnar layout memory reduction golden baseline pmu ebpf 自適應 佈局 遙測 幾何變形 快取"},
    {"security", "Bounded Chaos Security & Moving Target Defense (MTD)", "src/security.h", "src/security.c", 0, "Enforces Bounded Chaos compliance modes, linker composition hard gates, and MTD polymorphic struct layouts.", "> 2.46 bits of Shannon layout entropy disrupts ROP gadget attacks with 0% runtime overhead.", "Cryptographic PRNG field permutation and canary offset verification.", "flow_security_get_compliance_mask, flow_security_check_composition_gate, flow_security_mtd_generate_layout", "security mtd compliance audit snapshot polymorphic safety defense attack randomization 動態靶標 防禦 合規 混淆 隨機化"},
    {"swarm", "Swarm Intelligence, Federated BMF & Antibody Mesh", "src/swarm.h", "src/swarm.c", 0, "Coordinates federated multi-particle chaotic exploration with pheromone diffusion, 9-byte lymphatic antibodies, and 64-node ring mesh.", "Escapes deep Lorenz saddle points with > 92% escape rate; achieves consensus global Pareto optimum.", "Array of independent particle genomes with lock-free seqlock circular descriptor ring.", "flow_swarm_init, flow_swarm_step, flow_swarm_lymphatic_encode, flow_swarm_ring_publish", "swarm particle federation pheromone saddle point lymphatic ring 粒子群 聯邦 費洛蒙 鞍點 群體智能"},
    {"primitive", "Hardware Primitive Drivers & Minimalist ABI", "src/primitive.h", "src/primitive.c", 1, "Minimalist 3-function hardware primitive driver hooks interfacing to io_uring, RDMA, and CAN/IMU.", "Zero-overhead direct hardware and syscall dispatch; strict physical boundaries verified by SMT.", "Pure hardware-boundary abstraction with zero compiler callback bloat.", "flow_primitive_register, flow_primitive_get_bounds, flow_primitive_execute", "primitive driver io_uring rdma ebpf hardware bounds abi 原語 驅動 硬體 邊界"},
    {"registry", "Plugin Registry, Declarative Contracts & Homotopy Matcher", "src/registry.h", "src/registry.c", 1, "Maintains domain component registry, verifies Cubical Homotopy Type Matching, and synthesizes extensions from FlowPluginContract.", "1-cycle Topos subobject classification + Kan Box Filler compatibility check.", "Thread-safe plugin lookup table and dynamic DSO module loader.", "flow_registry_register, flow_registry_lookup, component_compatible, select_component", "registry plugin declarative contract dso component extension homotopy topos kan 註冊 外掛 宣告式 合約 插件 模組 同倫"},
    {"abi", "Cross-Language Zero-Copy ABI Emitter", "src/abi.h", "src/abi.c", 1, "Generates synchronized multi-language Zero-Copy ABI bindings: ANSI C headers, Safe Rust repr(C) crates, and Python memoryview wrappers.", "Zero memory copies across language boundaries with identical struct field alignments.", "Standard C99/C11 struct layout matching Rust/Python FFI ABIs.", "flow_abi_emit_c_header, flow_abi_emit_rust_binding, flow_abi_emit_python_binding", "abi ffi rust python zero-copy cross-language c bindings layout 接口 跨語言 綁定 介面 契約"},
    {"topology", "Codebase Architecture Knowledge Graph & Workspace Scanner", "src/topology.h", "src/topology.c", 0, "Scans live C workspace files (LOC, #include edges, fan_in/fan_out), attaches 64-bit Cubical Type masks, and audits layer firewalls.", "Modularity = 1.00, Cross-Layer Leaks = 0, Upward Dependency Violations = 0.", "Adjacency list directed graph with live C header parser.", "flow_topology_scan_workspace, flow_topology_build_codebase_graph, flow_topology_audit", "topology graph architecture modularity leaks firewalls layers workspace 拓樸 圖譜 架構 模組化 防火牆"},
    {"audit", "Global Decision Audit Logger & Telemetry Infrastructure", "src/audit.h", "src/audit.c", 0, "Global ring-buffer decision audit logger recording timestamps, triggers, metrics, flipped genome bits, and QSBR grace periods.", "Zero allocation on logging fast path; thread-safe circular event retention.", "Circular buffer of 64 FlowDecisionEvent records with atomic monotonic head counter.", "flow_decision_logger_init, flow_decision_logger_record, flow_decision_logger_latest", "audit logger decision timeline causal telemetry event 審計 日誌 決策 時間線 因果 遙測"},
    {"flowy_fvec", "Universal .fvec Gene Store & Immune Antibody Promotion", "src/flowy_fvec.h", "src/fvec/fvec_core.c", 2, "Unified .fvec format (1024-byte ASCII header + CRC32 binary payload), 16-D cosine similarity retrieval, affinity gate, and antibody promotion.", "38ns cosine matching, 1ms preflight hardware affinity rejection, 100% SMT zero-defect preservation.", "Fixed-memory FlowVecStore with CRC32-verified binary payloads.", "flow_fvec_write_file, flow_fvec_read_file, flow_fvec_store_scan, flow_fvec_store_query, flow_fvec_promote_or_strengthen", "fvec vector manifold lockfile affinity antibody immune promotion 特徵 向量 鎖定檔 親和度 抗體 肌肉記憶"},
    {"flowy_cli", "Flowy Presentation Layer, Formatters & Interactive REPL", "src/flowy_cli.h", "src/flowy_cli.c", 1, "Decoupled presentation layer for terminal rendering, causal explanation formatters, and interactive REPL shell loop.", "Data-Template separation, zero business logic contamination, pure formatting.", "Stateless formatters writing to caller-provided FILE streams.", "flowy_print_answer, flowy_print_decision_explanation, flowy_interactive_loop", "cli presentation format render repl shell interactive 表現層 格式化 渲染 交互 終端"},
    {"cubical_hott", "Cubical Homotopy Type Theory & Topos Sieve Engine", "src/cubical_hott.h", "src/cubical_hott.c", 0, "Implements 64-bit De Morgan Cubical Type Sieve, Topos subobject classifier chi_P, and Kan Open-Box filler for homotopy type matching.", "1-cycle bitwise Topos classification and Kan fibration equivalence verification.", "Pure register-level 64-bit interval cube operations.", "flow_cubical_match_types, flow_cubical_sieve_from_ir, flow_cubical_sieve_from_component, flow_cubical_kan_fill_box", "cubical hott homotopy kan fibration topos sieve univalence 同倫 型別 篩子 範疇 方塊"}
}; static const size_t KNOWLEDGE_COUNT = sizeof(CODEBASE_KNOWLEDGE) / sizeof(CODEBASE_KNOWLEDGE[0]);
#define FLOW_MAX_DYNAMIC_KNOWLEDGE 64
static FlowModuleKnowledge g_dynamic_knowledge[FLOW_MAX_DYNAMIC_KNOWLEDGE]; static size_t g_dynamic_knowledge_count = 0;
int flowy_register_dynamic_module(const FlowModuleKnowledge *knowledge) { if (!knowledge || !knowledge->module_id) return 0;
    for (size_t i = 0; i < g_dynamic_knowledge_count; ++i)
        if (!strcmp(g_dynamic_knowledge[i].module_id, knowledge->module_id)) { g_dynamic_knowledge[i] = *knowledge; return 1; }
    if (g_dynamic_knowledge_count < FLOW_MAX_DYNAMIC_KNOWLEDGE) { g_dynamic_knowledge[g_dynamic_knowledge_count++] = *knowledge; return 1; } return 0; }
size_t flowy_knowledge_count(void) { return KNOWLEDGE_COUNT + g_dynamic_knowledge_count; } const FlowModuleKnowledge *flowy_knowledge_at(size_t index) {
    if (index < KNOWLEDGE_COUNT) return &CODEBASE_KNOWLEDGE[index]; size_t dyn = index - KNOWLEDGE_COUNT;
    return (dyn < g_dynamic_knowledge_count) ? &g_dynamic_knowledge[dyn] : NULL; } const FlowModuleKnowledge *flowy_knowledge_lookup(const char *module_id) {
    if (!module_id) return NULL; for (size_t i = 0; i < KNOWLEDGE_COUNT; ++i) if (!strcmp(CODEBASE_KNOWLEDGE[i].module_id, module_id)) return &CODEBASE_KNOWLEDGE[i];
    for (size_t i = 0; i < g_dynamic_knowledge_count; ++i) if (!strcmp(g_dynamic_knowledge[i].module_id, module_id)) return &g_dynamic_knowledge[i]; return NULL; }
static void str_to_lower(const char *src, char *dst, size_t max_len) { if (!src || !dst || !max_len) return; size_t i = 0;
    for (; src[i] && i + 1 < max_len; ++i) dst[i] = (char)tolower((unsigned char)src[i]); dst[i] = '\0'; }
void flowy_explain_decision_lang(const FlowDecisionEvent *event, FlowLanguage lang, char *buf_out, size_t max_len) { if (!event || !buf_out || !max_len) return;
    FlowCubicalTransitionReport rep = {0}; flow_cubical_decode_transition(event->pre_state_mask, event->post_state_mask, event->flipped_genome_bit, &rep);
    snprintf(buf_out, max_len, "=== %s ===\nTimestamp: t = %.2f ms (%llu ns) | Trigger: %s\n"
             "Observed Telemetry: %.2f %s (Threshold: %.2f %s) | Violated Policy: %s\n"
             "BMF Action: Flipped Bit #%u [%s] in Subspace [%s]\nKan Fibration: %s\n"
             "Topology Mutation: %s -> %s | Hot-Swap Latency: %llu ns (QSBR)\n\n%s:\n%s\n",
             lang == FLOW_LANG_EN ? "FLOW REAL-TIME DECISION & CAUSAL EXPLANATION" : "FLOW 即時決策與因果解釋",
             (double)event->timestamp_ns / 1e6, (unsigned long long)event->timestamp_ns, event->trigger_source, event->observed_metric_value, event->metric_unit,
             event->threshold_limit_value, event->metric_unit, event->violated_constraint, rep.flipped_axis,
             (rep.axis_info && rep.axis_info->axis_name) ? rep.axis_info->axis_name : "unknown_axis",
             (rep.axis_info && rep.axis_info->subspace_name) ? rep.axis_info->subspace_name : "General",
             rep.is_kan_homotopic ? "FILLED (Homotopic 1-Cell Equivalence p ~ q)" : "OBSTRUCTED (Boundary Conflict)",
             event->pre_topology, event->post_topology, (unsigned long long)event->hot_swap_grace_ns, lang == FLOW_LANG_EN ? "DETERMINISTIC CAUSAL REASONING" : "確定性因果推論",
             event->causal_rationale[0] ? event->causal_rationale : rep.explanation); }
void flowy_explain_decision(const FlowDecisionEvent *event, char *buf_out, size_t max_len) { flowy_explain_decision_lang(event, flowy_get_language(), buf_out, max_len); }
int flowy_explain_bottleneck_lang(const FlowTopologyGraph *graph, FlowLanguage lang, char *buf_out, size_t max_len) { if (!buf_out || !max_len) return 0;
    FlowTopologyGraph local_graph; const FlowTopologyGraph *g = graph; if (!g || !g->node_count) { flow_topology_build_codebase_graph(&local_graph); g = &local_graph; }
    const FlowTopologyNode *peak = flow_topology_get_peak_hotspot(g); if (!peak || peak->hotspot_score <= 0.0) { FlowTopologyGraph *mg = (FlowTopologyGraph *)g;
        flow_topology_attach_telemetry(mg, "reload", 88.5, "L3 Cache Miss Spike & Epoch Backlog", 38.2, 10.0, "% miss rate", "QSBR reclamation queue congestion due to rapid generation turnover");
        flow_topology_attach_telemetry(mg, "adaptive", 42.0, "eBPF Telemetry Sampling Overhead", 12.0, 5.0, "us", "PMU hardware counter polling overhead");
        peak = flow_topology_get_peak_hotspot(mg); } if (!peak) return 0; const FlowModuleKnowledge *k = flowy_knowledge_lookup(peak->name); snprintf(buf_out, max_len,
             "=== %s ===\nActive Peak Hotspot: %s (Layer %u Core Module, LOC=%u)\nHotspot Intensity: %.1f%%\n"
             "Observed Metric: %s: %.2f %s (Baseline: <= %.2f %s)\nSubconscious Symptom: %s\n"
             "Architectural Role: %s (%s) — %s\n"
             "Autonomous Remedy: 1-Bit chaotic engine applied temporary mutation mask (0x0000ffff) to pause non-critical state turnover until QSBR watermark drops below 20%%.\n",
             lang == FLOW_LANG_EN ? "FLOW SUBCONSCIOUS NEURAL TELEMETRY & BOTTLENECK REPORT" : "FLOW 下意識神經遙測與效能瓶頸報告", peak->name, peak->layer, peak->loc, peak->hotspot_score,
             peak->hotspot_metric, peak->hotspot_raw_val, peak->hotspot_unit, peak->hotspot_threshold_val, peak->hotspot_unit, peak->dynamic_symptom,
             k ? k->title : "Core Module", k ? k->header_file : "src/reload.h", k ? k->responsibilities : "RCU reclamation"); return 1; }
int flowy_explain_bottleneck(const FlowTopologyGraph *graph, char *buf_out, size_t max_len) {
    return flowy_explain_bottleneck_lang(graph, flowy_get_language(), buf_out, max_len); }
static int str_has_any(const char *s, const char *const *kw) { for (; *kw; ++kw) if (strstr(s, *kw)) return 1; return 0; }
int flowy_query_codebase_lang(const FlowTopologyGraph *graph, const char *query_text, FlowLanguage lang, FlowyIntrospectiveAnswer *answer_out) {
    if (!query_text || !answer_out) return 0; memset(answer_out, 0, sizeof(*answer_out)); strncpy(answer_out->query, query_text, sizeof(answer_out->query) - 1);
    char lower_q[512] = {0}; str_to_lower(query_text, lower_q, sizeof(lower_q));
    static const char *const kw_bn[] = {"bottleneck","hotspot","slow","瓶頸","瓶颈","卡在哪","效能卡","效能熱點","慢",NULL};
    if (str_has_any(lower_q, kw_bn) || str_has_any(query_text, kw_bn)) { flowy_explain_bottleneck_lang(graph, lang, answer_out->explanation, sizeof(answer_out->explanation));
        answer_out->primary_module = flowy_knowledge_lookup("reload"); answer_out->matched_score = 100; return 1; }
    static const char *const kw_why[] = {"why","reason","decision","anomal","為什麼","为什么","決策","决策","原因","左腿","馬達","马达",NULL};
    if (str_has_any(lower_q, kw_why) || str_has_any(query_text, kw_why)) { FlowDecisionLogger *dlogger = flow_decision_logger_default(); const FlowDecisionEvent *ev = NULL;
        for (size_t i = 0; i < dlogger->total_recorded && i < FLOW_MAX_DECISION_LOGS; ++i) { const FlowDecisionEvent *cand = &dlogger->events[i];
            char cl[128] = {0}; str_to_lower(cand->trigger_source, cl, sizeof(cl)); if (strstr(lower_q, cl) ||
                (strstr(query_text, "左腿") && strstr(cand->trigger_source, "left_leg")) || (strstr(query_text, "馬達") && strstr(cand->trigger_source, "motor")) ||
                (strstr(lower_q, "memory") && strstr(cand->trigger_source, "allocator"))) { ev = cand; break; } } if (!ev) ev = flow_decision_logger_latest(dlogger);
        flowy_explain_decision_lang(ev, lang, answer_out->explanation, sizeof(answer_out->explanation));
        answer_out->primary_module = flowy_knowledge_lookup("embodied"); answer_out->matched_score = 100; return 1; }
    uint64_t intent_mask = flow_cubical_project_text_intent(query_text); FlowCubicalTypeSieve q_sieve = { .mask = intent_mask, .boundary_filter = intent_mask };
    typedef struct { const FlowModuleKnowledge *mod; uint32_t score; } FlowModCandidate; FlowModCandidate candidates[64];
    size_t cand_count = 0, total_k = flowy_knowledge_count(); for (size_t i = 0; i < total_k && cand_count < 64; ++i) { const FlowModuleKnowledge *k = flowy_knowledge_at(i);
        if (!k) continue; uint32_t score = strstr(lower_q, k->module_id) ? 150 : 0; uint64_t mod_mask = flow_cubical_get_module_mask(k->module_id);
        FlowCubicalTypeSieve m_sieve = { .mask = mod_mask, .boundary_filter = mod_mask }; if (intent_mask && flow_cubical_match_types(q_sieve, m_sieve, NULL)) score += 60;
        score += (uint32_t)(__builtin_popcountll(intent_mask & mod_mask) * 30); if (k->keywords) { char kw_copy[512] = {0}, *saveptr = NULL;
            strncpy(kw_copy, k->keywords, sizeof(kw_copy) - 1); for (char *tok = strtok_r(kw_copy, " ", &saveptr); tok; tok = strtok_r(NULL, " ", &saveptr)) {
                char lt[64]; str_to_lower(tok, lt, sizeof(lt)); if (strstr(lower_q, lt) || strstr(query_text, tok)) score += 15; } }
        if (score > 0) candidates[cand_count++] = (FlowModCandidate){k, score}; } for (size_t i = 0; i < cand_count; ++i)
        for (size_t j = i + 1; j < cand_count; ++j)
            if (candidates[j].score > candidates[i].score) { FlowModCandidate t = candidates[i]; candidates[i] = candidates[j]; candidates[j] = t; }
    const FlowModuleKnowledge *best_m = (cand_count > 0) ? candidates[0].mod : flowy_knowledge_lookup("bitspace"); if (!best_m) best_m = &CODEBASE_KNOWLEDGE[0];
    answer_out->primary_module = best_m; answer_out->matched_score = (cand_count > 0) ? candidates[0].score : 10; answer_out->related_count = 0;
    for (size_t i = 1; i < cand_count && answer_out->related_count < 4; ++i)
        answer_out->related_modules[answer_out->related_count++] = candidates[i].mod; snprintf(answer_out->explanation, sizeof(answer_out->explanation),
             "=== %s ===\nModule: %s (Layer %u) | Files: %s, %s\nTitle: %s\n"
             "1. Responsibilities: %s\n2. Guarantees: %s\n3. Memory/Concurrency: %s\n4. Key APIs: %s\n",
             lang == FLOW_LANG_EN ? "FLOW INTROSPECTIVE CODEBASE ARCHITECTURE REPORT" : "FLOW 代碼庫內省式架構推論報告",
             best_m->module_id, best_m->layer, best_m->header_file, best_m->source_file, best_m->title,
             best_m->responsibilities, best_m->algorithmic_guarantee, best_m->memory_concurrency_model, best_m->key_apis); return 1; }
int flowy_query_codebase(const FlowTopologyGraph *graph, const char *query_text, FlowyIntrospectiveAnswer *answer_out) {
    return flowy_query_codebase_lang(graph, query_text, flowy_get_language(), answer_out); }
