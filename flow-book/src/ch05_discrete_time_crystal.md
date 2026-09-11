# 第五章：Floquet 離散時間晶體動力學 (DTC 次諧波 2T 鎖定、極限環相角儲存與 CPG 節律)

> 「工程的嚴謹在於承認物理邊界。FLOW 不宣稱在經典單核 CPU 上運行量子多體時間晶體，而是借鑒 Floquet 時間平移對稱破缺理論，在相空間構建高抗噪的非線性極限環動力學原型。」

---

## 5.1 離散時間平移對稱破缺 (DTSB) 與極限環振子

在 `src/flow_time_crystal.c` 中，FLOW 基於 16~64 維噴流束相空間（Jet Bundle）構建了 Floquet 週期驅動的**非線性受迫振子系統**：

系統以週期 $T$ 接受相空間翻轉脈衝（Kick）：
$$H(t + T) = H(t)$$
在線性系統中，響應必然隨驅動週期 $T$ 耗散或共振；而在 FLOW 引入的 Duffing 非線性受迫振子與近 $180^\circ$（$0.95\pi$）相位旋轉下，系統自發打破離散時間平移對稱性，響應週期穩定鎖定為次諧波：
$$\tau_{\text{DTC}} = 2T$$

---

## 5.2 實測次諧波剛性與工程應用價值

DTC 演化由兩階段交替構成：
1. **Kick 脈衝旋轉階段**：相空間旋轉角 $\theta = \pi (1 - \epsilon)$，其中 $\epsilon$ 為允許的旋轉缺陷。
2. **非線性耦合演化階段**：Duffing 立方剛度非線性力 $F_i = \omega_i^2 q_i + L q_i^3$ 與近鄰交互作用。

### 工程真實性與應用場景
* **實測次諧波剛性**：在 `tests/test_f2_hodge.c` 與 `tests/audit-flow-book-all.c` 實測中，次諧波剛性比達 **100.0%**，展現極強的動力學吸引子剛性（Attractor Rigidity）。
* **與 $\mathbb{F}_2$-霍奇投影的「一陰一陽」幾何互補**：
  * **霍奇投影（陰 / 淬火）**：$P_{\text{exact}} = d \Delta^{-1} \delta$，負責在 1 個週期內消除所有不可積渦旋環路（$\delta_2\Psi$ 與 $H_1$ 諧波空洞），將系統硬性拉回滑模切面；
  * **時間晶體（陽 / 發動機）**：Floquet DTSB 週期踢擊，負責維持受拓撲保護的次諧波巡弋動力，驅動系統安全遍歷多面體相空間，永不失速或陷入混沌。

---

## 5.3 三大實體落地能力 (Landing Capabilities)

在 `src/flow_time_crystal.c` 中，FLOW 落地了三大抗噪幾何能力：

### 1. 拓撲免校準時鐘分頻器 (Jitter-Free Subharmonic Pacer)
* **API**：`flow_dtc_pace_subharmonic(dtc, dt_jitter, &pacer_tick)`
* **物理原理**：傳統數位計數器易受晶振抖動（Clock Jitter）影響產生時基漂移；離散時間晶體利用超立方體相空間的集體自旋鎖定，輸入帶有高頻隨機噪聲（如 $\pm 25\%$ 時延擾動）的微擾踢擊序列，能硬性輸出絕對純淨的 $\frac{1}{2}$ 或 $\frac{1}{4}$ 亞諧波節拍。
* **適用場景**：高抖動 CAN/IMU 傳感器時序整形、多核非對稱輪詢定拍。

### 2. 動態極限環手性記憶胞 (Dynamical Limit-Cycle Chirality Storage)
* **API**：`flow_dtc_encode_chirality(dtc, bit)` / `flow_dtc_decode_chirality(dtc)`
* **幾何不變量**：傳統靜態記憶體依賴電位翻轉（容易被熱噪聲或宇宙射線 SEU 翻轉）。FLOW 利用 2D 相平面軌道角動量 $L = \sum_k (q_{2k} p_{2k+1} - q_{2k+1} p_{2k})$ 作為拓撲不變量：
  * **軌道 A（自旋手性逆時針，CCW，$L > 0$）**：表示邏輯 `1`；
  * **軌道 B（自旋手性順時針，CW，$L < 0$）**：表示邏輯 `0`。
* **自愈特性**：即使座標受到瞬間噪聲衝擊，極限環吸引子的勢能面井深會持續將相點拉回穩態軌道，實現零漏電、抗微擾的動力學位元儲存。

### 3. 霍奇-時間晶體陰陽節奏調節閥 (Hodge-DTC Yin-Yang Rhythm Regulator)
* **API**：`flow_dtc_regulate_hodge_paced(dtc, surface_mask, quench_active, &state)`
* **協同閉環**：
  * 當 `quench_active = 1` 時，啟動霍奇瞬時淬火，單週期消除環路；
  * 當 `quench_active = 0` 時，時間晶體以次諧波步頻推動狀態在有界多面體流形內受控巡弋，軌跡受滑模切面硬性約束，徹底杜絕死鎖與混沌發散。

