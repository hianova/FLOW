/-
  Flow.Bundle — Symbiotic Fiber-Bundle Duality & Univalent Split Theorems
  =========================================================================
  Pure mathematical theorems governing discrete-continuous fiber bundles,
  2-Jet exact sector collapse, false-vacuum trapping, and univalent splitting.
-/
import Flow.Core

namespace Flow.Bundle

structure Config where
  sector  : Nat
  modulus : Int
  deriving Repr, DecidableEq

def sqDist (x xStar : Int) : Nat :=
  Int.natAbs (x - xStar) * Int.natAbs (x - xStar)

theorem sqDist_eq_zero_iff (x xStar : Int) : sqDist x xStar = 0 ↔ x = xStar := by
  unfold sqDist
  constructor
  · intro h
    have habs : Int.natAbs (x - xStar) = 0 := by
      cases Nat.mul_eq_zero.mp h with
      | inl h1 => exact h1
      | inr h2 => exact h2
    have hsub : x - xStar = 0 := Int.natAbs_eq_zero.mp habs
    omega
  · intro h
    subst h
    simp

structure Landscape where
  vacuumFloor : Nat → Nat
  fiberMin    : Nat → Int
  curvature   : Nat → Nat
  curv_pos    : ∀ s, 0 < curvature s
  trueSector  : Nat
  true_zero   : vacuumFloor trueSector = 0

def energy (L : Landscape) (c : Config) : Nat :=
  L.vacuumFloor c.sector + L.curvature c.sector * sqDist c.modulus (L.fiberMin c.sector)

theorem energy_eq_zero_iff (L : Landscape) (c : Config) :
    energy L c = 0 ↔ L.vacuumFloor c.sector = 0 ∧ c.modulus = L.fiberMin c.sector := by
  unfold energy
  constructor
  · intro h
    have h0 : L.vacuumFloor c.sector = 0 := by omega
    have hprod : L.curvature c.sector * sqDist c.modulus (L.fiberMin c.sector) = 0 := by omega
    have _ := L.curv_pos c.sector
    have hsq : sqDist c.modulus (L.fiberMin c.sector) = 0 := by
      cases Nat.mul_eq_zero.mp hprod with
      | inl hc => omega
      | inr hs => exact hs
    exact ⟨h0, (sqDist_eq_zero_iff _ _).mp hsq⟩
  · rintro ⟨h0, hm⟩
    have hsq : sqDist c.modulus (L.fiberMin c.sector) = 0 := (sqDist_eq_zero_iff _ _).mpr hm
    rw [h0, hsq]
    simp

structure LocalGeometricFlow where
  stepModulus : Nat → Int → Int

def stepGeo (F : LocalGeometricFlow) (c : Config) : Config :=
  { sector := c.sector, modulus := F.stepModulus c.sector c.modulus }

def iterGeo (F : LocalGeometricFlow) : Nat → Config → Config
  | 0,     c => c
  | t + 1, c => iterGeo F t (stepGeo F c)

theorem iterGeo_preserves_sector (F : LocalGeometricFlow) (t : Nat) (c : Config) :
    (iterGeo F t c).sector = c.sector := by
  induction t generalizing c with
  | zero => rfl
  | succ t ih => simp [iterGeo, stepGeo, ih]

theorem pure_2jet_trapped_in_false_vacuum
    (L : Landscape) (F : LocalGeometricFlow) (c0 : Config)
    (hFalse : 0 < L.vacuumFloor c0.sector) (t : Nat) :
    L.vacuumFloor c0.sector ≤ energy L (iterGeo F t c0) ∧ 0 < energy L (iterGeo F t c0) := by
  have hsec := iterGeo_preserves_sector F t c0
  unfold energy
  rw [hsec]
  omega

def jet1 (L : Landscape) (s : Nat) (x : Int) : Int :=
  (2 * Int.ofNat (L.curvature s)) * (x - L.fiberMin s)

def jet2 (L : Landscape) (s : Nat) : Int :=
  2 * Int.ofNat (L.curvature s)

def step2JetDifferential (L : Landscape) (c : Config) : Config :=
  { sector := c.sector, modulus := c.modulus - (jet1 L c.sector c.modulus) / (jet2 L c.sector) }

theorem jet2_newton_step_exact (L : Landscape) (c : Config) :
    step2JetDifferential L c = { sector := c.sector, modulus := L.fiberMin c.sector } := by
  unfold step2JetDifferential jet1 jet2
  have _ : 0 < L.curvature c.sector := L.curv_pos c.sector
  have hne : (2 * Int.ofNat (L.curvature c.sector) : Int) ≠ 0 := by
    simp only [Int.ofNat_eq_natCast]
    omega
  have hdiv :
      ((2 * Int.ofNat (L.curvature c.sector)) * (c.modulus - L.fiberMin c.sector)) /
        (2 * Int.ofNat (L.curvature c.sector)) =
      c.modulus - L.fiberMin c.sector := by
    rw [Int.mul_comm]
    exact Int.mul_ediv_cancel (c.modulus - L.fiberMin c.sector) hne
  rw [hdiv]
  congr 1
  omega

def collapse2Jet (L : Landscape) (c : Config) : Config :=
  step2JetDifferential L c

theorem collapse2Jet_reaches_sector_floor (L : Landscape) (c : Config) :
    energy L (collapse2Jet L c) = L.vacuumFloor c.sector := by
  unfold collapse2Jet
  rw [jet2_newton_step_exact]
  unfold energy sqDist
  simp

def evalDirectSum (a b : Int) : Int := a + b

def evalSplitSum (a b : Int) : Int :=
  let mid := a + b
  mid

theorem univalent_split_ext (a b : Int) :
    evalSplitSum a b = evalDirectSum a b := rfl

theorem quad_double_factor (a h : Nat) :
    a * (2 * h) * (2 * h) = 4 * (a * h * h) := by
  calc
    a * (2 * h) * (2 * h)
        = (a * 2 * h) * (2 * h) := by simp [Nat.mul_assoc]
    _ = 2 * (a * h) * (2 * h) := by
        rw [Nat.mul_comm a 2, Nat.mul_assoc 2 a h]
    _ = (2 * (a * h) * 2) * h := by simp [Nat.mul_assoc]
    _ = (2 * 2 * (a * h)) * h := by
        rw [Nat.mul_comm (2 * (a * h)) 2, ← Nat.mul_assoc 2 2 (a * h)]
    _ = 4 * (a * h * h) := by simp [Nat.mul_assoc]

def quadUnsplitCost (rUnit len : Nat) : Nat :=
  rUnit * len * len

def quadMidpointSplitCost (rUnit halfLen bufDelay : Nat) : Nat :=
  2 * (rUnit * halfLen * halfLen) + bufDelay

theorem midpoint_split_halves_quadratic (rUnit h bufDelay : Nat)
    (hGain : bufDelay < 2 * (rUnit * h * h)) :
    quadMidpointSplitCost rUnit h bufDelay < quadUnsplitCost rUnit (2 * h) := by
  unfold quadMidpointSplitCost quadUnsplitCost
  rw [quad_double_factor rUnit h]
  omega

def sectorFloor2 : Nat → Nat
  | 0 => 420
  | _ => 0

def sectorCurv2 : Nat := 8

def sectorMin2 : Nat → Int
  | 0 => 100
  | _ => 256

def bundleEnergy2 (s : Nat) (x : Int) : Nat :=
  let dx := Int.natAbs (x - sectorMin2 s)
  sectorFloor2 s + sectorCurv2 * dx * dx

def coordFlowIter2 (step : Nat → Int → Int) : Nat → (Nat × Int) → (Nat × Int)
  | 0, c => c
  | t + 1, (s, x) => coordFlowIter2 step t (s, step s x)

theorem two_sector_false_vacuum_trap
    (step : Nat → Int → Int) (x0 : Int) (t : Nat) :
    420 ≤ bundleEnergy2 (coordFlowIter2 step t (0, x0)).1
                        (coordFlowIter2 step t (0, x0)).2 ∧
    0 < bundleEnergy2 (coordFlowIter2 step t (0, x0)).1
                      (coordFlowIter2 step t (0, x0)).2 := by
  have hSec : ∀ n y, (coordFlowIter2 step n (0, y)).1 = 0 := by
    intro n
    induction n with
    | zero => intro y; rfl
    | succ n ih => intro y; exact ih (step 0 y)
  have hF := hSec t x0
  unfold bundleEnergy2
  rw [hF]
  simp [sectorFloor2, sectorCurv2]
  omega

theorem unrelaxed_local_min_strictly_above_global :
    bundleEnergy2 0 100 < bundleEnergy2 1 100 ∧ bundleEnergy2 1 256 = 0 := by
  decide

end Flow.Bundle
