<p align="center">
  <img src="assets/banner.svg" alt="FLOW Banner" width="100%" />
</p>

# FLOW

> **Pure `.lean` Mathematical-AST Fusion Engine, Cubical HoTT Type Sieve & Zero-Copy QSBR Runtime**

FLOW fuses **Domain Intent**, **Formal Proof**, and **Native Hardware Execution** into a single compact C17 & Rust engine (`12` core C17 library modules + `flowc.c` + `flowy_main.c` in `src/`, `plugins/embodied.{h,c,lean,flow}`, and `crates/flow-plugin*`, organized into **7 Component Scopes** with `Cross-Layer Leaks = 0` and `Modularity = 1.00`). Instead of maintaining separate configuration files, C/C++ kernels, FFI glue (`@[export]` / `extern "C"`), and post-hoc unit tests, developers or AI agents provide a single idiomatic **Lean 4 (`.lean`)** or declarative **`.flow`** specification. FLOW automatically infers memory layouts from native `structure` types, anchors compute kernels directly from `theorem` proofs, verifies cross-language structural isomorphism in the **Intensional Global Type Universe $\mathcal{U}_0$** (`FlowHoTTUniverse`), and wraps the synthesized C/LLVM operator in a lock-free **QSBR** epoch runtime with zero-copy adapters for **C, Rust, Python, JS/Bun, CUDA, MATLAB, and Julia**.

```text
┌────────────────────────────────────────────────────────────────────────────────────────┐
│               FLOW Three-Layer Mathematical-AST Fusion (7 Component Scopes)            │
├────────────────────────────────────────────────────────────────────────────────────────┤
│                                                                                        │
│   1️⃣【Pure .lean / .flow Intent & Proof — Single Source of Truth (`frontend`)】         │
│      • Native `structure`   : Auto-infers stride, alignment & hot-reload schema        │
│      • Pure `def` + `theorem`: Theorem-anchored kernel discovery (Zero @[export] FFI)  │
│      • Zero-sorry hard gate : Rejects any `sorry` or custom `axiom` at parse/audit time│
│                                                                                        │
│   2️⃣【Cubical HoTT Universe U_0, Exact Manifold & Topology Radar (`formal`/`exact`)】  │
│      • cubical_hott.c       : 64-Bit De Morgan Sieve & Univalence Gate U_0 (A ~= B)    │
│      • bitspace.c           : Q32.32 / 256-Bit Montgomery, .fvec & 5-Stage Token Ring  │
│      • smt.c / polyhedral.c : QF_LIA 4-Theorem Supreme Court & Presburger loop tiling  │
│      • topology.c           : Exogenous 7-Scope Radar & Probe Weaver (Leaks = 0)       │
│                                                                                        │
│   3️⃣【QSBR Zero-Copy Substrate, Embodied Jet & 7-Lang Projector (`runtime`/`abi`)】    │
│      • backend.c / reload.c : Native C17 codegen, In-Memory JIT & lock-free QSBR lease │
│      • flow_jet.c/embodied.c: Symplectic .fjet, 1kHz ZMP, 10kHz Mori-Zwanzig, CAN/IMU  │
│      • abi.c / registry.c   : 64B FlowExtentDescriptor across C/Rust/Py/JS/CUDA/M/Jl   │
│                                                                                        │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## ✨ Why FLOW? (Core Architectural Pillars & Cost-Benefit)

| Dimension | Traditional Systems / FFI Stack | FLOW Pure `.lean` × `.flow` Fusion |
| :--- | :--- | :--- |
| **Source of Truth** | Separate YAML config + C/Rust kernel + Python/JS FFI bindings + unit tests | **Pure `.lean`** (`What`: Math, `structure`, `def`, `theorem`) + Optional **`.flow` Manifest** (`Where & How`: `@quota`, `@embodied`, `@reload`, `@patchbay`) |
| **FFI, Dual-Track Numerics & NaN Safety** | Manual `@[export]`, `extern "C"`, lossy float/fixed mismatch & hand-written `isfinite()` guards | **Zero `@[export]` + Dual-Track 0-ULP IEEE-754 `double` & $Q32.32$ `FlowExactNumerics`** — lowers Lean `Float` / `ℝ` (`+ - * / % ^ sqrt exp log sin cos atan2 floor ceil round f_abs f_max f_min`) into both **0-ULP native IEEE-754 `double`** (`flowc_<fn>` / `flow_<id>_<fn>`, full $10^{\pm 308}$ dynamic range) and **deterministic 128-bit-intermediate $Q32.32$ fixed-point** (`flowc_<fn>_fx` / `flow_fx64_t` with `FLOW_FX64_NONE` fault isolation) + 256-bit Montgomery/Miller-Rabin BigInt (`secp256k1` verified), automatically erases zero-byte `Prop` proof fields (`h : x > 0`), and destructures tuple returns (`(Float × Float)` $\to$ `double *out_0, double *out_1`) |
| **Correctness & Univalence Gate** | Runtime assertions, GC pauses, detached proofs, or silent FFI layout drift | **Zero-`sorry` Lean 4 proof** + **`Lean-RT` Zero-GC Gate** + **QF_LIA 4-Theorem SMT Gate** + **HoTT Univalence Customs $\mathcal{U}_0$** (`A ≃ B ⇒ A = B` across Lean 4, C17, Rust & Polyglot ABI) |
| **Concurrency & Hot-Swap** | Manual mutexes / RW-locks, unbounded retirement queues, or process restarts | Automatic **64B cache-line-aligned QSBR** epoch lease (`< 100ns` hot-swap) with **Reservation-Before-Publication** (`FLOW_RELOAD_MAX_RETIRED_DEPTH = 64`), **Theorem-Verified State Migration** (`StateV1 -> StateV2`), and `mlock` pre-faulted arena with `-4` WCET latch |
| **7-Language Polyglot & Rust SDK** | Per-language wrapper maintenance & ad-hoc telemetry | Single-command 64B `FlowExtentDescriptor` & typed `structure` zero-copy emission for **C17, Rust, Python, JS/Bun, CUDA, MATLAB & Julia** (with `USize` mapping & reserved-keyword escaping `r#in` / `in_`), `#[repr(C)]` Rust Plugin SDK (`crates/flow-plugin`), and cross-DLL Shadow A/B verification (`flow_ab_shadow_*`) |

### 🧭 `.lean` (What: Pure Math) × `.flow` (Where & How: Hardware Manifest)

- **90% of applications (Pure Compute / Policy / Pricing / Ledger)**: Write **1 file (`app.lean`)**. `flowc` enforces the **`Lean-RT` Zero-GC Subset**, flattens nested `structure`s with natural C ABI alignment, lowers scalar and struct-to-struct `def`s to C17 with automatic `isfinite()` sanitization, and anchors the primary operator from your `theorem`.
- **10% of applications (Real-Time Embodied Robotics / Multi-Stage `@patchbay` / Hot-Reload Migration)**: Pair your pure math [`plugins/embodied.lean`](plugins/embodied.lean) with a declarative runtime manifest [`plugins/embodied.flow`](plugins/embodied.flow) using `bind`, `@quota`, `@embodied`, `@reload` (`from_schema` $\to$ `to_schema`), and `@patchbay` (`stage` / `wire`) — zero C/FFI glue required.

---

## ⚡ Pure `.lean` Zero-Shot Authoring & FFI Cheat Sheet (For Humans & AI Agents)

When authoring a new FLOW project in pure `.lean`, follow these **4 deterministic rules** to compile on the first try with zero trial-and-error:

| Rule | Specification & Exact Convention |
| :--- | :--- |
| **1. `Lean-RT` Zero-GC Subset, Proof Erasure & Dual-Track Numerics** | • Use zero-dependency Core Lean 4 (`noncomputable section` / `noncomputable def` supported; **never** use `sorry`, custom `axiom`, `List`, `Array`, `String`, closures `fun x =>`, or recursive functions/inductives). Supports up to `32` parameters/fields, `64` functions, and `128` theorems per specification.<br>• Primitive types map directly across all 7 languages: `UInt64` / `Nat` $\to$ `uint64_t`, `USize` $\to$ `size_t` / `usize` / `ctypes.c_size_t` / `Csize_t`, `UInt32` $\to$ `uint32_t`, `Bool` $\to$ `bool`, `Float` / `ℝ` / `Real` $\to$ `double` (with dual-track 0-ULP IEEE-754 `double` default entrypoints and `_fx` deterministic $Q32.32$ fixed-point `flow_fx64_t` entrypoints). Proof fields and proof parameters (`h_spot : spot > 0`, `h_prop : Prop`) are **automatically erased to 0 bytes** in C `struct`s and ABI signatures. Finite `inductive` enums map to `uint32_t`, nested `structure`s flatten automatically (`a.b.c` $\to$ `a.b_c`), and language keywords (`in`, `type`, `from`, `var`) are automatically escaped (`r#in`, `in_`).<br>• Built-in math & hardware codec intrinsics (`Float.abs`/`f_abs`, `Float.floor`, `Float.ceil`, `Float.round`, `Float.sqrt`, `Float.exp`, `Float.log`, `Float.sin`, `Float.cos`, `Float.atan2`, `Float.pow`, `Float.max`/`f_max`, `Float.min`/`f_min`, `Float.ofNat`, `Float.ofInt`, `Real.*`, `flow_quantize`, `flow_dequantize`) lower directly to 0-ULP IEEE-754 `double` (`flow_float_*`) and exact $Q32.32$ 128-bit-intermediate C17 helpers (`flow_fx64_*`). |
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
| **`import <module>`** | `.flow` top-level | Loads a domain or Rust plugin module (e.g. `import flow.embodied`, `import rust_simd_plugin`). |
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

## 🏛️ 7 Component Scopes & `topo` × `HoTT` Dual-Core Architecture

FLOW partitions its entire C17, Lean 4 / `.flow`, and Rust codebase (`30 Nodes | 0 Cross-Layer Leaks | Modularity = 1.00`) into **7 orthogonal Component Scopes** (`FlowComponentScope`), guarded by a dual-core self-bootstrapping auditor:

| Scope ID | Scope Name | Canonical Modules & Crates | Consolidated Subsystems & Formal Contract |
| :---: | :--- | :--- | :--- |
| **`[0]`** | **`frontend`** | `src/flow.h`, `src/parser.c`, `plugins/embodied.{lean,flow}`, `tests/cedar_authorization.lean` | **Lean 4 / `.flow` Specification Parser, AST & `SemanticIR` Lowering** (plus hardware cycle probes & NUMA topology in `flow.h`). |
| **`[1]`** | **`formal_sieve`** | `src/cubical_hott.{h,c}`, `src/smt.{h,c}`, `src/polyhedral.{h,c}` | **Cubical HoTT Type Sieve ($\mathbb{I}^{64}$), Intensional Type Universe $\mathcal{U}_0$, QF_LIA SMT Solver, Polyhedral & Capability Security** (absorbs `security.{h,c}`). |
| **`[2]`** | **`exact_manifold`** | `src/bitspace.{h,c}` | **256-Bit Montgomery/Miller-Rabin (`FlowU256`), $Q32.32$ Exact Numerics (`flow_fx64_t`), Bit-Manifold, `.fvec` Vault & 5-Stage Token Ring** (absorbs `flowy_fvec.{h,c}` & `token_ring.{h,c}`). |
| **`[3]`** | **`runtime_jet`** | `src/flow_jet.{h,c}`, `src/reload.{h,c}`, `src/backend.{h,c}`, `plugins/embodied.{h,c}` | **C17 Codegen Backend, In-Memory JIT, QSBR Hot-Reload, Symplectic `.fjet` & Embodied Drivers** (absorbs `jit.{h,c}`, `hardware_telemetry.{h,c}`, `flow_embodied_mz.{h,c}`, `driver_imu.{h,c}` & `driver_can.{h,c}`). |
| **`[4]`** | **`polyglot_abi`** | `src/registry.{h,c}`, `src/abi.{h,c}`, `src/plugin.h` + 8 built-in components | **7-Language Polyglot ABI Synthesizer (C17, Rust, Python, JS/Bun, CUDA, MATLAB, Julia) & Plugin Registry** (absorbs `builtin_plugin.c`). |
| **`[5]`** | **`introspection_ui`** | `src/topology.{h,c}`, `src/flowy.{h,c}`, `src/flowc.c`, `src/flowy_main.c` | **Living System Orchestrator, Exogenous Codebase Radar, Probe Weaver & `flowy` Algorithmic Control-Plane UI**. |
| **`[6]`** | **`rust_sdk`** | `Cargo.toml`, `crates/flow-plugin`, `crates/flow-plugin-example` | **Zero-Dependency Virtual Cargo Workspace & `#[repr(C)]` Rust Plugin SDK Crates**. |

### Dual-Core Self-Bootstrapping Division of Labor (`topology.c` × `cubical_hott.c`)

1. **`src/topology.c` — Exogenous Codebase Radar & Telemetry Probe Weaver (`topo`)**:
   - Scans the live C17/Lean/Rust AST `#include` and FFI dependency graph across all 7 Component Scopes, enforcing strict layer monotonicity (`Layer 0/1 Core` never depends on `Layer 2 Plugins` $\implies$ `Leaks = 0`).
   - Verifies **forward & reverse `Makefile` bijection** (`Makefile:BIJECTIVE`), **zero-dependency virtual Cargo workspace & `#[repr(C)]` Rust SDK parity** (`Rust-SDK:PARITY_OK`), **7-language ABI homomorphism** (`7-Lang-ABI:HOMOMORPHIC`), **runtime watchdog & Shadow A/B probe weaving** (`Probes:WOVEN`), and **256-bit `secp256k1` Montgomery + $Q32.32$ overflow self-bootstrapping** (`Exact-Numerics:SOUND`).
2. **`src/cubical_hott.c` — Endogenous Intensional Global Type Universe $\mathcal{U}_0$ & Univalence Gate (`HoTT`)**:
   - Implements `FlowHoTTUniverse` (`flow_hott_register_type`, `flow_hott_verify_univalence`) over the 64-bit De Morgan cubical complex $\mathbb{I}^{64}$.
   - Enforces **Voevodsky's Univalence Axiom ($A \simeq B \implies A = B$)** as a cross-language ABI customs gate (`HoTT-U0:UNIVALENT(7)`): every registered ABI and `.lean` type (`FlowPluginDescriptor`, `FlowPlugin`, `FlowUnit`, `FlowComponent`, `RegimeMode`, `Vec2`, `AuditFrame`) must preserve identical `field_count`, `byte_size`, `align_bytes`, `fx_scale_bits = 32`, and `sieve_fingerprint` across `Lean4`, `C17`, and `Rust`. Any layout mismatch triggers `FLOW_KAN_OBSTRUCTED_SINGULARITY` (`Homotopy Path Broken`).

---

## 💻 Quick Start & Usage

### 1. Build & Run All 8 C17 Domain Test Suites + Rust Workspace Tests

```sh
# Build libflow.a, flowc, flowy, and domain plugins (libflow_embodied.so)
make -j4 all

# Run all 8 native domain test suites (Brain, Body, Concurrency, Fvec-Swarm, System, Core, Axiom, HoTT) + CLI smoke tests
make -j4 test

# Run zero-dependency Rust Plugin SDK workspace unit tests
cargo test --workspace
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

# One-command 5-Artifact SDK Bundle: builds build/libreflex.so + build/reflex.h + build/reflex_abi.py + build/reflex_abi.rs + build/flow_reflex.m
./build/flowy build --shared reflex.lean

# Synthesize and serve the Algorithmic Control Plane (2D Skyline Bin-Packing + Sugiyama DAG + Watchdog E-Stop + Compliance Certificate Export)
./build/flowy ui reflex.lean --selftest

# Or manually emit all 7 language targets + HTML5 UI + Lean 4 certificate
./build/flowc reflex.lean -o build/reflex.c \
    --target-c-header build/reflex.h \
    --target-rust build/reflex.rs \
    --target-python build/reflex.py \
    --target-js build/reflex.mjs \
    --target-cuda build/reflex.cuh \
    --target-matlab build/flow_reflex.m \
    --target-julia build/reflex.jl \
    --target-ui build/reflex_ui.html
```

### 3. Six-Persona Application-Layer Experience

| Persona | Primary Entrypoint | Key Capabilities |
| :--- | :--- | :--- |
| **C / Firmware / Rust Engineer** | `flowc --target-c-header` / `--target-rust` / `flowy build --shared` | Standard C17 `typedef struct`/`enum` + static inline `flowc_<fn>`/`flowc_<fn>_fx`, `#[repr(C)]` Rust adapter, `FlowStatusCode` (`FLOW_OK = 0` .. `FLOW_ERR_WATCHDOG_ESTOP = -4`), `flow_quantize`/`flow_dequantize`, `run_patchbay`, `reset_watchdog` |
| **Python / AI Engineer** | `flowy build --shared` $\to$ `import <stem>_abi` | Zero-copy `ctypes.Structure` auto-marshaling (accepts `dict`/`tuple`/`Structure`), tuple returns, `_fx` exact fixed-point calls, `run_patchbay`, `migrate_state`, `trip_watchdog_for_test` & `reset_watchdog` |
| **MATLAB / Julia / CUDA / JS Engineer** | `flowc --target-matlab` / `--target-julia` / `--target-cuda` / `--target-js` | Full IR-driven MATLAB Handle Class (`flow_<id>.m`), Julia `ccall` + `@enum` module, CUDA host-launch & device extent header (`.cuh`), and Bun/Node.js `TypedArray` FFI module (`.mjs`) |
| **Blue-Collar Field Tech** | `flowy ui <spec>` (Panel 0 & Panel 1) | Sugiyama zero-crossing `@patchbay` DAG with per-stage WCET budget bars, live `WATCHDOG: NOMINAL` / `E-STOP LATCHED (-4)` indicator, and `Trip E-Stop` / `Reset Watchdog` buttons |
| **White-Collar Risk / Compliance** | `flowy ui <spec>` (Panel 2 & Panel 3) | 2D Skyline bin-packed operator cards with `FORMAL SAFETY GATE` & `✓ theorem` badges, enum dropdowns, nested struct inputs, SoD dual-signature ledger & `Export Audit JSON` |
| **Executive / CTO** | `./build/flowy audit` & `Export Compliance Certificate` | 1-second 7-Scope architectural, HoTT $\mathcal{U}_0$ univalence & SMT soundness audit (`Leaks: 0 | Modularity: 1.00 | HoTT-U0: UNIVALENT(7)`) + one-click JSON Compliance Certificate & SoD Liability Ledger download |

### 4. Real-World Production Blueprints & Benchmarks

| Blueprint / Repository | Domain & Problem Solved | Key Pure `.lean` Mechanisms |
| :--- | :--- | :--- |
| **[`tests/cedar_authorization.lean`](tests/cedar_authorization.lean)**<br>*(Built-in Benchmark)* | **Cloud IAM Policy Engine** (427 lines unmodified from [AWS `cedar-policy/cedar-spec`](https://github.com/cedar-policy/cedar-spec)) | Auto-infers 56B stride from `Request` + `Response`; anchors `isAuthorized` via `forbid_trumps_permit` & `default_deny`. Run via `./build/flowy run tests/cedar_authorization.lean`. |
| **[`hianova/Ricardian`](https://github.com/hianova/Ricardian)** | **Anti-Monopoly Macroeconomic Engine & Sovereign Settlement** (Ricardian differential rent, Harberger tax, double-entry ledger, Constant-Product AMM, Assembly Theory, 4-tier liquidity corridor, wash-trading filter, SEARU 1-bit chaos 3-SAT & Kardashev Era 3 ascension) | 64B `RicardianState`; 26 zero-`sorry` Lean 4 theorems anchoring `stepRicardian` across 1PB out-of-core extents (`0.10 µs` / step). |
| **[`hianova/flow_business`](https://github.com/hianova/flow_business)** | **Six-Pillar Enterprise Forensic Sidecar & Executive Ownership Ledger** (P2P, O2C discount anti-leakage, Treasury covenants, USMCA RVC, HR EEOC, Ad IVT) | Tri-State forensic gate (`AUTO_PASS` / `FORENSIC_HOLD` / `HARD_BLOCK`), Min-Cut counterfactual remedies ($\Delta Q$ or $\Delta B$), and SoD double-entry executive liability signatures. |

### 5. Live AST Topology Audit, 7 Component Scopes & HoTT Universe Introspection (`flowy`)

```sh
# 1. Run self-bootstrapping 7-scope codebase, polyglot homomorphism & HoTT U_0 univalence audit
./build/flowy audit

# 2. Inspect all 7 architectural Component Scopes (or filter by scope name, e.g. formal_sieve, rust_sdk)
./build/flowy scope
./build/flowy scope formal_sieve

# 3. Inspect the 64-Bit Cubical HoTT Sieve (I^64) & Intensional Global Type Universe U_0
./build/flowy topos

# 4. Query any module, scope, folded subsystem, or verified intent via Live AST + 5-Stage Token Ring
./build/flowy ask "cedar"
./build/flowy ask "univalence"
./build/flowy ask "mori_zwanzig"
./build/flowy inspect doc
```

---

## 🗂️ Repository Layout

```text
FLOW/
├── src/                # 12 core C17 library modules + flowc.c + flowy_main.c
│   ├── flow.h / parser.c              # [frontend] Lean 4 & .flow parser, SemanticIR, hardware cycle & NUMA probes
│   ├── cubical_hott.{h,c}             # [formal_sieve] 64-Bit Cubical HoTT Sieve (I^64) & Univalence Universe U_0
│   ├── smt.{h,c} / polyhedral.{h,c}   # [formal_sieve] QF_LIA 4-Theorem SMT solver, capability security & loop tiling
│   ├── bitspace.{h,c}                 # [exact_manifold] 256-Bit Montgomery, Q32.32 fixed-point, .fvec & Token Ring
│   ├── flow_jet.{h,c} / reload.{h,c}  # [runtime_jet] Symplectic .fjet, Koopman EDMD, In-Memory JIT & QSBR hot-reload
│   ├── backend.{h,c}                  # [runtime_jet] C17/LLVM/MLIR code synthesizer & pre-faulted mlock arena
│   ├── abi.{h,c} / registry.{h,c}     # [polyglot_abi] 7-language zero-copy ABI projector, UI & plugin registry
│   └── topology.{h,c} / flowy.{h,c}   # [introspection_ui] Exogenous 7-Scope Radar, Probe Weaver & flowy CLI/REPL
├── plugins/            # Unified embodied domain plugin & drivers (embodied.{h,c,lean,flow}: ZMP, 10kHz MZ, IMU, CAN-FD)
├── crates/             # Zero-dependency virtual Cargo workspace: Rust Plugin SDK (`flow-plugin`) & SIMD tile plugin (`flow-plugin-example`)
├── tests/              # 8 domain test suites + fuzz harness + AWS Cedar benchmark (`cedar_authorization.lean`)
├── flow-book/          # 6-chapter architectural & practical guide (Literate Markdown)
├── tools/              # Zero-dependency one-line installer (`tools/install.sh`)
└── Makefile            # Single source of truth C17 build, sanitizer (ASan/TSan) & test pipeline (verified by `flowy audit`)
```

---

## 📚 Documentation (`flow-book/`)

The 6-chapter architectural and practical guide lives in [`flow-book/src/SUMMARY.md`](flow-book/src/SUMMARY.md) as Literate Markdown (`.md`). Chapters containing embedded ```` ```flow ```` blocks can also be compiled directly by `flowc`.

---

## 📜 License

MIT License — see [LICENSE](LICENSE) for details.
