# Universal Recovery

This block centralizes the existing recovery implementation used by Motor Motion.

It preserves the existing behavior: stall/timeout enters recovery, the motor backs
off in the opposite direction using the configured recovery PWM and phase timeout,
then settles and retries the original operation. The same mechanism is used for
normal movement and homing. Recovery attempts remain configurable in
`../motion_recovery_config.cpp`.

The block does not own motor adapters, PCNT, PWM, PD control, or homing PWM. Those
remain in the existing Motor Motion implementation.

The `.ino` should include `../motor_motion_bundle.cpp` and use `MotorMotion`.
