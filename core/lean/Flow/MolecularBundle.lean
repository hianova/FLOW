/-
  Flow.MolecularBundle — Ab Initio Electronic Structure, Certified Brackets, 2-RDM Cones & SCF Algebra
  ====================================================================================================
  Pure Lean 4 (Init-only, zero IO) formalization extending the fermionic and unified gauge stack to
  ab initio electronic structure theory and molecular potential-energy optimization (Theorems M1-M5):
    1. Second-quantized 1-body/2-body particle-number conservation, exact exchange antisymmetry, and
       minimal-basis H2 FCI cancellation of RHF ionic dissociation error (Theorem M1);
    2. Certified two-sided energy brackets: Rayleigh-Ritz upper bound, denominator-free Weinstein
       variance bound, Temple lower bound, and Interval1D SOS certification (Theorem M2);
    3. 2-RDM N-representability (D, Q, G Gram PSD cones, exact arbitrary-n CAR affine maps, pair
       trace contraction, and dual SDP certified lower bound) (Theorem M3);
    4. Hohenberg-Kohn density uniqueness, Levy-Lieb constrained search, exact Virial coordinate scaling,
       and ERI Cauchy-Schwarz screening (Theorem M4);
    5. Closed-shell RHF density projector idempotency, Roothaan-Hall [F, P] = 0 stationarity, and
       bilevel SEARU x 2-Jet molecular conformer global convergence (Theorem M5).
-/
import Flow.Bundle
import Flow.Interval
import Flow.ConstructiveQFT
import Flow.UnifiedBundle

namespace Flow.MolecularBundle

open Flow.GaugeBundle
open Flow.ProjectiveBundle
open Flow.YangMillsBundle
open Flow.ConstructiveQFT
open Flow.FermionBundle
open Flow.Interval
open Flow.Bundle

private theorem int_sq_nonneg (z : Int) : 0 ≤ z * z := by
  by_cases hz : 0 ≤ z
  · exact Int.mul_nonneg hz hz
  · have hneg : 0 ≤ -z := by omega
    have hsq : (-z) * (-z) = z * z := Int.neg_mul_neg z z
    rw [← hsq]
    exact Int.mul_nonneg hneg hneg

private theorem sumFin_congr {N : Nat} (f g : Fin N → Int) (h : ∀ i, f i = g i) :
    sumFin N f = sumFin N g := by
  have hfn : f = g := funext h
  rw [hfn]

private theorem toggleBit_at' {n : Nat} (s : FockState n) (k : Fin n) :
    toggleBit s k k = !s k := by
  simp [toggleBit]

private theorem toggleBit_ne' {n : Nat} (s : FockState n) {i k : Fin n} (hik : i ≠ k) :
    toggleBit s k i = s i := by
  simp [toggleBit, hik]

private theorem toggleBit_self' {n : Nat} (s : FockState n) (k : Fin n) :
    toggleBit (toggleBit s k) k = s := by
  funext i
  by_cases hi : i = k
  · simp only [toggleBit, hi, ↓reduceIte, Bool.not_not]
  · simp [toggleBit, hi]

private theorem jwParityPrefix_sq_one' {n : Nat} (s : FockState n) (m : Nat) :
    jwParityPrefix s m * jwParityPrefix s m = 1 := by
  induction m with
  | zero => simp [jwParityPrefix]
  | succ m ih =>
    unfold jwParityPrefix
    by_cases hm : m < n
    · cases hbm : s ⟨m, hm⟩
      · simp [hm, hbm]; exact ih
      · simp [hm, hbm]; grind
    · simp [hm]; exact ih

private theorem jwParityPrefix_toggle_ge' {n : Nat} (s : FockState n) (k : Fin n) (m : Nat)
    (hm : m ≤ k.val) :
    jwParityPrefix (toggleBit s k) m = jwParityPrefix s m := by
  induction m with
  | zero => simp [jwParityPrefix]
  | succ m ih =>
    have hm1 : m ≤ k.val := by omega
    have hne : ∀ h : m < n, (⟨m, h⟩ : Fin n) ≠ k := by
      intro _ heq
      have hval : m = k.val := congrArg Fin.val heq
      omega
    unfold jwParityPrefix
    by_cases hmn : m < n
    · simp [hmn, toggleBit_ne' s (hne hmn), ih hm1]
    · simp [hmn, ih hm1]

private theorem jwSign_sq_one' {n : Nat} (s : FockState n) (k : Fin n) :
    jwSign s k * jwSign s k = 1 :=
  jwParityPrefix_sq_one' s k.val

private theorem jwSign_toggle_self' {n : Nat} (s : FockState n) (k : Fin n) :
    jwSign (toggleBit s k) k = jwSign s k :=
  jwParityPrefix_toggle_ge' s k k.val (Nat.le_refl k.val)

/-! ## Section 1: Fermionic Conservation, Exchange Antisymmetry & H2 Dissociation (Theorem M1) -/

def occBit (b : Bool) : Int :=
  if b then 1 else 0

def electronCountPrefix {n : Nat} (s : FockState n) : Nat → Nat
  | 0 => 0
  | m + 1 =>
    electronCountPrefix s m + if h : m < n then (if s ⟨m, h⟩ then 1 else 0) else 0

def electronCount {n : Nat} (s : FockState n) : Nat :=
  electronCountPrefix s n

def spinZ2Prefix {n : Nat} (s : FockState n) : Nat → Int
  | 0 => 0
  | m + 1 =>
    spinZ2Prefix s m +
      if h : m < n then (if m % 2 = 0 then occBit (s ⟨m, h⟩) else -occBit (s ⟨m, h⟩)) else 0

def spinZ2 {n : Nat} (s : FockState n) : Int :=
  spinZ2Prefix s n

def oneBodyOp {n : Nat} (p q : Fin n) (ψ : FockVec n) : FockVec n :=
  cCreate p (cAnnih q ψ)

def twoBodyOp {n : Nat} (p q r u : Fin n) (ψ : FockVec n) : FockVec n :=
  cCreate p (cCreate q (cAnnih u (cAnnih r ψ)))

private theorem electronCountPrefix_toggle_ge {n : Nat} (s : FockState n) (k : Fin n) (m : Nat)
    (hm : m ≤ k.val) :
    electronCountPrefix (toggleBit s k) m = electronCountPrefix s m := by
  induction m with
  | zero => simp [electronCountPrefix]
  | succ m ih =>
    have hm1 : m ≤ k.val := by omega
    have hne : ∀ h : m < n, (⟨m, h⟩ : Fin n) ≠ k := by
      intro _ heq
      have hv : m = k.val := congrArg Fin.val heq
      omega
    unfold electronCountPrefix
    by_cases hmn : m < n
    · simp [hmn, toggleBit_ne' s (hne hmn), ih hm1]
    · simp [hmn, ih hm1]

private theorem electronCountPrefix_toggle_lt_false {n : Nat} (s : FockState n) (k : Fin n)
    (hsk : s k = false) (m : Nat) (hm : k.val < m) :
    electronCountPrefix (toggleBit s k) m = electronCountPrefix s m + 1 := by
  induction m with
  | zero => omega
  | succ m ih =>
    unfold electronCountPrefix
    by_cases hkm : k.val = m
    · have hklt : m < n := by have hkl := k.isLt; omega
      have hidx : (⟨m, hklt⟩ : Fin n) = k := Fin.ext hkm.symm
      have hpre := electronCountPrefix_toggle_ge s k m (by omega)
      simp only [hklt, ↓reduceDIte, hidx, toggleBit_at', hsk, hpre]
      simp
    · have hltm : k.val < m := by omega
      have hne : ∀ h : m < n, (⟨m, h⟩ : Fin n) ≠ k := by
        intro _ heq
        exact hkm (congrArg Fin.val heq).symm
      by_cases hmn : m < n
      · simp only [hmn, ↓reduceDIte, toggleBit_ne' s (hne hmn), ih hltm]
        cases s ⟨m, hmn⟩ <;> simp <;> omega
      · simp [hmn, ih hltm]

private theorem electronCountPrefix_toggle_lt_true {n : Nat} (s : FockState n) (k : Fin n)
    (hsk : s k = true) (m : Nat) (hm : k.val < m) :
    electronCountPrefix (toggleBit s k) m + 1 = electronCountPrefix s m := by
  induction m with
  | zero => omega
  | succ m ih =>
    unfold electronCountPrefix
    by_cases hkm : k.val = m
    · have hklt : m < n := by have hkl := k.isLt; omega
      have hidx : (⟨m, hklt⟩ : Fin n) = k := Fin.ext hkm.symm
      have hpre := electronCountPrefix_toggle_ge s k m (by omega)
      simp only [hklt, ↓reduceDIte, hidx, toggleBit_at', hsk, hpre]
      simp
    · have hltm : k.val < m := by omega
      have hne : ∀ h : m < n, (⟨m, h⟩ : Fin n) ≠ k := by
        intro _ heq
        exact hkm (congrArg Fin.val heq).symm
      have ih' := ih hltm
      by_cases hmn : m < n
      · simp only [hmn, ↓reduceDIte, toggleBit_ne' s (hne hmn)]
        cases s ⟨m, hmn⟩ <;> simp <;> omega
      · simp only [hmn, ↓reduceDIte]
        omega

private theorem electronCount_toggle_false {n : Nat} (s : FockState n) (k : Fin n)
    (hsk : s k = false) :
    electronCount (toggleBit s k) = electronCount s + 1 :=
  electronCountPrefix_toggle_lt_false s k hsk n k.isLt

private theorem electronCount_toggle_true {n : Nat} (s : FockState n) (k : Fin n)
    (hsk : s k = true) :
    electronCount (toggleBit s k) + 1 = electronCount s :=
  electronCountPrefix_toggle_lt_true s k hsk n k.isLt

private theorem spinZ2Prefix_toggle_ge {n : Nat} (s : FockState n) (k : Fin n) (m : Nat)
    (hm : m ≤ k.val) :
    spinZ2Prefix (toggleBit s k) m = spinZ2Prefix s m := by
  induction m with
  | zero => simp [spinZ2Prefix]
  | succ m ih =>
    have hm1 : m ≤ k.val := by omega
    have hne : ∀ h : m < n, (⟨m, h⟩ : Fin n) ≠ k := by
      intro _ heq
      have hv : m = k.val := congrArg Fin.val heq
      omega
    unfold spinZ2Prefix
    by_cases hmn : m < n
    · simp [hmn, toggleBit_ne' s (hne hmn), ih hm1]
    · simp [hmn, ih hm1]

private theorem spinZ2Prefix_toggle_lt {n : Nat} (s : FockState n) (k : Fin n)
    (m : Nat) (hm : k.val < m) :
    spinZ2Prefix (toggleBit s k) m =
      spinZ2Prefix s m +
        (if s k then -1 else 1) * (if k.val % 2 = 0 then 1 else -1) := by
  induction m with
  | zero => omega
  | succ m ih =>
    unfold spinZ2Prefix
    by_cases hkm : k.val = m
    · have hklt : m < n := by have hkl := k.isLt; omega
      have hidx : (⟨m, hklt⟩ : Fin n) = k := Fin.ext hkm.symm
      have hpre := spinZ2Prefix_toggle_ge s k m (by omega)
      simp only [hklt, ↓reduceDIte, hidx, toggleBit_at', hpre, hkm]
      cases s k <;> by_cases hp : m % 2 = 0 <;> simp [occBit, hp] <;> omega
    · have hltm : k.val < m := by omega
      have hne : ∀ h : m < n, (⟨m, h⟩ : Fin n) ≠ k := by
        intro _ heq
        exact hkm (congrArg Fin.val heq).symm
      by_cases hmn : m < n
      · simp only [hmn, ↓reduceDIte, toggleBit_ne' s (hne hmn), ih hltm]
        omega
      · simp only [hmn, ↓reduceDIte, ih hltm]
        omega

private theorem spinZ2_toggle {n : Nat} (s : FockState n) (k : Fin n) :
    spinZ2 (toggleBit s k) =
      spinZ2 s + (if s k then -1 else 1) * (if k.val % 2 = 0 then 1 else -1) :=
  spinZ2Prefix_toggle_lt s k n k.isLt

private theorem cAnnih_support {n N : Nat} (k : Fin n) (ψ : FockVec n)
    (hψ : ∀ t, electronCount t ≠ N → ψ t = 0) :
    ∀ s, electronCount s + 1 ≠ N → cAnnih k ψ s = 0 := by
  intro s hs
  unfold cAnnih
  cases hsk : s k
  · have hcnt := electronCount_toggle_false s k hsk
    have hzero := hψ (toggleBit s k) (by omega)
    simp [hzero]
  · simp

private theorem cCreate_support {n N : Nat} (k : Fin n) (ψ : FockVec n)
    (hψ : ∀ t, electronCount t + 1 ≠ N → ψ t = 0) :
    ∀ s, electronCount s ≠ N → cCreate k ψ s = 0 := by
  intro s hs
  unfold cCreate
  cases hsk : s k
  · simp
  · have hcnt := electronCount_toggle_true s k hsk
    have hzero := hψ (toggleBit s k) (by omega)
    simp [hzero]

private theorem cAnnih_support2 {n N : Nat} (r u : Fin n) (ψ : FockVec n)
    (hψ : ∀ t, electronCount t ≠ N → ψ t = 0) :
    ∀ s, electronCount s + 2 ≠ N → cAnnih u (cAnnih r ψ) s = 0 := by
  have h1 := cAnnih_support r ψ hψ
  intro s hs
  unfold cAnnih
  cases hsu : s u
  · have hcnt := electronCount_toggle_false s u hsu
    have hzero := h1 (toggleBit s u) (by omega)
    dsimp only [cAnnih] at hzero
    simp [hzero]
  · simp

private theorem cCreate_support2 {n N : Nat} (q : Fin n) (φ : FockVec n)
    (hφ : ∀ t, electronCount t + 2 ≠ N → φ t = 0) :
    ∀ s, electronCount s + 1 ≠ N → cCreate q φ s = 0 := by
  intro s hs
  unfold cCreate
  cases hsq : s q
  · simp
  · have hcnt := electronCount_toggle_true s q hsq
    have hzero := hφ (toggleBit s q) (by omega)
    simp [hzero]

/-- Theorem M1A (One-Body Electron-Number Conservation): Any 1-body excitation `c_p† c_q` maps
    N-electron Fock states strictly into N-electron Fock states for arbitrary `n` and `N`. -/
theorem oneBody_preserves_electron_count {n N : Nat} (p q : Fin n) (ψ : FockVec n)
    (hψ : ∀ t, electronCount t ≠ N → ψ t = 0) :
    ∀ s, electronCount s ≠ N → oneBodyOp p q ψ s = 0 :=
  cCreate_support p (cAnnih q ψ) (cAnnih_support q ψ hψ)

/-- Theorem M1A-Spin (Same-Spin One-Body `S_z` Conservation): When `p` and `q` share spin parity
    (`p % 2 = q % 2`), `oneBodyOp p q` strictly preserves `spinZ2 = Sz`. -/
theorem oneBody_same_spin_preserves_spinZ2 {n : Nat} (p q : Fin n) (Sz : Int) (ψ : FockVec n)
    (hpq : p.val % 2 = q.val % 2)
    (hψ : ∀ t, spinZ2 t ≠ Sz → ψ t = 0) :
    ∀ s, spinZ2 s ≠ Sz → oneBodyOp p q ψ s = 0 := by
  intro s hs
  unfold oneBodyOp cCreate cAnnih
  cases hsp : s p
  · simp
  · have h1 := spinZ2_toggle s p
    simp only [hsp, ↓reduceIte] at h1
    cases hsq : toggleBit s p q
    · have h2 := spinZ2_toggle (toggleBit s p) q
      simp only [hsq, Bool.false_eq_true, ↓reduceIte] at h2
      have hsame : spinZ2 (toggleBit (toggleBit s p) q) = spinZ2 s := by
        by_cases hpar : q.val % 2 = 0
        · have hparp : p.val % 2 = 0 := by omega
          simp [hpar, hparp] at h1 h2
          omega
        · have hparp : p.val % 2 ≠ 0 := by omega
          simp [hpar, hparp] at h1 h2
          omega
      have hzero := hψ (toggleBit (toggleBit s p) q) (by rw [hsame]; exact hs)
      simp [hzero]
    · simp

/-- Theorem M1B (Two-Body Electron-Number Conservation): Any 2-body Coulomb interaction term
    `c_p† c_q† c_u c_r` maps N-electron Fock states strictly into N-electron Fock states for
    arbitrary basis size `n` and arbitrary electron number `N`. -/
theorem twoBody_preserves_electron_count {n N : Nat} (p q r u : Fin n) (ψ : FockVec n)
    (hψ : ∀ t, electronCount t ≠ N → ψ t = 0) :
    ∀ s, electronCount s ≠ N → twoBodyOp p q r u ψ s = 0 :=
  cCreate_support p _ (cCreate_support2 q _ (cAnnih_support2 r u ψ hψ))

/-- Theorem M1C (Two-Body Fermionic Exchange Antisymmetry & Pauli Collisions): Swapping either pair
    of creation indices `(p, q)` or annihilation indices `(r, u)` negates the 2-body operator, and
    repeated indices (`p = q` or `r = u`) vanish identically. -/
theorem twoBody_exchange_antisymmetry {n : Nat} (p q r u : Fin n) (ψ : FockVec n) (s : FockState n) :
    twoBodyOp p q r u ψ s + twoBodyOp q p r u ψ s = 0 ∧
    twoBodyOp p q r u ψ s + twoBodyOp p q u r ψ s = 0 ∧
    twoBodyOp p p r u ψ s = 0 ∧
    twoBodyOp p q r r ψ s = 0 := by
  refine ⟨?_, ?_, ?_, ?_⟩
  · exact car_create_anticomm p q (cAnnih u (cAnnih r ψ)) s
  · unfold twoBodyOp
    have hc : ∀ st, cAnnih u (cAnnih r ψ) st = -cAnnih r (cAnnih u ψ) st := by
      intro st
      have ha := car_annih_anticomm u r ψ st
      omega
    have hfun : cAnnih u (cAnnih r ψ) = fun st => -cAnnih r (cAnnih u ψ) st := funext hc
    rw [hfun]
    have hlin : ∀ (k : Fin n) (φ : FockVec n),
        cCreate k (fun x => -φ x) = fun x => -cCreate k φ x := by
      intro k φ
      funext st
      unfold cCreate
      cases st k <;> simp [Int.mul_neg]
    rw [hlin q, hlin p]
    dsimp only
    omega
  · exact pauli_exclusion_create p (cAnnih u (cAnnih r ψ)) s
  · unfold twoBodyOp
    have hz : cAnnih r (cAnnih r ψ) = fun _ => 0 := by
      funext st
      exact pauli_exclusion_annih r ψ st
    have hcz : ∀ (k : Fin n), cCreate k (fun _ => 0) = fun _ => 0 := by
      intro k
      funext st
      unfold cCreate
      cases st k <;> simp
    rw [hz, hcz q, hcz p]

/-- Theorem M1D (Minimal-Basis H2 FCI Cancels RHF Ionic Dissociation Error):
    (a) On the 2-determinant singlet subspace `[[E_HF, K], [K, E_D]]`, whenever double-excitation
        coupling `K ≠ 0`, there strictly exists an integer CI state lowering energy below `E_HF`;
    (b) At the dissociation limit `E_D = E_HF` with `K > 0`, the balanced covalent state `(1, -1)` is
        an exact eigenvector with eigenvalue `E_HF - K < E_HF`, eliminating the spurious RHF ionic
        contribution `K = (J_AA - J_AB) / 2` identically. -/
theorem h2_fci_cancels_rhf_ionic_dissociation_error (E_HF E_D K : Int) :
    (K ≠ 0 → ∃ a b : Int, 0 < a * a + b * b ∧
      E_HF * a * a + 2 * K * a * b + E_D * b * b < E_HF * (a * a + b * b)) ∧
    ((E_D = E_HF ∧ 0 < K) →
      E_HF * 1 + K * (-1) = (E_HF - K) * 1 ∧
      K * 1 + E_D * (-1) = (E_HF - K) * (-1) ∧
      E_HF * 1 * 1 + 2 * K * 1 * (-1) + E_D * (-1) * (-1) = (E_HF - K) * (1 * 1 + (-1) * (-1)) ∧
      E_HF - K < E_HF) := by
  constructor
  · intro hK
    let a : Int := -(if E_D ≥ E_HF then E_D - E_HF else 0) - 1
    let b : Int := K
    refine ⟨a, b, ?_, ?_⟩
    · have ha := int_sq_nonneg a
      have hb : 0 < K * K := by
        by_cases hp : 0 < K
        · exact Int.mul_pos hp hp
        · have hn : 0 < -K := by omega
          have hsq : (-K) * (-K) = K * K := Int.neg_mul_neg K K
          rw [← hsq]
          exact Int.mul_pos hn hn
      dsimp only [b]
      omega
    · have hbpos : 0 < K * K := by
        by_cases hp : 0 < K
        · exact Int.mul_pos hp hp
        · have hn : 0 < -K := by omega
          have hsq : (-K) * (-K) = K * K := Int.neg_mul_neg K K
          rw [← hsq]
          exact Int.mul_pos hn hn
      have hlin : 2 * a + (E_D - E_HF) ≤ -2 := by
        dsimp only [a]
        by_cases hde : E_D ≥ E_HF
        · simp [hde]; omega
        · simp [hde]; omega
      have hprod : K * K * (2 * a + (E_D - E_HF)) < 0 := by
        have hlt : 2 * a + (E_D - E_HF) < 0 := by omega
        exact Int.mul_neg_of_pos_of_neg hbpos hlt
      have hexp :
          (E_HF * a * a + 2 * K * a * b + E_D * b * b) - E_HF * (a * a + b * b) =
            K * K * (2 * a + (E_D - E_HF)) := by
        dsimp only [b]
        grind
      omega
  · intro ⟨hED, hK⟩
    subst hED
    refine ⟨by omega, by omega, by grind, by omega⟩

/-! ## Section 2: Certified Two-Sided Energy Brackets (Theorem M2) -/

def specW0 (m : Nat) (w : Fin m → Int) : Int :=
  sumFin m w

def specW1 (m : Nat) (w lam : Fin m → Int) : Int :=
  sumFin m (fun i => w i * lam i)

def specW2 (m : Nat) (w lam : Fin m → Int) : Int :=
  sumFin m (fun i => w i * lam i * lam i)

/-- Theorem M2A (Rayleigh-Ritz Variational Upper Bound): For any nonnegative spectral weights `w`
    and ground-state lower bound `∀ i, lam0 ≤ lam i`, `<H> ≥ lam0` identically in denominator-free form. -/
theorem variational_rayleigh_ritz_upper {m : Nat} (w lam : Fin m → Int) (lam0 : Int)
    (hw : ∀ i, 0 ≤ w i) (hlam : ∀ i, lam0 ≤ lam i) :
    lam0 * specW0 m w ≤ specW1 m w lam := by
  have hdiff : 0 ≤ sumFin m (fun i => w i * (lam i - lam0)) :=
    sumFin_nonneg m _ fun i => Int.mul_nonneg (hw i) (by have := hlam i; omega)
  have hfn : (fun i => w i * (lam i - lam0)) = (fun i => w i * lam i - lam0 * w i) := by
    funext i
    grind
  rw [hfn, sumFin_sub, sumFin_mul_left] at hdiff
  unfold specW0 specW1
  omega

/-- Theorem M2B-Var (Exact Spectral Variance Non-Negativity): `S0 * S2 - S1^2 ≥ 0`. -/
theorem spec_variance_nonneg {m : Nat} (w lam : Fin m → Int) (hw : ∀ i, 0 ≤ w i) :
    0 ≤ specW0 m w * specW2 m w lam - specW1 m w lam * specW1 m w lam := by
  have hcs := weighted_cauchy_schwarz w lam (fun _ => (1 : Int)) hw
  have hxx : sumFin m (fun k => w k * (lam k * lam k)) = specW2 m w lam := by
    unfold specW2
    exact sumFin_congr _ _ fun k => by grind
  have hxy : sumFin m (fun k => w k * (lam k * 1)) = specW1 m w lam := by
    unfold specW1
    exact sumFin_congr _ _ fun k => by grind
  have hyy : sumFin m (fun k => w k * (1 * 1)) = specW0 m w := by
    unfold specW0
    exact sumFin_congr _ _ fun k => by grind
  rw [hxx, hxy, hyy, Int.mul_comm (specW2 m w lam) (specW0 m w)] at hcs
  omega

/-- Theorem M2B (Denominator-Free Weinstein Theorem): If every eigenvalue is at distance at least `|δ|`
    from a trial expectation `μ`, then `δ^2 * S0 ≤ Var_μ(H)`, certifying existence of an eigenvalue within
    `sqrt(Var_μ)` of `μ`. -/
theorem weinstein_variance_lower_bound {m : Nat} (w lam : Fin m → Int) (μ δ : Int)
    (hw : ∀ i, 0 ≤ w i)
    (hdist : ∀ i, δ * δ ≤ (lam i - μ) * (lam i - μ)) :
    δ * δ * specW0 m w ≤
      specW2 m w lam - 2 * μ * specW1 m w lam + μ * μ * specW0 m w := by
  have hle : sumFin m (fun i => w i * (δ * δ)) ≤
      sumFin m (fun i => w i * ((lam i - μ) * (lam i - μ))) := by
    have hdiff : 0 ≤ sumFin m (fun i => w i * ((lam i - μ) * (lam i - μ) - δ * δ)) :=
      sumFin_nonneg m _ fun i => Int.mul_nonneg (hw i) (by have := hdist i; omega)
    have hfn : (fun i => w i * ((lam i - μ) * (lam i - μ) - δ * δ)) =
        (fun i => w i * ((lam i - μ) * (lam i - μ)) - w i * (δ * δ)) := by
      funext i; grind
    rw [hfn, sumFin_sub] at hdiff
    omega
  have hlhs : sumFin m (fun i => w i * (δ * δ)) = δ * δ * specW0 m w := by
    unfold specW0
    rw [← sumFin_mul_left]
    exact sumFin_congr _ _ fun i => by grind
  have hrhs : sumFin m (fun i => w i * ((lam i - μ) * (lam i - μ))) =
      specW2 m w lam - 2 * μ * specW1 m w lam + μ * μ * specW0 m w := by
    have hfn : (fun i => w i * ((lam i - μ) * (lam i - μ))) =
        (fun i => w i * lam i * lam i - (2 * μ) * (w i * lam i) + (μ * μ) * w i) := by
      funext i; grind
    rw [hfn, sumFin_add, sumFin_sub, sumFin_mul_left, sumFin_mul_left]
    unfold specW0 specW1 specW2
    grind
  rw [hlhs, hrhs] at hle
  exact hle

/-- Theorem M2C (Denominator-Free Temple Certified Lower Bound): Whenever `∀ i, (lam_i - lam0)(lam_i - E1_lb) ≥ 0`
    (satisfied whenever `lam0` is the ground state and every excited eigenvalue lies above `E1_lb`), the exact
    product `(E1_lb * S0 - S1)(S1 - lam0 * S0)` is bounded above by the scaled variance `S0 * S2 - S1^2`. -/
theorem temple_certified_lower_bound {m : Nat} (w lam : Fin m → Int) (lam0 E1_lb : Int)
    (hw : ∀ i, 0 ≤ w i)
    (hspec : ∀ i, 0 ≤ (lam i - lam0) * (lam i - E1_lb)) :
    (E1_lb * specW0 m w - specW1 m w lam) * (specW1 m w lam - lam0 * specW0 m w) ≤
      specW0 m w * specW2 m w lam - specW1 m w lam * specW1 m w lam := by
  have hw0 : 0 ≤ specW0 m w := sumFin_nonneg m w hw
  have hpoly : 0 ≤ sumFin m (fun i => w i * ((lam i - lam0) * (lam i - E1_lb))) :=
    sumFin_nonneg m _ fun i => Int.mul_nonneg (hw i) (hspec i)
  have hexp : sumFin m (fun i => w i * ((lam i - lam0) * (lam i - E1_lb))) =
      specW2 m w lam - (lam0 + E1_lb) * specW1 m w lam + (lam0 * E1_lb) * specW0 m w := by
    have hfn : (fun i => w i * ((lam i - lam0) * (lam i - E1_lb))) =
        (fun i => w i * lam i * lam i - (lam0 + E1_lb) * (w i * lam i) + (lam0 * E1_lb) * w i) := by
      funext i; grind
    rw [hfn, sumFin_add, sumFin_sub, sumFin_mul_left, sumFin_mul_left]
    rfl
  rw [hexp] at hpoly
  have hmul : 0 ≤ specW0 m w * (specW2 m w lam - (lam0 + E1_lb) * specW1 m w lam + (lam0 * E1_lb) * specW0 m w) :=
    Int.mul_nonneg hw0 hpoly
  have hid :
      (specW0 m w * specW2 m w lam - specW1 m w lam * specW1 m w lam) -
        (E1_lb * specW0 m w - specW1 m w lam) * (specW1 m w lam - lam0 * specW0 m w) =
      specW0 m w * (specW2 m w lam - (lam0 + E1_lb) * specW1 m w lam + (lam0 * E1_lb) * specW0 m w) := by
    grind
  omega

/-- Theorem M2D (Certified SOS Energy Bracket Enclosure): If an SOS operator certificate proves `E_lb ≤ lam0`
    and a variational state obeys `<H>/S0 ≤ E_ub`, then the `Flow.Interval.Interval1D` bracket `{lo := E_lb, hi := E_ub}`
    contains the exact ground energy `lam0` and encloses both `E_lb ≤ lam0 ≤ E_ub`. -/
theorem certified_energy_bracket_from_sos {m : Nat} (w lam : Fin m → Int) (E_lb lam0 E_ub : Int)
    (hw : ∀ i, 0 ≤ w i) (hwpos : 0 < specW0 m w)
    (hlam : ∀ i, lam0 ≤ lam i)
    (hlb : E_lb ≤ lam0) (hub : specW1 m w lam ≤ E_ub * specW0 m w) :
    let iv : Interval1D := { lo := E_lb, hi := E_ub }
    iv.contains lam0 ∧ 0 ≤ iv.width := by
  have hrr := variational_rayleigh_ritz_upper w lam lam0 hw hlam
  have hmul : lam0 * specW0 m w ≤ E_ub * specW0 m w := by omega
  have hle : lam0 ≤ E_ub := by
    by_cases hbad : E_ub < lam0
    · have hgt : 0 < (lam0 - E_ub) * specW0 m w := Int.mul_pos (by omega) hwpos
      have hdiff : (lam0 - E_ub) * specW0 m w = lam0 * specW0 m w - E_ub * specW0 m w := by grind
      omega
    · omega
  dsimp [Interval1D.contains, Interval1D.width]
  exact ⟨⟨hlb, hle⟩, by omega⟩

/-! ## Section 3: 2-RDM N-Representability (D, Q, G Cones, CAR Maps & Dual SDP) (Theorem M3) -/

private theorem numOp_eval_exact {n : Nat} (k : Fin n) (ψ : FockVec n) (s : FockState n) :
    cCreate k (cAnnih k ψ) s = occBit (s k) * ψ s := by
  unfold cCreate cAnnih occBit
  cases hsk : s k
  · simp
  · simp only [hsk, ↓reduceIte, toggleBit_at', toggleBit_self', jwSign_toggle_self']
    simp only [Bool.false_eq_true, Bool.not_true]
    have hsq := jwSign_sq_one' s k
    grind

private theorem holeNumOp_eval_exact {n : Nat} (k : Fin n) (ψ : FockVec n) (s : FockState n) :
    cAnnih k (cCreate k ψ) s = (1 - occBit (s k)) * ψ s := by
  have hcar := car_canonical_anticomm k k ψ s
  have hnum := numOp_eval_exact k ψ s
  simp only [↓reduceIte] at hcar
  grind

private theorem cCreate_mul_indep {n : Nat} (p : Fin n) (f : FockState n → Int)
    (hf : ∀ t, f (toggleBit t p) = f t) (ψ : FockVec n) (s : FockState n) :
    cCreate p (fun t => f t * ψ t) s = f s * cCreate p ψ s := by
  unfold cCreate
  cases hsp : s p
  · simp
  · simp only [↓reduceIte, Bool.true_eq_false]
    rw [hf s]
    grind

private theorem cAnnih_mul_indep {n : Nat} (p : Fin n) (f : FockState n → Int)
    (hf : ∀ t, f (toggleBit t p) = f t) (ψ : FockVec n) (s : FockState n) :
    cAnnih p (fun t => f t * ψ t) s = f s * cAnnih p ψ s := by
  unfold cAnnih
  cases hsp : s p
  · simp only [Bool.false_eq_true, ↓reduceIte]
    rw [hf s]
    grind
  · simp

/-- Theorem M3A (2-RDM D/Q/G Cone Gram Positivity): For every Fock vector `ψ`, finite sample `basis`,
    and linear operator `O` (specializing to 2-particle `D`, 2-hole `Q`, and particle-hole `G` pair
    operators), the diagonal Gram form `∑_b (O ψ (basis b))^2` is identically non-negative (`>= 0`). -/
theorem rdm_dqg_gram_psd {n B : Nat} (basis : Fin B → FockState n) (ψ : FockVec n)
    (O : FockVec n → FockVec n) :
    0 ≤ sumFin B (fun b => O ψ (basis b) * O ψ (basis b)) :=
  sumFin_nonneg B _ fun _ => int_sq_nonneg _

theorem rdm_dqg_concrete_sectors_psd {n B : Nat} (basis : Fin B → FockState n)
    (ψ : FockVec n) (p q : Fin n) :
    0 ≤ sumFin B (fun b => cAnnih p (cAnnih q ψ) (basis b) * cAnnih p (cAnnih q ψ) (basis b)) ∧
    0 ≤ sumFin B (fun b => cCreate p (cCreate q ψ) (basis b) * cCreate p (cCreate q ψ) (basis b)) ∧
    0 ≤ sumFin B (fun b => cAnnih p (cCreate q ψ) (basis b) * cAnnih p (cCreate q ψ) (basis b)) :=
  ⟨rdm_dqg_gram_psd basis ψ (fun f => cAnnih p (cAnnih q f)),
   rdm_dqg_gram_psd basis ψ (fun f => cCreate p (cCreate q f)),
   rdm_dqg_gram_psd basis ψ (fun f => cAnnih p (cCreate q f))⟩

/-- Theorem M3B (Exact Arbitrary-n CAR Affine Maps D -> Q and D -> G): On any `n`-orbital Fock
    space and distinct indices `p ≠ q`, the diagonal 2-particle (`D`), 2-hole (`Q`), and particle-hole
    (`G`) operators satisfy exact signed CAR polynomials in `occBit (s p)` and `occBit (s q)`. -/
theorem rdm_q_and_g_from_d_car_exact {n : Nat} (p q : Fin n) (hpq : p ≠ q)
    (ψ : FockVec n) (s : FockState n) :
    cCreate p (cCreate q (cAnnih q (cAnnih p ψ))) s =
      (occBit (s p) * occBit (s q)) * ψ s ∧
    cAnnih p (cAnnih q (cCreate q (cCreate p ψ))) s =
      (1 - occBit (s p) - occBit (s q) + occBit (s p) * occBit (s q)) * ψ s ∧
    cCreate p (cAnnih q (cCreate q (cAnnih p ψ))) s =
      (occBit (s p) - occBit (s p) * occBit (s q)) * ψ s := by
  have hocc_indep : ∀ t : FockState n, occBit (toggleBit t p q) = occBit (t q) := by
    intro t
    rw [toggleBit_ne' t (fun h => hpq h.symm)]
  refine ⟨?_, ?_, ?_⟩
  · have hq : cCreate q (cAnnih q (cAnnih p ψ)) = fun t => occBit (t q) * cAnnih p ψ t :=
      funext (numOp_eval_exact q (cAnnih p ψ))
    rw [hq, cCreate_mul_indep p _ hocc_indep (cAnnih p ψ) s, numOp_eval_exact p ψ s]
    grind
  · have hq : cAnnih q (cCreate q (cCreate p ψ)) =
        fun t => (1 - occBit (t q)) * cCreate p ψ t :=
      funext (holeNumOp_eval_exact q (cCreate p ψ))
    have hind : ∀ t : FockState n, (1 - occBit (toggleBit t p q)) = 1 - occBit (t q) := by
      intro t; rw [hocc_indep t]
    rw [hq, cAnnih_mul_indep p _ hind (cCreate p ψ) s, holeNumOp_eval_exact p ψ s]
    grind
  · have hq : cAnnih q (cCreate q (cAnnih p ψ)) =
        fun t => (1 - occBit (t q)) * cAnnih p ψ t :=
      funext (holeNumOp_eval_exact q (cAnnih p ψ))
    have hind : ∀ t : FockState n, (1 - occBit (toggleBit t p q)) = 1 - occBit (t q) := by
      intro t; rw [hocc_indep t]
    rw [hq, cCreate_mul_indep p _ hind (cAnnih p ψ) s, numOp_eval_exact p ψ s]
    grind

private theorem electronCountPrefix_eq_sumPrefix {n : Nat} (s : FockState n) (m : Nat) :
    (electronCountPrefix s m : Int) = sumPrefix (fun p : Fin n => occBit (s p)) m := by
  induction m with
  | zero => simp [electronCountPrefix, sumPrefix]
  | succ m ih =>
    unfold electronCountPrefix sumPrefix
    by_cases hm : m < n
    · simp only [hm, ↓reduceDIte]
      rw [Int.natCast_add, ih]
      cases s ⟨m, hm⟩ <;> simp [occBit]
    · simp only [hm, ↓reduceDIte]
      rw [Int.natCast_add, ih]
      simp

private theorem sumFin_occBit_eq_electronCount {n : Nat} (s : FockState n) :
    sumFin n (fun p => occBit (s p)) = (electronCount s : Int) := by
  unfold electronCount sumFin
  exact (electronCountPrefix_eq_sumPrefix s n).symm

/-- Theorem M3C (Exact 2-RDM Pair Trace Contraction): For any `n`-orbital Fock state `s`,
    orbital occupations lie in `{0, 1}`, `∑_p n_p = N`, and removing diagonal terms gives exact
    2-particle normalization `∑_{p ≠ q} n_p n_q = N (N - 1)`. -/
theorem rdm_trace_contraction_exact {n : Nat} (s : FockState n) :
    (∀ p, 0 ≤ occBit (s p) ∧ occBit (s p) ≤ 1) ∧
    sumFin n (fun p => occBit (s p)) = (electronCount s : Int) ∧
    sumFin n (fun p => sumFin n (fun q => if p = q then 0 else occBit (s p) * occBit (s q))) =
      (electronCount s : Int) * ((electronCount s : Int) - 1) := by
  refine ⟨?_, sumFin_occBit_eq_electronCount s, ?_⟩
  · intro p
    unfold occBit
    cases s p <;> simp
  · have hrow : ∀ p : Fin n,
        sumFin n (fun q => if p = q then 0 else occBit (s p) * occBit (s q)) =
          occBit (s p) * (electronCount s : Int) - occBit (s p) := by
      intro p
      have hfn :
          (fun q : Fin n => if p = q then (0 : Int) else occBit (s p) * occBit (s q)) =
          (fun q : Fin n => occBit (s p) * occBit (s q) -
            (if q = p then occBit (s p) * occBit (s p) else 0)) := by
        funext q
        by_cases hpq : p = q
        · subst hpq; simp
        · have hqp : q ≠ p := fun h => hpq h.symm
          simp [hpq, hqp]
      rw [hfn, sumFin_sub, sumFin_mul_left, sumFin_occBit_eq_electronCount s,
          sumFin_single_hit p (occBit (s p) * occBit (s p))]
      have hsq : occBit (s p) * occBit (s p) = occBit (s p) := by
        unfold occBit; cases s p <;> simp
      rw [hsq]
    rw [sumFin_congr _ _ hrow, sumFin_sub, sumFin_mul_right, sumFin_occBit_eq_electronCount s]
    grind

def negPart (x : Int) : Int :=
  if x < 0 then x else 0

/-- Theorem M3D (Certified 2-RDM Dual SDP Lower Bound with Eigenvalue Floor Correction):
    For any exact decomposition `E_state = E_dual + slackD + slackQ + slackG` with positive traces
    `tD, tQ, tG >= 0` and dual eigenvalue floors `tX * lamX <= slackX`, clamping negative residual
    eigenvalues guarantees `E_dual + tD*negPart(lamD) + tQ*negPart(lamQ) + tG*negPart(lamG) <= E_state`. -/
theorem rdm_dual_sdp_certified_lower_bound
    (E_state E_dual slackD slackQ slackG tD tQ tG lamD lamQ lamG : Int)
    (hDecomp : E_state = E_dual + slackD + slackQ + slackG)
    (htD : 0 ≤ tD) (htQ : 0 ≤ tQ) (htG : 0 ≤ tG)
    (hsD : tD * lamD ≤ slackD) (hsQ : tQ * lamQ ≤ slackQ) (hsG : tG * lamG ≤ slackG) :
    E_dual + tD * negPart lamD + tQ * negPart lamQ + tG * negPart lamG ≤ E_state := by
  have hnD : negPart lamD ≤ lamD := by unfold negPart; split <;> omega
  have hnQ : negPart lamQ ≤ lamQ := by unfold negPart; split <;> omega
  have hnG : negPart lamG ≤ lamG := by unfold negPart; split <;> omega
  have hmD : tD * negPart lamD ≤ tD * lamD := Int.mul_le_mul_of_nonneg_left hnD htD
  have hmQ : tQ * negPart lamQ ≤ tQ * lamQ := Int.mul_le_mul_of_nonneg_left hnQ htQ
  have hmG : tG * negPart lamG ≤ tG * lamG := Int.mul_le_mul_of_nonneg_left hnG htG
  omega

/-! ## Section 4: Hohenberg-Kohn Uniqueness, Levy-Lieb, Virial Scaling & ERI Screening (Theorem M4) -/

/-- Theorem M4A (Hohenberg-Kohn First Theorem): Two external potentials `v1, v2` differing by more
    than an additive shift on their non-degenerate ground densities `ρ1, ρ2` (evidenced by strict
    Rayleigh-Ritz variational inequalities) cannot share the same electron density (`ρ1 ≠ ρ2`). -/
theorem hohenberg_kohn_first_theorem {M : Nat} (F1 F2 : Int) (ρ1 ρ2 v1 v2 : Fin M → Int)
    (h1 : F1 + sumFin M (fun r => ρ1 r * v1 r) < F2 + sumFin M (fun r => ρ2 r * v1 r))
    (h2 : F2 + sumFin M (fun r => ρ2 r * v2 r) < F1 + sumFin M (fun r => ρ1 r * v2 r)) :
    ρ1 ≠ ρ2 := by
  intro heq
  subst heq
  omega

/-- Theorem M4B (Levy-Lieb Constrained Search Principle): Minimizing over wavefunctions yielding
    density `ρ` gives universal functional `F_LL(ρ) <= F(ψ)` and global ground energy `E0 <= F(ψ) + V(ρ)`. -/
theorem levy_lieb_variational_principle {Rho Psi : Type}
    (F_LL : Rho → Int) (V : Rho → Int) (F_psi : Psi → Int) (rhoOf : Psi → Rho)
    (E0 : Int) (ρ0 : Rho)
    (hGround : E0 = F_LL ρ0 + V ρ0)
    (hMinRho : ∀ ρ, E0 ≤ F_LL ρ + V ρ)
    (hConstrained : ∀ ψ, F_LL (rhoOf ψ) ≤ F_psi ψ) :
    (∀ ψ, E0 ≤ F_psi ψ + V (rhoOf ψ)) ∧ (∃ ρ, F_LL ρ + V ρ = E0) := by
  refine ⟨?_, ⟨ρ0, hGround.symm⟩⟩
  intro ψ
  have h1 := hMinRho (rhoOf ψ)
  have h2 := hConstrained ψ
  omega

def scaledVirialEnergy (T1 V1 ζ : Int) : Int :=
  ζ * ζ * T1 + ζ * V1

/-- Theorem M4C (Exact Virial Coordinate Scaling Theorem): Under uniform coordinate scaling `r -> ζ r`,
    the scaled kinetic-potential energy satisfies `4*T1*E(ζ) + V1^2 = (2*T1*ζ + V1)^2 >= 0`, achieving
    its unique global minimum iff `2*T1*ζ + V1 = 0`, which enforces exact Virial equilibrium `2T + V = 0`. -/
theorem virial_equilibrium_scaling_exact (T1 V1 ζ : Int) (hT1 : 0 < T1) :
    4 * T1 * scaledVirialEnergy T1 V1 ζ + V1 * V1 = (2 * T1 * ζ + V1) * (2 * T1 * ζ + V1) ∧
    -(V1 * V1) ≤ 4 * T1 * scaledVirialEnergy T1 V1 ζ ∧
    (4 * T1 * scaledVirialEnergy T1 V1 ζ = -(V1 * V1) ↔ 2 * T1 * ζ + V1 = 0) ∧
    (2 * T1 * ζ + V1 = 0 → 2 * (ζ * ζ * T1) + ζ * V1 = 0) := by
  have hid :
      4 * T1 * scaledVirialEnergy T1 V1 ζ + V1 * V1 =
        (2 * T1 * ζ + V1) * (2 * T1 * ζ + V1) := by
    unfold scaledVirialEnergy
    grind
  have hsq := int_sq_nonneg (2 * T1 * ζ + V1)
  have hiff : 4 * T1 * scaledVirialEnergy T1 V1 ζ = -(V1 * V1) ↔ 2 * T1 * ζ + V1 = 0 := by
    constructor
    · intro heq
      have hz : (2 * T1 * ζ + V1) * (2 * T1 * ζ + V1) = 0 := by omega
      cases Int.mul_eq_zero.mp hz with
      | inl h => exact h
      | inr h => exact h
    · intro hz
      rw [hz] at hid
      omega
  have hvir : 2 * T1 * ζ + V1 = 0 → 2 * (ζ * ζ * T1) + ζ * V1 = 0 := by
    intro hz
    have h1 : 2 * (ζ * ζ * T1) + ζ * V1 = ζ * (2 * T1 * ζ + V1) := by grind
    rw [h1, hz]
    exact Int.mul_zero ζ
  have _ := hT1
  exact ⟨hid, by omega, hiff, hvir⟩

def eriWeighted {G : Nat} (w : Fin G → Int) (u v : Fin G → Int) : Int :=
  sumFin G (fun g => w g * (u g * v g))

/-- Theorem M4D (ERI 8-Fold Symmetry Kernel & Cauchy-Schwarz Screening Bound): For any non-negative
    Coulomb quadrature kernel `w >= 0` and charge distributions `u, v`, `(u|v) = (v|u)` and
    `|(u|v)|^2 <= (u|u)(v|v)` identically, certifying exact Integral-Direct Schwarz screening. -/
theorem eri_cauchy_schwarz_screening {G : Nat} (w u v : Fin G → Int) (hw : ∀ g, 0 ≤ w g) :
    eriWeighted w u v = eriWeighted w v u ∧
    eriWeighted w u v * eriWeighted w u v ≤ eriWeighted w u u * eriWeighted w v v := by
  refine ⟨?_, weighted_cauchy_schwarz w u v hw⟩
  unfold eriWeighted
  exact sumFin_congr _ _ fun g => by grind

/-! ## Section 5: RHF Density Projector, Roothaan-Hall [F,P] = 0 & Conformer Bilevel PES (Theorem M5) -/

abbrev RMat (M N : Nat) : Type := Fin M → Fin N → Int

def rmatMul {A B C : Nat} (X : RMat A B) (Y : RMat B C) : RMat A C :=
  fun i j => sumFin B (fun k => X i k * Y k j)

def rmatTrans {A B : Nat} (X : RMat A B) : RMat B A :=
  fun i j => X j i

def rmatScale {A B : Nat} (s : Int) (X : RMat A B) : RMat A B :=
  fun i j => s * X i j

def rmatTr {A : Nat} (X : RMat A A) : Int :=
  sumFin A (fun i => X i i)

/-- Theorem M5A (Closed-Shell RHF Density Matrix Symmetry, Idempotency & Trace Count):
    Given `K` orbitals, `Nocc` occupied MOs with scaled orthonormal columns `C^T C = s I_{Nocc}`,
    the density matrix `P = C C^T` obeys `P^T = P`, `P^2 = s P`, and `2 Tr(P) = 2 s Nocc`. -/
theorem rhf_density_projector_idempotent {K Nocc : Nat} (s : Int) (C : RMat K Nocc)
    (hOrtho : rmatMul (rmatTrans C) C = fun i j => if i = j then s else 0) :
    let P := rmatMul C (rmatTrans C)
    rmatTrans P = P ∧
    rmatMul P P = rmatScale s P ∧
    2 * rmatTr P = 2 * s * (Nocc : Int) := by
  intro P
  refine ⟨?_, ?_, ?_⟩
  · funext i j
    unfold rmatTrans P rmatMul
    exact sumFin_congr _ _ fun μ => Int.mul_comm _ _
  · funext i j
    have hcol : ∀ μ ν : Fin Nocc,
        sumFin K (fun k => C k μ * C k ν) = if μ = ν then s else 0 := by
      intro μ ν
      have heq := congrFun (congrFun hOrtho μ) ν
      simpa [rmatMul, rmatTrans] using heq
    have hstep1 : rmatMul P P i j =
        sumFin Nocc (fun μ => C i μ * sumFin Nocc (fun ν =>
          sumFin K (fun k => C k μ * C k ν) * C j ν)) := by
      unfold rmatMul P rmatTrans
      calc
        sumFin K (fun k => sumFin Nocc (fun μ => C i μ * C k μ) *
            sumFin Nocc (fun ν => C k ν * C j ν))
          = sumFin K (fun k => sumFin Nocc (fun μ =>
              C i μ * C k μ * sumFin Nocc (fun ν => C k ν * C j ν))) := by
            exact sumFin_congr _ _ fun k => by rw [← sumFin_mul_right]
        _ = sumFin Nocc (fun μ => sumFin K (fun k =>
              C i μ * C k μ * sumFin Nocc (fun ν => C k ν * C j ν))) := by
            exact sumFin_swap_gen K Nocc _
        _ = sumFin Nocc (fun μ => C i μ * sumFin K (fun k =>
              C k μ * sumFin Nocc (fun ν => C k ν * C j ν))) := by
            exact sumFin_congr _ _ fun μ => by
              rw [← sumFin_mul_left]
              exact sumFin_congr _ _ fun k => by grind
        _ = sumFin Nocc (fun μ => C i μ * sumFin K (fun k =>
              sumFin Nocc (fun ν => C k μ * C k ν * C j ν))) := by
            exact sumFin_congr _ _ fun μ => by
              congr 1
              exact sumFin_congr _ _ fun k => by
                rw [← sumFin_mul_left]
                exact sumFin_congr _ _ fun ν => by grind
        _ = sumFin Nocc (fun μ => C i μ * sumFin Nocc (fun ν =>
              sumFin K (fun k => C k μ * C k ν * C j ν))) := by
            exact sumFin_congr _ _ fun μ => by
              congr 1
              exact sumFin_swap_gen K Nocc _
        _ = sumFin Nocc (fun μ => C i μ * sumFin Nocc (fun ν =>
              sumFin K (fun k => C k μ * C k ν) * C j ν)) := by
            exact sumFin_congr _ _ fun μ => by
              congr 1
              exact sumFin_congr _ _ fun ν => by rw [← sumFin_mul_right]
    rw [hstep1]
    have hinner : ∀ μ : Fin Nocc,
        sumFin Nocc (fun ν => sumFin K (fun k => C k μ * C k ν) * C j ν) = s * C j μ := by
      intro μ
      have hfn : (fun ν : Fin Nocc => sumFin K (fun k => C k μ * C k ν) * C j ν) =
          (fun ν : Fin Nocc => if ν = μ then s * C j μ else 0) := by
        funext ν
        rw [hcol μ ν]
        by_cases h : ν = μ
        · subst h; simp
        · have hne : μ ≠ ν := fun h' => h h'.symm
          simp [hne, h]
      rw [hfn, sumFin_single_hit μ (s * C j μ)]
    rw [sumFin_congr _ _ fun μ => by rw [hinner μ]]
    unfold rmatScale P rmatMul rmatTrans
    rw [← sumFin_mul_left]
    exact sumFin_congr _ _ fun μ => by grind
  · have htr : rmatTr P = sumFin Nocc (fun μ => sumFin K (fun k => C k μ * C k μ)) := by
      unfold rmatTr P rmatMul rmatTrans
      rw [sumFin_swap_gen K Nocc (fun k μ => C k μ * C k μ)]
    have hdiag : ∀ μ : Fin Nocc, sumFin K (fun k => C k μ * C k μ) = s := by
      intro μ
      have heq := congrFun (congrFun hOrtho μ) μ
      simpa [rmatMul, rmatTrans] using heq
    rw [htr, sumFin_congr _ _ hdiag, sumFin_const]
    grind

/-- Theorem M5B (Roothaan-Hall Commutator Stationarity `[F, P] = 0`): For symmetric Fock matrix
    `F^T = F` whose occupied MO columns obey `F C = C Λ` (`Λ = diag(eps)`), the commutator
    `F P - P F` vanishes identically at self-consistency (`convergence of DIIS`). -/
theorem roothaan_hall_commutator_zero {K Nocc : Nat} (F : RMat K K) (C : RMat K Nocc)
    (eps : Fin Nocc → Int)
    (hFsym : rmatTrans F = F)
    (hEig : ∀ i μ, rmatMul F C i μ = C i μ * eps μ) :
    let P := rmatMul C (rmatTrans C)
    ∀ i j, rmatMul F P i j - rmatMul P F i j = 0 := by
  intro P i j
  have hFP : rmatMul F P i j = sumFin Nocc (fun μ => C i μ * C j μ * eps μ) := by
    unfold rmatMul P rmatTrans
    calc
      sumFin K (fun k => F i k * sumFin Nocc (fun μ => C k μ * C j μ))
        = sumFin K (fun k => sumFin Nocc (fun μ => F i k * C k μ * C j μ)) := by
          exact sumFin_congr _ _ fun k => by
            rw [← sumFin_mul_left]
            exact sumFin_congr _ _ fun μ => by grind
      _ = sumFin Nocc (fun μ => sumFin K (fun k => F i k * C k μ * C j μ)) :=
          sumFin_swap_gen K Nocc _
      _ = sumFin Nocc (fun μ => sumFin K (fun k => F i k * C k μ) * C j μ) := by
          exact sumFin_congr _ _ fun μ => by rw [← sumFin_mul_right]
      _ = sumFin Nocc (fun μ => C i μ * eps μ * C j μ) := by
          exact sumFin_congr _ _ fun μ => by
            have he := hEig i μ
            unfold rmatMul at he
            rw [he]
      _ = sumFin Nocc (fun μ => C i μ * C j μ * eps μ) := by
          exact sumFin_congr _ _ fun μ => by grind
  have hPF : rmatMul P F i j = sumFin Nocc (fun μ => C i μ * C j μ * eps μ) := by
    have hFrow : ∀ μ, sumFin K (fun k => C k μ * F k j) = C j μ * eps μ := by
      intro μ
      have hsym : ∀ k, F k j = F j k := fun k => congrFun (congrFun hFsym j) k
      have he := hEig j μ
      unfold rmatMul at he
      calc
        sumFin K (fun k => C k μ * F k j)
          = sumFin K (fun k => F j k * C k μ) :=
            sumFin_congr _ _ fun k => by rw [hsym k, Int.mul_comm]
        _ = C j μ * eps μ := he
    unfold rmatMul P rmatTrans
    calc
      sumFin K (fun k => sumFin Nocc (fun μ => C i μ * C k μ) * F k j)
        = sumFin K (fun k => sumFin Nocc (fun μ => C i μ * (C k μ * F k j))) := by
          exact sumFin_congr _ _ fun k => by
            rw [← sumFin_mul_right]
            exact sumFin_congr _ _ fun μ => by grind
      _ = sumFin Nocc (fun μ => sumFin K (fun k => C i μ * (C k μ * F k j))) :=
          sumFin_swap_gen K Nocc _
      _ = sumFin Nocc (fun μ => C i μ * sumFin K (fun k => C k μ * F k j)) := by
          exact sumFin_congr _ _ fun μ => by rw [← sumFin_mul_left]
      _ = sumFin Nocc (fun μ => C i μ * (C j μ * eps μ)) := by
          exact sumFin_congr _ _ fun μ => by rw [hFrow μ]
      _ = sumFin Nocc (fun μ => C i μ * C j μ * eps μ) := by
          exact sumFin_congr _ _ fun μ => by grind
  rw [hFP, hPF]
  omega

/-- Theorem M5C (Bilevel SEARU x 2-Jet Molecular Conformer Global Convergence): On any discrete
    conformer-bond-coordinate molecular PES `Landscape`, local geometry relaxation (`2-Jet`) stays
    trapped inside metastable isomers (`0 < L.vacuumFloor c0.sector`), unrelaxed conformer hopping
    suffers Franck-Condon nuclear-clash false rejection, and coupled `SEARU x 2-Jet` achieves exact
    global ground-state convergence across any proposal stream covering `L.trueSector`. -/
theorem molecular_bilevel_symbiotic_duality (L : Landscape) :
    (∀ (F : LocalGeometricFlow) (c0 : Config) (_ : 0 < L.vacuumFloor c0.sector) (t : Nat),
      L.vacuumFloor c0.sector ≤ energy L (iterGeo F t c0) ∧ 0 < energy L (iterGeo F t c0)) ∧
    (∀ (s0 : Nat) (x : Int),
      L.vacuumFloor s0 < L.curvature L.trueSector * sqDist x (L.fiberMin L.trueSector) →
      energy L { sector := s0, modulus := L.fiberMin s0 } <
        energy L (instantonTunnelRaw L.trueSector x)) ∧
    (∀ (proposals : List Nat) (c0 : Config), L.trueSector ∈ proposals →
      energy L (runSymbiotic L proposals c0) = 0 ∧
      (runSymbiotic L proposals c0).modulus =
        L.fiberMin (runSymbiotic L proposals c0).sector) :=
  ⟨fun F c0 hFalse t => pure_2jet_trapped_in_false_vacuum L F c0 hFalse t,
   fun s0 x hStrain => unrelaxed_tunnel_masks_true_vacuum L s0 x hStrain,
   fun proposals c0 hHit => symbiotic_duality_complete L proposals c0 hHit⟩

end Flow.MolecularBundle
