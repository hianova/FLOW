# FLOW — HoTT × 2-Jet Mathematical-to-Code Grounding Interface

> **透過 Cubical HoTT（離散同倫型別／Grothendieck Topos 篩／Univalence 零拷貝同構）與 2-Jet（連續微分流形／二階切空間與曲率叢），創造將「純數學規範無損落地為機器執行碼」的標準介面。**

FLOW 不是通用語言執行期或容器搜尋器，而是連結 **純形式化數學規範（Lean 4）**、**連續物理微分流形（2-Jet Bundle $J^2(\mathcal{M}, \mathcal{N})$）** 與 **底層硬體指令集（Zero-GC C17 / LLVM SSA）** 的雙向標準介面。

---

## 1. 核心架構：HoTT × 2-Jet 雙向落地閉環

```
                    ┌────────────────────────────────────────────────────────┐
                    │              Pure Lean 4 Formal Specification          │
                    │   structures, total functions, relational invariants,  │
                    │   theorems: P(x) -> Q(x, f(x)), SpaceWithException     │
                    └───────────────┬────────────────────────▲───────────────┘
                                    │                        │
        [正向合成 Direct Synthesis]  │                        │ [逆向推導 Reverse Lifting]
        flowc spec.lean -o out.c    │                        │ flowc --lift-llvm (<file.ll/.c>)
        --target-c-header out.h     │                        │ Abstract Interval + Known-Bits
                                    ▼                        │
                    ┌────────────────────────────────────────┴───────────────┐
                    │         HoTT × 2-Jet Formal-Physical Bridge            │
                    │  • 2-Jet Bundle J^2(M,N): (v, df, d^2f, L_f V)         │
                    │  • O(1) Projection -> 64-bit Grothendieck Topos Sieve  │
                    │  • 1-Cycle Kan Open-Box Homotopy & Fallback Operator   │
                    │  • Univalence (A ≃ B) ≃ (A = B) Zero-Copy mmap Mirror  │
                    └───────────────────────────┬────────────────────────────┘
                                                │
                                                ▼
                    ┌────────────────────────────────────────────────────────┐
                    │        Embodied Hardware / Zero-GC C17 / MATLAB MEX    │
                    └────────────────────────────────────────────────────────┘
```

### I. 2-Jet 連續微分叢（[`src/flow_jet.h`](src/flow_jet.h) / [`src/flow_jet.c`](src/flow_jet.c)）
實作連續物理流形上的二階前向自動微分代數 $\mathbb{J}_2 = \mathbb{R}[\epsilon]/(\epsilon^3)$：
- **二階展開元 (`FlowJetScalar2`)**：同時攜帶零階狀態值 $f(x)$、一階切空間雅可比導數 $\nabla f(x)$ 與二階海森曲率 $\nabla^2 f(x)$，全程無動態配置、具備完整 IEEE-754 算術封閉性。
- **辛幾何相空間與控制障礙函數（CBF）**：於連續控制迴路中即時計算李導數（Lie Derivative）$\mathcal{L}_f V = \langle \nabla V(x), \dot{x} \rangle$、泊松括號 $\{f, g\}$、Koopman 譜半徑與 Mori-Zwanzig 非馬可夫記憶核，防止離散採樣間隙的包絡線穿透（Inter-sample Tunneling）。

### II. Cubical HoTT 離散同倫核（[`src/cubical_hott.h`](src/cubical_hott.h) / [`src/cubical_hott.c`](src/cubical_hott.c)）
將高階同倫型別論（Homotopy Type Theory）落地為硬體暫存器級別的位元代數：
- **64-bit Grothendieck Topos 篩（Topos Sieve）**：以單一 `uint64_t` 位元遮罩編碼多維前置不變量，透過 `flow_topos_classify_subobject` 在單一 CPU 週期內判定觀測態是否覆蓋定理前件。
- **Kan Open-Box 同倫填補（`flow_kan_check_homotopy`）**：檢驗狀態轉移路徑是否維持同倫等價；若受保護邊界軸破裂（`FLOW_KAN_OBSTRUCTED_SINGULARITY`），立即觸發安全退避。
- **高階歸納型別（HIT）異常坍縮（`SpaceWithException`）**：內部合法狀態滿足可縮空間 $\text{isContr}$（$h$-level $-2$），而違反合約的異常子流形則商化（Quotient）為 0 維邊界奇異點 $\partial\mathcal{M}$，並壓入 IEEE-754 Quiet NaN 51-bit payload（`0x7FF84...`），在硬體流水線中零分支傳播。
- **Univalence 公理零拷貝傳輸（[`src/reload.h`](src/reload.h) / [`src/reload.c`](src/reload.c)）**：利用 POSIX 雙重虛擬記憶體映射（Dual-Mapped `mmap` Ring Buffer）與 QSBR 靜止期回收，將類型同構 $(A \simeq B) \simeq (A = B)$ 落地為零拷貝位址視圖切換。

### III. `HoTT × 2-Jet` 橋接標準介面（[`src/flow_bridge.h`](src/flow_bridge.h) / [`src/flow_bridge.c`](src/flow_bridge.c)）
`FlowPhysicalBridge` 與 `flow_bridge_execute_hott_jet_step` 將連續微分流形與離散同倫篩合而為一：
1. **2-Jet 篩投射（`flow_bridge_project_jet2_sieve`）**：將感測或算子輸出的 2-Jet 樣本 $(v, d_1, d_2)$ 投射至 64-bit Topos Sieve：
   - Bit 0 (`FLOW_JET_SIEVE_FINITE`)：$v, d_1, d_2 \in \mathbb{R}$（無 NaN / Inf）
   - Bit 1 (`FLOW_JET_SIEVE_ZERO_ORDER_BOX`)：零階狀態包絡線 $v \in [v_{\min}, v_{\max}]$
   - Bit 2 (`FLOW_JET_SIEVE_FIRST_ORDER_LIPSCHITZ`)：一階切空間 Lipschitz 界限 $|d_1| \le L_{\max}$
   - Bit 3 (`FLOW_JET_SIEVE_SECOND_ORDER_CURVATURE`)：二階海森曲率界限 $|d_2| \le \kappa_{\max}$
   - Bit 4 (`FLOW_JET_SIEVE_LYAPUNOV_DISSIPATIVE`)：Lyapunov 指數耗散條件 $v \cdot d_1 \le -\gamma v^2$
   - Bit 5 (`FLOW_JET_SIEVE_TRANSVERSALITY`)：Thom 橫截性條件（迫近邊界 $\partial\mathcal{M}$ 時 $|d_1| \ge \epsilon_{\text{trans}}$）
2. **形式化守衛與同倫保底**：依序執行感測器量化（`quantize_observation`）、前置條件守衛（`check_precondition`）、Topos 篩與 Kan 同倫檢驗、主算子執行與硬體週期監控、後置與關係不變量守衛（`check_postcondition` / `check_relational_invariant`）；任何一步破裂均自動切換至同倫安全算子（`fallback_operator`）並輸出精確的 `FlowBoundaryViolationKind` 診斷標籤。

### IV. 正向編譯與 LLVM 逆向邊界推導（[`src/flowc.c`](src/flowc.c) / [`src/llvm_lifter.c`](src/llvm_lifter.c)）
- **正向直接映射**：純 Lean 4 規範（支援歐幾里得整數除法 `flow_int_div` / `flow_int_mod`、飽和自然數減法 `flow_nat_sub`、結構體不變量與定理蘊含守衛）直接編譯為獨立 Zero-GC C17 原始碼、單檔 `static inline` 標頭、Apple Silicon Metal Shading Language (`.metal`) 算子或 MATLAB MEX C 算子。
- **LLVM SSA 逆向推導**：當工程算子包含底層 C/LLVM 實作時，`flowc --lift-llvm` 直接解析 LLVM IR SSA 指令集（`add`, `sub`, `mul`, `sdiv`, `udiv`, `shl`, `lshr`, `ashr`, `and`, `or`, `xor`, `icmp`, `select`, `phi` 等），執行前向抽象區間分析（Interval Analysis）與已知位元傳播（Known-Bits Propagation），逆向合成 Lean 4 `BitVec` 邊界證明檔案（`theorem <fn>_in_bounds`）。

### V. GPU-Native BMF Meshlet 與子位元幾何管線（[`src/flow_gpu.h`](src/flow_gpu.h) / [`src/flow_gpu.c`](src/flow_gpu.c)）
將資產封裝（Prefab）直接收斂為 GPU 原生位元流形（Bit-Manifold Meshlet），徹底消除 OOP 場景樹指標追蹤（Pointer Chasing）與 DRAM 頻寬瓶頸：
- **無指標平鋪階層（`FlowBmfFlatNode`）**：以單一 64-byte 快取對齊連續陣列與拓撲序索引 `parent_idx < self_idx` 取代樹狀指標，單趟循序掃描完成全域座標變換。
- **1.58-bit 三態流形（`FlowBmfTernary64`）與 2-bit 區塊內插（`FlowBmfBlock2Bit`）**：以雙 64-bit 位元平面（`nonzero_mask`, `sign_mask`）編碼 $\{-1, 0, +1\}^{64}$，透過硬體 `POPCNT` 單週期完成 64 維內積；並提供與 GPU 硬體紋理單元（BC1/DXT1）同構之 2-bit 查找表內插。
- **10-bit 頂點量化、6-bit 微索引與全域晶格無裂縫保證（`FlowBmfMeshlet`）**：每個簇群（$\le 64$ 頂點、$\le 124$ 三角形）將頂點壓入單一 `uint32_t`（10-bit XYZ + 2-bit 拓撲縫合標籤），三角形壓入 18-bit 微索引（3 個三角形封裝進單一 64-bit 暫存器之 54 bits）；邊界頂點鎖定至全域整數晶格（Global Snap Lattice），由 Lean 4 形式化證明跨簇群邊界零裂縫（No T-Junctions）。
- **2-Jet 接觸幾何錐體剔除與 64-Lane Subgroup Ballot 串流壓縮**：由二階射叢 $J^2(\mathbb{R}^2, \mathbb{R}^3)$ 導出曲率補償 AABB 與法向量錐體（`cone_axis`, `cone_cutoff`），並將 64 個簇群之可見性瞬間折疊進單一 `uint64_t` Ballot 遮罩（對應 Metal `simd_ballot` / CUDA `__ballot_sync`），透過硬體 `POPCNT` 與 `CTZ` 在 1 個週期內完成無分支串流壓縮。

---

## 2. 模組架構一覽

| 檔案 | 職責說明 |
| :--- | :--- |
| [`src/flow_bridge.h`](src/flow_bridge.h), [`src/flow_bridge.c`](src/flow_bridge.c) | **HoTT × 2-Jet 核心橋接介面**：2-Jet 流形包絡線投射、Kan 同倫檢驗、量化／反量化與單週期保底算子切換 |
| [`src/flow_gpu.h`](src/flow_gpu.h), [`src/flow_gpu.c`](src/flow_gpu.c) | **GPU-Native BMF Meshlet 幾何管線**：無指標平鋪階層、1.58-bit/2-bit 子位元編碼、10-bit/6-bit 微簇群、2-Jet 錐體剔除與 64-lane Ballot 壓縮 |
| [`src/cubical_hott.h`](src/cubical_hott.h), [`src/cubical_hott.c`](src/cubical_hott.c) | **Cubical HoTT 離散同倫核**：64-bit Grothendieck Topos 篩、Kan Open-Box、HIT 異常商空間與多面體前拉/推前 |
| [`src/flow_jet.h`](src/flow_jet.h), [`src/flow_jet.c`](src/flow_jet.c) | **2-Jet 連續微分幾何引擎**：二階前向自動微分 $\mathbb{J}_2$、辛幾何泊松括號、Koopman 譜與 Mori-Zwanzig 記憶核 |
| [`src/llvm_lifter.h`](src/llvm_lifter.h), [`src/llvm_lifter.c`](src/llvm_lifter.c) | **LLVM SSA 逆向推導器**：從 LLVM IR / C 算子逆向推導抽象數值邊界並生成 Lean 4 `BitVec` 邊界證明義務 |
| [`src/parser.c`](src/parser.c), [`src/backend.c`](src/backend.c), [`src/abi.c`](src/abi.c) | **Pure Lean 4 $\to$ Zero-GC C17 / Metal 編譯器**：語法解析、定理前件／後件提取、C17 / Header / Metal MSL / MEX 生成與憑證審計 |
| [`src/bitspace.h`](src/bitspace.h), [`src/bitspace.c`](src/bitspace.c) | **精確數值與位元流形代數**：Lean 4 歐幾里得整數算術（`flow_int_div`/`mod`）、`FlowU256` 蒙哥馬利模冪與 Miller-Rabin 質數檢定 |
| [`src/reload.h`](src/reload.h), [`src/reload.c`](src/reload.c), [`src/flow_spsc.h`](src/flow_spsc.h) | **Univalence 零拷貝與併發底層**：雙重映射 `mmap` 環形緩衝區、QSBR 無鎖熱替換與 Lamport SPSC 佇列 |
| [`src/flowc.c`](src/flowc.c) | **唯一編譯器與逆向推導 CLI (`flowc`)**：支援正向 `.lean -> .c/.h/.metal` 與逆向 `--lift-llvm` |
| [`plugins/embodied.lean`](plugins/embodied.lean), [`plugins/cryptanalysis.lean`](plugins/cryptanalysis.lean), [`plugins/gpu_meshlet.lean`](plugins/gpu_meshlet.lean) | **純 Lean 4 參考規範範例**：1kHz ZMP 具身關節動力學、位元流形密碼分析與 GPU-Native BMF Meshlet 無裂縫幾何規範 |
| [`tests/test_hott_jet.c`](tests/test_hott_jet.c), [`tests/test_compiler_lifter.c`](tests/test_compiler_lifter.c), [`tests/fuzz-test.c`](tests/fuzz-test.c) | **毫秒級全記憶體核心測試套件**：涵蓋 HoTT × 2-Jet 代數核心、GPU BMF Meshlet、Lean 4 $\leftrightarrow$ C17/LLVM 雙向編譯與語法模糊測試 |

---

## 3. 快速建置與命令列用法 (`flowc`)

### 建置編譯器與執行期靜態庫
```bash
make clean && make
```
產出目標：
- `build/flowc`：Pure Lean 4 $\to$ C17 / Metal MSL 編譯器與 LLVM SSA $\to$ Lean 4 逆向推導 CLI
- `build/libflow.a`：完整編譯器與數學執行期靜態庫
- `build/libflow_rt.a`：可獨立連結之極輕量執行期庫（含 2-Jet、Cubical HoTT、HoTT × Jet Bridge、GPU BMF Meshlet、QSBR、精確數值）

### `flowc` 核心用法
```bash
# 1. 將純 Lean 4 規範編譯為獨立 Zero-GC C17、單檔 static inline 標頭與 Apple Silicon Metal Compute Kernel
./build/flowc plugins/gpu_meshlet.lean -o build/gpu_meshlet_gen.c --target-c-header build/gpu_meshlet_gen.h --target-metal build/gpu_meshlet_gen.metal

# 2. 產生 MATLAB C-MEX 介面或 Lean 4 自包含證明憑證
./build/flowc plugins/embodied.lean -o build/embodied_mex.c --target-mex
./build/flowc plugins/gpu_meshlet.lean -o build/gpu_meshlet_gen.c --target-lean build/gpu_meshlet_cert.lean

# 3. 從 LLVM IR (.ll) 或 C17 算子逆向推導抽象數值邊界並生成 Lean 4 證明義務
./build/flowc --lift-llvm operator.ll -o boundary_proof.lean
./build/flowc --lift-llvm operator.c -o boundary_proof.lean
```

### 執行毫秒級全記憶體測試套件
```bash
make test
```
包含 3 個零磁碟 I/O 的全記憶體測試執行檔與 CLI 冒煙測試（總耗時 `< 0.5s`）：
- `build/test-hott-jet`：2-Jet 前向自動微分、辛幾何流形、Cubical HoTT Topos 篩、Univalence $\mathcal{U}_0$ 同構、HoTT × 2-Jet 橋接、QSBR 熱替換與 GPU-Native BMF Meshlet 管線（311 項斷言）
- `build/test-compiler-lifter`：Lean 4 歐幾里得整數與 `FlowU256` 蒙哥馬利模冪、Pure Lean 4 $\to$ Zero-GC C17 / Metal MSL 正向合成、LLVM SSA $\to$ Lean 4 逆向區間推導（274 項斷言）
- `build/fuzz-test`：語法邊界語料庫與 1,000 次偽隨機 BMF 變異記憶體安全測試
