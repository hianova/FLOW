<p align="center">
  <img src="./assets/banner.svg" alt="FLOW — Formally Verified L0 Integer, HoTT &amp; 2-Jet Substrate" width="100%" />
</p>

# FLOW — Object-Free HoTT & 2-Jet Register Calculus

> **Software Engineering as Mathematics.**  
> Zero objects (`struct`-free core). Zero control-flow graphs (`0` conditional branches, `0` SSA `phi` nodes). Zero floating-point (`FloatOps == 0`). Zero heap (`HeapCalls == 0`).

`FLOW` does not execute domain code. Instead, `FLOW` models computation as a single, morphology-agnostic **branchless endomorphism** over **64-byte (`512-bit`) hardware register cells**:

$$\Phi : \mathcal{C}_{64}^{\text{state}} \times \mathcal{C}_{64}^{\text{bound}} \longrightarrow \mathcal{C}_{64}^{\text{state}}$$

Domain semantics never enter the runtime as code, branches, `lean -c` C runtime objects, or structs. **Lean 4** acts as the upstream semantic gatekeeper: downstream applications prove domain invariants via `Flow.Boundary` (`0 sorryAx`) and emit **sealed 64-byte binary `.flow` capsules** at build time (`lean --run`, zero C/IR codegen). **C17** enforces physical 64-byte cache-line alignment and lowers the `8 × u64` register vector directly onto hardware ALU, overflow-flag, and Acquire/Release atomic instructions across `ARM64`, `x86_64`, `RISC-V`, and `WebAssembly`.

---

## 1. Architectural Axioms

```text
  Domain Rules  ──►  [ Flow.lean ]  Proves Boundary α (0 sorryAx) & seals 64B .flow data
                           │        (Firewall against lean -c: emits binary data, 0 codegen)
                           ▼
                    [ flow_cell ]   8 × u64 (512-bit hardware vector register, align 64)
                           │        lane[6] = [Epoch:32 | Sieve:32] (SPSC + QSBR + HoTT Ω)
                           │        lane[7] = Goldilocks F_p Seal   (p = 2^64 - 2^32 + 1)
                           ▼
                    [ flow_step ]   Immutable branchless HoTT × 2-Jet transition law Φ
```

1. **HoTT Erases the "Object"**:
   By Univalence ($A \simeq B \implies A = B$), structs, queues, ring buffers, and hot-reloaders collapse into a single canonical carrier: a 512-bit hardware register vector (`uint64_t __attribute__((vector_size(64), aligned(64)))`) addressed over $\mathbb{Z}/2^k\mathbb{Z}$ (`idx & cap_mask`, zero out-of-bounds UB).
2. **Grothendieck Sieve ($\Omega$) Unifies Safety & Lock-Free Concurrency**:
   Hardware signed-overflow capture, Cubical Kan homotopy checks, seal verification, SPSC stream handshakes, and lock-free QSBR boundary hot-swaps inhabit one 64-bit word (`lane[6] = [Epoch:32 | Sieve:32]`) driven by Acquire/Release loads and stores (`flow_arena_load_acq` / `flow_arena_store_rel`).
3. **2-Jet ($J^2$) as a Universal Constraint / NP Solver**:
   Regardless of external problem morphology, every trajectory carries an exact Q48.16 **2-Jet fiber** $(j_0, j_1, j_2)$ — residual $j_0$, tangent $j_1$, and curvature $j_2$. Where first-order gradient descent and optimization Newton methods stall at critical points ($j_1 = 0$), $\Phi$ resolves exact Citardauq roots when $\Delta = j_1^2 - 2 j_0 j_2 \ge 0$ and uses Wick-rotated curvature tunneling ($\sqrt{|\Delta|} / |j_2|$) when $\Delta < 0$ to escape local minima in a single branchless pass.
4. **Upstream Anti-`lean -c` Firewall**:
   `SealedCell` in [`Flow.lean`](Flow.lean) has a `private` constructor. Downstream applications cannot forge unverified 64-byte boundaries or couple `<lean/lean.h>` into production; they implement `class Flow.Boundary`, discharge three compile-time proof obligations, and serialize `.flow` binary capsules.

---

## 2. Canonical 64-Byte Cell (`flow_cell`)

State and Boundary share an identical, symmetric `8 × u64` (`64-byte` cache-line) register grading:

| Lane | Offset | State Cell ($\mathcal{C}_{64}^{\text{state}}$) | Boundary Cell ($\mathcal{C}_{64}^{\text{bound}}$ — Minted by Lean) |
| :--- | :--- | :--- | :--- |
| `0` | `0..7` | `j0` — Q48.16 0-Jet (residual / energy) | `b0` — Target offset / barrier constant (Q48.16) |
| `1` | `8..15` | `j1` — Q48.16 1-Jet (first-order tangent) | `b1` — First-order coupling weight (Q48.16) |
| `2` | `16..23` | `j2` — Q48.16 2-Jet (second-order curvature) | `b2` — Second-order curvature / stiffness (Q48.16) |
| `3` | `24..31` | `x` — HoTT coordinate / Cubical path | `kan_filter` — Immutable homotopy axis mask |
| `4` | `32..39` | `dt` — Resolved step $\Delta t^* \ge 0$ (Q48.16) | `dt_max` — Admissible horizon $\Delta t_{\max} > 0$ (Q48.16) |
| `5` | `40..47` | `[DomainTag:32 \| Momentum:32]` | `[DomainTag:32 \| AuxParam:32]` |
| `6` | `48..55` | `[Epoch:32 \| Sieve:32]` — Live $\Omega$ state | `[Epoch:32 \| ReqSieve:32]` — Required $\Omega$ mask |
| `7` | `56..63` | `seal` — Goldilocks $\mathbb{F}_p$ state MAC | `seal` — Lean-minted $\mathbb{F}_p$ boundary MAC |

### Sieve ($\Omega$) Covering Axes (`lane[6][31:0]`)

| Bit | Macro | Invariant |
| :--- | :--- | :--- |
| `0` | `FLOW_SIEVE_SEAL_VALID` | Division-free Goldilocks $\mathbb{F}_p$ ($x \mapsto x^7$) sponge MAC matches `lanes[0..6]` (`seal != 0`) |
| `1` | `FLOW_SIEVE_DOMAIN_MATCH` | State and boundary carry identical 32-bit Univalence `DomainTag` (`lane[5][63:32]`) |
| `2` | `FLOW_SIEVE_NO_OVERFLOW` | Hardware ALU overflow flag == `0` across all Q48.16 / 128-bit intermediate ops |
| `3` | `FLOW_SIEVE_KAN_HOMOTOPIC` | Candidate step preserves locked Cubical axes: `((x ^ x_cand) & kan_filter) == 0` |
| `4` | `FLOW_SIEVE_HORIZON_VALID` | Boundary horizon satisfies $\Delta t_{\max} > 0$ and $0 \le \Delta t^* \le \Delta t_{\max}$ |
| `5` | `FLOW_SIEVE_CONVERGED` | Post-step residual lies within zero-locus tolerance ($\|r_0'\| \le 4$ LSB in Q48.16) |

---

## 3. Minimal End-to-End Usage

### Step 1 — Constrain & Mint `.flow` Binary Capsule in Lean 4 (Build-Time Only)

```lean
import Flow

structure MyConstraint where
  targetQ16 : Int

instance : Flow.Boundary MyConstraint where
  b0 c        := c.targetQ16
  b1 _        := 0
  b2 _        := -2 * Flow.q16One
  kanFilter _ := 0
  dtMax _     := 4 * Flow.q16One
  domainTag _ := 0x464C4F57
  dtMax_valid _    := by decide
  coeffs_bounded c := ... -- Discharged by domain invariant proof
  sieve_guarded _  := by bv_decide

-- Run via `lean --run`: outputs 64-byte sealed binary data (never `lean -c`)
def emit (c : MyConstraint) : IO Unit :=
  Flow.emitCell "rule.flow" (Flow.mintCell c)
```

### Step 2 — Execute Branchless Register Calculus in C17

```c
#include "flow.h"

/* Load 64-byte Lean-minted binary capsule into a 512-bit register value */
flow_cell bound = flow_cell_from_bytes(mmap_or_file_bytes);

/* Initialize state at critical point x = 0 with matching Univalence DomainTag */
flow_cell state = {0, 0, 0, 0, 0, (UINT64_C(0x464C4F57) << 32U) | 65536ULL, 0, 0};
state[7] = flow_seal_cell(state);

/* Single branchless HoTT × 2-Jet transition */
state = flow_step(state, bound);
```

---

## 4. Repository Structure

| File | Responsibility |
| :--- | :--- |
| [`Flow.lean`](Flow.lean) | Upstream proof-carrying `Boundary` typeclass, private `SealedCell` gatekeeper, Goldilocks $\mathbb{F}_p$ sponge, machine-checked Kan / firewall / stationary-trap escape theorems (`0 sorryAx`), and `.flow` binary capsule/arena emitters. |
| [`flow.h`](flow.h) | Complete freestanding (`-ffreestanding -nostdlib`) single-header substrate: `flow_cell` (`vector_size(64)`), unrolled 0-ULP `flow_isqrt_u64`, division-free $\mathbb{F}_p$ sponge, branchless `flow_step`, and Acquire/Release arena transport. |
| [`test_flow.c`](test_flow.c) | Bare-metal Ring-0 symbol probe (`FLOW_RING0_GATE`) and hosted verification suite (0-ULP Lean $\leftrightarrow$ C seal check, tamper/domain firewall, $R'(0)=0$ root jump, $\Delta < 0$ instanton tunneling, Kan obstruction, and multi-threaded QSBR hot-swaps). |
| [`Makefile`](Makefile) | Zero-warning `-Werror` build pipeline without `lean -c` or `leanc`. |
| [`VERSION`](VERSION) | Canonical release version (`5.0.0`). |

---

## 5. Build & Verification Gates

```bash
# Prove Lean 4 theorems (0 sorryAx) and mint sealed 64B .flow capsules
make capsules

# Execute formatting, formal proof, Ring-0 freestanding, and HoTT × 2-Jet gates
make clean && make test
```

`make test` enforces three verification gates in sequence:
1. **Gate 1 (`capsules`)**: Verifies `Flow.lean` under `lean -DwarningAsError=true` (`0 sorryAx`, `0` warnings) and mints 64-byte `.flow` binary capsules and a $2^2 \times 64\text{B}$ arena image.
2. **Gate 2 (`ring0`)**: Compiles `flow.h` under `-ffreestanding -nostdlib -fno-builtin -Werror` and asserts `nm -u == empty` (`0` undefined symbols, `0` conditional branch instructions, `0` FPU instructions).
3. **Gate 3 (`test-flow`)**: Runs 23,000+ assertions over Lean-minted `.flow` binary capsules, including non-convex stationary-trap tunneling and concurrent lock-free arena hot-swapping.

---

## License

Released under the [MIT License](LICENSE).
