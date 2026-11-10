# FLOW — Deterministic External-Constraint ASM × HoTT × 2-Jet Kernel

> **編譯期確定性部分求值（Partial Evaluation）＋ 執行期 90% ASM 全函數基底（Zero C UB）＋ 通用外部約束介面（Universal External-Constraint ABI $\mathbf{b}_{\text{ext}}, \mathbf{u}_{\text{ext}}$）。**

FLOW 將所有編譯期已知的型別同構（Univalence）、硬體拓撲規格（`PAGE=16KB, LINE=128B, WAVE=64`）與多面體法向量矩陣 $A$ 完全壓縮為靜態常數與編譯期 `_Static_assert`，執行期**僅保留對外部輸入變數 $(\mathbf{u}_{\text{ext}}, \mathbf{b}_{\text{ext}})$ 的不可化約全函數轉換**。歷史封存程式碼位於 [`archive/`](archive/) 供對照參考。

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
  │ 4. 無鎖外部變數通道 (src/flow_io.h & src/flow_core.c)                │
  │    • Cache-Line 對齊 SPSC 佇列 + 16KB UMA 零拷貝共享頁 + 無鎖 QSBR   │
  └──────────────────────────────────────────────────────────────────────┘
```

---

## 2. 模組架構一覽（5 Headers + 1 C 檔，零 `double` / 零 `-lm`）

| 檔案 | 職責說明 |
| :--- | :--- |
| [`tools/gen_hw_spec.sh`](tools/gen_hw_spec.sh), `build/flow_hw_spec.h` | **編譯期硬體規格探測**：自動生成 `FLOW_HW_PAGE_BYTES`、`FLOW_HW_CACHE_LINE_BYTES`、`FLOW_HW_L1D_BYTES`、`FLOW_HW_VECTOR_BYTES` 與 `FLOW_HW_WAVE_LANES` |
| [`src/flow_asm.h`](src/flow_asm.h) | **Total ISA 純整數全函數算術、Freestanding 裸機與 32-Bit MCU 基底**：支援 `-ffreestanding -nostdlib -fno-builtin`（`__STDC_HOSTED__ == 0`，0 外部未定義符號 `nm -u`）、跨 ARM64 / x86_64 / Xtensa LX6·LX7（`rsr.ccount`）/ RISC-V（`rdcycle`）硬體週期探針、純暫存器 128-bit 除法 `flow_u128_div_u128`（消除 `___udivti3`）、32-bit MCU 無 `__int128` 精確 64×64→128 算術、10-bit `FlowAsmSieveAxis`、$\mathbb{Z}/2^k\mathbb{Z}$ 遮罩定址、Wave64 Ballot / 1.58-bit 三態內積、Goldilocks 質數體 $\mathbb{F}_p$ ($p = 2^{64}-2^{32}+1$) 無除法模算術與 $x^7 \pmod p$ 雙射自同構（`flow_asm_fp64_pow7`）及 `rbit` 單週期位元反轉 |
| [`src/flow_hott_jet.h`](src/flow_hott_jet.h) | **通用外部約束 HoTT × 純 Q16 2-Jet 核心**：編譯期 Univalence `FLOW_STATIC_ASSERT_UNIVALENT`、位元級 HIT 異常商化 `flow_hott_quotient_sieve`、Kan 鄰居轉移 `flow_cubical_step_neighbor`、純 Q16 2-Jet 整數求根 `flow_jet2_solve_root`、區間凸組合內插 `flow_jet2_lerp`、自舉微分檢驗 `flow_asm_jet2_self_check_step` 與無分支遲滯 `FLOW_QPOLY_DEFINE_EX` |
| [`src/flow_image64.h`](src/flow_image64.h) | **通用 `.flow` 64-Byte Merkle 容器與零拷貝映射基底（零 `BLOOM` 依賴）**：定義 64B 超塊標頭 `FlowImageHeader64`、64B 區塊聯合體 `FlowImageBlock64`、FNV-1a Merkle MAC `flow_image64_compute_mac`、切片驗證 `flow_image64_verify`、$\mathbb{Z}/2^k\mathbb{Z}$ 無分支遮罩索引 `flow_image64_get_block_masked` 與零拷貝 `mmap` 載入器 `flow_image64_mmap_readonly` |
| [`src/flow_io.h`](src/flow_io.h), [`src/flow_core.c`](src/flow_core.c) | **無鎖外部約束輸入與熱切換層**：預設匯出 `flow_image64.h`，並提供 `FLOW_SPSC_DEFINE` 快取線對齊無鎖佇列、`flow_uma_buffer_alloc` 對齊共享記憶體池、零 Mutex 無鎖 QSBR 外部約束庫指標熱切換（`flow_qsbr_publish_ptr` / `flow_qsbr_load_ptr`）與 `flow_reload_resource_sieve` |
| [`tests/test_freestanding_kernel.c`](tests/test_freestanding_kernel.c), [`tests/test_asm_sieve.c`](tests/test_asm_sieve.c), [`tests/test_ext_constraint.c`](tests/test_ext_constraint.c), [`tests/test_qpoly_poc.c`](tests/test_qpoly_poc.c) | **純整數與 Ring-0 裸機驗證套件**：涵蓋 Stage `[0/3]` `-ffreestanding -nostdlib -fno-builtin` 零外部符號檢查（`nm -u == empty`，含 64-bit ASM 與 32-bit MCU 模式）、ASM 全函數算術、Q16 除零/定義域/溢位篩網、遮罩記憶體、2-Jet 自舉檢驗、通用外部約束與遲滯防抖，以及 PoC A–E 量化與零 Kernel 介入驗證 |
| [`archive/`](archive/) | **歷史版本封存**：保留重構前之舊版檔案供參考對照 |

---

## 3. 應用層最佳實踐與端到端開發指南（Zero Lean-to-C Codegen）

不需另外翻閱 `BLOOM` 或 `FLOWY` 原始碼，開發任何基於 `FLOW` 的應用層專案時，請遵循以下**核心設計鐵律**：

### 為什麼絕不做「Lean 轉 C 程式碼（Lean-to-C Codegen）」，也不手寫兩份規則函式？

- ❌ **錯誤反模式（Dual-Writing / Codegen）**：
  在 `.lean` 裡寫一套 `if/else` 業務分支函式，然後在 C 標頭檔（如 `my_rules.h`）又手寫（或自動轉譯）同一套 C 函式。這會退化成傳統的硬編碼分支邏輯：每次修改閾值或策略都必須重新編譯 C 執行檔，且完全無法使用 64-byte 快取線同構、`.flow` 零拷貝映射與無鎖 QSBR 熱更新。
- ✅ **FLOW 正統實踐（64B 資料描述子驅動預編譯 C17/ASM 通用算子）**：
  1. **C17 執行期算子永遠固定**：應用層直接呼叫 `FLOW` 預編譯好的通用算子（`FLOW_QPOLY_DEFINE_EX` 單週期無分支雙閾值遲滯、`FlowJet2` 二階泰勒預測與求根、`flow_cubical_step_neighbor` Kan 路由、`flow_asm_*` 全函數算術與 10-bit 篩網）。
  2. **`.lean` 僅負責「形式證明（`0 sorryAx`）」與「64B 外部約束參數 SSOT」**：在 `.lean` 中定義外部約束常數（`route_op_mask`、`b_enter_q16`、`b_exit_q16`、`weight_q16`、`qshift`、`req_mask`、`req_values`）並證明其不變量。
  3. **`flowy lean` 封裝為 64B 二進位描述子 / `.flow` 容器**：編譯期以 `flowy lean` 將 `.lean` 常數封裝成 64-byte 二進位檔（`.bin` 或 `.flow`），C17 執行期直接載入該 64-byte 描述子餵給 `FLOW` 算子，並可用 `flow_qsbr_publish_ptr` 進行 **0 停機、0 重編譯熱切換**。

---

### Step-by-Step 最小完整範例（零 `BLOOM` 耦合）

#### Step 1：撰寫 Lean 4 外部約束規格與證明（`research/AppRules.lean`）

```lean
structure AppRuleState where
  metricQ16 : Int
  sieveMask : UInt64

-- 64B FlowLeanAbiDesc64 標準 SSOT 外部約束參數（由 flowy lean 自動擷取封裝）
def route_op_mask : UInt32 := 0x0000000D
def qshift        : UInt32 := 16
def b_enter_q16   : Int    := 73728   -- 1.125 in Q16.16 (進入警戒閾值)
def b_exit_q16    : Int    := 57344   -- 0.875 in Q16.16 (解除警戒閾值)
def weight_q16    : Int32  := 3276
def req_mask      : UInt64 := 0x000000000000000F
def req_values    : UInt64 := 0x0000000000000005

-- 證明雙閾值遲滯死區嚴格有序且路由位元遮罩自洽（禁止使用 X = X := rfl 空洞定理）
theorem hyst_deadband_sound : b_exit_q16 < b_enter_q16 := by decide
theorem route_cap_sound :
  route_op_mask > 0 ∧ qshift = 16 ∧ weight_q16 > 0 ∧ (req_values &&& req_mask) = req_values := by decide
```

#### Step 2：C17 應用層載入 64B 描述子 / `.flow` 容器並驅動 `FLOW` 算子（`src/app.c`）

只需連結 `FLOW`（`-I/path/to/FLOW/src -I/path/to/FLOW/build /path/to/FLOW/build/libflow_rt.a`），**完全不需要引入 `BLOOM`**：

```c
#include <assert.h>
#include <stdio.h>
#include "flow_asm.h"
#include "flow_hott_jet.h"
#include "flow_io.h" /* 內含 flow_image64.h (.flow 64B 容器) 與無鎖 QSBR */

/* 1. 定義 64-byte 快取線對齊的規則描述子（與 flowy lean --emit-abi 二進位佈局 100% 同構） */
typedef struct __attribute__((aligned(64))) {
    uint64_t magic;                  /* 8B: 0x464C4F574C45414EULL ("FLOWLEAN") */
    uint32_t route_op_mask;          /* 4B: 算子路由遮罩 */
    uint32_t qshift;                 /* 4B: 定點位移 (16) */
    int32_t  b_enter_q16;            /* 4B: 遲滯觸發上界 b_enter */
    int32_t  b_exit_q16;             /* 4B: 遲滯釋放下界 b_exit */
    int32_t  weight_q16;             /* 4B: 權重 / 阻尼係數 */
    uint32_t theorem_and_field_pack; /* 4B: (theorem_count << 16) | field_count */
    uint64_t req_mask;               /* 8B: 必要能力遮罩 */
    uint64_t req_values;             /* 8B: 必要能力目標值 */
    uint64_t spec_schema_hash;       /* 8B: .lean 規格 FNV-1a 雜湊 */
    uint64_t abi_mac;                /* 8B: 前 56B 完整性 MAC */
} AppRuleDesc64;

FLOW_STATIC_ASSERT_UNIVALENT(AppRuleDesc64, FlowImageBlock64);

/* 2. 宣告預編譯 1D Q-Polyhedron 無分支遲滯算子 */
FLOW_QPOLY_DEFINE_EX(AppGovernorPoly, 1, 16, 4)

int main(void) {
    /* 3. 直接載入由 `flowy lean --emit-abi` 產生的 64B 二進位描述子（或透過 flow_image64_mmap_readonly 映射 .flow） */
    AppRuleDesc64 rule;
    FILE *f = fopen("build/app_rules.bin", "rb");
    assert(f && fread(&rule, sizeof(AppRuleDesc64), 1, f) == 1);
    fclose(f);

    /* 4. 將 64B 描述子參數注入 FLOW 預編譯算子（零動態編譯、零浮點、零配置） */
    AppGovernorPoly gov;
    AppGovernorPoly_init(&gov);
    gov.A[0][0] = 1;
    AppGovernorPoly_set_hysteresis(&gov, 0, rule.b_enter_q16, rule.b_exit_q16);

    int32_t x_in[1] = { 65536 }; /* 外部輸入 u_ext = 1.0 (位於 [b_exit, b_enter] 死區內) */
    FlowQPolyEval ev = AppGovernorPoly_eval_hysteresis(&gov, x_in, 0ULL);

    /* 5. 結合 2-Jet 二階微分預測與 10-bit 硬體篩網 */
    uint64_t sieve = FLOW_ASM_SIEVE_ALL_SOUND;
    FlowJet2 jet = flow_jet2_make(x_in[0], rule.weight_q16, 0);
    FlowResultU64 next_v = flow_asm_qadd_i64(jet.v, jet.dv, &sieve);
    assert(!flow_result_u64_is_boundary(next_v) && sieve == FLOW_ASM_SIEVE_ALL_SOUND);
    (void)ev;
    return 0;
}
```

#### Step 3：`Makefile` 整合 `FLOWY` Gate 0 驗證與 `.flow` 打包

```makefile
verify-gate0:
	# 1. 驗證 C 結構體 100% 64B 對齊無填充洞，且 C17 產出 0 Float / 0 Heap
	flowy slice src/app.c --audit-all --llvm-ir --mca
	# 2. 驗證 .lean 證明 (0 sorryAx, 0 同義反覆定理) 並直接產出 64B 二進位規則描述子
	flowy lean --from-lean research/AppRules.lean --emit-abi build/app_rules.bin --verify
	flowy lean --from-lean research/AppRules.lean --verify-abi build/app_rules.bin
	# 3. 基於 64B ABI 布局與 Lean 定理數自動推導語義版本與 64-Bit ABI 指紋
	flowy version .
```

---

### 生態系分層職責一覽（按需選用，互不強制耦合）

| 專案 | 定位與職責 | 依賴關係 |
| :--- | :--- | :--- |
| **`FLOW`** | **L0 數學、90% ASM 全函數算子、HoTT × 2-Jet、無鎖 QSBR 與 `.flow` 64B 容器核心** | 零外部依賴（支援裸機 `-ffreestanding`） |
| **`FLOWY`** | **Gate 0 跨層形式證明、64B 幾何、LLVM IR/MCA、代數風洞與自動語義版本 (`flowy version`) CLI** | 僅依賴 `FLOW` |
| **`UNIT`** | **資訊編碼、16B `UnitCell128` 協議、量子抗性 Goldilocks $\mathbb{F}_p$ 瞬態棘輪握手** | 僅依賴 `FLOW` |
| **`BLOOM`** | **純軟硬體無關 C17 視覺光學、SDF/CSG、物理排版、宣告式 `FlowUiIntentSpec64` 自動 Bento 合成與 SVG-UI 框架** | 依賴 `FLOW` + `UNIT`（零硬體/Metal 耦合） |
| **`FLUX`** | **系統核心：Zero-Root Capability 安全閘門、零拷貝 I/O 環與統一 GPU / Metal HAL** | 依賴 `FLOW` |
| **`ELE`** | **嵌入式與實體電子能量治理：PPM 表面電荷波導、2-Jet 零位元同步、CSI 相位全息、零程式碼 64B 驅動與 1-Cycle 熱插拔** | 僅依賴 `FLOW` |
| **`IDLE`** | **零訓練 0-ULP 解析幾何自動駕駛、具身非光滑接觸力學與自主搜救脫困核心** | 僅依賴 `FLOW` |

---

## 4. 快速建置與測試驗證

```bash
make fmt-check
make clean && make all && make test
```
包含 3 個全記憶體純整數測試執行檔：
- `build/test-asm-sieve`：驗證 ASM 有號 Q 算術溢位捕捉（`NZCV.V`）、純 Q16 `isqrt`/`qsqrt`/`qdiv`/`qlerp`/`qmargin3` 與邊界標籤篩網、遮罩環形記憶體定址（零 Segfault）、Wave64 1.58-bit 內積、Goldilocks $\mathbb{F}_p$（含 `fp64_pow7` 雙射）、`rbit` 位元反轉與泰勒自舉檢驗（100 萬次迭代基準測試）。
- `build/test-ext-constraint`：驗證編譯期零執行期位元組 Univalence、通用外部約束 $\mathbf{b}_{\text{ext}} / \mathbf{u}_{\text{ext}}$ 動態更新、高維度 `FLOW_QPOLY_DEFINE_EX`、單週期無分支雙閾值遲滯（`b_enter` vs `b_exit`）、純 Q16 2-Jet 二次世界線防穿隧求根，以及 SPSC + UMA + 無鎖 QSBR 外部約束熱切換。
- `build/test-qpoly-poc`：驗證 PoC A（2D/3D 保守內近似量化 0 誤判）、PoC B（ASM `FlowQPoly_eval` 求值加速與飽和保護）、PoC C（1,000 條二次軌跡下一個翻轉面預測 0 LSB 誤差）、PoC D（10,000 次隨機 Cubical HoTT 單一位元鄰居轉移一致性）、PoC E（$10^6$ 次熱路徑 `ru_minflt = 0, ru_majflt = 0, ru_nvcsw = 0` 零 Kernel 介入驗證）。
