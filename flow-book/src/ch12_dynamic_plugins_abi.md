# 第十二章：動態外掛與 ABI 契約 (如何撰寫您的第一個 FLOW Plugin，從宣告到發射 LLVM IR)

> 「一個偉大的活體系統必須具備無限擴展的生態。FLOW 定義了極簡的 Standardized Plugin ABI v2，將策略與機制徹底分離，讓任何 C/Rust 模組都能在 4 個函數內無縫融入活體超立方體。」

---

## 12.1 機制與策略分離：為什麼需要標準化 ABI？

在傳統系統中，為編譯器撰寫外掛通常意味著必須深入編譯器內部龐大的 AST 節點與 C++ 類別體系。這導致外掛高度脆弱，編譯器一旦升級版本，所有外掛即刻崩潰。

FLOW 在 `src/plugin.h` 與 `src/abi.h` 中確立了**「機制與策略完全分離」**的原則：
- **FLOW 核心只負責「機制」**：1-Bit 混沌退火、位元遮罩疊加、QSBR 寬限期調度、SMT 定理驗證。
- **領域外掛負責「策略」**：定義自己的維度位元數、能量評估公式與 LLVM IR / C 發射邏輯。

```text
FLOW Plugin ABI v2 互動架構:
┌────────────────────────────────────────────────────────────────────────┐
│                        FLOW 核心 (FlowBitSpace)                        │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ (透過 4 個純 C ABI 函數指標調用)
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│               動態領域外掛 (Dynamic DSO: libflow_*.so)                  │
├────────────────────────────────────────────────────────────────────────┤
│ 1. get_genome_bit_size() ──► 宣告外掛需要多少位元 (例如 16 bits)       │
│ 2. get_valid_mask()      ──► 根據環境狀態回傳有效位元遮罩              │
│ 3. evaluate_energy()     ──► 輸入基因組，輸出架構能量得分 (浮點數)     │
│ 4. emit_llvm_ir()        ──► 將勝出之基因組發射為 LLVM IR / C 程式碼   │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 12.2 標準 4-Function ABI 契約定義 (`src/plugin.h`)

任何動態共享函式庫（`.so` / `.dylib`）只需導出以下 4 個純 C 函數，即可成為一等公民的 FLOW Plugin：

```c
typedef struct {
    /* 1. 回傳該領域在 FlowBitSpace 中所佔用的位元長度 */
    size_t (*get_genome_bit_size)(void);

    /* 2. 根據當前物理環境 (CPU 核心數, 記憶體壓力, 溫度) 產出合法性遮罩 */
    uint64_t (*get_valid_mask)(const FlowEnvironmentState *env);

    /* 3. 評估給定基因組的架構能量得分 (數值越低越優) */
    double (*evaluate_energy)(uint64_t genome);

    /* 4. 將最優基因組發射為高效的 LLVM IR 或 C 原始碼 */
    void (*emit_llvm_ir)(uint64_t genome, void *module_or_out);
} FlowPluginABI;
```

---

## 12.3 實戰：撰寫您的第一個 FLOW 外掛 (C 語言實作)

以下展示一個自訂的高速矩陣乘法外掛 `flow.matmul` 的完整實作：

```c
/* flow_matmul_plugin.c */
#include "flow.h"
#include "plugin.h"
#include <stdio.h>

#define MATMUL_TILE_BITS 4
#define MATMUL_UNROLL_BITS 2
#define MATMUL_TOTAL_BITS (MATMUL_TILE_BITS + MATMUL_UNROLL_BITS) // 6 bits

static size_t matmul_get_genome_bit_size(void) {
    return MATMUL_TOTAL_BITS;
}

static uint64_t matmul_get_valid_mask(const FlowEnvironmentState *env) {
    /* 若記憶體受限 (< 32MB)，禁止 64x64 大瓦片 (Bits 0..3 必須 <= 0x07) */
    if (env && env->available_memory_bytes < 32 * 1024 * 1024) {
        return 0x00000007ULL;
    }
    return (1ULL << MATMUL_TOTAL_BITS) - 1ULL;
}

static double matmul_evaluate_energy(uint64_t genome) {
    unsigned tile_code = (unsigned)(genome & 0x0F);
    unsigned unroll_code = (unsigned)((genome >> 4) & 0x03);
    
    /* 啟發式能耗模型: 較大的瓦片與適度的展開能降低延遲能量 */
    double energy = 100.0 - (double)tile_code * 4.5 - (double)unroll_code * 6.0;
    return energy;
}

static void matmul_emit_llvm_ir(uint64_t genome, void *module_or_out) {
    FILE *out = (FILE *)module_or_out;
    unsigned tile_size = 1 << (genome & 0x0F);
    fprintf(out, "/* [flow.matmul] SIMD Tiled Kernel (Tile Size: %u) */\n", tile_size);
    fprintf(out, "void flow_matmul_kernel(const float *A, const float *B, float *C, size_t N) {\n");
    fprintf(out, "    // 根據基因組生成的高效向量化瓦片迴圈...\n");
    fprintf(out, "}\n");
}

static const FlowPluginABI MATMUL_ABI = {
    .get_genome_bit_size = matmul_get_genome_bit_size,
    .get_valid_mask = matmul_get_valid_mask,
    .evaluate_energy = matmul_evaluate_energy,
    .emit_llvm_ir = matmul_emit_llvm_ir
};

static const FlowPlugin MATMUL_PLUGIN = {
    .name = "flow.matmul",
    .version = "1.0",
    .doc_title = "SIMD Tiled Matrix Multiplication Kernel",
    .doc_layer = 2
};

static const FlowPluginDescriptor MATMUL_DESCRIPTOR = {
    .abi_major = FLOW_PLUGIN_ABI_MAJOR,
    .abi_minor = FLOW_PLUGIN_ABI_MINOR,
    .descriptor_size = sizeof(FlowPluginDescriptor),
    .module_name = "flow.matmul",
    .module_version = "1.0",
    .plugin = &MATMUL_PLUGIN,
    .abi_v2 = &MATMUL_ABI
};

/* 導出統一動態符號 */
const FlowPluginDescriptor *flow_plugin_entry_v1(void) {
    return &MATMUL_DESCRIPTOR;
}

const FlowPluginABI *flow_plugin_abi_v2(void) {
    return &MATMUL_ABI;
}
```

編譯為動態共享庫：
```sh
clang -std=c17 -O2 -shared -fPIC -DFLOW_PLUGIN_DSO \
    -Isrc flow_matmul_plugin.c -o build/libflow_matmul.so
```

---

## 12.4 跨語言 Rust 外掛開發支援 (`flow-plugin` Crate)

FLOW 官方提供了強型別的 Rust 綁定庫 `crates/flow-plugin`：

```rust
// crates/flow-plugin-example/src/lib.rs
use flow_plugin::prelude::*;

#[derive(Default)]
pub struct FastCryptoPlugin;

impl FlowPluginV2 for FastCryptoPlugin {
    fn genome_bit_size(&self) -> usize {
        8 // 8-bit crypto algorithm & round selection
    }

    fn valid_mask(&self, env: &EnvironmentState) -> u64 {
        if env.has_aes_ni { 0xFF } else { 0x0F }
    }

    fn evaluate_energy(&self, genome: u64) -> f64 {
        // Rust 安全評估邏輯
        50.0 - (genome as f64 * 0.2)
    }

    fn emit_ir(&self, genome: u64, writer: &mut dyn std::io::Write) {
        writeln!(writer, "// Rust-generated AES-GCM zero-copy pipeline (Genome: 0x{:02x})", genome).unwrap();
    }
}

export_flow_plugin!(FastCryptoPlugin);
```

透過純 C 的 ABI v2 契約，FLOW 外掛生態具備了跨 C、C++、Rust、Zig 的全語言相容性與零運行期跨語言開銷。
