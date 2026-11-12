/-
  FlowSpec.lean
  ================================================================================
  FLOW L0 — Authoritative Pure-Kernel Lean 4 Single-Source-of-Truth Specification
  ================================================================================
  Constrains FLOW's optimal hardware/ASM implementations (`src/flow_asm.h`,
  `src/flow_hott_jet.h`) with ZERO external tactics or
  non-standard axioms (`#print axioms` strictly `⊆ {propext, Classical.choice, Quot.sound}`):
    1. 32-Bit Limb Exact 64x64 -> 128 Multiplication (`FLOW_FORCE_32BIT_FALLBACK`)
    2. Division-Free Goldilocks Prime Field F_p (p = 2^64 - 2^32 + 1) & x^7 S-Box
    3. 192-Entry Chebyshev Minimax Single-`udiv` Universal `isqrtU64Fast_exact`
    4. 10-Bit Hardware Sieve, NaN-Boxed Boundary Pullback, Z/2^kZ Arena & `rbit64`
    5. Cubical De Morgan / HIT Quotient / Kan Neighbor, FlowJet2, Citardauq Root,
       Dual-Threshold Schmitt Hysteresis, Conservation Split & 64B Sponge Seal
-/

namespace FlowSpec

/-! ## 1. Canonical L0 Constants & 64-Byte Cache-Line Geometry -/

def qshift : UInt32 := 16
def q16One : Int := 65536
def wave64Size : UInt32 := 64
def qpolyMaxPlanes : UInt32 := 64
def cubicalMaxDim : UInt32 := 64

def hwPageBytes : UInt32 := 16384
def hwCacheLineBytes : UInt32 := 64
def hwCacheLineAppleBytes : UInt32 := 128
def hwL1dBytes : UInt32 := 65536
def hwVectorBytes : UInt32 := 16
def hwWaveLanes : UInt32 := 64
def hwFallbackX86Hz : UInt64 := 3000000000
def hwFallbackArmHz : UInt64 := 24000000

def goldenRatio64 : UInt64 := 0x9E3779B97F4A7C15
def fnvOffset64 : UInt64 := 0xCBF29CE484222325
def fnvPrime64 : UInt64 := 0x00000100000001B3
def goldilocksPrime : UInt64 := 0xFFFFFFFF00000001
def goldilocksPrimeNat : Nat := 18446744069414584321
def goldilocksEps : UInt64 := 0xFFFFFFFF
def fp64SpongeRoundConst : UInt64 := 0x9E3779B97F4A7C15

def boundaryTagMagic : UInt64 := 0x7FF8400000000000
def boundaryTagPrefixMask : UInt64 := 0x7FFCF00000000000
def boundaryPayloadMask : UInt64 := 0x00000FFFFFFFFFFF
def hottBoundaryOverflow : UInt16 := 0x0E01

def sieveIntNoOverflow : UInt64 := 0x001
def sieveIntNoCarry : UInt64 := 0x002
def sieveDomainValid : UInt64 := 0x004
def sieveIntNoDivzero : UInt64 := 0x008
def sieveQNoSaturation : UInt64 := 0x010
def sieveMemMaskedInbounds : UInt64 := 0x020
def sieveMemAligned : UInt64 := 0x040
def sieveQpolyInterior : UInt64 := 0x080
def sieveJet2Taylor0 : UInt64 := 0x100
def sieveJet2Taylor1 : UInt64 := 0x200
def sieveAllSound : UInt64 := 0x3FF

inductive FlowKanStatus where
  | filledHomotopic
  | obstructedSingularity
  deriving DecidableEq, Repr

structure FlowJet2 where
  v  : Int
  d1 : Int
  d2 : Int
  deriving DecidableEq, Repr

def sieveUnionAllAxes : UInt64 :=
  sieveIntNoOverflow ||| sieveIntNoCarry ||| sieveDomainValid ||| sieveIntNoDivzero |||
  sieveQNoSaturation ||| sieveMemMaskedInbounds ||| sieveMemAligned |||
  sieveQpolyInterior ||| sieveJet2Taylor0 ||| sieveJet2Taylor1

def oddInvU64 (a : UInt64) : UInt64 :=
  let x0 : UInt64 := 1
  let x1 := x0 * (2 - a * x0)
  let x2 := x1 * (2 - a * x1)
  let x3 := x2 * (2 - a * x2)
  let x4 := x3 * (2 - a * x3)
  let x5 := x4 * (2 - a * x4)
  x5 * (2 - a * x5)

def fnvPrimeInv64 : UInt64 := oddInvU64 fnvPrime64

/-! ## 2. Exact 32-Bit Limb 64x64 -> 128 Multiplication (`FLOW_FORCE_32BIT_FALLBACK`) -/

def mulU64Limb32 (a b : UInt64) : UInt64 × UInt64 :=
  let mask32 : UInt64 := 0xFFFFFFFF
  let a0 := a &&& mask32
  let a1 := a >>> 32
  let b0 := b &&& mask32
  let b1 := b >>> 32
  let p00 := a0 * b0
  let p01 := a0 * b1
  let p10 := a1 * b0
  let p11 := a1 * b1
  let mid := (p00 >>> 32) + (p01 &&& mask32) + (p10 &&& mask32)
  let lo := (p00 &&& mask32) + ((mid &&& mask32) <<< 32)
  let hi := p11 + (p01 >>> 32) + (p10 >>> 32) + (mid >>> 32)
  (hi, lo)

/-! ## 3. Division-Free Goldilocks Prime Field F_p (p = 2^64 - 2^32 + 1) -/

def fp64Reduce (x : UInt64) : UInt64 :=
  if goldilocksPrime ≤ x then x - goldilocksPrime else x

def fp64Add (a b : UInt64) : UInt64 :=
  let ar := fp64Reduce a
  let br := fp64Reduce b
  let diff := goldilocksPrime - br
  if diff ≤ ar then ar - diff else ar + br

def fp64Sub (a b : UInt64) : UInt64 :=
  let ar := fp64Reduce a
  let br := fp64Reduce b
  if br ≤ ar then ar - br else goldilocksPrime - (br - ar)

def fp64MulFast (a b : UInt64) : UInt64 :=
  let ar := fp64Reduce a
  let br := fp64Reduce b
  let (hi64, lo64) := mulU64Limb32 ar br
  let mid := hi64 &&& goldilocksEps
  let hi := hi64 >>> 32
  let t0 := fp64Sub (fp64Reduce lo64) hi
  let t1 := mid * goldilocksEps
  fp64Add t0 t1

def fp64Pow7 (x : UInt64) : UInt64 :=
  let x2 := fp64MulFast x x
  let x4 := fp64MulFast x2 x2
  let x6 := fp64MulFast x4 x2
  fp64MulFast x6 x

def fp64SpongeStep (state word : UInt64) : UInt64 :=
  let mixed := fp64Add state (fp64Add word fp64SpongeRoundConst)
  fp64Pow7 mixed

/-! ## 4. First-Principles 192-Entry Chebyshev Single-`udiv` `isqrt_u64_fast` -/

def isqrtChebEntry (k : Nat) : UInt32 :=
  let u := 64 + (k % 192)
  let y0 := Nat.sqrt (u <<< 56)
  let y1 := Nat.sqrt ((u + 1) <<< 56)
  let ym := Nat.sqrt ((2 * u + 1) <<< 55)
  let lo := (3 * y0 + 2 * ym - y1 + (1 <<< 17)) >>> 18
  let hi := (y1 - y0 + (1 <<< 11)) >>> 12
  UInt32.ofNat ((hi <<< 16) ||| (lo &&& 0xFFFF))

def clz64Loop (x : Nat) (fuel : Nat) : Nat :=
  match fuel with
  | 0 => 64
  | k + 1 => if x ≥ (1 <<< k) then 63 - k else clz64Loop x k

def clz64 (x : UInt64) : Nat :=
  if x == 0 then 64 else clz64Loop x.toNat 64

def heronFloorStep (x r : Nat) : Nat :=
  if r == 0 then 0
  else
    let r1 := (r + x / r) / 2
    if r1 * r1 > x then r1 - 1 else r1

def isqrtU64Fast (x : UInt64) : UInt64 :=
  if x < 2 then x
  else
    let evenLz := clz64 x &&& (Nat.xor 0xFF 1)
    let m := x.toNat <<< evenLz
    let idx := (m >>> 56) - 64
    let entry := (isqrtChebEntry idx).toNat
    let frac16 := (m >>> 40) &&& 0xFFFF
    let seed32 := (((entry &&& 0xFFFF) <<< 4) + (((entry >>> 16) * frac16) >>> 16)) <<< 12
    let r0 := (seed32 >>> (evenLz >>> 1)) ||| 1
    let r1 := (r0 + x.toNat / r0) >>> 1
    let rClamped := if r1 > 0xFFFFFFFF then 0xFFFFFFFF else r1
    let rFinal := if rClamped * rClamped > x.toNat then rClamped - 1 else rClamped
    UInt64.ofNat rFinal

def qsqrtInteriorU64 (xQ16 : UInt64) : UInt64 :=
  isqrtU64Fast (xQ16 <<< 16)

/-! ## 5. Boundary Pullback Tags, Z/2^kZ Arena, Bit-Reversal & Ternary Lattice -/

def makeBoundaryTag (fnId : UInt16) (guardIdx violationKind cubicalAxis : UInt8) (userCode : UInt16) : UInt64 :=
  let k4 : UInt64 := ((if violationKind == 0 then 1 else violationKind).toUInt64) &&& 0x0F
  boundaryTagMagic |||
    (((fnId.toUInt64) &&& 0x0FFF) <<< 32) |||
    ((guardIdx.toUInt64) <<< 24) |||
    (k4 <<< 20) |||
    (((cubicalAxis.toUInt64) &&& 0x3F) <<< 12) |||
    ((userCode.toUInt64) &&& 0x0FFF)

def isBoundaryTag (rawBits : UInt64) : Bool :=
  (rawBits &&& boundaryTagPrefixMask) == boundaryTagMagic

def boundaryTagCode (rawBits : UInt64) : UInt16 :=
  if isBoundaryTag rawBits then (rawBits &&& 0x0FFF).toUInt16 else 0

def arenaMaskedIdx (rawIdx capMask : BitVec 64) : BitVec 64 :=
  rawIdx &&& capMask

def arenaOobBits (rawIdx capMask : BitVec 64) : BitVec 64 :=
  rawIdx &&& (~~~capMask)

def rbit64BV (x : BitVec 64) : BitVec 64 :=
  let m1 : BitVec 64 := 0x5555555555555555#64
  let m2 : BitVec 64 := 0x3333333333333333#64
  let m4 : BitVec 64 := 0x0F0F0F0F0F0F0F0F#64
  let m8 : BitVec 64 := 0x00FF00FF00FF00FF#64
  let m16 : BitVec 64 := 0x0000FFFF0000FFFF#64
  let s1 := ((x >>> 1) &&& m1) ||| ((x &&& m1) <<< 1)
  let s2 := ((s1 >>> 2) &&& m2) ||| ((s1 &&& m2) <<< 2)
  let s4 := ((s2 >>> 4) &&& m4) ||| ((s2 &&& m4) <<< 4)
  let s8 := ((s4 >>> 8) &&& m8) ||| ((s4 &&& m8) <<< 8)
  let s16 := ((s8 >>> 16) &&& m16) ||| ((s8 &&& m16) <<< 16)
  (s16 >>> 32) ||| (s16 <<< 32)

def rbit64U64 (x : UInt64) : UInt64 :=
  UInt64.ofBitVec (rbit64BV x.toBitVec)

def ternaryCanonicalize (nzMask signMask : BitVec 64) : BitVec 64 :=
  signMask &&& nzMask

/-! ## 6. Cubical HoTT, Kan Neighbor, FlowJet2, Citardauq Root & Schmitt Hysteresis -/

def demorganWordNot (w : BitVec 64) : BitVec 64 := ~~~w
def demorganWordAnd (a b : BitVec 64) : BitVec 64 := a &&& b
def demorganWordOr (a b : BitVec 64) : BitVec 64 := a ||| b

def hottQuotientSieve (ambient exc : BitVec 64) : BitVec 64 :=
  ambient &&& (~~~exc)

def kanCheckHomotopy (maskP maskQ boundaryFilter : UInt64) : FlowKanStatus :=
  if ((maskP ^^^ maskQ) &&& boundaryFilter) == 0 then .filledHomotopic
  else .obstructedSingularity

def cubicalStepNeighbor (mask : UInt64) (bit : UInt32) (boundaryFilter : UInt64) : FlowKanStatus × UInt64 :=
  if bit ≥ cubicalMaxDim then (.obstructedSingularity, mask)
  else
    let cand := mask ^^^ ((1 : UInt64) <<< bit.toUInt64)
    let st := kanCheckHomotopy mask cand boundaryFilter
    match st with
    | .filledHomotopic => (.filledHomotopic, cand)
    | .obstructedSingularity => (.obstructedSingularity, mask)

def qlerpExactNum (x0 x1 alpha : Int) : Int :=
  (q16One - alpha) * x0 + alpha * x1

def qlerpQ16Floor (x0 x1 alpha : Int) : Int :=
  (qlerpExactNum x0 x1 alpha) / q16One

def qmargin3Exact (posA negB posC : Int) : Int :=
  (posA + posC) - negB

def schmittHystStep (bEnter bExit x : Int) (prevViolated : Bool) : Bool :=
  let activeBound := if prevViolated then bExit else bEnter
  decide (x > activeBound)

def schmittHystHi (bEnter bExit x : Int) (prevLatched : Bool) : Bool :=
  if x ≥ bEnter then true
  else if x ≤ bExit then false
  else prevLatched

def schmittHystLo (bEnter bExit x : Int) (prevLatched : Bool) : Bool :=
  if x ≤ bEnter then true
  else if x ≥ bExit then false
  else prevLatched

def qsplitPrimary (total num den : UInt64) : UInt64 :=
  if den == 0 then 0
  else
    let c := if num > den then den else num
    UInt64.ofNat ((total.toNat * c.toNat) / den.toNat)

def qsplitRemainder (total num den : UInt64) : UInt64 :=
  total - qsplitPrimary total num den

def capsule64Mac7 (domainSeed w0 w1 w2 w3 w4 w5 w6 : UInt64) : UInt64 :=
  let s0 := fp64SpongeStep goldenRatio64 domainSeed
  let s1 := fp64SpongeStep s0 w0
  let s2 := fp64SpongeStep s1 w1
  let s3 := fp64SpongeStep s2 w2
  let s4 := fp64SpongeStep s3 w3
  let s5 := fp64SpongeStep s4 w4
  let s6 := fp64SpongeStep s5 w5
  let s7 := fp64SpongeStep s6 w6
  if s7 == 0 then 1 else s7

def jet2AddExact (a b : FlowJet2) : FlowJet2 :=
  { v := a.v + b.v, d1 := a.d1 + b.d1, d2 := a.d2 + b.d2 }

def jet2SubExact (a b : FlowJet2) : FlowJet2 :=
  { v := a.v - b.v, d1 := a.d1 - b.d1, d2 := a.d2 - b.d2 }

def jet2EvalTaylorExact (j : FlowJet2) (dt : Int) : Int :=
  j.v * (2 * q16One * q16One) + j.d1 * dt * (2 * q16One) + j.d2 * dt * dt

private def ashrI64 (x : Int64) (s : Nat) : Int64 :=
  Int64.ofBitVec (BitVec.sshiftRight x.toBitVec s)

private def shlI64 (x : Int64) (s : Nat) : Int64 :=
  Int64.ofBitVec (x.toBitVec <<< s)

private def jet2NormalizeFast64 (v d1 d2 : Int64) : Option (Int64 × Int64 × Int64 × UInt64) :=
  let mv : UInt64 := v.toUInt64
  let mr0 : UInt64 := if d1.toInt < 0 then 0 - d1.toUInt64 else d1.toUInt64
  let mc0 : UInt64 := if d2.toInt < 0 then 0 - d2.toUInt64 else d2.toUInt64
  let orM : UInt64 := mv ||| mr0 ||| mc0
  if orM < 4096 || orM ≥ 0x40000000 then
    if d1 == Int64.minValue || d2 == Int64.minValue ||
       isBoundaryTag v.toUInt64 || isBoundaryTag d1.toUInt64 || isBoundaryTag d2.toUInt64 then
      none
    else
      let rI0 : Int64 := -d1
      let cI0 : Int64 := -d2
      if rI0.toInt ≤ 0 && cI0.toInt ≤ 0 then none
      else
        let max1 := if mv > mr0 then mv else mr0
        let maxM := if mc0 > max1 then mc0 else max1
        let lz := clz64 maxM
        let (vN, rN, cN) :=
          if lz < 42 then
            let shr := 42 - lz
            (ashrI64 v shr, ashrI64 rI0 shr, ashrI64 cI0 shr)
          else if lz > 42 then
            let shl := lz - 42
            (shlI64 v shl, shlI64 rI0 shl, shlI64 cI0 shl)
          else
            (v, rI0, cI0)
        if vN.toInt ≤ 0 || (rN.toInt ≤ 0 && cN.toInt ≤ 0) then none
        else
          let mrN : UInt64 := if rN.toInt < 0 then (-rN).toUInt64 else rN.toUInt64
          some (vN, rN, cN, mrN)
  else
    let rI0 : Int64 := -d1
    let cI0 : Int64 := -d2
    if rI0.toInt ≤ 0 && cI0.toInt ≤ 0 then none
    else some (v, rI0, cI0, mr0)

private def jet2HorizonReachable (vN rN cN tMaxQ16 : Int64) : Bool :=
  if tMaxQ16.toUInt64 < 0x40000000 then
    let target : Int := vN.toInt * q16One
    if cN.toInt < 0 then
      decide (rN.toInt > 0 ∧ rN.toInt * tMaxQ16.toInt ≥ target)
    else
      let tUb : Int := tMaxQ16.toInt + 1
      let fUb : Int := rN.toInt * tUb + (cN.toInt * (tUb * tUb)) / 131072
      decide (fUb ≥ target)
  else true

private def jet2WindowAccept (tU tMaxU : UInt64) : Option Int64 :=
  if tU > 0 && tU ≤ tMaxU then some (Int64.ofUInt64 tU) else none

private def jet2DivWindow (num den tMaxU : UInt64) : Option Int64 :=
  if den == 0 then none else jet2WindowAccept (num / den) tMaxU

/-- Exact bit-accurate specification of `flow_jet2_solve_root_fast64`:
    includes `INT64_MIN` & `isBoundaryTag` rejection, `clz64` normalization for
    `or_m < 2^12` or `or_m >= 2^30`, convex/concave horizon reachability sieve,
    and non-cancelling Citardauq root extraction via `isqrtU64Fast`. -/
def jet2SolveRootFast64 (v d1 d2 tMaxQ16 : Int64) : Option Int64 :=
  if v.toInt ≤ 0 || tMaxQ16.toInt ≤ 0 then none
  else
    match jet2NormalizeFast64 v d1 d2 with
    | none => none
    | some (vN, rN, cN, mr) =>
      if cN == 0 then
        jet2WindowAccept ((vN.toUInt64 <<< 16) / mr) tMaxQ16.toUInt64
      else if !(jet2HorizonReachable vN rN cN tMaxQ16) then
        none
      else
        let disc : Int := rN.toInt * rN.toInt + 2 * cN.toInt * vN.toInt
        if disc < 0 then none
        else
          let sq : UInt64 := isqrtU64Fast (UInt64.ofNat disc.toNat)
          let qSum : UInt64 := mr + sq
          let num : UInt64 := if rN.toInt ≥ 0 then (vN.toUInt64 <<< 17) else (qSum <<< 16)
          let den : UInt64 := if rN.toInt ≥ 0 then qSum else cN.toUInt64
          jet2DivWindow num den tMaxQ16.toUInt64

/-! ============================================================================
    7. Pure-Kernel Machine-Checked Formal Theorems (`0 sorryAx`, `0 bv_decide`)
    ============================================================================ -/

/-- Theorem 1: All 10 hardware sieve axes partition cleanly into `0x3FF` (`sieveAllSound`),
    and FNV-1a offset/prime constants are distinct and invertible mod `2^64`. -/
theorem flow_sieve_and_abi_constants_sound :
    sieveUnionAllAxes = sieveAllSound ∧
    qshift = 16 ∧ q16One = 65536 ∧ wave64Size = 64 ∧ qpolyMaxPlanes = 64 ∧
    hwPageBytes = 16384 ∧ (hwPageBytes &&& (hwPageBytes - 1)) = 0 ∧
    hwCacheLineBytes = 64 ∧ (hwCacheLineBytes &&& (hwCacheLineBytes - 1)) = 0 ∧
    hwCacheLineAppleBytes = 128 ∧ (hwCacheLineAppleBytes &&& (hwCacheLineAppleBytes - 1)) = 0 ∧
    hwL1dBytes = 65536 ∧ hwVectorBytes = 16 ∧ hwWaveLanes = 64 ∧
    hwFallbackX86Hz > 0 ∧ hwFallbackArmHz > 0 ∧
    fnvOffset64 = 0xCBF29CE484222325 ∧ fnvPrime64 * fnvPrimeInv64 = 1 := by
  decide +kernel

private theorem uint64_xor_cancel_left (h x : UInt64) : h ^^^ (h ^^^ x) = x := by
  apply UInt64.eq_of_toBitVec_eq
  apply BitVec.eq_of_getLsbD_eq
  intro i _hi
  simp only [UInt64.toBitVec_xor, BitVec.getLsbD_xor]
  cases h.toBitVec.getLsbD i <;> cases x.toBitVec.getLsbD i <;> rfl

private theorem uint64_xor_cancel_right (x w : UInt64) : (x ^^^ w) ^^^ w = x := by
  apply UInt64.eq_of_toBitVec_eq
  apply BitVec.eq_of_getLsbD_eq
  intro i _hi
  simp only [UInt64.toBitVec_xor, BitVec.getLsbD_xor]
  cases x.toBitVec.getLsbD i <;> cases w.toBitVec.getLsbD i <;> rfl

private theorem ite_zero_one_ne_zero (x : UInt64) :
    (if x == 0 then (1 : UInt64) else x) ≠ 0 := by
  cases hx : (x == 0)
  · simp only [Bool.false_eq_true, ↓reduceIte]
    intro h0; subst h0; simp at hx
  · simp only [↓reduceIte]
    decide

/-- Theorem 2: Universal 1D Schmitt Hi/Lo deadband stability, 128-bit exact conservation
    ratio split (`primary + remainder == total` with zero rounding leak), and non-zero
    Goldilocks F_p 64-byte capsule seal invariant. -/
theorem schmitt_qsplit_and_capsule_laws
    (bEnter bExit x : Int) (prev : Bool)
    (total num den seed w0 w1 w2 w3 w4 w5 w6 : UInt64) :
    (bExit < x → x < bEnter → schmittHystHi bEnter bExit x prev = prev) ∧
    (bEnter < x → x < bExit → schmittHystLo bEnter bExit x prev = prev) ∧
    ((qsplitPrimary total num den).toNat + (qsplitRemainder total num den).toNat = total.toNat) ∧
    capsule64Mac7 seed w0 w1 w2 w3 w4 w5 w6 ≠ 0 := by
  refine ⟨?_, ?_, ?_, ?_⟩
  · intro h1 h2
    unfold schmittHystHi
    have c1 : ¬(x ≥ bEnter) := by omega
    have c2 : ¬(x ≤ bExit) := by omega
    simp [c1, c2]
  · intro h1 h2
    unfold schmittHystLo
    have c1 : ¬(x ≤ bEnter) := by omega
    have c2 : ¬(x ≥ bExit) := by omega
    simp [c1, c2]
  · have h_pri_le : (qsplitPrimary total num den).toNat ≤ total.toNat := by
      unfold qsplitPrimary
      cases hd : (den == 0)
      · dsimp only [Bool.false_eq_true, ↓reduceIte]
        have hden_pos : den.toNat > 0 := by
          have hne : den ≠ 0 := fun h0 => by subst h0; simp at hd
          exact Nat.pos_of_ne_zero (fun hz => hne (UInt64.eq_of_toBitVec_eq (BitVec.eq_of_toNat_eq hz)))
        let c := if num > den then den else num
        have hc_le : c.toNat ≤ den.toNat := by
          dsimp only [c]
          split
          · exact Nat.le_refl _
          · rename_i hng
            exact Nat.le_of_not_lt (fun hlt => hng hlt)
        have hprod : total.toNat * c.toNat ≤ total.toNat * den.toNat :=
          Nat.mul_le_mul_left total.toNat hc_le
        let q := (total.toNat * c.toNat) / den.toNat
        have hdiv_le : q ≤ total.toNat := by
          have h1 : q ≤ (total.toNat * den.toNat) / den.toNat :=
            Nat.div_le_div_right (c := den.toNat) hprod
          have h2 : (total.toNat * den.toNat) / den.toNat = total.toNat :=
            Nat.mul_div_left total.toNat hden_pos
          omega
        have h_lt264 : q < 18446744073709551616 :=
          Nat.lt_of_le_of_lt hdiv_le total.toNat_lt
        have h_mod : (UInt64.ofNat q).toNat = q := by
          change q % 18446744073709551616 = q
          exact Nat.mod_eq_of_lt h_lt264
        change (UInt64.ofNat q).toNat ≤ total.toNat
        rw [h_mod]
        exact hdiv_le
      · simp
    unfold qsplitRemainder
    have h_sub : (total - qsplitPrimary total num den).toNat =
                 total.toNat - (qsplitPrimary total num den).toNat := by
      apply UInt64.toNat_sub_of_le
      rw [UInt64.le_iff_toNat_le]
      exact h_pri_le
    rw [h_sub]
    omega
  · exact ite_zero_one_ne_zero _

private theorem uint64_and_mask32 (x : UInt64) :
    (x &&& (0xFFFFFFFF : UInt64)).toNat = x.toNat % 4294967296 := by
  rw [UInt64.toNat_and]
  change x.toNat &&& (2 ^ 32 - 1) = x.toNat % (2 ^ 32)
  exact Nat.and_two_pow_sub_one_eq_mod x.toNat 32

private theorem uint64_shr32 (x : UInt64) :
    (x >>> 32).toNat = x.toNat / 4294967296 := by
  have hbv : (x >>> 32).toBitVec = x.toBitVec >>> 32 := rfl
  change (x >>> 32).toBitVec.toNat = x.toNat / 4294967296
  rw [hbv, BitVec.toNat_ushiftRight, Nat.shiftRight_eq_div_pow]
  rfl

private theorem uint64_shl32_low (x : UInt64) (hx : x.toNat < 4294967296) :
    (x <<< 32).toNat = x.toNat * 4294967296 := by
  have hbv : (x <<< 32).toBitVec = x.toBitVec <<< 32 := rfl
  have hxeq : x.toBitVec.toNat = x.toNat := rfl
  change (x <<< 32).toBitVec.toNat = x.toNat * 4294967296
  rw [hbv, BitVec.toNat_shiftLeft, Nat.shiftLeft_eq, hxeq]
  have hpow : (2 : Nat) ^ 32 = 4294967296 := rfl
  rw [hpow]
  exact Nat.mod_eq_of_lt (by omega)

/-- Theorem 3: 32-bit limb decomposition (`mulU64Limb32`, modeling `flow_mul_u64_to_u128`
    under `FLOW_FORCE_32BIT_FALLBACK`) is `0-ULP` exact across ALL `2^128` pairs `(a, b)`. -/
theorem mulU64Limb32_exact (a b : UInt64) :
    let (hi, lo) := mulU64Limb32 a b
    hi.toNat * 18446744073709551616 + lo.toNat = a.toNat * b.toNat := by
  unfold mulU64Limb32
  dsimp only
  let m32 : UInt64 := 0xFFFFFFFF
  have ha : a.toNat < 18446744073709551616 := a.toNat_lt
  have hb : b.toNat < 18446744073709551616 := b.toNat_lt
  have ha0 : (a &&& m32).toNat = a.toNat % 4294967296 := uint64_and_mask32 a
  have ha1 : (a >>> 32).toNat = a.toNat / 4294967296 := uint64_shr32 a
  have hb0 : (b &&& m32).toNat = b.toNat % 4294967296 := uint64_and_mask32 b
  have hb1 : (b >>> 32).toNat = b.toNat / 4294967296 := uint64_shr32 b
  let A0 := a.toNat % 4294967296
  let A1 := a.toNat / 4294967296
  let B0 := b.toNat % 4294967296
  let B1 := b.toNat / 4294967296
  have hA0_lt : A0 < 4294967296 := by omega
  have hA1_lt : A1 < 4294967296 := by omega
  have hB0_lt : B0 < 4294967296 := by omega
  have hB1_lt : B1 < 4294967296 := by omega
  let P00 := A0 * B0
  let P01 := A0 * B1
  let P10 := A1 * B0
  let P11 := A1 * B1
  have hP00_ub : P00 ≤ 4294967295 * 4294967295 := Nat.mul_le_mul (by omega) (by omega)
  have hP01_ub : P01 ≤ 4294967295 * 4294967295 := Nat.mul_le_mul (by omega) (by omega)
  have hP10_ub : P10 ≤ 4294967295 * 4294967295 := Nat.mul_le_mul (by omega) (by omega)
  have hP11_ub : P11 ≤ 4294967295 * 4294967295 := Nat.mul_le_mul (by omega) (by omega)
  have hp00 : ((a &&& m32) * (b &&& m32)).toNat = P00 := by
    rw [UInt64.toNat_mul, ha0, hb0]; change P00 % 18446744073709551616 = P00; exact Nat.mod_eq_of_lt (by omega)
  have hp01 : ((a &&& m32) * (b >>> 32)).toNat = P01 := by
    rw [UInt64.toNat_mul, ha0, hb1]; change P01 % 18446744073709551616 = P01; exact Nat.mod_eq_of_lt (by omega)
  have hp10 : ((a >>> 32) * (b &&& m32)).toNat = P10 := by
    rw [UInt64.toNat_mul, ha1, hb0]; change P10 % 18446744073709551616 = P10; exact Nat.mod_eq_of_lt (by omega)
  have hp11 : ((a >>> 32) * (b >>> 32)).toNat = P11 := by
    rw [UInt64.toNat_mul, ha1, hb1]; change P11 % 18446744073709551616 = P11; exact Nat.mod_eq_of_lt (by omega)
  let p00_u := (a &&& m32) * (b &&& m32)
  let p01_u := (a &&& m32) * (b >>> 32)
  let p10_u := (a >>> 32) * (b &&& m32)
  let p11_u := (a >>> 32) * (b >>> 32)
  let mid_u := (p00_u >>> 32) + (p01_u &&& m32) + (p10_u &&& m32)
  have hmid : mid_u.toNat = P00 / 4294967296 + P01 % 4294967296 + P10 % 4294967296 := by
    change ((p00_u >>> 32) + (p01_u &&& m32) + (p10_u &&& m32)).toNat = _
    rw [UInt64.toNat_add, UInt64.toNat_add, uint64_shr32, uint64_and_mask32, uint64_and_mask32,
        hp00, hp01, hp10]
    rw [Nat.mod_eq_of_lt (by omega), Nat.mod_eq_of_lt (by omega)]
  let M := P00 / 4294967296 + P01 % 4294967296 + P10 % 4294967296
  have hlo : ((p00_u &&& m32) + ((mid_u &&& m32) <<< 32)).toNat =
             P00 % 4294967296 + (M % 4294967296) * 4294967296 := by
    have hm_low_lt : (mid_u &&& m32).toNat < 4294967296 := by
      rw [uint64_and_mask32]; omega
    rw [UInt64.toNat_add, uint64_and_mask32, uint64_shl32_low _ hm_low_lt, uint64_and_mask32, hp00, hmid]
    exact Nat.mod_eq_of_lt (by omega)
  have hhi : (p11_u + (p01_u >>> 32) + (p10_u >>> 32) + (mid_u >>> 32)).toNat =
             P11 + P01 / 4294967296 + P10 / 4294967296 + M / 4294967296 := by
    rw [UInt64.toNat_add, UInt64.toNat_add, UInt64.toNat_add,
        uint64_shr32, uint64_shr32, uint64_shr32, hp11, hp01, hp10, hmid]
    rw [Nat.mod_eq_of_lt (by omega), Nat.mod_eq_of_lt (by omega), Nat.mod_eq_of_lt (by omega)]
  change (p11_u + (p01_u >>> 32) + (p10_u >>> 32) + (mid_u >>> 32)).toNat * 18446744073709551616 +
         ((p00_u &&& m32) + ((mid_u &&& m32) <<< 32)).toNat = a.toNat * b.toNat
  rw [hhi, hlo]
  have hexp : a.toNat * b.toNat = (A1 * 4294967296 + A0) * (B1 * 4294967296 + B0) := by
    have ha_eq : a.toNat = A1 * 4294967296 + A0 := by omega
    have hb_eq : b.toNat = B1 * 4294967296 + B0 := by omega
    conv => lhs; rw [ha_eq, hb_eq]
  have hring : (A1 * 4294967296 + A0) * (B1 * 4294967296 + B0) =
               P11 * 18446744073709551616 + (P01 + P10) * 4294967296 + P00 := by
    simp only [P00, P01, P10, P11, Nat.add_mul, Nat.mul_add]
    have c1 : A1 * 4294967296 * (B1 * 4294967296) = A1 * B1 * 18446744073709551616 := by
      calc A1 * 4294967296 * (B1 * 4294967296)
        _ = A1 * (4294967296 * (B1 * 4294967296)) := by rw [Nat.mul_assoc]
        _ = A1 * (B1 * 4294967296 * 4294967296) := by rw [← Nat.mul_assoc 4294967296 B1, Nat.mul_comm 4294967296 B1]
        _ = A1 * (B1 * 18446744073709551616) := by rw [Nat.mul_assoc B1]
        _ = A1 * B1 * 18446744073709551616 := by rw [← Nat.mul_assoc]
    have c2 : A0 * (B1 * 4294967296) = A0 * B1 * 4294967296 := by rw [← Nat.mul_assoc]
    have c3 : A1 * 4294967296 * B0 = A1 * B0 * 4294967296 := by
      rw [Nat.mul_assoc A1, Nat.mul_comm 4294967296 B0, ← Nat.mul_assoc]
    rw [c1, c2, c3]
    omega
  rw [hexp, hring]
  omega

/-- Theorem 4: `fp64Reduce` maps any `UInt64` into `[0, p)` while preserving its residue mod `p`. -/
theorem fp64Reduce_sound (x : UInt64) :
    (fp64Reduce x).toNat < goldilocksPrimeNat ∧
    (fp64Reduce x).toNat % goldilocksPrimeNat = x.toNat % goldilocksPrimeNat := by
  unfold fp64Reduce
  have hx : x.toNat < 18446744073709551616 := x.toNat_lt
  have hp : goldilocksPrime.toNat = goldilocksPrimeNat := rfl
  have hp_val : goldilocksPrimeNat = 18446744069414584321 := rfl
  split <;> rename_i h
  · have hle : goldilocksPrime ≤ x := h
    rw [UInt64.toNat_sub_of_le x goldilocksPrime hle]
    rw [UInt64.le_iff_toNat_le] at hle
    constructor
    · omega
    · have heq : x.toNat = (x.toNat - goldilocksPrime.toNat) + 1 * goldilocksPrimeNat := by omega
      conv => rhs; rw [heq]
      rw [Nat.add_mul_mod_self_right]
  · rw [UInt64.le_iff_toNat_le, Nat.not_le] at h
    exact ⟨by omega, rfl⟩

/-- Theorem 5: Division-free `fp64Add` is exact modular addition over Goldilocks `F_p`. -/
theorem fp64Add_exact (a b : UInt64) :
    (fp64Add a b).toNat = (a.toNat + b.toNat) % goldilocksPrimeNat := by
  have ⟨ha_lt, ha_mod⟩ := fp64Reduce_sound a
  have ⟨hb_lt, hb_mod⟩ := fp64Reduce_sound b
  unfold fp64Add
  dsimp only
  have hp : goldilocksPrime.toNat = goldilocksPrimeNat := rfl
  have hp_val : goldilocksPrimeNat = 18446744069414584321 := rfl
  have hdiff : (goldilocksPrime - fp64Reduce b).toNat = goldilocksPrimeNat - (fp64Reduce b).toNat := by
    rw [UInt64.toNat_sub_of_le]
    · rw [hp]
    · rw [UInt64.le_iff_toNat_le, hp]; omega
  have hsum_mod : ((fp64Reduce a).toNat + (fp64Reduce b).toNat) % goldilocksPrimeNat =
                  (a.toNat + b.toNat) % goldilocksPrimeNat := by
    rw [Nat.add_mod, ha_mod, hb_mod, ← Nat.add_mod]
  split <;> rename_i hge
  · have hge' : goldilocksPrimeNat - (fp64Reduce b).toNat ≤ (fp64Reduce a).toNat := by
      rw [UInt64.le_iff_toNat_le, hdiff] at hge; exact hge
    have hsub : (fp64Reduce a - (goldilocksPrime - fp64Reduce b)).toNat =
                (fp64Reduce a).toNat - (goldilocksPrimeNat - (fp64Reduce b).toNat) := by
      rw [UInt64.toNat_sub_of_le _ _ hge, hdiff]
    rw [hsub, ← hsum_mod]
    have h1 : (fp64Reduce a).toNat + (fp64Reduce b).toNat =
              ((fp64Reduce a).toNat - (goldilocksPrimeNat - (fp64Reduce b).toNat)) + 1 * goldilocksPrimeNat := by omega
    rw [h1, Nat.add_mul_mod_self_right, Nat.mod_eq_of_lt (by omega)]
  · rw [UInt64.le_iff_toNat_le, hdiff, Nat.not_le] at hge
    have hadd : (fp64Reduce a + fp64Reduce b).toNat = (fp64Reduce a).toNat + (fp64Reduce b).toNat := by
      rw [UInt64.toNat_add]
      apply Nat.mod_eq_of_lt
      omega
    rw [hadd, ← hsum_mod, Nat.mod_eq_of_lt (by omega)]

/-- Theorem 6: Division-free `fp64Sub` is exact modular subtraction over Goldilocks `F_p`. -/
theorem fp64Sub_exact (a b : UInt64) :
    (fp64Sub a b).toNat =
      ((a.toNat % goldilocksPrimeNat) + goldilocksPrimeNat - (b.toNat % goldilocksPrimeNat)) % goldilocksPrimeNat := by
  have ⟨ha_lt, ha_mod⟩ := fp64Reduce_sound a
  have ⟨hb_lt, hb_mod⟩ := fp64Reduce_sound b
  have ha_eq : (fp64Reduce a).toNat = a.toNat % goldilocksPrimeNat := by
    rw [← ha_mod, Nat.mod_eq_of_lt ha_lt]
  have hb_eq : (fp64Reduce b).toNat = b.toNat % goldilocksPrimeNat := by
    rw [← hb_mod, Nat.mod_eq_of_lt hb_lt]
  have hp : goldilocksPrime.toNat = goldilocksPrimeNat := rfl
  have hp_val : goldilocksPrimeNat = 18446744069414584321 := rfl
  unfold fp64Sub
  dsimp only
  split <;> rename_i hge
  · rw [UInt64.toNat_sub_of_le _ _ hge]
    rw [UInt64.le_iff_toNat_le] at hge
    rw [← ha_eq, ← hb_eq]
    have h1 : (fp64Reduce a).toNat + goldilocksPrimeNat - (fp64Reduce b).toNat =
              ((fp64Reduce a).toNat - (fp64Reduce b).toNat) + 1 * goldilocksPrimeNat := by omega
    rw [h1, Nat.add_mul_mod_self_right, Nat.mod_eq_of_lt (by omega)]
  · rw [UInt64.le_iff_toNat_le, Nat.not_le] at hge
    have hbr_sub : (fp64Reduce b - fp64Reduce a).toNat = (fp64Reduce b).toNat - (fp64Reduce a).toNat :=
      UInt64.toNat_sub_of_le _ _ (by rw [UInt64.le_iff_toNat_le]; omega)
    have hfin : (goldilocksPrime - (fp64Reduce b - fp64Reduce a)).toNat =
                goldilocksPrimeNat - ((fp64Reduce b).toNat - (fp64Reduce a).toNat) := by
      rw [UInt64.toNat_sub_of_le]
      · rw [hp, hbr_sub]
      · rw [UInt64.le_iff_toNat_le, hp, hbr_sub]; omega
    rw [hfin, ← ha_eq, ← hb_eq]
    have h2 : (fp64Reduce a).toNat + goldilocksPrimeNat - (fp64Reduce b).toNat =
              goldilocksPrimeNat - ((fp64Reduce b).toNat - (fp64Reduce a).toNat) := by omega
    rw [h2, Nat.mod_eq_of_lt (by omega)]

private theorem nat_goldilocks_rearrange (A B C D E lo P q : Nat)
    (hq : q ≤ P) (ht : B + q = E) :
    (A + B + (C + D)) + lo + 1 * P = ((lo + P - q) + D) + (A + E + C) := by
  omega

/-- Theorem 7: FLOW's division-free Goldilocks folding multiplier (`fp64MulFast`, modeling
    `flow_asm_fp64_mul` on both `__int128` and 32-bit fallback) is `0-ULP` identical to
    `(a * b) mod p` across ALL `2^128` pairs `(a, b)`. -/
theorem fp64MulFast_exact (a b : UInt64) :
    (fp64MulFast a b).toNat = (a.toNat * b.toNat) % goldilocksPrimeNat := by
  have ⟨ha_lt, ha_mod⟩ := fp64Reduce_sound a
  have ⟨hb_lt, hb_mod⟩ := fp64Reduce_sound b
  have hp_val : goldilocksPrimeNat = 18446744069414584321 := rfl
  unfold fp64MulFast
  dsimp only
  generalize hpair : mulU64Limb32 (fp64Reduce a) (fp64Reduce b) = hl
  rcases hl with ⟨hi64, lo64⟩
  have hhl := mulU64Limb32_exact (fp64Reduce a) (fp64Reduce b)
  rw [hpair] at hhl
  dsimp only at hhl
  have h_ar_lt : (fp64Reduce a).toNat < goldilocksPrimeNat := ha_lt
  have h_br_le : (fp64Reduce b).toNat ≤ 18446744073709551616 := Nat.le_of_lt (fp64Reduce b).toNat_lt
  have hprod_lt : (fp64Reduce a).toNat * (fp64Reduce b).toNat < goldilocksPrimeNat * 18446744073709551616 := by
    calc (fp64Reduce a).toNat * (fp64Reduce b).toNat
      _ ≤ (fp64Reduce a).toNat * 18446744073709551616 := Nat.mul_le_mul_left _ h_br_le
      _ < goldilocksPrimeNat * 18446744073709551616 :=
        Nat.mul_lt_mul_of_pos_right h_ar_lt (by decide +kernel)
  have hhi_bound : hi64.toNat < goldilocksPrimeNat := by
    apply Nat.lt_of_mul_lt_mul_right (a := 18446744073709551616)
    calc hi64.toNat * 18446744073709551616
      _ ≤ hi64.toNat * 18446744073709551616 + lo64.toNat := Nat.le_add_right _ _
      _ = (fp64Reduce a).toNat * (fp64Reduce b).toNat := hhl
      _ < goldilocksPrimeNat * 18446744073709551616 := hprod_lt
  have hmid : (hi64 &&& goldilocksEps).toNat = hi64.toNat % 4294967296 := uint64_and_mask32 hi64
  have hhi32 : (hi64 >>> 32).toNat = hi64.toNat / 4294967296 := uint64_shr32 hi64
  have heps : goldilocksEps.toNat = 4294967295 := by decide +kernel
  have ht1 : ((hi64 &&& goldilocksEps) * goldilocksEps).toNat = (hi64.toNat % 4294967296) * 4294967295 := by
    rw [UInt64.toNat_mul, hmid, heps]
    apply Nat.mod_eq_of_lt
    have hr_lt : hi64.toNat % 4294967296 < 4294967296 := Nat.mod_lt _ (by decide +kernel)
    calc (hi64.toNat % 4294967296) * 4294967295
      _ < 4294967296 * 4294967295 := Nat.mul_lt_mul_of_pos_right hr_lt (by decide +kernel)
      _ < 18446744073709551616 := by decide +kernel
  rw [fp64Add_exact, fp64Sub_exact, ht1]
  have ⟨_, hlo_mod⟩ := fp64Reduce_sound lo64
  rw [hlo_mod, hhi32]
  have hhi_lt_p : (hi64.toNat / 4294967296) % goldilocksPrimeNat = hi64.toNat / 4294967296 :=
    Nat.mod_eq_of_lt (Nat.lt_of_le_of_lt (Nat.div_le_self _ _) hhi_bound)
  rw [hhi_lt_p]
  have hmul_mod : ((fp64Reduce a).toNat * (fp64Reduce b).toNat) % goldilocksPrimeNat =
                  (a.toNat * b.toNat) % goldilocksPrimeNat := by
    rw [Nat.mul_mod, ha_mod, hb_mod, ← Nat.mul_mod]
  rw [← hmul_mod, ← hhl]
  let p := goldilocksPrimeNat
  let q32 := hi64.toNat / 4294967296
  let r32 := hi64.toNat % 4294967296
  have h_decomp : hi64.toNat = q32 * 4294967296 + r32 := by
    dsimp [q32, r32]
    rw [Nat.mul_comm]
    exact (Nat.div_add_mod hi64.toNat 4294967296).symm
  have hq32_lt : q32 < 4294967296 := by
    dsimp [q32]
    exact Nat.div_lt_of_lt_mul (by
      calc hi64.toNat < 18446744073709551616 := hi64.toNat_lt
        _ = 4294967296 * 4294967296 := by decide +kernel)
  have hqp_pos : q32 ≤ p :=
    Nat.le_of_lt (Nat.lt_trans hq32_lt (by dsimp [p]; rw [hp_val]; decide +kernel))
  change (((lo64.toNat % p + p - q32) % p) + r32 * 4294967295) % p =
         (hi64.toNat * 18446744073709551616 + lo64.toNat) % p
  rw [Nat.add_mod ((lo64.toNat % p + p - q32) % p), Nat.mod_mod, ← Nat.add_mod]
  have h_lhs_eq : ((lo64.toNat % p + p - q32) + r32 * 4294967295) % p =
                  ((lo64.toNat + p - q32) + r32 * 4294967295) % p := by
    have h_inner : (lo64.toNat % p + (p - q32 + r32 * 4294967295)) % p =
                   (lo64.toNat + (p - q32 + r32 * 4294967295)) % p := by
      rw [Nat.add_mod (lo64.toNat % p), Nat.mod_mod, ← Nat.add_mod]
    have ha1 : (lo64.toNat % p + p - q32) + r32 * 4294967295 =
               lo64.toNat % p + (p - q32 + r32 * 4294967295) := by
      rw [Nat.add_sub_assoc hqp_pos, Nat.add_assoc]
    have ha2 : (lo64.toNat + p - q32) + r32 * 4294967295 =
               lo64.toNat + (p - q32 + r32 * 4294967295) := by
      rw [Nat.add_sub_assoc hqp_pos, Nat.add_assoc]
    rw [ha1, ha2, h_inner]
  rw [h_lhs_eq, h_decomp]
  have h_cong : (q32 * 4294967296 + r32) * 18446744073709551616 + lo64.toNat + 1 * p =
                ((lo64.toNat + p - q32) + r32 * 4294967295) + (q32 * 4294967297 + r32) * p := by
    have hc64 : (18446744073709551616 : Nat) = p + 4294967295 := by dsimp [p]; rw [hp_val]
    have hc96 : (4294967296 : Nat) * 4294967295 + 1 = p := by dsimp [p]; rw [hp_val]
    have h497 : (4294967297 : Nat) = 4294967296 + 1 := by decide +kernel
    rw [Nat.add_mul (q32 * 4294967296) r32 18446744073709551616,
        hc64, Nat.mul_add (q32 * 4294967296) p 4294967295, Nat.mul_add r32 p 4294967295,
        Nat.add_mul (q32 * 4294967297) r32 p, h497, Nat.mul_add q32 4294967296 1,
        Nat.add_mul (q32 * 4294967296) (q32 * 1) p, Nat.mul_one]
    have hq_term : q32 * 4294967296 * 4294967295 + q32 = q32 * p := by
      calc q32 * 4294967296 * 4294967295 + q32
        _ = q32 * (4294967296 * 4294967295) + q32 * 1 := by rw [Nat.mul_assoc, Nat.mul_one]
        _ = q32 * (4294967296 * 4294967295 + 1) := by rw [← Nat.mul_add]
        _ = q32 * p := by rw [hc96]
    exact nat_goldilocks_rearrange
      (q32 * 4294967296 * p) (q32 * 4294967296 * 4294967295)
      (r32 * p) (r32 * 4294967295) (q32 * p) lo64.toNat p q32 hqp_pos hq_term
  have h_mod1 : ((q32 * 4294967296 + r32) * 18446744073709551616 + lo64.toNat + 1 * p) % p =
                ((q32 * 4294967296 + r32) * 18446744073709551616 + lo64.toNat) % p :=
    Nat.add_mul_mod_self_right _ 1 p
  rw [← h_mod1, h_cong, Nat.add_mul_mod_self_right]

/-- Theorem 8: `gcd(7, p - 1) = 1` (certifying `x |-> x^7 (mod p)` is a bijection over `F_p`),
    `fp64Pow7 x = x^7 mod p`, and `fp64SpongeStep` matches canonical algebraic sponge absorption. -/
theorem fp64Pow7_and_sponge_sound (x s w : UInt64) :
    Nat.gcd 7 (goldilocksPrimeNat - 1) = 1 ∧
    (fp64Pow7 x).toNat = (x.toNat ^ 7) % goldilocksPrimeNat ∧
    fp64SpongeStep s w = fp64Pow7 (fp64Add s (fp64Add w fp64SpongeRoundConst)) := by
  have hgcd : Nat.gcd 7 (goldilocksPrimeNat - 1) = 1 := by
    have hrem : (goldilocksPrimeNat - 1) % 7 = 5 := by decide +kernel
    rw [Nat.gcd_rec 7 (goldilocksPrimeNat - 1), hrem]
    decide +kernel
  refine ⟨hgcd, ?_, rfl⟩
  unfold fp64Pow7
  simp only [fp64MulFast_exact]
  rw [Nat.mul_mod, Nat.mul_mod ((x.toNat * x.toNat) % _ * ((x.toNat * x.toNat) % _) % _)]
  simp only [Nat.mul_mod_mod, Nat.mod_mul_mod]
  have hpow : x.toNat * x.toNat * (x.toNat * x.toNat) * (x.toNat * x.toNat) * x.toNat = x.toNat ^ 7 := by
    simp [Nat.pow_succ, Nat.mul_assoc]
  rw [hpow]

/-! ### Pure-Kernel Chebyshev + Heron Universal `isqrtU64Fast_exact` Lemmas -/

private theorem le_sqrt_of_sq_le {a b : Nat} (h : a * a ≤ b) : a ≤ Nat.sqrt b := by
  by_cases hle : a ≤ Nat.sqrt b
  · exact hle
  · have hgt : Nat.sqrt b < a := by omega
    have h1 : (Nat.sqrt b + 1) * (Nat.sqrt b + 1) ≤ a * a := Nat.mul_le_mul hgt hgt
    have h2 : b < (Nat.sqrt b + 1) * (Nat.sqrt b + 1) := Nat.lt_succ_sqrt b
    omega

private theorem pow_two_mul_eq (s : Nat) : 2 ^ (2 * s) = 2 ^ s * 2 ^ s := by
  rw [Nat.two_mul, Nat.pow_add]

private theorem sqrt_mul_four_pow (n s : Nat) :
    (Nat.sqrt (n <<< (2 * s))) >>> s = Nat.sqrt n := by
  rw [Nat.shiftLeft_eq, Nat.shiftRight_eq_div_pow, pow_two_mul_eq]
  have h2s_pos : 0 < 2 ^ s := Nat.two_pow_pos s
  generalize hS : Nat.sqrt (n * (2 ^ s * 2 ^ s)) = S
  have hS_sq : S * S ≤ n * (2 ^ s * 2 ^ s) := by rw [← hS]; exact Nat.sqrt_le _
  have hS_lt : n * (2 ^ s * 2 ^ s) < (S + 1) * (S + 1) := by rw [← hS]; exact Nat.lt_succ_sqrt _
  generalize hq : S / 2 ^ s = q
  generalize hr : S % 2 ^ s = rem
  have hdm : 2 ^ s * q + rem = S := by rw [← hq, ← hr]; exact Nat.div_add_mod S (2 ^ s)
  have hrem : rem < 2 ^ s := by rw [← hr]; exact Nat.mod_lt S h2s_pos
  have hq_le : q ≤ Nat.sqrt n := by
    have h1 : q * 2 ^ s ≤ S := by rw [Nat.mul_comm]; omega
    have h2 : (q * 2 ^ s) * (q * 2 ^ s) ≤ n * (2 ^ s * 2 ^ s) :=
      Nat.le_trans (Nat.mul_le_mul h1 h1) hS_sq
    have h3 : (q * q) * (2 ^ s * 2 ^ s) ≤ n * (2 ^ s * 2 ^ s) := by
      have heq : (q * 2 ^ s) * (q * 2 ^ s) = (q * q) * (2 ^ s * 2 ^ s) := by ac_rfl
      rw [← heq]; exact h2
    exact le_sqrt_of_sq_le (Nat.le_of_mul_le_mul_right h3 (Nat.mul_pos h2s_pos h2s_pos))
  have hr_le : Nat.sqrt n ≤ q := by
    by_cases hle : Nat.sqrt n ≤ q
    · exact hle
    · have hgt : q + 1 ≤ Nat.sqrt n := by omega
      have h1 : S + 1 ≤ (q + 1) * 2 ^ s := by
        rw [Nat.add_mul, Nat.one_mul, Nat.mul_comm q]
        omega
      have h2 : (S + 1) * (S + 1) ≤ ((q + 1) * 2 ^ s) * ((q + 1) * 2 ^ s) := Nat.mul_le_mul h1 h1
      have h3 : ((q + 1) * 2 ^ s) * ((q + 1) * 2 ^ s) ≤ n * (2 ^ s * 2 ^ s) := by
        have hqq : (q + 1) * (q + 1) ≤ n := Nat.le_trans (Nat.mul_le_mul hgt hgt) (Nat.sqrt_le n)
        have heq : ((q + 1) * 2 ^ s) * ((q + 1) * 2 ^ s) = ((q + 1) * (q + 1)) * (2 ^ s * 2 ^ s) := by ac_rfl
        rw [heq]
        exact Nat.mul_le_mul_right _ hqq
      omega
  omega

private theorem clz64Loop_spec (fuel n : Nat) (hf : fuel ≤ 64) (hpos : 1 ≤ n) (hlt : n < 1 <<< fuel) :
    clz64Loop n fuel ≤ 63 ∧
    1 <<< (63 - clz64Loop n fuel) ≤ n ∧
    n < 1 <<< (64 - clz64Loop n fuel) := by
  induction fuel with
  | zero =>
    have : 1 <<< 0 = 1 := rfl
    omega
  | succ k ih =>
    unfold clz64Loop
    split <;> rename_i hge
    · have hk1 : 63 - (63 - k) = k := by omega
      have hk2 : 64 - (63 - k) = k + 1 := by omega
      rw [hk1, hk2]
      exact ⟨by omega, hge, hlt⟩
    · exact ih (by omega) (by omega)

private theorem or_one_bounds (y : Nat) : y ≤ (y ||| 1) ∧ (y ||| 1) ≤ y + 1 ∧ 1 ≤ (y ||| 1) := by
  have hdiv : (y ||| 1) / 2 = y / 2 := by
    have h1 : (y ||| 1) >>> 1 = y >>> 1 := by
      rw [Nat.shiftRight_or_distrib]
      have hz : (1 : Nat) >>> 1 = 0 := rfl
      rw [hz, Nat.or_zero]
    rw [Nat.shiftRight_eq_div_pow, Nat.shiftRight_eq_div_pow] at h1
    exact h1
  have hmod : (y ||| 1) % 2 = 1 := by
    have ht : (y ||| 1).testBit 0 = true := by
      rw [Nat.testBit_or]
      have h10 : (1 : Nat).testBit 0 = true := rfl
      rw [h10, Bool.or_true]
    rw [Nat.testBit_zero, decide_eq_true_eq] at ht
    exact ht
  have hd1 := Nat.div_add_mod (y ||| 1) 2
  have hd2 := Nat.div_add_mod y 2
  omega

private theorem amgm_two_mul (a b : Nat) : 2 * (a * b) ≤ a * a + b * b := by
  by_cases h : a ≤ b
  · generalize hd : b - a = d
    have hb : b = a + d := by omega
    rw [hb]
    simp only [Nat.mul_add, Nat.add_mul]
    rw [Nat.mul_comm d a]
    omega
  · generalize hd : a - b = d
    have ha : a = b + d := by omega
    rw [ha]
    simp only [Nat.mul_add, Nat.add_mul]
    rw [Nat.mul_comm d b]
    omega

private theorem jensen_sq_nat (u t A0 B : Nat) :
    let N := u + t
    let A1 := A0 + B * N
    (u * A0 + t * A1) * (u * A0 + t * A1) ≤ N * (u * (A0 * A0) + t * (A1 * A1)) := by
  intro N A1
  suffices h : (u * A0 + t * A1) * (u * A0 + t * A1) + u * t * (B * N) * (B * N) =
               N * (u * (A0 * A0) + t * (A1 * A1)) by omega
  dsimp only [N, A1]
  simp only [Nat.mul_add, Nat.add_mul]
  ac_rfl

private theorem affine_interp_le (u t X0 X1 Y0 Y1 : Nat)
    (h0 : X0 ≤ Y0) (h1 : X1 ≤ Y1) :
    u * X0 + t * X1 ≤ u * Y0 + t * Y1 :=
  Nat.add_le_add (Nat.mul_le_mul_left u h0) (Nat.mul_le_mul_left t h1)

private def chebBucketCert (idx : Nat) : Bool :=
  let u := 64 + (idx % 192)
  let e := (isqrtChebEntry idx).toNat
  let A := e &&& 0xFFFF
  let B := e >>> 16
  let E := 900000
  let f0 := 16 * A * 65536
  let f1 := f0 + B * 65536
  let g0 := u <<< 56
  let g1 := (u + 1) <<< 56
  let M := Nat.sqrt ((2 * u + 1) <<< 55) + 1
  let lhs_lo_0 := 2 * M * (f0 + 834464 - B)
  let rhs_lo_0 := 16 * (((2 * u + 1) <<< 55) + M * M - (1 <<< 55))
  let lhs_lo_1 := 2 * M * (f1 + 834464 - B)
  let rhs_lo_1 := 16 * (((2 * u + 1) <<< 55) + M * M + (1 <<< 55))
  let ub0 := (f0 - E) * (f0 - E) ≤ 256 * g0
  let ub1 := (f1 - E) * (f1 - E) ≤ 256 * g1
  decide (B ≤ 8192 ∧ f0 ≥ 30000000000 ∧ lhs_lo_0 ≥ rhs_lo_0 ∧ lhs_lo_1 ≥ rhs_lo_1 ∧ ub0 ∧ ub1)

private theorem chebBucketCert_all : ∀ idx : Fin 192, chebBucketCert idx.val = true := by
  decide +kernel

private theorem seed_16_bounds (A BF seed32 : Nat)
    (heq : seed32 = (A * 16 + BF / 65536) * 4096) :
    16 * A * 65536 + BF ≤ 16 * seed32 + 65536 ∧
    16 * seed32 ≤ 16 * A * 65536 + BF := by
  omega

private theorem k1_shift_eq (X B K : Nat)
    (hB : B ≤ 8192) (hK : X + 834464 - B = K) :
    X + B * 65536 + 834464 - B = K + B * 65536 := by
  have h1 : B ≤ X + 834464 := by omega
  rw [← hK, ← Nat.sub_add_comm h1]
  have h3 : X + 834464 + B * 65536 = X + B * 65536 + 834464 := by ac_rfl
  rw [h3]

private theorem cheb_algebraic_bounds (u A B M frac16 m : Nat)
    (_hu_ge : 64 ≤ u) (_hM_pos : 0 < M) (hfrac_lt : frac16 < 65536)
    (hB_le : B ≤ 8192) (hf0_ge : 16 * A * 65536 ≥ 30000000000)
    (hm_lo : u * 72057594037927936 + frac16 * 1099511627776 ≤ m)
    (hm_hi : m < u * 72057594037927936 + (frac16 + 1) * 1099511627776)
    (hlo0 : 16 * (u * 72057594037927936 + M * M) ≤
            2 * M * (16 * A * 65536 + 834464 - B))
    (hlo1 : 16 * ((u + 1) * 72057594037927936 + M * M) ≤
            2 * M * (16 * A * 65536 + B * 65536 + 834464 - B))
    (hub0 : (16 * A * 65536 - 900000) * (16 * A * 65536 - 900000) ≤
            256 * (u * 72057594037927936))
    (hub1 : (16 * A * 65536 + B * 65536 - 900000) * (16 * A * 65536 + B * 65536 - 900000) ≤
            256 * ((u + 1) * 72057594037927936)) :
    let seed32 := (((A <<< 4) + ((B * frac16) >>> 16)) <<< 12)
    1875000000 ≤ seed32 ∧
    seed32 ≤ Nat.sqrt m + 56250 ∧
    Nat.sqrt m ≤ seed32 + 56250 := by
  dsimp only
  generalize hseed_var : (((A <<< 4) + ((B * frac16) >>> 16)) <<< 12) = seed32
  have hseed_eq : seed32 = (A * 16 + (B * frac16) / 65536) * 4096 := by
    rw [← hseed_var]
    simp only [Nat.shiftLeft_eq, Nat.shiftRight_eq_div_pow]
  generalize hBF : B * frac16 = BF at hseed_eq
  have hseed_min : 1875000000 ≤ seed32 := by omega
  have ⟨hseed_lo, hseed_hi⟩ := seed_16_bounds A BF seed32 hseed_eq
  have hub : seed32 ≤ Nat.sqrt m + 56250 := by
    generalize hA0 : 16 * A * 65536 - 900000 = A0
    generalize huw : 65536 - frac16 = uw
    have hut : uw + frac16 = 65536 := by omega
    have hA1 : A0 + B * (uw + frac16) = 16 * A * 65536 + B * 65536 - 900000 := by
      rw [hut]; omega
    have hj := jensen_sq_nat uw frac16 A0 B
    dsimp only at hj
    rw [hA1, hut] at hj
    rw [hA0] at hub0
    have hlin := affine_interp_le uw frac16 (A0 * A0)
      ((16 * A * 65536 + B * 65536 - 900000) * (16 * A * 65536 + B * 65536 - 900000))
      (256 * (u * 72057594037927936)) (256 * ((u + 1) * 72057594037927936)) hub0 hub1
    have hlhs : uw * A0 + frac16 * (16 * A * 65536 + B * 65536 - 900000) =
                65536 * (A0 + B * frac16) := by
      rw [← hA1, hut, Nat.mul_add frac16 A0, ← Nat.add_assoc, ← Nat.add_mul uw frac16 A0, hut]
      have : frac16 * (B * 65536) = 65536 * (B * frac16) := by ac_rfl
      rw [this, ← Nat.mul_add]
    have hrhs : 65536 * (uw * (256 * (u * 72057594037927936)) +
                frac16 * (256 * ((u + 1) * 72057594037927936))) ≤
                (256 * m) * (65536 * 65536) := by
      have hstep : uw * (256 * (u * 72057594037927936)) +
                   frac16 * (256 * ((u + 1) * 72057594037927936)) =
                   65536 * (256 * (u * 72057594037927936 + frac16 * 1099511627776)) := by
        have h72 : (u + 1) * 72057594037927936 = u * 72057594037927936 + 65536 * 1099511627776 := by
          rw [Nat.add_mul, Nat.one_mul]
        rw [h72, Nat.mul_add 256, Nat.mul_add frac16, ← Nat.add_assoc, ← Nat.add_mul uw frac16, hut]
        have : frac16 * (256 * (65536 * 1099511627776)) = 65536 * (256 * (frac16 * 1099511627776)) := by ac_rfl
        rw [this, ← Nat.mul_add 65536, ← Nat.mul_add 256]
      rw [hstep]
      have hle := Nat.mul_le_mul_left 65536 (Nat.mul_le_mul_left 65536 (Nat.mul_le_mul_left 256 hm_lo))
      have heq : 65536 * (65536 * (256 * m)) = (256 * m) * (65536 * 65536) := by ac_rfl
      rw [← heq]; exact hle
    have hsq64 : ((A0 + B * frac16) * (A0 + B * frac16)) * (65536 * 65536) ≤ (256 * m) * (65536 * 65536) := by
      have heq : ((A0 + B * frac16) * (A0 + B * frac16)) * (65536 * 65536) =
                 (65536 * (A0 + B * frac16)) * (65536 * (A0 + B * frac16)) := by ac_rfl
      rw [heq, ← hlhs]
      exact Nat.le_trans hj (Nat.le_trans (Nat.mul_le_mul_left 65536 hlin) hrhs)
    have hsq : (A0 + B * frac16) * (A0 + B * frac16) ≤ 256 * m :=
      Nat.le_of_mul_le_mul_right hsq64 (by omega)
    have hroot : A0 + B * frac16 ≤ Nat.sqrt (256 * m) := le_sqrt_of_sq_le hsq
    have hsqrt16 : Nat.sqrt (256 * m) < 16 * (Nat.sqrt m + 1) := by
      by_cases hc : Nat.sqrt (256 * m) < 16 * (Nat.sqrt m + 1)
      · exact hc
      · have hge : 16 * (Nat.sqrt m + 1) ≤ Nat.sqrt (256 * m) := by omega
        have h1 := Nat.le_trans (Nat.mul_le_mul hge hge) (Nat.sqrt_le (256 * m))
        have h2 : 256 * ((Nat.sqrt m + 1) * (Nat.sqrt m + 1)) ≤ 256 * m := by
          have eq1 : (16 * (Nat.sqrt m + 1)) * (16 * (Nat.sqrt m + 1)) =
                     (16 * 16) * ((Nat.sqrt m + 1) * (Nat.sqrt m + 1)) := by ac_rfl
          have h16_256 : (16 * 16 : Nat) = 256 := rfl
          rw [← h16_256, ← eq1]; exact h1
        have h3 := Nat.lt_succ_sqrt m
        simp only [Nat.succ_eq_add_one] at h3
        have h4 : (Nat.sqrt m + 1) * (Nat.sqrt m + 1) ≤ m :=
          Nat.le_of_mul_le_mul_left h2 (by omega)
        omega
    rw [hBF] at hroot
    have h16_ub : 16 * seed32 < 16 * (Nat.sqrt m + 1) + 900000 := by omega
    omega
  have hlb : Nat.sqrt m ≤ seed32 + 56250 := by
    generalize ht1 : frac16 + 1 = t1
    generalize hu1 : 65536 - t1 = u1
    have hut1 : u1 + t1 = 65536 := by omega
    generalize hK : 16 * A * 65536 + 834464 - B = K
    have hK1 := k1_shift_eq (16 * A * 65536) B K hB_le hK
    rw [hK] at hlo0
    rw [hK1] at hlo1
    generalize hG0 : u * 72057594037927936 + M * M = G0
    have hG1 : (u + 1) * 72057594037927936 + M * M = G0 + 65536 * 1099511627776 := by
      rw [← hG0, Nat.add_mul u 1, Nat.one_mul]
      have hc : (72057594037927936 : Nat) = 65536 * 1099511627776 := rfl
      rw [hc]; ac_rfl
    rw [hG0] at hlo0
    rw [hG1] at hlo1
    have hlin := affine_interp_le u1 t1
      (16 * G0) (16 * (G0 + 65536 * 1099511627776))
      (2 * M * K) (2 * M * (K + B * 65536)) hlo0 hlo1
    have hlhs1 : u1 * (16 * G0) + t1 * (16 * (G0 + 65536 * 1099511627776)) =
                 (16 * (G0 + t1 * 1099511627776)) * 65536 := by
      rw [Nat.mul_add 16 G0, Nat.mul_add t1, ← Nat.add_assoc, ← Nat.add_mul u1 t1, hut1]
      have eq1 : t1 * (16 * (65536 * 1099511627776)) = 65536 * (16 * (t1 * 1099511627776)) := by ac_rfl
      rw [eq1, ← Nat.mul_add 65536, ← Nat.mul_add 16, Nat.mul_comm 65536]
    have hrhs1 : u1 * (2 * M * K) + t1 * (2 * M * (K + B * 65536)) =
                 (2 * M * (K + B * t1)) * 65536 := by
      rw [Nat.mul_add (2 * M) K, Nat.mul_add t1, ← Nat.add_assoc, ← Nat.add_mul u1 t1, hut1]
      have eq1 : t1 * (2 * M * (B * 65536)) = 65536 * (2 * M * (B * t1)) := by ac_rfl
      rw [eq1, ← Nat.mul_add 65536, ← Nat.mul_add (2 * M), Nat.mul_comm 65536]
    rw [hlhs1, hrhs1] at hlin
    have hdiv64 : 16 * (G0 + t1 * 1099511627776) ≤ 2 * M * (K + B * t1) :=
      Nat.le_of_mul_le_mul_right hlin (by omega)
    have hamgm := amgm_two_mul (Nat.sqrt m) M
    have hsm_le : Nat.sqrt m * Nat.sqrt m + M * M ≤ G0 + t1 * 1099511627776 := by
      have hsq_m := Nat.sqrt_le m
      have hm_le : m ≤ u * 72057594037927936 + t1 * 1099511627776 := by
        rw [ht1] at hm_hi; exact Nat.le_of_lt hm_hi
      rw [← hG0]
      calc Nat.sqrt m * Nat.sqrt m + M * M
        _ ≤ m + M * M := Nat.add_le_add_right hsq_m _
        _ ≤ (u * 72057594037927936 + t1 * 1099511627776) + M * M := Nat.add_le_add_right hm_le _
        _ = u * 72057594037927936 + M * M + t1 * 1099511627776 := by ac_rfl
    have hK_seed : K + B * t1 ≤ 16 * seed32 + 900000 := by
      have hbt : B * t1 = BF + B := by rw [← ht1, Nat.mul_add, Nat.mul_one, hBF]
      omega
    have h2sm : (16 * Nat.sqrt m) * (2 * M) ≤ (16 * seed32 + 900000) * (2 * M) := by
      have eq1 : (16 * Nat.sqrt m) * (2 * M) = 16 * (2 * (Nat.sqrt m * M)) := by ac_rfl
      have eq2 : (16 * seed32 + 900000) * (2 * M) = 2 * M * (16 * seed32 + 900000) := by ac_rfl
      rw [eq1, eq2]
      calc 16 * (2 * (Nat.sqrt m * M))
        _ ≤ 16 * (Nat.sqrt m * Nat.sqrt m + M * M) := Nat.mul_le_mul_left 16 hamgm
        _ ≤ 16 * (G0 + t1 * 1099511627776) := Nat.mul_le_mul_left 16 hsm_le
        _ ≤ 2 * M * (K + B * t1) := hdiv64
        _ ≤ 2 * M * (16 * seed32 + 900000) := Nat.mul_le_mul_left (2 * M) hK_seed
    have h16_sm : 16 * Nat.sqrt m ≤ 16 * seed32 + 900000 :=
      Nat.le_of_mul_le_mul_right h2sm (by omega)
    omega
  exact ⟨hseed_min, hub, hlb⟩

private theorem evenLz_props : ∀ c : Fin 56,
    let e := c.val &&& (Nat.xor 0xFF 1)
    let s := e >>> 1
    e = 2 * s ∧ s < 28 ∧ e ≤ c.val ∧ c.val ≤ e + 1 := by
  decide +kernel

private theorem heron_err_budget_28 : ∀ s : Fin 28,
    let err := (56250 >>> s.val) + 2
    let rMin := 1875000000 >>> s.val
    err * err + 2 * err < 2 * rMin := by
  decide +kernel

private theorem isqrt_small_256 : ∀ x : Fin 256,
    (isqrtU64Fast (UInt64.ofNat x.val)).toNat = Nat.sqrt x.val := by
  decide +kernel

/-- Theorem 9: Universal Heron Over-Estimate & Floor-Fixup Law — for ANY `x` and seed `r > 0`,
    integer Heron contraction `r1 = (r + x / r) / 2` never undershoots `Nat.sqrt x`, and whenever
    `r1 <= Nat.sqrt x + 1`, `heronFloorStep x r = Nat.sqrt x` (`0-ULP`). -/
theorem heron_floor_step_sound (x r : Nat) (hr : r > 0) :
    let s := Nat.sqrt x
    let r1 := (r + x / r) / 2
    s ≤ r1 ∧ (r1 ≤ s + 1 → heronFloorStep x r = s) := by
  intro s r1
  have hs_le : s * s ≤ x := Nat.sqrt_le x
  have hx_lt : x < (s + 1) * (s + 1) := Nat.lt_succ_sqrt x
  have h_amgm : 2 * s ≤ r + x / r := by
    by_cases h_eq : r = s
    · have hdiv : r ≤ x / r := (Nat.le_div_iff_mul_le hr).2 (by rw [h_eq]; exact hs_le)
      omega
    · have h_sq : r * r + s * s ≥ 2 * (r * s) + 1 := by
        rcases Nat.lt_or_gt_of_ne h_eq with hlt | hgt
        · generalize hd : s - r = d
          have hd1 : d ≥ 1 := by omega
          have hd2 : d * d ≥ 1 := Nat.mul_le_mul hd1 hd1
          have hsr : s = r + d := by omega
          have hexp : s * s = r * r + 2 * (r * d) + d * d := by
            rw [hsr, Nat.add_mul, Nat.mul_add, Nat.mul_add, Nat.mul_comm d r]; omega
          have hrd : r * s = r * r + r * d := by rw [hsr, Nat.mul_add]
          omega
        · generalize hd : r - s = d
          have hd1 : d ≥ 1 := by omega
          have hd2 : d * d ≥ 1 := Nat.mul_le_mul hd1 hd1
          have hrs : r = s + d := by omega
          have hexp : r * r = s * s + 2 * (s * d) + d * d := by
            rw [hrs, Nat.add_mul, Nat.mul_add, Nat.mul_add, Nat.mul_comm d s]; omega
          have hrs2 : r * s = s * s + s * d := by rw [hrs, Nat.add_mul, Nat.mul_comm d s]
          omega
      have h_num : (2 * s) * r ≤ r * r + x := by
        have hc : (2 * s) * r = 2 * (r * s) := by rw [Nat.mul_assoc, Nat.mul_comm s r]
        omega
      have h_div : 2 * s ≤ (r * r + x) / r := (Nat.le_div_iff_mul_le hr).2 h_num
      have h_split : (r * r + x) / r = r + x / r := by
        rw [Nat.add_comm, Nat.add_mul_div_right _ _ hr, Nat.add_comm]
      omega
  have hs_le_r1 : s ≤ r1 := by change s ≤ (r + x / r) / 2; omega
  refine ⟨hs_le_r1, ?_⟩
  intro hr1_ub
  unfold heronFloorStep
  have hr_ne : (r == 0) = false := beq_false_of_ne (by omega)
  rw [hr_ne]
  dsimp only
  change (if r1 * r1 > x then r1 - 1 else r1) = s
  rcases Nat.eq_or_lt_of_le hs_le_r1 with heq | hgt
  · rw [← heq]; split <;> omega
  · have hr1_eq : r1 = s + 1 := by omega
    rw [hr1_eq]; split <;> omega

private theorem heron_contraction_from_seed (x m seed32 s : Nat)
    (hx_lt64 : x < 18446744073709551616) (hs_lt28 : s < 28)
    (hm_eq : m = x <<< (2 * s))
    (hseed_min : 1875000000 ≤ seed32)
    (hseed_ub : seed32 ≤ Nat.sqrt m + 56250)
    (hseed_lb : Nat.sqrt m ≤ seed32 + 56250) :
    let r0 := (seed32 >>> s) ||| 1
    let r1 := (r0 + x / r0) >>> 1
    let rClamped := if r1 > 0xFFFFFFFF then 0xFFFFFFFF else r1
    let rFinal := if rClamped * rClamped > x then rClamped - 1 else rClamped
    (UInt64.ofNat rFinal).toNat = Nat.sqrt x := by
  have hsx_eq : Nat.sqrt x = (Nat.sqrt m) >>> s := by rw [hm_eq, sqrt_mul_four_pow]
  have ⟨hr0_lo, hr0_hi, hr0_pos⟩ := or_one_bounds (seed32 >>> s)
  have herr_s : ∀ a b e : Nat, a ≤ b + e → (a >>> s) ≤ (b >>> s) + (e >>> s) + 1 := by
    intro a b e hab
    simp only [Nat.shiftRight_eq_div_pow]
    have hp := Nat.two_pow_pos s
    generalize hqb : b / 2 ^ s = qb
    generalize hrb : b % 2 ^ s = rb
    generalize hqe : e / 2 ^ s = qe
    generalize hre : e % 2 ^ s = re
    have hdmb : 2 ^ s * qb + rb = b := by rw [← hqb, ← hrb]; exact Nat.div_add_mod b (2 ^ s)
    have hdme : 2 ^ s * qe + re = e := by rw [← hqe, ← hre]; exact Nat.div_add_mod e (2 ^ s)
    have hm_b : rb < 2 ^ s := by rw [← hrb]; exact Nat.mod_lt b hp
    have hm_e : re < 2 ^ s := by rw [← hre]; exact Nat.mod_lt e hp
    have hsum : a < 2 ^ s * (qb + qe + 2) := by
      rw [Nat.mul_add, Nat.mul_add]
      omega
    have hlt_div : a / 2 ^ s < qb + qe + 2 := Nat.div_lt_of_lt_mul hsum
    omega
  have h1 := herr_s seed32 (Nat.sqrt m) 56250 hseed_ub
  have h2 := herr_s (Nat.sqrt m) seed32 56250 hseed_lb
  rw [← hsx_eq] at h1 h2
  dsimp only
  generalize hr0_var : (seed32 >>> s) ||| 1 = R0 at hr0_lo hr0_hi hr0_pos ⊢
  generalize hsx_var : Nat.sqrt x = sx at h1 h2 ⊢
  generalize herr_var : (56250 >>> s) + 2 = err
  have hR0_ub : R0 ≤ sx + err := by omega
  have hsx_ub : sx ≤ R0 + err := by omega
  have hR0_min : (1875000000 >>> s) ≤ R0 := by
    have hsh : (1875000000 >>> s) ≤ (seed32 >>> s) := by
      simp only [Nat.shiftRight_eq_div_pow]
      exact Nat.div_le_div_right hseed_min
    omega
  have hbud := heron_err_budget_28 ⟨s, hs_lt28⟩
  dsimp only at hbud
  rw [herr_var] at hbud
  have hx_ub : x ≤ sx * sx + 2 * sx := by
    have hlt := Nat.lt_succ_sqrt x
    rw [hsx_var] at hlt
    simp only [Nat.succ_eq_add_one, Nat.add_mul, Nat.mul_add, Nat.one_mul, Nat.mul_one] at hlt
    omega
  have hdiff_sq : R0 * R0 + sx * sx ≤ 2 * (R0 * sx) + err * err := by
    by_cases hcmp : sx ≤ R0
    · generalize hd : R0 - sx = d
      have hd_le : d ≤ err := by omega
      have hR0_d : R0 = sx + d := by omega
      have hdd : d * d ≤ err * err := Nat.mul_le_mul hd_le hd_le
      rw [hR0_d]
      simp only [Nat.add_mul, Nat.mul_add]
      rw [Nat.mul_comm d sx]
      omega
    · generalize hd : sx - R0 = d
      have hd_le : d ≤ err := by omega
      have hsx_d : sx = R0 + d := by omega
      have hdd : d * d ≤ err * err := Nat.mul_le_mul hd_le hd_le
      rw [hsx_d]
      simp only [Nat.add_mul, Nat.mul_add]
      rw [Nat.mul_comm d R0]
      omega
  have hnum_lt : R0 * R0 + x < R0 * (2 * sx + 4) := by
    have hrhs_exp : R0 * (2 * sx + 4) = 2 * (R0 * sx) + 4 * R0 := by
      rw [Nat.mul_add]
      have e1 : R0 * (2 * sx) = 2 * (R0 * sx) := by ac_rfl
      have e2 : R0 * 4 = 4 * R0 := by ac_rfl
      rw [e1, e2]
    rw [hrhs_exp]
    omega
  have hdiv_lt : (R0 * R0 + x) / R0 < 2 * sx + 4 := Nat.div_lt_of_lt_mul hnum_lt
  have hsplit : (R0 * R0 + x) / R0 = R0 + x / R0 := by
    rw [Nat.add_comm, Nat.add_mul_div_right _ _ hr0_pos, Nat.add_comm]
  generalize hr1_var : (R0 + x / R0) >>> 1 = R1
  have hr1_div : R1 = (R0 + x / R0) / 2 := by
    rw [← hr1_var, Nat.shiftRight_eq_div_pow]
  have hr1_le_succ : R1 ≤ sx + 1 := by rw [hr1_div]; omega
  have ⟨hsx_le_r1, hheron_eq⟩ := heron_floor_step_sound x R0 hr0_pos
  rw [hsx_var] at hsx_le_r1 hheron_eq
  have hsx_max : sx ≤ 0xFFFFFFFF := by
    by_cases hc : sx ≤ 0xFFFFFFFF
    · exact hc
    · have hge : 0x100000000 ≤ sx := by omega
      have hsq : sx * sx ≤ x := by rw [← hsx_var]; exact Nat.sqrt_le x
      have hbig := Nat.le_trans (Nat.mul_le_mul hge hge) hsq
      omega
  generalize hrc_var : (if R1 > 0xFFFFFFFF then 0xFFFFFFFF else R1) = rClamped
  generalize hrf_var : (if rClamped * rClamped > x then rClamped - 1 else rClamped) = rFinal
  have hfinal : rFinal = sx := by
    rw [← hrf_var, ← hrc_var]
    by_cases hgt32 : R1 > 0xFFFFFFFF
    · have hite : (if R1 > 0xFFFFFFFF then 0xFFFFFFFF else R1) = 0xFFFFFFFF := by split <;> omega
      rw [hite]
      have hsx_eq_max : sx = 0xFFFFFFFF := by omega
      have hle_x : sx * sx ≤ x := by rw [← hsx_var]; exact Nat.sqrt_le x
      rw [← hsx_eq_max]
      split <;> omega
    · have hite : (if R1 > 0xFFFFFFFF then 0xFFFFFFFF else R1) = R1 := by split <;> omega
      rw [hite]
      have hh : heronFloorStep x R0 = sx := hheron_eq (by rw [← hr1_div]; exact hr1_le_succ)
      unfold heronFloorStep at hh
      have hne : (R0 == 0) = false := beq_false_of_ne (by omega)
      rw [hne] at hh
      change (if ((R0 + x / R0) / 2) * ((R0 + x / R0) / 2) > x then ((R0 + x / R0) / 2) - 1 else (R0 + x / R0) / 2) = sx at hh
      rw [← hr1_div] at hh
      exact hh
  rw [hfinal]
  change sx % 18446744073709551616 = sx
  exact Nat.mod_eq_of_lt (by omega)

/-- Theorem 10: Universal `0-ULP` Exactness of FLOW's 192-Entry Chebyshev Single-`udiv`
    `isqrt_u64_fast` across ALL `2^64` inputs `x : UInt64`, plus `qsqrtInteriorU64` floor bounds. -/
theorem isqrtU64Fast_exact (x : UInt64) :
    (isqrtU64Fast x).toNat = Nat.sqrt x.toNat := by
  by_cases h256 : x.toNat < 256
  · have hx_eq : UInt64.ofNat x.toNat = x := UInt64.ofNat_toNat
    have hsmall := isqrt_small_256 ⟨x.toNat, h256⟩
    dsimp only at hsmall
    rw [hx_eq] at hsmall
    exact hsmall
  · have hx_lt64 : x.toNat < 18446744073709551616 := x.toNat_lt
    have hx_not_lt2 : ¬(x < 2) := by
      rw [UInt64.lt_iff_toNat_lt]; change ¬(x.toNat < 2); omega
    have hx_ne0 : (x == 0) = false := beq_false_of_ne (by intro h0; subst h0; contradiction)
    unfold isqrtU64Fast
    have hite2 : ∀ (t e : UInt64), ¬(x < 2) → (if x < 2 then t else e) = e := by
      intro t e _hn
      split
      · contradiction
      · rfl
    rw [hite2 _ _ hx_not_lt2]
    dsimp only
    have hclz_eq : clz64 x = clz64Loop x.toNat 64 := by unfold clz64; rw [hx_ne0]; rfl
    have ⟨hclz_le63, hclz_lo, hclz_hi⟩ :=
      clz64Loop_spec 64 x.toNat (by omega) (by omega) (by change x.toNat < 18446744073709551616; exact hx_lt64)
    have hclz_lt56 : clz64 x < 56 := by
      rw [hclz_eq]
      by_cases hlt : clz64Loop x.toNat 64 < 56
      · exact hlt
      · have hle8 : 64 - clz64Loop x.toNat 64 ≤ 8 := by omega
        have hpow8 : 1 <<< (64 - clz64Loop x.toNat 64) ≤ 256 := by
          rw [Nat.shiftLeft_eq, Nat.one_mul]
          exact Nat.le_trans (Nat.pow_le_pow_right (by omega) hle8) (by omega)
        omega
    have ⟨he_2s, hs_lt28, he_le_c, hc_le_e1⟩ := evenLz_props ⟨clz64 x, hclz_lt56⟩
    dsimp only at he_2s hs_lt28 he_le_c hc_le_e1
    generalize he_var : clz64 x &&& (Nat.xor 0xFF 1) = evenLz at he_2s hs_lt28 he_le_c hc_le_e1 ⊢
    generalize hs_var : evenLz >>> 1 = s at he_2s hs_lt28 ⊢
    generalize hm_var : x.toNat <<< evenLz = m
    have hm_bounds : 4611686018427387904 ≤ m ∧ m < 18446744073709551616 := by
      rw [← hm_var, Nat.shiftLeft_eq]
      rw [Nat.shiftLeft_eq, Nat.one_mul] at hclz_lo
      rw [Nat.shiftLeft_eq, Nat.one_mul] at hclz_hi
      rw [← hclz_eq] at hclz_lo hclz_hi
      constructor
      · have h1 : 62 ≤ (63 - clz64 x) + evenLz := by omega
        calc 4611686018427387904
          _ = 2 ^ 62 := rfl
          _ ≤ 2 ^ ((63 - clz64 x) + evenLz) := Nat.pow_le_pow_right (by omega) h1
          _ = 2 ^ (63 - clz64 x) * 2 ^ evenLz := Nat.pow_add 2 _ _
          _ ≤ x.toNat * 2 ^ evenLz := Nat.mul_le_mul_right _ hclz_lo
      · have h2 : (64 - clz64 x) + evenLz ≤ 64 := by omega
        calc x.toNat * 2 ^ evenLz
          _ < 2 ^ (64 - clz64 x) * 2 ^ evenLz :=
            Nat.mul_lt_mul_of_pos_right hclz_hi (Nat.two_pow_pos _)
          _ = 2 ^ ((64 - clz64 x) + evenLz) := (Nat.pow_add 2 _ _).symm
          _ ≤ 2 ^ 64 := Nat.pow_le_pow_right (by omega) h2
          _ = 18446744073709551616 := rfl
    have hm56_eq : m >>> 56 = m / 72057594037927936 := by
      rw [Nat.shiftRight_eq_div_pow]
    have hm40_eq : (m >>> 40) &&& 0xFFFF = (m / 1099511627776) % 65536 := by
      rw [Nat.shiftRight_eq_div_pow]
      have hp40 : (2 : Nat) ^ 40 = 1099511627776 := rfl
      rw [hp40]
      change (m / 1099511627776) &&& (2 ^ 16 - 1) = (m / 1099511627776) % (2 ^ 16)
      exact Nat.and_two_pow_sub_one_eq_mod _ 16
    generalize hidx_var : (m >>> 56) - 64 = idx
    generalize hfrac_var : (m >>> 40) &&& 0xFFFF = frac16
    have hidx_lt : idx < 192 := by rw [← hidx_var, hm56_eq]; omega
    have hfrac_lt : frac16 < 65536 := by rw [← hfrac_var, hm40_eq]; omega
    have hu_div : 64 + idx = m / 72057594037927936 := by rw [← hidx_var, hm56_eq]; omega
    have hm_lo_b : (64 + idx) * 72057594037927936 + frac16 * 1099511627776 ≤ m := by
      rw [hu_div, ← hfrac_var, hm40_eq]; omega
    have hm_hi_b : m < (64 + idx) * 72057594037927936 + (frac16 + 1) * 1099511627776 := by
      rw [hu_div, ← hfrac_var, hm40_eq]; omega
    have hcert := chebBucketCert_all ⟨idx, hidx_lt⟩
    unfold chebBucketCert at hcert
    rw [decide_eq_true_eq] at hcert
    have hidx_mod : idx % 192 = idx := Nat.mod_eq_of_lt hidx_lt
    rw [hidx_mod] at hcert
    generalize hentry : (isqrtChebEntry idx).toNat = entry at hcert ⊢
    generalize hA : entry &&& 0xFFFF = A at hcert ⊢
    generalize hB : entry >>> 16 = B at hcert ⊢
    generalize hM : Nat.sqrt ((2 * (64 + idx) + 1) <<< 55) + 1 = M at hcert
    rcases hcert with ⟨hB_le, hf0_ge, hlo0, hlo1, hub0, hub1⟩
    have hg0 : (64 + idx) <<< 56 = (64 + idx) * 72057594037927936 := by rw [Nat.shiftLeft_eq]
    have hg1 : (64 + idx + 1) <<< 56 = (64 + idx + 1) * 72057594037927936 := by rw [Nat.shiftLeft_eq]
    have hgm0 : ((2 * (64 + idx) + 1) <<< 55) + M * M - (1 <<< 55) =
                (64 + idx) * 72057594037927936 + M * M := by
      have hm1 : ((2 * (64 + idx) + 1) <<< 55) = (64 + idx) * 72057594037927936 + 36028797018963968 := by
        rw [Nat.shiftLeft_eq]; omega
      have hs1 : (1 <<< 55) = 36028797018963968 := rfl
      omega
    have hgm1 : ((2 * (64 + idx) + 1) <<< 55) + M * M + (1 <<< 55) =
                (64 + idx + 1) * 72057594037927936 + M * M := by
      have hm1 : ((2 * (64 + idx) + 1) <<< 55) = (64 + idx) * 72057594037927936 + 36028797018963968 := by
        rw [Nat.shiftLeft_eq]; omega
      have hs1 : (1 <<< 55) = 36028797018963968 := rfl
      omega
    rw [hg0] at hub0
    rw [hg1] at hub1
    rw [hgm0] at hlo0
    rw [hgm1] at hlo1
    have hM_pos : 0 < M := by rw [← hM]; omega
    have ⟨hseed_min, hseed_ub, hseed_lb⟩ :=
      cheb_algebraic_bounds (64 + idx) A B M frac16 m (by omega) hM_pos hfrac_lt
        hB_le hf0_ge hm_lo_b hm_hi_b hlo0 hlo1 hub0 hub1
    exact heron_contraction_from_seed x.toNat m
      ((((A <<< 4) + ((B * frac16) >>> 16)) <<< 12)) s
      hx_lt64 hs_lt28 (by rw [← hm_var, he_2s]) hseed_min hseed_ub hseed_lb

theorem qsqrtInteriorU64_exact (xQ16 : UInt64) (hx : xQ16.toNat < 281474976710656) :
    (qsqrtInteriorU64 xQ16).toNat * (qsqrtInteriorU64 xQ16).toNat ≤ xQ16.toNat * 65536 ∧
    xQ16.toNat * 65536 < ((qsqrtInteriorU64 xQ16).toNat + 1) * ((qsqrtInteriorU64 xQ16).toNat + 1) := by
  unfold qsqrtInteriorU64
  rw [isqrtU64Fast_exact]
  have hsh : (xQ16 <<< 16).toNat = xQ16.toNat * 65536 := by
    have hbv : (xQ16 <<< 16).toBitVec = xQ16.toBitVec <<< 16 := rfl
    have hxeq : xQ16.toBitVec.toNat = xQ16.toNat := rfl
    change (xQ16 <<< 16).toBitVec.toNat = xQ16.toNat * 65536
    rw [hbv, BitVec.toNat_shiftLeft, Nat.shiftLeft_eq, hxeq]
    have hp : (2 : Nat) ^ 16 = 65536 := rfl
    rw [hp]
    exact Nat.mod_eq_of_lt (by omega)
  rw [hsh]
  exact ⟨Nat.sqrt_le _, Nat.lt_succ_sqrt _⟩

/-! ### Pure-Kernel Boundary Pullback, `rbit64`, Arena & Cubical/Kan Proofs -/

private theorem payload_tag_bits (f : UInt16) (g k4 ax : UInt8) (code : UInt16) :
    let p : BitVec 64 :=
      ((BitVec.zeroExtend 64 f.toBitVec &&& 0x0FFF#64) <<< 32) |||
      (BitVec.zeroExtend 64 g.toBitVec <<< 24) |||
      ((BitVec.zeroExtend 64 k4.toBitVec &&& 0x0F#64) <<< 20) |||
      ((BitVec.zeroExtend 64 ax.toBitVec &&& 0x3F#64) <<< 12) |||
      (BitVec.zeroExtend 64 code.toBitVec &&& 0x0FFF#64)
    (0x7FF8400000000000#64 ||| p) &&& 0x7FFCF00000000000#64 = 0x7FF8400000000000#64 ∧
    (0x7FF8400000000000#64 ||| p) &&& 0x0FFF#64 = (BitVec.zeroExtend 64 code.toBitVec &&& 0x0FFF#64) := by
  intro p
  constructor
  · apply BitVec.eq_of_getLsbD_eq
    intro i hi
    by_cases h44 : i < 44
    · have hm : (0x7FFCF00000000000#64).getLsbD i = false := by
        have hf : ∀ j : Fin 64, j.val < 44 → (0x7FFCF00000000000#64).getLsbD j.val = false := by decide +kernel
        exact hf ⟨i, hi⟩ h44
      have hmag : (0x7FF8400000000000#64).getLsbD i = false := by
        have hf : ∀ j : Fin 64, j.val < 44 → (0x7FF8400000000000#64).getLsbD j.val = false := by decide +kernel
        exact hf ⟨i, hi⟩ h44
      simp [hm, hmag]
    · have hp : p.getLsbD i = false := by
        dsimp only [p]
        simp only [BitVec.getLsbD_or, BitVec.getLsbD_and, BitVec.getLsbD_shiftLeft,
          BitVec.getLsbD_setWidth, hi, decide_true, Bool.true_and]
        have hc1 : (0x0FFF#64).getLsbD (i - 32) = false := by
          have hf : ∀ j : Fin 64, ¬(j.val < 44) → (0x0FFF#64).getLsbD (j.val - 32) = false := by decide +kernel
          exact hf ⟨i, hi⟩ h44
        have hg1 : g.toBitVec.getLsbD (i - 24) = false :=
          BitVec.getLsbD_of_ge _ _ (by omega)
        have hk1 : (0x0F#64).getLsbD (i - 20) = false := by
          have hf : ∀ j : Fin 64, ¬(j.val < 44) → (0x0F#64).getLsbD (j.val - 20) = false := by decide +kernel
          exact hf ⟨i, hi⟩ h44
        have ha1 : (0x3F#64).getLsbD (i - 12) = false := by
          have hf : ∀ j : Fin 64, ¬(j.val < 44) → (0x3F#64).getLsbD (j.val - 12) = false := by decide +kernel
          exact hf ⟨i, hi⟩ h44
        have hu1 : (0x0FFF#64).getLsbD i = false := by
          have hf : ∀ j : Fin 64, ¬(j.val < 44) → (0x0FFF#64).getLsbD j.val = false := by decide +kernel
          exact hf ⟨i, hi⟩ h44
        simp [hc1, hg1, hk1, ha1, hu1]
      have hsub : ((0x7FF8400000000000#64).getLsbD i && (0x7FFCF00000000000#64).getLsbD i) =
                  (0x7FF8400000000000#64).getLsbD i := by
        have hf : ∀ j : Fin 64,
            ((0x7FF8400000000000#64).getLsbD j.val && (0x7FFCF00000000000#64).getLsbD j.val) =
            (0x7FF8400000000000#64).getLsbD j.val := by decide +kernel
        exact hf ⟨i, hi⟩
      simp [hp, hsub]
  · apply BitVec.eq_of_getLsbD_eq
    intro i hi
    by_cases h12 : i < 12
    · have hmag : (0x7FF8400000000000#64).getLsbD i = false := by
        have hf : ∀ j : Fin 64, j.val < 12 → (0x7FF8400000000000#64).getLsbD j.val = false := by decide +kernel
        exact hf ⟨i, hi⟩ h12
      dsimp only [p]
      have h32 : i < 32 := by omega
      have h24 : i < 24 := by omega
      have h20 : i < 20 := by omega
      simp only [BitVec.getLsbD_or, BitVec.getLsbD_and, BitVec.getLsbD_shiftLeft,
        BitVec.getLsbD_setWidth, hmag, h32, h24, h20, h12, hi, decide_true,
        Bool.true_and, Bool.not_true, Bool.false_and, Bool.false_or, Bool.and_assoc, Bool.and_self]
    · have hm : (0x0FFF#64).getLsbD i = false := by
        have hf : ∀ j : Fin 64, ¬(j.val < 12) → (0x0FFF#64).getLsbD j.val = false := by decide +kernel
        exact hf ⟨i, hi⟩ h12
      simp [hm]

/-- Theorem 11: Boundary tag synthesis satisfies `isBoundaryTag`, preserves the 12-bit user code,
    and rejects zero / interior payload tags (`0 bv_decide`). -/
theorem boundary_tag_roundtrip (fnId : UInt16) (g v ax : UInt8) (code : UInt16) :
    isBoundaryTag (makeBoundaryTag fnId g v ax code) = true ∧
    boundaryTagCode (makeBoundaryTag fnId g v ax code) = (code &&& 0x0FFF) ∧
    isBoundaryTag 0 = false ∧
    hottBoundaryOverflow = 0x0E01 ∧
    boundaryPayloadMask = 0x00000FFFFFFFFFFF := by
  let k8 : UInt8 := if v == 0 then 1 else v
  have ⟨h1, h2⟩ := payload_tag_bits fnId g k8 ax code
  have htag_bv : (makeBoundaryTag fnId g v ax code).toBitVec =
      0x7FF8400000000000#64 |||
        (((BitVec.zeroExtend 64 fnId.toBitVec &&& 0x0FFF#64) <<< 32) |||
        (BitVec.zeroExtend 64 g.toBitVec <<< 24) |||
        ((BitVec.zeroExtend 64 k8.toBitVec &&& 0x0F#64) <<< 20) |||
        ((BitVec.zeroExtend 64 ax.toBitVec &&& 0x3F#64) <<< 12) |||
        (BitVec.zeroExtend 64 code.toBitVec &&& 0x0FFF#64)) := by
    unfold makeBoundaryTag boundaryTagMagic
    dsimp only [k8]
    apply BitVec.eq_of_getLsbD_eq
    intro i hi
    simp [UInt64.toBitVec_or, UInt64.toBitVec_and, Bool.or_assoc, hi]
  have his : isBoundaryTag (makeBoundaryTag fnId g v ax code) = true := by
    unfold isBoundaryTag
    rw [beq_iff_eq]
    apply UInt64.eq_of_toBitVec_eq
    change (makeBoundaryTag fnId g v ax code).toBitVec &&& 0x7FFCF00000000000#64 = 0x7FF8400000000000#64
    rw [htag_bv]
    exact h1
  refine ⟨his, ?_, by decide +kernel, rfl, rfl⟩
  unfold boundaryTagCode
  rw [his]
  change ((makeBoundaryTag fnId g v ax code &&& 0x0FFF).toUInt16) = (code &&& 0x0FFF)
  have hlow : ((makeBoundaryTag fnId g v ax code) &&& 0x0FFF).toBitVec =
              (BitVec.zeroExtend 64 code.toBitVec &&& 0x0FFF#64) := by
    change (makeBoundaryTag fnId g v ax code).toBitVec &&& 0x0FFF#64 = _
    rw [htag_bv]
    exact h2
  generalize makeBoundaryTag fnId g v ax code = tagU at hlow ⊢
  have hnat_eq : (tagU &&& 0x0FFF).toNat = code.toNat &&& 4095 := by
    have hn := congrArg BitVec.toNat hlow
    change (tagU &&& 0x0FFF).toNat = (BitVec.zeroExtend 64 code.toBitVec &&& 0x0FFF#64).toNat at hn
    rw [BitVec.toNat_and, BitVec.toNat_setWidth] at hn
    have hc_lt : code.toBitVec.toNat < 18446744073709551616 := by
      have h16 : code.toBitVec.toNat < 65536 := code.toBitVec.isLt
      omega
    rw [Nat.mod_eq_of_lt hc_lt] at hn
    exact hn
  have hlt64k : (code.toNat &&& 4095) < 65536 := by
    have hm : code.toNat &&& (2 ^ 12 - 1) = code.toNat % (2 ^ 12) :=
      Nat.and_two_pow_sub_one_eq_mod code.toNat 12
    change (code.toNat &&& 4095) = code.toNat % 4096 at hm
    omega
  apply UInt16.eq_of_toBitVec_eq
  apply BitVec.eq_of_toNat_eq
  have hlhs : ((tagU &&& 0x0FFF).toUInt16).toBitVec.toNat = (tagU &&& 0x0FFF).toNat % 65536 :=
    UInt64.toNat_toUInt16 (tagU &&& 0x0FFF)
  have hrhs : (code &&& 0x0FFF).toBitVec.toNat = code.toNat &&& 4095 :=
    UInt16.toNat_and code 0x0FFF
  rw [hlhs, hrhs, hnat_eq, Nat.mod_eq_of_lt hlt64k]

private theorem nat_and_le_pow2 : ∀ (k a b : Nat), b < 2 ^ k → (a &&& b) ≤ b
  | 0, a, b, hb => by
    have hb0 : b = 0 := by omega
    subst hb0
    rw [Nat.and_zero]
    omega
  | k + 1, a, b, hb => by
    have hdiv : (a &&& b) / 2 = (a / 2) &&& (b / 2) := by
      have hs : (a &&& b) >>> 1 = (a >>> 1) &&& (b >>> 1) := Nat.shiftRight_and_distrib
      simpa [Nat.shiftRight_eq_div_pow] using hs
    have hmod : (a &&& b) % 2 ≤ b % 2 := by
      by_cases hb0 : b % 2 = 0
      · have hbt : b.testBit 0 = false := by
          rw [Nat.testBit_zero]
          exact decide_eq_false (by omega)
        have hat : (a &&& b).testBit 0 = false := by
          rw [Nat.testBit_and, hbt, Bool.and_false]
        rw [Nat.testBit_zero] at hat
        by_cases ha0 : (a &&& b) % 2 = 1
        · rw [ha0] at hat; contradiction
        · have hlt := Nat.mod_lt (a &&& b) (by omega : 0 < 2)
          omega
      · have hlt := Nat.mod_lt (a &&& b) (by omega : 0 < 2)
        have hblt := Nat.mod_lt b (by omega : 0 < 2)
        omega
    have hih := nat_and_le_pow2 k (a / 2) (b / 2) (by
      rw [Nat.pow_succ] at hb; omega)
    have hd1 := Nat.div_add_mod (a &&& b) 2
    have hd2 := Nat.div_add_mod b 2
    omega

private def swapStageCert (k : Nat) (m : BitVec 64) (j : Nat) : Bool :=
  let cShift := decide (j < 64) && !decide (j < k)
  let mLo := m.getLsbD j
  let mHi := cShift && m.getLsbD (j - k)
  (mLo && !mHi && decide (k + j = j ^^^ k)) ||
  (!mLo && mHi && decide (j - k = j ^^^ k))

private theorem swapStage_getLsbD (k : Nat) (m y : BitVec 64) (j : Fin 64)
    (hcert : swapStageCert k m j.val = true) :
    (((y >>> k) &&& m) ||| ((y &&& m) <<< k)).getLsbD j.val = y.getLsbD (j.val ^^^ k) := by
  simp only [BitVec.getLsbD_or, BitVec.getLsbD_and, BitVec.getLsbD_ushiftRight, BitVec.getLsbD_shiftLeft]
  unfold swapStageCert at hcert
  dsimp only at hcert
  rcases Bool.or_eq_true_iff.mp hcert with hleft | hright
  · rcases Bool.and_eq_true_iff.mp hleft with ⟨hl1, heq⟩
    rcases Bool.and_eq_true_iff.mp hl1 with ⟨hmLo, hmHi⟩
    rw [decide_eq_true_eq] at heq
    rw [hmLo, Bool.and_true, heq]
    cases hcs : (decide (j.val < 64) && !decide (j.val < k)) <;>
      cases hmh : m.getLsbD (j.val - k)
    · simp
    · simp
    · simp
    · rw [hcs, hmh] at hmHi; contradiction
  · rcases Bool.and_eq_true_iff.mp hright with ⟨hr1, heq⟩
    rcases Bool.and_eq_true_iff.mp hr1 with ⟨hmLo, hmHi⟩
    rw [Bool.not_eq_true'] at hmLo
    rcases Bool.and_eq_true_iff.mp hmHi with ⟨hcs, hmh⟩
    rw [decide_eq_true_eq] at heq
    rw [hmLo, Bool.and_false, Bool.false_or, hcs, Bool.true_and, hmh, Bool.and_true, heq]

private theorem rbit64_bit_routing (x : BitVec 64) (i : Fin 64) :
    (rbit64BV x).getLsbD i.val = x.getLsbD (63 - i.val) := by
  unfold rbit64BV
  dsimp only
  have h32 : ∀ (y : BitVec 64) (j : Fin 64),
      ((y >>> 32) ||| (y <<< 32)).getLsbD j.val = y.getLsbD (j.val ^^^ 32) := by
    intro y j
    simp only [BitVec.getLsbD_or, BitVec.getLsbD_ushiftRight, BitVec.getLsbD_shiftLeft]
    by_cases hj : j.val < 32
    · have hgt : y.getLsbD (32 + j.val) = y.getLsbD (j.val ^^^ 32) := by
        have heq : ∀ q : Fin 64, q.val < 32 → 32 + q.val = q.val ^^^ 32 := by decide +kernel
        rw [heq j hj]
      simp [j.isLt, hj, hgt]
    · have hge : y.getLsbD (32 + j.val) = false := BitVec.getLsbD_of_ge _ _ (by omega)
      have heq : j.val - 32 = j.val ^^^ 32 := by
        have hf : ∀ q : Fin 64, ¬(q.val < 32) → q.val - 32 = q.val ^^^ 32 := by decide +kernel
        exact hf j hj
      simp [j.isLt, hj, hge, heq]
  have hc16 : ∀ q : Fin 64, swapStageCert 16 0x0000FFFF0000FFFF#64 q.val = true := by decide +kernel
  have hc8  : ∀ q : Fin 64, swapStageCert 8  0x00FF00FF00FF00FF#64 q.val = true := by decide +kernel
  have hc4  : ∀ q : Fin 64, swapStageCert 4  0x0F0F0F0F0F0F0F0F#64 q.val = true := by decide +kernel
  have hc2  : ∀ q : Fin 64, swapStageCert 2  0x3333333333333333#64 q.val = true := by decide +kernel
  have hc1  : ∀ q : Fin 64, swapStageCert 1  0x5555555555555555#64 q.val = true := by decide +kernel
  have hx32 : i.val ^^^ 32 < 64 := by revert i; decide +kernel
  have hx16 : (i.val ^^^ 32) ^^^ 16 < 64 := by revert i; decide +kernel
  have hx8  : ((i.val ^^^ 32) ^^^ 16) ^^^ 8 < 64 := by revert i; decide +kernel
  have hx4  : (((i.val ^^^ 32) ^^^ 16) ^^^ 8) ^^^ 4 < 64 := by revert i; decide +kernel
  have hx2  : ((((i.val ^^^ 32) ^^^ 16) ^^^ 8) ^^^ 4) ^^^ 2 < 64 := by revert i; decide +kernel
  have hfin : (((((i.val ^^^ 32) ^^^ 16) ^^^ 8) ^^^ 4) ^^^ 2) ^^^ 1 = 63 - i.val := by
    revert i; decide +kernel
  let j1 : Fin 64 := ⟨i.val ^^^ 32, hx32⟩
  let j2 : Fin 64 := ⟨j1.val ^^^ 16, hx16⟩
  let j3 : Fin 64 := ⟨j2.val ^^^ 8, hx8⟩
  let j4 : Fin 64 := ⟨j3.val ^^^ 4, hx4⟩
  let j5 : Fin 64 := ⟨j4.val ^^^ 2, hx2⟩
  rw [h32 _ i,
      swapStage_getLsbD 16 _ _ j1 (hc16 j1),
      swapStage_getLsbD 8  _ _ j2 (hc8 j2),
      swapStage_getLsbD 4  _ _ j3 (hc4 j3),
      swapStage_getLsbD 2  _ _ j4 (hc2 j4),
      swapStage_getLsbD 1  _ _ j5 (hc1 j5),
      hfin]

/-- Theorem 12: Masked cyclic arena indexing (`Z / 2^k Z`), `rbit64` bit-routing involution,
    and canonical 1.58-bit ternary sign masking hold across all 64-bit words (`0 bv_decide`). -/
theorem arena_rbit_ternary_laws (idx mask x nz sg : BitVec 64) :
    arenaMaskedIdx idx mask ≤ mask ∧
    (arenaMaskedIdx idx mask) ||| (arenaOobBits idx mask) = idx ∧
    rbit64BV (rbit64BV x) = x ∧
    rbit64U64 (rbit64U64 (UInt64.ofBitVec x)) = UInt64.ofBitVec x ∧
    (ternaryCanonicalize nz sg) &&& (~~~nz) = 0#64 := by
  have hrbit : ∀ z : BitVec 64, rbit64BV (rbit64BV z) = z := by
    intro z
    apply BitVec.eq_of_getLsbD_eq
    intro i hi
    rw [rbit64_bit_routing (rbit64BV z) ⟨i, hi⟩,
        rbit64_bit_routing z ⟨63 - i, by omega⟩]
    have hc : 63 - (63 - i) = i := by omega
    rw [hc]
  refine ⟨?_, ?_, hrbit x, ?_, ?_⟩
  · unfold arenaMaskedIdx
    rw [BitVec.le_def, BitVec.toNat_and]
    exact nat_and_le_pow2 64 idx.toNat mask.toNat mask.isLt
  · unfold arenaMaskedIdx arenaOobBits
    apply BitVec.eq_of_getLsbD_eq
    intro i hi
    simp only [BitVec.getLsbD_or, BitVec.getLsbD_and, BitVec.getLsbD_not, hi, decide_true, Bool.true_and]
    cases idx.getLsbD i <;> cases mask.getLsbD i <;> rfl
  · unfold rbit64U64
    apply UInt64.eq_of_toBitVec_eq
    exact hrbit x
  · unfold ternaryCanonicalize
    apply BitVec.eq_of_getLsbD_eq
    intro i hi
    simp only [BitVec.getLsbD_and, BitVec.getLsbD_not, BitVec.getLsbD_zero, hi, decide_true, Bool.true_and]
    cases sg.getLsbD i <;> cases nz.getLsbD i <;> rfl

/-- Theorem 13: Universal Cubical De Morgan duality, HIT exception annihilation & idempotence,
    and Kan neighbor roundtrip involution vs boundary obstruction across all `(mask, filt, bit)`. -/
theorem cubical_hott_and_kan_laws (a b exc : BitVec 64) (mask filt : UInt64) (bit : UInt32) :
    demorganWordNot (demorganWordAnd a b) = demorganWordOr (demorganWordNot a) (demorganWordNot b) ∧
    (hottQuotientSieve a exc) &&& exc = 0#64 ∧
    hottQuotientSieve (hottQuotientSieve a exc) exc = hottQuotientSieve a exc ∧
    (bit ≥ cubicalMaxDim → cubicalStepNeighbor mask bit filt = (.obstructedSingularity, mask)) ∧
    ((cubicalStepNeighbor mask bit filt).1 = .obstructedSingularity →
      (cubicalStepNeighbor mask bit filt).2 = mask) ∧
    (bit < cubicalMaxDim → (((1 : UInt64) <<< bit.toUInt64) &&& filt) == 0 →
      let (st1, m1) := cubicalStepNeighbor mask bit filt
      let (st2, m2) := cubicalStepNeighbor m1 bit filt
      st1 = .filledHomotopic ∧ m1 = mask ^^^ ((1 : UInt64) <<< bit.toUInt64) ∧
      st2 = .filledHomotopic ∧ m2 = mask) ∧
    (bit < cubicalMaxDim → (((1 : UInt64) <<< bit.toUInt64) &&& filt) ≠ 0 →
      cubicalStepNeighbor mask bit filt = (.obstructedSingularity, mask)) := by
  refine ⟨?_, ?_, ?_, ?_, ?_, ?_, ?_⟩
  · unfold demorganWordNot demorganWordAnd demorganWordOr
    apply BitVec.eq_of_getLsbD_eq
    intro i hi
    simp only [BitVec.getLsbD_not, BitVec.getLsbD_and, BitVec.getLsbD_or, hi, decide_true, Bool.true_and]
    cases a.getLsbD i <;> cases b.getLsbD i <;> rfl
  · unfold hottQuotientSieve
    apply BitVec.eq_of_getLsbD_eq
    intro i hi
    simp only [BitVec.getLsbD_and, BitVec.getLsbD_not, BitVec.getLsbD_zero, hi, decide_true, Bool.true_and]
    cases a.getLsbD i <;> cases exc.getLsbD i <;> rfl
  · unfold hottQuotientSieve
    apply BitVec.eq_of_getLsbD_eq
    intro i hi
    simp only [BitVec.getLsbD_and, BitVec.getLsbD_not, hi, decide_true, Bool.true_and]
    cases a.getLsbD i <;> cases exc.getLsbD i <;> rfl
  · intro hge
    unfold cubicalStepNeighbor
    split
    · rfl
    · contradiction
  · unfold cubicalStepNeighbor
    split
    · intro _; rfl
    · dsimp only
      split <;> intro h
      · contradiction
      · rfl
  · intro hlt hfree
    have hnotge : ¬(bit ≥ cubicalMaxDim) := by
      intro hge
      have h1 : cubicalMaxDim.toNat ≤ bit.toNat := UInt32.le_iff_toNat_le.mp hge
      have h2 : bit.toNat < cubicalMaxDim.toNat := UInt32.lt_iff_toNat_lt.mp hlt
      omega
    have hstep1 : cubicalStepNeighbor mask bit filt =
                  (.filledHomotopic, mask ^^^ ((1 : UInt64) <<< bit.toUInt64)) := by
      unfold cubicalStepNeighbor
      split
      · contradiction
      · dsimp only
        unfold kanCheckHomotopy
        rw [uint64_xor_cancel_left, hfree]
        rfl
    have hstep2 : cubicalStepNeighbor (mask ^^^ ((1 : UInt64) <<< bit.toUInt64)) bit filt =
                  (.filledHomotopic, mask) := by
      unfold cubicalStepNeighbor
      split
      · contradiction
      · dsimp only
        unfold kanCheckHomotopy
        rw [uint64_xor_cancel_left, hfree, uint64_xor_cancel_right]
        rfl
    rw [hstep1]
    dsimp only
    rw [hstep2]
    exact ⟨rfl, rfl, rfl, rfl⟩
  · intro hlt hobs
    have hnotge : ¬(bit ≥ cubicalMaxDim) := by
      intro hge
      have h1 : cubicalMaxDim.toNat ≤ bit.toNat := UInt32.le_iff_toNat_le.mp hge
      have h2 : bit.toNat < cubicalMaxDim.toNat := UInt32.lt_iff_toNat_lt.mp hlt
      omega
    unfold cubicalStepNeighbor
    split
    · contradiction
    · dsimp only
      unfold kanCheckHomotopy
      rw [uint64_xor_cancel_left]
      have hbf : ((((1 : UInt64) <<< bit.toUInt64) &&& filt) == 0) = false := beq_false_of_ne hobs
      rw [hbf]
      rfl

private theorem jet2WindowAccept_sound (tU tMaxU : UInt64) (t : Int64)
    (h : jet2WindowAccept tU tMaxU = some t) :
    0 < t.toUInt64 ∧ t.toUInt64 ≤ tMaxU := by
  unfold jet2WindowAccept at h
  split at h
  · rename_i hok
    injection h with ht
    subst ht
    rcases Bool.and_eq_true_iff.mp hok with ⟨hpos, hle⟩
    rw [decide_eq_true_eq] at hpos hle
    exact ⟨hpos, hle⟩
  · contradiction

private theorem jet2DivWindow_sound (num den tMaxU : UInt64) (t : Int64)
    (h : jet2DivWindow num den tMaxU = some t) :
    0 < t.toUInt64 ∧ t.toUInt64 ≤ tMaxU := by
  unfold jet2DivWindow at h
  split at h
  · contradiction
  · exact jet2WindowAccept_sound _ _ _ h

/-- Theorem 14: Q48.16 convex combination endpoints, 3-term barrier margin identity,
    `FlowJet2` Abelian group & Taylor linearity, Schmitt hysteresis deadband, and universal
    `jet2SolveRootFast64` interior-witness & domain-guard laws. -/
theorem qlerp_qmargin_jet2_and_schmitt_laws
    (x0 x1 a b c bExit bEnter x dt : Int) (prev : Bool)
    (j1 j2 : FlowJet2) (v d1 d2 tmax t : Int64) :
    qlerpExactNum x0 x1 0 = q16One * x0 ∧
    qlerpExactNum x0 x1 q16One = q16One * x1 ∧
    qlerpQ16Floor x0 x1 0 = x0 ∧
    qmargin3Exact a b c + b = a + c ∧
    (bExit < x → x ≤ bEnter → schmittHystStep bEnter bExit x prev = prev) ∧
    jet2AddExact (jet2SubExact j1 j2) j2 = j1 ∧
    jet2EvalTaylorExact (jet2AddExact j1 j2) dt =
      jet2EvalTaylorExact j1 dt + jet2EvalTaylorExact j2 dt ∧
    jet2EvalTaylorExact j1 0 = j1.v * (2 * q16One * q16One) ∧
    (v.toInt ≤ 0 → jet2SolveRootFast64 v d1 d2 tmax = none) ∧
    (tmax.toInt ≤ 0 → jet2SolveRootFast64 v d1 d2 tmax = none) ∧
    (jet2SolveRootFast64 v d1 d2 tmax = some t → 0 < t.toUInt64 ∧ t.toUInt64 ≤ tmax.toUInt64) ∧
    jet2SolveRootFast64 65536 (-65536) 0 131072 = some 65536 ∧
    jet2SolveRootFast64 65536 16384 (-131072) 262144 = some 74238 := by
  refine ⟨?_, ?_, ?_, ?_, ?_, ?_, ?_, ?_, ?_, ?_, ?_, by decide +kernel, by decide +kernel⟩
  · unfold qlerpExactNum q16One; omega
  · unfold qlerpExactNum q16One; simp [Int.sub_self, Int.zero_mul, Int.zero_add]
  · unfold qlerpQ16Floor qlerpExactNum q16One; omega
  · unfold qmargin3Exact; omega
  · intro h1 h2
    unfold schmittHystStep
    cases prev <;> simp <;> omega
  · rcases j1 with ⟨v1, a1, b1⟩
    rcases j2 with ⟨v2, a2, b2⟩
    unfold jet2AddExact jet2SubExact
    dsimp only
    congr 1 <;> omega
  · unfold jet2EvalTaylorExact jet2AddExact q16One
    dsimp only
    simp only [Int.add_mul]
    omega
  · unfold jet2EvalTaylorExact q16One
    simp
  · intro hv
    unfold jet2SolveRootFast64
    have hc : (decide (v.toInt ≤ 0) || decide (tmax.toInt ≤ 0)) = true := by
      rw [decide_eq_true hv, Bool.true_or]
    rw [hc]
    rfl
  · intro ht
    unfold jet2SolveRootFast64
    have hc : (decide (v.toInt ≤ 0) || decide (tmax.toInt ≤ 0)) = true := by
      rw [decide_eq_true ht, Bool.or_true]
    rw [hc]
    rfl
  · intro hsol
    unfold jet2SolveRootFast64 at hsol
    split at hsol
    · contradiction
    · split at hsol
      · contradiction
      · split at hsol
        · exact jet2WindowAccept_sound _ _ _ hsol
        · split at hsol
          · contradiction
          · dsimp only at hsol
            split at hsol
            · contradiction
            · exact jet2DivWindow_sound _ _ _ _ hsol

end FlowSpec
