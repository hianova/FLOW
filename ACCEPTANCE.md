# FLOW acceptance

The reproducible gate is:

```sh
make acceptance
```

This document describes the generic FLOW core only. External domain adapters
are deliberately outside the core acceptance gate.

## M1 — intent to native C

Pass:

```sh
make test
make demos
```

The parser accepts the current vocabulary (`input`, `output`, `state`,
`flow`, `require`, `ensure`, `prefer`, `resource`, `capability`, `domain`,
`contract`, and `fallback`), lowers it to Semantic IR, selects a compatible
component, and emits compilable C for the generic benchmark shapes:

```text
bounded_queue  shared_cache  parallel_map
rank           binary_parser state_machine
```

## M2 — registry and selection

Pass:

```sh
make plugin-test
```

The registry starts with only generic built-in components. An external-style
plugin test registers a candidate and exercises compatibility, memory model,
verification, emission, and oracle dispatch without modifying FLOW core.

Selection filters incompatible candidates before ranking. Explicit
`--component` selection remains available for controlled comparisons; normal
compilation uses the registry selector.

## M3 — constraints and search feedback

Pass:

```sh
make benchmark
```

The verifier exposes `proven`, `runtime_check`, and `compile_error`. Search
uses a deterministic packed genome and one-bit mutation. Model search and
benchmark search share hard-constraint filtering. `--profile-out` and
`--profile` provide reproducible feedback between runs.

The current genome is intentionally small and inspectable. It is an engine
implementation detail, not the public definition of a domain's candidate
matrix; separating those two layers is part of the deep refactor.

## M4 — autopoiesis & meta-intent self-evolution (ouroboros)

Pass:

```sh
make autopoiesis-check
```

The autopoiesis check verifies that the compiler's own intent (`examples/compiler.flow`) is absorbed into the State/Topology Orchestrator canvas (`flowy absorb`), unified into the global topology, and annealed into a verified self-evolving Epoch (`flowy anneal`). The codebase manages its own meta-intent without external static bootstrap templates.

## M5 — reload and migration

Pass:

```sh
make reload-test
make live-reload-test
make backend-reload-test
make generated-reload-test
make reload-stress-test
```

The runtime covers the current same-process FlowUnit ABI, generation publish,
QSBR-safe reclamation, typed schema migration, bounded journal replay, and
rollback on candidate or replay failure. Dynamic modules, persistent
snapshots, crash recovery, and durable policy storage remain future work.

## M6 — adaptive runtime policy

Pass:

```sh
make adaptive-test
```

The v0 controller measures a sample window, consults an application workload
probe, applies an improvement threshold and cooldown, and requests a verified
live migration. The application still owns domain correctness and probe
semantics.

## M7 — generic security composition & Linker Hard Gates

Pass:

```sh
make security-test
```

The pure-C security core treats contract, verifier, sanitizer, resource, and
timeout failures as hard rejection conditions across 5 Linker Hard Gates:
1. Contract invariant gate
2. ABI / migration divergence gate
3. Ownership & concurrency gate
4. Resource quota gate
5. Multi-component composition gate

It tests deterministic one-bit plan mutations over hierarchical `FlowBitSpace` and produces verified attestation records.

## M8 — unified 1-Bit State Spine & Plan Artifact Persistence

Pass:

```sh
make bitspace-test
```

The unified hierarchical bitspace architecture:
- Low-order $k = \lceil \log_2 N \rceil$ bits select compatible candidate components.
- High-order bits dynamically decode candidate-specific plan dimensions (`tile`, `batch`, `layout`, `capacity`, `threads`, `shards`).
- 1-bit chaotic mutation explores both candidate space and parameter space.
- `.flowplan` evidence spine captures `contract_hash`, `plan_schema_hash`, `seed`, `genome`, dynamic dimension definitions, metrics, and attestation.

## Core boundary

```text
.flow contract (declarative constraints)
    -> module resolve
    -> domain semantic context
    -> hierarchical FlowBitSpace
    -> 1-bit chaotic search
    -> Linker Hard Gates
    -> FlowPlanArtifact (.flowplan)
    -> verified C / ABI backend emission
```

Properties:
- FLOW core has no domain names or backend-specific hardcodings.
- Plugins declare arbitrary candidate dimensions (`FlowDimensionKind`) searched seamlessly by `FlowBitSpace`.
- Hard constraints and Linker Hard Gates are separated from objective energy scoring.
- Measurement, verification evidence, and selection regret are first-class outputs.
- Active codebase has zero external dependencies on retired analyzers.
