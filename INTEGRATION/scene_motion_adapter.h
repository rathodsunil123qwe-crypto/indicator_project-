#pragma once

#include "../MOTOR_MOTION/motor_motion.h"

enum class SceneWaitTargetResult : uint8_t {
    PENDING,
    COMPLETE,
    FAILED,
};

class SceneMotionAdapter {
public:
    explicit SceneMotionAdapter(MotorMotion& motor) : motor_(motor) {}

    bool move(MotionAxis axes, uint8_t positionPercent, uint8_t speedPercent);
    void stop(MotionAxis axes);
    void pause();
    bool resume();
    SceneWaitTargetResult waitTarget(MotionAxis axes);
    bool ready(MotionAxis axes = MotionAxis::BOTH) const;
    MotionAxis pendingTargets() const { return pendingTargets_; }
    MotionFault lastFault() const { return lastFault_; }

private:
    MotorMotion& motor_;
    MotionAxis pendingTargets_ = MotionAxis::NONE;
    MotionFault lastFault_ = MotionFault::NONE;
    uint8_t leftPosition_ = 0;
    uint8_t rightPosition_ = 0;
    uint8_t leftSpeed_ = 0;
    uint8_t rightSpeed_ = 0;
    bool paused_ = false;
};
