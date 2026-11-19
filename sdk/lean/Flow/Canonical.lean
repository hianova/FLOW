/-
  Flow.Canonical — Single-Source Canonical Algebraic Morphisms (`FLOW/sdk`)
  =========================================================================
  Domain-free `.linear` GF(2^8) & 5-bit bijection reference graphs certified
  for universal projection across scalar, lattice, and unitary backends.
-/
import Flow.Morphism

namespace Flow.Canonical

set_option maxRecDepth 8192

def gf256SboxMac : Flow.Morphism.Graph :=
  let b := Flow.Morphism.Builder.init "gf256_sbox_mac" .linear true
  let (b, a)     := b.input "a" 8
  let (b, bv)    := b.input "b" 8
  let (b, c)     := b.input "c" 8
  let (b, sel)   := b.input "sel" 1
  let (b, cst63) := b.constU64 "cst63" 8 0x63
  let (b, invA)  := b.gf2mInv "inv_a" 8 a 0x11B
  let (b, prod)  := b.gf2mMul "prod" 8 invA bv 0x11B
  let (b, aff)   := b.xorVec "aff" 8 prod cst63
  let (b, sum)   := b.add "sum" 8 c aff
  let (b, y)     := b.ctrlSel "y" 8 sel prod sum
  b.build [y]

def gf256TowerSpec : Flow.Morphism.Graph :=
  let (b, a)   := (Flow.Morphism.Builder.init "gf256_tower_spec" .linear true).input "a" 8
  let (b, inv) := b.gf2mQuadInv "q" 4 a 0x13 0x8
  let (b, y)   := b.slice "y" 8 inv 0
  b.build [y]

def lut5BijectionSpec : Flow.Morphism.Graph :=
  let (b, x) := (Flow.Morphism.Builder.init "lut5_bijection_spec" .linear true).input "x" 5
  let tbl : List UInt64 := [
    0x04,0x0b,0x1f,0x14,0x1a,0x15,0x09,0x02,0x1b,0x05,0x08,0x12,0x1d,0x03,0x06,0x1c,
    0x1e,0x13,0x07,0x0e,0x00,0x0d,0x11,0x18,0x10,0x0c,0x01,0x19,0x16,0x0a,0x0f,0x17 ]
  let (b, y) := b.op .lut_rom "y" 5 [x] (lutWords := tbl)
  b.build [y]

theorem gf256SboxMac_universal :
    Flow.Morphism.admitsTarget gf256SboxMac .scalar = true ∧
    Flow.Morphism.admitsTarget gf256SboxMac .lattice = true ∧
    Flow.Morphism.admitsTarget gf256SboxMac .unitary = true := by
  decide

theorem gf256TowerSpec_universal :
    Flow.Morphism.admitsTarget gf256TowerSpec .scalar = true ∧
    Flow.Morphism.admitsTarget gf256TowerSpec .lattice = true ∧
    Flow.Morphism.admitsTarget gf256TowerSpec .unitary = true := by
  decide

theorem lut5BijectionSpec_universal :
    Flow.Morphism.admitsTarget lut5BijectionSpec .scalar = true ∧
    Flow.Morphism.admitsTarget lut5BijectionSpec .lattice = true ∧
    Flow.Morphism.admitsTarget lut5BijectionSpec .unitary = true := by
  decide

end Flow.Canonical
