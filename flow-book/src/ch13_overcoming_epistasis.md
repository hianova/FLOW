# 第十三章：跨越上位效應壁壘 (Epistasis、萊維飛行與動態機率偏移)

> 「當多個參數之間存在強烈的非線性耦合時，單獨改變任何一個參數只會讓系統變得更糟。FLOW 透過 SMT 基因連鎖圖譜與萊維飛行量子穿隧，徹底擊碎遺傳學中的上位效應壁壘。」

---

## 13.1 什麼是上位效應壁壘 (Epistasis Barrier)？

在生物遺傳學與演化計算中，**上位效應（Epistasis）** 指的是一個基因的表現型效果強烈依賴於另一個基因的存在與否。

在系統架構設計中，上位效應無處不在：

```text
軟體架構中的上位效應能量峽谷 (The Epistatic Canyon):
              狀態 A (低能量/次優)                    狀態 B (最低能量/全局最優)
              [threads = 1, layout = AoS]             [threads = 64, layout = SoA]
              (單執行緒，無鎖爭用，穩態)               (64 執行緒，SIMD 向量化，極致吞吐)
                         \                                 /
                          \                               /
                           \                             /
                            ▼                           ▼
                        中間態 C: [threads = 64, layout = AoS]
                        (高能量死亡峽谷: 64 核心在 AoS 上瘋狂爭用快取行，
                         延遲飆升 20 倍，被退火引擎立即拒絕！)
```

若依賴傳統的逐位元漸進變異（1-Bit Hill Climbing）：
1. 從狀態 A 出發，嘗試翻轉 `threads` 位元至 64 $\to$ 到達中間態 C。
2. 評估中間態 C $\to$ 能量暴增（效能崩潰）。
3. 退火引擎判定方向錯誤，立即回滾至狀態 A。
4. **系統被永久困在次優解 A 中，永遠無法到達全局最優解 B！**

---

## 13.2 SMT 驅動的基因連鎖群 (`FlowGeneLinkageMap`)

FLOW 解決上位效應的第一個殺手鐧是**「SMT 形式化基因連鎖群（Gene Linkage Groups）」**（定義於 `src/bitspace.h` 與 `src/bitspace.c`）：

```c
typedef struct {
    uint32_t bit_indices[FLOW_MAX_LINKED_BITS]; /* 連鎖位元索引陣列 (例如 {14, 18, 22}) */
    size_t bit_count;                           /* 連鎖位元數 */
    char rationale[64];                         /* 連鎖依據: "threads_shards_layout_synergy" */
} FlowGeneLinkageGroup;

typedef struct {
    FlowGeneLinkageGroup groups[FLOW_MAX_LINKAGE_GROUPS];
    size_t group_count;
} FlowGeneLinkageMap;
```

### 超級位元協同原子翻轉 (Super-Bit Coordinated Mutation)

當 SMT 形式化分析器在約束體系中檢測到非線性耦合時，自動將相關位元綁定為連鎖群。變異引擎在翻轉主位元時，**以原子步長同時翻轉所有連鎖位元**：

```c
/* src/bitspace.c */
void flow_genome_mutate_with_linkage(
    FlowGenome *g,
    const FlowGeneLinkageMap *linkage,
    uint64_t *rng_state,
    uint32_t *primary_bit_out,
    size_t *linked_flips_out
) {
    /* 1. 隨機選定主變異位元 */
    uint32_t primary_bit = flow_random_bit(rng_state, g->total_bits);
    flow_genome_flip_bit(g, primary_bit);

    /* 2. 查詢該位元是否屬於某個 SMT 連鎖群 */
    for (size_t i = 0; i < linkage->group_count; i++) {
        const FlowGeneLinkageGroup *grp = &linkage->groups[i];
        if (group_contains_bit(grp, primary_bit)) {
            /* 3. 同步翻轉群組內的所有連鎖位元，一次性跨越能量峽谷！ */
            for (size_t b = 0; b < grp->bit_count; b++) {
                if (grp->bit_indices[b] != primary_bit) {
                    flow_genome_flip_bit(g, grp->bit_indices[b]);
                }
            }
            if (linked_flips_out) *linked_flips_out = grp->bit_count;
            return;
        }
    }
}
```

這使得系統能以單一步長直接從狀態 A 躍遷至狀態 B，直接跨越中間的能量死谷！

---

## 13.3 萊維飛行 (Lévy Flights) 與重尾量子穿隧

對於未被 SMT 靜態連鎖涵蓋的複雜高階流形，FLOW 引入了**萊維飛行重尾分布（Lévy Flight Heavy-Tailed Distribution）**：

$$P(\text{Step Size } s) \sim |s|^{-\alpha}, \quad 1 < \alpha \le 3$$

```text
萊維飛行的超立方體軌跡:
[密集局部 1-Bit 退火] ──► (微調局部參數)
         │
         └──► 【長程量子穿隧 (Long Jump: 8 ~ 16 Bits 同時突變)】 ──► [探索全新幾何區域]
                                                                        │
                                                                        ▼
                                                             [再次進行局部 1-Bit 退火]
```

大部分步長為極微小的 1-Bit 擾動以精細收斂，極少數步長為跨越多個象限的超長跳躍（Macro-tunneling），徹底根絕了演化演算法在複雜崎嶇適應度地形上的早熟收斂難題。
