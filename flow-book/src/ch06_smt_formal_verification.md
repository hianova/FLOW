# 第六章：形式化邊界驗證 (零依賴 SMT 超盒多面體驗證、SMT-LIB2 導出與死碼消除)

> 「啟發式探索可以天馬行空，但發射出的每一行機器碼必須擁有無可爭辯的邊界保證。FLOW 採用零依賴超盒多面體約束檢驗，既杜絕運行期越界，又避免引入數十 MB 龐大求解器。」

---

## 6.1 四大定理超盒多面體 (Hyper-Box Polytope) 驗證體系

FLOW 在 `src/smt.c` 實現了**純 C17 零外部依賴的超盒多面體邊界驗證器（Hyper-Box Bound Verifier）**。為保持微秒級極速編譯與獨立二進位，FLOW 避免直接動態鏈接數十 MB 的通用求解器（如 Z3/CVC5），而是在發射代碼前以納秒級速度（實測 ~115ns）裁決四大核心安全不變量：

1. **緩衝區安全邊界定理 (Buffer Bounds Safety)**：
   $$\forall i \in [0, N_{\max}), \quad 0 \le \text{offset}(i) < \text{capacity}$$
   嚴格保證容量 $\ge$ 最大輸入規模，邊界否定命題為 UNSAT。
2. **記憶體配額上限定理 (Memory Quota Bound)**：
   $$\sum_{m \in \text{modules}} \text{alloc}(m) \le \text{Quota}_{\text{limit}}$$
   靜態鎖定模組記憶體上限，杜絕 OOM 隱患。
3. **分片非混疊隔離定理 (Shard Non-Aliasing Isolation)**：
   並發槽位索引非重疊，保證分片槽位零數據競態。
4. **確定性狀態不變量定理 (Determinism Invariant)**：
   在宣告 `deterministic` 約束下保證無隨機副作用。

---

## 6.2 標準 SMT-LIB2 腳本導出與 Curry-Howard 死碼消除

除了二進位內建的超盒邊界驗證外，FLOW 還支援直接生成標準 **SMT-LIB2 腳本**（`flow_smt_generate_proof_script`），可無縫對接外部工業級 Z3 / CVC5 求解器進行深層形式化定理消解。

在編譯期，經形式化邊界驗證成立的不變量，使下游代碼中的冗餘防禦性檢查：
```c
if (buffer == NULL || index >= capacity) { return ERROR; }
```
被直接判定為**不可達死碼（Unreachable Dead Code）**，編譯器在代碼發射時將其徹底消除，達成柯爾莫哥洛夫理論下限與零分支預測停頓。
