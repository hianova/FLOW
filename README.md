<p align="center">
  <img src="assets/banner.svg" alt="FLOW Banner" width="100%" />
</p>

# FLOW

> **A Pure-C Constraint Compiler & Dynamic JIT Implementation-Plan Optimizer.**

FLOW compiles declarative intent (`.flow`) into verified, hardware-specialized C implementations. Using **1-Bit Chaotic Search**, **Asynchronous JIT**, and **Unified QSBR**, FLOW dynamically searches and hot-swaps optimal data structures and concurrency layouts with zero runtime latency.

```text
project.flow ──► Semantic IR ──► 1024-Bit BitSpace ──► 1-Bit Chaos + Mask Canvas ──► Async JIT / QSBR ──► Zero-Latency Native Code
```

---

## Key Features

- **1024-Bit 1-Bit Chaotic Search (`FlowGenome`)**: Constant-time $O(1)$ mutation ($12.96\text{ ns/op}$) exploring high-dimensional Pareto implementation frontiers without combinatorial explosion.
- **3-Tier Dynamic Mask Canvas (`FlowMaskCanvas`)**: Superposes Tier-1 hard safety/verifier masks with Tier-2 eBPF/PMU telemetry and Tier-3 domain preferences for 1-cycle early pruning.
- **Asynchronous Background JIT (`FlowAsyncJITPool`)**: Offloads compilation completely to background workers, eliminating main-thread latency ($< 34\mu\text{s}$ P99 latency during live morphing).
- **Unified QSBR (`flow_qsbr_call`)**: Zero-atomic-write, zero-cache-bouncing lock-free memory reclamation delivering $> 390\text{M ops/sec}$ concurrent throughput with offline thread immunity.
- **Dynamic Adaptation & Moving Target Defense (MTD)**: Instant layout morphing (AoS $\leftrightarrow$ SoA $\leftrightarrow$ Columnar) under memory pressure ($96.9\%$ memory reduction) with polymorphic struct randomization.
- **Cross-Language Zero-Copy ABI**: Generates synchronized C headers, Safe Rust crates, and Python `memoryview` bindings.

---

## Example: `project.flow`

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

## Quickstart & CLI

```sh
# 1. Compile spec to optimized C with 1-Bit BitSpace search
flowc examples/project.flow -o build/project.c --search --iterations 100 --seed 42

# 2. Emit multi-language Zero-Copy ABI bindings (C header, Rust, Python)
flowc examples/rank.flow -o build/rank.c --target-c-header build/rank.h --target-rust build/rank.rs --target-python build/rank.py

# 3. Launch Language Server (LSP) over stdio
flowc --lsp

# 4. Generate Formal SMT-LIB2 Mathematical Proof
flowc examples/rank.flow -o build/rank.c --smt-proof build/rank.smt2
```

---

## Build & Test

```sh
# Build compiler and run full test suite (32 regression suites)
make
make test
make acceptance

# Install via Cargo or POSIX make
cargo install --path .
# or: sudo make install
```

---

## License

MIT License - see [LICENSE](LICENSE) for details.
