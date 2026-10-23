# APP TRANSPORT V1

## Purpose

APP TRANSPORT V1 is the first real network transport for the already-tested APP PROTOCOL V1.
It uses the ESP32 in Wi-Fi SoftAP mode and exposes a small HTTP/JSON interface for the future Android app.

## Boundary

```text
Android / HTTP
      |
      v
APP TRANSPORT V1
      |
      v
APP PROTOCOL V1
      |
      v
APP COMMANDS V1
      |
      v
Existing Runtime / Motion / Scene / Servo
```

Transport code must not call Runtime Manager, Motion, SceneVM, Servo, or Assignment directly.

## Wi-Fi V1

The ESP32 creates:

- SSID: `INDICATOR-V1`
- Password: `indicator1`
- HTTP address: `http://192.168.4.1/`

The Serial boot log prints the actual SoftAP IP.

## Endpoints

### POST `/api/command`

Send the exact APP PROTOCOL V1 JSON frame.

Example:

```json
{"id":"101","cmd":"LEFT"}
```

Immediate command response:

```json
{"id":"101","cmd":"CMD_LEFT","result":"OK"}
```

A long-running command such as HOME returns HTTP `202` with `result=STARTED`.
The completion response is then available from `/api/events`.

### GET `/api/events`

Returns the next asynchronous completion response.

If no completion is waiting, HTTP `204 No Content` is returned.

Example completion:

```json
{"id":"102","cmd":"CMD_HOME_LEFT","result":"COMPLETED"}
```

The request ID is preserved so Android can correlate the completion with its original request.

### POST `/api/upload?id=<requestId>`

Typed package upload for the Android app. The request must use
`multipart/form-data` and contain:

- one or more `scene` parts, each containing complete scene text;
- exactly one `config` part containing the complete `assignment.config` text.

Scene filenames are ignored. Scene identity continues to come from the internal
`ID` inside each scene, and the existing `UploadPackage`/Scene Storage path
remains responsible for validation, transactional writes, replacement, and
assignment-config commit.

Example response:

```json
{"id":"201","cmd":"UPLOAD_PACKAGE","result":"COMPLETE","data":"INSTALLED"}
```

An upload error returns HTTP `400` with the normal protocol error vocabulary.
The scalar `POST /api/command` path remains unchanged; sending
`UPLOAD_PACKAGE` there still returns `USE_UPLOAD_PACKAGE_API`.

### GET `/api/health`

Returns a simple transport/protocol health response.

Example:

```json
{"transport":"WIFI_HTTP_V1","protocol":"APP_PROTOCOL_V1","status":"OK","ip":"192.168.4.1"}
```

## First hardware test

1. Flash the package.
2. Open Serial Monitor.
3. Wait for:

```text
[PASS] App Transport V1 started | SSID=INDICATOR-V1 | IP=192.168.4.1 | HTTP=80
[READY] POST /api/command | GET /api/events | GET /api/health | POST /api/upload
```

4. Connect a phone/laptop to `INDICATOR-V1` using password `indicator1`.
5. Open `http://192.168.4.1/api/health`.
6. Send POST requests to `/api/command` using the same JSON frames already proven through `APPJSON`.

## Upload boundary

`UPLOAD_PACKAGE` is intentionally not accepted through the scalar JSON command envelope. The dedicated `/api/upload` multipart endpoint now feeds the existing typed `UploadPackage` API. The existing Serial/developer upload path is not removed.

## Existing Serial bridge

The existing `APPJSON ...` Serial bench commands remain available. They still feed APP PROTOCOL V1 and are not replaced by Wi-Fi.
