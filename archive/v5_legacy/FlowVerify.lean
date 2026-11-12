/-
  FlowVerify.lean
  ================================================================================
  FLOW L0 — 3-Way Differential Equivalence Harness (`FlowSpec == Native == MCU32`)
  ================================================================================
  Mechanically verifies Lock 3 of FLOW's Lean 4 SSOT contract:
    1. Live `FlowSpec.lean` FNV-1a hash == compiled `FLOW_SSOT_SPEC_HASH` in both
       native ASM and 32-bit C17 fallback translation units.
    2. `0-ULP` bitwise equivalence across `FlowSpec` (pure Lean 4), `flow_native`
       (ARM64/x86_64 ASM + `__int128`), and `flow_mcu32` (`FLOW_FORCE_C17_FALLBACK`
       + `FLOW_FORCE_32BIT_FALLBACK`) over stratified boundaries, normalization
       regimes, boundary tags, horizon sieves, and PRNG sweeps.
-/

import FlowSpec

namespace FlowVerify

@[extern "flow_native_ssot_spec_hash"] opaque nativeSpecHash (d : UInt8) : UInt64
@[extern "flow_mcu32_ssot_spec_hash"] opaque mcu32SpecHash (d : UInt8) : UInt64

@[extern "flow_native_isqrt_u64"] opaque nativeIsqrtU64 (x : UInt64) : UInt64
@[extern "flow_mcu32_isqrt_u64"] opaque mcu32IsqrtU64 (x : UInt64) : UInt64

@[extern "flow_native_qsqrt_val"] opaque nativeQsqrtVal (x : UInt64) : UInt64
@[extern "flow_mcu32_qsqrt_val"] opaque mcu32QsqrtVal (x : UInt64) : UInt64
@[extern "flow_native_qsqrt_tag"] opaque nativeQsqrtTag (x : UInt64) : UInt64
@[extern "flow_mcu32_qsqrt_tag"] opaque mcu32QsqrtTag (x : UInt64) : UInt64

@[extern "flow_native_qlerp_val"] opaque nativeQlerpVal (x0 x1 a : UInt64) : UInt64
@[extern "flow_mcu32_qlerp_val"] opaque mcu32QlerpVal (x0 x1 a : UInt64) : UInt64
@[extern "flow_native_qlerp_tag"] opaque nativeQlerpTag (x0 x1 a : UInt64) : UInt64
@[extern "flow_mcu32_qlerp_tag"] opaque mcu32QlerpTag (x0 x1 a : UInt64) : UInt64

@[extern "flow_native_qmargin3_val"] opaque nativeQmargin3Val (a b c : UInt64) : UInt64
@[extern "flow_mcu32_qmargin3_val"] opaque mcu32Qmargin3Val (a b c : UInt64) : UInt64
@[extern "flow_native_qmargin3_sieve"] opaque nativeQmargin3Sieve (a b c : UInt64) : UInt64
@[extern "flow_mcu32_qmargin3_sieve"] opaque mcu32Qmargin3Sieve (a b c : UInt64) : UInt64

@[extern "flow_native_fp64_add"] opaque nativeFp64Add (a b : UInt64) : UInt64
@[extern "flow_mcu32_fp64_add"] opaque mcu32Fp64Add (a b : UInt64) : UInt64
@[extern "flow_native_fp64_sub"] opaque nativeFp64Sub (a b : UInt64) : UInt64
@[extern "flow_mcu32_fp64_sub"] opaque mcu32Fp64Sub (a b : UInt64) : UInt64
@[extern "flow_native_fp64_mul"] opaque nativeFp64Mul (a b : UInt64) : UInt64
@[extern "flow_mcu32_fp64_mul"] opaque mcu32Fp64Mul (a b : UInt64) : UInt64
@[extern "flow_native_fp64_pow7"] opaque nativeFp64Pow7 (x : UInt64) : UInt64
@[extern "flow_mcu32_fp64_pow7"] opaque mcu32Fp64Pow7 (x : UInt64) : UInt64
@[extern "flow_native_fp64_sponge"] opaque nativeFp64Sponge (s w : UInt64) : UInt64
@[extern "flow_mcu32_fp64_sponge"] opaque mcu32Fp64Sponge (s w : UInt64) : UInt64

@[extern "flow_native_rbit64"] opaque nativeRbit64 (x : UInt64) : UInt64
@[extern "flow_mcu32_rbit64"] opaque mcu32Rbit64 (x : UInt64) : UInt64

@[extern "flow_native_make_tag"] opaque nativeMakeTag (f : UInt16) (g v ax : UInt8) (c : UInt16) : UInt64
@[extern "flow_mcu32_make_tag"] opaque mcu32MakeTag (f : UInt16) (g v ax : UInt8) (c : UInt16) : UInt64
@[extern "flow_native_is_tag"] opaque nativeIsTag (b : UInt64) : UInt8
@[extern "flow_mcu32_is_tag"] opaque mcu32IsTag (b : UInt64) : UInt8
@[extern "flow_native_tag_code"] opaque nativeTagCode (b : UInt64) : UInt16
@[extern "flow_mcu32_tag_code"] opaque mcu32TagCode (b : UInt64) : UInt16

@[extern "flow_native_kan_step_mask"] opaque nativeKanMask (m : UInt64) (b : UInt32) (f : UInt64) : UInt64
@[extern "flow_mcu32_kan_step_mask"] opaque mcu32KanMask (m : UInt64) (b : UInt32) (f : UInt64) : UInt64
@[extern "flow_native_kan_step_status"] opaque nativeKanStatus (m : UInt64) (b : UInt32) (f : UInt64) : UInt8
@[extern "flow_mcu32_kan_step_status"] opaque mcu32KanStatus (m : UInt64) (b : UInt32) (f : UInt64) : UInt8

@[extern "flow_native_jet2_root_fast64"] opaque nativeJet2RootFast64 (v d1 d2 tmax : UInt64) : UInt64
@[extern "flow_mcu32_jet2_root_fast64"] opaque mcu32Jet2RootFast64 (v d1 d2 tmax : UInt64) : UInt64
@[extern "flow_native_jet2_root_128"] opaque nativeJet2Root128 (v d1 d2 tmax : UInt64) : UInt64
@[extern "flow_mcu32_jet2_root_128"] opaque mcu32Jet2Root128 (v d1 d2 tmax : UInt64) : UInt64
@[extern "flow_native_jet2_nonlinear_mac"] opaque nativeJet2NonlinearMac (av ad1 ad2 bv bd1 bd2 dt : UInt64) : UInt64
@[extern "flow_mcu32_jet2_nonlinear_mac"] opaque mcu32Jet2NonlinearMac (av ad1 ad2 bv bd1 bd2 dt : UInt64) : UInt64

@[extern "flow_native_schmitt_hyst_1d"] opaque nativeSchmitt1D (be bx x : UInt64) (prev : UInt8) : UInt8
@[extern "flow_mcu32_schmitt_hyst_1d"] opaque mcu32Schmitt1D (be bx x : UInt64) (prev : UInt8) : UInt8
@[extern "flow_native_schmitt_hyst_hi"] opaque nativeSchmittHi (be bx x : UInt64) (prev : UInt8) : UInt8
@[extern "flow_mcu32_schmitt_hyst_hi"] opaque mcu32SchmittHi (be bx x : UInt64) (prev : UInt8) : UInt8
@[extern "flow_native_schmitt_hyst_lo"] opaque nativeSchmittLo (be bx x : UInt64) (prev : UInt8) : UInt8
@[extern "flow_mcu32_schmitt_hyst_lo"] opaque mcu32SchmittLo (be bx x : UInt64) (prev : UInt8) : UInt8

@[extern "flow_native_qsplit_pri"] opaque nativeQsplitPri (tot num den : UInt64) : UInt64
@[extern "flow_mcu32_qsplit_pri"] opaque mcu32QsplitPri (tot num den : UInt64) : UInt64
@[extern "flow_native_qsplit_rem"] opaque nativeQsplitRem (tot num den : UInt64) : UInt64
@[extern "flow_mcu32_qsplit_rem"] opaque mcu32QsplitRem (tot num den : UInt64) : UInt64

@[extern "flow_native_capsule64_sample_mac"] opaque nativeCapsule64Mac (seed : UInt64) : UInt64
@[extern "flow_mcu32_capsule64_sample_mac"] opaque mcu32Capsule64Mac (seed : UInt64) : UInt64

def fnv1a64 (bytes : ByteArray) : UInt64 :=
  bytes.foldl (init := FlowSpec.fnvOffset64) fun h b =>
    (h ^^^ b.toUInt64) * FlowSpec.fnvPrime64

def prngNext (s : UInt64) : UInt64 :=
  let x0 := if s == 0 then FlowSpec.goldenRatio64 else s
  let x1 := x0 ^^^ (x0 <<< 13)
  let x2 := x1 ^^^ (x1 >>> 7)
  x2 ^^^ (x2 <<< 17)

def intToBits64 (v : Int) : UInt64 :=
  (Int64.ofInt v).toUInt64

def leanSampleCapsuleMac (seed : UInt64) : UInt64 :=
  let w0 := seed ^^^ ((1 : UInt64) * FlowSpec.goldenRatio64)
  let w1 := seed ^^^ ((2 : UInt64) * FlowSpec.goldenRatio64)
  let w2 := seed ^^^ ((3 : UInt64) * FlowSpec.goldenRatio64)
  let w3 := seed ^^^ ((4 : UInt64) * FlowSpec.goldenRatio64)
  let w4 := seed ^^^ ((5 : UInt64) * FlowSpec.goldenRatio64)
  let w5 := seed ^^^ ((6 : UInt64) * FlowSpec.goldenRatio64)
  let w6 := seed ^^^ ((7 : UInt64) * FlowSpec.goldenRatio64)
  FlowSpec.capsule64Mac7 seed w0 w1 w2 w3 w4 w5 w6

def requireEq {α : Type} [BEq α] [Repr α] (label : String) (a b : α) : IO Unit := do
  if !(a == b) then
    throw <| IO.userError s!"[FAIL] {label}: expected {reprStr a}, got {reprStr b}"

end FlowVerify

def main : IO UInt32 := do
  let mut checks : Nat := 0

  -- 1. Live FlowSpec.lean FNV-1a hash vs Native & MCU32 compiled headers
  let specBytes ← IO.FS.readBinFile "FlowSpec.lean"
  let liveHash := FlowVerify.fnv1a64 specBytes
  FlowVerify.requireEq "native SSOT_SPEC_HASH" liveHash (FlowVerify.nativeSpecHash 0)
  FlowVerify.requireEq "mcu32 SSOT_SPEC_HASH" liveHash (FlowVerify.mcu32SpecHash 0)
  checks := checks + 2

  -- 2. Stratified + PRNG 3-way `isqrt_u64_fast` & `rbit64`
  let edgeU64 : Array UInt64 := #[
    0, 1, 2, 3, 4, 5, 8, 9, 15, 16, 17, 24, 25, 26, 63, 64, 65,
    255, 256, 257, 65535, 65536, 65537,
    0xFFFFFFFF, 0x100000000, 0x100000001,
    0x3FFFFFFFFFFFFFFF, 0x4000000000000000, 0x4000000000000001,
    0xFFFFFFFE00000000, 0xFFFFFFFE00000001, 0xFFFFFFFE00000002,
    0xFFFFFFFFFFFFFFFE, 0xFFFFFFFFFFFFFFFF
  ]
  for x in edgeU64 do
    let sLean := FlowSpec.isqrtU64Fast x
    let sRef := UInt64.ofNat (Nat.sqrt x.toNat)
    FlowVerify.requireEq "isqrt Lean vs Nat.sqrt" sRef sLean
    FlowVerify.requireEq "isqrt Lean vs Native" sLean (FlowVerify.nativeIsqrtU64 x)
    FlowVerify.requireEq "isqrt Lean vs MCU32" sLean (FlowVerify.mcu32IsqrtU64 x)
    let rLean := FlowSpec.rbit64U64 x
    FlowVerify.requireEq "rbit64 Lean vs Native" rLean (FlowVerify.nativeRbit64 x)
    FlowVerify.requireEq "rbit64 Lean vs MCU32" rLean (FlowVerify.mcu32Rbit64 x)
    checks := checks + 5

  let mut rng : UInt64 := 0xCAFEBABE12345678
  for i in [0:4096] do
    rng := FlowVerify.prngNext rng
    let shift : UInt64 := UInt64.ofNat (i % 64)
    let x := rng >>> shift
    let sLean := FlowSpec.isqrtU64Fast x
    let sRef := UInt64.ofNat (Nat.sqrt x.toNat)
    FlowVerify.requireEq "isqrt sweep Ref" sRef sLean
    FlowVerify.requireEq "isqrt sweep Native" sLean (FlowVerify.nativeIsqrtU64 x)
    FlowVerify.requireEq "isqrt sweep MCU32" sLean (FlowVerify.mcu32IsqrtU64 x)
    let rLean := FlowSpec.rbit64U64 rng
    FlowVerify.requireEq "rbit64 sweep Native" rLean (FlowVerify.nativeRbit64 rng)
    FlowVerify.requireEq "rbit64 sweep MCU32" rLean (FlowVerify.mcu32Rbit64 rng)
    checks := checks + 5

  -- 3. Goldilocks F_p field algebra (add, sub, mul, pow7, sponge)
  let fpEdges : Array UInt64 := #[
    0, 1, 2, 7, 0xFFFFFFFF, 0x100000000,
    FlowSpec.goldilocksPrime - 2, FlowSpec.goldilocksPrime - 1,
    FlowSpec.goldilocksPrime, FlowSpec.goldilocksPrime + 1,
    0xFFFFFFFFFFFFFFFE, 0xFFFFFFFFFFFFFFFF
  ]
  for a in fpEdges do
    let p7 := FlowSpec.fp64Pow7 a
    FlowVerify.requireEq "fp64_pow7 Native" p7 (FlowVerify.nativeFp64Pow7 a)
    FlowVerify.requireEq "fp64_pow7 MCU32" p7 (FlowVerify.mcu32Fp64Pow7 a)
    checks := checks + 2
    for b in fpEdges do
      let vAdd := FlowSpec.fp64Add a b
      let vSub := FlowSpec.fp64Sub a b
      let vMul := FlowSpec.fp64MulFast a b
      let vSpg := FlowSpec.fp64SpongeStep a b
      FlowVerify.requireEq "fp64_add Native" vAdd (FlowVerify.nativeFp64Add a b)
      FlowVerify.requireEq "fp64_add MCU32" vAdd (FlowVerify.mcu32Fp64Add a b)
      FlowVerify.requireEq "fp64_sub Native" vSub (FlowVerify.nativeFp64Sub a b)
      FlowVerify.requireEq "fp64_sub MCU32" vSub (FlowVerify.mcu32Fp64Sub a b)
      FlowVerify.requireEq "fp64_mul Native" vMul (FlowVerify.nativeFp64Mul a b)
      FlowVerify.requireEq "fp64_mul MCU32" vMul (FlowVerify.mcu32Fp64Mul a b)
      FlowVerify.requireEq "fp64_sponge Native" vSpg (FlowVerify.nativeFp64Sponge a b)
      FlowVerify.requireEq "fp64_sponge MCU32" vSpg (FlowVerify.mcu32Fp64Sponge a b)
      checks := checks + 8

  for _ in [0:1024] do
    rng := FlowVerify.prngNext rng
    let a := rng
    rng := FlowVerify.prngNext rng
    let b := rng
    let vMul := FlowSpec.fp64MulFast a b
    let vSpg := FlowSpec.fp64SpongeStep a b
    FlowVerify.requireEq "fp64_mul PRNG Native" vMul (FlowVerify.nativeFp64Mul a b)
    FlowVerify.requireEq "fp64_mul PRNG MCU32" vMul (FlowVerify.mcu32Fp64Mul a b)
    FlowVerify.requireEq "fp64_sponge PRNG Native" vSpg (FlowVerify.nativeFp64Sponge a b)
    FlowVerify.requireEq "fp64_sponge PRNG MCU32" vSpg (FlowVerify.mcu32Fp64Sponge a b)
    checks := checks + 4

  -- 4. Q48.16 `qsqrt`, `qlerp`, `qmargin3`, and Boundary Pullback Tags
  for k in [0:512] do
    rng := FlowVerify.prngNext rng
    let xPos : UInt64 := (rng >>> 16) &&& 0xFFFFFFFFFFFF
    let qs := FlowSpec.qsqrtInteriorU64 xPos
    FlowVerify.requireEq "qsqrt interior Native" qs (FlowVerify.nativeQsqrtVal xPos)
    FlowVerify.requireEq "qsqrt interior MCU32" qs (FlowVerify.mcu32QsqrtVal xPos)
    FlowVerify.requireEq "qsqrt interior tag Native" 0 (FlowVerify.nativeQsqrtTag xPos)
    FlowVerify.requireEq "qsqrt interior tag MCU32" 0 (FlowVerify.mcu32QsqrtTag xPos)

    let x0 : Int := (Int.ofNat ((rng &&& 0xFFFFFFFF).toNat)) - 0x80000000
    rng := FlowVerify.prngNext rng
    let x1 : Int := (Int.ofNat ((rng &&& 0xFFFFFFFF).toNat)) - 0x80000000
    let alpha : Int := Int.ofNat ((k * 129) % 65537)
    let ql := FlowVerify.intToBits64 (FlowSpec.qlerpQ16Floor x0 x1 alpha)
    let bx0 := FlowVerify.intToBits64 x0
    let bx1 := FlowVerify.intToBits64 x1
    let ba := FlowVerify.intToBits64 alpha
    FlowVerify.requireEq "qlerp Native" ql (FlowVerify.nativeQlerpVal bx0 bx1 ba)
    FlowVerify.requireEq "qlerp MCU32" ql (FlowVerify.mcu32QlerpVal bx0 bx1 ba)
    FlowVerify.requireEq "qlerp tag Native" 0 (FlowVerify.nativeQlerpTag bx0 bx1 ba)
    FlowVerify.requireEq "qlerp tag MCU32" 0 (FlowVerify.mcu32QlerpTag bx0 bx1 ba)

    let mExact := FlowSpec.qmargin3Exact x0 x1 alpha
    let bm := FlowVerify.intToBits64 mExact
    FlowVerify.requireEq "qmargin3 Native" bm (FlowVerify.nativeQmargin3Val bx0 bx1 ba)
    FlowVerify.requireEq "qmargin3 MCU32" bm (FlowVerify.mcu32Qmargin3Val bx0 bx1 ba)
    let expSieve :=
      if mExact < 0 then (FlowSpec.sieveAllSound &&& (~~~FlowSpec.sieveQpolyInterior))
      else FlowSpec.sieveAllSound
    FlowVerify.requireEq "qmargin3 sieve Native" expSieve (FlowVerify.nativeQmargin3Sieve bx0 bx1 ba)
    FlowVerify.requireEq "qmargin3 sieve MCU32" expSieve (FlowVerify.mcu32Qmargin3Sieve bx0 bx1 ba)

    let fnId : UInt16 := UInt16.ofNat (k + 1)
    let gIdx : UInt8 := UInt8.ofNat (k % 16)
    let vKind : UInt8 := UInt8.ofNat ((k % 6) + 1)
    let cAxis : UInt8 := UInt8.ofNat (k % 64)
    let uCode : UInt16 := UInt16.ofNat ((k * 37) &&& 0x0FFF)
    let tag := FlowSpec.makeBoundaryTag fnId gIdx vKind cAxis uCode
    FlowVerify.requireEq "make_tag Native" tag (FlowVerify.nativeMakeTag fnId gIdx vKind cAxis uCode)
    FlowVerify.requireEq "make_tag MCU32" tag (FlowVerify.mcu32MakeTag fnId gIdx vKind cAxis uCode)
    FlowVerify.requireEq "is_tag Native" 1 (FlowVerify.nativeIsTag tag)
    FlowVerify.requireEq "is_tag MCU32" 1 (FlowVerify.mcu32IsTag tag)
    FlowVerify.requireEq "tag_code Native" uCode (FlowVerify.nativeTagCode tag)
    FlowVerify.requireEq "tag_code MCU32" uCode (FlowVerify.mcu32TagCode tag)
    checks := checks + 18

  -- Negative radicand & out-of-domain alpha boundary pullback checks
  let negOne := FlowVerify.intToBits64 (-65536)
  FlowVerify.requireEq "qsqrt neg boundary Native" true (FlowSpec.isBoundaryTag (FlowVerify.nativeQsqrtTag negOne))
  FlowVerify.requireEq "qsqrt neg boundary MCU32" true (FlowSpec.isBoundaryTag (FlowVerify.mcu32QsqrtTag negOne))
  let badAlpha := FlowVerify.intToBits64 70000
  FlowVerify.requireEq "qlerp bad alpha Native" true (FlowSpec.isBoundaryTag (FlowVerify.nativeQlerpTag 0 65536 badAlpha))
  FlowVerify.requireEq "qlerp bad alpha MCU32" true (FlowSpec.isBoundaryTag (FlowVerify.mcu32QlerpTag 0 65536 badAlpha))
  checks := checks + 4

  -- 5. Cubical Kan neighbor, Schmitt hysteresis deadband, adversarial Citardauq & 128-bit Jet2
  for bit in [0:66] do
    let b32 := UInt32.ofNat bit
    let mask : UInt64 := 0x0123456789ABCDEF
    let filt : UInt64 := 0x00FF00FF00FF00FF
    let (st, nextMask) := FlowSpec.cubicalStepNeighbor mask b32 filt
    let stCode : UInt8 := match st with | .filledHomotopic => 0 | .obstructedSingularity => 1
    FlowVerify.requireEq "kan mask Native" nextMask (FlowVerify.nativeKanMask mask b32 filt)
    FlowVerify.requireEq "kan mask MCU32" nextMask (FlowVerify.mcu32KanMask mask b32 filt)
    FlowVerify.requireEq "kan status Native" stCode (FlowVerify.nativeKanStatus mask b32 filt)
    FlowVerify.requireEq "kan status MCU32" stCode (FlowVerify.mcu32KanStatus mask b32 filt)
    checks := checks + 4

  let bEnter : Int := 73728
  let bExit : Int := 57344
  for xVal in #[40000, 57344, 57345, 65536, 73728, 73729, 90000] do
    for prev in #[false, true] do
      let expB := if FlowSpec.schmittHystStep bEnter bExit xVal prev then (1 : UInt8) else 0
      let expHi := if FlowSpec.schmittHystHi bEnter bExit xVal prev then (1 : UInt8) else 0
      let expLo := if FlowSpec.schmittHystLo bExit bEnter xVal prev then (1 : UInt8) else 0
      let pU8 : UInt8 := if prev then 1 else 0
      let beBits := FlowVerify.intToBits64 bEnter
      let bxBits := FlowVerify.intToBits64 bExit
      let xBits := FlowVerify.intToBits64 xVal
      FlowVerify.requireEq "schmitt 1D Native" expB (FlowVerify.nativeSchmitt1D beBits bxBits xBits pU8)
      FlowVerify.requireEq "schmitt 1D MCU32" expB (FlowVerify.mcu32Schmitt1D beBits bxBits xBits pU8)
      FlowVerify.requireEq "schmitt Hi Native" expHi (FlowVerify.nativeSchmittHi beBits bxBits xBits pU8)
      FlowVerify.requireEq "schmitt Hi MCU32" expHi (FlowVerify.mcu32SchmittHi beBits bxBits xBits pU8)
      FlowVerify.requireEq "schmitt Lo Native" expLo (FlowVerify.nativeSchmittLo bxBits beBits xBits pU8)
      FlowVerify.requireEq "schmitt Lo MCU32" expLo (FlowVerify.mcu32SchmittLo bxBits beBits xBits pU8)
      checks := checks + 6

  -- Adversarial stratified cases for `jet2SolveRootFast64` (including `r_i < 0`, `or_m < 4096`,
  -- `or_m >= 2^30`, `INT64_MIN`, `makeBoundaryTag`, and convex/concave horizon rejections)
  let bTagBits := FlowSpec.makeBoundaryTag 7 1 2 3 0x0BEE
  let i64MinBits : UInt64 := 0x8000000000000000
  let jetRawCases : Array (UInt64 × UInt64 × UInt64 × UInt64) := #[
    (FlowVerify.intToBits64 65536, FlowVerify.intToBits64 (-65536), 0, FlowVerify.intToBits64 131072),
    (FlowVerify.intToBits64 131072, FlowVerify.intToBits64 (-65536), FlowVerify.intToBits64 (-32768), FlowVerify.intToBits64 262144),
    (FlowVerify.intToBits64 65536, FlowVerify.intToBits64 16384, FlowVerify.intToBits64 (-131072), FlowVerify.intToBits64 262144),
    (FlowVerify.intToBits64 65536, FlowVerify.intToBits64 65536, FlowVerify.intToBits64 65536, FlowVerify.intToBits64 131072),
    (FlowVerify.intToBits64 98304, FlowVerify.intToBits64 (-131072), FlowVerify.intToBits64 16384, FlowVerify.intToBits64 262144),
    -- Tiny unnormalized (`or_m < 4096`)
    (FlowVerify.intToBits64 120, FlowVerify.intToBits64 (-30), FlowVerify.intToBits64 (-10), FlowVerify.intToBits64 524288),
    (FlowVerify.intToBits64 100, FlowVerify.intToBits64 20, FlowVerify.intToBits64 (-80), FlowVerify.intToBits64 524288),
    -- Huge unnormalized (`or_m >= 2^30`)
    (FlowVerify.intToBits64 0x50000000, FlowVerify.intToBits64 (-0x28000000), FlowVerify.intToBits64 (-0x14000000), FlowVerify.intToBits64 524288),
    (FlowVerify.intToBits64 0x60000000, FlowVerify.intToBits64 0x10000000, FlowVerify.intToBits64 (-0x40000000), FlowVerify.intToBits64 524288),
    -- Horizon sieve rejection (`f_ub < target` and concave `c_i < 0` shortfall)
    (FlowVerify.intToBits64 655360, FlowVerify.intToBits64 (-1024), FlowVerify.intToBits64 (-512), FlowVerify.intToBits64 65536),
    (FlowVerify.intToBits64 655360, FlowVerify.intToBits64 (-4096), FlowVerify.intToBits64 4096, FlowVerify.intToBits64 65536),
    -- Boundary tags & INT64_MIN rejections
    (bTagBits, FlowVerify.intToBits64 (-65536), 0, FlowVerify.intToBits64 131072),
    (FlowVerify.intToBits64 65536, bTagBits, 0, FlowVerify.intToBits64 131072),
    (FlowVerify.intToBits64 65536, FlowVerify.intToBits64 (-65536), bTagBits, FlowVerify.intToBits64 131072),
    (FlowVerify.intToBits64 65536, i64MinBits, 0, FlowVerify.intToBits64 131072),
    (FlowVerify.intToBits64 65536, FlowVerify.intToBits64 (-65536), i64MinBits, FlowVerify.intToBits64 131072)
  ]
  for (vb, d1b, d2b, tb) in jetRawCases do
    let expRoot : UInt64 :=
      match FlowSpec.jet2SolveRootFast64 (Int64.ofUInt64 vb) (Int64.ofUInt64 d1b) (Int64.ofUInt64 d2b) (Int64.ofUInt64 tb) with
      | some t => t.toUInt64
      | none => 0
    FlowVerify.requireEq "jet2_root_fast64 adv Native" expRoot (FlowVerify.nativeJet2RootFast64 vb d1b d2b tb)
    FlowVerify.requireEq "jet2_root_fast64 adv MCU32" expRoot (FlowVerify.mcu32Jet2RootFast64 vb d1b d2b tb)
    FlowVerify.requireEq "jet2_root_128 Native vs MCU32"
      (FlowVerify.nativeJet2Root128 vb d1b d2b tb) (FlowVerify.mcu32Jet2Root128 vb d1b d2b tb)
    checks := checks + 3

  -- 1024-sample PRNG sweep across all scale regimes for `jet2SolveRootFast64` + 128-bit Jet2 ops
  for i in [0:1024] do
    rng := FlowVerify.prngNext rng
    let sh : UInt64 := UInt64.ofNat ((i % 4) * 14 + 4)
    let vb : UInt64 := if i % 16 == 0 then bTagBits else ((rng >>> sh) &&& 0x7FFFFFFFFFFFFFFF)
    rng := FlowVerify.prngNext rng
    let d1b : UInt64 := Int64.ofBitVec (BitVec.sshiftRight rng.toBitVec sh.toNat) |>.toUInt64
    rng := FlowVerify.prngNext rng
    let d2b : UInt64 := Int64.ofBitVec (BitVec.sshiftRight rng.toBitVec sh.toNat) |>.toUInt64
    rng := FlowVerify.prngNext rng
    let tb : UInt64 := (rng >>> 40) + 1024
    let expRoot : UInt64 :=
      match FlowSpec.jet2SolveRootFast64 (Int64.ofUInt64 vb) (Int64.ofUInt64 d1b) (Int64.ofUInt64 d2b) (Int64.ofUInt64 tb) with
      | some t => t.toUInt64
      | none => 0
    FlowVerify.requireEq "jet2_root_fast64 PRNG Native" expRoot (FlowVerify.nativeJet2RootFast64 vb d1b d2b tb)
    FlowVerify.requireEq "jet2_root_fast64 PRNG MCU32" expRoot (FlowVerify.mcu32Jet2RootFast64 vb d1b d2b tb)
    FlowVerify.requireEq "jet2_root_128 PRNG Native==MCU32"
      (FlowVerify.nativeJet2Root128 vb d1b d2b tb) (FlowVerify.mcu32Jet2Root128 vb d1b d2b tb)
    let macNat := FlowVerify.nativeJet2NonlinearMac vb d1b d2b d1b d2b vb tb
    let macMcu := FlowVerify.mcu32Jet2NonlinearMac vb d1b d2b d1b d2b vb tb
    FlowVerify.requireEq "jet2_nonlinear_mac Native==MCU32" macNat macMcu
    checks := checks + 4

  let qsplitEdges : Array (UInt64 × UInt64 × UInt64) := #[
    (0, 0, 0),
    (1000000, 0, 10000),
    (1000000, 250, 10000),
    (1000000, 10000, 10000),
    (1000000, 99999, 10000),
    (0xFFFFFFFFFFFFFFFF, 1, 3),
    (0xFFFFFFFFFFFFFFFF, 2, 3),
    (0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF),
    (0xFFFFFFFFFFFFFFFF, 7, 0)
  ]
  for (tot, num, den) in qsplitEdges do
    let expPri := FlowSpec.qsplitPrimary tot num den
    let expRem := FlowSpec.qsplitRemainder tot num den
    FlowVerify.requireEq "qsplit pri edge Native" expPri (FlowVerify.nativeQsplitPri tot num den)
    FlowVerify.requireEq "qsplit pri edge MCU32" expPri (FlowVerify.mcu32QsplitPri tot num den)
    FlowVerify.requireEq "qsplit rem edge Native" expRem (FlowVerify.nativeQsplitRem tot num den)
    FlowVerify.requireEq "qsplit rem edge MCU32" expRem (FlowVerify.mcu32QsplitRem tot num den)
    checks := checks + 4

  for k in [0:256] do
    rng := FlowVerify.prngNext rng
    let seed := rng ^^^ UInt64.ofNat k
    let expMac := FlowVerify.leanSampleCapsuleMac seed
    FlowVerify.requireEq "capsule64 MAC Native" expMac (FlowVerify.nativeCapsule64Mac seed)
    FlowVerify.requireEq "capsule64 MAC MCU32" expMac (FlowVerify.mcu32Capsule64Mac seed)
    rng := FlowVerify.prngNext rng
    let tot := rng
    rng := FlowVerify.prngNext rng
    let num := rng
    rng := FlowVerify.prngNext rng
    let den := if k % 16 == 0 then 0 else rng
    let expPri := FlowSpec.qsplitPrimary tot num den
    let expRem := FlowSpec.qsplitRemainder tot num den
    FlowVerify.requireEq "qsplit pri PRNG Native" expPri (FlowVerify.nativeQsplitPri tot num den)
    FlowVerify.requireEq "qsplit pri PRNG MCU32" expPri (FlowVerify.mcu32QsplitPri tot num den)
    FlowVerify.requireEq "qsplit rem PRNG Native" expRem (FlowVerify.nativeQsplitRem tot num den)
    FlowVerify.requireEq "qsplit rem PRNG MCU32" expRem (FlowVerify.mcu32QsplitRem tot num den)
    checks := checks + 6

  IO.println s!"  [PASS] FlowVerify 3-Way SSOT Equivalence: {checks} assertions (`FlowSpec == Native ASM == 32-Bit MCU` 0-ULP)"
  return 0
