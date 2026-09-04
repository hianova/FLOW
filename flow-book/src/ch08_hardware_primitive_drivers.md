# 第八章：硬體原語驅動 (奧坎剃刀下的 3-Function 極簡 ABI 與具身物理閘門)

> 「Plugin 不該是沉重的編譯器外掛，而只是大腦接在物理世界的視神經與肌肉。奧坎剃刀切除了一切非必要的 24 個回呼實體，只留下極簡的 3 個硬體原語驅動介面。」

---

## 8.1 戰略降維：Plugin 退化為「感官與手腳」

在 FLOW 早期的演進過程中，我們曾誤以為外掛（Plugin）應該承載業務邏輯、自訂搜尋演算法、甚至編譯器的 AST 發射邏輯，導致了可怕的 **24-Callback 回呼地獄**。

在覺醒後的三層心智模型中，邊界迎來了徹底的極簡：
*   **大腦 (The Brain)**：1-Bit 混沌退火 + SMT 最高法院。所有的拓樸排列、記憶體佈局與優化全部交給大腦。
*   **長期記憶 (Long-Term Memory)**：`.fvec` 權重庫負責記錄 100 萬次在線實證的架構肌肉記憶。
*   **感官與手腳 (Sensory Organs & Muscles)**：`FlowPrimitiveDriver`。**只要不涉及全新實體硬體與 OS Syscall，絕不增設任何外掛！**

---

## 8.2 極簡 3-Function Primitive Driver ABI (`src/primitive.h`)

新世代的硬體原語驅動只需實現極簡的 **3 個介面**：

```c
typedef struct FlowPrimitiveDriver {
    char driver_name[64];
    char driver_version[32];

    /* 1. register_primitive: 告訴大腦「我提供了一種新的硬體能力，例如 io_uring」 */
    int (*register_primitive)(void);

    /* 2. get_hardware_bounds: 告訴 SMT「這個硬體的物理極限在哪裡，例如 Queue Depth <= 4096」 */
    int (*get_hardware_bounds)(FlowHardwareBounds *bounds_out);

    /* 3. execute_primitive: 實際呼叫底層 Syscall 或硬體指令發送數據 */
    int (*execute_primitive)(const void *req, void *resp);
} FlowPrimitiveDriver;
```

---

## 8.3 具身智能與物理閘門 (Embodied Physical Gates, `src/embodied.c`)

當軟體進入機器人與物理實體世界時，演算法缺陷會造成真實的物理損毀。FLOW 在硬體驅動層建立了雙速率機制與物理閘門：
1. **雙速率分離 (Dual-Rate Frequency Separation)**：
   *   **1kHz 脊髓反射 (Spinal Tick)**：純同步、零記憶體分配、零系統調用，負責每毫秒計算牛頓-歐拉動力學與防摔保護。
   *   **1Hz 大腦皮層重構 (Cortical Reconfiguration)**：非同步運行 1-Bit 混沌退火與 SMT 形式化驗證。
2. **零力矩點穩定閘門 (ZMP Stability Gate)**：
   即時計算質心（Center of Mass, CoM）軌跡。若足部零力矩點偏離支撐多邊形邊界，微物理模擬器在 **2.5 微秒** 內觸發硬遮罩，強制將 62% 負載轉移至對側關節，保證實體機器人絕不傾倒。
3. **史密斯預測器 (Smith Predictor Phase Lag Compensation)**：
   針對致動器 3ms 的硬體死區時間（Dead Time）進行相位前饋補償，消除高頻震盪。

---

## 8.4 協同協議原語化：HTTP/1.1、HTTP/2 與 HTTP/3 QUIC (Protocol-as-Primitive)

傳統網路中介軟體（如 Nginx、Envoy）將 HTTP 視為龐大複雜的應用層外掛，導致了冗餘的記憶體拷貝與微秒級延遲。
FLOW 在奧坎剃刀下將網路協議徹底**原語化（Protocol-as-Primitive）**：

*   **HTTP 即分幀原語 (Framing Primitives)**：
    `http1_stream`、`http2_frame` 與 `quic_datagram` 直接實現極簡的 3-Function ABI，分幀解析延遲 $< 100\text{ 奈秒}$。
*   **SMT 形式化免疫走私與洪泛 (Anti-Smuggling & Anti-DDoS)**：
    SMT 最高法院透過 QF_LIA 定理形式化證明：
    *   **流並發上限定理**：候選流數 $\le$ 物理隊列上限（HTTP/1=1, HTTP/2=128, HTTP/3=512），徹底杜絕 Stream Flood DoS。
    *   **標頭表動態配額定理**：HPACK/QPACK 表大小 $\le 65536\text{ Bytes}$，在數學上粉碎 HPACK Bomb 記憶體耗盡攻擊。
*   **64-Bit 座標空間與 1-Bit 混沌在線自適應變形**：
    系統將協議選擇與流配置編碼進 64-bit 基因子空間。當面對突發 10 萬 QPS 或行動網路 5% 丟包時，1-Bit 混沌退火在線動態翻轉，實現 **HTTP/1.1 $\leftrightarrow$ HTTP/2 $\leftrightarrow$ HTTP/3 QUIC** 的毫秒級自愈式幾何形變！

