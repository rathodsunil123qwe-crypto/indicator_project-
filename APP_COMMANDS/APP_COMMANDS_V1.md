# APP COMMANDS V1

## Purpose

`APP_COMMANDS_V1` is the transport-independent command/action layer.
It receives normalized app requests and routes them into the existing tested
firmware subsystems. It does not reimplement Motor Motion, Recovery, SceneVM,
Scene Storage, Runtime Manager, or Servo logic.

The command map is organized into **families**. Commands inside one family
remain separate commands, but share the same communication/action pattern.

---

# Command Family Map

## 1. TURN_BUTTON

`LEFT_BUTTON` and `RIGHT_BUTTON` are separate commands in the same family.

```text
TURN_BUTTON
│
├── LEFT_BUTTON
│     REQUEST
│        ↓
│     ACTION: LEFT turn signal
│        ↓
│     RESULT: OK
│        OR
│     ERROR
│        ↓
│     ERROR REASON
│
└── RIGHT_BUTTON
      REQUEST
         ↓
      ACTION: RIGHT turn signal
         ↓
      RESULT: OK
         OR
      ERROR
         ↓
      ERROR REASON
```

The command layer sends the same normalized Event Manager path used by the
physical controller. These button commands are currently immediate event
dispatches, so the actual V1 response is `OK` rather than an asynchronous
`STARTED`/`COMPLETED` pair.

Canonical requests:

```json
{"id":"1","cmd":"LEFT_BUTTON"}
{"id":"2","cmd":"RIGHT_BUTTON"}
```

---

## 2. MIDDLE_BUTTON

```text
MIDDLE_BUTTON
    REQUEST
        ↓
    ACTION: Execute assigned middle-button action
        ↓
    RESULT: OK
        OR
    ERROR
        ↓
    ERROR REASON
```

The request includes `clicks` from 1 to 32.

```json
{"id":"3","cmd":"MIDDLE_BUTTON","clicks":3}
```

---

## 3. SERVICE_EXTEND

`SERVICE_LEFT_EXTEND` and `SERVICE_RIGHT_EXTEND` are separate commands in the
same family.

```text
SERVICE_EXTEND
│
├── SERVICE_LEFT_EXTEND
│     REQUEST
│        ↓
│     ACTION: Extend LEFT
│        ↓
│     STARTED
│        ↓
│     COMPLETED
│        OR
│     ERROR
│        ↓
│     ERROR REASON
│
└── SERVICE_RIGHT_EXTEND
      REQUEST
         ↓
      ACTION: Extend RIGHT
         ↓
      STARTED
         ↓
      COMPLETED
         OR
      ERROR
         ↓
      ERROR REASON
```

Service EXTEND uses the configured V1 service speed of 50%.

Canonical requests:

```json
{"id":"4","cmd":"SERVICE_LEFT_EXTEND"}
{"id":"5","cmd":"SERVICE_RIGHT_EXTEND"}
```

---

## 4. HOME

`HOME_LEFT` and `HOME_RIGHT` are separate commands in the same family.

```text
HOME
│
├── HOME_LEFT
│     REQUEST
│        ↓
│     ACTION: Home LEFT
│        ↓
│     STARTED
│        ↓
│     COMPLETED
│        OR
│     ERROR
│        ↓
│     ERROR REASON
│
└── HOME_RIGHT
      REQUEST
         ↓
      ACTION: Home RIGHT
         ↓
      STARTED
         ↓
      COMPLETED
         OR
      ERROR
         ↓
      ERROR REASON
```

HOME uses the existing normal `MotorMotion::home()` routine.

Canonical requests:

```json
{"id":"6","cmd":"HOME_LEFT"}
{"id":"7","cmd":"HOME_RIGHT"}
```

---

## 5. NUMBER_PLATE

```text
NUMBER_PLATE
│
└── NO_PLATE_UNLOCK
       REQUEST
          ↓
       ACTION: Number-plate unlock
          ↓
       STARTED
          ↓
       COMPLETED
          OR
       ERROR
          ↓
       ERROR REASON
```

The active `assignment.config` value `NO_PLATE LOCK TIMER` is used by the
existing servo sequence.

```json
{"id":"8","cmd":"NO_PLATE_UNLOCK"}
```

---


## 7. UPLOAD

```text
UPLOAD
│
└── UPLOAD_PACKAGE
       │
       REQUEST
          ↓
       ACTION: Install selected scenes + assignment.config
          ↓
       COMPLETE
          OR
       ERROR
          ↓
       ERROR REASON
```

The package is supplied through the typed `UploadPackage` API. The normal
`/api/command` JSON frame does not carry scene/config contents. The transport
layer is responsible only for receiving the package; this command layer passes
it to the existing `SceneAssignmentUploadV1`.

Developer-mode scene replacement remains active: an existing internal Scene ID
is replaced by the existing Scene Storage transactional write path.

## 6. ASSIGNMENT

```text
ASSIGNMENT
│
└── ASSIGNMENT_STATUS
       REQUEST
          ↓
       ACTION: Read active assignment
          ↓
       RESPONSE: DATA
          OR
       ERROR
          ↓
       ERROR REASON
```

```json
{"id":"9","cmd":"ASSIGNMENT_STATUS"}
```

---

## 7. UPLOAD

Upload remains a separate typed API because scene/config data is not a small
scalar command payload.

```text
UPLOAD
│
└── UPLOAD_PACKAGE
       REQUEST
          ↓
       ACTION: Install scenes + assignment config
          ↓
       RESULT: COMPLETE
          OR
       ERROR
          ↓
       ERROR REASON
```

`UPLOAD_PACKAGE` is intentionally rejected by the scalar JSON protocol until
the dedicated upload transport is defined.

---

# Common command lifecycle

Long-running operations:

```text
REQUEST
   ↓
ACTION
   ↓
STARTED
   ↓
┌───────────────┐
│               │
▼               ▼
COMPLETED       ERROR
                ↓
           ERROR REASON
```

Immediate operations:

```text
REQUEST
   ↓
ACTION
   ↓
OK / DATA
   OR
ERROR
   ↓
ERROR REASON
```

---

# V1 Error Vocabulary

The current implementation exposes these stable app-facing error codes:

- `INVALID_COMMAND`
- `INVALID_PAYLOAD`
- `BUSY`
- `NOT_HOMED`
- `MOTOR_FAULT`
- `NO_PLATE_ERROR`
- `SCENE_INVALID`
- `CONFIG_INVALID`
- `UPLOAD_FAILED`

The command layer does not invent lower-level motor/recovery errors. Existing
firmware subsystems remain authoritative for their actual behavior.

---

# Safety / ownership boundary

This module never drives GPIO14, GPIO36/VP, or GPIO39/VN. Physical controller
input remains in `PHYSICAL_CONTROLLER`.

It also does not modify Motor Motion, Recovery, SceneVM, Scene Storage,
Assignment parsing, Runtime Manager, or Servo implementation.

