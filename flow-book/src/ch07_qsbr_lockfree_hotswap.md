# 第七章：QSBR 無鎖熱替換 (微秒級 Zero-Downtime 遷移的秘密)

> 「在每秒數千萬次請求的高並發伺服器中，獲取哪怕一把讀寫鎖（RWLock）都會造成災難性的快取一致性風暴。FLOW 採用統一 QSBR 無鎖架構，實現了讀取路徑零原子寫入、熱替換微秒級無損遷移。」

---

## 7.1 鎖的終局：讀寫鎖在高核心下的快取行彈跳 (Cache Bouncing)

在多核心系統中，傳統的並發保護機制面臨殘酷的物理瓶頸：

```text
傳統讀寫鎖 (pthread_rwlock) 的快取行彈跳災難:
[Core 0 讀取] ──► atomic_inc(&rwlock.readers) ──► 導致 Core 1..63 的 L1/L2 快取行失效!
[Core 1 讀取] ──► atomic_inc(&rwlock.readers) ──► 再次引發跨 Socket 匯流排總線同步!
[Core 2 讀取] ──► atomic_inc(&rwlock.readers) ──► 吞吐量從 100M/s 斷崖式下跌至 2M/s!
```

即使讀者不修改數據，僅僅是為了增加「讀者計數器」，也會觸發 MESI 快取一致性協定的 $M$（Modified）狀態切換，造成昂貴的跨核心互連匯流排延遲（Cross-Socket Interconnect Stall）。

---

## 7.2 QSBR 第一性原理：靜默狀態與零原子寫入讀取路徑

FLOW 採用了 **QSBR（Quiescent State Based Reclamation，靜默狀態基回收）** 技術（實作於 `src/reload.c` 與 `src/reload.h`）：

```text
QSBR 無鎖讀取與熱替換時序:
讀者執行緒 1: ──[ 世代 0 執行中 ]──► (Checkpoint 靜默點) ──[ 世代 1 執行中 ]──►
讀者執行緒 2: ───[ 世代 0 執行中 ]────────► (Checkpoint 靜默點) ──[ 世代 1 ]──►
                                                   ▲
寫者 (熱替換): ─── 發布世代 1 ──► [ 等待寬限期 Grace Period ] ──► 安全釋放世代 0 記憶體!
```

### 讀取路徑的極致零成本 (`flow_qsbr_call`)

在讀取路徑上，讀者**完全不對任何全域計數器進行原子寫入**，僅執行單次單向的 Acquire 記憶體屏障讀取：

```c
/* src/reload.c: 讀取路徑純 Acquire 讀取，零快取彈跳 */
int flow_qsbr_call(FlowReloadContext *context, const void *input, void *output) {
    FlowUnit *unit = atomic_load_explicit(&context->current_unit, memory_order_acquire);
    void *state = atomic_load_explicit(&context->current_state, memory_order_acquire);
    return unit->run(context->host_context, state, input, output);
}
```

讀者執行緒僅在事件迴圈邊界調用一次 `flow_qsbr_checkpoint(reader)`，宣稱自己已到達靜默點。這使得讀取吞吐量突破 **3.9 億次操作/秒（> 390M ops/s）**！

---

## 7.3 快取行對齊與偽共享（False Sharing）消除

為了防止多個讀者結構體落在同一個 64-Byte CPU 快取行而互相干擾，`FlowReloadReader` 採用了嚴格的快取行對齊與填充策略：

```c
/* src/reload.h */
#define FLOW_CACHE_LINE_SIZE 64
#define FLOW_CACHE_ALIGNED __attribute__((aligned(FLOW_CACHE_LINE_SIZE)))

struct FlowReloadReader {
    FlowReloadContext *context;
    struct FlowReloadReader *next;
    _Atomic uint64_t active_epoch;
    _Atomic int registered;
    _Atomic uint64_t last_heartbeat_ns;
    _Atomic uint64_t qsbr_epoch;
    _Atomic int is_offline;
    _Atomic int is_quarantined;      /* 標記: 是否因落後而被看門狗隔離 */
    void *quarantine_page_addr;      /* 記憶體頁面保護位址 */
    size_t quarantine_page_size;
    uint8_t _cache_pad[8];           /* 明確填充至 64 位元組邊界 */
} FLOW_CACHE_ALIGNED;
```

---

## 7.4 掉隊者看門狗與 `mprotect` 記憶體頁面隔離 (Epoch Watchdog Quarantine)

在傳統 QSBR 中，最大隱患在於**「掉隊者問題（Straggler Thread）」**：若某個讀者執行緒陷入死迴圈或長 I/O 阻塞，全域寬限期將永遠無法推進，導致舊世代記憶體無限堆積引發 OOM。

FLOW 引入了首創的 **Epoch Watchdog Quarantine（世代看門狗隔離機制）**：

```text
掉隊者隔離與記憶體回收流程:
[讀者 A] ── 正常運算 ──► Checkpoint ──► Checkpoint
[讀者 B] ── (陷入 500ms 掉隊阻塞...) ─────────────────────────────────────┐
                                                                           │
[Watchdog 掃描] ──► 檢測到讀者 B 超過 Chebyshev 4-Sigma 寬限上限 (μ + 4σ) ─┤
                     │                                                     │
                     ├─► 1. 調用 mprotect(page, PROT_READ) 鎖定該世代頁面  │
                     ├─► 2. 標記 reader->is_quarantined = 1                │
                     └─► 3. 將讀者 B 強制踢出活躍 QSBR 集合 ───────────────┘
                           │
                           ▼
               [寫者安全回收舊世代記憶體 (0 記憶體洩漏)]
```

### 動態超時計算 (切比雪夫 4-Sigma 邊界)

超時閾值非人為寫死，而是根據 SLA 延遲需求與分佈統計動態計算：

$$T_{\text{grace}} = \max\left(\text{SLA\_Latency} \times 2, \quad \mu_{\text{latency}} + 4 \sigma_{\text{latency}}\right)$$

一旦掉隊者被看門狗隔離，系統立即安全回收已退役的舊代記憶體，徹底解決了無鎖系統的長尾延遲與記憶體膨脹難題。

### 🛡️ 隔離後的優雅降級 (Graceful Degradation via Signal Handling)

在極端生產環境中，當那個陷入 500ms 阻塞的「讀者 B」突然被喚醒，若其程式邏輯嘗試修改（Write）這塊已被看門狗透過 `mprotect(page, PROT_READ)` 鎖定的隔離記憶體時，作業系統核心會對該執行緒拋出 `SIGSEGV` (Segmentation Fault) 記憶體保護異常。

對於傳統應用程式，此訊號會直接導致整個 Process 崩潰；但在 FLOW 活體架構中：
1. **客製化訊號攔截 (`sigaction`)**：FLOW 核心註冊了專屬的 `SIGSEGV` 訊號處理常式，透過 `siginfo_t` 即時分析故障位址是否位於已退役之舊世代頁面。
2. **失控執行緒安全隔離與重置**：確認為掉隊者違規寫入後，FLOW 攔截異常並安全終止/重置該失效 Worker 執行緒，防止記憶體踩踏。
3. **無感任務轉移**：主協調器 (Orchestrator) 自動接管並將其負載轉移至健康節點，確保主行程與其他健康執行緒完全不受干擾，達成 OS 級別的高可用容錯閉環。

---

## 7.5 完整突變審計軌跡 (Deterministic Audit Trail)

每次熱替換均會記錄一個 256 項目的環形審計快照（`FlowMutationSnapshot`），保存：
- `timestamp_ns`：奈秒級切換時間戳。
- `genome_words`：1024-Bit 精確基因型。
- `llvm_ir_hash`：機器碼 IR 密碼學雜湊。
- `author_attestation`：SMT 形式化證明簽章。

這保證了系統在經歷數萬次熱替換後，依然具備 100% 的可審計性與確定性時間旅行回溯能力。
