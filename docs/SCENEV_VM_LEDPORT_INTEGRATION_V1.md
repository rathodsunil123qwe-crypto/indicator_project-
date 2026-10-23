# SceneVM V1 -> LED Runtime V1 Integration Contract

The current SceneVM V1 frozen interface is included as `SCENE_VM/scene_vm_v1_interface.h`
as a synchronized contract snapshot.

The integration object is:

`SceneLEDPortAdapter`

It forwards only:

- `set()`
- `clear()`
- `show()`

to `LEDRuntimeV1`.

No parser, expression evaluator, timing engine, or animation scheduler is
introduced here.

The next system integration stage should instantiate `IndicatorLEDHardwareV1`
and pass `scenePort()` to `SceneVM::begin(..., &ledHardware.scenePort())`.
The proven motor adapter remains a separate `MotionPort` implementation.
