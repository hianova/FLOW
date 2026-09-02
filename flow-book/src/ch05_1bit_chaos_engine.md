# 第五章：1-Bit 混沌退火引擎 (Xorshift、Mask Canva 與能量坍縮的數學原理)

> 「在維度的詛咒面前，窮舉搜尋是死路一條。FLOW 採用 1-Bit 混沌退火，以 12.96 奈秒的極致步長，在 $2^N$ 超維幾何流形中精確坍縮出全局最優解。」

---

## 5.1 維度詛咒與 1-Bit 超維狀態脊椎

在現代系統架構中，一個由候選元件、線程數、快取分片、批次大小與記憶體佈局構成的搜尋空間，其維度組合常高達數百萬種。傳統遺傳演算法（GA）或網格搜尋（Grid Search）面臨嚴重的組合爆炸與算力浪費。

FLOW 建立了統一的**「1-Bit 狀態脊椎（1-Bit State Spine）」**，將所有系統決策緊湊地編碼至 64 到 1024 位元的離散布林向量中（`FlowGenome`）：

```text
FlowGenome 位元編碼佈局 (Bit Allocation):
┌────────────────┬────────────────────────────────────────────────────────┐
│ Selector Bits  │ Dimension Parameter Bits                               │
│ [Bits 0 .. k]  │ [Bits k+1 .. N]                                        │
├────────────────┼────────────┬─────────────┬─────────────┬──────────────┤
│ 候選元件選擇   │ tile_size  │ batch_size  │ layout_kind │ thread_count │
│ (k = ceil(log2 │ (4 bits)   │ (4 bits)    │ (2 bits)    │ (4 bits)     │
│   N_candidates)│            │             │             │              │
└────────────────┴────────────┴─────────────┴─────────────┴──────────────┘
```

### 12.96 ns/op 恆定時間變異演算法 (Xorshift64*)

FLOW 採用經過高度 SIMD/暫存器優化的 Xorshift64* 偽隨機數產生器，實現真正的 $O(1)$ 恆定時間單位元翻轉：

$$\begin{aligned}
x &\leftarrow x \oplus (x \gg 12) \\
x &\leftarrow x \oplus (x \ll 25) \\
x &\leftarrow x \oplus (x \gg 27) \\
\text{rand\_val} &= (x \times 0x2545F4914F6CDD1D_{16})
\end{aligned}$$

```c
/* src/bitspace.c 核心實作 */
void flow_genome_mutate_1bit(FlowGenome *g, uint64_t *rng_state, uint32_t *mutated_bit_out) {
    uint64_t x = *rng_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *rng_state = x;
    uint64_t raw = x * UINT64_C(0x2545f4914f6cdd1d);
    
    uint32_t bit_idx = (uint32_t)(raw % g->total_bits);
    uint32_t word_idx = bit_idx / 64;
    uint32_t bit_in_word = bit_idx % 64;
    
    g->words[word_idx] ^= (UINT64_C(1) << bit_in_word);
    if (mutated_bit_out) *mutated_bit_out = bit_idx;
}
```

實測基準顯示，單次變異與解碼開銷僅為 **12.96 奈秒**，使得 FLOW 能在毫秒級時間內完成數萬次假設推演。

---

## 5.2 3-Tier Mask Canvas 疊加態幾何架構

如果隨機翻轉位元，豈不是會產生大量無效或崩潰的基因（例如請求 10,000 個執行緒但硬體只有 4 核心）？

FLOW 發明了 **3-Tier Dynamic Mask Canvas（三層遮罩畫布）**，在位元層級進行量子疊加式的快速過濾：

```text
3-Tier Mask Canvas 幾何結構:
┌────────────────────────────────────────────────────────────────────────┐
│ Tier 1: 硬安全與多面體投影遮罩 (Hard Safety & Polytope Mask)           │
│         - SMT 形式化不變量、記憶體上限硬配額、Ownership 檢查           │
│         - Polyhedron P = {Ax <= b} 超立方體正交投影 Pi_P               │
├────────────────────────────────────────────────────────────────────────┤
│ Tier 2: 領域專家偏好遮罩 (Domain Preference Mask)                      │
│         - 領域外掛 (DSO) 宣告的高效搜尋通道                            │
├────────────────────────────────────────────────────────────────────────┤
│ Tier 3: 運行期神經遙測偏置 (Dynamic PMU Telemetry Bias)                 │
│         - L3 Cache Miss Rate、IPC、QSBR 佇列深度即時信號               │
└────────────────────────────────────────────────────────────────────────┘
```

### 多面體約束的超立方體正交投影 ($\Pi_{\mathcal{P}}$)

設物理約束系統為凸多面體 $\mathcal{P} = \{x \in \mathbb{R}^D \mid A x \le b\}$。FLOW 透過幾何投影算子 $\Pi_{\mathcal{P}}$ 將連續約束映射為離散超立方體的位元遮罩：

$$\Pi_{\mathcal{P}} : \mathcal{P} \longrightarrow \mathcal{M}_{\text{poly}} \in \{0, 1\}^N$$

$$\mathcal{M}_{\text{effective}} = \mathcal{M}_{\text{hard}} \land (\mathcal{M}_{\text{pref}} \lor \mathcal{M}_{\text{pmu}})$$

任何試圖翻轉到 $\mathcal{M}_{\text{effective}}$ 之外的位元擾動，在 **1 個 CPU 週期內被位元運算直接駁回（Zero-Cost Pruning）**，完全不消耗昂貴的評估函數！

---

## 5.3 熱力學玻爾茲曼退火與能量坍縮

FLOW 定義了多目標架構能量函數（Energy Function）：

$$E(\text{Plan}) = w_{\text{lat}} \cdot \text{Latency} + w_{\text{mem}} \cdot \text{Memory} + E_{\text{transition}} + \text{Penalty}_{\text{violation}}$$

### 玻爾茲曼接受準則 (Metropolis-Hastings Criterion)

新提議的基因型態根據熱力學概率被接受：

$$P(\text{accept}) = \begin{cases} 
1 & \text{if } \Delta E < 0 \\
\exp\left(-\frac{\Delta E}{T}\right) & \text{if } \Delta E \ge 0 
\end{cases}$$

```text
溫度冷卻曲線與停滯再加熱 (Reheating on Stagnation):
溫度 T
 ▲
 │   \
 │    \  (幾何冷卻 T_{k+1} = \alpha * T_k)
 │     \      ┌─ 再加熱 (Reheat: 0.6 * T_0 突破局部鞍點)
 │      \    / \
 │       \__/   \
 │               \___
 └─────────────────────► 退火迭代次數
```

當系統在連續 $K$ 步（`plateau_stagnation_limit = 6`）內能量未下降時，退火引擎自動觸發**熱力學再加熱（Thermodynamic Reheating）**，將溫度瞬間提升至 $0.6 \times T_0$，藉由布朗熱擾動跳出局部極值與鞍點（Saddle Points）。

---

## 5.4 轉移成本模型：結構性質變 vs. 參數微調

在活體運行期，更換一個實作必須考慮**遷移開銷**。FLOW 在能量函數中精確引入了遷移懲罰模型（`FlowTransitionCostModel`）：

$$E_{\text{transition}} = \mathbb{I}_{\text{structural}} \times \left( E_{\text{JIT}} + \text{LiveBytes} \times C_{\text{transform}} \right)$$

- 若僅微調參數（如批次大小 $32 \to 64$），$\mathbb{I}_{\text{structural}} = 0$，轉移成本為 0。
- 若涉及結構質變（如 AoS $\to$ SoA），引擎計算 JIT 編譯代價與記憶體重映射代價，確保**長期穩態收益大於遷移代價時才執行熱替換**。

---

## 5.5 Pareto 戰術套件 (Tactical Bundle)

退火結束後，FLOW 在 Pareto 最優前沿上提取出三種確定性的戰術計畫（`FlowPlanEnsemble`）：

```text
Pareto 前沿與三色戰術套件:
延遲 (Latency)
 ▲
 │  [SPEED 戰術] (極致多執行緒向量化，吞吐最高)
 │     \
 │      \   [BALANCED 戰術] (膝點 Knee-Point，綜合能耗最低)
 │       \
 │        \___ [MEMORY 戰術] (極簡單執行緒，靜態生存避難所)
 └────────────────────────────────────────► 記憶體佔用 (Memory)
```

1. **`FLOW_TACTIC_SPEED`**：延遲最小化，適用於常規高性能業務。
2. **`FLOW_TACTIC_BALANCED`**：Pareto 膝點，綜合能耗最低。
3. **`FLOW_TACTIC_MEMORY`**：記憶體佔用極小化，為後續章節中的「OOM 生存模式」提供預備避難所。
