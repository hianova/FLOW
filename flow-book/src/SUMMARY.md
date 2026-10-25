# Summary

[前言：純粹數學與顯式硬體的奧卡姆剃刀](introduction.md)

---

# 第一部：哲學與四大數學同構 (Philosophy & 4 Mathematical Isomorphisms)
- [1. 什麼是 FLOW？ (從分離式編譯到 Pure Lean 4 $\to$ C17 湧現引擎)](ch01_what_is_flow.md)
- [2. 意圖 vs. 實作 (Pure `.lean` 零配置撰寫規範與顯式 C17 管線)](ch02_intent_vs_implementation.md)

# 第二部：同倫型別宇宙、精確流形與多面體合成 (Cubical HoTT $\mathcal{U}_0$ & Exact Manifold)
- [3. 64-Bit 立方同倫型別篩、單價公理宇宙 $\mathcal{U}_0$ 與多面體合成](ch03_cubical_hott_and_polyhedral.md)
- [4. 形式化最高法院：定理合成硬體合約 `lean_<fn>`、SMT 四大定理與實戰藍圖](ch04_smt_and_lean4_verification.md)

# 第三部：顯式 C17 原語、連續相空間與無鎖並發 (Explicit C17 Runtime & Primitives)
- [5. 顯式 C17 執行期 (`libflow_rt.a`)：無鎖 SPSC、接觸噴流束 (.fjet)、架構記憶 (.fvec) 與 QSBR 熱替換](ch05_contact_jet_and_qsbr_runtime.md)
- [6. 具身插件、硬體原語驅動與內生同倫宇宙 $\mathcal{U}_0$ 自舉審計](ch06_domain_plugins_and_topology.md)
