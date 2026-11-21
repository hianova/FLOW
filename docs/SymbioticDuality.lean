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
  let _ : ∀ (stochasticFloor strainPost strainPre : Nat), strainPost < strainPre →
      coupledLoss stochasticFloor strainPost < coupledLoss stochasticFloor strainPre :=
    scale_mismatch_strict_drop
  let _ : ∀ (stochasticFloor : Nat), coupledLoss stochasticFloor 0 = stochasticFloor :=
    stochastic_floor_invariance
  let _ : ∀ (S d r : Nat), 0 < S → 0 < d → d < S → 3 * S * S * r ≤ d * d * d → r < d ∧ r * r < d * d :=
    nonquad_2jet_contraction
  let _ : ∀ (K v : Nat), 2 ≤ K → 0 < v → v < K * K * v ∧ 4 * v ≤ K * K * v :=
    shift_rule_curv_var_strictly_better
  let _ : ∀ (zExit zEnter snr : Nat), snr < zEnter → stepSchmitt zExit zEnter snr false = false :=
    schmitt_sleep_blocks_subthreshold
  let _ : ∀ (zExit zEnter snr : Nat) (active : Bool), zExit ≤ snr → snr < zEnter →
      stepSchmitt zExit zEnter snr active = active :=
    schmitt_deadband_preserves_state
  let _ : ∀ (zExit zEnter snr : Nat) (active : Bool), zExit ≤ zEnter → zEnter ≤ snr →
      stepSchmitt zExit zEnter snr active = true :=
    schmitt_wakes_above_enter
  let _ : ∀ (zExit zEnter snr : Nat) (active : Bool), zExit ≤ zEnter → snr < zExit →
      stepSchmitt zExit zEnter snr active = false :=
    schmitt_sleeps_below_exit
  let _ : ∀ (stochasticFloor κ d e : Nat), 0 < κ → d < e →
      d * d < e * e ∧ coupledLoss stochasticFloor (κ * (d * d)) < coupledLoss stochasticFloor (κ * (e * e)) :=
    noisy_update_hurts_below_noise_floor
  let _ : ∀ (b η fm f0 fp : Int),
      shiftSlope (affineChannel b η fm) (affineChannel b η fp) * shiftCurv fm f0 fp =
      shiftSlope fm fp * shiftCurv (affineChannel b η fm) (affineChannel b η f0) (affineChannel b η fp) :=
    affine_2jet_ratio_invariant
  let _ : ∀ (b η fm fp deltaBias : Int), deltaBias ≠ 0 →
      shiftSlope (affineChannel b η fm) (affineChannel b η fp + deltaBias) = η * shiftSlope fm fp + deltaBias ∧
      shiftSlope (affineChannel b η fm) (affineChannel b η fp + deltaBias) ≠ η * shiftSlope fm fp :=
    axis_bias_breaks_shift_invariance
  let _ : ∀ (bound thresh z : Int), 0 ≤ bound → bound < thresh →
      -thresh < huberClip bound z ∧ huberClip bound z < thresh :=
    huber_single_glitch_cannot_trigger
  let _ : ∀ (bound thresh : Int), 0 ≤ bound → ∀ (zs : List Int), bound * (zs.length : Int) < thresh →
      -thresh < accumClipped bound 0 zs ∧ accumClipped bound 0 zs < thresh :=
    huber_requires_multi_epoch_persistence
  let _ : ∀ (μ est : Nat), 2 * μ < est → μ < Int.natAbs ((μ : Int) - (est : Int)) :=
    winners_curse_overshoot_hurts
  let _ : ∀ (μ est zSoft : Nat), zSoft < est → est < 2 * μ + zSoft →
      Int.natAbs ((μ : Int) - ((est - zSoft : Nat) : Int)) < μ :=
    soft_threshold_strict_improvement
  pure ()

def main : IO Unit := do
  let c0 : Config := { sector := 2, modulus := 9999999 }
  let pure2Jet := collapse2Jet demoLandscape c0
  let pureTunneled := instantonTunnelRaw 7 c0.modulus
  let symbioticOut := runSymbiotic demoLandscape [3, 1, 7, 4, 2] c0
  let sampleGrid : List Int := [-131072, 0, 65536, 327680, 9999999]
  let schQuiet := stepSchmitt 15 30 10 false
  let schBandOff := stepSchmitt 15 30 20 false
  let schBandOn := stepSchmitt 15 30 20 true
  let schWake := stepSchmitt 15 30 35 false
  let lossPre := coupledLoss 100 400
  let lossPost := coupledLoss 100 4
  let lossFloor := coupledLoss 100 0
  let lossHurtTrue := coupledLoss 100 (1 * (5 * 5))
  let lossHurtNoisy := coupledLoss 100 (1 * (20 * 20))
  let fm : Int := 55
  let f0 : Int := 0
  let fp : Int := 45
  let sIdeal := shiftSlope fm fp
  let cIdeal := shiftCurv fm f0 fp
  let sAff := shiftSlope (affineChannel 20 9 fm) (affineChannel 20 9 fp)
  let cAff := shiftCurv (affineChannel 20 9 fm) (affineChannel 20 9 f0) (affineChannel 20 9 fp)
  let glitchClip := huberClip 3 999999
  let shortAccum := accumClipped 3 0 [999999, 999999]
  let errRaw := Int.natAbs ((10 : Int) - (22 : Int))
  let errSoft := Int.natAbs ((10 : Int) - ((22 - 12 : Nat) : Int))
  IO.println "=== SymbioticDuality.lean: Machine-Checked Witness ==="
  IO.println s!"[Initial False Vacuum]       sector={c0.sector}, mod={c0.modulus}, E={energy demoLandscape c0}"
  IO.println s!"[Pure 2-Jet Differential]    sector={pure2Jet.sector}, mod={pure2Jet.modulus}, E={energy demoLandscape pure2Jet} (Trapped at j0=62 > 0)"
  IO.println s!"[Pure SEARU Raw Tunnel]      sector={pureTunneled.sector}, mod={pureTunneled.modulus}, E={energy demoLandscape pureTunneled} (False-Rejected: E >> 62)"
  IO.println s!"[Grid Ground Hits (M=5)]     pure={countPureGroundHits demoLandscape sampleGrid}/5 vs symbiotic={countSymbioticGroundHits demoLandscape sampleGrid}/5"
  IO.println s!"[Symbiotic SEARU x 2-Jet]    sector={symbioticOut.sector}, mod={symbioticOut.modulus}, E={energy demoLandscape symbioticOut} (EXACT GLOBAL GROUND STATE)"
  IO.println s!"[Schmitt Hysteresis (15,30)] snr=10(off)->{schQuiet}, deadband snr=20(off->{schBandOff}, on->{schBandOn}), wake snr=35(off)->{schWake}"
  IO.println s!"[Scale-Mismatch & NoiseFloor] drop: {lossPost} < {lossPre}; floor-only={lossFloor}; noise-hurt: true={lossHurtTrue} < noisy={lossHurtNoisy}"
  IO.println s!"[Affine SPAM Cancellation]   ideal=({sIdeal},{cIdeal}), affine(b=20,eta=9)=({sAff},{cAff}), cross={sAff * cIdeal}=={sIdeal * cAff}"
  IO.println s!"[Huber & Soft-Debias]        glitch(999999,B=3)->{glitchClip}<10, accum(2)={shortAccum}<10; err: raw={errRaw} vs soft={errSoft}"

end SymbioticDuality

def main : IO Unit := SymbioticDuality.main
