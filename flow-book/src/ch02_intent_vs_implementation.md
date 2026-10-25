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

### 路徑 B：宣告式 `.flow` 硬體與執行期清單（Where & How：90% 純 `.lean` vs. 10% `.lean` + `.flow`）

對於 90% 的純演算法、金融定價與權限引擎，**單一 `.lean` 檔案即為全部**；而當系統需要綁定實體感測器驅動（IMU / SocketCAN）、嚴格微秒級 SLA 預算或自訂 QSBR 熱重載策略時，只需撰寫宣告式 `.flow` 清單（如 [`plugins/embodied.flow`](../../plugins/embodied.flow)），透過 `bind` 直接吸收 `.lean` 的純數學核心，徹底分離 **What（數學定律）** 與 **Where & How（硬體拓樸與執行期約束）**：

```flow
project embodied_reflex
bind "plugins/embodied.lean"

@quota {
    deterministic
    lean_certified
    deadline 200us
    arena 64kb
    memory < 16mb
    fallback emergency_stop
}

@embodied {
    frequency 1khz
    symplectic
    max_drift 0.5%
    zmp_margin 0.02
    ingress sensor_imu_6dof
    egress socketcan_fd
    coordinates [hipPitch, hipRoll, kneePitch, anklePitch, ankleRoll, torsoYaw, comX, comY]
}

@reload {
    schema_version 2
    qsbr_timeout 5ms
    migrate embodiedStepKernel
}
```

此外，`@reload` 支援跨結構形式化狀態遷移（`from_schema StateV1`、`to_schema StateV2` 與 `migrate migrateV1ToV2`），而當多個實時 `.lean` 模組（例如感測器濾波、步態反射、扭矩限幅）需串接為零拷貝流水線時，可在 `.flow` 中使用 `@patchbay` 區塊宣告拓樸與各階段 WCET 預算：
- `stage <name> bind "<lean_path>" fn <fn_name> in <InStruct> out <OutStruct> wcet <time>`
- `wire <stage_1> -> <stage_2> -> <stage_3>`

`flowc` 會在編譯期自動驗證相鄰階段之 `OutStruct == InStruct` 與 byte stride 一致性、以 AST 指令成本模型推導靜態 WCET 下界，並由 SMT 證明總和延遲 $\sum \text{wcet}_i \le \text{deadline}$。同時，在將 `.lean` 的 `Float` 函數與結構轉換函數降階為 C17 時，會自動映射至無損近似 $Q32.32$ 定點數核心（`FlowExactNumerics`：`flow_fx64_t` / `FlowU256`，以 128-bit 中間精度與 `FLOW_FX64_NONE` 故障隔離取代有損 IEEE-754 浮點運算）、**自動注入 `!isfinite()` NaN/Inf 防火牆**，並合成零開銷 `--target-c-header` `static inline` 雙軌算子（`flowc_<fn>` 與純定點 `flowc_<fn>_fx`）與具名相空間視圖 `FlowJetView_<flow_name>`（`view.q->hipPitch`）。

### 2.1.1 `@keyword` 與 `.flow` / `.lean` 宣告式語法全集速查表

| `@keyword` / 指令 | 所屬層級 | 核心子指令與語法 | 編譯器與執行期語義 |
| :--- | :--- | :--- | :--- |
| **`@quota { ... }`**<br>*(別名：`quota`, `require`)* | `.flow` 硬體與預算契約 | `deterministic`, `lean_certified`, `deadline <time>`, `arena <size>`, `memory < <n>mb`, `fallback <policy>` | 強制零 `sorry` 證明與 $Q32.32$ 確定性定點運算；預配置 64B 對齊 + Page Pre-Faulting + `mlock` 靜態 Arena；啟動硬體週期看門狗（超時觸發 `-4` 緊急停機閂鎖）。 |
| **`@embodied [name] { ... }`**<br>*(別名：`embodied`, `jet`)* | `.flow` 具身相空間與 I/O | `frequency <hz>`, `symplectic`, `max_drift <val>`, `zmp_margin <val>`, `dt <time>`, `dim <n>`, `ingress <drv>`, `egress <drv>`, `coordinates [...]` | 自動綁定 `flow.embodied` 領域插件与 `zmp_torque_safe` 合約；執行辛幾何多面體配速並合成零偏移具名座標視圖 `FlowJetView_<id>`。 |
| **`@reload { ... }`**<br>*(別名：`reload`)* | `.flow` 無鎖熱重載與遷移 | `schema_version <int>`, `qsbr_timeout <time>`, `from_schema <S1>`, `to_schema <S2>`, `migrate <lean_fn>` | 配置 64B 快取行對齊之 QSBR 世代租約（含 **發布前預留空間 Reservation-Before-Publication** 上限 `FLOW_RELOAD_MAX_RETIRED_DEPTH = 64`）；自動降階經定理驗證之 `S1 -> S2` 狀態遷移算子。 |
| **`@patchbay { ... }`**<br>*(別名：`patchbay`)* | `.flow` 多階段零拷貝流水線 | `stage <name> bind "<.lean>" [fn <f>] [in <S1>] [out <S2>] [wcet <= <t>]`<br>`wire <s1> -> <s2> -> ...` | 串接多個 `.lean` 核函數為 DAG 流水線；編譯期靜態驗證相鄰階段型別與 Stride 一致性、AST WCET 下界，並由 SMT 證明總和延遲 $\sum \text{wcet}_i \le \text{deadline}$。 |
| **`@[export <sym>]`** | `.lean` 屬性標註 *(選用)* | `@[export myStepKernel]` | 顯式指定 `spec->extern_kernel` 主算子名稱（預設無需撰寫，`flowc` 會自動從 `theorem` 錨定推導並匯出所有純函數）。 |
| **`project` / `bind` / `import` / `thermal`** | `.flow` 頂層指令 | `project <name>`, `bind "<.lean>"`, `import <plugin>`, `thermal { max_temp, target_temp, power_limit, r_thermal, c_thermal }` | 宣告專案名稱、綁定純 `.lean` 數學核心、載入領域插件，以及定義硬體熱力學包絡線參數。 |

---

## 2.2 Pure `.lean` 零試錯撰寫鐵律（Zero-CoT Authoring Rules）

為了讓人類開發者與 AI Agent **一次編譯通過（零重試、不浪費推理 Token）**，撰寫 FLOW `.lean` 時請直接遵循以下四條鐵律：

### 鐵律 1：遵守 `Lean-RT` 零 GC 實時子集、巢狀 `structure` 展平與硬體編解碼內建函數
- **禁止外部套件與動態配置（`Lean-RT` Zero-GC Gate）**：不要寫 `import Mathlib`，且嚴格禁止 `List`、`Array`、`ByteArray`、`String`、高階閉包（`fun x =>`）、自遞迴函數或遞迴 `inductive` 型別。這確保所有算子皆為 O(1) 堆疊記憶體與零 GC 暫停。
- **C17 型別直接映射與巢狀 `structure` / 有限 `inductive` 支援**：
  - `UInt64` / `Nat` $\longrightarrow$ `uint64_t` (8 bytes)
  - `UInt32` $\longrightarrow$ `uint32_t` (4 bytes)
  - `Bool` $\longrightarrow$ `bool`
  - `Float` $\longrightarrow$ `double` (8 bytes)
  - 有限列舉 `inductive Mode | Idle | Torque` $\longrightarrow$ `uint32_t` (`0, 1, ...`)
  - 巢狀 `structure`（如 `GaitFrame` 內含 `ImuSample`）與鏈式欄位存取（`f.imu.ax`）會自動展平為自然對齊之 C ABI 結構（`f.imu_ax`），並自動產生對應的 Python `ctypes.Structure` 與 Rust `#[repr(C)] pub struct`。
- **硬體量化與反量化內建函數**：直接呼叫 `flow_quantize val scale offset min_q max_q`（`Float -> UInt64`）與 `flow_dequantize q scale offset`（`UInt64 -> Float`），`flowc` 會自動降階為無分支飽和 C17 硬體轉換指令。
- **最佳實踐**：將主狀態 `structure` 設計為 **8 個 `UInt64`/`Float` 欄位（共 64 bytes）**，剛好對齊單一 CPU Cache Line（`stride = 64, align = 8`）。

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
