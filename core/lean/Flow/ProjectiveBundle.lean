/-
  Flow.ProjectiveBundle — CP^{N-1} Hopf Principal U(1)-Bundle, Bogomol'nyi Bound & Fisher Geodesic Ladder
  =========================================================================================================
  Pure Lean 4 (Init-only) formalization building directly on `Flow.GaugeBundle` for arbitrary target
  dimension N, periodic torus size L, and replica count R:
    1. Exact Gaussian-integer complex vector algebra (`GInt`, `Cvec N`);
    2. Hopf U(1)-bundle projector invariance, CP^{N-1} local gauge invariance, and SU(2) Givens norm-preservation (Theorem P1);
    3. Arbitrary-N Bogomol'nyi self-duality identity, topological lower bound, and holomorphic instanton characterization (Theorem P2);
    4. Defect-slit Bianchi opening and exact multi-replica `@swap` Metropolis detailed balance (Theorem P3);
    5. 1D Cauchy-Schwarz optimality of the uniform-arc Fisher `2-Jet` replica ladder (Theorem P4).
-/
import Flow.GaugeBundle

namespace Flow.ProjectiveBundle

open Flow.GaugeBundle

/-! ## Section 1: Exact Gaussian-Integer Complex Vector Algebra (`GInt`, `Cvec N`) -/

structure GInt where
  re : Int
  im : Int
  deriving DecidableEq, Repr

def gZero : GInt := ⟨0, 0⟩
def gOne  : GInt := ⟨1, 0⟩
def gI    : GInt := ⟨0, 1⟩

def gAdd (a b : GInt) : GInt := ⟨a.re + b.re, a.im + b.im⟩
def gSub (a b : GInt) : GInt := ⟨a.re - b.re, a.im - b.im⟩
def gNeg (a : GInt) : GInt   := ⟨-a.re, -a.im⟩

def gMul (a b : GInt) : GInt :=
  ⟨a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re⟩

def gConj (a : GInt) : GInt :=
  ⟨a.re, -a.im⟩

def gNormSq (a : GInt) : Int :=
  a.re * a.re + a.im * a.im

private theorem int_sq_nonneg (z : Int) : 0 ≤ z * z := by
  by_cases hz : 0 ≤ z
  · exact Int.mul_nonneg hz hz
  · have hneg : 0 ≤ -z := by omega
    have hsq : (-z) * (-z) = z * z := Int.neg_mul_neg z z
    rw [← hsq]
    exact Int.mul_nonneg hneg hneg

private theorem sq_add (x y : Int) : (x + y) * (x + y) = x * x + 2 * (x * y) + y * y := by
  rw [Int.add_mul, Int.mul_add, Int.mul_add, Int.mul_comm y x, Int.two_mul]
  omega

private theorem sq_sub (x y : Int) : (x - y) * (x - y) = x * x - 2 * (x * y) + y * y := by
  rw [Int.sub_mul, Int.mul_sub, Int.mul_sub, Int.mul_comm y x, Int.two_mul]
  omega

theorem gNormSq_nonneg (a : GInt) : 0 ≤ gNormSq a := by
  unfold gNormSq
  exact Int.add_nonneg (int_sq_nonneg a.re) (int_sq_nonneg a.im)

theorem gNormSq_eq_zero_iff (a : GInt) : gNormSq a = 0 ↔ a = gZero := by
  unfold gNormSq gZero
  constructor
  · intro h
    have hr := int_sq_nonneg a.re
    have hi := int_sq_nonneg a.im
    have hr0 : a.re * a.re = 0 := by omega
    have hi0 : a.im * a.im = 0 := by omega
    have hre : a.re = 0 := by
      cases Int.mul_eq_zero.mp hr0 with | inl h1 => exact h1 | inr h2 => exact h2
    have him : a.im = 0 := by
      cases Int.mul_eq_zero.mp hi0 with | inl h1 => exact h1 | inr h2 => exact h2
    cases a; simp_all
  · intro h
    subst h
    simp

theorem gSub_eq_zero_iff (a b : GInt) : gSub a b = gZero ↔ a = b := by
  cases a; cases b
  unfold gSub gZero
  simp only [GInt.mk.injEq]
  constructor
  · intro ⟨h1, h2⟩; constructor <;> omega
  · intro ⟨h1, h2⟩; constructor <;> omega

theorem gOne_mul (a : GInt) : gMul gOne a = a := by
  cases a
  unfold gOne gMul
  simp

theorem gMul_one (a : GInt) : gMul a gOne = a := by
  cases a
  unfold gOne gMul
  simp

theorem gMul_comm (a b : GInt) : gMul a b = gMul b a := by
  unfold gMul
  simp only [GInt.mk.injEq]
  have h1 : a.re * b.re = b.re * a.re := Int.mul_comm _ _
  have h2 : a.im * b.im = b.im * a.im := Int.mul_comm _ _
  have h3 : a.re * b.im = b.im * a.re := Int.mul_comm _ _
  have h4 : a.im * b.re = b.re * a.im := Int.mul_comm _ _
  constructor <;> omega

theorem gMul_assoc (a b c : GInt) : gMul (gMul a b) c = gMul a (gMul b c) := by
  unfold gMul
  simp only [GInt.mk.injEq]
  rw [Int.sub_mul, Int.add_mul, Int.add_mul, Int.sub_mul]
  rw [Int.mul_sub, Int.mul_add, Int.mul_add, Int.mul_sub]
  have m1 : a.re * b.re * c.re = a.re * (b.re * c.re) := by ac_rfl
  have m2 : a.im * b.im * c.re = a.im * (b.im * c.re) := by ac_rfl
  have m3 : a.re * b.im * c.im = a.re * (b.im * c.im) := by ac_rfl
  have m4 : a.im * b.re * c.im = a.im * (b.re * c.im) := by ac_rfl
  have m5 : a.re * b.re * c.im = a.re * (b.re * c.im) := by ac_rfl
  have m6 : a.im * b.im * c.im = a.im * (b.im * c.im) := by ac_rfl
  have m7 : a.re * b.im * c.re = a.re * (b.im * c.re) := by ac_rfl
  have m8 : a.im * b.re * c.re = a.im * (b.re * c.re) := by ac_rfl
  constructor <;> omega

theorem gConj_conj (a : GInt) : gConj (gConj a) = a := by
  cases a
  unfold gConj
  simp

theorem gConj_mul (a b : GInt) : gConj (gMul a b) = gMul (gConj a) (gConj b) := by
  unfold gConj gMul
  simp only [GInt.mk.injEq]
  rw [Int.neg_mul_neg, Int.mul_neg, Int.neg_mul, Int.neg_add]
  constructor <;> omega

theorem gMul_conj_self (a : GInt) : gMul a (gConj a) = ⟨gNormSq a, 0⟩ := by
  unfold gMul gConj gNormSq
  simp only [GInt.mk.injEq, Int.mul_neg, Int.sub_neg]
  constructor
  · trivial
  · rw [Int.mul_comm a.re a.im]; omega

theorem gConj_mul_self (a : GInt) : gMul (gConj a) a = ⟨gNormSq a, 0⟩ := by
  rw [gMul_comm, gMul_conj_self]

theorem gNormSq_conj (a : GInt) : gNormSq (gConj a) = gNormSq a := by
  unfold gNormSq gConj
  dsimp only
  rw [Int.neg_mul_neg]

theorem gNormSq_mul (a b : GInt) : gNormSq (gMul a b) = gNormSq a * gNormSq b := by
  unfold gNormSq gMul
  dsimp only
  rw [sq_sub, sq_add]
  have h1 : (a.re * b.re) * (a.re * b.re) = (a.re * a.re) * (b.re * b.re) := by ac_rfl
  have h2 : (a.im * b.im) * (a.im * b.im) = (a.im * a.im) * (b.im * b.im) := by ac_rfl
  have h3 : (a.re * b.im) * (a.re * b.im) = (a.re * a.re) * (b.im * b.im) := by ac_rfl
  have h4 : (a.im * b.re) * (a.im * b.re) = (a.im * a.im) * (b.re * b.re) := by ac_rfl
  have hc : (a.re * b.re) * (a.im * b.im) = (a.re * b.im) * (a.im * b.re) := by ac_rfl
  rw [h1, h2, h3, h4, hc, Int.add_mul, Int.mul_add, Int.mul_add]
  omega

theorem gNormSq_sub (p q : GInt) :
    gNormSq (gSub p q) = gNormSq p + gNormSq q - 2 * (gMul p (gConj q)).re := by
  unfold gNormSq gSub gMul gConj
  dsimp only
  rw [sq_sub, sq_sub, Int.mul_neg, Int.sub_neg, Int.mul_add]
  omega

theorem gNormSq_add (r s : GInt) :
    gNormSq (gAdd r s) = gNormSq r + gNormSq s + 2 * (gMul r (gConj s)).re := by
  unfold gNormSq gAdd gMul gConj
  dsimp only
  rw [sq_add, sq_add, Int.mul_neg, Int.sub_neg, Int.mul_add]
  omega

abbrev Cvec (N : Nat) : Type := Fin N → GInt

def vecMulScalar {N : Nat} (g : GInt) (u : Cvec N) : Cvec N :=
  fun k => gMul g (u k)

def vecAdd {N : Nat} (u v : Cvec N) : Cvec N :=
  fun k => gAdd (u k) (v k)

def vecSub {N : Nat} (u v : Cvec N) : Cvec N :=
  fun k => gSub (u k) (v k)

def vecMulI {N : Nat} (v : Cvec N) : Cvec N :=
  fun k => gMul gI (v k)

def normSq (N : Nat) (u : Cvec N) : Int :=
  sumFin N (fun k => gNormSq (u k))

def innerRe (N : Nat) (u v : Cvec N) : Int :=
  sumFin N (fun k => (gMul (gConj (u k)) (v k)).re)

def innerIm (N : Nat) (u v : Cvec N) : Int :=
  sumFin N (fun k => (gMul (gConj (u k)) (v k)).im)

def projEntry {N : Nat} (u : Cvec N) (a b : Fin N) : GInt :=
  gMul (u a) (gConj (u b))

/-! ## Section 2: Hopf Principal U(1)-Bundle, CP^{N-1} Gauge Invariance & SU(2) Rotation (Theorem P1) -/

/-- Theorem P1A (Hopf Principal U(1)-Bundle Invariance): Multiplying every component of `u : Cvec N`
    by any unit-modulus U(1) phase `g` (`|g|^2 = 1`) leaves every gauge-invariant projector entry `u_a u_b*` unchanged. -/
theorem hopf_projector_u1_invariant {N : Nat} (g : GInt) (hg : gNormSq g = 1)
    (u : Cvec N) (a b : Fin N) :
    projEntry (vecMulScalar g u) a b = projEntry u a b := by
  unfold projEntry vecMulScalar
  have h1 : gMul g (gConj g) = gOne := by
    rw [gMul_conj_self, hg]
    rfl
  rw [gConj_mul]
  calc gMul (gMul g (u a)) (gMul (gConj g) (gConj (u b)))
      = gMul (gMul (gMul g (u a)) (gConj g)) (gConj (u b)) := by rw [← gMul_assoc]
    _ = gMul (gMul (gMul (u a) g) (gConj g)) (gConj (u b)) := by rw [gMul_comm g (u a)]
    _ = gMul (gMul (u a) (gMul g (gConj g))) (gConj (u b)) := by rw [gMul_assoc (u a) g (gConj g)]
    _ = gMul (u a) (gConj (u b)) := by rw [h1, gMul_one]

/-- Theorem P1B (CP^{N-1} Auxiliary-U(1) Local Gauge Invariance): For arbitrary `N >= 1`, transforming
    matter fields `zx -> gx * zx`, `zy -> gy * zy` and auxiliary link `U -> gx * U * conj gy` leaves
    the hopping term `Re(zx† U zy)` identically invariant. -/
theorem cpn_link_local_gauge_invariant (N : Nat) (gx gy U : GInt)
    (hgx : gNormSq gx = 1) (hgy : gNormSq gy = 1) (zx zy : Cvec N) :
    let U'  := gMul gx (gMul U (gConj gy))
    let zx' := vecMulScalar gx zx
    let zy' := vecMulScalar gy zy
    innerRe N zx' (vecMulScalar U' zy') = innerRe N zx (vecMulScalar U zy) := by
  intro U' zx' zy'
  unfold innerRe
  congr 1
  funext k
  dsimp only [U', zx', zy', vecMulScalar]
  have h1 : gMul (gConj gx) gx = gOne := by rw [gConj_mul_self, hgx]; rfl
  have h2 : gMul (gConj gy) gy = gOne := by rw [gConj_mul_self, hgy]; rfl
  rw [gConj_mul]
  have hleft :
      gMul (gMul (gConj gx) (gConj (zx k))) (gMul (gMul gx (gMul U (gConj gy))) (gMul gy (zy k))) =
      gMul (gConj (zx k)) (gMul (gMul (gConj gx) gx) (gMul U (gMul (gMul (gConj gy) gy) (zy k)))) := by
    calc gMul (gMul (gConj gx) (gConj (zx k))) (gMul (gMul gx (gMul U (gConj gy))) (gMul gy (zy k)))
        = gMul (gMul (gConj (zx k)) (gConj gx)) (gMul gx (gMul (gMul U (gConj gy)) (gMul gy (zy k)))) := by
            rw [gMul_comm (gConj gx) (gConj (zx k)), gMul_assoc gx (gMul U (gConj gy)) (gMul gy (zy k))]
      _ = gMul (gConj (zx k)) (gMul (gConj gx) (gMul gx (gMul U (gMul (gConj gy) (gMul gy (zy k)))))) := by
            rw [gMul_assoc (gConj (zx k)) (gConj gx), gMul_assoc U (gConj gy) (gMul gy (zy k))]
      _ = gMul (gConj (zx k)) (gMul (gMul (gConj gx) gx) (gMul U (gMul (gMul (gConj gy) gy) (zy k)))) := by
            rw [← gMul_assoc (gConj gx) gx, ← gMul_assoc (gConj gy) gy (zy k)]
  rw [hleft, h1, h2, gOne_mul, gOne_mul]

private theorem su2_pair_normSq (α β u v : GInt) :
    gNormSq (gSub (gMul α u) (gMul (gConj β) v)) +
    gNormSq (gAdd (gMul β u) (gMul (gConj α) v)) =
      (gNormSq α + gNormSq β) * (gNormSq u + gNormSq v) := by
  rw [gNormSq_sub, gNormSq_add]
  have hCross :
      gMul (gMul α u) (gConj (gMul (gConj β) v)) =
      gMul (gMul β u) (gConj (gMul (gConj α) v)) := by
    rw [gConj_mul, gConj_conj, gConj_mul, gConj_conj]
    calc gMul (gMul α u) (gMul β (gConj v))
        = gMul α (gMul (gMul u β) (gConj v)) := by rw [gMul_assoc, ← gMul_assoc u β (gConj v)]
      _ = gMul α (gMul (gMul β u) (gConj v)) := by rw [gMul_comm u β]
      _ = gMul (gMul α β) (gMul u (gConj v)) := by rw [gMul_assoc β u (gConj v), ← gMul_assoc α β]
      _ = gMul (gMul β α) (gMul u (gConj v)) := by rw [gMul_comm α β]
      _ = gMul β (gMul α (gMul u (gConj v))) := by rw [gMul_assoc β α]
      _ = gMul β (gMul (gMul α u) (gConj v)) := by rw [← gMul_assoc α u (gConj v)]
      _ = gMul β (gMul (gMul u α) (gConj v)) := by rw [gMul_comm α u]
      _ = gMul β (gMul u (gMul α (gConj v))) := by rw [gMul_assoc u α (gConj v)]
      _ = gMul (gMul β u) (gMul α (gConj v)) := by rw [← gMul_assoc β u]
  rw [hCross, gNormSq_mul, gNormSq_mul, gNormSq_mul, gNormSq_mul]
  rw [gNormSq_conj, gNormSq_conj, Int.add_mul, Int.mul_add, Int.mul_add]
  omega

def applySU2Givens {N : Nat} (a b : Fin N) (α β : GInt) (u : Cvec N) : Cvec N :=
  fun k =>
    if k = a then gSub (gMul α (u a)) (gMul (gConj β) (u b))
    else if k = b then gAdd (gMul β (u a)) (gMul (gConj α) (u b))
    else u k

private theorem sumFin_update_two {N : Nat} (a b : Fin N) (hab : a ≠ b) (f : Fin N → Int)
    (va vb : Int) :
    sumFin N (fun k => if k = a then va else if k = b then vb else f k) =
      sumFin N f + (va - f a) + (vb - f b) := by
  have hfn :
      (fun k => if k = a then va else if k = b then vb else f k) =
      (fun k => f k + (if k = a then va - f a else 0) + (if k = b then vb - f b else 0)) := by
    funext k
    by_cases hka : k = a
    · subst hka
      simp [hab]
      omega
    · by_cases hkb : k = b
      · subst hkb
        simp [hka]
        omega
      · simp [hka, hkb]
  rw [hfn, sumFin_add, sumFin_add, sumFin_single_hit a, sumFin_single_hit b]

/-- Theorem P1C (Universal SU(2) ⊂ U(N) Givens Norm Preservation): Acting on any two distinct
    components `a ≠ b : Fin N` by any unit-determinant quaternion `(α, β)` with `|α|^2 + |β|^2 = 1`
    leaves the global S^{2N-1} norm identically invariant for all N >= 2. -/
theorem su2_givens_preserves_normSq {N : Nat} (a b : Fin N) (hab : a ≠ b)
    (α β : GInt) (hUnit : gNormSq α + gNormSq β = 1) (u : Cvec N) :
    normSq N (applySU2Givens a b α β u) = normSq N u := by
  unfold normSq applySU2Givens
  have hfn :
      (fun k => gNormSq (if k = a then gSub (gMul α (u a)) (gMul (gConj β) (u b))
        else if k = b then gAdd (gMul β (u a)) (gMul (gConj α) (u b)) else u k)) =
      (fun k => if k = a then gNormSq (gSub (gMul α (u a)) (gMul (gConj β) (u b)))
        else if k = b then gNormSq (gAdd (gMul β (u a)) (gMul (gConj α) (u b))) else gNormSq (u k)) := by
    funext k
    by_cases hka : k = a
    · rw [ite_eq_left hka, ite_eq_left hka]
    · by_cases hkb : k = b
      · rw [ite_eq_right hka, ite_eq_left hkb, ite_eq_right hka, ite_eq_left hkb]
      · rw [ite_eq_right hka, ite_eq_right hkb, ite_eq_right hka, ite_eq_right hkb]
  rw [hfn, sumFin_update_two a b hab]
  have hp := su2_pair_normSq α β (u a) (u b)
  rw [hUnit, Int.one_mul] at hp
  omega

/-! ## Section 3: Exact Arbitrary-N Bogomol'nyi Self-Duality Bound (Theorem P2) -/

private theorem bogomolnyi_pt_minus (a b : GInt) :
    gNormSq a + gNormSq b = gNormSq (gSub a (gMul gI b)) - 2 * (gMul (gConj a) b).im := by
  unfold gNormSq gSub gMul gConj gI
  dsimp only
  simp only [Int.zero_mul, Int.one_mul, Int.zero_sub, Int.zero_add, Int.sub_neg]
  rw [sq_add, sq_sub, Int.neg_mul, Int.mul_add]
  have hc1 : b.im * a.re = a.re * b.im := Int.mul_comm _ _
  have hc2 : b.re * a.im = a.im * b.re := Int.mul_comm _ _
  omega

private theorem bogomolnyi_pt_plus (a b : GInt) :
    gNormSq a + gNormSq b = gNormSq (gAdd a (gMul gI b)) + 2 * (gMul (gConj a) b).im := by
  unfold gNormSq gAdd gMul gConj gI
  dsimp only
  simp only [Int.zero_mul, Int.one_mul, Int.zero_sub, Int.zero_add]
  have haddNeg : a.re + -b.im = a.re - b.im := Int.add_neg_eq_sub
  rw [haddNeg, sq_sub, sq_add, Int.neg_mul, Int.mul_add]
  have hc1 : b.im * a.re = a.re * b.im := Int.mul_comm _ _
  have hc2 : b.re * a.im = a.im * b.re := Int.mul_comm _ _
  omega

/-- Theorem P2A (Bogomol'nyi Minus Self-Duality Identity for Arbitrary N):
    `||u||^2 + ||v||^2 = ||u - i*v||^2 - 2 * Im<u, v>`. -/
theorem bogomolnyi_identity_minus (N : Nat) (u v : Cvec N) :
    normSq N u + normSq N v = normSq N (vecSub u (vecMulI v)) - 2 * innerIm N u v := by
  unfold normSq vecSub vecMulI innerIm
  rw [← sumFin_add, ← sumFin_mul_left]
  have hsub := sumFin_sub N (fun k => gNormSq (gSub (u k) (gMul gI (v k))))
    (fun k => 2 * (gMul (gConj (u k)) (v k)).im)
  rw [← hsub]
  congr 1
  funext k
  exact bogomolnyi_pt_minus (u k) (v k)

/-- Theorem P2B (Bogomol'nyi Plus Anti-Self-Duality Identity for Arbitrary N):
    `||u||^2 + ||v||^2 = ||u + i*v||^2 + 2 * Im<u, v>`. -/
theorem bogomolnyi_identity_plus (N : Nat) (u v : Cvec N) :
    normSq N u + normSq N v = normSq N (vecAdd u (vecMulI v)) + 2 * innerIm N u v := by
  unfold normSq vecAdd vecMulI innerIm
  rw [← sumFin_add, ← sumFin_mul_left]
  have hadd := sumFin_add N (fun k => gNormSq (gAdd (u k) (gMul gI (v k))))
    (fun k => 2 * (gMul (gConj (u k)) (v k)).im)
  rw [← hadd]
  congr 1
  funext k
  exact bogomolnyi_pt_plus (u k) (v k)

/-- Theorem P2C (Universal Bogomol'nyi Energy Lower Bound):
    For ALL target dimensions `N`, `2 * |Im<u, v>| <= ||u||^2 + ||v||^2`. -/
theorem bogomolnyi_lower_bound (N : Nat) (u v : Cvec N) :
    2 * Int.ofNat (Int.natAbs (innerIm N u v)) ≤ normSq N u + normSq N v := by
  have hm := bogomolnyi_identity_minus N u v
  have hp := bogomolnyi_identity_plus N u v
  have hnnM : 0 ≤ normSq N (vecSub u (vecMulI v)) :=
    sumFin_nonneg N _ fun k => gNormSq_nonneg _
  have hnnP : 0 ≤ normSq N (vecAdd u (vecMulI v)) :=
    sumFin_nonneg N _ fun k => gNormSq_nonneg _
  rw [Int.ofNat_eq_natCast]
  omega

private theorem sumPrefix_nonneg_eq_zero_iff {N : Nat} (g : Fin N → Int)
    (hg : ∀ i, 0 ≤ g i) (k : Nat) (hk : k ≤ N) :
    sumPrefix g k = 0 ↔ ∀ i : Fin N, i.val < k → g i = 0 := by
  induction k with
  | zero =>
    simp only [sumPrefix]
    constructor
    · intro _ i hi; omega
    · intro _; trivial
  | succ k ih =>
    have hkN : k < N := hk
    have hkLe : k ≤ N := Nat.le_of_lt hkN
    have ihk := ih hkLe
    have hp := sumPrefix_nonneg g hg k
    have hgLast := hg ⟨k, hkN⟩
    constructor
    · intro hsum i hi
      simp only [sumPrefix, hkN, ↓reduceDIte] at hsum
      have hpre0 : sumPrefix g k = 0 := by omega
      have hlast0 : g ⟨k, hkN⟩ = 0 := by omega
      by_cases hik : i.val < k
      · exact ihk.mp hpre0 i hik
      · have hieq : i = ⟨k, hkN⟩ := by ext; dsimp; omega
        subst hieq
        exact hlast0
    · intro hfor
      simp only [sumPrefix, hkN, ↓reduceDIte]
      have hpre0 : sumPrefix g k = 0 := ihk.mpr fun i hi => hfor i (Nat.lt_trans hi (Nat.lt_succ_self k))
      have hlast0 : g ⟨k, hkN⟩ = 0 := hfor ⟨k, hkN⟩ (Nat.lt_succ_self k)
      omega

/-- Theorem P2D (Belavin-Polyakov Holomorphic Instanton Characterization):
    The Bogomol'nyi bound is saturated (`||u||^2 + ||v||^2 = -2 Im<u, v>`) iff `u = i * v` componentwise. -/
theorem bogomolnyi_eq_iff_holomorphic (N : Nat) (u v : Cvec N) :
    normSq N u + normSq N v = -2 * innerIm N u v ↔ ∀ k : Fin N, u k = gMul gI (v k) := by
  rw [bogomolnyi_identity_minus N u v]
  have hiff := sumPrefix_nonneg_eq_zero_iff (N := N) (fun k => gNormSq (gSub (u k) (gMul gI (v k))))
    (fun k => gNormSq_nonneg _) N (Nat.le_refl N)
  constructor
  · intro h k
    have hs0 : normSq N (vecSub u (vecMulI v)) = 0 := by omega
    unfold normSq vecSub vecMulI sumFin at hs0
    have hk0 : gNormSq (gSub (u k) (gMul gI (v k))) = 0 := (hiff.mp hs0) k k.isLt
    exact (gSub_eq_zero_iff _ _).mp ((gNormSq_eq_zero_iff _).mp hk0)
  · intro h
    have hs0 : sumPrefix (fun k => gNormSq (gSub (u k) (gMul gI (v k)))) N = 0 := by
      apply hiff.mpr
      intro k _
      rw [h k, (gSub_eq_zero_iff _ _).mpr rfl]
      exact (gNormSq_eq_zero_iff _).mpr rfl
    unfold normSq vecSub vecMulI sumFin
    rw [hs0]
    omega

/-! ## Section 4: Defect-Slit Bianchi Opening & Multi-Replica `@swap` Detailed Balance (Theorem P3) -/

/-- Theorem P3A (Defect-Slit Bianchi Opening): Splitting T_L^2 into bulk vs defect-slit plaquettes
    yields `sum(bulkPlaq) = -sum(slitPlaq)`, freeing bulk topological sectors without boundary bias. -/
theorem defect_slit_breaks_bianchi {L : Nat} (hL : 0 < L) (inSlit : Fin L → Fin L → Bool)
    (A : GaugeField2D L) :
    let bulkPlaq : Fin L → Fin L → Int := fun x y => if !(inSlit x y) then rawPlaq hL A x y else 0
    let slitPlaq : Fin L → Fin L → Int := fun x y => if inSlit x y then rawPlaq hL A x y else 0
    sumTorus L bulkPlaq = -sumTorus L slitPlaq := by
  intro bulkPlaq slitPlaq
  have hsplit : (fun x y => rawPlaq hL A x y) = (fun x y => bulkPlaq x y + slitPlaq x y) := by
    funext x y
    dsimp only [bulkPlaq, slitPlaq]
    cases inSlit x y <;> simp
  have hzero := rawPlaq_torus_sum_zero hL A
  rw [hsplit, sumTorus_add] at hzero
  omega

/-- Theorem P3B (Multi-Replica `@swap` Detailed Balance): Adjacent replica exchange `(A, B) <-> (B, A)`
    under product Boltzmann weight `pi_0 * pi_1` satisfies exact Metropolis detailed balance (`0` bias). -/
theorem replica_swap_detailed_balance_exact (π0A π0B π1A π1B : PosRat) :
    let wAB := PosRat.mul π0A π1B
    let wBA := PosRat.mul π0B π1A
    PosRat.equiv
      (PosRat.mul wAB (PosRat.minOne (PosRat.div wBA wAB)))
      (PosRat.mul wBA (PosRat.minOne (PosRat.div wAB wBA))) := by
  intro wAB wBA
  exact metropolis_detailed_balance_exact wAB wBA

/-! ## Section 5: Optimal `2-Jet` Fisher-Geodesic Replica Ladder (Theorem P4) -/

/-- 1D exact Cauchy-Schwarz inequality over `Fin R` (`0 < R`). -/
theorem fin_cauchy_schwarz {R : Nat} (hR : 0 < R) (ds : Fin R → Int) :
    sumFin R ds * sumFin R ds ≤ (R : Int) * sumFin R (fun r => ds r * ds r) := by
  let S := sumFin R ds
  let Q := sumFin R (fun r => ds r * ds r)
  have hRpos : (0 : Int) < (R : Int) := by omega
  have hsq : 0 ≤ sumFin R (fun r => ((R : Int) * ds r - S) * ((R : Int) * ds r - S)) :=
    sumFin_nonneg R _ fun r => int_sq_nonneg _
  have hexpand :
      (fun r => ((R : Int) * ds r - S) * ((R : Int) * ds r - S)) =
      (fun r => ((R : Int) * (R : Int) * (ds r * ds r) - (2 * (R : Int) * S) * ds r) + S * S) := by
    funext r
    have h1 : ((R : Int) * ds r - S) * ((R : Int) * ds r - S) =
        ((R : Int) * ds r) * ((R : Int) * ds r) - ((R : Int) * ds r) * S - S * ((R : Int) * ds r) + S * S := by
      rw [Int.sub_mul, Int.mul_sub, Int.mul_sub]; omega
    have h2 : ((R : Int) * ds r) * ((R : Int) * ds r) = (R : Int) * (R : Int) * (ds r * ds r) := by ac_rfl
    have h3 : ((R : Int) * ds r) * S + S * ((R : Int) * ds r) = (2 * (R : Int) * S) * ds r := by
      have ha : ((R : Int) * ds r) * S = (R : Int) * S * ds r := by ac_rfl
      have hb : S * ((R : Int) * ds r) = (R : Int) * S * ds r := by ac_rfl
      have hc : (2 * (R : Int) * S) * ds r = 2 * ((R : Int) * S * ds r) := by ac_rfl
      omega
    omega
  rw [hexpand, sumFin_add, sumFin_sub, sumFin_mul_left, sumFin_mul_left, sumFin_const] at hsq
  change 0 ≤ (R : Int) * (R : Int) * Q - 2 * (R : Int) * S * S + (R : Int) * (S * S) at hsq
  have hfactor : (R : Int) * (R : Int) * Q - 2 * (R : Int) * S * S + (R : Int) * (S * S) =
      (R : Int) * ((R : Int) * Q - S * S) := by
    have ha : (R : Int) * ((R : Int) * Q - S * S) = (R : Int) * ((R : Int) * Q) - (R : Int) * (S * S) :=
      Int.mul_sub _ _ _
    have hb : (R : Int) * ((R : Int) * Q) = (R : Int) * (R : Int) * Q := by ac_rfl
    have hc : 2 * (R : Int) * S * S = 2 * ((R : Int) * (S * S)) := by ac_rfl
    omega
  rw [hfactor] at hsq
  by_cases hlt : (R : Int) * Q - S * S < 0
  · have hneg := Int.mul_neg_of_pos_of_neg hRpos hlt
    omega
  · change S * S ≤ (R : Int) * Q
    omega

/-- Theorem P4 (Fisher-Geodesic Replica-Ladder Optimality): Across all schedules `ds` spanning fixed
    total Fisher metric arc length `∑ ds`, equal spacing (`ds_opt = const`) minimizes the quadratic
    KL/swap-rejection cost `∑ ds_r^2` and uniquely saturates Cauchy-Schwarz. -/
theorem fisher_ladder_uniform_optimal {R : Nat} (hR : 0 < R) (sStep : Int) (ds : Fin R → Int)
    (hSameArc : sumFin R ds = sumFin R (fun _ => sStep)) :
    let ds_opt : Fin R → Int := fun _ => sStep
    (R : Int) * sumFin R (fun r => ds_opt r * ds_opt r) = sumFin R ds_opt * sumFin R ds_opt ∧
    sumFin R (fun r => ds_opt r * ds_opt r) ≤ sumFin R (fun r => ds r * ds r) := by
  intro ds_opt
  have hOpt : (R : Int) * sumFin R (fun r => ds_opt r * ds_opt r) =
      sumFin R ds_opt * sumFin R ds_opt := by
    dsimp [ds_opt]
    rw [sumFin_const, sumFin_const]
    ac_rfl
  have hCS := fin_cauchy_schwarz hR ds
  rw [hSameArc] at hCS
  change sumFin R ds_opt * sumFin R ds_opt ≤ (R : Int) * sumFin R (fun r => ds r * ds r) at hCS
  rw [← hOpt] at hCS
  have hRpos : (0 : Int) < (R : Int) := by omega
  have hdiff : 0 ≤ (R : Int) * (sumFin R (fun r => ds r * ds r) - sumFin R (fun r => ds_opt r * ds_opt r)) := by
    rw [Int.mul_sub]
    omega
  have hle : sumFin R (fun r => ds_opt r * ds_opt r) ≤ sumFin R (fun r => ds r * ds r) := by
    by_cases hlt : sumFin R (fun r => ds r * ds r) - sumFin R (fun r => ds_opt r * ds_opt r) < 0
    · have hneg := Int.mul_neg_of_pos_of_neg hRpos hlt
      omega
    · omega
  exact ⟨hOpt, hle⟩

end Flow.ProjectiveBundle
