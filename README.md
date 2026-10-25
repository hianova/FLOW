<p align="center">
  <img src="assets/banner.svg" alt="FLOW Banner" width="100%" />
</p>

# FLOW

> **Pure Lean 4 $\to$ C17 Mathematical Isomorphism Synthesizer, Cubical HoTT Type Sieve & Explicit Zero-Copy QSBR Runtime**

FLOW is a zero-dependency **Pure Lean 4 $\to$ C17** compiler and explicit C17 hard real-time runtime (`11` core C17 library modules + `flowc.c` + `flowy_main.c` in `src/` and `plugins/embodied.{h,c,lean}`).

Instead of inventing a third configuration or annotation DSL (`@quota`, `@embodied`, `@reload`, `@patchbay`, `.flow`), FLOW strictly separates **Pure Mathematical Truth (`.lean`)** from **Explicit Systems Engineering (`.c`)**:

- **`.lean` writes 100% Pure Lean 4 Math (Zero Annotations / Zero Config)**:
  Only standard Lean 4 `structure`, `inductive`, `def`, and `theorem` declarations.
- **`.c` writes 100% Explicit C17 Hardware & Pipeline Wiring**:
  Link against `libflow_rt.a` (`flow_spsc.h`, `flow_jet.h`, `reload.h`, `cubical_hott.h`, `polyhedral.h`) and configure SPSC ring buffers, QSBR hot-swap epochs, and symplectic contact jets directly in readable C17 code.

---

## 🏛️ The 4 Mathematical Isomorphisms (`flowc`: Lean 4 $\to$ C17 Emergence)

`flowc` synthesizes C17 operators and hardware-grade contracts from `.lean` via **4 pure mathematical isomorphisms**, with zero hardcoded domain nouns and zero fake pipeline templates:

```text
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                    FLOW: 4 Pure Mathematical Isomorphisms (Lean 4 -> C17)              │
├────────────────────────────────────────────────────────────────────────────────────────┤
│                                                                                        │
│  1️⃣ Product Types -> C17 Structs (`structure S`, `(A × B)`)                           │
│     • Erases 0-byte `Prop` proof fields (`h : x > 0`)                                  │
│     • Flattens nested structures (`PoseFrame.pos.x` -> `pos_x`) & tuple `<fn>_tuple_t` │
│     • Pure data fields naturally determine C17 ABI alignment & byte stride             │
│                                                                                        │
│  2️⃣ Sum Types -> C17 Enums & Zero-Branch FSM LUTs (`inductive E`)                     │
│     • Finite `inductive` types map 1-to-1 to C17 `typedef enum`                        │
│     • Pure `inductive` state transitions (`match s, ev with`) compile to zero-branch   │
│       C17 lookup tables (`static const uint8_t <fn>_transition_table[|S|][|E|]`)       │
│     • `Lean-RT` Zero-GC Gate rejects recursive inductives, `List`, `Array`, closures   │
│                                                                                        │
│  3️⃣ Pure Function Composition -> C17 Static Inline & 2-Jet AD Graph (`def`)           │
│     • `private def` -> internal C17 `static inline` helper / constant                  │
│     • `def f` -> Tri-Track C17 operators:                                              │
│         - IEEE-754 `double`: `f(...)` (DSO) & `flowc_f(...)` (`static inline` header)  │
│         - Exact Q32.32 `flow_fx64_t`: `f_fx(...)` & `flowc_f_fx(...)`                  │
│         - 2-Jet Forward AD `FlowJetScalar2`: `jet2_f(...)` & `flowc_jet2_f(...)`       │
│           computing `(f(x), f'(x), f''(x))` (Value, Delta, Gamma) via 2-Jet chain rule │
│     • `let x := rhs in` & `let (a, b) := g x in` lower to real C17 local variables     │
│       and direct C17 function calls (`g(x, &a, &b)`)                                   │
│                                                                                        │
│  4️⃣ Theorems -> C17 Hardware Contracts (`theorem` -> `lean_<fn>`)                     │
│     • Structural Domain Precondition (`structure` `Prop` fields & `def` proof params) │
│       -> `if (!(domain_ok)) return fallback;` (`FLOW_LEAN_PRE_GUARD`)                 │
│     • Theorem Hypotheses `P(x) -> Q(x, f(x))` -> Material Implication Guard:          │
│       `if ((P(x)) && !(Q(x, _ret))) return fallback;` (`FLOW_LEAN_IMPLIES_GUARD`)     │
│     • Local theorems (`v > 0`, `v < 0`) stack into a denser mesh without blocking!    │
│                                                                                        │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## ✨ Core Architectural Pillars

| Dimension | Traditional FFI / DSL Stack | FLOW Pure `.lean` + Explicit C17 |
| :--- | :--- | :--- |
| **Single Source of Math** | Separate YAML/DSL annotations + manual `@[export]` + C/C++ re-implementation | **100% Pure `.lean`** (`structure`, `inductive`, `def`, `theorem`) with **Zero `@[export]`**, **Zero `@` Config Injection**, and **Zero-Branch C17 FSM LUTs** (`<fn>_transition_table[|S|][|E|]`) |
| **Tri-Track Numerics & 2-Jet AD** | Lossy float/fixed mismatch & hand-written analytical derivatives / Greeks | **0-ULP IEEE-754 `double`** (`flowc_<fn>`) + **Deterministic $Q32.32$ Fixed-Point** (`flowc_<fn>_fx`) + **2-Jet Forward AD** (`flowc_jet2_<fn>` / `FlowJetScalar2` computing `(f, f', f'')`) + **256-Bit BigInt** (`FlowU256`) |
| **Theorem-Backed Hardware Contracts** | Detached proofs or manual runtime `assert()` | Structural domain preconditions (`FLOW_LEAN_PRE_GUARD`) + per-theorem **Material Implication Guards** (`P_k(x) -> Q_k(x, _ret)` via `FLOW_LEAN_IMPLIES_GUARD`) in `lean_<fn>()` / `flowc_lean_<fn>()` |
| **Explicit C17 Runtime (`libflow_rt.a`)** | Opaque compiler-generated pipeline boilerplate | Clean C17 primitives: lock-free **`flow_spsc.h`** ring buffers, 64B cache-line-aligned **`reload.h`** QSBR hot-swap (`< 100ns`), symplectic **`flow_jet.h`** contact manifolds, **`polyhedral.h`**, and **`cubical_hott.h`** |
| **Zero-`sorry` & `Lean-RT` Zero-GC Gate** | Unchecked axioms or hidden GC allocations | Rejects `sorry`, `admit`, `sorryAx`, `native_decide`, custom `axiom`, unclosed `/-` comments, `List`, `Array`, `ByteArray`, `++`, closures (`fun x =>`), and recursion at compile time |

---

## 💻 Quick Start & Usage

### 1. Build & Run All 8 C17 Domain Test Suites + Fuzz Harness

```sh
# Build libflow.a, libflow_rt.a, flowc, flowy, and libflow_embodied.so
make -j4 all

# Run all 8 native domain test suites (Brain, Body, Concurrency, Fvec-Swarm, System, Core, Axiom, HoTT), 1,000-mutation Fuzz & CLI smoke tests
make -j4 test
```

### 2. Author Pure Lean 4 Math & Synthesize C17 Operators

Create a pure Lean 4 specification (e.g. `control.lean`):

```lean
namespace Embodied.Control

structure PoseFrame where
  pos_x : Float
  vel_x : Float
  ticks : UInt64

private def half_dt : Float := 0.5

def bounded_gain (x : Float) (limit : Float) : Float :=
  if x > limit then limit else x

theorem bounded_gain_upper (x : Float) (limit : Float) (h_lim : limit > 0.0) :
  bounded_gain x limit <= limit := by decide

theorem bounded_gain_nonneg (x : Float) (limit : Float) :
  x >= 0.0 → bounded_gain x limit >= 0.0 := by decide

end Embodied.Control
```

Compile to C17 + static inline C17 header, or build a shared library directly:

```sh
# Emit C17 implementation + static inline C17 header (flowc_bounded_gain, flowc_bounded_gain_fx, flowc_lean_bounded_gain)
./build/flowc control.lean -o build/control.c --target-c-header build/control.h

# Emit C17 implementation + MATLAB / Simulink MEX dispatcher (`mexFunction`)
./build/flowc control.lean -o build/control_mex.c --target-c-header build/control.h --target-mex

# Or build build/libcontrol.so + build/control.h in one command
./build/flowy build --shared control.lean
```

### 3. MATLAB / Simulink Zero-Modification C Caller & Native MEX (`--target-mex`)

1. **Simulink `C Caller Block` & MATLAB `loadlibrary` / `clibgen` (Zero-Modification)**:
   Because `--target-c-header` emits a 100% pure C17 header (`typedef struct` with `Prop` proof fields erased and pure `double` / `flow_fx64_t` operators) and suppresses `int main(void)` in the `.c` file, `build/control.h` and `build/control.c` can be dragged directly into Simulink's **C Caller Block** for closed-loop SIL/PIL verification or loaded via `loadlibrary('libcontrol.so', 'control.h')`.
2. **High-Throughput Native MEX Dispatcher (`--target-mex`)**:
   For million-iteration Monte Carlo or historical tick backtesting in MATLAB, `--target-mex` appends a native C17 `mexFunction` dispatcher to the generated `.c` file:
   ```matlab
   % Compile in MATLAB (one line):
   mex -Iinclude build/control_mex.c

   % Invoke theorem-guarded or raw C17 operators at native speed:
   cmd = control_mex('lean_bounded_gain', 120.0, 80.0);

   % Or evaluate 2-Jet Forward AD (Value, 1st-order Delta, 2nd-order Gamma) in one call:
   [val, delta, gamma] = control_mex('jet2_bounded_gain', 60.0, 80.0);
   ```

### 4. Explicit C17 Pipeline Wiring (`libflow_rt.a`)

Wire your synthesized C17 operators with explicit C17 primitives (`flow_spsc.h`, `flow_jet.h`, `reload.h`):

```c
#include "flow_spsc.h"
#include "flow_jet.h"
#include "reload.h"
#include "control.h"
#include <assert.h>

FLOW_SPSC_DEFINE(CmdRing, double, 64)

int main(void) {
    CmdRing ring;
    CmdRing_init(&ring);

    /* Call theorem-guarded C17 operator (limit > 0.0 && x >= 0.0 => _ret <= limit && _ret >= 0.0) */
    double cmd = flowc_lean_bounded_gain(120.0, 80.0);
    assert(cmd == 80.0);

    /* Violating any theorem hypothesis triggers hardware-grade pre-guard -> safe fallback 0.0 */
    assert(flowc_lean_bounded_gain(-5.0, 80.0) == 0.0);

    assert(CmdRing_push(&ring, cmd) == 1);
    return 0;
}
```

### 5. Introspection & Formal Audit (`flowy`)

```sh
# Verify Cubical HoTT Universe U_0 univalence & QF_LIA SMT soundness
./build/flowy audit

# Inspect 64-Bit De Morgan Cubical Sieve (I^64) & Intensional Type Universe U_0
./build/flowy topos

# Query .fvec architectural memory vault or simulate .fjet symplectic phase space
./build/flowy fvec list
```

---

## 🗂️ Repository Layout

```text
FLOW/
├── src/
│   ├── flow.h / parser.c              # Pure Lean 4 -> SemanticIR parser, Zero-GC gate & theorem contract extractor
│   ├── backend.{h,c}                  # C17 / LLVM-IR / MLIR code synthesizer & dual-track (double / Q32.32 fx64) kernels
│   ├── abi.{h,c} / registry.{h,c}     # C17 header & Lean 4 certificate emitter, 64B FlowExtentDescriptor & plugin registry
│   ├── flow_spsc.h                    # Header-only C17 lock-free cache-line-aligned SPSC ring buffer primitive
│   ├── flow_jet.{h,c}                 # Symplectic contact manifold (.fjet), Koopman EDMD, hardware cycle & NUMA probes
│   ├── reload.{h,c}                   # Lock-free QSBR epoch hot-reload & mlock pre-faulted arena runtime
│   ├── cubical_hott.{h,c}             # 64-Bit Cubical HoTT Sieve (I^64), Kan filler & Univalence Universe U_0
│   ├── smt.{h,c} / polyhedral.{h,c}   # QF_LIA 4-Theorem SMT solver, capability security & Presburger loop tiling
│   ├── bitspace.{h,c}                 # 256-Bit Montgomery/Miller-Rabin (FlowU256), Q32.32 fixed-point & .fvec vault
│   └── flowy.{h,c} / flowc.c / flowy_main.c # Compiler CLI (flowc) & introspection / build assistant (flowy)
├── plugins/                           # Embodied reflex plugin & pure Lean 4 specification (embodied.{h,c,lean})
├── tests/                             # 8 domain test suites, 1,000-mutation fuzz harness & AWS Cedar benchmark (cedar_authorization.lean)
├── flow-book/                         # Architectural & practical guide
├── tools/                             # Zero-dependency one-line C17 installer (tools/install.sh)
└── Makefile                           # Native C17 build, sanitizer (ASan/TSan) & hermetic test pipeline
```

---

## 📜 License

MIT License — see [LICENSE](LICENSE) for details.
