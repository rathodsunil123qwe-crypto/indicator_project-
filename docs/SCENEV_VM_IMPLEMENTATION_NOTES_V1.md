# INDICATOR SCENE VM V1 — Implementation Notes / Conformance Lock

## Scope

This implementation is the V9.1 Scene Language runtime boundary only.

It does **not** implement or contain:

- motor PID
- encoder counting
- homing
- stall recovery
- WS2812 transport
- GPIO/pin configuration
- controller click mapping
- LittleFS storage
- Android protocol/UI

Those domains remain outside SceneVM behind `MotionPort` and `LEDPort`.

## 1. Frozen public interface

`SCENE_VM/scene_vm_v1_interface.h` remains the public V1 boundary.

`LEDSelector` intentionally remains index-based. The interface has no selector-variable-name field. Therefore the implementation stores the dynamic selector identifier internally in `SelectorSpec::variable`, resolves it at execution time, and passes the resolved integer in `LEDSelector::index`.

This avoids silently changing the frozen adapter ABI while still implementing `[i]` correctly.

## 2. Resumable execution model

The previous Phase-1 recursive `runBlock()` model was not safe for waits inside nested control flow because a local child index could be recreated after a wait.

V1 now compiles the parsed control tree into a **flat instruction stream**:

- `IF_FALSE` + `JUMP`
- `WHILE_CHECK` + back jump
- `FOR_INIT`, `FOR_CHECK`, `FOR_STEP`, `FOR_END`
- ordinary commands

Execution owns one persistent `pc`.

A `WAIT` increments `pc` before entering `WAITING_TIME`; `WAIT TARGET` does the same before entering `WAITING_TARGET`. When the wait completes, execution continues from that exact PC. This is valid inside arbitrarily nested IF/WHILE/FOR structures within the configured VM nesting/step limits.

FOR loop state is stored in a persistent `forStack`. Each frame records the loop variable name and the outer variable value, allowing the Builder contract's block-scoped restoration rule to survive waits.

## 3. Canonical `.scene` parsing

`load()` accepts the complete Builder scene format:

```text
SCENE
ID       = ...
NAME     = ...
DESC     = ...
CATEGORY = NORMAL | NO_PLATE
CAPABILITIES = ...       # optional

COLORS
colour1 = R,G,B

SCRIPT
...
END
```

The parser enforces:

- SCENE first
- COLORS before SCRIPT
- duplicate section rejection
- required metadata
- known metadata only
- valid category
- known, unique capabilities
- maximum 32 color slots
- RGB slot range 0..255
- final SCRIPT END
- non-empty script body

Color slots are loaded into the VM environment at `start()`. Their type is COLOR and they cannot be redefined by LET. A LET alias of a color slot remains COLOR because runtime values are typed.

Script-only input is retained as a host-test convenience. Canonical application/runtime input should be the complete `.scene` file.

## 4. Category/capability enforcement

The VM re-checks Builder policy instead of trusting the producer blindly.

### NORMAL

- normal motion/LED commands are permitted
- `NO_PLATELEDS` requires `SYNCED_COMPATIBLE`

### NO_PLATE

- no MOVE
- no STOP
- no WAIT TARGET
- only `BOTH NO_PLATELEDS` SET/CLEAR operations
- ordinary WAIT and SHOW remain legal because the language contract describes NO_PLATE as LED-only, not wait-free
- no capabilities

### TURN_SIGNAL_COMPATIBLE

All motion and indicator-LED operations must target `BOTH`. `NO_PLATELEDS` also requires BOTH.

### WELCOME_COMPATIBLE / SYNCED_COMPATIBLE

These are metadata capabilities only; their external arbitration remains outside SceneVM.

## 5. Selector semantics

Supported forms:

- `[1]`
- `[1,3,5-7]`
- `[1-10]`
- `[i]`

Static lists/ranges are expanded to individual `LEDPort` calls because the frozen adapter accepts one resolved selector index.

Dynamic `[i]` retains the identifier internally. At execution time the variable must:

1. exist
2. be NUMBER
3. be finite
4. be an integer
5. be inside the physical V9.1 profile range

Physical profile used by the VM is currently:

Physical LED counts are **not stored in SceneVM**. `LEDPort::zoneCount(Zone)` is the runtime authority and must be backed by the ESP `LED_CONFIG` domain. The VM therefore remains valid when the hardware LED counts change.

- `LENS` is always one dedicated semantic LED and never accepts a selector.
- `FRONTLEDS`, `MAINLEDS`, `BACKLEDS`, and `NO_PLATELEDS` counts are supplied by `LED_CONFIG` through `LEDPort::zoneCount()`.

## 6. Expressions

The runtime implements NUMBER, COLOR and BOOL values plus:

- `+ - * / %`
- `== != > < >= <=`
- `RGB`
- `ABS MIN MAX CLAMP MAP LERP`
- `SIN COS TAN` using radians
- `RANDOM` in `[0,1]`
- `RANDOM_CHOICE` with homogeneous argument types
- `TRUE/FALSE`
- `TIME` and `DELTA`

RGB components must be 0..255 and are rounded to the nearest integer.

`TIME` is scene-relative elapsed milliseconds.
`DELTA` is milliseconds since the previous VM update; first update is zero.
Both are read-only.

## 7. WAIT semantics

WAIT accepts the V9.1 limits:

- plain milliseconds: maximum 3,600,000 ms
- `ms`: maximum 3,600,000 ms
- `sec`: maximum 3,600 sec

WAIT is cooperative. It never blocks the host thread.

`WAIT TARGET` is cooperative and checks `MotionPort::faulted()` before completion. A motion fault becomes VM ERROR and commands `MotionPort::stop(BOTH)` as the safety action.

Any fatal VM error also stops BOTH through MotionPort.

## 8. Runtime safety

MOVE evaluates POS and SPEED at execution time and enforces:

- POS 0..100
- SPEED 1..100
- finite numeric values

LED selectors are range-checked at execution time so a dynamic variable cannot bypass the physical profile.

A cooperative instruction budget prevents a scene made entirely of zero-time control flow from monopolizing one VM update.

## 9. Implementation status

The SceneVM implementation is retained as a protected source block for the next
stage. Its historical qualification/test harness is intentionally not included
in this motor-safety baseline. No SceneVM implementation source was modified by
the Motor Safety Foundation update.
