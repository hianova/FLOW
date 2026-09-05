# 第十章：硬體原語驅動 (SocketCAN/CAN-FD 3-函數極簡 ABI 與微秒級 SMT 搶佔仲裁)

> 「驅動不該是沉重的外掛，而只是大腦接在物理世界的視神經與肌肉。奧坎剃刀切除了一切非必要實體，只留下極簡的 3 函數 ABI。」

---

## 10.1 奧坎剃刀下的 3-Function 極簡 ABI

FLOW 淘汰了傳統驅動框架中臃腫的 24 個回呼介面，提煉出最極簡的 3-函數原語驅動 ABI：
1. **`flow_driver_init`**：向核心註冊硬體原語與能力。
2. **`flow_driver_declare_invariants`**：呈報 SMT 物理邊界與硬體驗證合約。
3. **`flow_driver_dispatch_primitive`**：執行原子調度與資料流傳輸。

---

## 10.2 SocketCAN / CAN-FD 微秒級搶佔

原生驅動直接對接 Linux SocketCAN 與 CAN-FD 匯流排，SMT 優先權仲裁定理數學嚴格證明：
$$T_{\text{halt}} \le 300\ \mu\text{s}$$
在馬達過載或緊急中斷時，急停訊框以最高優先權插隊發射，永不受常規遙測阻塞。
