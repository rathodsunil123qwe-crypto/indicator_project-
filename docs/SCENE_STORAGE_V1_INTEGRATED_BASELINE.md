# Scene Storage V1 — Integrated System Baseline

## Purpose

This package is the proven Indicator firmware baseline with **Scene Storage V1 added only**.

The storage responsibility is strictly:

**STORE → REJECT DUPLICATE ID → MAKE UNIQUE SCENE AVAILABLE BY ID**

## Integration rule

The existing Motor Motion, Universal Recovery, LED Runtime, Motion Safety Lighting, and SceneVM source are carried forward unchanged from the proven baseline.

The only firmware-level addition to the baseline sketch is initialization of `SceneStorageV1`.

No automatic scene playback or Runtime Manager behavior is added here.

## Storage boundary

Production upload data is received as scene content and passed to:

`SceneStorageV1::installFromText()`

The storage layer does not depend on:

- PC `data/` files
- source filenames
- Android filenames
- a catalog/list operation
- SceneVM parsing of the complete language

The internal scene `ID` is the unique identity. The committed LittleFS path is derived from that ID only as a storage path.

## Available scene

A stored scene can be retrieved by internal ID using:

`SceneStorageV1::readById()`

The returned scene text can then be supplied to the locked SceneVM by the future Runtime Manager/integration layer.

## APIs

- `begin()` — mount LittleFS without automatic formatting.
- `installFromText()` — validate envelope/ID, reject duplicate ID, transactionally store a unique scene.
- `existsById()` — determine whether a valid stored scene exists for an ID.
- `readById()` — retrieve stored scene text by ID.

There is intentionally **no `list()` API** in Storage V1.
