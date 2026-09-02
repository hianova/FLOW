# 第十一章：語意推論與沙盤推演 (如何讓 Flowy 模擬與解釋架構變化)

> 「活體系統不僅要能自動適應，更要具備向人類解釋『為什麼做此決定』的因果表達能力，以及在不改動生產環境的前提下進行『如果...會怎樣』的反事實沙盤推演。」

---

## 11.1 即時因果解釋引擎：`flowy why` 與 `flowy bottleneck`

在傳統自適應系統或神經網路優化器中，系統的行為往往是「黑盒子」——沒有人知道為什麼系統突然更換了佇列或降級了執行緒。

FLOW 建立了**「確定性因果決策記錄器（FlowDecisionLogger）」**（實作於 `src/flowy.h` 與 `src/flowy.c`）：

```c
typedef struct {
    uint64_t timestamp_ns;
    FlowDecisionTriggerType trigger_type; /* 觸發源: 例如 MEMORY_PRESSURE, CACHE_MISS_SPIKE */
    char trigger_source[64];              /* 觸發硬體: 例如 "pmu_l3_cache", "arena_allocator" */
    double observed_metric_value;         /* 觀測值: 例如 38.5% */
    double threshold_limit_value;         /* 閾值: 例如 12.0% */
    char violated_constraint[128];        /* 違背之約束: "L3 Cache Miss Rate Hard Ceiling" */
    uint32_t flipped_genome_bit;          /* 翻轉之位元: 14 */
    char pre_topology[64];                /* 遷移前架構: "AoS_LinearArray" */
    char post_topology[64];               /* 遷移後架構: "SoA_Sharded_LoadBalance" */
    char causal_rationale[512];           /* 確定性因果解釋 */
    uint64_t hot_swap_grace_ns;           /* QSBR 寬限期耗時 (例如 84 ns) */
} FlowDecisionEvent;
```

### 終端機即時因果查詢

```sh
# 查詢最新一次質變的確定性因果
flowy why

# 查詢當前系統潛意識遙測的最高峰值瓶頸
flowy bottleneck

# 查看完整架構演化時間軸
flowy timeline
```

輸出範例：
```text
================================================================================
FLOWY DETERMINISTIC CAUSAL DECISION EXPLANATION
================================================================================
Timestamp: 1725301234567890 ns
Trigger:   CACHE_MISS_SPIKE (Source: pmu_l3_cache)
Violation: Observed L3 Cache Miss Rate 38.5% exceeded threshold 12.0%.

[Autonomous Action Taken]
Flipped Genome Bit #14: Remodeled topology from [AoS_LinearArray] to [SoA_Sharded_LoadBalance].
Hot-swap executed via QSBR grace period in 84 ns (0 dropped requests).

[Causal Rationale]
Transitioned to Struct of Arrays (SoA) layout to enable AVX-512 continuous memory strides,
reducing cache miss penalty by 85% and restoring steady-state latency SLA.
================================================================================
```

---

## 11.2 反事實沙盤推演 ("What-If" Counterfactual Simulation)

當架構師想要評估一個假設情境（例如：「如果把伺服器記憶體從 64MB 砍到 8MB，並將並發執行緒提升到 32，系統會發生什麼事？」），他不需要在生產環境冒險壓測。

Flowy 提供了**反事實沙盤推演引擎（`flow_orchestrator_simulate_what_if`）**：

```c
/* src/orchestrator.c */
int flow_orchestrator_simulate_what_if(
    FlowOrchestrator *orch,
    int hypothetical_memory_mb,
    int hypothetical_top_n,
    int hypothetical_threads,
    FlowCounterfactualReport *report_out
);
```

```text
沙盤推演流程:
[使用者輸入假設] ──► flowy what-if --memory 8mb --threads 32
                            │
                            ├─► 1. 複製當前拓樸狀態至隔離記憶體沙盒
                            ├─► 2. 注入假設約束，執行 1-Bit 混沌退火重估
                            ├─► 3. SMT 驗證硬配額定理
                            └─► 4. 輸出預測報告與結構崩潰警報
```

```sh
flowy what-if --memory 8 --threads 32
```

輸出報告：
```text
[WHAT-IF COUNTERFACTUAL SIMULATION REPORT]
Hypothetical Scenario: Memory 8MB, Threads 32
- Feasibility: INFEASIBLE (Hard Gate Violation)
- Structural Collapse Warning: 32 threads require min 16MB thread-local buffers (16MB > 8MB).
- Throughput Prediction: -92% (Severe memory thrashing)
- QSBR Reclaim Frequency: 18.5x increase
- Recommendation: Maintain threads <= 4 when memory < 16MB.
```

---

## 11.3 拓樸合成與自動修復 (Min-Cut Patch Synthesis)

當多個團隊合併不同的 `.flow` 意圖檔案產生約束衝突時（例如模組 A 要求 `memory < 16mb`，模組 B 要求 `capacity 100000` 需至少 32MB），`flowy absorb` 會觸發**最小割（Min-Cut）衝突修補合成器**（`flow_orchestrator_synthesize_remediation`）：

$$\min_{e \in E_{\text{cut}}} \text{Weight}(e) \quad \text{s.t.} \quad \mathcal{P}_{\text{A}} \cap \mathcal{P}_{\text{B}} \neq \emptyset$$

Flowy 不僅報告衝突，還會直接生成符合數學相容性的 `.flow` 補丁建議：

```text
[FLOWY AUTO-REMEDIATION SYNTHESIS]
Conflict Detected: [spec_a.flow] (memory < 16mb) vs [spec_b.flow] (capacity 100000).
Min-Cut Dimension: capacity

Proposed Remediation Patch:
--- examples/spec_b.flow
+++ examples/spec_b.flow
@@ -2,3 +2,3 @@
 input task_stream {
-    max_count 100000
+    max_count 48000
 }
```

這使得大型架構的整合演進具備了全自動的數學輔助決策能力。
