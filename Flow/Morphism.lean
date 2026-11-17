/-
  Flow.Morphism — Universal QTT Resource-Typed SSA Circuit IR (`.fir`)
  ======================================================================
  Domain-free QTT linear/cartesian SSA graph specification, ergonomic `Builder`,
  projection admissibility theorems, and binary `"FIR1"` encoder (`emitFir`).
-/
import Flow.Core

namespace Flow.Morphism

inductive Multiplicity where
  | linear
  | cartesian
  deriving Repr, DecidableEq, Inhabited

inductive Target where
  | scalar
  | lattice
  | unitary
  deriving Repr, DecidableEq, Inhabited

inductive OpKind where
  | const_u64
  | slice
  | concat
  | xor_vec
  | and_vec
  | not_vec
  | or_reduce
  | add
  | sub
  | mul_fxp
  | sqr_fxp
  | gf2m_mul
  | gf2m_inv
  | lut_rom
  | ctrl_sel
  | shl_var
  | shr_var
  | eq_zero
  | stage_pipe
  deriving Repr, DecidableEq, Inhabited

def OpKind.code : OpKind → UInt16
  | .const_u64  => 1
  | .slice      => 2
  | .concat     => 3
  | .xor_vec    => 4
  | .and_vec    => 5
  | .not_vec    => 6
  | .or_reduce  => 7
  | .add        => 8
  | .sub        => 9
  | .mul_fxp    => 10
  | .sqr_fxp    => 11
  | .gf2m_mul   => 12
  | .gf2m_inv   => 13
  | .lut_rom    => 14
  | .ctrl_sel   => 15
  | .shl_var    => 16
  | .shr_var    => 17
  | .eq_zero    => 18
  | .stage_pipe => 19

def OpKind.isLinearAdmissible : OpKind → Bool
  | .stage_pipe => false
  | _           => true

structure ValSpec where
  name        : String
  width       : Nat
  isClassical : Bool := false
  deriving Repr, DecidableEq, Inhabited

structure OpSpec where
  kind     : OpKind
  outVal   : Nat
  ins      : List Nat
  p0       : UInt64 := 0
  p1       : UInt64 := 0
  p2       : UInt64 := 0
  p3       : UInt64 := 0
  lutWords : List UInt64 := []
  deriving Repr, DecidableEq, Inhabited

structure Graph where
  name     : String
  mult     : Multiplicity
  cleanAnc : Bool := true
  vals     : List ValSpec
  inputs   : List Nat
  outputs  : List Nat
  ops      : List OpSpec
  deriving Repr, DecidableEq, Inhabited

private def checkOps (mult : Multiplicity) (base : Nat) : Nat → List OpSpec → Bool
  | _, [] => true
  | idx, op :: rest =>
    let expectedOut := base + idx
    let arityOk :=
      match op.kind with
      | .const_u64  => op.ins.length == 0
      | .slice      => op.ins.length == 1
      | .concat     => op.ins.length ≥ 1 && op.ins.length ≤ 6
      | .xor_vec    => op.ins.length == 2
      | .and_vec    => op.ins.length == 2
      | .not_vec    => op.ins.length == 1
      | .or_reduce  => op.ins.length == 1
      | .add        => op.ins.length == 2
      | .sub        => op.ins.length == 2
      | .mul_fxp    => op.ins.length == 2
      | .sqr_fxp    => op.ins.length == 1
      | .gf2m_mul   => op.ins.length == 2 && op.p0.toNat ≥ 1 && op.p0.toNat ≤ 32
      | .gf2m_inv   => (op.ins.length == 1 || op.ins.length == 2) && op.p0.toNat ≥ 1 && op.p0.toNat ≤ 32
      | .lut_rom    => op.ins.length == 1 && !op.lutWords.isEmpty
      | .ctrl_sel   => op.ins.length == 3
      | .shl_var    => op.ins.length == 2
      | .shr_var    => op.ins.length == 2
      | .eq_zero    => op.ins.length == 1
      | .stage_pipe => op.ins.length ≤ 6
    let multOk := match mult with
      | .cartesian => true
      | .linear    => op.kind.isLinearAdmissible
    (op.outVal == expectedOut) &&
    (op.ins.length ≤ 6) &&
    (op.ins.all (fun iv => iv < op.outVal)) &&
    arityOk &&
    multOk &&
    checkOps mult base (idx + 1) rest

def Graph.wf (g : Graph) : Bool :=
  let nIn := g.inputs.length
  let nOut := g.outputs.length
  let nVals := g.vals.length
  let maxW := match g.mult with | .linear => 256 | .cartesian => 512
  (g.name != "") &&
  (nIn > 0 && nIn ≤ 32) &&
  (nOut > 0 && nOut ≤ 32) &&
  (nVals ≤ 128) &&
  (g.ops.length ≤ 128) &&
  (nIn + g.ops.length == nVals) &&
  (g.inputs == List.range nIn) &&
  (g.outputs.all (fun ov => ov < nVals)) &&
  (g.vals.all (fun v => 1 ≤ v.width && v.width ≤ maxW)) &&
  checkOps g.mult nIn 0 g.ops

def admitsTarget (g : Graph) (t : Target) : Bool :=
  match t with
  | .scalar  => g.wf
  | .lattice => g.wf
  | .unitary => g.wf && (g.mult == .linear)

theorem linear_graph_universal_projection (g : Graph)
    (hWf : g.wf = true) (hLin : g.mult = .linear) :
    admitsTarget g .scalar = true ∧
    admitsTarget g .lattice = true ∧
    admitsTarget g .unitary = true := by
  simp [admitsTarget, hWf, hLin]

theorem cartesian_blocks_unitary (g : Graph) (hCart : g.mult = .cartesian) :
    admitsTarget g .unitary = false := by
  simp [admitsTarget, hCart]

/-! ## Ergonomic Functional SSA Graph Builder -/

abbrev ValId := Nat

structure Builder where
  name     : String
  mult     : Multiplicity := .linear
  cleanAnc : Bool := true
  vals     : Array ValSpec := #[]
  inputs   : Array Nat := #[]
  ops      : Array OpSpec := #[]
  deriving Repr, Inhabited

def Builder.init (name : String) (mult : Multiplicity := .linear) (cleanAnc : Bool := true) : Builder :=
  { name := name, mult := mult, cleanAnc := cleanAnc }

def Builder.input (b : Builder) (name : String) (width : Nat) (isClassical : Bool := false) :
    Builder × ValId :=
  if !b.ops.isEmpty then (b, 0)
  else
    let id := b.vals.size
    ({ b with vals := b.vals.push { name := name, width := width, isClassical := isClassical },
              inputs := b.inputs.push id }, id)

def Builder.op (b : Builder) (kind : OpKind) (name : String) (width : Nat) (ins : List ValId)
    (p0 p1 p2 p3 : UInt64 := 0) (lutWords : List UInt64 := []) (isClassical : Bool := false) :
    Builder × ValId :=
  let id := b.vals.size
  let b' := { b with
    vals := b.vals.push { name := name, width := width, isClassical := isClassical },
    ops  := b.ops.push { kind := kind, outVal := id, ins := ins, p0 := p0, p1 := p1, p2 := p2,
                         p3 := p3, lutWords := lutWords } }
  (b', id)

def Builder.constU64 (b : Builder) (name : String) (width : Nat) (val : UInt64) : Builder × ValId :=
  b.op .const_u64 name width [] val

def Builder.slice (b : Builder) (name : String) (width : Nat) (src : ValId) (lo : UInt64) : Builder × ValId :=
  b.op .slice name width [src] lo

def Builder.concat (b : Builder) (name : String) (width : Nat) (srcs : List ValId) : Builder × ValId :=
  b.op .concat name width srcs

def Builder.xorVec (b : Builder) (name : String) (width : Nat) (x y : ValId) : Builder × ValId :=
  b.op .xor_vec name width [x, y]

def Builder.andVec (b : Builder) (name : String) (width : Nat) (x y : ValId) : Builder × ValId :=
  b.op .and_vec name width [x, y]

def Builder.notVec (b : Builder) (name : String) (width : Nat) (x : ValId) : Builder × ValId :=
  b.op .not_vec name width [x]

def Builder.orReduce (b : Builder) (name : String) (x : ValId) : Builder × ValId :=
  b.op .or_reduce name 1 [x]

def Builder.add (b : Builder) (name : String) (width : Nat) (x y : ValId) : Builder × ValId :=
  b.op .add name width [x, y]

def Builder.sub (b : Builder) (name : String) (width : Nat) (x y : ValId) : Builder × ValId :=
  b.op .sub name width [x, y]

def Builder.mulFxp (b : Builder) (name : String) (width : Nat) (x y : ValId)
    (fracA fracB fracOut : UInt64) : Builder × ValId :=
  b.op .mul_fxp name width [x, y] fracA fracB fracOut

def Builder.sqrFxp (b : Builder) (name : String) (width : Nat) (x : ValId)
    (fracA fracB fracOut : UInt64) : Builder × ValId :=
  b.op .sqr_fxp name width [x] fracA fracB fracOut

def Builder.gf2mMul (b : Builder) (name : String) (deg : Nat) (x y : ValId) (poly : UInt64) : Builder × ValId :=
  b.op .gf2m_mul name deg [x, y] (UInt64.ofNat deg) poly

def Builder.gf2mInv (b : Builder) (name : String) (deg : Nat) (x : ValId) (poly : UInt64) : Builder × ValId :=
  b.op .gf2m_inv name deg [x] (UInt64.ofNat deg) poly

def Builder.ctrlSel (b : Builder) (name : String) (width : Nat) (sel vf vt : ValId) : Builder × ValId :=
  b.op .ctrl_sel name width [sel, vf, vt]

def Builder.shlVar (b : Builder) (name : String) (width : Nat) (x sh : ValId) : Builder × ValId :=
  b.op .shl_var name width [x, sh]

def Builder.shrVar (b : Builder) (name : String) (width : Nat) (x sh : ValId) : Builder × ValId :=
  b.op .shr_var name width [x, sh]

def Builder.eqZero (b : Builder) (name : String) (x : ValId) : Builder × ValId :=
  b.op .eq_zero name 1 [x]

def Builder.stagePipe (b : Builder) (name : String) (width : Nat) (ins : List ValId)
    (p0 p1 p2 p3 : UInt64 := 0) : Builder × ValId :=
  b.op .stage_pipe name width ins p0 p1 p2 p3

def Builder.build (b : Builder) (outputs : List ValId) : Graph :=
  { name     := b.name
    mult     := b.mult
    cleanAnc := b.cleanAnc
    vals     := b.vals.toList
    inputs   := b.inputs.toList
    outputs  := outputs
    ops      := b.ops.toList }

/-! ## Binary `"FIR1"` Encoder -/

def firMagic : UInt32 := 0x31524946

private def pushU32ListAligned8 (buf : ByteArray) (xs : List Nat) : ByteArray :=
  Id.run do
    let mut out := buf
    for x in xs do
      out := pushLE32 out (UInt32.ofNat x)
    if xs.length % 2 == 1 then
      out := pushLE32 out 0
    return out

def encodeFir (g : Graph) : Option ByteArray := do
  if !g.wf then none
  let lutTotal : Nat := g.ops.foldl (fun acc op => acc + op.lutWords.length) 0
  if lutTotal > 4096 then
    none
  let multCode : UInt32 := match g.mult with | .linear => 1 | .cartesian => 2
  let flags : UInt32 := if g.cleanAnc then 1 else 0
  let mut buf := ByteArray.empty
  buf := pushLE32 buf firMagic
  buf := pushLE32 buf 1
  buf := pushLE32 buf multCode
  buf := pushLE32 buf flags
  buf := pushLE32 buf (UInt32.ofNat g.vals.length)
  buf := pushLE32 buf (UInt32.ofNat g.inputs.length)
  buf := pushLE32 buf (UInt32.ofNat g.outputs.length)
  buf := pushLE32 buf (UInt32.ofNat g.ops.length)
  buf := pushLE32 buf (UInt32.ofNat lutTotal)
  buf := pushLE32 buf 0
  buf := pushFixedAscii buf g.name 24
  for v in g.vals do
    buf := pushLE16 buf (UInt16.ofNat v.width)
    buf := buf.push (if v.isClassical then 1 else 0)
    buf := buf.push 0 |>.push 0 |>.push 0 |>.push 0 |>.push 0
    buf := pushFixedAscii buf v.name 24
  buf := pushU32ListAligned8 buf g.inputs
  buf := pushU32ListAligned8 buf g.outputs
  let mut lutOff : Nat := 0
  let mut lutPool : Array UInt64 := #[]
  for op in g.ops do
    buf := pushLE16 buf op.kind.code
    buf := pushLE16 buf (UInt16.ofNat op.ins.length)
    buf := pushLE32 buf (UInt32.ofNat op.outVal)
    for k in [:6] do
      let iv := match op.ins[k]? with | some x => x | none => 0
      buf := pushLE32 buf (UInt32.ofNat iv)
    buf := pushLE32 buf (UInt32.ofNat lutOff)
    buf := pushLE32 buf (UInt32.ofNat op.lutWords.length)
    buf := pushLE64 buf op.p0
    buf := pushLE64 buf op.p1
    buf := pushLE64 buf op.p2
    buf := pushLE64 buf op.p3
    lutOff := lutOff + op.lutWords.length
    for lw in op.lutWords do
      lutPool := lutPool.push lw
  for lw in lutPool do
    buf := pushLE64 buf lw
  let h := fnv1a64Bytes buf
  buf := pushLE64 buf h
  return buf

def emitFir (path : System.FilePath) (g : Graph) : IO Bool := do
  match encodeFir g with
  | none => return false
  | some bytes =>
    IO.FS.writeBinFile path bytes
    return true

def emitFirChecked (path : System.FilePath) (g : Graph) (_h : g.wf = true := by decide) : IO Bool :=
  emitFir path g

end Flow.Morphism
