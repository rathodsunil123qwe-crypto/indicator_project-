# INDICATOR Scene + Assignment Upload Transaction V1

## Goal

Treat a package containing scene text and `assignment.config` as one logical update. The transaction's recovery policy is rollback: if power is lost while files are being replaced, the next boot restores the previously committed files and the app can retry the whole upload.

## Commit sequence

1. Validate incoming scene IDs, duplicate IDs, assignment syntax, and referenced scene IDs.
2. Write and verify the staged `assignment.config`.
3. Copy the old assignment and each existing target scene into `/upload_txn/` backups.
4. Write and publish `/upload_txn/journal` only after the backups have been created.
5. Ask the runtime manager to stop only matching scene execution. INDICATOR scenes are stopped in place (motion stop + clear indicator outputs) and their repeat/paused contexts are discarded. A matching NO-PLATE scene is stopped and its NO-PLATE output is cleared. The update does not automatically start the replacement scene.
6. Install each incoming scene using the existing storage write/verify mechanism.
7. Replace `assignment.config`.
8. Remove the journal as the transaction decision point, then clean up backups and staging artifacts.

## Power-loss recovery

`SceneAssignmentUploadV1::begin()` checks the journal before app upload handling begins. If a journal exists, it conservatively rolls back each target scene and the assignment file. If a prior scene did not exist, the newly introduced canonical file is removed. If rollback cannot be completed because a required backup is missing or a filesystem operation fails, initialization fails rather than continuing with uncertain data.

## Runtime policy

The old VM execution is discarded when its scene is affected. Rollback restores files/configuration, not the previous VM instruction pointer, LED animation phase, or motor movement. Neither the old nor the new scene is resumed automatically by the transaction itself.

## Important limitations

- This change has not been compiled or hardware-tested in this environment; it must be built with the project's installed Arduino ESP32 core before flashing.
- LittleFS cannot make several file renames physically atomic. The journal and backups implement recoverable rollback, not a filesystem-level multi-file atomic primitive.
- The journal and backup copies consume temporary LittleFS space. If staging/backups cannot be written, the upload is rejected before canonical files are modified.
- The app's existing BLE chunk protocol is unchanged. Chunk receipt is not a commit acknowledgement; the app should treat the existing final package response as success only when it reports a completed upload.
- Strict duplicate/unknown-key validation in `assignment.config` and the parked SceneVM/Scene Builder language redesign are separate follow-up tasks, not included here.
