# 第二章：意圖 vs. 實作 (Pure `.lean` 與 `.flow` 雙前端語義降階)

> 「在 FLOW 中，資料結構的定義即是記憶體 ABI，安全定理的證明即是算子核函數的錨點。」

---

## 2.1 雙前端合一：Pure `.lean` 與宣告式 `.flow`

FLOW 的語義剖析器（[`src/parser.c`](../../src/parser.c)）支援兩種完全無命令式樣板的輸入形式，並將其統一降階為強型別的 `FlowSpec` 與 `SemanticIR`：

### 路徑 A：Pure `.lean`（領域意圖 + 形式化證明 + 算子核函數合一）

開發者或定理證明工具直接產出標準 Lean 4 檔案，**無需任何 `@[export]` 或 FLOW 專屬語法**：

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

## 2.2 從 `FlowSpec` 到 `SemanticIR` 與 Plugin 意圖函子降階

解析後的 `FlowSpec` 經由 `lower_to_ir()`（[`src/parser.c`](../../src/parser.c)）轉化為 `SemanticIR`，並依序調用已註冊插件的 `lower_domain_semantics` 函子（例如 [`plugins/embodied.c`](../../plugins/embodied.c)）：

```text
Pure .lean / .flow
   │
   ├──► parse_spec() / parse_lean_spec() (src/parser.c)
   │       • 推導 structure 欄位與 stride/align
   │       • 由 theorem 自動錨定 extern_kernel 算子
   │
   ├──► lower_to_ir() (src/parser.c)
   │       • 產出基礎 SemanticIR 與 Presburger 多面體邊界
   │
   └──► plugin->lower_domain_semantics(spec, ir, plugin) (plugins/embodied.c)
           • 將 Embodied 意圖自動降階為 1000us (1kHz) 實時 SLA
           • 自動合成 Symplectic Contact-Jet 辛幾何相空間約束
```

| 維度 | 傳統手寫系統 | FLOW 自動推導與合成 |
| :--- | :--- | :--- |
| **記憶體 Stride / 對齊** | 手寫 `sizeof`、`alignas` 與跨語言 struct 定義 | 從 Lean `structure` 欄位自動推導並產生 64B `FlowExtentDescriptor` |
| **算子 FFI 匯出** | 手動標註 `@[export]`、`extern "C"` 與膠水函數 | 定理錨定自動識別 `def`，由 `backend.c` 合成 `__attribute__((weak))` 原生核函數 |
| **資料結構與並發** | 硬編碼 `std::mutex` 或特定 Hash Table | 透過 64-Bit Cubical HoTT 篩選器與 Token Ring 自動收斂最佳無鎖元件 |
| **熱更新與記憶體回收** | 手動管理讀寫鎖或停機重啟 | 自動包裝 `acquire_epoch` / `release_epoch` QSBR 世代租約 |
