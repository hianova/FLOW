# 第三章：拓樸圖譜 (Topology Graph) (將程式碼降維成可推算的依賴約束流形)

> 「軟體的架構不是文字檔目錄的堆疊，而是一張高維拓樸圖。在 FLOW 中，程式碼被降維成可直接進行圖論運算、親和性分析與遙測附著的活體神經圖譜。」

---

## 3.1 程式碼即圖譜：`FlowTopologyGraph` 核心架構

在 FLOW 系統中，程式碼不再以靜態檔案路徑存在，而是被統構為一張具備強型別層級的有向加權圖（`FlowTopologyGraph`，定義於 `src/topology.h` 與 `src/topology.c`）：

```text
FLOW 4-Layer 拓樸層級模型:
┌────────────────────────────────────────────────────────────────────────┐
│ Layer 3: 宣告意圖層 (Intent / User Layer)                              │
│          FlowNode: { task_stream, transform, collect, ranking }        │
├───────────────────────────────────┬────────────────────────────────────┤
│ Layer 2: 領域驅動與流形層 (Memory & Drivers)                           │
│          FlowNode: { flow.primitive, flow.embodied, flow.fvec }        │
├───────────────────────────────────┴────────────────────────────────────┤
│ Layer 1: ABI 與元件註冊層 (Registry / ABI Layer)                       │
│          FlowNode: { linear_array, sharded_hash, FlowUnit, QSBR }      │
├────────────────────────────────────────────────────────────────────────┤
│ Layer 0: 極簡核心大腦層 (Core Brain & Engine)                          │
│          FlowNode: { parser, bitspace, search, smt, topology }         │
└────────────────────────────────────────────────────────────────────────┘
```

### 資料結構解析 (`src/topology.h`)

```c
typedef enum {
    FLOW_NODE_CORE_MODULE = 0,   /* 核心編譯器模組 */
    FLOW_NODE_PLUGIN = 1,        /* 領域驅動 (Primitive Driver) */
    FLOW_NODE_COMPONENT = 2,     /* 架構元件 (如 sharded_hash) */
    FLOW_NODE_INTENT_OP = 3,     /* 意圖操作元 (如 transform) */
    FLOW_NODE_DIMENSION = 4,     /* BitSpace 探索維度 (如 threads, shards) */
    FLOW_NODE_SHARD_GROUP = 5,   /* 分區叢集 */
    FLOW_NODE_FVEC_EXPERIENCE = 6/* 歷史 .fvec 架構經驗節點 */
} FlowNodeType;

typedef enum {
    FLOW_EDGE_CALLS = 0,             /* 函數與符號直接調用 */
    FLOW_EDGE_USES = 1,              /* 結構體或型別引用 */
    FLOW_EDGE_IMPLEMENTS = 2,        /* 外掛實作元件介面 */
    FLOW_EDGE_BINDS_DIMENSION = 3,   /* 元件綁定 BitSpace 維度 */
    FLOW_EDGE_DATA_FLOW = 4,         /* 數據流管線連接 (A -> B) */
    FLOW_EDGE_SHARD_AFFINITY = 5,    /* 節點間的快取/NUMA 親和性 */
    FLOW_EDGE_MEMORIALIZES = 6       /* .fvec 模型對底層實體元件的記憶繫結 */
} FlowEdgeType;
```

---

## 3.2 潛意識神經遙測：硬體信號如何附著於拓樸

傳統 APM（應用效能監控）將監控指標輸出到日誌中，開發者必須肉眼查看 Grafana 儀表板。FLOW 的革命性創舉在於**「潛意識神經遙測附著（Subconscious Neural Telemetry Ingestion）」**。

運行期的 eBPF 探針、PMU 硬體性能計數器與 QSBR 隊列狀態，會實時被寫入拓樸圖譜的對應節點中：

```c
int flow_topology_attach_telemetry(
    FlowTopologyGraph *graph,
    const char *node_name,
    double hotspot_score,
    const char *metric_name,
    double raw_value,
    double threshold_value,
    const char *unit,
    const char *symptom_description
);
```

當某個節點（例如 `reload` 或 `left_leg_motor`）的熱點分數異常飆高時，拓樸圖譜會自動計算**圖的最小割（Min-Cut）與瓶頸影響傳遞路徑**，直接通知 1-Bit 混沌退火引擎進行局部幾何修復。

---

## 3.3 架構防火牆與零跨層滲漏保證

FLOW 實施了絕對嚴格的代碼庫架構審計（`flow_topology_audit_codebase`）：
1. **層級單向性**：Layer $N$ 只能調用 Layer $M \le N$，嚴禁反向向上依賴（Upward Dependency Violations = 0）。
2. **零跨層滲漏**：核心模組之間的耦合必須嚴格遵守封裝邊界。
3. **模組度保證**：代碼庫的模組化分數（Modularity Score）在編譯期恆等於 1.00。
