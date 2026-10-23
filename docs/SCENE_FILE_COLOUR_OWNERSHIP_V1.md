# Scene File Colour Ownership V1

## Decision

Editable RGB values belong to the complete `.scene` file. The app will later load the original scene asset, edit the designated colour slots in that file, and upload the resulting complete scene.

The ESP32 Runtime Manager resolves a scene ID from `/config/assignment.config`, reads the stored scene file, and passes that complete text to SceneVM. It does not apply colour overrides from assignment configuration.

## Assignment configuration

`assignment.config` contains timers and scene-ID assignments only. It must not contain editable `COLORS` blocks or RGB slot values.

Example:

```text
TURN SIGNAL AUTO-OFF TIMER = 10SEC
NO_PLATE LOCK TIMER = 5SEC

WELCOME SCENE
ID = WELCOME_001

TURN SIGNAL SCENE
ID = PRIORITY_TEST_001

NO-PLATE NORMAL SIDE SCENE
ID = NO_PLATE_TEST_001

NO-PLATE GRAPHIC SIDE SCENE
ID = NO_PLATE_GRAPHIC_001

3 CLICKS SCENE
ID = NORMAL_TEST_001
```

The exact IDs above are examples; assignments must reference IDs that exist in Scene Storage. Fixed assignment sections remain required. Multi-click sections are optional.

## Backward compatibility

The current parser historically tolerated `COLORS` and arbitrary assignment fields. The new runtime ignores legacy colour entries because colour-override APIs have been removed. Re-upload the ID-only configuration to make the active format explicit.

## Verification checklist

- [ ] Compile firmware with the project's installed Arduino-ESP32 core and libraries.
- [ ] Upload ID-only `assignment.config`.
- [ ] Upload a scene whose `COLORS` section contains the chosen edited RGB values.
- [ ] Confirm WELCOME uses the stored scene colours.
- [ ] Confirm TURN SIGNAL LEFT and RIGHT use the same stored scene colours while preserving side context.
- [ ] Confirm NO-PLATE NORMAL/GRAPHIC each load their assigned complete scene.
- [ ] Confirm click-mapped scenes load their stored file and SYNCED routing remains unchanged.
- [ ] Inspect LittleFS scene content and confirm it is the edited uploaded file.
- [ ] Confirm no runtime colour-override diagnostics or API references remain.
