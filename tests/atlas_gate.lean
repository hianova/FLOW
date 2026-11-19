import Flow

def c0 : Flow.Rule :=
  { b0 := 0, b1 := Flow.q16One, b2 := 0, bExit := -100 * Flow.q16One, bEnter := 10 * Flow.q16One,
    dtMax := 10 * Flow.q16One, uGain := Flow.q16One, nextLo := 0, nextHi := 1, kanLock := 0 }

def c1 : Flow.Rule :=
  { b0 := 0, b1 := 0, b2 := 2 * Flow.q16One, bExit := 2 * Flow.q16One, bEnter := 50 * Flow.q16One,
    dtMax := 10 * Flow.q16One, uGain := Flow.q16One, nextLo := 0, nextHi := 1, kanLock := 0x0001 }

def cHarbor : Flow.Rule :=
  { b0 := 0, b1 := 2 * Flow.q16One, b2 := 0, bExit := -50 * Flow.q16One, bEnter := 50 * Flow.q16One,
    dtMax := 10 * Flow.q16One, uGain := Flow.q16One, nextLo := 0, nextHi := 1, kanLock := 0x0003 }

def cForeign : Flow.Rule :=
  { b0 := 0, b1 := Flow.q16One, b2 := 0, bExit := -10 * Flow.q16One, bEnter := 10 * Flow.q16One,
    dtMax := 10 * Flow.q16One, uGain := Flow.q16One, nextLo := 0, nextHi := 7, kanLock := 0 }

def main (args : List String) : IO UInt32 := do
  let outDir : System.FilePath := System.FilePath.mk (args.headD "build/capsules")
  IO.FS.createDirAll outDir
  let s0 := Flow.mintState 1 0 (initChart := 0) (initMode := 0x0001)
  Flow.emitBundle (outDir / "surge.flow") 1 s0 [c0, c1] c0 2
  Flow.emitCell (outDir / "harbor.cell") (Flow.mintCell cHarbor 5)
  Flow.emitCell (outDir / "foreign.cell") (Flow.mintCell cForeign 5)
  return 0
