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

### Completed Capabilities
- **Lock-Free RCU Fast Path**: Read-side invocations (`flow_reload_call`) use atomic RCU epoch tracking with zero mutex locks on the common path. Test runs observe low single-digit overhead (~3–8% vs direct C call, subject to platform benchmarking).
- **Mode-Dispatched Migration**: Explicit branching across `FLOW_MIGRATE_AUTO`, `FLOW_MIGRATE_SNAPSHOT_COW`, and `FLOW_MIGRATE_STOP_THE_WORLD`.
- **Bounded Live Journal & Fallback**: Concurrent mutation journal logging during migration with automatic stop-the-world catchup fallback on overflow (`flow_reload_live_finish_or_fallback`).
- **Dynamic DSO Module Loading**: `flow_registry_load_dso()` loads shared objects via `dlopen()` / `dlsym()`, validating the standard `FlowPluginDescriptor` ABI (major/minor version, struct size, module hash). Loaded modules remain safely resident in memory.
- **Builtin Plan Artifact Activation**: `flow_reload_plan()` instantiates real executable units (`builtin_create_unit` for `sharded_hash`, `linear_array`, `parallel_map`) with dynamically allocated state, full mutation journaling, and schema-compatible live state transfer.
- **End-to-End Cross-Language Vertical Slice**: Validated pipeline passing Python writable `memoryview` and read-only `bytes` buffers into C native cores, performing live plan hot reloads without data loss, and verifying Rust borrowed slice (`&[u8]`) bounds.
- **Contract and Schema Consistency Validation**: Structural verification ensuring plan dimensions, genome encoding, and component contracts match without schema drift.
- **Self-Host Stage 2 Alignment**: Reproduces identical component selection, contract hash, and `.flowplan` lock format between native `flowc` and stage 2 bootstrap.

### Remaining / Roadmap Items
- **Safe Dynamic DSO Unload**: Module handles currently remain resident to guarantee callback and destructor memory safety. Dynamic `dlclose()` requires full RCU generation reference count draining.
- **Third-Party External Plugin `create_unit`**: External plugins currently fall back to generic state machines unless they provide an explicit `create_unit` callback implementation.
- **True OS/Hardware Copy-on-Write**: Snapshot migration uses live journal replay rather than OS page-level CoW.
- **Standalone Package Distribution**: C headers, Rust slices, and Python ctypes adapters are code-generated integration targets, not standalone distributed packages.
- **Full-Pipeline Self-Host Native Modernization**: Stage 2 bootstrap passes all semantic regression gates, but has not yet been rewritten to directly embed the unified `FlowBitSpace` library.

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
```

The security layer searches plan/composition boundaries. It is not a universal
domain vulnerability scanner. A module remains responsible for domain-local
correctness through its verifier, differential oracle, sanitizer setup, and
regression corpus.

## Repository Boundary

Active FLOW has no build or test dependency on CovOpt, Firefox, or `mp4parse`.
The retired CovOpt tree is kept only as historical material under `work/retired/`
and is excluded from the active build.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

