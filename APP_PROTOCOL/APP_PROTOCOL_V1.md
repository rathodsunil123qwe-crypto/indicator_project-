# APP PROTOCOL V1

## Purpose

APP PROTOCOL V1 is the transport-independent JSON layer above `APP_COMMANDS_V1`.
It converts a small, stable JSON request into an existing typed app command and
returns a structured JSON response.

```text
Android / Wi-Fi / BLE / Serial
             |
             v
      APP PROTOCOL V1
             |
             v
      APP COMMANDS V1
             |
             v
       Existing runtime
```

The protocol layer does not contain motor, scene, servo, or Runtime Manager logic.

## Command families

The protocol uses the same canonical command vocabulary as APP COMMANDS V1.

```text
TURN_BUTTON
├── LEFT_BUTTON
└── RIGHT_BUTTON

MIDDLE_BUTTON

SERVICE_EXTEND
├── SERVICE_LEFT_EXTEND
└── SERVICE_RIGHT_EXTEND

HOME
├── HOME_LEFT
└── HOME_RIGHT

NUMBER_PLATE
└── NO_PLATE_UNLOCK

ASSIGNMENT
└── ASSIGNMENT_STATUS

UPLOAD
└── UPLOAD_PACKAGE
```

LEFT/RIGHT, SERVICE LEFT/RIGHT, and HOME LEFT/RIGHT remain separate commands;
the family only describes their shared command shape.

## Request format

Every request requires a string `id` and string `cmd`.

### Turn button

```json
{"id":"1","cmd":"LEFT_BUTTON"}
{"id":"2","cmd":"RIGHT_BUTTON"}
```

### Middle button

```json
{"id":"3","cmd":"MIDDLE_BUTTON","clicks":3}
```

`clicks` must be 1..32.

### Service / Home

```json
{"id":"4","cmd":"SERVICE_LEFT_EXTEND"}
{"id":"5","cmd":"SERVICE_RIGHT_EXTEND"}
{"id":"6","cmd":"HOME_LEFT"}
{"id":"7","cmd":"HOME_RIGHT"}
```

### No-plate unlock

```json
{"id":"8","cmd":"NO_PLATE_UNLOCK"}
```

### Assignment status

```json
{"id":"9","cmd":"ASSIGNMENT_STATUS"}
```

The assignment configuration is returned as escaped JSON string data.

### Upload package

`UPLOAD_PACKAGE` is recognized as a protocol command name, but is intentionally
not carried inside the scalar JSON frame. The dedicated transport upload endpoint
passes the typed `UploadPackage` directly through this protocol boundary.

The typed upload contains selected scene texts plus one `assignment.config`.
Scene filenames are not used as identity; the existing Scene Storage layer
continues to use the internal Scene ID and its developer-mode replacement policy.

## Response format

Immediate command:

```json
{"id":"1","cmd":"LEFT_BUTTON","result":"OK"}
```

Asynchronous command:

```json
{"id":"6","cmd":"HOME_LEFT","result":"STARTED"}
```

and when the physical operation completes:

```json
{"id":"6","cmd":"HOME_LEFT","result":"COMPLETED"}
```

Example error:

```json
{"id":"6","cmd":"HOME_LEFT","result":"ERROR","error":"MOTOR_FAULT"}
```

The request ID is retained for asynchronous service/home/servo operations so a
future Android client can correlate the completion with its original request.

## Backward compatibility

V1 also accepts the previous scalar request names `LEFT`, `RIGHT`, and
`MIDDLE_CLICK` and maps them internally to `LEFT_BUTTON`, `RIGHT_BUTTON`, and
`MIDDLE_BUTTON`. Responses use the new canonical command names.

## Serial bench wrapper

The `APPJSON ` prefix is only a temporary Serial wrapper. The JSON object is the
actual protocol frame delivered to the protocol layer.

## Important boundary

Do not add Wi-Fi/BLE code here. A transport only needs to provide complete frames
to `processFrame()` and send the returned JSON frame back to its peer.
