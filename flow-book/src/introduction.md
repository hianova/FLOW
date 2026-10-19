# 前言：意圖驅動的奧卡姆剃刀 (The Intent-Driven Occam's Razor)

軟體工程長久以來受困於「意圖（What）」與「實作（How）」的混雜：開發者為了滿足延遲、吞吐量或記憶體上限，被迫手動編寫鎖機制、快取淘汰策略與防禦性樣板碼。

**FLOW** 透過奧卡姆剃刀（Occam's Razor）將核心收斂至 `< 6,000` 行的純 C17 引擎，建立四層正交閉環：

1. **意圖宣告（Declarative Intent）**：使用者僅在 `.flow` 或 Markdown（Doc-as-Intent）中宣告資料流向與契約邊界（`require`、`ensure`、`prefer`、`lean_certified`）。
2. **同倫型別篩與多面體合成（Cubical HoTT & Polyhedral Synthesis）**：將語義約束與元件能力投影至 64-Bit De Morgan 區間 $\mathbb{I}^{64}$，以單一 CPU 週期完成 Kan Open-Box 同倫篩選，並由 Presburger 多面體引擎合成最佳區塊與平行度。
3. **形式化最高法院（SMT & Lean 4 Reflection）**：所有合成計畫皆須通過 QF_BV / QF_LIA 四大安全定理（緩衝區邊界、記憶體配額、分片不重疊、決定論）與零 `sorry` 的 Lean 4 自舉證明。
4. **零摩擦執行與無鎖演化（QSBR & Contact-Jet Runtime）**：透過 64B 快取行對齊的波前 QSBR 熱替換、`.fvec` 架構記憶庫與 `.fjet` 熱力學接觸噴流束，實現零停機動態演化。
