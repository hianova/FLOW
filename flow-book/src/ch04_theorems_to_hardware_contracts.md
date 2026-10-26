# 第四章：定理湧現為硬體合約：從數學證明到 C17 執行期防護網 (Theorems to C17 Contracts)

FLOW 將 `.lean` 中的數學證明直接轉化為 C17 硬體級執行期防線與靜態不變量驗證。

---

## 4.1 形式化驗證與硬體合約合成機制

1. **結構型定義域前置斷言與定理實質蘊含合約（`structure` `Prop` & `theorem` $\to$ `lean_<fn>`，[`src/parser.c`](../../src/parser.c), [`src/backend.c`](../../src/backend.c)）**：
   - **結構型定義域假設（Structural / Domain Precondition）**：來自輸入 `structure` 內嵌的 `Prop` 欄位（如 `h_wv : w_v ≥ 0`、`h_wa : w_a > 0`）與 `def` 簽名中的依賴證明參數，直接編譯為執行前輸入邊界斷言 `FLOW_LEAN_PRE_GUARD(domain_ok, fallback)`。
   - **定理實質蘊含守衛（Material Implication Guard $P_k(x) \implies Q_k(x, \text{\_ret})$）**：凡是針對算子 `def f` 證明的 `theorem`，其前提 $P_k(x)$ 與結論 $Q_k(x, \text{\_ret})$ 直接編譯為實質蘊含斷言 `FLOW_LEAN_IMPLIES_GUARD(P_k(x), Q_k(x, _ret), fallback)`（即 `if ((P_k(x)) && !(Q_k(x, _ret))) return fallback;`）。
   - 當多條局部 `theorem`（例如互斥的 `v > 0` 上漲定理與 `v < 0` 下跌定理，或 `inductive` 狀態機的狀態轉移定理）針對同一個 `f` 時，所有定理守衛無縫疊加並織成嚴密的執行期防護網，合成出 `lean_f()`（DSO 匯出函數）與 `flowc_lean_f()`（`--target-c-header` 靜態內聯函數）。
2. **靜態不變量與多胞形安全看門狗（[`src/flow.h`](../../src/flow.h)）**：
   - 強制檢驗緩衝區邊界（`buffer_bounds_safety`）、記憶體配額（`memory_quota_bound`）、分片無別名正交（`shard_non_aliasing`）與決定論不變量（`determinism_invariant`）。
   - 支援微秒級預算之保守多胞形區間包絡線快速驗證（`flow_smt_verify_with_budget`）與安全閘門。
3. **Lean 4 形式化證書自舉審計（[`src/abi.c`](../../src/abi.c)）**：
   - `flowc --target-lean <cert.lean>` 自動輸出涵蓋系統安全定理的 Lean 4 形式化證書，並透過 `flow_abi_verify_lean_certificate` 驗證零 `sorry` 完整性。

---

## 4.2 標準 C17 編譯與共享庫封裝流程

只需一行 `flowc` 或 `flowy build --shared` 指令，即可從純 `.lean` 檔案生成標準 C17 算子與標頭檔：

```sh
# 1. 生成 C17 算子原始碼、C17 靜態內聯標頭檔與 Lean 4 形式化證書
./build/flowc plugins/embodied.lean \
    -o build/embodied_gen.c \
    --target-c-header build/embodied_gen.h \
    --target-lean build/embodied_cert.lean

# 2. 或使用 flowy 一鍵編譯 C17 動態共享庫 (.so) 與標頭檔 (.h)
./build/flowy build --shared plugins/embodied.lean
```

在顯式 C17 應用程式中直接引入生成的標頭檔即可呼叫原生算子、定點數算子與定理保護算子：

```c
#include "build/embodied_gen.h"
#include <assert.h>
#include <math.h>

int main(void) {
    /* 1. 0-ULP IEEE-754 double 靜態內聯算子 (含自動 isfinite 防火牆) */
    assert(flowc_isTorqueWithinLimit(45.0, 80.0) == 1);
    assert(flowc_isTorqueWithinLimit(NAN, 80.0) == 0);

    /* 2. Q32.32 確定性定點數算子 (flow_fx64_t) */
    flow_fx64_t pd_fx = flowc_pdReflexLaw_fx(
        FLOW_FX_ONE >> 1, FLOW_FX_ONE, 20LL * FLOW_FX_ONE, 2LL * FLOW_FX_ONE);
    assert(pd_fx == -12LL * FLOW_FX_ONE);

    /* 3. 帶定理前後置硬體斷言保護的合約包裝算子 */
    assert(flowc_lean_embodiedStepKernel(0, 64, 64, 1) == 0);
    return 0;
}
```

---

## 4.3 真實世界純 `.lean` 驗證基準

| 檔案與模組 | 應用領域與驗證規模 | Pure `.lean` 湧現機制 |
| :--- | :--- | :--- |
| **具身雙足反射控制核心**<br>[`plugins/embodied.lean`](../../plugins/embodied.lean) | **1kHz 雙足機器人關節扭矩限制、PD 反射控制律與步態核函數** | 自動合成 `isTorqueWithinLimit`、`dampedAngularAccel`、`pdReflexLaw`、`embodiedStepKernel` 之 `double` / `flow_fx64_t` 雙軌算子與 `lean_*` 合約。 |
