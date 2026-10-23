# MOTOR MOTION V1.2 — TUNING GUIDE V1.2

## 1. Purpose

This guide documents the existing Motor Motion V1.2 tuning values and where
those values are owned. It is a tuning reference only. It does **not** define
a new algorithm and it does not authorize behavior or architecture changes.

V1.2 separates two kinds of tuning:

- `MOTOR_MOTION/motion_config.cpp` — physical travel geometry and PD position-control values.
- `MOTOR_MOTION/motion_recovery_config.cpp` — homing, stall, timeout, retry/recovery, and homing-status-light policy.

The values in these files are currently identical for LEFT and RIGHT. That is
intentional and preserves the current calibrated behavior.

---

## 2. Before changing any value

1. Change only one tuning value at a time when possible.
2. Record the original value before testing.
3. Rebuild and run the host regression after every change.
4. For physical changes, test one axis at a time first.
5. Do not change GPIO mappings, state-machine logic, public APIs, or recovery
   algorithms as part of a tuning adjustment.
6. Do not move values between `motion_config` and `motion_recovery_config`.
7. Do not use the service range as the normal scene range.

The ESP32 WROOM32 DOIT DevKit V1 remains the target hardware profile.

---

## 3. `motion_config.cpp` — physical and PD tuning

### 3.1 `homeCounts = 0`

**Meaning:** Encoder count assigned to the fully homed mechanical position.

**Units:** encoder counts.

**Used for:** home reference, normal position conversion, range checks and
post-homing target state.

**Current LEFT:** `0`

**Current RIGHT:** `0`

Do not change this simply to compensate for a different scene position. It is
the defined zero/reference position of the motion model.

### 3.2 `sceneMaxCounts = 6000`

**Meaning:** Maximum commanded travel available to normal scene motion.

**Units:** encoder counts.

**Current LEFT:** `6000`

**Current RIGHT:** `6000`

Scene percentage conversion uses the span from `homeCounts` to
`sceneMaxCounts`.

### 3.3 `serviceMaxCounts = 10000`

**Meaning:** Maximum commanded travel available to service operations.

**Units:** encoder counts.

**Current LEFT:** `10000`

**Current RIGHT:** `10000`

This is intentionally larger than the normal scene range. It is not the
normal scene travel limit.

### 3.4 `hardRangeMarginCounts = 100`

**Meaning:** Encoder margin permitted beyond the active travel range before
the runtime reports `COUNTER_RANGE`.

**Units:** encoder counts.

**Current LEFT:** `100`

**Current RIGHT:** `100`

This is a safety/range tolerance, not additional intended travel.

### 3.5 `positionToleranceCounts = 10`

**Meaning:** Maximum absolute encoder error accepted as `AT_TARGET`.

**Units:** encoder counts.

**Current LEFT:** `10`

**Current RIGHT:** `10`

A smaller value makes target acceptance tighter; a larger value makes it more
forgiving. Any change should be validated against real mechanical repeatability.

### 3.6 `kp = 0.45`

**Meaning:** Proportional gain of the PD position controller.

**Units:** controller gain; treated as unitless in this implementation.

**Current LEFT:** `0.45`

**Current RIGHT:** `0.45`

Higher proportional gain increases the controller response to position error.
Do not change casually; excessive gain can produce aggressive or unstable
motion.

### 3.7 `kd = 0.12`

**Meaning:** Derivative gain of the PD position controller.

**Units:** controller gain; treated as unitless in this implementation.

**Current LEFT:** `0.12`

**Current RIGHT:** `0.12`

Derivative action uses the change in position error between updates. Tune only
with controlled hardware testing because it affects damping/response.

### 3.8 `minimumEffectivePwm = 90`

**Meaning:** Minimum PWM magnitude used when the PD controller requests a
non-zero output.

**Units:** PWM value on the `0..255` scale.

**Current LEFT:** `90`

**Current RIGHT:** `90`

This compensates for the motor/gearbox's minimum effective drive level. It is
not a general speed setting.

### 3.9 `maximumPwm = 255`

**Meaning:** Absolute PWM ceiling used by the normal PD controller.

**Units:** PWM value on the `0..255` scale.

**Current LEFT:** `255`

**Current RIGHT:** `255`

The requested scene/service speed percentage scales this ceiling before the PD
output is applied.

---

## 4. `motion_recovery_config.cpp` — recovery policy tuning

The recovery configuration is the single tuning source for abnormal-motion
handling. The field order below matches `MotionRecoveryConfig` exactly.

### 4.1 `homeSeekDirection = -1`

**Meaning:** Signed motor direction used while seeking the HOME switch.

**Units:** direction sign (`-1` or `+1`).

**Current LEFT:** `-1`

**Current RIGHT:** `-1`

This must match the proven motor/encoder direction convention.

### 4.2 `homeReleaseDirection = +1`

**Meaning:** Signed motor direction used to move away from the HOME switch
once it has been detected.

**Units:** direction sign (`-1` or `+1`).

**Current LEFT:** `+1`

**Current RIGHT:** `+1`

It is intentionally opposite to `homeSeekDirection` for the current hardware.

### 4.3 `homingPwm = 100`

**Meaning:** PWM magnitude used during both homing seek and release.

**Units:** PWM `0..255`.

**Current LEFT:** `100`

**Current RIGHT:** `100`

### 4.4 `homeReleaseCounts = 50`

**Meaning:** Encoder travel required after HOME detection before the switch is
considered released and homing can complete.

**Units:** encoder counts.

**Current LEFT:** `50`

**Current RIGHT:** `50`

### 4.5 `homeTimeoutMs = 15000`

**Meaning:** Maximum time allowed for the homing seek phase.

**Units:** milliseconds.

**Current LEFT:** `15000 ms` / `15 s`

**Current RIGHT:** `15000 ms` / `15 s`

### 4.6 `homeReleaseTimeoutMs = 1500`

**Meaning:** Maximum time allowed for the HOME switch release phase.

**Units:** milliseconds.

**Current LEFT:** `1500 ms` / `1.5 s`

**Current RIGHT:** `1500 ms` / `1.5 s`

### 4.7 `progressDeltaCounts = 1`

**Meaning:** Minimum encoder movement considered real progress.

**Units:** encoder counts.

**Current LEFT:** `1`

**Current RIGHT:** `1`

Stall detection requires less than this amount of movement for the configured
stall timeout.

### 4.8 `stallTimeoutMs = 500`

**Meaning:** Time without sufficient encoder progress before a commanded motor
is treated as stalled.

**Units:** milliseconds.

**Current LEFT:** `500 ms`

**Current RIGHT:** `500 ms`

This participates in both homing-seek and normal movement stall detection.

### 4.9 `moveTimeoutMs = 12000`

**Meaning:** Maximum duration of a normal scene move.

**Units:** milliseconds.

**Current LEFT:** `12000 ms` / `12 s`

**Current RIGHT:** `12000 ms` / `12 s`

This is distinct from the shorter/earlier stall-progress test: a motor can be
making progress and still exceed the overall move timeout.

### 4.10 `serviceMoveTimeoutMs = 25000`

**Meaning:** Maximum duration of a service move.

**Units:** milliseconds.

**Current LEFT:** `25000 ms` / `25 s`

**Current RIGHT:** `25000 ms` / `25 s`

The service timeout is intentionally longer than the normal scene-move timeout.

---

## 5. Recovery retry tuning

### 5.1 `maxRecoveryAttempts = 3`

**Meaning:** Number of recovery retries permitted after the initial attempt.

**Units:** retry count.

**Current LEFT:** `3`

**Current RIGHT:** `3`

This means:

- attempt 1 = initial operation;
- recovery 1 starts attempt 2;
- recovery 2 starts attempt 3;
- recovery 3 starts attempt 4;
- failure of attempt 4 latches the corresponding failure state.

The recovery counter is axis-local.

### 5.2 `recoveryPwm1 = 70`

**Meaning:** PWM used during recovery attempt 1.

**Units:** PWM `0..255`.

**Current LEFT:** `70`

**Current RIGHT:** `70`

### 5.3 `recoveryPwm2 = 90`

**Meaning:** PWM used during recovery attempt 2.

**Units:** PWM `0..255`.

**Current LEFT:** `90`

**Current RIGHT:** `90`

### 5.4 `recoveryPwm3 = 100`

**Meaning:** PWM used during recovery attempt 3 and any later permitted attempt.

**Units:** PWM `0..255`.

**Current LEFT:** `100`

**Current RIGHT:** `100`

The recovery policy selects these values by attempt number; it does not alter
the normal PD controller gains.

### 5.5 `recoveryBackoffCounts = 1000`

**Meaning:** Requested encoder distance used to move away before retrying.

**Units:** encoder counts.

**Current LEFT:** `1000`

**Current RIGHT:** `1000`

The calculated recovery target is clamped to the active allowed range, so the
configured backoff cannot itself request an out-of-range target.

### 5.6 `recoveryPhaseTimeoutMs = 700`

**Meaning:** Maximum time allowed for the non-blocking `STALL_BACKOFF` phase.

**Units:** milliseconds.

**Current LEFT:** `700 ms`

**Current RIGHT:** `700 ms`

If the backoff target is not reached before this timeout, the motor is stopped
and the recovery enters the settle phase.

### 5.7 `recoverySettleMs = 20`

**Meaning:** Pause after recovery backoff before retrying the original operation.

**Units:** milliseconds.

**Current LEFT:** `20 ms`

**Current RIGHT:** `20 ms`

---

## 6. Homing status BACKLEDS

These values belong to recovery/status indication, not SceneVM effects.

### `homingLedBrightness = 100`

BACKLEDS brightness used during homing status indication.

### Active homing/fault color

`homingRedR = 255`

`homingRedG = 0`

`homingRedB = 0`

Result: configured red.

### Successful-homing color

`homingGreenR = 0`

`homingGreenG = 255`

`homingGreenB = 0`

Result: configured green.

All channels use the `0..255` RGB range.

---

## 7. How the recovery sequence works

### Homing

1. MotionEngine starts homing.
2. The axis seeks HOME using `homeSeekDirection` and `homingPwm`.
3. If HOME is detected, the motor reverses using `homeReleaseDirection`.
4. The switch must release after `homeReleaseCounts` of travel.
5. If homing stalls or times out, the same recovery policy is consulted.
6. Recovery backs away using the configured recovery PWM and bounded backoff.
7. After `recoverySettleMs`, that axis restarts the homing attempt.
8. The successful opposite axis is not restarted.
9. Once the configured recovery attempts are exhausted, homing fails.

### Normal scene/service movement

1. MotionEngine runs the normal PD controller.
2. Encoder progress is observed using `progressDeltaCounts`.
3. If progress stops for `stallTimeoutMs`, stall recovery begins.
4. The motor backs away from the original target direction.
5. The recovery target is bounded by the active motion range.
6. After the backoff or `recoveryPhaseTimeoutMs`, the motor stops.
7. After `recoverySettleMs`, the original target is retried.
8. A later stall consumes another bounded recovery attempt.
9. Exhaustion latches the appropriate fault.

---

## 8. Practical tuning guidance

### If the motor moves too aggressively near the target

Review `kp` and `kd` together. Do not use `minimumEffectivePwm` as a substitute
for controller tuning.

### If the motor does not move reliably at small errors

Review `minimumEffectivePwm` only after confirming the PD output is otherwise
correct.

### If normal movement is falsely reported as stalled

Review mechanical encoder progress and then `progressDeltaCounts` /`stallTimeoutMs`.
Do not immediately increase the stall timeout without understanding why the
encoder is not reporting movement.

### If a healthy long move times out

Review `moveTimeoutMs` against the actual mechanical travel time. For service
operations, use `serviceMoveTimeoutMs`.

### If a jam is not clearing during recovery

Review `recoveryPwm1..3`, `recoveryBackoffCounts`, and the recovery phase timing.
Change one value at a time and test carefully.

### If homing does not release cleanly

Review `homeReleaseDirection`, `homeReleaseCounts`, `homingPwm`, and
`homeReleaseTimeoutMs`.

### If the motor repeatedly reaches a mechanical limit during recovery

Review the configured `recoveryBackoffCounts` together with the active travel
bounds. The runtime clamps the recovery target to those bounds.

---

## 9. Values that should NOT be tuned here

The following are outside these configuration files and must not be changed as
part of ordinary tuning:

- ESP32 GPIO mappings
- encoder PCNT implementation
- H-bridge implementation
- MotionEngine state transitions
- MotionPort / SceneMotionAdapter API
- SceneVM behavior
- LED transport implementation
- recovery policy algorithm
- service API semantics

This guide documents configuration. It is not permission to alter architecture.

---

## 10. Test discipline

### Host test

Run:

```text
g++ -std=c++17 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror -I tests/host_stub -I MOTOR_MOTION tests/test_motion_v12.cpp MOTOR_MOTION/motion_engine.cpp MOTOR_MOTION/motion_config.cpp MOTOR_MOTION/motion_recovery_config.cpp MOTOR_MOTION/UNIVERSAL_RECOVERY/universal_recovery.cpp -o /tmp/motor_v12_test
/tmp/motor_v12_test
```

Expected output:

```text
Motor Motion V1.2 host tests: PASS
```

### Hardware Test 3.1

Use:

`INDICATOR_SCENE_SYSTEM_V1_TEST3_1_MOTOR_RECOVERY.ino`

The test is intended for the ESP32 WROOM32 DOIT DevKit V1. It exercises the
motor recovery/homing block and status-light observer; it does not execute
SceneVM scenes.

Known GPIO34/35 input-only pull-up diagnostics remain non-fatal and are not
part of this tuning guide.

---

## 11. Current locked values — quick reference

| Parameter | LEFT | RIGHT | Unit |
|---|---:|---:|---|
| homeCounts | 0 | 0 | counts |
| sceneMaxCounts | 6000 | 6000 | counts |
| serviceMaxCounts | 10000 | 10000 | counts |
| hardRangeMarginCounts | 100 | 100 | counts |
| positionToleranceCounts | 10 | 10 | counts |
| kp | 0.45 | 0.45 | gain |
| kd | 0.12 | 0.12 | gain |
| minimumEffectivePwm | 90 | 90 | PWM |
| maximumPwm | 255 | 255 | PWM |
| homeSeekDirection | -1 | -1 | direction |
| homeReleaseDirection | +1 | +1 | direction |
| homingPwm | 100 | 100 | PWM |
| homeReleaseCounts | 50 | 50 | counts |
| homeTimeoutMs | 15000 | 15000 | ms |
| homeReleaseTimeoutMs | 1500 | 1500 | ms |
| progressDeltaCounts | 1 | 1 | counts |
| stallTimeoutMs | 500 | 500 | ms |
| moveTimeoutMs | 12000 | 12000 | ms |
| serviceMoveTimeoutMs | 25000 | 25000 | ms |
| maxRecoveryAttempts | 3 | 3 | retries |
| recoveryPwm1 | 70 | 70 | PWM |
| recoveryPwm2 | 90 | 90 | PWM |
| recoveryPwm3 | 100 | 100 | PWM |
| recoveryBackoffCounts | 1000 | 1000 | counts |
| recoveryPhaseTimeoutMs | 700 | 700 | ms |
| recoverySettleMs | 20 | 20 | ms |
| homingLedBrightness | 100 | 100 | brightness |
| homingRedRGB | 255,0,0 | 255,0,0 | RGB |
| homingGreenRGB | 0,255,0 | 0,255,0 | RGB |

These values are the current V1.2 baseline for Test 3.1.
