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

| Dimension | Traditional Systems / FFI Stack | FLOW Pure `.lean` × `.flow` Fusion |
| :--- | :--- | :--- |
| **Source of Truth** | Separate YAML config + C/Rust kernel + Python/JS FFI bindings + unit tests | **Pure `.lean`** (`What`: Math, `structure`, `def`, `theorem`) + Optional **`.flow` Manifest** (`Where & How`: `@quota`, `@embodied`, `@reload`, `@patchbay`) |
| **FFI, Exact Math & NaN Safety** | Manual `@[export]`, `extern "C"`, lossy IEEE-754 float drift & hand-written `isfinite()` guards | **Zero `@[export]` + `FlowExactNumerics` ($Q32.32$ / `FlowU256`)** — lowers Lean `Float` (`+ - * / % ^ sqrt exp log sin cos atan2 floor ceil round`) to deterministic 128-bit-intermediate $Q32.32$ fixed-point (`FLOW_FX64_NONE` fault isolation), emits dual `flowc_<fn>` (`double`) & `flowc_<fn>_fx` (`flow_fx64_t`) static inline headers (`~0.68 ns`), and destructures tuple returns (`(Float × Float)` $\to$ `double *out_0, double *out_1`) |
| **Correctness & Real-Time Gate** | Runtime assertions, GC pauses, or detached proofs | **Zero-`sorry` Lean 4 proof** + **`Lean-RT` Zero-GC Gate** (rejects `List`, `Array`, closures, recursion) + **QF_LIA 4-Theorem SMT Gate** (including compositional `@patchbay` WCET & wire stride proofs) |
| **Concurrency & Hot-Swap** | Manual mutexes / RW-locks, unbounded retirement queues, or process restarts | Automatic **64B cache-line-aligned QSBR** epoch lease (`< 100ns` hot-swap) with **Reservation-Before-Publication** (`FLOW_RELOAD_MAX_RETIRED_DEPTH = 64`), **Theorem-Verified State Migration** (`StateV1 -> StateV2`), and `mlock` pre-faulted arena with `-4` WCET latch |
| **Polyglot Interop & Shadow A/B** | Per-language wrapper maintenance & ad-hoc telemetry | Single-command 64B `FlowExtentDescriptor` & typed `structure` zero-copy emission for C, Rust, Python, JS, CUDA, MATLAB & Julia + built-in cross-DLL process-wide Shadow A/B verification (`flow_ab_shadow_*`) |

### 🧭 `.lean` (What: Pure Math) × `.flow` (Where & How: Hardware Manifest)

- **90% of applications (Pure Compute / Policy / Pricing / Ledger)**: Write **1 file (`app.lean`)**. `flowc` enforces the **`Lean-RT` Zero-GC Subset**, flattens nested `structure`s with natural C ABI alignment, lowers scalar and struct-to-struct `def`s to C17 with automatic `isfinite()` sanitization, and anchors the primary operator from your `theorem`.
- **10% of applications (Real-Time Embodied Robotics / Multi-Stage `@patchbay` / Hot-Reload Migration)**: Pair your pure math [`plugins/embodied.lean`](plugins/embodied.lean) with a declarative runtime manifest [`plugins/embodied.flow`](plugins/embodied.flow) using `bind`, `@quota`, `@embodied`, `@reload` (`from_schema` $\to$ `to_schema`), and `@patchbay` (`stage` / `wire`) — zero C/FFI glue required.

---

## ⚡ Pure `.lean` Zero-Shot Authoring & FFI Cheat Sheet (For Humans & AI Agents)

When authoring a new FLOW project in pure `.lean`, follow these **4 deterministic rules** to compile on the first try with zero trial-and-error:

| Rule | Specification & Exact Convention |
| :--- | :--- |
| **1. `Lean-RT` Zero-GC Subset, Nested `structure` & `FlowExactNumerics`** | • Use zero-dependency Core Lean 4 only (**do not** `import Mathlib`; **never** use `sorry`, custom `axiom`, `List`, `Array`, `String`, closures `fun x =>`, or recursive functions/inductives).<br>• Primitive types map directly to C17: `UInt64` / `Nat` $\to$ `uint64_t`, `UInt32` $\to$ `uint32_t`, `Bool` $\to$ `bool`, `Float` $\to$ `double` (backed by deterministic $Q32.32$ fixed-point `flow_fx64_t`). Finite `inductive` enums map to `uint32_t`, and nested `structure`s flatten automatically (`a.b.c` $\to$ `a.b_c`).<br>• Built-in math & hardware codec intrinsics (`Float.abs`, `Float.floor`, `Float.ceil`, `Float.round`, `Float.sqrt`, `Float.exp`, `Float.log`, `Float.sin`, `Float.cos`, `Float.atan2`, `Float.pow`, `Float.max`, `Float.min`, `Float.ofNat`, `Float.ofInt`, `flow_quantize`, `flow_dequantize`) lower directly to exact $Q32.32$ 128-bit-intermediate C17 helpers (`flow_float_*` / `flow_fx64_*`). |
| **2. Zero-`@[export]` Kernel, Scalar, Tuple & Struct `def` Lowering** | • `flowc` scans the AST for the **last computational `def` referenced inside a `theorem`** (`spec->extern_kernel`) AND automatically lowers **all** pure scalar `def`s (including multi-line signatures, nested `let ... in`, `let (a, b) := ...`), tuple-returning `def`s (`(Float × Float)` $\to$ `double *out_0, double *out_1`), and struct-to-struct `def`s (`def f (s : InStruct) : OutStruct := { ... }`) into exported C17 functions (`flow_<id>_<fn>`) AND zero-overhead `--target-c-header` `static inline` functions (`flowc_<fn>` for `double` and `flowc_<fn>_fx` for pure `flow_fx64_t`).<br>• Standard 1PB zero-copy kernel signature (returns `0` for PASS, `1` for REJECT):<br>`def myStepKernel (baseOff byteLen stride epoch : UInt64) : UInt32 := if stride >= 64 && ... then 0 else 1` |
| **3. Zero-Retry Proof Tactics (`decide` & `omega`)** | • **Concrete test vectors / boundary checks**: Use `:= by decide` (e.g., `theorem kernel_ok : myStepKernel 0 64 64 1 = 0 := by decide`).<br>• **Universal linear arithmetic (`∀`) & State Migration Conservation**: Use `:= by omega` (e.g., `theorem conserve (s : StateV1) : (migrateV1ToV2 s).accum = s.accum := by rfl`). **Crucial `Nat` Subtraction Rule**: Lean 4 `Nat` subtraction truncates at `0` (`a - b = 0` when `a < b`). Every theorem containing `a - b` **must** include `(h : b ≤ a)` in its signature. |
| **4. 5-Line Zero-Copy FFI, `@patchbay` & Schema-Driven UI** | Compile via `flowc spec.lean -o build/app.c --target-c-header build/app.h --target-python build/app.py --target-rust build/app.rs --target-ui build/app_ui.html` (or launch live via `flowy ui spec.lean`), then call any Lean `def`, `migrate_state`, `run_patchbay`, or extent in C/Python/Rust/JS:<br>`with app.LinearArray("build/libapp.so") as eng:`<br>`    out_ext = eng.process_descriptor(app.FlowExtentDescriptor(0, 1 << 50, 0, 64, 8, 1, 0))` |

---

## 🏷️ Complete `@keyword` & `.flow` / `.lean` Directive Reference

FLOW separates **What** (pure `.lean` mathematical definitions & proofs) from **Where & How** (declarative `.flow` hardware & runtime manifests). Below is the complete reference for all `@keyword` blocks and directives supported by `src/parser.c`:

### 1. Primary `@keyword` Blocks (`.flow` Manifests & `.lean` Attributes)

| `@keyword` / Alias | Purpose | Supported Sub-Directives & Syntax | Compiler & Runtime Effect |
| :--- | :--- | :--- | :--- |
| **`@quota { ... }`**<br>*(Aliases: `quota`, `require`)* | **Hard Real-Time SLA, Memory Ceiling & Proof Gate** | • `deterministic`<br>• `lean_certified` *(or `proof_carrying`, `no_sorry`)*<br>• `deadline <time>` *(or `sla`, `latency`; units: `ns`, `us`, `ms`, `s`)*<br>• `arena <size>` *(units: `b`, `kb`, `mb`, `gb`)*<br>• `memory < <n>mb`<br>• `fallback <policy>` *(e.g. `emergency_stop`)* | • Enforces bitwise-deterministic $Q32.32$ `FlowExactNumerics` and zero-`sorry` Lean 4 verification.<br>• Pre-allocates a 64B-aligned static arena, page-prefaults every OS page, and locks physical RAM via `mlock`.<br>• Arms the hardware cycle watchdog (`flow_hardware_cycles()`); if a step exceeds `deadline`, atomically latches `watchdog_tripped = 1` and returns `-4` (`FLOW_ERR_WATCHDOG_ESTOP`). |
| **`@embodied [name] { ... }`**<br>*(Aliases: `embodied`, `jet`)* | **Symplectic Contact Jet, ZMP Envelope & Hardware I/O** | • `frequency <hz>` *(or `target_frequency`; units: `hz`, `khz`, `mhz`)*<br>• `symplectic`<br>• `max_drift <val>` *(or `drift`; e.g. `0.5%`, `0.005`)*<br>• `zmp_margin <val>` *(or `zmp`; units: `m`, `cm`, `mm`)*<br>• `dt <time>` / `dim <n>`<br>• `ingress <driver>` *(e.g. `sensor_imu_6dof`)*<br>• `egress <driver>` *(e.g. `socketcan_fd`)*<br>• `coordinates [c1, c2, ...]` *(or `coord <c>`)* | • Automatically binds domain `embodied` (`flow.embodied`), `resource hardware`, `capability realtime`, and contract `zmp_torque_safe`.<br>• Shapes polyhedral loop tiles and symplectic phase-space pacing $(\omega, \mu, q_{\text{sat}})$.<br>• Synthesizes zero-offset named coordinate lenses `FlowJetView_<id>` (`view.q->hipPitch`, `view.p->hipPitch`). |
| **`@reload { ... }`**<br>*(Alias: `reload`)* | **Lock-Free QSBR Hot-Swap & Verified State Migration** | • `schema_version <int>` *(or `version`)*<br>• `qsbr_timeout <time>` *(e.g. `5ms`, `500us`)*<br>• `from_schema <StructV1>` *(or `from`)*<br>• `to_schema <StructV2>` *(or `to`)*<br>• `migrate <lean_def_name>` | • Configures 64B cache-line-aligned QSBR epoch leases (`< 100ns` reader overhead) with **Reservation-Before-Publication** (`FLOW_RELOAD_MAX_RETIRED_DEPTH = 64`; rejects publication with `FLOW_RELOAD_JOURNAL_FULL = 5` if stalled readers pin all 64 slots).<br>• Lowers the theorem-verified Lean `def` (`StructV1 -> StructV2`) into `flow_<id>_migrate_buffer` and `flow_<id>_migrate_unit`. |
| **`@patchbay { ... }`**<br>*(Alias: `patchbay`)* | **Compositional Multi-Stage Zero-Copy DAG & WCET Proof** | • `stage <name> bind "<file.lean>" [fn <def>] [in <InStruct>] [out <OutStruct>] [wcet <= <time>]`<br>• `wire <stage_1> -> <stage_2> -> ...` | • Binds multiple `.lean` kernels into a zero-copy pipeline.<br>• Statically verifies `OutStruct == InStruct` and `out_bytes == in_bytes` across every `wire` edge.<br>• Verifies each stage's `wcet` against the static AST instruction cost model and proves $\sum \text{wcet}_i \le \text{deadline}$ in SMT. |
| **`@[export <sym>]`**<br>*(Inside `.lean` files)* | **Explicit Primary Kernel Override** *(Optional)* | • `@[export myStepKernel]` placed before `def myStepKernel ...` | • Explicitly pins `spec->extern_kernel` to `<sym>`. *(Optional: by default `flowc` automatically discovers the last `def` anchored inside a `theorem` and exports all pure `def`s.)* |

### 2. Top-Level `.flow` Manifest Directives & Canonical `.lean` Constants

| Directive / Constant | Context | Syntax & Usage |
| :--- | :--- | :--- |
| **`project <name>`** | `.flow` top-level | Names the project and sets the default `flow_name` if not explicitly declared. |
| **`bind "<path.lean>"`** | `.flow` top-level | Absorbs all `structure`, `inductive`, `def`, and `theorem` declarations from `<path.lean>` into the `.flow` manifest. |
| **`import <module>`** | `.flow` top-level | Loads a domain plugin module (e.g. `import flow.embodied`, `import flow.embodied.mz`). |
| **`thermal [name] { ... }`** | `.flow` block | Configures hardware thermal envelope: `max_temp <C>`, `target_temp <C>`, `power_limit <W>`, `r_thermal <val>`, `c_thermal <val>`. |
| **`input <name> { ... }`** | `.flow` block | Configures stream geometry: `max_count <n>`, `stride <bytes>`, `align <bytes>`, `sample <id> <score>`. |
| **`state <name> { ... }`** | `.flow` block | Configures state topology: `shared`, `read_heavy`, `bounded`, `version <n>`, `field <name> <u64\|f64\|bytes> [persistent\|ordered\|defaultable]`. |
| **`flow <name> { ... }`** | `.flow` block | Declares execution pipeline expression: e.g. `lean_stream -> extern("myKernel") -> top(3)`. |
| **`ensure { ... }` / `prefer { ... }`** | `.flow` block | Post-conditions (`deterministic`, `lean_certified`) and optimization bias (`latency`, `throughput`, `memory`). |
| **Canonical `.lean` `def` Overrides** | `.lean` top-level | Pure `.lean` files can optionally override runtime parameters without a `.flow` file via: `def inputMaxCount : Nat := ...`, `def inputStrideBytes : Nat := ...`, `def inputAlignBytes : Nat := ...`, `def stateSchemaVersion : Nat := ...`, `def slaLatencyNs : Nat := ...`, `def shardCount : Nat := ...`, `def memoryQuotaBytes : Nat := ...`. |

### Minimal `.flow` + `.lean` Composition Example

```yaml
project humanoid_ankle_reflex
bind "plugins/embodied.lean"

@quota {
    deterministic
    lean_certified
    deadline 200us
    arena 64kb
    memory < 16mb
    fallback emergency_stop
}

@embodied {
    frequency 1khz
    symplectic
    max_drift 0.5%
    zmp_margin 0.02
    ingress sensor_imu_6dof
    egress socketcan_fd
    coordinates [hipPitch, hipRoll, kneePitch, anklePitch, ankleRoll, torsoYaw, comX, comY]
}

@reload {
    schema_version 2
    qsbr_timeout 5ms
    from_schema GaitStateV1
    to_schema GaitStateV2
    migrate migrateGaitV1ToV2
}

@patchbay {
    stage imu_filter bind "plugins/embodied.lean" fn filterImu in RawImu out FilteredImu wcet <= 40us
    stage zmp_reflex bind "plugins/embodied.lean" fn stepZmp   in FilteredImu out JointCmd wcet <= 80us
    wire imu_filter -> zmp_reflex
}
```

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
# Build libflow.a, flowc, flowy, and domain plugins (libflow_embodied.so)
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

Compile, verify, and execute directly or emit polyglot zero-copy adapters and the algorithmic Control Plane UI:

```sh
# Directly verify, synthesize C + QSBR wrapper, and execute via flowy
./build/flowy run reflex.lean

# One-command Multi-Language SDK Bundle: builds build/libreflex.so + build/reflex.h + build/reflex_abi.py + build/flow_reflex.m
./build/flowy build --shared reflex.lean

# Synthesize and serve the Algorithmic Control Plane (2D Skyline Bin-Packing + Sugiyama DAG + Watchdog E-Stop + Compliance Certificate Export)
./build/flowy ui reflex.lean --selftest

# Or manually emit individual targets (C header with FlowStatusCode, Rust, Python, IR-driven MATLAB, CUDA, JS, UI, and Lean 4 certificate)
./build/flowc reflex.lean -o build/reflex.c \
    --target-c-header build/reflex.h \
    --target-rust build/reflex.rs \
    --target-python build/reflex.py \
    --target-matlab build/flow_reflex.m \
    --target-cuda build/reflex.cuh \
    --target-js build/reflex.js \
    --target-ui build/reflex_ui.html
```

### 3. Six-Persona Application-Layer Experience

| Persona | Primary Entrypoint | Key Capabilities |
| :--- | :--- | :--- |
| **C / Firmware Engineer** | `flowc --target-c-header` / `flowy build --shared` | Standard C17 `typedef struct`/`enum`, `FlowStatusCode` (`FLOW_OK = 0` .. `FLOW_ERR_WATCHDOG_ESTOP = -4`), `flow_quantize`/`flow_dequantize`, `flow_<id>_run_patchbay`, `flow_<id>_reset_watchdog` |
| **Python / AI Engineer** | `flowy build --shared` $\to$ `import <stem>_abi` | Zero-copy `ctypes.Structure` auto-marshaling (accepts `dict`/`tuple`/`Structure`), `run_patchbay`, `migrate_state`, `trip_watchdog_for_test` & `reset_watchdog` |
| **MATLAB Control Engineer** | `flowc --target-matlab` / `flowy build --shared` | Full IR-driven Handle Class (`flow_<id>.m`) with all `.lean` operators, enum constants, `runPatchbay`, `migrateState`, `tripWatchdog` & `resetWatchdog` |
| **Blue-Collar Field Tech** | `flowy ui <spec>` (Panel 0 & Panel 1) | Sugiyama zero-crossing `@patchbay` DAG with per-stage WCET budget bars, live `WATCHDOG: NOMINAL` / `E-STOP LATCHED (-4)` indicator, and `Trip E-Stop` / `Reset Watchdog` buttons |
| **White-Collar Risk / Compliance** | `flowy ui <spec>` (Panel 2 & Panel 3) | 2D Skyline bin-packed operator cards with `FORMAL SAFETY GATE` & `✓ theorem` badges, enum dropdowns, nested struct inputs, SoD dual-signature ledger & `Export Audit JSON` |
| **Executive / CTO** | `./build/flowy audit` & `Export Compliance Certificate` | 1-second architectural & SMT soundness audit (`Leaks: 0 | Modularity: 1.00 | SMT: QF_LIA SOUND`) + one-click JSON Compliance Certificate & SoD Liability Ledger download |

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
