<p align="center">
  <img src="./assets/banner.svg" alt="FLOW — Formally Verified L0 Integer, HoTT &amp; 2-Jet Substrate" width="100%" />
</p>

# FLOW — Object-Free HoTT & 2-Jet Register Calculus

> **Software Engineering as Mathematics.**  
> Zero objects (`struct`-free core). Zero control-flow graphs (`0` conditional branches, `0` SSA `phi` nodes). Zero floating-point (`FloatOps == 0`). Zero heap (`HeapCalls == 0`).

`FLOW` does not execute domain code. Instead, `FLOW` models computation as a single, morphology-agnostic **branchless endomorphism** over **64-byte (`512-bit`) hardware register cells**:

$$\Phi : \mathcal{C}_{64}^{\text{state}} \times \mathcal{C}_{64}^{\text{bound}} \longrightarrow \mathcal{C}_{64}^{\text{state}}$$

Domain semantics never enter the runtime as code, branches, C macros, `lean -c` C runtime objects, or structs. **Lean 4** is the sole upstream semantic gatekeeper: downstream applications prove domain invariants via `Flow.Boundary` (`0 sorryAx`) and emit **self-contained `.flow` binary bundles** (`State + 2^k Boundaries`) at build time (`lean --run`, zero C/IR codegen). **C17** enforces physical 64-byte cache-line alignment and lowers the `8 × u64` register vector directly onto hardware ALU, overflow-flag, and Acquire/Release atomic instructions across `ARM64`, `x86_64`, `RISC-V`, and `WebAssembly`.

---

## 1. Architectural Axioms

```text
  Domain Rules  ──►  [ Flow.lean ]  Proves Boundary α (0 sorryAx) & mints .flow bundles
                           │        • SealedState ("FLOWSTA5") + 2^k SealedBound ("FLOWBND5")
                           │        • Zero C/IR codegen, zero lean -c, zero C seal minting
                           ▼
                    [ flow_cell ]   8 × u64 (512-bit hardware vector register, align 64)
                           │        lane[6] = [Epoch:32 | Sieve:32] (SPSC + QSBR + HoTT Ω)
                           │        lane[7] = Domain-separated Goldilocks F_p MAC
                           ▼
                    [ flow_step ]   Immutable branchless HoTT × 2-Jet transition law Φ
```

1. **HoTT Erases the "Object"**:
   By Univalence ($A \simeq B \implies A = B$), structs, queues, ring buffers, and hot-reloaders collapse into one canonical carrier: a 512-bit hardware register vector (`uint64_t __attribute__((vector_size(64), aligned(64)))`) addressed over $\mathbb{Z}/2^k\mathbb{Z}$ (`idx & cap_mask`, zero out-of-bounds UB).
2. **Grothendieck Sieve ($\Omega$) & Epoch-Invariant Seals**:
   Hardware signed-overflow capture, Cubical Kan homotopy checks, dual seal verification, SPSC stream handshakes, and lock-free QSBR boundary hot-swaps share `lane[6] = [Epoch:32 | Sieve:32]`. Boundary seals (`FLOW_BOUND_SEED`) cover `lanes[0..5]` and `[31:0] ReqSieve` while excluding `[63:32] Epoch`, allowing `flow_arena_publish_bound` to advance runtime epochs **without ever re-signing a Lean boundary in C**.
3. **2-Jet ($J^2$) as a Universal Constraint / NP Solver**:
   Regardless of external problem morphology, every trajectory carries an exact Q48.16 **2-Jet fiber** $(j_0, j_1, j_2)$ — residual $j_0$, tangent $j_1$, and curvature $j_2$. Where first-order gradient descent and optimization Newton methods stall at critical points ($j_1 = 0$), $\Phi$ resolves exact Citardauq roots when $\Delta = j_1^2 - 2 j_0 j_2 \ge 0$ and uses Wick-rotated curvature tunneling ($\sqrt{|\Delta|} / |j_2|$) when $\Delta < 0$ to escape local minima in a single branchless pass.
4. **Hermetic Anti-Drift Gatekeeper**:
   Both `SealedState` and `SealedBound` in [`Flow.lean`](Flow.lean) have `private` constructors with distinct cryptographic domain seeds, and `flow.h` exposes **zero seal-minting functions** (plus anti-laundering protection that keeps unsealed C cells permanently at `w7 == 0`). Downstream C hosts never duplicate domain tags, constants, or business logic.

---

## 2. Canonical 64-Byte Cell (`flow_cell`)

State and Boundary share an identical, symmetric `8 × u64` (`64-byte` cache-line) register grading:

| Lane | Offset | State Cell ($\mathcal{C}_{64}^{\text{state}}$ — `"FLOWSTA5"`) | Boundary Cell ($\mathcal{C}_{64}^{\text{bound}}$ — `"FLOWBND5"`) |
| :--- | :--- | :--- | :--- |
| `0` | `0..7` | `j0` — Q48.16 0-Jet (residual / energy) | `b0` — Target offset / barrier constant (Q48.16) |
| `1` | `8..15` | `j1` — Q48.16 1-Jet (first-order tangent) | `b1` — First-order coupling weight (Q48.16) |
| `2` | `16..23` | `j2` — Q48.16 2-Jet (second-order curvature) | `b2` — Second-order curvature / stiffness (Q48.16) |
| `3` | `24..31` | `x` — HoTT coordinate / Cubical path | `kan_filter` — Immutable homotopy axis mask |
| `4` | `32..39` | `dt` — Resolved step $\Delta t^* \ge 0$ (Q48.16) | `dt_max` — Admissible horizon $\Delta t_{\max} > 0$ (Q48.16) |
| `5` | `40..47` | `[DomainTag:32 \| Momentum:32]` | `[DomainTag:32 \| AuxParam:32]` |
| `6` | `48..55` | `[Epoch:32 \| Sieve:32]` — Live $\Omega$ state | `[Epoch:32 \| ReqSieve:32]` — Required $\Omega$ mask |
| `7` | `56..63` | `seal` — $\mathbb{F}_p$ MAC over `lanes[0..6]` | `seal` — Epoch-invariant $\mathbb{F}_p$ MAC |

### Sieve ($\Omega$) Covering Axes (`lane[6][31:0]`)

| Bit | Macro | Invariant |
| :--- | :--- | :--- |
| `0` | `FLOW_SIEVE_SEAL_VALID` | Both `state` (`FLOW_STATE_SEED`) and `bound` (`FLOW_BOUND_SEED`) pass Goldilocks $\mathbb{F}_p$ verification |
| `1` | `FLOW_SIEVE_DOMAIN_MATCH` | State and boundary carry identical 32-bit Univalence `DomainTag` (`lane[5][63:32]`) |
| `2` | `FLOW_SIEVE_NO_OVERFLOW` | Hardware ALU overflow flag == `0` across all Q48.16 / 128-bit intermediate ops |
| `3` | `FLOW_SIEVE_KAN_HOMOTOPIC` | Candidate step preserves locked Cubical axes: `((x ^ x_cand) & kan_filter) == 0` |
| `4` | `FLOW_SIEVE_HORIZON_VALID` | Boundary horizon satisfies $\Delta t_{\max} > 0$ and $0 \le \Delta t^* \le \Delta t_{\max}$ |
| `5` | `FLOW_SIEVE_CONVERGED` | Post-step residual lies within zero-locus tolerance ($\|r_0'\| \le 4$ LSB in Q48.16) |

---

## 3. Minimal End-to-End Usage (Zero Domain Code in C)

### Step 1 — Prove & Mint Self-Contained `.flow` Bundle in Lean 4

```lean
import Flow

structure MyRule where
  targetQ16 : Int
  hBound    : -Flow.maxSafeQ16 ≤ targetQ16 ∧ targetQ16 ≤ Flow.maxSafeQ16

instance : Flow.Boundary MyRule where
  b0 r        := r.targetQ16
  b1 _        := 0
  b2 _        := -2 * Flow.q16One
  kanFilter _ := 0
  dtMax _     := 4 * Flow.q16One
  domainTag _ := 0x41505031 -- "APP1" stays solely in Lean
  dtMax_valid _    := by decide
  coeffs_bounded r := ⟨r.hBound.1, r.hBound.2, by decide, by decide, by decide, by decide⟩
  sieve_guarded _  := by
    unfold Flow.sieveSafetyGate Flow.sieveBaseGate Flow.sieveSealValid
           Flow.sieveDomainMatch Flow.sieveNoOverflow Flow.sieveHorizonValid
    bv_decide

-- Build-time (`LEAN_PATH=build lean --run AppSpec.lean`): emits State + 2^0 Boundary
def main : IO UInt32 := do
  let r : MyRule := ⟨2 * Flow.q16One, by decide⟩
  Flow.emitBundle "app.flow" 0 (Flow.mintState r 0) [r] r
  return 0
```

### Step 2 — Generic 4-Line Host Execution in C17

```c
#include "flow.h"

/* Slot 0 = Sealed State, Slot 1 = Sealed Boundary (0 domain constants in C) */
flow_cell state = flow_cell_from_bytes(&bundle_bytes[0]);
flow_cell bound = flow_cell_from_bytes(&bundle_bytes[64]);
state = flow_step(state, bound);
```

---

## 4. Repository Structure

| File | Responsibility |
| :--- | :--- |
| [`Flow.lean`](Flow.lean) | Pure library module (no `_root_.main` collision): `Boundary` typeclass, private `SealedState` / `SealedBound` gatekeepers, epoch-invariant $\mathbb{F}_p$ seals, machine-checked Kan / anti-laundering / stationary-trap escape theorems (`0 sorryAx`), and `.flow` bundle emitters. |
| [`flow.h`](flow.h) | Complete freestanding (`-ffreestanding -nostdlib`) single-header substrate: `flow_cell` (`vector_size(64)`), unrolled 0-ULP `flow_isqrt_u64`, read-only `flow_verify_state` / `flow_verify_bound`, branchless `flow_step`, and `flow_arena_publish_bound` / `flow_arena_load_acq`. |
| [`test_flow.c`](test_flow.c) | Bare-metal Ring-0 symbol probe (`FLOW_RING0_GATE`) and hosted verification suite consuming Lean-minted `.flow` bundles with zero C-side seal minting or domain macros. |
| [`Makefile`](Makefile) | Zero-warning `-Werror` build pipeline without `lean -c` or `leanc`. |
| [`VERSION`](VERSION) | Canonical release version (`5.0.0`). |

---

## 5. Build & Verification Gates

```bash
# Prove Lean 4 theorems (0 sorryAx) and mint sealed .flow bundles
make capsules

# Execute formatting, formal proof, Ring-0 freestanding, and HoTT × 2-Jet gates
make clean && make test
```

`make test` enforces three verification gates in sequence:
1. **Gate 1 (`capsules`)**: Compiles `Flow.lean` under `lean -DwarningAsError=true` (`0 sorryAx`, `0` warnings) and mints self-contained `.flow` binary bundles.
2. **Gate 2 (`ring0`)**: Compiles `flow.h` under `-ffreestanding -nostdlib -fno-builtin -Werror` and asserts `nm -u == empty` (`0` undefined symbols, `0` conditional branch instructions, `0` FPU instructions).
3. **Gate 3 (`test-flow`)**: Verifies dual domain-separated seals, anti-laundering rejection, epoch-invariant hot-swaps, $R'(0)=0$ Citardauq root jumps, $\Delta < 0$ instanton tunneling, and Kan obstructions.

---

## License

Released under the [MIT License](LICENSE).
