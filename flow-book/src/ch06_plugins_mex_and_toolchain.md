# 第六章：模組化擴展與工具鏈：動態插件、MATLAB/Simulink MEX 與系統自省 (Extensions & Ecosystem)

> **給開發者的白話翻譯**：FLOW 將核心收斂為純 C17 模組（`libflow.a` 編譯器核心與 `libflow_rt.a` 最小執行期），並透過 [`src/plugin.h`](../../src/plugin.h) 介面掛載具身控制插件 [`plugins/embodied.c`](../../plugins/embodied.c)，同時由 [`src/cubical_hott.c`](../../src/cubical_hott.c) 的全域型別宇宙 $\mathcal{U}_0$ 驗證 Lean 4 $\leftrightarrow$ C17 的單價同構性。

---

## 6.1 具身控制插件與動態模組載入 (`src/plugin.h`, `plugins/embodied.h`, `plugins/embodied.c`)

[`plugins/embodied.h`](../../plugins/embodied.h) 與 [`plugins/embodied.c`](../../plugins/embodied.c) 配合純數學規格 [`plugins/embodied.lean`](../../plugins/embodied.lean)，在單一 C17 動態模組中提供：

1. **實時關節阻抗控制與摩擦補償**：
   以 $Q32.32$ 定點整數算術在關節空間執行零浮點漂移之耗散衰減與力矩限制。
2. **零動態記憶體分配原語驅動**：
   透過標準硬體原語驅動介面（`FlowPrimitiveDriver`），提供無動態配置的感測器融合與扭矩指令封包編解碼。
3. **跨 DSO 影子 A/B 驗證探針（`flow_ab_shadow_*`）**：
   合成的 C17 DSO 內建原子計數與誤差比對探針（`flow_ab_shadow_record`、`flow_ab_shadow_get_calls`、`flow_ab_shadow_get_mismatches`），可在線上對照新舊算子或 `double` vs. `flow_fx64_t` 雙軌輸出。

---

## 6.2 `flowy` 系統自省與同倫型別宇宙 $\mathcal{U}_0$ 審計 (`src/cubical_hott.c`, `src/flowy.c`)

開發者可隨時透過 `flowy` 執行形式化審計與同倫型別宇宙檢視：

```sh
# 1. 執行型別宇宙 U_0 單價公理、QF_LIA 與精確數值流形審計
./build/flowy audit

# 2. 檢視 64-Bit 立方同倫型別篩 (I^64) 與全域型別宇宙 U_0 同構路徑雜湊
./build/flowy topos

# 3. 檢視實時調度決策與因果歸因
./build/flowy why

# 4. 執行全套標準測試套件
make test
```

---

## 6.3 MATLAB / Simulink 零修改直通與 `--target-mex` 原生分派器 (`src/abi.c`, `src/flowc.c`)

由於 `flowc` 產出的 C17 標頭檔與實作檔已收斂為 **100% 純淨 C17 ABI**（無證明欄位、無隱藏執行期狀態，且搭配 `--target-c-header` / `--target-mex` 時自動省略 `int main(void)`），FLOW 可透過兩條極簡路徑無縫接入 MATLAB 與 Simulink 生態：

1. **零修改直通 Simulink `C Caller Block` 與 MATLAB `loadlibrary` / `clibgen`**：
   - 執行 `./build/flowc model.lean -o build/model.c --target-c-header build/model.h`。
   - 直接將 `build/model.h` 與 `build/model.c` 拖入 Simulink 的 **C Caller Block**，即可將 `lean_<fn>` 當作形式化認證過的控制器方塊（Verified Controller Block）執行閉迴路模擬（SIL / PIL）；或在 MATLAB 中透過 `loadlibrary('libmodel.so', 'model.h')` 直接呼叫。
2. **百萬次蒙地卡羅與高頻回測專用：`--target-mex` 原生 C17 分派器**：
   - 執行 `./build/flowc model.lean -o build/model_mex.c --target-c-header build/model.h --target-mex`。
   - [`flow_abi_emit_mex_dispatcher`](../../src/abi.c) 會在 `build/model_mex.c` 尾端自動生成 `mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])` 分派器，涵蓋所有非私有標量與多回傳值算子（`<fn>`、定理防護版 `lean_<fn>`、以及 2-Jet 前向自動微分版 `jet2_<fn>`）。
   - 在 MATLAB 中僅需一行指令編譯，即可享受原生 C17 速度、Lean 4 執行期定理保護與 2-Jet 二階自動微分（一次取回 `[價格, Delta, Gamma]`）：

```matlab
% 在 MATLAB 中編譯並執行
mex -O -Ibuild build/model_mex.c
[px, delta, gamma] = model_mex('jet2_portfolio_utility', S, K);
```
