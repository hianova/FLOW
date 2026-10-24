# 第六章：領域插件 ABI 邊界與真實 AST 拓撲自省

> **給開發者的白話翻譯**：大型 C/C++ 專案演進久了，最怕「底層與上層互相 `#include` 變成義大利麵條碼（Spaghetti Code）」。FLOW 內建了 `flowy audit` 與 `flowy ask`，每次建置時自動掃描所有 `.c/.h` 的真實 AST 相依圖，強制確保 **跨層洩漏 `Leaks = 0`**、**模組化指數 `Modularity = 1.00`**。

---

## 6.1 15 模組正交核心與插件架構 (`src/plugin.h`, `plugins/`)

為維持核心引擎（`src/` 15 個正交模組家族）的純潔性，所有特定硬體與領域知識皆透過嚴格的插件介面（`plugins/` 3 個正交模組）隔離：

- **高階語義函子插件（`FlowPlugin` + `FLOW_DEFINE_PLUGIN_DESCRIPTOR`）**：
  實作 `lower_domain_semantics(spec, ir, plugin)`，例如 [`plugins/embodied.c`](../../plugins/embodied.c) 配合 [`plugins/embodied.lean`](../../plugins/embodied.lean) 與 [`plugins/embodied.flow`](../../plugins/embodied.flow) 將領域意圖降階為 1000Hz 控制迴圈、`fallback emergency_stop` 硬即時看門狗與 ZMP 穩定約束，並透過一行 `FLOW_DEFINE_PLUGIN_DESCRIPTOR` 自動完成靜態與 DSO 雙模式註冊。
- **極簡宣告式硬體原語驅動（`FlowPrimitiveDriver` + `FLOW_DEFINE_PRIMITIVE_DRIVER`）**：
  僅需提供單一 `execute` 函數並呼叫一行 `FLOW_DEFINE_PRIMITIVE_DRIVER(...)`，即可零樣板掛載 SocketCAN/CAN-FD（[`plugins/driver_can.c`](../../plugins/driver_can.c)）或 1000Hz IMU 濾波器（[`plugins/driver_imu.c`](../../plugins/driver_imu.c)）。
- **具名相空間視圖（`FLOW_JET_DEFINE_NAMED_VIEW`）與多語 SDK 一鍵封裝（`flowy build --shared`）**：
  將 `FlowJet` 的 `q[] / p[] / a[]` 直接映射為具名 C 結構指標（如 `view.q->hipPitch`），並透過 `./build/flowy build --shared <spec>` 一鍵產出包含 `FlowStatusCode` 與 `reset_watchdog` 的 `.so`、`.h`、Python `_abi.py` 與完整 IR 驅動的 MATLAB `flow_<id>.m`。

---

## 6.2 真實 AST 工作區拓撲掃描與 CLI 自省 (`src/topology.c`, `flowy`)

開發者或 AI Agent 可隨時透過 `flowy` 查詢工作區架構健康度與模組職責：

```sh
# 1. 掃描工作區真實 #include 相依圖，驗證跨層洩漏 Leaks == 0 且 Modularity == 1.00
./build/flowy audit

# 2. 定位系統中扇入/扇出與行數權重最高的核心瓶頸節點
./build/flowy inspect bottleneck

# 3. 以自然語言或關鍵字查詢任一模組、插件或已驗證的 .lean 規格
./build/flowy ask "cedar"
./build/flowy ask "token_ring"
```
