<p align="center">
  <img src="assets/banner.svg" alt="FLOW Banner" width="100%" />
</p>

# FLOW

> **Living Topology Orchestrator & Autonomous Continuous Evolution Engine**

FLOW compiles declarative intents (`.flow`) into zero-overhead native code. Using **1-Bit Chaotic Annealing**, **Declarative Dynamic Plugins (DSO)**, **Unified QSBR with Watchdog Quarantine**, **SMT Epistatic Gene Linkage**, and **Smith Predictor Dual-Rate Control**, FLOW explores, verifies, and hot-swaps optimal machine implementations with zero runtime latency.

```text
 ┌────────────────────────────────────────────────────────────────────────┐
 │                      FLOW DECOUPLED ARCHITECTURE                       │
 │                                                                        │
 │ [flowc] (Minimal Compiler) ──► 1-Bit Chaos Engine ──► C / IR / ABI     │
 │                                        ▲                               │
 │                                        │ (On-Demand DSO Masks)         │
 │                        [Dynamic Plugins: libflow_*.so]                 │
 │                                                                        │
 │ [flowy] (Introspective CLI) ──► Topology Graph ──► Formal SMT & Audit  │
 └────────────────────────────────────────────────────────────────────────┘
```

---

## Key Subsystems & Guarantees

- **Occam's Razor Decoupled Core**:
  - `build/flowc`: Ultra-minimal ahead-of-time compiler (Parser, 1-Bit Chaos Engine, Multi-Target Emitters).
  - `build/flowy`: Standalone introspection assistant for architecture querying, causal explanation, neural telemetry, and living documentation.
  - `build/libflow_*.so`: Dynamic DSO plugins (`flow.embodied`, `flow.smt`, `flow.security`, `flow.swarm`, `flow.genetic`) loaded on-demand.
- **1-Bit Chaotic Annealing (`FlowGenome`)**: Constant-time $O(1)$ mutation ($12.96\text{ ns/op}$) exploring high-dimensional Pareto implementation frontiers without combinatorial explosion.
- **SMT Epistatic Gene Linkage (`FlowSMT`)**: Automated SMT analysis identifies tightly coupled bit groups, mutating them atomically to eliminate epistasis barriers.
- **Unified QSBR with Watchdog Quarantine**: Lock-free epoch reclamation ($> 390\text{M ops/s}$) featuring an Epoch Watchdog and memory page isolation (`mprotect`) preventing straggler-induced memory leaks.
- **Virtual Memory Zero-Copy Page Remap**: Instantaneous sub-microsecond hot-swap page-remapping with zero memcpy overhead.
- **Embodied AI & Smith Predictor (`FlowEmbodied`)**: Micro-physics ZMP zero-fall simulator, dual-rate 10kHz spinal reflex, and 3ms dead-time phase lag compensation.

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

## Quickstart & CLI Usage

### 1. Minimal Ahead-of-Time Compilation (`flowc`)

```sh
# Basic compilation to native C
flowc examples/rank.flow -o generated/rank.c

# 1-Bit Chaotic Search optimization with profile feedback
flowc examples/rank.flow -o generated/rank.c --search --iterations 250 --seed 42

# Multi-target cross-language and IR code generation
flowc examples/rank.flow -o generated/rank.c \
    --target-c-header generated/rank.h \
    --target-rust generated/rank.rs \
    --target-python generated/rank.py \
    --target-mlir generated/rank.mlir \
    --target-llvm-ir generated/rank.ll
```

### 2. Standalone Introspection & Architecture Assistant (`flowy`)

```sh
# Introspective Codebase Q&A
flowy ask "how does lock-free QSBR memory reclamation work?"

# Real-Time Causal Decision & Hotspot Explanations
flowy why                  # Explains deterministic causality of latest real-time morph
flowy bottleneck           # Identifies neural telemetry peak hotspot & self-healing action
flowy timeline             # Real-time dynamic decision timeline

# Living Codebase Documentation
flowy doc all              # Complete living architecture docs
flowy doc bitspace         # Specific module documentation

# Living Topology Orchestration & Global Annealing
flowy absorb examples/compiler.flow
flowy anneal examples/compiler.flow examples/project.flow
flowy landscape            # Pareto multi-objective topology report

# Unified Architectural & Formal SMT Invariant Audit
flowy audit                # Comprehensive 22-node codebase & theorem prover audit
flowy audit-mechanisms     # Quantitative mechanism efficiency audit

# Interactive Living Assistant Shell
flowy shell
```

---

## Build & Test

```sh
# Build flowc, flowy, and dynamic plugins
make all

# Run full test suite (40 regression & hardening suites)
make test

# Run end-to-end acceptance & benchmark verification
make acceptance

# System installation
sudo make install
```

---

## License

MIT License - see [LICENSE](LICENSE) for details.
