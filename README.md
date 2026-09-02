<p align="center">
  <img src="assets/banner.svg" alt="FLOW Banner" width="100%" />
</p>

# FLOW

> **Living Topology Orchestrator & Autonomous Continuous Evolution Engine**

FLOW manages declarative intents and constraint topologies (`.flow`). Using **1-Bit Chaotic Annealing**, **Declarative Plugin Contracts**, **Asynchronous JIT**, and **Unified QSBR**, FLOW continuously searches, verifies, and hot-swaps optimal machine code with zero runtime latency.

```text
 ┌────────────────────────────────────────────────────────────────────────┐
 │                      LIVING CODEBASE SUITE                             │
 │                                                                        │
 │ [flow absorb] ──► Global Constraint Topology ──► 1024-Bit BitSpace     │
 │                           │                              │             │
 │                           ▼                              ▼             │
 │ [flow daemon] ◄── Continuous Annealing ──► Async JIT & Unified QSBR   │
 └────────────────────────────────────────────────────────────────────────┘
```

---

## Key Features

- **Living Topology Orchestrator**: `flow absorb` unifies multi-intent constraint graphs via Semantic Merge; `flow daemon` runs continuous background annealing to minimize entropy.
- **Declarative Plugin Contracts**: Zero-C-callback plugin architecture (`FlowPluginContract`) synthesizes domain verification, dimension spaces, and cost models automatically.
- **1024-Bit Chaotic Search (`FlowGenome`)**: Constant-time $O(1)$ mutation ($12.96\text{ ns/op}$) exploring high-dimensional Pareto implementation frontiers without combinatorial explosion.
- **3-Tier Dynamic Mask Canvas (`FlowMaskCanvas`)**: Superposes Tier-1 hard safety masks, Tier-2 eBPF/PMU telemetry, and Tier-3 domain preferences for 1-cycle early pruning.
- **Enterprise Production Suite**: Features Deterministic Audit Trails with 100% exact time-travel replay, sub-microsecond ($< 1\mu\text{s}$) QSBR Golden Baseline fallback, and Bounded Chaos compliance gating.
- **Asynchronous JIT & Unified QSBR**: Background worker pool eliminating compilation latency with zero-atomic-write lock-free QSBR memory reclamation ($> 390\text{M ops/s}$).

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
# 1. Absorb intent into global constraint topology
flowc absorb examples/project.flow

# 2. Global chaotic Pareto annealing & epoch solidification
flowc anneal examples/compiler.flow examples/project.flow

# 3. Flowy: Living Introspection, Causal Explanation & Living Docs
flowc ask "how does lock-free QSBR memory reclamation work?"
flowc why                  # Explains deterministic causality of latest real-time morph
flowc bottleneck           # Identifies neural telemetry peak hotspot & self-healing action
flowc timeline             # Real-time dynamic decision timeline
flowc doc bitspace         # Living codebase API & invariant documentation
flowc flowy                # Interactive introspective reasoning shell

# 4. Unified Architectural & SMT Formal Invariant Audit
flowc audit

# 5. Mechanism Efficiency & Quantitative Empirical Audit
flowc audit-mechanisms

# 6. Launch background continuous evolution daemon
flowc daemon --cycles 10 --interval-ms 500

# 7. Multi-objective state time travel (speed, balanced, memory)
flowc morph speed
```

---

## Build & Test

```sh
# Build binary and run full test suite (38 regression suites)
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
