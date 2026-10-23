# Motor Motion V1.2 — Test 3.1 Note

This package supersedes the earlier V1.1 organization for development/testing.
The purpose is to make homing, stall detection, fault handling, recovery and
homing status lighting easy to find and modify.

## Hardware test

The Arduino sketch is named:
`INDICATOR_SCENE_SYSTEM_V1_TEST3_1_MOTOR_RECOVERY.ino`

It targets ESP32 WROOM32 DOIT DevKit V1 and performs homing with the status LED
observer. It does not execute SceneVM scenes.

Known GPIO34/35 pull-up diagnostics are intentionally unchanged and remain
non-fatal for this project.

## Host regression

Command:
`g++ -std=c++17 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror -I tests/host_stub -I MOTOR_MOTION tests/test_motion_v12.cpp MOTOR_MOTION/motion_engine.cpp MOTOR_MOTION/motion_config.cpp MOTOR_MOTION/motion_recovery_config.cpp MOTOR_MOTION/UNIVERSAL_RECOVERY/universal_recovery.cpp -o /tmp/motor_v12_test`

Result:
`Motor Motion V1.2 host tests: PASS`

The strict build uses `-Werror` and links `MOTOR_MOTION/UNIVERSAL_RECOVERY/universal_recovery.cpp`,
which is required because MotionEngine calls the reusable recovery policy.
The previous documentation command omitted that policy implementation and could
therefore fail at link time even though the host source compiled cleanly.

The host suite covers failed-side-only homing recovery, bounded exhaustion,
and reuse of the same policy for normal motion stalls.
