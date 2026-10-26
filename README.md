<p align="center">
  <img src="assets/banner.svg" alt="FLOW Banner" width="100%" />
</p>

# FLOW: 64-Bit Discrete Cubical HoTT Engine

> **64-Bit Discrete Cubical Homotopy Type Theory (HoTT) Engine, De Morgan Parallel Interval Algebra & Intensional Univalence Universe $U_0$**

FLOW 是一個專注於**離散同倫類型論（Discrete Cubical Homotopy Type Theory）**與**單價型別宇宙（Univalence Universe $U_0$）**的極簡高效率 C17 形式化驗證引擎。

專案徹底揚棄傳統冗贅的配置 DSL 與過度包裝的學術空殼，直接將同倫幾何約束、Kan 復形開盒條件與單價公理（$A \simeq B \implies A =_{U_0} B$）映射至現代 64 位元暫存器與純粹 C17 資料流中。

---

## 🏛️ 核心架構：離散同倫類型論 (Cubical HoTT Core)

FLOW 的理論核心完全建立在離散立方體集（Discrete Cubical Sets）與格羅滕迪克拓撲（Grothendieck Topos）之上，提供 100% 形式化自洽的六大同倫演算支柱：

```text
┌────────────────────────────────────────────────────────────────────────────────────────┐
│             FLOW: 64-Bit Discrete Cubical HoTT Mathematical Foundations                 │
├────────────────────────────────────────────────────────────────────────────────────────┤
│                                                                                        │
│  1️⃣ De Morgan 抽象區間與並行位元代數 (Interval I = {0, 1})                             │
│     • 交集 (Meet, AND)、聯集 (Join, OR)、以及對偶反演 (Involution, NOT ~)              │
│     • 單 CPU 週期內完成 64 維空間的 De Morgan 對偶恆等式驗證：                         │
│       ~(a ∧ b) == (~a ∨ ~b)                                                            │
│                                                                                        │
│  2️⃣ 立方體集幾何運算子 (Cubical Face & Degeneracy Operators)                           │
│     • 面投影運算子 (Face Projections): ∂_i^0 與 ∂_i^1                                  │
│     • 退化運算子 (Degeneracy Operators): ε_i 插入中性態                                │
│     • 嚴格保證立方體面運算子交換律：∂_i^e ∂_j^d == ∂_{j-1}^d ∂_i^e (i < j)            │
│                                                                                        │
│  3️⃣ Kan 條件與開盒填塞 (Kan Open-Box Filling & Homotopy Equivalence)                   │
│     • 邊界篩網濾波與差分匹配：Mismatch = (Mask_p ^ Mask_q) & BoundaryFilter == 0      │
│     • 零不匹配判定為同倫等價 (Kan Filled Homotopic)，非零即判定為拓撲奇點阻礙           │
│                                                                                        │
│  4️⃣ 格羅滕迪克拓撲子物件分類子 (Topos Subobject Classifier Ω = 2)                      │
│     • 映射同構：Hom(X, Ω) ≃ Sub(X)，子物件特徵標籤 χ ∈ {0, 1}                          │
│     • 嚴格判定子物件在環境篩網（Ambient Sieve）中的包含封閉性                          │
│                                                                                        │
│  5️⃣ 64 維超立方體微架構語義篩網 (I^64 Microarchitecture Semantic Sieve)               │
│     • 將記憶體、並發、排程、資料佈局與安全策略正交投影至 64 維幾何軸                   │
│     • 提供拓撲狀態躍遷解碼器 (Transition Decoder) 與 1-Cell 同倫路徑導向               │
│                                                                                        │
│  6️⃣ 內涵全域型別宇宙 U_0 與單價性原理 (Univalence Principle: A ≃ B ⟹ A =_U_0 B)       │
│     • 註冊跨語言型別（Lean 4 標量規範 ⟷ C17 ABI）                                      │
│     • 當欄位數量、位元長度、對齊邊界與篩網指紋同構時，宇宙自動證明兩型別等價           │
│     • 生成單價性同倫路徑雜湊（Path Hash），驗證型別相等性                              │
│                                                                                        │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 🛠️ 輔助系統工程基石

除了純粹的同倫代數，FLOW 保留了經過嚴格工程驗證的輕量 C17 執行期輔助模組：
- **2-Jet Forward AD 自動微分** (`flow_jet.h`)：二階 Taylor 展開微分鏈鎖律（Value, 1st Delta, 2nd Gamma）。
- **QSBR 無鎖世代熱重載** (`reload.h`)：基於靜止狀態（Quiescent State）的世代動態切換與無鎖 Epoch 推進。
- **無鎖快取對齊 SPSC 環形緩衝區** (`flow_spsc.h`)：純 C17 Header-only 單生產者單消費者佇列。

---

## 💻 快速開始 (Quick Start)

### 1. 編譯專案核心與執行 Cubical HoTT 全套測試

```sh
# 編譯 libflow.a, flowc 編譯器與 flowy 內省工具
make -j4 all

# 執行全套領域測試與模糊測試 (515 項斷言 + 1,000 次模糊測試 100% 通過)
make test
```

### 2. 檢驗型別宇宙與單價性證明 (`flowy`)

```sh
# 檢視 64-Bit De Morgan 篩網 (I^64) 與內涵型別宇宙 U_0 單價性驗證狀態
./build/flowy topos

# 執行形式化一致性審查
./build/flowy audit
```

### 3. 將 Lean 4 規格合成為純淨 C17 標頭檔 (`flowc`)

```sh
# 編譯純量 Lean 4 規範至 C17 實作與 static inline 標頭檔
./build/flowc spec.lean -o build/spec.c --target-c-header build/spec.h

# 生成附帶 MATLAB MEX 分派器的 C17 接口
./build/flowc spec.lean -o build/spec_mex.c --target-c-header build/spec.h --target-mex
```

---

## 🗂️ 專案目錄結構 (Repository Layout)

```text
FLOW/
├── src/
│   ├── cubical_hott.{h,c}             # 64-Bit 離散 Cubical HoTT 核心、Kan 填塞器與單價宇宙 U_0
│   ├── flow.h / parser.c              # 標量 Lean 4 解析器與語義 IR 降階
│   ├── backend.{h,c}                  # 純 C17 算子與雙軌 (double / Q32.32 fx64) 代碼生成
│   ├── abi.{h,c} / registry.{h,c}     # C17 標頭檔與 MEX 分派器合成、型別指紋登錄
│   ├── flow_spsc.h                    # 快取行對齊的純 C17 無鎖 SPSC 環形緩衝區
│   ├── flow_jet.h                     # 2-Jet Forward AD 二階自動微分算子
│   ├── reload.{h,c}                   # 無鎖 QSBR 世代動態熱重載
│   └── flowy.{h,c} / flowc.c / flowy_main.c # CLI 編譯器與同倫宇宙內省工具
├── tests/
│   ├── test_cubical_hott.c            # Cubical HoTT 71 項同倫、Kan 與單價性核心測試
│   └── hott_test_kit.h                # 輕量零依賴同倫測試套件
└── Makefile                           # 純淨 C17 建置管道
```

---

## 📜 授權條款 (License)

MIT License — 詳見 [LICENSE](LICENSE)。
