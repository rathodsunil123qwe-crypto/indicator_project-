#include "scene_motion_port_adapter.h"

MotionAxis SceneMotionPortAdapter::toAxis(IndicatorSceneVM::Side side) {
    using IndicatorSceneVM::Side;
    switch (side) {
        case Side::LEFT: return MotionAxis::LEFT;
        case Side::RIGHT: return MotionAxis::RIGHT;
        case Side::BOTH: return MotionAxis::BOTH;
        case Side::NONE: return MotionAxis::NONE;
    }
    return MotionAxis::NONE;
}

bool SceneMotionPortAdapter::move(IndicatorSceneVM::Side side, uint8_t positionPercent, uint8_t speedPercent) {
    const MotionAxis axis = toAxis(side);
    if (axis == MotionAxis::NONE) { bridgeFault_ = MotionFault::INVALID_COMMAND; bridgeFaultValid_ = true; return false; }
    bridgeFaultValid_ = false;
    if (motion_.move(axis, positionPercent, speedPercent)) return true;
    bridgeFault_ = motion_.lastFault();
    if (bridgeFault_ == MotionFault::NONE) bridgeFault_ = MotionFault::INVALID_COMMAND;
    bridgeFaultValid_ = true;
    return false;
}

bool SceneMotionPortAdapter::stop(IndicatorSceneVM::Side side) {
    const MotionAxis axis = toAxis(side);
    if (axis == MotionAxis::NONE) return false;
    motion_.stop(axis);
    bridgeFaultValid_ = false;
    return true;
}

bool SceneMotionPortAdapter::targetReached(IndicatorSceneVM::Side side) const {
    const MotionAxis axis = toAxis(side);
    if (axis == MotionAxis::NONE) { bridgeFault_ = MotionFault::INVALID_COMMAND; bridgeFaultValid_ = true; return false; }
    const SceneWaitTargetResult result = motion_.waitTarget(axis);
    if (result == SceneWaitTargetResult::COMPLETE) { bridgeFaultValid_ = false; return true; }
    if (result == SceneWaitTargetResult::FAILED) {
        bridgeFault_ = motion_.lastFault();
        if (bridgeFault_ == MotionFault::NONE) bridgeFault_ = MotionFault::INVALID_COMMAND;
        bridgeFaultValid_ = true;
    }
    return false;
}

bool SceneMotionPortAdapter::faulted() const { return bridgeFaultValid_; }
const char* SceneMotionPortAdapter::faultText() const { return motionFaultName(bridgeFault_); }
