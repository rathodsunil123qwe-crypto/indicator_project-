#pragma once

#include <Arduino.h>

namespace NoPlateLockServoV1 {

static constexpr uint8_t SERVO_PIN = 16;
static constexpr uint32_t SERVO_FREQUENCY_HZ = 50;
static constexpr uint8_t SERVO_RESOLUTION_BITS = 14;
static constexpr uint32_t NO_PLATE_LOCK_DUTY = 1183;
static constexpr uint32_t NO_PLATE_UNLOCK_DUTY = 819;
static constexpr uint32_t LOCK_HOLD_MS = 1000UL;

enum class State : uint8_t {
    DETACHED,
    UNLOCKED_WAITING,
    LOCKING,
    LOCKED
};

bool begin();
bool requestUnlock(uint32_t lockTimerMs);
void update();
void detach();
State state();

} // namespace NoPlateLockServoV1
