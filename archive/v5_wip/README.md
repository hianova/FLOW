<p align="center">
  <img src="./assets/banner.svg" alt="FLOW — Formally Verified L0 Integer, HoTT &amp; 2-Jet Substrate" width="100%" />
</p>

# FLOW — Formally Verified L0 Integer, Cubical HoTT & Q48.16 2-Jet Substrate

`FLOW` is a standalone, zero-floating-point (`FloatOps == 0`), zero-heap (`HeapCalls == 0`), single-source-of-truth (SSOT) C17/ASM algebra, geometry, and external-constraint execution substrate. Every algebraic constant, hardware topology default, lookup table, and total integer operator is machine-checked in pure core Lean 4 (`0 sorryAx`, `lean -DwarningAsError=true`) and verified bit-exact (`0-ULP`) across hardware assembly and 32-bit freestanding targets.

`FLOW` builds two static archives from the same verified source tree:

- **`build/libflow.a` (Freestanding Bare-Metal / Ring-0 Core)**: Compiles [`src/flow_qpoly.c`](src/flow_qpoly.c) with [`src/flow_asm.h`](src/flow_asm.h), [`src/flow_hott_jet.h`](src/flow_hott_jet.h), and [`src/flow_ssot_gen.h`](src/flow_ssot_gen.h). Requires **zero OS headers**, **zero `<stdatomic.h>`**, **zero FPU instructions**, and **zero undefined runtime symbols** (`nm -u == empty` under `-ffreestanding -nostdlib -fno-builtin` on `x86_64`, `AArch64`, `RV32IMAC`, and `ARMv7-M`).
- **`build/libflow_rt.a` (Zero-Heap Hosted Runtime Extension)**: Supersets `libflow.a` with [`src/flow_io.h`](src/flow_io.h) and [`src/flow_core.c`](src/flow_core.c), adding cache-line padded lock-free SPSC rings (`FLOW_SPSC_DEFINE`), page-aligned shared UMA pools (`flow_uma_buffer_alloc`), and caller-allocated lock-free QSBR pointer hot-swapping (`flow_reload_init` / `flow_qsbr_publish_ptr`) with **zero `malloc`/`calloc`/`free` symbols**.

---

## 1. Three-Lock Lean 4 SSOT Architecture

```
  ┌──────────────────────────────────────────────────────────────────────────┐
  │ Lock 1: Formal Specification & Proofs (FlowSpec.lean)                    │
  │   • Pure Lean 4 kernel (`0 sorryAx`, `#print axioms` standard-only)      │
  │   • Proves 32-bit 64x64->128 limb multiplication, Goldilocks F_p field   │
  │     algebra & x^7 S-box bijection, 192-entry Chebyshev minimax isqrt,    │
  │     10-bit hardware sieve, Cubical Kan steps, Schmitt Hi/Lo deadbands,   │
  │     128-bit conservation splits, and 64-byte capsule sponge seals        │
  └────────────────────────────────────┬─────────────────────────────────────┘
                                       │
                                       ▼
  ┌──────────────────────────────────────────────────────────────────────────┐
  │ Lock 2: Deterministic Header & Table Emission (FlowGen.lean)             │
  │   • Emits src/flow_ssot_gen.h directly from FlowSpec definitions         │
  │   • Seals output with 64-bit FNV-1a FLOW_SSOT_SPEC_HASH                  │
  │   • Eliminates handwritten magic numbers, masks, and lookup tables       │
  └────────────────────────────────────┬─────────────────────────────────────┘
                                       │
                                       ▼
  ┌──────────────────────────────────────────────────────────────────────────┐
  │ Lock 3: 3-Way Differential Equivalence (FlowVerify.lean + FFI)           │
  │   • Verifies live FlowSpec.lean hash == compiled Native & MCU32 headers  │
  │   • Enforces 0-ULP bitwise identity: FlowSpec == Native ASM == 32b MCU   │
  └──────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Repository Structure & Module Reference

| File | Layer | Responsibility |
| :--- | :--- | :--- |
| [`FlowSpec.lean`](FlowSpec.lean) | Lock 1 (SSOT) | Authoritative mathematical definitions and machine-checked proofs for all L0 constants, hardware defaults, Chebyshev `isqrt` table entries, Goldilocks $\mathbb{F}_p$ ($p = 2^{64} - 2^{32} + 1$) arithmetic, 10-bit sieve axes, Cubical HoTT Kan homotopy, 2-Jet Citardauq root solving, Schmitt hysteresis, and 128-bit conservation ratio splits. |
| [`FlowGen.lean`](FlowGen.lean), [`src/flow_ssot_gen.h`](src/flow_ssot_gen.h) | Lock 2 (Codegen) | Deterministic generator emitting `FLOW_SSOT_*` macros and `FLOW_SSOT_ISQRT_LIN_TABLE_INIT` sealed by `FLOW_SSOT_SPEC_HASH`. |
| [`FlowVerify.lean`](FlowVerify.lean), [`tests/flow_verify_ffi.c`](tests/flow_verify_ffi.c) | Lock 3 (Audit) | Dual-compiled FFI differential harness (`flow_native` hardware ASM vs `flow_mcu32` pure 32-bit C17 fallback) proving `0-ULP` runtime equivalence against `FlowSpec`. |
| [`src/flow_asm.h`](src/flow_asm.h) | Freestanding L0 | Total integer ISA substrate: flag-capturing signed Q48.16 arithmetic (`adds`/`subs`/`smulh`), software-register 128-bit division (`flow_u128_div_u128`), single-`udiv` Chebyshev `flow_asm_isqrt_u64_fast`, 32-bit limb 128-bit fallback (`FlowU128Pair`/`FlowI128Pair`), $\mathbb{Z}/2^k\mathbb{Z}$ masked arena addressing, Wave64 `1.58-bit` ternary dot products, Goldilocks $\mathbb{F}_p$ sponge (`flow_asm_fp64_sponge_step`), 128-bit conservation split (`flow_qsplit_u64`), and 64-byte capsule sealing (`flow_capsule64_seal` / `flow_capsule64_verify`). |
| [`src/flow_hott_jet.h`](src/flow_hott_jet.h) | Freestanding L0 | Compile-time layout univalence (`FLOW_STATIC_ASSERT_UNIVALENT`), Cubical De Morgan / HIT quotient / Kan neighbor step (`flow_cubical_step_neighbor`), saturated Q48.16 scalar helpers (`flow_q16_*_sat`), Q48.16 2-Jet fiber algebra & quadratic root solvers (`flow_jet2_solve_root`, `flow_jet2_solve_root_fast64`), 1D dual-threshold Schmitt hysteresis (`flow_schmitt_hyst_hi_i64`, `flow_schmitt_hyst_lo_i64`), and pure-value `FLOW_QPOLY_DEFINE_EX` polyhedra. |
| [`src/flow_qpoly.c`](src/flow_qpoly.c) | `libflow.a` | Pure freestanding conservative rational-to-fixed-point inner polyhedron quantization (`flow_qpoly_quantize_inner`) and 2-Jet next-face impact predictor (`flow_qpoly_predict_next_face`). |
| [`src/flow_io.h`](src/flow_io.h), [`src/flow_core.c`](src/flow_core.c) | `libflow_rt.a` | Cache-line isolated lock-free SPSC ring (`FLOW_SPSC_DEFINE`), POSIX page-aligned UMA shared memory (`flow_uma_buffer_alloc`), and zero-heap caller-allocated lock-free QSBR epoch hot-swap (`flow_reload_init`, `flow_reload_fini`, `flow_qsbr_publish_ptr`, `flow_qsbr_load_ptr`). |

---

## 3. Hardware Topology Defaults & Compile-Time Overrides

Standard hardware topology constants are derived from [`FlowSpec.lean`](FlowSpec.lean) into [`src/flow_ssot_gen.h`](src/flow_ssot_gen.h) and exposed in [`src/flow_asm.h`](src/flow_asm.h) behind `#ifndef` guards so specialized silicon targets can override any parameter via compiler flags (`-D...`):

| Macro | SSOT Default | Notes / Override Flag |
| :--- | :--- | :--- |
| `FLOW_HW_PAGE_BYTES` | `16384U` | Override via `-DFLOW_HW_PAGE_BYTES=4096U` |
| `FLOW_HW_CACHE_LINE_BYTES` | `128U` (Apple Silicon) / `64U` (default) | Override via `-DFLOW_HW_CACHE_LINE_BYTES=64U` |
| `FLOW_HW_L1D_BYTES` | `65536U` | Compile-time upper bound for `FLOW_QPOLY_DEFINE_EX` |
| `FLOW_HW_VECTOR_BYTES` | `16U` | Override via `-DFLOW_HW_VECTOR_BYTES=32U` / `64U` |
| `FLOW_HW_WAVE_LANES` | `64U` | Wave64 ballot & ternary register width |
| `FLOW_HW_FALLBACK_X86_HZ` | `3000000000ULL` | Override via `-DFLOW_HW_FALLBACK_X86_HZ=...` |
| `FLOW_HW_FALLBACK_ARM_HZ` | `24000000ULL` | Used only if `cntfrq_el0` reads `0` |

---

## 4. Minimal C17 Example

### Freestanding Polyhedron, Schmitt Hysteresis & 64-Byte Capsule Seal (`libflow.a`)

```c
#include <assert.h>
#include "flow_hott_jet.h"

/* 64-byte cache-line aligned external rule capsule */
typedef struct __attribute__((aligned(64))) {
    int64_t  b_enter_q16;
    int64_t  b_exit_q16;
    int64_t  vel_q16;
    int64_t  acc_q16;
    uint64_t quota_total;
    uint64_t ratio_num;
    uint64_t ratio_den;
    uint64_t mac; /* Offset 56..63: Goldilocks F_p sponge seal */
} RuleCapsule64;

_Static_assert(sizeof(RuleCapsule64) == 64, "Capsule must be 64 bytes");
FLOW_QPOLY_DEFINE_EX(ControlPoly1D, 1, 16, 4)

int main(void) {
    RuleCapsule64 cap = {
        .b_enter_q16 = 73728, /* 1.125 in Q48.16 */
        .b_exit_q16  = 57344, /* 0.875 in Q48.16 */
        .vel_q16     = -65536,
        .acc_q16     = 0,
        .quota_total = 1000000ULL,
        .ratio_num   = 250ULL,
        .ratio_den   = 10000ULL,
    };
    flow_capsule64_seal(&cap, FLOW_GOLDEN_RATIO_64);
    assert(flow_capsule64_verify(&cap, FLOW_GOLDEN_RATIO_64));

    ControlPoly1D poly;
    ControlPoly1D_init(&poly);
    poly.A[0][0] = 1;
    ControlPoly1D_set_hysteresis(&poly, 0U, cap.b_enter_q16, cap.b_exit_q16);

    int32_t x_obs[1] = {65536}; /* Inside [b_exit, b_enter] deadband */
    FlowQPolyEval ev = ControlPoly1D_eval_hysteresis(&poly, x_obs, 0ULL);
    assert(ev.violated_mask == 0ULL);

    uint64_t fee = 0ULL, net = 0ULL;
    (void)flow_qsplit_u64(cap.quota_total, cap.ratio_num, cap.ratio_den, &fee, &net);
    assert(fee + net == cap.quota_total);
    return 0;
}
```

### Zero-Heap Lock-Free Constraint Hot-Swap (`libflow_rt.a`)

```c
FlowReloadContext qsbr; /* Caller-allocated: stack, BSS, or engine struct (0 heap) */
FlowReloadReader reader;
flow_reload_init(&qsbr, NULL);
flow_reload_reader_register(&qsbr, &reader);

flow_qsbr_publish_ptr(&qsbr, &poly_bank_a);
const ControlPoly1D *active = (const ControlPoly1D *)flow_qsbr_load_ptr(&qsbr, &reader);
flow_qsbr_checkpoint(&reader);

flow_reload_reader_unregister(&reader);
flow_reload_fini(&qsbr);
```

---

## 5. Build & Verification Gates

```bash
# Regenerate src/flow_ssot_gen.h from FlowSpec.lean
make ssot-gen

# Enforce clang-format cleanliness across src/ and tests/
make fmt-check

# Clean build + Gate 0..3 verification suite
make clean && make test
```

`make test` executes all verification gates in sequence:
1. **Archive Hygiene Gate**: Verifies `nm -u` on `build/obj/flow_qpoly.o` (`libflow.a`) is completely empty and `build/libflow_rt.a` contains zero heap allocator symbols (`malloc`/`calloc`/`realloc`/`free`).
2. **Gate 0 (`ssot-check`)**: Verifies `src/flow_ssot_gen.h` matches `FlowSpec.lean` byte-for-byte and runs `build/test-lean-ssot` (`FlowSpec == Native ASM == 32-Bit MCU` `0-ULP` differential equivalence).
3. **Bare-Metal Ring-0 Gate (`[0/3]`)**: Compiles `tests/test_freestanding_kernel.c` and `src/flow_qpoly.c` under `-ffreestanding -nostdlib -fno-builtin` across host, 32-bit fallback, `riscv32-unknown-none-elf` (`RV32IMAC` `-O2` & `-O0`), and `armv7m-none-eabi`, asserting zero undefined symbols.
4. **Total ASM & Sieve Suite (`[1/3]`, `build/test-asm-sieve`)**: Signed overflow flag capture, Q48.16 operators, masked cyclic arenas, Wave64 ternary dot products, Goldilocks $\mathbb{F}_p$ sponge, `rbit64`, and 2-Jet Taylor self-checking.
5. **External-Constraint & QSBR Suite (`[2/3]`, `build/test-ext-constraint`)**: Compile-time univalence, multi-dimensional `FLOW_QPOLY_DEFINE_EX`, dual-threshold hysteresis, 2-Jet worldline anti-tunneling, and multi-threaded zero-heap QSBR hot-swap stress tests.
6. **Quantization & Zero-Fault Hot-Path Suite (`[3/3]`, `build/test-qpoly-poc`)**: Conservative inner quantization soundness, `10^7` polyhedron evaluations, `1000` quadratic trajectory face forecasts (`0 LSB` error), `10,000` Cubical Kan transitions, and `10^6` hot-path steps with `0` minor/major page faults and `0` voluntary context switches.

---

## License

Released under the [MIT License](LICENSE).
