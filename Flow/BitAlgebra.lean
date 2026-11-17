/-
  Flow.BitAlgebra — Machine-Word, Bit-Mask, SEARU, Kan-Lock & IEEE-754 Bit-Slice Algebra
  =======================================================================================
  Pure Lean 4 + CaDiCaL `bv_decide` machine-word verification (zero `sorryAx`).
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

theorem ring_rewind_bv32_64 (head stepsBack : BitVec 32) :
    BitVec.ult (((head &&& 63#32) + 64#32 - (stepsBack &&& 63#32)) &&& 63#32) 64#32 = true := by
  bv_decide

theorem subtable_slot4_inbounds_bv32 (idx : BitVec 32) :
    let slot := 250#32 + ((idx &&& 15#32) >>> 2#32)
    BitVec.ule 250#32 slot = true ∧ BitVec.ult slot 254#32 = true := by
  bv_decide

theorem slot_disjoint_hi2_bv32 (slotIdx : BitVec 32)
    (hBase : BitVec.ult slotIdx 254#32 = true) :
    (slotIdx &&& 255#32) != 254#32 ∧ (slotIdx &&& 255#32) != 255#32 := by
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

def bitfield5PackBV (f0 f1 f2 f3 f4 : BitVec 32) : BitVec 32 :=
  ((f0 &&& 0xF#32) <<< 0#32) |||
  ((f1 &&& 0xF#32) <<< 4#32) |||
  ((f2 &&& 0xF#32) <<< 8#32) |||
  ((f3 &&& 0x3F#32) <<< 12#32) |||
  ((f4 &&& 0xF#32) <<< 18#32)

theorem bitfield5_pack_roundtrip_bv32 (f0 f1 f2 f3 f4 : BitVec 32) :
    let m := bitfield5PackBV f0 f1 f2 f3 f4
    ((m >>> 0#32)  &&& 0xF#32)  = (f0 &&& 0xF#32) ∧
    ((m >>> 4#32)  &&& 0xF#32)  = (f1 &&& 0xF#32) ∧
    ((m >>> 8#32)  &&& 0xF#32)  = (f2 &&& 0xF#32) ∧
    ((m >>> 12#32) &&& 0x3F#32) = (f3 &&& 0x3F#32) ∧
    ((m >>> 18#32) &&& 0xF#32)  = (f4 &&& 0xF#32) := by
  unfold bitfield5PackBV
  bv_decide

theorem scaled_frac_u16_inbounds_bv32 (rawToi : BitVec 32) :
    let toi := clampS32BV rawToi 0#32 65536#32
    let idx := (toi * 800#32) >>> 16#32
    BitVec.ule idx 800#32 = true := by
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
    ieee754ToQ16 0x40E5ACC000000000 req errBit = (44390 * Flow.q16One, req) ∧
    ieee754SignedToQ16 0xC05E200000000000 req errBit = (-(120 * Flow.q16One + 32768), req) ∧
    (ieee754ToQ16 0x7FF8000000000000 req errBit).2 ≠ req ∧
    (ieee754ToQ16 0xC0E5ACC000000000 req errBit).2 ≠ req := by
  decide

/-- 7-bit little-endian ASCII 8-char symbol encoder into 56 bits. -/
def packAscii7x8 (s : String) : UInt64 :=
  s.toList.foldr (fun c acc => (acc <<< 7) ||| (c.toNat.toUInt64 &&& 0x7F)) 0

theorem packAscii7x8_exact :
    packAscii7x8 "ABC" = 0x000000000010E141 ∧
    packAscii7x8 "K01" = 0x00000000000C584B ∧
    packAscii7x8 "Z99" = 0x00000000000E5CDA := by
  decide

end Flow.BitAlgebra
