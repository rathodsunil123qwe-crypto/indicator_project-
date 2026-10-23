# INDICATOR V3 — CURRENT FIRMWARE BASELINE

This package is the current integrated firmware baseline for the next hardware validation.

## Included validated blocks

- Existing Motor Motion foundation
- Homing
- Universal Recovery / motor safety foundation
- LED Runtime
- SceneVM V1
- Scene Storage V1
- Assignment Configuration reader
- WELCOME Runtime
- Complete `.scene` file execution by Scene ID (editable colours are stored in the scene file)
- Universal TURN SIGNAL scene
- Runtime LEFT / RIGHT context
- ID-only assignment configuration (no runtime colour override)

## Intentionally not included yet

- TURN SIGNAL auto-off timer
- STOP behaviour
- Physical controller
- Android controller
- New runtime behaviour not already hardware-tested

## Firmware entry point

There is exactly one Arduino sketch in this package:

`INDICATOR_V3_FIRMWARE.ino`

The `.h` and `.cpp` files remain modular so individual systems such as motor tuning/PID can be edited without searching through a giant sketch.

## Hardware test

1. Flash `INDICATOR_V3_FIRMWARE.ino`.
2. Open Serial Monitor at 115200.
3. Allow both motors to home.
4. WELCOME should execute once automatically from `assignment.config`.
5. Type `RIGHT` and press Enter.
6. Verify only RIGHT executes the universal TURN SIGNAL scene.
7. Type `LEFT` and press Enter.
8. Verify only LEFT executes the same universal TURN SIGNAL scene.
9. Verify the uploaded `.scene` file contains the edited RGB values and the runtime uses those stored values without applying an assignment override.
10. Repeat LEFT/RIGHT tests as desired.

Known ESP32 input-only GPIO pull-up warnings are not part of this validation and are intentionally ignored.

## Current Development Block

**Development Scene Replacement + Assignment Status V1** has been added.

- Same valid Scene ID replaces the stored scene transactionally during development.
- Invalid replacement leaves the existing committed scene untouched.
- Runtime Manager can read back the validated active `assignment.config` as the ESP-side source of truth for future Android device synchronization.
- Serial diagnostic: `ASSIGNMENT_STATUS`.

## APP COMMAND INTERFACE V1

`APP_COMMANDS/app_commands_v1.h/.cpp` is the transport-independent Android command layer. It routes visual controller commands to the existing Event Manager, calls existing service/motion and NO-PLATE APIs, exposes assignment.config status, and accepts complete scene+assignment upload packages. Wi-Fi/BLE transport is intentionally separate and not implemented in this block.
