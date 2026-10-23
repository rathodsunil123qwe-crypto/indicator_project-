# SceneVM Dual-Domain V1

## Purpose

This block introduces two independent SceneVM execution instances without changing the frozen SceneVM interface.

### NO_PLATE domain
- Owns the NO_PLATE SceneVM instance.
- Receives no MotionPort.
- LEDPort exposes only `NO_PLATELEDS`.
- Autonomous GPIO17 selection and looping remain in Runtime Manager.

### INDICATOR domain
- Owns the INDICATOR SceneVM instance.
- Receives the proven SceneMotionPortAdapter.
- LEDPort exposes `FRONTLEDS`, `MAINLEDS`, `LENS`, and `BACKLEDS`.
- NO_PLATELEDS is inaccessible from this VM.

### Shared hardware rule
Both domains share the existing LEDRuntimeV1 framebuffer/transport object. Each domain writes only its permitted zones, so a `SHOW` from either VM renders the current combined state rather than replacing the other domain's logical buffer.

## Current stage

Only these scene purposes are exercised:
- NO_PLATE: autonomous repeating scene.
- WELCOME: indicator-domain scene after homing.

TURN SIGNAL remains an indicator-domain feature and is not otherwise changed by this block. SYNCED and future ownership transitions are intentionally not implemented.

## Expected runtime

```text
NO_PLATE VM  ------------------> NO_PLATE LEDs
     |
     | independent
     v
INDICATOR VM ------------------> FRONT / MAIN / LENS / BACK
                                  + MOTION
```

Both VMs are updated every loop. A busy NO_PLATE VM therefore does not make the INDICATOR VM busy, and vice versa.
