/-
  Flow.Emit — File-System Binary Emitter Helpers (`FLOW/sdk`)
  ============================================================
  Thin IO wrappers around pure `Flow.Core` encoders (`mintCell`, `mintArena`, `mintBundle`).
-/
import Flow.Core

namespace Flow

def emitCell (path : System.FilePath) (c : Cell64) : IO Unit :=
  IO.FS.writeBinFile path c.toBytes

def emitState (path : System.FilePath) (s : Cell64) : IO Unit :=
  IO.FS.writeBinFile path s.toBytes

def emitArena (path : System.FilePath) (kExp : Nat) (specs : List Rule) (fallback : Rule)
    (epoch : UInt32 := 1) : IO Unit :=
  IO.FS.writeBinFile path (mintArena kExp specs fallback epoch)

def emitBundle (path : System.FilePath) (kExp : Nat) (initState : Cell64) (specs : List Rule)
    (fallback : Rule) (epoch : UInt32 := 1) : IO Unit :=
  IO.FS.writeBinFile path (mintBundle kExp initState specs fallback epoch)

end Flow
