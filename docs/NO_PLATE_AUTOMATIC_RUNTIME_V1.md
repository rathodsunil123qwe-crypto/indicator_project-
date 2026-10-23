# INDICATOR V3 — NO-PLATE Automatic Runtime V1

## Scope

This block consumes the existing NO-PLATE scene assignments already stored in `/config/assignment.config`. It does not create or modify NO-PLATE scene definitions or assignments.

## GPIO17 selector

- GPIO17 is wired to GND through the toggle switch.
- Firmware uses `INPUT_PULLUP`.
- HIGH = `NO-PLATE NORMAL SIDE SCENE` assignment.
- LOW = `NO-PLATE GRAPHIC SIDE SCENE` assignment.

The button configuration is isolated in `NO_PLATE/no_plate_button_config.h/.cpp` so the future NO-PLATE locking-servo configuration can live in the same subsystem.

## Runtime behavior

NO-PLATE has no controller/event command. After homing and WELCOME startup, the Runtime Manager automatically resolves the selected fixed assignment from the active LittleFS `assignment.config`, loads the complete `.scene` file for that Scene ID from Scene Storage, and starts the file as stored. Editable RGB values must already be present in the uploaded `.scene` file; `assignment.config` does not override them.

When the scene reaches VM completion, Runtime Manager starts the selected NO-PLATE scene again. Therefore NO-PLATE runs continuously in a loop.

If GPIO17 changes while NO-PLATE is running, the current NO-PLATE scene is stopped and the newly selected existing assignment is started immediately.

WELCOME and TURN SIGNAL remain foreground runtime operations. The automatic NO-PLATE loop resumes after a foreground scene finishes.

## Not included yet

- NO-PLATE lock timer
- locking servo
- TURN SIGNAL auto-off timer
- public STOP behavior

## Hardware test target

1. Boot and home.
2. Confirm WELCOME runs once.
3. Confirm GPIO17 HIGH selects NORMAL assignment and scene loops.
4. Toggle GPIO17 LOW and confirm GRAPHIC assignment starts with the RGB values stored in its `.scene` file.
5. Confirm the selected scene repeats after completion.
6. Toggle back HIGH and confirm NORMAL selection returns.
7. Confirm `ASSIGNMENT_STATUS` still reports the existing active assignment configuration.
