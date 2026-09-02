# 第四章：拓樸圖譜 (Topology Graph) (將程式碼降維成可推算的依賴約束)

> 「軟體的架構不是文字檔目錄的堆疊，而是一張高維拓樸圖。在 FLOW 中，程式碼被降維成可直接進行圖論運算、親和性分析與遙測附著的活體神經圖譜。」

---

## 4.1 程式碼即圖譜：`FlowTopologyGraph` 核心架構

在 FLOW 系統中，程式碼不再以靜態檔案路徑存在，而是被統構為一張具備強型別層級的有向加權圖（`FlowTopologyGraph`，定義於 `src/topology.h` 與 `src/topology.c`）：

```text
FLOW 4-Layer 拓樸層級模型:
┌────────────────────────────────────────────────────────────────────────┐
│ Layer 3: 宣告意圖層 (Intent / User Layer)                              │
│          FlowNode: { task_stream, transform, collect, ranking }        │
├───────────────────────────────────┬────────────────────────────────────┤
│ Layer 2: 領域外掛層 (Plugin Layer)│                                    │
│          FlowNode: { flow.smt, flow.embodied, flow.security }          │
├───────────────────────────────────┴────────────────────────────────────┤
│ Layer 1: ABI 與元件註冊層 (Registry / ABI Layer)                       │
│          FlowNode: { linear_array, sharded_hash, FlowUnit, QSBR }      │
├────────────────────────────────────────────────────────────────────────┤
│ Layer 0: 極簡核心層 (Core Compiler & Chaos Engine)                     │
│          FlowNode: { parser, semantic, bitspace, search, verifier }    │
└────────────────────────────────────────────────────────────────────────┘
```

### 資料結構解析 (`src/topology.h`)

```c
typedef enum {
    FLOW_NODE_CORE_MODULE = 0,   /* 核心編譯器模組 */
    FLOW_NODE_PLUGIN = 1,        /* 領域外掛 (DSO) */
    FLOW_NODE_COMPONENT = 2,     /* 架構元件 (如 sharded_hash) */
    FLOW_NODE_INTENT_OP = 3,     /* 意圖操作元 (如 transform) */
    FLOW_NODE_DIMENSION = 4,     /* BitSpace 探索維度 (如 threads, shards) */
    FLOW_NODE_SHARD_GROUP = 5    /* 分區叢集 */
} FlowNodeType;

typedef enum {
    FLOW_EDGE_CALLS = 0,             /* 函數與符號直接調用 */
    FLOW_EDGE_USES = 1,              /* 結構體或型別引用 */
    FLOW_EDGE_IMPLEMENTS = 2,        /* 外掛實作元件介面 */
    FLOW_EDGE_BINDS_DIMENSION = 3,   /* 元件綁定 BitSpace 維度 */
    FLOW_EDGE_DATA_FLOW = 4,         /* 數據流管線連接 (A -> B) */
    FLOW_EDGE_SHARD_AFFINITY = 5     /* 節點間的快取/NUMA 親和性 */
} FlowEdgeType;
```

---

## 4.2 潛意識神經遙測：硬體信號如何附著於拓樸

傳統 APM（應用效能監控）將監控指標輸出到日誌中，開發者必須肉眼查看 Grafana 儀表板。FLOW 的革命性創舉在於**「潛意識神經遙測附著（Subconscious Neural Telemetry Ingestion）」**。

運行期的 eBPF 探針、PMU 硬體性能計數器與 QSBR 隊列狀態，會實時被寫入拓樸圖譜的對應節點中：

```c
int flow_topology_attach_telemetry(
    FlowTopologyGraph *graph,
    const char *node_name,
    double hotspot_score,          /* 熱點強度評分 [0.0 ~ 100.0] */
    const char *metric_name,       /* 例如: "L3 Cache Miss Rate" */
    double raw_val,                /* 實測值: 例如 34.8% */
    double thresh_val,             /* 閾值: 例如 10.0% */
    const char *unit,              /* 單位: "%" */
    const char *symptom            /* 症狀描述 */
);
```

```text
神經遙測附著拓樸示意:
[Node: sharded_hash] 
  ├── Hotspot Score: 92.4 (CRITICAL)
  ├── Metric: L3 Cache Miss Rate (34.8% > 10.0%)
  └── Dynamic Symptom: "High cross-socket cache bouncing under 64 cores"
        │
        └──► [Flowy 自我意識引擎] ──► 觸發 1-Bit 遮罩偏置 ──► 質變至 SoA_EventLoop
```

---

## 4.3 拓樸審計與架構洩漏檢測 (`flow_topology_audit`)

為了確保「意圖層（Layer 3）」絕不被底層「實作層（Layer 0/1）」所污染，`flow_topology_audit()` 執行嚴苛的圖論隔離檢查：

$$\text{Leak Condition}: \exists e = (u, v) \in E \quad \text{such that} \quad \text{layer}(u) > \text{layer}(v) + 1 \land \text{type}(e) = \text{FLOW\_EDGE\_USES}$$

```c
void flow_topology_audit(const FlowTopologyGraph *graph, FlowTopologyAuditReport *report) {
    /* 檢測跨層洩漏 (Cross-layer leaks) */
    /* 計算平均耦合度 (Average coupling) 與 模組化分數 (Modularity score) */
}
```

若有開發者不慎在 `.flow` 意圖中引用了特定 C 語言實作結構，拓樸審計將立即報警並拒絕合約吸收（`flowy absorb` 失敗），從數學上徹底杜絕架構腐化。

---

## 4.4 圖譜序列化與視覺化輸出

拓樸圖譜支援直接導出為標準 Graphviz DOT 格式與 JSON 格式，便於外部工具即時渲染：

```sh
# 導出當前系統拓樸圖譜
flowy doc all --format dot > build/topology.dot
dot -Tsvg build/topology.dot -o build/topology.svg
```

透過拓樸圖譜，FLOW 第一次讓大型軟體架構具備了可推算、可微調、具備神經感知能力的活體骨架。
