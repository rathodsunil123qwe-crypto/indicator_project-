#pragma once

#include "../MOTOR_MOTION/motor_motion.h"
#include "../MOTOR_MOTION/motion_recovery_config.h"
#include "../LED_RUNTIME/led_runtime_v1.h"

// ============================================================
// MOTOR SAFETY LIGHTING V1
// ============================================================
// Universal motor-safety observer. It is deliberately outside MotionEngine:
// the motor block remains independent of LED hardware, while this observer
// translates motor safety states into BACKLEDS indications.
//
// Safety indications:
//   HOMING_SEEK / HOMING_RELEASE -> solid red
//   STALL_BACKOFF / STALL_SETTLE -> blinking orange
//   FAULT                       -> blinking red
//
// Normal MOVING/IDLE/AT_TARGET states do not claim BACKLEDS. This prevents the
// safety observer from unnecessarily overwriting scene-owned BACKLEDS.
// When a safety state becomes active, the safety indication has priority.
class MotionSafetyLighting {
public:
    MotionSafetyLighting(IndicatorLED::LEDRuntimeV1& leds,
                         const MotionRecoveryConfig& leftRecovery,
                         const MotionRecoveryConfig& rightRecovery)
        : leds_(leds), leftRecovery_(leftRecovery), rightRecovery_(rightRecovery) {}

    // Call once after LED runtime initialization.
    void begin();

    // Call every loop after MotionEngine::update(). This observes both axes.
    void update(const MotorMotion& motor);

    // Explicitly release safety ownership of BACKLEDS.
    void cancel();

private:
    IndicatorLED::LEDRuntimeV1& leds_;
    const MotionRecoveryConfig& leftRecovery_;
    const MotionRecoveryConfig& rightRecovery_;
    bool active_ = false;
    uint32_t lastBlinkMs_ = 0;
    bool blinkOn_ = true;
    MotionState lastLeftState_ = MotionState::UNHOMED;
    MotionState lastRightState_ = MotionState::UNHOMED;

    void renderAxis(MotionAxis axis, const MotionStatus& status, uint32_t now);
    void setBack(MotionAxis axis, IndicatorSceneVM::SceneColor color, uint8_t brightness);
    void clearBack(MotionAxis axis);
    void clearAllBack();
};
