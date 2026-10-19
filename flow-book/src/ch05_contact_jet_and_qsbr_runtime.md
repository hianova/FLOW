# 第五章：熱力學接觸噴流束 (.fjet)、架構記憶 (.fvec) 與 QSBR 熱替換

FLOW 的執行期基底結合了連續相空間物理與無鎖並發：

- **熱力學接觸噴流束（`src/flow_jet.c`）**：在辛幾何相空間 $(q, p, \ddot{q})$ 上擴充熱力學接觸作用量 $s$，結合 Velocity Verlet 辛積分、Mori-Zwanzig 記憶核與線上串流 EDMD（Koopman 轉移算子 $\text{Tr}(K) < 0$），主動抑制硬體降頻（Anti-Throttling）。
- **大一統 `.fvec` 架構記憶庫（`src/fvec/fvec_core.c`）**：以 1024-byte 標頭與 CRC32 封裝經 SMT 驗證的基因組態，支援 `< 100ns` 零冷啟動預載切換（Pre-Staging Vault）與 9-byte 淋巴抗體廣播。
- **波前耦合 QSBR 無鎖熱替換（`src/reload.c`, `src/token_ring.c`）**：利用 64-byte 快取行對齊（`alignas(64)`）與世代狀態遷移，在零讀端原子寫入開銷下完成微秒級熱更新。
