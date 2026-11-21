/-
  Flow.ConstructiveQFT — Osterwalder-Schrader Reflection Positivity, Mass Gap & Variational GEVP
  =================================================================================================
  Pure Lean 4 (Init-only, zero IO) constructive Euclidean lattice field theory core building atop
  `Flow.YangMillsBundle`:
    1. Exact Osterwalder-Schrader reflection positivity, Schur product closure & Cauchy-Schwarz (Theorem Q1);
    2. Transfer-matrix Gram positivity, Hankel log-convexity & spectral mass gap upper bound (Theorem Q2);
    3. Strong-coupling area-law bound & exact Creutz ratio perimeter cancellation (Theorem Q3);
    4. Variational GEVP / multi-smearing Rayleigh-Ritz ground-state monotonicity & bound (Theorem Q4);
    5. Connected vacuum variance positivity & exponential clustering decay (Theorem Q5).
-/
import Flow.YangMillsBundle

namespace Flow.ConstructiveQFT

open Flow.GaugeBundle
open Flow.ProjectiveBundle
open Flow.YangMillsBundle

/-! ## Helpers: Generalized `sumFin` Fubini Swap, Exponentiation & Weighted Cauchy-Schwarz -/

private theorem int_sq_nonneg (z : Int) : 0 ≤ z * z := by
  by_cases hz : 0 ≤ z
  · exact Int.mul_nonneg hz hz
  · have hneg : 0 ≤ -z := by omega
    have hsq : (-z) * (-z) = z * z := Int.neg_mul_neg z z
    rw [← hsq]
    exact Int.mul_nonneg hneg hneg

private theorem sumPrefix_mul_right {N : Nat} (f : Fin N → Int) (c : Int) (k : Nat) :
    sumPrefix (fun i => f i * c) k = sumPrefix f k * c := by
  induction k with
  | zero => simp [sumPrefix]
  | succ k ih =>
    simp only [sumPrefix, ih]
    split
    · rw [Int.add_mul]
    · simp

theorem sumFin_mul_right (N : Nat) (f : Fin N → Int) (c : Int) :
    sumFin N (fun i => f i * c) = sumFin N f * c :=
  sumPrefix_mul_right f c N

private theorem sumPrefix_swap_gen {A B : Nat} (g : Fin A → Fin B → Int) (ka kb : Nat) :
    sumPrefix (fun a => sumPrefix (fun b => g a b) kb) ka =
      sumPrefix (fun b => sumPrefix (fun a => g a b) ka) kb := by
  induction ka with
  | zero =>
    simp only [sumPrefix]
    exact (sumPrefix_zero kb).symm
  | succ ka ih =>
    have hsplit :
        (fun b : Fin B => sumPrefix (fun a : Fin A => g a b) (ka + 1)) =
        (fun b : Fin B => sumPrefix (fun a : Fin A => g a b) ka +
            if ha : ka < A then g ⟨ka, ha⟩ b else 0) := by
      funext b
      simp only [sumPrefix]
    rw [hsplit, sumPrefix_add]
    simp only [sumPrefix]
    rw [ih]
    congr 1
    split
    · rfl
    · exact (sumPrefix_zero kb).symm

theorem sumFin_swap_gen (A B : Nat) (g : Fin A → Fin B → Int) :
    sumFin A (fun a => sumFin B (fun b => g a b)) =
      sumFin B (fun b => sumFin A (fun a => g a b)) :=
  sumPrefix_swap_gen g A B

private theorem sumFin_bilinear_sq (N : Nat) (a x y : Fin N → Int) (A B : Int) :
    sumFin N (fun k => a k * ((A * x k + B * y k) * (A * x k + B * y k))) =
      A * A * sumFin N (fun k => a k * (x k * x k)) +
      2 * A * B * sumFin N (fun k => a k * (x k * y k)) +
      B * B * sumFin N (fun k => a k * (y k * y k)) := by
  have hfn :
      (fun k => a k * ((A * x k + B * y k) * (A * x k + B * y k))) =
      (fun k => (A * A * (a k * (x k * x k)) + 2 * A * B * (a k * (x k * y k))) +
        B * B * (a k * (y k * y k))) := by
    funext k
    grind
  rw [hfn, sumFin_add, sumFin_add, sumFin_mul_left, sumFin_mul_left, sumFin_mul_left]

/-- Exact weighted Cauchy-Schwarz inequality over `Fin N` for arbitrary nonnegative weights `a k >= 0`. -/
theorem weighted_cauchy_schwarz {N : Nat} (a x y : Fin N → Int) (ha : ∀ k, 0 ≤ a k) :
    sumFin N (fun k => a k * (x k * y k)) * sumFin N (fun k => a k * (x k * y k)) ≤
      sumFin N (fun k => a k * (x k * x k)) * sumFin N (fun k => a k * (y k * y k)) := by
  let Sxx := sumFin N (fun k => a k * (x k * x k))
  let Sxy := sumFin N (fun k => a k * (x k * y k))
  let Syy := sumFin N (fun k => a k * (y k * y k))
  have hQuad : ∀ A B : Int, 0 ≤ A * A * Sxx + 2 * A * B * Sxy + B * B * Syy := by
    intro A B
    rw [← sumFin_bilinear_sq N a x y A B]
    exact sumFin_nonneg N _ fun k => Int.mul_nonneg (ha k) (int_sq_nonneg _)
  have hXX : 0 ≤ Sxx := sumFin_nonneg N _ fun k => Int.mul_nonneg (ha k) (int_sq_nonneg (x k))
  have hYY : 0 ≤ Syy := sumFin_nonneg N _ fun k => Int.mul_nonneg (ha k) (int_sq_nonneg (y k))
  by_cases hY0 : Syy = 0
  · have hXY0 : Sxy = 0 := by
      by_cases hNe : Sxy = 0
      · exact hNe
      · by_cases hPos : 0 < Sxy
        · have hNeg := hQuad 1 (-(Sxx + 1))
          have hPosTail : 0 ≤ (Sxx + 1) * (Sxy - 1) :=
            Int.mul_nonneg (by omega) (by omega)
          have hExp :
              1 * 1 * Sxx + 2 * 1 * (-(Sxx + 1)) * Sxy + (-(Sxx + 1)) * (-(Sxx + 1)) * Syy =
              1 * 1 * Sxx + (-2 * (Sxx + 1) - 2 * ((Sxx + 1) * (Sxy - 1))) +
                (-(Sxx + 1)) * (-(Sxx + 1)) * Syy := by
            grind
          rw [hExp, hY0] at hNeg
          omega
        · have hNeg := hQuad 1 (Sxx + 1)
          have hPosTail : 0 ≤ (Sxx + 1) * (-Sxy - 1) :=
            Int.mul_nonneg (by omega) (by omega)
          have hExp :
              1 * 1 * Sxx + 2 * 1 * (Sxx + 1) * Sxy + (Sxx + 1) * (Sxx + 1) * Syy =
              1 * 1 * Sxx + (-2 * (Sxx + 1) - 2 * ((Sxx + 1) * (-Sxy - 1))) +
                (Sxx + 1) * (Sxx + 1) * Syy := by
            grind
          rw [hExp, hY0] at hNeg
          omega
    change Sxy * Sxy ≤ Sxx * Syy
    rw [hY0, hXY0]
    simp
  · have hYPos : 0 < Syy := by omega
    have hD := hQuad Syy (-Sxy)
    have hFactor :
        Syy * Syy * Sxx + 2 * Syy * (-Sxy) * Sxy + (-Sxy) * (-Sxy) * Syy =
        Syy * (Sxx * Syy - Sxy * Sxy) := by
      grind
    rw [hFactor] at hD
    by_cases hLt : Sxx * Syy - Sxy * Sxy < 0
    · have hNeg := Int.mul_neg_of_pos_of_neg hYPos hLt
      omega
    · change Sxy * Sxy ≤ Sxx * Syy
      omega

def intPow (x : Int) : Nat → Int
  | 0     => 1
  | n + 1 => x * intPow x n

@[simp] private theorem intPow_zero (x : Int) : intPow x 0 = 1 := rfl
@[simp] private theorem intPow_succ (x : Int) (n : Nat) : intPow x (n + 1) = x * intPow x n := rfl

theorem intPow_nonneg {x : Int} (hx : 0 ≤ x) (n : Nat) : 0 ≤ intPow x n := by
  induction n with
  | zero => simp [intPow]
  | succ k ih => rw [intPow_succ]; exact Int.mul_nonneg hx ih

theorem intPow_pos {x : Int} (hx : 0 < x) (n : Nat) : 0 < intPow x n := by
  induction n with
  | zero => simp [intPow]
  | succ k ih => rw [intPow_succ]; exact Int.mul_pos hx ih

theorem intPow_mono {p q : Int} (hp : 0 ≤ p) (hpq : p ≤ q) (n : Nat) :
    intPow p n ≤ intPow q n := by
  induction n with
  | zero => simp [intPow]
  | succ k ih =>
    rw [intPow_succ, intPow_succ]
    have hq0 : 0 ≤ q := by omega
    have hp0 : 0 ≤ intPow p k := intPow_nonneg hp k
    exact Int.mul_le_mul hpq ih hp0 hq0

theorem intPow_add (x : Int) (m n : Nat) :
    intPow x (m + n) = intPow x m * intPow x n := by
  induction m with
  | zero => simp [intPow]
  | succ k ih =>
    have hEq : k + 1 + n = (k + n) + 1 := by omega
    rw [hEq, intPow_succ, intPow_succ, ih, Int.mul_assoc]

/-! ## Section 1: Exact Osterwalder-Schrader Reflection Positivity & Schur Closure (Theorem Q1) -/

def osKernel (R M : Nat) (lam : Fin R → Int) (phi : Fin R → Fin M → Int) (i j : Fin M) : Int :=
  sumFin R (fun r => lam r * (phi r i * phi r j))

def osForm (R M : Nat) (w : Fin M → Int) (lam : Fin R → Int) (phi : Fin R → Fin M → Int)
    (F G : Fin M → Int) : Int :=
  sumFin M (fun i => sumFin M (fun j => w i * w j * osKernel R M lam phi i j * F i * G j))

/-- Theorem Q1A (Exact Rank-R Reflection Factorization): Every reflected cross-hyperplane
    correlator `<Theta F, G>` factorizes strictly as `sum_r lam_r * <Theta phi_r, F> * <phi_r, G>`. -/
theorem os_form_factorizes (R M : Nat) (w : Fin M → Int) (lam : Fin R → Int)
    (phi : Fin R → Fin M → Int) (F G : Fin M → Int) :
    osForm R M w lam phi F G =
      sumFin R (fun r =>
        lam r * (sumFin M (fun i => w i * phi r i * F i) * sumFin M (fun j => w j * phi r j * G j))) := by
  unfold osForm osKernel
  have hInner :
      (fun i => sumFin M (fun j => w i * w j * sumFin R (fun r => lam r * (phi r i * phi r j)) * F i * G j)) =
      (fun i => sumFin M (fun j => sumFin R (fun r => lam r * ((w i * phi r i * F i) * (w j * phi r j * G j))))) := by
    funext i
    congr 1
    funext j
    have hMul :
        sumFin R (fun r => lam r * (phi r i * phi r j)) * (w i * F i * (w j * G j)) =
        sumFin R (fun r => lam r * (phi r i * phi r j) * (w i * F i * (w j * G j))) :=
      (sumFin_mul_right R (fun r => lam r * (phi r i * phi r j)) _).symm
    have hLhs :
        w i * w j * sumFin R (fun r => lam r * (phi r i * phi r j)) * F i * G j =
        sumFin R (fun r => lam r * (phi r i * phi r j)) * (w i * F i * (w j * G j)) := by
      ac_rfl
    rw [hLhs, hMul]
    congr 1
    funext r
    ac_rfl
  rw [hInner]
  have hSwapJ :
      (fun i => sumFin M (fun j => sumFin R (fun r => lam r * ((w i * phi r i * F i) * (w j * phi r j * G j))))) =
      (fun i => sumFin R (fun r => sumFin M (fun j => lam r * ((w i * phi r i * F i) * (w j * phi r j * G j))))) := by
    funext i
    exact sumFin_swap_gen M R _
  rw [hSwapJ, sumFin_swap_gen M R]
  congr 1
  funext r
  have hPullJ :
      (fun i => sumFin M (fun j => lam r * ((w i * phi r i * F i) * (w j * phi r j * G j)))) =
      (fun i => (lam r * (w i * phi r i * F i)) * sumFin M (fun j => w j * phi r j * G j)) := by
    funext i
    rw [← sumFin_mul_left M (lam r * (w i * phi r i * F i)) (fun j => w j * phi r j * G j)]
    congr 1
    funext j
    ac_rfl
  rw [hPullJ, sumFin_mul_right M, sumFin_mul_left M]
  ac_rfl

/-- Theorem Q1B (Osterwalder-Schrader Reflection Positivity): For all nonnegative spectral weights
    `lam r >= 0`, arbitrary functional weights `w`, and arbitrary test observables `F`, `<Theta F, F> >= 0`. -/
theorem os_reflection_positivity_exact (R M : Nat) (w : Fin M → Int) (lam : Fin R → Int)
    (phi : Fin R → Fin M → Int) (F : Fin M → Int) (hlam : ∀ r, 0 ≤ lam r) :
    0 ≤ osForm R M w lam phi F F := by
  rw [os_form_factorizes]
  exact sumFin_nonneg R _ fun r => Int.mul_nonneg (hlam r) (int_sq_nonneg _)

/-- Theorem Q1C (Schur/Hadamard Closure of Reflection-Positive Kernels): The pointwise product
    of two reflection-positive cross-hyperplane kernels preserves exact OS positivity, enabling
    nonnegative transfer-matrix products and character/Boltzmann series expansions. -/
theorem os_schur_product_positivity (R1 R2 M : Nat) (w : Fin M → Int)
    (lam1 : Fin R1 → Int) (phi1 : Fin R1 → Fin M → Int) (h1 : ∀ r, 0 ≤ lam1 r)
    (lam2 : Fin R2 → Int) (phi2 : Fin R2 → Fin M → Int) (h2 : ∀ r, 0 ≤ lam2 r)
    (F : Fin M → Int) :
    0 ≤ sumFin M (fun i => sumFin M (fun j =>
      w i * w j * (osKernel R1 M lam1 phi1 i j * osKernel R2 M lam2 phi2 i j) * F i * F j)) := by
  have hExpand :
      sumFin M (fun i => sumFin M (fun j =>
        w i * w j * (osKernel R1 M lam1 phi1 i j * osKernel R2 M lam2 phi2 i j) * F i * F j)) =
      sumFin R1 (fun r1 => sumFin R2 (fun r2 =>
        (lam1 r1 * lam2 r2) *
          (sumFin M (fun i => w i * phi1 r1 i * phi2 r2 i * F i) *
           sumFin M (fun j => w j * phi1 r1 j * phi2 r2 j * F j)))) := by
    have hPoint :
        (fun i => sumFin M (fun j =>
          w i * w j * (osKernel R1 M lam1 phi1 i j * osKernel R2 M lam2 phi2 i j) * F i * F j)) =
        (fun i => sumFin M (fun j =>
          sumFin R1 (fun r1 => sumFin R2 (fun r2 =>
            (lam1 r1 * lam2 r2) *
              ((w i * phi1 r1 i * phi2 r2 i * F i) * (w j * phi1 r1 j * phi2 r2 j * F j)))))) := by
      funext i; congr 1; funext j
      unfold osKernel
      have hK12 :
          sumFin R1 (fun r1 => lam1 r1 * (phi1 r1 i * phi1 r1 j)) *
            sumFin R2 (fun r2 => lam2 r2 * (phi2 r2 i * phi2 r2 j)) =
          sumFin R1 (fun r1 => sumFin R2 (fun r2 =>
            (lam1 r1 * lam2 r2) * ((phi1 r1 i * phi2 r2 i) * (phi1 r1 j * phi2 r2 j)))) := by
        rw [← sumFin_mul_right R1 _ _]
        congr 1; funext r1
        rw [← sumFin_mul_left R2 _ _]
        congr 1; funext r2
        ac_rfl
      have hW :
          w i * w j * (sumFin R1 (fun r1 => lam1 r1 * (phi1 r1 i * phi1 r1 j)) *
            sumFin R2 (fun r2 => lam2 r2 * (phi2 r2 i * phi2 r2 j))) * F i * F j =
          (sumFin R1 (fun r1 => lam1 r1 * (phi1 r1 i * phi1 r1 j)) *
            sumFin R2 (fun r2 => lam2 r2 * (phi2 r2 i * phi2 r2 j))) * (w i * F i * (w j * F j)) := by
        ac_rfl
      rw [hW, hK12, ← sumFin_mul_right R1]
      congr 1; funext r1
      rw [← sumFin_mul_right R2]
      congr 1; funext r2
      ac_rfl
    rw [hPoint]
    have hSwap1 :
        (fun i => sumFin M (fun j => sumFin R1 (fun r1 => sumFin R2 (fun r2 =>
          (lam1 r1 * lam2 r2) * ((w i * phi1 r1 i * phi2 r2 i * F i) * (w j * phi1 r1 j * phi2 r2 j * F j)))))) =
        (fun i => sumFin R1 (fun r1 => sumFin M (fun j => sumFin R2 (fun r2 =>
          (lam1 r1 * lam2 r2) * ((w i * phi1 r1 i * phi2 r2 i * F i) * (w j * phi1 r1 j * phi2 r2 j * F j)))))) := by
      funext i; exact sumFin_swap_gen M R1 _
    rw [hSwap1, sumFin_swap_gen M R1]
    congr 1; funext r1
    have hSwap2 :
        (fun i => sumFin M (fun j => sumFin R2 (fun r2 =>
          (lam1 r1 * lam2 r2) * ((w i * phi1 r1 i * phi2 r2 i * F i) * (w j * phi1 r1 j * phi2 r2 j * F j))))) =
        (fun i => sumFin R2 (fun r2 => sumFin M (fun j =>
          (lam1 r1 * lam2 r2) * ((w i * phi1 r1 i * phi2 r2 i * F i) * (w j * phi1 r1 j * phi2 r2 j * F j))))) := by
      funext i; exact sumFin_swap_gen M R2 _
    rw [hSwap2, sumFin_swap_gen M R2]
    congr 1; funext r2
    have hPullJ :
        (fun i => sumFin M (fun j =>
          (lam1 r1 * lam2 r2) * ((w i * phi1 r1 i * phi2 r2 i * F i) * (w j * phi1 r1 j * phi2 r2 j * F j)))) =
        (fun i => ((lam1 r1 * lam2 r2) * (w i * phi1 r1 i * phi2 r2 i * F i)) *
          sumFin M (fun j => w j * phi1 r1 j * phi2 r2 j * F j)) := by
      funext i
      rw [← sumFin_mul_left M ((lam1 r1 * lam2 r2) * (w i * phi1 r1 i * phi2 r2 i * F i))]
      congr 1; funext j; ac_rfl
    rw [hPullJ, sumFin_mul_right M, sumFin_mul_left M]
    ac_rfl
  rw [hExpand]
  exact sumFin_nonneg R1 _ fun r1 =>
    sumFin_nonneg R2 _ fun r2 =>
      Int.mul_nonneg (Int.mul_nonneg (h1 r1) (h2 r2)) (int_sq_nonneg _)

/-- Theorem Q1D (Osterwalder-Schrader Hilbert Seminorm Cauchy-Schwarz Inequality):
    `<Theta F, G>^2 <= <Theta F, F> * <Theta G, G>`, guaranteeing a genuine OS Hilbert inner product. -/
theorem os_cauchy_schwarz (R M : Nat) (w : Fin M → Int) (lam : Fin R → Int)
    (phi : Fin R → Fin M → Int) (F G : Fin M → Int) (hlam : ∀ r, 0 ≤ lam r) :
    osForm R M w lam phi F G * osForm R M w lam phi F G ≤
      osForm R M w lam phi F F * osForm R M w lam phi G G := by
  rw [os_form_factorizes, os_form_factorizes, os_form_factorizes]
  exact weighted_cauchy_schwarz lam
    (fun r => sumFin M (fun i => w i * phi r i * F i))
    (fun r => sumFin M (fun i => w i * phi r i * G i))
    hlam

/-! ## Section 2: Transfer-Matrix Gram Positivity, Log-Convexity & Spectral Mass Gap (Theorem Q2) -/

def matVec (M : Nat) (K : Fin M → Fin M → Int) (v : Fin M → Int) (r : Fin M) : Int :=
  sumFin M (fun c => K r c * v c)

/-- Theorem Q2A (Transfer Matrix Gram Positivity `T = K^T K >= 0`): Any symmetric factored
    lattice transfer matrix defines a positive semidefinite quadratic form on Hilbert slices. -/
theorem transfer_gram_positive (M : Nat) (K : Fin M → Fin M → Int) (v : Fin M → Int) :
    0 ≤ sumFin M (fun i => sumFin M (fun j => v i * sumFin M (fun k => K k i * K k j) * v j)) := by
  have hEq :
      sumFin M (fun i => sumFin M (fun j => v i * sumFin M (fun k => K k i * K k j) * v j)) =
      sumFin M (fun k => matVec M K v k * matVec M K v k) := by
    have hIn :
        (fun i => sumFin M (fun j => v i * sumFin M (fun k => K k i * K k j) * v j)) =
        (fun i => sumFin M (fun k => sumFin M (fun j => (K k i * v i) * (K k j * v j)))) := by
      funext i
      have hJ :
          (fun j => v i * sumFin M (fun k => K k i * K k j) * v j) =
          (fun j => sumFin M (fun k => (K k i * v i) * (K k j * v j))) := by
        funext j
        have hW :
            v i * sumFin M (fun k => K k i * K k j) * v j =
            sumFin M (fun k => K k i * K k j) * (v i * v j) := by ac_rfl
        rw [hW, ← sumFin_mul_right M]
        congr 1; funext k; ac_rfl
      rw [hJ, sumFin_swap_gen M M]
    rw [hIn, sumFin_swap_gen M M]
    congr 1; funext k
    have hPull :
        (fun i => sumFin M (fun j => (K k i * v i) * (K k j * v j))) =
        (fun i => (K k i * v i) * matVec M K v k) := by
      funext i; unfold matVec; rw [← sumFin_mul_left M]
    rw [hPull, sumFin_mul_right M]
    rfl
  rw [hEq]
  exact sumFin_nonneg M _ fun k => int_sq_nonneg _

def spectralCorr (K : Nat) (c mu : Fin K → Int) (t : Nat) : Int :=
  sumFin K (fun k => c k * intPow (mu k) t)

/-- Theorem Q2B (Correlator Positivity): Every spectral sum `sum_k c_k mu_k^t` with `c_k >= 0, mu_k >= 0`
    remains nonnegative at all Euclidean times `t >= 0`. -/
theorem correlator_nonneg (K : Nat) (c mu : Fin K → Int) (t : Nat)
    (hc : ∀ k, 0 ≤ c k) (hmu : ∀ k, 0 ≤ mu k) :
    0 ≤ spectralCorr K c mu t := by
  unfold spectralCorr
  exact sumFin_nonneg K _ fun k => Int.mul_nonneg (hc k) (intPow_nonneg (hmu k) t)

/-- Theorem Q2C (Hankel Log-Convexity / Effective-Mass Monotonicity): For every reflection-positive
    correlator `C(t) = sum_k c_k mu_k^t`, `C(t+1)^2 <= C(t) * C(t+2)`, proving monotone
    convergence of effective masses `m_eff(t) = ln(C(t)/C(t+1))` from above toward the mass gap. -/
theorem correlator_log_convex (K : Nat) (c mu : Fin K → Int) (t : Nat)
    (hc : ∀ k, 0 ≤ c k) (hmu : ∀ k, 0 ≤ mu k) :
    spectralCorr K c mu (t + 1) * spectralCorr K c mu (t + 1) ≤
      spectralCorr K c mu t * spectralCorr K c mu (t + 2) := by
  have hCS := weighted_cauchy_schwarz
    (fun k => c k * intPow (mu k) t)
    (fun _ => 1)
    mu
    (fun k => Int.mul_nonneg (hc k) (intPow_nonneg (hmu k) t))
  have h0 :
      sumFin K (fun k => (c k * intPow (mu k) t) * (1 * 1)) = spectralCorr K c mu t := by
    unfold spectralCorr; congr 1; funext k; grind
  have h1 :
      sumFin K (fun k => (c k * intPow (mu k) t) * (1 * mu k)) = spectralCorr K c mu (t + 1) := by
    unfold spectralCorr; simp only [intPow_succ]; congr 1; funext k; ac_rfl
  have h2 :
      sumFin K (fun k => (c k * intPow (mu k) t) * (mu k * mu k)) = spectralCorr K c mu (t + 2) := by
    unfold spectralCorr; simp only [intPow_succ]; congr 1; funext k; ac_rfl
  rw [h0, h1, h2] at hCS
  exact hCS

/-- Theorem Q2D (Spectral Mass Gap Upper Bound): If the transfer spectrum below vacuum obeys `mu_k <= muMax`,
    then `C(t) <= C(0) * muMax^t` and single-step decay `C(t+1) <= muMax * C(t)` holds for all `t`. -/
theorem mass_gap_spectral_upper_bound (K : Nat) (c mu : Fin K → Int) (t : Nat) (muMax : Int)
    (hc : ∀ k, 0 ≤ c k) (hmu : ∀ k, 0 ≤ mu k) (hcap : ∀ k, mu k ≤ muMax) (hmax0 : 0 ≤ muMax) :
    spectralCorr K c mu t ≤ spectralCorr K c mu 0 * intPow muMax t ∧
    spectralCorr K c mu (t + 1) ≤ muMax * spectralCorr K c mu t := by
  constructor
  · unfold spectralCorr
    simp only [intPow_zero, Int.mul_one]
    rw [← sumFin_mul_right K _ (intPow muMax t)]
    have hDiff :
        sumFin K (fun k => c k * intPow muMax t) - sumFin K (fun k => c k * intPow (mu k) t) =
        sumFin K (fun k => c k * (intPow muMax t - intPow (mu k) t)) := by
      rw [← sumFin_sub]
      congr 1; funext k; rw [Int.mul_sub]
    have hNonneg : 0 ≤ sumFin K (fun k => c k * (intPow muMax t - intPow (mu k) t)) := by
      apply sumFin_nonneg K _ fun k => ?_
      have hm := intPow_mono (hmu k) (hcap k) t
      exact Int.mul_nonneg (hc k) (by omega)
    omega
  · unfold spectralCorr
    rw [← sumFin_mul_left K muMax _]
    have hDiff :
        sumFin K (fun k => muMax * (c k * intPow (mu k) t)) -
          sumFin K (fun k => c k * intPow (mu k) (t + 1)) =
        sumFin K (fun k => c k * intPow (mu k) t * (muMax - mu k)) := by
      rw [← sumFin_sub]
      congr 1; funext k; simp only [intPow_succ]; grind
    have hNonneg : 0 ≤ sumFin K (fun k => c k * intPow (mu k) t * (muMax - mu k)) := by
      apply sumFin_nonneg K _ fun k => ?_
      have h1 := Int.mul_nonneg (hc k) (intPow_nonneg (hmu k) t)
      have h2 : 0 ≤ muMax - mu k := by have := hcap k; omega
      exact Int.mul_nonneg h1 h2
    let _ := hmax0
    omega

/-! ## Section 3: Strong-Coupling Area Law & Exact Creutz Ratio Cancellation (Theorem Q3) -/

/-- Theorem Q3A (Strong-Coupling Area-Law Dominance): For `0 <= p <= q` (`q > 0`) and tiling area `R*T <= area`,
    the leading cluster bound `(p/q)^area <= (p/q)^(R*T)` holds in exact integer cross-product form. -/
theorem strong_coupling_area_law_bound (p q : Int) (hp : 0 ≤ p) (hpq : p ≤ q) (hq : 0 < q)
    (rt area : Nat) (hArea : rt ≤ area) :
    intPow p area * intPow q rt ≤ intPow p rt * intPow q area := by
  have ⟨d, hd⟩ := Nat.exists_eq_add_of_le hArea
  rw [hd, intPow_add, intPow_add]
  have h1 : intPow p rt * intPow p d * intPow q rt = (intPow p rt * intPow q rt) * intPow p d := by ac_rfl
  have h2 : intPow p rt * (intPow q rt * intPow q d) = (intPow p rt * intPow q rt) * intPow q d := by ac_rfl
  rw [h1, h2]
  have hPre : 0 ≤ intPow p rt * intPow q rt :=
    Int.mul_nonneg (intPow_nonneg hp rt) (Int.le_of_lt (intPow_pos hq rt))
  have hPow : intPow p d ≤ intPow q d := intPow_mono hp hpq d
  exact Int.mul_le_mul_of_nonneg_left hPow hPre

def wilsonLogLaw (c0 perim sigma : Int) (R T : Int) : Int :=
  c0 + perim * (2 * (R + T)) + sigma * (R * T)

/-- Theorem Q3B (Exact Creutz Ratio Perimeter Cancellation): The four-loop Creutz finite difference
    `-ln(W(R,T) W(R-1,T-1) / (W(R,T-1) W(R-1,T)))` annihilates both `c0` and UV perimeter divergence,
    returning the bare area-law string tension `sigma` identically. -/
theorem creutz_ratio_cancels_perimeter (c0 perim sigma R T : Int) :
    wilsonLogLaw c0 perim sigma R T + wilsonLogLaw c0 perim sigma (R - 1) (T - 1) -
      wilsonLogLaw c0 perim sigma R (T - 1) - wilsonLogLaw c0 perim sigma (R - 1) T = sigma := by
  unfold wilsonLogLaw
  grind

def posRatPow (r : PosRat) : Nat → PosRat
  | 0     => PosRat.one
  | n + 1 => PosRat.mul r (posRatPow r n)

private theorem posRatPow_num (r : PosRat) (n : Nat) :
    (posRatPow r n).num = intPow r.num n := by
  induction n with
  | zero => rfl
  | succ k ih => unfold posRatPow intPow PosRat.mul; dsimp only; rw [ih]

private theorem posRatPow_den (r : PosRat) (n : Nat) :
    (posRatPow r n).den = intPow r.den n := by
  induction n with
  | zero => rfl
  | succ k ih => unfold posRatPow intPow PosRat.mul; dsimp only; rw [ih]

/-- Theorem Q3C (Multiplicative Wilson Creutz Extraction in `Q_{>0}`): For exact area-law Wilson loops
    `W(R,T) = c0 * p^(2*(R+T)) * gamma^(R*T)`, the Creutz ratio extracts `gamma` (`= exp(-sigma)`)
    with zero residual factor. -/
theorem creutz_ratio_posrat_exact (c0 p gamma : PosRat) (R T : Nat) :
    let W : Nat → Nat → PosRat := fun r t =>
      PosRat.mul c0 (PosRat.mul (posRatPow p (2 * (r + t))) (posRatPow gamma (r * t)))
    PosRat.equiv
      (PosRat.div (PosRat.mul (W (R + 1) (T + 1)) (W R T))
                  (PosRat.mul (W (R + 1) T) (W R (T + 1))))
      gamma := by
  intro W
  unfold PosRat.equiv PosRat.div
  dsimp only [W]
  simp only [PosRat.mul, posRatPow_num, posRatPow_den]
  have hp1 : 2 * ((R + 1) + (T + 1)) + 2 * (R + T) = 2 * ((R + 1) + T) + 2 * (R + (T + 1)) := by omega
  have hg1 : (R + 1) * (T + 1) + R * T = 1 + ((R + 1) * T + R * (T + 1)) := by
    have ha : (R + 1) * (T + 1) = R * T + R + T + 1 := by grind
    have hb : (R + 1) * T = R * T + T := by grind
    have hc : R * (T + 1) = R * T + R := by grind
    omega
  have hPn : intPow p.num (2 * ((R + 1) + (T + 1))) * intPow p.num (2 * (R + T)) =
             intPow p.num (2 * ((R + 1) + T)) * intPow p.num (2 * (R + (T + 1))) := by
    rw [← intPow_add, ← intPow_add, hp1]
  have hPd : intPow p.den (2 * ((R + 1) + (T + 1))) * intPow p.den (2 * (R + T)) =
             intPow p.den (2 * ((R + 1) + T)) * intPow p.den (2 * (R + (T + 1))) := by
    rw [← intPow_add, ← intPow_add, hp1]
  have hGn : intPow gamma.num ((R + 1) * (T + 1)) * intPow gamma.num (R * T) =
             gamma.num * (intPow gamma.num ((R + 1) * T) * intPow gamma.num (R * (T + 1))) := by
    rw [← intPow_add, ← intPow_add, hg1, intPow_add]; simp
  have hGd : intPow gamma.den ((R + 1) * (T + 1)) * intPow gamma.den (R * T) =
             gamma.den * (intPow gamma.den ((R + 1) * T) * intPow gamma.den (R * (T + 1))) := by
    rw [← intPow_add, ← intPow_add, hg1, intPow_add]; simp
  calc (c0.num * (intPow p.num (2 * ((R + 1) + (T + 1))) * intPow gamma.num ((R + 1) * (T + 1))) *
          (c0.num * (intPow p.num (2 * (R + T)) * intPow gamma.num (R * T))) *
          (c0.den * (intPow p.den (2 * ((R + 1) + T)) * intPow gamma.den ((R + 1) * T)) *
            (c0.den * (intPow p.den (2 * (R + (T + 1))) * intPow gamma.den (R * (T + 1)))))) * gamma.den
      = c0.num * c0.num * c0.den * c0.den *
          (intPow p.num (2 * ((R + 1) + (T + 1))) * intPow p.num (2 * (R + T))) *
          (intPow gamma.num ((R + 1) * (T + 1)) * intPow gamma.num (R * T)) *
          (intPow p.den (2 * ((R + 1) + T)) * intPow p.den (2 * (R + (T + 1)))) *
          (intPow gamma.den ((R + 1) * T) * intPow gamma.den (R * (T + 1))) * gamma.den := by ac_rfl
    _ = c0.num * c0.num * c0.den * c0.den *
          (intPow p.num (2 * ((R + 1) + T)) * intPow p.num (2 * (R + (T + 1)))) *
          (gamma.num * (intPow gamma.num ((R + 1) * T) * intPow gamma.num (R * (T + 1)))) *
          (intPow p.den (2 * ((R + 1) + (T + 1))) * intPow p.den (2 * (R + T))) *
          (intPow gamma.den ((R + 1) * (T + 1)) * intPow gamma.den (R * T)) := by
          rw [hPn, hGn, ← hPd, hGd]; ac_rfl
    _ = gamma.num *
          (c0.den * (intPow p.den (2 * ((R + 1) + (T + 1))) * intPow gamma.den ((R + 1) * (T + 1))) *
            (c0.den * (intPow p.den (2 * (R + T)) * intPow gamma.den (R * T))) *
            (c0.num * (intPow p.num (2 * ((R + 1) + T)) * intPow gamma.num ((R + 1) * T)) *
              (c0.num * (intPow p.num (2 * (R + (T + 1))) * intPow gamma.num (R * (T + 1)))))) := by ac_rfl

/-! ## Section 4: Variational GEVP / Multi-Smearing Rayleigh-Ritz Optimality (Theorem Q4) -/

def corrMat (K S : Nat) (v : Fin K → Fin S → Int) (mu : Fin K → Int) (t : Nat) (a b : Fin S) : Int :=
  sumFin K (fun k => v k a * v k b * intPow (mu k) t)

def projCorr (K S : Nat) (v : Fin K → Fin S → Int) (mu : Fin K → Int) (t : Nat)
    (u : Fin S → Int) : Int :=
  sumFin S (fun a => sumFin S (fun b => u a * u b * corrMat K S v mu t a b))

/-- Theorem Q4A (Variational Projection Spectral Identity): Any trial superposition `u : Fin S -> Int`
    projects the `S x S` smearing matrix `C_{ab}(t)` cleanly onto positive spectral weights
    `|u^T v_k|^2 mu_k^t`. -/
theorem projCorr_spectral_eq (K S : Nat) (v : Fin K → Fin S → Int) (mu : Fin K → Int)
    (t : Nat) (u : Fin S → Int) :
    projCorr K S v mu t u =
      spectralCorr K
        (fun k => sumFin S (fun a => u a * v k a) * sumFin S (fun b => u b * v k b)) mu t := by
  unfold projCorr corrMat spectralCorr
  have hIn :
      (fun a => sumFin S (fun b => u a * u b * sumFin K (fun k => v k a * v k b * intPow (mu k) t))) =
      (fun a => sumFin S (fun b => sumFin K (fun k => intPow (mu k) t * ((u a * v k a) * (u b * v k b))))) := by
    funext a; congr 1; funext b
    have hM :
        u a * u b * sumFin K (fun k => v k a * v k b * intPow (mu k) t) =
        sumFin K (fun k => v k a * v k b * intPow (mu k) t) * (u a * u b) := by ac_rfl
    rw [hM, ← sumFin_mul_right K]
    congr 1; funext k; ac_rfl
  rw [hIn]
  have hSwapB :
      (fun a => sumFin S (fun b => sumFin K (fun k => intPow (mu k) t * ((u a * v k a) * (u b * v k b))))) =
      (fun a => sumFin K (fun k => sumFin S (fun b => intPow (mu k) t * ((u a * v k a) * (u b * v k b))))) := by
    funext a; exact sumFin_swap_gen S K _
  rw [hSwapB, sumFin_swap_gen S K]
  congr 1; funext k
  have hPull :
      (fun a => sumFin S (fun b => intPow (mu k) t * ((u a * v k a) * (u b * v k b)))) =
      (fun a => (intPow (mu k) t * (u a * v k a)) * sumFin S (fun b => u b * v k b)) := by
    funext a
    rw [← sumFin_mul_left S (intPow (mu k) t * (u a * v k a)) (fun b => u b * v k b)]
    congr 1; funext b; ac_rfl
  rw [hPull, sumFin_mul_right S, sumFin_mul_left S]
  ac_rfl

/-- Theorem Q4B (Rayleigh-Ritz Variational Mass-Gap Bound): For EVERY multi-smearing trial state `u`,
    `C_u(t) >= 0`, effective mass converges monotonically from above (`C_u(t+1)^2 <= C_u(t)*C_u(t+2)`),
    and `C_u(t+1) <= muMax * C_u(t)` strictly bounds the ground-state mass gap from above. -/
theorem rayleigh_ritz_ground_bound (K S : Nat) (v : Fin K → Fin S → Int) (mu : Fin K → Int)
    (t : Nat) (u : Fin S → Int) (muMax : Int)
    (hmu : ∀ k, 0 ≤ mu k) (hcap : ∀ k, mu k ≤ muMax) (hmax0 : 0 ≤ muMax) :
    0 ≤ projCorr K S v mu t u ∧
    projCorr K S v mu (t + 1) u * projCorr K S v mu (t + 1) u ≤
      projCorr K S v mu t u * projCorr K S v mu (t + 2) u ∧
    projCorr K S v mu (t + 1) u ≤ muMax * projCorr K S v mu t u := by
  rw [projCorr_spectral_eq, projCorr_spectral_eq, projCorr_spectral_eq]
  have hc : ∀ k, 0 ≤ sumFin S (fun a => u a * v k a) * sumFin S (fun b => u b * v k b) :=
    fun k => int_sq_nonneg _
  refine ⟨correlator_nonneg K _ mu t hc hmu,
          correlator_log_convex K _ mu t hc hmu,
          (mass_gap_spectral_upper_bound K _ mu t muMax hc hmu hcap hmax0).2⟩

/-! ## Section 5: Connected Vacuum Variance Positivity & Exponential Clustering (Theorem Q5) -/

/-- Theorem Q5A (Connected Vacuum Variance Nonnegativity): Clearing zero-momentum vacuum VEVs yields
    exact nonnegative variance `sum_i w_i * (W * f_i - sum_j w_j f_j)^2 >= 0` for all `w_i >= 0`. -/
theorem connected_var_nonneg (M : Nat) (w f : Fin M → Int) (hw : ∀ i, 0 ≤ w i) :
    let totalW := sumFin M w
    let meanF  := sumFin M (fun j => w j * f j)
    0 ≤ sumFin M (fun i => w i * ((totalW * f i - meanF) * (totalW * f i - meanF))) := by
  intro totalW meanF
  exact sumFin_nonneg M _ fun i => Int.mul_nonneg (hw i) (int_sq_nonneg _)

/-- Theorem Q5B (Exponential Clustering Under a Strictly Positive Mass Gap): When the connected spectrum
    satisfies `mu_k <= N / D < 1` (`0 < D, 0 <= N < D`), correlations decay strictly exponentially as
    `C_conn(t) * D^t <= C_conn(0) * N^t`. -/
theorem os_exponential_clustering (K : Nat) (c mu : Fin K → Int) (t : Nat) (N D : Int)
    (hc : ∀ k, 0 ≤ c k) (hmu : ∀ k, 0 ≤ mu k) (hN : 0 ≤ N) (hD : 0 < D)
    (hGap : ∀ k, mu k * D ≤ N) :
    spectralCorr K c mu t * intPow D t ≤ spectralCorr K c mu 0 * intPow N t := by
  have hScaled :
      spectralCorr K c (fun k => mu k * D) t = spectralCorr K c mu t * intPow D t := by
    unfold spectralCorr
    rw [← sumFin_mul_right K _ (intPow D t)]
    congr 1; funext k
    have hPowMul : intPow (mu k * D) t = intPow (mu k) t * intPow D t := by
      induction t with
      | zero => simp [intPow]
      | succ m ihm => unfold intPow; rw [ihm]; ac_rfl
    rw [hPowMul]; ac_rfl
  have hInit :
      spectralCorr K c (fun k => mu k * D) 0 = spectralCorr K c mu 0 := by
    unfold spectralCorr intPow
    rfl
  have hMuD : ∀ k, 0 ≤ mu k * D := fun k => Int.mul_nonneg (hmu k) (Int.le_of_lt hD)
  have hBound := (mass_gap_spectral_upper_bound K c (fun k => mu k * D) t N hc hMuD hGap hN).1
  rw [hScaled, hInit] at hBound
  exact hBound

end Flow.ConstructiveQFT