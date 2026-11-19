/-
  Flow.BitAlgebra — Pure Machine-Word, Bit-Mask, SEARU, Kan-Lock & IEEE-754 Algebra
  ===================================================================================
  Pure Lean 4 + CaDiCaL `bv_decide` machine-word verification (zero IO, zero `sorryAx`).
-/
import Flow.Core
import Std.Tactic.BVDecide

namespace Flow.BitAlgebra

theorem ring_mask_mod_inbounds (idx cap : Nat) (hPos : 0 < cap) :
    idx % cap < cap :=
  Nat.mod_lt idx hPos

theorem ring_mask_bv32_256 (idx : BitVec 32) :
    BitVec.ult (idx &&& 0xFF#32) 256#32 = true := by
  bv_decide

theorem ring_mask_bv32_64 (idx : BitVec 32) :
    BitVec.ult (idx &&& 63#32) 64#32 = true := by
  bv_decide

theorem ring_rewind_bv32_64 (head stepsBack : BitVec 32) :
    BitVec.ult (((head &&& 63#32) + 64#32 - (stepsBack &&& 63#32)) &&& 63#32) 64#32 = true := by
  bv_decide

def u32x2Decompose (bitIdx : Nat) : Nat × Nat :=
  (bitIdx / 32, bitIdx % 32)

def u32x2Reconstruct (pair : Nat × Nat) : Nat :=
  pair.1 * 32 + pair.2

theorem u32x2_bijection (bitIdx : Nat) (h : bitIdx < 64) :
    let p := u32x2Decompose bitIdx
    p.1 < 2 ∧ p.2 < 32 ∧ u32x2Reconstruct p = bitIdx := by
  unfold u32x2Decompose u32x2Reconstruct
  dsimp
  omega

def u32x2SetLoBV (lo32 bitIdx : BitVec 32) : BitVec 32 :=
  if BitVec.ult bitIdx 32#32 then lo32 ||| (1#32 <<< (bitIdx &&& 31#32)) else lo32

def u32x2SetHiBV (hi32 bitIdx : BitVec 32) : BitVec 32 :=
  if !BitVec.ult bitIdx 32#32 && BitVec.ult bitIdx 64#32 then hi32 ||| (1#32 <<< (bitIdx &&& 31#32)) else hi32

def u32x2TestBV (lo32 hi32 bitIdx : BitVec 32) : Bool :=
  let word  := if BitVec.ult bitIdx 32#32 then lo32 else hi32
  let shift := bitIdx &&& 31#32
  ((word >>> shift) &&& 1#32) != 0#32

theorem u32x2_set_test_exact_bv32 (lo32 hi32 bitIdx : BitVec 32)
    (h : BitVec.ult bitIdx 64#32 = true) :
    u32x2TestBV (u32x2SetLoBV lo32 bitIdx) (u32x2SetHiBV hi32 bitIdx) bitIdx = true := by
  unfold u32x2SetLoBV u32x2SetHiBV u32x2TestBV
  bv_decide

theorem u32x2_set_frame_independence_bv32 (lo32 hi32 i j : BitVec 32)
    (hi : BitVec.ult i 64#32 = true) (hj : BitVec.ult j 64#32 = true) (hne : i != j) :
    u32x2TestBV (u32x2SetLoBV lo32 i) (u32x2SetHiBV hi32 i) j = u32x2TestBV lo32 hi32 j := by
  unfold u32x2SetLoBV u32x2SetHiBV u32x2TestBV
  bv_decide

theorem kan_lock_project_bv16 (modeReq curMode kanLock : BitVec 16) :
    let nextMode := (modeReq &&& ~~~kanLock) ||| (curMode &&& kanLock)
    (nextMode &&& kanLock) = (curMode &&& kanLock) ∧
    (nextMode &&& ~~~kanLock) = (modeReq &&& ~~~kanLock) := by
  bv_decide

theorem kan_lock_project_bv64 (modeReq curMode kanLock : BitVec 64) :
    let nextMode := (modeReq &&& ~~~kanLock) ||| (curMode &&& kanLock)
    (nextMode &&& kanLock) = (curMode &&& kanLock) ∧
    (nextMode &&& ~~~kanLock) = (modeReq &&& ~~~kanLock) := by
  bv_decide

theorem flag_set_clear_idempotent_bv16 (flags bitMask : BitVec 16)
    (hClean : (flags &&& bitMask) = 0#16) :
    let ovr := flags ||| bitMask
    let rev := ovr &&& ~~~bitMask
    (ovr &&& bitMask) = bitMask ∧ rev = flags ∧ (rev &&& bitMask) = 0#16 := by
  bv_decide

def searuHealGateBV64 (status64 dt64 : BitVec 64) : Bool :=
  ((status64 &&& 1#64) == 0#64) && (dt64 == 0#64)

theorem searu_phase_gate_silence_bv64 (status64 dt64 : BitVec 64)
    (hSmooth : (status64 &&& 1#64) != 0#64 ∨ dt64 != 0#64) :
    searuHealGateBV64 status64 dt64 = false := by
  unfold searuHealGateBV64
  bv_decide

theorem searu_kan_pin_flip_invariant_bv64 (cur flip kanPin : BitVec 64) :
    let next := cur ^^^ (flip &&& ~~~kanPin)
    (next &&& kanPin) = (cur &&& kanPin) := by
  bv_decide

def clampS32BV (x lo hi : BitVec 32) : BitVec 32 :=
  if BitVec.slt hi x then hi
  else if BitVec.slt x lo then lo
  else x

theorem clamp_s32_bv32_bounded (x lo hi : BitVec 32) (hOrd : BitVec.sle lo hi = true) :
    let res := clampS32BV x lo hi
    BitVec.sle lo res = true ∧ BitVec.sle res hi = true := by
  unfold clampS32BV
  bv_decide

theorem sieve_mask_fault_blocks (mask req : UInt64) (hNe : (mask &&& req) ≠ req) :
    ((mask &&& req) == req) = false :=
  beq_false_of_ne hNe

/-- Pure integer IEEE-754 binary64 bit-slicer to unsigned Q16.16 (`0` floating-point instructions). -/
def ieee754ToQ16 (rawBits sv errBit : UInt64) : Int × UInt64 :=
  let sign   := rawBits >>> 63
  let expRaw := (rawBits >>> 52) &&& 0x7FF
  if sign != 0 || expRaw == 0 || expRaw == 0x7FF then
    (0, sv &&& (~~~errBit))
  else
    let exp : Int := Int.ofNat expRaw.toNat - 1023
    if exp > 30 || exp < -16 then
      (0, sv &&& (~~~errBit))
    else
      let mant  : Nat := ((rawBits &&& 0x000FFFFFFFFFFFFF) ||| 0x0010000000000000).toNat
      let shift : Nat := (36 - exp).toNat
      let half  : Nat := 1 <<< (shift - 1)
      (Int.ofNat ((mant + half) >>> shift), sv)

/-- Pure integer signed IEEE-754 binary64 bit-slicer to Q16.16 (`0` floating-point instructions). -/
def ieee754SignedToQ16 (rawBits sv errBit : UInt64) : Int × UInt64 :=
  let magBits := rawBits &&& 0x7FFFFFFFFFFFFFFF
  if magBits == 0 then
    (0, sv)
  else
    let (mag, svOut) := ieee754ToQ16 magBits sv errBit
    (if (rawBits >>> 63) != 0 then -mag else mag, svOut)

theorem ieee754_bit_extraction_exact_and_guarded :
    let req : UInt64 := 0x00000000000003FF
    let errBit : UInt64 := 0x0000000000000004
    ieee754ToQ16 0x3FF0000000000000 req errBit = (1 * Flow.q16One, req) ∧
    ieee754SignedToQ16 0xBFF8000000000000 req errBit = (-(Flow.q16One + 32768), req) ∧
    (ieee754ToQ16 0x7FF8000000000000 req errBit).2 ≠ req ∧
    (ieee754ToQ16 0xBFF0000000000000 req errBit).2 ≠ req := by
  decide

end Flow.BitAlgebra
