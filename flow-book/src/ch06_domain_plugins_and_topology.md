# 第六章：領域插件 ABI 邊界與真實 AST 拓撲自省

> **給開發者的白話翻譯**：大型 C/C++ 專案演進久了，最怕「底層與上層互相 `#include` 變成義大利麵條碼（Spaghetti Code）」。FLOW 內建了 `flowy audit` 與 `flowy ask`，每次建置時自動掃描所有 `.c/.h` 的真實 AST 相依圖，強制確保 **跨層洩漏 `Leaks = 0`**、**模組化指數 `Modularity = 1.00`**。

---

## 6.1 雙層插件架構 (`src/plugin.h`, `src/primitive.h`, `plugins/`)

為維持核心引擎（`src/`）的純潔性，所有特定硬體與領域知識皆透過嚴格的插件介面隔離：

- **高階語義函子插件（`FlowPlugin`）**：
  實作 `lower_domain_semantics(spec, ir, plugin)`，例如 [`plugins/embodied.c`](../../plugins/embodied.c) 將 `.lean` 中的 `namespace Embodied.*` 自動降階為 1000Hz 控制迴圈與 ZMP 穩定約束。
- **極簡 3-函數硬體原語驅動（`FlowPrimitiveDriver`）**：
  僅需實作 `register_primitive`、`get_hardware_bounds`、`execute_primitive` 三個函數，即可掛載如 SocketCAN/CAN-FD（[`plugins/driver_can.c`](../../plugins/driver_can.c)）或 1000Hz IMU 濾波器（[`plugins/driver_imu.c`](../../plugins/driver_imu.c)）。

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
