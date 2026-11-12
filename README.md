<p align="center">
  <img src="./assets/logo.svg?cb=20261112v5" alt="FLOW — Formally Verified L0 Integer, HoTT &amp; 2-Jet Substrate" width="100%" />
</p>

# FLOW — Object-Free HoTT & 2-Jet Manifold Execution Substrate

> **Software Engineering as Mathematics.**  
> **Zero C in Business Logic.** Zero objects (`struct`-free core). Zero control-flow graphs (`0` conditional branches, `0` SSA `phi` nodes, `0` VM opcodes). Zero floating-point (`FloatOps == 0`). Zero heap (`HeapCalls == 0`).

`FLOW` eliminates imperative application code. Downstream systems do **not** write `.c` files, do **not** invoke `lean -c`, and do **not** emit SSA control flow or bytecode instructions. Instead:

1. **Upstream (Pure Mathematics — `.lean` $\to$ `.flow`)**: Domain logic is authored and proved exclusively in **Lean 4** (`0 sorryAx`), where compile-time partial evaluation collapses multi-regime business rules, hysteresis deadbands, Cubical permission locks, and nonlinear equilibrium laws into a self-contained **`.flow` Manifold Atlas** (`1` initial `State` cell + $2^k$ `Chart` cells, `64 bytes` each).
2. **Runtime (Immutable Silicon Physics — `flow` / `flow.h`)**: `FLOW` owns the **single precompiled C17 execution engine** (`build/flow` CLI & bare-metal [`flow.h`](flow.h)). External runtime observations $(u_{\text{ext}}, m_{\text{req}})$ stream directly through the `.flow` Atlas via one branchless 512-bit register-vector endomorphism:

$$\Phi_{\text{atlas}} : \mathcal{C}_{64}^{\text{state}} \times \mathcal{A}_{2^k}^{\text{chart}} \times (u_{\text{ext}}, m_{\text{req}}) \longrightarrow \mathcal{C}_{64}^{\text{state}}$$

---

## 1. Architectural Axioms

```text
  Pure Domain Spec (.lean)
        │  Proves Chart invariants (0 sorryAx, 0 lean -c):
        │  • b_exit ≤ b_enter, dt_max > 0, NextLo/NextHi < 2^k, Ω safety
        ▼
  Manifold Atlas (.flow)    (1 + 2^k) × 64-byte cache-line cells
        │  • Cell 0        : Sealed initial State ("FLOWSTA5")
        │  • Cells 1..2^k  : Power-of-two Chart Arena ("FLOWBND5", epoch-invariant)
        ▼
  Universal Engine (flow)   Single fixed C17 binary (0 business C files downstream)
        │  • Streams live external inputs (u_ext, m_req) + @swap hot-reloads
        │  • Executes branchless HoTT chart gluing × 2-Jet solver in 8×u64 registers
        ▼
  Deterministic State Trajectory & Decisions
```

1. **Zero C in the Application Layer**:
   A `.flow` file is **not** a passive config file read by handwritten C logic; it **is** the complete business system after Lean partial evaluation. Changing business behavior means proving and hot-swapping a new `.flow` Atlas — never writing or recompiling C.
2. **HoTT Erases `if/else` State Machines & `struct` Objects**:
   By Univalence ($A \simeq B \implies A = B$), every state and chart shares one canonical carrier: a 64-byte hardware vector (`uint64_t __attribute__((vector_size(64), aligned(64)))`). Conditional business branching is replaced by **Schmitt Hysteresis Chart Gluing** over $\mathbb{Z}/2^k\mathbb{Z}$ (`idx & cap_mask`, zero out-of-bounds UB) and **Cubical Kan Projection** over an orthogonal 16-bit discrete mode space.
3. **2-Jet ($J^2$) Replaces Imperative Formula Trees**:
   On each active chart, external runtime signal $u_{\text{ext}}$ pulls back into the local 2-Jet residual $R(x, u_{\text{ext}}) = (b_0 - u_{\text{gain}} \cdot u_{\text{ext}}) + b_1 x + \frac{1}{2} b_2 x^2$. In one branchless pass, $\Phi_{\text{atlas}}$ resolves exact **Citardauq** roots when $\Delta \ge 0$ (escaping $R' = 0$ critical/saddle points where first-order methods stall) and **Wick-rotated curvature tunneling** ($\sqrt{|\Delta|}/|b_2|$) when $\Delta < 0$. Setting $u_{\text{gain}} = 0$ creates shock-isolated circuit-breaker charts.
4. **Dual-Seed Cryptographic Seals & Epoch-Invariant Hot-Swap**:
   [`Flow.lean`](Flow.lean) is the sole seal issuer (`"FLOWSTA5"` for states, `"FLOWBND5"` for charts; [`flow.h`](flow.h) exposes zero seal-minting functions and blocks seal laundering). Chart seals exclude dynamic `[63:32] Epoch`, allowing `flow_arena_publish_bound` to hot-swap live charts across threads without C re-signing.

---

## 2. Canonical 64-Byte Register Grading (`flow_cell`)

Continuous Q48.16 coordinates (`lane[3]`) and discrete Cubical topology (`lane[5]`) occupy strictly orthogonal lanes so discrete flags never pollute fixed-point carries:

| Lane | Bytes | State Cell ($\mathcal{C}_{64}^{\text{state}}$ — `"FLOWSTA5"`) | Chart Cell ($\mathcal{C}_{64}^{\text{chart}}$ — `"FLOWBND5"`) |
| :--- | :--- | :--- | :--- |
| `0` | `0..7` | `j0` — Q48.16 0-Jet (post-step residual) | `b0` — Base offset / equilibrium constant (Q48.16 `i64`) |
| `1` | `8..15` | `j1` — Q48.16 1-Jet (first-order tangent) | `b1` — First-order stiffness / slope (Q48.16 `i64`) |
| `2` | `16..23` | `j2` — Q48.16 2-Jet (second-order curvature) | `b2` — Second-order curvature (Q48.16 `i64`) |
| `3` | `24..31` | `x` — **Pure** Q48.16 continuous coordinate (`i64`) | `[b_exit:i32 \| b_enter:i32]` — Schmitt band (Q16.16, $b_{\text{exit}} \le b_{\text{enter}}$) |
| `4` | `32..39` | `dt` — Resolved step $\Delta t^* \ge 0$ (Q48.16 `i64`) | `[dt_max:u32 \| u_gain:i32]` — Horizon $\Delta t_{\max} > 0$ & $u_{\text{ext}}$ gain (Q16.16) |
| `5` | `40..47` | `[DomainTag:32 \| Chart:16 \| Mode:16]` | `[DomainTag:32 \| NextLo:8 \| NextHi:8 \| KanLock:16]` |
| `6` | `48..55` | `[Epoch:32 \| LiveSieve:32]` — Live $\Omega$ state | `[Epoch:32 \| ReqSieve:32]` — Required $\Omega$ mask |
| `7` | `56..63` | `seal` — Goldilocks $\mathbb{F}_p$ MAC (`lanes[0..6]`) | `seal` — Epoch-invariant Goldilocks $\mathbb{F}_p$ MAC |

### Grothendieck Sieve ($\Omega$) Covering Axes (`lane[6][31:0]`)

| Bit | Macro | Invariant |
| :--- | :--- | :--- |
| `0` | `FLOW_SIEVE_SEAL_VALID` | Both `state` (`FLOW_STATE_SEED`) and active `chart` (`FLOW_BOUND_SEED`) pass $\mathbb{F}_p$ verification |
| `1` | `FLOW_SIEVE_DOMAIN_MATCH` | State and active chart carry identical 32-bit Univalence `DomainTag` |
| `2` | `FLOW_SIEVE_NO_OVERFLOW` | Hardware ALU signed-overflow flag == `0` across all Q48.16 / 128-bit operations |
| `3` | `FLOW_SIEVE_KAN_HOMOTOPIC` | Requested discrete `Mode` transition preserves locked Cubical axes: `((mode ^ m_req) & kan_lock) == 0` |
| `4` | `FLOW_SIEVE_HORIZON_VALID` | Chart horizon satisfies $\Delta t_{\max} > 0$ and $0 \le \Delta t^* \le \Delta t_{\max}$ |
| `5` | `FLOW_SIEVE_CONVERGED` | Post-step residual lies within zero-locus tolerance ($\|r_0'\| \le 4$ LSB in Q48.16) |

---

## 3. Downstream Workflow (Zero C Files in Application)

A downstream project directory contains **only** `.lean` specification(s) and its build/test script — **never application `.c` files**.

### Step 1 — Prove & Emit `.flow` Manifold Atlas in Lean 4

```lean
import Flow

def tag : UInt32 := 0x52415445 -- "RATE"

-- 2-Chart (2^1) Surge Controller:
-- Chart 0 (Normal) : tracks u_ext linearly; jumps to Chart 1 if x' > 10.0
-- Chart 1 (Surge)  : convex square-root damping + locked audit bit 0x0001; returns to Chart 0 if x' < 2.0
def normalChart : Flow.Rule 1 where
  b0       := 0
  b1       := Flow.q16One
  b2       := 0
  bExit    := -100 * Flow.q16One
  bEnter   := 10 * Flow.q16One
  dtMax    := 10 * Flow.q16One
  uGain    := Flow.q16One
  domTag   := tag
  nextLo   := 0
  nextHi   := 1
  kanLock  := 0

def surgeChart : Flow.Rule 1 where
  b0       := 0
  b1       := 0
  b2       := 2 * Flow.q16One
  bExit    := 2 * Flow.q16One
  bEnter   := 50 * Flow.q16One
  dtMax    := 10 * Flow.q16One
  uGain    := Flow.q16One
  domTag   := tag
  nextLo   := 0
  nextHi   := 1
  kanLock  := 0x0001

def main : IO UInt32 := do
  let s0 := Flow.mintState normalChart 0 (initChart := 0) (initMode := 0x0001)
  Flow.emitBundle "surge.flow" 1 s0 [normalChart, surgeChart] normalChart
  return 0
```

Compile and emit the binary `.flow` Atlas (zero `lean -c`):

```bash
LEAN_PATH=/path/to/FLOW/build lean -DwarningAsError=true --run SurgeSpec.lean
```

### Step 2 — Verify & Stream Live Events Through `FLOW`'s Fixed Engine

```bash
# Cryptographically verify seals, DomainTag univalence, and 2^k Atlas geometry
/path/to/FLOW/build/flow verify surge.flow

# Stream live (u_ext [mode_hex]) observations or @swap hot-reload directives via stdin
printf "4.0\n12.0\n9.0 0x0000\n1.0\n" | /path/to/FLOW/build/flow run surge.flow
```

- `4.0`: Chart `0` moves $x \to 4.0$.
- `12.0`: Moves $x \to 12.0 > b_{\text{enter}} = 10.0 \implies$ branchlessly transitions to Chart `1` (Surge convex damping).
- `9.0 0x0000`: Settles $x \to \sqrt{9} = 3.0 \in [2.0, 50.0]$ Schmitt deadband $\implies$ stays on Chart `1` without flapping; `kan_lock = 0x0001` preserves locked audit bit `0x0001` despite `0x0000` request.
- `1.0`: Settles $x \to \sqrt{1} = 1.0 < b_{\text{exit}} = 2.0 \implies$ branchlessly returns to Chart `0`.

### Bare-Metal / Ring-0 Silicon Embedding (Driver / Kernel Plumbing Only)

[`flow.h`](flow.h) is included directly **only** when wiring raw hardware registers, DMA rings, or kernel interrupts in `-ffreestanding -nostdlib` environments:

```c
#include "flow.h"

/* Pure physical transport: 0 domain constants, 0 structs, 0 conditional branches */
state = flow_step_atlas(state, chart_arena, cap_mask, u_ext_q16, mode_req_u16);
```

---

## 4. Repository Structure

| File | Responsibility |
| :--- | :--- |
| [`Flow.lean`](Flow.lean) | Upstream formal gatekeeper (`0 sorryAx`, no `_root_.main` collision): `Chart` typeclass, parametric `Rule k` builder, dual-seed $\mathbb{F}_p$ seals, Kan / Schmitt / 2-Jet theorems, and `.flow` Atlas emitters. |
| [`flow.h`](flow.h) | Freestanding (`-ffreestanding -nostdlib`) single-header substrate: `flow_cell` (`vector_size(64)`), 0-ULP `flow_isqrt_u64`, read-only seal verifiers, branchless `flow_step` & `flow_step_atlas`, and lock-free `flow_arena_publish_bound` / `flow_arena_try_swap` / `flow_arena_load_acq`. |
| [`flow.c`](flow.c) | Precompiled universal C17 execution engine (`build/flow`): `flow verify` and `flow run` (pure integer Q48.16 stream I/O and zero-downtime monotonic `@swap` hot-reloading, zero floats). |
| [`test_flow.c`](test_flow.c) | Ring-0 bare-metal symbol gate (`FLOW_RING0_GATE`) and HoTT $\times$ 2-Jet Atlas verification suite. |
| [`Makefile`](Makefile) | Zero-warning `-Werror` build pipeline producing `build/Flow.olean`, `build/flow`, and verification gates without `lean -c`. |
| [`VERSION`](VERSION) | Canonical release version (`5.0.0`). |

---

## 5. Build & Verification Gates

```bash
# Build Flow.olean, universal ./build/flow engine, and core .flow capsules
make all

# Execute formatting, formal proof, Ring-0 freestanding, engine, and CLI gates
make clean && make test
```

`make test` enforces four verification gates in sequence:
1. **Gate 1 (`capsules`)**: Compiles [`Flow.lean`](Flow.lean) under `lean -DwarningAsError=true` (`0 sorryAx`, `0` warnings) and mints sealed `.flow` Manifold Atlases.
2. **Gate 2 (`ring0`)**: Compiles [`flow.h`](flow.h) under `-ffreestanding -nostdlib -fno-builtin -Werror` and asserts `nm -u == empty` (`0` undefined symbols, `0` conditional branches, `0` FPU instructions).
3. **Gate 3 (`test-flow`)**: Verifies dual seals, anti-laundering, Schmitt chart gluing, orthogonal Kan mode locks, $R'=0$ Citardauq jumps, $\Delta < 0$ tunneling, and lock-free QSBR hot-swaps.
4. **Gate 4 (`flow` CLI)**: End-to-end zero-C execution via `./build/flow verify` and `./build/flow run`.

---

## License

Released under the [MIT License](LICENSE).
