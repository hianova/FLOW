<p align="center">
  <img src="assets/banner.svg" alt="FLOW Banner" width="100%" />
</p>

# FLOW

> **Intent-Driven Declarative Compiler, Cubical HoTT Type Sieve & Lean 4 Verified Runtime**

FLOW compiles declarative intents (`.flow` and literate `.md` specifications) into formally verified, zero-overhead native code and Lean 4 proof certificates. Guided by **Occam's Razor**, FLOW maintains a compact, zero-bloat core (`src/` ~5,800 LOC, 0 cross-layer leaks) built on **64-Bit Discrete Cubical Homotopy Type Theory (`cubical_hott.c`)**, **Presburger Polyhedral Synthesis (`polyhedral.c`)**, **SMT QF_LIA / QF_BV Formal Verification (`smt.c`)**, **Phase Space Contact-Jet Bundles (`.fjet`)**, **Architectural Memory Lockfiles (`.fvec`)**, and **Wavefront-Coupled QSBR Live Hot Reload (`reload.c`)**.

```text
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                       FLOW Intent-Driven Architecture (~5,800 LOC)                     │
├────────────────────────────────────────────────────────────────────────────────────────┤
│                                                                                        │
│   🧠【Layer 1 & 2: Intent Lowering, Cubical HoTT Sieve & Polyhedral Synthesis】         │
│      • parser.c / flowc.c  : Declarative .flow & Literate Markdown (Doc-as-Intent)     │
│      • cubical_hott.c      : 64-Bit De Morgan Cubical Type Sieve & Kan Open-Box Filler │
│      • polyhedral.c        : Presburger affine & quadratic Hessian loop synthesis      │
│      • bitspace.c / search.c: 64-bit genome encoding & one chaotic 1-bit mutation      │
│      • token_ring.c        : Discrete attention operator Canvas_{t+1} = Φ(Canvas ⊗ M)  │
│      • topology.c          : Real AST workspace dependency scanner (Modularity Q=1.00) │
│                                                                                        │
│   ⚖️【Layer 3: Formal Verification — SMT Supreme Court & Lean 4 Reflection】           │
│      • smt.c               : QF_LIA / QF_BV 4-Theorem Prover & SMT-LIB2 exporter       │
│      • abi.c               : Lean 4 proof certificate emitter (0-sorry, @& FFI, 64-bit)│
│      • geometric_axiom.c   : Unified Fiber Bundle Section (SMT + Polytope + BMF + Jet) │
│      • security.c          : 5-Gate composition firewall, W^X JIT & Continuity Gates   │
│                                                                                        │
│   🦾【Layer 4: Runtime Substrate, Live Reload & Domain Plugins】                       │
│      • reload.c / jit.c    : Lock-free QSBR live hot reload & dual-mapped W^X JIT      │
│      • flow_jet.c          : Thermal Contact-Jet, Symplectic Verlet & Online EDMD      │
│      • fvec/fvec_core.c    : 1024B .fvec architectural memory & <100ns pre-staging     │
│      • swarm.c             : 64-node lock-free ring mesh & 9-byte lymphatic gossip     │
│      • plugins/            : Clean ABI domain plugins (embodied, CAN-FD, IMU, MZ)      │
│                                                                                        │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## ✨ Core Architectural Pillars

### 1. 64-Bit Discrete Cubical HoTT & Kan Homotopy Sieve (`src/cubical_hott.c`)
- Projects declarative `SemanticIR` requirements and `Component` capabilities into 64-bit `FlowCubicalTypeSieve` masks over the De Morgan interval $\mathbb{I} = \{0, 1\}$.
- Verifies component compatibility and live-reload state transitions in **1 CPU cycle** via Kan open-box homotopy: `(intent ^ candidate) & boundary_filter == 0`.

### 2. Self-Hosting Lean 4 Reflection & Zero-Sorry Gate (`src/abi.c`, `src/security.c`)
- Emits standalone **Lean 4** formal proof certificates (`--target-lean`) alongside C headers (`--target-c-header`), Rust (`--target-rust`), and Python (`--target-python`) bindings.
- Formally proves 10 machine-checked theorems (`by decide` / `by rfl`), including **Non-Vacuous Precondition Witness**, **4 SMT Safety Theorems**, **Kan Open-Box Homotopy**, **Geometric Transversality**, **Dual-Track Spec/Exec Isomorphism**, and **Zero-Copy `@&` Borrowed FFI** with dimensional newtypes (`flow_pct_vol_t`, `flow_point_vol_t`).

### 3. Unified Fiber Bundle & Thermal Contact-Jet (`src/geometric_axiom.c`, `src/flow_jet.c`)
- Unifies discrete 64-bit BMF switchboards with continuous phase-space Contact-Jet dynamics $(q, p, \ddot{q}, s)$, coupling symplectic Velocity Verlet integration, Mori-Zwanzig memory kernels, online streaming EDMD, and thermodynamic dissipation control.

### 4. Real AST Workspace Topology & Autonomic CLI (`src/topology.c`, `src/flowy.c`)
- Dynamically scans `src/` and `plugins/` C/H files to construct the live dependency graph, verifying `Cross-Layer Leaks == 0` and `Modularity == 1.00`.
- Provides `flowy` for causal inspection (`flowy why`, `flowy audit`, `flowy absorb`, `flowy anneal`) and `.fvec` / `.fjet` lifecycle management.

---

## 💻 Quick Start & CLI Usage

### Build & Run Full Verification Suite

```sh
# Build libflow.a, flowc, flowy, and domain plugins (libflow_embodied.so, libflow_swarm.so)
make -j4 all

# Run all 8 consolidated test suites + E2E compiler & literate Markdown tests
make test

# Run self-hosting compiler Lean 4 reflection & topology annealing check
make autopoiesis-check

# Audit real workspace AST topology and 8 core dynamic mechanisms
./build/flowy audit
./build/flowy audit-mechanisms
```

### Compiler (`flowc`) & Runtime Brain (`flowy`)

```sh
# Compile a declarative .flow spec with Lean 4 certificate and cross-language ABI headers
./build/flowc examples/rank.flow -o generated/rank.c \
    --target-c-header generated/rank.h \
    --target-rust generated/rank.rs \
    --target-python generated/rank.py \
    --target-lean generated/rank.lean

# Compile a Literate Markdown specification (Doc-as-Intent) directly
./build/flowc flow-book/src/ch02_intent_vs_implementation.md -o generated/ch02.c --search --iterations 50 --seed 42
```

---

## 📚 The FLOW Book (`flow-book/`)

The 6-chapter distilled bilingual specification and architectural guide lives in [`flow-book/src/`](flow-book/src/SUMMARY.md) as pure Literate Markdown (`.md`). Chapters containing embedded ```` ```flow ```` blocks can be compiled directly by `flowc`.

---

## 📜 License

MIT License — see [LICENSE](LICENSE) for details.
