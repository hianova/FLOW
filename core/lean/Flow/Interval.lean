/-
  Flow.Interval — Constructive 1D/2D Intervals & Snap-Grid Order Algebra
  =======================================================================
  Pure domain-free geometric algebra covering interval unions/intersections,
  snap-grid order preservation, disjoint spans, and 2D axis-aligned boxes
  (zero IO, zero `sorryAx`).
-/
import Flow.Core

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

theorem ordered_span_disjoint (x1 x2 w1 minGap : Int)
    (_hSpace : 0 ≤ minGap) (hOrder : x1 + w1 + minGap ≤ x2) :
    x1 + w1 ≤ x2 ∧ minGap ≤ x2 - (x1 + w1) := by
  omega

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

theorem mirror_coord_dist_sym (x1 x2 : Int) :
    Int.natAbs ((-x1) - (-x2)) = Int.natAbs (x1 - x2) := by
  omega

end Flow.Interval
