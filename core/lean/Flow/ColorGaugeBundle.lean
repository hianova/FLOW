/-
  Flow.ColorGaugeBundle — Arbitrary SU(N) & 4D SU(3) Color Gauge Theory, Cabibbo-Marinari & Z_3 Center
  =========================================================================================================
  Pure Lean 4 (Init-only, zero IO) formalization extending `Flow.YangMillsBundle` beyond SU(2):
    1. Arbitrary-N complex matrix algebra over Gaussian integers `GInt`, adjoint invariance, and
       infinite-Z^4 SU(N) Wilson plaquette gauge invariance (Theorem C1);
    2. Cabibbo-Marinari SU(2) -> SU(N) subgroup embeddings, action reduction to 2x2 quaternionic blocks,
       exact microcanonical Over-Relaxation, and 3x3 SU(3) determinant preservation (Theorem C2);
    3. Exact Eisenstein integer ring Z[omega] (omega^2 + omega + 1 = 0) and infinite-Z^4 Z_3 center
       symmetry of the Wilson action alongside arbitrary-N_t Polyakov loop rotation (Theorem C3);
    4. N-ality zero adjoint representation character invariance, triality-3 baryon invariance, and
       exact SU(3) Casimir ratio C2(8)/C2(3) = 9/4 with perimeter-screening threshold (Theorem C4);
    5. 4D SU(3) defect-wall `@swap` decomposition & PosRat Metropolis detailed balance (Theorem C5).
-/
import Flow.YangMillsBundle

namespace Flow.ColorGaugeBundle

open Flow.GaugeBundle
open Flow.ProjectiveBundle
open Flow.YangMillsBundle

/-! ## Section 1: Arbitrary-N Complex Matrix Algebra & Infinite-Z^4 SU(N) Invariance (Theorem C1) -/

private theorem gAdd_assoc (a b c : GInt) : gAdd (gAdd a b) c = gAdd a (gAdd b c) := by
  cases a; cases b; cases c
  unfold gAdd
  simp only [GInt.mk.injEq]
  omega

private theorem gAdd_comm (a b : GInt) : gAdd a b = gAdd b a := by
  cases a; cases b
  unfold gAdd
  simp only [GInt.mk.injEq]
  omega

private theorem gAdd_zero (a : GInt) : gAdd a gZero = a := by
  cases a
  unfold gAdd gZero
  simp

private theorem gZero_add (a : GInt) : gAdd gZero a = a := by
  cases a
  unfold gAdd gZero
  simp

private theorem gZero_mul (a : GInt) : gMul gZero a = gZero := by
  cases a
  unfold gZero gMul
  simp

private theorem gMul_zero (a : GInt) : gMul a gZero = gZero := by
  cases a
  unfold gZero gMul
  simp

private theorem gMul_add (a b c : GInt) : gMul a (gAdd b c) = gAdd (gMul a b) (gMul a c) := by
  cases a; cases b; cases c
  unfold gMul gAdd
  simp only [GInt.mk.injEq]
  grind

private theorem gAdd_mul (a b c : GInt) : gMul (gAdd a b) c = gAdd (gMul a c) (gMul b c) := by
  cases a; cases b; cases c
  unfold gMul gAdd
  simp only [GInt.mk.injEq]
  grind

private theorem gConj_zero : gConj gZero = gZero := rfl

private theorem gConj_add (a b : GInt) : gConj (gAdd a b) = gAdd (gConj a) (gConj b) := by
  cases a; cases b
  unfold gConj gAdd
  simp only [Int.neg_add]

private theorem gConj_neg (a : GInt) : gConj (gNeg a) = gNeg (gConj a) := by
  cases a
  unfold gConj gNeg
  simp

private theorem gConj_one : gConj gOne = gOne := rfl

def gSumPrefix {N : Nat} (f : Fin N → GInt) : Nat → GInt
  | 0 => gZero
  | k + 1 => gAdd (gSumPrefix f k) (if h : k < N then f ⟨k, h⟩ else gZero)

def gSum (N : Nat) (f : Fin N → GInt) : GInt :=
  gSumPrefix f N

private theorem gSumPrefix_zero {N : Nat} (k : Nat) :
    gSumPrefix (fun _ : Fin N => gZero) k = gZero := by
  induction k with
  | zero => rfl
  | succ k ih =>
    simp only [gSumPrefix, ih]
    split <;> exact gZero_add gZero

theorem gSum_zero (N : Nat) : gSum N (fun _ => gZero) = gZero :=
  gSumPrefix_zero N

private theorem gSumPrefix_add {N : Nat} (f g : Fin N → GInt) (k : Nat) :
    gSumPrefix (fun i => gAdd (f i) (g i)) k = gAdd (gSumPrefix f k) (gSumPrefix g k) := by
  induction k with
  | zero => rfl
  | succ k ih =>
    simp only [gSumPrefix, ih]
    split
    · grind [gAdd]
    · simp only [gAdd_zero]

theorem gSum_add (N : Nat) (f g : Fin N → GInt) :
    gSum N (fun i => gAdd (f i) (g i)) = gAdd (gSum N f) (gSum N g) :=
  gSumPrefix_add f g N

private theorem gSumPrefix_mul_left {N : Nat} (c : GInt) (f : Fin N → GInt) (k : Nat) :
    gSumPrefix (fun i => gMul c (f i)) k = gMul c (gSumPrefix f k) := by
  induction k with
  | zero => simp [gSumPrefix, gMul_zero]
  | succ k ih =>
    simp only [gSumPrefix, ih]
    split
    · rw [gMul_add]
    · simp only [gAdd_zero]

theorem gSum_mul_left (N : Nat) (c : GInt) (f : Fin N → GInt) :
    gSum N (fun i => gMul c (f i)) = gMul c (gSum N f) :=
  gSumPrefix_mul_left c f N

private theorem gSumPrefix_mul_right {N : Nat} (f : Fin N → GInt) (c : GInt) (k : Nat) :
    gSumPrefix (fun i => gMul (f i) c) k = gMul (gSumPrefix f k) c := by
  induction k with
  | zero => simp [gSumPrefix, gZero_mul]
  | succ k ih =>
    simp only [gSumPrefix, ih]
    split
    · rw [gAdd_mul]
    · simp only [gAdd_zero]

theorem gSum_mul_right (N : Nat) (f : Fin N → GInt) (c : GInt) :
    gSum N (fun i => gMul (f i) c) = gMul (gSum N f) c :=
  gSumPrefix_mul_right f c N

private theorem gSumPrefix_conj {N : Nat} (f : Fin N → GInt) (k : Nat) :
    gSumPrefix (fun i => gConj (f i)) k = gConj (gSumPrefix f k) := by
  induction k with
  | zero => rfl
  | succ k ih =>
    simp only [gSumPrefix, ih]
    split
    · rw [gConj_add]
    · simp only [gConj_zero, gConj_add]

theorem gSum_conj (N : Nat) (f : Fin N → GInt) :
    gSum N (fun i => gConj (f i)) = gConj (gSum N f) :=
  gSumPrefix_conj f N

private theorem gSumPrefix_swap {M N : Nat} (f : Fin M → Fin N → GInt) (k m : Nat) :
    gSumPrefix (fun i => gSumPrefix (f i) m) k = gSumPrefix (fun j => gSumPrefix (fun i => f i j) k) m := by
  induction k with
  | zero =>
    simp only [gSumPrefix]
    symm
    exact gSumPrefix_zero m
  | succ k ih =>
    simp only [gSumPrefix]
    rw [ih]
    split
    · rw [← gSumPrefix_add]
    · symm
      rw [gAdd_comm]
      have hconst : gSumPrefix (fun j => gAdd (gSumPrefix (fun i => f i j) k) gZero) m =
                    gSumPrefix (fun j => gSumPrefix (fun i => f i j) k) m := by
        congr 1; funext j; exact gAdd_zero _
      rw [hconst, gZero_add]

theorem gSum_swap (M N : Nat) (f : Fin M → Fin N → GInt) :
    gSum M (fun i => gSum N (fun j => f i j)) = gSum N (fun j => gSum M (fun i => f i j)) :=
  gSumPrefix_swap f M N

private theorem gSumPrefix_single_hit {N : Nat} (idx : Fin N) (v : GInt) (k : Nat) (hk : k ≤ N) :
    gSumPrefix (fun i : Fin N => if i = idx then v else gZero) k =
      if idx.val < k then v else gZero := by
  induction k with
  | zero => simp [gSumPrefix]
  | succ k ih =>
    have hkN : k < N := hk
    have hkLe : k ≤ N := Nat.le_of_lt hkN
    simp only [gSumPrefix, hkN, ↓reduceDIte, ih hkLe]
    by_cases hlt : idx.val < k
    · have hlt1 : idx.val < k + 1 := by omega
      have hne : (⟨k, hkN⟩ : Fin N) ≠ idx := by
        intro heq
        have hv := congrArg Fin.val heq
        dsimp at hv
        omega
      simp [hlt, hlt1, hne, gAdd_zero]
    · by_cases heqv : idx.val = k
      · have hlt1 : idx.val < k + 1 := by omega
        have heq : (⟨k, hkN⟩ : Fin N) = idx := by ext; exact heqv.symm
        simp [hlt, hlt1, heq, gZero_add]
      · have hlt1 : ¬(idx.val < k + 1) := by omega
        have hne : (⟨k, hkN⟩ : Fin N) ≠ idx := by
          intro heq
          have hv := congrArg Fin.val heq
          dsimp at hv
          omega
        simp [hlt, hlt1, hne, gAdd_zero]

theorem gSum_single_hit (N : Nat) (idx : Fin N) (v : GInt) :
    gSum N (fun i : Fin N => if i = idx then v else gZero) = v := by
  unfold gSum
  rw [gSumPrefix_single_hit idx v N (Nat.le_refl N)]
  simp [idx.isLt]

private theorem gSumPrefix_re_eq {N : Nat} (f : Fin N → GInt) (k : Nat) :
    (gSumPrefix f k).re = sumPrefix (fun i => (f i).re) k := by
  induction k with
  | zero => rfl
  | succ k ih =>
    simp only [gSumPrefix, sumPrefix, gAdd, ih]
    split <;> simp [gZero]

theorem gSum_re_eq (N : Nat) (f : Fin N → GInt) :
    (gSum N f).re = sumFin N (fun i => (f i).re) :=
  gSumPrefix_re_eq f N

private theorem gSum_three (f : Fin 3 → GInt) :
    gSum 3 f = gAdd (gAdd (f 0) (f 1)) (f 2) := by
  unfold gSum
  simp [gSumPrefix, gZero_add]

abbrev Mat (N : Nat) : Type := Fin N → Fin N → GInt

def matId (N : Nat) : Mat N :=
  fun i j => if i = j then gOne else gZero

def matMul {N : Nat} (A B : Mat N) : Mat N :=
  fun i j => gSum N (fun k => gMul (A i k) (B k j))

def matDag {N : Nat} (A : Mat N) : Mat N :=
  fun i j => gConj (A j i)

def matTr {N : Nat} (A : Mat N) : GInt :=
  gSum N (fun i => A i i)

def matAd {N : Nat} (g X : Mat N) : Mat N :=
  matMul (matMul g X) (matDag g)

def IsUnitary {N : Nat} (U : Mat N) : Prop :=
  matMul U (matDag U) = matId N ∧ matMul (matDag U) U = matId N

theorem matId_mul {N : Nat} (A : Mat N) : matMul (matId N) A = A := by
  funext i j
  unfold matMul matId
  have hfn : (fun k => gMul (if i = k then gOne else gZero) (A k j)) =
      (fun k => if k = i then A i j else gZero) := by
    funext k
    by_cases hk : k = i
    · subst hk; simp [gOne_mul]
    · have hik : i ≠ k := fun h => hk h.symm
      simp [hk, hik, gZero_mul]
  rw [hfn, gSum_single_hit]

theorem matMul_id {N : Nat} (A : Mat N) : matMul A (matId N) = A := by
  funext i j
  unfold matMul matId
  have hfn : (fun k => gMul (A i k) (if k = j then gOne else gZero)) =
      (fun k => if k = j then A i j else gZero) := by
    funext k
    by_cases hk : k = j
    · subst hk; simp [gMul_one]
    · simp [hk, gMul_zero]
  rw [hfn, gSum_single_hit]

theorem matMul_assoc {N : Nat} (A B C : Mat N) :
    matMul (matMul A B) C = matMul A (matMul B C) := by
  funext i j
  unfold matMul
  calc gSum N (fun l => gMul (gSum N (fun k => gMul (A i k) (B k l))) (C l j))
      = gSum N (fun l => gSum N (fun k => gMul (gMul (A i k) (B k l)) (C l j))) := by
          congr 1; funext l; rw [← gSum_mul_right]
    _ = gSum N (fun k => gSum N (fun l => gMul (A i k) (gMul (B k l) (C l j)))) := by
          rw [gSum_swap]
          congr 1; funext k; congr 1; funext l; exact gMul_assoc _ _ _
    _ = gSum N (fun k => gMul (A i k) (gSum N (fun l => gMul (B k l) (C l j)))) := by
          congr 1; funext k; exact gSum_mul_left N (A i k) (fun l => gMul (B k l) (C l j))

theorem matDag_dag {N : Nat} (A : Mat N) : matDag (matDag A) = A := by
  funext i j
  unfold matDag
  exact gConj_conj (A i j)

theorem matDag_mul {N : Nat} (A B : Mat N) :
    matDag (matMul A B) = matMul (matDag B) (matDag A) := by
  funext i j
  unfold matDag matMul
  rw [← gSum_conj]
  congr 1
  funext k
  rw [gConj_mul, gMul_comm]

theorem matTr_mul_comm {N : Nat} (A B : Mat N) :
    matTr (matMul A B) = matTr (matMul B A) := by
  unfold matTr matMul
  rw [gSum_swap]
  congr 1
  funext k
  congr 1
  funext i
  exact gMul_comm (A i k) (B k i)

theorem matTr_ad_invariant {N : Nat} (g X : Mat N) (hg : IsUnitary g) :
    matTr (matAd g X) = matTr X := by
  unfold matAd
  rw [matTr_mul_comm (matMul g X) (matDag g), ← matMul_assoc, hg.2, matId_mul]

def plaqHolonomyN {N : Nat} (u1 u2 u3 u4 : Mat N) : Mat N :=
  matMul (matMul u1 u2) (matMul (matDag u3) (matDag u4))

abbrev GaugeField4DN (N : Nat) : Type := Site4D → Fin 4 → Mat N

def gaugeTransform4DN {N : Nat} (g : Site4D → Mat N) (U : GaugeField4DN N) : GaugeField4DN N :=
  fun p μ => matMul (matMul (g p) (U p μ)) (matDag (g (shift4D p μ)))

def plaq4DN {N : Nat} (U : GaugeField4DN N) (p : Site4D) (μ ν : Fin 4) : Mat N :=
  plaqHolonomyN (U p μ) (U (shift4D p μ) ν) (U (shift4D p ν) μ) (U p ν)

private theorem shift4D_comm_local (p : Site4D) (μ ν : Fin 4) :
    shift4D (shift4D p μ) ν = shift4D (shift4D p ν) μ := by
  rcases p with ⟨t, x, y, z⟩
  rcases μ with ⟨m, hm⟩
  rcases ν with ⟨n, hn⟩
  have hm4 : m = 0 ∨ m = 1 ∨ m = 2 ∨ m = 3 := by omega
  have hn4 : n = 0 ∨ n = 1 ∨ n = 2 ∨ n = 3 := by omega
  rcases hm4 with rfl | rfl | rfl | rfl <;>
    rcases hn4 with rfl | rfl | rfl | rfl <;>
    simp [shift4D] <;> grind

/-- Theorem C1A (Arbitrary-N Non-Abelian 4D Plaquette Covariance): Transforming the 4 directed
    edges of any plaquette under local SU(N) corner matrices multiplies the ordered holonomy by `Ad(g0)`. -/
theorem suN_plaq4D_gauge_covariant {N : Nat} (g0 g1 g2 g3 u1 u2 u3 u4 : Mat N)
    (hg1 : IsUnitary g1) (hg2 : IsUnitary g2) (hg3 : IsUnitary g3) :
    let u1' := matMul (matMul g0 u1) (matDag g1)
    let u2' := matMul (matMul g1 u2) (matDag g2)
    let u3' := matMul (matMul g3 u3) (matDag g2)
    let u4' := matMul (matMul g0 u4) (matDag g3)
    plaqHolonomyN u1' u2' u3' u4' = matAd g0 (plaqHolonomyN u1 u2 u3 u4) := by
  intro u1' u2' u3' u4'
  have hc1 : matMul (matDag g1) g1 = matId N := hg1.2
  have hc2 : matMul (matDag g2) g2 = matId N := hg2.2
  have hc3 : matMul (matDag g3) g3 = matId N := hg3.2
  have h12 : matMul u1' u2' = matMul (matMul g0 (matMul u1 u2)) (matDag g2) := by
    dsimp only [u1', u2']
    calc matMul (matMul (matMul g0 u1) (matDag g1)) (matMul (matMul g1 u2) (matDag g2))
        = matMul (matMul g0 u1) (matMul (matDag g1) (matMul g1 (matMul u2 (matDag g2)))) := by
            rw [matMul_assoc (matMul g0 u1) (matDag g1), matMul_assoc g1 u2 (matDag g2)]
      _ = matMul (matMul g0 u1) (matMul (matMul (matDag g1) g1) (matMul u2 (matDag g2))) := by
            rw [← matMul_assoc (matDag g1) g1]
      _ = matMul (matMul g0 u1) (matMul u2 (matDag g2)) := by rw [hc1, matId_mul]
      _ = matMul (matMul (matMul g0 u1) u2) (matDag g2) := by rw [← matMul_assoc (matMul g0 u1) u2]
      _ = matMul (matMul g0 (matMul u1 u2)) (matDag g2) := by rw [matMul_assoc g0 u1 u2]
  have h34 : matMul (matDag u3') (matDag u4') =
      matMul (matMul g2 (matMul (matDag u3) (matDag u4))) (matDag g0) := by
    dsimp only [u3', u4']
    rw [matDag_mul, matDag_mul, matDag_mul, matDag_mul, matDag_dag, matDag_dag]
    calc matMul (matMul g2 (matMul (matDag u3) (matDag g3))) (matMul g3 (matMul (matDag u4) (matDag g0)))
        = matMul (matMul g2 (matDag u3)) (matMul (matDag g3) (matMul g3 (matMul (matDag u4) (matDag g0)))) := by
            rw [← matMul_assoc g2 (matDag u3) (matDag g3), matMul_assoc (matMul g2 (matDag u3)) (matDag g3)]
      _ = matMul (matMul g2 (matDag u3)) (matMul (matMul (matDag g3) g3) (matMul (matDag u4) (matDag g0))) := by
            rw [← matMul_assoc (matDag g3) g3]
      _ = matMul (matMul g2 (matDag u3)) (matMul (matDag u4) (matDag g0)) := by rw [hc3, matId_mul]
      _ = matMul (matMul (matMul g2 (matDag u3)) (matDag u4)) (matDag g0) := by
            rw [← matMul_assoc (matMul g2 (matDag u3)) (matDag u4)]
      _ = matMul (matMul g2 (matMul (matDag u3) (matDag u4))) (matDag g0) := by
            rw [matMul_assoc g2 (matDag u3) (matDag u4)]
  unfold plaqHolonomyN matAd
  rw [h12, h34]
  calc matMul (matMul (matMul g0 (matMul u1 u2)) (matDag g2))
              (matMul (matMul g2 (matMul (matDag u3) (matDag u4))) (matDag g0))
      = matMul (matMul g0 (matMul u1 u2))
               (matMul (matDag g2) (matMul g2 (matMul (matMul (matDag u3) (matDag u4)) (matDag g0)))) := by
          rw [matMul_assoc (matMul g0 (matMul u1 u2)) (matDag g2),
              matMul_assoc g2 (matMul (matDag u3) (matDag u4)) (matDag g0)]
    _ = matMul (matMul g0 (matMul u1 u2))
               (matMul (matMul (matDag g2) g2) (matMul (matMul (matDag u3) (matDag u4)) (matDag g0))) := by
          rw [← matMul_assoc (matDag g2) g2]
    _ = matMul (matMul g0 (matMul u1 u2)) (matMul (matMul (matDag u3) (matDag u4)) (matDag g0)) := by
          rw [hc2, matId_mul]
    _ = matMul (matMul g0 (matMul (matMul u1 u2) (matMul (matDag u3) (matDag u4)))) (matDag g0) := by
          rw [← matMul_assoc (matMul g0 (matMul u1 u2)) (matMul (matDag u3) (matDag u4)) (matDag g0)]
          rw [matMul_assoc g0 (matMul u1 u2) (matMul (matDag u3) (matDag u4))]

/-- Theorem C1B (Exact SU(N) Wilson Trace Invariance): Both the complex trace and real trace
    `(matTr U_P).re` are identically invariant under arbitrary unitary corner transforms. -/
theorem suN_wilsonTr_gauge_invariant {N : Nat} (g0 g1 g2 g3 u1 u2 u3 u4 : Mat N)
    (hg0 : IsUnitary g0) (hg1 : IsUnitary g1) (hg2 : IsUnitary g2) (hg3 : IsUnitary g3) :
    let u1' := matMul (matMul g0 u1) (matDag g1)
    let u2' := matMul (matMul g1 u2) (matDag g2)
    let u3' := matMul (matMul g3 u3) (matDag g2)
    let u4' := matMul (matMul g0 u4) (matDag g3)
    matTr (plaqHolonomyN u1' u2' u3' u4') = matTr (plaqHolonomyN u1 u2 u3 u4) ∧
    (matTr (plaqHolonomyN u1' u2' u3' u4')).re = (matTr (plaqHolonomyN u1 u2 u3 u4)).re := by
  intro u1' u2' u3' u4'
  have hcov := suN_plaq4D_gauge_covariant g0 g1 g2 g3 u1 u2 u3 u4 hg1 hg2 hg3
  have htr : matTr (plaqHolonomyN u1' u2' u3' u4') = matTr (plaqHolonomyN u1 u2 u3 u4) := by
    rw [hcov]
    exact matTr_ad_invariant g0 (plaqHolonomyN u1 u2 u3 u4) hg0
  exact ⟨htr, congrArg GInt.re htr⟩

/-- Theorem C1C (Infinite-Z^4 Universal SU(N) Gauge Invariance): For EVERY matrix rank `N`,
    spacetime vertex `p : Site4D`, plane `(mu, nu)`, and local unitary transform `g`,
    every Wilson plaquette trace on infinite Z^4 is identically invariant. -/
theorem suN_wilsonPlaq_infinite_Z4_gauge_invariant (N : Nat) (g : Site4D → Mat N)
    (hg : ∀ p : Site4D, IsUnitary (g p)) (U : GaugeField4DN N) (p : Site4D) (μ ν : Fin 4) :
    (matTr (plaq4DN (gaugeTransform4DN g U) p μ ν)).re = (matTr (plaq4DN U p μ ν)).re := by
  unfold plaq4DN gaugeTransform4DN
  rw [shift4D_comm_local p ν μ]
  exact (suN_wilsonTr_gauge_invariant (g p) (g (shift4D p μ)) (g (shift4D (shift4D p μ) ν))
    (g (shift4D p ν)) (U p μ) (U (shift4D p μ) ν) (U (shift4D p ν) μ) (U p ν)
    (hg p) (hg (shift4D p μ)) (hg (shift4D (shift4D p μ) ν)) (hg (shift4D p ν))).2

/-! ## Section 2: Cabibbo-Marinari SU(2) -> SU(N) Embedding & SU(3) Determinant Rigidity (Theorem C2) -/

def su2Embed (N : Nat) (a b : Fin N) (_hab : a ≠ b) (α β : GInt) : Mat N :=
  fun i j =>
    if i = a ∧ j = a then α
    else if i = a ∧ j = b then β
    else if i = b ∧ j = a then gNeg (gConj β)
    else if i = b ∧ j = b then gConj α
    else if i = j ∧ i ≠ a ∧ i ≠ b then gOne
    else gZero

private theorem gMul_neg_right (x y : GInt) : gMul x (gNeg y) = gNeg (gMul x y) := by
  cases x; cases y; unfold gMul gNeg; simp only [GInt.mk.injEq]; grind

private theorem gNeg_mul_left (x y : GInt) : gMul (gNeg x) y = gNeg (gMul x y) := by
  cases x; cases y; unfold gMul gNeg; simp only [GInt.mk.injEq]; grind

private theorem gAdd_neg_self (x : GInt) : gAdd x (gNeg x) = gZero := by
  cases x; unfold gAdd gNeg gZero; simp only [GInt.mk.injEq]; constructor <;> omega

private theorem gNeg_add_self (x : GInt) : gAdd (gNeg x) x = gZero := by
  cases x; unfold gAdd gNeg gZero; simp only [GInt.mk.injEq]; constructor <;> omega

private theorem su2Embed_aa {N : Nat} (a b : Fin N) (hab : a ≠ b) (α β : GInt) :
    su2Embed N a b hab α β a a = α := by
  unfold su2Embed; simp

private theorem su2Embed_ab {N : Nat} (a b : Fin N) (hab : a ≠ b) (α β : GInt) :
    su2Embed N a b hab α β a b = β := by
  unfold su2Embed; grind

private theorem su2Embed_ba {N : Nat} (a b : Fin N) (hab : a ≠ b) (α β : GInt) :
    su2Embed N a b hab α β b a = gNeg (gConj β) := by
  unfold su2Embed; grind

private theorem su2Embed_bb {N : Nat} (a b : Fin N) (hab : a ≠ b) (α β : GInt) :
    su2Embed N a b hab α β b b = gConj α := by
  unfold su2Embed; grind

private theorem su2Embed_a_other {N : Nat} (a b : Fin N) (hab : a ≠ b) (α β : GInt)
    (j : Fin N) (hja : j ≠ a) (hjb : j ≠ b) :
    su2Embed N a b hab α β a j = gZero := by
  unfold su2Embed; grind

private theorem su2Embed_b_other {N : Nat} (a b : Fin N) (hab : a ≠ b) (α β : GInt)
    (j : Fin N) (hja : j ≠ a) (hjb : j ≠ b) :
    su2Embed N a b hab α β b j = gZero := by
  unfold su2Embed; grind

private theorem su2Embed_other_row {N : Nat} (a b : Fin N) (hab : a ≠ b) (α β : GInt)
    (i j : Fin N) (hia : i ≠ a) (hib : i ≠ b) :
    su2Embed N a b hab α β i j = if i = j then gOne else gZero := by
  unfold su2Embed; grind

private theorem su2Embed_other_col {N : Nat} (a b : Fin N) (hab : a ≠ b) (α β : GInt)
    (i j : Fin N) (hja : j ≠ a) (hjb : j ≠ b) :
    su2Embed N a b hab α β i j = if i = j then gOne else gZero := by
  unfold su2Embed; grind

private theorem gSum_two_distinct (N : Nat) (a b : Fin N) (hab : a ≠ b) (va vb : GInt) :
    gSum N (fun k => if k = a then va else if k = b then vb else gZero) = gAdd va vb := by
  have hba : b ≠ a := hab.symm
  have hfn : (fun k : Fin N => if k = a then va else if k = b then vb else gZero) =
      (fun k => gAdd (if k = a then va else gZero) (if k = b then vb else gZero)) := by
    funext k
    by_cases hka : k = a
    · rw [hka]; simp [hab, gAdd_zero]
    · by_cases hkb : k = b
      · rw [hkb]; simp [hba, gZero_add]
      · simp [hka, hkb, gZero_add]
  rw [hfn, gSum_add, gSum_single_hit, gSum_single_hit]

private theorem gSum_decompose_pair (N : Nat) (a b : Fin N) (hab : a ≠ b) (f : Fin N → GInt) :
    gSum N f = gAdd (gAdd (f a) (f b))
      (gSum N (fun k => if k ≠ a ∧ k ≠ b then f k else gZero)) := by
  have hba : b ≠ a := hab.symm
  have hfn : gSum N f = gSum N (fun k => gAdd (if k = a then f a else if k = b then f b else gZero)
      (if k ≠ a ∧ k ≠ b then f k else gZero)) := by
    congr 1; funext k
    by_cases hka : k = a
    · rw [hka]; simp [hab, gAdd_zero]
    · by_cases hkb : k = b
      · rw [hkb]; simp [hba, gAdd_zero]
      · simp [hka, hkb, gZero_add]
  rw [hfn, gSum_add, gSum_two_distinct N a b hab (f a) (f b)]

/-- Theorem C2A (Arbitrary-N Cabibbo-Marinari Unitarity): Embedding any `|α|^2 + |β|^2 = 1`
    SU(2) matrix into distinct indices `(a, b)` yields an exact unitary matrix in `U(N)`. -/
theorem su2Embed_unitary (N : Nat) (a b : Fin N) (hab : a ≠ b) (α β : GInt)
    (hnorm : gNormSq α + gNormSq β = 1) :
    IsUnitary (su2Embed N a b hab α β) := by
  let R := su2Embed N a b hab α β
  have hba : b ≠ a := hab.symm
  have hUnitRe : (⟨gNormSq α + gNormSq β, 0⟩ : GInt) = gOne := by
    rw [hnorm]; rfl
  have hLeft : matMul R (matDag R) = matId N := by
    funext i j
    unfold matMul matDag matId
    by_cases hia : i = a
    · rw [hia]
      by_cases hja : j = a
      · rw [hja]
        have hfn : (fun k => gMul (R a k) (gConj (R a k))) =
            (fun k => if k = a then gMul α (gConj α)
                      else if k = b then gMul β (gConj β) else gZero) := by
          funext k
          by_cases hka : k = a
          · rw [hka]; simp [R, su2Embed_aa]
          · by_cases hkb : k = b
            · rw [hkb]; simp [R, hba, su2Embed_ab]
            · simp [R, hka, hkb, su2Embed_a_other a b hab α β k hka hkb, gConj_zero, gMul_zero]
        rw [hfn, gSum_two_distinct N a b hab, gMul_conj_self, gMul_conj_self]
        simpa [gAdd, hnorm] using hUnitRe
      · by_cases hjb : j = b
        · rw [hjb]
          have hfn : (fun k => gMul (R a k) (gConj (R b k))) =
              (fun k => if k = a then gMul α (gNeg β)
                        else if k = b then gMul β α else gZero) := by
            funext k
            by_cases hka : k = a
            · rw [hka]; simp [R, su2Embed_aa, su2Embed_ba, gConj_neg, gConj_conj]
            · by_cases hkb : k = b
              · rw [hkb]; simp [R, hba, su2Embed_ab, su2Embed_bb, gConj_conj]
              · simp [R, hka, hkb, su2Embed_a_other a b hab α β k hka hkb, gZero_mul]
          rw [hfn, gSum_two_distinct N a b hab, gMul_neg_right, gMul_comm α β, gNeg_add_self]
          simp [hab]
        · have haj : a ≠ j := fun h => hja h.symm
          have hfn : (fun k => gMul (R a k) (gConj (R j k))) = (fun _ => gZero) := by
            funext k
            dsimp only [R]
            rw [su2Embed_other_row a b hab α β j k hja hjb]
            by_cases hjk : j = k
            · rw [← hjk, su2Embed_a_other a b hab α β j hja hjb, gZero_mul]
            · simp [hjk, gConj_zero, gMul_zero]
          rw [hfn, gSum_zero]; simp [haj]
    · by_cases hib : i = b
      · rw [hib]
        by_cases hja : j = a
        · rw [hja]
          have hfn : (fun k => gMul (R b k) (gConj (R a k))) =
              (fun k => if k = a then gMul (gNeg (gConj β)) (gConj α)
                        else if k = b then gMul (gConj α) (gConj β) else gZero) := by
            funext k
            by_cases hka : k = a
            · rw [hka]; simp [R, su2Embed_ba, su2Embed_aa]
            · by_cases hkb : k = b
              · rw [hkb]; simp [R, hba, su2Embed_bb, su2Embed_ab]
              · simp [R, hka, hkb, su2Embed_b_other a b hab α β k hka hkb, gZero_mul]
          rw [hfn, gSum_two_distinct N a b hab, gNeg_mul_left, gMul_comm (gConj β) (gConj α), gNeg_add_self]
          simp [hba]
        · by_cases hjb : j = b
          · rw [hjb]
            have hfn : (fun k => gMul (R b k) (gConj (R b k))) =
                (fun k => if k = a then gMul (gNeg (gConj β)) (gNeg β)
                          else if k = b then gMul (gConj α) α else gZero) := by
              funext k
              by_cases hka : k = a
              · rw [hka]; simp [R, su2Embed_ba, gConj_neg, gConj_conj]
              · by_cases hkb : k = b
                · rw [hkb]; simp [R, hba, su2Embed_bb, gConj_conj]
                · simp [R, hka, hkb, su2Embed_b_other a b hab α β k hka hkb, gZero_mul]
            rw [hfn, gSum_two_distinct N a b hab, gNeg_mul_left, gMul_neg_right]
            have hneg2 : gNeg (gNeg (gMul (gConj β) β)) = gMul (gConj β) β := by
              cases β; unfold gNeg gMul; simp only [GInt.mk.injEq]; omega
            rw [hneg2, gConj_mul_self, gConj_mul_self]
            have hsum : gAdd ⟨gNormSq β, 0⟩ ⟨gNormSq α, 0⟩ = ⟨gNormSq α + gNormSq β, 0⟩ := by
              unfold gAdd; simp only [GInt.mk.injEq]; omega
            rw [hsum, hUnitRe]; simp
          · have hbj : b ≠ j := fun h => hjb h.symm
            have hfn : (fun k => gMul (R b k) (gConj (R j k))) = (fun _ => gZero) := by
              funext k
              dsimp only [R]
              rw [su2Embed_other_row a b hab α β j k hja hjb]
              by_cases hjk : j = k
              · rw [← hjk, su2Embed_b_other a b hab α β j hja hjb, gZero_mul]
              · simp [hjk, gConj_zero, gMul_zero]
            rw [hfn, gSum_zero]; simp [hbj]
      · have hfn : (fun k => gMul (R i k) (gConj (R j k))) =
            (fun k => if k = i then gConj (R j i) else gZero) := by
          funext k
          dsimp only [R]
          rw [su2Embed_other_row a b hab α β i k hia hib]
          by_cases hki : k = i
          · rw [hki]; simp [gOne_mul]
          · have hik : i ≠ k := fun h => hki h.symm
            simp [hik, hki, gZero_mul]
        rw [hfn, gSum_single_hit]
        dsimp only [R]
        rw [su2Embed_other_col a b hab α β j i hia hib]
        by_cases hij : i = j
        · simp [hij, gConj_one]
        · simp [hij, Ne.symm hij, gConj_zero]
  have hRight : matMul (matDag R) R = matId N := by
    funext i j
    unfold matMul matDag matId
    by_cases hia : i = a
    · rw [hia]
      by_cases hja : j = a
      · rw [hja]
        have hfn : (fun k => gMul (gConj (R k a)) (R k a)) =
            (fun k => if k = a then gMul (gConj α) α
                      else if k = b then gMul (gNeg β) (gNeg (gConj β)) else gZero) := by
          funext k
          by_cases hka : k = a
          · rw [hka]; simp [R, su2Embed_aa]
          · by_cases hkb : k = b
            · rw [hkb]; simp [R, hba, su2Embed_ba, gConj_neg, gConj_conj]
            · simp [R, hka, hkb, su2Embed_other_row a b hab α β k a hka hkb, gConj_zero, gZero_mul]
        rw [hfn, gSum_two_distinct N a b hab, gNeg_mul_left, gMul_neg_right]
        have hneg2 : gNeg (gNeg (gMul β (gConj β))) = gMul β (gConj β) := by
          cases β; unfold gNeg gMul; simp only [GInt.mk.injEq]; omega
        rw [hneg2, gConj_mul_self, gMul_conj_self]
        simpa [gAdd, hnorm] using hUnitRe
      · by_cases hjb : j = b
        · rw [hjb]
          have hfn : (fun k => gMul (gConj (R k a)) (R k b)) =
              (fun k => if k = a then gMul (gConj α) β
                        else if k = b then gMul (gNeg β) (gConj α) else gZero) := by
            funext k
            by_cases hka : k = a
            · rw [hka]; simp [R, su2Embed_aa, su2Embed_ab]
            · by_cases hkb : k = b
              · rw [hkb]; simp [R, hba, su2Embed_ba, su2Embed_bb, gConj_neg, gConj_conj]
              · simp [R, hka, hkb, su2Embed_other_row a b hab α β k a hka hkb, gConj_zero, gZero_mul]
          rw [hfn, gSum_two_distinct N a b hab, gNeg_mul_left, gMul_comm β (gConj α), gAdd_neg_self]
          simp [hab]
        · have haj : a ≠ j := fun h => hja h.symm
          have hfn : (fun k => gMul (gConj (R k a)) (R k j)) = (fun _ => gZero) := by
            funext k
            dsimp only [R]
            rw [su2Embed_other_col a b hab α β k j hja hjb]
            by_cases hkj : k = j
            · rw [hkj, su2Embed_other_row a b hab α β j a hja hjb]
              simp [hja, gConj_zero, gZero_mul]
            · simp [hkj, gMul_zero]
          rw [hfn, gSum_zero]; simp [haj]
    · by_cases hib : i = b
      · rw [hib]
        by_cases hja : j = a
        · rw [hja]
          have hfn : (fun k => gMul (gConj (R k b)) (R k a)) =
              (fun k => if k = a then gMul (gConj β) α
                        else if k = b then gMul α (gNeg (gConj β)) else gZero) := by
            funext k
            by_cases hka : k = a
            · rw [hka]; simp [R, su2Embed_ab, su2Embed_aa]
            · by_cases hkb : k = b
              · rw [hkb]; simp [R, hba, su2Embed_bb, su2Embed_ba, gConj_conj]
              · simp [R, hka, hkb, su2Embed_other_row a b hab α β k b hka hkb, gConj_zero, gZero_mul]
          rw [hfn, gSum_two_distinct N a b hab, gMul_neg_right, gMul_comm α (gConj β), gAdd_neg_self]
          simp [hba]
        · by_cases hjb : j = b
          · rw [hjb]
            have hfn : (fun k => gMul (gConj (R k b)) (R k b)) =
                (fun k => if k = a then gMul (gConj β) β
                          else if k = b then gMul α (gConj α) else gZero) := by
              funext k
              by_cases hka : k = a
              · rw [hka]; simp [R, su2Embed_ab]
              · by_cases hkb : k = b
                · rw [hkb]; simp [R, hba, su2Embed_bb, gConj_conj]
                · simp [R, hka, hkb, su2Embed_other_row a b hab α β k b hka hkb, gConj_zero, gZero_mul]
            rw [hfn, gSum_two_distinct N a b hab, gConj_mul_self, gMul_conj_self]
            have hsum : gAdd ⟨gNormSq β, 0⟩ ⟨gNormSq α, 0⟩ = ⟨gNormSq α + gNormSq β, 0⟩ := by
              unfold gAdd; simp only [GInt.mk.injEq]; omega
            rw [hsum, hUnitRe]; simp
          · have hbj : b ≠ j := fun h => hjb h.symm
            have hfn : (fun k => gMul (gConj (R k b)) (R k j)) = (fun _ => gZero) := by
              funext k
              dsimp only [R]
              rw [su2Embed_other_col a b hab α β k j hja hjb]
              by_cases hkj : k = j
              · rw [hkj, su2Embed_other_row a b hab α β j b hja hjb]
                simp [hjb, gConj_zero, gZero_mul]
              · simp [hkj, gMul_zero]
            rw [hfn, gSum_zero]; simp [hbj]
      · have hfn : (fun k => gMul (gConj (R k i)) (R k j)) =
            (fun k => if k = i then R i j else gZero) := by
          funext k
          dsimp only [R]
          rw [su2Embed_other_col a b hab α β k i hia hib]
          by_cases hki : k = i
          · rw [hki]; simp [gConj_one, gOne_mul]
          · simp [hki, gConj_zero, gZero_mul]
        rw [hfn, gSum_single_hit]
        dsimp only [R]
        rw [su2Embed_other_row a b hab α β i j hia hib]
  exact ⟨hLeft, hRight⟩

/-- Theorem C2B (Unitary Closure Under Left Cabibbo-Marinari Multiplication). -/
theorem su2Embed_mul_preserves_unitary {N : Nat} (a b : Fin N) (hab : a ≠ b)
    (α β : GInt) (hnorm : gNormSq α + gNormSq β = 1) (U : Mat N) (hU : IsUnitary U) :
    IsUnitary (matMul (su2Embed N a b hab α β) U) := by
  have hR := su2Embed_unitary N a b hab α β hnorm
  let R := su2Embed N a b hab α β
  constructor
  · rw [matDag_mul, matMul_assoc, ← matMul_assoc U (matDag U) (matDag R), hU.1, matId_mul, hR.1]
  · rw [matDag_mul, matMul_assoc (matDag U) (matDag R) (matMul R U), ← matMul_assoc (matDag R) R U,
        hR.2, matId_mul, hU.2]

def subblockQuat {N : Nat} (W : Mat N) (a b : Fin N) : Quat :=
  { w := (W a a).re + (W b b).re
  , x := -((W a b).im + (W b a).im)
  , y := (W b a).re - (W a b).re
  , z := (W b b).im - (W a a).im }

def embedQuat (q : Quat) : GInt × GInt :=
  (⟨q.w, q.z⟩, ⟨q.y, q.x⟩)

/-- Theorem C2C (Cabibbo-Marinari Exact Action Reduction to 2x2 SU(2) Quaternionic Block):
    Left-multiplying `W : Mat N` by any embedded SU(2) quaternion `q` reduces `Re Tr(R_ab(q) W)`
    to the spectating diagonal trace plus exact quaternionic inner product `<q, subblockQuat W a b>`. -/
theorem cabibbo_marinari_action_reduction (N : Nat) (a b : Fin N) (hab : a ≠ b)
    (W : Mat N) (q : Quat) :
    let R := su2Embed N a b hab (embedQuat q).1 (embedQuat q).2
    (matTr (matMul R W)).re =
      sumFin N (fun k => if k ≠ a ∧ k ≠ b then (W k k).re else 0) +
      qInner q (subblockQuat W a b) := by
  intro R
  have hba : b ≠ a := fun h => hab h.symm
  unfold matTr matMul
  rw [gSum_decompose_pair N a b hab]
  have rowA : gSum N (fun l => gMul (R a l) (W l a)) =
      gAdd (gMul (embedQuat q).1 (W a a)) (gMul (embedQuat q).2 (W b a)) := by
    have hfn : (fun l => gMul (R a l) (W l a)) =
        (fun l => if l = a then gMul (embedQuat q).1 (W a a)
                  else if l = b then gMul (embedQuat q).2 (W b a) else gZero) := by
      funext l
      by_cases hla : l = a
      · rw [hla]; simp [R, su2Embed_aa]
      · by_cases hlb : l = b
        · rw [hlb]; simp [R, hba, su2Embed_ab]
        · simp [R, hla, hlb, su2Embed_a_other a b hab (embedQuat q).1 (embedQuat q).2 l hla hlb, gZero_mul]
    rw [hfn, gSum_two_distinct N a b hab]
  have rowB : gSum N (fun l => gMul (R b l) (W l b)) =
      gAdd (gMul (gNeg (gConj (embedQuat q).2)) (W a b)) (gMul (gConj (embedQuat q).1) (W b b)) := by
    have hfn : (fun l => gMul (R b l) (W l b)) =
        (fun l => if l = a then gMul (gNeg (gConj (embedQuat q).2)) (W a b)
                  else if l = b then gMul (gConj (embedQuat q).1) (W b b) else gZero) := by
      funext l
      by_cases hla : l = a
      · rw [hla]; simp [R, su2Embed_ba]
      · by_cases hlb : l = b
        · rw [hlb]; simp [R, hba, su2Embed_bb]
        · simp [R, hla, hlb, su2Embed_b_other a b hab (embedQuat q).1 (embedQuat q).2 l hla hlb, gZero_mul]
    rw [hfn, gSum_two_distinct N a b hab]
  have rowOther : gSum N (fun k => if k ≠ a ∧ k ≠ b then gSum N (fun l => gMul (R k l) (W l k)) else gZero) =
      gSum N (fun k => if k ≠ a ∧ k ≠ b then W k k else gZero) := by
    congr 1
    funext k
    split
    · rename_i hkab
      have hfn : (fun l => gMul (R k l) (W l k)) = (fun l => if l = k then W k k else gZero) := by
        funext l
        dsimp only [R]
        rw [su2Embed_other_row a b hab (embedQuat q).1 (embedQuat q).2 k l hkab.1 hkab.2]
        by_cases hlk : l = k
        · rw [hlk]; simp [gOne_mul]
        · have hkl : k ≠ l := fun h => hlk h.symm
          simp [hlk, hkl, gZero_mul]
      rw [hfn, gSum_single_hit]
    · rfl
  rw [rowA, rowB, rowOther]
  have hReSum : (gAdd (gAdd (gAdd (gMul (embedQuat q).1 (W a a)) (gMul (embedQuat q).2 (W b a)))
                            (gAdd (gMul (gNeg (gConj (embedQuat q).2)) (W a b)) (gMul (gConj (embedQuat q).1) (W b b))))
                      (gSum N (fun k => if k ≠ a ∧ k ≠ b then W k k else gZero))).re =
      (gSum N (fun k => if k ≠ a ∧ k ≠ b then W k k else gZero)).re +
      ((gMul (embedQuat q).1 (W a a)).re + (gMul (embedQuat q).2 (W b a)).re +
       (gMul (gNeg (gConj (embedQuat q).2)) (W a b)).re + (gMul (gConj (embedQuat q).1) (W b b)).re) := by
    unfold gAdd; dsimp only; omega
  rw [hReSum, gSum_re_eq]
  have hOtherRe : sumFin N (fun i => (if i ≠ a ∧ i ≠ b then W i i else gZero).re) =
      sumFin N (fun k => if k ≠ a ∧ k ≠ b then (W k k).re else 0) := by
    congr 1; funext k
    by_cases hkab : k ≠ a ∧ k ≠ b
    · simp [hkab]
    · simp [hkab, gZero]
  rw [hOtherRe]
  unfold embedQuat subblockQuat qInner gMul gNeg gConj
  dsimp only
  grind

/-- Theorem C2D (Cabibbo-Marinari Microcanonical Over-Relaxation Exactness): Reflecting any
    embedded SU(2) quaternion against the 2x2 subblock staple projection `v = subblockQuat W a b`
    preserves `S` identically (`<q', v> = |v|^2 <q, v>`) on arbitrary SU(N) lattices. -/
theorem cabibbo_marinari_overrelax_action_exact (N : Nat) (a b : Fin N) (hab : a ≠ b)
    (W : Mat N) (q : Quat) :
    let v := subblockQuat W a b
    let q' := su2OverRelax q v
    let R := su2Embed N a b hab (embedQuat q').1 (embedQuat q').2
    (matTr (matMul R W)).re =
      sumFin N (fun k => if k ≠ a ∧ k ≠ b then (W k k).re else 0) + qNormSq v * qInner q v := by
  intro v q' R
  rw [cabibbo_marinari_action_reduction N a b hab W q', su2_overrelax_action_exact q v]

def su3Det (M : Mat 3) : GInt :=
  let c0 := gSub (gMul (M 1 1) (M 2 2)) (gMul (M 1 2) (M 2 1))
  let c1 := gSub (gMul (M 1 0) (M 2 2)) (gMul (M 1 2) (M 2 0))
  let c2 := gSub (gMul (M 1 0) (M 2 1)) (gMul (M 1 1) (M 2 0))
  gAdd (gSub (gMul (M 0 0) c0) (gMul (M 0 1) c1)) (gMul (M 0 2) c2)

theorem su3Det_id : su3Det (matId 3) = gOne := by
  decide

private theorem su3_embed_01_eq (α β : GInt) (U : Mat 3) :
    matMul (su2Embed 3 0 1 (by decide) α β) U = fun i j =>
      match i.val with
      | 0 => gAdd (gMul α (U 0 j)) (gMul β (U 1 j))
      | 1 => gAdd (gMul (gNeg (gConj β)) (U 0 j)) (gMul (gConj α) (U 1 j))
      | _ => U 2 j := by
  funext i j
  unfold matMul
  rw [gSum_three]
  rcases i with ⟨iv, hiv⟩
  have hiv3 : iv = 0 ∨ iv = 1 ∨ iv = 2 := by omega
  rcases hiv3 with rfl | rfl | rfl
  · simp [su2Embed_aa, su2Embed_ab, su2Embed_a_other, gZero_mul, gAdd_zero]
  · simp [su2Embed_ba, su2Embed_bb, su2Embed_b_other, gZero_mul, gAdd_zero]
  · simp [su2Embed_other_row, gOne_mul, gZero_mul, gZero_add]

private theorem su3_embed_02_eq (α β : GInt) (U : Mat 3) :
    matMul (su2Embed 3 0 2 (by decide) α β) U = fun i j =>
      match i.val with
      | 0 => gAdd (gMul α (U 0 j)) (gMul β (U 2 j))
      | 1 => U 1 j
      | _ => gAdd (gMul (gNeg (gConj β)) (U 0 j)) (gMul (gConj α) (U 2 j)) := by
  funext i j
  unfold matMul
  rw [gSum_three]
  rcases i with ⟨iv, hiv⟩
  have hiv3 : iv = 0 ∨ iv = 1 ∨ iv = 2 := by omega
  rcases hiv3 with rfl | rfl | rfl
  · simp [su2Embed_aa, su2Embed_ab, su2Embed_a_other, gZero_mul, gAdd_zero]
  · simp [su2Embed_other_row, gOne_mul, gZero_mul, gZero_add, gAdd_zero]
  · simp [su2Embed_ba, su2Embed_bb, su2Embed_b_other, gZero_mul, gAdd_zero]

private theorem su3_embed_12_eq (α β : GInt) (U : Mat 3) :
    matMul (su2Embed 3 1 2 (by decide) α β) U = fun i j =>
      match i.val with
      | 0 => U 0 j
      | 1 => gAdd (gMul α (U 1 j)) (gMul β (U 2 j))
      | _ => gAdd (gMul (gNeg (gConj β)) (U 1 j)) (gMul (gConj α) (U 2 j)) := by
  funext i j
  unfold matMul
  rw [gSum_three]
  rcases i with ⟨iv, hiv⟩
  have hiv3 : iv = 0 ∨ iv = 1 ∨ iv = 2 := by omega
  rcases hiv3 with rfl | rfl | rfl
  · simp [su2Embed_other_row, gOne_mul, gZero_mul, gAdd_zero]
  · simp [su2Embed_aa, su2Embed_ab, su2Embed_a_other, gZero_mul, gZero_add]
  · simp [su2Embed_ba, su2Embed_bb, su2Embed_b_other, gZero_mul, gZero_add]

private def det2 (x0 x1 y0 y1 : GInt) : GInt :=
  gSub (gMul x0 y1) (gMul x1 y0)

private theorem gSub_add_dist (a b c d : GInt) : gAdd (gSub a b) (gSub c d) = gSub (gAdd a c) (gAdd b d) := by
  cases a; cases b; cases c; cases d; unfold gAdd gSub; simp only [GInt.mk.injEq]; constructor <;> omega

private theorem gMul_sub (a b c : GInt) : gMul a (gSub b c) = gSub (gMul a b) (gMul a c) := by
  cases a; cases b; cases c; unfold gMul gSub; simp only [GInt.mk.injEq]; grind

private theorem su3Det_row2 (M : Mat 3) :
    su3Det M = gAdd (gSub (gMul (M 2 0) (det2 (M 0 1) (M 0 2) (M 1 1) (M 1 2)))
                          (gMul (M 2 1) (det2 (M 0 0) (M 0 2) (M 1 0) (M 1 2))))
                    (gMul (M 2 2) (det2 (M 0 0) (M 0 1) (M 1 0) (M 1 1))) := by
  unfold su3Det det2 gAdd gSub gMul
  simp only [GInt.mk.injEq]
  grind

private theorem su3Det_row1 (M : Mat 3) :
    su3Det M = gAdd (gSub (gMul (M 1 1) (det2 (M 0 0) (M 0 2) (M 2 0) (M 2 2)))
                          (gMul (M 1 0) (det2 (M 0 1) (M 0 2) (M 2 1) (M 2 2))))
                    (gNeg (gMul (M 1 2) (det2 (M 0 0) (M 0 1) (M 2 0) (M 2 1)))) := by
  unfold su3Det det2 gAdd gSub gMul gNeg
  simp only [GInt.mk.injEq]
  grind

private theorem det2_su2_comb (α β x0 x1 y0 y1 : GInt) :
    det2 (gAdd (gMul α x0) (gMul β y0)) (gAdd (gMul α x1) (gMul β y1))
         (gAdd (gMul (gNeg (gConj β)) x0) (gMul (gConj α) y0))
         (gAdd (gMul (gNeg (gConj β)) x1) (gMul (gConj α) y1)) =
      gMul ⟨gNormSq α + gNormSq β, 0⟩ (det2 x0 x1 y0 y1) := by
  unfold det2 gNormSq gAdd gSub gMul gNeg gConj
  simp only [GInt.mk.injEq]
  grind

private theorem det_scale_pair_01 (α β : GInt) (U : Mat 3) :
    su3Det (fun i j =>
      match i.val with
      | 0 => gAdd (gMul α (U 0 j)) (gMul β (U 1 j))
      | 1 => gAdd (gMul (gNeg (gConj β)) (U 0 j)) (gMul (gConj α) (U 1 j))
      | _ => U 2 j) =
      gMul ⟨gNormSq α + gNormSq β, 0⟩ (su3Det U) := by
  rw [su3Det_row2, su3Det_row2 U]
  change gAdd (gSub (gMul (U 2 0) (det2 (gAdd (gMul α (U 0 1)) (gMul β (U 1 1)))
                                        (gAdd (gMul α (U 0 2)) (gMul β (U 1 2)))
                                        (gAdd (gMul (gNeg (gConj β)) (U 0 1)) (gMul (gConj α) (U 1 1)))
                                        (gAdd (gMul (gNeg (gConj β)) (U 0 2)) (gMul (gConj α) (U 1 2)))))
                    (gMul (U 2 1) (det2 (gAdd (gMul α (U 0 0)) (gMul β (U 1 0)))
                                        (gAdd (gMul α (U 0 2)) (gMul β (U 1 2)))
                                        (gAdd (gMul (gNeg (gConj β)) (U 0 0)) (gMul (gConj α) (U 1 0)))
                                        (gAdd (gMul (gNeg (gConj β)) (U 0 2)) (gMul (gConj α) (U 1 2))))))
              (gMul (U 2 2) (det2 (gAdd (gMul α (U 0 0)) (gMul β (U 1 0)))
                                  (gAdd (gMul α (U 0 1)) (gMul β (U 1 1)))
                                  (gAdd (gMul (gNeg (gConj β)) (U 0 0)) (gMul (gConj α) (U 1 0)))
                                  (gAdd (gMul (gNeg (gConj β)) (U 0 1)) (gMul (gConj α) (U 1 1))))) =
    gMul ⟨gNormSq α + gNormSq β, 0⟩
      (gAdd (gSub (gMul (U 2 0) (det2 (U 0 1) (U 0 2) (U 1 1) (U 1 2)))
                  (gMul (U 2 1) (det2 (U 0 0) (U 0 2) (U 1 0) (U 1 2))))
            (gMul (U 2 2) (det2 (U 0 0) (U 0 1) (U 1 0) (U 1 1))))
  simp only [det2_su2_comb]
  let s : GInt := ⟨gNormSq α + gNormSq β, 0⟩
  calc gAdd (gSub (gMul (U 2 0) (gMul s (det2 (U 0 1) (U 0 2) (U 1 1) (U 1 2))))
                  (gMul (U 2 1) (gMul s (det2 (U 0 0) (U 0 2) (U 1 0) (U 1 2)))))
            (gMul (U 2 2) (gMul s (det2 (U 0 0) (U 0 1) (U 1 0) (U 1 1))))
      = gAdd (gSub (gMul s (gMul (U 2 0) (det2 (U 0 1) (U 0 2) (U 1 1) (U 1 2))))
                   (gMul s (gMul (U 2 1) (det2 (U 0 0) (U 0 2) (U 1 0) (U 1 2)))))
             (gMul s (gMul (U 2 2) (det2 (U 0 0) (U 0 1) (U 1 0) (U 1 1)))) := by
          rw [← gMul_assoc (U 2 0) s, gMul_comm (U 2 0) s, gMul_assoc s (U 2 0)]
          rw [← gMul_assoc (U 2 1) s, gMul_comm (U 2 1) s, gMul_assoc s (U 2 1)]
          rw [← gMul_assoc (U 2 2) s, gMul_comm (U 2 2) s, gMul_assoc s (U 2 2)]
    _ = gMul s (gAdd (gSub (gMul (U 2 0) (det2 (U 0 1) (U 0 2) (U 1 1) (U 1 2)))
                           (gMul (U 2 1) (det2 (U 0 0) (U 0 2) (U 1 0) (U 1 2))))
                     (gMul (U 2 2) (det2 (U 0 0) (U 0 1) (U 1 0) (U 1 1)))) := by
          rw [← gMul_sub, ← gMul_add]

private theorem det_scale_pair_02 (α β : GInt) (U : Mat 3) :
    su3Det (fun i j =>
      match i.val with
      | 0 => gAdd (gMul α (U 0 j)) (gMul β (U 2 j))
      | 1 => U 1 j
      | _ => gAdd (gMul (gNeg (gConj β)) (U 0 j)) (gMul (gConj α) (U 2 j))) =
      gMul ⟨gNormSq α + gNormSq β, 0⟩ (su3Det U) := by
  rw [su3Det_row1, su3Det_row1 U]
  change gAdd (gSub (gMul (U 1 1) (det2 (gAdd (gMul α (U 0 0)) (gMul β (U 2 0)))
                                        (gAdd (gMul α (U 0 2)) (gMul β (U 2 2)))
                                        (gAdd (gMul (gNeg (gConj β)) (U 0 0)) (gMul (gConj α) (U 2 0)))
                                        (gAdd (gMul (gNeg (gConj β)) (U 0 2)) (gMul (gConj α) (U 2 2)))))
                    (gMul (U 1 0) (det2 (gAdd (gMul α (U 0 1)) (gMul β (U 2 1)))
                                        (gAdd (gMul α (U 0 2)) (gMul β (U 2 2)))
                                        (gAdd (gMul (gNeg (gConj β)) (U 0 1)) (gMul (gConj α) (U 2 1)))
                                        (gAdd (gMul (gNeg (gConj β)) (U 0 2)) (gMul (gConj α) (U 2 2))))))
              (gNeg (gMul (U 1 2) (det2 (gAdd (gMul α (U 0 0)) (gMul β (U 2 0)))
                                        (gAdd (gMul α (U 0 1)) (gMul β (U 2 1)))
                                        (gAdd (gMul (gNeg (gConj β)) (U 0 0)) (gMul (gConj α) (U 2 0)))
                                        (gAdd (gMul (gNeg (gConj β)) (U 0 1)) (gMul (gConj α) (U 2 1)))))) =
    gMul ⟨gNormSq α + gNormSq β, 0⟩
      (gAdd (gSub (gMul (U 1 1) (det2 (U 0 0) (U 0 2) (U 2 0) (U 2 2)))
                  (gMul (U 1 0) (det2 (U 0 1) (U 0 2) (U 2 1) (U 2 2))))
            (gNeg (gMul (U 1 2) (det2 (U 0 0) (U 0 1) (U 2 0) (U 2 1)))))
  simp only [det2_su2_comb]
  let s : GInt := ⟨gNormSq α + gNormSq β, 0⟩
  calc gAdd (gSub (gMul (U 1 1) (gMul s (det2 (U 0 0) (U 0 2) (U 2 0) (U 2 2))))
                  (gMul (U 1 0) (gMul s (det2 (U 0 1) (U 0 2) (U 2 1) (U 2 2)))))
            (gNeg (gMul (U 1 2) (gMul s (det2 (U 0 0) (U 0 1) (U 2 0) (U 2 1)))))
      = gAdd (gSub (gMul s (gMul (U 1 1) (det2 (U 0 0) (U 0 2) (U 2 0) (U 2 2))))
                   (gMul s (gMul (U 1 0) (det2 (U 0 1) (U 0 2) (U 2 1) (U 2 2)))))
             (gMul s (gNeg (gMul (U 1 2) (det2 (U 0 0) (U 0 1) (U 2 0) (U 2 1))))) := by
          rw [← gMul_assoc (U 1 1) s, gMul_comm (U 1 1) s, gMul_assoc s (U 1 1)]
          rw [← gMul_assoc (U 1 0) s, gMul_comm (U 1 0) s, gMul_assoc s (U 1 0)]
          rw [← gMul_assoc (U 1 2) s, gMul_comm (U 1 2) s, gMul_assoc s (U 1 2), gMul_neg_right]
    _ = gMul s (gAdd (gSub (gMul (U 1 1) (det2 (U 0 0) (U 0 2) (U 2 0) (U 2 2)))
                           (gMul (U 1 0) (det2 (U 0 1) (U 0 2) (U 2 1) (U 2 2))))
                     (gNeg (gMul (U 1 2) (det2 (U 0 0) (U 0 1) (U 2 0) (U 2 1))))) := by
          rw [← gMul_sub, ← gMul_add]

private theorem det_scale_pair_12 (α β : GInt) (U : Mat 3) :
    su3Det (fun i j =>
      match i.val with
      | 0 => U 0 j
      | 1 => gAdd (gMul α (U 1 j)) (gMul β (U 2 j))
      | _ => gAdd (gMul (gNeg (gConj β)) (U 1 j)) (gMul (gConj α) (U 2 j))) =
      gMul ⟨gNormSq α + gNormSq β, 0⟩ (su3Det U) := by
  unfold su3Det
  dsimp only
  change gAdd (gSub (gMul (U 0 0) (det2 (gAdd (gMul α (U 1 1)) (gMul β (U 2 1)))
                                        (gAdd (gMul α (U 1 2)) (gMul β (U 2 2)))
                                        (gAdd (gMul (gNeg (gConj β)) (U 1 1)) (gMul (gConj α) (U 2 1)))
                                        (gAdd (gMul (gNeg (gConj β)) (U 1 2)) (gMul (gConj α) (U 2 2)))))
                    (gMul (U 0 1) (det2 (gAdd (gMul α (U 1 0)) (gMul β (U 2 0)))
                                        (gAdd (gMul α (U 1 2)) (gMul β (U 2 2)))
                                        (gAdd (gMul (gNeg (gConj β)) (U 1 0)) (gMul (gConj α) (U 2 0)))
                                        (gAdd (gMul (gNeg (gConj β)) (U 1 2)) (gMul (gConj α) (U 2 2))))))
              (gMul (U 0 2) (det2 (gAdd (gMul α (U 1 0)) (gMul β (U 2 0)))
                                  (gAdd (gMul α (U 1 1)) (gMul β (U 2 1)))
                                  (gAdd (gMul (gNeg (gConj β)) (U 1 0)) (gMul (gConj α) (U 2 0)))
                                  (gAdd (gMul (gNeg (gConj β)) (U 1 1)) (gMul (gConj α) (U 2 1))))) =
    gMul ⟨gNormSq α + gNormSq β, 0⟩
      (gAdd (gSub (gMul (U 0 0) (det2 (U 1 1) (U 1 2) (U 2 1) (U 2 2)))
                  (gMul (U 0 1) (det2 (U 1 0) (U 1 2) (U 2 0) (U 2 2))))
            (gMul (U 0 2) (det2 (U 1 0) (U 1 1) (U 2 0) (U 2 1))))
  simp only [det2_su2_comb]
  let s : GInt := ⟨gNormSq α + gNormSq β, 0⟩
  calc gAdd (gSub (gMul (U 0 0) (gMul s (det2 (U 1 1) (U 1 2) (U 2 1) (U 2 2))))
                  (gMul (U 0 1) (gMul s (det2 (U 1 0) (U 1 2) (U 2 0) (U 2 2)))))
            (gMul (U 0 2) (gMul s (det2 (U 1 0) (U 1 1) (U 2 0) (U 2 1))))
      = gAdd (gSub (gMul s (gMul (U 0 0) (det2 (U 1 1) (U 1 2) (U 2 1) (U 2 2))))
                   (gMul s (gMul (U 0 1) (det2 (U 1 0) (U 1 2) (U 2 0) (U 2 2)))))
             (gMul s (gMul (U 0 2) (det2 (U 1 0) (U 1 1) (U 2 0) (U 2 1)))) := by
          rw [← gMul_assoc (U 0 0) s, gMul_comm (U 0 0) s, gMul_assoc s (U 0 0)]
          rw [← gMul_assoc (U 0 1) s, gMul_comm (U 0 1) s, gMul_assoc s (U 0 1)]
          rw [← gMul_assoc (U 0 2) s, gMul_comm (U 0 2) s, gMul_assoc s (U 0 2)]
    _ = gMul s (gAdd (gSub (gMul (U 0 0) (det2 (U 1 1) (U 1 2) (U 2 1) (U 2 2)))
                           (gMul (U 0 1) (det2 (U 1 0) (U 1 2) (U 2 0) (U 2 2))))
                     (gMul (U 0 2) (det2 (U 1 0) (U 1 1) (U 2 0) (U 2 1)))) := by
          rw [← gMul_sub, ← gMul_add]

/-- Theorem C2E (SU(3) Cabibbo-Marinari Determinant Multiplicativity): Left-multiplying any
    3x3 matrix `U` by any of the three SU(2) subgroups `(0,1)`, `(0,2)`, `(1,2)` multiplies `det(U)`
    by `|α|^2 + |β|^2`, preserving `det(U) = 1` identically across all sweeps. -/
theorem su3Det_mul_su2Embed (α β : GInt) (U : Mat 3) :
    su3Det (matMul (su2Embed 3 0 1 (by decide) α β) U) =
      gMul ⟨gNormSq α + gNormSq β, 0⟩ (su3Det U) ∧
    su3Det (matMul (su2Embed 3 0 2 (by decide) α β) U) =
      gMul ⟨gNormSq α + gNormSq β, 0⟩ (su3Det U) ∧
    su3Det (matMul (su2Embed 3 1 2 (by decide) α β) U) =
      gMul ⟨gNormSq α + gNormSq β, 0⟩ (su3Det U) := by
  refine ⟨?_, ?_, ?_⟩
  · rw [su3_embed_01_eq, det_scale_pair_01]
  · rw [su3_embed_02_eq, det_scale_pair_02]
  · rw [su3_embed_12_eq, det_scale_pair_12]

/-! ## Section 3: Eisenstein Ring Z[omega] & Infinite-Z^4 Z_3 Center Symmetry (Theorem C3) -/

structure EInt where
  a : Int
  b : Int
  deriving DecidableEq, Repr

def eZero : EInt   := ⟨0, 0⟩
def eOne : EInt    := ⟨1, 0⟩
def eOmega : EInt  := ⟨0, 1⟩
def eOmega2 : EInt := ⟨-1, -1⟩

def eAdd (x y : EInt) : EInt := ⟨x.a + y.a, x.b + y.b⟩
def eSub (x y : EInt) : EInt := ⟨x.a - y.a, x.b - y.b⟩
def eNeg (x : EInt) : EInt   := ⟨-x.a, -x.b⟩
def eMul (x y : EInt) : EInt := ⟨x.a * y.a - x.b * y.b, x.a * y.b + x.b * y.a - x.b * y.b⟩
def eConj (x : EInt) : EInt  := ⟨x.a - x.b, -x.b⟩
def eNormSq (x : EInt) : Int := x.a * x.a - x.a * x.b + x.b * x.b

theorem eOmega_cube : eMul (eMul eOmega eOmega) eOmega = eOne := by
  decide

theorem eOmega_sq_eq : eMul eOmega eOmega = eOmega2 := by
  decide

theorem eOmega_sum_zero : eAdd (eAdd eOne eOmega) eOmega2 = eZero := by
  decide

theorem eOmega_unit : eMul eOmega (eConj eOmega) = eOne := by
  decide

theorem eOmega2_unit : eMul eOmega2 (eConj eOmega2) = eOne := by
  decide

theorem eMul_comm (x y : EInt) : eMul x y = eMul y x := by
  cases x; cases y; unfold eMul; simp only [EInt.mk.injEq]; grind

theorem eMul_assoc (x y z : EInt) : eMul (eMul x y) z = eMul x (eMul y z) := by
  cases x; cases y; cases z; unfold eMul; simp only [EInt.mk.injEq]; grind

theorem eOne_mul (x : EInt) : eMul eOne x = x := by
  cases x; unfold eOne eMul; simp only [EInt.mk.injEq]; grind

theorem eMul_one (x : EInt) : eMul x eOne = x := by
  cases x; unfold eOne eMul; simp only [EInt.mk.injEq]; grind

theorem eZero_add (x : EInt) : eAdd eZero x = x := by
  cases x; unfold eZero eAdd; simp

theorem eAdd_zero (x : EInt) : eAdd x eZero = x := by
  cases x; unfold eZero eAdd; simp

theorem eMul_zero (x : EInt) : eMul x eZero = eZero := by
  cases x; unfold eZero eMul; simp only [EInt.mk.injEq]; grind

theorem eZero_mul (x : EInt) : eMul eZero x = eZero := by
  cases x; unfold eZero eMul; simp only [EInt.mk.injEq]; grind

theorem eMul_add (x y z : EInt) : eMul x (eAdd y z) = eAdd (eMul x y) (eMul x z) := by
  cases x; cases y; cases z; unfold eMul eAdd; simp only [EInt.mk.injEq]; grind

theorem eAdd_mul (x y z : EInt) : eMul (eAdd x y) z = eAdd (eMul x z) (eMul y z) := by
  cases x; cases y; cases z; unfold eMul eAdd; simp only [EInt.mk.injEq]; grind

theorem eConj_conj (x : EInt) : eConj (eConj x) = x := by
  cases x; unfold eConj; simp only [EInt.mk.injEq]; omega

theorem eConj_add (x y : EInt) : eConj (eAdd x y) = eAdd (eConj x) (eConj y) := by
  cases x; cases y; unfold eConj eAdd; simp only [EInt.mk.injEq]; omega

theorem eConj_mul (x y : EInt) : eConj (eMul x y) = eMul (eConj x) (eConj y) := by
  cases x; cases y; unfold eConj eMul; simp only [EInt.mk.injEq]; grind

theorem eMul_conj_self (x : EInt) : eMul x (eConj x) = ⟨eNormSq x, 0⟩ := by
  cases x; unfold eMul eConj eNormSq; simp only [EInt.mk.injEq]; grind

theorem eNormSq_nonneg (x : EInt) : 0 ≤ eNormSq x := by
  unfold eNormSq
  have h1 : 0 ≤ (x.a - x.b) * (x.a - x.b) := by
    by_cases h : 0 ≤ x.a - x.b
    · exact Int.mul_nonneg h h
    · have hm : 0 ≤ -(x.a - x.b) := by omega
      have hsq : (-(x.a - x.b)) * (-(x.a - x.b)) = (x.a - x.b) * (x.a - x.b) := Int.neg_mul_neg _ _
      rw [← hsq]; exact Int.mul_nonneg hm hm
  have h2 : 0 ≤ x.a * x.a := by
    by_cases h : 0 ≤ x.a
    · exact Int.mul_nonneg h h
    · have hm : 0 ≤ -x.a := by omega
      have hsq : (-x.a) * (-x.a) = x.a * x.a := Int.neg_mul_neg _ _
      rw [← hsq]; exact Int.mul_nonneg hm hm
  have h3 : 0 ≤ x.b * x.b := by
    by_cases h : 0 ≤ x.b
    · exact Int.mul_nonneg h h
    · have hm : 0 ≤ -x.b := by omega
      have hsq : (-x.b) * (-x.b) = x.b * x.b := Int.neg_mul_neg _ _
      rw [← hsq]; exact Int.mul_nonneg hm hm
  have hid : 2 * (x.a * x.a - x.a * x.b + x.b * x.b) =
      x.a * x.a + x.b * x.b + (x.a - x.b) * (x.a - x.b) := by
    grind
  omega

def eSumPrefix {N : Nat} (f : Fin N → EInt) : Nat → EInt
  | 0 => eZero
  | k + 1 => eAdd (eSumPrefix f k) (if h : k < N then f ⟨k, h⟩ else eZero)

def eSum (N : Nat) (f : Fin N → EInt) : EInt :=
  eSumPrefix f N

private theorem eSumPrefix_mul_left {N : Nat} (c : EInt) (f : Fin N → EInt) (k : Nat) :
    eSumPrefix (fun i => eMul c (f i)) k = eMul c (eSumPrefix f k) := by
  induction k with
  | zero => simp [eSumPrefix, eMul_zero]
  | succ k ih =>
    simp only [eSumPrefix, ih]
    split
    · rw [eMul_add]
    · simp only [eAdd_zero]

theorem eSum_mul_left (N : Nat) (c : EInt) (f : Fin N → EInt) :
    eSum N (fun i => eMul c (f i)) = eMul c (eSum N f) :=
  eSumPrefix_mul_left c f N

private theorem eSumPrefix_mul_right {N : Nat} (f : Fin N → EInt) (c : EInt) (k : Nat) :
    eSumPrefix (fun i => eMul (f i) c) k = eMul (eSumPrefix f k) c := by
  induction k with
  | zero => simp [eSumPrefix, eZero_mul]
  | succ k ih =>
    simp only [eSumPrefix, ih]
    split
    · rw [eAdd_mul]
    · simp only [eAdd_zero]

theorem eSum_mul_right (N : Nat) (f : Fin N → EInt) (c : EInt) :
    eSum N (fun i => eMul (f i) c) = eMul (eSum N f) c :=
  eSumPrefix_mul_right f c N

abbrev EMat (N : Nat) : Type := Fin N → Fin N → EInt
abbrev EGaugeField4D (N : Nat) : Type := Site4D → Fin 4 → EMat N

def eMatId (N : Nat) : EMat N := fun i j => if i = j then eOne else eZero
def eMatMul {N : Nat} (A B : EMat N) : EMat N := fun i j => eSum N (fun k => eMul (A i k) (B k j))
def eMatDag {N : Nat} (A : EMat N) : EMat N := fun i j => eConj (A j i)
def eMatScale {N : Nat} (z : EInt) (A : EMat N) : EMat N := fun i j => eMul z (A i j)
def eMatTr {N : Nat} (A : EMat N) : EInt := eSum N (fun i => A i i)

private theorem eMatScale_one {N : Nat} (A : EMat N) : eMatScale eOne A = A := by
  funext i j; exact eOne_mul (A i j)

private theorem eMatScale_scale {N : Nat} (z1 z2 : EInt) (A : EMat N) :
    eMatScale z1 (eMatScale z2 A) = eMatScale (eMul z1 z2) A := by
  funext i j; exact (eMul_assoc z1 z2 (A i j)).symm

private theorem eMatMul_scale_left {N : Nat} (z : EInt) (A B : EMat N) :
    eMatMul (eMatScale z A) B = eMatScale z (eMatMul A B) := by
  funext i j
  unfold eMatMul eMatScale
  rw [← eSum_mul_left]
  congr 1; funext k; exact eMul_assoc z (A i k) (B k j)

private theorem eMatMul_scale_right {N : Nat} (z : EInt) (A B : EMat N) :
    eMatMul A (eMatScale z B) = eMatScale z (eMatMul A B) := by
  funext i j
  unfold eMatMul eMatScale
  rw [← eSum_mul_left]
  congr 1; funext k
  calc eMul (A i k) (eMul z (B k j))
      = eMul (eMul (A i k) z) (B k j) := by rw [← eMul_assoc]
    _ = eMul (eMul z (A i k)) (B k j) := by rw [eMul_comm (A i k) z]
    _ = eMul z (eMul (A i k) (B k j)) := by rw [eMul_assoc]

private theorem eMatDag_scale {N : Nat} (z : EInt) (A : EMat N) :
    eMatDag (eMatScale z A) = eMatScale (eConj z) (eMatDag A) := by
  funext i j
  unfold eMatDag eMatScale
  exact eConj_mul z (A j i)

theorem eMatTr_scale {N : Nat} (z : EInt) (A : EMat N) :
    eMatTr (eMatScale z A) = eMul z (eMatTr A) := by
  unfold eMatTr eMatScale
  exact eSum_mul_left N z (fun i => A i i)

def ePlaqHolonomy {N : Nat} (u1 u2 u3 u4 : EMat N) : EMat N :=
  eMatMul (eMatMul u1 u2) (eMatMul (eMatDag u3) (eMatDag u4))

def ePlaq4D {N : Nat} (U : EGaugeField4D N) (p : Site4D) (μ ν : Fin 4) : EMat N :=
  ePlaqHolonomy (U p μ) (U (shift4D p μ) ν) (U (shift4D p ν) μ) (U p ν)

def eCenterTwistSlice {N : Nat} (t0 : Int) (z : EInt) (U : EGaugeField4D N) : EGaugeField4D N :=
  fun p μ =>
    let (t, _, _, _) := p
    if μ.val = 0 ∧ t = t0 then eMatScale z (U p μ) else U p μ

def ePolyakovProd {N : Nat} : (nt : Nat) → (Nat → EMat N) → EMat N
  | 0, _     => eMatId N
  | n + 1, u => eMatMul (ePolyakovProd n u) (u n)

/-- Theorem C3A (Infinite-Z^4 Exact Z_3 Center Symmetry of Wilson Plaquette):
    Multiplying all temporal links at time-slice `t = t0` by any unit Eisenstein phase `z`
    (`z * conj(z) = 1`, e.g. `eOmega`, `eOmega2`, or `eOne`) leaves EVERY plaquette on
    infinite Z^4 identically invariant (`dS = 0`). -/
theorem z3_centerTwist_preserves_plaq4D {N : Nat} (t0 : Int) (z : EInt) (hz : eMul z (eConj z) = eOne)
    (U : EGaugeField4D N) (p : Site4D) (μ ν : Fin 4) (hmn : μ ≠ ν) :
    ePlaq4D (eCenterTwistSlice t0 z U) p μ ν = ePlaq4D U p μ ν := by
  rcases p with ⟨t, x, y, zc⟩
  rcases μ with ⟨m, hm⟩
  rcases ν with ⟨n, hn⟩
  have hmnv : m ≠ n := fun h => hmn (Fin.ext h)
  have hm4 : m = 0 ∨ m = 1 ∨ m = 2 ∨ m = 3 := by omega
  have hn4 : n = 0 ∨ n = 1 ∨ n = 2 ∨ n = 3 := by omega
  have hzc : eMul (eConj z) z = eOne := by rw [eMul_comm]; exact hz
  unfold ePlaq4D ePlaqHolonomy eCenterTwistSlice
  rcases hm4 with rfl | rfl | rfl | rfl <;>
    rcases hn4 with rfl | rfl | rfl | rfl
  all_goals
    first
    | exact absurd rfl hmnv
    | (simp only [shift4D]
       by_cases ht : t = t0
       · subst ht
         simp [eMatMul_scale_left, eMatMul_scale_right, eMatDag_scale,
               eMatScale_scale, hzc, eMatScale_one]
       · simp [ht])

private theorem ePolyakovProd_agree_lt {N : Nat} (n t0 : Nat) (hn : n ≤ t0) (z : EInt)
    (u : Nat → EMat N) :
    ePolyakovProd n (fun t => if t = t0 then eMatScale z (u t) else u t) = ePolyakovProd n u := by
  induction n with
  | zero => rfl
  | succ k ih =>
    unfold ePolyakovProd
    have hkt0 : k ≠ t0 := by omega
    have hkLe : k ≤ t0 := by omega
    rw [ih hkLe]
    simp [hkt0]

/-- Theorem C3B (Z_3 Center Rotation of Arbitrary-N_t Polyakov Loops):
    Twisting any single time-slice `t0 < nt` by `z` multiplies the ordered temporal Wilson line
    by `z` and rotates its trace `Tr(L) -> z * Tr(L)` identically for all `N` and `nt`. -/
theorem z3_centerTwist_rotates_polyakov {N : Nat} (nt t0 : Nat) (ht0 : t0 < nt) (z : EInt)
    (u : Nat → EMat N) :
    let u' : Nat → EMat N := fun t => if t = t0 then eMatScale z (u t) else u t
    ePolyakovProd nt u' = eMatScale z (ePolyakovProd nt u) ∧
    eMatTr (ePolyakovProd nt u') = eMul z (eMatTr (ePolyakovProd nt u)) := by
  intro u'
  have hprod : ePolyakovProd nt u' = eMatScale z (ePolyakovProd nt u) := by
    induction nt with
    | zero => omega
    | succ k ih =>
      unfold ePolyakovProd
      by_cases hk : k = t0
      · subst hk
        have hprefix := ePolyakovProd_agree_lt k k (Nat.le_refl k) z u
        change eMatMul (ePolyakovProd k u') (if k = k then eMatScale z (u k) else u k) =
          eMatScale z (eMatMul (ePolyakovProd k u) (u k))
        rw [ite_eq_left rfl, hprefix, eMatMul_scale_right]
      · have ht0Lt : t0 < k := by omega
        have hkNe : u' k = u k := by simp [u', hk]
        rw [ih ht0Lt, hkNe, eMatMul_scale_left]
  exact ⟨hprod, by rw [hprod, eMatTr_scale]⟩

/-- Theorem C3C (Exact Vanishing of Unbroken Z_3 Confined Order Parameter):
    Summing a Polyakov trace across the three Z_3 center images `{1, omega, omega^2}` cancels
    identically to zero for every `P : EInt`, enforcing `<L> = 0` in the confined phase. -/
theorem z3_symmetric_polyakov_vanishes (P : EInt) :
    eAdd (eAdd P (eMul eOmega P)) (eMul eOmega2 P) = eZero := by
  cases P
  unfold eAdd eMul eOmega eOmega2 eZero
  simp only [EInt.mk.injEq]
  grind

/-! ## Section 4: Adjoint Representation, N-ality Zero Screening & Casimir Scaling (Theorem C4) -/

def adjChar (P : EInt) : EInt :=
  eSub (eMul P (eConj P)) eOne

def ePow (z : EInt) : Nat → EInt
  | 0 => eOne
  | n + 1 => eMul (ePow z n) z

/-- Theorem C4A (Adjoint Representation Z_3 Center Invariance):
    Since `Tr_8(U) = |Tr_3(U)|^2 - 1`, multiplying the fundamental loop by any Z_3 center element
    `z` (`z * conj(z) = 1`) leaves the adjoint Polyakov loop and Wilson loop identically invariant. -/
theorem z3_adjoint_char_invariant (z P : EInt) (hz : eMul z (eConj z) = eOne) :
    adjChar (eMul z P) = adjChar P := by
  unfold adjChar
  rw [eConj_mul]
  calc eSub (eMul (eMul z P) (eMul (eConj z) (eConj P))) eOne
      = eSub (eMul (eMul z (eConj z)) (eMul P (eConj P))) eOne := by
          congr 1
          rw [eMul_assoc z P, ← eMul_assoc P (eConj z) (eConj P), eMul_comm P (eConj z),
              eMul_assoc (eConj z) P (eConj P), ← eMul_assoc z (eConj z)]
    _ = eSub (eMul P (eConj P)) eOne := by rw [hz, eOne_mul]

/-- Theorem C4B (Triality-Zero Baryon Invariance vs Fundamental Non-Invariance):
    Any Z_3-charged fundamental probe transforms nontrivially (`eOmega != eOne`, `eOmega2 != eOne`),
    while every triality-0 (`k = 3m` quark baryon) sector satisfies `omega^(3m) = 1` identically. -/
theorem z3_triality_zero_invariant (m : Nat) :
    ePow eOmega (3 * m) = eOne ∧ eOmega ≠ eOne ∧ eOmega2 ≠ eOne := by
  refine ⟨?_, by decide, by decide⟩
  induction m with
  | zero => rfl
  | succ m ih =>
    have hstep : 3 * (m + 1) = ((3 * m + 1) + 1) + 1 := by omega
    rw [hstep]
    simp only [ePow]
    rw [eMul_assoc, eMul_assoc, ← eMul_assoc eOmega eOmega eOmega, eOmega_cube, eMul_one, ih]

def casimirFundNum (N : Nat) : Int :=
  (N : Int) * (N : Int) - 1

def casimirAdjNum (N : Nat) : Int :=
  2 * (N : Int) * (N : Int)

theorem suN_casimir_identity (N : Nat) :
    casimirAdjNum N - 2 * casimirFundNum N = 2 := by
  unfold casimirAdjNum casimirFundNum
  grind

/-- Theorem C4C (Exact SU(3) Adjoint-to-Fundamental Casimir Ratio 9/4):
    Since `2 N C2(3) = N^2 - 1 = 8` and `2 N C2(8) = 2 N^2 = 18`, the exact Casimir scaling
    ratio in the unbroken string regime is `C2(8) / C2(3) = 9 / 4`. -/
theorem su3_casimir_ratio_nine_fourths :
    4 * casimirAdjNum 3 = 9 * casimirFundNum 3 ∧
    casimirFundNum 3 = 8 ∧
    casimirAdjNum 3 = 18 := by
  unfold casimirAdjNum casimirFundNum
  decide

def posRatPow (r : PosRat) : Nat → PosRat
  | 0 => PosRat.one
  | n + 1 => PosRat.mul (posRatPow r n) r

private theorem posRat_le_trans {a b c : PosRat}
    (hab : a.num * b.den ≤ b.num * a.den)
    (hbc : b.num * c.den ≤ c.num * b.den) :
    a.num * c.den ≤ c.num * a.den := by
  have hc : 0 ≤ c.den := Int.le_of_lt c.den_pos
  have ha : 0 ≤ a.den := Int.le_of_lt a.den_pos
  have h1 : (a.num * c.den) * b.den ≤ (b.num * c.den) * a.den := by
    have hm := Int.mul_le_mul_of_nonneg_right hab hc
    have hl : (a.num * b.den) * c.den = (a.num * c.den) * b.den := by ac_rfl
    have hr : (b.num * a.den) * c.den = (b.num * c.den) * a.den := by ac_rfl
    omega
  have h2 : (b.num * c.den) * a.den ≤ (c.num * a.den) * b.den := by
    have hm := Int.mul_le_mul_of_nonneg_right hbc ha
    have hr : (c.num * b.den) * a.den = (c.num * a.den) * b.den := by ac_rfl
    omega
  exact Int.le_of_mul_le_mul_right (Int.le_trans h1 h2) b.den_pos

private theorem posRatPow_succ_le (r : PosRat) (hr : r.num ≤ r.den) (n : Nat) :
    (posRatPow r (n + 1)).num * (posRatPow r n).den ≤
      (posRatPow r n).num * (posRatPow r (n + 1)).den := by
  change ((posRatPow r n).num * r.num) * (posRatPow r n).den ≤
    (posRatPow r n).num * ((posRatPow r n).den * r.den)
  have hn : 0 ≤ (posRatPow r n).num := Int.le_of_lt (posRatPow r n).num_pos
  have hd : 0 ≤ (posRatPow r n).den := Int.le_of_lt (posRatPow r n).den_pos
  have h1 : (posRatPow r n).num * r.num ≤ (posRatPow r n).num * r.den :=
    Int.mul_le_mul_of_nonneg_left hr hn
  have h2 := Int.mul_le_mul_of_nonneg_right h1 hd
  have hr2 : (posRatPow r n).num * r.den * (posRatPow r n).den =
      (posRatPow r n).num * ((posRatPow r n).den * r.den) := by ac_rfl
  omega

theorem posRatPow_le_of_le (r : PosRat) (hr : r.num ≤ r.den) {a b : Nat} (hab : a ≤ b) :
    (posRatPow r b).num * (posRatPow r a).den ≤ (posRatPow r a).num * (posRatPow r b).den := by
  obtain ⟨d, rfl⟩ := Nat.exists_eq_add_of_le hab
  induction d with
  | zero => exact Int.le_refl _
  | succ d ih =>
    have hstep : a + (d + 1) = (a + d) + 1 := by omega
    rw [hstep]
    exact posRat_le_trans (posRatPow_succ_le r hr (a + d)) (ih (by omega))

/-- Theorem C4D (Casimir-Scaled Adjoint Area-Law Dominance `W_8 <= W_3` via `9/4`):
    Because `C2(8)/C2(3) = 9/4` (`4 * A <= 9 * A`), any fundamental area-law contraction
    factor `r <= 1` decays strictly faster in the adjoint representation: `r^(9A) <= r^(4A)`. -/
theorem adjoint_casimir_faster_than_fund (r : PosRat) (hr : r.num ≤ r.den) (A : Nat) :
    (posRatPow r (9 * A)).num * (posRatPow r (4 * A)).den ≤
      (posRatPow r (4 * A)).num * (posRatPow r (9 * A)).den :=
  posRatPow_le_of_le r hr (by omega)

/-- Theorem C4E (Persistent Gluonic String Breaking for N-ality Zero Loops):
    Once Casimir-scaled adjoint area-law decay drops below the gluonic perimeter-screened
    bound `floor` at area `A0`, it stays dominated by `floor` for all larger areas `A >= A0`. -/
theorem adjoint_string_breaking_persistent (r floor : PosRat) (hr : r.num ≤ r.den)
    (A0 A : Nat) (hA : A0 ≤ A)
    (hcross : (posRatPow r (9 * A0)).num * floor.den ≤ floor.num * (posRatPow r (9 * A0)).den) :
    (posRatPow r (9 * A)).num * floor.den ≤ floor.num * (posRatPow r (9 * A)).den :=
  posRat_le_trans (posRatPow_le_of_le r hr (by omega : 9 * A0 ≤ 9 * A)) hcross

/-! ## Section 5: 4D SU(3) Defect-Wall `@swap` Decomposition & Detailed Balance (Theorem C5) -/

/-- Theorem C5A (4D SU(3) Defect-Wall `@swap` Difference):
    Exchanging replica wall couplings `(c0, c1)` depends exclusively on the localized 3D
    defect-wall plaquette sums `(s1, s2)` while canceling arbitrary bulk action identically. -/
theorem su3_defect_wall_swap_delta (sBulkA s1A s2A sBulkB s1B s2B c0 c1 : Int) :
    (defectWallAction sBulkA s1A s2A c1 + defectWallAction sBulkB s1B s2B c0) -
    (defectWallAction sBulkA s1A s2A c0 + defectWallAction sBulkB s1B s2B c1) =
      (c0 - c1) * ((s1B - s1A) + (c0 + c1) * (s2B - s2A)) :=
  defect_wall_swap_delta sBulkA s1A s2A sBulkB s1B s2B c0 c1

/-- Theorem C5B (4D SU(3) Defect-Wall Multi-Replica `@swap` Detailed Balance):
    Adjacent replica exchange on the SU(3) defect wall obeys exact Metropolis flux symmetry
    over `PosRat` (`0` equilibrium sampling bias). -/
theorem su3_replica_swap_detailed_balance (π0A π0B π1A π1B : PosRat) :
    let wAB := PosRat.mul π0A π1B
    let wBA := PosRat.mul π0B π1A
    PosRat.equiv
      (PosRat.mul wAB (PosRat.minOne (PosRat.div wBA wAB)))
      (PosRat.mul wBA (PosRat.minOne (PosRat.div wAB wBA))) :=
  replica_swap_detailed_balance_exact π0A π0B π1A π1B

/-- Theorem C5C (Defect-Wall Z_3 Center Preservation):
    Weighted defect-wall plaquette traces over `Z[omega]` preserve full complex Z_3 center
    invariance in both Eisenstein components (`a` and `b`) plaquette-by-plaquette. -/
theorem su3_defect_wall_preserves_z3 {N : Nat} (t0 : Int) (z : EInt) (hz : eMul z (eConj z) = eOne)
    (U : EGaugeField4D N) (w : Site4D → Fin 4 → Fin 4 → Int) (p : Site4D) (μ ν : Fin 4)
    (hmn : μ ≠ ν) :
    eMul ⟨w p μ ν, 0⟩ (eMatTr (ePlaq4D (eCenterTwistSlice t0 z U) p μ ν)) =
      eMul ⟨w p μ ν, 0⟩ (eMatTr (ePlaq4D U p μ ν)) := by
  rw [z3_centerTwist_preserves_plaq4D t0 z hz U p μ ν hmn]

end Flow.ColorGaugeBundle
