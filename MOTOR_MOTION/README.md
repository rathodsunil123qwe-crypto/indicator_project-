# Motor Motion Safety Foundation V1

The proven Motor Motion implementation owns the motor adapter, encoder/PCNT, PWM, PD controller, normal movement, and homing behavior.

`UNIVERSAL_RECOVERY/` contains the reusable recovery state and decisions shared by homing, normal movement, and service movement. The existing recovery timing/backoff behavior is intentionally preserved: a recovery backoff completes when its bounded target is reached **or** its configured recovery-phase timer expires.

## Failure policy

- Homing stall/timeout/release-timeout use Universal Recovery.
- Normal movement stall uses Universal Recovery.
- Normal/service movement `MOVE_TIMEOUT` uses the same Universal Recovery path.
- `COUNTER_RANGE` remains a non-recoverable position-integrity fault and stops/latches the affected axis.
- When bounded recovery attempts are exhausted, the existing terminal `RECOVERY_EXHAUSTED` / homing failure behavior is preserved.

## Diagnostics

`MotionStatus::recoveryTrigger` and `MotionEvent::recoveryTrigger` preserve the root condition that started the current/last recovery sequence. `fault` remains the current latched fault. `MotionFaultClass` provides a small classification boundary for future application diagnostics.

## Public motor boundary

`MotorMotion` is the public application boundary. It keeps the `.ino` and future Android/Scene Manager layers from constructing or modifying the internal MotionEngine/platform/recovery objects. Future Android retry intentionally uses the existing `home()` / `startHoming()` operation; no separate retry API is required.

## Events

`MotorMotion::pollEvent(axis, event)` exposes non-blocking per-axis events: `RECOVERY_STARTED`, `RECOVERY_BACKOFF`, `RECOVERY_RETRY`, `RECOVERY_SUCCESS`, and `MOTOR_FAULT`.
