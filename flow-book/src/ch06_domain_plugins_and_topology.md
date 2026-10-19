# 第六章：領域插件 ABI 邊界與真實 AST 拓撲自省

為維持核心的純潔性，所有特定硬體與領域邏輯皆透過嚴格的插件介面隔離：

- **雙層插件架構（`src/plugin.h`, `src/primitive.h`, `plugins/`）**：
  - 高階語義插件（`FlowPlugin`）與極簡 3-函數硬體原語驅動（`FlowPrimitiveDriver`：`register_primitive`、`get_hardware_bounds`、`execute_primitive`）。
  - `plugins/` 收納具身雙頻率控制（`embodied.c`）、Mori-Zwanzig 黏彈性阻抗控制（`flow_embodied_mz.c`）、SocketCAN/CAN-FD（`driver_can.c`）與 1000Hz IMU 濾波器（`driver_imu.c`）。
- **真實 AST 工作區拓撲掃描器（`src/topology.c`, `flowy audit`）**：
  - `flow_topology_scan_workspace` 直接掃描工作區 `src/` 與 `plugins/` 的 `#include` 相依邊、計算真實 LOC 與 Fan-in/Fan-out，確保跨層洩漏數 `Leaks == 0`、模組化指數 `Modularity == 1.00`。
