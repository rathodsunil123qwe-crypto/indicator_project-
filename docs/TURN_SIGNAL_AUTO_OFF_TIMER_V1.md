# TURN SIGNAL AUTO-OFF TIMER V1

## Scope

This block adds the TURN SIGNAL auto-off timer using the existing
`TURN SIGNAL AUTO-OFF TIMER = <N>SEC` assignment field.

Current uploaded configuration preserved by this package:
`TURN SIGNAL AUTO-OFF TIMER = 10SEC`

## Runtime behavior

- LEFT/RIGHT starts the configured timer.
- LEFT while RIGHT is active, or RIGHT while LEFT is active, replaces the
  foreground TURN SIGNAL and restarts the full timer.
- A normal/click-mapped INDICATOR scene replacement cancels the TURN SIGNAL
  timer.
- Manual STOP cancels the timer.
- Timer expiry uses the same `stopIndicator()` cleanup path as STOP:
  - stop the foreground TURN SIGNAL;
  - clear TURN SIGNAL context/output;
  - resume the one parked underlying normal scene when present;
  - otherwise return both motors to position 0 at speed 100 and clear
    INDICATOR LEDs.
- NO_PLATE VM and NO_PLATE LEDs are untouched.
- SceneVM `WAIT` timing is independent from the runtime timer.

## Hardware test

1. Flash this package.
2. Confirm the serial boot/config output and that the current timer value is
   the value expected from `assignment.config`.
3. Send `L`.
4. Verify TURN SIGNAL starts and serial reports timer start.
5. Wait for the configured period and verify auto-off cleanup.
6. Repeat with `R`.
7. Test `L -> R` and verify the second request restarts the full timer.
8. Start a normal scene, then `L`; verify timer expiry resumes the parked normal
   scene.
9. Start `L`, then replace it with a normal/click scene before expiry; verify
   the timer no longer stops the new normal scene.
10. Verify NO_PLATE continues independently throughout.

## Build note

Source/static checks were performed in the available environment. The
Arduino/ESP32 toolchain binaries are not installed here, so compilation and
upload must be performed in Arduino IDE on the target hardware.
