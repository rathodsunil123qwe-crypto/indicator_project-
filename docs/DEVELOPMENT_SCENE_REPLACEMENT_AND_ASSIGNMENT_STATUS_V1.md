# INDICATOR V3 — Development Scene Replacement + Assignment Status V1

## Purpose

This block adds two development-stage capabilities to the current validated firmware baseline:

1. **Development scene replacement by Scene ID**
2. **ESP-side active assignment status readback**

The TURN SIGNAL timer, STOP behaviour, physical controller, NO-PLATE runtime and multi-click runtime remain outside this block.

## 1. Development Scene Storage Policy

During development, Scene ID remains the only scene identity.

```text
Incoming .scene
    ↓
Read internal Scene ID
    ↓
Valid scene?
 ┌───────┴────────┐
 NO               YES
 ↓                 ↓
REJECT       Existing ID?
               ┌───┴───┐
              NO       YES
              ↓         ↓
            STORE     REPLACE
```

A replacement is transactional:

```text
new scene
   ↓
write temporary file
   ↓
verify complete stored content + Scene ID
   ↓
move old committed file to backup
   ↓
commit new file
   ↓
remove backup
```

If validation, write, verification or commit fails, the previous committed scene is preserved.

No byte-by-byte comparison is performed. Same valid Scene ID means replacement in development mode.

### Production policy note

This is intentionally a **development-mode policy**. Production can later switch the duplicate-ID result back to rejection without changing Scene ID identity or the rest of the architecture.

## 2. Active Assignment Status Readback

The Runtime Manager now exposes the validated active `/config/assignment.config` through a read API.

The ESP is the source of truth for what is currently assigned on that device.

The returned content contains the complete active assignment configuration, including:

- fixed assignments
- multi-click assignments that are present
- Scene IDs
- scene IDs and configured timers (editable RGB values belong in uploaded `.scene` files)
- configured timers

The Android app can later request this same information after connecting through Wi-Fi/BLE and reconstruct its UI/state even when the user connects with a different phone or reinstalls the app.

The current serial test command is:

```text
ASSIGNMENT_STATUS
```

The test command prints the exact validated active configuration. No Wi-Fi/BLE transport is implemented in this block.

## 3. Test Contract

### Test A — New Scene

Upload a scene whose Scene ID does not exist.

Expected:

```text
STORED
```

### Test B — Same-ID Modified Scene

Modify the scene content while keeping exactly the same Scene ID and upload again.

Expected:

```text
REPLACED
```

Then read the scene by ID and confirm the new content is present.

### Test C — Invalid Same-ID Replacement

Send malformed content using an existing Scene ID.

Expected:

```text
INVALID_SCENE
```

Then read the scene by ID and confirm the previously valid scene is still present.

### Test D — Assignment Status

After the active assignment is installed, issue:

```text
ASSIGNMENT_STATUS
```

Expected:

```text
[PASS] Active assignment.config read + validated
----- BEGIN ACTIVE ASSIGNMENT -----
...
------ END ACTIVE ASSIGNMENT ------
```

The printed content must match the active assignment configuration stored on the ESP.

## 4. Locked Architecture After This Block

```text
Android / future transport
        ↓
  received scene text
        ↓
 Scene Storage V1
        ↓
 validate Scene ID + content
        ↓
 same ID → REPLACE (development)
 new ID  → STORE

Android / future status request
        ↓
 Runtime Manager
        ↓
 /config/assignment.config
        ↓
 validated active configuration
```

Editable RGB values are part of the complete `.scene` file uploaded by the app. The Runtime Manager reads the assigned Scene ID and loads that stored file directly; it does not apply a second colour override from `assignment.config`.
