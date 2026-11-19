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

def OpKind.isAffineGF2 : OpKind → Bool
  | .const_u64 => true
  | .slice     => true
  | .concat    => true
  | .xor_vec   => true
  | .not_vec   => true
  | _          => false

def OpKind.isBooleanDecomposed : OpKind → Bool
  | .const_u64 => true
  | .slice     => true
  | .concat    => true
  | .xor_vec   => true
  | .not_vec   => true
  | .and_vec   => true
  | _          => false

theorem decomposed_op_linear_admissible (k : OpKind)
    (h : k.isBooleanDecomposed = true) :
    k.isLinearAdmissible = true := by
  cases k <;> simp [OpKind.isBooleanDecomposed] at h <;> rfl

theorem decomposed_non_affine_is_and (k : OpKind)
    (hDec : k.isBooleanDecomposed = true) (hNonAff : k.isAffineGF2 = false) :
    k = .and_vec := by
  cases k <;> simp [OpKind.isBooleanDecomposed, OpKind.isAffineGF2] at hDec hNonAff <;> rfl

def maxVals : Nat := 512
def maxOps  : Nat := 512

def ceilLog2 (n : Nat) : Nat :=
  Id.run do
    let mut lg : Nat := 0
    let mut span : Nat := 1
    for _ in [:64] do
      if span < n then
        span := span * 2
        lg := lg + 1
    return lg

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
  (nVals ≤ maxVals) &&
  (g.ops.length ≤ maxOps) &&
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

/-! ## Pure F2-Affine Classification & Multiplicative-Complexity Analysis -/

def opMultBits (vals : List ValSpec) (op : OpSpec) : Nat :=
  if op.kind.isAffineGF2 then 0
  else
    let ow := match vals[op.outVal]? with | some v => v.width | none => 0
    let iw0 := match op.ins[0]? >>= (fun iv => vals[iv]?) with | some v => v.width | none => 0
    match op.kind with
    | .and_vec   => ow
    | .ctrl_sel  => ow
    | .or_reduce => if iw0 ≤ 1 then 0 else iw0 - 1
    | .eq_zero   => if iw0 ≤ 1 then 0 else iw0 - 1
    | .add       => if ow ≤ 1 then 0 else ow - 1
    | .sub       => if ow ≤ 1 then 0 else ow - 1
    | .gf2m_mul  => op.p0.toNat * op.p0.toNat
    | .gf2m_inv  => (if op.p0.toNat > 1 then op.p0.toNat - 1 else 1) * op.p0.toNat * op.p0.toNat
    | _          => ow * (if iw0 == 0 then 1 else iw0)

def opStageDelta (vals : List ValSpec) (op : OpSpec) : Nat :=
  if op.kind.isAffineGF2 then 0
  else
    match op.kind with
    | .and_vec   => 1
    | .ctrl_sel  => 1
    | .gf2m_mul  => 1
    | .or_reduce =>
      let iw0 := match op.ins[0]? >>= (fun iv => vals[iv]?) with | some v => v.width | none => 1
      let lg := ceilLog2 iw0
      if lg == 0 then 1 else lg
    | .eq_zero =>
      let iw0 := match op.ins[0]? >>= (fun iv => vals[iv]?) with | some v => v.width | none => 1
      let lg := ceilLog2 iw0
      if lg == 0 then 1 else lg
    | .gf2m_inv => if op.p0.toNat > 1 then op.p0.toNat - 1 else 1
    | _ =>
      let ow := match vals[op.outVal]? with | some v => v.width | none => 1
      if ow == 0 then 1 else ow

def Graph.affineOpCount (g : Graph) : Nat :=
  (g.ops.filter (fun op => op.kind.isAffineGF2)).length

def Graph.nonAffineOpCount (g : Graph) : Nat :=
  (g.ops.filter (fun op => !op.kind.isAffineGF2)).length

private def sumMultBits (vals : List ValSpec) : List OpSpec → Nat
  | [] => 0
  | op :: rest => opMultBits vals op + sumMultBits vals rest

def Graph.multBits (g : Graph) : Nat :=
  sumMultBits g.vals g.ops

private def lookupDepth : List (Nat × Nat) → Nat → Nat
  | [], _ => 0
  | (k, v) :: rest, q => if k == q then v else lookupDepth rest q

private def maxInputDepth (dm : List (Nat × Nat)) : List Nat → Nat
  | [] => 0
  | iv :: rest => Nat.max (lookupDepth dm iv) (maxInputDepth dm rest)

private def maxRecordedDepth : List (Nat × Nat) → Nat
  | [] => 0
  | (_, d) :: rest => Nat.max d (maxRecordedDepth rest)

private def calcDepths (vals : List ValSpec) : List OpSpec → List (Nat × Nat) → Nat
  | [], dm => maxRecordedDepth dm
  | op :: rest, dm =>
    let dIn := maxInputDepth dm op.ins
    let dOut := dIn + opStageDelta vals op
    calcDepths vals rest ((op.outVal, dOut) :: dm)

def Graph.multDepth (g : Graph) : Nat :=
  calcDepths g.vals g.ops []

theorem op_affine_zero_delta (vals : List ValSpec) (op : OpSpec)
    (h : op.kind.isAffineGF2 = true) :
    opMultBits vals op = 0 ∧ opStageDelta vals op = 0 := by
  simp [opMultBits, opStageDelta, h]

private theorem sumMultBits_zero (vals : List ValSpec) :
    ∀ ops, ops.all (fun op => op.kind.isAffineGF2) = true → sumMultBits vals ops = 0
  | [], _ => rfl
  | op :: rest, h => by
    simp only [List.all_cons, Bool.and_eq_true] at h
    have hz := (op_affine_zero_delta vals op h.1).1
    simp [sumMultBits, hz, sumMultBits_zero vals rest h.2]

theorem all_affine_zero_multBits (g : Graph)
    (h : g.ops.all (fun op => op.kind.isAffineGF2) = true) :
    g.multBits = 0 :=
  sumMultBits_zero g.vals g.ops h

private theorem lookupDepth_zero :
    ∀ dm q, (∀ x ∈ dm, x.2 = 0) → lookupDepth dm q = 0
  | [], _, _ => rfl
  | (k, v) :: rest, q, h => by
    have hv : v = 0 := h (k, v) (List.mem_cons_self)
    have hrest : ∀ x ∈ rest, x.2 = 0 := fun x hx => h x (List.mem_cons_of_mem _ hx)
    simp only [lookupDepth]
    split
    · exact hv
    · exact lookupDepth_zero rest q hrest

private theorem maxInputDepth_zero (dm : List (Nat × Nat)) (h : ∀ x ∈ dm, x.2 = 0) :
    ∀ ins, maxInputDepth dm ins = 0
  | [] => rfl
  | iv :: rest => by
    simp [maxInputDepth, lookupDepth_zero dm iv h, maxInputDepth_zero dm h rest]

private theorem maxRecordedDepth_zero :
    ∀ dm, (∀ x ∈ dm, x.2 = 0) → maxRecordedDepth dm = 0
  | [], _ => rfl
  | (k, d) :: rest, h => by
    have hd : d = 0 := h (k, d) (List.mem_cons_self)
    have hrest : ∀ x ∈ rest, x.2 = 0 := fun x hx => h x (List.mem_cons_of_mem _ hx)
    simp [maxRecordedDepth, hd, maxRecordedDepth_zero rest hrest]

private theorem calcDepths_zero (vals : List ValSpec) :
    ∀ ops dm, ops.all (fun op => op.kind.isAffineGF2) = true →
      (∀ x ∈ dm, x.2 = 0) → calcDepths vals ops dm = 0
  | [], dm, _, hdm => maxRecordedDepth_zero dm hdm
  | op :: rest, dm, hops, hdm => by
    simp only [List.all_cons, Bool.and_eq_true] at hops
    have hd : opStageDelta vals op = 0 := (op_affine_zero_delta vals op hops.1).2
    have hIn : maxInputDepth dm op.ins = 0 := maxInputDepth_zero dm hdm op.ins
    have hdm' : ∀ x ∈ ((op.outVal, maxInputDepth dm op.ins + opStageDelta vals op) :: dm), x.2 = 0 := by
      intro x hx
      cases hx with
      | head => simp [hIn, hd]
      | tail _ hmem => exact hdm x hmem
    simpa [calcDepths] using calcDepths_zero vals rest _ hops.2 hdm'

theorem all_affine_zero_multDepth (g : Graph)
    (h : g.ops.all (fun op => op.kind.isAffineGF2) = true) :
    g.multDepth = 0 :=
  calcDepths_zero g.vals g.ops [] h (fun _ hx => nomatch hx)

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

private def takeDrop6 : List (ValId × Nat) → List (ValId × Nat) × List (ValId × Nat)
  | a :: b :: c :: d :: e :: f :: rest => ([a, b, c, d, e, f], rest)
  | xs => (xs, [])

private def group6 : Nat → List (ValId × Nat) → List (List (ValId × Nat))
  | 0, _ => []
  | _ + 1, [] => []
  | fuel + 1, xs =>
    let (hd, tl) := takeDrop6 xs
    hd :: group6 fuel tl

private def packRoundChunks (b : Builder) (pfx : String) (round idx : Nat) :
    List (List (ValId × Nat)) → Builder × List (ValId × Nat)
  | [] => (b, [])
  | ch :: rest =>
    match ch with
    | [] => packRoundChunks b pfx round idx rest
    | [(v, w)] =>
      let (b', tail) := packRoundChunks b pfx round (idx + 1) rest
      (b', (v, w) :: tail)
    | _ =>
      let wSum := ch.foldl (fun s x => s + x.2) 0
      let (b1, vc) := b.concat s!"{pfx}_c{round}_{idx}" wSum (ch.map Prod.fst)
      let (b2, tail) := packRoundChunks b1 pfx round (idx + 1) rest
      (b2, (vc, wSum) :: tail)

private def packChunks6 : Nat → Builder → String → Nat → List (ValId × Nat) → Builder × ValId
  | 0, b, _, _, items =>
    match items with
    | (v, _) :: _ => (b, v)
    | [] => (b, 0)
  | _ + 1, b, _, _, [] => (b, 0)
  | _ + 1, b, _, _, [(v, _)] => (b, v)
  | fuel + 1, b, pfx, round, items =>
    let groups := group6 items.length items
    let (b', packed) := packRoundChunks b pfx round 0 groups
    packChunks6 fuel b' pfx (round + 1) packed

private def sliceBits (b : Builder) (pfx : String) (src : ValId) (idx : Nat) :
    Nat → Builder × List ValId
  | 0 => (b, [])
  | rem + 1 =>
    let (b1, bitId) := b.slice s!"{pfx}_b{idx}" 1 src (UInt64.ofNat idx)
    let (b2, tail) := sliceBits b1 pfx src (idx + 1) rem
    (b2, bitId :: tail)

private def selectMaskedBits (mask : UInt64) (i : Nat) : List ValId → List ValId
  | [] => []
  | v :: rest =>
    let tail := selectMaskedBits mask (i + 1) rest
    if ((mask >>> UInt64.ofNat i) &&& 1) == 1 then v :: tail else tail

private def xorBitsChain (b : Builder) (pfx : String) (r k : Nat) (acc : ValId) :
    List ValId → Builder × ValId
  | [] => (b, acc)
  | v :: rest =>
    let (b', nxt) := b.xorVec s!"{pfx}_r{r}_{k}" 1 acc v
    xorBitsChain b' pfx r (k + 1) nxt rest

private def buildLinRows (b : Builder) (pfx : String) (bits : List ValId) (r : Nat) :
    List UInt64 → Builder × List (ValId × Nat)
  | [] => (b, [])
  | mask :: rest =>
    let (b1, rVal) :=
      match selectMaskedBits mask 0 bits with
      | [] => b.constU64 s!"{pfx}_r{r}_z" 1 0
      | v0 :: vs => xorBitsChain b pfx r 0 v0 vs
    let (b2, tail) := buildLinRows b1 pfx bits (r + 1) rest
    (b2, (rVal, 1) :: tail)

def Builder.linMapGF2 (b : Builder) (pfx : String) (src : ValId) (inWidth : Nat)
    (rowMasks : List UInt64) : Builder × ValId :=
  if rowMasks.isEmpty then
    b.constU64 s!"{pfx}_empty" 1 0
  else
    let (b1, bits) := sliceBits b pfx src 0 inWidth
    let (b2, rows) := buildLinRows b1 pfx bits 0 rowMasks
    packChunks6 (rows.length + 1) b2 pfx 0 rows

private def gf2mMulRec : Nat → UInt64 → UInt64 → UInt64 → UInt64 → UInt64 → UInt64
  | 0, _, _, top, _, res => res &&& (top - 1)
  | rem + 1, a, b, top, irred, res =>
    let res' := if (b &&& 1) != 0 then res ^^^ a else res
    let aSh := a <<< 1
    let a' := if (aSh &&& top) != 0 then aSh ^^^ irred else aSh
    gf2mMulRec rem a' (b >>> 1) top irred res'

private def gf2mMulPure (deg : Nat) (a b poly : UInt64) : UInt64 :=
  let top : UInt64 := (1 : UInt64) <<< UInt64.ofNat deg
  let mask := top - 1
  gf2mMulRec deg (a &&& mask) (b &&& mask) top poly 0

private def quadNormCol (m c : Nat) (basePoly lam : UInt64) : UInt64 :=
  if c < m then
    let v : UInt64 := (1 : UInt64) <<< UInt64.ofNat c
    gf2mMulPure m v v basePoly
  else
    let w : UInt64 := (1 : UInt64) <<< UInt64.ofNat (c - m)
    let w2 := gf2mMulPure m w w basePoly
    gf2mMulPure m lam w2 basePoly

private def quadNormRowMaskLoop (m r : Nat) (basePoly lam : UInt64) (c : Nat) :
    Nat → UInt64 → UInt64
  | 0, acc => acc
  | rem + 1, acc =>
    let colVal := quadNormCol m c basePoly lam
    let bitSet := ((colVal >>> UInt64.ofNat r) &&& 1) == 1
    let acc' := if bitSet then acc ||| ((1 : UInt64) <<< UInt64.ofNat c) else acc
    quadNormRowMaskLoop m r basePoly lam (c + 1) rem acc'

private def quadNormRowsLoop (m : Nat) (basePoly lam : UInt64) (r : Nat) :
    Nat → List UInt64
  | 0 => []
  | rem + 1 =>
    let rowMask := quadNormRowMaskLoop m r basePoly lam 0 (2 * m) 0
    rowMask :: quadNormRowsLoop m basePoly lam (r + 1) rem

private def quadNormRows (m : Nat) (basePoly lam : UInt64) : List UInt64 :=
  quadNormRowsLoop m basePoly lam 0 m

def Builder.gf2mQuadInv (b : Builder) (pfx : String) (m : Nat) (src : ValId)
    (basePoly lam : UInt64) : Builder × ValId :=
  let (b1, x0)    := b.slice s!"{pfx}_x0" m src 0
  let (b2, x1)    := b1.slice s!"{pfx}_x1" m src (UInt64.ofNat m)
  let (b3, linSq) := b2.linMapGF2 s!"{pfx}_lsq" src (2 * m) (quadNormRows m basePoly lam)
  let (b4, p01)   := b3.gf2mMul s!"{pfx}_p01" m x0 x1 basePoly
  let (b5, delta) := b4.xorVec s!"{pfx}_del" m p01 linSq
  let (b6, d)     := b5.gf2mInv s!"{pfx}_inv" m delta basePoly
  let (b7, x01)   := b6.xorVec s!"{pfx}_x01" m x0 x1
  let (b8, y0)    := b7.gf2mMul s!"{pfx}_y0" m x01 d basePoly
  let (b9, y1)    := b8.gf2mMul s!"{pfx}_y1" m x1 d basePoly
  b9.concat s!"{pfx}_out" (2 * m) [y0, y1]

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
