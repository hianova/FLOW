# 第六章：JIT 代碼發射與幾何變形 (AoS 到 SoA 即時重映射與生存模式)

> 「程式碼不是雕刻在石頭上的死文字，而是能在記憶體中自發變形的黏土。當記憶體即將崩潰時，系統在微秒內完成拓樸幾何變形，從 AoS 切換至緊緻 SoA，躲過 OS OOM Killer 的致命屠刀。」

---

## 6.1 JIT 代碼發射與多後端管線 (`src/jit.c`, `src/backend.c`)

FLOW 擁有超輕量的 JIT 即時代碼發射器，能將收斂並經 SMT 認證的拓樸方案瞬間編譯為本地機器代碼或 C 原始碼：
*   **多目標發射能力**：同步支援 C 原始碼、標準 C 標頭檔、Rust FFI 綁定、Python 介面，以及 MLIR / LLVM IR。
*   **零拷貝資料排布**：遵循嚴格的 C11 記憶體對齊標準，跨語言呼叫時記憶體零拷貝。

---

## 6.2 幾何變形：AoS 與 SoA 的動態視圖切換

在不同的並發與硬體條件下，記憶體佈局的優劣完全相反：
*   **AoS (Array of Structures)**：適合需要頻繁存取完整實體欄位的場景，但對 SIMD 向量化極不友善。
*   **SoA (Structure of Arrays)**：將同屬性欄位連續存放，能徹底發揮 AVX-512 / ARM NEON 向量暫存器威力，且快取行利用率極高。

```text
記憶體幾何變形 (AoS <-> SoA):
[AoS 視圖] [X0 Y0 Z0][X1 Y1 Z1][X2 Y2 Z2]... (實體資料密集)
     │
     ▼ 50 微秒內虛擬記憶體分頁重映射 (mremap)
[SoA 視圖] [X0 X1 X2...][Y0 Y1 Y2...][Z0 Z1 Z2...] (向量化連續記憶體)
```

FLOW 利用 Linux 核心的 `mremap` 虛擬記憶體分頁重映射技術，在**零記憶體拷貝**的前提下，達成微秒級的記憶體幾何形變。

---

## 6.3 記憶體高水位與生存模式避難所 (Static Survival Mode)

當伺服器遭遇極限負載（例如可用記憶體瞬間跌破安全臨界值 16MB）：
1. **自我意識否決 (Self-Aware JIT Veto)**：
   JIT 編譯器本身需要數十 MB 的 AST 與符號空間。在極端低記憶體下調用 JIT 會立即觸發 Linux OOM Killer。FLOW 具備自我意識感知，在此狀態下**嚴禁 Fork JIT 編譯器**。
2. **靜態生存模式 (Static Survival Mode)**：
   指針立即路由至預先分配、零動態記憶體分配的極限避難所模式（`Static_Survival_Mode`）。
3. **施密特觸發器 (Schmitt Trigger Hysteresis)**：
   設定上門檻（如 105MB）與下門檻（如 95MB），徹底杜絕系統在高低負載震盪時產生抖動（Flapping）。
