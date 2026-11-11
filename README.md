# FLOW — Deterministic External-Constraint ASM × HoTT × 2-Jet Kernel

> **編譯期確定性部分求值（Partial Evaluation）＋ 執行期 90% ASM 全函數基底（Zero C UB）＋ 通用外部約束介面（Universal External-Constraint ABI $\mathbf{b}_{\text{ext}}, \mathbf{u}_{\text{ext}}$）。**

`FLOW`（`libflow_rt.a`）是一套零外部依賴、支援 `-ffreestanding -nostdlib` 裸機與跨平台 User-Space 的確定性代數與幾何控制核心。所有編譯期已知的型別同構（Univalence）、硬體拓撲規格（`PAGE=16KB, LINE=128B, WAVE=64`）與多面體法向量矩陣 $A$ 皆在編譯期壓縮為靜態常數與 `_Static_assert`，執行期**僅保留對外部輸入變數 $(\mathbf{u}_{\text{ext}}, \mathbf{b}_{\text{ext}})$ 的不可化約全函數轉換**（`FloatOps == 0`、`HeapCalls == 0`）。歷史封存程式碼位於 [`archive/`](archive/) 供對照參考。

---

## 1. 核心架構：編譯期靜態投影 → 執行期 C17/ASM 全函數核心

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
  │ 2. 90% ASM 純整數全函數基底 (src/flow_asm.h — Zero Float / Zero UB)  │
  │    • 整數 Q 算術: adds / subs / mul / smulh / extr + cset vs         │
  │    • 128-bit Q16 算子: qdiv / isqrt_u128 / qsqrt / qlerp / qmargin3  │
  │    • 遮罩環形記憶體: and + bic + ldr/str (Z / 2^k Z 零越界 Segfault) │
  │    • Wave64 位元代數: POPCNT 1.58-bit 三態內積 & CTZ 單週期壓縮      │
  │    • Goldilocks F_p 無除法模運算、x^7 (mod p) 雙射與 rbit 位元反轉   │
  └──────────────────────────────────┬───────────────────────────────────┘
                                     │
                                     ▼
  ┌──────────────────────────────────────────────────────────────────────┐
  │ 3. 通用外部約束 HoTT × 純 Q16 2-Jet 引擎 (src/flow_hott_jet.h)       │
  │    • 動態外部約束 b_ext 與單週期無分支雙閾值遲滯 (b_enter / b_exit) │
  │    • 純 Q16 2-Jet 二次世界線整數求根、凸組合內插 (Lerp) 與微分自檢   │
  │    • Cubical Kan 單一位元鄰居步進 (flow_cubical_step_neighbor)       │
  └──────────────────────────────────┬───────────────────────────────────┘
                                     │
                                     ▼
  ┌──────────────────────────────────────────────────────────────────────┐
  │ 4. 無鎖外部變數通道與 64B 容器 (src/flow_io.h & src/flow_image64.h)   │
  │    • Cache-Line 對齊 SPSC 佇列 + 16KB UMA 零拷貝共享頁 + 無鎖 QSBR   │
  │    • 64-Byte Merkle 容器 (.flow) 零拷貝唯讀映射與 Z / 2^k Z 遮罩讀取 │
  └──────────────────────────────────────────────────────────────────────┘
```

---

## 2. 模組架構一覽（5 Headers + 1 C 檔，零 `double` / 零 `-lm`）

| 檔案 | 職責說明 |
| :--- | :--- |
| [`tools/gen_hw_spec.sh`](tools/gen_hw_spec.sh), `build/flow_hw_spec.h` | **編譯期硬體規格探測**：自動生成 `FLOW_HW_PAGE_BYTES`、`FLOW_HW_CACHE_LINE_BYTES`、`FLOW_HW_L1D_BYTES`、`FLOW_HW_VECTOR_BYTES` 與 `FLOW_HW_WAVE_LANES` |
| [`src/flow_asm.h`](src/flow_asm.h) | **Total ISA 純整數全函數算術、Freestanding 裸機與 32-Bit MCU 基底**：支援 `-ffreestanding -nostdlib -fno-builtin`（`__STDC_HOSTED__ == 0`，0 外部未定義符號 `nm -u`）、跨 ARM64 / x86_64 / Xtensa LX6·LX7（`rsr.ccount`）/ RISC-V（`rdcycle`）硬體週期探針、純暫存器 128-bit 除法 `flow_u128_div_u128`（消除 `___udivti3`）、單次 `udiv` 切比雪夫分段線性精確整數平方根 `flow_asm_isqrt_u64_fast`、32-bit MCU 無 `__int128` 精確 64×64→128 算術、10-bit `FlowAsmSieveAxis`、$\mathbb{Z}/2^k\mathbb{Z}$ 遮罩定址、Wave64 Ballot / 1.58-bit 三態內積、Goldilocks 質數體 $\mathbb{F}_p$ ($p = 2^{64}-2^{32}+1$) 無除法模算術、$x^7 \pmod p$ 雙射自同構（`flow_asm_fp64_pow7`）與標準代數海綿步（`flow_asm_fp64_sponge_step`）及 `rbit` 單週期位元反轉 |
| [`src/flow_hott_jet.h`](src/flow_hott_jet.h) | **通用外部約束 HoTT × 純 Q16 2-Jet 核心**：編譯期 Univalence `FLOW_STATIC_ASSERT_UNIVALENT`、位元級 HIT 異常商化 `flow_hott_quotient_sieve`、Kan 鄰居轉移 `flow_cubical_step_neighbor`、純 Q16 2-Jet 整數求根 `flow_jet2_solve_root` 與 Citardauq 無相減消去快速求根 `flow_jet2_solve_root_fast64`、區間凸組合內插 `flow_jet2_lerp`、自舉微分檢驗 `flow_asm_jet2_self_check_step` 與無分支遲滯 `FLOW_QPOLY_DEFINE_EX` |
| [`src/flow_image64.h`](src/flow_image64.h) | **通用 `.flow` 64-Byte Merkle 容器與零拷貝映射基底**：定義 64B 超塊標頭 `FlowImageHeader64`、64B 區塊聯合體 `FlowImageBlock64`、FNV-1a Merkle MAC `flow_image64_compute_mac`、切片驗證 `flow_image64_verify`、$\mathbb{Z}/2^k\mathbb{Z}$ 無分支遮罩索引 `flow_image64_get_block_masked` 與零拷貝 `mmap` 載入器 `flow_image64_mmap_readonly` |
| [`src/flow_io.h`](src/flow_io.h), [`src/flow_core.c`](src/flow_core.c) | **無鎖外部約束輸入與熱切換層**：預設匯出 `flow_image64.h`，並提供 `FLOW_SPSC_DEFINE` 快取線對齊無鎖佇列、`flow_uma_buffer_alloc` 對齊共享記憶體池、零 Mutex 無鎖 QSBR 外部約束庫指標熱切換（`flow_qsbr_publish_ptr` / `flow_qsbr_load_ptr`）與 `flow_reload_resource_sieve` |
| [`tests/test_freestanding_kernel.c`](tests/test_freestanding_kernel.c), [`tests/test_asm_sieve.c`](tests/test_asm_sieve.c), [`tests/test_ext_constraint.c`](tests/test_ext_constraint.c), [`tests/test_qpoly_poc.c`](tests/test_qpoly_poc.c) | **純整數與 Ring-0 裸機驗證套件**：涵蓋 Stage `[0/3]` `-ffreestanding -nostdlib -fno-builtin` 零外部符號檢查（`nm -u == empty`，含 64-bit ASM 與 32-bit MCU 模式）、ASM 全函數算術、Q16 除零/定義域/溢位篩網、遮罩記憶體、2-Jet 自舉檢驗、通用外部約束與遲滯防抖，以及 PoC A–E 量化與零 Kernel 介入驗證 |
| [`archive/`](archive/) | **歷史版本封存**：保留重構前之舊版檔案供參考對照 |

---

## 3. 算子與參數分離設計指南（64-Byte Descriptor Driven）

在基於 `FLOW` 的系統中，推薦採用 **「固定預編譯 C17/ASM 通用算子 ＋ 64-Byte 外部約束描述子」** 模式，避免將閾值與規則硬編碼為分散的 `if/else` 分支函式：

1. **C17 執行期算子固定不變**：直接呼叫 `FLOW` 預編譯算子（`FLOW_QPOLY_DEFINE_EX` 單週期無分支雙閾值遲滯、`FlowJet2` 二階泰勒預測與求根、`flow_cubical_step_neighbor` Kan 路由、`flow_asm_*` 全函數算術與 10-bit 篩網）。
2. **64B 規則描述子直映與熱切換**：將外部約束參數（如 `b_enter_q16`、`b_exit_q16`、`weight_q16`、能力遮罩）封裝於 64-byte 對齊結構體或 `.flow` 容器中，執行期透過 `flow_image64_mmap_readonly` 零拷貝載入，並以 `flow_qsbr_publish_ptr` 進行 **0 停機、0 重編譯熱切換**。

### 最小完整 C17 範例

連結 `-I/path/to/FLOW/src -I/path/to/FLOW/build /path/to/FLOW/build/libflow_rt.a`：

```c
#include <assert.h>
#include "flow_asm.h"
#include "flow_hott_jet.h"
#include "flow_io.h" /* 內含 flow_image64.h (.flow 64B 容器) 與無鎖 QSBR */

/* 1. 定義 64-byte 快取線對齊的外部約束描述子 */
typedef struct __attribute__((aligned(64))) {
    uint64_t magic;                  /* 8B: 規則識別碼 */
    uint32_t route_op_mask;          /* 4B: 算子路由遮罩 */
    uint32_t qshift;                 /* 4B: 定點位移 (16) */
    int32_t  b_enter_q16;            /* 4B: 遲滯觸發上界 b_enter */
    int32_t  b_exit_q16;             /* 4B: 遲滯釋放下界 b_exit */
    int32_t  weight_q16;             /* 4B: 權重 / 阻尼係數 */
    uint32_t reserved_meta;          /* 4B: 欄位與版本元資料 */
    uint64_t req_mask;               /* 8B: 必要能力遮罩 */
    uint64_t req_values;             /* 8B: 必要能力目標值 */
    uint64_t spec_schema_hash;       /* 8B: 規格雜湊 */
    uint64_t abi_mac;                /* 8B: 完整性簽章 */
} AppRuleDesc64;

FLOW_STATIC_ASSERT_UNIVALENT(AppRuleDesc64, FlowImageBlock64);

/* 2. 宣告預編譯 1D Q-Polyhedron 無分支遲滯算子 */
FLOW_QPOLY_DEFINE_EX(AppGovernorPoly, 1, 16, 4)

int main(void) {
    AppRuleDesc64 rule = {
        .magic = 0x464C4F5752554C45ULL,
        .route_op_mask = 0x0DU,
        .qshift = 16U,
        .b_enter_q16 = 73728, /* 1.125 in Q16.16 */
        .b_exit_q16 = 57344,  /* 0.875 in Q16.16 */
        .weight_q16 = 3276,
    };

    /* 3. 將 64B 描述子參數注入預編譯算子（零浮點、零配置） */
    AppGovernorPoly gov;
    AppGovernorPoly_init(&gov);
    gov.A[0][0] = 1;
    AppGovernorPoly_set_hysteresis(&gov, 0, rule.b_enter_q16, rule.b_exit_q16);

    int32_t x_in[1] = { 65536 }; /* 外部輸入 u_ext = 1.0 (位於 [b_exit, b_enter] 死區內) */
    FlowQPolyEval ev = AppGovernorPoly_eval_hysteresis(&gov, x_in, 0ULL);

    /* 4. 結合 2-Jet 二階微分預測與 10-bit 硬體篩網 */
    uint64_t sieve = FLOW_ASM_SIEVE_ALL_SOUND;
    FlowJet2 jet = flow_jet2_make(x_in[0], rule.weight_q16, 0);
    FlowResultU64 next_v = flow_asm_qadd_i64(jet.v, jet.dv, &sieve);
    assert(!flow_result_u64_is_boundary(next_v) && sieve == FLOW_ASM_SIEVE_ALL_SOUND);
    (void)ev;
    return 0;
}
```

---

## 4. 快速建置與測試驗證

```bash
make fmt-check
make clean && make all && make test
```

包含裸機零外部符號檢查（`test_freestanding_kernel.c`）與 3 個全記憶體純整數測試執行檔：
- `build/test-asm-sieve`：驗證 ASM 有號 Q 算術溢位捕捉（`NZCV.V`）、純 Q16 `isqrt`/`qsqrt`/`qdiv`/`qlerp`/`qmargin3` 與邊界標籤篩網、遮罩環形記憶體定址（零 Segfault）、Wave64 1.58-bit 內積、Goldilocks $\mathbb{F}_p$（含 `fp64_pow7` 雙射與 `fp64_sponge_step`）、`rbit` 位元反轉與泰勒自舉檢驗（100 萬次迭代基準測試）。
- `build/test-ext-constraint`：驗證編譯期零執行期位元組 Univalence、通用外部約束 $\mathbf{b}_{\text{ext}} / \mathbf{u}_{\text{ext}}$ 動態更新、高維度 `FLOW_QPOLY_DEFINE_EX`、單週期無分支雙閾值遲滯（`b_enter` vs `b_exit`）、純 Q16 2-Jet 二次世界線防穿隧求根，以及 SPSC + UMA + 無鎖 QSBR 外部約束熱切換。
- `build/test-qpoly-poc`：驗證 PoC A（2D/3D 保守內近似量化 0 誤判）、PoC B（ASM `FlowQPoly_eval` 求值加速與飽和保護）、PoC C（1,000 條二次軌跡下一個翻轉面預測 0 LSB 誤差）、PoC D（10,000 次隨機 Cubical HoTT 單一位元鄰居轉移一致性）、PoC E（$10^6$ 次熱路徑 `ru_minflt = 0, ru_majflt = 0, ru_nvcsw = 0` 零 Kernel 介入驗證）。
