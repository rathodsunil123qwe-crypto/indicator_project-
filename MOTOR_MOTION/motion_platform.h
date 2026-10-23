#pragma once

#include "motion_types.h"

// The MotionEngine only talks to this interface. Replacing ESP32 PCNT/LEDC
// on a future MCU means implementing this one small hardware adapter.
class MotionPlatform {
public:
    virtual ~MotionPlatform() = default;

    virtual bool begin() = 0;
    virtual int32_t readEncoderCounts(MotionAxis axis) = 0;
    virtual void zeroEncoderCounts(MotionAxis axis) = 0;
    virtual bool homeSwitchActive(MotionAxis axis) = 0;
    virtual void driveSignedPwm(MotionAxis axis, int16_t pwm) = 0;
    virtual void stopMotor(MotionAxis axis) = 0;
    virtual int16_t appliedPwm(MotionAxis axis) const = 0;
};
