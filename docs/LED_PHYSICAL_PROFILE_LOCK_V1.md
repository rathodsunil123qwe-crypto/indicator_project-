# LED Physical Profile Lock V1

## Locked decision

The LED hardware geometry is governed by a single source of truth:
`LED_RUNTIME/led_config.h`.

No LED runtime source, adapter, test, or SceneVM integration file may repeat
physical LED counts or the FRONTLEDS/MAINLEDS/LENS relationship as independent
configuration values.

## Current profile

| Item | Value | Meaning |
|---|---:|---|
| LEFT_FRONT_PIN | 22 | left FRONTLEDS WS2812 data pin |
| LEFT_BACK_PIN | 23 | left BACKLEDS data pin |
| RIGHT_FRONT_PIN | 18 | right FRONTLEDS data pin |
| RIGHT_BACK_PIN | 19 | right BACKLEDS data pin |
| NO_PLATE_PIN | 21 | number-plate strip data pin |
| FRONT_COUNT | 10 | complete front strip |
| MAIN_COUNT | derived | `FRONT_COUNT - 1` |
| LENS_INDEX | derived | `FRONT_COUNT - 1`, zero-based |
| BACK_COUNT | 8 | back strip per side |
| NO_PLATE_COUNT | 7 | number-plate strip |

## Future hardware change example

If the front strip becomes 12 pixels:

```text
FRONT_COUNT = 12
```

The resulting geometry is automatically:

```text
FRONTLEDS = 12 physical LEDs
MAINLEDS  = physical LEDs 1..11
LENS      = physical LED 12
```

No second count or lens-index edit is required.

## Why derived values are locked

Keeping `MAIN_COUNT` and `LENS_INDEX` derived prevents an inconsistent profile
such as FRONT=12, MAIN=11, LENS=10. The compiler will reject such a state via
`static_assert`.

## Boundary rule

The physical profile is a hardware concern. SceneVM does not own these counts.
The Builder has its own physical-profile declaration for authoring/validation;
the hardware runtime has this physical-profile declaration for actual device
behavior. Their values must match for a hardware build, and an intentional
hardware change must update both versioned profiles.
