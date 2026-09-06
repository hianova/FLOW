# 第十一章：JIT 代碼發射與記憶體幾何變形 (AoS 到 SoA 即時重映射與生存模式)

> 「程式碼不是雕刻在石頭上的死文字，而是能在記憶體中靈活轉置的數據結構。當記憶體壓力逼近臨界時，系統在微秒內完成拓樸幾何變形，安全退守靜態避難所。」

---

## 11.1 AoS 與 SoA 的動態幾何變形

在高並發情境中，物件陣列（Array of Structures, AoS）利於單一實體完整存取，但在向量計算中難以利用 SIMD；結構陣列（Structure of Arrays, SoA）則將同欄位聚合，最大化快取行利用率與向量化吞吐。

FLOW 在 `src/jit.c`（`flow_jit_migrate_state_layout`）中實現了**微秒級狀態佈局轉置器**：
$$\text{AoS} \longleftrightarrow \text{SoA}$$
* **結構轉置**：在 C17 中以跨欄位步長迴圈完成轉置，單字節轉置耗時僅 ~0.20ns；
* **列式零拷貝引用分享**：對於未修改的欄位（Columnar Partial Transformation），直接指針互換實現 0 拷貝遷移；
* **平台可移植性**：針對 macOS/BSD 等不支援 Linux 特有 `mremap` 的平台，系統透過標準指針交換與內聯轉置保證全平台一致運行。

---

## 11.2 自適應 JIT 否決與 Static Survival 避難所

JIT 編譯器本身在發射代碼時需要消耗工作記憶體。當系統可用記憶體暴跌至臨界水位以下時，盲目編譯易觸發作業系統 OOM Killer。FLOW 引入守衛機制：
$$\text{Available RAM} < \text{JIT Threshold} \implies \text{JIT Veto}$$
系統自動否決 JIT，將請求指針無鎖引流至零動態配置的 `Static_Survival_Mode` 靜態生存避難所，實現 0 崩潰、0 請求丟失。
