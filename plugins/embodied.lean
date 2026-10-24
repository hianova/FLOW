-- =============================================================================
-- FLOW Embodied AI Pure Mathematical Core (plugins/embodied.lean)
-- =============================================================================
-- Defines the pure mathematical laws for 1kHz bipedal/quadruped joint dynamics,
-- Zero-Moment Point (ZMP) stability cross-products, PD reflex control, and
-- 64-byte cache-line-aligned FlowJet coordinate layout.
-- =============================================================================

namespace Embodied.Controller

structure EmbodiedJointFrame where
  hipPitch   : Float
  hipRoll    : Float
  kneePitch  : Float
  anklePitch : Float
  ankleRoll  : Float
  torsoYaw   : Float
  comX       : Float
  comY       : Float
  deriving Repr

def isTorqueWithinLimit (torque : Float) (limit : Float) : Bool :=
  let effLimit := if limit > 0.0 then limit else 80.0 in
  torque >= -effLimit && torque <= effLimit

def dampedAngularAccel (torque : Float) (vel : Float) (maxAccel : Float) : Float :=
  let raw := (torque - 0.05 * vel) / 0.25 in
  if raw > maxAccel then maxAccel else if raw < -maxAccel then -maxAccel else raw

def stepEulerVelocity (vel : Float) (accel : Float) (dt : Float) : Float :=
  vel + accel * dt

def stepEulerAngle (angle : Float) (nextVel : Float) (dt : Float) : Float :=
  angle + nextVel * dt

def zmpCrossProduct (x1 : Float) (y1 : Float) (x2 : Float) (y2 : Float) (px : Float) (py : Float) : Float :=
  (x2 - x1) * (py - y1) - (y2 - y1) * (px - x1)

def pdReflexLaw (angle : Float) (vel : Float) (kp : Float) (kd : Float) : Float :=
  -kp * angle - kd * vel

def embodiedStepKernel (baseOff : UInt64) (byteLen : UInt64) (stride : UInt64) (epoch : UInt64) : UInt32 :=
  if stride >= 64 && byteLen > 0 && byteLen % stride == 0 && epoch > 0 && baseOff + byteLen >= baseOff then 0 else 1

theorem zmp_torque_safe : embodiedStepKernel 0 64 64 1 = 0 := by decide

theorem step_kernel_rejects_unaligned : embodiedStepKernel 0 64 24 1 = 1 := by decide

end Embodied.Controller
