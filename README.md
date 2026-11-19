<p align="center">
  <img src="./assets/logo.svg?cb=20261114v7" alt="FLOW — Zero-Risk 2-Jet &amp; SEARU Symbiotic Manifold Substrate" width="100%" />
</p>

# FLOW — Layered Pure-Math Ring-0 Substrate (`core/`) & Toolchain (`sdk/`) (`v8.0.0`)

> **Software Engineering as Silicon Physics.**  
> **Zero structs.** **Zero control-flow branches in Ring-0 kernels.** **Zero SIMD/FPU instructions (`-mgeneral-regs-only`).** **Zero indirect calls (`0` `blr`).** **Zero overflow/zombie traps.** **Zero heap.** **Zero hand-tuned magic constants.**

`FLOW` is strictly factored into two decoupled layers with unidirectional dependencies (`sdk/ -> core/`, **zero** domain-specific or domain-laundered identifiers):

1. **Layer 0 — Pure Mathematics & Ring-0 GPR Kernel ([`core/`](core/))**:
   - **Verified Pure-Math Lean 4 SSOT ([`core/lean/`](core/lean/))**: `0` `IO`, `0` codegen, `0 sorryAx`, `-DwarningAsError=true` across [`Flow/Core.lean`](core/lean/Flow/Core.lean), [`Flow/BitAlgebra.lean`](core/lean/Flow/BitAlgebra.lean), [`Flow/Bundle.lean`](core/lean/Flow/Bundle.lean), [`Flow/Interval.lean`](core/lean/Flow/Interval.lean), and the pure-math umbrella [`Flow/Math.lean`](core/lean/Flow/Math.lean).
   - **Continuous Local Operator ([`core/flow.h`](core/flow.h))**: Executes one total, branchless `8 × u64` General-Purpose Register (GPR) endomorphism with a **128-bit Guard-Bit Wide-Accumulator**, **odd-symmetric nearest-integer rounding**, and **lossless $2^{-49}$ sub-LSB autonomous fiber conservation**:

$$\Phi_{\text{atlas}} : \mathcal{C}_{64}^{\text{state}} \times \mathcal{A}_{2^k}^{\text{chart}} \times (u_{\text{ext}}, m_{\text{req}}) \longrightarrow \mathcal{C}_{64}^{\text{state}}$$

   - **Discrete Global Operator ([`core/searu.h`](core/searu.h) + [`core/flow_opt_params.h`](core/flow_opt_params.h))**: Couples 1-bit Chaos and hardware `CLZ` Zipf/Pareto Lévy-flight (`0` `powf`, `0` `udiv`, `0` `blr`, zero `<stdio.h>`, zero circuit-synthesis baggage) symbiotically with `core/flow.h`.

2. **Layer 1 — Software Engineering Toolchain & IR SDK ([`sdk/`](sdk/))**:
   - **Lean 4 Codegen & Emitters ([`sdk/lean/`](sdk/lean/))**: File IO emitters ([`Flow/Emit.lean`](sdk/lean/Flow/Emit.lean)), full-512-bit freestanding zero-`memcpy` C17/C++20 `.h` + zero-64b `.wgsl` + `.flow` bundle `Spec` ([`Flow/Schema.lean`](sdk/lean/Flow/Schema.lean)), QTT resource-typed `.fir` `Builder` ([`Flow/Morphism.lean`](sdk/lean/Flow/Morphism.lean)), canonical shared algebraic morphisms ([`Flow/Canonical.lean`](sdk/lean/Flow/Canonical.lean)), [`Flow/Sdk.lean`](sdk/lean/Flow/Sdk.lean), and compatibility umbrella [`Flow.lean`](sdk/lean/Flow.lean).
   - **Modular C17/C++20 IR Headers**: Freestanding `.fir` wire codec/enums ([`sdk/flow_ir_wire.h`](sdk/flow_ir_wire.h)), zero-`<stdio.h>` `.fir` validator/evaluator ([`sdk/flow_ir.h`](sdk/flow_ir.h)), host `<stdio.h>` file wrapper ([`sdk/flow_ir_io.h`](sdk/flow_ir_io.h)), GF(2) multiplicative-complexity solver ([`sdk/searu_mc_gf2.h`](sdk/searu_mc_gf2.h)), `.fir` pass ([`sdk/flow_ir_synth.h`](sdk/flow_ir_synth.h)), and CLI ([`sdk/flow_cli.c`](sdk/flow_cli.c)).

---

## 1. Silicon & Algebraic Guarantees

1. **Pure GPR Register Substrate (`-mgeneral-regs-only` Clean)**:
   Every cell is a `64`-byte `uint64_t[8]` array (`flow_cell`). Zero `vector_size` SIMD cross-bank transfers, zero FPU instructions, and safe for direct Ring-0 / interrupt-handler execution without `kernel_neon_begin` / `kernel_fpu_begin`.
2. **Total 128-Bit Guard-Bit Solver (Zero Overflow / Zero Zombie Traps)**:
   Inputs normalize symmetrically into `[-0x7FFFFFFF, +0x7FFFFFFF]` (`Q16.16`). Residuals evaluate at $2^{-49}$ scale (`G0`) and tangents at $2^{-32}$ scale (`G1`) through constant-time `0-ULP` `SEARU`-synthesized degree-3 Horner + Newton `isqrt` and 128-by-64 division (`0` `udiv`, `0` LUTs) with strict 128-bit Lyapunov monotonicity.
3. **Exact Gauge & Spatial Mirror Symmetry**:
   Odd-symmetric rounding and gauge-covariant branch selection ensure exact sign & spatial mirror equivariance across all asymmetric states.
4. **Lossless Autonomous 2-Jet Fiber Conservation**:
   In autonomous mode (`b1 == 0 && b2 == 0`), `state[7]` preserves sub-LSB fractional phase (`0` LSB secular drift).
5. **Multi-Core Linearizable Branchless `@swap` & Tear-Guarded Reader (`ARMv8 Weak-Memory Clean`)**:
   `flow_arena_try_swap` and `flow_step_atlas` coordinate-hold guarantee zero torn snapshots and strict dual-writer mutual exclusion.
6. **Autonomous Dual-Operator Self-Synthesis (`make self-opt`)**:
   Synthesizes `0-ULP` degree-3 Minimax-Horner seeds and evolved `SEARU` schedules into [`core/flow_opt_params.h`](core/flow_opt_params.h).

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

## 3. Consuming `core/` vs `sdk/`

- **Pure-Math Lean (`IO`-free)**: `LEAN_PATH=/path/to/FLOW/build/lean` and `import Flow.Math` (or `Flow.Core`, `Flow.Bundle`, etc.).
- **SDK Lean (Codegen / `.flow` / `.fir`)**: `import Flow.Schema`, `import Flow.Morphism`, `import Flow.Emit`, or convenience `import Flow`.
- **Freestanding / Ring-0 C17**: `-I/path/to/FLOW/core` and `#include "flow.h"` / `#include "searu.h"` (or add `-I/path/to/FLOW/sdk` for freestanding `#include "flow_ir.h"` without `<stdio.h>`).
- **Host / Synthesis C17**: `-I/path/to/FLOW/core -I/path/to/FLOW/sdk` and explicitly include [`flow_ir_io.h`](sdk/flow_ir_io.h), [`searu_mc_gf2.h`](sdk/searu_mc_gf2.h), or [`flow_ir_synth.h`](sdk/flow_ir_synth.h) as needed (include-order independent).

```bash
make -C /path/to/FLOW test fmt-check
```

---

## 4. Repository Structure

| Path | Layer | Responsibility |
| :--- | :--- | :--- |
| [`core/lean/Flow/`](core/lean/Flow/) | Core Math | Domain-free Lean 4 math & `bv_decide` theorems (`Core`, `BitAlgebra`, `Bundle`, `Interval`, `Math`) — zero `IO`, zero `sorryAx`. |
| [`core/flow_opt_params.h`](core/flow_opt_params.h) | Core Ring-0 | Auto-synthesized `0-ULP` Horner seeds (`flow.h`) & evolved `SEARU` schedules (`searu.h`). |
| [`core/flow.h`](core/flow.h) | Core Ring-0 | Freestanding GPR-only 2-Jet arithmetic substrate & lock-free `@swap`. |
| [`core/searu.h`](core/searu.h) | Core Ring-0 | Freestanding GPR-only 1-bit Chaos, `CLZ` Zipf Lévy-flight & Bilevel 2-Jet co-solver. |
| [`sdk/lean/`](sdk/lean/) | SDK Lean | IO emitters (`Flow.Emit`), zero-`memcpy` 512b C17/WGSL Schema generator (`Flow.Schema`), QTT `.fir` Builder (`Flow.Morphism`), canonical morphisms (`Flow.Canonical`), `Flow.Sdk`, `Flow`. |
| [`sdk/flow_ir_wire.h`](sdk/flow_ir_wire.h) | SDK Wire (Freestanding) | Minimal zero-`flow.h`, zero-`<stdio.h>`, pure-GPR `"FIR1"` wire constants, opcodes & binary encoder primitives. |
| [`sdk/flow_ir.h`](sdk/flow_ir.h) | SDK IR (Freestanding) | Zero-heap, zero-`<stdio.h>`, zero-jump-table C17/C++20 `.fir` codec, arity-checked validator, QTT linearity gate & GPR evaluator. |
| [`sdk/flow_ir_io.h`](sdk/flow_ir_io.h) | SDK Host IO | `<stdio.h>` `flow_ir_load_file` / `flow_ir_save_file` wrappers. |
| [`sdk/searu_mc_gf2.h`](sdk/searu_mc_gf2.h) | SDK Synth | Standalone GF(2) ANF-RREF multiplicative-complexity synthesizer (`searu_solve_mc_gf2`). |
| [`sdk/flow_ir_synth.h`](sdk/flow_ir_synth.h) | SDK Synth | `.fir` compositional & whole-graph multiplicative-complexity pass. |
| [`sdk/flow_cli.c`](sdk/flow_cli.c) | SDK CLI | Zero-heap, zero-float CLI & `self-opt` / `fir-synth` driver (`build/flow`). |
| [`tests/`](tests/) | Verification | Standalone Lean + C17 verification gates (`atlas_gate.lean`, `ir_schema_gate.lean`, `ir_schema_gate.c`). |
| [`Makefile`](Makefile) | Build & Gates | Enforces Gate 0 (layer isolation & anti-domain-laundering), Gate 1 (Ring-0 ISA), and Gates 2–5 (`leanchecker Flow.Math` & `Flow`, `SymbioticDuality`, freestanding Schema/C++20, `.fir`). |
| [`docs/`](docs/) | Theory & Witness | Thin [`SymbioticDuality.lean`](docs/SymbioticDuality.lean) executable witness importing `Flow.Bundle`, formal paper & zh-TW guide. |
| [`VERSION`](VERSION) | Release | `8.0.0` |

---

## License

Released under the [MIT License](LICENSE).
