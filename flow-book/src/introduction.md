# 前言：純粹數學與顯式硬體的奧卡姆剃刀

## 給開發者的 30 秒白話定位：FLOW 解決什麼問題？

在日常工程中，我們經常面對三種無法靠傳統 Unit Test 或配置 DSL 根治的痛點：
1. **商業與計價邏輯漏洞**（例如：多重折價疊加導致標錯價虧本、跨階梯返利邊界失守）：Unit Test 只能測你想得到的幾組數字，卻無法窮舉所有邊界。
2. **高頻風控與韌體邊界失守**（例如：量化交易毫秒級部位檢查、工業感測器與機器人控制迴圈）：用高階 GC 語言寫太慢且有停頓；用 C/C++ 手寫又極易發生無號數下溢（Underflow）、NaN 擴散或邊界條件遺漏。
3. **配置注入 DSL 與黑盒樣板地獄**：許多框架發明了類似 Spring Boot XML/Annotation 的中間配置語言（`@quota`、`@patchbay`），在背後吐出沒人想看的樣板 C 代碼與多語言膠水碼。

**FLOW 的解法非常直接：徹底廢除第三種配置/注入語言，回歸「Lean 4 純數學 + C17 顯式工程」的雙核正交架構**：

1. **`.lean` 永遠只寫 100% 純數學（Zero Annotations / Zero Config）**：
   - 裡面只有標準 Lean 4 的四個東西：`structure`、`inductive`、`def`、`theorem`。
   - **連 `@[export]` 都不用寫**：`flowc` 直接透過 **4 條純粹的數學同構映射（Isomorphisms）**，將積型別映射為 C17 `struct`（並將 `structure` 內嵌 `Prop` 欄位提取為定義域前置斷言）、有限和型別狀態轉移映射為 C17 `enum` 與零分支查表（`static const uint8_t <fn>_transition_table`）、純函數複合映射為 C17 三軌算子網路（0-ULP IEEE-754 `double`、確定性 $Q32.32$ 定點數 `flow_fx64_t`、以及二階對偶數前向自動微分 `FlowJetScalar2` `jet2_<fn>`），並將證明該 `def` 的每條 `theorem` 自動編譯為實質蘊含硬體斷言（$P_k(x) \implies Q_k(x, \text{\_ret})$，`FLOW_LEAN_IMPLIES_GUARD`）。
2. **形式化防呆、同倫型別宇宙 $\mathcal{U}_0$ 與多面體合成（Compiler Brain — 編譯器在背景自動完成）**：
   - 編譯器在背景透過 **零 `sorry` 反作弊掃描**、**`Lean-RT` 零 GC 靜態閘門**、64-Bit Cubical HoTT 型別篩（$\mathbb{I}^{64}$）、內生全域型別宇宙 `FlowHoTTUniverse`（$\mathcal{U}_0$ 單價公理 $A \simeq B \implies A = B$ Lean4 $\leftrightarrow$ C17 同構檢驗）與 SMT 四大安全定理（`buffer_bounds`、`memory_quota`、`shard_isolation`、`determinism`），確認 **0 反例（`UNSAT`）** 後才產出 C17 程式碼。
3. **C17 (`*.c`) 直接用顯式程式碼寫管線與硬體配置（Explicit Code over `@` Magic）**：
   - `libflow_rt.a` 提供乾淨透明的 C17 原語（[`flow_spsc.h`](../../src/flow_spsc.h)、[`flow_jet.h`](../../src/flow_jet.h)、[`reload.h`](../../src/reload.h)、[`cubical_hott.h`](../../src/cubical_hott.h)、[`polyhedral.h`](../../src/polyhedral.h)），讓應用層直接以 10 行一目了然的 C17 `struct` 初始化與函式呼叫組裝 SPSC 環形緩衝區、`< 100ns` 無鎖 QSBR 熱替換與辛幾何相空間配速，不做任何黑盒接線。
