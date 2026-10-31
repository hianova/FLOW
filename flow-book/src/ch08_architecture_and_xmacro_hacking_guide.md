# 第八章：Architecture & X-Macro Hacking Guide（核心架構與外部開發者擴充速查表）

> **給外部開發者與審計者的核心承諾**：FLOW 的設計目標是成為**不依賴 AI Agent、人類開發者直接閱讀原始碼即可修改與驗證的純 C17 數學基礎庫**。為此，FLOW 借鏡 **LLVM TableGen（X-Macro 資料表驅動）**、**LLVM Pass 語意下沉（`SemanticIR`）** 與 **Linux Kernel Single-Header 宏統合**，將所有型別映射、數學內建算子、算子變體遮罩與 Runtime/CodeGen 共用型別收斂為單一真相來源（SSOT）。

---

## 8.1 編譯鏈路與單向 Pass 架構圖 (`flowc` & `libflow.a`)

FLOW 嚴格禁止後端（`backend.c`、`abi.c`）重新解析 AST 或推論型別語意；所有語意屬性均於 `parser.c` 的 `lower_to_ir` 階段一次性算好並寫入 `SemanticIR`：

```text
  Pure Lean 4 (.lean) / 觀測軌跡 (.csv)
                  │
                  ▼
  ┌──────────────────────────────────────────────────────────┐
  │ 1. 前端解析與降階 (src/parser.c + src/flow.h X-Macros)   │
  │    • parse_spec()       : 解析 structure / inductive /   │
  │                           def / theorem，執行 Zero-Sorry │
  │                           與 Zero-GC 即時閘門            │
  │    • lower_to_ir()      : 降階為 SemanticIR              │
  │    • flow_ir_compute_fn_variants() :                     │
  │        一次性計算 fn->expr_mode (FlowLeanExprMode)       │
  │        與 fn->variant_mask (FlowOperatorVariantBit)      │
  └──────────────────────────────────────────────────────────┘
                  │
                  ▼  SemanticIR (唯讀語意中間表示)
  ┌──────────────────────────────────────────────────────────┐
  │ 2. 同倫篩網、多面體與單價驗證 (src/cubical_hott.c,       │
  │    src/bitspace.c, src/flow.h)                           │
  │    • 64-Bit Cubical HoTT Sieve & Kan Open-Box Filling    │
  │    • Univalence U_0 (A[64] ≃ A_SOA64)                    │
  │    • Conservative Box-Interval SMT Verifier              │
  └──────────────────────────────────────────────────────────┘
                  │
        ┌─────────┴─────────┐
        ▼                   ▼
  ┌───────────────┐   ┌───────────────┐
  │ 3a. C17 實作  │   │ 3b. C17 標頭  │
  │ src/backend.c │   │ src/abi.c     │
  │ 讀取 variant  │   │ 讀取 variant  │
  │ _mask 與 expr │   │ _mask 與共用  │
  │ _mode 吐出 .c │   │ FLOW_STR 吐.h │
  └───────────────┘   └───────────────┘
```

---

## 8.2 五大常見修改場景：一行表驅動速查表（Hacking Cheat Sheet）

| 你想新增／修改的功能 | 唯一需要修改的單一真相來源（SSOT）位置 | 自動生效範圍 |
| :--- | :--- | :--- |
| **① 新增 Lean 4 純量型別或別名**<br>（例如新增 `Float16` 或數學符號別名） | [`src/flow.h`](../../src/flow.h) 的 **`FLOW_LEAN_SCALAR_TYPES_X(X)`** 新增一行：<br>`X("MyScalar", "MyScalar", "double", 8U, FLOW_EXPR_FLOAT)` | `parser.c` 的位元組寬度計算、純量合法性檢查、結構體欄位佈局、`flow_lean_scalar_to_c_type`、`flow_c_type_expr_mode` 全自動生效。 |
| **② 新增 Lean 4 數學內建函式**<br>（例如新增 `Float.tanh` / `Real.tanh`） | 1. [`src/flow.h`](../../src/flow.h) 的 **`FLOW_LEAN_BUILTIN_OPS_X(X)`** 新增映射：<br>`X("Float.tanh", "flow_float_tanh")`<br>2. [`src/bitspace.h`](../../src/bitspace.h) 的 **`FLOW_LEAN_FLOAT_OPS_C17`** 與 **`FLOW_JET2_MATH_C17`** 補上對應 `static inline` 實作 |1. `parser.c` 的 `flow_lean_normalize_expr` 與純量本體白名單自動放行；<br>2. `backend.c` 自動吐出 IEEE-754 `flow_float_tanh` 與二階自動微分 `flow_jet2_tanh`；<br>3. 本體 `libflow.a` 與生成的 `.c`/`.h` 同步獲得該函式。 |
| **③ 新增算子合成變體（Variant）**<br>（例如新增一種認證或批次包裝器） | 1. [`src/flow.h`](../../src/flow.h) 的 **`FLOW_OPERATOR_VARIANTS_X(X)`** 新增位元遮罩定義<br>2. [`src/parser.c`](../../src/parser.c) 的 **`flow_ir_compute_fn_variants()`** 設定啟用條件 | `FlowOperatorVariantBit` 列舉自動生成，`backend.c` 與 `abi.c` 直接讀取 `fn->variant_mask` 決定是否吐出宣告與實作。 |
| **④ 修改 Runtime 與 CodeGen 共用的 C17 型別或邊界輔助函式** | • [`src/bitspace.h`](../../src/bitspace.h)：`FLOW_JET_SCALAR2_TYPEDEF_C17`、`FLOW_ABI_CORE_TYPES_C17`<br>• [`src/cubical_hott.h`](../../src/cubical_hott.h)：`FLOW_BOUNDARY_VIOLATION_KIND_C17`、`FLOW_LEAN_PRIMITIVE_WITNESS_TYPES_C17`、`FLOW_HOTT_BOUNDARY_HELPERS_C17` | 本體編譯直接展開宏定義；`backend.c` 與 `abi.c` 透過 `FLOW_STR(...)` 字串化吐出至生成的 `.c`/`.h`，零雙份模板同步負擔。 |
| **⑤ 新增 `flowy` CLI 子命令** | [`src/flowy_main.c`](../../src/flowy_main.c) 的 **`FLOW_CLI_COMMANDS[]`** 註冊命令別名陣列與 handler 函式指標 | `flowy <subcommand>` 自動分派執行。 |

---

## 8.3 實戰範例：30 秒內為 FLOW 新增 `Float.tanh` 算子（含二階自動微分 `Jet2`）

假設你想在 `.lean` 規格中直接撰寫 `Float.tanh x`，並讓 FLOW 自動合成 IEEE-754 實作與 `FlowJetScalar2` 二階前向自動微分（$f(x) = \tanh x,\; f'(x) = 1 - \tanh^2 x,\; f''(x) = -2\tanh x(1 - \tanh^2 x)$）：

### 步驟 1：在 [`src/flow.h`](../../src/flow.h) 的 `FLOW_LEAN_BUILTIN_OPS_X` 加入一行
```c
#define FLOW_LEAN_BUILTIN_OPS_X(X) \
    /* ... 既有算子 ... */ \
    X("Float.tanh", "flow_float_tanh") \
    X("Real.tanh",  "flow_float_tanh")
```

### 步驟 2：在 [`src/bitspace.h`](../../src/bitspace.h) 的 `FLOW_LEAN_FLOAT_OPS_C17` 與 `FLOW_JET2_MATH_C17` 加入實作
```c
/* 於 FLOW_LEAN_FLOAT_OPS_C17 補上： */
static inline __attribute__((unused)) double flow_float_tanh(double x) { \
    return __builtin_tanh(x); \
}

/* 於 FLOW_JET2_MATH_C17 補上二階鏈式法則 (Chain Rule)： */
static inline __attribute__((unused)) FlowJetScalar2 flow_jet2_tanh(FlowJetScalar2 x) { \
    double t = __builtin_tanh(x.v); \
    double sech2 = 1.0 - t * t; \
    double d1 = sech2 * x.d1; \
    double d2 = -2.0 * t * sech2 * x.d1 * x.d1 + sech2 * x.d2; \
    return flow_jet2_make(t, d1, d2); \
}
```

**完成！** 你不需要碰 `parser.c`、不需要碰 `backend.c`、也不需要碰 `abi.c`。執行 `make test`，解析器白名單、AST 正規化、C17 代碼生成與 `flowc_jet2_<fn>` 二階微分算子全部自動串接生效。

---

## 8.4 核心原始碼檔案導覽圖（Source Code Map）

| 檔案路徑 | 職責說明 | 閱讀優先級 |
| :--- | :--- | :--- |
| [`src/flow.h`](../../src/flow.h) | **全專案總綱**：X-Macro 單一真相表（`FLOW_LEAN_SCALAR_TYPES_X`、`FLOW_LEAN_BUILTIN_OPS_X`、`FLOW_OPERATOR_VARIANTS_X`）、`SemanticIR` 結構定義與保守區間 SMT 驗證器（`flow_smt_verify_rules_impl`）。 | ⭐⭐⭐⭐⭐（必讀第一站） |
| [`src/parser.c`](../../src/parser.c) | Lean 4 子集解析器、Zero-Sorry / Zero-GC 審查閘門、X-Macro 型別映射展開，以及 `flow_ir_compute_fn_variants` 語意降階。 | ⭐⭐⭐⭐ |
| [`src/bitspace.h`](../../src/bitspace.h) / [`src/bitspace.c`](../../src/bitspace.c) | 64-Bit 位元空間搜尋、Pareto 多面體評估，以及透過 `FLOW_STR(...)` 與 CodeGen 共用的 IEEE-754 / `FlowJetScalar2` / `FlowU256` 256-bit 大數與矩陣運算宏。 | ⭐⭐⭐⭐ |
| [`src/cubical_hott.h`](../../src/cubical_hott.h) / [`src/cubical_hott.c`](../../src/cubical_hott.c) | 64-Bit De Morgan 區間代數、Kan 開盒填塞、子物件分類器見證型別宏（`FLOW_LEAN_PRIMITIVE_WITNESS_TYPES_C17`）、單價 SOA64 驗證、同倫纖維 $h$-Level 與離散 4-Jet（$J^4 E$）、以及 $\partial M$ Quiet NaN 邊界拉回宏（`FLOW_HOTT_BOUNDARY_HELPERS_C17`）。 | ⭐⭐⭐⭐ |
| [`src/backend.c`](../../src/backend.c) / [`src/abi.c`](../../src/abi.c) | 純 C17 算子實作（`.c`）、獨立靜態內聯標頭檔（`--target-c-header`）與 MATLAB MEX 分派器（`--target-mex`）生成器。 | ⭐⭐⭐ |
| [`src/induce.h`](../../src/induce.h) / [`src/induce.c`](../../src/induce.c) | 歸納式知識合成：四階經驗矩、Wasserstein-1 / KS 距離、連分數有理數多項式擬合 $\mathbb{Q}[x]$、有限網格反射與 SMM 隨機微分方程反推。 | ⭐⭐⭐ |
| [`src/flow_jet.h`](../../src/flow_jet.h) / [`src/flow_jet.c`](../../src/flow_jet.c) | 辛流形能量漂移監控與 $O(1)$ 相空間注意力路由器（`FlowPhaseAttentionRouter`）。 | ⭐⭐⭐ |
| [`src/reload.h`](../../src/reload.h) / [`src/reload.c`](../../src/reload.c) / [`src/flow_spsc.h`](../../src/flow_spsc.h) | 無鎖 QSBR 世代熱重載狀態機、Dual-Mapped 環形緩衝區與 Header-only 無鎖 SPSC 佇列。 | ⭐⭐⭐ |

---

## 8.5 程式碼排版與外部審計紀律（`make fmt` & `-Werror`）

為確保每一行 C17 程式碼皆具備高可讀性與零未定義行為，FLOW 在根目錄提供 [`.clang-format`](../../.clang-format)（100 列寬、禁止單行擠壓多個 statement 或條件分支）與嚴格的 `-Werror` 建置目標：

```sh
# 1. 自動將全專案 src/、plugins/、tests/ 格式化為標準多行 C17 排版
make fmt

# 2. CI 排版一致性檢查（零差異通過）
make fmt-check

# 3. 在 -std=c17 -Wall -Wextra -Wpedantic -Werror 下執行完整編譯與 4 大測試套件
make clean && make test
```
