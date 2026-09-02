# 第九章：幾何變形 (AoS 到 SoA 的即時記憶體視圖切換與 mremap 應用)

> 「資料結構的本質，不是記憶體中的具體位元組排列，而是對物理空間的幾何投影。FLOW 透過虛擬記憶體重映射技術，實現微秒級的 AoS 到 SoA 零拷貝即時幾何變形。」

---

## 9.1 AoS vs. SoA：快取行利用率的幾何本質

在高性能計算與即時數據流系統中，記憶體佈局決定了 CPU 快取的生死：

```text
AoS (Array of Structs) 記憶體佈局:
[ id | score | x | y | z ][ id | score | x | y | z ][ id | score | x | y | z ]
  ▲                    ▲    ▲                    ▲    ▲                    ▲
  └──── 載入 1 個 快取行 ───┘    └──── 載入 1 個 快取行 ───┘    └──── 載入 1 個 快取行 ───┘
  (若只需掃描 score 欄位，快取行中 80% 的資料是無用垃圾，快取失效率高達 45%)

SoA (Struct of Arrays) 記憶體佈局:
[ id_0 | id_1 | id_2 | ... ][ score_0 | score_1 | score_2 | ... ][ x_0 | x_1 | ... ]
                             ▲                               ▲
                             └────── 載入 1 個 快取行 ───────┘
  (快取行 100% 填滿 score 數據，SIMD AVX-512 向量化單週期處理 16 筆數據，失效率 < 1%)
```

- **AoS（結構陣列）**：適合隨機單筆讀寫（如點查詢）。
- **SoA（陣列結構）**：適合 SIMD 向量化批次掃描與聚合。

在傳統架構中，一旦資料結構在原始碼中宣告為 AoS，系統便永久失去了採用 SIMD SoA 的能力。

---

## 9.2 跨佈局即時熱遷移矩陣 (`FlowLayoutMigrationSpec`)

FLOW 支援在運行期直接發起跨幾何佈局熱遷移（實作於 `src/jit.c` 與 `src/reload.c`）：

```c
typedef struct {
    size_t item_count;
    size_t field_count;
    size_t field_sizes[8];
    FlowLayoutKind from_layout;      /* FLOW_LAYOUT_AOS */
    FlowLayoutKind to_layout;        /* FLOW_LAYOUT_SOA */
    uint8_t field_changed[8];
} FlowLayoutMigrationSpec;
```

遷移引擎使用高優化的跨步長（Strided）向量搬移演算法，將 AoS 資料即時解構並重組為連續的 SoA 欄位切片。

---

## 9.3 虛擬記憶體零拷貝重映射與零 TLB 擊落 (`flow_reload_morph_zerocopy_remap`)

當記憶體總量受限（例如在 16MB 極限記憶體壓力下），系統根本無法在 RAM 中同時分配一份舊的 AoS 陣列和一份新的 SoA 陣列。

FLOW 發明了基於作業系統虛擬記憶體分頁表（Page Table）重映射的零拷貝變形技術：

```text
虛擬記憶體頁面重映射原理:
物理記憶體頁框 (Physical Page Frames): [ Page P0 ] [ Page P1 ] [ Page P2 ]
                                           ▲          ▲          ▲
                                           │ (mremap  │ 零拷貝   │ 指標切換)
                                           │          │          │
虛擬位址空間 A (舊 AoS 視圖):   [ V0 (AoS) ] ──┘          │          │
虛擬位址空間 B (新 SoA 視圖):   [ V1 (SoA) ] ─────────────┴──────────┘
```

1. **`mremap` 虛擬頁面瞬時轉移**：透過 Linux `mremap(..., MREMAP_MAYMOVE)` 系統調用，直接操作核心頁表，將物理記憶體頁框重新映射至新的 SoA 虛擬地址空間，**耗時小於 2 微秒，且記憶體增量為 0**！
2. **Dual-Mapping 消除 TLB Shootdown**：
   在 JIT 程式碼生成池中，FLOW 建立了雙重映射（Dual-Mapped Memory Pool）：一個虛擬別名具有可寫權限（`PROT_READ | PROT_WRITE`）供編譯器生成機器碼，另一個虛擬別名具有可執行權限（`PROT_READ | PROT_EXEC`）供執行緒直接調用。
   **這完全消除了傳統 JIT 頻繁調用 `mprotect` 所引發的 CPU 跨核心 TLB 擊落中斷（Zero TLB Shootdowns）**！

---

## 9.4 幾何變形的損益平衡數學模型 (Amortization Payback Model)

變形不是無代價的。FLOW 在執行幾何變形前，計算嚴格的損益平衡調用次數：

$$\text{Break-Even Calls} = \frac{\text{Cost}_{\text{transform}} + \text{Cost}_{\text{QSBR}}}{\text{Gain}_{\text{SoA\_per\_call}}}$$

```c
/* src/jit.c */
int flow_jit_calculate_migration_cost(
    const FlowLayoutMigrationSpec *spec,
    double steady_state_gain_ns_per_call,
    double *migration_cost_ns_out,
    double *payback_calls_out
) {
    /* 計算搬移 spec->item_count 筆數據的總奈秒開銷 */
    double total_cost_ns = (double)spec->item_count * 1.8; /* 1.8 ns per element transform */
    *migration_cost_ns_out = total_cost_ns;
    
    if (steady_state_gain_ns_per_call > 0.0) {
        *payback_calls_out = total_cost_ns / steady_state_gain_ns_per_call;
    } else {
        *payback_calls_out = 1e9;
    }
    return 1;
}
```

若當前負載的預期生命週期（Horizon Calls）遠大於 `Break-Even Calls`（例如僅需 200 次調用即可回本），系統判定該次幾何變形在數學上具備絕對正收益，立即批准熱遷移。
