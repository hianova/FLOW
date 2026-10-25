# 第五章：顯式 C17 執行期 (`libflow_rt.a`)：無鎖 SPSC、接觸噴流束 (.fjet)、架構記憶 (.fvec) 與 QSBR 熱替換

> **給開發者的白話翻譯**：`libflow_rt.a` 只提供乾淨、透明、零黑盒的 C17 硬實時原語（[`flow_spsc.h`](../../src/flow_spsc.h)、[`flow_jet.h`](../../src/flow_jet.h)、[`reload.h`](../../src/reload.h)、[`cubical_hott.h`](../../src/cubical_hott.h)、[`polyhedral.h`](../../src/polyhedral.h)），讓應用層的 C17 程式碼直接顯式初始化與呼叫。

---

## 5.1 零配置無鎖 SPSC 環形緩衝區 (`src/flow_spsc.h`)

[`src/flow_spsc.h`](../../src/flow_spsc.h) 提供 64-byte 快取行對齊（消除 False Sharing）、編譯期 2 的次方容量靜態斷言的 C17 泛型無鎖單生產者／單消費者佇列巨集 `FLOW_SPSC_DEFINE(Name, Type, Cap)`：

```c
#include "flow_spsc.h"
#include <assert.h>

/* 宣告容量為 64 (必須為 2 的次方) 的強型別無鎖環形緩衝區 */
FLOW_SPSC_DEFINE(TorqueRing, double, 64)

void control_loop_example(void) {
    TorqueRing ring;
    TorqueRing_init(&ring);

    assert(TorqueRing_push(&ring, 42.5) == 1);
    double cmd = 0.0;
    assert(TorqueRing_pop(&ring, &cmd) == 1 && cmd == 42.5);
}
```

---

## 5.2 波前耦合 QSBR 無鎖熱替換與預鎖定記憶體池 (`src/reload.h`, `src/reload.c`)

- **64B 快取行對齊、世代租約（Epoch Lease）與發布前預留空間（Reservation-Before-Publication）**：
  每次呼叫 `flow_qsbr_call` 或 `process_descriptor` 時，系統僅以單條原子指令推進 `qsbr_epoch`（耗時 `< 100ns`），待舊世代所有讀取請求自然結束後才安全回收舊狀態，徹底杜絕 Use-After-Free 與讀寫鎖競爭。
- **預留空間上限（`FLOW_RELOAD_MAX_RETIRED_DEPTH = 64`）**：
  在發布新版本算子前強制檢查退休佇列深度；若滿載則先嘗試內聯回收未釘選世代，若因異常 Reader 卡死導致佇列仍滿，立即退回 `FLOW_RELOAD_JOURNAL_FULL`，絕不污染現有狀態或造成無限記憶體堆積。
- **AArch64 / x86_64 記憶體內原生 JIT 引擎（`flow_jit_compile_ir`）**：
  [`src/reload.c`](../../src/reload.c) 內建零外部依賴之機器碼 JIT 與 W^X 權限保護，支援毫秒級算子熱生成與驗證。

---

## 5.3 熱力學接觸噴流束 `.fjet`、具名座標視圖與 `.fvec` 架構記憶庫 (`src/flow_jet.h`, `src/bitspace.h`)

- **具名相空間視圖（`FLOW_JET_DEFINE_NAMED_VIEW`）**：
  在 C17 中透過 `FLOW_JET_DEFINE_NAMED_VIEW(ViewType, CoordsStruct)` 即可將 64 維辛幾何噴流束 `FlowJet` 直接映射為零偏移的具名欄位指標（如 `view.q->hipPitch`、`view.p->hipPitch`），並透過 `flow_jet_sanitize_finite(&jet)` 一鍵清除任何 NaN/Inf 異常值。
- **熱力學接觸流形與硬體週期探針（[`src/flow_jet.c`](../../src/flow_jet.c), [`src/flow.h`](../../src/flow.h)）**：
  結合跨平台硬體週期計數器（AArch64 `cntvct_el0` / x86_64 `rdtsc`）與 NUMA 拓撲探針，在相空間 $(q, p, \ddot{q})$ 上追蹤熱耗散作用量 $s$ 與 Koopman EDMD 算子學習。
- **`.fvec` 架構記憶庫與 9-Byte 群體免疫抗體（[`src/bitspace.c`](../../src/bitspace.c)）**：
  將通過 SMT 驗證的 64-bit 架構基因持久化為帶 CRC32 校驗的 `.fvec` 檔案，並支援 9-byte 抗體封包微秒級跨節點同步。
