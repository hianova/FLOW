# 第二章：意圖 vs. 實作 (.flow 檔案的本質：我們只宣告約束，不寫邏輯)

> 「在 FLOW 的哲學中，撰寫程式碼不是告訴 CPU 如何一步一步執行指令，而是向宇宙宣告系統必須服從的幾何邊界與不變量。」

---

## 2.1 語法即合約：`.flow` 檔案的解構

一個標準的 `.flow` 意圖描述檔，是完全宣告式（Declarative）的。它不包含任何傳統程式語言中的命令式陳述句（沒有 `for`、`while`、`malloc` 或指標運算）。

以生產環境中的 `project.flow` 與 `rank.flow` 為例：

```flow
// examples/project.flow
project browser_runtime

input task_stream {
    max_count 10000
}

flow browser_pipeline {
    task_stream -> transform -> collect
}

import builtin

require {
    deterministic
    memory < 64mb
}

prefer {
    latency
}
```

```flow
// examples/rank.flow
input user_scores {
    max_count 1000
}

output top_ranking {
    top 10
}

flow ranking_pipeline {
    user_scores -> sort -> top
}

require {
    deterministic
    memory < 16mb
}

prefer {
    latency
}
```

這兩份意圖合約傳遞了極致精確的拓樸與約束資訊：
1. **拓樸流向（Dataflow Directed Graph）**：`user_scores -> sort -> top` 定義了一個線性的數據管線。
2. **容量與硬邊界（Hard Invariants）**：`max_count 1000` 與 `memory < 16mb`。
3. **優化目標（Pareto Preferences）**：`prefer { latency }` 明確要求退火引擎偏向最低延遲解。

---

## 2.2 核心解析與語意降維：`FlowSpec` 到 `SemanticIR`

在 FLOW 核心庫中，語意剖析器（`src/parser.c`）與語意降維器（`src/semantic.c`）負責將純文字意圖轉化為形式化約束結構：

```text
.flow 文本 ──► parse_spec() (src/parser.c) ──► FlowSpec ──► lower_to_ir() (src/semantic.c) ──► SemanticIR
```

### 核心資料結構解析 (`src/flow.h`)

```c
typedef struct {
    char input_name[FLOW_NAME];
    int max_count;
    FlowSample samples[FLOW_SAMPLE_MAX];
    size_t sample_count;
    char output_name[FLOW_NAME];
    char state_name[FLOW_NAME];
    int shared;
    int read_heavy;
    int bounded;
    char flow_name[FLOW_NAME];
    FlowNode flow_nodes[FLOW_NODE_MAX];
    size_t flow_node_count;
    int top_n;
    int deterministic;
    int memory_mb;
    int prefer_latency;
    FlowConstraint constraints[FLOW_CONSTRAINT_MAX];
    size_t constraint_count;
    /* ... 模組與能力宣告 ... */
} FlowSpec;
```

當 `lower_to_ir()` 被調用時，系統進行**不變量推導與事實提取（Fact Extraction）**，產出強型別的 `SemanticIR`：

```c
typedef struct SemanticIR {
    char flow_name[FLOW_NAME];
    FlowNode flow_nodes[FLOW_NODE_MAX];
    size_t flow_node_count;
    int flow_parallelizable;     /* 推導: 管線是否支援無狀態並行 */
    int fact_ordered;            /* 推導: 輸出是否必須嚴格保序 */
    int fact_range_proven;       /* 推導: 數值範圍是否受界 */
    int fact_size_preserved;     /* 推導: 是否為 1-to-1 轉換 */
    int fact_deterministic;      /* 宣告: 確定性定理約束 */
    int memory_limit_mb;         /* 硬配額: 記憶體上限 (MB) */
    int prefer_latency;          /* 偏好: 延遲權重 vs 記憶體權重 */
    FlowFact facts[FLOW_NODE_MAX];
    size_t fact_count;
    /* ... 多面體約束系統 ... */
} SemanticIR;
```

---

## 2.3 意圖與實作的完美解耦

為什麼 FLOW 強烈禁止在 `.flow` 中寫入具體實作邏輯？

| 維度 | 傳統寫法 (寫實作) | FLOW 寫法 (寫意圖) |
| :--- | :--- | :--- |
| **資料結構選擇** | 開發者硬編碼 `std::vector` 或 `HashMap` | 引擎從 `linear_array`、`sharded_hash`、`lockfree_ring` 中自動挑選 |
| **並發度** | 硬編碼 `std::thread::spawn(8)` | 退火引擎根據當前 CPU 核心數與 L3 快取動態退火出最佳 `threads` 與 `shards` |
| **記憶體佈局** | 寫死結構體欄位順序（AoS） | 根據 PMU 快取失效率動態在 AoS、SoA 與 Columnar 之間切換 |
| **容錯與降級** | 到處充斥 `try/catch` 與防禦性程式碼 | 宣告式 `fallback` 策略，由 QSBR 運行期自動路由至 Golden Baseline |

```text
意圖與候選解空間的映射矩陣:
.flow 意圖 (SemanticIR)
    │
    ├─► 候選元件庫 (Component Registry):
    │   ├─ [Candidate 0]: linear_array      (單執行緒, 零快取爭用, 記憶體最小)
    │   ├─ [Candidate 1]: sharded_hash      (多執行緒, 高吞吐, 記憶體開銷中等)
    │   └─ [Candidate 2]: lockfree_ring     (極致微秒延遲, 記憶體固定預分配)
    │
    └─► 參數維度空間 (FlowPlanDimensionSet):
        ├─ tile_size   : { 16, 32, 64, 128 }
        ├─ batch_size  : { 1, 8, 32, 64 }
        ├─ layout      : { AoS, SoA, Columnar }
        └─ threads     : { 1, 2, 4, 8, 16 }
```

開發者只需維護一份僅有 15 行的 `.flow` 意圖檔案，FLOW 的混沌退火引擎就能在數萬個潛在實作組合中，精確坍縮出當前物理硬體下的最優機器碼！
