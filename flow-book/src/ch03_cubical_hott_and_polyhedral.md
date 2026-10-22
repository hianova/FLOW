# 第三章：64-Bit 離散同倫型別篩與 Presburger 多面體合成

> **給開發者的白話翻譯**：傳統編譯器在選擇底層資料結構（要用 Lock-Free Ring Buffer、Sharded Hash 還是 Linear Array？）與迴圈切塊（Loop Tiling）時，通常依賴緩慢的啟發式搜尋。FLOW 把所有系統需求與硬體能力編碼成一個 **64-bit 整數遮罩（`uint64_t`）**，用 **1 個 CPU 時脈週期的位元運算**就能瞬間篩出唯一合法且最快的高性能元件。

---

## 3.1 64-Bit 立方同倫型別篩 (`src/cubical_hott.c`)

FLOW 將語義 IR（`SemanticIR`）與註冊元件（`Component`）映射為 64-Bit 離散立方同倫型別篩（`FlowCubicalTypeSieve`，定義於 [`src/cubical_hott.h`](../../src/cubical_hott.h)）：

- **6 大正交工程子空間（6 Orthogonal Subspaces）**：
  64 個位元被嚴格劃分為記憶體模型（Memory）、並發模型（Concurrency）、微架構親和（Microarch）、順序與決定論（Ordering）、硬體加速（Accelerator）與形式化安全門檻（Verification）。
- **Kan Open-Box 單週期相容性裁決**：
  給定使用者意圖遮罩 $M_{\text{intent}}$、候選元件能力遮罩 $M_{\text{cand}}$ 與邊界不變量濾波器 $B$，檢查一個底層元件是否能安全承載該業務邏輯，簡化為單條 C17 位元指令：
  $$((M_{\text{intent}} \oplus M_{\text{cand}}) \land B) = 0$$
  只要結果為 `0`，代表該元件在記憶體與並發維度上與規格 100% 相容（`KAN_FILLED`）。

---

## 3.2 1-Bit 混沌突變與 Presburger 多面體排程 (`src/polyhedral.c`)

1. **1-Bit 混沌突變與 5 階段 Token Ring（[`src/bitspace.c`](../../src/bitspace.c), [`src/token_ring.c`](../../src/token_ring.c)）**：
   - 僅在通過同倫篩的合法子空間中執行單一位元翻轉搜尋，保證在 8 個步進內收斂至李雅普諾夫能量（Lyapunov Energy $E$）最低的最佳架構組態。
2. **Presburger 仿射與二次多面體排程（[`src/polyhedral.c`](../../src/polyhedral.c)）**：
   - 自動根據 L1/L2 Cache 大小與資料 `stride`，求得最佳迴圈切塊尺寸 $T^*$ 與 SIMD 向量化寬度 $V^*$，讓產出的 C17 程式碼不需手動調優即可跑滿硬體頻寬。
