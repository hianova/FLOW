# 第一章：什麼是 FLOW？ (從分離式編譯到數學-AST 融合引擎)

> 「當數學證明本身就是 AST 的一部分，我們便不再需要手寫 FFI 膠水、防禦性分支與並發鎖樣板。」

---

## 1.1 傳統系統開發的三重割裂（為什麼 AI 寫的 Code 還是會在 Production 出事？）

在傳統系統與企業核心開發中，一個演算法或商業規則從需求走向生產環境，往往必須跨越三道人工鴻溝：
1. **規格與實作割裂**：PM 或架構師寫在文件裡的規則（例如「折扣後毛利不得低於 5%」、「抽卡 90 抽內必出保底」），到了工程師或 AI 手上變成四散的 `if/else`。只要漏掉一個極端邊界，上線就會變成「標錯價之亂」或資金虧損。
2. **FFI 標記與轉接負擔**：為了讓 Python (FastAPI)、Node.js/Bun、Rust 或 CUDA 共用同一套高速核心，開發者必須手動標註 `@[export]`、`extern "C"`，並小心翼翼地對齊每個欄位的 Byte Offset。
3. **執行期並發與硬體調優硬編碼**：為了線上熱更新風控參數（Hot-Swap）或防止高負載停頓，核心算子被大量的 Mutex 讀寫鎖與記憶體管理程式碼淹沒。

---

## 1.2 FLOW 的三層數學-AST 融合架構與七大元件作用域

FLOW 將核心引擎收斂為 **7 大正交元件作用域（`frontend`、`formal_sieve`、`exact_manifold`、`runtime_jet`、`polyglot_abi`、`introspection_ui`、`rust_sdk`）**，建立以 **「Pure `.lean` / `.flow` 意圖與證明」、「Cubical HoTT 單價宇宙 $\mathcal{U}_0$ × 精確流形」、「QSBR 零拷貝執行基底與七語同態投影」** 為核心的三位一體架構：

```text
FLOW 三層無縫融合 (7 Component Scopes | Leaks = 0 | Modularity = 1.00):
                 ┌───────────────────────────────────────────┐
                 │  1. Pure .lean / .flow 單一真實來源       │
                 │  • 原生 structure -> 自動推導 Stride/ABI  │
                 │  • 定理錨定算子   -> 零 @[export] 綁定 def│
                 └─────────────────────┬─────────────────────┘
                                       │
                     領域插件函子降階 (plugins/embodied.c)
                                       ▼
    ┌──────────────────────────────────────────────────────────────────────┐
    │   2. 64-Bit Cubical HoTT 單價宇宙 U_0 + 精確流形 (`bitspace.c`)       │
    │   • 內源性 HoTT U_0 單價海關 (Lean4 ~= C17 ~= Rust) + 外源性 topo 雷達│
    │   • Q32.32 定點數 + 256-Bit Montgomery + 5-Stage Token Ring + .fvec   │
    └──────────────────┬────────────────────────────────┬──────────────────┘
                       │                                │
           (SMT QF_LIA 四大定理守門)          (64B 快取行對齊原子切換)
                       ▼                                ▼
         ┌───────────────────────────┐    ┌───────────────────────────┐
         │ 3a. 零 sorry + 四大定理   │    │ 3b. QSBR 世代鎖 + 七語言  │
         │ buffer / memory / shard / │    │ 64B FlowExtentDescriptor  │
         │ determinism UNSAT 證明    │    │ (C/Rust/Py/JS/CUDA/M/Jl)  │
         └───────────────────────────┘    └───────────────────────────┘
```

### 工程師如何解讀 `flowc` 的編譯輸出？

當你執行 `flowc spec.lean -o build/app.c` 時，終端機輸出的每一行都對應一項具體的工程保證：

| 編譯器輸出訊息 | 白話工程意義 |
| :--- | :--- |
| `verifier: status=proven capacity=1024 estimated_bytes=8192` | **邏輯與記憶體上限已證明**：所有 `.lean` 定理皆無 `sorry`，且編譯器保證該模組在執行期最多只會使用 **8 KB** 靜態記憶體（零 GC、零記憶體洩漏）。 |
| `SMT proof: status=unsat_proven_sound (4/4 theorems verified UNSAT)` | **零反例保證（UNSAT = Unsatisfiable）**：SMT 求解器嘗試在所有可能輸入中尋找「能讓陣列越界、記憶體超標、多執行緒競爭或結果不一致」的反例，結果為「無法滿足（找不到任何漏洞）」。 |
| `symplectic: omega=2.000 mu=0.000977 q_sat=1280.0` | **硬體節流與背壓保護**：編譯器已自動計算出最佳迴圈區塊（Tile）與佇列飽和門檻（`q_sat`），避免高併發時把 CPU 快取打爆。 |
