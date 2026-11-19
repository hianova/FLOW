import Flow

def synthPktPacked : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "pkt_p" "synth_pkt_p_t" "pkt_p" [
    ("tag_ch", .cchar, 4),
    ("vals_i16", .i16, 4),
    ("v_u32", .u32, 4),
    ("pad_u32", .u32, 1),
    ("acc_u64", .u64, 4)
  ]

def synthArena : Flow.Schema.ArenaSpec where
  cPrefix         := "synth_arena"
  headerGuard     := "SYNTH_ARENA_H"
  totalCells      := 16
  cCellUnionName  := "synth_cell64_t"
  cArenaMapName   := "synth_arena_map_t"
  cArenaUnionName := "synth_arena_t"
  regions := [
    { name := "hdr",  baseSlot := 0, count := 1,  cSlotMacro := "SYNTH_SLOT_HDR",  mapDecl := "synth_pkt_p_t hdr;" },
    { name := "ctrl", baseSlot := 1, count := 3,  cSlotMacro := "SYNTH_SLOT_CTRL", cCountMacro := "SYNTH_MAX_CTRL", mapDecl := "synth_pkt_p_t ctrl[SYNTH_MAX_CTRL];" },
    { name := "data", baseSlot := 4, count := 12, cSlotMacro := "SYNTH_SLOT_DATA", mapDecl := "uint64_t data[12][8];" }
  ]
  layouts := [
    synthPktPacked,
    { name := "pkt", cTypeName := "synth_pkt_t", unionMember := "pkt", aligned64 := true, fields := [
      { name := "tag_u8",   bitOffset := 0,   ty := .u8 },
      { name := "pad_u8",   bitOffset := 8,   ty := .u8 },
      { name := "flag_u16", bitOffset := 16,  ty := .u16 },
      { name := "len_u32",  bitOffset := 32,  ty := .u32 },
      { name := "val_i32",  bitOffset := 64,  ty := .i32 },
      { name := "pad_u32",  bitOffset := 96,  ty := .u32 },
      { name := "seq_u64",  bitOffset := 128, ty := .u64, count := 4 },
      { name := "acc_i64",  bitOffset := 384, ty := .i64, count := 2 }
    ] }
  ]

def synthBundleSpec : Flow.Schema.Spec where
  cPrefix := "synth_bundle_abi"
  headerGuard := "SYNTH_BUNDLE_ABI_H"
  kExp := 2
  tagChart := 1
  fields := [
    { name := "alpha", chartSlot := 1, lane := 0, minVal := 1, maxVal := 10000 },
    { name := "beta",  chartSlot := 2, lane := 1, minVal := -5000, maxVal := 5000 },
    { name := "gamma", chartSlot := 3, lane := 2, minVal := 0, maxVal := 65535 }
  ]

def synthLinGraph : Flow.Morphism.Graph :=
  let b0 := Flow.Morphism.Builder.init "synth_lin" .linear true
  let (b1, a) := b0.input "a" 8
  let (b2, x) := b1.input "x" 8
  let (b3, sel) := b2.input "sel" 1
  let (b4, invA) := b3.gf2mInv "inv_a" 8 a 0x11B
  let (b5, sq) := b4.sqrFxp "sq" 8 invA 4 4 4
  let (b6, sum) := b5.add "sum" 8 sq x
  let (b7, ez) := b6.eqZero "ez" a
  let (b8, y0) := b7.ctrlSel "y0" 8 sel invA sum
  let (b9, y) := b8.ctrlSel "y" 8 ez y0 x
  b9.build [y]

def synthCartGraph : Flow.Morphism.Graph :=
  let b0 := Flow.Morphism.Builder.init "synth_cart" .cartesian false
  let (b1, s0) := b0.input "state_in" 512
  let (b2, p0) := b1.stagePipe "pipe0" 64 [s0] 1 2 3 0
  b2.build [p0]

def synthLinMapGraph : Flow.Morphism.Graph :=
  let b0 := Flow.Morphism.Builder.init "synth_linmap" .linear true
  let (b1, x) := b0.input "x" 9
  let (b2, m1) := b1.linMapGF2 "m1" x 9 [0x101, 0x002, 0x004, 0x008, 0x010, 0x020, 0x040, 0x080, 0x100]
  let (b3, p) := b2.andVec "p" 9 m1 x
  b3.build [p]

def synthQuadInvGraph : Flow.Morphism.Graph :=
  let (b0, x) := (Flow.Morphism.Builder.init "synth_quad_inv" .linear true).input "x" 8
  let (b1, y) := b0.gf2mQuadInv "qi" 4 x 0x13 0x8
  b1.build [y]

def synthMul4Graph : Flow.Morphism.Graph :=
  let b0 := Flow.Morphism.Builder.init "synth_mul4" .linear true
  let (b1, a) := b0.input "a" 4
  let (b2, bv) := b1.input "b" 4
  let (b3, y) := b2.gf2mMul "m4" 4 a bv 0x13
  b3.build [y]

def synthInv4Graph : Flow.Morphism.Graph :=
  let (b0, a) := (Flow.Morphism.Builder.init "synth_inv4" .linear true).input "a" 4
  let (b1, y) := b0.gf2mInv "i4" 4 a 0x13
  b1.build [y]

def synthLut5Graph : Flow.Morphism.Graph :=
  let (b0, x) := (Flow.Morphism.Builder.init "synth_lut5" .linear true).input "x" 5
  let tbl : List UInt64 := [
    0x04,0x0b,0x1f,0x14,0x1a,0x15,0x09,0x02,0x1b,0x05,0x08,0x12,0x1d,0x03,0x06,0x1c,
    0x1e,0x13,0x07,0x0e,0x00,0x0d,0x11,0x18,0x10,0x0c,0x01,0x19,0x16,0x0a,0x0f,0x17 ]
  let (b1, y) := b0.op .lut_rom "y" 5 [x] (lutWords := tbl)
  b1.build [y]

def main (args : List String) : IO UInt32 := do
  let outDir : System.FilePath := System.FilePath.mk (args.headD "build/ir_gate")
  IO.FS.createDirAll outDir
  let ok1 ← synthArena.emitC17HeaderChecked (outDir / "synth_arena_abi.h")
  let ok2 ← synthArena.emitWgslModuleChecked (outDir / "synth_arena.wgsl")
  let ok3 ← synthBundleSpec.emitAbiHeaderChecked (outDir / "synth_bundle_abi.h")
  let fb : Flow.Rule :=
    { b1 := Flow.q16One, dtMax := 10 * Flow.q16One, bExit := -100 * Flow.q16One,
      bEnter := 100 * Flow.q16One, uGain := Flow.q16One }
  let st0 := Flow.mintState 2 0
  match synthBundleSpec.packBundle st0 [fb] fb [("alpha", 42), ("beta", -123), ("gamma", 777)] 1 with
  | none => return 1
  | some bytes => IO.FS.writeBinFile (outDir / "synth_bundle.flow") bytes
  let ok4 ← Flow.Morphism.emitFirChecked (outDir / "synth_lin.fir") synthLinGraph
  let ok5 ← Flow.Morphism.emitFirChecked (outDir / "synth_cart.fir") synthCartGraph
  let ok6 ← Flow.Morphism.emitFirChecked (outDir / "synth_linmap.fir") synthLinMapGraph
  let ok7 := (synthLinMapGraph.multBits == 9) && (synthLinMapGraph.multDepth == 1) &&
             (synthLinMapGraph.nonAffineOpCount == 1)
  let ok8 ← Flow.Morphism.emitFirChecked (outDir / "synth_quad_inv.fir") synthQuadInvGraph
  let ok9 ← Flow.Morphism.emitFirChecked (outDir / "synth_mul4.fir") synthMul4Graph
  let ok10 ← Flow.Morphism.emitFirChecked (outDir / "synth_inv4.fir") synthInv4Graph
  let ok11 ← Flow.Morphism.emitFirChecked (outDir / "synth_lut5.fir") synthLut5Graph
  let ok12 := Flow.Morphism.admitsTarget Flow.Canonical.gf256SboxMac .unitary &&
              Flow.Morphism.admitsTarget Flow.Canonical.gf256TowerSpec .unitary &&
              Flow.Morphism.admitsTarget Flow.Canonical.lut5BijectionSpec .unitary
  if !(ok1 && ok2 && ok3 && ok4 && ok5 && ok6 && ok7 && ok8 && ok9 && ok10 && ok11 && ok12) then
    return 1
  return 0
