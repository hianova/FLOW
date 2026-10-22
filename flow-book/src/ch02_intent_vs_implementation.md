# 第二章：意圖 vs. 實作 (Pure `.lean` 零試錯撰寫規範與雙前端降階)

> 「在 FLOW 中，資料結構的定義即是記憶體 ABI，安全定理的證明即是算子核函數的錨點。」

---

## 2.1 雙前端合一：Pure `.lean` 與宣告式 `.flow`

FLOW 的語義剖析器（[`src/parser.c`](../../src/parser.c)）支援兩種完全無命令式樣板的輸入形式，並將其統一降階為強型別的 `FlowSpec` 與 `SemanticIR`：

### 路徑 A：Pure `.lean`（領域意圖 + 形式化證明 + 算子核函數合一）

開發者或 AI 直接產出標準 Lean 4 檔案，**無需任何 `@[export]` 或 FLOW 專屬語法**：

```lean
namespace Embodied.GaitReflex

structure JointState where
  angle    : Float
  velocity : Float
  torque   : Float
  deriving Repr

def stepReflex (baseOff byteLen stride epoch : UInt64) : UInt32 :=
  if stride >= 24 && epoch > 0 && baseOff + byteLen >= baseOff then 0 else 1

theorem zmp_torque_safe : stepReflex 0 24 24 1 = 0 := by decide

end Embodied.GaitReflex
```

當 `parse_lean_spec()` 解析此檔案時，會自動完成三件事：
1. **原生 `structure` 記憶體佈局推導**：掃描 `JointState` 的欄位型別（3 個 `Float` $\rightarrow$ $3 \times 8 = 24\text{ bytes}$），自動設定 `input_stride_bytes = 24`、`input_align_bytes = 8` 並填入熱重載 `state_fields[]`。
2. **定理錨定算子推導（Theorem-Anchored Kernel Discovery）**：掃描所有非 `Prop` 的計算型 `def`（如 `stepReflex`），並在 `theorem zmp_torque_safe` 的型別與證明主體中檢驗其是否被引用驗證；一旦確認受定理保護，立即將 `stepReflex` 綁定為 `spec->extern_kernel`。
3. **零 `sorry` 鐵門與領域標籤提取**：嚴格拒絕任何未完成的 `sorry` 或自訂 `axiom`，並從 `namespace Embodied.*` 與定理名稱提取領域意圖供 Plugin 降階。

---

### 路徑 B：宣告式 `.flow`（管線拓樸與硬體邊界合約）

對於純資料流管線配置，使用者也可撰寫輕量的 `.flow` 區塊（或嵌入於 Markdown 的 ```` ```flow ```` 區塊中）：

```flow
project realtime_ranking

input user_scores {
    max_count 4096
    stride 64
    align 64
}

state score_table {
    shared
    read_heavy
    bounded
}

flow ranking_pipeline {
    user_scores -> sort -> top
}

require {
    deterministic
    memory < 64mb
}

prefer {
    latency
}
```

---

## 2.2 Pure `.lean` 零試錯撰寫鐵律（Zero-CoT Authoring Rules）

為了讓人類開發者與 AI Agent **一次編譯通過（零重試、不浪費推理 Token）**，撰寫 FLOW `.lean` 時請直接遵循以下四條鐵律：

### 鐵律 1：只使用零依賴 Core Lean 4 子集與 64B 對齊 `structure`
- **禁止外部套件**：不要寫 `import Mathlib`，FLOW 使用自給自足的原生 Lean 4 核心型別。
- **C17 型別直接映射**：
  - `UInt64` / `Nat` $\longrightarrow$ `uint64_t` (8 bytes)
  - `UInt32` $\longrightarrow$ `uint32_t` (4 bytes)
  - `Bool` $\longrightarrow$ `bool`
  - `Float` $\longrightarrow$ `double` (8 bytes)
- **最佳實踐**：將主狀態 `structure` 設計為 **8 個 `UInt64` 欄位（共 64 bytes）**，剛好對齊單一 CPU Cache Line（`stride = 64, align = 8`）。

### 鐵律 2：用 `:= by decide` 與 `:= by omega` 一行秒殺證明（注意 `Nat` 減法陷阱！）
- **常數驗收（Concrete Vectors）**：只要定理中全部是具體數字（例如驗證邊界測試向量），一律在結尾寫 `:= by decide`。
- **通用代數與不等式（Universal `∀` Bounds）**：只要涉及變數加減與大小比較，一律在結尾寫 `:= by omega`。
- **⚠️ 唯一常見陷阱（`Nat` 無號數減法截斷）**：
  - 在 Lean 4 中，自然數 `Nat` 當 `a < b` 時，`a - b` 會截斷為 `0`，因此 `(a - b) + b = a` **不一定成立**，直接跑 `by omega` 會報錯！
  - **標準解法**：凡是定理中出現 `a - b`，務必在參數中加入前提 `(h : b ≤ a)`，例如：
    ```lean
    def remainingQuota (total consumed : Nat) : Nat := total - consumed

    theorem quota_conservation (total consumed : Nat) (h : consumed ≤ total) :
        remainingQuota total consumed + consumed = total := by
      unfold remainingQuota
      omega
    ```

### 鐵律 3：三個可直接複製貼上的實戰設計模式（Copy-Paste Patterns）

1. **模式一：電商折價 / 金融風控防漏閥門（Monotonic Gate）**
   ```lean
   def isInvoiceSafe (grossCents discountCents floorCents : Nat) : Bool :=
     decide (discountCents ≤ grossCents ∧ grossCents - discountCents ≥ floorCents)

   theorem invoice_never_below_floor (gross disc floor : Nat) :
       isInvoiceSafe gross disc floor = true → gross - disc ≥ floor := by
     unfold isInvoiceSafe
     intro h
      revert h; decide +revert
   ```
2. **模式二：企業三態狀態機與最小反例修復（Tri-State & Min-Cut Remedy）**
   ```lean
   -- 0 = AUTO_PASS (自動放行), 1 = FORENSIC_HOLD (轉高管簽核), 2 = HARD_BLOCK (硬體級阻斷)
   def classifyOrder (driftBps budgetBps maxOverrideBps : Nat) : Nat :=
     if driftBps ≤ budgetBps then 0
     else if driftBps ≤ budgetBps + maxOverrideBps then 1
     else 2

   def minCutReduction (driftBps budgetBps : Nat) : Nat :=
     driftBps - budgetBps

   theorem min_cut_restores_budget (drift budget : Nat) (h : budget ≤ drift) :
       drift - minCutReduction drift budget = budget := by
     unfold minCutReduction
     omega
   ```
3. **模式三：檔案末尾的 1PB 零拷貝定理錨定核函數（Theorem-Anchored Kernel）**
   - 將檔案最後一個 `def` 寫成 64B `FlowExtentDescriptor` 檢查器，並用一條 `theorem` 引用它，`flowc` 就會自動將其綁定為 C17 主算子：
   ```lean
   def appStepKernel (baseOff byteLen stride epoch : UInt64) : UInt32 :=
     if stride >= 64 && byteLen >= 64 && baseOff + byteLen >= baseOff then 0 else 1

   theorem app_kernel_sound : appStepKernel 0 64 64 1 = 0 := by decide
   ```
