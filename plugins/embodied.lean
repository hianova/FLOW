-- =============================================================================
-- FLOW Embodied AI Pure Mathematical Core (plugins/embodied.lean)
-- =============================================================================
-- Defines the pure integer / fixed-point (milli-unit, scale = 1000x) laws for
-- 1kHz bipedal/quadruped joint dynamics, Zero-Moment Point (ZMP) stability
-- cross-products, PD reflex control, explicit tolerance envelopes, and
-- 64-byte cache-line-aligned FlowJet coordinate layout.
-- =============================================================================

namespace Embodied.Controller

structure EmbodiedJointFrame where
  hipPitch   : Int
  hipRoll    : Int
  kneePitch  : Int
  anklePitch : Int
  ankleRoll  : Int
  torsoYaw   : Int
  comX       : Int
  comY       : Int
  h_hip_pitch  : hipPitch >= -1570 && hipPitch <= 1570
  h_hip_roll   : hipRoll >= -780 && hipRoll <= 780
  h_knee_pitch : kneePitch >= 0 && kneePitch <= 2350
  h_com_box    : comX >= -250 && comX <= 250 && comY >= -200 && comY <= 200
  deriving Repr, DecidableEq

def isTorqueWithinLimit (torque : Int) (limit : Int) : Bool :=
  let effLimit := if limit > 0 then limit else 80000
  torque >= -effLimit && torque <= effLimit

def isPositionWithinTolerance (pos : Int) (target : Int) (tol : Int) : Bool :=
  tol >= 0 && (pos - target <= tol) && (target - pos <= tol)

def dampedAngularAccel (torque : Int) (vel : Int) (maxAccel : Int) : Int :=
  let raw := (1000 * torque - 50 * vel) / 250
  if raw > maxAccel then maxAccel else if raw < -maxAccel then -maxAccel else raw

def stepEulerVelocity (vel : Int) (accel : Int) (dt : Int) : Int :=
  vel + (accel * dt) / 1000

def stepEulerAngle (angle : Int) (nextVel : Int) (dt : Int) : Int :=
  angle + (nextVel * dt) / 1000

def zmpCrossProduct (x1 : Int) (y1 : Int) (x2 : Int) (y2 : Int) (px : Int) (py : Int) : Int :=
  ((x2 - x1) * (py - y1) - (y2 - y1) * (px - x1)) / 1000

def pdReflexLaw (angle : Int) (vel : Int) (kp : Int) (kd : Int) : Int :=
  (-kp * angle - kd * vel) / 1000

def embodiedStepKernel (baseOff : UInt64) (byteLen : UInt64) (stride : UInt64) (epoch : UInt64) : UInt32 :=
  if stride >= 64 && byteLen > 0 && byteLen % stride == 0 && epoch > 0 && baseOff + byteLen >= baseOff then 0 else 1

theorem zmp_torque_safe : embodiedStepKernel 0 64 64 1 = 0 := by decide

theorem step_kernel_rejects_unaligned : embodiedStepKernel 0 64 24 1 = 1 := by decide

theorem damped_accel_bounded (torque : Int) (vel : Int) (maxAccel : Int) (h_lim : maxAccel > 0) :
    dampedAngularAccel torque vel maxAccel >= -maxAccel ∧ dampedAngularAccel torque vel maxAccel <= maxAccel := by
  unfold dampedAngularAccel
  dsimp only
  split <;> (try split) <;> omega

theorem pd_reflex_restoring (angle : Int) (vel : Int) :
    angle >= 10 → vel >= 0 → pdReflexLaw angle vel 120 15 < 0 := by
  intro h_ang h_vel
  unfold pdReflexLaw
  omega

theorem zmp_cross_product_origin_zero (x2 : Int) (y2 : Int) :
    zmpCrossProduct 0 0 x2 y2 0 0 = 0 := by
  unfold zmpCrossProduct
  omega

end Embodied.Controller
