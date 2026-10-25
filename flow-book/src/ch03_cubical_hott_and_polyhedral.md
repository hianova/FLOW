# 第三章：64-Bit 離散同倫型別篩、全域型別宇宙 $\mathcal{U}_0$ 與多面體流形

> **給開發者的白話翻譯**：FLOW 把所有系統需求與硬體能力編碼成一個 **64-bit 整數遮罩（`uint64_t`）**，用 **1 個 CPU 時脈週期的位元運算**就能瞬間篩出合法的高性能元件；並透過 **內生立方同倫全域型別宇宙 $\mathcal{U}_0$（`FlowHoTTUniverse`）** 確保 Lean 4 與 C17 之間的型別同構即恆等（Univalence: $(A \simeq B) \implies (A = B)$）。

---

## 3.1 64-Bit 立方同倫型別篩與全域型別宇宙 $\mathcal{U}_0$ (`src/cubical_hott.c`)

FLOW 將語義 IR（`SemanticIR`）、系統核心結構與註冊元件（`Component`）映射至 [`src/cubical_hott.h`](../../src/cubical_hott.h) 與 [`src/cubical_hott.c`](../../src/cubical_hott.c) 的雙層同倫架構中：

1. **64-Bit 離散立方同倫型別篩（`FlowCubicalTypeSieve`）與 Kan Open-Box 單週期裁決**：
   - **6 大正交工程子空間（6 Orthogonal Subspaces）**：64 個位元被嚴格劃分為記憶體模型（Memory $\mathbb{I}^{[0..7]}$）、並發模型（Concurrency $\mathbb{I}^{[8..15]}$）、微架構親和（Microarch $\mathbb{I}^{[16..23]}$）、記憶體佈局（Layout $\mathbb{I}^{[24..31]}$）、物理相空間（Physical $\mathbb{I}^{[32..47]}$）與形式化安全門檻（Security $\mathbb{I}^{[48..63]}$）。
   - **Kan Open-Box 單週期相容性裁決**：給定規格意圖遮罩 $M_{\text{intent}}$、候選元件能力遮罩 $M_{\text{cand}}$ 與邊界不變量濾波器 $B$，檢查一個底層元件是否能安全承載該規格，簡化為單條 C17 位元指令：
     $$((M_{\text{intent}} \oplus M_{\text{cand}}) \land B) = 0$$
     只要結果為 `0`，代表該元件在記憶體與並發維度上與規格 100% 相容（`FLOW_KAN_FILLED_HOMOTOPIC`）。
2. **內生全域內涵型別宇宙 $\mathcal{U}_0$（`FlowHoTTUniverse`）與單價公理海關（Univalence Customs Gate）**：
   - 跨語言核心結構（如 64B `FlowExtentDescriptor` 與使用者 `.lean` 結構）在 `FLOW_HOTT_LANG_LEAN`（Lean 4）與 `FLOW_HOTT_LANG_C17`（C17）下註冊至 `FlowHoTTUniverse`（`flow_hott_register_type`）。
   - 每個型別項目（`FlowHoTTTypeEntry`）皆檢驗 `field_count`、`byte_size`、`align_bytes`、`fx_scale_bits` 與 `sieve_fingerprint`，並透過 `flow_hott_verify_univalence` 強制執行單價公理 $(A \simeq B) \implies (A = B)$。開發者可隨時執行 `./build/flowy topos` 或 `./build/flowy audit` 檢視宇宙 $\mathcal{U}_0$ 的單價同構狀態（`HoTT-U0:UNIVALENT`）。

---

## 3.2 精確數值流形、1-Bit 混沌突變與 Presburger 多面體排程 (`src/bitspace.c`, `src/polyhedral.c`)

1. **零浮點確定性數值流形（`FlowExactNumerics`，[`src/bitspace.h`](../../src/bitspace.h), [`src/bitspace.c`](../../src/bitspace.c)）**：
   - 內建跨平台零 IEEE-754 漂移之 **$Q32.32$ 定點數算術（`flow_fx64_t`）**、$Q16.16$ 快速近似超越函數、定點線性代數（`flow_fx_mat_mul_ikj`、`flow_fx_gradient_descent_step`）與 **256-bit 無符號大整數 Montgomery 模乘／Miller-Rabin 質數檢定（`FlowU256`，通過 `secp256k1` 驗證）**。
2. **1-Bit 混沌突變、5 階段 Token Ring 與 `.fvec` 架構記憶（[`src/bitspace.h`](../../src/bitspace.h), [`src/bitspace.c`](../../src/bitspace.c)）**：
   - 在通過同倫篩的合法子空間中執行單一位元翻轉搜尋，搭配零配置 5 階段 Token Ring 決策管線與 1024B 標頭的 `.fvec` 架構記憶庫。
3. **Presburger 仿射與二次多面體排程（[`src/polyhedral.c`](../../src/polyhedral.c)）**：
   - 自動根據 L1/L2 Cache 大小與資料 `stride`，求得最佳迴圈切塊尺寸 $T^*$ 與 SIMD 向量化寬度 $V^*$。
