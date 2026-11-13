<p align="center">
  <img src="./assets/logo.svg?cb=20261113v6" alt="FLOW — Zero-Risk 2-Jet &amp; HoTT Manifold Arithmetic Substrate" width="100%" />
</p>

# FLOW — Zero-Risk 2-Jet & HoTT Manifold Arithmetic Substrate (`v6.0.0`)

> **Software Engineering as Silicon Physics.**  
> **Zero structs.** **Zero control-flow branches in kernel.** **Zero SIMD/FPU instructions (`-mgeneral-regs-only`).** **Zero overflow/zombie traps.** **Zero heap.**

`FLOW` is a Tape-Out-grade foundational arithmetic substrate for multi-regime dynamical and control systems:

1. **Upstream (`.lean` $\to$ `.flow`)**: [`Flow.lean`](Flow.lean) compiles 2-Jet manifold charts, Schmitt hysteresis deadbands, and orthogonal Cubical Kan mode locks directly into a cache-line-aligned `(1 + 2^k) × 64B` **`.flow` Manifold Atlas** (`0` external imports, `0` `lean -c` codegen).
2. **Runtime (`flow.h` / `flow`)**: [`flow.h`](flow.h) executes one total, branchless `8 × u64` General-Purpose Register (GPR) endomorphism with a **128-bit Guard-Bit Wide-Accumulator**, **odd-symmetric nearest-integer rounding**, and **lossless $2^{-49}$ sub-LSB autonomous fiber conservation**:

$$\Phi_{\text{atlas}} : \mathcal{C}_{64}^{\text{state}} \times \mathcal{A}_{2^k}^{\text{chart}} \times (u_{\text{ext}}, m_{\text{req}}) \longrightarrow \mathcal{C}_{64}^{\text{state}}$$

---

## 1. Silicon & Algebraic Guarantees

1. **Pure GPR Register Substrate (`-mgeneral-regs-only` Clean)**:
   Every cell is a `64`-byte `uint64_t[8]` array (`flow_cell`). Zero `vector_size` SIMD cross-bank transfers, zero FPU instructions, and safe for direct Ring-0 / interrupt-handler / bare-metal execution without `kernel_neon_begin` / `kernel_fpu_begin`.
2. **Total 128-Bit Guard-Bit Solver (Zero Overflow / Zero Zombie Traps)**:
   Inputs normalize symmetrically into `[-0x7FFFFFFF, +0x7FFFFFFF]` (`Q16.16`, eliminating two's complement `INT32_MIN` negation asymmetry). Residuals evaluate exactly at $2^{-49}$ scale (`G0`) and tangents at $2^{-32}$ scale (`G1`), resolving Citardauq quadratic roots ($\Delta \ge 0$), $C^0$-continuous minimum-$|R|$ barrier vertices ($\Delta < 0$), and linear steps ($R_2 = 0$) through a single muxed 128-by-64 restoring divider with strict 128-bit Lyapunov monotonicity ($|R(x')| \le |R(x)|$).
3. **Exact Gauge & Spatial Mirror Symmetry**:
   Odd-symmetric rounding (`sym_shr(-v) == -sym_shr(v)`) and gauge-covariant branch selection ensure $\Delta x(-R) = \Delta x(R)$ everywhere and $\Delta x(-x, -b_1) = -\Delta x(x, b_1)$ across all asymmetric states.
4. **Lossless Autonomous 2-Jet Fiber Conservation**:
   In autonomous mode (`b1 == 0 && b2 == 0`), `state[7]` carries the exact `[j1_sub16:16 | 0:15 | j0_sub33:33]` sub-LSB fractional phase, achieving `0` LSB secular drift across arbitrarily long trajectories.

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
| `6` | `48..55` | `[Epoch:32 \| Status:32]` (`bit 0` = `FLOW_STATUS_CONVERGED`) | `[Epoch:32 \| 0:32]` — Monotonic chart generation epoch |
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

### Step 2 — Verify & Stream Events via `flow` CLI or Bare-Metal `flow.h`

```bash
/path/to/FLOW/build/flow verify surge.flow
printf "4.0\n12.0\n9.0 0x0000\n1.0\n" | /path/to/FLOW/build/flow run surge.flow
```

Bare-metal / Ring-0 C embedding (`-ffreestanding -nostdlib -mgeneral-regs-only`):

```c
#include "flow.h"

flow_step_atlas(state, state, chart_arena, cap_mask, u_ext_q16, mode_req_u16);
```

---

## 4. Repository Structure & Build

| File | Responsibility |
| :--- | :--- |
| [`Flow.lean`](Flow.lean) | Pure Lean 4 binary layout emitter (`Cell64`, `Rule`, `mintCell`, `mintState`, `mintArena`, `mintBundle`). |
| [`flow.h`](flow.h) | Freestanding GPR-only single-header arithmetic substrate (`flow_step`, `flow_step_atlas`, `flow_arena_try_swap`). |
| [`flow.c`](flow.c) | Universal zero-heap, zero-float C17 execution CLI (`flow verify`, `flow run`, `@swap`). |
| [`Makefile`](Makefile) | `-Werror` build pipeline (`make all`, `make fmt`, `make install`). |
| [`VERSION`](VERSION) | Release version (`6.0.0`). |

*(Hardware opcode decoding, 128-bit bit-lattice verification, gauge/mirror symmetry, Lyapunov monotonicity, and Conway toroidal cellular endurance are verified externally via `flow_inspect`.)*

---

## License

Released under the [MIT License](LICENSE).
