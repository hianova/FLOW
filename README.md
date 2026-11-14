<p align="center">
  <img src="./assets/logo.svg?cb=20261114v7" alt="FLOW — Zero-Risk 2-Jet &amp; SEARU Symbiotic Manifold Substrate" width="100%" />
</p>

# FLOW — Zero-Risk 2-Jet & SEARU Symbiotic Manifold Substrate (`v7.0.1`)

> **Software Engineering as Silicon Physics.**  
> **Zero structs.** **Zero control-flow branches in Ring-0 kernels.** **Zero SIMD/FPU instructions (`-mgeneral-regs-only`).** **Zero indirect calls (`0` `blr`).** **Zero overflow/zombie traps.** **Zero heap.** **Zero hand-tuned magic constants.**

`FLOW` is a Tape-Out-grade dual-operator arithmetic and combinatorial optimization substrate for multi-regime dynamical, hardware synthesis, and real-time control systems:

1. **Upstream (`.lean` $\to$ `.flow`)**: [`Flow.lean`](Flow.lean) compiles 2-Jet manifold charts, Schmitt hysteresis deadbands, and orthogonal Cubical Kan mode locks directly into a cache-line-aligned `(1 + 2^k) × 64B` **`.flow` Manifold Atlas** (`0` external imports, `0` `lean -c` codegen).
2. **Continuous Local Operator (`flow.h`)**: [`flow.h`](flow.h) executes one total, branchless `8 × u64` General-Purpose Register (GPR) endomorphism with a **128-bit Guard-Bit Wide-Accumulator**, **odd-symmetric nearest-integer rounding**, and **lossless $2^{-49}$ sub-LSB autonomous fiber conservation**:

$$\Phi_{\text{atlas}} : \mathcal{C}_{64}^{\text{state}} \times \mathcal{A}_{2^k}^{\text{chart}} \times (u_{\text{ext}}, m_{\text{req}}) \longrightarrow \mathcal{C}_{64}^{\text{state}}$$

3. **Discrete Global Operator & Meta-Synthesizer (`searu.h` / `flow self-opt`)**: [`searu.h`](searu.h) couples 1-bit Chaos and hardware `CLZ` Zipf/Pareto Lévy-flight (`0` `powf`, `0` `udiv`, `0` `blr`) symbiotically with `flow.h` — waking only at `!CONVERGED && dt == 0` barriers to heal discrete topologies via lock-free `@swap`, and autonomously synthesizing `flow.h`'s `0-ULP` Horner seeds and its own Crucible schedules into [`flow_opt_params.h`](flow_opt_params.h) (`make self-opt`).

---

## 1. Silicon & Algebraic Guarantees

1. **Pure GPR Register Substrate (`-mgeneral-regs-only` Clean)**:
   Every cell is a `64`-byte `uint64_t[8]` array (`flow_cell`). Zero `vector_size` SIMD cross-bank transfers, zero FPU instructions, and safe for direct Ring-0 / interrupt-handler / bare-metal execution across ARM64 and x86_64 without `kernel_neon_begin` / `kernel_fpu_begin`.
2. **Total 128-Bit Guard-Bit Solver (Zero Overflow / Zero Zombie Traps)**:
   Inputs normalize symmetrically into `[-0x7FFFFFFF, +0x7FFFFFFF]` (`Q16.16`, eliminating two's complement `INT32_MIN` negation asymmetry). Residuals evaluate exactly at $2^{-49}$ scale (`G0`) and tangents at $2^{-32}$ scale (`G1`), resolving Citardauq quadratic roots ($\Delta \ge 0$), $C^0$-continuous minimum-$|R|$ barrier vertices ($\Delta < 0$), and linear steps ($R_2 = 0$) through constant-time `0-ULP` `SEARU`-synthesized degree-3 Horner + `umulh` Newton-Raphson `isqrt` and 128-by-64 division (`0` `udiv`, `0` LUTs) with strict 128-bit Lyapunov monotonicity ($|R(x')| \le |R(x)|$).
3. **Exact Gauge & Spatial Mirror Symmetry**:
   Odd-symmetric rounding (`sym_shr(-v) == -sym_shr(v)`) and gauge-covariant branch selection ensure $\Delta x(-R) = \Delta x(R)$ everywhere and $\Delta x(-x, -b_1) = -\Delta x(x, b_1)$ across all asymmetric states.
4. **Lossless Autonomous 2-Jet Fiber Conservation**:
   In autonomous mode (`b1 == 0 && b2 == 0`), `state[7]` carries the exact `[j1_sub16:16 | 0:15 | j0_sub33:33]` sub-LSB fractional phase, achieving `0` LSB secular drift across arbitrarily long trajectories.
5. **Multi-Core Linearizable Branchless `@swap` & Tear-Guarded Reader (`ARMv8 Weak-Memory Clean`)**:
   `flow_arena_try_swap` executes branchless `LDAPR + Stack-Sink Pointer Mux + LSE CASAL + STLR` using the 32-bit lock sentinel `FLOW_SWAP_LOCK_TAG` (`0x464C4F57`), guaranteeing `0` shared-arena bytes written on rejected or contended swaps and strict dual-writer mutual exclusion. `flow_step_atlas` pairs `LDAPR` (`w6_pre`), payload snapshot (`0..5`), `DMB ISHLD`, and post-check (`w6_post`) to enforce a branchless Coordinate-Hold Stutter during active `@swap` windows (`0` torn chart snapshots).
6. **Autonomous Dual-Operator Self-Synthesis (`make self-opt`, Idempotent Fixed Point)**:
   `flow self-opt` eliminates human magic constants by driving `SEARU × FLOW 2-Jet` in a closed 2-stage synthesis loop: Stage 1 synthesizes `0-ULP` degree-3 Minimax-Horner seeds for `isqrt` and `recip` verified exhaustively across all `65,536` bins (`0` LUT bytes), and Stage 2 meta-evolves decoupled per-solver `SEARU` schedules across 32 training + 32 held-out NP instances (`64 / 64 = 100%` convergence, `3.47×` speedup over baseline).

---

## 2. Canonical 64-Byte Cell Layout (`flow_cell`)

| Lane | Bytes | State Cell (`state[0..7]`) | Chart Cell (`bound[0..7]`) |
| :--- | :--- | :--- | :--- |
| `0` | `0..7` | `j0` — 0-Jet value / post-step residual (`Q48.16 i64`) | `b0` — Base offset / equilibrium constant (`Q16.16 i64`) |
| `1` | `8..15` | `j1` — 1-Jet first-order slope (`Q48.16 i64`) | `b1` — First-order stiffness / slope (`Q16.16 i64`) |
| `2` | `16..23` | `j2` — 2-Jet second-order curvature (`Q16.16 i64`) | `b2` — Second-order curvature (`Q16.16 i64`) |
| `3` | `24..31` | `x` — Manifold coordinate (`Q16.16 i64`) | `[b_exit:i32 \| b_enter:i32]` — Schmitt band ($b_{\text{exit}} \le b_{\text{enter}}$) |
| `4` | `32..39` | `dt` — Resolved step magnitude $\|\Delta x\| \ge 0$ (`u64`) | `[dt_max:i32 \| u_gain:i32]` — Horizon $\Delta t_{\max} \ge 1$ & $u_{\text{ext}}$ gain |
| `5` | `40..47` | `[0:32 \| Chart:16 \| Mode:16]` | `[0:32 \| NextLo:8 \| NextHi:8 \| KanLock:16]` |
| `6` | `48..55` | `[Epoch:32 \| Status:32]` (`bit 0` = `FLOW_STATUS_CONVERGED`) | `[Epoch:32 \| Tag:32]` (`0` committed, `0x464C4F57` `@swap` lock) |
| `7` | `56..63` | `[j1_sub16:16 \| 0:15 \| j0_sub33:33]` — Sub-LSB phase | `0` |

---

## 3. Workflow

### Step 1 — Emit `.flow` Manifold Atlas in Lean 4

```lean
import Flow

def normalChart : Flow.Rule where
  b0      := 0
  b1      := Flow.q16One
  b2      := 0
  bExit   := -100 * Flow.q16One
  bEnter  := 10 * Flow.q16One
  dtMax   := 10 * Flow.q16One
  uGain   := Flow.q16One
  nextLo  := 0
  nextHi  := 1
  kanLock := 0

def surgeChart : Flow.Rule where
  b0      := 0
  b1      := 0
  b2      := 2 * Flow.q16One
  bExit   := 2 * Flow.q16One
  bEnter  := 50 * Flow.q16One
  dtMax   := 10 * Flow.q16One
  uGain   := Flow.q16One
  nextLo  := 0
  nextHi  := 1
  kanLock := 0x0001

def main : IO UInt32 := do
  let s0 := Flow.mintState 1 0 (initChart := 0) (initMode := 0x0001)
  Flow.emitBundle "surge.flow" 1 s0 [normalChart, surgeChart] normalChart
  return 0
```

```bash
LEAN_PATH=/path/to/FLOW/build lean -DwarningAsError=true --run SurgeSpec.lean
```

### Step 2 — Verify, Stream, Co-Solve & Self-Synthesize

```bash
/path/to/FLOW/build/flow verify surge.flow
printf "4.0\n12.0\n9.0 0x0000\n1.0\n" | /path/to/FLOW/build/flow run surge.flow
/path/to/FLOW/build/flow searu-verify
make -C /path/to/FLOW self-opt   # Autonomous SEARU x FLOW 2-Jet synthesis -> flow_opt_params.h
```

Bare-metal / Ring-0 & Application-Layer Dual-Operator C17 embedding (`-ffreestanding -nostdlib -mgeneral-regs-only`, `0` `blr` indirect calls):

```c
#include "flow.h"
#include "searu.h"

/* 1. Phase-gated continuous + discrete co-solver (0 noise during smooth 2-Jet descent;
 *    wakes SEARU + atomic @swap ONLY when trapped at !CONVERGED && dt == 0): */
flow_step_atlas(state, state, chart_arena, cap_mask, u_ext_q16, mode_req_u16);
searu_heal_and_swap(chart_arena, cap_mask, state, terms, num_terms, kan_bit_pin,
                    u_ext_q16, target_x_q16, 6U, &rng_seed, &epoch, 4096U, NULL);

/* 2. Zero-BLR inline custom discrete NP solver (KanLock pins + CLZ Zipf Sparse-k): */
SEARU_SOLVE_INLINE(&res, num_vars, init_canvas, kan_pin, seed, max_iters,
                   cand, frust, energy, {
    /* Inline C17 objective computing `energy` and optional `frust[0..3]` mask */
});
```

---

## 4. Repository Structure & Build

| File | Responsibility |
| :--- | :--- |
| [`Flow.lean`](Flow.lean) | Pure Lean 4 binary layout emitter (`Cell64`, `Rule`, `mintCell`, `mintState`, `mintArena`, `mintBundle`). |
| [`flow_opt_params.h`](flow_opt_params.h) | Auto-synthesized `0-ULP` degree-3 Horner seeds (`flow.h`) & meta-evolved `SEARU` schedules (`searu.h`) emitted by `make self-opt`. |
| [`flow.h`](flow.h) | Freestanding GPR-only single-header 2-Jet arithmetic substrate (`flow_step`, `flow_step_atlas`, `flow_arena_try_swap`). |
| [`searu.h`](searu.h) | Freestanding GPR-only 1-bit Chaos, `CLZ` Zipf Lévy-flight & symbiotic `SEARU × FLOW 2-Jet` NP co-solver (`searu_heal_and_swap`, `SEARU_SOLVE_INLINE`, `3-SAT`, `GraphColor`, `Bilevel 2-Jet + @swap`). |
| [`flow.c`](flow.c) | Universal zero-heap, zero-float C17 CLI & self-optimizing synthesizer (`flow verify`, `flow run`, `flow searu-verify`, `flow self-opt`, `@swap`). |
| [`Makefile`](Makefile) | Self-contained `-Werror` build, freestanding ISA/ABI audit (`0` branches, `0` `udiv`, `0` `blr`, `0` FPU/SIMD), and autonomous synthesis (`make all`, `make test`, `make self-opt`, `make fmt`). |
| [`VERSION`](VERSION) | Release version (`7.0.1`). |

*(Silicon-level bit-blasted SAT/temporal-induction proofs (`9x QED`) and `0-ULP` X-poisoned RTL co-simulation are paired in the companion `ELE` hardware repository.)*

---

## License

Released under the [MIT License](LICENSE).
