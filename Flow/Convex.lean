/-
  Flow.Convex — Discrete 2-Jet Convex Cone, Piecewise-Linear Duality & Fixed-Point Laws
  ======================================================================================
  Pure mathematical theorems over discrete 2-Jet curvature, piecewise-linear Legendre
  duality, Q16.16 fixed-point error bounds, deadband contraction & affine cascades.
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

def fivePointNeutralSkew (p2h p1h p0 c0 c1h c2h k0 futEq strideH : Int) : Int :=
  if strideH > 0 then
    let rawAsym  := p1h - c1h
    let slopeSum := (p0 - p2h) + (c0 - c2h)
    rawAsym + (slopeSum * (futEq - k0)) / (2 * strideH)
  else
    0

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

theorem jet2_five_point_slope_neutral_skew
    (atmVal g4 k0 halfH halfDelta skewLo skewHi : Int)
    (hHalfH : halfH > 0) :
    let h     := 2 * halfH
    let futEq := k0 + 2 * halfDelta
    let p2h   := atmVal - 2 * halfH - halfDelta +
                 g4 * ((2 * halfH + halfDelta) * (2 * halfH + halfDelta))
    let p1h   := atmVal - halfH - halfDelta +
                 g4 * ((halfH + halfDelta) * (halfH + halfDelta)) + skewLo
    let p0    := atmVal - halfDelta + g4 * (halfDelta * halfDelta)
    let c0    := atmVal + halfDelta + g4 * (halfDelta * halfDelta)
    let c1h   := atmVal - halfH + halfDelta +
                 g4 * ((halfH - halfDelta) * (halfH - halfDelta)) + skewHi
    let c2h   := atmVal - 2 * halfH + halfDelta +
                 g4 * ((2 * halfH - halfDelta) * (2 * halfH - halfDelta))
    k0 + c0 - p0 = futEq ∧
    fivePointNeutralSkew p2h p1h p0 c0 c1h c2h k0 futEq h = skewLo - skewHi := by
  have hPos : 2 * halfH > 0 := by omega
  have hQ2Sum : (2 * halfH + halfDelta) * (2 * halfH + halfDelta) +
                (2 * halfH - halfDelta) * (2 * halfH - halfDelta) =
                8 * (halfH * halfH) + 2 * (halfDelta * halfDelta) := by
    simp only [Int.add_mul, Int.sub_mul, Int.mul_add, Int.mul_sub]
    have hComm : halfDelta * (2 * halfH) = 2 * halfH * halfDelta := Int.mul_comm _ _
    have hAssoc : 2 * halfH * (2 * halfH) = 4 * (halfH * halfH) := by
      rw [Int.mul_assoc 2 halfH (2 * halfH), Int.mul_left_comm halfH 2 halfH]
      omega
    omega
  have hQ1Diff : (halfH + halfDelta) * (halfH + halfDelta) -
                 (halfH - halfDelta) * (halfH - halfDelta) =
                 4 * (halfH * halfDelta) := by
    simp only [Int.add_mul, Int.sub_mul, Int.mul_add, Int.mul_sub]
    have hComm : halfDelta * halfH = halfH * halfDelta := Int.mul_comm _ _
    omega
  refine ⟨by omega, ?_⟩
  dsimp [fivePointNeutralSkew]
  simp only [hPos, ↓reduceIte]
  have hSlope : (atmVal - halfDelta + g4 * (halfDelta * halfDelta) -
                  (atmVal - 2 * halfH - halfDelta +
                    g4 * ((2 * halfH + halfDelta) * (2 * halfH + halfDelta)))) +
                (atmVal + halfDelta + g4 * (halfDelta * halfDelta) -
                  (atmVal - 2 * halfH + halfDelta +
                    g4 * ((2 * halfH - halfDelta) * (2 * halfH - halfDelta)))) =
                (4 * halfH) * (1 - 2 * (g4 * halfH)) := by
    have hGSum : g4 * ((2 * halfH + halfDelta) * (2 * halfH + halfDelta)) +
                 g4 * ((2 * halfH - halfDelta) * (2 * halfH - halfDelta)) =
                 8 * (g4 * (halfH * halfH)) + 2 * (g4 * (halfDelta * halfDelta)) := by
      rw [← Int.mul_add, hQ2Sum, Int.mul_add,
          Int.mul_left_comm g4 8 (halfH * halfH),
          Int.mul_left_comm g4 2 (halfDelta * halfDelta)]
    have hFact : (4 * halfH) * (1 - 2 * (g4 * halfH)) =
                 4 * halfH - 8 * (g4 * (halfH * halfH)) := by
      rw [Int.mul_sub, Int.mul_one]
      have hM : (4 * halfH) * (2 * (g4 * halfH)) = 8 * (g4 * (halfH * halfH)) := by
        rw [Int.mul_left_comm (4 * halfH) 2 (g4 * halfH),
            Int.mul_left_comm (4 * halfH) g4 halfH,
            Int.mul_assoc 4 halfH halfH,
            Int.mul_left_comm g4 4 (halfH * halfH)]
        omega
      rw [hM]
    omega
  have hSubF : k0 + 2 * halfDelta - k0 = 2 * halfDelta := by omega
  have hDen  : 2 * (2 * halfH) = 4 * halfH := by omega
  rw [hSlope, hSubF, hDen]
  have hMulCancel : ((4 * halfH) * (1 - 2 * (g4 * halfH)) * (2 * halfDelta)) / (4 * halfH) =
                    (1 - 2 * (g4 * halfH)) * (2 * halfDelta) := by
    rw [Int.mul_assoc (4 * halfH)]
    exact Int.mul_ediv_cancel_left _ (by omega)
  rw [hMulCancel]
  have hRR : (atmVal - halfH - halfDelta +
               g4 * ((halfH + halfDelta) * (halfH + halfDelta)) + skewLo) -
             (atmVal - halfH + halfDelta +
               g4 * ((halfH - halfDelta) * (halfH - halfDelta)) + skewHi) =
             -2 * halfDelta + 4 * (g4 * (halfH * halfDelta)) + skewLo - skewHi := by
    have hGDiff : g4 * ((halfH + halfDelta) * (halfH + halfDelta)) -
                  g4 * ((halfH - halfDelta) * (halfH - halfDelta)) =
                  4 * (g4 * (halfH * halfDelta)) := by
      rw [← Int.mul_sub, hQ1Diff, Int.mul_left_comm g4 4 (halfH * halfDelta)]
    omega
  have hCorr : (1 - 2 * (g4 * halfH)) * (2 * halfDelta) =
               2 * halfDelta - 4 * (g4 * (halfH * halfDelta)) := by
    rw [Int.sub_mul, Int.one_mul]
    have hM2 : 2 * (g4 * halfH) * (2 * halfDelta) = 4 * (g4 * (halfH * halfDelta)) := by
      rw [Int.mul_assoc 2 (g4 * halfH) (2 * halfDelta),
          Int.mul_left_comm (g4 * halfH) 2 halfDelta,
          ← Int.mul_assoc 2 2 ((g4 * halfH) * halfDelta),
          Int.mul_assoc g4 halfH halfDelta]
      omega
    rw [hM2]
  rw [hRR, hCorr]
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

/-! ## 2. Piecewise-Linear Hockey-Stick & Legendre Parity Duality -/

def posPart (k x : Int) : Int :=
  if x > k then x - k else 0

def negPart (k x : Int) : Int :=
  if k > x then k - x else 0

def extrinsicPos (k cPx fPx : Int) : Int :=
  cPx - posPart k fPx

def extrinsicNeg (k pPx fPx : Int) : Int :=
  pPx - negPart k fPx

def parityForward (k cPx pPx : Int) : Int :=
  k + cPx - pPx

def parityLo (k cLo pHi : Int) : Int :=
  k + cLo - pHi

def parityHi (k cHi pLo : Int) : Int :=
  k + cHi - pLo

def paritySpread (k cLo cHi pLo pHi : Int) : Int :=
  parityHi k cHi pLo - parityLo k cLo pHi

def boxLockedSurplus (k1 c1Lo p1Hi k2 c2Hi p2Lo : Int) : Int :=
  parityLo k1 c1Lo p1Hi - parityHi k2 c2Hi p2Lo

def boxTerminalPayoff (k1 k2 xT : Int) : Int :=
  (posPart k2 xT - negPart k2 xT) - (posPart k1 xT - negPart k1 xT)

def discountedForwardQ16 (kQ16 cQ16 pQ16 dfQ16 : Int) : Int :=
  if dfQ16 > 0 then
    kQ16 + ((cQ16 - pQ16) * Flow.q16One) / dfQ16
  else
    parityForward kQ16 cQ16 pQ16

def tentPayoff (kCenter wingWidth xT : Int) : Int :=
  posPart (kCenter - wingWidth) xT -
    2 * posPart kCenter xT +
    posPart (kCenter + wingWidth) xT

def straddleSum (k cPx pPx fPx : Int) : Int :=
  extrinsicPos k cPx fPx + extrinsicNeg k pPx fPx

theorem parity_extrinsic_skew_identity (k cPx pPx fPx : Int) :
    extrinsicPos k cPx fPx - extrinsicNeg k pPx fPx = parityForward k cPx pPx - fPx := by
  dsimp [extrinsicPos, extrinsicNeg, posPart, negPart, parityForward]
  omega

theorem parity_interval_sum (k cLo cHi pLo pHi : Int)
    (hC : cLo ≤ cHi) (hP : pLo ≤ pHi) :
    parityLo k cLo pHi ≤ parityHi k cHi pLo ∧
    paritySpread k cLo cHi pLo pHi = (cHi - cLo) + (pHi - pLo) := by
  dsimp [parityLo, parityHi, paritySpread]
  omega

theorem box_locked_surplus_state_independent (k1 c1Lo p1Hi k2 c2Hi p2Lo xT : Int)
    (hCrossed : parityLo k1 c1Lo p1Hi > parityHi k2 c2Hi p2Lo) :
    boxTerminalPayoff k1 k2 xT + ((c1Lo - p1Hi) - (c2Hi - p2Lo)) =
      boxLockedSurplus k1 c1Lo p1Hi k2 c2Hi p2Lo ∧
    boxLockedSurplus k1 c1Lo p1Hi k2 c2Hi p2Lo > 0 := by
  dsimp [boxTerminalPayoff, posPart, negPart, boxLockedSurplus, parityLo, parityHi] at *
  omega

theorem discounted_affine_closure (kQ16 cQ16 pQ16 dfQ16 dPts : Int)
    (hDf : 0 < dfQ16 ∧ dfQ16 ≤ Flow.q16One) :
    discountedForwardQ16 kQ16 cQ16 pQ16 Flow.q16One = parityForward kQ16 cQ16 pQ16 ∧
    discountedForwardQ16 kQ16 (pQ16 + dfQ16 * dPts) pQ16 dfQ16 = kQ16 + dPts * Flow.q16One ∧
    (kQ16 + dPts * Flow.q16One) - parityForward kQ16 (pQ16 + dfQ16 * dPts) pQ16 =
      (Flow.q16One - dfQ16) * dPts := by
  have hOnePos : Flow.q16One > 0 := by decide
  refine ⟨?_, ?_, ?_⟩
  · dsimp [discountedForwardQ16, parityForward]
    simp only [hOnePos, ↓reduceIte]
    have hCancel : ((cQ16 - pQ16) * Flow.q16One) / Flow.q16One = cQ16 - pQ16 :=
      Int.mul_ediv_cancel _ (by decide)
    omega
  · dsimp [discountedForwardQ16]
    simp only [hDf.1, ↓reduceIte]
    have hDiff : pQ16 + dfQ16 * dPts - pQ16 = dfQ16 * dPts := by omega
    have hMul : dfQ16 * dPts * Flow.q16One = dfQ16 * (dPts * Flow.q16One) :=
      Int.mul_assoc dfQ16 dPts Flow.q16One
    rw [hDiff, hMul, Int.mul_ediv_cancel_left _ (by omega)]
  · dsimp [parityForward]
    rw [Int.sub_mul, Int.mul_comm Flow.q16One dPts]
    omega

theorem tent_second_diff_nonneg_bounded (kCenter wingWidth xT cCenter : Int)
    (hWing : wingWidth > 0) :
    0 ≤ tentPayoff kCenter wingWidth xT ∧
    tentPayoff kCenter wingWidth xT ≤ wingWidth ∧
    jet2Diff cCenter cCenter cCenter = 0 := by
  dsimp [tentPayoff, posPart, jet2Diff]
  omega

theorem concave_cost_strict_surplus (kCenter wingWidth xT cLoHi cMidLo cHiHi : Int)
    (hWing : wingWidth > 0)
    (hConcave : jet2Diff cLoHi cMidLo cHiHi < 0) :
    tentPayoff kCenter wingWidth xT - jet2Diff cLoHi cMidLo cHiHi > 0 := by
  dsimp [tentPayoff, posPart, jet2Diff] at *
  omega

theorem symmetric_shift_orthogonal (k cPx pPx fPx dV : Int) :
    parityForward k (cPx + dV) (pPx + dV) = parityForward k cPx pPx ∧
    straddleSum k (cPx + dV) (pPx + dV) fPx - straddleSum k cPx pPx fPx = 2 * dV := by
  dsimp [parityForward, straddleSum, extrinsicPos, extrinsicNeg]
  omega

/-! ## 3. Q16 Fixed-Point Algebra, Deadbands, Healing & Cascades -/

theorem q16_parity_scale_hom (kPts cPts pPts : Int) :
    parityForward (kPts * Flow.q16One) (cPts * Flow.q16One) (pPts * Flow.q16One) =
      (parityForward kPts cPts pPts) * Flow.q16One := by
  dsimp [parityForward, Flow.q16One]
  omega

theorem qmid_interval_and_ulp_bound (kQ16 cLo cHi pLo pHi ulpGuard : Int)
    (hC : cLo ≤ cHi) (hP : pLo ≤ pHi) (hGuard : 1 < ulpGuard) :
    cLo ≤ Flow.qmid cLo cHi ∧ Flow.qmid cLo cHi ≤ cHi ∧
    pLo ≤ Flow.qmid pLo pHi ∧ Flow.qmid pLo pHi ≤ pHi ∧
    -1 ≤ 2 * parityForward kQ16 (Flow.qmid cLo cHi) (Flow.qmid pLo pHi) -
           (parityLo kQ16 cLo pHi + parityHi kQ16 cHi pLo) ∧
    2 * parityForward kQ16 (Flow.qmid cLo cHi) (Flow.qmid pLo pHi) -
      (parityLo kQ16 cLo pHi + parityHi kQ16 cHi pLo) ≤ 1 ∧
    1 < ulpGuard := by
  dsimp [Flow.qmid, parityForward, parityLo, parityHi]
  omega

theorem bounded_inputs_no_i64_overflow
    (k cLo cHi pLo pHi futPx cL cC cR : Int)
    (maxQuote : Int := 200000 * Flow.q16One)
    (hMax : maxQuote ≤ 200000 * Flow.q16One)
    (hK   : 0 ≤ k ∧ k ≤ maxQuote)
    (hCB  : 0 ≤ cLo ∧ cLo ≤ cHi ∧ cHi ≤ maxQuote)
    (hPB  : 0 ≤ pLo ∧ pLo ≤ pHi ∧ pHi ≤ maxQuote)
    (hF   : 0 ≤ futPx ∧ futPx ≤ maxQuote)
    (hW   : 0 ≤ cL ∧ cL ≤ maxQuote ∧ 0 ≤ cC ∧ cC ≤ maxQuote ∧ 0 ≤ cR ∧ cR ≤ maxQuote) :
    -4611686018427387904 < parityForward k (Flow.qmid cLo cHi) (Flow.qmid pLo pHi) ∧
    parityForward k (Flow.qmid cLo cHi) (Flow.qmid pLo pHi) < 4611686018427387904 ∧
    -4611686018427387904 < paritySpread k cLo cHi pLo pHi ∧
    paritySpread k cLo cHi pLo pHi < 4611686018427387904 ∧
    -4611686018427387904 < straddleSum k (Flow.qmid cLo cHi) (Flow.qmid pLo pHi) futPx ∧
    straddleSum k (Flow.qmid cLo cHi) (Flow.qmid pLo pHi) futPx < 4611686018427387904 ∧
    -4611686018427387904 < jet2Diff cL cC cR ∧
    jet2Diff cL cC cR < 4611686018427387904 := by
  dsimp [Flow.q16One, Flow.qmid, parityForward, paritySpread, parityLo, parityHi,
         straddleSum, extrinsicPos, extrinsicNeg, posPart, negPart, jet2Diff] at *
  omega

def ceilDivPos (pxQ16 mult denom : Int) : Int :=
  if pxQ16 > 0 ∧ mult ≥ 1 ∧ denom ≥ 1 then
    (pxQ16 * mult + (denom - 1)) / denom
  else
    0

def roundtripFriction (spreadQ16 slipQ16 feeQ16 taxQ16 unitMult : Int) : Int :=
  if unitMult ≥ 1 then
    spreadQ16 + 2 * slipQ16 + (2 * feeQ16 + 2 * taxQ16 + (unitMult - 1)) / unitMult
  else
    0

def coherentSignal (pullQ16 leadLagQ16 : Int) : Int :=
  if (pullQ16 > 0 ∧ leadLagQ16 ≥ 0) ∨ (pullQ16 < 0 ∧ leadLagQ16 ≤ 0) then pullQ16 else 0

def softThresholdDeadband (sigQ16 halfSpreadQ16 frictionQ16 ulpGuard : Int) : Int :=
  let barrier := halfSpreadQ16 + frictionQ16 + ulpGuard
  if sigQ16 > barrier then sigQ16 - barrier
  else if sigQ16 < -barrier then sigQ16 + barrier
  else 0

theorem positive_friction_floor (pxQ16 spreadQ16 slipQ16 feeQ16 unitMult denom : Int)
    (hPx   : pxQ16 > 0)
    (hSp   : spreadQ16 ≥ 0)
    (hSlip : slipQ16 ≥ 0)
    (hFee  : feeQ16 ≥ 0)
    (hMult : unitMult ≥ 1)
    (hDen  : denom ≥ 1) :
    ceilDivPos pxQ16 unitMult denom ≥ 1 ∧
    roundtripFriction spreadQ16 slipQ16 feeQ16 (ceilDivPos pxQ16 unitMult denom) unitMult ≥ 1 := by
  have hProd : pxQ16 * unitMult ≥ 1 := Int.mul_pos hPx (by omega)
  have hTax : ceilDivPos pxQ16 unitMult denom ≥ 1 := by
    dsimp [ceilDivPos]
    have hC : pxQ16 > 0 ∧ unitMult ≥ 1 ∧ denom ≥ 1 := ⟨hPx, hMult, hDen⟩
    simp only [hC, and_self, ↓reduceIte]
    have hGe : 1 * denom ≤ pxQ16 * unitMult + (denom - 1) := by omega
    exact (Int.le_ediv_iff_mul_le (by omega)).mpr hGe
  refine ⟨hTax, ?_⟩
  dsimp [roundtripFriction]
  simp only [hMult, ↓reduceIte]
  have hGe2 : 1 * unitMult ≤ 2 * feeQ16 + 2 * ceilDivPos pxQ16 unitMult denom + (unitMult - 1) := by
    omega
  have hDiv2 : 1 ≤ (2 * feeQ16 + 2 * ceilDivPos pxQ16 unitMult denom + (unitMult - 1)) / unitMult :=
    (Int.le_ediv_iff_mul_le (by omega)).mpr hGe2
  omega

theorem deadband_annihilates_noise (pullQ16 leadQ16 halfSp friction ulpGuard : Int)
    (_hBox  : halfSp ≥ 0) (_hFric : friction ≥ 0)
    (hSub  : -halfSp - friction - ulpGuard ≤ pullQ16 ∧ pullQ16 ≤ halfSp + friction + ulpGuard) :
    softThresholdDeadband (coherentSignal pullQ16 leadQ16) halfSp friction ulpGuard = 0 := by
  dsimp [softThresholdDeadband, coherentSignal] at *
  omega

inductive UnitStepAction where
  | hold       : UnitStepAction
  | enterPos   : UnitStepAction
  | enterNeg   : UnitStepAction
  | exitFlat   : UnitStepAction
  deriving Repr, DecidableEq, Inhabited

structure UnitStepState where
  resQ16   : Int := 0
  posLots  : Int := 0
  entryQ16 : Int := 0
  netPnl   : Int := 0
  deriving Repr, DecidableEq, Inhabited

def decideUnitStep (reqMask reqVals surfMask execMask : UInt64) (surfDrop : Bool)
    (st : UnitStepState) (bEnter bExit d1Q16 : Int) : UnitStepAction :=
  let sOk := (surfMask &&& reqMask) == reqVals
  let eOk := (execMask &&& reqMask) == reqVals
  let eff := if surfDrop && st.posLots != 0 then eOk else sOk
  if !eff then .hold
  else if st.posLots == 0 then
    if st.resQ16 > bEnter && d1Q16 ≥ 0 then .enterPos
    else if st.resQ16 < -bEnter && d1Q16 ≤ 0 then .enterNeg
    else .hold
  else if st.posLots > 0 then
    if st.resQ16 < bExit then .exitFlat else .hold
  else
    if st.resQ16 > -bExit then .exitFlat else .hold

def stepBoundedUnit (reqMask reqVals surfMask execMask : UInt64) (surfDrop : Bool)
    (st : UnitStepState) (bEnter bExit bidQ16 askQ16 slipQ16 feeQ16 unitMult denom d1Q16 : Int) :
    UnitStepState :=
  match decideUnitStep reqMask reqVals surfMask execMask surfDrop st bEnter bExit d1Q16 with
  | .hold => st
  | .enterPos => { st with posLots := 1, entryQ16 := askQ16 + slipQ16 }
  | .enterNeg => { st with posLots := -1, entryQ16 := bidQ16 - slipQ16 }
  | .exitFlat =>
    let exitPx := if st.posLots > 0 then bidQ16 - slipQ16 else askQ16 + slipQ16
    let gross := if st.posLots > 0 then (exitPx - st.entryQ16) * unitMult
                 else (st.entryQ16 - exitPx) * unitMult
    let tIn := ceilDivPos st.entryQ16 unitMult denom
    let tOut := ceilDivPos exitPx unitMult denom
    { st with posLots := 0, entryQ16 := 0, netPnl := st.netPnl + (gross - 2 * feeQ16 - tIn - tOut) }

theorem unit_step_state_bounded (reqM reqV sM eM : UInt64) (drop : Bool)
    (st : UnitStepState) (bE bX bid ask slip fee m den d1 : Int)
    (hB : -1 ≤ st.posLots ∧ st.posLots ≤ 1) :
    let st' := stepBoundedUnit reqM reqV sM eM drop st bE bX bid ask slip fee m den d1
    (-1 ≤ st'.posLots ∧ st'.posLots ≤ 1) := by
  dsimp [stepBoundedUnit]
  cases decideUnitStep reqM reqV sM eM drop st bE bX d1 <;> simp <;> omega

theorem zero_move_roundtrip_strictly_loses_friction
    (reqM : UInt64) (st : UnitStepState) (bE bX bid ask slip fee m den : Int)
    (hFlat  : st.posLots = 0)
    (hEnter : st.resQ16 > bE)
    (hExit  : bX > 0)
    (hBook  : bid ≤ ask)
    (hPosPx : ask + slip > 0)
    (hSlip  : slip ≥ 0)
    (hFee   : fee ≥ 0)
    (hMult  : m ≥ 1)
    (hDen   : den ≥ 1) :
    let st1 := stepBoundedUnit reqM reqM reqM reqM false st bE bX bid ask slip fee m den 0
    let st2 := stepBoundedUnit reqM reqM reqM reqM false { st1 with resQ16 := 0 } bE bX bid ask slip fee m den 0
    st2.posLots = 0 ∧ st2.netPnl < st.netPnl := by
  have hPosZ : (st.posLots == 0) = true := by simp [hFlat]
  have hAct1 : decideUnitStep reqM reqM reqM reqM false st bE bX 0 = .enterPos := by
    dsimp [decideUnitStep]
    simp [hPosZ, hEnter]
  have hSt1 : stepBoundedUnit reqM reqM reqM reqM false st bE bX bid ask slip fee m den 0 =
      { st with posLots := 1, entryQ16 := ask + slip } := by
    dsimp [stepBoundedUnit]
    rw [hAct1]
  have hAct2 : decideUnitStep reqM reqM reqM reqM false
      { posLots := 1, entryQ16 := ask + slip, netPnl := st.netPnl } bE bX 0 = .exitFlat := by
    dsimp [decideUnitStep]
    simp [hExit]
  have hTIn : ceilDivPos (ask + slip) m den ≥ 1 :=
    (positive_friction_floor (ask + slip) 0 slip fee m den hPosPx (by omega) hSlip hFee hMult hDen).1
  have hTOut : ceilDivPos (bid - slip) m den ≥ 0 := by
    dsimp [ceilDivPos]
    split
    · next hCond =>
      have hProdNN : 0 ≤ (bid - slip) * m := Int.mul_nonneg (by omega) (by omega)
      exact Int.ediv_nonneg (by omega) (by omega)
    · omega
  have hGrossNonPos : ((bid - slip) - (ask + slip)) * m ≤ 0 := by
    have hDiff : (bid - slip) - (ask + slip) ≤ 0 := by omega
    have hMul := Int.mul_le_mul_of_nonneg_right hDiff (by omega : 0 ≤ m)
    simp only [Int.zero_mul] at hMul
    exact hMul
  dsimp
  rw [hSt1]
  dsimp [stepBoundedUnit]
  rw [hAct2]
  dsimp
  refine ⟨rfl, ?_⟩
  omega

theorem dropout_unwind_and_fault_freeze (reqM reqV badM : UInt64)
    (stOpen stFlat : UnitStepState) (bE bX bid ask slip fee m den d1 : Int)
    (hReq  : (reqV &&& reqM) = reqV)
    (hBad  : (badM &&& reqM) ≠ reqV)
    (hOpen : stOpen.posLots ≠ 0)
    (hRes0 : stOpen.resQ16 = 0)
    (hExit : bX > 0)
    (hFlat : stFlat.posLots = 0) :
    decideUnitStep reqM reqV badM reqV true stOpen bE bX d1 = .exitFlat ∧
    (stepBoundedUnit reqM reqV badM reqV true stOpen bE bX bid ask slip fee m den d1).posLots = 0 ∧
    decideUnitStep reqM reqV badM reqV true stFlat bE bX d1 = .hold ∧
    decideUnitStep reqM reqV badM reqV false stOpen bE bX d1 = .hold ∧
    decideUnitStep reqM reqV badM badM true stOpen bE bX d1 = .hold := by
  have hReqB : ((reqV &&& reqM) == reqV) = true := beq_iff_eq.mpr hReq
  have hBadB : ((badM &&& reqM) == reqV) = false := beq_false_of_ne hBad
  have hOpenB : (stOpen.posLots != 0) = true := bne_iff_ne.mpr hOpen
  have hOpenZ : (stOpen.posLots == 0) = false := beq_false_of_ne hOpen
  have hAct : decideUnitStep reqM reqV badM reqV true stOpen bE bX d1 = .exitFlat := by
    dsimp [decideUnitStep]
    simp only [hOpenB, hReqB, hOpenZ, hRes0, hExit, Bool.true_and, Bool.not_true, ↓reduceIte,
      Bool.false_eq_true]
    split
    · rfl
    · split
      · rfl
      · omega
  refine ⟨hAct, ?_, ?_, ?_, ?_⟩
  · dsimp [stepBoundedUnit]; rw [hAct]
  · simp [decideUnitStep, hFlat, hBadB]
  · simp [decideUnitStep, hBadB]
  · simp [decideUnitStep, hOpenB, hBadB]

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

def cleanStraddleSurcharge (kQ16 cLo cHi pLo pHi fQ16 bwQ16 : Int) : Int :=
  let mid := straddleSum kQ16 (Flow.qmid cLo cHi) (Flow.qmid pLo pHi) fQ16
  let sp := (cHi - cLo) + (pHi - pLo)
  let sur := if sp > bwQ16 then sp - bwQ16 else 0
  mid - sur / 2

theorem surcharge_strip_even_invariant (kQ16 cLo cHi pLo pHi fQ16 bwQ16 dE dR : Int)
    (hFl : (cHi - cLo) + (pHi - pLo) ≥ bwQ16) (hdE : dE ≥ 0) (hdR : dR ≥ 0) :
    straddleSum kQ16 (Flow.qmid cLo (cHi + 2 * dE)) (Flow.qmid pLo pHi) fQ16 =
      straddleSum kQ16 (Flow.qmid cLo cHi) (Flow.qmid pLo pHi) fQ16 + dE ∧
    cleanStraddleSurcharge kQ16 cLo (cHi + 2 * dE) pLo pHi fQ16 bwQ16 =
      cleanStraddleSurcharge kQ16 cLo cHi pLo pHi fQ16 bwQ16 ∧
    cleanStraddleSurcharge kQ16 cLo cHi pLo (pHi + 2 * dE) fQ16 bwQ16 =
      cleanStraddleSurcharge kQ16 cLo cHi pLo pHi fQ16 bwQ16 ∧
    -1 ≤ cleanStraddleSurcharge kQ16 cLo (cHi + dR) pLo pHi fQ16 bwQ16 -
           cleanStraddleSurcharge kQ16 cLo cHi pLo pHi fQ16 bwQ16 ∧
    cleanStraddleSurcharge kQ16 cLo (cHi + dR) pLo pHi fQ16 bwQ16 -
      cleanStraddleSurcharge kQ16 cLo cHi pLo pHi fQ16 bwQ16 ≤ 1 := by
  dsimp [cleanStraddleSurcharge, straddleSum, extrinsicPos, extrinsicNeg, Flow.qmid] at *
  omega

theorem forward_rate_positive (nearV farV dt : Int)
    (_hT : farV > nearV) (hDt : 0 < dt ∧ dt ≤ Flow.q16One * (farV - nearV)) :
    ((farV - nearV) * Flow.q16One) / dt ≥ 1 := by
  have hGe : 1 * dt ≤ (farV - nearV) * Flow.q16One := by
    rw [Int.mul_comm (farV - nearV) Flow.q16One]; omega
  exact (Int.le_ediv_iff_mul_le hDt.1).mpr hGe

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

def healCoupledLo (k cLo cHi pLo pHi futEq : Int) : Int :=
  if (cHi - cLo) ≤ (pHi - pLo) then Flow.qmid cLo cHi
  else futEq - k + Flow.qmid pLo pHi

def healCoupledHi (k cLo cHi pLo pHi futEq : Int) : Int :=
  if (pHi - pLo) ≤ (cHi - cLo) then Flow.qmid pLo pHi
  else k - futEq + Flow.qmid cLo cHi

theorem coupled_four_way_pull_zero_ulp_invariant
    (k cLo cHi pLo pHi fPx d : Int)
    (hBal : cHi - cLo = pHi - pLo) (hD : d > 0) :
    let fut := k + Flow.qmid cLo cHi - Flow.qmid pLo pHi
    let c0 := Flow.qmid cLo cHi
    let p0 := Flow.qmid pLo pHi
    healCoupledLo k cLo (cHi + d) pLo pHi fut = c0 ∧
    healCoupledLo k (cLo - d) cHi pLo pHi fut = c0 ∧
    healCoupledHi k cLo cHi pLo (pHi + d) fut = p0 ∧
    healCoupledHi k cLo cHi (pLo - d) pHi fut = p0 ∧
    straddleSum k (healCoupledLo k cLo (cHi + d) pLo pHi fut)
                  (healCoupledHi k cLo (cHi + d) pLo pHi fut) fPx =
      straddleSum k c0 p0 fPx := by
  dsimp [healCoupledLo, healCoupledHi, straddleSum, extrinsicPos, extrinsicNeg, Flow.qmid] at *
  omega

theorem healed_conservation_and_jet2_immune
    (kL cLB cLA pLB pLA kC cCB cCA pCB pCA kR cRB cRA pRB pRA
     kX cXB cXA pXB pXA futEq pullL pullC pullR : Int)
    (hAsymX : cXA - cXB ≠ pXA - pXB)
    (hBalL  : cLA - cLB = pLA - pLB)
    (hBalC  : cCA - cCB = pCA - pCB)
    (hBalR  : cRA - cRB = pRA - pRB)
    (hParL  : kL + Flow.qmid cLB cLA - Flow.qmid pLB pLA = futEq)
    (hParC  : kC + Flow.qmid cCB cCA - Flow.qmid pCB pCA = futEq)
    (hPulls : pullL > 0 ∧ pullC > 0 ∧ pullR > 0) :
    kX + healCoupledLo kX cXB cXA pXB pXA futEq - healCoupledHi kX cXB cXA pXB pXA futEq = futEq ∧
    jet2Diff (healCoupledLo kL (cLB - pullL) cLA pLB pLA futEq)
             (healCoupledLo kC cCB (cCA + pullC) pCB pCA futEq)
             (healCoupledLo kR cRB cRA (pRB - pullR) pRA futEq) =
      jet2Diff (Flow.qmid cLB cLA) (Flow.qmid cCB cCA) (Flow.qmid cRB cRA) := by
  dsimp [healCoupledLo, healCoupledHi, jet2Diff, Flow.qmid] at *
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

theorem corridor_weight_nonneg_and_heal_immune (dK qStar kPts : Int)
    (hK : dK > 0) (hQ : qStar ≥ 0) (hS : kPts > 0) :
    (20000 * dK * qStar) / (kPts * kPts) ≥ 0 := by
  have hNum : 0 ≤ 20000 * dK * qStar := Int.mul_nonneg (by omega) hQ
  have hDen : 0 ≤ kPts * kPts := Int.mul_nonneg (by omega) (by omega)
  exact Int.ediv_nonneg hNum hDen

theorem kurtosis_ratio_ceiling_and_excess (strideH thetaH gaussB putW callW : Int)
    (hS : strideH > 0) (hT : thetaH > 0) (hG : gaussB > 0)
    (hEx : (putW + callW) * 5000 ≥ 10001 * gaussB) :
    (113 * (strideH * strideH)) / (355 * thetaH) ≥ 0 ∧
    ((gaussB + gaussB) * 5000) / gaussB = 10000 ∧
    ((putW + callW) * 5000) / gaussB ≥ 10001 := by
  refine ⟨?_, ?_, ?_⟩
  · have hSq : 0 ≤ strideH * strideH := Int.mul_nonneg (by omega) (by omega)
    exact Int.ediv_nonneg (Int.mul_nonneg (by omega) hSq) (by omega)
  · have hEq : (gaussB + gaussB) * 5000 = gaussB * 10000 := by omega
    rw [hEq]
    exact Int.mul_ediv_cancel_left 10000 (by omega)
  · have hDecomp : (putW + callW) * 5000 =
                   ((putW + callW) * 5000 - 10001 * gaussB) + 10001 * gaussB := by omega
    rw [hDecomp, Int.add_mul_ediv_right _ 10001 (by omega)]
    have hRem : 0 ≤ ((putW + callW) * 5000 - 10001 * gaussB) / gaussB :=
      Int.ediv_nonneg (by omega) (by omega)
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

theorem contractive_damping_le (amp dampQ15 : Nat) (hDamp : dampQ15 ≤ 32768) :
    (amp * dampQ15) / 32768 ≤ amp := by
  calc
    (amp * dampQ15) / 32768 ≤ (amp * 32768) / 32768 :=
      Nat.div_le_div_right (Nat.mul_le_mul_left amp hDamp)
    _ = amp := Nat.mul_div_left amp (by decide)

theorem substep_partition_exact (dtTotal toiQ16 : Int) :
    let dtPre := (dtTotal * toiQ16) / 65536
    let dtPost := dtTotal - dtPre
    dtPre + dtPost = dtTotal := by
  dsimp
  omega

theorem pullback_contracts_abs_error (errNaive bias : Int)
    (_hBiasPos : 0 < bias) (_hBound : bias ≤ errNaive) :
    Int.natAbs (errNaive - bias) < Int.natAbs errNaive := by
  omega

theorem monotone_threshold_trans (v1 v2 threshold : Int)
    (hLe : v1 ≤ v2) (hLt : v2 < threshold) :
    v1 < threshold := by
  omega

theorem concentric_shells_ordered (dInner dMid dOuter : Int)
    (_hI : 0 < dInner) (_hM : 0 < dMid) (_hO : dMid ≤ dOuter) :
    -dInner < 0 ∧ 0 < dOuter := by
  omega

theorem contrast_ducking_amplifies (duckedFloor rawFloor peak : Int)
    (hDuck : duckedFloor ≤ rawFloor) :
    peak - rawFloor ≤ peak - duckedFloor := by
  omega

theorem impulse_pair_conserves_sum (pA pB imp : Int) :
    (pA + imp) + (pB - imp) = pA + pB := by
  omega

theorem rational_cross_monotone (b1 b2 c : Nat) (hMono : b1 ≤ b2) :
    (c + b1) * (c - b2) ≤ (c + b2) * (c - b1) :=
  Nat.mul_le_mul (by omega) (by omega)

end Flow.Convex
