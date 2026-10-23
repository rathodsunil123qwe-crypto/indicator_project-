#include "motor_motion.h"

MotorMotion::MotorMotion()
    : platform_(),
      engine_(platform_, kLeftMotionConfig, kRightMotionConfig,
              kLeftRecoveryConfig, kRightRecoveryConfig) {}

bool MotorMotion::begin() { return engine_.begin(); }
void MotorMotion::update() { engine_.update(); }
bool MotorMotion::home(MotionAxis axes) { return engine_.startHoming(axes); }
bool MotorMotion::startHoming(MotionAxis axes) { return home(axes); }
bool MotorMotion::move(MotionAxis axes, uint8_t positionPercent, uint8_t speedPercent) {
    return engine_.moveToPercent(axes, positionPercent, speedPercent);
}
bool MotorMotion::serviceExtend(MotionAxis axes, uint8_t speedPercent) {
    return engine_.moveToServiceEnd(axes, speedPercent);
}
bool MotorMotion::serviceHome(MotionAxis axes, uint8_t speedPercent) {
    return engine_.moveToServiceHome(axes, speedPercent);
}
void MotorMotion::stop(MotionAxis axes) { engine_.stop(axes); }
MotionStatus MotorMotion::status(MotionAxis axis) const { return engine_.status(axis); }
bool MotorMotion::targetsReached(MotionAxis axes) const { return engine_.targetsReached(axes); }
bool MotorMotion::hasFault(MotionAxis axes) const { return engine_.hasFault(axes); }
MotionFault MotorMotion::firstFault(MotionAxis axes) const { return engine_.firstFault(axes); }
bool MotorMotion::pollEvent(MotionAxis axis, MotionEvent& event) { return engine_.pollEvent(axis, event); }
