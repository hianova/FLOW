# 第四章：Pure `.lean` 三層無縫融合與 AWS Cedar 真實世界驗證

FLOW 將 `.lean` 提升為 **「領域意圖 + 數學證明 + 算子核函數」三位一體的單一真實來源（Single Source of Truth）**：

## 4.1 三層自動推導機制

1. **零 `@[export]` 之定理錨定算子推導 ([`src/parser.c`](../../src/parser.c))**：
   - 開發者只需撰寫標準 Lean 4 的 `structure`（資料型別）、`def`（純計算函數）與 `theorem`（安全邊界證明）。
   - 解析器支援跨行 `/- ... -/` 區塊註解剝離、`public`/`private` 修飾詞與通用代數資料型別（ADT）8-byte ABI 對齊，自動從 `structure` 推導 `stride`、`align` 與熱重載欄位，並將被 `theorem` 引用驗證的 `def` 自動綁定為 `extern_kernel`，於 [`src/backend.c`](../../src/backend.c) 合成帶有 `__attribute__((weak))` 的原生 C 核函數。
2. **Plugin 領域意圖函子降階 ([`plugins/embodied.c`](../../plugins/embodied.c))**：
   - 當 `.lean` 宣告 `namespace Embodied.*` 與 `theorem zmp_torque_safe` 時，`EMBODIED_PLUGIN` 自動透過 `lower_domain_semantics` 展開 1kHz 實時迴圈 SLA、辛幾何（Symplectic Jet）相空間約束與 ZMP 穩定多邊形遮罩。
3. **SMT QF_LIA 四大定理與零 `sorry` 鐵門 ([`src/smt.c`](../../src/smt.c), [`src/abi.c`](../../src/abi.c))**：
   - 強制檢驗緩衝區邊界（`buffer_bounds`）、記憶體配額（`memory_quota`）、分片正交（`shard_isolation`）與決定論（`determinism`），並嚴格拒絕任何含 `sorry` 或自訂 `axiom` 之輸入。

---

## 4.2 工業級驗收基準：AWS Cedar (`tests/cedar_authorization.lean`)

在 [`tests/cedar_authorization.lean`](../../tests/cedar_authorization.lean)（427 行 / 16.4 KB）中，我們直接引入了 **AWS Cedar (`cedar-policy/cedar-spec`)** 官方形式化驗證專案的核心授權規格與安全證明（完全未加任何 `@[export]` 或 FLOW 專屬語法）：

- **原生資料結構**：`public structure Request`（4 欄位：`principal`, `action`, `resource`, `context`）與 `public structure Response`（3 欄位：`decision`, `determiningPolicies`, `erroringPolicies`），由 FLOW 自動推導出 `state_field_count = 7`、`input_stride_bytes = 56`、`input_align_bytes = 8`。
- **定理錨定算子**：由 `forbid_trumps_permit`、`default_deny`、`allowed_iff_explicitly_permitted_and_not_denied`、`order_and_dup_independent` 等 10+ 條機械化定理自動錨定 `public def isAuthorized` 為系統核心算子。
- **單行指令直通執行**：透過 `./build/flowy run tests/cedar_authorization.lean`，直接完成證明審計、C + QSBR 動態庫合成與 1PB 零拷貝 `FlowExtentDescriptor` 驗證（涵蓋於 `tests/test_system.c` Stage 11c）。
