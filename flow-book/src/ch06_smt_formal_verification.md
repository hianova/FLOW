# 第六章：形式化最高法院 (SMT 4 大定理 QF_LIA UNSAT 證明與 Curry-Howard 死碼消除)

> 「啟發式探索可以天馬行空，但發射出的每一行機器碼必須擁有無可爭辯的數學證明。SMT 定理證明器是 FLOW 宇宙的最高法院，凡無證明者，一律否決。」

---

## 6.1 四大定理 QF_LIA 形式化證明體系

FLOW 內建基於 QF_LIA（量詞自由線性整數算術）的形式化證明器（`src/smt.c`），在發射代碼前強制證明四大不變量定理：

1. **緩衝區安全邊界定理 (Buffer Bounds Safety)**：
   $$\forall i \in [0, N_{\max}), \quad 0 \le \text{offset}(i) < \text{capacity}$$
   證明拒絕所有緩衝區溢位可能，否定命題 UNSAT。
2. **記憶體配額上限定理 (Memory Quota Bound)**：
   $$\sum_{m \in \text{modules}} \text{alloc}(m) \le \text{Quota}_{\text{limit}}$$
   嚴格杜絕 OOM 隱患。
3. **分片非混疊隔離定理 (Shard Non-Aliasing Isolation)**：
   $$\forall s_1 \ne s_2, \quad \text{Range}(s_1) \cap \text{Range}(s_2) = \emptyset$$
   數學保證並發分片零數據競態。
4. **確定性狀態不變量定理 (Determinism Invariant)**：
   相同輸入與隨機種子必然收斂至嚴格相同的相空間軌跡。

---

## 6.2 Curry-Howard 同構與死碼消除 (DCE)

根據 Curry-Howard 同構（命題即型別、證明即程式），一旦 SMT 最高法院在編譯邊界完成前置條件證明，下游呼叫鏈中的所有防禦性檢查：
```c
// 舊有防禦壞味道：
if (buffer == NULL || index >= capacity) { return ERROR; }
```
被直接論證為**不可達死碼（Unreachable Dead Code）**，編譯器毫不留情將其從二進位中徹底拔除，消除管線停頓，達成柯爾莫哥洛夫理論下限。
