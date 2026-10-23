# INDICATOR SCENE VM V1 — INTERFACE LOCK

Status: **LOCKED FOR IMPLEMENTATION**
Language source: **Scene Language V9.1**
Runtime target: ESP32 / Arduino C++

## 1. Scope

Scene VM V1 is the execution layer for `.scene` files produced by the Scene Builder.
It is responsible for parsing/compiling the scene language, evaluating expressions,
executing control flow, managing scene-relative timing, and dispatching hardware
operations through narrow adapters.

It does **not** own or reimplement motor-control algorithms, encoder handling,
homing, PID, stall detection, WS2812 transport, or physical pin configuration.
Those remain outside the VM behind hardware-facing adapter interfaces.

This interface document is the implementation contract. Every non-obvious rule in
VM code must have a matching comment referring to the rule below.

## 2. Language execution contract

### 2.1 FOR

Canonical form:

    FOR (i = 1; i <= 6; i = i + 1)
        ...
    END

The three clauses are:

1. initializer: `<loop-variable> = <number-expression>`
2. condition: `<boolean-expression>`
3. final expression: `<same-loop-variable> = <number-expression>`

Execution order is exactly:

    initializer
    condition
    body
    final expression
    condition
    ...

The loop variable is numeric and block-scoped. If an outer variable with the same
name existed before the loop, its value/type is restored after the loop closes.

The final expression may implement any numeric step supported by the expression
language, including `i = i + 1`, `i = i + 2`, `i = i - 2`, or `i = i * 2`.

The VM must not invent a hidden automatic `+1` step.

### 2.2 WHILE

`WHILE <boolean-expression>` checks the condition before every iteration.
The body executes only while the condition is true.

### 2.3 IF / ELSE

`IF` evaluates a boolean expression. With `ELSE`, exactly one branch executes.
Nested blocks are resolved using structured block matching created during compile.

### 2.4 END

The final `SCRIPT END` terminates the scene.
Other `END` tokens close the nearest open `IF`, `WHILE`, or `FOR` block.
The Builder already rejects malformed block structure; the VM parser must reject it too.

## 3. Time model

### TIME

`TIME` is **scene-relative elapsed execution time in milliseconds**.
At scene start it is zero (subject to the VM's first update timestamp), and it
increases monotonically from the scene start.

`TIME` is not wall-clock time, RTC time, Unix time, or device uptime.

### DELTA

`DELTA` is the elapsed time in milliseconds between the current VM update/tick and
the immediately preceding VM update/tick for the active scene.

`TIME` and `DELTA` are VM-provided, read-only numeric values. A scene cannot assign
to either name.

For the first tick, `DELTA` is defined as `0 ms`.

The VM uses an unsigned monotonic millisecond source and handles timer wrap-around
with unsigned-difference semantics where practical.

## 4. WAIT

Accepted forms:

    WAIT 200
    WAIT 200ms
    WAIT 2sec
    WAIT TARGET

Plain integer and `ms` mean milliseconds. `sec` is converted to milliseconds.

`WAIT TARGET` yields execution until the motion target established by the most recent
`MOVE` reports reached or reports failure. `MOVE BOTH` waits for BOTH; `MOVE LEFT`
waits for LEFT; `MOVE RIGHT` waits for RIGHT. Non-motion commands do not change the
current motion-target context. A `WAIT TARGET` before any `MOVE` is invalid.

A NO_PLATE scene cannot use `WAIT TARGET`.

## 5. Motion dispatch boundary

The VM accepts:

    MOVE <LEFT|RIGHT|BOTH> POS <number-expression> SPEED <number-expression>
    STOP <LEFT|RIGHT|BOTH>

The Builder defines POS and SPEED as numeric expressions. Runtime safety validation
must still enforce the hardware-safe runtime range before dispatch:

- POS: 0..100 percent
- SPEED: 1..100

The VM converts the validated numeric result to the hardware adapter's required
integer representation. Rounding/conversion must be documented in the implementation
and tested; the VM must not silently send out-of-range values.

`WAIT TARGET` delegates completion/failure to the motion adapter.

## 6. LED dispatch boundary

The VM accepts:

    SET <TARGET> <ZONE>[selector] COLOR <color-expression> [BRIGHTNESS <0..255>]
    CLEAR <TARGET> <ZONE>[selector]
    SHOW

Zones:

- FRONTLEDS
- MAINLEDS
- LENS
- BACKLEDS
- NO_PLATELEDS

Selectors are either static physical indices/ranges or one numeric variable index.
LENS is a dedicated physical LED and does not accept a selector.

The VM must preserve the distinction between `FRONTLEDS` and `MAINLEDS`; LENS is
also physically the FRONTLEDS position defined by the current hardware profile, but
`LENS` is exposed as its own semantic zone in the language.

Runtime physical LED counts are owned by the ESP `LED_CONFIG` domain. The VM must not
hard-code those counts. `LEDPort::zoneCount(Zone)` is the runtime query used for selector
bounds. `LENS` remains one dedicated semantic LED.

If BRIGHTNESS is omitted, the LED adapter receives an explicit `hasBrightness=false`
state. The VM must not guess or synthesize a hidden brightness value.
The LED hardware/configuration layer owns the defined default behavior for omitted
brightness.

## 7. Color/type system

Runtime value types are:

- NUMBER
- COLOR
- BOOL

The VM expression evaluator must enforce the same typing rules as the Builder.
Numeric operators operate on NUMBER values. Comparisons require compatible operand
types. `RGB(R,G,B)` returns COLOR. `RANDOM_CHOICE` returns one homogeneous value type.

Declared COLORS are immutable scene parameter slots at runtime. `LET` may read them
and may create/reassign script variables, but cannot redefine the color slot itself.

## 8. Variable model

Script variables are case-insensitive by name.
Identifiers preserve their declared spelling only where serialization/display requires it.

`LET` variables are script-scope.
`FOR` variables are block-scoped and restore any outer same-name value after `END`.
`TIME` and `DELTA` are reserved read-only VM values.

## 9. Capability/category gate

Categories:

- NORMAL
- NO_PLATE

Capabilities:

- WELCOME_COMPATIBLE
- TURN_SIGNAL_COMPATIBLE
- SYNCED_COMPATIBLE

The VM must enforce the same semantic restrictions already enforced by the Builder.
In particular:

- NO_PLATE is LED-only, NO_PLATELEDS only, BOTH only, and has no capabilities.
- TURN_SIGNAL_COMPATIBLE requires motion and indicator LED operations to use BOTH.
- NORMAL scenes may access NO_PLATELEDS only with SYNCED_COMPATIBLE.
- NO_PLATELEDS uses BOTH only.

PRIORITY and SIGNAL are not VM language constructs.
Triggered LEFT/RIGHT priority context is an external runtime concern, not a scene
language target/keyword.

## 10. Execution lifecycle API

The V1 public VM lifecycle is:

    begin(motionPort, ledPort)
    load(sceneProgram)
    start(context)
    update()
    stop()
    pause()
    resume()

Inspection:

    state()
    active()
    paused()
    lastError()

`update()` is non-blocking. It advances the VM only as far as allowed by its timing
and cooperative execution budget. WAIT and WAIT TARGET must never busy-wait.

`start()` resets the scene execution context, program counter, loop frames, timing
state, and transient runtime variables. It does not modify the immutable scene data.

## 11. Execution context

The runtime may supply external context separately from the scene source:

    LEFT
    RIGHT
    NONE

This context is metadata for the controller/runtime. V9.1 scene language has no
SIGNAL target and no language-level dependency on this context.

The VM must not reinterpret a normal `LEFT` or `RIGHT` scene command as a universal
priority command. Side-neutral priority scenes are authored with BOTH operations.

## 12. Error contract

Errors are deterministic and fail-safe.

On a fatal runtime error:

1. VM enters an error/inactive state.
2. The current scene does not continue executing.
3. Active motion is stopped through the motion adapter using BOTH.
4. The error remains available through `lastError()` until the next lifecycle reset.

Runtime errors include, at minimum:

- invalid scene structure
- unsupported instruction
- expression/type failure
- undefined variable
- invalid selector value
- runtime POS/SPEED range failure
- motion adapter failure
- WAIT TARGET failure
- LED adapter failure
- resource/buffer exhaustion
- loop-stack overflow

## 13. Ownership rules

The VM owns:

- scene execution state
- program counter
- loop frames
- script variables
- expression evaluation
- timing state
- execution errors

The VM does not own:

- motor PID
- encoder counters
- homing logic
- stall recovery
- motor GPIO/PWM implementation
- LED GPIO/data-pin configuration
- WS2812 transport implementation
- physical LED count configuration

Adapters are the only hardware boundary visible to the VM.

## 14. Non-blocking requirement

Scene VM V1 is cooperative. `update()` must return promptly.
No command may contain an internal long delay loop.
WAIT is represented as VM state; it does not call `delay()` for the requested duration.

## 15. Compatibility gate

The VM V1 implementation is accepted only when:

- every V9.1 Builder-accepted construct has a defined runtime meaning;
- every Builder-rejected construct is rejected by the VM parser/compiler or cannot
  reach runtime from a valid `.scene` file;
- every expression function has defined runtime semantics;
- FOR behavior is tested for +1, +2, negative, multiplication, zero-iteration,
  nested loops, and outer-variable restoration;
- TIME and DELTA are tested in a deterministic fake-clock environment;
- selectors and physical LED profile boundaries are tested;
- capability/category restrictions are tested;
- mock motor and mock LED adapters pass all dispatch tests;
- no proven hardware implementation is modified as part of VM development.

This document is the frozen interface. Any later language change requires a new
language revision and a corresponding VM interface revision; do not silently change
V1 behavior.
