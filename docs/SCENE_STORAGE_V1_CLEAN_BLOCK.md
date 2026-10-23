# Scene Storage V1 — Clean Block Contract

## Responsibility

**STORE -> REJECT DUPLICATE -> MAKE UNIQUE SCENE AVAILABLE BY ID**

## Public API

### `begin()`
Mounts LittleFS without automatic formatting and ensures `/scenes` exists.

### `installFromText(sceneText, sceneLength, storedId)`
Receives scene content from the caller. Extracts and validates the internal ID,
rejects an already-installed ID, then performs a transactional write:

1. write `/scenes/<ID>.scene.tmp`
2. verify byte count and internal ID
3. recheck duplicate/destination
4. rename to `/scenes/<ID>.scene`

### `existsById(sceneId)`
Checks the canonical stored path and verifies the internal ID in the stored
content.

### `readById(sceneId, out)`
Reads a stored scene by internal ID for a runtime consumer such as SceneVM.

## Explicitly not part of Storage V1

- scene catalog/listing
- scene playback
- scene scheduling
- controller logic
- priority selection
- Android UI/API logic
- PC/native file handling
- SceneVM parsing/execution
- motor or LED policy

## Android integration rule

When Android uploads a scene, the HTTP/API layer supplies the received scene
content directly to `installFromText()`. The storage layer does not receive or
use the Android/PC filename for identity decisions.
