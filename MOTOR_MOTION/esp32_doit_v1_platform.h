#pragma once

#include "motion_platform.h"

// ESP32-WROOM DOIT DevKit V1 hardware profile. This class intentionally keeps
// the legacy ESP32 PCNT v1 API below the hardware boundary.
class Esp32DoitV1MotionPlatform final : public MotionPlatform {
public:
    bool begin() override;
    int32_t readEncoderCounts(MotionAxis axis) override;
    void zeroEncoderCounts(MotionAxis axis) override;
    bool homeSwitchActive(MotionAxis axis) override;
    void driveSignedPwm(MotionAxis axis, int16_t pwm) override;
    void stopMotor(MotionAxis axis) override;
    int16_t appliedPwm(MotionAxis axis) const override;

private:
    int16_t appliedPwm_[2] = {0, 0};

    static uint8_t indexOf(MotionAxis axis);
    static void configureQuadrature(uint8_t pcntUnit, int pinA, int pinB);
};
