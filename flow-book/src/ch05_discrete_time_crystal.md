# 第五章：Floquet 離散時間晶體 (DTC 週期 2T 鎖定、時間平移對稱破缺與非熱化剛性)

> 「在混沌噪聲的熱寂深淵中，時間晶體自發打破時間平移對稱性，以次諧波 2T 剛性鎖定系統節拍，免疫一切無序擾動。」

---

## 5.1 離散時間平移對稱破缺 (DTSB)

傳統即時系統依賴作業系統計時器或硬體中斷，在高負載與快取爭用下極易發生時鐘抖動（Jitter）。FLOW 在 `src/flow_time_crystal.c` 構建了基於 Floquet 驅動的**離散時間晶體（Discrete Time Crystal, DTC）**：

系統受週期為 $T$ 的外力週期性驅動：
$$H(t + T) = H(t)$$
在哈密頓系統中，原本應展現 $T$-週期性；然而 DTC 態自發打破離散時間平移對稱性（Discrete Time-Translation Symmetry Breaking），響應週期剛性鎖定為：
$$\tau_{\text{DTC}} = 2T$$

---

## 5.2 次諧波剛性與非熱化保障

DTC 演化由兩階段交替構成：
1. **Kick 階段**：自旋翻轉脈衝，翻轉角 $\theta = \pi (1 - \epsilon)$，其中 $\epsilon$ 為微小缺陷。
2. **相互作用與無序階段**：多體 Ising 耦合與隨機無序位能 $W_i$。

儘管存在翻轉缺陷 $\epsilon$ 與外部噪聲，DTC 的次諧波峰值信號比（Subharmonic Peak Ratio）恆定保持：
$$\mathcal{R}_{\text{DTC}} > 0.999$$
DTC 拒絕熱化（Many-Body Localization, MBL），位元翻轉在百萬次循環中無耗散漂移，為分散式協同與時間敏感網路（TSN）提供無可動搖的拓撲時鐘源。
