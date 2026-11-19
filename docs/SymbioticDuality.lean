/-
  SymbioticDuality.lean — Machine-Checked Executable Witness (`Flow.Bundle`)
  ==========================================================================
  Thin runnable witness importing the canonical SSOT theorems in `Flow.Bundle`
  (`core/lean/Flow/Bundle.lean`) — zero duplicated theorem definitions.
-/
import Flow.Bundle

namespace SymbioticDuality

open Flow.Bundle

def demoLandscape : Landscape where
  vacuumFloor := fun s => if s == 7 then 0 else 42 + (s % 5) * 10
  fiberMin    := fun s => Int.ofNat s * 65536 - 131072
  curvature   := fun s => 3 + (s % 4)
  curv_pos    := by intro s; omega
  trueSector  := 7
  true_zero   := by decide

#eval do
  let _ : ∀ (L : Landscape) (c : Config), energy L (collapse2Jet L c) = L.vacuumFloor c.sector :=
    collapse2Jet_reaches_sector_floor
  let _ : ∀ (L : Landscape) (proposals : List Nat) (c0 : Config), L.trueSector ∈ proposals →
      energy L (runSymbiotic L proposals c0) = 0 ∧
      (runSymbiotic L proposals c0).modulus = L.fiberMin (runSymbiotic L proposals c0).sector :=
    symbiotic_duality_complete
  pure ()

def main : IO Unit := do
  let c0 : Config := { sector := 2, modulus := 9999999 }
  let pure2Jet := collapse2Jet demoLandscape c0
  let pureTunneled := instantonTunnelRaw 7 c0.modulus
  let symbioticOut := runSymbiotic demoLandscape [3, 1, 7, 4, 2] c0
  let sampleGrid : List Int := [-131072, 0, 65536, 327680, 9999999]
  IO.println "=== SymbioticDuality.lean: Machine-Checked Witness ==="
  IO.println s!"[Initial False Vacuum]       sector={c0.sector}, mod={c0.modulus}, E={energy demoLandscape c0}"
  IO.println s!"[Pure 2-Jet Differential]    sector={pure2Jet.sector}, mod={pure2Jet.modulus}, E={energy demoLandscape pure2Jet} (Trapped at j0=62 > 0)"
  IO.println s!"[Pure SEARU Raw Tunnel]      sector={pureTunneled.sector}, mod={pureTunneled.modulus}, E={energy demoLandscape pureTunneled} (False-Rejected: E >> 62)"
  IO.println s!"[Grid Ground Hits (M=5)]     pure={countPureGroundHits demoLandscape sampleGrid}/5 vs symbiotic={countSymbioticGroundHits demoLandscape sampleGrid}/5"
  IO.println s!"[Symbiotic SEARU x 2-Jet]    sector={symbioticOut.sector}, mod={symbioticOut.modulus}, E={energy demoLandscape symbioticOut} (EXACT GLOBAL GROUND STATE)"

end SymbioticDuality

def main : IO Unit := SymbioticDuality.main
