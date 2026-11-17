/-
  Flow.Schema — Full-512-Bit Zero-Waste Cell & Multi-Slot Arena Schema Co-Generator
  ================================================================================
  Part A: Arbitrary 512-bit naturally aligned `CellLayout64` + disjoint `ArenaSpec`,
          auto-emitting zero-UB C17/C++20 `.h` codecs and zero-64b-int `.wgsl` accessors.
  Part B: Rule-compatible `.flow` bundle `Spec` (`w0, w1, w2, w7` metadata fields).
-/
import Flow.Core

namespace Flow.Schema

/-! ## Part A: Full-512-Bit Zero-Waste CellLayout64 & ArenaSpec -/

inductive ScalarType where
  | cchar
  | u8
  | u16
  | i16
  | u32
  | i32
  | u64
  | i64
  deriving Repr, DecidableEq, Inhabited

def ScalarType.bitWidth : ScalarType → Nat
  | .cchar => 8
  | .u8    => 8
  | .u16   => 16
  | .i16   => 16
  | .u32   => 32
  | .i32   => 32
  | .u64   => 64
  | .i64   => 64

def ScalarType.cType : ScalarType → String
  | .cchar => "char"
  | .u8    => "uint8_t"
  | .u16   => "uint16_t"
  | .i16   => "int16_t"
  | .u32   => "uint32_t"
  | .i32   => "int32_t"
  | .u64   => "uint64_t"
  | .i64   => "int64_t"

def ScalarType.maskExpr64 : ScalarType → String
  | .cchar => "UINT64_C(0xFF)"
  | .u8    => "UINT64_C(0xFF)"
  | .u16   => "UINT64_C(0xFFFF)"
  | .i16   => "UINT64_C(0xFFFF)"
  | .u32   => "UINT64_C(0xFFFFFFFF)"
  | .i32   => "UINT64_C(0xFFFFFFFF)"
  | .u64   => "UINT64_MAX"
  | .i64   => "UINT64_MAX"

structure BitSliceField where
  name      : String
  bitOffset : Nat
  ty        : ScalarType
  count     : Nat := 1
  deriving Repr, DecidableEq, Inhabited

def BitSliceField.totalBits (f : BitSliceField) : Nat :=
  f.ty.bitWidth * f.count

structure CellLayout64 where
  name        : String
  cTypeName   : String
  fields      : List BitSliceField
  aligned64   : Bool := false
  unionMember : String := ""
  deriving Repr, DecidableEq, Inhabited

structure SlotRegion where
  name        : String
  baseSlot    : Nat
  count       : Nat
  cSlotMacro  : String := ""
  cCountMacro : String := ""
  mapDecl     : String := ""
  deriving Repr, DecidableEq, Inhabited

structure ArenaSpec where
  cPrefix         : String
  headerGuard     : String
  totalCells      : Nat
  regions         : List SlotRegion
  layouts         : List CellLayout64
  cCellUnionName  : String := ""
  cArenaMapName   : String := ""
  cArenaUnionName : String := ""
  deriving Repr, DecidableEq, Inhabited

def noDupNames : List String → Bool
  | [] => true
  | x :: xs => !(xs.contains x) && noDupNames xs

private def disjointBitFieldPair (a b : BitSliceField) : Bool :=
  (a.bitOffset + a.totalBits ≤ b.bitOffset) || (b.bitOffset + b.totalBits ≤ a.bitOffset)

private def allBitFieldsDisjoint : List BitSliceField → Bool
  | [] => true
  | f :: rest => rest.all (disjointBitFieldPair f) && allBitFieldsDisjoint rest

private def checkContiguous512 : Nat → List BitSliceField → Bool
  | pos, [] => pos == 512
  | pos, f :: fs => (f.bitOffset == pos) && checkContiguous512 (pos + f.totalBits) fs

def CellLayout64.wf (L : CellLayout64) : Bool :=
  (!L.fields.isEmpty) &&
  (L.name != "") &&
  (L.cTypeName != "") &&
  noDupNames (L.fields.map (fun f => f.name)) &&
  L.fields.all (fun f =>
    (f.name != "") &&
    (0 < f.count) &&
    (f.bitOffset % f.ty.bitWidth == 0) &&
    (f.bitOffset + f.totalBits ≤ 512)) &&
  allBitFieldsDisjoint L.fields &&
  (!L.aligned64 || checkContiguous512 0 L.fields)

def CellLayout64.packed64 (name cTypeName : String) (unionMember : String := "")
    (specs : List (String × ScalarType × Nat)) : CellLayout64 :=
  let build := specs.foldl (fun (acc : Nat × List BitSliceField) (sp : String × ScalarType × Nat) =>
    let (off, revFs) := acc
    let f : BitSliceField := { name := sp.1, bitOffset := off, ty := sp.2.1, count := sp.2.2 }
    (off + f.totalBits, f :: revFs)) (0, [])
  { name := name
    cTypeName := cTypeName
    fields := build.2.reverse
    aligned64 := true
    unionMember := unionMember }

private def disjointRegionPair (a b : SlotRegion) : Bool :=
  (a.baseSlot + a.count ≤ b.baseSlot) || (b.baseSlot + b.count ≤ a.baseSlot)

private def allRegionsDisjoint : List SlotRegion → Bool
  | [] => true
  | r :: rest => rest.all (disjointRegionPair r) && allRegionsDisjoint rest

private def checkRegionsPartition : Nat → Nat → List SlotRegion → Bool
  | pos, total, [] => pos == total
  | pos, total, r :: rs =>
    (r.baseSlot == pos) && (r.mapDecl != "") && checkRegionsPartition (pos + r.count) total rs

def ArenaSpec.wf (a : ArenaSpec) : Bool :=
  (0 < a.totalCells) &&
  (a.cPrefix != "") &&
  (a.headerGuard != "") &&
  noDupNames (a.regions.map (fun r => r.name)) &&
  noDupNames (a.layouts.map (fun l => l.name)) &&
  noDupNames (a.layouts.map (fun l => l.cTypeName)) &&
  a.regions.all (fun r => (r.name != "") && (0 < r.count) && (r.baseSlot + r.count ≤ a.totalCells)) &&
  allRegionsDisjoint a.regions &&
  a.layouts.all (fun l => l.wf) &&
  (a.cArenaMapName == "" || checkRegionsPartition 0 a.totalCells a.regions)

theorem cellLayout64_wf_nonempty (L : CellLayout64) (h : L.wf = true) :
    !L.fields.isEmpty = true := by
  unfold CellLayout64.wf at h
  simp only [Bool.and_eq_true] at h
  exact h.1.1.1.1.1.1

theorem arenaSpec_wf_positive_cells (a : ArenaSpec) (h : a.wf = true) :
    0 < a.totalCells := by
  unfold ArenaSpec.wf at h
  simp only [Bool.and_eq_true] at h
  exact decide_eq_true_eq.mp h.1.1.1.1.1.1.1.1.1

private def setCellWord (c : Cell64) (word : Nat) (v : UInt64) : Cell64 :=
  match word with
  | 0 => { c with w0 := v }
  | 1 => { c with w1 := v }
  | 2 => { c with w2 := v }
  | 3 => { c with w3 := v }
  | 4 => { c with w4 := v }
  | 5 => { c with w5 := v }
  | 6 => { c with w6 := v }
  | 7 => { c with w7 := v }
  | _ => c

private def getCellWord (c : Cell64) (word : Nat) : UInt64 :=
  match word with
  | 0 => c.w0
  | 1 => c.w1
  | 2 => c.w2
  | 3 => c.w3
  | 4 => c.w4
  | 5 => c.w5
  | 6 => c.w6
  | 7 => c.w7
  | _ => 0

private def findVal (values : List (String × Int)) (name : String) : Option Int :=
  match values.find? (fun kv => kv.1 == name) with
  | some kv => some kv.2
  | none => none

private def maskBits64 (w : Nat) : UInt64 :=
  if w >= 64 then 0xFFFFFFFFFFFFFFFF else (1 <<< UInt64.ofNat w) - 1

def CellLayout64.packCell (L : CellLayout64) (values : List (String × Int)) : Option Cell64 := do
  if !L.wf then none
  let mut c : Cell64 := {}
  for f in L.fields do
    if f.count == 1 then
      let v ← findVal values f.name
      let word := f.bitOffset / 64
      let sh := UInt64.ofNat (f.bitOffset % 64)
      let raw := (intToU64Twos v) &&& maskBits64 f.ty.bitWidth
      let prev := getCellWord c word
      c := setCellWord c word (prev ||| (raw <<< sh))
    else
      for k in [:f.count] do
        let v := (findVal values s!"{f.name}[{k}]").getD 0
        let bit := f.bitOffset + k * f.ty.bitWidth
        let word := bit / 64
        let sh := UInt64.ofNat (bit % 64)
        let raw := (intToU64Twos v) &&& maskBits64 f.ty.bitWidth
        let prev := getCellWord c word
        c := setCellWord c word (prev ||| (raw <<< sh))
  return c

private def hexChar (n : Nat) : Char :=
  if n < 10 then Char.ofNat (48 + n) else Char.ofNat (55 + n)

def u64ToHexUpper (v : UInt64) : String :=
  Id.run do
    let mut s := ""
    for i in [:16] do
      let shift := (15 - i) * 4
      let nib := ((v >>> UInt64.ofNat shift).toNat) % 16
      s := s.push (hexChar nib)
    return s

def ArenaSpec.schemaHash (a : ArenaSpec) : UInt64 :=
  Id.run do
    let mut buf := ByteArray.empty
    buf := pushFixedAscii buf a.cPrefix 24
    buf := pushLE32 buf (UInt32.ofNat a.totalCells)
    for r in a.regions do
      buf := pushFixedAscii buf r.name 24
      buf := pushLE32 buf (UInt32.ofNat r.baseSlot)
      buf := pushLE32 buf (UInt32.ofNat r.count)
    for l in a.layouts do
      buf := pushFixedAscii buf l.name 24
      buf := pushFixedAscii buf l.cTypeName 24
      for f in l.fields do
        buf := pushFixedAscii buf f.name 24
        buf := pushLE32 buf (UInt32.ofNat f.bitOffset)
        buf := pushLE32 buf (UInt32.ofNat f.ty.bitWidth)
        buf := pushLE32 buf (UInt32.ofNat f.count)
    return fnv1a64Bytes buf

private def renderFieldPackStmt (f : BitSliceField) : String :=
  if f.count == 1 then
    let w := f.bitOffset / 64
    let sh := f.bitOffset % 64
    match f.ty with
    | .cchar =>
      s!"    \{ uint8_t raw = 0U; memcpy(&raw, &in->{f.name}, sizeof(raw)); out_cell[{w}U] |= ((uint64_t)raw) << {sh}U; }\n"
    | .i16 =>
      s!"    \{ uint16_t raw = 0U; memcpy(&raw, &in->{f.name}, sizeof(raw)); out_cell[{w}U] |= ((uint64_t)raw) << {sh}U; }\n"
    | .i32 =>
      s!"    \{ uint32_t raw = 0U; memcpy(&raw, &in->{f.name}, sizeof(raw)); out_cell[{w}U] |= ((uint64_t)raw) << {sh}U; }\n"
    | .i64 =>
      s!"    \{ uint64_t raw = 0ULL; memcpy(&raw, &in->{f.name}, sizeof(raw)); out_cell[{w}U] = raw; }\n"
    | .u64 =>
      s!"    out_cell[{w}U] = in->{f.name};\n"
    | _ =>
      s!"    out_cell[{w}U] |= (((uint64_t)in->{f.name}) & {f.ty.maskExpr64}) << {sh}U;\n"
  else
    match f.ty with
    | .u64 =>
      s!"    for (size_t k = 0U; k < {f.count}U; ++k) \{ out_cell[{f.bitOffset / 64}U + k] = in->{f.name}[k]; }\n"
    | .i64 =>
      s!"    for (size_t k = 0U; k < {f.count}U; ++k) \{ uint64_t raw = 0ULL; memcpy(&raw, &in->{f.name}[k], sizeof(raw)); out_cell[{f.bitOffset / 64}U + k] = raw; }\n"
    | .cchar =>
      s!"    for (size_t k = 0U; k < {f.count}U; ++k) \{ size_t bit = {f.bitOffset}U + k * 8U; uint8_t raw = 0U; memcpy(&raw, &in->{f.name}[k], sizeof(raw)); out_cell[bit >> 6U] |= ((uint64_t)raw) << (bit & 63U); }\n"
    | .i16 =>
      s!"    for (size_t k = 0U; k < {f.count}U; ++k) \{ size_t bit = {f.bitOffset}U + k * 16U; uint16_t raw = 0U; memcpy(&raw, &in->{f.name}[k], sizeof(raw)); out_cell[bit >> 6U] |= ((uint64_t)raw) << (bit & 63U); }\n"
    | .i32 =>
      s!"    for (size_t k = 0U; k < {f.count}U; ++k) \{ size_t bit = {f.bitOffset}U + k * 32U; uint32_t raw = 0U; memcpy(&raw, &in->{f.name}[k], sizeof(raw)); out_cell[bit >> 6U] |= ((uint64_t)raw) << (bit & 63U); }\n"
    | _ =>
      s!"    for (size_t k = 0U; k < {f.count}U; ++k) \{ size_t bit = {f.bitOffset}U + k * {f.ty.bitWidth}U; out_cell[bit >> 6U] |= (((uint64_t)in->{f.name}[k]) & {f.ty.maskExpr64}) << (bit & 63U); }\n"

private def renderFieldUnpackStmt (f : BitSliceField) : String :=
  if f.count == 1 then
    let w := f.bitOffset / 64
    let sh := f.bitOffset % 64
    match f.ty with
    | .cchar =>
      s!"    \{ uint8_t raw = (uint8_t)((cell[{w}U] >> {sh}U) & UINT64_C(0xFF)); memcpy(&out->{f.name}, &raw, sizeof(raw)); }\n"
    | .u8  => s!"    out->{f.name} = (uint8_t)((cell[{w}U] >> {sh}U) & UINT64_C(0xFF));\n"
    | .u16 => s!"    out->{f.name} = (uint16_t)((cell[{w}U] >> {sh}U) & UINT64_C(0xFFFF));\n"
    | .i16 =>
      s!"    \{ uint16_t raw = (uint16_t)((cell[{w}U] >> {sh}U) & UINT64_C(0xFFFF)); memcpy(&out->{f.name}, &raw, sizeof(raw)); }\n"
    | .u32 => s!"    out->{f.name} = (uint32_t)((cell[{w}U] >> {sh}U) & UINT64_C(0xFFFFFFFF));\n"
    | .i32 =>
      s!"    \{ uint32_t raw = (uint32_t)((cell[{w}U] >> {sh}U) & UINT64_C(0xFFFFFFFF)); memcpy(&out->{f.name}, &raw, sizeof(raw)); }\n"
    | .u64 => s!"    out->{f.name} = cell[{w}U];\n"
    | .i64 =>
      s!"    \{ uint64_t raw = cell[{w}U]; memcpy(&out->{f.name}, &raw, sizeof(raw)); }\n"
  else
    match f.ty with
    | .cchar =>
      s!"    for (size_t k = 0U; k < {f.count}U; ++k) \{ size_t bit = {f.bitOffset}U + k * 8U; uint8_t raw = (uint8_t)((cell[bit >> 6U] >> (bit & 63U)) & UINT64_C(0xFF)); memcpy(&out->{f.name}[k], &raw, sizeof(raw)); }\n"
    | .u8 =>
      s!"    for (size_t k = 0U; k < {f.count}U; ++k) \{ size_t bit = {f.bitOffset}U + k * 8U; out->{f.name}[k] = (uint8_t)((cell[bit >> 6U] >> (bit & 63U)) & UINT64_C(0xFF)); }\n"
    | .u16 =>
      s!"    for (size_t k = 0U; k < {f.count}U; ++k) \{ size_t bit = {f.bitOffset}U + k * 16U; out->{f.name}[k] = (uint16_t)((cell[bit >> 6U] >> (bit & 63U)) & UINT64_C(0xFFFF)); }\n"
    | .i16 =>
      s!"    for (size_t k = 0U; k < {f.count}U; ++k) \{ size_t bit = {f.bitOffset}U + k * 16U; uint16_t raw = (uint16_t)((cell[bit >> 6U] >> (bit & 63U)) & UINT64_C(0xFFFF)); memcpy(&out->{f.name}[k], &raw, sizeof(raw)); }\n"
    | .u32 =>
      s!"    for (size_t k = 0U; k < {f.count}U; ++k) \{ size_t bit = {f.bitOffset}U + k * 32U; out->{f.name}[k] = (uint32_t)((cell[bit >> 6U] >> (bit & 63U)) & UINT64_C(0xFFFFFFFF)); }\n"
    | .i32 =>
      s!"    for (size_t k = 0U; k < {f.count}U; ++k) \{ size_t bit = {f.bitOffset}U + k * 32U; uint32_t raw = (uint32_t)((cell[bit >> 6U] >> (bit & 63U)) & UINT64_C(0xFFFFFFFF)); memcpy(&out->{f.name}[k], &raw, sizeof(raw)); }\n"
    | .u64 =>
      s!"    for (size_t k = 0U; k < {f.count}U; ++k) \{ out->{f.name}[k] = cell[{f.bitOffset / 64}U + k]; }\n"
    | .i64 =>
      s!"    for (size_t k = 0U; k < {f.count}U; ++k) \{ uint64_t raw = cell[{f.bitOffset / 64}U + k]; memcpy(&out->{f.name}[k], &raw, sizeof(raw)); }\n"

def CellLayout64.renderC17Codec (cPrefix : String) (L : CellLayout64) : String :=
  let fnPrefix := s!"{cPrefix}_{L.name}"
  let structFields := L.fields.foldl (fun acc f =>
    if f.count == 1 then acc ++ s!"    {f.ty.cType} {f.name};\n"
    else acc ++ s!"    {f.ty.cType} {f.name}[{f.count}];\n") ""
  let packBody := L.fields.foldl (fun acc f => acc ++ renderFieldPackStmt f) ""
  let unpackBody := L.fields.foldl (fun acc f => acc ++ renderFieldUnpackStmt f) ""
  let structDecl :=
    if L.aligned64 then
      "typedef struct FLOW_SCHEMA_ALIGN64 {\n" ++
      structFields ++
      s!"} {L.cTypeName};\n" ++
      s!"FLOW_SCHEMA_STATIC_ASSERT64({L.cTypeName});\n\n"
    else
      "typedef struct {\n" ++
      structFields ++
      s!"} {L.cTypeName};\n\n"
  structDecl ++
  s!"static inline void {fnPrefix}_pack(flow_cell out_cell, const {L.cTypeName} *in) \{\n" ++
  "    for (size_t i = 0U; i < 8U; ++i) {\n" ++
  "        out_cell[i] = UINT64_C(0);\n" ++
  "    }\n" ++
  "    if (!in) {\n        return;\n    }\n" ++
  packBody ++
  "}\n\n" ++
  s!"static inline void {fnPrefix}_unpack(const flow_cell cell, {L.cTypeName} *out) \{\n" ++
  "    if (!out) {\n        return;\n    }\n" ++
  unpackBody ++
  "}\n\n" ++
  s!"static inline void {fnPrefix}_pack_bytes(uint8_t out_bytes[64], const {L.cTypeName} *in) \{\n" ++
  "    flow_cell tmp;\n" ++
  s!"    {fnPrefix}_pack(tmp, in);\n" ++
  "    flow_cell_to_bytes(tmp, out_bytes);\n" ++
  "}\n\n" ++
  s!"static inline void {fnPrefix}_unpack_bytes(const uint8_t raw[64], {L.cTypeName} *out) \{\n" ++
  "    flow_cell tmp;\n" ++
  "    flow_cell_from_bytes(tmp, raw);\n" ++
  s!"    {fnPrefix}_unpack(tmp, out);\n" ++
  "}\n\n"

def ArenaSpec.renderC17Header (a : ArenaSpec) : Option String :=
  if !a.wf then none
  else
    let upper := a.cPrefix.toUpper
    let hashHex := u64ToHexUpper a.schemaHash
    let regionDefs := a.regions.foldl (fun acc r =>
      let ru := r.name.toUpper
      let m1 := if r.cSlotMacro != "" then s!"#define {r.cSlotMacro} {r.baseSlot}U\n" else ""
      let m2 := if r.cCountMacro != "" then s!"#define {r.cCountMacro} {r.count}U\n" else ""
      acc ++
      s!"#define {upper}_SLOT_{ru}_BASE  {r.baseSlot}U\n" ++
      s!"#define {upper}_SLOT_{ru}_COUNT {r.count}U\n" ++ m1 ++ m2) ""
    let codecs := a.layouts.foldl (fun acc l => acc ++ l.renderC17Codec a.cPrefix) ""
    let cellUnion :=
      if a.cCellUnionName == "" then ""
      else
        let members := a.layouts.foldl (fun acc l =>
          if l.unionMember == "" then acc else acc ++ s!"    {l.cTypeName} {l.unionMember};\n") ""
        "typedef union FLOW_SCHEMA_ALIGN64 {\n" ++
        "    uint8_t  bytes[64];\n" ++
        "    uint32_t u32[16];\n" ++
        "    uint64_t u64[8];\n" ++
        members ++
        s!"} {a.cCellUnionName};\n" ++
        s!"FLOW_SCHEMA_STATIC_ASSERT64({a.cCellUnionName});\n\n"
    let arenaMap :=
      if a.cArenaMapName == "" then ""
      else
        let mapFields := a.regions.foldl (fun acc r => acc ++ s!"    {r.mapDecl}\n") ""
        let arenaUnion :=
          if a.cArenaUnionName != "" && a.cCellUnionName != "" then
            "typedef union FLOW_SCHEMA_ALIGN64 {\n" ++
            s!"    {a.cCellUnionName} cells[{upper}_TOTAL_CELLS];\n" ++
            s!"    {a.cArenaMapName} map;\n" ++
            s!"} {a.cArenaUnionName};\n" ++
            s!"FLOW_SCHEMA_STATIC_ASSERT(sizeof({a.cArenaUnionName}) == {upper}_ARENA_BYTES, \"{a.cArenaUnionName} size mismatch\");\n\n"
          else ""
        "typedef struct FLOW_SCHEMA_ALIGN64 {\n" ++
        mapFields ++
        s!"} {a.cArenaMapName};\n" ++
        s!"FLOW_SCHEMA_STATIC_ASSERT(sizeof({a.cArenaMapName}) == {upper}_ARENA_BYTES, \"{a.cArenaMapName} size mismatch\");\n\n" ++
        arenaUnion
    let alignMacros :=
      "#ifndef FLOW_SCHEMA_ALIGN64\n" ++
      "#if defined(_MSC_VER)\n" ++
      "#define FLOW_SCHEMA_ALIGN64 __declspec(align(64))\n" ++
      "#else\n" ++
      "#define FLOW_SCHEMA_ALIGN64 __attribute__((aligned(64)))\n" ++
      "#endif\n" ++
      "#if defined(__cplusplus)\n" ++
      "#define FLOW_SCHEMA_STATIC_ASSERT(cond, msg) static_assert(cond, msg)\n" ++
      "#define FLOW_SCHEMA_STATIC_ASSERT64(T) static_assert(sizeof(T) == 64U, #T \" must be 64B\"); static_assert(alignof(T) == 64U, #T \" must be 64B aligned\")\n" ++
      "#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)\n" ++
      "#define FLOW_SCHEMA_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)\n" ++
      "#define FLOW_SCHEMA_STATIC_ASSERT64(T) _Static_assert(sizeof(T) == 64U, #T \" must be 64B\"); _Static_assert(_Alignof(T) == 64U, #T \" must be 64B aligned\")\n" ++
      "#else\n" ++
      "#error \"FLOW_SCHEMA requires ISO C11+ or ISO C++11+\"\n" ++
      "#endif\n" ++
      "#endif /* FLOW_SCHEMA_ALIGN64 */\n\n"
    let hdr :=
      s!"/* AUTO-GENERATED BY `Flow.Schema.ArenaSpec.emitC17Header` (`Flow.Schema` v{Flow.version}). */\n" ++
      s!"#ifndef {a.headerGuard}\n" ++
      s!"#define {a.headerGuard}\n\n" ++
      "#include \"flow.h\"\n" ++
      "#include <stdbool.h>\n" ++
      "#include <stddef.h>\n" ++
      "#include <stdint.h>\n" ++
      "#include <string.h>\n\n" ++
      alignMacros ++
      "#ifdef __cplusplus\n" ++
      "extern \"C\" {\n" ++
      "#endif\n\n" ++
      s!"#define {upper}_TOTAL_CELLS {a.totalCells}U\n" ++
      s!"#define {upper}_ARENA_BYTES ((size_t){a.totalCells}U * 64ULL)\n" ++
      s!"#define {upper}_SCHEMA_HASH UINT64_C(0x{hashHex})\n\n" ++
      regionDefs ++ "\n" ++
      codecs ++
      cellUnion ++
      arenaMap ++
      "#ifdef __cplusplus\n" ++
      "}\n" ++
      "#endif\n\n" ++
      s!"#endif /* {a.headerGuard} */\n"
    some hdr

def ArenaSpec.emitC17Header (path : System.FilePath) (a : ArenaSpec) : IO Bool := do
  match a.renderC17Header with
  | none => return false
  | some hdr =>
    IO.FS.writeFile path hdr
    return true

def ArenaSpec.emitC17HeaderChecked (path : System.FilePath) (a : ArenaSpec)
    (_h : a.wf = true := by decide) : IO Bool :=
  a.emitC17Header path

private def renderFieldWgslFn (layoutName : String) (f : BitSliceField) : String :=
  if f.count == 1 then
    let w32 := f.bitOffset / 32
    let sh32 := f.bitOffset % 32
    match f.ty with
    | .cchar | .u8 =>
      s!"fn {layoutName}_{f.name}(c : Cell64) -> u32 \{\n" ++
      s!"    return (flow_cell_u32(c, {w32}u) >> {sh32}u) & 0xFFu;\n}\n\n"
    | .u16 =>
      s!"fn {layoutName}_{f.name}(c : Cell64) -> u32 \{\n" ++
      s!"    return (flow_cell_u32(c, {w32}u) >> {sh32}u) & 0xFFFFu;\n}\n\n"
    | .i16 =>
      s!"fn {layoutName}_{f.name}(c : Cell64) -> i32 \{\n" ++
      s!"    let u = (flow_cell_u32(c, {w32}u) >> {sh32}u) & 0xFFFFu;\n" ++
      s!"    return bitcast<i32>(u << 16u) >> 16u;\n}\n\n"
    | .u32 =>
      s!"fn {layoutName}_{f.name}(c : Cell64) -> u32 \{\n" ++
      s!"    return flow_cell_u32(c, {w32}u);\n}\n\n"
    | .i32 =>
      s!"fn {layoutName}_{f.name}(c : Cell64) -> i32 \{\n" ++
      s!"    return bitcast<i32>(flow_cell_u32(c, {w32}u));\n}\n\n"
    | .u64 | .i64 =>
      s!"fn {layoutName}_{f.name}_lo32(c : Cell64) -> u32 \{\n" ++
      s!"    return flow_cell_u32(c, {w32}u);\n}\n\n" ++
      s!"fn {layoutName}_{f.name}_hi32(c : Cell64) -> u32 \{\n" ++
      s!"    return flow_cell_u32(c, {w32 + 1}u);\n}\n\n"
  else
    let w32 := f.bitOffset / 32
    match f.ty with
    | .cchar | .u8 =>
      s!"fn {layoutName}_{f.name}(c : Cell64, k : u32) -> u32 \{\n" ++
      s!"    let bit = {f.bitOffset}u + (k % {f.count}u) * 8u;\n" ++
      "    return (flow_cell_u32(c, bit >> 5u) >> (bit & 31u)) & 0xFFu;\n}\n\n"
    | .u16 =>
      s!"fn {layoutName}_{f.name}(c : Cell64, k : u32) -> u32 \{\n" ++
      s!"    let bit = {f.bitOffset}u + (k % {f.count}u) * 16u;\n" ++
      "    return (flow_cell_u32(c, bit >> 5u) >> (bit & 31u)) & 0xFFFFu;\n}\n\n"
    | .i16 =>
      s!"fn {layoutName}_{f.name}(c : Cell64, k : u32) -> i32 \{\n" ++
      s!"    let bit = {f.bitOffset}u + (k % {f.count}u) * 16u;\n" ++
      "    let u = (flow_cell_u32(c, bit >> 5u) >> (bit & 31u)) & 0xFFFFu;\n" ++
      "    return bitcast<i32>(u << 16u) >> 16u;\n}\n\n"
    | .u32 =>
      s!"fn {layoutName}_{f.name}(c : Cell64, k : u32) -> u32 \{\n" ++
      s!"    return flow_cell_u32(c, {w32}u + (k % {f.count}u));\n}\n\n"
    | .i32 =>
      s!"fn {layoutName}_{f.name}(c : Cell64, k : u32) -> i32 \{\n" ++
      s!"    return bitcast<i32>(flow_cell_u32(c, {w32}u + (k % {f.count}u)));\n}\n\n"
    | .u64 | .i64 =>
      s!"fn {layoutName}_{f.name}_lo32(c : Cell64, k : u32) -> u32 \{\n" ++
      s!"    return flow_cell_u32(c, {w32}u + (k % {f.count}u) * 2u);\n}\n\n" ++
      s!"fn {layoutName}_{f.name}_hi32(c : Cell64, k : u32) -> u32 \{\n" ++
      s!"    return flow_cell_u32(c, {w32}u + (k % {f.count}u) * 2u + 1u);\n}\n\n"

def CellLayout64.renderWgslAccessors (cPrefix : String) (L : CellLayout64) : String :=
  let prefixName := s!"{cPrefix}_{L.name}"
  L.fields.foldl (fun acc f => acc ++ renderFieldWgslFn prefixName f) ""

def ArenaSpec.renderWgslModule (a : ArenaSpec) : Option String :=
  if !a.wf then none
  else
    let upper := a.cPrefix.toUpper
    let regionConsts := a.regions.foldl (fun acc r =>
      let ru := r.name.toUpper
      acc ++
      s!"const {upper}_SLOT_{ru}_BASE : u32 = {r.baseSlot}u;\n" ++
      s!"const {upper}_SLOT_{ru}_COUNT : u32 = {r.count}u;\n") ""
    let accessors := a.layouts.foldl (fun acc l => acc ++ l.renderWgslAccessors a.cPrefix) ""
    let wgsl :=
      s!"// AUTO-GENERATED BY `Flow.Schema.ArenaSpec.emitWgslModule` (`Flow.Schema` v{Flow.version}).\n" ++
      "// Pure 32-bit portable W3C WGSL (zero 64-bit integer tokens).\n\n" ++
      "struct Cell64 {\n" ++
      "    q0 : vec4<u32>,\n" ++
      "    q1 : vec4<u32>,\n" ++
      "    q2 : vec4<u32>,\n" ++
      "    q3 : vec4<u32>,\n" ++
      "};\n\n" ++
      s!"const {upper}_TOTAL_CELLS : u32 = {a.totalCells}u;\n" ++
      regionConsts ++ "\n" ++
      "fn flow_cell_u32(c : Cell64, w : u32) -> u32 {\n" ++
      "    let q = w >> 2u;\n" ++
      "    let l = w & 3u;\n" ++
      "    var v : vec4<u32> = c.q3;\n" ++
      "    if (q == 0u) { v = c.q0; }\n" ++
      "    else if (q == 1u) { v = c.q1; }\n" ++
      "    else if (q == 2u) { v = c.q2; }\n" ++
      "    if (l == 0u) { return v.x; }\n" ++
      "    if (l == 1u) { return v.y; }\n" ++
      "    if (l == 2u) { return v.z; }\n" ++
      "    return v.w;\n" ++
      "}\n\n" ++
      accessors ++
      "@group(0) @binding(0) var<storage, read> flow_arena : array<Cell64>;\n\n" ++
      "@compute @workgroup_size(1)\n" ++
      "fn flow_schema_probe(@builtin(global_invocation_id) gid : vec3<u32>) {\n" ++
      "    let _c = flow_arena[gid.x % " ++ s!"{upper}_TOTAL_CELLS];\n" ++
      "    let _w = flow_cell_u32(_c, 0u);\n" ++
      "    _ = _w;\n" ++
      "}\n"
    some wgsl

def ArenaSpec.emitWgslModule (path : System.FilePath) (a : ArenaSpec) : IO Bool := do
  match a.renderWgslModule with
  | none => return false
  | some src =>
    IO.FS.writeFile path src
    return true

def ArenaSpec.emitWgslModuleChecked (path : System.FilePath) (a : ArenaSpec)
    (_h : a.wf = true := by decide) : IO Bool :=
  a.emitWgslModule path

/-! ## Part B: Rule-Compatible Atlas Metadata (`Spec`) -/

structure FieldDesc where
  name      : String
  chartSlot : Nat
  lane      : Nat
  minVal    : Int := -maxQ16
  maxVal    : Int := maxQ16
  deriving Repr, DecidableEq

structure Spec where
  cPrefix     : String
  headerGuard : String
  kExp        : Nat
  tagChart    : Nat
  fields      : List FieldDesc
  deriving Repr, DecidableEq

def isAllowedLane (lane : Nat) : Bool :=
  lane == 0 || lane == 1 || lane == 2 || lane == 7

def noDupCoords : List (Nat × Nat) → Bool
  | [] => true
  | x :: xs => !(xs.contains x) && noDupCoords xs

def Spec.wf (s : Spec) : Bool :=
  let nCharts := 2 ^ s.kExp
  (s.cPrefix != "") &&
  (s.headerGuard != "") &&
  (s.kExp ≤ 8) &&
  (s.tagChart < nCharts) &&
  (!s.fields.isEmpty) &&
  noDupCoords (s.fields.map (fun f => (f.chartSlot, f.lane))) &&
  noDupNames (s.fields.map (fun f => f.name)) &&
  s.fields.all (fun f =>
    (f.name != "") &&
    (f.chartSlot < nCharts) &&
    isAllowedLane f.lane &&
    !((f.chartSlot == s.tagChart) && (f.lane == 7)) &&
    (-maxQ16 ≤ f.minVal && f.minVal ≤ f.maxVal && f.maxVal ≤ maxQ16))

theorem spec_wf_tag_inbounds (s : Spec) (h : s.wf = true) : s.tagChart < 2 ^ s.kExp := by
  unfold Spec.wf at h
  simp only [Bool.and_eq_true] at h
  exact decide_eq_true_eq.mp h.1.1.1.1.2

theorem spec_wf_kexp_le_8 (s : Spec) (h : s.wf = true) : s.kExp ≤ 8 := by
  unfold Spec.wf at h
  simp only [Bool.and_eq_true] at h
  exact decide_eq_true_eq.mp h.1.1.1.1.1.2

def Spec.schemaHash (s : Spec) : UInt64 :=
  Id.run do
    let mut buf := ByteArray.empty
    buf := pushFixedAscii buf s.cPrefix 24
    buf := pushLE32 buf (UInt32.ofNat s.kExp)
    buf := pushLE32 buf (UInt32.ofNat s.tagChart)
    for f in s.fields do
      buf := pushFixedAscii buf f.name 24
      buf := pushLE32 buf (UInt32.ofNat f.chartSlot)
      buf := pushLE32 buf (UInt32.ofNat f.lane)
      buf := pushLE64 buf (intToU64Twos f.minVal)
      buf := pushLE64 buf (intToU64Twos f.maxVal)
    return fnv1a64Bytes buf

private def setCellLane (c : Cell64) (lane : Nat) (v : UInt64) : Cell64 :=
  match lane with
  | 0 => { c with w0 := v }
  | 1 => { c with w1 := v }
  | 2 => { c with w2 := v }
  | 7 => { c with w7 := v }
  | _ => c

def Spec.packBundle (s : Spec) (initState : Cell64) (dynCharts : List Rule) (fallback : Rule)
    (values : List (String × Int)) (epoch : UInt32 := 1) : Option ByteArray := do
  if !s.wf then none
  let nCharts := 2 ^ s.kExp
  let cap : UInt8 := UInt8.ofNat (nCharts - 1)
  let maskRule (r : Rule) : Rule :=
    { r with nextLo := r.nextLo &&& cap, nextHi := r.nextHi &&& cap }
  let fbCell := mintCell (maskRule fallback) epoch
  let baseArr : Array Cell64 := Id.run do
    let mut arr := #[]
    for i in [:nCharts] do
      let c := match dynCharts[i]? with
        | some r => mintCell (maskRule r) epoch
        | none => fbCell
      arr := arr.push c
    return arr
  let mut cells := baseArr
  for f in s.fields do
    let v ← findVal values f.name
    if v < f.minVal || v > f.maxVal then
      none
    let c := cells[f.chartSlot]!
    cells := cells.set! f.chartSlot (setCellLane c f.lane (intToU64Twos v))
  let tc := cells[s.tagChart]!
  cells := cells.set! s.tagChart (setCellLane tc 7 s.schemaHash)
  let mut out := initState.toBytes
  for c in cells do
    out := out ++ c.toBytes
  return out

def Spec.renderAbiHeader (s : Spec) : Option String :=
  if !s.wf then none
  else
    let upper := s.cPrefix.toUpper
    let nCharts := 2 ^ s.kExp
    let bBytes := (1 + nCharts) * 64
    let hashHex := u64ToHexUpper s.schemaHash
    let structFields := s.fields.foldl (fun acc f =>
      acc ++ s!"    int64_t {f.name};\n") ""
    let unpackBody := s.fields.foldl (fun acc f =>
      acc ++
      s!"    out->{f.name} = (int64_t)arena[{f.chartSlot}U][{f.lane}U];\n" ++
      s!"    if (out->{f.name} < INT64_C({f.minVal}) || out->{f.name} > INT64_C({f.maxVal})) \{\n" ++
      s!"        return false;\n" ++
      s!"    }\n") ""
    let hdr :=
      s!"/* AUTO-GENERATED BY `Flow.Schema.emitAbiHeader` (`Flow.Schema` v{Flow.version}). DO NOT EDIT. */\n" ++
      s!"#ifndef {s.headerGuard}\n" ++
      s!"#define {s.headerGuard}\n\n" ++
      "#include \"flow.h\"\n" ++
      "#include <stdbool.h>\n" ++
      "#include <stddef.h>\n" ++
      "#include <stdint.h>\n\n" ++
      "#ifdef __cplusplus\n" ++
      "extern \"C\" {\n" ++
      "#endif\n\n" ++
      s!"#define {upper}_K_EXP        {s.kExp}U\n" ++
      s!"#define {upper}_CHART_COUNT  {nCharts}U\n" ++
      s!"#define {upper}_BUNDLE_BYTES {bBytes}U\n" ++
      s!"#define {upper}_TAG_CHART    {s.tagChart}U\n" ++
      s!"#define {upper}_SCHEMA_HASH  UINT64_C(0x{hashHex})\n\n" ++
      "typedef struct {\n" ++
      structFields ++
      s!"} {s.cPrefix}_t;\n\n" ++
      s!"static inline bool {s.cPrefix}_unpack(const flow_cell *arena, size_t chart_count,\n" ++
      s!"                                       {s.cPrefix}_t *out) \{\n" ++
      "    if (!arena || !out || chart_count < " ++ s!"{upper}_CHART_COUNT) \{\n" ++
      "        return false;\n" ++
      "    }\n" ++
      s!"    if (arena[{upper}_TAG_CHART][7] != {upper}_SCHEMA_HASH) \{\n" ++
      "        return false;\n" ++
      "    }\n" ++
      unpackBody ++
      "    return true;\n" ++
      "}\n\n" ++
      "#ifdef __cplusplus\n" ++
      "}\n" ++
      "#endif\n\n" ++
      s!"#endif /* {s.headerGuard} */\n"
    some hdr

def Spec.emitAbiHeader (path : System.FilePath) (s : Spec) : IO Bool := do
  match s.renderAbiHeader with
  | none => return false
  | some hdr =>
    IO.FS.writeFile path hdr
    return true

def Spec.emitAbiHeaderChecked (path : System.FilePath) (s : Spec)
    (_h : s.wf = true := by decide) : IO Bool :=
  s.emitAbiHeader path

end Flow.Schema
