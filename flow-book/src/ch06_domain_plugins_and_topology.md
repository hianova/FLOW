# 第六章：具身插件、硬體原語驅動與內生同倫宇宙 $\mathcal{U}_0$ 自舉審計

> **給開發者的白話翻譯**：FLOW 將核心收斂為純 C17 模組（`libflow.a` 編譯器核心與 `libflow_rt.a` 最小執行期），並透過 [`src/plugin.h`](../../src/plugin.h) 介面掛載具身控制插件 [`plugins/embodied.c`](../../plugins/embodied.c)，同時由 [`src/cubical_hott.c`](../../src/cubical_hott.c) 的內生型別宇宙 $\mathcal{U}_0$ 驗證 Lean 4 $\leftrightarrow$ C17 的單價同構性。

---

## 6.1 具身控制插件與硬體原語驅動 (`src/plugin.h`, `plugins/embodied.h`, `plugins/embodied.c`)

[`plugins/embodied.h`](../../plugins/embodied.h) 與 [`plugins/embodied.c`](../../plugins/embodied.c) 配合純數學規格 [`plugins/embodied.lean`](../../plugins/embodied.lean)，在單一 C17 模組中提供：

1. **10kHz Mori-Zwanzig 非馬可夫熱力學記憶核（`flow_mz_step_10khz`）**：
   以 $Q32.32$ 定點整數算術在關節空間執行零浮點漂移之熱耗散記憶衰減與摩擦補償。
2. **6-DoF 互補濾波器 IMU 驅動與 SocketCAN/CAN-FD 零分配匯流排驅動**：
   透過宣告式硬體原語驅動介面（`FlowPrimitiveDriver` + `FLOW_DEFINE_PRIMITIVE_DRIVER`），提供無動態配置的感測器融合與扭矩指令封包編解碼。
3. **跨 DSO 影子 A/B 驗證探針（`flow_ab_shadow_*`）**：
   合成的 C17 DSO 內建原子計數與誤差比對探針（`flow_ab_shadow_record`、`flow_ab_shadow_get_calls`、`flow_ab_shadow_get_mismatches`），可在線上對照新舊算子或 `double` vs. `flow_fx64_t` 雙軌輸出。

---

## 6.2 `flowy` 系統自省與內生同倫宇宙 $\mathcal{U}_0$ 審計 (`src/cubical_hott.c`, `src/flowy.c`)

開發者可隨時透過 `flowy` 執行形式化審計、同倫型別宇宙檢視、相空間模擬與測試套件：

```sh
# 1. 執行內生同倫型別宇宙 U_0 單價公理、QF_LIA SMT 與精確數值流形審計
./build/flowy audit

# 2. 檢視 64-Bit 立方同倫型別篩 (I^64) 與全域型別宇宙 U_0 同構路徑雜湊
./build/flowy topos

# 3. 檢視與查詢 .fvec 架構記憶庫或執行 .fjet 辛幾何相空間模擬
./build/flowy fvec list
./build/flowy why

# 4. 執行全套 8 大領域測試套件
./build/flowy test all
```
