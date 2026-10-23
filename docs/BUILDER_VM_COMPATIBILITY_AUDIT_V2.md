# Builder → Scene VM Compatibility Audit V2

Status: **COMPATIBILITY GATE PASSED / V9.1 LANGUAGE + VM RUNTIME CONTRACT LOCKED**

## Source of truth

The current Builder implementation and regression tests are authoritative for
Scene Language V9.1. The earlier V5 VM prototype is not used as an implementation
source or architecture dependency.

## Changes formally locked in this revision

### FOR

V9.1 replaces the old `FOR name FROM start TO end` form with:

    FOR (name = number-expression; boolean-expression; name = number-expression)
        ...
    END

Only the loop variable may be assigned by the initializer/final expression.
This gives explicit programmable iteration while keeping loop control deterministic.
Examples:

    FOR (i = 1; i <= 6; i = i + 1)
    FOR (i = 1; i <= 6; i = i + 2)
    FOR (i = 6; i >= 1; i = i - 2)
    FOR (i = 1; i <= 8; i = i * 2)

The VM executes initializer → condition → body → final expression → condition.

### TIME

`TIME` is scene-relative elapsed milliseconds.
It is read-only and is not wall-clock time or device uptime.

### DELTA

`DELTA` is elapsed milliseconds since the previous SceneVM update/tick.
It is read-only. First-tick DELTA is defined as 0 ms.

## Complete compatibility matrix

| Area | Builder V9.1 | VM V1 contract | Status |
|---|---|---|---|
| Scene metadata | ID/NAME/DESC/CATEGORY/CAPABILITIES | Parsed and validated | LOCKED |
| Categories | NORMAL, NO_PLATE | Same | LOCKED |
| Capabilities | 3 named capabilities | Same semantic gate | LOCKED |
| Colors | editable RGB slots | immutable scene parameters | LOCKED |
| MOVE | LEFT/RIGHT/BOTH + numeric POS/SPEED | motion adapter dispatch | LOCKED |
| WAIT TARGET | supported | waits for most recent MOVE target side(s) | LOCKED |
| STOP | LEFT/RIGHT/BOTH | motion adapter dispatch | LOCKED |
| WAIT | ms/ms/sec | VM timer state | LOCKED |
| SET | target/zone/selector/color/optional brightness | LED adapter dispatch | LOCKED |
| CLEAR | target/zone/selector | LED adapter dispatch | LOCKED |
| SHOW | supported | LED adapter show | LOCKED |
| LET | typed variable assignment | typed variable store | LOCKED |
| IF/ELSE | structured boolean control | compiled block control | LOCKED |
| WHILE | structured boolean control | compiled loop control | LOCKED |
| FOR | C-style 3-clause loop | same execution order | LOCKED |
| END | block/final script END | same structured matching | LOCKED |
| Expressions | typed recursive expressions | same types/operators/functions | LOCKED |
| TIME | numeric special value | scene elapsed ms | LOCKED |
| DELTA | numeric special value | tick delta ms | LOCKED |
| Selectors | static + numeric variable | runtime-resolved selector; bounds from LEDPort::zoneCount() | LOCKED |
| LENS selector | forbidden | reject | LOCKED |
| NO_PLATELEDS | BOTH only | reject invalid targets | LOCKED |
| TURN_SIGNAL | BOTH operations | enforce capability gate | LOCKED |
| SYNCED | required for NORMAL NO_PLATELEDS | enforce capability gate | LOCKED |
| PRIORITY | absent | absent | LOCKED |
| SIGNAL | absent | absent | LOCKED |
| Native effects | absent | absent | LOCKED |
| Comments | # line comments | # line comments | LOCKED |
| Resource limits | 4096 script lines / 32 colors / 3600s WAIT / 64 nesting | same static limits + 10,000-step cooperative update burst | LOCKED |

## Required implementation tests

Before real ESP integration, the VM must pass host-side/mock-adapter tests for:

1. all command families;
2. expression types and all function arities;
3. FOR +1/+2/-2/multiplication;
4. FOR zero iterations;
5. nested FOR/IF/WHILE;
6. FOR outer-variable restoration;
7. TIME and DELTA using a fake monotonic clock;
8. WAIT without blocking;
9. WAIT TARGET pending/success/failure;
10. static and dynamic selectors;
11. runtime selector bounds;
12. optional brightness presence/absence;
13. category/capability restrictions;
14. fatal-error motor stop;
15. resource limits and loop depth;
16. deterministic parsing of Builder-produced `.scene` files.

## Freeze decision

The Builder language and VM execution boundary are now sufficiently specified to
begin Scene VM V1 implementation. Future changes must be versioned; V1 behavior
must not be silently altered.


## Runtime authority locks

### LED physical counts
The Builder's `DEFAULT_LED_PROFILE` is authoring/validation data only. The ESP
`LED_CONFIG` domain is the hardware authority. SceneVM receives current counts via
`LEDPort::zoneCount(Zone)` and must not hard-code physical LED counts.

### WAIT TARGET
`WAIT TARGET` refers to the current motion target established by the most recent
`MOVE`. `MOVE BOTH` => wait BOTH. `MOVE LEFT` => wait LEFT. `MOVE RIGHT` => wait
RIGHT. Non-motion commands do not change this context. A WAIT TARGET before any MOVE
is invalid at runtime because there is no current motion target.

### Comments
`#` line comments are accepted by both Builder and VM and are stripped before command
parsing. They are developer readability only and have no execution semantics.
