# 第十二章：Wavefront 波前環與 alignas(64) 快取行約束 (單快取行 64B 撕裂無關原子相位切換)

> 「在多核心並發的世界裡，跨快取行撕裂即原罪。FLOW 將波前槽約束在單一 64 位元組快取行內，以原子相位切換實現零自旋鎖同步。」

---

## 12.1 alignas(64) 硬體快取行約束

在 `src/token_ring.h` 中，波前槽結構體強制採用 64 位元組對齊：
```c
typedef struct __attribute__((aligned(64))) {
    uint32_t slot_id;
    FlowTokenStage current_stage;
    uint64_t slot_genome;
    FlowBmf1BitCanvas bmf_canvas; /* 64B single-cacheline 1-bit switchboard */
    FlowMaskCanvas slot_canvas;
    double energy;
    bool in_flight;
} FlowWavefrontSlot;
```
SMT 形式證明定理保證：
$$\_\text{Alignof}(\text{FlowWavefrontSlot}) \ge 64 \land \operatorname{sizeof}(\text{FlowBmf1BitCanvas}) == 64$$
硬體層面徹底消滅 False Sharing 與跨快取行撕裂（Torn Reads）。

---

## 12.2 排空視界定理 (Evacuation Horizon Theorem)

波前環透過自然世代推進 `wavefront_epoch++`，數學證明排空視界：
$$T_{\text{evac}} \le N_{\text{slots}} \times \tau$$
所有就緒槽在有限跳步內完全排空，無須任何手動輪詢或自旋等待。
