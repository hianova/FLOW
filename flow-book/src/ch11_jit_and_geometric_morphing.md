# 第十一章：JIT 代碼發射與幾何變形 (AoS 到 SoA 即時重映射與生存模式)

> 「程式碼不是雕刻在石頭上的死文字，而是能在記憶體中自發變形的黏土。當記憶體即將崩潰時，系統在微秒內完成拓樸幾何變形，躲過 OS OOM Killer 的致命屠刀。」

---

## 11.1 AoS 與 SoA 的動態幾何變形

在高並發微服務中，物件陣列（Array of Structures, AoS）利於整筆資料讀取，但在並發快取爭用下引發嚴重的 False Sharing；結構陣列（Structure of Arrays, SoA）則適合 SIMD 向量運算。

FLOW JIT 支援透過 `mremap` 進行零拷貝虛擬記憶體重映射：
$$\text{AoS} \longleftrightarrow \text{SoA}$$
在微秒內完成記憶體拓樸重組，瞬間減少 97% 快取行撕裂。

---

## 11.2 自覺 JIT 否決與 Static Survival 避難所

當系統可用記憶體暴跌至臨界水位以下時，JIT 編譯器若盲目 Fork 編譯進程必將觸發作業系統 OOM Killer。FLOW 內建自我意識守衛：
$$\text{Available RAM} < \text{JIT Threshold} \implies \text{JIT Veto}$$
系統自動否決 JIT，將請求指針無鎖引流至零動態配置的 `Static_Survival_Mode` 靜態生存避難所，實現 0 崩潰、0 請求丟失。
