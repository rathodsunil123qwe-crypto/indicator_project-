# INDICATOR V3 — Dual SceneVM LED Timing Isolation Fix V1

## Problem addressed

A NO_PLATE scene containing only:

    SET BOTH NO_PLATELEDS COLOR white
    SHOW
    END

could complete immediately and be reloaded continuously by the runtime. Because the shared LED runtime `SHOW` operation retransmitted every physical LED strip, this tight completion loop could consume excessive update time and disturb INDICATOR scene LED timing.

Adding a WAIT to the NO_PLATE scene masked the scheduling problem, but WAIT must remain a scene-timing command and must not be required merely to let another VM execute.

## Fix

1. NO_PLATE and INDICATOR SceneVM adapters now commit only their own LED hardware domain:
   - NO_PLATE VM -> NO_PLATE strip only.
   - INDICATOR VM -> FRONT/MAIN/LENS/BACK strips only.
2. `clearIndicatorOutputs()` and indicator framebuffer restore now commit only indicator strips.
3. Motor safety lighting commits only the indicator LED domain.
4. A static NO_PLATE scene with no timed `WAIT` is treated as a persistent state scene. It executes once, reaches COMPLETE, and its LED output remains latched instead of being reloaded in a zero-time loop.
5. A NO_PLATE scene containing a timed `WAIT` retains the existing automatic repeat behavior for animation/loop scenes.
6. INDICATOR scene `WAIT` timing is not changed.

## No changes

- Motor motion / homing / recovery
- TURN SIGNAL pause/resume/replacement
- TURN SIGNAL auto-off behavior
- STOP behavior
- click-mapped scene replacement
- assignment.config parsing
- capabilities parsing
- GPIO17 NO_PLATE selection
- SceneVM language grammar
- Scene Storage identity/replacement behavior
- No new scene IDs or assignments
- No `DEV_REPLACE_TEST_001` development test

## Hardware test

### Test A — static NO_PLATE
Use the existing `NO_PLATE_TEST_001` assignment with:

    SET BOTH NO_PLATELEDS COLOR white
    SHOW
    END

Expected:

- NO_PLATE becomes solid.
- No rapid flicker.
- No artificial WAIT is required.
- INDICATOR events remain responsive.

### Test B — animated NO_PLATE
Use an existing NO_PLATE scene containing timed WAIT commands.

Expected:

- Animation repeats normally.
- Its WAIT values control its animation timing.
- INDICATOR timing remains independent.

### Test C — INDICATOR 500 ms LED effect
Use the existing priority/normal scene with:

    SHOW
    WAIT 500ms
    CLEAR ...
    SHOW
    WAIT 500ms

Expected:

- ON interval is approximately 500 ms.
- OFF interval is approximately 500 ms.
- No extra flicker caused by NO_PLATE activity.
- Motor behavior is unchanged.

### Test D — simultaneous runtime
Run static or animated NO_PLATE while repeatedly triggering LEFT/RIGHT and click-mapped INDICATOR scenes.

Expected:

- NO_PLATE remains independent.
- INDICATOR LED effects keep their own timing.
- TURN SIGNAL replacement/pause behavior remains unchanged.
- No motor regression.
