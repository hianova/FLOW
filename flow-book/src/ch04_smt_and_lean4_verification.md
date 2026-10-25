# 第四章：形式化最高法院：定理合成硬體合約 `lean_<fn>`、SMT 四大定理與實戰藍圖

FLOW 將 `.lean` 的數學證明直接轉化為 C17 硬體級執行期防線與靜態 SMT 證明。

---

## 4.1 三層形式化驗證與硬體合約合成機制

1. **定理自動合成 C17 硬體前後置合約（`theorem` $\to$ `lean_<fn>`，[`src/parser.c`](../../src/parser.c), [`src/backend.c`](../../src/backend.c)）**：
   - 凡是針對算子 `def f` 證明的 `theorem`：
     - 定理的前提（包含顯式參數 `(h : P x)` 與蘊含前提 `P x → Q (f x)`）自動正規化並收斂為輸入邊界斷言 `FLOW_LEAN_PRE_GUARD`。
     - 定理的結論 `Q (f x)` 自動將 `f x` 代換為 `_ret`，收斂為輸出不變量斷言 `FLOW_LEAN_POST_GUARD`。
   - 當多條 `theorem` 針對同一個 `f` 時，它們的前提與結論自動取邏輯交集（$\bigwedge_i P_i \implies \bigwedge_j Q_j$），合成出 `lean_f()`（DSO 匯出函數）與 `flowc_lean_f()`（`--target-c-header` 靜態內聯函數）。
2. **SMT QF_LIA 四大定理與保守多面體看門狗（[`src/smt.c`](../../src/smt.c)）**：
   - 強制檢驗緩衝區邊界（`buffer_bounds_safety`）、記憶體配額（`memory_quota_bound`）、分片無別名正交（`shard_non_aliasing`）與決定論不變量（`determinism_invariant`）。
   - 支援微秒級預算之保守多面體區間包絡線快速驗證（`flow_smt_verify_with_budget`）與插件能力安全閘門（`flow_security_check_composition_gate`）。
3. **Lean 4 形式化證書自舉審計（[`src/abi.c`](../../src/abi.c)）**：
   - `flowc --target-lean <cert.lean>` 自動輸出涵蓋 12 條系統安全定理（緩衝區邊界、記憶體配額、QSBR 世代單調性、Kan Open-Box 同倫、雙軌差分神諭等）的 Lean 4 證書，並透過 `flow_abi_verify_lean_certificate` 驗證零 `sorry` 與 `lean` 編譯器類型檢查。

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
| **1. AWS Cedar 授權引擎**<br>[`tests/cedar_authorization.lean`](../../tests/cedar_authorization.lean) | **雲端 IAM 授權防呆**（427 行標準 Lean 4 規格，取自 AWS 官方 `cedar-policy/cedar-spec`） | 從 `Decision` 列舉與 `Request` / `Response` 結構自動湧現 56B Stride C17 佈局與 1PB `FlowExtentDescriptor` 零拷貝處理器。 |
| **2. 具身雙足反射控制核心**<br>[`plugins/embodied.lean`](../../plugins/embodied.lean) | **1kHz 雙足機器人關節扭矩限制、PD 反射控制律與步態核函數** | 自動合成 `isTorqueWithinLimit`、`dampedAngularAccel`、`pdReflexLaw`、`embodiedStepKernel` 之 `double` / `flow_fx64_t` 雙軌算子與 `lean_*` 合約。 |
