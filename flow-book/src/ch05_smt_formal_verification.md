# 第五章：形式化最高法院 (SMT 4 大定理 UNSAT 證明與 1-Cycle 修剪)

> 「啟發式搜尋可以天馬行空，但發射出的每一行機器碼必須擁有無可爭辯的數學證明。SMT 定理證明器是 FLOW 宇宙的最高法院，凡無證明者，一律否決。」

---

## 5.1 形式化驗證即最高法院：拒絕「盲目信任」

在傳統編譯器與 AI 代碼生成系統中，最致命的弱點在於**「無法證明生成的代碼永不越界」**。即使跑過 100 萬個測試用例，極端邊界條件依然可能引發緩衝區溢位或資料競爭。

在 FLOW 的體系中，1-Bit 混沌引擎提議的任何候選實作，都必須接受純 C 打造的 SMT 形式化驗證引擎（`src/smt.c`）審計。

```text
SMT 形式化最高法院審查管線:
┌────────────────────────┐
│ 1-Bit 混沌退火候選實作 │
└───────────┬────────────┘
            │ 提議候選 Plan (FlowPlanAssignment & Metrics)
            ▼
┌────────────────────────────────────────────────────────────────────────┐
│ SMT-LIB2 QF_BV / QF_LIA 定理生成 (flow_smt_generate_proof_script)      │
├────────────────────────────────────────────────────────────────────────┤
│ 1. 定理一: 緩衝區邊界安全 (Buffer Bounds Safety)                       │
│ 2. 定理二: 記憶體配額有界性 (Memory Quota Boundedness)                 │
│ 3. 定理三: 分片無別名隔離 (Shard Non-Aliasing & Isolation)             │
│ 4. 定理四: 函數確定性不變量 (Functional Determinism Invariant)         │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ Bit-Blasting 命題邏輯求解
                                    ▼
                     ┌─────────────────────────────┐
                     │ 是否所有否定命題皆為 UNSAT? │
                     └──────┬───────────────┬──────┘
                            │ 是 (PROVEN)   │ 否 (SAT Counterexample)
                            ▼               ▼
                 ┌────────────────────┐   ┌────────────────────────┐
                 │ 簽發 SMT 證明背書  │   │ 硬約束立即否決 (Veto)  │
                 │ 允許發射機器碼     │   │ 將該基因變異機率歸零   │
                 └────────────────────┘   └────────────────────────┘
```

---

## 5.2 四大形式化定理

`src/smt.c` 透過 `flow_smt_generate_proof_script()` 動態為每一次編譯合成完整的 SMT-LIB2 形式化腳本，採用 **QF_BV（無量詞位元向量邏輯）** 與 **QF_LIA（無量詞線性整數算術）**：

### 1. 緩衝區邊界安全定理 (Buffer Bounds Safety)
證明在任意輸入長度 $L$ 下，訪問索引 $idx < L$ 恆嚴格小於分配容量 $C$。
其否定命題為：是否存在某個 $idx < L$ 滿足 $idx \ge C$？SMT 證明其為 **UNSAT**。

### 2. 記憶體配額有界性定理 (Memory Quota Boundedness)
證明預估消耗記憶體恆小於等於 `.flow` 宣告的配額邊界：
$$\text{EstimatedBytes} \le \text{MemoryQuotaBytes}$$
否定命題證明為 **UNSAT**。

### 3. 分片無別名隔離定理 (Shard Non-Aliasing & Isolation)
證明不同並發核心所持有的記憶體分片區間嚴格不相交（$\text{Disjoint}$）：
$$\forall i \ne j, \quad \text{Range}_i \cap \text{Range}_j = \emptyset$$
否定命題證明為 **UNSAT**，從數學上根除一切資料競爭（Data Race）。

### 4. 函數確定性不變量 (Functional Determinism Invariant)
證明相同輸入數據集在相同隨機種子下，輸出結果的雜湊值恆相等。

---

## 5.3 奧坎剃刀大統整：1-Cycle 多面體位元修剪

過去系統中曾存在獨立的 `src/verifier.c` 與 SMT 職責重疊。重構後，我們**將合約檢查與硬體邊界完整合流進 SMT 最高法院**：
*   **多面體硬限制遮罩**：`flow_verifier_get_contract_mask` 與 `flow_verifier_get_resource_mask` 直接在 SMT 頂層生效。
*   **1 個 CPU 週期修剪**：在混沌引擎生成狀態時，只需 1 條 bitwise-AND 指令，立即修剪 99.9% 違反 SMT 多面體邊界的非法狀態，大幅提升搜尋收斂速度！
