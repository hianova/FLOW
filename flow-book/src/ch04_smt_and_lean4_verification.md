# 第四章：形式化最高法院 — SMT 四大定理與 Lean 4 自舉證明

在任何機器碼或跨語言標頭發出之前，FLOW 的形式化驗證層（`src/smt.c`, `src/abi.c`, `src/security.c`）強制執行兩道數學守門：

1. **SMT QF_BV / QF_LIA 四大安全定理**：
   - `theorem1_buffer_bounds_safety`：輸入上限嚴格小於或等於容器容量（$\text{inputMaxCount} \le \text{Capacity}$）。
   - `theorem2_memory_quota_bound`：總配置位元組不逾越硬性配額（$\text{AllocatedBytes} \le \text{MemoryLimit}$）。
   - `theorem3_shard_non_aliasing`：並發分片索引彼此正交無競爭。
   - `theorem4_functional_determinism`：相同輸入必產生唯一決定論輸出。
2. **Lean 4 自舉反射證書（`--target-lean`）**：
   - 自動輸出包含 10 項定理（`by decide` / `by rfl`）的 `.lean` 證書，內建非空前提見證（`non_vacuous_precondition_witness`）、雙軌規格/執行同構證明（`dual_track_exec_mirror_isomorphic`），以及零拷貝 `@&` 借用 FFI 與量綱新類型（`flow_pct_vol_t`, `flow_point_vol_t`）。
   - 內建稽核器（`flow_abi_audit_lean_certificate`）嚴格拒絕任何 `sorry`、`admit` 或自訂 `axiom`。
