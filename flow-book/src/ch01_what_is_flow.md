# 第一章：什麼是 FLOW？ (從分離式編譯到 Pure Lean 4 $\to$ C17 湧現引擎)

> 「當數學定理直接湧現為 C17 硬體保護殼，而硬體管線直接寫在顯式 C17 中，我們便不再需要任何第三種配置語言或樣板程式碼。」

---

## 1.1 傳統系統開發的三重割裂

在傳統系統與實時核心開發中，一個演算法從數學規格走向生產環境，往往必須跨越三道人工鴻溝：
1. **規格與實作割裂**：規格寫在文件或定理證明器裡，到了工程師手上卻變成手寫的 C/C++ `if/else`。只要漏掉一個極端邊界或 NaN 檢查，上線就會變成重大事故。
2. **手寫 FFI 與字串拼貼樣板**：為了把數學公式轉成 C 函式，傳統工具往往塞入大量硬編碼的名詞白名單或假管線模板。
3. **過度設計的 `@` 註解配置語言**：用自創的 `@quota`、`@patchbay` DSL 去描述原本在 C17 `main.c` 裡只需 10 行就能寫清楚的 Ring Buffer 與執行緒配置。

---

## 1.2 FLOW 的四大數學同構映射（4 Pure Mathematical Isomorphisms）

FLOW 的核心編譯器 `flowc`（[`src/parser.c`](../../src/parser.c)、[`src/backend.c`](../../src/backend.c)、[`src/abi.c`](../../src/abi.c)）拿掉了所有寫死的業務名詞與假管線模板，整個「Lean 4 湧現 C17」完全收斂為 **4 條純粹的數學同構映射**：

```text
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                    FLOW: 4 Pure Mathematical Isomorphisms (Lean 4 -> C17)              │
├────────────────────────────────────────────────────────────────────────────────────────┤
│                                                                                        │
│  1. 積型別湧現記憶體佈局 (Product Types -> C17 Structs)                                │
│     • Lean `structure S` 與多回傳值 `(A × B)` -> C17 `typedef struct` (`<fn>_tuple_t`) │
│     • 自動抹除 0-byte `Prop` 證明欄位，純數據欄位自然決定 alignment 與 byte stride     │
│                                                                                        │
│  2. 和型別湧現狀態空間 (Sum Types -> C17 Enums)                                        │
│     • Lean `inductive E` -> C17 `typedef enum`                                         │
│                                                                                        │
│  3. 純函數複合湧現算子網路 (Pure Functions -> C17 Static Inline Graph)                 │
│     • 每個 `def f` 一對一映射為 C17 算子 (`f`, `f_fx`, `flowc_f`, `flowc_f_fx`)        │
│     • `let x := rhs in` 與 `let (a, b) := g x in` 直接映射為原生 C17 區域變數與函式呼叫│
│                                                                                        │
│  4. 定理湧現硬體級防護邊界 (Theorems -> C17 Hardware Contracts `lean_<fn>`)            │
│     • 定理前提 (Hypotheses `(h : P x)` 或 `P x → Q (f x)`) -> `FLOW_LEAN_PRE_GUARD`    │
│     • 定理結論 (Conclusion `Q (f x)`) -> `FLOW_LEAN_POST_GUARD`                        │
│     • 多條定理針對同一算子 `f` 自動取邏輯交集：(/\ P_i) => (/\ Q_j)                    │
│                                                                                        │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

### 工程師如何解讀 `flowc` 的編譯輸出？

當你執行 `flowc spec.lean -o build/app.c --target-c-header build/app.h` 時，終端機輸出的每一行都對應一項具體的工程保證：

| 編譯器輸出訊息 | 白話工程意義 |
| :--- | :--- |
| `verifier: status=proven capacity=1024 estimated_bytes=8192` | **邏輯與記憶體上限已證明**：所有 `.lean` 定理皆無 `sorry` / 自訂 `axiom`，且通過 `Lean-RT` 零 GC 靜態閘門（零動態配置、零記憶體洩漏）。 |
| `SMT proof: status=unsat_proven_sound (4/4 theorems verified UNSAT)` | **零反例保證（UNSAT = Unsatisfiable）**：SMT 求解器證明 `buffer_bounds`、`memory_quota`、`shard_isolation`、`determinism` 四大不變量在所有合法輸入下皆無反例。 |
| `symplectic: omega=2.000 mu=0.000977 q_sat=1280.0` | **硬體節流與背壓參數**：根據資料對齊與容量推導出的多面體區塊與相空間阻尼參數。 |
