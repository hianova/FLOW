# FLOW

FLOW is a pure-C, project-level constraint compiler and implementation-plan optimizer.

The maintained source of truth is a project `.flow` file. It describes intent,
contracts, imports, resource limits, and optimization preferences. It does not
describe the implementation algorithm. Imported modules provide the domain
semantics, candidate implementations, plan dimensions, cost models, verifiers,
oracles, and native emitters.

```text
project.flow
    -> core parser / SemanticIR
    -> imported module resolution
    -> compatible candidate plans
    -> hierarchical FlowBitSpace
    -> one-bit search + hard gates
    -> FlowPlanArtifact (.flowplan / .lock)
    -> generated C / Zero-Copy ABI adapter
```

FLOW is not a general-purpose scripting language, a universal C synthesizer,
or an exhaustive implementation-frontier enumerator. Its job is to search a
verified implementation space supplied by modules and select a deployable
plan under project constraints.

## Project flow

`project.flow` is the project-level constraint canvas. It replaces scattered
manual tuning decisions as the primary project input.

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

The current example is [examples/project.flow](examples/project.flow).
Other files under `examples/` are focused regression fixtures, not separate
project configuration systems.

## Build and acceptance

```sh
make
make test
make acceptance
```

## Installation

### Method 1: Via `cargo install` (Rust Ecosystem)
```sh
cargo install --git https://github.com/hianova/FLOW.git
# or locally:
cargo install --path .
```

### Method 2: Via `make install` (Standard POSIX)
```sh
make
sudo make install
# or install to ~/.local/bin without root:
make install PREFIX=$HOME/.local
```

### Method 3: One-Line Install Script
```sh
curl -fsSL https://raw.githubusercontent.com/hianova/FLOW/main/tools/install.sh | sh
```

The acceptance command covers generic C generation, dynamic DSO loading,
hierarchical bit-space search, plan persistence, lock-free RCU reload/migration,
adaptive runtime policy, zero-copy ABI view validation, C/Rust/Python vertical
slice, compositional security gates, benchmarks, and the bootstrap regression check.

## Current State and Architecture Scope

- **In-Memory Zero-I/O JIT Engine (`src/jit.h`, `src/jit.c`)**: Synthesizes and loads executable machine code directly in-process from LLVM IR into executable pages, achieving sub-millisecond hot-swaps with zero disk I/O, and automatically registering JIT IP ranges `[start_ip, end_ip)` for eBPF PMU attribution.
- **Smart State Layout Migration & Columnar Zero-Copy (`FLOW_LAYOUT_AOS`, `FLOW_LAYOUT_SOA`, `FLOW_LAYOUT_COLUMNAR`)**: High-performance dynamic transformation routines between Array-of-Structs and Struct-of-Arrays with partial columnar zero-copy pointer preservation for untouched fields.
- **Migration Cost & Payback Amortization Model**: Evaluates live state transformation cost ($0.2\text{ns/byte}$) against steady-state gain to compute break-even horizons, preventing churn when migration overhead exceeds performance benefit.
- **Lock-Free RCU Fast Path**: Read-side invocations (`flow_reload_call`) use atomic RCU epoch tracking with zero mutex locks on the common path. Test runs observe low single-digit overhead (~3–8% vs direct C call).
- **Mode-Dispatched Migration & Live Journal**: Explicit branching across `FLOW_MIGRATE_AUTO`, `FLOW_MIGRATE_SNAPSHOT_COW`, and `FLOW_MIGRATE_STOP_THE_WORLD` with bounded live mutation journal logging and automatic fallback.
- **eBPF Silicon-Grade Telemetry & Anti-Thrashing (`tools/flow_probe.bpf.c`)**: Instruction Pointer (IP) range attribution filtering (`[start_ip, end_ip)`) ensuring PMU hardware samples (L3 misses, IPC) reflect FLOW execution only. Employs Exponential Moving Average (EMA), anomaly streak confirmation, and exponential backoff cooldown to eliminate thrashing.
- **Non-Blocking Progressive LSP Server (`flowc --lsp`)**: Full JSON-RPC 2.0 Language Server with sub-millisecond static syntax/contract diagnostics (`publishDiagnostics`), hover documentation, and dynamic streaming of Pareto frontiers (`flow/paretoUpdate`), SMT proofs, and topology graphs.
- **Multi-Objective Pareto Frontier & Plan Ensembles (`--ensemble <prefix>`)**: Automatic extraction of Pareto knee-points into 3 deployable tactical candidates (`Speed`, `Balanced`, `Memory`), emitting unified dispatch headers (`_ensemble.h`) and lockfiles (`_bundle.lock`).
- **Formal SMT-LIB2 Mathematical Proofs (`--smt-proof <proof.smt2>`)**: Sound QF_BV SMT-LIB 2.6 logic theorem emission asserting invariant negations (Buffer Bounds, Memory Quota, Shard Non-Aliasing, and Functional Determinism) for Proof-Carrying Code.
- **MLIR `flow` Dialect & Intent Preservation (`--target-mlir <file.mlir>`)**: Emits high-level `flow.intent` and `flow.constraint` operations lowered into standard `func.func`, `scf.for`, and `memref` pipelines.
- **Direct LLVM IR Emission & LTO (`--target-llvm-ir <file.ll>`)**: Emits native LLVM IR bitcode text for cross-language Link-Time Optimization (LTO) with zero-cost function inlining across Rust, C++, and C host runtimes.
- **Codebase Topology Knowledge Graph & Firewall Audit (`--topology-audit`, `--topology <file.json/dot>`)**: In-memory knowledge graph auditing compiler layers (Core Layer 0 vs Interface Layer 1 vs Plugin Layer 2), guaranteeing zero interface leaks while driving shard partition locality.
- **Mutation Heatmap & Deterministic Replay (`--heatmap`, `--explain-seed <SEED>`)**: Zero-overhead in-memory failure counter tracking across 1-bit mutation searches with deterministic step-by-step diagnostic replay.
- **Safe Rust Plugin SDK (`crates/flow-plugin`)**: Zero-dependency Safe Rust crate with `declare_flow_plugin!` exporting standard CDYLIB plugins verified by DSO loaders (`flow_registry_load_dso`).
- **LLVM libFuzzer & Standalone Robustness Harness (`make fuzz-test`, `make fuzz`)**: Complete memory safety verification against malformed, unbounded, and corrupted specification streams.
- **End-to-End Cross-Language Vertical Slice**: Validated pipeline passing Python writable `memoryview` and read-only `bytes` buffers into C native cores, performing live plan hot reloads without data loss, and verifying Rust borrowed slice (`&[u8]`) bounds.
- **Self-Host Bootstrap Alignment**: Reproduces identical component selection, contract hash, and `.flowplan` lock format between native `flowc` and stage 2/3 bootstrap engines.

## CLI Reference

```sh
# Basic compilation to C
flowc examples/rank.flow -o build/rank.c

# BitSpace search with iterations and deterministic seed
flowc examples/rank.flow -o build/rank.c --search --iterations 100 --seed 42

# Launch Language Server Protocol (LSP) Server over stdio
flowc --lsp

# Emit Pareto Plan Ensemble Bundle (Speed, Balanced, Memory)
flowc examples/rank.flow -o build/rank.c --search --ensemble build/rank

# Emit Formal SMT-LIB2 Mathematical Proof Script
flowc examples/rank.flow -o build/rank.c --smt-proof build/rank.smt2

# Emit MLIR flow dialect and LLVM IR bitcode
flowc examples/rank.flow -o build/rank.c --target-mlir build/rank.mlir --target-llvm-ir build/rank.ll

# Export Codebase / Intent Topology Graph and perform Interface Firewall Audit
flowc examples/rank.flow -o build/rank.c --topology build/rank_topology.json --topology-audit

# Search failure heatmap & deterministic mutation replay
flowc examples/rank.flow -o build/rank.c --search --heatmap --explain-seed 42

# Generate multi-language Zero-Copy ABI adapters
flowc examples/rank.flow -o build/rank.c --target-c-header build/rank.h --target-rust build/rank.rs --target-python build/rank.py
```

## Verification and Security

[src/security.c](src/security.c) implements generic linker hard gates for composition:
1. Contract invariants;
2. ABI and migration compatibility;
3. Ownership and concurrency boundaries;
4. Resource quotas;
5. Composition-level closure.

```sh
make security-test
make abi-test
make vertical-slice-test
make ensemble-test
make smt-test
make mlir-llvm-test
make topology-test
make ebpf-pmu-test
make lsp-test
make jit-migration-test
make fuzz-test
```

## Repository Boundary

Active FLOW has no build or test dependency on CovOpt, Firefox, or `mp4parse`.
The retired CovOpt tree is kept only as historical material under `work/retired/`
and is excluded from the active build.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

