<p align="center">
  <img src="assets/banner.svg" alt="FLOW Banner" width="100%" />
</p>

# FLOW

> **Living Topology Orchestrator & Autonomous Continuous Evolution Engine**

FLOW compiles declarative intents (`.flow`) into zero-overhead native code. Operating on a **Pure Tripartite Core (Brain, Body, Soul)**, FLOW replaces handwritten heuristics and combinatorial optimization with **1-Bit Chaotic Annealing**, **SMT Epistatic Gene Linkage**, **Universal Architectural Lockfiles (`.fvec`)**, **Minimalist 3-Function Hardware Primitive Drivers**, and **Unified QSBR Live Hot Reload**.

```text
┌────────────────────────────────────────────────────────────────────────┐
│                        FLOW 終極純粹三大主軸架構                       │
├────────────────────────────────────────────────────────────────────────┤
│                                                                        │
│   🧠【大腦核心 (The Brain)】                                          │
│      • search.c   : 馬可夫 1-Bit 混沌退火 + 量子機率偏移 (突破 Epistasis)│
│      • smt.c      : 形式化驗證最高法院 (統整硬體邊界、合約 Mask、4 大定理)│
│      • topology.c : 拓樸流形與依賴圖譜                                 │
│      • bitspace.c : 多面體硬限制、連鎖群與 1-Bit 編碼空間              │
│      • parser.c   : 語法分析與語意降維 (包含 lower_to_ir)              │
│                                                                        │
│   🦾【物理肉體 (The Body)】                                           │
│      • jit.c      : 即時組合語言與 C 源碼發射器                        │
│      • reload.c   : 零原子鎖 QSBR 熱替換與世代追蹤                     │
│      • primitive.c: 極簡 3-Function 實體硬體驅動 (io_uring/RDMA/eBPF)  │
│      • adaptive.c : 硬體 PMU 遙測與控制器                              │
│      • backend.c  : 多後端發射 (LLVM/MLIR/C/Rust/Python)               │
│                                                                        │
│   🧬【靈魂記憶 (The Soul)】                                           │
│      • flowy_fvec.c: 大一統 .fvec 載體、海馬迴流形與 Hub 共享中心     │
│      • flowy.c     : 內省解釋、神經元對話與模組知識圖譜                │
│                                                                        │
│   ⚙️【表現層與基礎設施】                                              │
│      • audit.c    : 全域決策審計與遙測事件日誌設施                     │
│      • flowy_cli.c: 前端 CLI、終端排版渲染與 REPL 互動式 Prompt        │
│                                                                        │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 🌟 核心子系統與架構保證 (Key Subsystems)

### 1. 1-Bit 混沌退火引擎 (`FlowBitSpace`)
- **超輕量 $O(1)$ 突變**：以 $12.96\text{ ns/op}$ 的純暫存器位元翻轉探索高維帕累托前緣，消滅傳統遺傳演算法（GA）中破壞拓樸結構的交叉破壞（Crossover）。
- **SMT 基因連鎖群 (Epistatic Linkage Groups)**：形式化求解非線性上位效應壁壘，自動將關聯基因綁定為超級位元進行原子翻轉。

### 2. 形式化驗證最高法院 (`FlowSMT`)
- **零缺陷形式化數學保證**：所有提議的候選實作均在發射前證明 4 大核心定理否定命題為 **UNSAT**：
  1. **緩衝區邊界安全** (Buffer Bounds Safety)
  2. **記憶體配額有界性** (Memory Quota Bound)
  3. **分片無別名隔離** (Shard Non-Aliasing & Isolation)
  4. **函數確定性不變量** (Functional Determinism Invariant)
- **1-Cycle 多面體位元修剪**：透過硬限制遮罩（Hard Constraint Mask）在 1 個時鐘週期內修剪掉 99.9% 的非法幾何狀態。

### 3. 大一統通用鎖定檔 (`.fvec`) 與零秒冷啟動
- **Universal Architectural Lockfile**：徹底取代傳統單一二進位資料庫與參數鎖，採用 1024-Byte 自描述明文 ASCII 表頭 + CRC32 二進位 Payload。
- **1ms 硬體親和度前檢 (Hardware Affinity Gate)**：自帶 SMT 簽章與硬體拓樸特徵（AVX2、L1 快取大小），跨架構不符（如 x86 AVX2 搬至 ARM Cortex-M）時在 1 毫秒內安全拒絕，保障可重現構建極限。
- **GitOps 目錄特徵庫 (`.flow/vecs/`)**：架構模型享受標準 Git 版本控制（`git diff`, `git revert`），支援抗體昇華與赫布強化。

### 4. 極簡 3-Function 硬體原語驅動 ABI (`src/primitive.h`)
- **戰略降維**：Plugin 降格為純粹的感官與手腳（Hardware Drivers），切除非必要的 24 個編譯器回呼實體。
- **極簡三介面**：
  1. `register_primitive()`: 向大腦宣告硬體原語能力（如 `io_uring`, `RDMA`, `eBPF Maps`）。
  2. `get_hardware_bounds()`: 向 SMT 法院呈報物理極限邊界。
  3. `execute_primitive()`: 執行零拷貝硬體調度。

### 5. 零原子鎖 QSBR 熱替換 (`FlowReload`)
- **超低延遲微秒級遷移**：讀取路徑零原子寫、零快取彈跳，多核心讀取吞吐量高達 $> 390\text{M ops/s}$。
- **守護監控隔離**：內建 Epoch Watchdog 與記憶體分頁隔離機制（`mprotect`），徹底杜絕慢讀取執行緒引發的記憶體洩漏。

### 6. 確定性因果推論大腦 (`Flowy`)
- **純粹智慧**：徹底剝離 UI 渲染、終端排版與測試治具，以純粹資料結構運算提供 **0% 幻覺** 的代碼庫圖譜檢索、即時決策因果解釋（`flowy why`）與神經遙測熱點分析（`flowy bottleneck`）。

---

## 🚀 範例：意圖規格 `project.flow`

```flow
project browser_runtime

input task_stream {
    max_count 10000
}

flow browser_pipeline {
    task_stream -> transform -> collect
}

import builtin

require {
    deterministic
    memory < 64mb
}

prefer {
    latency
}
```

---

## 💻 命令列快速入門 (CLI Usage)

### 1. 前台極速編譯器 (`flowc`)

```sh
# 1. 前台 O(1) 零秒冷啟動：直接套用通用 .fvec 鎖定檔 (37 微秒完成發射)
flowc examples/rank.flow -o generated/rank.c --apply-fvec .flow/vecs/hft_ultra_low_latency.fvec

# 2. 生成帶有 SMT 簽章與硬體親和度檢查的 Universal Lockfile
flowc examples/project.flow -o generated/project.c --lock .flow/vecs/project_prod.fvec

# 3. 夜間尋優 / 背景守護模式 (1-Bit 混沌退火)
flowc examples/rank.flow -o generated/rank.c --search --iterations 250 --seed 42

# 4. 多後端發射 (C, Rust, Python, MLIR, LLVM IR)
flowc examples/rank.flow -o generated/rank.c \
    --target-c-header generated/rank.h \
    --target-rust generated/rank.rs \
    --target-python generated/rank.py \
    --target-mlir generated/rank.mlir \
    --target-llvm-ir generated/rank.ll
```

### 2. 獨立架構助理與推論大腦 (`flowy`)

```sh
# 1. 代碼庫決定論語意問答 (支援繁中/英文，零幻覺)
flowy ask "QSBR 的無鎖寬限期是如何運作的？"

# 2. 即時因果決策解釋 (解釋為何觸發拓樸幾何變形)
flowy why

# 3. 系統下意識神經遙測熱點分析
flowy bottleneck

# 4. 決策時間線與審計日誌
flowy timeline

# 5. 《The FLOW Book》活體電子書終端閱讀器
flowy book all
flowy book 12

# 6. 互動式 REPL 助手
flowy shell
```

---

## 🧪 建置與 100% 形式化驗證 (Build & Test)

```sh
# 編譯純粹三主軸核心庫、flowc、flowy 與動態外掛
make all

# 執行全套 60 項生產快照重放與形式化數學測試套件 (全綠燈，耗時 ~25 秒)
make test

# 同步中英雙語《The FLOW Book》知識圖譜靜態綁定
make sync-book

# 系統安裝
sudo make install
```

---

## 📜 授權條款 (License)

MIT License - 詳見 [LICENSE](LICENSE) 文件。
