# 第六章：領域插件 ABI、7 大組件領域與 `topo` × `HoTT` 雙核自省

> **給開發者的白話翻譯**：大型 C/C++ 與多語專案演進久了，最怕兩件事——「底層與上層互相 `#include` 變成義大利麵條碼（Spaghetti Code）」以及「C 標頭檔改了欄位，Rust SDK 或多語言 FFI 卻忘記同步」。FLOW 內建了 **`topo` × `HoTT` 雙核架構**（[`src/topology.c`](../../src/topology.c) 外生雷達與 [`src/cubical_hott.c`](../../src/cubical_hott.c) 內生型別宇宙 $\mathcal{U}_0$），每次執行 `flowy audit` 或 `flowy topos` 時自動掃描所有 `.c/.h/.rs/.lean` 的真實 AST 相依圖與 ABI 對偶性，強制確保 **跨層洩漏 `Leaks = 0`**、**模組化指數 `Modularity = 1.00`** 且 **全語言型別單價同構（`HoTT-U0:UNIVALENT`）**。

---

## 6.1 12 模組正交核心、大一統具身插件與 Rust 安全 SDK (`src/plugin.h`, `plugins/embodied.c`, `crates/flow-plugin`)

為維持核心引擎（`src/` 12 個正交函式庫模組 + `flowc` / `flowy` 雙驅動）的極致純潔性，所有特定硬體與領域知識皆透過嚴格的插件介面隔離：

- **高階語義函子與硬體驅動大一統插件（[`plugins/embodied.h`](../../plugins/embodied.h), [`plugins/embodied.c`](../../plugins/embodied.c)）**：
  配合 [`plugins/embodied.lean`](../../plugins/embodied.lean) 與 [`plugins/embodied.flow`](../../plugins/embodied.flow)，在單一無縫模組中同時提供：
  1. `EMBODIED_PLUGIN`（`FlowPlugin` + `FLOW_DEFINE_PLUGIN_DESCRIPTOR`）：將 `namespace Embodied.*` 與 `theorem zmp_torque_safe` 自動降階為 1000Hz 控制迴圈、`fallback emergency_stop` 硬即時看門狗與 ZMP 穩定多邊形約束。
  2. **10kHz Mori-Zwanzig 非馬可夫熱力學記憶核**（`flow_mz_step_10khz`）：以 $Q32.32$ 定點整數算術在關節空間執行零浮點漂移之熱耗散記憶衰減與摩擦補償。
  3. **極簡宣告式硬體原語驅動**（`FlowPrimitiveDriver` + `FLOW_DEFINE_PRIMITIVE_DRIVER`）：內建 6-DoF 互補濾波器 IMU 驅動（`flow_imu_complementary_step`）與 SocketCAN/CAN-FD 零分配硬體匯流排驅動（`flow_can_pack_torque_command`）。
- **Rust 零外部依賴虛擬工作區與巨集化插件 SDK（[`crates/flow-plugin`](../../crates/flow-plugin/src/lib.rs)）**：
  透過 `declare_flow_plugin!` 與 `declare_flow_unit_plugin!` 宣告式巨集，讓開發者以零第三方依賴（Zero External Crates）的純淨 Rust 撰寫領域降階函子與硬體驅動，並由 `Makefile` 與 `topology.c` 自動執行 C17 $\leftrightarrow$ Rust ABI 雙射與 `#[repr(C)]` 同構驗證（`Makefile:BIJECTIVE`、`Rust-SDK:PARITY_OK`）。
- **多語 SDK 5 件式一鍵封裝（`flowy build --shared <spec.lean|spec.flow>`）**：
  一行指令自動驗證並產出包含 `FlowStatusCode` 與 `reset_watchdog` 的 `.so`、`.h`、Python `_abi.py`、Rust `_ffi.rs` 與完整 IR 驅動的 MATLAB `flow_<id>.m`。

---

## 6.2 7 大組件領域與 `topo` × `HoTT` 雙核自省 (`src/topology.c`, `src/cubical_hott.c`, `flowy`)

FLOW 將全工作區（C17 核心、具身插件與 Rust SDK）嚴密劃分為 **7 大正交組件領域（`FlowComponentScope`）**：`frontend`、`formal_sieve`、`exact_manifold`、`runtime_jet`、`polyglot_abi`、`introspection_ui` 與 `rust_sdk`。開發者或 AI Agent 可隨時透過 `flowy` 執行外生拓撲雷達與內生同倫宇宙自省：

```sh
# 1. 掃描工作區真實 #include 與 use 相依圖，驗證 Leaks == 0 且 Modularity == 1.00
./build/flowy audit

# 2. 檢視 7 大組件領域 (Component Scopes) 矩陣與特定領域邊界
./build/flowy scope
./build/flowy scope rust_sdk

# 3. 執行 topo × HoTT 雙核自舉稽核：驗證 Makefile 雙射、Rust SDK 同構、7 語言 ABI 與宇宙 U_0 單價公理
./build/flowy topos

# 4. 以自然語言或關鍵字查詢任一模組、已收斂子系統（如 jit, token_ring, fvec）或 .lean 規格
./build/flowy ask "cedar"
./build/flowy ask "token_ring"
./build/flowy ask "univalence"
```

