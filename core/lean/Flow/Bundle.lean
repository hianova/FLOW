/-
  Flow.Bundle — Symbiotic Fiber-Bundle Duality & Univalent Split Theorems
  =========================================================================
  Canonical single source of truth for discrete-continuous fiber bundles,
  2-Jet exact sector collapse, false-vacuum trapping, Wilsonian CLZ RG shells,
  univalent midpoint splitting, and global symbiotic convergence (zero IO, zero `sorryAx`).
-/
import Flow.Core

namespace Flow.Bundle

structure Config where
  sector  : Nat
  modulus : Int
  deriving Repr, DecidableEq

/-- Exact non-negative squared fiber distance `(x - xStar)^2`. -/
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

/-- Total bundle energy `E(s, x) = j0(s) + curvature(s) * (x - x*(s))^2`. -/
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

theorem collapse2Jet_modulus (L : Landscape) (c : Config) :
    (collapse2Jet L c).sector = c.sector ∧ (collapse2Jet L c).modulus = L.fiberMin c.sector := by
  unfold collapse2Jet
  rw [jet2_newton_step_exact]
  exact ⟨rfl, rfl⟩

theorem collapse2Jet_reaches_sector_floor (L : Landscape) (c : Config) :
    energy L (collapse2Jet L c) = L.vacuumFloor c.sector := by
  unfold collapse2Jet
  rw [jet2_newton_step_exact]
  unfold energy sqDist
  simp

theorem collapse2Jet_monotone (L : Landscape) (c : Config) :
    energy L (collapse2Jet L c) ≤ energy L c := by
  rw [collapse2Jet_reaches_sector_floor]
  unfold energy
  omega

/-! ## Univalent Splitting & Dyadic CLZ Partition of Unity -/

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

def dyadicShellSize (m : Nat) : Nat := 2 ^ (m + 1) - 2 ^ m

theorem clz_dyadic_shell_exact (m : Nat) : dyadicShellSize m = 2 ^ m := by
  unfold dyadicShellSize
  rw [Nat.pow_succ, Nat.mul_two, Nat.add_sub_cancel]

def sumDyadicShells : Nat → Nat
  | 0     => 0
  | w + 1 => sumDyadicShells w + dyadicShellSize w

theorem clz_rg_partition_of_unity (W : Nat) : 1 + sumDyadicShells W = 2 ^ W := by
  induction W with
  | zero => rfl
  | succ w ih =>
    unfold sumDyadicShells
    rw [clz_dyadic_shell_exact, Nat.pow_succ, Nat.mul_two, ← ih]
    omega

/-! ## Raw Tunneling Obstruction, Grid Compression & Global Symbiotic Convergence -/

def instantonTunnelRaw (nextSector : Nat) (rawModulus : Int) : Config :=
  { sector := nextSector, modulus := rawModulus }

theorem pure_searu_uv_modulus_obstruction
    (L : Landscape) (x : Int) (hOff : x ≠ L.fiberMin L.trueSector) :
    L.curvature L.trueSector ≤ energy L (instantonTunnelRaw L.trueSector x) ∧
    0 < energy L (instantonTunnelRaw L.trueSector x) := by
  unfold energy instantonTunnelRaw
  simp [L.true_zero]
  have hneq : sqDist x (L.fiberMin L.trueSector) ≠ 0 := by
    intro hz
    exact hOff ((sqDist_eq_zero_iff _ _).mp hz)
  have hge1 : 1 ≤ sqDist x (L.fiberMin L.trueSector) := by omega
  have hcurv := L.curv_pos L.trueSector
  have hmul : L.curvature L.trueSector * 1 ≤ L.curvature L.trueSector * sqDist x (L.fiberMin L.trueSector) :=
    Nat.mul_le_mul_left (L.curvature L.trueSector) hge1
  simp at hmul
  exact ⟨hmul, Nat.lt_of_lt_of_le hcurv hmul⟩

theorem unrelaxed_tunnel_masks_true_vacuum
    (L : Landscape) (s0 : Nat) (x : Int)
    (hStrain : L.vacuumFloor s0 < L.curvature L.trueSector * sqDist x (L.fiberMin L.trueSector)) :
    energy L { sector := s0, modulus := L.fiberMin s0 } <
    energy L (instantonTunnelRaw L.trueSector x) := by
  unfold energy instantonTunnelRaw sqDist at *
  simp [L.true_zero]
  exact hStrain

def countPureGroundHits (L : Landscape) (grid : List Int) : Nat :=
  (grid.filter (fun x => decide (energy L (instantonTunnelRaw L.trueSector x) = 0))).length

def countSymbioticGroundHits (L : Landscape) (grid : List Int) : Nat :=
  (grid.filter (fun x => decide (energy L (collapse2Jet L (instantonTunnelRaw L.trueSector x)) = 0))).length

theorem filter_eq_singleton_le_one (xs : List Int) (target : Int) (hNodup : xs.Nodup) :
    (xs.filter (fun x => decide (x = target))).length ≤ 1 := by
  induction xs with
  | nil => simp
  | cons a rest ih =>
    rw [List.nodup_cons] at hNodup
    rcases hNodup with ⟨haNotMem, hRestNodup⟩
    by_cases hEq : a = target
    · subst hEq
      have hRestZero : rest.filter (fun x => decide (x = a)) = [] := by
        rw [List.filter_eq_nil_iff]
        intro x hx
        simp only [decide_eq_true_eq]
        intro hxa
        subst hxa
        exact haNotMem hx
      simp [hRestZero]
    · simp [hEq, ih hRestNodup]

theorem pure_tunnel_ground_hits_le_one
    (L : Landscape) (grid : List Int) (hNodup : grid.Nodup) :
    countPureGroundHits L grid ≤ 1 := by
  unfold countPureGroundHits
  have hPredEq :
      (fun x => decide (energy L (instantonTunnelRaw L.trueSector x) = 0)) =
      (fun x => decide (x = L.fiberMin L.trueSector)) := by
    funext x
    by_cases hx : x = L.fiberMin L.trueSector
    · subst hx
      have hz : energy L (instantonTunnelRaw L.trueSector (L.fiberMin L.trueSector)) = 0 := by
        unfold energy instantonTunnelRaw sqDist
        simp [L.true_zero]
      simp [hz]
    · have hpos := (pure_searu_uv_modulus_obstruction L x hx).2
      have hne : energy L (instantonTunnelRaw L.trueSector x) ≠ 0 := by omega
      simp [hx, hne]
  rw [hPredEq]
  exact filter_eq_singleton_le_one grid (L.fiberMin L.trueSector) hNodup

theorem symbiotic_tunnel_ground_hits_all
    (L : Landscape) (grid : List Int) :
    countSymbioticGroundHits L grid = grid.length := by
  unfold countSymbioticGroundHits
  have hAll :
      grid.filter (fun x => decide (energy L (collapse2Jet L (instantonTunnelRaw L.trueSector x)) = 0)) =
      grid := by
    rw [List.filter_eq_self]
    intro x _
    simp only [decide_eq_true_eq]
    rw [collapse2Jet_reaches_sector_floor]
    exact L.true_zero
  rw [hAll]

def stepSymbiotic (L : Landscape) (proposedSector : Nat) (c : Config) : Config :=
  let candidate := collapse2Jet L { sector := proposedSector, modulus := c.modulus }
  if energy L candidate ≤ energy L c then candidate else c

def runSymbiotic (L : Landscape) : List Nat → Config → Config
  | [],            c => c
  | sProp :: rest, c => runSymbiotic L rest (stepSymbiotic L sProp c)

theorem stepSymbiotic_monotone (L : Landscape) (sProp : Nat) (c : Config) :
    energy L (stepSymbiotic L sProp c) ≤ energy L c := by
  dsimp [stepSymbiotic]
  by_cases h : energy L (collapse2Jet L { sector := sProp, modulus := c.modulus }) ≤ energy L c
  · simp [h]
  · simp [h]

theorem runSymbiotic_monotone (L : Landscape) (proposals : List Nat) (c : Config) :
    energy L (runSymbiotic L proposals c) ≤ energy L c := by
  induction proposals generalizing c with
  | nil => exact Nat.le_refl _
  | cons s rest ih =>
    unfold runSymbiotic
    exact Nat.le_trans (ih _) (stepSymbiotic_monotone L s c)

theorem runSymbiotic_absorbs_ground_state (L : Landscape) (proposals : List Nat) (c : Config)
    (hZero : energy L c = 0) :
    energy L (runSymbiotic L proposals c) = 0 := by
  have hle := runSymbiotic_monotone L proposals c
  omega

theorem symbiotic_duality_complete
    (L : Landscape) (proposals : List Nat) (c0 : Config)
    (hHit : L.trueSector ∈ proposals) :
    energy L (runSymbiotic L proposals c0) = 0 ∧
    (runSymbiotic L proposals c0).modulus = L.fiberMin (runSymbiotic L proposals c0).sector := by
  induction proposals generalizing c0 with
  | nil => contradiction
  | cons s rest ih =>
    unfold runSymbiotic
    cases List.mem_cons.mp hHit with
    | inl hEq =>
      subst hEq
      have hStepZero : energy L (stepSymbiotic L L.trueSector c0) = 0 := by
        dsimp [stepSymbiotic]
        have hCandZero : energy L (collapse2Jet L { sector := L.trueSector, modulus := c0.modulus }) = 0 := by
          rw [collapse2Jet_reaches_sector_floor, L.true_zero]
        simp [hCandZero]
      have hFinalZero := runSymbiotic_absorbs_ground_state L rest _ hStepZero
      have hChar := (energy_eq_zero_iff L _).mp hFinalZero
      exact ⟨hFinalZero, hChar.2⟩
    | inr hMem =>
      exact ih (stepSymbiotic L s c0) hMem

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
