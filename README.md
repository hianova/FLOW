<p align="center">
  <img src="./assets/banner.svg" alt="FLOW — Formally Verified L0 Integer, HoTT &amp; 2-Jet Substrate" width="100%" />
</p>

# FLOW — Object-Free HoTT & 2-Jet Register Calculus

> **Software Engineering as Mathematics.**  
> Zero objects (`struct`-free core). Zero control-flow graphs (branchless, `0` SSA `phi` nodes). Zero floating-point (`FloatOps == 0`). Zero heap (`HeapCalls == 0`).

`FLOW` does not execute domain code. Instead, `FLOW` treats computation as a single, morphology-agnostic **branchless endomorphism** over **64-byte (`512-bit`) hardware register cells**:

$$\Phi : \mathcal{C}_{64}^{\text{state}} \times \mathcal{C}_{64}^{\text{bound}} \longrightarrow \mathcal{C}_{64}^{\text{state}}$$

Domain logic never enters the runtime as code, branches, or data structures. **Lean 4** constrains and proves domain invariants externally and emits **64-byte sealed binary boundary cells** (data, never IR or C codegen). **C17** constrains physical 64-byte alignment and maps the `8 × u64` register vector directly onto hardware ALU, overflow-flag, and Acquire/Release atomic primitives across `ARM64`, `x86_64`, `RISC-V`, and `WebAssembly`.

---

## 1. Architectural Axioms

```text
  Domain Rules  ──►  [ Flow.lean ]  Proves invariants (0 sorryAx) & seals 64B binary data
                           │        (Emits pure 64-byte cells — zero codegen, zero SSA)
                           ▼
                    [ flow_cell ]   8 × u64 (512-bit hardware vector register, align 64)
                           │        lane[6] = [Epoch:32 | Sieve:32] (SPSC + QSBR + HoTT Ω)
                           │        lane[7] = Goldilocks F_p Seal   (p = 2^64 - 2^32 + 1)
                           ▼
                    [ flow_step ]   Immutable branchless HoTT × 2-Jet transition law Φ
```

1. **HoTT Erases the "Object"**:
   Under Univalence ($A \simeq B \implies A = B$), structs, queues, ring buffers, and hot-reloaders collapse into one universal value type: a 512-bit register vector (`uint64_t __attribute__((vector_size(64), aligned(64)))`) indexed over $\mathbb{Z}/2^k\mathbb{Z}$ (`idx & mask`, zero out-of-bounds UB).
2. **Grothendieck Sieve ($\Omega$) Unifies Safety & Concurrency**:
   Hardware signed-overflow capture, Cubical Kan homotopy checks, seal verification, SPSC stream handshakes, and lock-free QSBR epoch swaps share a single 64-bit lane (`[Epoch:32 | Sieve:32]`) driven by single-instruction Acquire/Release loads and stores.
3. **2-Jet ($J^2$) as a Universal Constraint / NP Solver**:
   Regardless of external problem morphology, every trajectory carries an exact Q48.16 **2-Jet fiber** $(j_0, j_1, j_2)$ — residual $j_0$, tangent $j_1$, and curvature $j_2$. Where first-order methods stall in local minima ($j_1 = 0$) or tunnel across thin walls, $\Phi$ resolves exact quadratic roots when $\Delta = j_1^2 - 2 j_0 j_2 \ge 0$ and uses curvature $j_2$ to tunnel out of stationary traps in a single branchless pass.
4. **Lean & C as Pure External Constraints**:
   - **Lean 4** is the boundary of business semantics: it proves conservation/compatibility laws and outputs sealed 64-byte binary capsules.
   - **C17** is the boundary of silicon ABI: static alignment assertions, native `vector_size(64)` register values, and hardware overflow/atomic builtins (`-ffreestanding -nostdlib`).

---

## 2. Canonical 64-Byte Cell (`flow_cell`)

State and Boundary share an identical, symmetric `8 × u64` (`64-byte` cache-line) register grading:

| Lane | Offset | State Cell ($\mathcal{C}_{64}^{\text{state}}$) | Boundary Cell ($\mathcal{C}_{64}^{\text{bound}}$ — Minted by Lean) |
| :--- | :--- | :--- | :--- |
| `0` | `0..7` | `j0` — Q48.16 0-Jet (residual / energy) | `b0` — Target offset / barrier constant (Q48.16) |
| `1` | `8..15` | `j1` — Q48.16 1-Jet (first-order tangent) | `b1` — First-order coupling weight (Q48.16) |
| `2` | `16..23` | `j2` — Q48.16 2-Jet (second-order curvature) | `b2` — Second-order curvature / stiffness (Q48.16) |
| `3` | `24..31` | `x` — HoTT coordinate / Cubical path | `kan_filter` — Immutable homotopy axis mask |
| `4` | `32..39` | `dt` — Resolved step $\Delta t^*$ (Q48.16) | `dt_max` — Admissible horizon $\Delta t_{\max}$ (Q48.16) |
| `5` | `40..47` | `aux` — Coupled momentum / dual fiber | `domain_id` — Univalence schema equivalence tag |
| `6` | `48..55` | `[Epoch:32 \| Sieve:32]` — Live $\Omega$ state | `[Epoch:32 \| ReqSieve:32]` — Required $\Omega$ mask |
| `7` | `56..63` | `seal` — Goldilocks $\mathbb{F}_p$ state MAC | `seal` — Lean-minted $\mathbb{F}_p$ boundary MAC |

### Sieve ($\Omega$) Covering Axes (`lane[6][31:0]`)

| Bit | Axis | Invariant |
| :--- | :--- | :--- |
| `0` | `FLOW_SIEVE_SEAL_VALID` | Goldilocks $\mathbb{F}_p$ sponge MAC matches `lanes[0..6]` (`seal != 0`) |
| `1` | `FLOW_SIEVE_DOMAIN_MATCH` | State and boundary share identical Univalence `domain_id` |
| `2` | `FLOW_SIEVE_NO_OVERFLOW` | Hardware ALU overflow flag == `0` across all Q48.16 / 128-bit ops |
| `3` | `FLOW_SIEVE_KAN_HOMOTOPIC` | Cubical transition preserves invariant axes: `((x ^ x') & kan_filter) == 0` |
| `4` | `FLOW_SIEVE_HORIZON_VALID` | Resolved step satisfies $0 \le \Delta t^* \le \Delta t_{\max}$ |
| `5` | `FLOW_SIEVE_CONVERGED` | Residual lies within boundary zero-locus / admissible basin |

---

## 3. Branchless Transition Law ($\Phi$)

Every invocation of `flow_step(state, bound)` executes a straight-line register pipeline with **zero control-flow branches**:

1. **Seal & Univalence Gate**: Recomputes the Goldilocks $\mathbb{F}_p$ ($p = 2^{64} - 2^{32} + 1$, $x \mapsto x^7$ S-box) sponge over `bound[0..6]` and checks `domain_id` equality, projecting validity directly into `Sieve`.
2. **2-Jet Fiber Composition**: Pulls the current coordinate `x` and fiber `(j0, j1, j2)` through the boundary operator `(b0, b1, b2)` using 128-bit intermediate Q48.16 arithmetic with hardware overflow capture.
3. **Second-Order Root & Tunneling Step**:
   - **Root Regime ($\Delta = j_1^2 - 2 j_0 j_2 \ge 0$)**: Computes the numerically stable Citardauq root $\Delta t_{\text{root}}$ to land directly on the zero-crossing without discrete overshoot.
   - **Tunneling Regime ($\Delta < 0$ or $j_1 = 0$ trap)**: Computes the curvature-driven inflection / escape step $\Delta t_{\text{tunnel}}$ from $(j_1, j_2)$ to exit local minima without backtracking.
   - Selects $\Delta t^* \in [0, \Delta t_{\max}]$ via bitwise blending (`(a & mask) | (b & ~mask)`).
4. **HoTT Kan Transport & Epoch Advance**: Transports `x` along the unobstructed Cubical axes (`~kan_filter`), updates `(j0, j1, j2)` via exact second-order Taylor pushforward, increments `Epoch`, and seals `lane[7]`. If any required Sieve axis fails, bitwise blending retains the prior valid coordinate while recording the exact obstruction bitmask in `lane[6]`.

Arena cells in shared memory (`flow_arena_load_acq` / `flow_arena_store_rel`) wrap `flow_cell` transfers in hardware Acquire/Release semantics over `(idx & cap_mask)`, providing zero-copy SPSC streaming and lock-free QSBR boundary hot-swapping with zero auxiliary structures.

---

## 4. Repository Structure

| File | Role |
| :--- | :--- |
| [`Flow.lean`](Flow.lean) | Pure Lean 4 formalization (`0 sorryAx`) of the 64B cell algebra, Goldilocks $\mathbb{F}_p$ sponge, HoTT Sieve laws, and 2-Jet solver invariants; synthesizes sealed `.flow` 64-byte binary boundary capsules. |
| [`flow.h`](flow.h) | Complete freestanding (`-ffreestanding -nostdlib`) single-header substrate defining `flow_cell` (`vector_size(64)`), branchless `flow_step`, and lock-free Acquire/Release arena transport. |
| [`test_flow.c`](test_flow.c) | Verification suite validating Lean-minted `.flow` capsules, 0-ULP differential equivalence, stationary-trap curvature tunneling, Kan obstruction sieving, and multi-threaded QSBR epoch hot-swaps. |
| [`Makefile`](Makefile) | Strict `-Werror` C17 + `-ffreestanding -nostdlib` Ring-0 symbol gate (`nm -u == empty`) + Lean 4 formal proof & binary capsule generation pipeline. |

---

## 5. Build & Verification

```bash
# 1. Prove formal invariants in Lean 4 and mint sealed 64B .flow boundary capsules
make capsules

# 2. Verify clang-format, freestanding Ring-0 zero-symbol gate, and HoTT × 2-Jet suite
make test
```

---

## License

Released under the [MIT License](LICENSE).
