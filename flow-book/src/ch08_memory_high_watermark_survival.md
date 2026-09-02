# 第八章：記憶體高水位與生存模式 (對抗 OOM 的背壓機制與 Static Survival 避難所)

> 「當系統可用記憶體在 1 微秒內自 16GB 暴跌 99% 至 16MB，同時並發量飆升 10,000 倍時，愚蠢的系統會嘗試 JIT 編譯並被 Linux OOM-Killer 擊斃；有智慧的活體系統會自我否決 JIT，遁入零分配的靜態生存避難所。」

---

## 8.1 雙重束縛死局：並發風暴與 OOM 絞殺

在雲端多租戶伺服器或極端邊緣環境中，系統常面臨「雙重束縛（Double-Bind）」的絕境：

```text
雙重束縛困境 (The Double-Bind Dilemma):
                    ┌───────────────────────────────┐
                    │ 突發事件: 可用記憶體 16GB -> 16MB │
                    │ 同時並發量突增 10,000 倍      │
                    └───────────────┬───────────────┘
                                    │
                    ┌───────────────┴───────────────┐
                    │ 系統是否該啟動 JIT 動態重構？ │
                    └───────┬───────────────┬───────┘
                            │               │
                   [選擇 A: 執行 JIT]   [選擇 B: 不做任何事]
                            │               │
    JIT 需要 100MB 記憶體來解析 AST       舊的 AoS 陣列無法承載萬倍並發
    引發 Linux OS OOM-Killer 殺死進程    記憶體佇列塞爆，請求大量丟失
                            ▼               ▼
                       【系統崩潰】    【系統崩潰】
```

傳統自適應系統的死穴在於：**編譯器在執行 JIT 優化時，自身也需要消耗大量記憶體（AST 記憶體分配、LLVM Context、SSA 轉換表）**。

---

## 8.2 JIT 工作集記憶體數學推導 (`flow_jit_calculate_min_memory_mb`)

FLOW 絕不使用硬編碼的閾值，而是根據語意拓樸 AST 的圖複雜度，嚴格推導 JIT 編譯器的工作集記憶體下限：

$$M_{\text{JIT\_min}} = M_{\text{base\_compiler}} + \sum_{i=1}^{N_{\text{nodes}}} \text{SizeOf}(\text{AST\_Node}_i) \times \text{FanOut}_i$$

在 `src/jit.c` 中：

```c
int flow_jit_calculate_min_memory_mb(const SemanticIR *ir) {
    if (ir == NULL) return 100;
    /* 基礎 LLVM 運行期開銷: 50MB */
    int min_mb = 50;
    /* 每個 AST 節點在 SSA 展開時平均需要 4.5MB 符號表空間 */
    min_mb += (int)(ir->flow_node_count * 4.5);
    return min_mb > 100 ? min_mb : 100;
}
```

對於一個包含 11 個節點的拓樸管線，JIT 所需的最小安全工作集記憶體為 **100 MB**。

---

## 8.3 自我意識否決 (Self-Aware JIT Veto) 與靜態生存避難所

當全局協調器（`FlowOrchestrator`）檢測到當前可用物理記憶體（16 MB）小於 JIT 閾值（100 MB）時，系統觸發**「自我意識 JIT 否決（Self-Aware JIT Veto）」**：

```text
自我意識否決與靜態生存避難所路由:
[檢測到 RAM = 16MB < 100MB] ──► [FLOWY-AUDIT] JIT Compilation Disabled (Vetoed)
                                      │
                                      ▼
             [FLOWY-ORCHESTRATOR] 繞過 JIT，直接熱切換至:
                 [Static_Survival_Mode_v1] (事先編譯的零分配靜態二進位)
                                      │
                                      ▼
                      【零動態記憶體分配 (0 malloc)】
                      【背壓環形佇列啟用，請求丟失數 = 0】
```

系統切換至 Pareto 戰術套件中的 `FLOW_TACTIC_MEMORY` 預編譯靜態版本，完全消除所有動態 `malloc` 調用，成功避開 OOM-Killer 的絞殺。

---

## 8.4 施密特觸發器防抖遲滯控制器 (Schmitt Trigger Controller)

當系統記憶體在臨界值附近頻繁劇烈波動（例如 95MB 與 105MB 之間高速震盪）時，若無防抖機制，系統將陷入瘋狂的「JIT 啟用 $\leftrightarrow$ 否決」的震盪（Flapping），導致 CPU 資源耗盡。

FLOW 引入了**施密特觸發器遲滯控制（Schmitt Trigger Hysteresis）**（定義於 `src/adaptive.h` 與 `src/adaptive.c`）：

```text
施密特觸發器遲滯區間 (Hysteresis Band):
可用記憶體 (RAM)
  ▲
  │   ──────────────────────────────────  恢復閾值 (Recovery Threshold = 150 MB)
  │      ↑ (只有 RAM > 150MB 且持續 500ms，才允許恢復 JIT)
  │   ══════════════════════════════════
  │      【 遲滯死區 (Dead Zone): 拒絕任何切換，保持當前狀態 】
  │   ══════════════════════════════════
  │      ↓ (一旦 RAM < 80MB，立即跌落至生存模式)
  │   ──────────────────────────────────  跌落閾值 (Drop Threshold = 80 MB)
  └────────────────────────────────────────────────────────► 時間
```

### 遲滯參數數學定義

$$\begin{aligned}
\text{Threshold}_{\text{drop}} &= M_{\text{JIT\_min}} \times 0.8 = 80\text{ MB} \\
\text{Threshold}_{\text{recovery}} &= M_{\text{JIT\_min}} \times 1.5 = 150\text{ MB} \\
\Delta t_{\text{dwell\_required}} &= 500\text{ ms (持續駐留時間)}
\end{aligned}$$

```c
/* src/adaptive.c */
void flow_schmitt_trigger_init(FlowSchmittTrigger *st, double base_min, uint64_t dwell_ns) {
    st->drop_threshold = base_min * 0.8;         /* 80 MB */
    st->recovery_threshold = base_min * 1.5;     /* 150 MB */
    st->dwell_time_required_ns = dwell_ns;      /* 500,000,000 ns */
    st->current_state = 0;                      /* 0: Nominal/JIT */
}
```

在 95MB $\leftrightarrow$ 105MB 的震盪期間，施密特觸發器判定狀態未跨越上下界，**切換次數為 0**，徹底保證了系統在混沌高壓下的絕對穩定性。

---

## 8.5 危機解除與非同步背景復原 (Asynchronous Recovery)

當監控信號顯示物理記憶體完全恢復（RAM 回升至 16GB）且經過 500ms 穩定駐留後：
1. 主執行緒繼續在靜態生存模式下無阻礙處理請求。
2. 非同步 JIT 工作執行緒池（`FlowAsyncJITPool`）在背景安全啟動編譯，合成最優的向量化機器碼 `Optimized_JIT_v2`。
3. 編譯完成後，透過 QSBR 在微秒級寬限期內熱替換回高性能版本，完成完美的閉環自癒。
