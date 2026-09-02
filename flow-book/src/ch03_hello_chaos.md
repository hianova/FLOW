# 第三章：Hello, Chaos (第一個 FLOW 專案的運作原理與生命週期)

> 「我們從一個最純粹的範例出發，親眼見證一份宣告式意圖如何透過 1-Bit 混沌引擎，在千分之五秒內自發坍縮成高效能的 Native C 程式碼。」

---

## 3.1 構建與初體驗：編譯第一個 FLOW 專案

在 FLOW 專案根目錄中，我們可以使用最極簡的前端編譯器 `flowc`（純 C 打造，編譯產物僅數百 KB）來處理經典的排序管線範例 `examples/rank.flow`：

```sh
# 1. 基礎編譯：將 .flow 意圖直接轉譯為 C 代碼
flowc examples/rank.flow -o generated/rank.c

# 2. 啟用 1-Bit 混沌退火搜尋（進行 250 次變異迭代，固定隨機種子 42）
flowc examples/rank.flow -o generated/rank.c --search --iterations 250 --seed 42

# 3. 多目標後端同時發射 (C Header / Rust / Python / MLIR / LLVM IR)
flowc examples/rank.flow -o generated/rank.c \
    --target-c-header generated/rank.h \
    --target-rust generated/rank.rs \
    --target-python generated/rank.py \
    --target-mlir generated/rank.mlir \
    --target-llvm-ir generated/rank.ll

# 4. 使用系統 C 編譯器將生成代碼編譯為本機二進位執行檔
clang -std=c17 -O2 -pthread generated/rank.c -o build/rank -lm
./build/rank
```

執行後，終端機將輸出：
```text
flow: rank
top 10 results:
rank 0: score 992 (id: 481)
rank 1: score 987 (id: 102)
rank 2: score 981 (id: 774)
...
```

---

## 3.2 深度追蹤：FLOW 編譯與發射生命週期

從終端機鍵入命令到產出二進位檔案，FLOW 內部經歷了如下六個嚴格解耦的生命週期階段：

```text
FLOW 完整編譯管線:
┌────────────────────────────────────────────────────────────────────────────┐
│ 1. 語法解析 (Parsing)                                                      │
│    `src/parser.c`: 讀取 .flow 語法文本，構建 FlowSpec 語意結構體。         │
├────────────────────────────────────────────────────────────────────────────┤
│ 2. 語意降維與事實推導 (Semantic Lowering & Fact Deduction)                 │
│    `src/semantic.c`: 推導 parallelizable、ordered、deterministic 等事實，  │
│    構建抽象語意中介表示 SemanticIR。                                       │
├────────────────────────────────────────────────────────────────────────────┤
│ 3. 候選元件匹配與超維 BitSpace 初始化 (Candidate Registry & BitSpace)      │
│    `src/registry.c` & `src/bitspace.c`: 篩選相容元件，分配低階選擇位元      │
│    與高階參數維度位元，初始化離散超立方體空間 FlowBitSpace。               │
├────────────────────────────────────────────────────────────────────────────┤
│ 4. 1-Bit 混沌退火與多面體硬遮罩篩選 (1-Bit Chaotic Annealing)              │
│    `src/search.c`: 以 12.96 ns/op 的極速進行 1-Bit 擾動，                  │
│    結合 3-Tier Mask Canvas 過濾非法解，沿 Pareto 前沿逼近最低能量解。     │
├────────────────────────────────────────────────────────────────────────────┤
│ 5. SMT 形式化驗證與定理審計 (Formal SMT Verification)                      │
│    `src/smt.c`: 發射 QF_BV SMT-LIB2 證明腳本，驗證緩衝區邊界、記憶體上限、 │
│    分片隔離與確定性，產出證明背書（Proof Attestation）。                  │
├────────────────────────────────────────────────────────────────────────────┤
│ 6. 多目標後端發射 (Multi-Target Emission)                                 │
│    `src/backend.c`: 根據坍縮後的最佳基因組，發射原生 C17、Rust、LLVM IR。 │
└────────────────────────────────────────────────────────────────────────────┘
```

---

## 3.3 生成代碼剖析：生成的 C 語言長什麼樣？

查看 `generated/rank.c`，你會發現 FLOW 發射的代碼具備以下卓越特性：
1. **零外部依賴**：僅包含 `<stdio.h>`, `<stdlib.h>`, `<string.h>` 等 C 標準標頭檔。
2. **無鎖高快取親和性**：資料結構依照最佳化基因組佈局，記憶體對齊至 64 位元組快取行（Cacheline-aligned）。
3. **內建 QSBR 熱替換適配器**（當啟用 `--reload-adapter` 時）：自動生成符合 `FlowUnit` ABI 的 `init`、`run`、`migrate` 與 `drop` 函數指標。

```c
/* generated/rank.c (節錄) */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

typedef struct {
    int id;
    int score;
} FlowRankItem;

typedef struct {
    FlowRankItem items[1000];
    size_t count;
    size_t capacity;
} FlowRankState;

/* 由 1-Bit 混沌退火引擎選定的最佳執行路徑 */
void flow_rank_run(FlowRankState *state, const FlowRankItem *input, size_t input_len) {
    /* 向量化堆積排序與 Top-K 快速篩選 */
    // ... 高效能無分支排序迴圈 ...
}
```

---

## 3.4 證據脊椎：`.flowplan` 計畫產物的生成與驗證

FLOW 不僅產出代碼，還會產出具備密碼學背書的**「計畫證據脊椎（Evidence Spine）」**（即 `FlowPlanArtifact`）。該產物包含了：
- `contract_hash`：`.flow` 合約的 SHA256 雜湊值。
- `plan_schema_hash`：維度定義架構的雜湊值。
- `seed` 與 `genome`：確定性重現該實作所需的 PRNG 種子與 64/1024-Bit 基因組。
- `FlowSMTProofAttestation`：形式化定理證明的合格簽章。

任何第三方均可使用 `flowc --verify-plan artifact.flowplan` 在千分之一秒內驗證二進位代碼是否真正滿足當初宣告的意圖合約！
