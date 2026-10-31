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

---

## 4.4 HoTT 邊界型別語法（`Option` / `FlowResult` / `SpaceWithException`）與 `flowy why` 自省

除了傳統回傳 `0` 的保護包裝（`lean_<fn>`）與子型別證人包裝（`lean_certified_<fn>`），FLOW 原生支援在 `.lean` 中撰寫**零分配同倫邊界型別與模式匹配**，並自動合成兩類零開銷邊界合約包裝：

1. **原生 `.lean` 邊界建構子與模式匹配語法**：
   - 支援回傳與參數型別 `Option <T>`、`FlowResult <T>` 與 `SpaceWithException <T> <Prop>`。
   - 支援建構子 `some v` / `none`、`interior v` / `boundary <code_u12>`、`point v` / `collapse`，以及載荷綁定模式匹配：
     ```lean
     def safe_reciprocal (x : Float) (h_pos : x > 0.0) : FlowResult Float :=
       if x > 0.001 then interior (1.0 / x) else boundary 77

     def recover_or_default (r : FlowResult Float) (fallback : Float) : Float :=
       match r with
       | interior v => v + 1.0
       | boundary b => fallback
     ```
2. **雙軌零成本邊界拉回包裝器（`lean_boundary_<fn>` 與 `lean_try_<fn>`）**：
   - **`lean_boundary_<fn>` / `flowc_boundary_<fn>`**：針對 `Float`（`double`）算子，當前置條件（`h_pos`）、定理實質蘊含守衛或顯式 `boundary <code>` 觸發時，直接回傳編碼了 `(fn_id, guard_idx, violation_kind, cubical_axis, user_code)` 的 **IEEE-754 Quiet NaN**，在硬體 FPU 管線中以零分支指令傳播。
   - **`lean_try_<fn>` / `flowc_try_<fn>`**：針對任意非 Tuple 算子，回傳 `lean_result_<T>`（`struct { <T> value; uint64_t boundary_tag; }`），當 `boundary_tag == 0` 時代表結果位於可縮內部 $M^\circ$，否則可直接呼叫 `flowc_decode_boundary_tag` 解碼違規的 Lean 4 定理名稱與守衛表達式。
   - **CLI 形式化自省（`flowy why`）**：將觀測到的 64-bit 邊界 NaN 位元組傳入 `./build/flowy why 0x7ff84... <spec.lean>`，即可瞬間還原觸發邊界拉回的算子名稱、定理名稱、守衛條件與 64-bit 立方同倫軸。
