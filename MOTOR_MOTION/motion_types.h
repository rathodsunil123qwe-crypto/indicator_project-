#pragma once

#include <Arduino.h>

enum class MotionAxis : uint8_t { NONE=0, LEFT=1, RIGHT=2, BOTH=3 };
inline bool motionHasAxis(MotionAxis mask, MotionAxis axis) {
    return (static_cast<uint8_t>(mask) & static_cast<uint8_t>(axis)) != 0;
}

enum class MotionState : uint8_t {
    UNHOMED, HOMING_SEEK, HOMING_RELEASE, IDLE, MOVING,
    STALL_BACKOFF, STALL_SETTLE, AT_TARGET, FAULT
};

enum class MotionFault : uint8_t {
    NONE, NOT_HOMED, INVALID_COMMAND, COUNTER_RANGE,
    HOME_TIMEOUT, HOME_RELEASE_TIMEOUT, STALL_TIMEOUT, MOVE_TIMEOUT,
    RECOVERY_EXHAUSTED, HOMING_FAILED
};

enum class MotionEventType : uint8_t {
    NONE,
    RECOVERY_STARTED,
    RECOVERY_BACKOFF,
    RECOVERY_RETRY,
    RECOVERY_SUCCESS,
    MOTOR_FAULT
};

// High-level fault classification is intentionally small. It lets future
// consumers (for example Android) distinguish command errors, recoverable
// mechanical faults, and position-integrity faults without knowing the
// internal MotionEngine state machine.
enum class MotionFaultClass : uint8_t {
    NONE,
    COMMAND,
    RECOVERABLE,
    POSITION_INTEGRITY,
    RECOVERY_FAILED
};

// Event data is a compact, asynchronous diagnostic record.
// `fault` is the currently latched fault (if any); `recoveryTrigger` records
// the original condition that caused the current recovery attempt. Keeping
// these separate prevents RECOVERY_STARTED from losing its root cause.
struct MotionEvent {
    MotionEventType type = MotionEventType::NONE;
    MotionAxis axis = MotionAxis::NONE;
    MotionFault fault = MotionFault::NONE;
    MotionFault recoveryTrigger = MotionFault::NONE;
    uint8_t recoveryAttempt = 0;
    uint32_t timestampMs = 0;
};

const char* motionEventName(MotionEventType type);

enum MotionWarning : uint16_t {
    MOTION_WARNING_NONE = 0,
    MOTION_WARNING_RECOVERY_ACTIVE = 1 << 0,
    MOTION_WARNING_SOFT_LIMIT = 1 << 1,
};

// Geometry + controller only. All homing/stall/recovery parameters are in
// MotionRecoveryConfig, making failure behavior independently tunable.
struct MotionAxisConfig {
    int32_t homeCounts;
    int32_t sceneMaxCounts;
    int32_t serviceMaxCounts;
    int32_t hardRangeMarginCounts;
    int32_t positionToleranceCounts;
    float kp;
    float kd;
    uint8_t minEffectivePwm;
    uint8_t maxPwm;
};

struct MotionStatus {
    MotionState state;
    MotionFault fault;
    MotionFault recoveryTrigger;
    MotionFaultClass faultClass;
    uint16_t warnings;
    int32_t encoderCounts;
    int32_t targetCounts;
    int32_t activeMaxCounts;
    uint8_t positionPercent;
    uint8_t speedPercent;
    int16_t appliedPwm;
    uint8_t recoveryAttempt;
    uint8_t homingAttempt;
    bool homed;
    bool homeSwitchActive;
    bool serviceMoveActive;
};

const char* motionStateName(MotionState state);
const char* motionFaultName(MotionFault fault);
MotionFaultClass motionFaultClass(MotionFault fault);
bool motionFaultRecoverable(MotionFault fault);
