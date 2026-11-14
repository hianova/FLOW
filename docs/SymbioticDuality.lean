/-
  SymbioticDuality.lean — Machine-Checked Theory of 2-Jet (String) × SEARU (QFT) Duality
  ======================================================================================
  Authors : Kuang-Ta Lin & Antigravity (Google DeepMind)
  License : MIT (FLOW v7.0.1 Formal Verification Companion)
  Target  : Pure Lean 4 (zero external dependencies, zero `sorry`, `-DwarningAsError=true`)

  Summary
  -------
  Formalizes frustrated optimization and physical vacuum selection on a non-trivial
  discrete-continuous Fiber Bundle `π : Config → Nat`, `Config ≅ Sector × Modulus`:
    - Base `sector : Nat` models disconnected topological / flux sectors (`π₀`, `KanLock`,
      NP boolean/combinatorial basins, or Calabi–Yau flux vacua).
    - Fiber `modulus : Int` models high-precision fixed-point continuous moduli (`Q16.16` /
      64-bit state coordinates inside a local `Flow.lean` chart).

  Machine-Checked Main Results:
    1. `energy_eq_zero_iff`: Exact characterization of global ground states.
    2. `pure_2jet_trapped_in_false_vacuum` (Theorem 1 — String Landscape No-Go):
       Every smooth local geometric flow preserves topological sector `π₀` and is trapped
       forever (`∀ t, E(t) ≥ j₀(s₀) > 0`) in any metastable false vacuum.
    3. `jet2_newton_step_exact` & `collapse2Jet_reaches_sector_floor` (Theorem 1B):
       Within any fixed sector, the exact `2-Jet` differential operator `x ↦ x - j₁(x) / j₂`
       collapses arbitrary continuous modulus displacement to `x*(s)` in a single step.
    4. `clz_dyadic_shell_exact` & `clz_rg_partition_of_unity` (Theorem 3 — Wilsonian RG):
       Hardware `CLZ` partitions `[0, 2^W)` into `{0}` plus `W` dyadic momentum shells
       `[2^m, 2^{m+1})` of exact measure `2^m`, yielding scale-free Zipf/Pareto laws with
       zero floating-point operations (`1 + ∑_{m=0}^{W-1} 2^m = 2^W`).
    5. `pure_searu_uv_modulus_obstruction` & `unrelaxed_tunnel_masks_true_vacuum` (Theorem 2):
       Pure non-perturbative sector tunneling without `2-Jet` fiber relaxation leaves
       strictly positive UV modulus strain on `2^W - 1` of `2^W` fiber states and causes
       greedy acceptance rules to reject the true topological vacuum (`False Rejection`).
    6. `pure_tunnel_ground_hits_le_one` vs `symbiotic_tunnel_ground_hits_all` (Theorem 5):
       Exact `M : 1` (`2^W : 1`) phase-space volume separation on any duplicate-free
       discrete modulus grid of cardinality `M`.
    7. `stepSymbiotic_monotone`, `runSymbiotic_absorbs_ground_state`, &
       `symbiotic_duality_complete` (Theorem 4 — Global Symbiotic Completeness):
       The bilevel composition `SEARU ∘ FLOW 2-Jet` is strictly Lyapunov non-increasing,
       absorbs into `E = 0`, and reaches the exact global ground state `(s*, x*(s*))`
       whenever the proposal stream visits a zero-floor sector.
-/

namespace SymbioticDuality

/-!
  ## 1. Non-Trivial Fiber-Bundle Landscape (`π : Config → Sector`)
-/

/-- Total space configuration `c = (sector, modulus)` of the discrete-continuous bundle. -/
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

/--
  Frustrated Multi-Vacuum Fiber-Bundle Landscape:
  - `vacuumFloor s` (`j₀(s)`): topological zero-point energy / cosmological constant of sector `s`.
  - `fiberMin s` (`x*(s)`): continuous modulus equilibrium inside sector `s`.
  - `curvature s` (`½ j₂(s) > 0`): positive local Hessian stiffness along the fiber of sector `s`.
  - `trueSector` (`s*`): witness of a true topological ground-state sector with `j₀(s*) = 0`.
-/
structure Landscape where
  vacuumFloor : Nat → Nat
  fiberMin    : Nat → Int
  curvature   : Nat → Nat
  curv_pos    : ∀ s, 0 < curvature s
  trueSector  : Nat
  true_zero   : vacuumFloor trueSector = 0

/-- Total bundle energy `E(s, x) = j₀(s) + curvature(s) · (x - x*(s))²`. -/
def energy (L : Landscape) (c : Config) : Nat :=
  L.vacuumFloor c.sector + L.curvature c.sector * sqDist c.modulus (L.fiberMin c.sector)

/-- Exact zero-energy characterization: `E(s, x) = 0 ↔ j₀(s) = 0 ∧ x = x*(s)`. -/
theorem energy_eq_zero_iff (L : Landscape) (c : Config) :
    energy L c = 0 ↔ L.vacuumFloor c.sector = 0 ∧ c.modulus = L.fiberMin c.sector := by
  unfold energy
  constructor
  · intro h
    have h0 : L.vacuumFloor c.sector = 0 := by omega
    have hprod : L.curvature c.sector * sqDist c.modulus (L.fiberMin c.sector) = 0 := by omega
    have hpos := L.curv_pos c.sector
    have hsq : sqDist c.modulus (L.fiberMin c.sector) = 0 := by
      cases Nat.mul_eq_zero.mp hprod with
      | inl hc => omega
      | inr hs => exact hs
    exact ⟨h0, (sqDist_eq_zero_iff _ _).mp hsq⟩
  · rintro ⟨h0, hm⟩
    have hsq : sqDist c.modulus (L.fiberMin c.sector) = 0 := (sqDist_eq_zero_iff _ _).mpr hm
    rw [h0, hsq]
    simp

/-!
  ## 2. Operator I: Smooth `2-Jet` Worldsheet / Geometric Flow (`Flow.lean` / `flow.h`)
-/

/-- Any smooth local worldsheet / geometric flow preserves the topological sector `π₀`. -/
structure LocalGeometricFlow where
  stepModulus : Nat → Int → Int

def stepGeo (F : LocalGeometricFlow) (c : Config) : Config :=
  { sector := c.sector, modulus := F.stepModulus c.sector c.modulus }

def iterGeo (F : LocalGeometricFlow) : Nat → Config → Config
  | 0,     c => c
  | t + 1, c => iterGeo F t (stepGeo F c)

/-- Homotopy invariance of the base projection `π(iterGeo F t c) = π(c)`. -/
theorem iterGeo_preserves_sector (F : LocalGeometricFlow) (t : Nat) (c : Config) :
    (iterGeo F t c).sector = c.sector := by
  induction t generalizing c with
  | zero => rfl
  | succ t ih =>
    simp [iterGeo, stepGeo, ih]

/--
  **THEOREM 1 (String Landscape / Topological Trapping No-Go for Pure `2-Jet` Flow)**:
  For ANY local geometric flow `F` and ANY iteration count `t ∈ ℕ`, if `c0` lies in a
  metastable false vacuum (`vacuumFloor c0.sector > 0`), its energy remains bounded below
  by `vacuumFloor c0.sector > 0` forever. Smooth geometry alone cannot cross `π₀` barriers.
-/
theorem pure_2jet_trapped_in_false_vacuum
    (L : Landscape) (F : LocalGeometricFlow) (c0 : Config)
    (hFalse : 0 < L.vacuumFloor c0.sector) (t : Nat) :
    L.vacuumFloor c0.sector ≤ energy L (iterGeo F t c0) ∧ 0 < energy L (iterGeo F t c0) := by
  have hsec := iterGeo_preserves_sector F t c0
  unfold energy
  rw [hsec]
  omega

/-- First jet (gradient) `j₁(s, x) = 2 · curvature(s) · (x - x*(s))` along the fiber. -/
def jet1 (L : Landscape) (s : Nat) (x : Int) : Int :=
  (2 * Int.ofNat (L.curvature s)) * (x - L.fiberMin s)

/-- Second jet (Hessian) `j₂(s) = 2 · curvature(s) > 0` along the fiber. -/
def jet2 (L : Landscape) (s : Nat) : Int :=
  2 * Int.ofNat (L.curvature s)

/-- Explicit `2-Jet` Newton/Citardauq differential step `x ↦ x - j₁(s, x) / j₂(s)`. -/
def step2JetDifferential (L : Landscape) (c : Config) : Config :=
  { sector := c.sector, modulus := c.modulus - (jet1 L c.sector c.modulus) / (jet2 L c.sector) }

/--
  **THEOREM 1B (Algebraic 1-Step Exactness of the `2-Jet` Differential Quotient)**:
  Because `j₂(s) = 2 · curvature(s) > 0` divides `j₁(s, x) = j₂(s) · (x - x*(s))` exactly,
  the `2-Jet` differential update `x - j₁ / j₂` lands on `fiberMin s` in **one exact step**
  from ANY initial continuous coordinate `x ∈ ℤ`.
-/
theorem jet2_newton_step_exact (L : Landscape) (c : Config) :
    step2JetDifferential L c = { sector := c.sector, modulus := L.fiberMin c.sector } := by
  unfold step2JetDifferential jet1 jet2
  have hpos : 0 < L.curvature c.sector := L.curv_pos c.sector
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

/-- Canonical `2-Jet` fiber projector (proven equal to `step2JetDifferential` in Theorem 1B). -/
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

/-!
  ## 3. Operator II: `SEARU` Quantum-Field Tunneling & Wilsonian `CLZ` RG Shells (`searu.h`)
-/

/-- Exact dyadic momentum-shell cardinality `|[2^m, 2^{m+1})|`. -/
def dyadicShellSize (m : Nat) : Nat := 2 ^ (m + 1) - 2 ^ m

/--
  **THEOREM 3 (Wilsonian RG / `CLZ` Dyadic Partition of Unity)**:
  1. Each `CLZ` octave `m` has exact dyadic measure `2^m` (`P = 2^{-(W-m)}`).
  2. Summing the `W` shells `m = 0 .. W - 1` plus the infrared zero mode `{0}` partitions
     the entire `W`-bit register space `[0, 2^W)` without FPU, tables, or division.
-/
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

/-- Pure curvature-blind instanton transition `s → nextSector` at unrelaxed modulus `rawModulus`. -/
def instantonTunnelRaw (nextSector : Nat) (rawModulus : Int) : Config :=
  { sector := nextSector, modulus := rawModulus }

/--
  **THEOREM 2 (Pure Instanton UV Moduli Obstruction)**:
  Whenever a pure sector jump lands in `trueSector` (`j₀ = 0`) at ANY off-minimum coordinate
  `x ≠ fiberMin trueSector`, residual ultraviolet fiber energy satisfies
  `energy ≥ curvature(trueSector) > 0`.
-/
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

/--
  **COROLLARY 2B (False Rejection of True Vacuum Under Unrelaxed Tunneling)**:
  If `c0` sits at the relaxed floor of a metastable false vacuum `s0` (`E = j₀(s0) > 0`), and
  an instanton tunnels to `trueSector` (`j₀ = 0`) with unrelaxed fiber strain
  `curvature(trueSector) · (x - x*)² > j₀(s0)`, the unrelaxed true-sector state has STRICTLY
  HIGHER energy than the false vacuum — causing any monotone/greedy acceptor to reject `trueSector`!
-/
theorem unrelaxed_tunnel_masks_true_vacuum
    (L : Landscape) (s0 : Nat) (x : Int)
    (hStrain : L.vacuumFloor s0 < L.curvature L.trueSector * sqDist x (L.fiberMin L.trueSector)) :
    energy L { sector := s0, modulus := L.fiberMin s0 } <
    energy L (instantonTunnelRaw L.trueSector x) := by
  unfold energy instantonTunnelRaw sqDist at *
  simp [L.true_zero]
  exact hStrain

/-!
  ## 4. Phase-Space Volume Separation (`2^W : 1`) on Finite Fixed-Point Grids
-/

/-- Count how many fiber coordinates in `grid` achieve `E = 0` under pure unrelaxed tunneling. -/
def countPureGroundHits (L : Landscape) (grid : List Int) : Nat :=
  (grid.filter (fun x => decide (energy L (instantonTunnelRaw L.trueSector x) = 0))).length

/-- Count how many fiber coordinates in `grid` achieve `E = 0` under `2-Jet`-relaxed tunneling. -/
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

/--
  **THEOREM 5A (Pure Tunneling Hits Ground State on at Most 1 Point of Any `Nodup` Fiber Grid)**:
  Across an entire `W`-bit fixed-point fiber (`M = 2^W` distinct coordinates, e.g. `2^32` in `Q16.16`),
  at most **1** coordinate yields `E = 0` without `2-Jet` relaxation (`≥ M - 1` states fail).
-/
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

/--
  **THEOREM 5B (Symbiotic `2-Jet ∘ Tunnel` Hits Ground State on ALL `M` Points of Any Fiber Grid)**:
  With `2-Jet` fiber projection, **every single coordinate** (`M` out of `M`, i.e. `2^W` out of `2^W`)
  collapses to `E = 0` upon entering `trueSector` — proving an exact `M : 1` (`2^W : 1`) phase-space
  compression!
-/
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

/-!
  ## 5. Bilevel Symbiotic Unification (`SEARU` Tunneling ∘ `FLOW 2-Jet` Collapse + `@swap`)
-/

/-- One symbiotic bilevel step: tunnel to `proposedSector`, apply `collapse2Jet`, elitist `@swap`. -/
def stepSymbiotic (L : Landscape) (proposedSector : Nat) (c : Config) : Config :=
  let candidate := collapse2Jet L { sector := proposedSector, modulus := c.modulus }
  if energy L candidate ≤ energy L c then candidate else c

/-- Run symbiotic co-evolution over a finite stream of `SEARU` topological sector proposals. -/
def runSymbiotic (L : Landscape) : List Nat → Config → Config
  | [],            c => c
  | sProp :: rest, c => runSymbiotic L rest (stepSymbiotic L sProp c)

/-- Exact single-step Lyapunov monotonicity (`E(t+1) ≤ E(t)`). -/
theorem stepSymbiotic_monotone (L : Landscape) (sProp : Nat) (c : Config) :
    energy L (stepSymbiotic L sProp c) ≤ energy L c := by
  dsimp [stepSymbiotic]
  by_cases h : energy L (collapse2Jet L { sector := sProp, modulus := c.modulus }) ≤ energy L c
  · simp [h]
  · simp [h]

/-- Multi-step trajectory Lyapunov monotonicity (`E(T) ≤ E(0)`). -/
theorem runSymbiotic_monotone (L : Landscape) (proposals : List Nat) (c : Config) :
    energy L (runSymbiotic L proposals c) ≤ energy L c := by
  induction proposals generalizing c with
  | nil => exact Nat.le_refl _
  | cons s rest ih =>
    unfold runSymbiotic
    exact Nat.le_trans (ih _) (stepSymbiotic_monotone L s c)

/-- Ground-state absorption (`E = 0` is an invariant absorbing manifold under `@swap`). -/
theorem runSymbiotic_absorbs_ground_state (L : Landscape) (proposals : List Nat) (c : Config)
    (hZero : energy L c = 0) :
    energy L (runSymbiotic L proposals c) = 0 := by
  have hle := runSymbiotic_monotone L proposals c
  omega

/--
  **THEOREM 4 (Global Symbiotic Completeness)**:
  From ANY initial configuration `c0` (trapped in any metastable false vacuum `s0` AND displaced
  by any continuous modulus offset `c0.modulus`), if `SEARU`'s non-perturbative proposal stream
  visits `L.trueSector` at least once, `runSymbiotic` guaranteedly converges to an exact global
  ground state with `energy = 0` AND exact continuous equilibrium `modulus = fiberMin sector`.
-/
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

/-!
  ## 6. Executable `Q16.16` Numerical Witness (`lean -DwarningAsError=true --run`)
-/

def demoLandscape : Landscape where
  vacuumFloor := fun s => if s == 7 then 0 else 42 + (s % 5) * 10
  fiberMin    := fun s => Int.ofNat s * 65536 - 131072
  curvature   := fun s => 3 + (s % 4)
  curv_pos    := by intro s; omega
  trueSector  := 7
  true_zero   := by decide

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
