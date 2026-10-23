#include "motion_status_lighting.h"

namespace {
using IndicatorSceneVM::LEDSelector;
using IndicatorSceneVM::SceneColor;
using IndicatorSceneVM::Side;
using IndicatorSceneVM::Zone;

constexpr LEDSelector kWholeBack{};
constexpr uint32_t kBlinkPeriodMs = 300;
constexpr SceneColor kHomingColor{255, 0, 0};
constexpr SceneColor kRecoveryColor{255, 80, 0};
constexpr SceneColor kFaultColor{255, 0, 0};
}

void MotionSafetyLighting::begin() {
    active_ = false;
    lastBlinkMs_ = millis();
    blinkOn_ = true;
    lastLeftState_ = MotionState::UNHOMED;
    lastRightState_ = MotionState::UNHOMED;
    clearAllBack();
}

void MotionSafetyLighting::update(const MotorMotion& motor) {
    const uint32_t now = millis();
    const MotionStatus left = motor.status(MotionAxis::LEFT);
    const MotionStatus right = motor.status(MotionAxis::RIGHT);

    // Blink is global in timing but rendered independently per axis. This
    // keeps LEFT/RIGHT safety ownership local while making both patterns
    // visually synchronized.
    if (static_cast<uint32_t>(now - lastBlinkMs_) >= kBlinkPeriodMs) {
        lastBlinkMs_ = now;
        blinkOn_ = !blinkOn_;
    }

    renderAxis(MotionAxis::LEFT, left, now);
    renderAxis(MotionAxis::RIGHT, right, now);

    const bool anySafety =
        left.state == MotionState::HOMING_SEEK || left.state == MotionState::HOMING_RELEASE ||
        left.state == MotionState::STALL_BACKOFF || left.state == MotionState::STALL_SETTLE ||
        left.state == MotionState::FAULT ||
        right.state == MotionState::HOMING_SEEK || right.state == MotionState::HOMING_RELEASE ||
        right.state == MotionState::STALL_BACKOFF || right.state == MotionState::STALL_SETTLE ||
        right.state == MotionState::FAULT;

    active_ = anySafety;
    lastLeftState_ = left.state;
    lastRightState_ = right.state;
}

void MotionSafetyLighting::renderAxis(MotionAxis axis, const MotionStatus& status, uint32_t now) {
    (void)now;
    switch (status.state) {
        case MotionState::HOMING_SEEK:
        case MotionState::HOMING_RELEASE:
            // Solid red means the axis is in the normal boot/home safety path.
            setBack(axis, kHomingColor, axis == MotionAxis::LEFT ? leftRecovery_.safetyLedBrightness : rightRecovery_.safetyLedBrightness);
            return;

        case MotionState::STALL_BACKOFF:
        case MotionState::STALL_SETTLE:
            // Blinking orange identifies bounded automatic recovery.
            if (blinkOn_) setBack(axis, kRecoveryColor, axis == MotionAxis::LEFT ? leftRecovery_.safetyLedBrightness : rightRecovery_.safetyLedBrightness);
            else clearBack(axis);
            return;

        case MotionState::FAULT:
            // Blinking red identifies a latched motor fault. The motor is
            // already stopped by MotionEngine::fail().
            if (blinkOn_) setBack(axis, kFaultColor, axis == MotionAxis::LEFT ? leftRecovery_.safetyLedBrightness : rightRecovery_.safetyLedBrightness);
            else clearBack(axis);
            return;

        default:
            // Do not claim BACKLEDS during ordinary scene operation.
            if (lastLeftState_ == MotionState::HOMING_SEEK || lastLeftState_ == MotionState::HOMING_RELEASE ||
                lastLeftState_ == MotionState::STALL_BACKOFF || lastLeftState_ == MotionState::STALL_SETTLE ||
                lastLeftState_ == MotionState::FAULT ||
                lastRightState_ == MotionState::HOMING_SEEK || lastRightState_ == MotionState::HOMING_RELEASE ||
                lastRightState_ == MotionState::STALL_BACKOFF || lastRightState_ == MotionState::STALL_SETTLE ||
                lastRightState_ == MotionState::FAULT) {
                clearBack(axis);
            }
            return;
    }
}

void MotionSafetyLighting::cancel() {
    clearAllBack();
    active_ = false;
}

void MotionSafetyLighting::setBack(MotionAxis axis, SceneColor color, uint8_t brightness) {
    const Side side = axis == MotionAxis::LEFT ? Side::LEFT : Side::RIGHT;
    leds_.set(side, Zone::BACKLEDS, kWholeBack, color, true, brightness);
    leds_.showIndicator();
}

void MotionSafetyLighting::clearBack(MotionAxis axis) {
    const Side side = axis == MotionAxis::LEFT ? Side::LEFT : Side::RIGHT;
    leds_.clear(side, Zone::BACKLEDS, kWholeBack);
    leds_.showIndicator();
}

void MotionSafetyLighting::clearAllBack() {
    clearBack(MotionAxis::LEFT);
    clearBack(MotionAxis::RIGHT);
}
