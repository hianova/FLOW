# 第三章：64-Bit 離散同倫型別篩與 Presburger 多面體合成

FLOW 將語義 IR（`SemanticIR`）與註冊元件（`Component`）映射為 64-Bit 離散立方同倫型別篩（`FlowCubicalTypeSieve`，定義於 `src/cubical_hott.h`）：

- **Kan Open-Box 同倫條件**：給定意圖遮罩 $M_{\text{intent}}$、候選元件遮罩 $M_{\text{cand}}$ 與邊界不變量濾波器 $B$，相容性檢查簡化為單一時脈週期的位元運算：
  $$((M_{\text{intent}} \oplus M_{\text{cand}}) \land B) = 0$$
- **1-Bit 混沌突變與 BMF 畫布（`src/bitspace.c`, `src/search.c`）**：在合法多面體子空間中執行單一位元翻轉（`one chaotic 1-bit mutation`），完全消除無效組合爆炸。
- **Presburger 仿射與二次 Hessian 多面體排程（`src/polyhedral.c`）**：透過 Farkas 引理與二次體積回收求得最佳 Tile 尺寸 $T^*$ 與向量化寬度 $V^*$。
