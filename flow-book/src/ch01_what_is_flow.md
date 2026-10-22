# 第一章：什麼是 FLOW？ (從分離式編譯到數學-AST 融合引擎)

> 「當數學證明本身就是 AST 的一部分，我們便不再需要手寫 FFI 膠水、防禦性分支與並發鎖樣板。」

---

## 1.1 傳統系統開發的三重割裂

在傳統高性能系統與 AI 算子開發中，一個演算法從理論走向生產環境，往往必須跨越三道人工鴻溝：
1. **規格與實作割裂**：形式化驗證寫在定理證明器中，實際執行的卻是另一份手寫 C/C++ 或 Rust 程式碼。
2. **FFI 標記與轉接負擔**：為了讓 Python、JS 或 CUDA 呼叫底層核心，開發者必須手動標註 `@[export]`、`extern "C"`，並對齊結構體偏移量。
3. **執行期並發與硬體調優硬編碼**：為了加上 RCU/QSBR 無鎖熱更新、NUMA 親和性或 SIMD 區塊排程，核心算子被大量非業務的基礎設施程式碼淹沒。

---

## 1.2 FLOW 的三層數學-AST 融合架構

FLOW 建立了以 **「Pure `.lean` / `.flow` 意圖與證明」、「Cubical HoTT 同倫篩與領域插件函子」、「QSBR 零拷貝執行基底」** 為核心的三位一體架構：

```text
FLOW 三層無縫融合:
                 ┌───────────────────────────────────────────┐
                 │  1. Pure .lean / .flow 單一真實來源       │
                 │  • 原生 structure -> 自動推導 Stride/ABI  │
                 │  • 定理錨定算子   -> 零 @[export] 綁定 def│
                 └─────────────────────┬─────────────────────┘
                                       │
                     領域插件函子降階 (lower_domain_semantics)
                                       ▼
    ┌──────────────────────────────────────────────────────────────────────┐
    │          2. 64-Bit Cubical HoTT 同倫篩 + 5-Stage Token Ring          │
    │   • 6 大正交代數子空間 (Memory / Concurrency / Microarch / ...)      │
    │   • Presburger 多面體排程 + 接觸噴流束 (.fjet) + 架構記憶庫 (.fvec)  │
    └──────────────────┬────────────────────────────────┬──────────────────┘
                       │                                │
           (SMT QF_LIA 四大定理守門)          (64B 快取行對齊原子切換)
                       ▼                                ▼
         ┌───────────────────────────┐    ┌───────────────────────────┐
         │ 3a. 零 sorry + 四大定理   │    │ 3b. QSBR 世代鎖 + 多語言  │
         │ buffer / memory / shard / │    │ 64B FlowExtentDescriptor  │
         │ determinism UNSAT 證明    │    │ (C/Rust/Py/JS/CUDA/MATLAB)│
         └───────────────────────────┘    └───────────────────────────┘
```

1. **零 FFI 樣板（Zero-`@[export]` Theorem-Anchored Discovery）**：在 `.lean` 中被 `theorem` 驗證的計算函數 `def`，自動成為系統核函數入口。
2. **意圖函子封裝複雜度（Plugins as Intent Functors）**：領域專家只需在 `.lean` 宣告 `namespace Embodied.*`，插件自動補齊 1kHz SLA 與辛幾何相空間約束。
3. **真實 AST 拓樸自省（Live AST Topology）**：`src/topology.c` 100% 動態掃描工作區 `#include` 相依圖與註冊元件，確保零跨層洩漏（`Leaks = 0`, `Modularity = 1.00`）。
