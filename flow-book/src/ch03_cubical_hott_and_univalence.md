# 第三章：64-Bit 離散同倫型別篩與單價宇宙 $\mathcal{U}_0$ (Cubical HoTT & Univalence Universe)

> **給開發者的白話翻譯**：FLOW 將系統規格與底層架構能力編碼為 **64-bit 離散同倫遮罩（`uint64_t`）**，透過 **De Morgan 抽象區間與 Kan 開盒填塞條件**，以單一 CPU 時脈週期的位元運算瞬間判定空間同倫等價性；並透過 **全域型別宇宙 $\mathcal{U}_0$（`FlowHoTTUniverse`）** 確保 Lean 4 規格型別與 C17 實作型別之間的型別同構即恆等（Univalence: $A \simeq B \implies A =_{\mathcal{U}_0} B$）。

---

## 3.1 64-Bit 立方同倫型別篩與全域型別宇宙 $\mathcal{U}_0$ (`src/cubical_hott.h`, `src/cubical_hott.c`)

FLOW 將語義 IR（`SemanticIR`）、系統核心結構與註冊元件（`Component`）映射至 [`src/cubical_hott.h`](../../src/cubical_hott.h) 與 [`src/cubical_hott.c`](../../src/cubical_hott.c) 的同倫幾何架構中：

1. **64-Bit 離散立方同倫型別篩（`FlowCubicalTypeSieve`）與 Kan Open-Box 單週期裁決**：
   - **6 大正交工程子空間（6 Orthogonal Subspaces）**：64 個位元被嚴格劃分為記憶體模型（Memory $\mathbb{I}^{[0..7]}$）、並發模型（Concurrency $\mathbb{I}^{[8..15]}$）、微架構親和（Microarch $\mathbb{I}^{[16..23]}$）、記憶體佈局（Layout $\mathbb{I}^{[24..31]}$）、物理相空間（Physical $\mathbb{I}^{[32..47]}$）與形式化安全門檻（Security $\mathbb{I}^{[48..63]}$）。
   - **Kan Open-Box 單週期相容性裁決**：給定規格意圖遮罩 $M_{\text{intent}}$、候選元件能力遮罩 $M_{\text{cand}}$ 與邊界不變量濾波器 $B$，檢查底層元件是否能安全承載該規格：
     $$((M_{\text{intent}} \oplus M_{\text{cand}}) \land B) = 0$$
     只要結果為 `0`，代表該元件在幾何拓撲上與規格同倫等價（`FLOW_KAN_FILLED_HOMOTOPIC`）。
2. **內生全域內涵型別宇宙 $\mathcal{U}_0$（`FlowHoTTUniverse`）與單價公理檢驗**：
   - 跨語言核心結構（如 64B `FlowExtentDescriptor` 與使用者定義的 `.lean` 結構）在 `FLOW_HOTT_LANG_LEAN`（Lean 4）與 `FLOW_HOTT_LANG_C17`（C17）下註冊至全域宇宙 $\mathcal{U}_0$（`flow_hott_register_type`）。
   - 每個型別項目（`FlowHoTTTypeEntry`）皆精確檢驗 `field_count`、`byte_size`、`align_bytes`、`fx_scale_bits` 與同倫指紋，並透過 `flow_hott_verify_univalence` 驗證單價性定理 $(A \simeq B) \implies (A =_{\mathcal{U}_0} B)$。
   - 開發者可隨時透過終端執行 `./build/flowy topos` 或 `./build/flowy audit` 檢驗型別宇宙 $\mathcal{U}_0$ 的單價同構狀態（`HoTT-U0:UNIVALENT`）。

---

## 3.2 確定性數值流形與多胞形邊界約束 (`src/bitspace.h`, `src/cubical_hott.h`)

1. **確定性數值流形（`FlowExactNumerics`，[`src/bitspace.h`](../../src/bitspace.h), [`src/bitspace.c`](../../src/bitspace.c)）**：
   - 提供跨平台零 IEEE-754 漂移之 **$Q32.32$ 定點數算術（`flow_fx64_t`）**、$Q16.16$ 快速超越函數、定點線性代數（`flow_fx_mat_mul_ikj`、`flow_fx_gradient_descent_step`）與 **256-bit 無符號大整數 Montgomery 模乘／Miller-Rabin 質數檢定（`FlowU256`，通過 `secp256k1` 驗證）**。
2. **幾何多胞形邊界系統（`FlowPolyhedron`，[`src/cubical_hott.h`](../../src/cubical_hott.h)）**：
   - 將仿射約束（`FlowAffineConstraint`）與多面體結構統一內聚於同倫標頭檔中，利用整數線性不等式 $A \cdot x \le b$ 定義多維參數之封閉凸多面體，在編譯期求得最佳排程與對齊邊界，保證實時管線絕不超出硬體快取容量。
