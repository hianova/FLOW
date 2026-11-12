import Std.Tactic.BVDecide

/-
  FLOW — Upstream Semantic Gatekeeper & Manifold Atlas Emitter
  ============================================================
  Pure Lean 4 library module (zero `_root_.main` collision, zero `lean -c` codegen).
  Compiles multi-regime domain rules, Schmitt hysteresis deadbands, orthogonal
  Cubical Kan mode locks, and 2-Jet equilibrium laws via partial evaluation into
  self-contained `(1 + 2^k) × 64B` `.flow` Manifold Atlases (`0 sorryAx`).
-/

namespace Flow

def version : String := "5.0.0"

def q16Shift   : Nat := 16
def q16One     : Int := 65536
def maxSafeQ16 : Int := 70368744177664 -- 2^46
def minI32     : Int := -2147483648
def maxI32     : Int := 2147483647

def fpMod       : Nat    := 0xFFFFFFFF00000001
def golden64    : UInt64 := 0x9E3779B97F4A7C15
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
  Epoch-invariant Chart (`Boundary`) seal (`boundSeed64`):
  Masks `w6` to `[31:0] ReqSieve` so runtime QSBR / `@swap` `Epoch` advances in C
  never require or allow re-signing a Lean-minted chart cell.
-/
def sealBoundLanes (w0 w1 w2 w3 w4 w5 w6 : UInt64) : UInt64 :=
  sealWithSeed boundSeed64 w0 w1 w2 w3 w4 w5 (w6 &&& 0xFFFFFFFF)

/-- State seal (`stateSeed64`): Covers full `w0..w6` including live `[Epoch:32 | Sieve:32]`. -/
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

/-- Epoch-Invariance Theorem: Advancing a chart's runtime Epoch leaves its Lean seal unchanged. -/
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

def verifyBoundCell (c : Cell64) : Bool :=
  c.w7 == sealBoundCell c

def verifyStateCell (c : Cell64) : Bool :=
  c.w7 == sealStateCell c

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

/-! ## 4. Upstream Chart Typeclass, `Rule k` Builder & Sealed Gatekeepers -/

/--
  Proof-carrying Manifold Chart over a `2^k`-slot Atlas (`k ≤ 8`):
  - `b0, b1, b2` : Local 2-Jet quadratic manifold coefficients (Q48.16)
  - `bExit, bEnter` : Schmitt hysteresis deadband `[bExit, bEnter]` (Q16.16 `i32`)
  - `dtMax` : Admissible per-step displacement horizon `> 0` (Q16.16 `i32`)
  - `uGain` : External runtime input `u_ext` coupling gain (Q16.16 `i32`)
  - `domainTag` : 32-bit Univalence domain identifier
  - `nextLo, nextHi` : Branchless hysteresis target chart indices in `0 .. 2^k - 1`
  - `kanLock` : Orthogonal 16-bit discrete Cubical mode lock mask
-/
class Chart (k : outParam Nat) (α : Type) where
  b0        : α → Int
  b1        : α → Int
  b2        : α → Int
  bExit     : α → Int := fun _ => minI32
  bEnter    : α → Int := fun _ => maxI32
  dtMax     : α → Int
  uGain     : α → Int := fun _ => 0
  domainTag : α → UInt32
  nextLo    : α → UInt8 := fun _ => 0
  nextHi    : α → UInt8 := fun _ => 0
  kanLock   : α → UInt16 := fun _ => 0
  reqSieve  : α → UInt32 := fun _ => sieveSafetyGate
  atlas_pow2_valid : α → k ≤ 8
  dtMax_valid      : ∀ s : α, 0 < dtMax s ∧ dtMax s ≤ maxI32
  uGain_valid      : ∀ s : α, minI32 ≤ uGain s ∧ uGain s ≤ maxI32
  hyst_valid       : ∀ s : α, minI32 ≤ bExit s ∧ bExit s ≤ bEnter s ∧ bEnter s ≤ maxI32
  coeffs_bounded   : ∀ s : α,
    -maxSafeQ16 ≤ b0 s ∧ b0 s ≤ maxSafeQ16 ∧
    -maxSafeQ16 ≤ b1 s ∧ b1 s ≤ maxSafeQ16 ∧
    -maxSafeQ16 ≤ b2 s ∧ b2 s ≤ maxSafeQ16
  adj_valid        : ∀ s : α, (nextLo s).toNat < 2 ^ k ∧ (nextHi s).toNat < 2 ^ k
  sieve_guarded    : ∀ s : α, (reqSieve s &&& sieveBaseGate) = sieveBaseGate

/--
  Ergonomic dependent chart specification for a `2^k`-chart Atlas (`k ≤ 8`).
  All proof obligations default to `by decide` so concrete domain rules are
  machine-checked at compile time with zero boilerplate.
-/
structure Rule (k : Nat) where
  b0        : Int    := 0
  b1        : Int    := 0
  b2        : Int    := 0
  bExit     : Int    := minI32
  bEnter    : Int    := maxI32
  dtMax     : Int    := q16One
  uGain     : Int    := 0
  domTag    : UInt32
  nextLo    : UInt8  := 0
  nextHi    : UInt8  := 0
  kanLock   : UInt16 := 0
  reqSieve  : UInt32 := sieveSafetyGate
  hK        : k ≤ 8 := by decide
  hHorizon  : 0 < dtMax ∧ dtMax ≤ maxI32 := by decide
  hGain     : minI32 ≤ uGain ∧ uGain ≤ maxI32 := by decide
  hHyst     : minI32 ≤ bExit ∧ bExit ≤ bEnter ∧ bEnter ≤ maxI32 := by decide
  hBounded  : -maxSafeQ16 ≤ b0 ∧ b0 ≤ maxSafeQ16 ∧
              -maxSafeQ16 ≤ b1 ∧ b1 ≤ maxSafeQ16 ∧
              -maxSafeQ16 ≤ b2 ∧ b2 ≤ maxSafeQ16 := by decide
  hAdj      : nextLo.toNat < 2 ^ k ∧ nextHi.toNat < 2 ^ k := by decide
  hGuarded  : (reqSieve &&& sieveBaseGate) = sieveBaseGate := by decide

instance {k : Nat} : Chart k (Rule k) where
  b0 r               := r.b0
  b1 r               := r.b1
  b2 r               := r.b2
  bExit r            := r.bExit
  bEnter r           := r.bEnter
  dtMax r            := r.dtMax
  uGain r            := r.uGain
  domainTag r        := r.domTag
  nextLo r           := r.nextLo
  nextHi r           := r.nextHi
  kanLock r          := r.kanLock
  reqSieve r         := r.reqSieve
  atlas_pow2_valid r := r.hK
  dtMax_valid r      := r.hHorizon
  uGain_valid r      := r.hGain
  hyst_valid r       := r.hHyst
  coeffs_bounded r   := r.hBounded
  adj_valid r        := r.hAdj
  sieve_guarded r    := r.hGuarded

/-- Private constructor prevents forging unverified 64-byte chart cells. -/
structure SealedBound where
  private mk ::
  cell : Cell64
  seal_eq : cell.w7 = sealBoundCell cell

/-- Private constructor prevents forging unsealed initial state cells. -/
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

/-- Mints a 64-byte sealed Chart cell from any verified `Chart k` instance. -/
def mintCell {k : Nat} {α : Type} [Chart k α] (spec : α) (epoch : UInt32 := 1) : SealedBound :=
  let w3 := ((intToU32Twos (Chart.bExit spec)).toUInt64 <<< 32) |||
            (intToU32Twos (Chart.bEnter spec)).toUInt64
  let w4 := ((intToU32Twos (Chart.dtMax spec)).toUInt64 <<< 32) |||
            (intToU32Twos (Chart.uGain spec)).toUInt64
  let w5 := ((Chart.domainTag spec).toUInt64 <<< 32) |||
            ((Chart.nextLo spec).toUInt64 <<< 24) |||
            ((Chart.nextHi spec).toUInt64 <<< 16) |||
            (Chart.kanLock spec).toUInt64
  let w6 := (epoch.toUInt64 <<< 32) ||| (Chart.reqSieve spec).toUInt64
  packSealedBound
    (intToU64Twos (Chart.b0 spec))
    (intToU64Twos (Chart.b1 spec))
    (intToU64Twos (Chart.b2 spec))
    w3 w4 w5 w6

/-- Mints an initial domain-bound `State` cell directly from a verified `Chart k` anchor. -/
def mintState {k : Nat} {α : Type} [Chart k α] (anchor : α) (x0Q16 : Int)
    (initChart : UInt16 := 0) (initMode : UInt16 := 0)
    (j0Q16 : Int := 0) (j1Q16 : Int := 0) (j2Q16 : Int := 0) : SealedState :=
  let capMask : UInt16 := UInt16.ofNat ((2 ^ k) - 1)
  let safeChart : UInt16 := initChart &&& capMask
  let w5 := ((Chart.domainTag anchor).toUInt64 <<< 32) |||
            (safeChart.toUInt64 <<< 16) |||
            initMode.toUInt64
  packSealedState
    (intToU64Twos j0Q16)
    (intToU64Twos j1Q16)
    (intToU64Twos j2Q16)
    (intToU64Twos x0Q16)
    0
    w5
    sieveSealValid.toUInt64

def appendBounds (acc : ByteArray) : Nat → List SealedBound → SealedBound → ByteArray
  | 0, _, _ => acc
  | n + 1, c :: rest, fb => appendBounds (acc ++ c.toBytes) n rest fb
  | n + 1, [], fb => appendBounds (acc ++ fb.toBytes) n [] fb

theorem appendBounds_size (acc : ByteArray) (n : Nat) (cs : List SealedBound) (fb : SealedBound) :
    (appendBounds acc n cs fb).size = acc.size + n * 64 := by
  induction n generalizing acc cs with
  | zero => simp [appendBounds]
  | succ m ih =>
    cases cs with
    | nil =>
      simp [appendBounds, ih, ByteArray.size_append, SealedBound.toBytes_size]
      omega
    | cons hd tl =>
      simp [appendBounds, ih, ByteArray.size_append, SealedBound.toBytes_size]
      omega

/-- Emits a `2^k × 64B` power-of-two Chart Arena (`idx & ((1 << k) - 1)`). -/
def mintArena {kOut : Nat} {α : Type} [Chart kOut α] (k : Nat) (specs : List α) (fallback : α)
    (epoch : UInt32 := 1) : ByteArray :=
  let cells := specs.map (fun s => mintCell s epoch)
  let fb := mintCell fallback epoch
  appendBounds ByteArray.empty (2 ^ k) cells fb

theorem mintArena_size {kOut : Nat} {α : Type} [Chart kOut α] (k : Nat) (specs : List α)
    (fallback : α) (epoch : UInt32) :
    (mintArena k specs fallback epoch).size = (2 ^ k) * 64 := by
  simp [mintArena, appendBounds_size, ByteArray.size_empty]

/--
  Emits a self-contained `(1 + 2^k) × 64B` `.flow` Manifold Atlas Bundle:
  - Cell `0` (`bytes [0..63]`): Sealed initial `State` cell (`"FLOWSTA5"`)
  - Cells `1 .. 2^k`: Power-of-two `2^k` Sealed `Chart` Arena (`"FLOWBND5"`)
-/
def mintBundle {kOut : Nat} {α : Type} [Chart kOut α] (k : Nat) (initState : SealedState)
    (specs : List α) (fallback : α) (epoch : UInt32 := 1) : ByteArray :=
  let cells := specs.map (fun s => mintCell s epoch)
  let fb := mintCell fallback epoch
  appendBounds initState.toBytes (2 ^ k) cells fb

theorem mintBundle_size {kOut : Nat} {α : Type} [Chart kOut α] (k : Nat) (initState : SealedState)
    (specs : List α) (fallback : α) (epoch : UInt32) :
    (mintBundle k initState specs fallback epoch).size = (1 + 2 ^ k) * 64 := by
  simp [mintBundle, appendBounds_size, SealedState.toBytes_size]
  omega

def emitCell (path : System.FilePath) (b : SealedBound) : IO Unit :=
  IO.FS.writeBinFile path b.toBytes

def emitState (path : System.FilePath) (s : SealedState) : IO Unit :=
  IO.FS.writeBinFile path s.toBytes

def emitArena {k : Nat} {α : Type} [Chart k α] (path : System.FilePath) (kExp : Nat)
    (specs : List α) (fallback : α) (epoch : UInt32 := 1) (hEq : kExp = k := by rfl) : IO Unit :=
  let _ := hEq
  IO.FS.writeBinFile path (mintArena kExp specs fallback epoch)

def emitBundle {k : Nat} {α : Type} [Chart k α] (path : System.FilePath) (kExp : Nat)
    (initState : SealedState) (specs : List α) (fallback : α)
    (epoch : UInt32 := 1) (hEq : kExp = k := by rfl) : IO Unit :=
  let _ := hEq
  IO.FS.writeBinFile path (mintBundle kExp initState specs fallback epoch)

/-! ## 5. Formal Invariants of the HoTT × 2-Jet Atlas Transition Law (`0 sorryAx`) -/

def kanModeProjBV (mode mReq lock : BitVec 16) : BitVec 16 :=
  (mReq &&& ~~~lock) ||| (mode &&& lock)

/-- Orthogonal Cubical Kan Mode Projection Theorem: Locked 16-bit mode axes never flip. -/
theorem kanMode_preserves_locked (mode mReq lock : BitVec 16) :
    (((kanModeProjBV mode mReq lock) ^^^ mode) &&& lock) = 0#16 := by
  unfold kanModeProjBV
  bv_decide

def muxBV {w : Nat} (commit : Bool) (tr fa : BitVec w) : BitVec w :=
  if commit then tr else fa

/-- Unconditional Kan Mode Preservation across committed or rejected transitions. -/
theorem step_mode_preserves_kan_lock (commit : Bool) (mode mReq lock : BitVec 16) :
    (((muxBV commit (kanModeProjBV mode mReq lock) mode) ^^^ mode) &&& lock) = 0#16 := by
  cases commit <;> simp [muxBV, kanMode_preserves_locked]

/-- Dual-Seal & Univalence Firewall Theorem: Any state/chart seal or domain failure blocks commit. -/
theorem unsealed_or_foreign_blocks_commit
    (stateSealOk boundSealOk domOk : Bool) (x xNext : BitVec 64)
    (hFail : stateSealOk = false ∨ boundSealOk = false ∨ domOk = false) :
    muxBV ((stateSealOk && boundSealOk) && domOk) xNext x = x := by
  rcases hFail with hs | hb | hd
  · simp [hs, muxBV]
  · simp [hb, muxBV]
  · simp [hd, muxBV]

/-- Anti-Laundering Theorem: Unsealed states remain permanently unsealed (`w7 = 0`). -/
theorem unsealed_state_never_laundered (candSeal : BitVec 64) :
    muxBV false candSeal 0#64 = 0#64 := rfl

/-- Branchless Schmitt Hysteresis Chart Selector over `Z / 2^k Z`. -/
def schmittNextChart (k : Nat) (chart nextLo nextHi : Nat) (bExit bEnter uExt : Int) : Nat :=
  let c1 := if uExt < bExit then nextLo else chart
  let c2 := if uExt > bEnter then nextHi else c1
  c2 % (2 ^ k)

/-- Schmitt Deadband Anti-Flapping Theorem: Inside `[bExit, bEnter]`, active chart never flaps. -/
theorem schmitt_deadband_holds_chart
    (k chart nextLo nextHi : Nat) (bExit bEnter uExt : Int)
    (hChart : chart < 2 ^ k) (hBand : bExit ≤ uExt ∧ uExt ≤ bEnter) :
    schmittNextChart k chart nextLo nextHi bExit bEnter uExt = chart := by
  unfold schmittNextChart
  have hNotLo : ¬(uExt < bExit) := by omega
  have hNotHi : ¬(uExt > bEnter) := by omega
  simp [hNotLo, hNotHi, Nat.mod_eq_of_lt hChart]

/-- Atlas Closure Theorem: Chart index is always strictly bounded in `0 .. 2^k - 1` (`0` OOB). -/
theorem schmitt_chart_in_bounds
    (k chart nextLo nextHi : Nat) (bExit bEnter uExt : Int) :
    schmittNextChart k chart nextLo nextHi bExit bEnter uExt < 2 ^ k := by
  unfold schmittNextChart
  have hPos : 0 < 2 ^ k := Nat.two_pow_pos k
  exact Nat.mod_lt _ hPos

def liftNonzeroStep (r0 r1 r2 rawStep : Int) (gradDir : Int) : Int :=
  if r0 ≠ 0 ∧ (r1 ≠ 0 ∨ r2 ≠ 0) then
    if rawStep = 0 then (if gradDir < 0 then -1 else 1) else rawStep
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

theorem liftNonzeroStep_ne_zero (r0 r2 rawStep gradDir : Int)
    (hRes : r0 ≠ 0) (hCurv : r2 ≠ 0) :
    liftNonzeroStep r0 0 r2 rawStep gradDir ≠ 0 := by
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
    (r0 r2 rawStep gradDir dtMax : Int)
    (hResidual : r0 ≠ 0) (hCurv : r2 ≠ 0) (hHorizon : 0 < dtMax) :
    let step := clampStep (liftNonzeroStep r0 0 r2 rawStep gradDir) dtMax
    0 < (Int.natAbs step : Int) ∧ (Int.natAbs step : Int) ≤ dtMax := by
  intro step
  have hNz : step ≠ 0 :=
    clampStep_ne_zero _ dtMax (liftNonzeroStep_ne_zero r0 r2 rawStep gradDir hResidual hCurv) hHorizon
  have hBnd := clampStep_horizon_bounded (liftNonzeroStep r0 0 r2 rawStep gradDir) dtMax hHorizon
  exact ⟨by omega, hBnd.2⟩

/-! ## 6. Core Verification Atlas Fixtures (Invoked by FLOW's `Makefile`) -/

def flowDomainTag : UInt32 := 0x464C4F57 -- "FLOW"

/-- Chart 0: Instanton / Barrier Well (`1 + x^2`, `Δ = -4 < 0`, curvature tunneling `dt = 1.0`). -/
def chartInstanton : Rule 2 where
  b0     := 1 * q16One
  b1     := 0
  b2     := 2 * q16One
  bExit  := -100 * q16One
  bEnter := 100 * q16One
  dtMax  := 2 * q16One
  uGain  := 0
  domTag := flowDomainTag
  nextLo := 0
  nextHi := 0

/-- Chart 1: Normal Linear Tracking + Hysteresis (`x* = u_ext`, jumps to Chart 2 if `u_ext > 10`). -/
def chartNormalTrack : Rule 2 where
  b0     := 0
  b1     := q16One
  b2     := 0
  bExit  := -100 * q16One
  bEnter := 10 * q16One
  dtMax  := 4 * q16One
  uGain  := q16One
  domTag := flowDomainTag
  nextLo := 1
  nextHi := 2

/-- Chart 2: Saddle / Convex Surge (`-4 - u_ext + x^2 = 0`, `b1 = 0`, Kan lock `0x0003`, `[6, 50]`). -/
def chartConvexSurge : Rule 2 where
  b0      := -4 * q16One
  b1      := 0
  b2      := 2 * q16One
  bExit   := 6 * q16One
  bEnter  := 50 * q16One
  dtMax   := 4 * q16One
  uGain   := q16One
  domTag  := flowDomainTag
  nextLo  := 1
  nextHi  := 3
  kanLock := 0x0003

/-- Chart 3: Circuit-Breaker Safe Harbor (`uGain = 0` isolates shock, pulls `x -> 1.0`, Kan gated). -/
def chartSafeHarbor : Rule 2 where
  b0       := -1 * q16One
  b1       := q16One
  b2       := 0
  bExit    := 2 * q16One
  bEnter   := 500 * q16One
  dtMax    := 8 * q16One
  uGain    := 0
  domTag   := flowDomainTag
  nextLo   := 1
  nextHi   := 3
  kanLock  := 0x0003
  reqSieve := sieveSafetyGate ||| sieveKanHomotopic

def chartZeroTarget : Rule 2 where
  b0     := 0
  b1     := 0
  b2     := 0
  dtMax  := 4 * q16One
  uGain  := 0
  domTag := flowDomainTag

def chartForeign : Rule 2 where
  b0     := 2 * q16One
  b1     := 0
  b2     := -2 * q16One
  dtMax  := 4 * q16One
  domTag := 0x4F544852 -- "OTHR"

/-- Emits core `.flow` Atlas & probe cells for `make test` (`Gate 1..4`). -/
def emitCoreTestFixtures (dirPath : String := "build/capsules") : IO Unit := do
  let outDir := System.FilePath.mk dirPath
  IO.FS.createDirAll outDir
  -- 4-Chart Atlas (`k = 2`), starting on Chart 1 (`chartNormalTrack`), `x0 = 0`, `mode = 0x0003`
  let sInit := mintState chartNormalTrack 0 (initChart := 1) (initMode := 0x0003)
  emitBundle (outDir / "core_atlas4.flow") 2 sInit
    [chartInstanton, chartNormalTrack, chartConvexSurge, chartSafeHarbor]
    chartNormalTrack 1
  -- Additional Lean-minted states & cells for engine unit tests
  emitState (outDir / "state_chart0.flow")   (mintState chartInstanton 0 (initChart := 0))
  emitState (outDir / "state_chart2.flow")   (mintState chartConvexSurge 0 (initChart := 2) (initMode := 0x0003))
  emitState (outDir / "state_overflow.flow") (mintState chartConvexSurge ((1 <<< 61) : Int) (initChart := 2))
  emitState (outDir / "state_foreign.flow")  (mintState chartForeign 0)
  emitState (outDir / "state_fiber.flow")
    (mintState chartZeroTarget 0 (initChart := 0) (initMode := 0) (2 * q16One) (-3 * q16One) (2 * q16One))
  emitCell  (outDir / "zero_target.flow")    (mintCell chartZeroTarget 1)
  emitCell  (outDir / "swap_harbor.flow")    (mintCell chartSafeHarbor 1)
  IO.println s!"  [PASS] Minted sealed .flow Manifold Atlas & probes in {outDir}"

end Flow
