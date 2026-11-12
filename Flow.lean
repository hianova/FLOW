import Std.Tactic.BVDecide

/-
  FLOW — Upstream Semantic Boundary & Formal Proof Gatekeeper
  ===========================================================
  Pure library module (zero `_root_.main` collision, zero `lean -c` codegen).
  Forces downstream domain specifications through proof-carrying `Boundary`
  instances and mints domain-separated, epoch-invariant 64-byte `.flow` cells
  and self-contained `State + Arena` bundles (`0 sorryAx`).
-/

namespace Flow

def version : String := "5.0.0"

def q16Shift : Nat := 16
def q16One : Int := 65536
def maxSafeQ16 : Int := 70368744177664 -- 2^46

def fpMod : Nat := 0xFFFFFFFF00000001
def golden64 : UInt64 := 0x9E3779B97F4A7C15
def boundSeed64 : UInt64 := 0x464C4F57424E4435 -- "FLOWBND5" (< fpMod)
def stateSeed64 : UInt64 := 0x464C4F5753544135 -- "FLOWSTA5" (< fpMod)

def sieveSealValid    : UInt32 := (1 : UInt32) <<< 0
def sieveDomainMatch  : UInt32 := (1 : UInt32) <<< 1
def sieveNoOverflow   : UInt32 := (1 : UInt32) <<< 2
def sieveKanHomotopic : UInt32 := (1 : UInt32) <<< 3
def sieveHorizonValid : UInt32 := (1 : UInt32) <<< 4
def sieveConverged    : UInt32 := (1 : UInt32) <<< 5
def sieveBaseGate     : UInt32 := sieveSealValid ||| sieveDomainMatch
def sieveSafetyGate   : UInt32 :=
  sieveSealValid ||| sieveDomainMatch ||| sieveNoOverflow ||| sieveHorizonValid

/-! ## 1. Canonical 64-Byte Cell Carrier (`8 × u64`) -/

structure Cell64 where
  w0 : UInt64
  w1 : UInt64
  w2 : UInt64
  w3 : UInt64
  w4 : UInt64
  w5 : UInt64
  w6 : UInt64
  w7 : UInt64
  deriving Repr, DecidableEq

/-! ## 2. Domain-Separated Goldilocks `F_p` (`p = 2^64 - 2^32 + 1`) Sponge Seals -/

def fpReduce64 (w : UInt64) : Nat :=
  if w.toNat ≥ fpMod then w.toNat - fpMod else w.toNat

def fpAdd (a b : Nat) : Nat :=
  (a + b) % fpMod

def fpMul (a b : Nat) : Nat :=
  (a * b) % fpMod

def fpSbox7 (x : Nat) : Nat :=
  let x2 := fpMul x x
  let x3 := fpMul x2 x
  let x6 := fpMul x3 x3
  fpMul x6 x

def absorbLane (st : Nat) (w : UInt64) (idx : Nat) : Nat :=
  let wLo := fpReduce64 w
  let wHi := if w.toNat ≥ fpMod then 1 else 0
  let s1 := fpSbox7 (fpAdd st wLo)
  fpAdd (fpMul s1 golden64.toNat) (wHi + (idx + 1))

def nonZeroU64 (u : UInt64) : UInt64 :=
  if u = 0 then 1 else u

theorem nonZeroU64_ne_zero (u : UInt64) : nonZeroU64 u ≠ 0 := by
  unfold nonZeroU64
  split
  · decide
  · assumption

def sealWithSeed (seed w0 w1 w2 w3 w4 w5 w6 : UInt64) : UInt64 :=
  let s0 := seed.toNat
  let s1 := absorbLane s0 w0 0
  let s2 := absorbLane s1 w1 1
  let s3 := absorbLane s2 w2 2
  let s4 := absorbLane s3 w3 3
  let s5 := absorbLane s4 w4 4
  let s6 := absorbLane s5 w5 5
  let s7 := absorbLane s6 w6 6
  nonZeroU64 (UInt64.ofNat (fpSbox7 s7))

/--
  Epoch-invariant Boundary Seal (`boundSeed64`):
  Masks `w6` to `[31:0] reqSieve` so runtime QSBR/SPSC `Epoch` advances in C
  NEVER require or allow re-sealing a Lean-minted boundary cell.
-/
def sealBoundLanes (w0 w1 w2 w3 w4 w5 w6 : UInt64) : UInt64 :=
  sealWithSeed boundSeed64 w0 w1 w2 w3 w4 w5 (w6 &&& 0xFFFFFFFF)

/-- State Seal (`stateSeed64`): Covers full `w0..w6` including live `[Epoch:32 | Sieve:32]`. -/
def sealStateLanes (w0 w1 w2 w3 w4 w5 w6 : UInt64) : UInt64 :=
  sealWithSeed stateSeed64 w0 w1 w2 w3 w4 w5 w6

def sealBoundCell (c : Cell64) : UInt64 :=
  sealBoundLanes c.w0 c.w1 c.w2 c.w3 c.w4 c.w5 c.w6

def sealStateCell (c : Cell64) : UInt64 :=
  sealStateLanes c.w0 c.w1 c.w2 c.w3 c.w4 c.w5 c.w6

theorem sealBoundLanes_ne_zero (w0 w1 w2 w3 w4 w5 w6 : UInt64) :
    sealBoundLanes w0 w1 w2 w3 w4 w5 w6 ≠ 0 := by
  unfold sealBoundLanes sealWithSeed
  exact nonZeroU64_ne_zero _

theorem sealStateLanes_ne_zero (w0 w1 w2 w3 w4 w5 w6 : UInt64) :
    sealStateLanes w0 w1 w2 w3 w4 w5 w6 ≠ 0 := by
  unfold sealStateLanes sealWithSeed
  exact nonZeroU64_ne_zero _

/-- Epoch-Invariance Theorem: Updating `[63:32] Epoch` leaves the boundary seal unchanged. -/
theorem sealBound_epoch_invariant
    (w0 w1 w2 w3 w4 w5 : UInt64) (ep1 ep2 req : UInt32) :
    let w6a := (ep1.toUInt64 <<< 32) ||| req.toUInt64
    let w6b := (ep2.toUInt64 <<< 32) ||| req.toUInt64
    sealBoundLanes w0 w1 w2 w3 w4 w5 w6a = sealBoundLanes w0 w1 w2 w3 w4 w5 w6b := by
  intro w6a w6b
  have hMask : (w6a &&& 0xFFFFFFFF) = (w6b &&& 0xFFFFFFFF) := by
    unfold w6a w6b
    bv_decide
  unfold sealBoundLanes
  rw [hMask]

/-! ## 3. Little-Endian 64B Wire Serialization -/

def pushLE64 (buf : ByteArray) (v : UInt64) : ByteArray :=
  let b0 : UInt8 := UInt8.ofNat (v.toNat % 256)
  let b1 : UInt8 := UInt8.ofNat ((v.toNat >>> 8) % 256)
  let b2 : UInt8 := UInt8.ofNat ((v.toNat >>> 16) % 256)
  let b3 : UInt8 := UInt8.ofNat ((v.toNat >>> 24) % 256)
  let b4 : UInt8 := UInt8.ofNat ((v.toNat >>> 32) % 256)
  let b5 : UInt8 := UInt8.ofNat ((v.toNat >>> 40) % 256)
  let b6 : UInt8 := UInt8.ofNat ((v.toNat >>> 48) % 256)
  let b7 : UInt8 := UInt8.ofNat ((v.toNat >>> 56) % 256)
  (((((((buf.push b0).push b1).push b2).push b3).push b4).push b5).push b6).push b7

theorem pushLE64_size (buf : ByteArray) (v : UInt64) :
    (pushLE64 buf v).size = buf.size + 8 := by
  simp [pushLE64, ByteArray.size_push, Nat.add_assoc]

def Cell64.toBytes (c : Cell64) : ByteArray :=
  pushLE64 (pushLE64 (pushLE64 (pushLE64 (pushLE64 (pushLE64 (pushLE64
    (pushLE64 ByteArray.empty c.w0) c.w1) c.w2) c.w3) c.w4) c.w5) c.w6) c.w7

theorem Cell64.toBytes_size (c : Cell64) : c.toBytes.size = 64 := by
  simp [Cell64.toBytes, pushLE64_size, ByteArray.size_empty]

/-! ## 4. Sealed Boundary & State Gatekeepers (`class Boundary`) -/

/-- Private constructor prevents forging unverified 64-byte boundary cells. -/
structure SealedBound where
  private mk ::
  cell : Cell64
  seal_eq : cell.w7 = sealBoundCell cell

/-- Private constructor prevents forging unsealed initial state cells in C. -/
structure SealedState where
  private mk ::
  cell : Cell64
  seal_eq : cell.w7 = sealStateCell cell

private def packSealedBound (w0 w1 w2 w3 w4 w5 w6 : UInt64) : SealedBound :=
  { cell := { w0, w1, w2, w3, w4, w5, w6, w7 := sealBoundLanes w0 w1 w2 w3 w4 w5 w6 }
    seal_eq := rfl }

private def packSealedState (w0 w1 w2 w3 w4 w5 w6 : UInt64) : SealedState :=
  { cell := { w0, w1, w2, w3, w4, w5, w6, w7 := sealStateLanes w0 w1 w2 w3 w4 w5 w6 }
    seal_eq := rfl }

def SealedBound.toBytes (b : SealedBound) : ByteArray := b.cell.toBytes
def SealedState.toBytes (s : SealedState) : ByteArray := s.cell.toBytes

theorem SealedBound.toBytes_size (b : SealedBound) : b.toBytes.size = 64 :=
  Cell64.toBytes_size b.cell

theorem SealedState.toBytes_size (s : SealedState) : s.toBytes.size = 64 :=
  Cell64.toBytes_size s.cell

def intToU64Twos (x : Int) : UInt64 :=
  UInt64.ofNat (BitVec.ofInt 64 x).toNat

def intToU32Twos (x : Int) : UInt32 :=
  UInt32.ofNat (BitVec.ofInt 32 x).toNat

/--
  Standard upstream interface for domain applications.
  Downstream code implements `Boundary α` and emits `.flow` binary bundles
  (`State + 2^k Boundaries`) at build time (`lean --run`), keeping domain tags,
  initial states, and invariants 100% inside Lean (zero `lean -c`, zero C macros).
-/
class Boundary (α : Type) where
  b0        : α → Int
  b1        : α → Int
  b2        : α → Int
  kanFilter : α → UInt64
  dtMax     : α → Int
  domainTag : α → UInt32
  auxParam  : α → UInt32 := fun _ => 0
  reqSieve  : α → UInt32 := fun _ => sieveSafetyGate
  dtMax_valid :
    ∀ a, 0 < dtMax a ∧ dtMax a ≤ maxSafeQ16
  coeffs_bounded :
    ∀ a, -maxSafeQ16 ≤ b0 a ∧ b0 a ≤ maxSafeQ16 ∧
         -maxSafeQ16 ≤ b1 a ∧ b1 a ≤ maxSafeQ16 ∧
         -maxSafeQ16 ≤ b2 a ∧ b2 a ≤ maxSafeQ16
  sieve_guarded :
    ∀ a, (reqSieve a &&& sieveBaseGate) = sieveBaseGate

def mintCell [Boundary α] (spec : α) (epoch : UInt32 := 1) : SealedBound :=
  packSealedBound
    (intToU64Twos (Boundary.b0 spec))
    (intToU64Twos (Boundary.b1 spec))
    (intToU64Twos (Boundary.b2 spec))
    (Boundary.kanFilter spec)
    (intToU64Twos (Boundary.dtMax spec))
    (((Boundary.domainTag spec).toUInt64 <<< 32) ||| (Boundary.auxParam spec).toUInt64)
    ((epoch.toUInt64 <<< 32) ||| (Boundary.reqSieve spec).toUInt64)

/--
  Mints a domain-bound initial `State` cell directly from a `Boundary` anchor,
  so C hosts never hardcode `DomainTag` or call any seal function.
-/
def mintState [Boundary α] (anchor : α) (x0Q16 : Int)
    (j0Q16 : Int := 0) (j1Q16 : Int := 0) (j2Q16 : Int := 0)
    (momentumQ16 : Int := q16One) : SealedState :=
  let dom := (Boundary.domainTag anchor).toUInt64 <<< 32
  let mom := (intToU32Twos momentumQ16).toUInt64
  packSealedState
    (intToU64Twos j0Q16)
    (intToU64Twos j1Q16)
    (intToU64Twos j2Q16)
    (intToU64Twos x0Q16)
    0
    (dom ||| mom)
    sieveSealValid.toUInt64

def appendBounds (acc : ByteArray) : Nat → List SealedBound → SealedBound → ByteArray
  | 0, _, _ => acc
  | n + 1, c :: rest, fb => appendBounds (acc ++ c.toBytes) n rest fb
  | n + 1, [], fb => appendBounds (acc ++ fb.toBytes) n [] fb

theorem appendBounds_size (acc : ByteArray) (n : Nat) (cs : List SealedBound) (fb : SealedBound) :
    (appendBounds acc n cs fb).size = acc.size + n * 64 := by
  induction n generalizing acc cs with
  | zero => simp [appendBounds]
  | succ k ih =>
    cases cs with
    | nil =>
      simp [appendBounds, ih, ByteArray.size_append, SealedBound.toBytes_size]
      omega
    | cons hd tl =>
      simp [appendBounds, ih, ByteArray.size_append, SealedBound.toBytes_size]
      omega

/-- Emits a `2^k × 64B` power-of-two Boundary Arena (`idx & ((1 << k) - 1)`). -/
def mintArena [Boundary α] (k : Nat) (specs : List α) (fallback : α) (epoch : UInt32 := 1) :
    ByteArray :=
  let cells := specs.map (fun s => mintCell s epoch)
  let fb := mintCell fallback epoch
  appendBounds ByteArray.empty (2 ^ k) cells fb

theorem mintArena_size [Boundary α] (k : Nat) (specs : List α) (fallback : α) (epoch : UInt32) :
    (mintArena k specs fallback epoch).size = (2 ^ k) * 64 := by
  simp [mintArena, appendBounds_size, ByteArray.size_empty]

/--
  Emits a self-contained `(1 + 2^k) × 64B` `.flow` Bundle:
  - Cell `0` (`bytes [0..63]`): Sealed initial `State` cell
  - Cells `1 .. 2^k`: Power-of-two `2^k` Sealed `Boundary` arena
-/
def mintBundle [Boundary α] (k : Nat) (initState : SealedState)
    (specs : List α) (fallback : α) (epoch : UInt32 := 1) : ByteArray :=
  let cells := specs.map (fun s => mintCell s epoch)
  let fb := mintCell fallback epoch
  appendBounds initState.toBytes (2 ^ k) cells fb

theorem mintBundle_size [Boundary α] (k : Nat) (initState : SealedState)
    (specs : List α) (fallback : α) (epoch : UInt32) :
    (mintBundle k initState specs fallback epoch).size = (1 + 2 ^ k) * 64 := by
  simp [mintBundle, appendBounds_size, SealedState.toBytes_size]
  omega

def emitCell (path : System.FilePath) (b : SealedBound) : IO Unit :=
  IO.FS.writeBinFile path b.toBytes

def emitState (path : System.FilePath) (s : SealedState) : IO Unit :=
  IO.FS.writeBinFile path s.toBytes

def emitArena [Boundary α] (path : System.FilePath) (k : Nat) (specs : List α)
    (fallback : α) (epoch : UInt32 := 1) : IO Unit :=
  IO.FS.writeBinFile path (mintArena k specs fallback epoch)

def emitBundle [Boundary α] (path : System.FilePath) (k : Nat) (initState : SealedState)
    (specs : List α) (fallback : α) (epoch : UInt32 := 1) : IO Unit :=
  IO.FS.writeBinFile path (mintBundle k initState specs fallback epoch)

/-! ## 5. Formal Invariants of the HoTT × 2-Jet Transition Law (`0 sorryAx`) -/

def kanProjectBV (x xCand filter : BitVec 64) : BitVec 64 :=
  (xCand &&& ~~~filter) ||| (x &&& filter)

/-- Kan Cubical Projection Theorem: Immutable homotopy axes never flip. -/
theorem kanProject_preserves_filter (x xCand filter : BitVec 64) :
    (((kanProjectBV x xCand filter) ^^^ x) &&& filter) = 0#64 := by
  unfold kanProjectBV
  bv_decide

def muxBV (commit : Bool) (tr fa : BitVec 64) : BitVec 64 :=
  if commit then tr else fa

/-- Unconditional Kan Homotopy Preservation across committed or rejected steps. -/
theorem step_coord_preserves_kan_axes (commit : Bool) (x xCand filter : BitVec 64) :
    (((muxBV commit (kanProjectBV x xCand filter) x) ^^^ x) &&& filter) = 0#64 := by
  cases commit <;> simp [muxBV, kanProject_preserves_filter]

/-- Dual-Seal & Univalence Firewall Theorem: Any state/bound seal or domain failure blocks commit. -/
theorem unsealed_or_foreign_blocks_commit
    (stateSealOk boundSealOk domOk : Bool) (x xProj : BitVec 64)
    (hFail : stateSealOk = false ∨ boundSealOk = false ∨ domOk = false) :
    muxBV ((stateSealOk && boundSealOk) && domOk) xProj x = x := by
  rcases hFail with hs | hb | hd
  · simp [hs, muxBV]
  · simp [hb, muxBV]
  · simp [hd, muxBV]

/-- Anti-Laundering Theorem: Unsealed C-fabricated states remain permanently unsealed (`w7 = 0`). -/
theorem unsealed_state_never_laundered (candSeal : BitVec 64) :
    muxBV false candSeal 0#64 = 0#64 := rfl

def liftNonzeroStep (r0 r1 r2 rawStep : Int) (tunDir : Int) : Int :=
  if r0 ≠ 0 ∧ (r1 ≠ 0 ∨ r2 ≠ 0) then
    if rawStep = 0 then (if tunDir < 0 then -1 else 1) else rawStep
  else
    0

def clampStep (nzStep dtMax : Int) : Int :=
  if nzStep > dtMax then dtMax
  else if nzStep < -dtMax then -dtMax
  else nzStep

/-- Horizon Bound Theorem: Resolved displacement magnitude always lies in `[0, dtMax]`. -/
theorem clampStep_horizon_bounded (nzStep dtMax : Int) (hPos : 0 < dtMax) :
    0 ≤ (Int.natAbs (clampStep nzStep dtMax) : Int) ∧
    (Int.natAbs (clampStep nzStep dtMax) : Int) ≤ dtMax := by
  unfold clampStep
  split
  · omega
  · split <;> omega

theorem liftNonzeroStep_ne_zero (r0 r2 rawStep tunDir : Int)
    (hRes : r0 ≠ 0) (hCurv : r2 ≠ 0) :
    liftNonzeroStep r0 0 r2 rawStep tunDir ≠ 0 := by
  unfold liftNonzeroStep
  split
  · split
    · split <;> omega
    · assumption
  · rename_i hNot
    exact False.elim (hNot ⟨hRes, Or.inr hCurv⟩)

theorem clampStep_ne_zero (s dtMax : Int) (hs : s ≠ 0) (hPos : 0 < dtMax) :
    clampStep s dtMax ≠ 0 := by
  unfold clampStep
  split
  · omega
  · split <;> omega

/--
  2-Jet Stationary Trap Escape Theorem:
  At any critical point where `r0 ≠ 0`, `r1 = 0` (gradient stalled), and `r2 ≠ 0`,
  the 2-Jet step magnitude is strictly positive (`0 < |Δt*| ≤ dtMax`).
-/
theorem stationary_trap_escapes
    (r0 r2 rawStep tunDir dtMax : Int)
    (hResidual : r0 ≠ 0) (hCurv : r2 ≠ 0) (hHorizon : 0 < dtMax) :
    let step := clampStep (liftNonzeroStep r0 0 r2 rawStep tunDir) dtMax
    0 < (Int.natAbs step : Int) ∧ (Int.natAbs step : Int) ≤ dtMax := by
  intro step
  have hNz : step ≠ 0 :=
    clampStep_ne_zero _ dtMax (liftNonzeroStep_ne_zero r0 r2 rawStep tunDir hResidual hCurv) hHorizon
  have hBnd := clampStep_horizon_bounded (liftNonzeroStep r0 0 r2 rawStep tunDir) dtMax hHorizon
  exact ⟨by omega, hBnd.2⟩

/-! ## 6. Core Verification Fixtures (No Top-Level `_root_.main`) -/

structure QuadraticRule where
  b0Val      : Int
  b1Val      : Int
  b2Val      : Int
  kanMask    : UInt64
  horizon    : Int
  domTag     : UInt32
  reqMask    : UInt32
  hHorizon   : 0 < horizon ∧ horizon ≤ maxSafeQ16
  hBounded   : -maxSafeQ16 ≤ b0Val ∧ b0Val ≤ maxSafeQ16 ∧
               -maxSafeQ16 ≤ b1Val ∧ b1Val ≤ maxSafeQ16 ∧
               -maxSafeQ16 ≤ b2Val ∧ b2Val ≤ maxSafeQ16
  hGuarded   : (reqMask &&& sieveBaseGate) = sieveBaseGate

instance : Boundary QuadraticRule where
  b0 r := r.b0Val
  b1 r := r.b1Val
  b2 r := r.b2Val
  kanFilter r := r.kanMask
  dtMax r := r.horizon
  domainTag r := r.domTag
  reqSieve r := r.reqMask
  dtMax_valid r := r.hHorizon
  coeffs_bounded r := r.hBounded
  sieve_guarded r := r.hGuarded

def flowDomainTag : UInt32 := 0x464C4F57 -- "FLOW"

private theorem mkSieveProof : (sieveSafetyGate &&& sieveBaseGate) = sieveBaseGate := by
  unfold sieveSafetyGate sieveBaseGate sieveSealValid sieveDomainMatch
         sieveNoOverflow sieveHorizonValid
  bv_decide

def ruleQuadraticTrap : QuadraticRule where
  b0Val    := 2 * q16One
  b1Val    := 0
  b2Val    := -2 * q16One
  kanMask  := 0
  horizon  := 4 * q16One
  domTag   := flowDomainTag
  reqMask  := sieveSafetyGate
  hHorizon := by decide
  hBounded := by decide
  hGuarded := mkSieveProof

def ruleInstantonWell : QuadraticRule where
  b0Val    := 2 * q16One
  b1Val    := 0
  b2Val    := 2 * q16One
  kanMask  := 0
  horizon  := 4 * q16One
  domTag   := flowDomainTag
  reqMask  := sieveSafetyGate
  hHorizon := by decide
  hBounded := by decide
  hGuarded := mkSieveProof

def ruleKanGuarded : QuadraticRule where
  b0Val    := 3 * q16One
  b1Val    := 1 * q16One
  b2Val    := 0
  kanMask  := 0xFFFF000000000000
  horizon  := 8 * q16One
  domTag   := flowDomainTag
  reqMask  := sieveSafetyGate ||| sieveKanHomotopic
  hHorizon := by decide
  hBounded := by decide
  hGuarded := by
    unfold sieveSafetyGate sieveBaseGate sieveSealValid sieveDomainMatch
           sieveNoOverflow sieveHorizonValid sieveKanHomotopic
    bv_decide

def ruleGlobalBasin : QuadraticRule where
  b0Val    := -9 * (q16One / 2)
  b1Val    := 0
  b2Val    := 1 * q16One
  kanMask  := 0
  horizon  := 4 * q16One
  domTag   := flowDomainTag
  reqMask  := sieveSafetyGate
  hHorizon := by decide
  hBounded := by decide
  hGuarded := mkSieveProof

def ruleZeroTarget : QuadraticRule where
  b0Val    := 0
  b1Val    := 0
  b2Val    := 0
  kanMask  := 0
  horizon  := 4 * q16One
  domTag   := flowDomainTag
  reqMask  := sieveSafetyGate
  hHorizon := by decide
  hBounded := by decide
  hGuarded := mkSieveProof

def ruleForeignDomain : QuadraticRule where
  b0Val    := 2 * q16One
  b1Val    := 0
  b2Val    := -2 * q16One
  kanMask  := 0
  horizon  := 4 * q16One
  domTag   := 0x4F544852 -- "OTHR"
  reqMask  := sieveSafetyGate
  hHorizon := by decide
  hBounded := by decide
  hGuarded := mkSieveProof

/-- Emits self-contained `.flow` test bundles & states (invoked by FLOW's own `Makefile`). -/
def emitCoreTestFixtures (dirPath : String := "build/capsules") : IO Unit := do
  let outDir := System.FilePath.mk dirPath
  IO.FS.createDirAll outDir
  -- Self-contained bundle: Slot 0 = x0=0 State, Slots 1..4 = [Instanton, Basin, QuadTrap, Kan]
  emitBundle (outDir / "core_bundle4.flow") 2
    (mintState ruleQuadraticTrap 0)
    [ruleInstantonWell, ruleGlobalBasin, ruleQuadraticTrap, ruleKanGuarded]
    ruleQuadraticTrap 1
  -- Additional Lean-minted initial states & probes (zero manual sealing in C!)
  emitState (outDir / "state_pos1.flow")     (mintState ruleQuadraticTrap (1 * q16One))
  emitState (outDir / "state_overflow.flow") (mintState ruleQuadraticTrap ((1 <<< 61) : Int))
  emitState (outDir / "state_foreign.flow")  (mintState ruleForeignDomain 0)
  emitState (outDir / "state_fiber.flow")
    (mintState ruleZeroTarget 0 (2 * q16One) (-3 * q16One) (2 * q16One))
  emitCell  (outDir / "zero_target.flow")    (mintCell ruleZeroTarget 1)
  IO.println s!"  [PASS] Minted sealed .flow bundle & states in {outDir}"

end Flow
