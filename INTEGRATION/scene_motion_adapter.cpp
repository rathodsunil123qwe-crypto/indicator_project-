#include "../INTEGRATION/scene_motion_adapter.h"
#include <initializer_list>

bool SceneMotionAdapter::move(MotionAxis axes, uint8_t positionPercent, uint8_t speedPercent) {
    if (!motor_.move(axes, positionPercent, speedPercent)) {
        lastFault_ = motor_.firstFault(axes);
        if (lastFault_ == MotionFault::NONE) lastFault_ = MotionFault::INVALID_COMMAND;
        return false;
    }
    if (motionHasAxis(axes, MotionAxis::LEFT)) { leftPosition_ = positionPercent; leftSpeed_ = speedPercent; }
    if (motionHasAxis(axes, MotionAxis::RIGHT)) { rightPosition_ = positionPercent; rightSpeed_ = speedPercent; }
    pendingTargets_ = static_cast<MotionAxis>(static_cast<uint8_t>(pendingTargets_) | static_cast<uint8_t>(axes));
    lastFault_ = MotionFault::NONE;
    paused_ = false;
    return true;
}

void SceneMotionAdapter::stop(MotionAxis axes) {
    motor_.stop(axes);
    pendingTargets_ = static_cast<MotionAxis>(static_cast<uint8_t>(pendingTargets_) & ~static_cast<uint8_t>(axes));
    paused_ = false;
}

void SceneMotionAdapter::pause() {
    if (paused_) return;
    motor_.stop(MotionAxis::BOTH);
    paused_ = true;
}

bool SceneMotionAdapter::resume() {
    if (!paused_) return true;
    paused_ = false;
    bool ok = true;
    if (motionHasAxis(pendingTargets_, MotionAxis::LEFT)) {
        if (!motor_.move(MotionAxis::LEFT, leftPosition_, leftSpeed_)) {
            lastFault_ = motor_.firstFault(MotionAxis::LEFT);
            if (lastFault_ == MotionFault::NONE) lastFault_ = MotionFault::INVALID_COMMAND;
            ok = false;
        }
    }
    if (motionHasAxis(pendingTargets_, MotionAxis::RIGHT)) {
        if (!motor_.move(MotionAxis::RIGHT, rightPosition_, rightSpeed_)) {
            lastFault_ = motor_.firstFault(MotionAxis::RIGHT);
            if (lastFault_ == MotionFault::NONE) lastFault_ = MotionFault::INVALID_COMMAND;
            ok = false;
        }
    }
    return ok;
}

SceneWaitTargetResult SceneMotionAdapter::waitTarget(MotionAxis axes) {
    if (axes == MotionAxis::NONE || !motionHasAxis(pendingTargets_, axes)) {
        lastFault_ = MotionFault::INVALID_COMMAND;
        return SceneWaitTargetResult::FAILED;
    }
    if (motor_.hasFault(axes)) {
        lastFault_ = motor_.firstFault(axes);
        return SceneWaitTargetResult::FAILED;
    }
    if (!motor_.targetsReached(axes)) return SceneWaitTargetResult::PENDING;
    pendingTargets_ = static_cast<MotionAxis>(static_cast<uint8_t>(pendingTargets_) & ~static_cast<uint8_t>(axes));
    lastFault_ = MotionFault::NONE;
    return SceneWaitTargetResult::COMPLETE;
}

bool SceneMotionAdapter::ready(MotionAxis axes) const {
    if (axes == MotionAxis::NONE) return false;
    for (MotionAxis axis : {MotionAxis::LEFT, MotionAxis::RIGHT}) {
        if (!motionHasAxis(axes, axis)) continue;
        const MotionStatus s = motor_.status(axis);
        if (!s.homed || s.state == MotionState::FAULT || s.state == MotionState::HOMING_SEEK || s.state == MotionState::HOMING_RELEASE) return false;
    }
    return true;
}
