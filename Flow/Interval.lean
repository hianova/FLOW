/-
  Flow.Interval — Constructive 1D/2D Intervals, Lattice Geometry & Signed-Distance Bounds
  =======================================================================================
  Pure domain-free geometric algebra covering interval unions/intersections,
  snap-grid order preservation, lattice clearance, smooth-min bounds & 2D boxes.
-/
import Flow.Bundle

namespace Flow.Interval

structure Interval1D where
  lo : Int
  hi : Int
  deriving Repr, DecidableEq

def Interval1D.contains (iv : Interval1D) (x : Int) : Prop :=
  iv.lo ≤ x ∧ x ≤ iv.hi

def Interval1D.union (a b : Interval1D) : Interval1D :=
  { lo := if a.lo ≤ b.lo then a.lo else b.lo
    hi := if a.hi ≥ b.hi then a.hi else b.hi }

def Interval1D.intersect (a b : Interval1D) : Interval1D :=
  { lo := if a.lo ≥ b.lo then a.lo else b.lo
    hi := if a.hi ≤ b.hi then a.hi else b.hi }

def Interval1D.width (a : Interval1D) : Int :=
  a.hi - a.lo

theorem union_encloses_operands (left right : Interval1D) (x : Int)
    (h : left.contains x ∨ right.contains x) :
    (left.union right).contains x := by
  unfold Interval1D.contains Interval1D.union at *
  dsimp
  rcases h with ⟨hl0, hl1⟩ | ⟨hr0, hr1⟩
  · constructor <;> split <;> omega
  · constructor <;> split <;> omega

theorem intersect_tightens_width (a b : Interval1D)
    (hNonEmpty : (a.intersect b).lo ≤ (a.intersect b).hi) :
    0 ≤ (a.intersect b).width ∧
    (a.intersect b).width ≤ a.width ∧
    (a.intersect b).width ≤ b.width := by
  unfold Interval1D.intersect Interval1D.width at *
  dsimp at *
  split at hNonEmpty <;> split at hNonEmpty <;> omega

theorem padded_subspan_bounded (parentLo parentHi pad offset childSpan : Int)
    (hPad : 0 ≤ pad) (hOff : 0 ≤ offset) (hSpan : 0 ≤ childSpan)
    (hFit : 2 * pad + offset + childSpan ≤ parentHi - parentLo) :
    parentLo ≤ parentLo + pad + offset ∧
    parentLo + pad + offset ≤ parentLo + pad + offset + childSpan ∧
    parentLo + pad + offset + childSpan ≤ parentHi - pad ∧
    parentHi - pad ≤ parentHi := by
  omega

theorem midpoint_cut_disjoint (x0 c x1 halfGap : Int)
    (hOrder : x0 + halfGap ≤ c ∧ c + halfGap ≤ x1) (hGap : 1 ≤ halfGap) :
    x0 ≤ c - halfGap ∧ (c - halfGap) < (c + halfGap) ∧ c + halfGap ≤ x1 := by
  omega

inductive ParityKind where
  | even
  | odd
  deriving DecidableEq, Repr

def parityTop (r : Nat) : ParityKind :=
  if r % 2 == 0 then ParityKind.even else ParityKind.odd

def parityBot (r : Nat) : ParityKind :=
  if r % 2 == 0 then ParityKind.odd else ParityKind.even

theorem alternating_parity_sharing (r : Nat) :
    parityTop r = parityBot (r + 1) := by
  unfold parityTop parityBot
  by_cases h0 : r % 2 = 0
  · have h1 : (r + 1) % 2 = 1 := by omega
    simp [h0, h1]
  · have h1 : (r + 1) % 2 = 0 := by omega
    simp [h0, h1]

def snapGrid (x g : Nat) : Nat :=
  (x / g) * g

theorem snap_grid_aligned (x g : Nat) :
    snapGrid x g % g = 0 := by
  unfold snapGrid
  exact Nat.mul_mod_left (x / g) g

theorem snap_grid_monotone (x1 x2 g : Nat) (hLe : x1 ≤ x2) :
    snapGrid x1 g ≤ snapGrid x2 g := by
  unfold snapGrid
  exact Nat.mul_le_mul_right g (Nat.div_le_div_right hLe)

theorem lattice_stride_clearance (k t1 t2 pitch wPad gap : Int)
    (hDiff : k ≤ t2 - t1) (hPitch : 0 ≤ pitch) (hRule : wPad + gap ≤ k * pitch) :
    t1 * pitch + wPad + gap ≤ t2 * pitch := by
  have hMul : k * pitch ≤ (t2 - t1) * pitch := Int.mul_le_mul_of_nonneg_right hDiff hPitch
  rw [Int.sub_mul] at hMul
  omega

theorem lattice_unit_clearance (t1 t2 pitch wPad minGap : Int)
    (hTrack : t1 + 1 ≤ t2) (hPitch : 0 ≤ pitch) (hRule : wPad + minGap ≤ pitch) :
    t1 * pitch + wPad + minGap ≤ t2 * pitch :=
  lattice_stride_clearance 1 t1 t2 pitch wPad minGap (by omega) hPitch (by omega)

theorem lattice_double_clearance (t1 t2 pitch wPad wideGap : Int)
    (hGuard : t1 + 2 ≤ t2) (hPitch : 0 ≤ pitch) (hRule : wPad + wideGap ≤ 2 * pitch) :
    t1 * pitch + wPad + wideGap ≤ t2 * pitch :=
  lattice_stride_clearance 2 t1 t2 pitch wPad wideGap (by omega) hPitch hRule

theorem ordered_span_disjoint (x1 x2 w1 minGap : Int)
    (_hSpace : 0 ≤ minGap) (hOrder : x1 + w1 + minGap ≤ x2) :
    x1 + w1 ≤ x2 ∧ minGap ≤ x2 - (x1 + w1) := by
  omega

theorem symmetric_margin_enclosure (cx cy span enc minEnc : Int)
    (_hNonneg : 0 ≤ minEnc) (hEnc : minEnc ≤ enc) :
    (cx - enc ≤ cx ∧ cx + span ≤ cx + span + enc) ∧
    (cy - enc ≤ cy ∧ cy + span ≤ cy + span + enc) ∧
    (minEnc ≤ cx - (cx - enc) ∧ minEnc ≤ (cx + span + enc) - (cx + span)) := by
  omega

def linearDensityCost (rho iLoad span : Nat) : Nat :=
  rho * iLoad * span

theorem linear_density_monotone (rho iLoad l1 l2 : Nat) (hLen : l1 ≤ l2) :
    linearDensityCost rho iLoad l1 ≤ linearDensityCost rho iLoad l2 := by
  unfold linearDensityCost
  exact Nat.mul_le_mul_left (rho * iLoad) hLen

theorem midpoint_tap_quad_reduction (rUnit jDensity h : Nat) (_hPos : 0 < rUnit * jDensity * h * h) :
    2 * (rUnit * jDensity * h * h) < rUnit * jDensity * (2 * h) * (2 * h) := by
  rw [Flow.Bundle.quad_double_factor (rUnit * jDensity) h]
  omega

theorem positive_eighth_floor (n : Nat) (_hPos : 0 < n) :
    0 < n / 8 + 1 := by
  omega

theorem margin_pullback_safe_gap (gap minGap biasLeft biasRight : Int)
    (hSafe : minGap + biasLeft + biasRight ≤ gap) :
    minGap ≤ gap - biasLeft - biasRight := by
  omega

def smoothMinModel (a b corr : Int) : Int :=
  (if a ≤ b then a else b) - corr

theorem smooth_min_le_operands (a b corr : Int) (hCorr : 0 ≤ corr) :
    smoothMinModel a b corr ≤ a ∧ smoothMinModel a b corr ≤ b := by
  unfold smoothMinModel
  split <;> omega

def halfSpaceClip (rawD dist ndotp : Int) : Int :=
  if dist - ndotp > rawD then dist - ndotp else rawD

theorem half_space_clip_pos (rawD dist ndotp : Int) (hClipped : ndotp < dist) :
    0 < halfSpaceClip rawD dist ndotp := by
  unfold halfSpaceClip
  split <;> omega

def slabClip (rawD dist ndotp slabHalf : Int) : Int :=
  let diff := if ndotp ≥ dist then ndotp - dist else dist - ndotp
  let clipD := diff - slabHalf
  if clipD > rawD then clipD else rawD

theorem slab_clip_pos (rawD dist ndotp slabHalf : Int)
    (hOut : ndotp - dist > slabHalf ∨ dist - ndotp > slabHalf) :
    0 < slabClip rawD dist ndotp slabHalf := by
  unfold slabClip
  dsimp
  rcases hOut with h | h <;> split <;> split <;> omega

theorem max_neg_iff_both_neg (dA dB : Int) :
    (if dA ≥ dB then dA else dB) < 0 ↔ dA < 0 ∧ dB < 0 := by
  split <;> omega

structure Box2D where
  x0 : Int
  y0 : Int
  x1 : Int
  y1 : Int
  deriving Repr, DecidableEq

def boxPts5 (b : Box2D) : List (Int × Int) :=
  [(b.x0, b.y0), (b.x1, b.y0), (b.x1, b.y1), (b.x0, b.y1), (b.x0, b.y0)]

def boxArea (b : Box2D) : Int :=
  (b.x1 - b.x0) * (b.y1 - b.y0)

theorem closed_box_five_pts (b : Box2D) :
    (boxPts5 b).length = 5 ∧ (boxPts5 b).head? = (boxPts5 b).getLast? := by
  simp [boxPts5]

theorem positive_box_area (b : Box2D) (hx : b.x0 < b.x1) (hy : b.y0 < b.y1) :
    0 < boxArea b := by
  unfold boxArea
  exact Int.mul_pos (by omega) (by omega)

theorem even_framed_record_len (nBytes : Nat) :
    (4 + 2 * nBytes) % 2 = 0 := by
  omega

theorem mirror_coord_dist_sym (x1 x2 : Int) :
    Int.natAbs ((-x1) - (-x2)) = Int.natAbs (x1 - x2) := by
  omega

end Flow.Interval
