# Motor Safety Foundation V1 - Final Pre-Hardware-Test

## Scope
This is the final controlled motor-safety update before real ESP32 testing. It intentionally changes only the motor failure/recovery path, its diagnostic event data, and the external BACKLEDS safety observer.

## Locked recovery behavior
- Homing stall/timeout/release-timeout enter the existing Universal Recovery mechanism.
- Normal movement stall enters the same mechanism.
- Normal movement `MOVE_TIMEOUT` now enters the same mechanism.
- Service movement uses the same movement state machine and therefore the same recovery path.
- `COUNTER_RANGE` remains non-recoverable through blind backoff because encoder position integrity is not trusted.
- Recovery backoff completion remains **target reached OR recovery phase timer expiry**. This is intentional and unchanged.

## Diagnostics
`MotionEvent::recoveryTrigger` records the root fault that started the current recovery attempt. This is separate from `MotionEvent::fault`, which represents the currently latched fault. No Android retry API is added; future Android retry calls the existing HOME operation.

## Safety LEDs
`MotionSafetyLighting` is an observer outside `MotionEngine` and `SceneVM`.

| Motor state | BACKLEDS indication |
|---|---|
| HOMING_SEEK / HOMING_RELEASE | solid red |
| STALL_BACKOFF / STALL_SETTLE | blinking orange |
| FAULT | blinking red |
| ordinary MOVING / IDLE / AT_TARGET | no safety override |

The observer owns BACKLEDS only while a safety state is active, preventing unnecessary interference with scene-owned LEDs during ordinary operation.

## SceneVM
The actual SceneVM source is retained. VM qualification/test scenes and test harnesses are excluded from this baseline. SceneVM implementation code is not modified by this motor-safety update.

## Hardware test matrix
1. Boot + LEFT/RIGHT homing success.
2. Homing stall -> recovery -> successful homing.
3. Normal LEFT movement stall -> recovery -> target.
4. Normal RIGHT movement stall -> recovery -> target.
5. Normal movement `MOVE_TIMEOUT` -> recovery -> target.
6. Repeated jam -> recovery exhaustion -> latched FAULT.
7. `COUNTER_RANGE` -> immediate latched FAULT, no blind recovery.
8. Verify BACKLEDS: red homing, orange recovery, red fault.
9. Reboot after latched fault and verify normal boot HOME behavior.
10. Verify LEFT/RIGHT recovery remains axis-local.
