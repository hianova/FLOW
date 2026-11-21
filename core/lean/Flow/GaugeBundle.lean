/-
  Flow.GaugeBundle — 2D Lattice Principal U(1) Bundle, Luscher Admissibility & Instanton Optimality
  ==================================================================================================
  Canonical formalization of compact U(1) gauge connections on an arbitrary L x L periodic torus T_L^2:
    1. Exact principal-branch circle wrapping (-H, H] and winding numbers over Int;
    2. Arbitrary L x L periodic 2D torus sums, telescoping, and exact 2D Cauchy-Schwarz inequality;
    3. Discrete Bianchi identity & exact integer Chern quantization (Theorem G1);
    4. Exact local + periodic-winding U(1) gauge invariance of curvature, charge, and action (Theorem G2);
    5. Luscher admissibility & iterated local-flow topological freezing No-Go for all L >= 1, t >= 0 (Theorem G3);
    6. Harmonic 2-Jet instanton Cauchy-Schwarz optimality vs bare-core L^2/4 UV penalty for all L >= 2 (Theorem G4);
    7. Exact micro-reversible translation & invertible-flow Metropolis-Hastings detailed balance (Theorem G5).
-/
import Flow.Core

namespace Flow.GaugeBundle

/-! ## Section 1: Exact Principal-Branch Circle Arithmetic (`(-H, H]`) -/

def period (H : Nat) : Int :=
  2 * (H : Int)

theorem period_pos {H : Nat} (hH : 0 < H) : 0 < period H := by
  unfold period
  omega

def wrap (H : Nat) (θ : Int) : Int :=
  (θ + (H : Int) - 1) % period H - ((H : Int) - 1)

def wind (H : Nat) (θ : Int) : Int :=
  (θ + (H : Int) - 1) / period H

theorem wrap_bounds {H : Nat} (hH : 0 < H) (θ : Int) :
    -(H : Int) < wrap H θ ∧ wrap H θ ≤ (H : Int) := by
  have hp : 0 < period H := period_pos hH
  have hpNe : period H ≠ 0 := by omega
  have hge : 0 ≤ (θ + (H : Int) - 1) % period H := Int.emod_nonneg _ hpNe
  have hlt : (θ + (H : Int) - 1) % period H < period H := Int.emod_lt_of_pos _ hp
  unfold wrap period at *
  omega

theorem wrap_decomp {H : Nat} (hH : 0 < H) (θ : Int) :
    θ = wrap H θ + wind H θ * period H := by
  have _ := period_pos hH
  unfold wrap wind
  have hed := Int.ediv_mul_add_emod (θ + (H : Int) - 1) (period H)
  omega

theorem wrap_eq_self_of_bounds {H : Nat} (_hH : 0 < H) {θ : Int}
    (hlo : -(H : Int) < θ) (hhi : θ ≤ (H : Int)) :
    wrap H θ = θ := by
  unfold wrap period
  have h0 : 0 ≤ θ + (H : Int) - 1 := by omega
  have hlt : θ + (H : Int) - 1 < 2 * (H : Int) := by omega
  rw [Int.emod_eq_of_lt h0 hlt]
  omega

theorem wind_eq_zero_of_bounds {H : Nat} (_hH : 0 < H) {θ : Int}
    (hlo : -(H : Int) < θ) (hhi : θ ≤ (H : Int)) :
    wind H θ = 0 := by
  unfold wind period
  have h0 : 0 ≤ θ + (H : Int) - 1 := by omega
  have hlt : θ + (H : Int) - 1 < 2 * (H : Int) := by omega
  exact Int.ediv_eq_zero_of_lt h0 hlt

theorem wrap_add_mul_period (H : Nat) (θ k : Int) :
    wrap H (θ + k * period H) = wrap H θ := by
  unfold wrap
  have heq : θ + k * period H + (H : Int) - 1 = (θ + (H : Int) - 1) + period H * k := by
    rw [Int.mul_comm k (period H)]
    omega
  rw [heq, Int.add_mul_emod_self_left]

theorem wrap_admissible_step {H : Nat} (hH : 0 < H) (a δ : Int)
    (hlo : -(H : Int) < wrap H a + δ) (hhi : wrap H a + δ ≤ (H : Int)) :
    wrap H (a + δ) = wrap H a + δ := by
  have ha := wrap_decomp hH a
  have hsum : a + δ = (wrap H a + δ) + wind H a * period H := by omega
  rw [hsum, wrap_add_mul_period, wrap_eq_self_of_bounds hH hlo hhi]

theorem wind_admissible_step {H : Nat} (hH : 0 < H) (a δ : Int)
    (hlo : -(H : Int) < wrap H a + δ) (hhi : wrap H a + δ ≤ (H : Int)) :
    wind H (a + δ) = wind H a := by
  have hp : period H ≠ 0 := by
    have := period_pos hH
    omega
  have ha := wrap_decomp hH a
  have had := wrap_decomp hH (a + δ)
  have hw := wrap_admissible_step hH a δ hlo hhi
  have heqMul : wind H (a + δ) * period H = wind H a * period H := by omega
  exact Int.eq_of_mul_eq_mul_right hp heqMul

theorem wrap_add_mul_decomp {H : Nat} (hH : 0 < H) {x k : Int}
    (hlo : -(H : Int) < x) (hhi : x ≤ (H : Int)) :
    wrap H (x + k * period H) = x ∧ wind H (x + k * period H) = k := by
  have hwrap : wrap H (x + k * period H) = x := by
    rw [wrap_add_mul_period]
    exact wrap_eq_self_of_bounds hH hlo hhi
  have hp : period H ≠ 0 := by
    have := period_pos hH
    omega
  have had := wrap_decomp hH (x + k * period H)
  rw [hwrap] at had
  have heqMul : wind H (x + k * period H) * period H = k * period H := by omega
  exact ⟨hwrap, Int.eq_of_mul_eq_mul_right hp heqMul⟩

/-! ## Section 2: Arbitrary `L × L` Periodic Torus Sums & 2D Cauchy-Schwarz -/

def nextIdx {L : Nat} (hL : 0 < L) (i : Fin L) : Fin L :=
  ⟨(i.val + 1) % L, Nat.mod_lt _ hL⟩

def prevIdx {L : Nat} (hL : 0 < L) (i : Fin L) : Fin L :=
  ⟨(i.val + L - 1) % L, Nat.mod_lt _ hL⟩

theorem nextIdx_prevIdx {L : Nat} (hL : 0 < L) (i : Fin L) :
    nextIdx hL (prevIdx hL i) = i := by
  ext
  simp only [nextIdx, prevIdx]
  have hi : i.val < L := i.isLt
  have heq : (i.val + L - 1) % L + 1 = (i.val + (L - 1)) % L + 1 := by
    congr 2; omega
  rw [heq, Nat.mod_add_mod]
  have hsum : i.val + (L - 1) + 1 = i.val + L := by omega
  rw [hsum, Nat.add_mod_right, Nat.mod_eq_of_lt hi]

def sumPrefix {N : Nat} (f : Fin N → Int) : Nat → Int
  | 0 => 0
  | k + 1 => sumPrefix f k + if h : k < N then f ⟨k, h⟩ else 0

def sumFin (N : Nat) (f : Fin N → Int) : Int :=
  sumPrefix f N

def sumTorus (L : Nat) (f : Fin L → Fin L → Int) : Int :=
  sumFin L (fun x => sumFin L (fun y => f x y))

theorem sumPrefix_zero {N : Nat} (k : Nat) :
    sumPrefix (fun _ : Fin N => (0 : Int)) k = 0 := by
  induction k with
  | zero => simp [sumPrefix]
  | succ k ih =>
    simp only [sumPrefix, ih]
    split <;> simp

theorem sumFin_zero (N : Nat) :
    sumFin N (fun _ => (0 : Int)) = 0 :=
  sumPrefix_zero N

theorem sumPrefix_add {N : Nat} (f g : Fin N → Int) (k : Nat) :
    sumPrefix (fun i => f i + g i) k = sumPrefix f k + sumPrefix g k := by
  induction k with
  | zero => simp [sumPrefix]
  | succ k ih =>
    simp only [sumPrefix, ih]
    split <;> omega

theorem sumFin_add (N : Nat) (f g : Fin N → Int) :
    sumFin N (fun i => f i + g i) = sumFin N f + sumFin N g :=
  sumPrefix_add f g N

theorem sumPrefix_sub {N : Nat} (f g : Fin N → Int) (k : Nat) :
    sumPrefix (fun i => f i - g i) k = sumPrefix f k - sumPrefix g k := by
  induction k with
  | zero => simp [sumPrefix]
  | succ k ih =>
    simp only [sumPrefix, ih]
    split <;> omega

theorem sumFin_sub (N : Nat) (f g : Fin N → Int) :
    sumFin N (fun i => f i - g i) = sumFin N f - sumFin N g :=
  sumPrefix_sub f g N

theorem sumPrefix_neg {N : Nat} (f : Fin N → Int) (k : Nat) :
    sumPrefix (fun i => -(f i)) k = -sumPrefix f k := by
  induction k with
  | zero => simp [sumPrefix]
  | succ k ih =>
    simp only [sumPrefix, ih]
    split <;> omega

theorem sumFin_neg (N : Nat) (f : Fin N → Int) :
    sumFin N (fun i => -(f i)) = -sumFin N f :=
  sumPrefix_neg f N

theorem sumPrefix_const {N : Nat} (c : Int) (k : Nat) (hk : k ≤ N) :
    sumPrefix (fun _ : Fin N => c) k = (k : Int) * c := by
  induction k with
  | zero => simp [sumPrefix]
  | succ k ih =>
    have hkN : k < N := hk
    have hkLe : k ≤ N := Nat.le_of_lt hkN
    simp only [sumPrefix, hkN, ↓reduceDIte, ih hkLe]
    have hnat : ((k + 1 : Nat) : Int) = (k : Int) + 1 := by omega
    rw [hnat, Int.add_mul, Int.one_mul]

theorem sumFin_const (N : Nat) (c : Int) :
    sumFin N (fun _ => c) = (N : Int) * c :=
  sumPrefix_const c N (Nat.le_refl N)

theorem sumPrefix_mul_left {N : Nat} (c : Int) (f : Fin N → Int) (k : Nat) :
    sumPrefix (fun i => c * f i) k = c * sumPrefix f k := by
  induction k with
  | zero => simp [sumPrefix]
  | succ k ih =>
    simp only [sumPrefix, ih]
    split
    · rw [Int.mul_add]
    · simp

theorem sumFin_mul_left (N : Nat) (c : Int) (f : Fin N → Int) :
    sumFin N (fun i => c * f i) = c * sumFin N f :=
  sumPrefix_mul_left c f N

theorem sumPrefix_nonneg {N : Nat} (f : Fin N → Int) (hf : ∀ i, 0 ≤ f i) (k : Nat) :
    0 ≤ sumPrefix f k := by
  induction k with
  | zero => simp [sumPrefix]
  | succ k ih =>
    simp only [sumPrefix]
    split
    · exact Int.add_nonneg ih (hf _)
    · exact Int.add_nonneg ih (Int.le_refl 0)

theorem sumFin_nonneg (N : Nat) (f : Fin N → Int) (hf : ∀ i, 0 ≤ f i) :
    0 ≤ sumFin N f :=
  sumPrefix_nonneg f hf N

private theorem nextIdx_mk_lt {N : Nat} (hN : 0 < N) {k : Nat}
    (hk : k < N) (hk1 : k + 1 < N) :
    nextIdx hN ⟨k, hk⟩ = ⟨k + 1, hk1⟩ := by
  ext
  simp only [nextIdx]
  exact Nat.mod_eq_of_lt hk1

private theorem sumPrefix_shift_aux {N : Nat} (hN : 0 < N) (f : Fin N → Int) :
    ∀ k : Nat, k < N →
      sumPrefix (fun i => f (nextIdx hN i)) k = sumPrefix f (k + 1) - f ⟨0, hN⟩
  | 0, _ => by
    simp only [sumPrefix, hN, ↓reduceDIte]
    omega
  | k + 1, hk => by
    have hkPrev : k < N := Nat.lt_trans (Nat.lt_succ_self k) hk
    have ih := sumPrefix_shift_aux hN f k hkPrev
    have hnext : nextIdx hN ⟨k, hkPrev⟩ = ⟨k + 1, hk⟩ := nextIdx_mk_lt hN hkPrev hk
    calc sumPrefix (fun i => f (nextIdx hN i)) (k + 1)
        = sumPrefix (fun i => f (nextIdx hN i)) k + f (nextIdx hN ⟨k, hkPrev⟩) := by
          simp only [sumPrefix, hkPrev, ↓reduceDIte]
      _ = (sumPrefix f (k + 1) - f ⟨0, hN⟩) + f ⟨k + 1, hk⟩ := by
          rw [ih, hnext]
      _ = sumPrefix f (k + 2) - f ⟨0, hN⟩ := by
          simp only [sumPrefix, hk, ↓reduceDIte]
          omega

theorem sumFin_shift_next {N : Nat} (hN : 0 < N) (f : Fin N → Int) :
    sumFin N (fun i => f (nextIdx hN i)) = sumFin N f := by
  unfold sumFin
  have hlast : N - 1 < N := Nat.sub_lt hN Nat.zero_lt_one
  have heqN : N = (N - 1) + 1 := by omega
  have hwrap : nextIdx hN ⟨N - 1, hlast⟩ = ⟨0, hN⟩ := by
    ext
    simp only [nextIdx]
    have hsum : (N - 1) + 1 = N := by omega
    rw [hsum, Nat.mod_self]
  calc sumPrefix (fun i => f (nextIdx hN i)) N
      = sumPrefix (fun i => f (nextIdx hN i)) ((N - 1) + 1) := by rw [← heqN]
    _ = sumPrefix (fun i => f (nextIdx hN i)) (N - 1) + f (nextIdx hN ⟨N - 1, hlast⟩) := by
        simp only [sumPrefix, hlast, ↓reduceDIte]
    _ = (sumPrefix f ((N - 1) + 1) - f ⟨0, hN⟩) + f ⟨0, hN⟩ := by
        rw [sumPrefix_shift_aux hN f (N - 1) hlast, hwrap]
    _ = sumPrefix f N := by
        rw [← heqN]
        omega

theorem sumFin_telescoping {N : Nat} (hN : 0 < N) (g : Fin N → Int) :
    sumFin N (fun i => g (nextIdx hN i) - g i) = 0 := by
  rw [sumFin_sub, sumFin_shift_next]
  omega

private theorem sumPrefix_single_hit {N : Nat} (idx : Fin N) (v : Int) (k : Nat) (hk : k ≤ N) :
    sumPrefix (fun i : Fin N => if i = idx then v else 0) k =
      if idx.val < k then v else 0 := by
  induction k with
  | zero =>
    simp [sumPrefix]
  | succ k ih =>
    have hkN : k < N := hk
    have hkLe : k ≤ N := Nat.le_of_lt hkN
    simp only [sumPrefix, hkN, ↓reduceDIte, ih hkLe]
    by_cases hlt : idx.val < k
    · have hlt1 : idx.val < k + 1 := by omega
      have hne : (⟨k, hkN⟩ : Fin N) ≠ idx := by
        intro heq
        have hv := congrArg Fin.val heq
        dsimp at hv
        omega
      simp [hlt, hlt1, hne]
    · by_cases heqv : idx.val = k
      · have hlt1 : idx.val < k + 1 := by omega
        have heq : (⟨k, hkN⟩ : Fin N) = idx := by
          ext; exact heqv.symm
        simp [hlt, hlt1, heq]
      · have hlt1 : ¬(idx.val < k + 1) := by omega
        have hne : (⟨k, hkN⟩ : Fin N) ≠ idx := by
          intro heq
          have hv := congrArg Fin.val heq
          dsimp at hv
          omega
        simp [hlt, hlt1, hne]

theorem sumFin_single_hit {N : Nat} (idx : Fin N) (v : Int) :
    sumFin N (fun i : Fin N => if i = idx then v else 0) = v := by
  unfold sumFin
  rw [sumPrefix_single_hit idx v N (Nat.le_refl N)]
  simp [idx.isLt]

private theorem sumPrefix_swap {L : Nat} (g : Fin L → Fin L → Int) (kx ky : Nat) :
    sumPrefix (fun x => sumPrefix (fun y => g x y) ky) kx =
      sumPrefix (fun y => sumPrefix (fun x => g x y) kx) ky := by
  induction kx with
  | zero =>
    simp only [sumPrefix]
    exact (sumPrefix_zero ky).symm
  | succ kx ih =>
    have hsplit :
        (fun y : Fin L => sumPrefix (fun x : Fin L => g x y) (kx + 1)) =
        (fun y : Fin L => sumPrefix (fun x : Fin L => g x y) kx +
            if hx : kx < L then g ⟨kx, hx⟩ y else 0) := by
      funext y
      simp only [sumPrefix]
    rw [hsplit, sumPrefix_add]
    simp only [sumPrefix]
    rw [ih]
    congr 1
    split
    · rfl
    · exact (sumPrefix_zero ky).symm

theorem sumTorus_swap (L : Nat) (f : Fin L → Fin L → Int) :
    sumTorus L f = sumTorus L (fun y x => f x y) :=
  sumPrefix_swap f L L

theorem sumTorus_add (L : Nat) (f g : Fin L → Fin L → Int) :
    sumTorus L (fun x y => f x y + g x y) = sumTorus L f + sumTorus L g := by
  unfold sumTorus
  have hrow : (fun x => sumFin L (fun y => f x y + g x y)) =
      (fun x => sumFin L (fun y => f x y) + sumFin L (fun y => g x y)) := by
    funext x
    exact sumFin_add L (f x) (g x)
  rw [hrow, sumFin_add]

theorem sumTorus_sub (L : Nat) (f g : Fin L → Fin L → Int) :
    sumTorus L (fun x y => f x y - g x y) = sumTorus L f - sumTorus L g := by
  unfold sumTorus
  have hrow : (fun x => sumFin L (fun y => f x y - g x y)) =
      (fun x => sumFin L (fun y => f x y) - sumFin L (fun y => g x y)) := by
    funext x
    exact sumFin_sub L (f x) (g x)
  rw [hrow, sumFin_sub]

theorem sumTorus_neg (L : Nat) (f : Fin L → Fin L → Int) :
    sumTorus L (fun x y => -(f x y)) = -sumTorus L f := by
  unfold sumTorus
  have hrow : (fun x => sumFin L (fun y => -(f x y))) =
      (fun x => -sumFin L (fun y => f x y)) := by
    funext x
    exact sumFin_neg L (f x)
  rw [hrow, sumFin_neg]

theorem sumTorus_const (L : Nat) (c : Int) :
    sumTorus L (fun _ _ => c) = (L * L : Int) * c := by
  unfold sumTorus
  have hrow : (fun _ : Fin L => sumFin L (fun _ : Fin L => c)) = fun _ => (L : Int) * c := by
    funext _
    exact sumFin_const L c
  rw [hrow, sumFin_const]
  ac_rfl

theorem sumTorus_mul_left (L : Nat) (c : Int) (f : Fin L → Fin L → Int) :
    sumTorus L (fun x y => c * f x y) = c * sumTorus L f := by
  unfold sumTorus
  have hrow : (fun x => sumFin L (fun y => c * f x y)) =
      (fun x => c * sumFin L (fun y => f x y)) := by
    funext x
    exact sumFin_mul_left L c (f x)
  rw [hrow, sumFin_mul_left]

theorem sumTorus_nonneg (L : Nat) (f : Fin L → Fin L → Int) (hf : ∀ x y, 0 ≤ f x y) :
    0 ≤ sumTorus L f := by
  unfold sumTorus
  exact sumFin_nonneg L _ fun x => sumFin_nonneg L _ fun y => hf x y

theorem sumTorus_shift_x {L : Nat} (hL : 0 < L) (f : Fin L → Fin L → Int) :
    sumTorus L (fun x y => f (nextIdx hL x) y) = sumTorus L f := by
  unfold sumTorus
  exact sumFin_shift_next hL (fun x => sumFin L (fun y => f x y))

theorem sumTorus_shift_y {L : Nat} (hL : 0 < L) (f : Fin L → Fin L → Int) :
    sumTorus L (fun x y => f x (nextIdx hL y)) = sumTorus L f := by
  unfold sumTorus
  congr 1
  funext x
  exact sumFin_shift_next hL (fun y => f x y)

theorem sumTorus_telescoping_x {L : Nat} (hL : 0 < L) (g : Fin L → Fin L → Int) :
    sumTorus L (fun x y => g (nextIdx hL x) y - g x y) = 0 := by
  rw [sumTorus_sub, sumTorus_shift_x]
  omega

theorem sumTorus_telescoping_y {L : Nat} (hL : 0 < L) (g : Fin L → Fin L → Int) :
    sumTorus L (fun x y => g x (nextIdx hL y) - g x y) = 0 := by
  rw [sumTorus_sub, sumTorus_shift_y]
  omega

theorem sumTorus_single_hit {L : Nat} (ix iy : Fin L) (v : Int) :
    sumTorus L (fun x y : Fin L => if x = ix ∧ y = iy then v else 0) = v := by
  unfold sumTorus
  have hrow : (fun x : Fin L => sumFin L (fun y : Fin L => if x = ix ∧ y = iy then v else 0)) =
      (fun x : Fin L => if x = ix then v else 0) := by
    funext x
    by_cases hx : x = ix
    · subst hx
      simp [sumFin_single_hit iy v]
    · simp only [hx, false_and, ↓reduceIte]
      exact sumFin_zero L
  rw [hrow]
  exact sumFin_single_hit ix v

private theorem int_sq_nonneg (z : Int) : 0 ≤ z * z := by
  by_cases hz : 0 ≤ z
  · exact Int.mul_nonneg hz hz
  · have hneg : 0 ≤ -z := by omega
    have hsq : (-z) * (-z) = z * z := Int.neg_mul_neg z z
    rw [← hsq]
    exact Int.mul_nonneg hneg hneg

theorem torus_cauchy_schwarz {L : Nat} (hL : 0 < L) (f : Fin L → Fin L → Int) :
    sumTorus L f * sumTorus L f ≤
      (L * L : Int) * sumTorus L (fun x y => f x y * f x y) := by
  let V : Int := ((L * L : Nat) : Int)
  let S : Int := sumTorus L f
  let Q : Int := sumTorus L (fun x y => f x y * f x y)
  have hVpos : 0 < V := by
    dsimp [V]
    have hmul : 0 < L * L := Nat.mul_pos hL hL
    omega
  have hsqNonneg : 0 ≤ sumTorus L (fun x y => (V * f x y - S) * (V * f x y - S)) :=
    sumTorus_nonneg L _ fun x y => int_sq_nonneg _
  have hexpand :
      (fun x y => (V * f x y - S) * (V * f x y - S)) =
      (fun x y => (V * V * (f x y * f x y) - (2 * V * S) * f x y) + S * S) := by
    funext x y
    have h1 : (V * f x y - S) * (V * f x y - S) =
        (V * f x y) * (V * f x y) - (V * f x y) * S - S * (V * f x y) + S * S := by
      rw [Int.sub_mul, Int.mul_sub, Int.mul_sub]
      omega
    have h2 : (V * f x y) * (V * f x y) = V * V * (f x y * f x y) := by ac_rfl
    have h3 : (V * f x y) * S + S * (V * f x y) = (2 * V * S) * f x y := by
      have ha : (V * f x y) * S = V * S * f x y := by ac_rfl
      have hb : S * (V * f x y) = V * S * f x y := by ac_rfl
      have hc : (2 * V * S) * f x y = 2 * (V * S * f x y) := by ac_rfl
      omega
    omega
  rw [hexpand, sumTorus_add, sumTorus_sub, sumTorus_mul_left, sumTorus_mul_left, sumTorus_const] at hsqNonneg
  change 0 ≤ V * V * Q - 2 * V * S * S + ((L * L : Nat) : Int) * (S * S) at hsqNonneg
  have hVsum : ((L * L : Nat) : Int) = V := rfl
  have hNatCast : ((L * L : Nat) : Int) = (L : Int) * (L : Int) := Int.natCast_mul L L
  have hfactor : V * V * Q - 2 * V * S * S + V * (S * S) = V * (V * Q - S * S) := by
    have ha : V * (V * Q - S * S) = V * (V * Q) - V * (S * S) := Int.mul_sub _ _ _
    have hb : V * (V * Q) = V * V * Q := by ac_rfl
    have hc : 2 * V * S * S = 2 * (V * (S * S)) := by ac_rfl
    omega
  rw [hVsum, hfactor] at hsqNonneg
  by_cases hlt : V * Q - S * S < 0
  · have hneg : V * (V * Q - S * S) < 0 := Int.mul_neg_of_pos_of_neg hVpos hlt
    omega
  · have hge : 0 ≤ V * Q - S * S := by omega
    have hres : S * S ≤ V * Q := by omega
    simpa [V, S, Q, hNatCast] using hres

/-! ## Section 3: 2D Lattice U(1) Gauge Field, Bianchi Identity & Chern Quantization (Theorem G1) -/

structure GaugeField2D (L : Nat) where
  ax : Fin L → Fin L → Int
  ay : Fin L → Fin L → Int

def addField {L : Nat} (A B : GaugeField2D L) : GaugeField2D L :=
  { ax := fun x y => A.ax x y + B.ax x y
  , ay := fun x y => A.ay x y + B.ay x y }

def negField {L : Nat} (A : GaugeField2D L) : GaugeField2D L :=
  { ax := fun x y => -(A.ax x y)
  , ay := fun x y => -(A.ay x y) }

def rawPlaq {L : Nat} (hL : 0 < L) (A : GaugeField2D L) (x y : Fin L) : Int :=
  A.ax x y + A.ay (nextIdx hL x) y - A.ax x (nextIdx hL y) - A.ay x y

def plaqAngle {L : Nat} (hL : 0 < L) (H : Nat) (A : GaugeField2D L) (x y : Fin L) : Int :=
  wrap H (rawPlaq hL A x y)

def plaqWind {L : Nat} (hL : 0 < L) (H : Nat) (A : GaugeField2D L) (x y : Fin L) : Int :=
  wind H (rawPlaq hL A x y)

def topoCharge {L : Nat} (hL : 0 < L) (H : Nat) (A : GaugeField2D L) : Int :=
  -sumTorus L (fun x y => plaqWind hL H A x y)

def quadAction {L : Nat} (hL : 0 < L) (H : Nat) (A : GaugeField2D L) : Int :=
  sumTorus L (fun x y => plaqAngle hL H A x y * plaqAngle hL H A x y)

theorem rawPlaq_add {L : Nat} (hL : 0 < L) (A B : GaugeField2D L) (x y : Fin L) :
    rawPlaq hL (addField A B) x y = rawPlaq hL A x y + rawPlaq hL B x y := by
  unfold rawPlaq addField
  dsimp only
  omega

/-- Discrete Bianchi identity on T_L^2: sum of raw plaquettes vanishes identically for every L >= 1. -/
theorem rawPlaq_torus_sum_zero {L : Nat} (hL : 0 < L) (A : GaugeField2D L) :
    sumTorus L (fun x y => rawPlaq hL A x y) = 0 := by
  have hsplit :
      (fun x y => rawPlaq hL A x y) =
      (fun x y => (A.ay (nextIdx hL x) y - A.ay x y) - (A.ax x (nextIdx hL y) - A.ax x y)) := by
    funext x y
    unfold rawPlaq
    omega
  rw [hsplit, sumTorus_sub, sumTorus_telescoping_x hL A.ay, sumTorus_telescoping_y hL A.ax]
  omega

/-- Theorem G1 (Integer Chern Quantization): For any L >= 1 and H > 0, sum of compact plaquette
    angles equals an exact integer multiple of `period H`. -/
theorem topoCharge_exact_chern {L : Nat} (hL : 0 < L) {H : Nat} (hH : 0 < H)
    (A : GaugeField2D L) :
    sumTorus L (fun x y => plaqAngle hL H A x y) = period H * topoCharge hL H A := by
  have hdec :
      (fun x y => rawPlaq hL A x y) =
      (fun x y => plaqAngle hL H A x y + period H * plaqWind hL H A x y) := by
    funext x y
    unfold plaqAngle plaqWind
    have hd := wrap_decomp hH (rawPlaq hL A x y)
    rw [Int.mul_comm (wind H (rawPlaq hL A x y)) (period H)] at hd
    exact hd
  have hsum : sumTorus L (fun x y => rawPlaq hL A x y) =
      sumTorus L (fun x y => plaqAngle hL H A x y) +
      period H * sumTorus L (fun x y => plaqWind hL H A x y) := by
    rw [hdec, sumTorus_add, sumTorus_mul_left]
  rw [rawPlaq_torus_sum_zero hL A] at hsum
  unfold topoCharge
  rw [Int.mul_neg]
  omega

/-! ## Section 4: Exact Local + Periodic-Winding U(1) Gauge Invariance (Theorem G2) -/

def gaugeTransformPeriodic {L : Nat} (hL : 0 < L) (H : Nat)
    (Λ nx ny : Fin L → Fin L → Int) (A : GaugeField2D L) : GaugeField2D L :=
  { ax := fun x y => A.ax x y + (Λ (nextIdx hL x) y - Λ x y) + period H * nx x y
  , ay := fun x y => A.ay x y + (Λ x (nextIdx hL y) - Λ x y) + period H * ny x y }

theorem gaugeTransform_rawPlaq {L : Nat} (hL : 0 < L) (H : Nat)
    (Λ nx ny : Fin L → Fin L → Int) (A : GaugeField2D L) (x y : Fin L) :
    rawPlaq hL (gaugeTransformPeriodic hL H Λ nx ny A) x y =
      rawPlaq hL A x y +
        (nx x y + ny (nextIdx hL x) y - nx x (nextIdx hL y) - ny x y) * period H := by
  unfold rawPlaq gaugeTransformPeriodic
  dsimp only
  have hm1 : period H * nx x y = nx x y * period H := Int.mul_comm _ _
  have hm2 : period H * ny (nextIdx hL x) y = ny (nextIdx hL x) y * period H := Int.mul_comm _ _
  have hm3 : period H * nx x (nextIdx hL y) = nx x (nextIdx hL y) * period H := Int.mul_comm _ _
  have hm4 : period H * ny x y = ny x y * period H := Int.mul_comm _ _
  have hmul :
      (nx x y + ny (nextIdx hL x) y - nx x (nextIdx hL y) - ny x y) * period H =
      nx x y * period H + ny (nextIdx hL x) y * period H -
        nx x (nextIdx hL y) * period H - ny x y * period H := by
    rw [Int.sub_mul, Int.sub_mul, Int.add_mul]
  rw [hm1, hm2, hm3, hm4, hmul]
  omega

/-- Theorem G2A: Compact plaquette angles are pointwise gauge-invariant for arbitrary L >= 1. -/
theorem gaugeTransform_preserves_plaqAngle {L : Nat} (hL : 0 < L) (H : Nat)
    (Λ nx ny : Fin L → Fin L → Int) (A : GaugeField2D L) (x y : Fin L) :
    plaqAngle hL H (gaugeTransformPeriodic hL H Λ nx ny A) x y = plaqAngle hL H A x y := by
  unfold plaqAngle
  rw [gaugeTransform_rawPlaq, wrap_add_mul_period]

/-- Theorem G2B: Both topological charge and quadratic action are strictly gauge-invariant for all L >= 1. -/
theorem gaugeTransform_preserves_topoCharge_and_action {L : Nat} (hL : 0 < L) {H : Nat} (hH : 0 < H)
    (Λ nx ny : Fin L → Fin L → Int) (A : GaugeField2D L) :
    topoCharge hL H (gaugeTransformPeriodic hL H Λ nx ny A) = topoCharge hL H A ∧
    quadAction hL H (gaugeTransformPeriodic hL H Λ nx ny A) = quadAction hL H A := by
  have hpPos : 0 < period H := period_pos hH
  have hpNe : period H ≠ 0 := by omega
  have hang : ∀ x y, plaqAngle hL H (gaugeTransformPeriodic hL H Λ nx ny A) x y = plaqAngle hL H A x y :=
    fun x y => gaugeTransform_preserves_plaqAngle hL H Λ nx ny A x y
  have hsum :
      sumTorus L (fun x y => plaqAngle hL H (gaugeTransformPeriodic hL H Λ nx ny A) x y) =
      sumTorus L (fun x y => plaqAngle hL H A x y) := by
    congr 1; funext x; congr 1; funext y; exact hang x y
  rw [topoCharge_exact_chern hL hH, topoCharge_exact_chern hL hH] at hsum
  have hQ : topoCharge hL H (gaugeTransformPeriodic hL H Λ nx ny A) = topoCharge hL H A := by
    have hsum' : topoCharge hL H (gaugeTransformPeriodic hL H Λ nx ny A) * period H =
        topoCharge hL H A * period H := by
      rw [Int.mul_comm _ (period H), Int.mul_comm _ (period H)]
      exact hsum
    exact Int.eq_of_mul_eq_mul_right hpNe hsum'
  have hAct : quadAction hL H (gaugeTransformPeriodic hL H Λ nx ny A) = quadAction hL H A := by
    unfold quadAction
    congr 1; funext x; congr 1; funext y; rw [hang x y]
  exact ⟨hQ, hAct⟩

/-! ## Section 5: Luscher Admissibility & Topological Freezing No-Go (Theorem G3) -/

/-- Theorem G3A (Luscher Plaquette Admissibility): Any update `A + δA` keeping every plaquette
    strictly inside `(-H, H]` preserves every winding number `plaqWind` and `topoCharge` exactly. -/
theorem luscher_admissibility_preserves_topoCharge {L : Nat} (hL : 0 < L) {H : Nat} (hH : 0 < H)
    (A δA : GaugeField2D L)
    (hAdm : ∀ x y, -(H : Int) < plaqAngle hL H A x y + rawPlaq hL δA x y ∧
                   plaqAngle hL H A x y + rawPlaq hL δA x y ≤ (H : Int)) :
    topoCharge hL H (addField A δA) = topoCharge hL H A := by
  unfold topoCharge
  congr 1
  congr 1
  funext x y
  unfold plaqWind
  rw [rawPlaq_add]
  have hxy := hAdm x y
  exact wind_admissible_step hH (rawPlaq hL A x y) (rawPlaq hL δA x y) hxy.1 hxy.2

theorem rawPlaq_link_bound {L : Nat} (hL : 0 < L) (δA : GaugeField2D L) (ε : Int)
    (hBnd : ∀ x y, -ε ≤ δA.ax x y ∧ δA.ax x y ≤ ε ∧ -ε ≤ δA.ay x y ∧ δA.ay x y ≤ ε)
    (x y : Fin L) :
    -(4 * ε) ≤ rawPlaq hL δA x y ∧ rawPlaq hL δA x y ≤ 4 * ε := by
  unfold rawPlaq
  have h1 := hBnd x y
  have h2 := hBnd (nextIdx hL x) y
  have h3 := hBnd x (nextIdx hL y)
  omega

/-- Theorem G3B (Corridor Freezing Corollary): Uniform small link updates `|δA_μ| ≤ ε` cannot tunnel
    out of a Luscher corridor `-(H) + 4ε < θ_P ≤ H - 4ε`. -/
theorem luscher_link_corridor_preserves_topoCharge {L : Nat} (hL : 0 < L) {H : Nat} (hH : 0 < H)
    (A δA : GaugeField2D L) (ε : Int)
    (hCorr : ∀ x y, -(H : Int) + 4 * ε < plaqAngle hL H A x y ∧
                    plaqAngle hL H A x y ≤ (H : Int) - 4 * ε)
    (hLink : ∀ x y, -ε ≤ δA.ax x y ∧ δA.ax x y ≤ ε ∧ -ε ≤ δA.ay x y ∧ δA.ay x y ≤ ε) :
    topoCharge hL H (addField A δA) = topoCharge hL H A := by
  apply luscher_admissibility_preserves_topoCharge hL hH A δA
  intro x y
  have hc := hCorr x y
  have hb := rawPlaq_link_bound hL δA ε hLink x y
  omega

def iterGaugeFlow {L : Nat} (step : GaugeField2D L → GaugeField2D L) :
    Nat → GaugeField2D L → GaugeField2D L
  | 0, A => A
  | t + 1, A => iterGaugeFlow step t (step A)

/-- Theorem G3C (Iterated Topological Freezing No-Go): Any multi-step local flow preserving
    plaquette admissibility at each step has infinite tunneling time (`Q(t) = Q(0)` for all t). -/
theorem luscher_iter_flow_frozen {L : Nat} (hL : 0 < L) {H : Nat} (hH : 0 < H)
    (step : GaugeField2D L → GaugeField2D L)
    (hStep : ∀ A, ∀ x y, -(H : Int) < plaqAngle hL H A x y + (rawPlaq hL (step A) x y - rawPlaq hL A x y) ∧
                         plaqAngle hL H A x y + (rawPlaq hL (step A) x y - rawPlaq hL A x y) ≤ (H : Int))
    (t : Nat) (A0 : GaugeField2D L) :
    topoCharge hL H (iterGaugeFlow step t A0) = topoCharge hL H A0 := by
  induction t generalizing A0 with
  | zero => rfl
  | succ t ih =>
    unfold iterGaugeFlow
    rw [ih (step A0)]
    unfold topoCharge
    congr 1
    congr 1
    funext x y
    unfold plaqWind
    let δ := rawPlaq hL (step A0) x y - rawPlaq hL A0 x y
    have hsplit : rawPlaq hL (step A0) x y = rawPlaq hL A0 x y + δ := by
      dsimp [δ]
      omega
    rw [hsplit]
    have hs := hStep A0 x y
    exact wind_admissible_step hH (rawPlaq hL A0 x y) δ hs.1 hs.2

/-! ## Section 6: Harmonic 2-Jet Instanton Optimality vs Bare-Core UV Penalty (Theorem G4) -/

/-- Theorem G4A (Topological Sector Quadratic Action Lower Bound):
    Across ALL gauge configurations on T_L^2, `(period H * Q)^2 ≤ L^2 * quadAction`. -/
theorem sector_quadAction_lower_bound {L : Nat} (hL : 0 < L) {H : Nat} (hH : 0 < H)
    (A : GaugeField2D L) :
    (period H * topoCharge hL H A) * (period H * topoCharge hL H A) ≤
      (L * L : Int) * quadAction hL H A := by
  rw [← topoCharge_exact_chern hL hH A]
  exact torus_cauchy_schwarz hL (fun x y => plaqAngle hL H A x y)

def harmonicInstanton (L : Nat) (δ0 : Int) : GaugeField2D L :=
  { ax := fun _ y => -(y.val : Int) * δ0
  , ay := fun x y => if y.val + 1 = L then (L : Int) * (x.val : Int) * δ0 else 0 }

private theorem neg_sub1_mul (a b : Int) : -((a - 1) : Int) * b = -(a * b) + b := by
  rw [Int.neg_mul, Int.sub_mul, Int.one_mul]
  omega

private theorem add1_mul_L (a b c : Int) : a * (b + 1) * c = a * b * c + a * c := by
  rw [Int.mul_add, Int.mul_one, Int.add_mul]

private theorem neg_add1_mul (a b : Int) : -(a + 1) * b = -(a * b) - b := by
  rw [Int.neg_mul, Int.add_mul, Int.one_mul]
  omega

theorem harmonicInstanton_rawPlaq {L : Nat} (hL : 2 ≤ L) (δ0 : Int) (x y : Fin L) :
    rawPlaq (by omega : 0 < L) (harmonicInstanton L δ0) x y =
      if x.val + 1 = L ∧ y.val + 1 = L then δ0 - (L * L : Int) * δ0 else δ0 := by
  unfold rawPlaq harmonicInstanton nextIdx
  dsimp only
  have hx : x.val < L := x.isLt
  have hy : y.val < L := y.isLt
  by_cases hyLast : y.val + 1 = L
  · have hymod : (y.val + 1) % L = 0 := by rw [hyLast, Nat.mod_self]
    have hynext : ¬((y.val + 1) % L + 1 = L) := by rw [hymod]; omega
    have hy1 : (y.val : Int) = (L : Int) - 1 := by omega
    rw [hymod]
    rw [ite_eq_left hyLast, ite_eq_left hyLast]
    by_cases hxLast : x.val + 1 = L
    · have hxmod : (x.val + 1) % L = 0 := by rw [hxLast, Nat.mod_self]
      rw [hxmod, ite_eq_left ⟨hxLast, hyLast⟩]
      simp only [Int.natCast_zero, Int.mul_zero, Int.zero_mul, Int.neg_zero, Int.sub_zero]
      have hx1 : (x.val : Int) = (L : Int) - 1 := by omega
      rw [hy1, hx1, neg_sub1_mul]
      have hmul2 : (L : Int) * ((L : Int) - 1) * δ0 = (L : Int) * (L : Int) * δ0 - (L : Int) * δ0 := by
        rw [Int.mul_sub, Int.mul_one, Int.sub_mul]
      rw [hmul2]
      omega
    · have hxlt : x.val + 1 < L := by omega
      have hxmod : (x.val + 1) % L = x.val + 1 := Nat.mod_eq_of_lt hxlt
      have hand : ¬(x.val + 1 = L ∧ y.val + 1 = L) := by omega
      rw [hxmod, ite_eq_right hand]
      simp only [Int.natCast_zero, Int.neg_zero]
      have hx1 : ((x.val + 1 : Nat) : Int) = (x.val : Int) + 1 := by omega
      rw [hy1, hx1, neg_sub1_mul, add1_mul_L]
      omega
  · have hylt : y.val + 1 < L := by omega
    have hymod : (y.val + 1) % L = y.val + 1 := Nat.mod_eq_of_lt hylt
    have hand : ¬(x.val + 1 = L ∧ y.val + 1 = L) := by omega
    rw [hymod, ite_eq_right hyLast, ite_eq_right hyLast, ite_eq_right hand]
    simp only [Int.add_zero, Int.sub_zero]
    have hy1 : ((y.val + 1 : Nat) : Int) = (y.val : Int) + 1 := by omega
    rw [hy1, neg_add1_mul, Int.neg_mul]
    omega

/-- Theorem G4B (Uniform Harmonic Instanton): For `H = 2 * L^2 * K` and `δ0 = 4 * K` (`L ≥ 2, K > 0`),
    every plaquette angle equals `+δ0`, total charge is `+1`, and it saturates Cauchy-Schwarz! -/
theorem harmonicInstanton_optimal {L K : Nat} (hL : 2 ≤ L) (hK : 0 < K) :
    let hLpos : 0 < L := by omega
    let H := 2 * L * L * K
    let δ0 : Int := 4 * (K : Int)
    let A := harmonicInstanton L δ0
    (∀ x y, plaqAngle hLpos H A x y = δ0) ∧
    topoCharge hLpos H A = 1 ∧
    (L * L : Int) * quadAction hLpos H A = (period H * 1) * (period H * 1) := by
  intro hLpos H δ0 A
  have hH : 0 < H := by
    have hm1 : 0 < 2 * L := Nat.mul_pos (by omega) hLpos
    have hm2 : 0 < 2 * L * L := Nat.mul_pos hm1 hLpos
    exact Nat.mul_pos hm2 hK
  have hPerEq : (L * L : Int) * δ0 = period H := by
    have hHnat : (H : Int) = 2 * (L : Int) * (L : Int) * (K : Int) := rfl
    unfold period
    dsimp [δ0]
    rw [hHnat]
    have h4 : (2 : Int) * (2 * (L : Int) * (L : Int) * (K : Int)) =
        (L : Int) * (L : Int) * (4 * (K : Int)) := by
      have h22 : (4 : Int) = 2 * 2 := rfl
      rw [h22]
      ac_rfl
    exact h4.symm
  have hδ0_pos : 0 < δ0 := by dsimp [δ0]; omega
  have h4le : (4 : Int) ≤ (L * L : Int) := by
    have hnn : 4 ≤ L * L := Nat.mul_le_mul hL hL
    have hc : ((L * L : Nat) : Int) = (L : Int) * (L : Int) := Int.natCast_mul L L
    omega
  have hmul : 4 * δ0 ≤ (L * L : Int) * δ0 := Int.mul_le_mul_of_nonneg_right h4le (by omega)
  have hδ0_le : δ0 ≤ (H : Int) := by
    have hp : period H = 2 * (H : Int) := rfl
    omega
  have hlo : -(H : Int) < δ0 := by omega
  have hx0 : (L - 1) + 1 = L := by omega
  let ix : Fin L := ⟨L - 1, by omega⟩
  have hang : ∀ x y, plaqAngle hLpos H A x y = δ0 := by
    intro x y
    unfold plaqAngle A
    rw [harmonicInstanton_rawPlaq hL δ0 x y]
    split
    · have hsub : δ0 - (L * L : Int) * δ0 = δ0 + (-1 : Int) * period H := by
        rw [hPerEq]
        omega
      rw [hsub]
      exact (wrap_add_mul_decomp hH hlo hδ0_le).1
    · exact wrap_eq_self_of_bounds hH hlo hδ0_le
  have hwind : ∀ x y, plaqWind hLpos H A x y = if x = ix ∧ y = ix then (-1 : Int) else 0 := by
    intro x y
    unfold plaqWind A
    rw [harmonicInstanton_rawPlaq hL δ0 x y]
    have hxEq : (x = ix) ↔ (x.val + 1 = L) := by
      constructor
      · intro h; subst h; simp [ix, hx0]
      · intro h; ext; dsimp [ix]; omega
    have hyEq : (y = ix) ↔ (y.val + 1 = L) := by
      constructor
      · intro h; subst h; simp [ix, hx0]
      · intro h; ext; dsimp [ix]; omega
    by_cases hc : x = ix ∧ y = ix
    · have hxL : x.val + 1 = L := hxEq.mp hc.1
      have hyL : y.val + 1 = L := hyEq.mp hc.2
      rw [ite_eq_left ⟨hxL, hyL⟩, ite_eq_left hc]
      have hsub2 : δ0 - (L * L : Int) * δ0 = δ0 + (-1 : Int) * period H := by
        rw [hPerEq]
        omega
      rw [hsub2]
      exact (wrap_add_mul_decomp hH hlo hδ0_le).2
    · have hrc : ¬(x.val + 1 = L ∧ y.val + 1 = L) := by
        intro ⟨h1, h2⟩
        exact hc ⟨hxEq.mpr h1, hyEq.mpr h2⟩
      rw [ite_eq_right hrc, ite_eq_right hc]
      exact wind_eq_zero_of_bounds hH hlo hδ0_le
  have hQ : topoCharge hLpos H A = 1 := by
    unfold topoCharge
    have hfun : (fun x y => plaqWind hLpos H A x y) =
        (fun x y => if x = ix ∧ y = ix then (-1 : Int) else 0) := by
      funext x y
      exact hwind x y
    rw [hfun, sumTorus_single_hit ix ix (-1)]
    omega
  have hOpt : (L * L : Int) * quadAction hLpos H A = (period H * 1) * (period H * 1) := by
    unfold quadAction
    have hsq : (fun x y => plaqAngle hLpos H A x y * plaqAngle hLpos H A x y) =
        (fun _ _ => δ0 * δ0) := by
      funext x y
      rw [hang x y]
    rw [hsq, sumTorus_const, hPerEq.symm]
    have ha1 : (L : Int) * (L : Int) * ((L : Int) * (L : Int) * (δ0 * δ0)) =
        ((L : Int) * (L : Int) * δ0 * 1) * ((L : Int) * (L : Int) * δ0 * 1) := by
      rw [Int.mul_one]
      ac_rfl
    exact ha1
  exact ⟨hang, hQ, hOpt⟩

def bareCoreInstanton (L : Nat) (H_half : Int) : GaugeField2D L :=
  { ax := fun x y => if x.val = 1 ∧ y.val = 1 then -(2 * H_half) else 0
  , ay := fun x y => if x.val = 1 ∧ (y.val = 0 ∨ y.val = 1) then H_half else 0 }

private theorem mod_succ_ne_one {L v : Nat} (hv : v < L) (hv0 : v ≠ 0) :
    (v + 1) % L ≠ 1 := by
  by_cases hlast : v + 1 = L
  · rw [hlast, Nat.mod_self]; omega
  · have hlt : v + 1 < L := by omega
    rw [Nat.mod_eq_of_lt hlt]; omega

theorem bareCoreInstanton_rawPlaq {L : Nat} (hL : 2 ≤ L) (H_half : Int) (x y : Fin L) :
    rawPlaq (by omega : 0 < L) (bareCoreInstanton L H_half) x y =
      if (x.val = 0 ∨ x.val = 1) ∧ (y.val = 0 ∨ y.val = 1) then
        (if x.val = 1 ∧ y.val = 1 then -(3 * H_half) else H_half)
      else 0 := by
  unfold rawPlaq bareCoreInstanton nextIdx
  dsimp only
  have hy2 : (1 + 1) % L ≠ 1 := mod_succ_ne_one hL (by omega)
  by_cases hx0 : x.val = 0
  · have hx1 : (0 + 1) % L = 1 := Nat.mod_eq_of_lt hL
    by_cases hy0 : y.val = 0
    · simp [hx0, hy0, hx1]
    · by_cases hy1 : y.val = 1
      · simp [hx0, hy1, hx1, hy2]
      · have hy2' : (y.val + 1) % L ≠ 1 := mod_succ_ne_one y.isLt (by omega)
        simp [hx0, hy0, hy1, hy2']
  · by_cases hx1 : x.val = 1
    · by_cases hy0 : y.val = 0
      · have hy1 : (0 + 1) % L = 1 := Nat.mod_eq_of_lt hL
        simp [hx1, hy0, hy2, hy1]
        omega
      · by_cases hy1 : y.val = 1
        · simp [hx1, hy1, hy2]
          omega
        · have hy2' : (y.val + 1) % L ≠ 1 := mod_succ_ne_one y.isLt (by omega)
          simp [hx1, hy0, hy1, hy2', hy2]
    · have hx2' : (x.val + 1) % L ≠ 1 := mod_succ_ne_one x.isLt hx0
      simp [hx0, hx1, hx2']

private theorem sumFin_two_hit {N : Nat} (i0 i1 : Fin N) (hne : i0 ≠ i1) (v0 v1 : Int) :
    sumFin N (fun i => if i = i0 then v0 else if i = i1 then v1 else 0) = v0 + v1 := by
  have hfun :
      (fun i => if i = i0 then v0 else if i = i1 then v1 else 0) =
      (fun i => (if i = i0 then v0 else 0) + (if i = i1 then v1 else 0)) := by
    funext i
    by_cases h0 : i = i0
    · subst h0; simp [hne]
    · by_cases h1 : i = i1
      · subst h1; simp [h0]
      · simp [h0, h1]
  rw [hfun, sumFin_add, sumFin_single_hit, sumFin_single_hit]

private theorem sumTorus_four_hits {L : Nat} (hL : 2 ≤ L) (v : Int) :
    let i0 : Fin L := ⟨0, by omega⟩
    let i1 : Fin L := ⟨1, by omega⟩
    sumTorus L (fun x y => if (x = i0 ∨ x = i1) ∧ (y = i0 ∨ y = i1) then v else 0) = 4 * v := by
  intro i0 i1
  have hne : i0 ≠ i1 := by
    intro heq
    have := congrArg Fin.val heq
    dsimp [i0, i1] at this
    omega
  unfold sumTorus
  have hrow :
      (fun x : Fin L => sumFin L (fun y : Fin L =>
        if (x = i0 ∨ x = i1) ∧ (y = i0 ∨ y = i1) then v else 0)) =
      (fun x : Fin L => if x = i0 then 2 * v else if x = i1 then 2 * v else 0) := by
    funext x
    by_cases hx0 : x = i0
    · subst hx0
      have hinner :
          (fun y : Fin L => if (i0 = i0 ∨ i0 = i1) ∧ (y = i0 ∨ y = i1) then v else 0) =
          (fun y : Fin L => if y = i0 then v else if y = i1 then v else 0) := by
        funext y
        by_cases hy0 : y = i0 <;> by_cases hy1 : y = i1 <;> simp [hy0, hy1]
      rw [hinner, sumFin_two_hit i0 i1 hne v v]
      simp; omega
    · by_cases hx1 : x = i1
      · subst hx1
        have hinner :
            (fun y : Fin L => if (i1 = i0 ∨ i1 = i1) ∧ (y = i0 ∨ y = i1) then v else 0) =
            (fun y : Fin L => if y = i0 then v else if y = i1 then v else 0) := by
          funext y
          by_cases hy0 : y = i0 <;> by_cases hy1 : y = i1 <;> simp [hy0, hy1]
        rw [hinner, sumFin_two_hit i0 i1 hne v v]
        simp [hx0]; omega
      · have hzero :
            (fun y : Fin L => if (x = i0 ∨ x = i1) ∧ (y = i0 ∨ y = i1) then v else 0) =
            (fun _ => (0 : Int)) := by
          funext y
          simp [hx0, hx1]
        rw [hzero, sumFin_zero]
        simp [hx0, hx1]
  rw [hrow, sumFin_two_hit i0 i1 hne (2 * v) (2 * v)]
  omega

/-- Theorem G4C (Bare-Core UV Penalty): The 3-link 2x2 core instanton carries identical topological
    charge `Q = +1`, yet suffers an exact `(L^2 / 4)x` UV quadratic action explosion relative to
    the harmonic 2-Jet instanton for every lattice size `L >= 2`. -/
theorem bare_vs_harmonic_uv_penalty {L K : Nat} (hL : 2 ≤ L) (hK : 0 < K) :
    let hLpos : 0 < L := by omega
    let H := 2 * L * L * K
    let H_half : Int := ((L * L * K : Nat) : Int)
    let δ0 : Int := 4 * (K : Int)
    let Ab := bareCoreInstanton L H_half
    let Ah := harmonicInstanton L δ0
    topoCharge hLpos H Ab = 1 ∧
    topoCharge hLpos H Ah = 1 ∧
    4 * quadAction hLpos H Ab = (L * L : Int) * quadAction hLpos H Ah := by
  intro hLpos H H_half δ0 Ab Ah
  have hH : 0 < H := by
    have hm1 : 0 < 2 * L := Nat.mul_pos (by omega) hLpos
    have hm2 : 0 < 2 * L * L := Nat.mul_pos hm1 hLpos
    exact Nat.mul_pos hm2 hK
  have hHarm := harmonicInstanton_optimal hL hK
  have hH_nat : H = 2 * (L * L * K) := by
    dsimp [H]; rw [Nat.mul_assoc 2 L L, Nat.mul_assoc 2 (L * L) K]
  have hH_two : (H : Int) = 2 * H_half := by
    dsimp [H_half]; rw [hH_nat, Int.natCast_mul]; rfl
  have hPer4 : period H = 4 * H_half := by
    unfold period; rw [hH_two]; omega
  have hHpos : 0 < H_half :=
    Int.natCast_pos.mpr (Nat.mul_pos (Nat.mul_pos hLpos hLpos) hK)
  have hlo : -(H : Int) < H_half := by rw [hH_two]; omega
  have hhi : H_half ≤ (H : Int) := by rw [hH_two]; omega
  let i0 : Fin L := ⟨0, hLpos⟩
  let i1 : Fin L := ⟨1, by omega⟩
  have hAngBare : ∀ x y, plaqAngle hLpos H Ab x y =
      if (x = i0 ∨ x = i1) ∧ (y = i0 ∨ y = i1) then H_half else 0 := by
    intro x y
    unfold plaqAngle Ab
    rw [bareCoreInstanton_rawPlaq hL H_half x y]
    have hx01 : (x = i0 ∨ x = i1) ↔ (x.val = 0 ∨ x.val = 1) := by
      constructor
      · intro h; cases h with | inl h0 => subst h0; exact Or.inl rfl | inr h1 => subst h1; exact Or.inr rfl
      · intro h; cases h with | inl h0 => left; ext; exact h0 | inr h1 => right; ext; exact h1
    have hy01 : (y = i0 ∨ y = i1) ↔ (y.val = 0 ∨ y.val = 1) := by
      constructor
      · intro h; cases h with | inl h0 => subst h0; exact Or.inl rfl | inr h1 => subst h1; exact Or.inr rfl
      · intro h; cases h with | inl h0 => left; ext; exact h0 | inr h1 => right; ext; exact h1
    by_cases hxy : (x = i0 ∨ x = i1) ∧ (y = i0 ∨ y = i1)
    · have hrc : (x.val = 0 ∨ x.val = 1) ∧ (y.val = 0 ∨ y.val = 1) := ⟨hx01.mp hxy.1, hy01.mp hxy.2⟩
      rw [ite_eq_left hrc, ite_eq_left hxy]
      by_cases h11 : x.val = 1 ∧ y.val = 1
      · rw [ite_eq_left h11]
        have hsub : -(3 * H_half) = H_half + (-1 : Int) * period H := by rw [hPer4]; omega
        rw [hsub]
        exact (wrap_add_mul_decomp hH hlo hhi).1
      · rw [ite_eq_right h11]
        exact wrap_eq_self_of_bounds hH hlo hhi
    · have hrc : ¬((x.val = 0 ∨ x.val = 1) ∧ (y.val = 0 ∨ y.val = 1)) := by
        intro ⟨h1, h2⟩
        exact hxy ⟨hx01.mpr h1, hy01.mpr h2⟩
      rw [ite_eq_right hrc, ite_eq_right hxy]
      exact wrap_eq_self_of_bounds hH (by omega) (by omega)
  have hWindBare : ∀ x y, plaqWind hLpos H Ab x y =
      if x = i1 ∧ y = i1 then (-1 : Int) else 0 := by
    intro x y
    unfold plaqWind Ab
    rw [bareCoreInstanton_rawPlaq hL H_half x y]
    have hx1 : (x = i1) ↔ (x.val = 1) := by
      constructor
      · intro h; subst h; rfl
      · intro h; ext; exact h
    have hy1 : (y = i1) ↔ (y.val = 1) := by
      constructor
      · intro h; subst h; rfl
      · intro h; ext; exact h
    by_cases h11 : x = i1 ∧ y = i1
    · have hxv : x.val = 1 := hx1.mp h11.1
      have hyv : y.val = 1 := hy1.mp h11.2
      have hrc : (x.val = 0 ∨ x.val = 1) ∧ (y.val = 0 ∨ y.val = 1) := ⟨Or.inr hxv, Or.inr hyv⟩
      have h11v : x.val = 1 ∧ y.val = 1 := ⟨hxv, hyv⟩
      rw [ite_eq_left hrc, ite_eq_left h11v, ite_eq_left h11]
      have hsub : -(3 * H_half) = H_half + (-1 : Int) * period H := by rw [hPer4]; omega
      rw [hsub]
      exact (wrap_add_mul_decomp hH hlo hhi).2
    · have hrc11 : ¬(x.val = 1 ∧ y.val = 1) := by
        intro ⟨h1, h2⟩
        exact h11 ⟨hx1.mpr h1, hy1.mpr h2⟩
      rw [ite_eq_right h11]
      by_cases hbox : (x.val = 0 ∨ x.val = 1) ∧ (y.val = 0 ∨ y.val = 1)
      · rw [ite_eq_left hbox, ite_eq_right hrc11]
        exact wind_eq_zero_of_bounds hH hlo hhi
      · rw [ite_eq_right hbox]
        exact wind_eq_zero_of_bounds hH (by omega) (by omega)
  have hQb : topoCharge hLpos H Ab = 1 := by
    unfold topoCharge
    have hfun : (fun x y => plaqWind hLpos H Ab x y) =
        (fun x y => if x = i1 ∧ y = i1 then (-1 : Int) else 0) := by
      funext x y
      exact hWindBare x y
    rw [hfun, sumTorus_single_hit i1 i1 (-1)]
    omega
  have hActBare : quadAction hLpos H Ab = 4 * (H_half * H_half) := by
    unfold quadAction
    have hsq : (fun x y => plaqAngle hLpos H Ab x y * plaqAngle hLpos H Ab x y) =
        (fun x y => if (x = i0 ∨ x = i1) ∧ (y = i0 ∨ y = i1) then H_half * H_half else 0) := by
      funext x y
      rw [hAngBare x y]
      split <;> simp
    rw [hsq]
    exact sumTorus_four_hits hL (H_half * H_half)
  have hScale : 4 * quadAction hLpos H Ab = (L * L : Int) * quadAction hLpos H Ah := by
    rw [hActBare, hHarm.2.2, hPer4, Int.mul_one]
    have h4h : 4 * (4 * (H_half * H_half)) = (4 * H_half) * (4 * H_half) := by ac_rfl
    exact h4h
  exact ⟨hQb, hHarm.2.1, hScale⟩

/-! ## Section 7: Exact Micro-Reversible Involution & Zero-Bias Detailed Balance (Theorem G5) -/

theorem addField_negField_right {L : Nat} (A δ : GaugeField2D L) :
    addField (addField A δ) (negField δ) = A := by
  cases A; cases δ
  unfold addField negField
  simp only [GaugeField2D.mk.injEq]
  constructor <;> funext x y <;> omega

theorem addField_negField_left {L : Nat} (A δ : GaugeField2D L) :
    addField (addField A (negField δ)) δ = A := by
  cases A; cases δ
  unfold addField negField
  simp only [GaugeField2D.mk.injEq]
  constructor <;> funext x y <;> omega

/-- Exact positive rational `(num / den)` over `Int` for division-free Metropolis-Hastings proofs. -/
structure PosRat where
  num : Int
  den : Int
  num_pos : 0 < num
  den_pos : 0 < den

def PosRat.equiv (a b : PosRat) : Prop :=
  a.num * b.den = b.num * a.den

def PosRat.mul (a b : PosRat) : PosRat :=
  { num := a.num * b.num
  , den := a.den * b.den
  , num_pos := Int.mul_pos a.num_pos b.num_pos
  , den_pos := Int.mul_pos a.den_pos b.den_pos }

def PosRat.div (a b : PosRat) : PosRat :=
  { num := a.num * b.den
  , den := a.den * b.num
  , num_pos := Int.mul_pos a.num_pos b.den_pos
  , den_pos := Int.mul_pos a.den_pos b.num_pos }

def PosRat.inv (a : PosRat) : PosRat :=
  { num := a.den
  , den := a.num
  , num_pos := a.den_pos
  , den_pos := a.num_pos }

def PosRat.one : PosRat :=
  { num := 1, den := 1, num_pos := by omega, den_pos := by omega }

def PosRat.minOne (r : PosRat) : PosRat :=
  if r.num ≤ r.den then r else PosRat.one

/-- Theorem G5A (General Invertible-Flow Detailed Balance): For any volume-changing invertible flow
    with Jacobian factor `J` (`J_rev = J.inv`) and target Boltzmann weights `wA, wB > 0`,
    `wA * min(1, (wB / wA) * J)` is identically equal in `Q_{>0}` to `wB * J * min(1, (wA / wB) * J^-1)`. -/
theorem invertible_flow_detailed_balance_exact (wA wB J : PosRat) :
    PosRat.equiv
      (PosRat.mul wA (PosRat.minOne (PosRat.mul (PosRat.div wB wA) J)))
      (PosRat.mul (PosRat.mul wB J) (PosRat.minOne (PosRat.mul (PosRat.div wA wB) (PosRat.inv J)))) := by
  unfold PosRat.equiv PosRat.minOne PosRat.mul PosRat.div PosRat.inv PosRat.one
  dsimp only
  have hSwapL : wB.den * wA.num * J.den = wA.num * wB.den * J.den := by ac_rfl
  have hSwapR : wA.den * wB.num * J.num = wB.num * wA.den * J.num := by ac_rfl
  by_cases hFwd : wB.num * wA.den * J.num ≤ wB.den * wA.num * J.den
  · have hFwdCancel :
        wA.num * (wB.num * wA.den * J.num) * (wB.den * J.den) =
        wB.num * J.num * (wA.den * (wB.den * wA.num * J.den)) := by ac_rfl
    by_cases hRev : wA.num * wB.den * J.den ≤ wA.den * wB.num * J.num
    · have hRevEq : wA.num * wB.den * J.den = wA.den * wB.num * J.num := by omega
      simp only [hFwd, hRev, ↓reduceIte]
      calc wA.num * (wB.num * wA.den * J.num) * (wB.den * J.den * (wA.den * wB.num * J.num))
          = (wA.num * (wB.num * wA.den * J.num) * (wB.den * J.den)) * (wA.den * wB.num * J.num) := by
            ac_rfl
        _ = (wB.num * J.num * (wA.den * (wB.den * wA.num * J.den))) * (wA.den * wB.num * J.num) := by
            rw [hFwdCancel]
        _ = wB.num * J.num * (wA.num * wB.den * J.den) * (wA.den * (wB.den * wA.num * J.den)) := by
            rw [← hRevEq]
            ac_rfl
    · simp only [hFwd, hRev, ↓reduceIte, Int.mul_one]
      exact hFwdCancel
  · have hRev : wA.num * wB.den * J.den ≤ wA.den * wB.num * J.num := by omega
    simp only [hFwd, hRev, ↓reduceIte, Int.mul_one]
    ac_rfl

/-- Theorem G5B (Unit-Jacobian Specialization `J = 1`): Symmetric translation proposals satisfy exact
    Metropolis detailed balance with `0` sampling bias. -/
theorem metropolis_detailed_balance_exact (wA wB : PosRat) :
    PosRat.equiv
      (PosRat.mul wA (PosRat.minOne (PosRat.div wB wA)))
      (PosRat.mul wB (PosRat.minOne (PosRat.div wA wB))) := by
  have h := invertible_flow_detailed_balance_exact wA wB PosRat.one
  unfold PosRat.equiv PosRat.minOne PosRat.mul PosRat.div PosRat.inv PosRat.one at *
  simpa only [Int.mul_one] using h

end Flow.GaugeBundle
