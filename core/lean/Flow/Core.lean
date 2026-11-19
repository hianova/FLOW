/-
  Flow.Core — Canonical 64B Cell Layout, Wire Encoding & 2-Jet Schmitt Atlas
  ===========================================================================
  Pure Lean 4 foundational definitions (zero external imports, zero IO, zero `sorryAx`).
-/

namespace Flow

def version  : String := "8.0.0"
def q16Shift : Nat    := 16
def q16One   : Int    := 65536
def maxQ16   : Int    := 0x7FFFFFFF -- Symmetric Q16.16 bound (±32,767.99998)

/-! ## 1. Canonical 64-Byte Cell (`8 × u64`) & Little-Endian Wire Layout -/

structure Cell64 where
  w0 : UInt64 := 0
  w1 : UInt64 := 0
  w2 : UInt64 := 0
  w3 : UInt64 := 0
  w4 : UInt64 := 0
  w5 : UInt64 := 0
  w6 : UInt64 := 0
  w7 : UInt64 := 0
  deriving Repr, DecidableEq, Inhabited

def pushLE16 (buf : ByteArray) (v : UInt16) : ByteArray :=
  let b0 : UInt8 := UInt8.ofNat (v.toNat % 256)
  let b1 : UInt8 := UInt8.ofNat ((v.toNat >>> 8) % 256)
  (buf.push b0).push b1

def pushLE32 (buf : ByteArray) (v : UInt32) : ByteArray :=
  let b0 : UInt8 := UInt8.ofNat (v.toNat % 256)
  let b1 : UInt8 := UInt8.ofNat ((v.toNat >>> 8) % 256)
  let b2 : UInt8 := UInt8.ofNat ((v.toNat >>> 16) % 256)
  let b3 : UInt8 := UInt8.ofNat ((v.toNat >>> 24) % 256)
  (((buf.push b0).push b1).push b2).push b3

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

theorem pushLE64_size (b : ByteArray) (v : UInt64) :
    (pushLE64 b v).size = b.size + 8 := by
  simp [pushLE64, ByteArray.size_push]

def pushFixedAscii (buf : ByteArray) (s : String) (width : Nat) : ByteArray :=
  Id.run do
    let raw := s.toUTF8
    let mut out := buf
    for i in [:width] do
      let b : UInt8 := if i + 1 < width && i < raw.size then raw[i]! else 0
      out := out.push b
    return out

def fnv1a64Bytes (buf : ByteArray) : UInt64 :=
  Id.run do
    let mut h : UInt64 := 0xCBF29CE484222325
    let prime : UInt64 := 0x00000100000001B3
    for i in [:buf.size] do
      h := (h ^^^ (buf[i]!).toUInt64) * prime
    return h

def Cell64.toBytes (c : Cell64) : ByteArray :=
  pushLE64 (pushLE64 (pushLE64 (pushLE64 (pushLE64 (pushLE64 (pushLE64
    (pushLE64 ByteArray.empty c.w0) c.w1) c.w2) c.w3) c.w4) c.w5) c.w6) c.w7

theorem cell64_bytes_exact (c : Cell64) : c.toBytes.size = 64 := by
  simp [Cell64.toBytes, pushLE64_size, ByteArray.size_empty]

/-! ## 2. Total Symmetric Normalization & Atlas Specification -/

def clampSym (x lim : Int) : Int :=
  if x > lim then lim
  else if x < -lim then -lim
  else x

def intToU64Twos (x : Int) : UInt64 :=
  UInt64.ofNat (BitVec.ofInt 64 x).toNat

def intToU32Twos (x : Int) : UInt32 :=
  UInt32.ofNat (BitVec.ofInt 32 x).toNat

/--
  2-Jet Manifold Chart specification:
  - `b0, b1, b2`    : Local 2-Jet quadratic coefficients (Q16.16)
  - `bExit, bEnter` : Schmitt hysteresis deadband `[bExit, bEnter]` (Q16.16)
  - `dtMax`         : Per-step displacement horizon `>= 1` (Q16.16)
  - `uGain`         : External input `u_ext` coupling gain (Q16.16)
  - `nextLo, nextHi`: Hysteresis target chart indices in `0 .. 2^k - 1`
  - `kanLock`       : Orthogonal 16-bit discrete Cubical mode lock mask
-/
structure Rule where
  b0      : Int    := 0
  b1      : Int    := 0
  b2      : Int    := 0
  bExit   : Int    := -maxQ16
  bEnter  : Int    := maxQ16
  dtMax   : Int    := q16One
  uGain   : Int    := 0
  nextLo  : UInt8  := 0
  nextHi  : UInt8  := 0
  kanLock : UInt16 := 0
  deriving Repr, DecidableEq

/-- Mints a normalized 64-byte Chart cell (`bound[0..7]`). -/
def mintCell (r : Rule) (epoch : UInt32 := 1) : Cell64 :=
  let c0 := clampSym r.b0 maxQ16
  let c1 := clampSym r.b1 maxQ16
  let c2 := clampSym r.b2 maxQ16
  let lo := clampSym r.bExit maxQ16
  let hiRaw := clampSym r.bEnter maxQ16
  let hi := if hiRaw < lo then lo else hiRaw
  let dtRaw := clampSym r.dtMax maxQ16
  let dt := if dtRaw < 1 then 1 else dtRaw
  let ug := clampSym r.uGain maxQ16
  let w3 := ((intToU32Twos lo).toUInt64 <<< 32) ||| (intToU32Twos hi).toUInt64
  let w4 := ((intToU32Twos dt).toUInt64 <<< 32) ||| (intToU32Twos ug).toUInt64
  let w5 := (r.nextLo.toUInt64 <<< 24) ||| (r.nextHi.toUInt64 <<< 16) ||| r.kanLock.toUInt64
  let w6 := epoch.toUInt64 <<< 32
  { w0 := intToU64Twos c0
    w1 := intToU64Twos c1
    w2 := intToU64Twos c2
    w3 := w3
    w4 := w4
    w5 := w5
    w6 := w6
    w7 := 0 }

/-- Mints a normalized 64-byte initial State cell (`state[0..7]`). -/
def mintState (kExp : Nat) (x0Q16 : Int) (initChart : UInt16 := 0) (initMode : UInt16 := 0)
    (j0Q16 : Int := 0) (j1Q16 : Int := 0) (j2Q16 : Int := 0) : Cell64 :=
  let kClamped := if kExp > 8 then 8 else kExp
  let capMask : UInt16 := UInt16.ofNat ((2 ^ kClamped) - 1)
  let safeChart : UInt16 := initChart &&& capMask
  let xClamped := clampSym x0Q16 maxQ16
  let j2Clamped := clampSym j2Q16 maxQ16
  let w5 := (safeChart.toUInt64 <<< 16) ||| initMode.toUInt64
  { w0 := intToU64Twos j0Q16
    w1 := intToU64Twos j1Q16
    w2 := intToU64Twos j2Clamped
    w3 := intToU64Twos xClamped
    w4 := 0
    w5 := w5
    w6 := 0
    w7 := 0 }

def appendCells (acc : ByteArray) : Nat → List Cell64 → Cell64 → ByteArray
  | 0, _, _ => acc
  | n + 1, c :: rest, fb => appendCells (acc ++ c.toBytes) n rest fb
  | n + 1, [], fb => appendCells (acc ++ fb.toBytes) n [] fb

/-- Emits a `2^k × 64B` power-of-two Chart Arena (`k <= 8`). -/
def mintArena (kExp : Nat) (specs : List Rule) (fallback : Rule) (epoch : UInt32 := 1) : ByteArray :=
  let kClamped := if kExp > 8 then 8 else kExp
  let cap : UInt8 := UInt8.ofNat ((2 ^ kClamped) - 1)
  let maskRule (r : Rule) : Rule :=
    { r with nextLo := r.nextLo &&& cap, nextHi := r.nextHi &&& cap }
  let cells := specs.map (fun s => mintCell (maskRule s) epoch)
  let fb := mintCell (maskRule fallback) epoch
  appendCells ByteArray.empty (2 ^ kClamped) cells fb

/-- Emits a `(1 + 2^k) × 64B` `.flow` Manifold Atlas Bundle (State + `2^k` Charts). -/
def mintBundle (kExp : Nat) (initState : Cell64) (specs : List Rule) (fallback : Rule)
    (epoch : UInt32 := 1) : ByteArray :=
  initState.toBytes ++ mintArena kExp specs fallback epoch

/-! ## 3. Canonical Piecewise-Linear & Tropical Synthesis Primitives -/

@[reducible] def relu (x : Int) : Int := if x > 0 then x else 0
@[reducible] def imin (a b : Int) : Int := if a ≤ b then a else b
@[reducible] def imax (a b : Int) : Int := if a ≥ b then a else b
@[reducible] def iabs (x : Int) : Int := if x ≥ 0 then x else -x
@[reducible] def qmid (a b : Int) : Int := (a + b) / 2

/-! ## 4. Structural Well-Formedness, Schmitt Stability & Shock Isolation Theorems -/

def Rule.wf (kExp : Nat) (r : Rule) : Bool :=
  let kClamped := if kExp > 8 then 8 else kExp
  let nCharts := 2 ^ kClamped
  (-maxQ16 ≤ r.b0 && r.b0 ≤ maxQ16) &&
  (-maxQ16 ≤ r.b1 && r.b1 ≤ maxQ16) &&
  (-maxQ16 ≤ r.b2 && r.b2 ≤ maxQ16) &&
  (-maxQ16 ≤ r.bExit && r.bExit ≤ maxQ16) &&
  (-maxQ16 ≤ r.bEnter && r.bEnter ≤ maxQ16) &&
  (-maxQ16 ≤ r.uGain && r.uGain ≤ maxQ16) &&
  (r.bExit ≤ r.bEnter) &&
  (1 ≤ r.dtMax && r.dtMax ≤ maxQ16) &&
  (r.nextLo.toNat < nCharts) &&
  (r.nextHi.toNat < nCharts)

def schmittNext (kExp : Nat) (cur : UInt8) (r : Rule) (xNext : Int) : UInt8 :=
  let kClamped := if kExp > 8 then 8 else kExp
  let cap : UInt8 := UInt8.ofNat ((2 ^ kClamped) - 1)
  let raw := if xNext < r.bExit then r.nextLo else cur
  let nxt := if xNext > r.bEnter then r.nextHi else raw
  nxt &&& cap

def Rule.evalLinearResidual (r : Rule) (x uExt : Int) : Int :=
  (r.b0 * q16One - r.uGain * uExt) + r.b1 * x

theorem schmitt_deadband_holds (kExp : Nat) (cur : UInt8) (r : Rule) (xNext : Int)
    (hCap : (cur &&& UInt8.ofNat ((2 ^ (if kExp > 8 then 8 else kExp)) - 1)) = cur)
    (hBand : r.bExit ≤ xNext ∧ xNext ≤ r.bEnter) :
    schmittNext kExp cur r xNext = cur := by
  have h1 : ¬ (xNext < r.bExit) := by omega
  have h2 : ¬ (xNext > r.bEnter) := by omega
  simp [schmittNext, h1, h2, hCap]

theorem zero_ugain_isolates_shocks (r : Rule) (hZero : r.uGain = 0) (x u1 u2 : Int) :
    r.evalLinearResidual x u1 = r.evalLinearResidual x u2 := by
  simp [Rule.evalLinearResidual, hZero]

def stepHysteresisBool (prev : Bool) (x bEnter bExit : Int) : Bool :=
  if x ≤ bEnter then true
  else if bExit ≤ x then false
  else prev

theorem hysteresis_bool_no_chatter (prev : Bool) (x bEnter bExit : Int)
    (h1 : bEnter < x) (h2 : x < bExit) :
    stepHysteresisBool prev x bEnter bExit = prev := by
  unfold stepHysteresisBool
  split
  · omega
  · split
    · omega
    · rfl

end Flow
