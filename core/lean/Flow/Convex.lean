/-
  Flow.Convex — Pure Discrete 2-Jet Convexity, Orthogonal Projection & Fixed-Point Algebra
  =========================================================================================
  Pure domain-free theorems governing discrete 2-Jet second differences, Jensen variance,
  piecewise-linear cones, orthogonal residual splitting, and affine cascades
  (zero IO, zero `sorryAx`).
-/
import Flow.Core

namespace Flow.Convex

/-! ## 1. Discrete 2-Jet Convex Cone & Curvature Scaling -/

def jet2Diff (fMinus fZero fPlus : Int) : Int :=
  fMinus - 2 * fZero + fPlus

theorem jet2_local_min_pos_2d (fU0 fU1 fUM fV0 fV1 fVM : Int)
    (hUx : fU0 < fU1 ∧ fU0 < fUM) (hVx : fV0 < fV1 ∧ fV0 < fVM) :
    0 < jet2Diff fUM fU0 fU1 + jet2Diff fVM fV0 fV1 := by
  unfold jet2Diff
  omega

def wideCurvatureNorm (vOuterLo vMid vOuterHi strideMult : Int) : Int :=
  if strideMult > 0 then
    jet2Diff vOuterLo vMid vOuterHi / (strideMult * strideMult)
  else
    0

def tailCurvatureAsym (cLeft cRight : Int) : Int :=
  cLeft - cRight

theorem jet2_quadratic_stride_snr_and_noise_floor
    (v0 d1 gL gR h m noise : Int)
    (hM     : m > 0)
    (hNoise : 0 ≤ noise ∧ noise < m * m) :
    let bUnitL := jet2Diff (v0 + d1 * (-h) + gL * ((-h) * (-h)))
                           v0
                           (v0 + d1 * h + gL * (h * h))
    let bWideL := jet2Diff (v0 + d1 * (-(m * h)) + gL * ((-(m * h)) * (-(m * h))))
                           v0
                           (v0 + d1 * (m * h) + gL * ((m * h) * (m * h)))
    let bWideR := jet2Diff (v0 + d1 * (-(m * h)) + gR * ((-(m * h)) * (-(m * h))))
                           v0
                           (v0 + d1 * (m * h) + gR * ((m * h) * (m * h)))
    bWideL = (m * m) * bUnitL ∧
    wideCurvatureNorm
      (v0 + d1 * (-(m * h)) + gL * ((-(m * h)) * (-(m * h))) + noise)
      v0
      (v0 + d1 * (m * h) + gL * ((m * h) * (m * h)))
      m = bUnitL ∧
    (gL > gR → h ≠ 0 → tailCurvatureAsym bWideL bWideR > 0) := by
  have hSq1  : (-h) * (-h) = h * h := Int.neg_mul_neg h h
  have hSqM  : (-(m * h)) * (-(m * h)) = (m * h) * (m * h) := Int.neg_mul_neg (m * h) (m * h)
  have hNeg1 : d1 * (-h) = -(d1 * h) := Int.mul_neg d1 h
  have hNegM : d1 * (-(m * h)) = -(d1 * (m * h)) := Int.mul_neg d1 (m * h)
  have hProd : (m * h) * (m * h) = (m * m) * (h * h) := by
    rw [Int.mul_assoc m h (m * h), Int.mul_left_comm h m h, ← Int.mul_assoc m m (h * h)]
  have hGL   : gL * ((m * h) * (m * h)) = (m * m) * (gL * (h * h)) := by
    rw [hProd, Int.mul_left_comm gL (m * m) (h * h)]
  have hGR   : gR * ((m * h) * (m * h)) = (m * m) * (gR * (h * h)) := by
    rw [hProd, Int.mul_left_comm gR (m * m) (h * h)]
  have hM2Pos : m * m > 0 := Int.mul_pos hM hM
  refine ⟨?_, ?_, ?_⟩
  · dsimp [jet2Diff]
    rw [hSq1, hSqM, hNeg1, hNegM, hGL]
    have hBase1 : v0 + -(d1 * h) + gL * (h * h) - 2 * v0 + (v0 + d1 * h + gL * (h * h)) =
                  2 * (gL * (h * h)) := by omega
    rw [hBase1, Int.mul_left_comm (m * m) 2 (gL * (h * h))]
    omega
  · dsimp [wideCurvatureNorm, jet2Diff]
    simp only [hM, ↓reduceIte]
    rw [hSq1, hSqM, hNeg1, hNegM, hGL]
    have hNum : v0 + -(d1 * (m * h)) + (m * m) * (gL * (h * h)) + noise - 2 * v0 +
                (v0 + d1 * (m * h) + (m * m) * (gL * (h * h))) =
                noise + (2 * (gL * (h * h))) * (m * m) := by
      have hComm : 2 * ((m * m) * (gL * (h * h))) = (2 * (gL * (h * h))) * (m * m) := by
        rw [← Int.mul_assoc 2 (m * m) (gL * (h * h)),
            Int.mul_comm 2 (m * m),
            Int.mul_assoc (m * m) 2 (gL * (h * h)),
            Int.mul_comm (m * m) (2 * (gL * (h * h)))]
      omega
    rw [hNum, Int.add_mul_ediv_right noise (2 * (gL * (h * h))) (by omega)]
    have hZero : noise / (m * m) = 0 := Int.ediv_eq_zero_of_lt hNoise.1 hNoise.2
    have hBase : v0 + -(d1 * h) + gL * (h * h) - 2 * v0 + (v0 + d1 * h + gL * (h * h)) =
                 2 * (gL * (h * h)) := by omega
    rw [hZero, hBase]
    omega
  · intro hGt hNe
    dsimp [tailCurvatureAsym, jet2Diff]
    rw [hSqM, hNegM, hGL, hGR]
    have hH2Pos : h * h > 0 := by
      rcases Int.lt_trichotomy h 0 with hLt | rfl | hGtH
      · have hPos : -h > 0 := by omega
        have hP := Int.mul_pos hPos hPos
        rw [hSq1] at hP
        exact hP
      · contradiction
      · exact Int.mul_pos hGtH hGtH
    have hDiffPos : (gL - gR) * ((m * m) * (h * h)) > 0 :=
      Int.mul_pos (by omega) (Int.mul_pos hM2Pos hH2Pos)
    have hSub : gL * ((m * m) * (h * h)) - gR * ((m * m) * (h * h)) =
                (gL - gR) * ((m * m) * (h * h)) := by
      rw [Int.sub_mul]
    have hLComm : (m * m) * (gL * (h * h)) = gL * ((m * m) * (h * h)) :=
      Int.mul_left_comm (m * m) gL (h * h)
    have hRComm : (m * m) * (gR * (h * h)) = gR * ((m * m) * (h * h)) :=
      Int.mul_left_comm (m * m) gR (h * h)
    omega

def positiveGap (hi lo : Int) : Int :=
  if hi > lo ∧ lo ≥ 0 then hi - lo else 0

theorem jensen_discrete_variance_pos (vLow vHigh hiVar loVar delta : Int)
    (hSpread : vLow ≠ vHigh)
    (hLoPos  : loVar > 0)
    (hDelta  : delta ≥ 0)
    (hDom    : loVar + delta < hiVar) :
    2 * (vLow * vLow + vHigh * vHigh) - (vLow + vHigh) * (vLow + vHigh) =
      (vHigh - vLow) * (vHigh - vLow) ∧
    (vHigh - vLow) * (vHigh - vLow) > 0 ∧
    positiveGap hiVar loVar > 0 ∧
    positiveGap hiVar loVar < hiVar ∧
    positiveGap hiVar (loVar + delta) ≤ positiveGap hiVar loVar := by
  have hDiffSqPos : (vHigh - vLow) * (vHigh - vLow) > 0 := by
    rcases Int.lt_trichotomy (vHigh - vLow) 0 with hLt | hEq | hGt
    · have hNeg : -(vHigh - vLow) > 0 := by omega
      have hP := Int.mul_pos hNeg hNeg
      rw [Int.neg_mul_neg] at hP
      exact hP
    · omega
    · exact Int.mul_pos hGt hGt
  refine ⟨?_, hDiffSqPos, ?_, ?_, ?_⟩
  · simp only [Int.add_mul, Int.sub_mul, Int.mul_add, Int.mul_sub]
    have hComm : vHigh * vLow = vLow * vHigh := Int.mul_comm _ _
    omega
  · dsimp [positiveGap]; omega
  · dsimp [positiveGap]; omega
  · dsimp [positiveGap]; omega

/-! ## 2. Piecewise-Linear Hinge Primitives & Generic Ratio Scaling -/

def posPart (k x : Int) : Int :=
  if x > k then x - k else 0

def negPart (k x : Int) : Int :=
  if k > x then k - x else 0

def ceilDivPos (numQ16 mult denom : Int) : Int :=
  if numQ16 > 0 ∧ mult ≥ 1 ∧ denom ≥ 1 then
    (numQ16 * mult + (denom - 1)) / denom
  else
    0

theorem scale_invariant_ratio (scale num den multScale cNum cDen : Int)
    (hScale : scale > 0) :
    let ratio := (num * cNum * multScale) / (den * cDen)
    let scaledRatio := ((scale * num) * cNum * multScale) / ((scale * den) * cDen)
    scaledRatio = ratio := by
  dsimp
  have hL : scale * num * cNum * multScale = scale * (num * cNum * multScale) := by
    rw [Int.mul_assoc scale num cNum, Int.mul_assoc scale (num * cNum) multScale]
  have hR : scale * den * cDen = scale * (den * cDen) := Int.mul_assoc scale den cDen
  rw [hL, hR]
  exact Int.mul_ediv_mul_of_pos _ _ hScale

/-! ## 3. Orthogonal Projection, Kink Extraction & Affine Cascades -/

def orthoResidual (x newBase proj : Int) : Int :=
  if x > 0 ∧ newBase > 0 then (x - newBase) - proj else 0

theorem ortho_projection_strips_drift (x0 proj : Int)
    (hX0 : x0 > 0) (hX1 : x0 + proj > 0) :
    orthoResidual (x0 + proj) x0 proj = 0 := by
  dsimp [orthoResidual]
  simp [hX0, hX1]
  omega

theorem pythagorean_ortho_variance (x x0 proj : Int)
    (hX : x > 0) (hX0 : x0 > 0) (hNZ : proj ≠ 0) :
    let eps := orthoResidual x x0 proj
    let tot := x - x0
    proj * proj > 0 ∧
    proj * proj + eps * eps > eps * eps ∧
    tot * tot - 2 * (proj * eps) = proj * proj + eps * eps := by
  have hPos : proj * proj > 0 := by
    rcases Int.lt_trichotomy proj 0 with hlt | rfl | hgt
    · have h1 := Int.mul_pos (by omega : -proj > 0) (by omega : -proj > 0)
      rw [Int.neg_mul_neg] at h1
      exact h1
    · contradiction
    · exact Int.mul_pos hgt hgt
  dsimp [orthoResidual]
  simp [hX, hX0]
  refine ⟨hPos, by omega, ?_⟩
  have hSplit : x - x0 = proj + (x - x0 - proj) := by omega
  conv => lhs; arg 1; rw [hSplit]
  rw [Int.add_mul, Int.mul_add, Int.mul_add, Int.mul_comm (x - x0 - proj) proj]
  omega

def absDevLoss (x center halfSp : Int) : Int :=
  let err := if x ≥ center then x - center else center - x
  if err > halfSp then err - halfSp else 0

theorem anchored_loss_zero_and_dominant (x0 proj eps halfSp : Int)
    (hO : -halfSp ≤ eps ∧ eps ≤ halfSp)
    (_hD : (if eps ≥ 0 then eps else -eps) ≤ (if proj + eps ≥ 0 then proj + eps else -(proj + eps))) :
    absDevLoss (x0 + proj + eps) (x0 + proj) halfSp = 0 ∧
    absDevLoss (x0 + proj + eps) (x0 + proj) halfSp ≤ absDevLoss (x0 + proj + eps) x0 halfSp := by
  dsimp [absDevLoss] at *
  omega

theorem collinear_kink_exact_extraction (v0 slope kink dt01 dt12 : Int)
    (_hDt01 : dt01 > 0) (hDt12 : dt12 > 0) :
    ((v0 + slope * dt01 + kink) - v0) - ((slope * dt12) * dt01) / dt12 = kink ∧
    ((v0 + slope * dt01) - v0) - ((slope * dt12) * dt01) / dt12 = 0 := by
  have hMul : slope * dt12 * dt01 = dt12 * (slope * dt01) := by
    rw [Int.mul_comm slope dt12, Int.mul_assoc]
  have hDiv : (slope * dt12 * dt01) / dt12 = slope * dt01 := by
    rw [hMul]
    exact Int.mul_ediv_cancel_left (slope * dt01) (by omega)
  rw [hDiv]
  omega

def affineCascade (nearV beta farV : Int) : Int :=
  nearV + beta * farV

theorem affine_cascade_nonneg (nearV beta farV : Int)
    (hN : 0 ≤ nearV) (hB : 0 ≤ beta) (hF : 0 ≤ farV) :
    0 ≤ affineCascade nearV beta farV := by
  unfold affineCascade
  exact Int.add_nonneg hN (Int.mul_nonneg hB hF)

theorem affine_cascade_zero_occludes (nearV farV : Int) :
    affineCascade nearV 0 farV = nearV := by
  unfold affineCascade
  simp

theorem pullback_contracts_abs_error (errNaive bias : Int)
    (_hBiasPos : 0 < bias) (_hBound : bias ≤ errNaive) :
    Int.natAbs (errNaive - bias) < Int.natAbs errNaive := by
  omega

theorem monotone_threshold_trans (v1 v2 threshold : Int)
    (hLe : v1 ≤ v2) (hLt : v2 < threshold) :
    v1 < threshold := by
  omega

theorem impulse_pair_conserves_sum (pA pB imp : Int) :
    (pA + imp) + (pB - imp) = pA + pB := by
  omega

theorem rational_cross_monotone (b1 b2 c : Nat) (hMono : b1 ≤ b2) :
    (c + b1) * (c - b2) ≤ (c + b2) * (c - b1) :=
  Nat.mul_le_mul (by omega) (by omega)

end Flow.Convex
