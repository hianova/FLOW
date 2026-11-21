/-
  Flow.LatticeBootstrap — Certified Lattice Schwinger-Dyson Bootstrap & Exact Renormalization Group
  ==================================================================================================
  Pure Lean 4 (Init-only, zero IO) formalization unifying non-perturbative lattice loop equations,
  exact integer-polynomial dual SOS certificates, 4D SU(2) quaternionic Gram/SD bounds, and exact
  block-spin / Migdal-Kadanoff renormalization group theorems (`Theorems B1-B5`):
    1. Exact integer-scaled Schwinger-Dyson moment reductions (2D U(1), 2D SU(2)), dual SOS
       interval certification (`Flow.Interval`), and exact Gross-Witten-Wadia critical point
       necessity at lambda_c = 2 (Theorem B1);
    2. Exact 4D SU(2) quaternionic field-strength / Schwinger-Dyson sum-of-squares, 4D non-perturbative
       Cauchy-Schwarz upper bound `2*beta*u_P^2 + u_P <= 2*beta`, and OS variance bound (Theorem B2);
    3. Exact 4D staple reflection positivity (`W_{1x2} >= u_P^2`, `W_{chair} >= u_P^2`), 7-link
       backtracking Gram bounds, and resultant norm-squared identity (Theorem B3);
    4. Exact 1D/2D block-spin character decimation, 4D Kadanoff `b^2 = 4` bond-moving Jensen
       inequalities, asymptotic-freedom confinement basin entry, and Coulomb fixed rays (Theorem B4);
    5. Concrete machine-checked dual SOS interval certificates for 2D SU(2) (`beta = 2, 5/2`),
       2D U(1) (`beta = 2`), and 4D SU(2) (`beta = 5/2`) (Theorem B5).
-/
import Flow.ConstructiveQFT
import Flow.Interval

namespace Flow.LatticeBootstrap

open Flow.GaugeBundle
open Flow.YangMillsBundle
open Flow.ConstructiveQFT
open Flow.Interval

/-! ## Helpers: Local Sum & Inequality Combinators -/

private theorem int_sq_nonneg (z : Int) : 0 ≤ z * z := by
  by_cases hz : 0 ≤ z
  · exact Int.mul_nonneg hz hz
  · have hneg : 0 ≤ -z := by omega
    have hsq : (-z) * (-z) = z * z := Int.neg_mul_neg z z
    rw [← hsq]
    exact Int.mul_nonneg hneg hneg

private def absDiff (a b : Nat) : Nat :=
  if a ≤ b then b - a else a - b

private theorem sumPrefix_zero_fn {N : Nat} (k : Nat) :
    sumPrefix (fun _ : Fin N => (0 : Int)) k = 0 := by
  induction k with
  | zero => simp [sumPrefix]
  | succ k ih =>
    simp only [sumPrefix, ih]
    split <;> omega

private theorem sumFin_zero_fn (N : Nat) :
    sumFin N (fun _ => (0 : Int)) = 0 :=
  sumPrefix_zero_fn N

private theorem sumFin_sub (N : Nat) (f g : Fin N → Int) :
    sumFin N (fun i => f i - g i) = sumFin N f - sumFin N g := by
  have h1 : (fun i => f i - g i) = (fun i => f i + -(g i)) := by
    funext i; omega
  rw [h1, sumFin_add, sumFin_neg]
  omega

private theorem sumPrefix_indicator (N k0 : Nat) (hk0 : k0 < N) (c : Int) :
    ∀ k : Nat, k ≤ N →
      sumPrefix (fun j : Fin N => if j.val = k0 then c else 0) k =
        if k0 < k then c else 0
  | 0, _ => by simp [sumPrefix]
  | k + 1, hk => by
    have hkLe : k ≤ N := Nat.le_of_lt hk
    have hkLt : k < N := hk
    have ih := sumPrefix_indicator N k0 hk0 c k hkLe
    simp only [sumPrefix, hkLt, ↓reduceDIte, ih]
    by_cases h1 : k = k0
    · subst h1
      simp only [↓reduceIte]
      have hnot : ¬(k < k) := Nat.lt_irrefl k
      have hlt : k < k + 1 := Nat.lt_succ_self k
      simp [hnot, hlt]
    · have hEq : (k0 < k + 1) ↔ (k0 < k) := by omega
      simp [h1, hEq]

private theorem sumFin_indicator {N : Nat} (k0 : Nat) (hk0 : k0 < N) (c : Int) :
    sumFin N (fun j : Fin N => if j.val = k0 then c else 0) = c := by
  unfold sumFin
  rw [sumPrefix_indicator N k0 hk0 c N (Nat.le_refl N)]
  simp [hk0]

private theorem sumFin_cauchy_const_right (N : Nat) (f : Fin N → Int) :
    sumFin N f * sumFin N f ≤ (N : Int) * sumFin N (fun i => f i * f i) := by
  have h := weighted_cauchy_schwarz (fun _ : Fin N => (1 : Int)) (fun _ => 1) f (fun _ => by omega)
  have h1 : (fun k : Fin N => (1 : Int) * (1 * f k)) = f := by
    funext k; rw [Int.one_mul, Int.one_mul]
  have h2 : (fun k : Fin N => (1 : Int) * (1 * 1)) = (fun _ : Fin N => (1 : Int)) := by
    funext k; omega
  have h3 : (fun k : Fin N => (1 : Int) * (f k * f k)) = (fun k : Fin N => f k * f k) := by
    funext k; rw [Int.one_mul]
  rw [h1, h2, h3, sumFin_const, Int.mul_one] at h
  exact h

private theorem mul_le_cancel_left_pos {a x y : Int} (ha : 0 < a) (h : a * x ≤ a * y) : x ≤ y := by
  by_cases hxy : x ≤ y
  · exact hxy
  · have hNeg := Int.mul_lt_mul_of_pos_left (by omega : y < x) ha
    omega

private theorem mul_lt_zero_cancel_pos {a x : Int} (ha : 0 < a) (h : a * x < 0) : x < 0 := by
  by_cases hx : 0 ≤ x
  · have hNonneg := Int.mul_nonneg (Int.le_of_lt ha) hx
    omega
  · omega

/-! ## Section 1: Exact Schwinger-Dyson Reduction, Dual SOS Bounds & GWW Transition (Theorem B1) -/

/-- Exact integer-scaled 2D U(1) Schwinger-Dyson constant coefficient `a_n`:
    for `beta = p/q`, `p^n * W_n = u1SdCoeffA p q n * W_0 + u1SdCoeffB p q n * W_1`. -/
def u1SdCoeffA (p q : Int) : Nat → Int
  | 0     => 1
  | 1     => 0
  | n + 2 => p * p * u1SdCoeffA p q n - 2 * ((n : Int) + 1) * q * u1SdCoeffA p q (n + 1)

/-- Exact integer-scaled 2D U(1) Schwinger-Dyson linear coefficient `b_n`. -/
def u1SdCoeffB (p q : Int) : Nat → Int
  | 0     => 0
  | 1     => p
  | n + 2 => p * p * u1SdCoeffB p q n - 2 * ((n : Int) + 1) * q * u1SdCoeffB p q (n + 1)

private theorem u1SdCoeffA_step (p q : Int) (n : Nat) :
    u1SdCoeffA p q (n + 2) = p * p * u1SdCoeffA p q n - 2 * ((n : Int) + 1) * q * u1SdCoeffA p q (n + 1) := rfl

private theorem u1SdCoeffB_step (p q : Int) (n : Nat) :
    u1SdCoeffB p q (n + 2) = p * p * u1SdCoeffB p q n - 2 * ((n : Int) + 1) * q * u1SdCoeffB p q (n + 1) := rfl

/-- Exact strong-induction 2D U(1) Schwinger-Dyson affine reduction for arbitrary moment
    sequences satisfying `p * (W_n - W_{n+2}) = 2 * (n + 1) * q * W_{n+1}`. -/
theorem u1_2d_sd_affine_reduction (p q D u : Int) (W : Nat → Int)
    (h0 : W 0 = D) (h1 : W 1 = u)
    (hSd : ∀ n : Nat, p * (W n - W (n + 2)) = 2 * ((n : Int) + 1) * q * W (n + 1)) :
    ∀ n : Nat, intPow p n * W n = u1SdCoeffA p q n * D + u1SdCoeffB p q n * u := by
  intro n
  have hPair :
      ∀ k : Nat,
        intPow p k * W k = u1SdCoeffA p q k * D + u1SdCoeffB p q k * u ∧
        intPow p (k + 1) * W (k + 1) = u1SdCoeffA p q (k + 1) * D + u1SdCoeffB p q (k + 1) * u := by
    intro k
    induction k with
    | zero =>
      constructor
      · simp [intPow, u1SdCoeffA, u1SdCoeffB, h0]
      · simp [intPow, u1SdCoeffA, u1SdCoeffB, h1, Int.mul_comm]
    | succ m ih =>
      rcases ih with ⟨ih0, ih1⟩
      constructor
      · exact ih1
      · have hStep := hSd m
        have hS : p * W (m + 2) = p * W m - 2 * ((m : Int) + 1) * q * W (m + 1) := by
          have hSub : p * W m - p * W (m + 2) = p * (W m - W (m + 2)) := (Int.mul_sub p (W m) (W (m + 2))).symm
          omega
        have hLHS : intPow p (m + 2) * W (m + 2) = intPow p (m + 1) * (p * W (m + 2)) := by
          calc intPow p (m + 2) * W (m + 2)
              = (p * intPow p (m + 1)) * W (m + 2) := rfl
            _ = intPow p (m + 1) * (p * W (m + 2)) := by ac_rfl
        have hPm : intPow p (m + 1) = p * intPow p m := rfl
        have hRHS : intPow p (m + 1) * (p * W m - 2 * ((m : Int) + 1) * q * W (m + 1)) =
            p * p * (intPow p m * W m) - 2 * ((m : Int) + 1) * q * (intPow p (m + 1) * W (m + 1)) := by
          rw [Int.mul_sub]
          have hT1 : intPow p (m + 1) * (p * W m) = p * p * (intPow p m * W m) := by
            rw [hPm]; ac_rfl
          have hT2 : intPow p (m + 1) * (2 * ((m : Int) + 1) * q * W (m + 1)) =
              2 * ((m : Int) + 1) * q * (intPow p (m + 1) * W (m + 1)) := by
            ac_rfl
          rw [hT1, hT2]
        rw [hLHS, hS, hRHS, ih0, ih1, u1SdCoeffA_step, u1SdCoeffB_step]
        grind
  exact (hPair n).1

/-- Exact integer-scaled 2D SU(2) Schwinger-Dyson constant coefficient `a_n`:
    for `beta = p/q`, `p^n * M_n = su2SdCoeffA p q n * M_0 + su2SdCoeffB p q n * M_1`. -/
def su2SdCoeffA (p q : Int) : Nat → Int
  | 0     => 1
  | 1     => 0
  | 2     => p * p
  | k + 3 => p * p * su2SdCoeffA p q (k + 1) + q * ((k : Int) + 1) * (p * p) * su2SdCoeffA p q k -
              q * ((k : Int) + 4) * su2SdCoeffA p q (k + 2)

/-- Exact integer-scaled 2D SU(2) Schwinger-Dyson linear coefficient `b_n`. -/
def su2SdCoeffB (p q : Int) : Nat → Int
  | 0     => 0
  | 1     => p
  | 2     => -(3 * p * q)
  | k + 3 => p * p * su2SdCoeffB p q (k + 1) + q * ((k : Int) + 1) * (p * p) * su2SdCoeffB p q k -
              q * ((k : Int) + 4) * su2SdCoeffB p q (k + 2)

private theorem su2SdCoeffA_step (p q : Int) (k : Nat) :
    su2SdCoeffA p q (k + 3) = p * p * su2SdCoeffA p q (k + 1) + q * ((k : Int) + 1) * (p * p) * su2SdCoeffA p q k -
      q * ((k : Int) + 4) * su2SdCoeffA p q (k + 2) := rfl

private theorem su2SdCoeffB_step (p q : Int) (k : Nat) :
    su2SdCoeffB p q (k + 3) = p * p * su2SdCoeffB p q (k + 1) + q * ((k : Int) + 1) * (p * p) * su2SdCoeffB p q k -
      q * ((k : Int) + 4) * su2SdCoeffB p q (k + 2) := rfl

/-- Exact 2D SU(2) Schwinger-Dyson affine moment reduction over pure `Int`:
    every moment `M_n` reduces symbolically to a linear function of `(M_0, M_1) = (D, u)`
    with exact `p^n` denominator scaling. -/
theorem su2_2d_sd_affine_reduction (p q D u : Int) (M : Nat → Int)
    (h0 : M 0 = D) (h1 : M 1 = u)
    (hBase : p * (M 0 - M 2) = 3 * q * M 1)
    (hSd : ∀ k : Nat, p * (M (k + 1) - M (k + 3)) =
        q * (((k : Int) + 4) * M (k + 2) - ((k : Int) + 1) * M k)) :
    ∀ n : Nat, intPow p n * M n = su2SdCoeffA p q n * D + su2SdCoeffB p q n * u := by
  intro n
  have hTriple :
      ∀ k : Nat,
        intPow p k * M k = su2SdCoeffA p q k * D + su2SdCoeffB p q k * u ∧
        intPow p (k + 1) * M (k + 1) = su2SdCoeffA p q (k + 1) * D + su2SdCoeffB p q (k + 1) * u ∧
        intPow p (k + 2) * M (k + 2) = su2SdCoeffA p q (k + 2) * D + su2SdCoeffB p q (k + 2) * u := by
    intro k
    induction k with
    | zero =>
      refine ⟨?_, ?_, ?_⟩
      · simp [intPow, su2SdCoeffA, su2SdCoeffB, h0]
      · simp [intPow, su2SdCoeffA, su2SdCoeffB, h1, Int.mul_comm]
      · have hM2 : p * M 2 = p * D - 3 * q * u := by
          have hSub : p * M 0 - p * M 2 = p * (M 0 - M 2) := (Int.mul_sub p (M 0) (M 2)).symm
          rw [h0] at hBase hSub
          rw [h1] at hBase
          omega
        have hP2 : intPow p 2 * M 2 = p * (p * M 2) := by
          simp [intPow]; ac_rfl
        rw [hP2, hM2]
        unfold su2SdCoeffA su2SdCoeffB
        rw [Int.mul_sub]
        have hL : p * (p * D) = p * p * D := by ac_rfl
        have hR : p * (3 * q * u) = 3 * p * q * u := by ac_rfl
        rw [hL, hR]
        have hNeg : -(3 * p * q) * u = -(3 * p * q * u) := Int.neg_mul (3 * p * q) u
        omega
    | succ m ih =>
      rcases ih with ⟨ih0, ih1, ih2⟩
      refine ⟨ih1, ih2, ?_⟩
      have hStep := hSd m
      have hS3 : p * M (m + 3) =
          p * M (m + 1) + q * ((m : Int) + 1) * M m - q * ((m : Int) + 4) * M (m + 2) := by
        have hSub : p * M (m + 1) - p * M (m + 3) = p * (M (m + 1) - M (m + 3)) :=
          (Int.mul_sub p (M (m + 1)) (M (m + 3))).symm
        have hSubR : q * (((m : Int) + 4) * M (m + 2) - ((m : Int) + 1) * M m) =
            q * ((m : Int) + 4) * M (m + 2) - q * ((m : Int) + 1) * M m := by
          rw [Int.mul_sub, Int.mul_assoc, Int.mul_assoc]
        omega
      have hLHS : intPow p (m + 3) * M (m + 3) = intPow p (m + 2) * (p * M (m + 3)) := by
        calc intPow p (m + 3) * M (m + 3)
            = (p * intPow p (m + 2)) * M (m + 3) := rfl
          _ = intPow p (m + 2) * (p * M (m + 3)) := by ac_rfl
      have hPow21 : intPow p (m + 2) = p * intPow p (m + 1) := rfl
      have hPow2m : intPow p (m + 2) = p * (p * intPow p m) := rfl
      have hExpand : intPow p (m + 2) * (p * M (m + 1) + q * ((m : Int) + 1) * M m - q * ((m : Int) + 4) * M (m + 2)) =
          p * p * (intPow p (m + 1) * M (m + 1)) +
          q * ((m : Int) + 1) * (p * p) * (intPow p m * M m) -
          q * ((m : Int) + 4) * (intPow p (m + 2) * M (m + 2)) := by
        rw [Int.mul_sub, Int.mul_add]
        have hT1 : intPow p (m + 2) * (p * M (m + 1)) = p * p * (intPow p (m + 1) * M (m + 1)) := by
          rw [hPow21]; ac_rfl
        have hT2 : intPow p (m + 2) * (q * ((m : Int) + 1) * M m) =
            q * ((m : Int) + 1) * (p * p) * (intPow p m * M m) := by
          rw [hPow2m]; ac_rfl
        have hT3 : intPow p (m + 2) * (q * ((m : Int) + 4) * M (m + 2)) =
            q * ((m : Int) + 4) * (intPow p (m + 2) * M (m + 2)) := by
          ac_rfl
        rw [hT1, hT2, hT3]
      rw [hLHS, hS3, hExpand, ih0, ih1, ih2, su2SdCoeffA_step, su2SdCoeffB_step]
      grind
  exact (hTriple n).1

/-- Dual SOS positivity lower bound: `0 <= A * D + B * u` with `0 < B` forces `(-A) * D <= B * u`,
    and any integer lower witness `lo * B <= (-A) * D` certifies `lo <= u`. -/
theorem sd_dual_sos_lower_bound (A B D u lo : Int)
    (hSos : 0 ≤ A * D + B * u) (hB : 0 < B) (hWit : lo * B ≤ (-A) * D) :
    (-A) * D ≤ B * u ∧ lo ≤ u := by
  have hNeg : (-A) * D = -(A * D) := Int.neg_mul A D
  have h1 : (-A) * D ≤ B * u := by rw [hNeg]; omega
  have h2 : B * lo ≤ B * u := by
    have hCom : B * lo = lo * B := Int.mul_comm B lo
    omega
  exact ⟨h1, mul_le_cancel_left_pos hB h2⟩

/-- Dual SOS positivity upper bound: `0 <= A * D + B * u` with `B < 0` forces `(-B) * u <= A * D`,
    and any integer upper witness `A * D <= (-B) * hi` certifies `u <= hi`. -/
theorem sd_dual_sos_upper_bound (A B D u hi : Int)
    (hSos : 0 ≤ A * D + B * u) (hB : B < 0) (hWit : A * D ≤ (-B) * hi) :
    (-B) * u ≤ A * D ∧ u ≤ hi := by
  have h1 : (-B) * u ≤ A * D := by
    have : (-B) * u = -(B * u) := Int.neg_mul B u
    omega
  have hNegB : 0 < -B := by omega
  exact ⟨h1, mul_le_cancel_left_pos hNegB (by omega)⟩

/-- Combining certified lower and upper dual SOS bounds into `Flow.Interval.Interval1D`
    intersection encloses `u` with nonnegative interval width `0 <= hi - lo`. -/
theorem bootstrap_interval_certified (Al Bl Au Bu D u lo hi widLo widHi : Int)
    (hSosLo : 0 ≤ Al * D + Bl * u) (hBl : 0 < Bl) (hWlo : lo * Bl ≤ (-Al) * D)
    (hSosHi : 0 ≤ Au * D + Bu * u) (hBu : Bu < 0) (hWhi : Au * D ≤ (-Bu) * hi)
    (hSpanLo : u ≤ hi + widLo) (hW0 : 0 ≤ widLo)
    (hSpanHi : lo - widHi ≤ u) (hW1 : 0 ≤ widHi) :
    let ivLo : Interval1D := ⟨lo, hi + widLo⟩
    let ivHi : Interval1D := ⟨lo - widHi, hi⟩
    (ivLo.intersect ivHi).contains u ∧
    0 ≤ (ivLo.intersect ivHi).width ∧
    lo ≤ u ∧ u ≤ hi := by
  intro ivLo ivHi
  have hLo := (sd_dual_sos_lower_bound Al Bl D u lo hSosLo hBl hWlo).2
  have hHi := (sd_dual_sos_upper_bound Au Bu D u hi hSosHi hBu hWhi).2
  have hInterLo : (ivLo.intersect ivHi).lo = lo := by
    unfold Interval1D.intersect ivLo ivHi
    dsimp only
    split <;> omega
  have hInterHi : (ivLo.intersect ivHi).hi = hi := by
    unfold Interval1D.intersect ivLo ivHi
    dsimp only
    split <;> omega
  have hNonEmpty : (ivLo.intersect ivHi).lo ≤ (ivLo.intersect ivHi).hi := by
    rw [hInterLo, hInterHi]; omega
  have hTight := intersect_tightens_width ivLo ivHi hNonEmpty
  have hCont : (ivLo.intersect ivHi).contains u := by
    unfold Interval1D.contains
    rw [hInterLo, hInterHi]
    exact ⟨hLo, hHi⟩
  let _ := hSpanLo; let _ := hW0; let _ := hSpanHi; let _ := hW1
  exact ⟨hCont, hTight.1, hLo, hHi⟩

/-- Gross-Witten-Wadia (`SU(infinity)`) planar loop equation at `lambda = q / p`:
    `p * D * (W_{k-1} - W_{k+1}) = q * sum_{m=0}^{k-1} W_m * W_{k-m}` (`k >= 1`). -/
def gwwLoopEq (p q D : Int) (W : Nat → Int) (k : Nat) : Prop :=
  p * D * (W (k - 1) - W (k + 1)) = q * sumFin k (fun m => W m.val * W (k - m.val))

private theorem sumFin_split_first (k : Nat) (f : Fin (k + 1) → Int) :
    sumFin (k + 1) f = f ⟨0, Nat.zero_lt_succ k⟩ +
      sumFin k (fun m => f ⟨m.val + 1, Nat.succ_lt_succ m.isLt⟩) := by
  unfold sumFin
  have hShift : ∀ n : Nat, n ≤ k →
      sumPrefix f (n + 1) = f ⟨0, Nat.zero_lt_succ k⟩ +
        sumPrefix (fun m : Fin k => f ⟨m.val + 1, Nat.succ_lt_succ m.isLt⟩) n := by
    intro n hn
    induction n with
    | zero =>
      simp only [sumPrefix, Nat.zero_lt_succ k, ↓reduceDIte]
      omega
    | succ r ih =>
      have hrLe : r ≤ k := Nat.le_of_lt hn
      have hrLt : r < k := hn
      have hr1Lt : r + 1 < k + 1 := Nat.succ_lt_succ hrLt
      change sumPrefix f (r + 1) + (if h : r + 1 < k + 1 then f ⟨r + 1, h⟩ else 0) = _
      simp only [hr1Lt, ↓reduceDIte]
      rw [ih hrLe]
      have hNext :
          sumPrefix (fun m : Fin k => f ⟨m.val + 1, Nat.succ_lt_succ m.isLt⟩) (r + 1) =
          sumPrefix (fun m : Fin k => f ⟨m.val + 1, Nat.succ_lt_succ m.isLt⟩) r +
            f ⟨r + 1, hr1Lt⟩ := by
        simp only [sumPrefix, hrLt, ↓reduceDIte]
      rw [hNext]
      omega
  exact hShift k (Nat.le_refl k)

/-- Exactness of the strong-coupling GWW branch `(W_0, W_1, W_{k+2}) = (D, D/lambda, 0)`
    across EVERY order `k >= 1` of the nonlinear Makeenko-Migdal loop equations. -/
theorem gww_strong_branch_exact (p q D u : Int) (W : Nat → Int)
    (h0 : W 0 = D) (h1 : W 1 = u) (h2 : ∀ m : Nat, W (m + 2) = 0)
    (hBranch : q * u = p * D) :
    ∀ k : Nat, gwwLoopEq p q D W (k + 1) := by
  intro k
  unfold gwwLoopEq
  cases k with
  | zero =>
    have hF1 : sumFin 1 (fun m => W m.val * W (1 - m.val)) = W 0 * W 1 := by
      unfold sumFin sumPrefix
      simp only [sumPrefix]
      split
      · simp
      · omega
    have hK0 : (1 : Nat) - 1 = 0 := rfl
    rw [hK0, hF1, h0, h1, h2 0, Int.sub_zero]
    calc p * D * D = q * u * D := by rw [← hBranch]
      _ = q * (D * u) := by ac_rfl
  | succ k' =>
    cases k' with
    | zero =>
      have hF2 : sumFin 2 (fun m => W m.val * W (2 - m.val)) = W 0 * W 2 + W 1 * W 1 := by
        rw [sumFin_split_first]
        have hOne : sumFin 1 (fun m : Fin 1 => W (m.val + 1) * W (2 - (m.val + 1))) = W 1 * W 1 := by
          unfold sumFin sumPrefix
          simp only [sumPrefix]
          split
          · simp
          · omega
        rw [hOne]
        rfl
      have hK1 : (2 : Nat) - 1 = 1 := rfl
      rw [hK1, hF2, h0, h1, h2 0, h2 1, Int.sub_zero, Int.mul_zero, Int.zero_add]
      calc p * D * u = u * (p * D) := by ac_rfl
        _ = u * (q * u) := by rw [← hBranch]
        _ = q * (u * u) := by ac_rfl
    | succ k'' =>
      have hTail :
          sumFin (k'' + 3) (fun m => W m.val * W (k'' + 3 - m.val)) = 0 := by
        rw [sumFin_split_first]
        have hZero0 : W 0 * W (k'' + 3 - 0) = 0 := by
          have hE : k'' + 3 - 0 = (k'' + 1) + 2 := by omega
          rw [hE, h2, Int.mul_zero]
        rw [hZero0]
        have hRest :
            (fun m : Fin (k'' + 2) => W (m.val + 1) * W (k'' + 3 - (m.val + 1))) =
            (fun _ => 0) := by
          funext m
          rcases m with ⟨mv, hmv⟩
          cases mv with
          | zero =>
            have hE : k'' + 3 - (0 + 1) = k'' + 2 := by omega
            rw [hE, h2, Int.mul_zero]
          | succ mv' =>
            have hE : mv' + 1 + 1 = mv' + 2 := by omega
            rw [hE, h2, Int.zero_mul]
        rw [hRest, sumFin_zero_fn]
        omega
      have hL : k'' + 3 - 1 = k'' + 2 := by omega
      have hL2 : k'' + 3 + 1 = (k'' + 2) + 2 := by omega
      rw [hL, hL2, hTail, h2 k'', h2 (k'' + 2)]
      simp

/-- Explicit Toeplitz quadratic form `v^T T_K(W) v` over `Fin K`. -/
def toeplitzQuad (W : Nat → Int) (K : Nat) (v : Fin K → Int) : Int :=
  sumFin K (fun i => sumFin K (fun j => v i * v j * W (absDiff i.val j.val)))

private def altVec (K : Nat) (i : Fin K) : Int :=
  if i.val % 2 = 0 then 1 else -1

private theorem altVec_self (K : Nat) (i : Fin K) : altVec K i * altVec K i = 1 := by
  unfold altVec
  split <;> decide

private theorem altVec_adj (K : Nat) (i j : Fin K) (hij : j.val = i.val + 1 ∨ i.val = j.val + 1) :
    altVec K i * altVec K j = -1 := by
  unfold altVec
  rcases hij with h1 | h2
  · have hMod : i.val % 2 = 0 ∨ i.val % 2 = 1 := by omega
    rcases hMod with h | h
    · have h1m : (j.val) % 2 = 1 := by omega
      simp [h, h1m]
    · have h1m : (j.val) % 2 = 0 := by omega
      simp [h, h1m]
  · have hMod : j.val % 2 = 0 ∨ j.val % 2 = 1 := by omega
    rcases hMod with h | h
    · have h1m : (i.val) % 2 = 1 := by omega
      simp [h, h1m]
    · have h1m : (i.val) % 2 = 0 := by omega
      simp [h, h1m]

private theorem sumFin_strong_row (K : Nat) (D u : Int) (W : Nat → Int)
    (h0 : W 0 = D) (h1 : W 1 = u) (h2 : ∀ m : Nat, W (m + 2) = 0) (i : Fin K) :
    sumFin K (fun j => altVec K i * altVec K j * W (absDiff i.val j.val)) =
      D - (if i.val > 0 then u else 0) - (if i.val + 1 < K then u else 0) := by
  have hPoint :
      (fun j : Fin K => altVec K i * altVec K j * W (absDiff i.val j.val)) =
      (fun j : Fin K =>
        (if j.val = i.val then D else 0) +
        ((if 0 < i.val ∧ j.val + 1 = i.val then -u else 0) +
         (if j.val = i.val + 1 then -u else 0))) := by
    funext j
    by_cases hEq : j.val = i.val
    · have hD : absDiff i.val j.val = 0 := by unfold absDiff; split <;> omega
      have hjEq : j = i := Fin.ext hEq
      rw [hD, h0, hjEq, altVec_self, Int.one_mul]
      repeat split <;> omega
    · by_cases hPrev : 0 < i.val ∧ j.val + 1 = i.val
      · have hD : absDiff i.val j.val = 1 := by unfold absDiff; split <;> omega
        have hAdj := altVec_adj K i j (Or.inr (by omega))
        rw [hD, h1, hAdj, Int.neg_one_mul]
        repeat split <;> omega
      · by_cases hNext : j.val = i.val + 1
        · have hD : absDiff i.val j.val = 1 := by unfold absDiff; split <;> omega
          have hAdj := altVec_adj K i j (Or.inl hNext)
          rw [hD, h1, hAdj, Int.neg_one_mul]
          repeat split <;> omega
        · have hGe : ∃ m : Nat, absDiff i.val j.val = m + 2 := by
            refine ⟨absDiff i.val j.val - 2, ?_⟩
            unfold absDiff; split <;> omega
          rcases hGe with ⟨m, hm⟩
          rw [hm, h2 m, Int.mul_zero]
          repeat split <;> omega
  rw [hPoint, sumFin_add, sumFin_add]
  have hDiag : sumFin K (fun j : Fin K => if j.val = i.val then D else 0) = D :=
    sumFin_indicator i.val i.isLt D
  have hLeft : sumFin K (fun j : Fin K => if 0 < i.val ∧ j.val + 1 = i.val then -u else 0) =
      if i.val > 0 then -u else 0 := by
    by_cases hi : 0 < i.val
    · have hEqFn : (fun j : Fin K => if 0 < i.val ∧ j.val + 1 = i.val then -u else 0) =
          (fun j : Fin K => if j.val = i.val - 1 then -u else 0) := by
        funext j
        have hCond : (0 < i.val ∧ j.val + 1 = i.val) ↔ (j.val = i.val - 1) := by omega
        simp [hCond]
      rw [hEqFn, sumFin_indicator (i.val - 1) (by omega) (-u)]
      simp [hi]
    · have hZeroFn : (fun j : Fin K => if 0 < i.val ∧ j.val + 1 = i.val then -u else 0) =
          (fun _ => 0) := by
        funext j; simp [hi]
      rw [hZeroFn, sumFin_zero_fn]
      simp [hi]
  have hRight : sumFin K (fun j : Fin K => if j.val = i.val + 1 then -u else 0) =
      if i.val + 1 < K then -u else 0 := by
    by_cases hi : i.val + 1 < K
    · rw [sumFin_indicator (i.val + 1) hi (-u)]
      simp [hi]
    · have hZeroFn : (fun j : Fin K => if j.val = i.val + 1 then -u else 0) = (fun _ => 0) := by
        funext j
        have hNo : ¬(j.val = i.val + 1) := by omega
        simp [hNo]
      rw [hZeroFn, sumFin_zero_fn]
      simp [hi]
  rw [hDiag, hLeft, hRight]
  split <;> split <;> omega

/-- Exact closed-form evaluation of the alternating Fourier mode `v_i = (-1)^i` on the
    strong-coupling GWW Toeplitz matrix `T_K`: `v^T T_K v = K * D - 2 * (K - 1) * u`. -/
theorem gww_strong_branch_toeplitz_eval (K : Nat) (hK : 1 ≤ K) (D u : Int) (W : Nat → Int)
    (h0 : W 0 = D) (h1 : W 1 = u) (h2 : ∀ m : Nat, W (m + 2) = 0) :
    toeplitzQuad W K (altVec K) = (K : Int) * D - 2 * ((K : Int) - 1) * u := by
  unfold toeplitzQuad
  have hRow :
      (fun i : Fin K => sumFin K (fun j => altVec K i * altVec K j * W (absDiff i.val j.val))) =
      (fun i : Fin K => D - (if i.val > 0 then u else 0) - (if i.val + 1 < K then u else 0)) := by
    funext i; exact sumFin_strong_row K D u W h0 h1 h2 i
  rw [hRow]
  have hEq :
      (fun i : Fin K => D - (if i.val > 0 then u else 0) - (if i.val + 1 < K then u else 0)) =
      (fun i : Fin K => (D - 2 * u) + ((if i.val = 0 then u else 0) + (if i.val = K - 1 then u else 0))) := by
    funext i
    have hGt : (i.val > 0) ↔ ¬(i.val = 0) := by omega
    have hLt : (i.val + 1 < K) ↔ ¬(i.val = K - 1) := by
      have hi := i.isLt; omega
    by_cases hA : i.val = 0 <;> by_cases hB : i.val = K - 1 <;> simp_all <;> omega
  rw [hEq, sumFin_add, sumFin_add, sumFin_const]
  rw [sumFin_indicator 0 (by omega) u, sumFin_indicator (K - 1) (by omega) u]
  have hKInt : (K : Int) ≥ 1 := by omega
  grind

/-- Exact Gross-Witten-Wadia critical point (`lambda_c = 2`): for EVERY coupling
    `lambda = q/p < 2` (`0 < p, q`, `q < 2 * p`), there exists a finite Toeplitz truncation `K`
    where the strong-coupling branch `W_1 = D / lambda` strictly violates Toeplitz positivity. -/
theorem gww_strong_branch_ruled_out_below_two (p q D u : Int) (W : Nat → Int)
    (hp : 0 < p) (hq : 0 < q) (hD : 0 < D) (hSub : q < 2 * p)
    (h0 : W 0 = D) (h1 : W 1 = u) (h2 : ∀ m : Nat, W (m + 2) = 0)
    (hBranch : q * u = p * D) :
    ∃ K : Nat, 1 ≤ K ∧ toeplitzQuad W K (altVec K) < 0 := by
  let K : Nat := 2 * p.natAbs + 2
  have hK1 : 1 ≤ K := by omega
  refine ⟨K, hK1, ?_⟩
  rw [gww_strong_branch_toeplitz_eval K hK1 D u W h0 h1 h2]
  have hKp : (K : Int) = 2 * p + 2 := by
    have hpAbs : (p.natAbs : Int) = p := Int.natAbs_of_nonneg (Int.le_of_lt hp)
    have hKdef : K = 2 * p.natAbs + 2 := rfl
    rw [hKdef]
    omega
  have hGap : 2 * p < (K : Int) * (2 * p - q) := by
    have hPos : 1 ≤ 2 * p - q := by omega
    have hStep : (K : Int) * 1 ≤ (K : Int) * (2 * p - q) :=
      Int.mul_le_mul_of_nonneg_left hPos (by omega)
    rw [Int.mul_one, hKp] at hStep
    rw [hKp]
    omega
  have hCoef : (K : Int) * q - 2 * ((K : Int) - 1) * p < 0 := by
    have hExp : (K : Int) * q - 2 * ((K : Int) - 1) * p = 2 * p - (K : Int) * (2 * p - q) := by grind
    omega
  have hScaled : q * ((K : Int) * D - 2 * ((K : Int) - 1) * u) =
      ((K : Int) * q - 2 * ((K : Int) - 1) * p) * D := by
    calc q * ((K : Int) * D - 2 * ((K : Int) - 1) * u)
        = (K : Int) * q * D - 2 * ((K : Int) - 1) * (q * u) := by grind
      _ = (K : Int) * q * D - 2 * ((K : Int) - 1) * (p * D) := by rw [hBranch]
      _ = ((K : Int) * q - 2 * ((K : Int) - 1) * p) * D := by grind
  have hNegScaled : q * ((K : Int) * D - 2 * ((K : Int) - 1) * u) < 0 := by
    rw [hScaled]
    exact Int.mul_neg_of_neg_of_pos hCoef hD
  exact mul_lt_zero_cancel_pos hq hNegScaled

/-! ## Section 2: 4D SU(2) Quaternionic Field-Strength & Schwinger-Dyson Bounds (Theorem B2) -/

/-- Pure `su(2)` Lie-algebra inner product of imaginary components of two quaternions. -/
def quatImDot (q1 q2 : Quat) : Int :=
  q1.x * q2.x + q1.y * q2.y + q1.z * q2.z

/-- Squared `su(2)` field-strength norm of a quaternion (`q.normSq - q.w^2`). -/
def quatImNormSq (q : Quat) : Int :=
  quatImDot q q

/-- Exact link-staple identity separating the Wilson plaquette difference into `su(2)` algebra inner
    products: `Re(U_{P_s} U_{P_t}^\dag) - Re(U_{P_s}) Re(U_{P_t}) = F_s \cdot F_t`. -/
theorem quat_plaq_diff_eq_im_dot (q1 q2 : Quat) :
    (qMul q1 (qConj q2)).w - q1.w * q2.w = quatImDot q1 q2 ∧
    qNormSq q1 - q1.w * q1.w = quatImNormSq q1 := by
  constructor
  · unfold qMul qConj quatImDot; dsimp only; grind
  · unfold qNormSq quatImNormSq quatImDot; omega

/-- Exact 4D Schwinger-Dyson sum-of-squares positivity: summing all 36 pairwise link-staple
    field-strength dot products equals the squared norm of the total staple vector `|sum_s F_s|^2 >= 0`. -/
theorem su2_4d_sd_sum_of_squares (q : Fin 6 → Quat) :
    sumFin 6 (fun s => sumFin 6 (fun t => quatImDot (q s) (q t))) =
      sumFin 6 (fun s => (q s).x) * sumFin 6 (fun s => (q s).x) +
      sumFin 6 (fun s => (q s).y) * sumFin 6 (fun s => (q s).y) +
      sumFin 6 (fun s => (q s).z) * sumFin 6 (fun s => (q s).z) ∧
    0 ≤ sumFin 6 (fun s => sumFin 6 (fun t => quatImDot (q s) (q t))) := by
  have hFactor : ∀ f : Fin 6 → Int,
      sumFin 6 (fun s => sumFin 6 (fun t => f s * f t)) = sumFin 6 f * sumFin 6 f := by
    intro f
    have hIn : (fun s : Fin 6 => sumFin 6 (fun t => f s * f t)) =
        (fun s : Fin 6 => f s * sumFin 6 f) := by
      funext s; exact sumFin_mul_left 6 (f s) f
    rw [hIn, sumFin_mul_right]
  have hSplit :
      sumFin 6 (fun s => sumFin 6 (fun t => quatImDot (q s) (q t))) =
      sumFin 6 (fun s => sumFin 6 (fun t => (q s).x * (q t).x)) +
      sumFin 6 (fun s => sumFin 6 (fun t => (q s).y * (q t).y)) +
      sumFin 6 (fun s => sumFin 6 (fun t => (q s).z * (q t).z)) := by
    have hRow :
        (fun s : Fin 6 => sumFin 6 (fun t => quatImDot (q s) (q t))) =
        (fun s : Fin 6 =>
          (sumFin 6 (fun t => (q s).x * (q t).x) +
           sumFin 6 (fun t => (q s).y * (q t).y)) +
           sumFin 6 (fun t => (q s).z * (q t).z)) := by
      funext s
      have ht :
          (fun t : Fin 6 => quatImDot (q s) (q t)) =
          (fun t : Fin 6 => ((q s).x * (q t).x + (q s).y * (q t).y) + (q s).z * (q t).z) := by
        funext t; unfold quatImDot; omega
      rw [ht, sumFin_add, sumFin_add]
    rw [hRow, sumFin_add, sumFin_add]
  have hEq :
      sumFin 6 (fun s => sumFin 6 (fun t => quatImDot (q s) (q t))) =
      sumFin 6 (fun s => (q s).x) * sumFin 6 (fun s => (q s).x) +
      sumFin 6 (fun s => (q s).y) * sumFin 6 (fun s => (q s).y) +
      sumFin 6 (fun s => (q s).z) * sumFin 6 (fun s => (q s).z) := by
    rw [hSplit, hFactor, hFactor, hFactor]
  refine ⟨hEq, ?_⟩
  rw [hEq]
  have hx := int_sq_nonneg (sumFin 6 (fun s => (q s).x))
  have hy := int_sq_nonneg (sumFin 6 (fun s => (q s).y))
  have hz := int_sq_nonneg (sumFin 6 (fun s => (q s).z))
  omega

/-- Hypercubic collapse & exact non-perturbative 4D SU(2) Schwinger-Dyson upper bound:
    `|sum_s F_s|^2 <= 6 * sum_s |F_s|^2`. -/
theorem su2_4d_sd_cauchy_schwarz_upper (q : Fin 6 → Quat) :
    sumFin 6 (fun s => sumFin 6 (fun t => quatImDot (q s) (q t))) ≤
      6 * sumFin 6 (fun s => quatImNormSq (q s)) := by
  have hSOS := (su2_4d_sd_sum_of_squares q).1
  rw [hSOS]
  have hx := sumFin_cauchy_const_right 6 (fun s => (q s).x)
  have hy := sumFin_cauchy_const_right 6 (fun s => (q s).y)
  have hz := sumFin_cauchy_const_right 6 (fun s => (q s).z)
  have hNorm :
      sumFin 6 (fun s => quatImNormSq (q s)) =
      sumFin 6 (fun s => (q s).x * (q s).x) +
      sumFin 6 (fun s => (q s).y * (q s).y) +
      sumFin 6 (fun s => (q s).z * (q s).z) := by
    have hf : (fun s : Fin 6 => quatImNormSq (q s)) =
        (fun s : Fin 6 => ((q s).x * (q s).x + (q s).y * (q s).y) + (q s).z * (q s).z) := by
      funext s; unfold quatImNormSq quatImDot; omega
    rw [hf, sumFin_add, sumFin_add]
  rw [hNorm]
  grind

/-- Non-perturbative 4D SU(2) quadratic plaquette inequality (`2*beta*u_P^2 + u_P <= 2*beta`)
    and hypercubic lower nonnegativity (`0 <= u_P`) deduced directly from 4D SD symmetry. -/
theorem su2_4d_sd_plaquette_quadratic_bound (p q S uP uP2 diagVar coplDiff orthDiff : Int)
    (hp : 0 < p) (hq : 0 < q) (hS : 0 < S)
    (hSymNonneg : 0 ≤ 6 * (diagVar + coplDiff + 4 * orthDiff))
    (hCS : 6 * (diagVar + coplDiff + 4 * orthDiff) ≤ 36 * diagVar)
    (hDiag : diagVar = S * S - uP2)
    (hVar : uP * uP ≤ uP2)
    (hSdEq : 3 * q * S * uP = p * (diagVar + coplDiff + 4 * orthDiff)) :
    0 ≤ uP ∧ 2 * p * (uP * uP) + q * S * uP ≤ 2 * p * (S * S) := by
  have hSum0 : 0 ≤ diagVar + coplDiff + 4 * orthDiff := by omega
  have hU0 : 0 ≤ uP := by
    have hProd0 : 0 ≤ 3 * q * S * uP := by
      rw [hSdEq]; exact Int.mul_nonneg (Int.le_of_lt hp) hSum0
    have hCoef : 0 < 3 * q * S := Int.mul_pos (Int.mul_pos (by omega) hq) hS
    by_cases hu : 0 ≤ uP
    · exact hu
    · have hNeg := Int.mul_neg_of_pos_of_neg hCoef (by omega : uP < 0)
      omega
  have hLeDiag : diagVar + coplDiff + 4 * orthDiff ≤ 6 * diagVar := by omega
  have hPdiag : p * (diagVar + coplDiff + 4 * orthDiff) ≤ p * (6 * (S * S - uP * uP)) := by
    apply Int.mul_le_mul_of_nonneg_left _ (Int.le_of_lt hp)
    omega
  have hMul3 : 3 * (2 * p * (uP * uP) + q * S * uP) ≤ 3 * (2 * p * (S * S)) := by
    have hL : 3 * (2 * p * (uP * uP) + q * S * uP) = 6 * p * (uP * uP) + 3 * q * S * uP := by grind
    have hR : 3 * (2 * p * (S * S)) = 6 * p * (S * S) := by grind
    have hPexp : p * (6 * (S * S - uP * uP)) = 6 * p * (S * S) - 6 * p * (uP * uP) := by grind
    rw [hL, hR, hSdEq]
    rw [hPexp] at hPdiag
    omega
  exact ⟨hU0, mul_le_cancel_left_pos (by omega : (0 : Int) < 3) hMul3⟩

/-- 4D OS variance lower bound (`u_{P^2} >= 1 - 3*u_P/beta`): assuming 4D OS reflection positivity
    across shared-edge and diagonal hyperplanes (`0 <= coplDiff` and `0 <= orthDiff`), the
    plaquette fluctuation `1 - u_{P^2}` is strictly bounded above by `3 * u_P / beta`. -/
theorem su2_4d_sd_os_variance_bound (p q S uP uP2 coplDiff orthDiff : Int)
    (hp : 0 < p) (hCopl : 0 ≤ coplDiff) (hOrth : 0 ≤ orthDiff)
    (hSdEq : 3 * q * S * uP = p * (S * S - uP2 + coplDiff + 4 * orthDiff)) :
    p * (S * S - uP2) ≤ 3 * q * S * uP := by
  have hPos : p * (S * S - uP2) ≤ p * (S * S - uP2 + coplDiff + 4 * orthDiff) := by
    apply Int.mul_le_mul_of_nonneg_left _ (Int.le_of_lt hp)
    omega
  omega

/-! ## Section 3: Exact 4D Staple Reflection Positivity & 7-Link Gram Bounds (Theorem B3) -/

/-- Exact 4D staple reflection positivity across a link-containing hyperplane (both coplanar `1x2`
    and orthogonal `chair`): `0 <= W_pair * S - 2 * c * u_P + c^2` for all `c : Int` forces
    `u_P^2 <= W_pair * S` (`W_{1x2} >= u_P^2`, `W_{chair} >= u_P^2`). -/
theorem su2_4d_staple_reflection_bound (S uP wPair : Int)
    (hRP : ∀ c : Int, 0 ≤ wPair * S - 2 * c * uP + c * c) :
    uP * uP ≤ wPair * S := by
  have hInst := hRP uP
  have hEq : wPair * S - 2 * uP * uP + uP * uP = wPair * S - uP * uP := by grind
  omega

/-- 7x7 link+6-staples Gram matrix bounds (`{U_l, V_0, ..., V_5}`):
    proves the exact Schur-complement inequality `6 * u_P^2 <= 1 + W_{1x2} + 4 * W_{chair}`,
    as well as coplanar difference `W_{1x2} <= 1` and quadrupole positivity `2 * W_{chair} <= 1 + W_{1x2}`. -/
theorem su2_4d_backtracking_gram_7 (S uP w12 wCh : Int) (hS : 0 < S)
    (hSymQuad : ∀ c0 c1 : Int,
        0 ≤ c0 * c0 * S + 12 * c0 * c1 * uP + 6 * c1 * c1 * (S + w12 + 4 * wCh))
    (hAntiCopl : 0 ≤ 2 * S - 2 * w12)
    (hQuadrupole : 0 ≤ 4 * S + 4 * w12 - 8 * wCh) :
    6 * (uP * uP) ≤ S * (S + w12 + 4 * wCh) ∧
    w12 ≤ S ∧
    2 * wCh ≤ S + w12 := by
  have hInst := hSymQuad (-(6 * uP)) S
  have hFactor :
      (-(6 * uP)) * (-(6 * uP)) * S + 12 * (-(6 * uP)) * S * uP +
        6 * S * S * (S + w12 + 4 * wCh) =
      (6 * S) * (S * (S + w12 + 4 * wCh) - 6 * (uP * uP)) := by
    grind
  rw [hFactor] at hInst
  have h6S : 0 < 6 * S := by omega
  have hMain : 6 * (uP * uP) ≤ S * (S + w12 + 4 * wCh) := by
    have hZ : (6 * S) * 0 ≤ (6 * S) * (S * (S + w12 + 4 * wCh) - 6 * (uP * uP)) := by
      simpa using hInst
    have hDiff := mul_le_cancel_left_pos h6S hZ
    omega
  exact ⟨hMain, by omega, by omega⟩

private def qSumFin (N : Nat) (f : Fin N → Quat) : Quat :=
  ⟨sumFin N (fun i => (f i).w),
   sumFin N (fun i => (f i).x),
   sumFin N (fun i => (f i).y),
   sumFin N (fun i => (f i).z)⟩

/-- Exact resultant norm-squared identity for `N` quaternionic staples:
    `|sum_s V_s|^2 = sum_s |V_s|^2 + sum_{s != t} Re(V_s V_t^\dag)`.
    For 6 unit SU(2) staples this reduces directly to `R_l^2 = 6 + 6*W_{1x2} + 24*W_{chair}`. -/
theorem su2_4d_staple_norm_sq_exact (N : Nat) (V : Fin N → Quat) :
    qNormSq (qSumFin N V) =
      sumFin N (fun s => qNormSq (V s)) +
      sumFin N (fun s => sumFin N (fun t => if s = t then 0 else (qMul (V s) (qConj (V t))).w)) := by
  have hComp : ∀ c : Fin N → Int,
      sumFin N c * sumFin N c =
      sumFin N (fun s => c s * c s) +
      sumFin N (fun s => sumFin N (fun t => if s = t then 0 else c s * c t)) := by
    intro c
    have hDouble : sumFin N c * sumFin N c =
        sumFin N (fun s => sumFin N (fun t => c s * c t)) := by
      have hf : (fun s : Fin N => sumFin N (fun t => c s * c t)) = (fun s => c s * sumFin N c) := by
        funext s; exact sumFin_mul_left N (c s) c
      rw [hf, sumFin_mul_right]
    rw [hDouble, ← sumFin_add]
    congr 1
    funext s
    have hSplitRow :
        (fun t : Fin N => c s * c t) =
        (fun t : Fin N => (if t.val = s.val then c s * c s else 0) +
          (if s = t then 0 else c s * c t)) := by
      funext t
      by_cases hst : s = t
      · subst hst; simp
      · have hNeVal : ¬(t.val = s.val) := fun hv => hst (Fin.ext hv.symm)
        simp [hst, hNeVal]
    rw [hSplitRow, sumFin_add, sumFin_indicator s.val s.isLt (c s * c s)]
  unfold qNormSq qSumFin
  dsimp only
  rw [hComp (fun s => (V s).w), hComp (fun s => (V s).x),
      hComp (fun s => (V s).y), hComp (fun s => (V s).z)]
  have hSelf :
      sumFin N (fun s => (V s).w * (V s).w) + sumFin N (fun s => (V s).x * (V s).x) +
      sumFin N (fun s => (V s).y * (V s).y) + sumFin N (fun s => (V s).z * (V s).z) =
      sumFin N (fun s => (V s).w * (V s).w + (V s).x * (V s).x + (V s).y * (V s).y + (V s).z * (V s).z) := by
    rw [← sumFin_add, ← sumFin_add, ← sumFin_add]
  have hCross :
      sumFin N (fun s => sumFin N (fun t => if s = t then 0 else (V s).w * (V t).w)) +
      sumFin N (fun s => sumFin N (fun t => if s = t then 0 else (V s).x * (V t).x)) +
      sumFin N (fun s => sumFin N (fun t => if s = t then 0 else (V s).y * (V t).y)) +
      sumFin N (fun s => sumFin N (fun t => if s = t then 0 else (V s).z * (V t).z)) =
      sumFin N (fun s => sumFin N (fun t => if s = t then 0 else (qMul (V s) (qConj (V t))).w)) := by
    rw [← sumFin_add, ← sumFin_add, ← sumFin_add]
    congr 1; funext s
    rw [← sumFin_add, ← sumFin_add, ← sumFin_add]
    congr 1; funext t
    by_cases hst : s = t
    · simp [hst]
    · simp only [hst, ↓reduceIte, qMul, qConj]; grind
  rw [← hSelf, ← hCross]
  omega

/-! ## Section 4: Exact Multi-Scale Block-Spin & Migdal-Kadanoff RG Theorems (Theorem B4) -/

/-- Exact 1D `b = 2` transfer decimation step `r -> r^2`. -/
def rgDecimate1D (r : PosRat) : PosRat :=
  posRatPow r 2

/-- Iterated `n`-step dyadic 1D RG flow. -/
def rgFlow1D : Nat → PosRat → PosRat
  | 0, r     => r
  | n + 1, r => rgDecimate1D (rgFlow1D n r)

private theorem posRatPow_num_eq (r : PosRat) (n : Nat) :
    (posRatPow r n).num = intPow r.num n := by
  induction n with
  | zero => rfl
  | succ k ih => unfold posRatPow intPow PosRat.mul; dsimp only; rw [ih]

private theorem posRatPow_den_eq (r : PosRat) (n : Nat) :
    (posRatPow r n).den = intPow r.den n := by
  induction n with
  | zero => rfl
  | succ k ih => unfold posRatPow intPow PosRat.mul; dsimp only; rw [ih]

private theorem posRatPow_mul_exp (r : PosRat) (a b : Nat) :
    (posRatPow (posRatPow r a) b).num = (posRatPow r (a * b)).num ∧
    (posRatPow (posRatPow r a) b).den = (posRatPow r (a * b)).den := by
  induction b with
  | zero => simp [posRatPow, PosRat.one]
  | succ k ih =>
    rcases ih with ⟨ihn, ihd⟩
    have hExp : a * (k + 1) = a + a * k := by rw [Nat.mul_succ, Nat.add_comm]
    rw [hExp]
    constructor
    · calc (posRatPow (posRatPow r a) (k + 1)).num
          = (posRatPow r a).num * (posRatPow (posRatPow r a) k).num := by rfl
        _ = intPow r.num a * intPow r.num (a * k) := by
            rw [ihn, posRatPow_num_eq, posRatPow_num_eq]
        _ = intPow r.num (a + a * k) := (intPow_add r.num a (a * k)).symm
        _ = (posRatPow r (a + a * k)).num := (posRatPow_num_eq r (a + a * k)).symm
    · calc (posRatPow (posRatPow r a) (k + 1)).den
          = (posRatPow r a).den * (posRatPow (posRatPow r a) k).den := by rfl
        _ = intPow r.den a * intPow r.den (a * k) := by
            rw [ihd, posRatPow_den_eq, posRatPow_den_eq]
        _ = intPow r.den (a + a * k) := (intPow_add r.den a (a * k)).symm
        _ = (posRatPow r (a + a * k)).den := (posRatPow_den_eq r (a + a * k)).symm

/-- Exact 1D block-spin RG (`Theorem B4a`): `n` dyadic steps contract character `r` strictly as
    `r^(2^n)` (`r < 1 -> r^2 < r`), with `T = 0` (`r = 1`) as the exact conformal fixed point. -/
theorem block_spin_1d_exact_rg (r : PosRat) (n : Nat) :
    ((rgFlow1D n r).num = (posRatPow r (2 ^ n)).num ∧
     (rgFlow1D n r).den = (posRatPow r (2 ^ n)).den) ∧
    (r.num < r.den → (rgDecimate1D r).num * r.den < r.num * (rgDecimate1D r).den) ∧
    (r.num = r.den → (rgDecimate1D r).num * r.den = r.num * (rgDecimate1D r).den) := by
  have hFlow : ∀ k : Nat,
      (rgFlow1D k r).num = (posRatPow r (2 ^ k)).num ∧
      (rgFlow1D k r).den = (posRatPow r (2 ^ k)).den := by
    intro k
    induction k with
    | zero =>
      have h1 : (2 ^ 0 : Nat) = 1 := rfl
      rw [h1, posRatPow_num_eq, posRatPow_den_eq]
      simp [rgFlow1D, intPow]
    | succ m ihm =>
      rcases ihm with ⟨ihn, ihd⟩
      have hDecN : (rgFlow1D (m + 1) r).num = (rgFlow1D m r).num * (rgFlow1D m r).num := by
        change (posRatPow (rgFlow1D m r) 2).num = _
        rw [posRatPow_num_eq]; simp [intPow]
      have hDecD : (rgFlow1D (m + 1) r).den = (rgFlow1D m r).den * (rgFlow1D m r).den := by
        change (posRatPow (rgFlow1D m r) 2).den = _
        rw [posRatPow_den_eq]; simp [intPow]
      have hPow2 : 2 ^ (m + 1) = 2 ^ m + 2 ^ m := by omega
      constructor
      · rw [hDecN, ihn, posRatPow_num_eq, posRatPow_num_eq, hPow2, intPow_add]
      · rw [hDecD, ihd, posRatPow_den_eq, posRatPow_den_eq, hPow2, intPow_add]
  refine ⟨hFlow n, ?_, ?_⟩
  · intro hLt
    unfold rgDecimate1D
    rw [posRatPow_num_eq, posRatPow_den_eq]
    change (r.num * (r.num * 1)) * r.den < r.num * (r.den * (r.den * 1))
    rw [Int.mul_one, Int.mul_one]
    have h1 : r.num * (r.num * r.den) < r.den * (r.num * r.den) :=
      Int.mul_lt_mul_of_pos_right hLt (Int.mul_pos r.num_pos r.den_pos)
    have hEqL : (r.num * r.num) * r.den = r.num * (r.num * r.den) := by ac_rfl
    have hEqR : r.num * (r.den * r.den) = r.den * (r.num * r.den) := by ac_rfl
    rw [hEqL, hEqR]
    exact h1
  · intro hEq
    unfold rgDecimate1D
    rw [posRatPow_num_eq, posRatPow_den_eq]
    change (r.num * (r.num * 1)) * r.den = r.num * (r.den * (r.den * 1))
    rw [Int.mul_one, Int.mul_one, hEq]
    ac_rfl

/-- Exact 2D `b x b` gauge character decimation `c -> c^(b^2)`. -/
def rgDecimate2D (b : Nat) (c : PosRat) : PosRat :=
  posRatPow c (b * b)

/-- Exact 2D block-spin gauge decimation & area-law RG invariance (`Theorem B4b`):
    1. A fine-scale Wilson loop of size `(b*R) x (b*T)` at character `c` equals the coarse loop
       of size `R x T` at `rgDecimate2D b c = c^(b^2)` identically in `Q_{>0}`.
    2. For `c < 1` and `b >= 2`, `rgDecimate2D b c < c` strictly drives couplings toward confinement. -/
theorem block_spin_2d_character_decimation (c : PosRat) (b R T : Nat) :
    PosRat.equiv (posRatPow c ((b * R) * (b * T))) (posRatPow (rgDecimate2D b c) (R * T)) ∧
    (c.num < c.den → 2 ≤ b → (rgDecimate2D b c).num * c.den < c.num * (rgDecimate2D b c).den) := by
  constructor
  · unfold PosRat.equiv rgDecimate2D
    have hPair := posRatPow_mul_exp c (b * b) (R * T)
    have hExp : (b * R) * (b * T) = (b * b) * (R * T) := by grind
    rw [hExp, hPair.1, hPair.2]
  · intro hLt hb
    unfold rgDecimate2D
    rw [posRatPow_num_eq, posRatPow_den_eq]
    have hb4 : b * b ≥ 4 := Nat.mul_le_mul hb hb
    have hGe2 : ∃ k : Nat, b * b = k + 2 := ⟨b * b - 2, by omega⟩
    rcases hGe2 with ⟨k, hk⟩
    rw [hk]
    have hPowStep : ∀ m : Nat, intPow c.num (m + 1) * c.den ≤ c.num * intPow c.den (m + 1) := by
      intro m
      induction m with
      | zero =>
        change (c.num * 1) * c.den ≤ c.num * (c.den * 1)
        rw [Int.mul_one, Int.mul_one]
        exact Int.le_refl _
      | succ t ih =>
        change (c.num * intPow c.num (t + 1)) * c.den ≤ c.num * (c.den * intPow c.den (t + 1))
        have hStep1 : (c.num * intPow c.num (t + 1)) * c.den = c.num * (intPow c.num (t + 1) * c.den) := by
          ac_rfl
        have hStep2 : c.num * (c.num * intPow c.den (t + 1)) ≤ c.num * (c.den * intPow c.den (t + 1)) := by
          apply Int.mul_le_mul_of_nonneg_left _ (Int.le_of_lt c.num_pos)
          exact Int.mul_le_mul_of_nonneg_right (Int.le_of_lt hLt) (intPow_nonneg (Int.le_of_lt c.den_pos) (t + 1))
        calc (c.num * intPow c.num (t + 1)) * c.den
            = c.num * (intPow c.num (t + 1) * c.den) := hStep1
          _ ≤ c.num * (c.num * intPow c.den (t + 1)) :=
              Int.mul_le_mul_of_nonneg_left ih (Int.le_of_lt c.num_pos)
          _ ≤ c.num * (c.den * intPow c.den (t + 1)) := hStep2
    have hOne := hPowStep k
    change (c.num * intPow c.num (k + 1)) * c.den < c.num * (c.den * intPow c.den (k + 1))
    have hLeft : (c.num * intPow c.num (k + 1)) * c.den ≤ c.num * (c.num * intPow c.den (k + 1)) := by
      calc (c.num * intPow c.num (k + 1)) * c.den
          = c.num * (intPow c.num (k + 1) * c.den) := by ac_rfl
        _ ≤ c.num * (c.num * intPow c.den (k + 1)) :=
            Int.mul_le_mul_of_nonneg_left hOne (Int.le_of_lt c.num_pos)
    have hRight : c.num * (c.num * intPow c.den (k + 1)) < c.num * (c.den * intPow c.den (k + 1)) := by
      apply Int.mul_lt_mul_of_pos_left _ c.num_pos
      exact Int.mul_lt_mul_of_pos_right hLt (intPow_pos c.den_pos (k + 1))
    exact Int.lt_of_le_of_lt hLeft hRight

/-- Exact discrete Kadanoff bond-moving Jensen/Holder inequalities (`Theorem B4c`) for `m = 2`
    and `m = 4` (4D bond-moving exponent `b^{D-2} = 2^2 = 4`):
    `(sum_i w_i a_i)^4 <= (sum_i w_i)^3 * sum_i w_i a_i^4`. -/
theorem mk_bond_moving_jensen_bound (M : Nat) (w a : Fin M → Int)
    (hw : ∀ i, 0 ≤ w i) :
    let W0 := sumFin M w
    let S1 := sumFin M (fun i => w i * a i)
    let S2 := sumFin M (fun i => w i * (a i * a i))
    let S4 := sumFin M (fun i => w i * intPow (a i) 4)
    S1 * S1 ≤ W0 * S2 ∧
    intPow S1 4 ≤ intPow W0 3 * S4 := by
  intro W0 S1 S2 S4
  have hM2 : S1 * S1 ≤ W0 * S2 := by
    have hCS := weighted_cauchy_schwarz w (fun _ => (1 : Int)) a hw
    have h1 : (fun k : Fin M => w k * (1 * a k)) = (fun k => w k * a k) := by
      funext k; rw [Int.one_mul]
    have h2 : (fun k : Fin M => w k * (1 * 1)) = w := by
      funext k; rw [Int.mul_one, Int.mul_one]
    rw [h1, h2] at hCS
    exact hCS
  have hS4Eq : S4 = sumFin M (fun i => w i * ((a i * a i) * (a i * a i))) := by
    change sumFin M (fun i => w i * intPow (a i) 4) = _
    congr 1; funext i
    have h4 : intPow (a i) 4 = (a i * a i) * (a i * a i) := by
      simp [intPow]; ac_rfl
    rw [h4]
  have hM2b : S2 * S2 ≤ W0 * S4 := by
    rw [hS4Eq]
    have hCS := weighted_cauchy_schwarz w (fun _ => (1 : Int)) (fun i => a i * a i) hw
    have h1 : (fun k : Fin M => w k * (1 * (a k * a k))) = (fun k => w k * (a k * a k)) := by
      funext k; rw [Int.one_mul]
    have h2 : (fun k : Fin M => w k * (1 * 1)) = w := by
      funext k; rw [Int.mul_one, Int.mul_one]
    rw [h1, h2] at hCS
    exact hCS
  have hW0 : 0 ≤ W0 := sumFin_nonneg M w hw
  have hS2Nonneg : 0 ≤ S2 := sumFin_nonneg M _ fun i => Int.mul_nonneg (hw i) (int_sq_nonneg (a i))
  have hS1SqNonneg : 0 ≤ S1 * S1 := int_sq_nonneg S1
  have hPow1 : (S1 * S1) * (S1 * S1) ≤ (W0 * S2) * (W0 * S2) :=
    Int.mul_le_mul hM2 hM2 hS1SqNonneg (Int.mul_nonneg hW0 hS2Nonneg)
  have hPow2 : (W0 * S2) * (W0 * S2) = W0 * W0 * (S2 * S2) := by ac_rfl
  have hPow3 : W0 * W0 * (S2 * S2) ≤ W0 * W0 * (W0 * S4) :=
    Int.mul_le_mul_of_nonneg_left hM2b (Int.mul_nonneg hW0 hW0)
  have hL : intPow S1 4 = (S1 * S1) * (S1 * S1) := by
    simp [intPow]; ac_rfl
  have hR : intPow W0 3 * S4 = W0 * W0 * (W0 * S4) := by
    simp [intPow]; ac_rfl
  exact ⟨hM2, by rw [hL, hR]; exact Int.le_trans hPow1 (by rw [hPow2]; exact hPow3)⟩

/-- Exact 4D asymptotic-freedom basin-entry theorem (`Theorem B4d`): if effective coupling
    decreases by at least `delta > 0` per RG blocking step (`beta (k + 1) <= beta k - delta`),
    every UV starting coupling `beta 0` enters the confining strong-coupling region `<= beta_s`
    in finite RG depth. -/
theorem rg_asymptotic_freedom_confinement_step (beta : Nat → Int) (delta betaS : Int)
    (hDelta : 0 < delta) (hStep : ∀ k : Nat, beta (k + 1) ≤ beta k - delta) :
    (∀ k : Nat, beta k ≤ beta 0 - (k : Int) * delta) ∧
    (∃ K : Nat, beta K ≤ betaS) := by
  have hBound : ∀ k : Nat, beta k ≤ beta 0 - (k : Int) * delta := by
    intro k
    induction k with
    | zero => simp
    | succ m ih =>
      have hm := hStep m
      have hNat : ((m + 1 : Nat) : Int) = (m : Int) + 1 := by omega
      rw [hNat]; grind
  refine ⟨hBound, ?_⟩
  by_cases h0 : beta 0 ≤ betaS
  · exact ⟨0, h0⟩
  · let K : Nat := (beta 0 - betaS).natAbs + 1
    refine ⟨K, ?_⟩
    have hK := hBound K
    have hAbs : ((beta 0 - betaS).natAbs : Int) = beta 0 - betaS :=
      Int.natAbs_of_nonneg (by omega)
    have h1 : (K : Int) = beta 0 - betaS + 1 := by omega
    have h2 : (K : Int) * 1 ≤ (K : Int) * delta :=
      Int.mul_le_mul_of_nonneg_left (by omega) (by omega)
    omega

/-- Exact 4D Abelian Coulomb fixed-ray barrier (`Theorem B4e`): whenever RG preserves couplings
    above the critical point `betaC` (`betaC <= beta k -> beta k <= beta (k + 1)`), any flow
    initialized in the Coulomb phase `betaC < beta 0` remains permanently deconfined (`betaC < beta k`
    for all `k`). -/
theorem rg_coulomb_fixed_ray_no_confinement (beta : Nat → Int) (betaC : Int)
    (hInit : betaC < beta 0) (hMono : ∀ k : Nat, betaC ≤ beta k → beta k ≤ beta (k + 1)) :
    ∀ k : Nat, betaC < beta k := by
  intro k
  induction k with
  | zero => exact hInit
  | succ m ih =>
    have hm := hMono m (Int.le_of_lt ih)
    omega

/-! ## Section 5: Machine-Checked Dual SOS Bootstrap Certificates (Theorem B5) -/

/-- Exact Hankel `H_0` quadratic form `sum_{i,j < K} v_i v_j M_{i+j}`. -/
def hankel0Val (M : Nat → Int) (K : Nat) (v : Fin K → Int) : Int :=
  sumFin K (fun i => sumFin K (fun j => v i * v j * M (i.val + j.val)))

/-- Exact bounded Hankel `H_1` quadratic form `sum_{i,j < K} v_i v_j (M_{i+j} - M_{i+j+2})`. -/
def hankel1Val (M : Nat → Int) (K : Nat) (v : Fin K → Int) : Int :=
  sumFin K (fun i => sumFin K (fun j => v i * v j * (M (i.val + j.val) - M (i.val + j.val + 2))))

/-- Cosine-basis Toeplitz-Hankel form `sum_{i,j < K} v_i v_j (W_{|i-j|} + W_{i+j})`. -/
def trigCosMatVal (W : Nat → Int) (K : Nat) (v : Fin K → Int) : Int :=
  sumFin K (fun i => sumFin K (fun j => v i * v j * (W (absDiff i.val j.val) + W (i.val + j.val))))

/-- Sine-basis Toeplitz-Hankel form `sum_{i,j = 1..K} v_i v_j (W_{|i-j|} - W_{i+j})`. -/
def trigSinMatVal (W : Nat → Int) (K : Nat) (v : Fin K → Int) : Int :=
  sumFin K (fun i => sumFin K (fun j => v i * v j * (W (absDiff (i.val + 1) (j.val + 1)) - W (i.val + j.val + 2))))

private def listVec {K : Nat} (xs : List Int) (i : Fin K) : Int :=
  xs.getD i.val 0

private def h0CoeffA (p : Int) (N K : Nat) (a : Nat → Int) (v : Fin K → Int) : Int :=
  sumFin K (fun i => sumFin K (fun j =>
    v i * v j * (intPow p (N - (i.val + j.val)) * a (i.val + j.val))))

private def h0CoeffB (p : Int) (N K : Nat) (b : Nat → Int) (v : Fin K → Int) : Int :=
  sumFin K (fun i => sumFin K (fun j =>
    v i * v j * (intPow p (N - (i.val + j.val)) * b (i.val + j.val))))

private def h1CoeffA (p : Int) (N K : Nat) (a : Nat → Int) (v : Fin K → Int) : Int :=
  sumFin K (fun i => sumFin K (fun j =>
    v i * v j * (intPow p (N - (i.val + j.val)) * a (i.val + j.val) -
                 intPow p (N - (i.val + j.val + 2)) * a (i.val + j.val + 2))))

private def h1CoeffB (p : Int) (N K : Nat) (b : Nat → Int) (v : Fin K → Int) : Int :=
  sumFin K (fun i => sumFin K (fun j =>
    v i * v j * (intPow p (N - (i.val + j.val)) * b (i.val + j.val) -
                 intPow p (N - (i.val + j.val + 2)) * b (i.val + j.val + 2))))

private def cosCoeffA (p : Int) (N K : Nat) (a : Nat → Int) (v : Fin K → Int) : Int :=
  sumFin K (fun i => sumFin K (fun j =>
    v i * v j * (intPow p (N - absDiff i.val j.val) * a (absDiff i.val j.val) +
                 intPow p (N - (i.val + j.val)) * a (i.val + j.val))))

private def cosCoeffB (p : Int) (N K : Nat) (b : Nat → Int) (v : Fin K → Int) : Int :=
  sumFin K (fun i => sumFin K (fun j =>
    v i * v j * (intPow p (N - absDiff i.val j.val) * b (absDiff i.val j.val) +
                 intPow p (N - (i.val + j.val)) * b (i.val + j.val))))

private def sinCoeffA (p : Int) (N K : Nat) (a : Nat → Int) (v : Fin K → Int) : Int :=
  sumFin K (fun i => sumFin K (fun j =>
    v i * v j * (intPow p (N - absDiff (i.val + 1) (j.val + 1)) * a (absDiff (i.val + 1) (j.val + 1)) -
                 intPow p (N - (i.val + j.val + 2)) * a (i.val + j.val + 2))))

private def sinCoeffB (p : Int) (N K : Nat) (b : Nat → Int) (v : Fin K → Int) : Int :=
  sumFin K (fun i => sumFin K (fun j =>
    v i * v j * (intPow p (N - absDiff (i.val + 1) (j.val + 1)) * b (absDiff (i.val + 1) (j.val + 1)) -
                 intPow p (N - (i.val + j.val + 2)) * b (i.val + j.val + 2))))

private theorem scaled_moment_eq (p D u : Int) (N : Nat) (M a b : Nat → Int)
    (hAff : ∀ n : Nat, intPow p n * M n = a n * D + b n * u)
    {m : Nat} (hm : m ≤ N) :
    intPow p N * M m = (intPow p (N - m) * a m) * D + (intPow p (N - m) * b m) * u := by
  have hSplit : N = (N - m) + m := by omega
  rw [hSplit, intPow_add, Int.mul_assoc, hAff m]
  grind

private theorem quad_affine_collapse (K : Nat) (v : Fin K → Int) (A B : Fin K → Fin K → Int) (D u : Int) :
    sumFin K (fun i => sumFin K (fun j => v i * v j * (A i j * D + B i j * u))) =
      sumFin K (fun i => sumFin K (fun j => v i * v j * A i j)) * D +
      sumFin K (fun i => sumFin K (fun j => v i * v j * B i j)) * u := by
  have hf :
      (fun i : Fin K => sumFin K (fun j => v i * v j * (A i j * D + B i j * u))) =
      (fun i : Fin K => sumFin K (fun j => v i * v j * A i j) * D +
                        sumFin K (fun j => v i * v j * B i j) * u) := by
    funext i
    rw [← sumFin_mul_right, ← sumFin_mul_right, ← sumFin_add]
    congr 1; funext j; grind
  rw [hf, sumFin_add, sumFin_mul_right, sumFin_mul_right]

private theorem hankel0_affine_cert (p D u : Int) (M a b : Nat → Int) (N K : Nat)
    (v : Fin K → Int) (hBound : 2 * K ≤ N + 2)
    (hAff : ∀ n : Nat, intPow p n * M n = a n * D + b n * u) :
    intPow p N * hankel0Val M K v = h0CoeffA p N K a v * D + h0CoeffB p N K b v * u := by
  unfold hankel0Val h0CoeffA h0CoeffB
  rw [← sumFin_mul_left]
  have hIn :
      (fun i : Fin K => intPow p N * sumFin K (fun j => v i * v j * M (i.val + j.val))) =
      (fun i : Fin K => sumFin K (fun j => v i * v j *
        ((intPow p (N - (i.val + j.val)) * a (i.val + j.val)) * D +
         (intPow p (N - (i.val + j.val)) * b (i.val + j.val)) * u))) := by
    funext i
    rw [← sumFin_mul_left]
    congr 1; funext j
    have hm : i.val + j.val ≤ N := by have hi := i.isLt; have hj := j.isLt; omega
    calc intPow p N * (v i * v j * M (i.val + j.val))
        = v i * v j * (intPow p N * M (i.val + j.val)) := by ac_rfl
      _ = v i * v j * ((intPow p (N - (i.val + j.val)) * a (i.val + j.val)) * D +
                       (intPow p (N - (i.val + j.val)) * b (i.val + j.val)) * u) := by
          rw [scaled_moment_eq p D u N M a b hAff hm]
  rw [hIn]
  exact quad_affine_collapse K v _ _ D u

private theorem hankel1_affine_cert (p D u : Int) (M a b : Nat → Int) (N K : Nat)
    (v : Fin K → Int) (hBound : 2 * K ≤ N)
    (hAff : ∀ n : Nat, intPow p n * M n = a n * D + b n * u) :
    intPow p N * hankel1Val M K v = h1CoeffA p N K a v * D + h1CoeffB p N K b v * u := by
  unfold hankel1Val h1CoeffA h1CoeffB
  rw [← sumFin_mul_left]
  have hIn :
      (fun i : Fin K => intPow p N * sumFin K (fun j => v i * v j * (M (i.val + j.val) - M (i.val + j.val + 2)))) =
      (fun i : Fin K => sumFin K (fun j => v i * v j *
        ((intPow p (N - (i.val + j.val)) * a (i.val + j.val) -
          intPow p (N - (i.val + j.val + 2)) * a (i.val + j.val + 2)) * D +
         (intPow p (N - (i.val + j.val)) * b (i.val + j.val) -
          intPow p (N - (i.val + j.val + 2)) * b (i.val + j.val + 2)) * u))) := by
    funext i
    rw [← sumFin_mul_left]
    congr 1; funext j
    have hm1 : i.val + j.val ≤ N := by have hi := i.isLt; have hj := j.isLt; omega
    have hm2 : i.val + j.val + 2 ≤ N := by have hi := i.isLt; have hj := j.isLt; omega
    calc intPow p N * (v i * v j * (M (i.val + j.val) - M (i.val + j.val + 2)))
        = v i * v j * (intPow p N * M (i.val + j.val) - intPow p N * M (i.val + j.val + 2)) := by grind
      _ = v i * v j * ((intPow p (N - (i.val + j.val)) * a (i.val + j.val) -
                        intPow p (N - (i.val + j.val + 2)) * a (i.val + j.val + 2)) * D +
                       (intPow p (N - (i.val + j.val)) * b (i.val + j.val) -
                        intPow p (N - (i.val + j.val + 2)) * b (i.val + j.val + 2)) * u) := by
          rw [scaled_moment_eq p D u N M a b hAff hm1, scaled_moment_eq p D u N M a b hAff hm2]; grind
  rw [hIn]
  exact quad_affine_collapse K v _ _ D u

private theorem trigCos_affine_cert (p D u : Int) (W a b : Nat → Int) (N K : Nat)
    (v : Fin K → Int) (hBound : 2 * K ≤ N + 2)
    (hAff : ∀ n : Nat, intPow p n * W n = a n * D + b n * u) :
    intPow p N * trigCosMatVal W K v = cosCoeffA p N K a v * D + cosCoeffB p N K b v * u := by
  unfold trigCosMatVal cosCoeffA cosCoeffB
  rw [← sumFin_mul_left]
  have hIn :
      (fun i : Fin K => intPow p N * sumFin K (fun j => v i * v j * (W (absDiff i.val j.val) + W (i.val + j.val)))) =
      (fun i : Fin K => sumFin K (fun j => v i * v j *
        ((intPow p (N - absDiff i.val j.val) * a (absDiff i.val j.val) +
          intPow p (N - (i.val + j.val)) * a (i.val + j.val)) * D +
         (intPow p (N - absDiff i.val j.val) * b (absDiff i.val j.val) +
          intPow p (N - (i.val + j.val)) * b (i.val + j.val)) * u))) := by
    funext i
    rw [← sumFin_mul_left]
    congr 1; funext j
    have hm1 : absDiff i.val j.val ≤ N := by
      have hi := i.isLt; have hj := j.isLt; unfold absDiff; split <;> omega
    have hm2 : i.val + j.val ≤ N := by have hi := i.isLt; have hj := j.isLt; omega
    calc intPow p N * (v i * v j * (W (absDiff i.val j.val) + W (i.val + j.val)))
        = v i * v j * (intPow p N * W (absDiff i.val j.val) + intPow p N * W (i.val + j.val)) := by grind
      _ = v i * v j * ((intPow p (N - absDiff i.val j.val) * a (absDiff i.val j.val) +
                        intPow p (N - (i.val + j.val)) * a (i.val + j.val)) * D +
                       (intPow p (N - absDiff i.val j.val) * b (absDiff i.val j.val) +
                        intPow p (N - (i.val + j.val)) * b (i.val + j.val)) * u) := by
          rw [scaled_moment_eq p D u N W a b hAff hm1, scaled_moment_eq p D u N W a b hAff hm2]; grind
  rw [hIn]
  exact quad_affine_collapse K v _ _ D u

private theorem trigSin_affine_cert (p D u : Int) (W a b : Nat → Int) (N K : Nat)
    (v : Fin K → Int) (hBound : 2 * K ≤ N)
    (hAff : ∀ n : Nat, intPow p n * W n = a n * D + b n * u) :
    intPow p N * trigSinMatVal W K v = sinCoeffA p N K a v * D + sinCoeffB p N K b v * u := by
  unfold trigSinMatVal sinCoeffA sinCoeffB
  rw [← sumFin_mul_left]
  have hIn :
      (fun i : Fin K => intPow p N * sumFin K (fun j => v i * v j *
        (W (absDiff (i.val + 1) (j.val + 1)) - W (i.val + j.val + 2)))) =
      (fun i : Fin K => sumFin K (fun j => v i * v j *
        ((intPow p (N - absDiff (i.val + 1) (j.val + 1)) * a (absDiff (i.val + 1) (j.val + 1)) -
          intPow p (N - (i.val + j.val + 2)) * a (i.val + j.val + 2)) * D +
         (intPow p (N - absDiff (i.val + 1) (j.val + 1)) * b (absDiff (i.val + 1) (j.val + 1)) -
          intPow p (N - (i.val + j.val + 2)) * b (i.val + j.val + 2)) * u))) := by
    funext i
    rw [← sumFin_mul_left]
    congr 1; funext j
    have hm1 : absDiff (i.val + 1) (j.val + 1) ≤ N := by
      have hi := i.isLt; have hj := j.isLt; unfold absDiff; split <;> omega
    have hm2 : i.val + j.val + 2 ≤ N := by have hi := i.isLt; have hj := j.isLt; omega
    calc intPow p N * (v i * v j * (W (absDiff (i.val + 1) (j.val + 1)) - W (i.val + j.val + 2)))
        = v i * v j * (intPow p N * W (absDiff (i.val + 1) (j.val + 1)) -
                       intPow p N * W (i.val + j.val + 2)) := by grind
      _ = v i * v j * ((intPow p (N - absDiff (i.val + 1) (j.val + 1)) * a (absDiff (i.val + 1) (j.val + 1)) -
                        intPow p (N - (i.val + j.val + 2)) * a (i.val + j.val + 2)) * D +
                       (intPow p (N - absDiff (i.val + 1) (j.val + 1)) * b (absDiff (i.val + 1) (j.val + 1)) -
                        intPow p (N - (i.val + j.val + 2)) * b (i.val + j.val + 2)) * u) := by
          rw [scaled_moment_eq p D u N W a b hAff hm1, scaled_moment_eq p D u N W a b hAff hm2]; grind
  rw [hIn]
  exact quad_affine_collapse K v _ _ D u

/-- Machine-checked certified bootstrap witness (`Theorem B5a`):
    2D SU(2) at `beta = 2` (`K = 5`, zero floating-point assumptions) encloses exact
    `I_2(2)/I_1(2) = 0.4331274267...` strictly inside `[0.433125, 0.433129]`. -/
theorem cert_su2_2d_beta2_interval (D u : Int) (M : Nat → Int) (hD : 0 < D)
    (h0 : M 0 = D) (h1 : M 1 = u)
    (hBase : 2 * (M 0 - M 2) = 3 * 1 * M 1)
    (hSd : ∀ k : Nat, 2 * (M (k + 1) - M (k + 3)) = 1 * (((k : Int) + 4) * M (k + 2) - ((k : Int) + 1) * M k))
    (hH1 : 0 ≤ hankel1Val M 4 (listVec [69, -261, -496, 1000]))
    (hH0 : 0 ≤ hankel0Val M 5 (listVec [27, 248, -593, -556, 1000])) :
    433125 * D ≤ 1000000 * u ∧ 1000000 * u ≤ 433129 * D := by
  let vLo := listVec (K := 4) [69, -261, -496, 1000]
  let vHi := listVec (K := 5) [27, 248, -593, -556, 1000]
  have hAff := su2_2d_sd_affine_reduction 2 1 D u M h0 h1 hBase hSd
  have hLinLo := hankel1_affine_cert 2 D u M (su2SdCoeffA 2 1) (su2SdCoeffB 2 1) 8 4 vLo (by omega) hAff
  have hLinHi := hankel0_affine_cert 2 D u M (su2SdCoeffA 2 1) (su2SdCoeffB 2 1) 8 5 vHi (by omega) hAff
  have hSosLo : 0 ≤ h1CoeffA 2 8 4 (su2SdCoeffA 2 1) vLo * D +
                    h1CoeffB 2 8 4 (su2SdCoeffB 2 1) vLo * u := by
    rw [← hLinLo]
    exact Int.mul_nonneg (intPow_nonneg (by omega) 8) hH1
  have hSosHi : 0 ≤ h0CoeffA 2 8 5 (su2SdCoeffA 2 1) vHi * D +
                    h0CoeffB 2 8 5 (su2SdCoeffB 2 1) vHi * u := by
    rw [← hLinHi]
    exact Int.mul_nonneg (intPow_nonneg (by omega) 8) hH0
  have hLoEval : 0 < h1CoeffB 2 8 4 (su2SdCoeffB 2 1) vLo ∧
                 433125 * h1CoeffB 2 8 4 (su2SdCoeffB 2 1) vLo ≤
                   1000000 * (-(h1CoeffA 2 8 4 (su2SdCoeffA 2 1) vLo)) := by
    decide
  have hHiEval : h0CoeffB 2 8 5 (su2SdCoeffB 2 1) vHi < 0 ∧
                 1000000 * h0CoeffA 2 8 5 (su2SdCoeffA 2 1) vHi ≤
                   433129 * (-(h0CoeffB 2 8 5 (su2SdCoeffB 2 1) vHi)) := by
    decide
  have hLo := sd_dual_sos_lower_bound
    (1000000 * h1CoeffA 2 8 4 (su2SdCoeffA 2 1) vLo)
    (h1CoeffB 2 8 4 (su2SdCoeffB 2 1) vLo) D (1000000 * u) (433125 * D)
    (by calc 0 ≤ 1000000 * (h1CoeffA 2 8 4 (su2SdCoeffA 2 1) vLo * D +
                            h1CoeffB 2 8 4 (su2SdCoeffB 2 1) vLo * u) :=
              Int.mul_nonneg (by omega) hSosLo
          _ = _ := by grind)
    hLoEval.1
    (by calc (433125 * D) * h1CoeffB 2 8 4 (su2SdCoeffB 2 1) vLo
            = (433125 * h1CoeffB 2 8 4 (su2SdCoeffB 2 1) vLo) * D := by ac_rfl
          _ ≤ (1000000 * -(h1CoeffA 2 8 4 (su2SdCoeffA 2 1) vLo)) * D :=
              Int.mul_le_mul_of_nonneg_right hLoEval.2 (Int.le_of_lt hD)
          _ = _ := by grind)
  have hHi := sd_dual_sos_upper_bound
    (1000000 * h0CoeffA 2 8 5 (su2SdCoeffA 2 1) vHi)
    (h0CoeffB 2 8 5 (su2SdCoeffB 2 1) vHi) D (1000000 * u) (433129 * D)
    (by calc 0 ≤ 1000000 * (h0CoeffA 2 8 5 (su2SdCoeffA 2 1) vHi * D +
                            h0CoeffB 2 8 5 (su2SdCoeffB 2 1) vHi * u) :=
              Int.mul_nonneg (by omega) hSosHi
          _ = _ := by grind)
    hHiEval.1
    (by calc 1000000 * h0CoeffA 2 8 5 (su2SdCoeffA 2 1) vHi * D
            ≤ (433129 * -(h0CoeffB 2 8 5 (su2SdCoeffB 2 1) vHi)) * D :=
              Int.mul_le_mul_of_nonneg_right hHiEval.2 (Int.le_of_lt hD)
          _ = _ := by grind)
  exact ⟨hLo.2, hHi.2⟩

/-- Machine-checked certified bootstrap witness (`Theorem B5b`):
    2D SU(2) at `beta = 5/2` (`p = 5, q = 2`, `K = 5`) encloses exact `I_2(2.5)/I_1(2.5) = 0.5071951000...`
    strictly inside `[0.507190, 0.507198]`. -/
theorem cert_su2_2d_beta25_interval (D u : Int) (M : Nat → Int) (hD : 0 < D)
    (h0 : M 0 = D) (h1 : M 1 = u)
    (hBase : 5 * (M 0 - M 2) = 3 * 2 * M 1)
    (hSd : ∀ k : Nat, 5 * (M (k + 1) - M (k + 3)) = 2 * (((k : Int) + 4) * M (k + 2) - ((k : Int) + 1) * M k))
    (hH1 : 0 ≤ hankel1Val M 4 (listVec [77, -199, -617, 1000]))
    (hH0 : 0 ≤ hankel0Val M 5 (listVec [11, 290, -505, -696, 1000])) :
    507190 * D ≤ 1000000 * u ∧ 1000000 * u ≤ 507198 * D := by
  let vLo := listVec (K := 4) [77, -199, -617, 1000]
  let vHi := listVec (K := 5) [11, 290, -505, -696, 1000]
  have hAff := su2_2d_sd_affine_reduction 5 2 D u M h0 h1 hBase hSd
  have hLinLo := hankel1_affine_cert 5 D u M (su2SdCoeffA 5 2) (su2SdCoeffB 5 2) 8 4 vLo (by omega) hAff
  have hLinHi := hankel0_affine_cert 5 D u M (su2SdCoeffA 5 2) (su2SdCoeffB 5 2) 8 5 vHi (by omega) hAff
  have hSosLo : 0 ≤ h1CoeffA 5 8 4 (su2SdCoeffA 5 2) vLo * D +
                    h1CoeffB 5 8 4 (su2SdCoeffB 5 2) vLo * u := by
    rw [← hLinLo]
    exact Int.mul_nonneg (intPow_nonneg (by omega) 8) hH1
  have hSosHi : 0 ≤ h0CoeffA 5 8 5 (su2SdCoeffA 5 2) vHi * D +
                    h0CoeffB 5 8 5 (su2SdCoeffB 5 2) vHi * u := by
    rw [← hLinHi]
    exact Int.mul_nonneg (intPow_nonneg (by omega) 8) hH0
  have hLoEval : 0 < h1CoeffB 5 8 4 (su2SdCoeffB 5 2) vLo ∧
                 507190 * h1CoeffB 5 8 4 (su2SdCoeffB 5 2) vLo ≤
                   1000000 * (-(h1CoeffA 5 8 4 (su2SdCoeffA 5 2) vLo)) := by
    decide
  have hHiEval : h0CoeffB 5 8 5 (su2SdCoeffB 5 2) vHi < 0 ∧
                 1000000 * h0CoeffA 5 8 5 (su2SdCoeffA 5 2) vHi ≤
                   507198 * (-(h0CoeffB 5 8 5 (su2SdCoeffB 5 2) vHi)) := by
    decide
  have hLo := sd_dual_sos_lower_bound
    (1000000 * h1CoeffA 5 8 4 (su2SdCoeffA 5 2) vLo)
    (h1CoeffB 5 8 4 (su2SdCoeffB 5 2) vLo) D (1000000 * u) (507190 * D)
    (by calc 0 ≤ 1000000 * (h1CoeffA 5 8 4 (su2SdCoeffA 5 2) vLo * D +
                            h1CoeffB 5 8 4 (su2SdCoeffB 5 2) vLo * u) :=
              Int.mul_nonneg (by omega) hSosLo
          _ = _ := by grind)
    hLoEval.1
    (by calc (507190 * D) * h1CoeffB 5 8 4 (su2SdCoeffB 5 2) vLo
            = (507190 * h1CoeffB 5 8 4 (su2SdCoeffB 5 2) vLo) * D := by ac_rfl
          _ ≤ (1000000 * -(h1CoeffA 5 8 4 (su2SdCoeffA 5 2) vLo)) * D :=
              Int.mul_le_mul_of_nonneg_right hLoEval.2 (Int.le_of_lt hD)
          _ = _ := by grind)
  have hHi := sd_dual_sos_upper_bound
    (1000000 * h0CoeffA 5 8 5 (su2SdCoeffA 5 2) vHi)
    (h0CoeffB 5 8 5 (su2SdCoeffB 5 2) vHi) D (1000000 * u) (507198 * D)
    (by calc 0 ≤ 1000000 * (h0CoeffA 5 8 5 (su2SdCoeffA 5 2) vHi * D +
                            h0CoeffB 5 8 5 (su2SdCoeffB 5 2) vHi * u) :=
              Int.mul_nonneg (by omega) hSosHi
          _ = _ := by grind)
    hHiEval.1
    (by calc 1000000 * h0CoeffA 5 8 5 (su2SdCoeffA 5 2) vHi * D
            ≤ (507198 * -(h0CoeffB 5 8 5 (su2SdCoeffB 5 2) vHi)) * D :=
              Int.mul_le_mul_of_nonneg_right hHiEval.2 (Int.le_of_lt hD)
          _ = _ := by grind)
  exact ⟨hLo.2, hHi.2⟩

/-- Machine-checked certified bootstrap witness (`Theorem B5c`):
    2D U(1) at `beta = 2` (`p = 2, q = 1`, `K = 6` cosine/sine Toeplitz-Hankel matrices) encloses
    exact `I_1(2)/I_0(2) = 0.6977746579...` strictly inside `[0.697774, 0.697776]`. -/
theorem cert_u1_2d_beta2_interval (D u : Int) (W : Nat → Int) (hD : 0 < D)
    (h0 : W 0 = D) (h1 : W 1 = u)
    (hSd : ∀ n : Nat, 2 * (W n - W (n + 2)) = 2 * ((n : Int) + 1) * 1 * W (n + 1))
    (hSin : 0 ≤ trigSinMatVal W 5 (listVec [60, -215, 564, -1000, 899]))
    (hCos : 0 ≤ trigCosMatVal W 6 (listVec [-15, 66, -216, 564, -1000, 899])) :
    697774 * D ≤ 1000000 * u ∧ 1000000 * u ≤ 697776 * D := by
  let vLo := listVec (K := 5) [60, -215, 564, -1000, 899]
  let vHi := listVec (K := 6) [-15, 66, -216, 564, -1000, 899]
  have hAff := u1_2d_sd_affine_reduction 2 1 D u W h0 h1 hSd
  have hLinLo := trigSin_affine_cert 2 D u W (u1SdCoeffA 2 1) (u1SdCoeffB 2 1) 10 5 vLo (by omega) hAff
  have hLinHi := trigCos_affine_cert 2 D u W (u1SdCoeffA 2 1) (u1SdCoeffB 2 1) 10 6 vHi (by omega) hAff
  have hSosLo : 0 ≤ sinCoeffA 2 10 5 (u1SdCoeffA 2 1) vLo * D +
                    sinCoeffB 2 10 5 (u1SdCoeffB 2 1) vLo * u := by
    rw [← hLinLo]
    exact Int.mul_nonneg (intPow_nonneg (by omega) 10) hSin
  have hSosHi : 0 ≤ cosCoeffA 2 10 6 (u1SdCoeffA 2 1) vHi * D +
                    cosCoeffB 2 10 6 (u1SdCoeffB 2 1) vHi * u := by
    rw [← hLinHi]
    exact Int.mul_nonneg (intPow_nonneg (by omega) 10) hCos
  have hLoEval : 0 < sinCoeffB 2 10 5 (u1SdCoeffB 2 1) vLo ∧
                 697774 * sinCoeffB 2 10 5 (u1SdCoeffB 2 1) vLo ≤
                   1000000 * (-(sinCoeffA 2 10 5 (u1SdCoeffA 2 1) vLo)) := by
    decide
  have hHiEval : cosCoeffB 2 10 6 (u1SdCoeffB 2 1) vHi < 0 ∧
                 1000000 * cosCoeffA 2 10 6 (u1SdCoeffA 2 1) vHi ≤
                   697776 * (-(cosCoeffB 2 10 6 (u1SdCoeffB 2 1) vHi)) := by
    decide
  have hLo := sd_dual_sos_lower_bound
    (1000000 * sinCoeffA 2 10 5 (u1SdCoeffA 2 1) vLo)
    (sinCoeffB 2 10 5 (u1SdCoeffB 2 1) vLo) D (1000000 * u) (697774 * D)
    (by calc 0 ≤ 1000000 * (sinCoeffA 2 10 5 (u1SdCoeffA 2 1) vLo * D +
                            sinCoeffB 2 10 5 (u1SdCoeffB 2 1) vLo * u) :=
              Int.mul_nonneg (by omega) hSosLo
          _ = _ := by grind)
    hLoEval.1
    (by calc (697774 * D) * sinCoeffB 2 10 5 (u1SdCoeffB 2 1) vLo
            = (697774 * sinCoeffB 2 10 5 (u1SdCoeffB 2 1) vLo) * D := by ac_rfl
          _ ≤ (1000000 * -(sinCoeffA 2 10 5 (u1SdCoeffA 2 1) vLo)) * D :=
              Int.mul_le_mul_of_nonneg_right hLoEval.2 (Int.le_of_lt hD)
          _ = _ := by grind)
  have hHi := sd_dual_sos_upper_bound
    (1000000 * cosCoeffA 2 10 6 (u1SdCoeffA 2 1) vHi)
    (cosCoeffB 2 10 6 (u1SdCoeffB 2 1) vHi) D (1000000 * u) (697776 * D)
    (by calc 0 ≤ 1000000 * (cosCoeffA 2 10 6 (u1SdCoeffA 2 1) vHi * D +
                            cosCoeffB 2 10 6 (u1SdCoeffB 2 1) vHi * u) :=
              Int.mul_nonneg (by omega) hSosHi
          _ = _ := by grind)
    hHiEval.1
    (by calc 1000000 * cosCoeffA 2 10 6 (u1SdCoeffA 2 1) vHi * D
            ≤ (697776 * -(cosCoeffB 2 10 6 (u1SdCoeffB 2 1) vHi)) * D :=
              Int.mul_le_mul_of_nonneg_right hHiEval.2 (Int.le_of_lt hD)
          _ = _ := by grind)
  exact ⟨hLo.2, hHi.2⟩

/-- Machine-checked non-perturbative 4D SU(2) plaquette upper bound (`Theorem B5d`):
    at `beta = 5/2` (`p = 5, q = 2`), any infinite-volume 4D SU(2) Schwinger-Dyson equilibrium
    satisfies `1000 * uP <= 905 * S` (`u_P <= 0.905`, consistent with Stage 4 MC `0.65214`). -/
theorem cert_4d_su2_beta25_upper (S uP uP2 diagVar coplDiff orthDiff : Int)
    (hS : 0 < S)
    (hSymNonneg : 0 ≤ 6 * (diagVar + coplDiff + 4 * orthDiff))
    (hCS : 6 * (diagVar + coplDiff + 4 * orthDiff) ≤ 36 * diagVar)
    (hDiag : diagVar = S * S - uP2)
    (hVar : uP * uP ≤ uP2)
    (hSdEq : 3 * 2 * S * uP = 5 * (diagVar + coplDiff + 4 * orthDiff)) :
    0 ≤ uP ∧ 1000 * uP ≤ 905 * S := by
  have hQ := su2_4d_sd_plaquette_quadratic_bound 5 2 S uP uP2 diagVar coplDiff orthDiff
    (by omega) (by omega) hS hSymNonneg hCS hDiag hVar hSdEq
  have hQuad : 10 * (uP * uP) + 2 * S * uP ≤ 10 * (S * S) := by grind
  refine ⟨hQ.1, ?_⟩
  by_cases hGt : 905 * S < 1000 * uP
  · have hSq1 : 905 * S * (905 * S) < 905 * S * (1000 * uP) :=
      Int.mul_lt_mul_of_pos_left hGt (by omega)
    have hSq2 : 905 * S * (1000 * uP) < 1000 * uP * (1000 * uP) :=
      Int.mul_lt_mul_of_pos_right hGt (by omega)
    have hSq : 905 * S * (905 * S) < 1000 * uP * (1000 * uP) :=
      Int.lt_trans hSq1 hSq2
    have hL : 10 * (1000 * uP * (1000 * uP)) + 2000 * S * (1000 * uP) ≤ 10000000 * (S * S) := by
      have : 10 * (1000 * uP * (1000 * uP)) + 2000 * S * (1000 * uP) =
             1000000 * (10 * (uP * uP) + 2 * S * uP) := by grind
      omega
    have hNum : 10 * (905 * S * (905 * S)) + 2000 * S * (905 * S) = 10000250 * (S * S) := by grind
    have hPosSS : 0 < S * S := Int.mul_pos hS hS
    have h2 : 2000 * S * (905 * S) < 2000 * S * (1000 * uP) :=
      Int.mul_lt_mul_of_pos_left hGt (by omega)
    omega
  · omega

end Flow.LatticeBootstrap