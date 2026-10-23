# NO-PLATE LOCK SERVO V1 — UL TEST

## Hardware
- Servo signal: GPIO16
- PWM frequency: 50 Hz
- PWM resolution: 14-bit
- UNLOCK duty: 819
- LOCK duty: 1183

## UL sequence
1. Serial `ul` requests unlock.
2. GPIO16 is attached to the ESP32 LEDC PWM peripheral.
3. UNLOCK PWM duty 819 is applied and held continuously.
4. The configured `NO_PLATE LOCK TIMER` is read from the active `/config/assignment.config`.
5. After that timer expires, LOCK PWM duty 1183 is applied.
6. LOCK PWM is held for 1 second.
7. GPIO16 PWM is detached.
8. Servo returns to IDLE.

The sequence is non-blocking and does not change the existing event handling for `l`, `r`, or `1..32`.
