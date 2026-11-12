/-
  FlowVerify.lean
  ================================================================================
  FLOW L0 — 3-Way Differential Equivalence Harness (`FlowSpec == Native == MCU32`)
  ================================================================================
  Mechanically verifies Lock 3 of FLOW's Lean 4 SSOT contract:
    1. Live `FlowSpec.lean` FNV-1a hash == compiled `FLOW_SSOT_SPEC_HASH` in both
       native ASM and 32-bit C17 fallback translation units.
    2. `0-ULP` bitwise 3-way equivalence across `FlowSpec` (pure Lean 4),
       `flow_native` (ARM64/x86_64 ASM + `__int128`), and `flow_mcu32`
       (`FLOW_FORCE_C17_FALLBACK` + `FLOW_FORCE_32BIT_FALLBACK`) over the core
       algebraic, Goldilocks F_p, Chebyshev sqrt, and 2-Jet Citardauq operators.
-/

import FlowSpec

namespace FlowVerify

@[extern "flow_native_ssot_spec_hash"] opaque nativeSpecHash (d : UInt8) : UInt64
@[extern "flow_mcu32_ssot_spec_hash"] opaque mcu32SpecHash (d : UInt8) : UInt64

@[extern "flow_native_clz64_nz"] opaque nativeClz64 (x : UInt64) : UInt32
@[extern "flow_mcu32_clz64_nz"] opaque mcu32Clz64 (x : UInt64) : UInt32
@[extern "flow_native_ctz64_nz"] opaque nativeCtz64 (x : UInt64) : UInt32
@[extern "flow_mcu32_ctz64_nz"] opaque mcu32Ctz64 (x : UInt64) : UInt32
@[extern "flow_native_popcnt64"] opaque nativePopcnt64 (x : UInt64) : UInt32
@[extern "flow_mcu32_popcnt64"] opaque mcu32Popcnt64 (x : UInt64) : UInt32
@[extern "flow_native_rbit64"] opaque nativeRbit64 (x : UInt64) : UInt64
@[extern "flow_mcu32_rbit64"] opaque mcu32Rbit64 (x : UInt64) : UInt64

@[extern "flow_native_mul_u64_hi"] opaque nativeMulHi (a b : UInt64) : UInt64
@[extern "flow_mcu32_mul_u64_hi"] opaque mcu32MulHi (a b : UInt64) : UInt64
@[extern "flow_native_mul_u64_lo"] opaque nativeMulLo (a b : UInt64) : UInt64
@[extern "flow_mcu32_mul_u64_lo"] opaque mcu32MulLo (a b : UInt64) : UInt64

@[extern "flow_native_isqrt_u64"] opaque nativeIsqrtU64 (x : UInt64) : UInt64
@[extern "flow_mcu32_isqrt_u64"] opaque mcu32IsqrtU64 (x : UInt64) : UInt64

@[extern "flow_native_fp64_reduce"] opaque nativeFp64Reduce (x : UInt64) : UInt64
@[extern "flow_mcu32_fp64_reduce"] opaque mcu32Fp64Reduce (x : UInt64) : UInt64
@[extern "flow_native_fp64_add"] opaque nativeFp64Add (a b : UInt64) : UInt64
@[extern "flow_mcu32_fp64_add"] opaque mcu32Fp64Add (a b : UInt64) : UInt64
@[extern "flow_native_fp64_sub"] opaque nativeFp64Sub (a b : UInt64) : UInt64
@[extern "flow_mcu32_fp64_sub"] opaque mcu32Fp64Sub (a b : UInt64) : UInt64
@[extern "flow_native_fp64_mul"] opaque nativeFp64Mul (a b : UInt64) : UInt64
@[extern "flow_mcu32_fp64_mul"] opaque mcu32Fp64Mul (a b : UInt64) : UInt64
@[extern "flow_native_fp64_sponge"] opaque nativeFp64Sponge (s w : UInt64) : UInt64
@[extern "flow_mcu32_fp64_sponge"] opaque mcu32Fp64Sponge (s w : UInt64) : UInt64

@[extern "flow_native_make_tag"] opaque nativeMakeTag (f : UInt16) (g v ax : UInt8) (c : UInt16) : UInt64
@[extern "flow_mcu32_make_tag"] opaque mcu32MakeTag (f : UInt16) (g v ax : UInt8) (c : UInt16) : UInt64
@[extern "flow_native_is_tag"] opaque nativeIsTag (b : UInt64) : UInt8
@[extern "flow_mcu32_is_tag"] opaque mcu32IsTag (b : UInt64) : UInt8
@[extern "flow_native_tag_code"] opaque nativeTagCode (b : UInt64) : UInt16
@[extern "flow_mcu32_tag_code"] opaque mcu32TagCode (b : UInt64) : UInt16

@[extern "flow_native_ternary_dot"] opaque nativeTernaryDot (nza sga nzb sgb : UInt64) : UInt64
@[extern "flow_mcu32_ternary_dot"] opaque mcu32TernaryDot (nza sga nzb sgb : UInt64) : UInt64

@[extern "flow_native_jet2_solve_root"] opaque nativeJet2SolveRoot (v d1 d2 tmax : UInt64) : UInt64
@[extern "flow_mcu32_jet2_solve_root"] opaque mcu32Jet2SolveRoot (v d1 d2 tmax : UInt64) : UInt64
@[extern "flow_native_jet2_eval_taylor"] opaque nativeJet2EvalTaylor (v d1 d2 dt : UInt64) : UInt64
@[extern "flow_mcu32_jet2_eval_taylor"] opaque mcu32Jet2EvalTaylor (v d1 d2 dt : UInt64) : UInt64

@[extern "flow_native_capsule64_mac"] opaque nativeCapsule64Mac (seed : UInt64) : UInt64
@[extern "flow_mcu32_capsule64_mac"] opaque mcu32Capsule64Mac (seed : UInt64) : UInt64

def fnv1a64 (bytes : ByteArray) : UInt64 :=
  bytes.foldl (init := FlowSpec.fnvOffset64) fun h b =>
    (h ^^^ b.toUInt64) * FlowSpec.fnvPrime64

def leanPopcnt64 (x : UInt64) : UInt32 :=
  let rec loop (n : Nat) (cnt : Nat) : Nat :=
    match cnt with
    | 0 => 0
    | s + 1 => (n &&& 1) + loop (n >>> 1) s
  UInt32.ofNat (loop x.toNat 64)

def leanCtz64 (x : UInt64) : UInt32 :=
  if x == 0 then 64 else
    let rec loop (n : Nat) (idx : Nat) : Nat :=
      if idx ≥ 64 then 64
      else if (n >>> idx) &&& 1 == 1 then idx
      else loop n (idx + 1)
    UInt32.ofNat (loop x.toNat 0)

def leanRbit64 (x : UInt64) : UInt64 :=
  let rec loop (n : Nat) (acc : Nat) (step : Nat) : Nat :=
    match step with
    | 0 => acc
    | s + 1 => loop (n >>> 1) ((acc <<< 1) ||| (n &&& 1)) s
  UInt64.ofNat (loop x.toNat 0 64)

def leanTernaryDot (nza sga nzb sgb : UInt64) : UInt64 :=
  let act := nza &&& nzb
  let sa := sga &&& nza
  let sb := sgb &&& nzb
  let xorSign := sa ^^^ sb
  let sameSign := act &&& (~~~xorSign)
  let oppSign := act &&& xorSign
  let pSame := (leanPopcnt64 sameSign).toNat
  let pOpp := (leanPopcnt64 oppSign).toNat
  let diff : Int := (pSame : Int) - (pOpp : Int)
  UInt64.ofInt diff

def leanSampleCapsuleMac (seed : UInt64) : UInt64 :=
  let w0 := seed ^^^ ((1 : UInt64) * FlowSpec.goldenRatio64)
  let w1 := seed ^^^ ((2 : UInt64) * FlowSpec.goldenRatio64)
  let w2 := seed ^^^ ((3 : UInt64) * FlowSpec.goldenRatio64)
  let w3 := seed ^^^ ((4 : UInt64) * FlowSpec.goldenRatio64)
  let w4 := seed ^^^ ((5 : UInt64) * FlowSpec.goldenRatio64)
  let w5 := seed ^^^ ((6 : UInt64) * FlowSpec.goldenRatio64)
  let w6 := seed ^^^ ((7 : UInt64) * FlowSpec.goldenRatio64)
  let h0 := FlowSpec.fp64SpongeStep FlowSpec.goldenRatio64 seed
  let h1 := FlowSpec.fp64SpongeStep h0 w0
  let h2 := FlowSpec.fp64SpongeStep h1 w1
  let h3 := FlowSpec.fp64SpongeStep h2 w2
  let h4 := FlowSpec.fp64SpongeStep h3 w3
  let h5 := FlowSpec.fp64SpongeStep h4 w4
  let h6 := FlowSpec.fp64SpongeStep h5 w5
  let h7 := FlowSpec.fp64SpongeStep h6 w6
  if h7 == 0 then 1 else h7

def prngNext (s : UInt64) : UInt64 :=
  s * 6364136223846793005 + 1442695040888963407

def requireEq {α : Type} [BEq α] [Repr α] (label : String) (a b : α) : IO Unit := do
  if a != b then
    IO.println s!"[FAIL] {label}: expected {repr a}, got {repr b}"
    throw (IO.userError s!"Verification failure: {label}")

end FlowVerify

open FlowVerify

def main : IO UInt32 := do
  let mut checks : Nat := 0

  -- 1. Spec Hash Lock
  let specBytes ← IO.FS.readBinFile "FlowSpec.lean"
  let liveHash := specBytes.foldl (init := FlowSpec.fnvOffset64) fun h b =>
    (h ^^^ b.toUInt64) * FlowSpec.fnvPrime64
  let natHash := nativeSpecHash 0
  let mcuHash := mcu32SpecHash 0
  requireEq "SpecHash Live == Native" liveHash natHash
  requireEq "SpecHash Live == MCU32" liveHash mcuHash
  checks := checks + 2

  -- 2. Fixed edge cases for Bit Algebra, Fp64, Chebyshev Sqrt, BoundaryTag
  let edge64 : Array UInt64 := #[
    0, 1, 2, 3, 0x5555555555555555, 0xAAAAAAAAAAAAAAAA,
    0x7FFFFFFFFFFFFFFF, 0x8000000000000000, 0xFFFFFFFF00000000,
    0x00000000FFFFFFFF, 0xFFFFFFFF00000001, 0xFFFFFFFFFFFFFFFE,
    0xFFFFFFFFFFFFFFFF
  ]

  for x in edge64 do
    let expClz := UInt32.ofNat (FlowSpec.clz64 x)
    requireEq "clz64 Native" expClz (nativeClz64 x)
    requireEq "clz64 MCU32" expClz (mcu32Clz64 x)

    let expCtz := leanCtz64 x
    requireEq "ctz64 Native" expCtz (nativeCtz64 x)
    requireEq "ctz64 MCU32" expCtz (mcu32Ctz64 x)

    let expPop := leanPopcnt64 x
    requireEq "popcnt64 Native" expPop (nativePopcnt64 x)
    requireEq "popcnt64 MCU32" expPop (mcu32Popcnt64 x)

    let expRbit := leanRbit64 x
    requireEq "rbit64 Native" expRbit (nativeRbit64 x)
    requireEq "rbit64 MCU32" expRbit (mcu32Rbit64 x)

    let expSqrt := FlowSpec.isqrtU64Fast x
    requireEq "isqrt_u64 Native" expSqrt (nativeIsqrtU64 x)
    requireEq "isqrt_u64 MCU32" expSqrt (mcu32IsqrtU64 x)

    let expRed := FlowSpec.fp64Reduce x
    requireEq "fp64_reduce Native" expRed (nativeFp64Reduce x)
    requireEq "fp64_reduce MCU32" expRed (mcu32Fp64Reduce x)

    let expIsTag := if FlowSpec.isBoundaryTag x then (1 : UInt8) else (0 : UInt8)
    requireEq "is_tag Native" expIsTag (nativeIsTag x)
    requireEq "is_tag MCU32" expIsTag (mcu32IsTag x)

    checks := checks + 14

  -- 3. Binary Operators across Edge Pairs
  for a in edge64 do
    for b in edge64 do
      let (hi, lo) := FlowSpec.mulU64Limb32 a b
      requireEq "mul_u64_hi Native" hi (nativeMulHi a b)
      requireEq "mul_u64_hi MCU32" hi (mcu32MulHi a b)
      requireEq "mul_u64_lo Native" lo (nativeMulLo a b)
      requireEq "mul_u64_lo MCU32" lo (mcu32MulLo a b)

      let expAdd := FlowSpec.fp64Add a b
      requireEq "fp64_add Native" expAdd (nativeFp64Add a b)
      requireEq "fp64_add MCU32" expAdd (mcu32Fp64Add a b)

      let expSub := FlowSpec.fp64Sub a b
      requireEq "fp64_sub Native" expSub (nativeFp64Sub a b)
      requireEq "fp64_sub MCU32" expSub (mcu32Fp64Sub a b)

      let expMul := FlowSpec.fp64MulFast a b
      requireEq "fp64_mul Native" expMul (nativeFp64Mul a b)
      requireEq "fp64_mul MCU32" expMul (mcu32Fp64Mul a b)

      let expSp := FlowSpec.fp64SpongeStep a b
      requireEq "fp64_sponge Native" expSp (nativeFp64Sponge a b)
      requireEq "fp64_sponge MCU32" expSp (mcu32Fp64Sponge a b)

      checks := checks + 10

  -- 4. BoundaryTag roundtrip
  for fnId in [1, 10, 100, 4095] do
    for code in [0, 1, 42, 4095] do
      let tag := FlowSpec.makeBoundaryTag (UInt16.ofNat fnId) 1 2 3 (UInt16.ofNat code)
      requireEq "make_tag Native" tag (nativeMakeTag (UInt16.ofNat fnId) 1 2 3 (UInt16.ofNat code))
      requireEq "make_tag MCU32" tag (mcu32MakeTag (UInt16.ofNat fnId) 1 2 3 (UInt16.ofNat code))
      requireEq "is_tag roundtrip" (1 : UInt8) (nativeIsTag tag)
      requireEq "tag_code roundtrip" (UInt16.ofNat code) (nativeTagCode tag)
      checks := checks + 4

  -- 5. PRNG Differential Sweep (512 iterations)
  let mut rng : UInt64 := 0x123456789ABCDEF0
  for i in [0:512] do
    rng := prngNext rng
    let a := rng
    rng := prngNext rng
    let b := rng

    -- Bit Ops
    let expClz := UInt32.ofNat (FlowSpec.clz64 a)
    requireEq "clz64 PRNG Native" expClz (nativeClz64 a)
    requireEq "clz64 PRNG MCU32" expClz (mcu32Clz64 a)

    let expCtz := leanCtz64 a
    requireEq "ctz64 PRNG Native" expCtz (nativeCtz64 a)
    requireEq "ctz64 PRNG MCU32" expCtz (mcu32Ctz64 a)

    let expPop := leanPopcnt64 a
    requireEq "popcnt64 PRNG Native" expPop (nativePopcnt64 a)
    requireEq "popcnt64 PRNG MCU32" expPop (mcu32Popcnt64 a)

    let expRbit := leanRbit64 a
    requireEq "rbit64 PRNG Native" expRbit (nativeRbit64 a)
    requireEq "rbit64 PRNG MCU32" expRbit (mcu32Rbit64 a)

    -- Multiplier
    let (hi, lo) := FlowSpec.mulU64Limb32 a b
    requireEq "mul_hi PRNG Native" hi (nativeMulHi a b)
    requireEq "mul_hi PRNG MCU32" hi (mcu32MulHi a b)
    requireEq "mul_lo PRNG Native" lo (nativeMulLo a b)
    requireEq "mul_lo PRNG MCU32" lo (mcu32MulLo a b)

    -- Sqrt
    let expSqrt := FlowSpec.isqrtU64Fast a
    requireEq "isqrt PRNG Native" expSqrt (nativeIsqrtU64 a)
    requireEq "isqrt PRNG MCU32" expSqrt (mcu32IsqrtU64 a)

    -- Fp64
    let expRed := FlowSpec.fp64Reduce a
    requireEq "fp64_red PRNG Native" expRed (nativeFp64Reduce a)
    requireEq "fp64_red PRNG MCU32" expRed (mcu32Fp64Reduce a)

    let expMul := FlowSpec.fp64MulFast a b
    requireEq "fp64_mul PRNG Native" expMul (nativeFp64Mul a b)
    requireEq "fp64_mul PRNG MCU32" expMul (mcu32Fp64Mul a b)

    -- Ternary Dot
    rng := prngNext rng
    let nzb := rng
    rng := prngNext rng
    let sgb := rng
    let expTern := leanTernaryDot a b nzb sgb
    requireEq "ternary_dot PRNG Native" expTern (nativeTernaryDot a b nzb sgb)
    requireEq "ternary_dot PRNG MCU32" expTern (mcu32TernaryDot a b nzb sgb)

    -- 2-Jet Citardauq Solve Root
    rng := prngNext rng
    let vb := (rng >>> 20) + 1
    rng := prngNext rng
    let d1b := rng
    rng := prngNext rng
    let d2b := rng
    let tmax : UInt64 := 0x10000000
    let iv := Int64.ofUInt64 vb
    let id1 := Int64.ofUInt64 d1b
    let id2 := Int64.ofUInt64 d2b
    let it := Int64.ofUInt64 tmax
    let expRoot : UInt64 :=
      match FlowSpec.jet2SolveRootFast64 iv id1 id2 it with
      | some t => t.toUInt64
      | none => 0
    requireEq "jet2_solve_root PRNG Native" expRoot (nativeJet2SolveRoot vb d1b d2b tmax)
    requireEq "jet2_solve_root PRNG MCU32" expRoot (mcu32Jet2SolveRoot vb d1b d2b tmax)

    -- 2-Jet Taylor Eval Native == MCU32
    let natTaylor := nativeJet2EvalTaylor vb d1b d2b ((rng >>> 48) + 1)
    let mcuTaylor := mcu32Jet2EvalTaylor vb d1b d2b ((rng >>> 48) + 1)
    requireEq "jet2_eval_taylor PRNG Native == MCU32" natTaylor mcuTaylor

    -- Capsule MAC
    if i % 8 == 0 then
      let expMac := leanSampleCapsuleMac a
      requireEq "capsule64 MAC Native" expMac (nativeCapsule64Mac a)
      requireEq "capsule64 MAC MCU32" expMac (mcu32Capsule64Mac a)
      checks := checks + 2

    checks := checks + 19

  IO.println s!"  [PASS] FlowVerify 3-Way SSOT Equivalence: {checks} assertions (`FlowSpec == Native ASM == 32-Bit MCU` 0-ULP)"
  return 0
