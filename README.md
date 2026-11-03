# FLOW v4 — Deterministic External-Constraint ASM × HoTT × 2-Jet Kernel

> **編譯期確定性部分求值（Partial Evaluation）＋ 執行期 90% ASM 全函數基底（Zero C UB）＋ 通用外部約束介面（Universal External-Constraint ABI $\mathbf{b}_{\text{ext}}, \mathbf{u}_{\text{ext}}$）。**

FLOW v4 將所有編譯期已知的型別同構（Univalence）、硬體拓撲規格（`PAGE=16KB, LINE=128B, WAVE=64`）與多面體法向量矩陣 $A$ 完全壓縮為靜態常數與編譯期 `_Static_assert`，執行期**僅保留對外部輸入變數 $(\mathbf{u}_{\text{ext}}, \mathbf{b}_{\text{ext}})$ 的不可化約全函數轉換**。舊版 v3 程式碼已完整封存於 [`archive/v3_legacy/`](archive/v3_legacy/) 供對照參考。

---

## 1. 核心架構：Frontend (Lean / Domain Model) → Universal C/ASM Kernel

```
  ┌──────────────────────────────────────────────────────────────────────┐
  │ 1. 離線/編譯期確定性投影 (Partial Evaluation / Zero Runtime Bloat)   │
  │    • 硬體規格 build/flow_hw_spec.h (PAGE, CACHE_LINE, L1D, WAVE64)  │
  │    • 編譯期 Univalence 同構檢驗 FLOW_STATIC_ASSERT_UNIVALENT         │
  │    • 有理數多面體保守內近似量化 flow_qpoly_quantize_inner -> 靜態 A  │
  └──────────────────────────────────┬───────────────────────────────────┘
                                     │
                                     ▼
  ┌──────────────────────────────────────────────────────────────────────┐
  │ 2. 90% ASM 全函數基底 (src/flow_asm.h — Zero C Undefined Behavior)   │
  │    • 整數 Q 算術: adds / subs / smull / smulh / extr + cset vs       │
  │    • 浮點異常篩: mrs fpsr / stmxcsr + Quiet NaN 邊界坍縮 (0x7FF84...)│
  │    • 遮罩環形記憶體: and + bic + ldr/str (Z / 2^k Z 零越界 Segfault) │
  │    • Wave64 位元代數: POPCNT 1.58-bit 三態內積 & CTZ 單週期壓縮      │
  └──────────────────────────────────┬───────────────────────────────────┘
                                     │
                                     ▼
  ┌──────────────────────────────────────────────────────────────────────┐
  │ 3. 通用外部約束 HoTT × 2-Jet 引擎 (src/flow_hott_jet.h)              │
  │    • 動態外部約束 b_ext 與單週期無分支雙閾值遲滯 (b_enter / b_exit) │
  │    • 2-Jet 二次世界線求根 (flow_jet2_solve_root) 與自舉殘差自檢      │
  │    • Cubical Kan 單一位元鄰居步進 (flow_cubical_step_neighbor)       │
  └──────────────────────────────────┬───────────────────────────────────┘
                                     │
                                     ▼
  ┌──────────────────────────────────────────────────────────────────────┐
  │ 4. 無鎖外部變數通道 (src/flow_io.h & src/flow_core.c)                │
  │    • Cache-Line 對齊 SPSC 佇列 + 16KB UMA 零拷貝共享頁 + 無鎖 QSBR   │
  └──────────────────────────────────────────────────────────────────────┘
```

---

## 2. Clean-Slate v4 模組架構一覽（4 Headers + 1 C 檔）

| 檔案 | 職責說明 |
| :--- | :--- |
| [`tools/gen_hw_spec.sh`](tools/gen_hw_spec.sh), `build/flow_hw_spec.h` | **編譯期硬體規格探測**：自動生成 `FLOW_HW_PAGE_BYTES`、`FLOW_HW_CACHE_LINE_BYTES`、`FLOW_HW_L1D_BYTES`、`FLOW_HW_VECTOR_BYTES` 與 `FLOW_HW_WAVE_LANES` |
| [`src/flow_asm.h`](src/flow_asm.h) | **Total ISA 全函數算術與硬體篩網基底**：10-bit `FlowAsmSieveAxis`、`flow_asm_qadd_i64`、`flow_asm_qsub_i64`、`flow_asm_qmul_shift16_i64`、`flow_asm_qdot_i32`、`flow_asm_qpoly4d_eval_plane`、硬體 `FPSR`/`MXCSR` 探針、$\mathbb{Z}/2^k\mathbb{Z}$ 遮罩定址與 Wave64 Ballot / 1.58-bit 三態內積 |
| [`src/flow_hott_jet.h`](src/flow_hott_jet.h) | **通用外部約束 HoTT × 2-Jet 核心**：編譯期 Univalence `FLOW_STATIC_ASSERT_UNIVALENT`、位元級 HIT 異常商化 `flow_hott_quotient_sieve`、Kan 鄰居轉移 `flow_cubical_step_neighbor`、2-Jet 求根 `flow_jet2_solve_root`、自舉微分檢驗 `flow_asm_jet2_self_check_step` 與支援無分支遲滯（`b_enter`/`b_exit`）之 `FLOW_QPOLY_DEFINE` |
| [`src/flow_io.h`](src/flow_io.h), [`src/flow_core.c`](src/flow_core.c) | **無鎖外部約束輸入與熱切換層**：`FLOW_SPSC_DEFINE` 快取線對齊無鎖佇列、`flow_uma_buffer_alloc` 16KB 對齊共享記憶體池、零 Mutex 無鎖 QSBR 外部約束庫指標熱切換（`flow_qsbr_publish_ptr` / `flow_qsbr_load_ptr`）與 `flow_reload_resource_sieve` |
| [`tests/test_asm_sieve.c`](tests/test_asm_sieve.c), [`tests/test_ext_constraint.c`](tests/test_ext_constraint.c), [`tests/test_qpoly_poc.c`](tests/test_qpoly_poc.c) | **Clean-Slate v4 測試與基準驗證套件**：涵蓋 ASM 全函數算術、硬體旗標、遮罩記憶體、2-Jet 自舉檢驗、通用外部約束與遲滯防抖、世界線防穿隧，以及 PoC A–E 量化與零 Kernel 介入驗證 |
| [`archive/v3_legacy/`](archive/v3_legacy/) | **v3 歷史版本完整封存**：保留重構前之 16 個 `src/*` 與 5 個 `tests/*` 供參考對照 |

---

## 3. 快速建置與測試驗證

```bash
make fmt-check
make clean && make all && make test
```
包含 3 個全記憶體測試執行檔：
- `build/test-asm-sieve`：驗證 ASM 有號 Q 算術溢位捕捉（`NZCV.V`）、硬體 FPU 狀態暫存器（`FPSR`/`MXCSR`）、遮罩環形記憶體定址（零 Segfault）、Wave64 1.58-bit 內積與 2-Jet 泰勒自舉檢驗（100 萬次迭代基準測試）。
- `build/test-ext-constraint`：驗證編譯期零執行期位元組 Univalence、通用外部約束 $\mathbf{b}_{\text{ext}} / \mathbf{u}_{\text{ext}}$ 動態更新、單週期無分支雙閾值遲滯（`b_enter` vs `b_exit`）、2-Jet 二次世界線防穿隧求根，以及 SPSC + UMA + 無鎖 QSBR 外部約束熱切換。
- `build/test-qpoly-poc`：驗證 PoC A（2D/3D 保守內近似量化 0 誤判）、PoC B（ASM `FlowQPoly_eval` vs `double` 求值加速與飽和保護）、PoC C（1,000 條二次軌跡下一個翻轉面預測）、PoC D（10,000 次隨機 Cubical HoTT 單一位元鄰居轉移一致性）、PoC E（$10^6$ 次熱路徑 `ru_minflt = 0, ru_majflt = 0, ru_nvcsw = 0` 零 Kernel 介入驗證）。
