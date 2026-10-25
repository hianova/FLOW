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
2. **和型別（`inductive` $\to$ C17 `typedef enum`）**：
   - `ControlMode` 直接湧現為 `typedef enum { idle = 0, track = 1, fault = 2 } ControlMode;`。
3. **純函數與區域綁定（`def` / `private def` / `let ... in` $\to$ C17 算子網路）**：
   - `private def half_dt` 僅在內部展開，絕不污染公開 C 標頭或匯出符號表。
   - 非 `private` 的 `def` 自動生成雙軌 C17 算子：IEEE-754 `double` 版（`bounded_gain` / `flowc_bounded_gain`）與確定性 $Q32.32$ 定點數版（`bounded_gain_fx` / `flowc_bounded_gain_fx`）。
   - 算子內的 `let x := rhs in` 與 `let (a, b) := callee ... in` 直接生成 C17 原生區域變數與函式呼叫。
4. **多定理邏輯交集湧現硬體合約（`theorem` $\to$ `lean_<fn>` / `flowc_lean_<fn>`）**：
   - `bounded_gain_upper` 的 `(h_lim : limit > 0.0)` 與 `bounded_gain_nonneg` 的蘊含前提 `x >= 0.0 →` 自動取交集合成為前置斷言：
     `FLOW_LEAN_PRE_GUARD((limit > 0.0) && (x >= 0.0), 0.0);`
   - 兩條定理的結論自動取交集合成為後置斷言：
     `FLOW_LEAN_POST_GUARD((_ret <= limit) && (_ret >= 0.0), 0.0);`

---

## 2.3 Pure `.lean` 零試錯撰寫鐵律（`Lean-RT` Zero-GC Subset）

1. **遵守 `Lean-RT` 零 GC 實時子集與零作弊鐵門**：
   - 嚴格禁止 `sorry`、`admit`、`sorryAx`、`native_decide`、自訂 `axiom` 與未閉合的 `/-` 區塊註解。
   - 嚴格禁止動態堆積配置與無界遞迴：`List`、`Array`、`ByteArray`、`++`、高階閉包（`fun x =>`）、自遞迴函數與遞迴 `inductive` 型別。
   - 支援最多 `128` 個函數（`FLOW_LEAN_FN_MAX`）、`128` 條定理（`FLOW_LEAN_THEOREM_MAX`）與 `32` 個參數／欄位。
2. **證明欄位與證明參數自動抹除（Proof Erasure）**：
   - `structure` 或 `def` 中的 `Prop` 命題與不等式欄位（如 `(h_lim : limit > 0.0)`）在 C17 `struct` 與函式簽名中自動抹除為 0 bytes，同時自動收斂至 `lean_<fn>` 的前置斷言。
3. **內建數學與硬體量化原語**：
   - `Float.abs` (`f_abs`)、`Float.sqrt`、`Float.exp`、`Float.log`、`Float.sin`、`Float.cos`、`Float.atan2`、`Float.pow`、`Float.floor`、`Float.ceil`、`Float.round`、`Float.max` (`f_max`)、`Float.min` (`f_min`)、`Float.ofNat`、`Float.ofInt`、`Nat.min`、`Nat.max`，以及硬體量化 `flow_quantize` / `flow_dequantize`，皆直接映射為零開銷 C17 內聯算子與 $Q32.32$ 定點算子。
