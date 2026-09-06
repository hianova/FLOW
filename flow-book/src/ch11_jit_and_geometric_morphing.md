# 第十一章：JIT 代碼發射與記憶體幾何變形 (AoS 到 SoA 即時重映射與生存模式)

> 「程式碼不是雕刻在石頭上的死文字，而是能在記憶體中靈活轉置的數據結構。當記憶體壓力逼近臨界時，系統在微秒內完成拓樸幾何變形，安全退守靜態避難所。」

---

## 11.1 AoS 與 SoA 的動態幾何變形

在高並發情境中，物件陣列（Array of Structures, AoS）利於單一實體完整存取，但在向量計算中難以利用 SIMD；結構陣列（Structure of Arrays, SoA）則將同欄位聚合，最大化快取行利用率與向量化吞吐。

FLOW 在 `src/jit.c`（`flow_jit_migrate_state_layout`）中實現了**微秒級狀態佈局轉置器**：
$$\text{AoS} \longleftrightarrow \text{SoA}$$
* **結構轉置**：在 C17 中以跨欄位步長迴圈完成轉置，單字節轉置耗時僅 ~0.20ns；
* **列式零拷貝引用分享**：對於未修改的欄位（Columnar Partial Transformation），直接指針互換實現 0 拷貝遷移；
* **跨平台幾何轉置**：在不依賴 Linux 特有 `mremap` 的前提下，系統透過標準指針交換與高速跨步轉置保證全平台（macOS Darwin、Linux POSIX）一致運行。

---

## 11.2 雙重映射 $W \oplus X$ 零 TLB Shootdown 機器碼發射

在作業系統層級，同時具備可寫與可執行權限的記憶體頁面（$W \land X$）是重大安全漏洞。傳統 JIT 引擎在發射代碼時，必須反覆調用 `mprotect()` 進行切換（RW $\to$ RX），這會觸發高開銷的跨核跨 CPU TLB Shootdown IPI 中斷。

FLOW 採用**硬體雙重別名映射（Dual-Mapped Zero-TLB Memory Mirror）**：
* **macOS / Darwin**：透過 Mach 核心 `mach_vm_remap` 將同一塊物理記憶體同時映射為 `write_heap`（`VM_PROT_READ | VM_PROT_WRITE`）與 `exec_heap`（`VM_PROT_READ | VM_PROT_EXECUTE`）；
* **Linux / POSIX**：透過 `memfd_create` 與共享描述符分別映射為 RW 與 RX 別名頁面；
* **原生硬體機器碼發射**：編譯器在 `write_heap` 直接發射 ARM64（`fadd`, `fmul`, `add`, `ret` 等 32-bit opcodes）與 x86-64 原生指令，執行快取同步（`sys_icache_invalidate` / `__builtin___clear_cache`）後直接經由 `exec_heap` 函數指針執行，達成 0ns FFI 負擔與 0 次 TLB Shootdown。

---

## 11.3 自適應 JIT 否決與 Static Survival 避難所

JIT 編譯器本身在發射代碼時需要消耗工作記憶體。當系統可用記憶體暴跌至臨界水位以下時，盲目編譯易觸發作業系統 OOM Killer。FLOW 引入守衛機制：
$$\text{Available RAM} < \text{JIT Threshold} \implies \text{JIT Veto}$$
系統自動否決 JIT，將請求指針無鎖引流至零動態配置的 `Static_Survival_Mode` 靜態生存避難所，實現 0 崩潰、0 請求丟失。
