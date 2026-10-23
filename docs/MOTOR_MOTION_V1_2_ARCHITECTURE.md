# INDICATOR MOTOR MOTION V1.2 — Architecture and Configuration Lock

## Purpose

V1.2 reorganizes the motor block so the proven motion-control core is separated
from all fault, stall, homing, timeout, retry and recovery tuning.

This is an organizational/refactoring release. The proven ESP32 DOIT V1 motor
mapping, encoder sign convention, PD controller and state-machine behavior are
preserved unless explicitly described below.

## Single ownership rule

`MOTOR_MOTION/motion_config.h/.cpp` contains ONLY physical travel geometry and
PD position-control values:
- home count
- scene maximum count
- service maximum count
- hard-range margin
- position tolerance
- Kp/Kd
- minimum effective PWM
- maximum PWM

`MOTOR_MOTION/motion_recovery_config.h/.cpp` is the SINGLE source of
truth for:
- homing seek/release directions
- homing PWM
- homing release distance
- homing seek/release timeouts
- progress threshold
- stall timeout
- normal move timeout
- service move timeout
- maximum recovery retries
- recovery PWM levels
- recovery backoff distance
- recovery phase timeout
- recovery settle time
- homing BACKLEDS colors/brightness

Therefore a future mechanical revision should first be evaluated against the
RECOVERY configuration, without searching through MotionEngine for scattered
stall/homing constants.

## Attempt semantics

`maxRecoveryAttempts = 3` means:
- homing attempt 1 is the initial attempt;
- recovery 1 starts homing attempt 2;
- recovery 2 starts homing attempt 3;
- recovery 3 starts homing attempt 4;
- if attempt 4 fails, `HOMING_FAILED` is latched.

Recovery is axis-local. If LEFT is already successfully homed while RIGHT is
stuck, LEFT is never restarted because RIGHT needs recovery.

## Fault/recovery policy code

`RECOVERY/motion_fault_recovery.h/.cpp` contains the reusable policy functions:
attempt availability, recovery PWM selection, and bounded backoff calculation.
It contains no GPIO, encoder or LED I/O. MotionEngine owns live state/timing.

## Unified recovery

The same policy object supplies retry availability, recovery PWM and bounded
backoff calculations for:
1. homing stalls/timeouts; and
2. normal scene/service movement stalls.

The live MotionEngine state machine remains responsible for when to invoke the
policy and for tracking attempt/timing state. The policy itself performs no I/O.

This separation is intentional: recovery tuning is policy/configuration, while
motor control remains a separate execution mechanism.

## Homing status LEDs

`RECOVERY/motion_status_lighting.*` is the system observer for homing status.
It is outside MotionEngine so MotionEngine remains independent from WS2812
transport.

During an explicitly started homing indication:
- each active side shows configured red on its BACKLEDS;
- each side that successfully reaches `AT_TARGET + homed + no fault` shows
  configured green;
- after BOTH sides successfully home, BOTH BACKLEDS are cleared.

The LED colors and brightness are configuration values in the recovery policy,
not hard-coded constants in the observer.

## Hardware profile

Target: ESP32 WROOM32 DOIT DevKit V1.

Proven motor map remains:
- LEFT H-bridge: GPIO25 / GPIO26
- RIGHT H-bridge: GPIO13 / GPIO27
- LEFT encoder: GPIO34 / GPIO35
- RIGHT encoder: GPIO32 / GPIO33
- LEFT HOME: GPIO4
- RIGHT HOME: GPIO5

The known GPIO34/35 input-only pull-up diagnostic is not changed by V1.2.

## SceneVM boundary

SceneVM must use the MotionPort/SceneMotionAdapter boundary. It must not access
MotionEngine recovery internals or GPIOs directly.

A motor jam during a scene therefore uses the same bounded recovery subsystem
without making SceneVM aware of the recovery implementation.
