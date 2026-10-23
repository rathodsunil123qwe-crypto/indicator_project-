# Scene File Colour Ownership V1 — Change Log

Implemented in this package:
- Removed runtime colour-override application from WELCOME, TURN SIGNAL, NO-PLATE, and click-mapped scene execution.
- Runtime Manager now resolves the assignment Scene ID, reads the complete stored `.scene` file, and passes that file text into the existing SceneVM flow.
- Removed assignment-config colour-override model/API and runtime colour-override diagnostics.
- Updated the sample `data/config/assignment.config` to keep timers and scene-ID assignments only.
- Updated related README and runtime documentation.

Compatibility:
- The assignment parser historically tolerated `COLORS` lines and unrecognized assignment fields. Runtime no longer reads/applies legacy colour entries. Re-upload the ID-only sample/config to make the new format explicit.
- SceneVM interface, scene language, motion adapters, side context, dual-domain ownership, and BLE/upload protocol were not intentionally changed.

Not verified:
- Firmware compilation and ESP32 hardware tests were not run in this environment because Arduino CLI/PlatformIO and the project's installed Arduino-ESP32 build environment were unavailable.
