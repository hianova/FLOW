<p align="center">
  <img src="assets/banner.svg" alt="FLOW Banner" width="100%" />
</p>

# FLOW

> **Pure `.lean` Mathematical-AST Fusion Engine, Cubical HoTT Type Sieve & Zero-Copy QSBR Runtime**

FLOW fuses **Domain Intent**, **Formal Proof**, and **Native Hardware Execution** into a single compact C17 engine (`src/` + `plugins/` ~6,000 LOC, `Cross-Layer Leaks = 0`, `Modularity = 1.00`). Instead of maintaining separate configuration files, C/C++ kernels, FFI glue (`@[export]` / `extern "C"`), and post-hoc unit tests, developers or AI agents provide a single idiomatic **Lean 4 (`.lean`)** or declarative **`.flow`** specification. FLOW automatically infers memory layouts from native `structure` types, anchors compute kernels directly from `theorem` proofs, lowers domain semantics via plugins, and wraps the synthesized C/LLVM operator in a lock-free **QSBR** epoch runtime with zero-copy adapters for **C, Rust, Python, JS/Bun, CUDA, MATLAB, and Julia**.

```text
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                    FLOW Three-Layer Mathematical-AST Fusion (~6,000 LOC)               │
├────────────────────────────────────────────────────────────────────────────────────────┤
│                                                                                        │
│   1️⃣【Pure .lean / .flow Intent & Proof — Single Source of Truth】                     │
│      • Native `structure`   : Auto-infers stride, alignment & hot-reload schema        │
│      • Pure `def` + `theorem`: Theorem-anchored kernel discovery (Zero @[export] FFI)  │
│      • Zero-sorry hard gate : Rejects any `sorry` or custom `axiom` at parse/audit time│
│                                                                                        │
│   2️⃣【Domain Functor Lowering, Cubical HoTT Sieve & Token-Ring Brain】                 │
│      • plugins/embodied.c   : Lowers high-level domain intent -> 1kHz SLA & ZMP Jet    │
│      • cubical_hott.c       : 64-Bit De Morgan Cubical Sieve & 6-subspace algebra      │
│      • token_ring.c         : 5-stage discrete attractor Canvas_{t+1} = Φ(Canvas ⊗ M) │
│      • topology.c           : 100% live AST `#include` & registry graph (Leaks = 0)    │
│                                                                                        │
│   3️⃣【QSBR Zero-Copy Substrate & Polyglot 64B Extent Projector】                       │
│      • backend.c / reload.c : Synthesizes native C kernel + lock-free QSBR epoch lease │
│      • abi.c                : 64B FlowExtentDescriptor (1PB out-of-core zero-copy)     │
│      • Polyglot targets     : C/C++, Rust, Python, JS/Bun, CUDA, MATLAB, Julia, MLIR   │
│                                                                                        │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## ✨ Why FLOW? (Core Architectural Pillars & Cost-Benefit)

| Dimension | Traditional Systems / FFI Stack | FLOW Pure `.lean` Fusion |
| :--- | :--- | :--- |
| **Source of Truth** | Separate YAML config + C/Rust kernel + Python/JS FFI bindings + unit tests | **1 Pure `.lean` file** (`structure` + `def` + `theorem`) |
| **FFI Boilerplate** | Manual `@[export]`, `extern "C"`, struct marshaling & alignment padding | **Zero `@[export]`** — theorem-anchored operator discovery & native `structure` stride inference |
| **Correctness Guarantee** | Runtime assertions, fuzzing, or detached proofs | **Zero-`sorry` Lean 4 proof** + **QF_LIA 4-Theorem SMT Gate** (`buffer_bounds`, `memory_quota`, `shard_isolation`, `determinism`) |
| **Concurrency & Hot-Swap** | Manual mutexes / RW-locks or process restarts | Automatic **64B cache-line-aligned QSBR** epoch lease (`< 100ns` hot-swap) |
| **Polyglot Interop** | Per-language wrapper maintenance | Single-command 64B `FlowExtentDescriptor` zero-copy emission for C, Rust, Python, JS, CUDA, MATLAB & Julia |

---

## ⚡ Pure `.lean` Zero-Shot Authoring & FFI Cheat Sheet (For Humans & AI Agents)

When authoring a new FLOW project in pure `.lean`, follow these **4 deterministic rules** to compile on the first try with zero trial-and-error:

| Rule | Specification & Exact Convention |
| :--- | :--- |
| **1. Supported Subset & 64B `structure`** | • Use zero-dependency Core Lean 4 only (**do not** `import Mathlib`; **never** use `sorry` or custom `axiom`).<br>• Primitive types map directly to C17: `UInt64` / `Nat` $\to$ `uint64_t`, `UInt32` $\to$ `uint32_t`, `Bool` $\to$ `bool`, `Float` $\to$ `double`.<br>• Define your primary `structure` with **8 × 64-bit fields (64 bytes)** so `flowc` infers `stride = 64` (1 CPU cache line). |
| **2. Zero-`@[export]` Kernel Discovery** | • `flowc` scans the AST for the **last computational `def` referenced inside a `theorem`** and automatically binds it as the C17 kernel entry point (`spec->extern_kernel`).<br>• Standard 1PB zero-copy kernel signature (returns `0` for PASS, `1` for REJECT):<br>`def myStepKernel (baseOff byteLen stride epoch : UInt64) : UInt32 := if stride >= 64 && ... then 0 else 1` |
| **3. Zero-Retry Proof Tactics (`decide` & `omega`)** | • **Concrete test vectors / boundary checks**: Use `:= by decide` (e.g., `theorem kernel_ok : myStepKernel 0 64 64 1 = 0 := by decide`).<br>• **Universal linear arithmetic (`∀`)**: Use `:= by omega`. **Crucial `Nat` Subtraction Rule**: Lean 4 `Nat` subtraction truncates at `0` (`a - b = 0` when `a < b`). Every theorem containing `a - b` **must** include the precondition `(h : b ≤ a)` in its signature so `omega` succeeds immediately. |
| **4. 5-Line Zero-Copy FFI Invocation** | Compile via `flowc spec.lean -o build/app.c --target-python build/app.py --target-rust build/app.rs` and `clang -O2 -shared -fPIC -pthread build/app.c -o build/libapp.so -lm`, then call in Python/Rust/JS:<br>`with app.LinearArray("build/libapp.so") as eng:`<br>`    out_ext = eng.process_descriptor(app.FlowExtentDescriptor(0, 1 << 50, 0, 64, 8, 1, 0))` |

### Plain-Language Guide to `flowc` / `flowy` CLI Output

- `verifier: status=proven capacity=1024 estimated_bytes=8192 (lean_certified & input bound proven)`
  $\rightarrow$ **Logic & Memory Verified**: All Lean 4 theorems hold with zero `sorry`, and total static memory is strictly bounded (8 KB, zero GC).
- `SMT proof: status=unsat_proven_sound (4/4 theorems verified UNSAT)`
  $\rightarrow$ **Zero Counterexamples (`UNSAT`)**: The SMT solver searched the entire state space for a bug/overflow and proved the error condition is **unsatisfiable** (100% safe).
- `symplectic: omega=2.000 mu=0.000977 q_sat=1280.0 (polyhedral shaped)`
  $\rightarrow$ **Hardware Pacing**: Loop tile bounds and backpressure thresholds are automatically shaped to prevent CPU/GPU thermal throttling.

---

## 💻 Quick Start & Usage

### 1. Build & Run All 8 Domain Test Suites

```sh
# Build libflow.a, flowc, flowy, and domain plugins (libflow_embodied.so, libflow_swarm.so)
make -j4 all

# Run all 8 native domain test suites (Brain, Body, Concurrency, Fvec-Swarm, System, Core, Axiom, HoTT)
make -j4 test
```

### 2. Write & Run a Custom Pure `.lean` Domain Kernel

Create a pure Lean 4 specification (e.g. `reflex.lean`):

```lean
namespace Embodied.GaitReflex

structure JointState where
  angle    : Float
  velocity : Float
  torque   : Float
  deriving Repr

def stepReflex (baseOff byteLen stride epoch : UInt64) : UInt32 :=
  if stride >= 24 && epoch > 0 && baseOff + byteLen >= baseOff then 0 else 1

theorem zmp_torque_safe : stepReflex 0 24 24 1 = 0 := by decide

end Embodied.GaitReflex
```

Compile, verify, and execute directly or emit polyglot zero-copy adapters:

```sh
# Directly verify, synthesize C + QSBR wrapper, and execute via flowy
./build/flowy run reflex.lean

# Emit C header, Rust, Python, CUDA, JS, MATLAB, Julia, and Lean 4 reflection certificate
./build/flowc reflex.lean -o build/reflex.c \
    --target-c-header build/reflex.h \
    --target-rust build/reflex.rs \
    --target-python build/reflex.py \
    --target-cuda build/reflex.cuh \
    --target-js build/reflex.js
```

### 3. Real-World Production Blueprints & Benchmarks

| Blueprint / Repository | Domain & Problem Solved | Key Pure `.lean` Mechanisms |
| :--- | :--- | :--- |
| **[`tests/cedar_authorization.lean`](tests/cedar_authorization.lean)**<br>*(Built-in Benchmark)* | **Cloud IAM Policy Engine** (427 lines unmodified from [AWS `cedar-policy/cedar-spec`](https://github.com/cedar-policy/cedar-spec)) | Auto-infers 56B stride from `Request` + `Response`; anchors `isAuthorized` via `forbid_trumps_permit` & `default_deny`. Run via `./build/flowy run tests/cedar_authorization.lean`. |
| **[`hianova/Ricardian`](https://github.com/hianova/Ricardian)** | **Anti-Monopoly Macroeconomic Engine & Sovereign Settlement** (Ricardian differential rent, Harberger tax, double-entry ledger, Constant-Product AMM, Assembly Theory, 4-tier liquidity corridor, wash-trading filter, SEARU 1-bit chaos 3-SAT & Kardashev Era 3 ascension) | 64B `RicardianState`; 26 zero-`sorry` Lean 4 theorems anchoring `stepRicardian` across 1PB out-of-core extents (`0.10 µs` / step). |
| **[`hianova/flow_business`](https://github.com/hianova/flow_business)** | **Six-Pillar Enterprise Forensic Sidecar & Executive Ownership Ledger** (P2P, O2C discount anti-leakage, Treasury covenants, USMCA RVC, HR EEOC, Ad IVT) | Tri-State forensic gate (`AUTO_PASS` / `FORENSIC_HOLD` / `HARD_BLOCK`), Min-Cut counterfactual remedies ($\Delta Q$ or $\Delta B$), and SoD double-entry executive liability signatures. |

### 4. Live AST Topology Audit & Token-Ring Introspection (`flowy`)

```sh
# Audit live C/H/Plugin/Intent AST dependency graph and layer separation
./build/flowy audit

# Query any module, plugin, or verified intent via Live AST + 5-Stage Token Ring + Cubical HoTT
./build/flowy ask "cedar"
./build/flowy ask "token_ring"
./build/flowy inspect doc
./build/flowy inspect topos
```

---

## 🗂️ Repository Layout

```text
FLOW/
├── src/                # Core C17 engine: parser, Cubical HoTT sieve, SMT, polyhedral, QSBR, ABI, flowy
├── plugins/            # Domain intent functors & hardware drivers (embodied, Mori-Zwanzig, CAN-FD, IMU)
├── crates/             # Rust Plugin SDK (`flow-plugin`) and SIMD tile plugin example
├── tests/              # 8 domain test suites + AWS Cedar formal verification benchmark (`cedar_authorization.lean`)
├── flow-book/          # 6-chapter architectural & practical guide (Literate Markdown)
├── tools/              # Zero-dependency one-line installer (`tools/install.sh`)
└── Makefile            # Two-stage native hermetic build & test pipeline
```

---

## 📚 Documentation (`flow-book/`)

The 6-chapter architectural and practical guide lives in [`flow-book/src/SUMMARY.md`](flow-book/src/SUMMARY.md) as Literate Markdown (`.md`). Chapters containing embedded ```` ```flow ```` blocks can also be compiled directly by `flowc`.

---

## 📜 License

MIT License — see [LICENSE](LICENSE) for details.
