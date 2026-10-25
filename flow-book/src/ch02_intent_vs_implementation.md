# 第二章：意圖 vs. 實作 (Pure `.lean` 零配置撰寫規範與顯式 C17 管線)

> 「數學與定理直接寫在 Lean 4，硬體與管線直接寫在 C17 —— 中間不需要任何第三種配置語言。」

---

## 2.1 為什麼徹底廢除 `.flow` 與 `@` 註解注入？

在「Lean 4 + C17」的架構裡：
1. **`.lean` 永遠只寫 100% 純數學（Zero Annotations / Zero Config）**：
   - 檔案內只有標準 Lean 4 的 `structure`、`inductive`、`def`、`theorem`。
   - 嚴格拒絕 `@quota`、`@embodied`、`@reload`、`@patchbay` 等非標準註解（[`src/parser.c`](../../src/parser.c) 遇到退休的 `@` 註解會立即報錯攔截）。
   - 不使用任何偽裝成 Lean 變數的硬體配置注入（例如寫死名稱的 `def slaLatencyNs` 在 `parser.c` 中僅被視為普通純函數，絕不偷偷修改編譯器內部硬體設定）。
2. **C17 (`*.c`) 直接用顯式程式碼寫管線與硬體配置**：
   - 頻率是 1kHz、Ring Buffer 是 64 格、先跑哪個算子再跑哪個算子——在 C17 的 `main.c` 裡本來就是 10 行一目了然的 `struct` 初始化與函式呼叫，直接連結 `libflow_rt.a` 即可。

---

## 2.2 Pure `.lean` 的四大數學同構映射實例

開發者直接撰寫標準 Lean 4 檔案（無需 `@[export]`）：

```lean
namespace Embodied.Control

inductive ControlMode where
  | idle
  | track
  | fault

structure Vec2 where
  x : Float
  y : Float

structure PoseFrame where
  pos   : Vec2
  ticks : UInt64

private def half_dt : Float := 0.5

def step_pose (p : PoseFrame) (vx : Float) (vy : Float) : PoseFrame :=
  { pos := { x := p.pos.x + vx * half_dt, y := p.pos.y + vy * half_dt }, ticks := p.ticks + 1 }

def bounded_gain (x : Float) (limit : Float) : Float :=
  if x > limit then limit else x

theorem bounded_gain_upper (x : Float) (limit : Float) (h_lim : limit > 0.0) :
  bounded_gain x limit <= limit := by decide

theorem bounded_gain_nonneg (x : Float) (limit : Float) :
  x >= 0.0 → bounded_gain x limit >= 0.0 := by decide

end Embodied.Control
```

當執行 `flowc control.lean -o build/control.c --target-c-header build/control.h` 時，會自動湧現：

1. **積型別與巢狀結構展平（`structure` $\to$ C17 `typedef struct`）**：
   - `PoseFrame` 自動展平為包含 `double pos_x; double pos_y; uint64_t ticks;` 的 C17 `typedef struct PoseFrame`（24 bytes, 8-byte aligned）。
   - 多回傳值函數 `def f (...) : (Float × Float)` 自動在標頭檔湧現 `typedef struct { double out_0; double out_1; } f_tuple_t;` 與雙指標輸出介面。
2. **和型別與零分支有限狀態機查表（`inductive` $\to$ C17 `typedef enum` + `transition_table`）**：
   - `ControlMode` 直接湧現為 `typedef enum { idle = 0, track = 1, fault = 2 } ControlMode;`。
   - 當算子之輸入（1 或 2 個參數）與回傳型別皆為有限 `inductive` 列舉（如 `def step_order_fsm (s : OrderState) (ev : ExecEvent) : OrderState := match s, ev with ...`），`flowc` 在編譯期直接窮舉笛卡兒積 $(s, ev) \in S \times E$ 並合成 C17 零分支查表 `static const uint8_t step_order_fsm_transition_table[STATE_COUNT][EVENT_COUNT]`，將非同步狀態轉移壓縮為 $O(1)$ 零分支陣列索引，同時透過 `lean_step_order_fsm` 執行狀態轉移定理（如 `no_zombie_order`）實質蘊含保護。
3. **純函數、區域綁定與 2-Jet 前向自動微分（`def` / `private def` / `let ... in` $\to$ 三軌 C17 算子網路）**：
   - `private def half_dt` 僅在內部展開，絕不污染公開 C 標頭或匯出符號表。
   - 非 `private` 的純量浮點 `def` 自動生成三軌 C17 算子：
     1. IEEE-754 `double` 版（`bounded_gain` / `flowc_bounded_gain`）
     2. 確定性 $Q32.32$ 定點數版（`bounded_gain_fx` / `flowc_bounded_gain_fx`）
     3. **2-Jet 二階對偶數自動微分版（`jet2_bounded_gain` / `flowc_jet2_bounded_gain`）**：以 `FlowJetScalar2 { double v; double d1; double d2; }` 沿 AST 鏈式法則自動傳遞 $(f(x), f'(x), f''(x))$（在量化金融中一次算出理論價格 $V$、一階敏感度 Delta $\partial_S V$ 與二階曲率 Gamma $\partial_{SS} V$，在機器人控制中一次算出位形、速度雅可比與加速度海森對角項）。
   - 算子內的 `let x := rhs in` 與 `let (a, b) := callee ... in` 直接生成 C17 原生區域變數與函式呼叫。
4. **結構型定義域前置斷言與定理實質蘊含後置斷言（`structure` `Prop` & `theorem` $\to$ `lean_<fn>` / `flowc_lean_<fn>`）**：
   - **結構型定義域假設（Structural / Domain Precondition $\to$ `fn->precond_expr`）**：來自輸入 `structure` 內嵌的 `Prop` 欄位（如 `h_equity_pos : total_equity > 0`、`h_var_nonneg : micro_variance >= 0`）或 `def` 簽名中的依賴證明參數（如 `(h_lim : limit > 0.0)`），違反代表輸入物理意義非法，直接於呼叫前攔截：
     `if (!(domain_ok)) return 0.0;`
   - **定理實質蘊含斷言（Material Implication Guard $P(x) \implies Q(x, \text{\_ret})$）**：每條針對算子證明的 `theorem` 皆編譯為實質蘊含斷言：
     `if ((P_k(x)) && !(Q_k(x, _ret))) return 0.0;`
   - 如此一來，在 `.lean` 中撰寫越多局部定理（例如同時證明 `v > 0` 上漲性質與 `v < 0` 下跌性質），`lean_<fn>` 的形式化防護網就越密，且絕不誤殺合法輸入！

---

## 2.3 Pure `.lean` 零試錯撰寫鐵律（`Lean-RT` Zero-GC Subset）

1. **遵守 `Lean-RT` 零 GC 實時子集與零作弊鐵門**：
   - 嚴格禁止 `sorry`、`admit`、`sorryAx`、`native_decide`、自訂 `axiom` 與未閉合的 `/-` 區塊註解。
   - 嚴格禁止動態堆積配置與無界遞迴：`List`、`Array`、`ByteArray`、`++`、高階閉包（`fun x =>`）、自遞迴函數與遞迴 `inductive` 型別。
   - 支援最多 `128` 個函數（`FLOW_LEAN_FN_MAX`）、`128` 條定理（`FLOW_LEAN_THEOREM_MAX`）與 `32` 個參數／欄位。
2. **證明欄位與證明參數自動抹除（Proof Erasure）**：
   - `structure` 或 `def` 中的 `Prop` 命題與不等式欄位（如 `(h_lim : limit > 0.0)`）在 C17 `struct` 與函式簽名中自動抹除為 0 bytes，同時自動收斂至 `lean_<fn>` 的前置斷言。
3. **內建數學、Mathlib 語法糖與實戰合成人體工學**：
   - `Float.abs` (`f_abs`)、**Mathlib 絕對值語法糖 `|x|`**、`Float.sqrt`、`Float.exp`、`Float.log`、`Float.sin`、`Float.cos`、`Float.atan2`、`Float.pow`、`Float.floor`、`Float.ceil`、`Float.round`、`Float.max` (`f_max`)、`Float.min` (`f_min`)、`Float.ofNat`、`Float.ofInt`、`Nat.min`、`Nat.max`，以及硬體量化 `flow_quantize` / `flow_dequantize`，皆直接映射為零開銷 C17 內聯算子與 $Q32.32$ 定點算子。
   - **縮排感知跨行 `let` 綁定**：長公式的 `let var :=\n  expr` 支援跨行書寫，自動折疊為完整 C17 區域變數綁定。
   - **實質蘊含定理守衛與雙向蘊含過濾**：`extract_lean_theorem_contract` 將每條可計算標量定理 $\forall x,\, P(x) \implies Q(x, f(x))$ 編譯為實質蘊含斷言 `if ((P(x)) && !(Q(x, _ret))) return fallback;`，讓多條互斥前提的局部定理（如 `v > 0` 與 `v < 0`）可同時共存並強化防護網，並自動過濾含未展開 `↔` / `→` 的高階命題。
   - **`Spec` $\to$ `Exec` 同名可計算鏡像覆寫（Computable Mirror Override）**：當同一檔案先以 `ℝ` 定義理論層 `Spec.fn` 並完成定理證明，後以 `Float` 定義同名執行層 `Exec.fn` 時，`parser.c` 自動以 `Exec` 算子本體覆寫，同時完整繼承 `Spec` 階段的定理前後置契約。
   - **純淨函式庫與 MATLAB MEX 合成（`--target-c-header` / `--target-mex`）**：搭配 `--target-c-header` 或 `--target-mex` 時，生成的 `.c` 檔預設不輸出 `int main(void)`，可直接與應用層 `main.c`、Simulink C Caller 或 MATLAB `mex` 零衝突連結。
