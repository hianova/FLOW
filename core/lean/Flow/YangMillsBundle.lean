/-
  Flow.YangMillsBundle — 4D Non-Abelian SU(2) Pure Yang-Mills & Infinite-Z^4 Quaternionic Rigidity
  ==================================================================================================
  Pure Lean 4 (Init-only, zero IO) formalization extending `Flow.ProjectiveBundle` to 4D SU(2):
    1. Exact non-commutative Lipschitz quaternion algebra (`Quat`) & Euler four-square identity;
    2. Exact 4D non-Abelian Wilson plaquette gauge covariance & invariance on infinite Z^4 (Theorem Y1);
    3. 4D Yang-Mills BPST self-duality bound, Chern-Pontryagin extremals, and Ad(SU(2)) invariance (Theorem Y2);
    4. Exact SU(2) microcanonical Over-Relaxation action, norm, and involution rigidity (Theorem Y3);
    5. Exact Z_2 center symmetry of arbitrary-N_t thermal Polyakov loops on infinite Z^4 (Theorem Y4);
    6. 4D defect-wall @swap action decomposition & exact product-weight detailed balance (Theorem Y5).
-/
import Flow.ProjectiveBundle

namespace Flow.YangMillsBundle

open Flow.GaugeBundle
open Flow.ProjectiveBundle

/-! ## Section 1: Exact Non-Commutative Quaternion Ring (`Quat`) & Euler Four-Square Identity -/

structure Quat where
  w : Int
  x : Int
  y : Int
  z : Int
  deriving DecidableEq, Repr

def qZero : Quat   := ⟨0, 0, 0, 0⟩
def qOne : Quat    := ⟨1, 0, 0, 0⟩
def qNegOne : Quat := ⟨-1, 0, 0, 0⟩
def qI : Quat      := ⟨0, 1, 0, 0⟩
def qJ : Quat      := ⟨0, 0, 1, 0⟩
def qK : Quat      := ⟨0, 0, 0, 1⟩

def qAdd (a b : Quat) : Quat := ⟨a.w + b.w, a.x + b.x, a.y + b.y, a.z + b.z⟩
def qSub (a b : Quat) : Quat := ⟨a.w - b.w, a.x - b.x, a.y - b.y, a.z - b.z⟩
def qNeg (a : Quat) : Quat   := ⟨-a.w, -a.x, -a.y, -a.z⟩
def qScale (s : Int) (a : Quat) : Quat := ⟨s * a.w, s * a.x, s * a.y, s * a.z⟩
def qConj (a : Quat) : Quat  := ⟨a.w, -a.x, -a.y, -a.z⟩

def qMul (a b : Quat) : Quat :=
  ⟨a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
   a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
   a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
   a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w⟩

def qNormSq (a : Quat) : Int :=
  a.w * a.w + a.x * a.x + a.y * a.y + a.z * a.z

def qInner (a b : Quat) : Int :=
  a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z

def qAd (g X : Quat) : Quat :=
  qMul (qMul g X) (qConj g)

private theorem int_sq_nonneg (z : Int) : 0 ≤ z * z := by
  by_cases hz : 0 ≤ z
  · exact Int.mul_nonneg hz hz
  · have hneg : 0 ≤ -z := by omega
    have hsq : (-z) * (-z) = z * z := Int.neg_mul_neg z z
    rw [← hsq]
    exact Int.mul_nonneg hneg hneg

theorem qMul_noncomm_witness : qMul qI qJ ≠ qMul qJ qI := by
  decide

theorem qOne_mul (a : Quat) : qMul qOne a = a := by
  cases a; unfold qOne qMul; simp only [Quat.mk.injEq]; grind

theorem qMul_one (a : Quat) : qMul a qOne = a := by
  cases a; unfold qOne qMul; simp only [Quat.mk.injEq]; grind

theorem qMul_assoc (a b c : Quat) : qMul (qMul a b) c = qMul a (qMul b c) := by
  cases a; cases b; cases c
  unfold qMul
  simp only [Quat.mk.injEq]
  grind

theorem qConj_conj (a : Quat) : qConj (qConj a) = a := by
  cases a; unfold qConj; simp only [Quat.mk.injEq]; grind

theorem qConj_mul (a b : Quat) : qConj (qMul a b) = qMul (qConj b) (qConj a) := by
  cases a; cases b
  unfold qConj qMul
  simp only [Quat.mk.injEq]
  grind

theorem qMul_conj_self (a : Quat) : qMul a (qConj a) = ⟨qNormSq a, 0, 0, 0⟩ := by
  cases a; unfold qMul qConj qNormSq; simp only [Quat.mk.injEq]; grind

theorem qConj_mul_self (a : Quat) : qMul (qConj a) a = ⟨qNormSq a, 0, 0, 0⟩ := by
  cases a; unfold qMul qConj qNormSq; simp only [Quat.mk.injEq]; grind

theorem qNormSq_nonneg (a : Quat) : 0 ≤ qNormSq a := by
  unfold qNormSq
  have h1 := int_sq_nonneg a.w
  have h2 := int_sq_nonneg a.x
  have h3 := int_sq_nonneg a.y
  have h4 := int_sq_nonneg a.z
  omega

theorem qNormSq_eq_zero_iff (a : Quat) : qNormSq a = 0 ↔ a = qZero := by
  unfold qNormSq qZero
  constructor
  · intro h
    have h1 := int_sq_nonneg a.w
    have h2 := int_sq_nonneg a.x
    have h3 := int_sq_nonneg a.y
    have h4 := int_sq_nonneg a.z
    have hw : a.w * a.w = 0 := by omega
    have hx : a.x * a.x = 0 := by omega
    have hy : a.y * a.y = 0 := by omega
    have hz : a.z * a.z = 0 := by omega
    have ew : a.w = 0 := by cases Int.mul_eq_zero.mp hw with | inl p => exact p | inr p => exact p
    have ex : a.x = 0 := by cases Int.mul_eq_zero.mp hx with | inl p => exact p | inr p => exact p
    have ey : a.y = 0 := by cases Int.mul_eq_zero.mp hy with | inl p => exact p | inr p => exact p
    have ez : a.z = 0 := by cases Int.mul_eq_zero.mp hz with | inl p => exact p | inr p => exact p
    cases a; simp_all
  · intro h
    subst h
    simp

theorem qNormSq_conj (a : Quat) : qNormSq (qConj a) = qNormSq a := by
  cases a; unfold qNormSq qConj; simp only; grind

theorem qSub_eq_zero_iff (a b : Quat) : qSub a b = qZero ↔ a = b := by
  cases a; cases b
  unfold qSub qZero
  simp only [Quat.mk.injEq]
  constructor
  · intro ⟨h1, h2, h3, h4⟩; refine ⟨?_, ?_, ?_, ?_⟩ <;> omega
  · intro ⟨h1, h2, h3, h4⟩; refine ⟨?_, ?_, ?_, ?_⟩ <;> omega

theorem qAdd_eq_zero_iff (a b : Quat) : qAdd a b = qZero ↔ a = qNeg b := by
  cases a; cases b
  unfold qAdd qNeg qZero
  simp only [Quat.mk.injEq]
  constructor
  · intro ⟨h1, h2, h3, h4⟩; refine ⟨?_, ?_, ?_, ?_⟩ <;> omega
  · intro ⟨h1, h2, h3, h4⟩; refine ⟨?_, ?_, ?_, ?_⟩ <;> omega

theorem euler_four_square (a b : Quat) : qNormSq (qMul a b) = qNormSq a * qNormSq b := by
  unfold qNormSq qMul; dsimp only; grind

theorem qRe_mul_comm (a b : Quat) : (qMul a b).w = (qMul b a).w := by
  unfold qMul; dsimp only; grind

theorem qInner_eq_mul_conj (a b : Quat) : qInner a b = (qMul a (qConj b)).w := by
  unfold qInner qMul qConj; dsimp only; grind

theorem qAd_preserves_normSq (g X : Quat) (hg : qNormSq g = 1) :
    qNormSq (qAd g X) = qNormSq X := by
  unfold qAd
  rw [euler_four_square, euler_four_square, qNormSq_conj, hg]
  grind

theorem qAd_preserves_inner (g X Y : Quat) (hg : qNormSq g = 1) :
    qInner (qAd g X) (qAd g Y) = qInner X Y := by
  unfold qInner qAd qMul qConj qNormSq at *
  dsimp only at *
  have hMul : (g.w * g.w + g.x * g.x + g.y * g.y + g.z * g.z) = 1 := hg
  grind

/-! ## Section 2: 4D Non-Abelian Wilson Plaquette Gauge Invariance on Infinite Z^4 (Theorem Y1) -/

def plaqHolonomy (u1 u2 u3 u4 : Quat) : Quat :=
  qMul (qMul u1 u2) (qMul (qConj u3) (qConj u4))

private theorem qNormOne_conj_mul {g : Quat} (hg : qNormSq g = 1) :
    qMul (qConj g) g = qOne := by
  rw [qConj_mul_self, hg]
  rfl

private theorem qNormOne_mul_conj {g : Quat} (hg : qNormSq g = 1) :
    qMul g (qConj g) = qOne := by
  rw [qMul_conj_self, hg]
  rfl

/-- Theorem Y1A (Non-Abelian 4D Plaquette Covariance): Transforming the 4 directed edges of any
    plaquette under local SU(2) corner elements `g0, g1, g2, g3` multiplies the ordered holonomy by `Ad(g0)`. -/
theorem plaq4D_gauge_covariant (g0 g1 g2 g3 u1 u2 u3 u4 : Quat)
    (hg1 : qNormSq g1 = 1) (hg2 : qNormSq g2 = 1) (hg3 : qNormSq g3 = 1) :
    let u1' := qMul (qMul g0 u1) (qConj g1)
    let u2' := qMul (qMul g1 u2) (qConj g2)
    let u3' := qMul (qMul g3 u3) (qConj g2)
    let u4' := qMul (qMul g0 u4) (qConj g3)
    plaqHolonomy u1' u2' u3' u4' = qAd g0 (plaqHolonomy u1 u2 u3 u4) := by
  intro u1' u2' u3' u4'
  have hc1 : qMul (qConj g1) g1 = qOne := qNormOne_conj_mul hg1
  have hc2 : qMul (qConj g2) g2 = qOne := qNormOne_conj_mul hg2
  have hc3 : qMul (qConj g3) g3 = qOne := qNormOne_conj_mul hg3
  have h12 : qMul u1' u2' = qMul (qMul g0 (qMul u1 u2)) (qConj g2) := by
    dsimp only [u1', u2']
    calc qMul (qMul (qMul g0 u1) (qConj g1)) (qMul (qMul g1 u2) (qConj g2))
        = qMul (qMul g0 u1) (qMul (qConj g1) (qMul g1 (qMul u2 (qConj g2)))) := by
            rw [qMul_assoc (qMul g0 u1) (qConj g1), qMul_assoc g1 u2 (qConj g2)]
      _ = qMul (qMul g0 u1) (qMul (qMul (qConj g1) g1) (qMul u2 (qConj g2))) := by
            rw [← qMul_assoc (qConj g1) g1]
      _ = qMul (qMul g0 u1) (qMul u2 (qConj g2)) := by rw [hc1, qOne_mul]
      _ = qMul (qMul (qMul g0 u1) u2) (qConj g2) := by rw [← qMul_assoc (qMul g0 u1) u2]
      _ = qMul (qMul g0 (qMul u1 u2)) (qConj g2) := by rw [qMul_assoc g0 u1 u2]
  have h34 : qMul (qConj u3') (qConj u4') = qMul (qMul g2 (qMul (qConj u3) (qConj u4))) (qConj g0) := by
    dsimp only [u3', u4']
    rw [qConj_mul, qConj_mul, qConj_mul, qConj_mul, qConj_conj, qConj_conj]
    calc qMul (qMul g2 (qMul (qConj u3) (qConj g3))) (qMul g3 (qMul (qConj u4) (qConj g0)))
        = qMul (qMul g2 (qConj u3)) (qMul (qConj g3) (qMul g3 (qMul (qConj u4) (qConj g0)))) := by
            rw [← qMul_assoc g2 (qConj u3) (qConj g3), qMul_assoc (qMul g2 (qConj u3)) (qConj g3)]
      _ = qMul (qMul g2 (qConj u3)) (qMul (qMul (qConj g3) g3) (qMul (qConj u4) (qConj g0))) := by
            rw [← qMul_assoc (qConj g3) g3]
      _ = qMul (qMul g2 (qConj u3)) (qMul (qConj u4) (qConj g0)) := by rw [hc3, qOne_mul]
      _ = qMul (qMul (qMul g2 (qConj u3)) (qConj u4)) (qConj g0) := by
            rw [← qMul_assoc (qMul g2 (qConj u3)) (qConj u4)]
      _ = qMul (qMul g2 (qMul (qConj u3) (qConj u4))) (qConj g0) := by
            rw [qMul_assoc g2 (qConj u3) (qConj u4)]
  unfold plaqHolonomy qAd
  rw [h12, h34]
  calc qMul (qMul (qMul g0 (qMul u1 u2)) (qConj g2)) (qMul (qMul g2 (qMul (qConj u3) (qConj u4))) (qConj g0))
      = qMul (qMul g0 (qMul u1 u2)) (qMul (qConj g2) (qMul g2 (qMul (qMul (qConj u3) (qConj u4)) (qConj g0)))) := by
          rw [qMul_assoc (qMul g0 (qMul u1 u2)) (qConj g2), qMul_assoc g2 (qMul (qConj u3) (qConj u4)) (qConj g0)]
    _ = qMul (qMul g0 (qMul u1 u2)) (qMul (qMul (qConj g2) g2) (qMul (qMul (qConj u3) (qConj u4)) (qConj g0))) := by
          rw [← qMul_assoc (qConj g2) g2]
    _ = qMul (qMul g0 (qMul u1 u2)) (qMul (qMul (qConj u3) (qConj u4)) (qConj g0)) := by
          rw [hc2, qOne_mul]
    _ = qMul (qMul g0 (qMul (qMul u1 u2) (qMul (qConj u3) (qConj u4)))) (qConj g0) := by
          rw [← qMul_assoc (qMul g0 (qMul u1 u2)) (qMul (qConj u3) (qConj u4)) (qConj g0)]
          rw [qMul_assoc g0 (qMul u1 u2) (qMul (qConj u3) (qConj u4))]

/-- Theorem Y1B (Exact Wilson Trace Gauge Invariance): Cyclic invariance of Re(q) makes the Wilson
    plaquette real trace identically invariant under arbitrary corner transforms (|g0|^2 = 1). -/
theorem wilsonTr_gauge_invariant (g0 g1 g2 g3 u1 u2 u3 u4 : Quat)
    (hg0 : qNormSq g0 = 1) (hg1 : qNormSq g1 = 1) (hg2 : qNormSq g2 = 1) (hg3 : qNormSq g3 = 1) :
    let u1' := qMul (qMul g0 u1) (qConj g1)
    let u2' := qMul (qMul g1 u2) (qConj g2)
    let u3' := qMul (qMul g3 u3) (qConj g2)
    let u4' := qMul (qMul g0 u4) (qConj g3)
    (plaqHolonomy u1' u2' u3' u4').w = (plaqHolonomy u1 u2 u3 u4).w := by
  intro u1' u2' u3' u4'
  have hcov : plaqHolonomy u1' u2' u3' u4' = qAd g0 (plaqHolonomy u1 u2 u3 u4) :=
    plaq4D_gauge_covariant g0 g1 g2 g3 u1 u2 u3 u4 hg1 hg2 hg3
  rw [congrArg Quat.w hcov]
  unfold qAd
  rw [qRe_mul_comm, ← qMul_assoc, qNormOne_conj_mul hg0, qOne_mul]

abbrev Site4D : Type := Int × Int × Int × Int
abbrev GaugeField4D : Type := Site4D → Fin 4 → Quat

def shift4D (p : Site4D) (μ : Fin 4) : Site4D :=
  let (t, x, y, z) := p
  match μ.val with
  | 0 => (t + 1, x, y, z)
  | 1 => (t, x + 1, y, z)
  | 2 => (t, x, y + 1, z)
  | _ => (t, x, y, z + 1)

private theorem shift4D_comm (p : Site4D) (μ ν : Fin 4) :
    shift4D (shift4D p μ) ν = shift4D (shift4D p ν) μ := by
  rcases p with ⟨t, x, y, z⟩
  rcases μ with ⟨m, hm⟩
  rcases ν with ⟨n, hn⟩
  have hm4 : m = 0 ∨ m = 1 ∨ m = 2 ∨ m = 3 := by omega
  have hn4 : n = 0 ∨ n = 1 ∨ n = 2 ∨ n = 3 := by omega
  rcases hm4 with rfl | rfl | rfl | rfl <;>
    rcases hn4 with rfl | rfl | rfl | rfl <;>
    simp [shift4D] <;> grind

def gaugeTransform4D (g : Site4D → Quat) (U : GaugeField4D) : GaugeField4D :=
  fun p μ => qMul (qMul (g p) (U p μ)) (qConj (g (shift4D p μ)))

def plaq4D (U : GaugeField4D) (p : Site4D) (μ ν : Fin 4) : Quat :=
  plaqHolonomy (U p μ) (U (shift4D p μ) ν) (U (shift4D p ν) μ) (U p ν)

/-- Theorem Y1C (Infinite-Z^4 Universal SU(2) Gauge Invariance): Over the entire infinite 4D
    spacetime lattice Z^4, EVERY local SU(2) gauge map `g : Site4D -> Quat` preserves every
    Wilson plaquette trace `(plaq4D U p mu nu).w` identically. -/
theorem wilsonPlaq_infinite_Z4_gauge_invariant (g : Site4D → Quat)
    (hg : ∀ p : Site4D, qNormSq (g p) = 1) (U : GaugeField4D) (p : Site4D) (μ ν : Fin 4) :
    (plaq4D (gaugeTransform4D g U) p μ ν).w = (plaq4D U p μ ν).w := by
  unfold plaq4D gaugeTransform4D
  rw [shift4D_comm p ν μ]
  exact wilsonTr_gauge_invariant (g p) (g (shift4D p μ)) (g (shift4D (shift4D p μ) ν)) (g (shift4D p ν))
    (U p μ) (U (shift4D p μ) ν) (U (shift4D p ν) μ) (U p ν)
    (hg p) (hg (shift4D p μ)) (hg (shift4D (shift4D p μ) ν)) (hg (shift4D p ν))

/-! ## Section 3: 4D Yang-Mills BPST Self-Duality Bound & Ad(SU(2)) Invariance (Theorem Y2) -/

abbrev Field3 : Type := Fin 3 → Quat

def normSq3 (F : Field3) : Int :=
  sumFin 3 (fun k => qNormSq (F k))

def inner3 (E B : Field3) : Int :=
  sumFin 3 (fun k => qInner (E k) (B k))

def sub3 (E B : Field3) : Field3 := fun k => qSub (E k) (B k)
def add3 (E B : Field3) : Field3 := fun k => qAdd (E k) (B k)
def neg3 (B : Field3) : Field3   := fun k => qNeg (B k)
def ad3 (g : Quat) (F : Field3) : Field3 := fun k => qAd g (F k)

private theorem qNormSq_sub_eq (a b : Quat) :
    qNormSq a + qNormSq b = qNormSq (qSub a b) + 2 * qInner a b := by
  unfold qNormSq qSub qInner; dsimp only; grind

private theorem qNormSq_add_eq (a b : Quat) :
    qNormSq a + qNormSq b = qNormSq (qAdd a b) - 2 * qInner a b := by
  unfold qNormSq qAdd qInner; dsimp only; grind

/-- Theorem Y2A (4D Yang-Mills Self-Duality Identity): `||E||^2 + ||B||^2 = ||E - B||^2 + 2 <E, B>`. -/
theorem ym_selfdual_identity (E B : Field3) :
    normSq3 E + normSq3 B = normSq3 (sub3 E B) + 2 * inner3 E B := by
  unfold normSq3 sub3 inner3
  rw [← sumFin_add, ← sumFin_mul_left, ← sumFin_add]
  congr 1; funext k; exact qNormSq_sub_eq (E k) (B k)

/-- Theorem Y2B (4D Yang-Mills Anti-Self-Duality Identity): `||E||^2 + ||B||^2 = ||E + B||^2 - 2 <E, B>`. -/
theorem ym_antiselfdual_identity (E B : Field3) :
    normSq3 E + normSq3 B = normSq3 (add3 E B) - 2 * inner3 E B := by
  unfold normSq3 add3 inner3
  rw [← sumFin_add, ← sumFin_mul_left, ← sumFin_sub]
  congr 1; funext k; exact qNormSq_add_eq (E k) (B k)

/-- Theorem Y2C (BPST Topological Action Lower Bound): For all color-electric and color-magnetic fields,
    `2 * |<E, B>| <= ||E||^2 + ||B||^2`. -/
theorem ym_bpst_lower_bound (E B : Field3) :
    2 * Int.ofNat (Int.natAbs (inner3 E B)) ≤ normSq3 E + normSq3 B := by
  have hs := ym_selfdual_identity E B
  have ha := ym_antiselfdual_identity E B
  have hnnS : 0 ≤ normSq3 (sub3 E B) := sumFin_nonneg 3 _ fun k => qNormSq_nonneg _
  have hnnA : 0 ≤ normSq3 (add3 E B) := sumFin_nonneg 3 _ fun k => qNormSq_nonneg _
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

/-- Theorem Y2D (Self-Dual BPST Instanton Characterization): Saturation `||E||^2 + ||B||^2 = 2 <E, B>`
    holds iff `E = B` in all 3 directions. -/
theorem ym_bpst_eq_iff_selfdual (E B : Field3) :
    normSq3 E + normSq3 B = 2 * inner3 E B ↔ ∀ k : Fin 3, E k = B k := by
  rw [ym_selfdual_identity E B]
  have hiff := sumPrefix_nonneg_eq_zero_iff (N := 3) (fun k => qNormSq (qSub (E k) (B k)))
    (fun k => qNormSq_nonneg _) 3 (Nat.le_refl 3)
  constructor
  · intro h k
    have hs0 : normSq3 (sub3 E B) = 0 := by omega
    unfold normSq3 sub3 sumFin at hs0
    have hk0 : qNormSq (qSub (E k) (B k)) = 0 := (hiff.mp hs0) k k.isLt
    exact (qSub_eq_zero_iff _ _).mp ((qNormSq_eq_zero_iff _).mp hk0)
  · intro h
    have hs0 : sumPrefix (fun k => qNormSq (qSub (E k) (B k))) 3 = 0 := by
      apply hiff.mpr
      intro k _
      rw [h k, (qSub_eq_zero_iff _ _).mpr rfl]
      exact (qNormSq_eq_zero_iff _).mpr rfl
    unfold normSq3 sub3 sumFin
    rw [hs0]
    omega

/-- Theorem Y2E (Anti-Self-Dual BPST Characterization): Saturation `||E||^2 + ||B||^2 = -2 <E, B>`
    holds iff `E = -B` in all 3 directions. -/
theorem ym_bpst_eq_iff_antiselfdual (E B : Field3) :
    normSq3 E + normSq3 B = -2 * inner3 E B ↔ ∀ k : Fin 3, E k = qNeg (B k) := by
  rw [ym_antiselfdual_identity E B]
  have hiff := sumPrefix_nonneg_eq_zero_iff (N := 3) (fun k => qNormSq (qAdd (E k) (B k)))
    (fun k => qNormSq_nonneg _) 3 (Nat.le_refl 3)
  constructor
  · intro h k
    have hs0 : normSq3 (add3 E B) = 0 := by omega
    unfold normSq3 add3 sumFin at hs0
    have hk0 : qNormSq (qAdd (E k) (B k)) = 0 := (hiff.mp hs0) k k.isLt
    exact (qAdd_eq_zero_iff _ _).mp ((qNormSq_eq_zero_iff _).mp hk0)
  · intro h
    have hs0 : sumPrefix (fun k => qNormSq (qAdd (E k) (B k))) 3 = 0 := by
      apply hiff.mpr
      intro k _
      rw [h k, (qAdd_eq_zero_iff _ _).mpr rfl]
      exact (qNormSq_eq_zero_iff _).mpr rfl
    unfold normSq3 add3 sumFin
    rw [hs0]
    omega

/-- Theorem Y2F (Ad(SU(2)) Gauge Invariance of Yang-Mills Action & Chern Density): Local adjoint conjugation
    preserves both `||E||^2 + ||B||^2` and `<E, B>` identically. -/
theorem ym_eb_ad_invariant (g : Quat) (hg : qNormSq g = 1) (E B : Field3) :
    normSq3 (ad3 g E) + normSq3 (ad3 g B) = normSq3 E + normSq3 B ∧
    inner3 (ad3 g E) (ad3 g B) = inner3 E B := by
  constructor
  · unfold normSq3 ad3
    have hE : sumFin 3 (fun k => qNormSq (qAd g (E k))) = sumFin 3 (fun k => qNormSq (E k)) := by
      congr 1; funext k; exact qAd_preserves_normSq g (E k) hg
    have hB : sumFin 3 (fun k => qNormSq (qAd g (B k))) = sumFin 3 (fun k => qNormSq (B k)) := by
      congr 1; funext k; exact qAd_preserves_normSq g (B k) hg
    rw [hE, hB]
  · unfold inner3 ad3
    congr 1; funext k; exact qAd_preserves_inner g (E k) (B k) hg

/-! ## Section 4: Exact SU(2) Microcanonical Over-Relaxation Rigidity (Theorem Y3) -/

def su2OverRelax (U V : Quat) : Quat :=
  qSub (qScale (2 * qInner U V) V) (qScale (qNormSq V) U)

/-- Theorem Y3A (Over-Relaxation Exact Action Invariance): Projective reflection `U' = 2<U,V>V - |V|^2 U`
    leaves `<U', V> = |V|^2 <U, V>` identically (dS = 0 on SU(2)). -/
theorem su2_overrelax_action_exact (U V : Quat) :
    qInner (su2OverRelax U V) V = qNormSq V * qInner U V := by
  unfold su2OverRelax qInner qSub qScale qNormSq; dsimp only; grind

/-- Theorem Y3B (Over-Relaxation Norm Preservation): `|U'|^2 = |V|^4 |U|^2` (hence `= 1` on S^3). -/
theorem su2_overrelax_preserves_normSq (U V : Quat) :
    qNormSq (su2OverRelax U V) = qNormSq V * qNormSq V * qNormSq U := by
  unfold su2OverRelax qNormSq qInner qSub qScale; dsimp only; grind

/-- Theorem Y3C (Over-Relaxation Involution): Applying SU(2) Over-Relaxation twice yields `|V|^4 U`. -/
theorem su2_overrelax_involutive (U V : Quat) :
    su2OverRelax (su2OverRelax U V) V = qScale (qNormSq V * qNormSq V) U := by
  unfold su2OverRelax qSub qScale qInner qNormSq
  simp only [Quat.mk.injEq]
  grind

/-! ## Section 5: Exact Z_2 Center Symmetry & Arbitrary-N_t Polyakov Loop (Theorem Y4) -/

def polyakovProd : (nt : Nat) → (Nat → Quat) → Quat
  | 0, _     => qOne
  | n + 1, u => qMul (polyakovProd n u) (u n)

def centerFlipSlice (t0 : Int) (U : GaugeField4D) : GaugeField4D :=
  fun p μ =>
    let (t, _, _, _) := p
    if μ.val = 0 ∧ t = t0 then qNeg (U p μ) else U p μ

private theorem qNeg_conj (a : Quat) : qConj (qNeg a) = qNeg (qConj a) := by
  cases a; rfl

private theorem qNeg_mul (a b : Quat) : qMul (qNeg a) b = qNeg (qMul a b) := by
  cases a; cases b; unfold qNeg qMul; simp only [Quat.mk.injEq]; grind

private theorem qMul_neg (a b : Quat) : qMul a (qNeg b) = qNeg (qMul a b) := by
  cases a; cases b; unfold qNeg qMul; simp only [Quat.mk.injEq]; grind

private theorem qNeg_neg (a : Quat) : qNeg (qNeg a) = a := by
  cases a; unfold qNeg; simp only [Quat.mk.injEq]; grind

/-- Theorem Y4A (Z_2 Center Symmetry of Wilson Action): Negating all temporal links on time-slice `t = t0`
    leaves EVERY Wilson plaquette on infinite Z^4 identically unchanged (`dS = 0`, global involution). -/
theorem centerFlip_preserves_plaq4D (t0 : Int) (U : GaugeField4D) (p : Site4D) (μ ν : Fin 4)
    (hmn : μ ≠ ν) :
    plaq4D (centerFlipSlice t0 U) p μ ν = plaq4D U p μ ν := by
  rcases p with ⟨t, x, y, z⟩
  rcases μ with ⟨m, hm⟩
  rcases ν with ⟨n, hn⟩
  have hmnv : m ≠ n := fun h => hmn (Fin.ext h)
  have hm4 : m = 0 ∨ m = 1 ∨ m = 2 ∨ m = 3 := by omega
  have hn4 : n = 0 ∨ n = 1 ∨ n = 2 ∨ n = 3 := by omega
  unfold plaq4D plaqHolonomy centerFlipSlice
  rcases hm4 with rfl | rfl | rfl | rfl <;>
    rcases hn4 with rfl | rfl | rfl | rfl
  all_goals
    first
    | exact absurd rfl hmnv
    | (simp only [shift4D]
       by_cases ht : t = t0
       · subst ht
         simp [qNeg_conj, qNeg_mul, qMul_neg, qNeg_neg]
       · simp [ht])

private theorem polyakovProd_agree_lt (n t0 : Nat) (hn : n ≤ t0) (u : Nat → Quat) :
    polyakovProd n (fun t => if t = t0 then qNeg (u t) else u t) = polyakovProd n u := by
  induction n with
  | zero => rfl
  | succ k ih =>
    unfold polyakovProd
    have hkt0 : k ≠ t0 := by omega
    have hkLe : k ≤ t0 := by omega
    rw [ih hkLe]
    simp [hkt0]

/-- Theorem Y4B (Odd Polyakov Order Parameter): Flipping any single time-slice `t0 < nt` negates
    the ordered temporal Wilson line and its normalized trace `Tr(L)/2 -> -Tr(L)/2` for all `nt`. -/
theorem centerFlip_negates_polyakov (nt t0 : Nat) (ht0 : t0 < nt) (u : Nat → Quat) :
    let u' : Nat → Quat := fun t => if t = t0 then qNeg (u t) else u t
    polyakovProd nt u' = qNeg (polyakovProd nt u) ∧
    (polyakovProd nt u').w = -(polyakovProd nt u).w := by
  intro u'
  have hprod : polyakovProd nt u' = qNeg (polyakovProd nt u) := by
    induction nt with
    | zero => omega
    | succ k ih =>
      unfold polyakovProd
      by_cases hk : k = t0
      · subst hk
        have hprefix := polyakovProd_agree_lt k k (Nat.le_refl k) u
        change qMul (polyakovProd k u') (if k = k then qNeg (u k) else u k) =
          qNeg (qMul (polyakovProd k u) (u k))
        rw [ite_eq_left rfl, hprefix, qMul_neg]
      · have ht0Lt : t0 < k := by omega
        have hkNe : u' k = u k := by simp [u', hk]
        rw [ih ht0Lt, hkNe, qNeg_mul]
  exact ⟨hprod, by rw [hprod]; rfl⟩

/-! ## Section 6: 4D Defect-Wall Coupling Decomposition & `@swap` Detailed Balance (Theorem Y5) -/

def defectWallAction (sBulk s1 s2 c : Int) : Int :=
  sBulk + c * s1 + c * c * s2

/-- Theorem Y5A (Exact 4D Defect-Wall `@swap` Energy Difference): The global Boltzmann exchange
    `dS` depends exclusively on the localized defect-plaquette sums `(s1, s2)` of the two replicas. -/
theorem defect_wall_swap_delta (sBulkA s1A s2A sBulkB s1B s2B c0 c1 : Int) :
    (defectWallAction sBulkA s1A s2A c1 + defectWallAction sBulkB s1B s2B c0) -
    (defectWallAction sBulkA s1A s2A c0 + defectWallAction sBulkB s1B s2B c1) =
      (c0 - c1) * ((s1B - s1A) + (c0 + c1) * (s2B - s2A)) := by
  unfold defectWallAction
  grind

/-- Theorem Y5B (4D SU(2) Multi-Replica `@swap` Detailed Balance): Adjacent defect-wall replica
    exchange satisfies exact Metropolis transition-flux symmetry over `PosRat` (`0` equilibrium bias). -/
theorem ym_replica_swap_detailed_balance (π0A π0B π1A π1B : PosRat) :
    let wAB := PosRat.mul π0A π1B
    let wBA := PosRat.mul π0B π1A
    PosRat.equiv
      (PosRat.mul wAB (PosRat.minOne (PosRat.div wBA wAB)))
      (PosRat.mul wBA (PosRat.minOne (PosRat.div wAB wBA))) :=
  replica_swap_detailed_balance_exact π0A π0B π1A π1B

end Flow.YangMillsBundle