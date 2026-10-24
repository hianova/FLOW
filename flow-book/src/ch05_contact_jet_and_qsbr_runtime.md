# 第五章：熱力學接觸噴流束 (.fjet)、架構記憶 (.fvec) 與 QSBR 熱替換

> **給開發者的白話翻譯**：線上高併發系統最怕兩件事——「改一個風控參數就要重啟服務（或被讀寫鎖卡死）」以及「長時間高負載導致 CPU/GPU 過熱降頻、延遲暴衝」。FLOW 在執行期內建了 **QSBR 無鎖熱替換**、**`.fvec` 預載快取**與 **`.fjet` 防降頻配速器**，完全不需要使用者手寫任何執行緒鎖。

---

## 5.1 波前耦合 QSBR 無鎖熱替換、形式化狀態遷移與硬實時防線 (`src/reload.c`, `src/backend.c`)

- **什麼是 QSBR（Quiescent-State-Based Reclamation）？**
  當管理員或 CFO 在後台動態調整風控參數、或是熱載入新編譯的算子時，讀取端執行緒**不需要拿任何 Mutex 或 RW-Lock**。
- **64B 快取行對齊與世代租約（Epoch Lease）**：
  每次呼叫 `process_descriptor` 或 `flow_qsbr_call` 時，系統僅以單條原子指令推進 `qsbr_epoch`（耗時 `< 100ns`），待舊世代所有讀取請求自然結束後才安全回收舊狀態，徹底杜絕 Use-After-Free 與鎖競爭。
- **形式化驗證之跨世代狀態遷移（Verified State Migration `StateV1 -> StateV2`）**：
  在 `@reload` 中宣告 `from_schema StateV1`、`to_schema StateV2` 與 `migrate migrateV1ToV2`，並在 `.lean` 中證明守恆定理（如 `theorem migrate_conserves_accum (s : StateV1) : (migrateV1ToV2 s).accum = s.accum := by rfl`）。`flowc` 會在編譯期窮舉驗證該守恆性質，並自動合成 `flow_<id>_migrate_buffer` 與 `flow_<id>_migrate_unit` 供 `FlowReloadContext` 或 Python `engine.migrate_state(v1_bytes)` 執行無損跨結構轉換。
- **預先觸發鎖定記憶體池（Pre-Faulted `mlock` Arena）與 `-4` WCET 緊急停機閂鎖**：
  當 `@quota` 宣告 `arena <bytes>` 與 `fallback emergency_stop` 時，`flow_<id>_create()` 會自動配置 64B 對齊之靜態區域、逐頁寫入觸發實體分頁（Page Pre-Faulting）並呼叫 `mlock` 鎖定實體記憶體以消除實時迴圈中的 Page Fault 抖動；若執行期單步耗時超過 SLA `deadline`，硬體看門狗會立即將原子閂鎖 `watchdog_tripped` 設為 `1`，後續所有 `process` / `process_descriptor` / `run_patchbay` 呼叫皆以零副作用立即回傳 `-4`（`FLOW_ERR_WCET_EMERGENCY_STOP`）。

---

## 5.2 大一統 `.fvec` 架構記憶庫 (`src/fvec/fvec_core.c`)

- **零冷啟動預載（Pre-Staging Vault）**：
  已通過 SMT 驗證的 64-bit 架構基因與參數組態，可序列化為帶有 1024-byte 標頭與 CRC32 校驗的 `.fvec` 檔案。
- **群體免疫廣播（9-Byte Antibody Frame）**：
  當某個節點偵測到異常負載並切換至更安全的架構基因時，僅需廣播 9 bytes 的精簡抗體封包，叢集內其他節點即可在微秒內同步切換。

---

## 5.3 熱力學接觸噴流束 `.fjet` (`src/flow_jet.c`)

- 在機器人關節控制（1kHz SLA）或高頻串流場景中，`flow_jet.c` 在相空間 $(q, p, \ddot{q})$ 上追蹤熱耗散作用量 $s$，當預測到硬體即將觸發溫度降頻（Thermal Throttling）時，自動平滑調整步進配速以維持尾延遲（P99 Latency）穩定。
