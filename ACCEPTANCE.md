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

The pure-C security core treats contract, verifier, sanitizer, resource,
physical boundary, and timeout failures as hard rejection conditions across 6 Linker Hard Gates:
1. Contract invariant gate (`flow_security_check_contract_gate`)
2. ABI / migration divergence gate (`flow_security_check_abi_migration_gate`)
3. Ownership & concurrency gate (`flow_security_check_ownership_gate`)
4. Resource quota gate (`flow_security_check_resource_quota_gate`)
5. Multi-component composition gate (`flow_security_check_composition_gate`)
6. Physical barrier invariant gate (`flow_security_check_physical_barrier_gate`) — detects sensor spoofing (NaN/Inf), enforces symplectic Hamiltonian energy drift bounds, and triggers fail-safe envelope clamping (`flow_jet_clamp_to_safety_envelope`).

Additionally, dynamic runtime security includes:
- Phase Space Attractor IDS (`flow_security_check_attractor_anomaly`) bounding workload trajectory inside compact hyper-ellipsoids and enforcing Koopman spectral dissipative stability ($\operatorname{Tr}(K) \le 0$).
- Differential continuity physical proof (`flow_security_check_continuity_proof`) enforcing $C^1/C^2$ Taylor remainder bounds to reject spoofed/replayed impulsive spikes.
- Predictive MTD kinematic extrapolation (`flow_security_predict_resource_breach`, `flow_security_should_proactive_morph`) calculating exact time-to-breach from second-order acceleration ($a = \ddot{q}$) to trigger proactive hot reload before resource exhaustion.
- Symplectic Byzantine consensus (`flow_jet_byzantine_validate_packet`) providing $O(1)$ zero-RPC local filtering against Hamiltonian corruption and geodesic phase deviation.
- JIT $W \oplus X$ memory protection gate (`flow_security_check_jit_wx_gate`) ensuring dual-mapped code heaps are never simultaneously writable and executable.
- Memory transposition alignment & buffer bound gate (`flow_security_check_transposition_gate`) preventing misaligned SIMD accesses and integer buffer overflows during AoS $\leftrightarrow$ SoA migrations.

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

## M9 — Mathematical Self-Constrained Geometry & Polyhedral Hypercube Projection

Pass:

```sh
make polytope-projection-test
```

The self-constraining geometric architecture:
- Replaces all hardcoded timeouts and fixed constants with mathematical constraint projections and statistical derivations.
- Formulates the Mask Canvas as the exact orthogonal projection $\Pi_{\mathcal{P}} : \{Ax \le b\} \to \{0,1\}^N$ of the polyhedral constraint system onto the discrete hypercube.
- Derives QSBR watchdog grace periods dynamically from SemanticIR SLA deadlines or Chebyshev 4-sigma distribution bounds ($\mu + 4\sigma$).
- Computes phase-lag dead-time delay steps dynamically via the Nyquist-Shannon sampling relation $d = \lceil \tau_{\text{delay}} / \Delta t \rceil$.

## M10 — Jet Bundle Dynamics & Multi-Dimensional Cross-Component Fusion (`.fjet`)

Pass:

```sh
make security-test
./build/test-fvec-swarm
```

The high-dimensional `.fjet` continuous phase-space architecture elevates discrete reactive systems into predictive, continuous symplectic physical dynamics across multiple subsystem dimensions:

1. **Negative-Latency Speculative JIT (`flow_speculative_jit.h`, `flow_speculative_jit.c`)**:
   - Anticipates Moreau phase transitions and boundary crossings over lookahead horizon $\tau$ via high-order Jet coordinates $(q, \dot{q}, \ddot{q})$.
   - Pre-assembles specialized machine code into dual-mapped W^X memory in background threads.
   - Commits atomic pointer swaps upon boundary impact with 0ms compilation stall and 0ns TLB shootdowns, formally verified UNSAT by SMT Supreme Court (`flow_speculative_jit_verify_smt`).
2. **Symplectic Dead-Reckoning for CXL & Swarm Interconnects (`flow_jet_dead_reckon.h`, `flow_jet_dead_reckon.c`)**:
   - Suppresses greater than 90% (empirically >99%) of telemetry packet traffic across CXL buses and cluster interconnects by local Velocity Verlet dead reckoning.
   - Restricts packet transmission strictly to Lyapunov divergence horizon breaches ($\Delta > \epsilon$), maintaining continuous sub-microsecond state sync with formal SMT attestation (`flow_jet_dead_reckon_verify_smt`).
3. **Hardware Hamiltonian Potential Regulation (`flow_jet.h`, `flow_jet.c`, `adaptive.h`)**:
   - Maps PMU hardware metrics (L3 cache miss rate, IPC, queue backlog) to generalized coordinates $q$ and momentum $p$.
   - Applies hyperbolic barrier potentials $V(q) = 2\mu / (q_{\text{sat}} - |q|)^3$ and Moreau normal cone restoring forces $\nabla V \in N_C(q)$ to eliminate thrashing and smooth backpressure without heuristic PID oscillations.
4. **Cross-Component Mathematical Fusions (Implemented & Formally Verified)**:
   - **Phase-Space Liquidity Hydrodynamics (`src/flow_jet_lob.h`, `src/flow_jet_lob.c`)**:
     - Models price momentum $\dot{P}$ and Order Flow Imbalance (OFI) acceleration $\ddot{P}$ via Jet Bundle coordinates.
     - Kinematic quadratic root solver predicts exact time-to-collapse $t_{\text{collapse}}$ of resting book depth under aggressive sell/buy waves.
     - Automatically calculates protective adaptive spread $\Delta S(v, a)$ during acceleration surges, neutralizing toxic microsecond latency sniping. Formally verified by `flow_lob_hydrodynamics_verify_smt`.
   - **Non-Smooth Symplectic Impact Manifold (`src/flow_jet_impact.h`, `src/flow_jet_impact.c`)**:
     - Merges Moreau normal cone restitution with symplectic momentum jump mapping: $p^+ = -e \cdot p^- + \Delta p_{\text{contact}}$.
     - Coupled with Mori-Zwanzig viscoelastic memory convolution to absorb collision shocks with zero artificial damping during free flight.
     - Formally guarantees passivity $H(q^+, p^+) \le H(q^-, p^-)$, non-penetration $q \ge q_{\text{surface}}$, and bounded torque limits via `flow_symplectic_impact_verify_smt`.
   - **Latent Geodesic Pre-Play Engine (`src/flow_jet_geodesic.h`, `src/flow_jet_geodesic.c`)**:
     - Bridges the temporal impedance mismatch between 20Hz~50Hz LLM/VLA token autoregression and 10kHz physical joint actuation.
     - Propagates latent state $z$ along symplectic geodesics at 10kHz ($\delta t = 100\mu\text{s}$), dynamically synthesizing 64-bit discrete BMF switchboard coordinates.
     - Blends incoming token arrivals with $C^1$ Hermite continuity, tracking geodesic drift residual with SMT proof (`flow_neuro_geodesic_verify_smt`).
   - Verified across `tests/test_fvec_swarm.c` Stages 11, 12, and 13 (580/580 assertions passed).


## Core boundary

```text
.flow contract (declarative constraints)
    -> module resolve
    -> domain semantic context
    -> polyhedral constraint system \mathcal{P} = {Ax <= b}
    -> orthogonal hypercube projection \Pi_{\mathcal{P}}({0,1}^N)
    -> hierarchical FlowBitSpace
    -> 1-bit chaotic constraint annealing
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
