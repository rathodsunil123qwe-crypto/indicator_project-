#pragma once

#include "../SCENE_VM/scene_vm_v1_interface.h"
#include "../INTEGRATION/scene_motion_adapter.h"

class SceneMotionPortAdapter final : public IndicatorSceneVM::MotionPort {
public:
    explicit SceneMotionPortAdapter(SceneMotionAdapter& motion) : motion_(motion) {}
    bool move(IndicatorSceneVM::Side side, uint8_t positionPercent, uint8_t speedPercent) override;
    bool stop(IndicatorSceneVM::Side side) override;
    bool targetReached(IndicatorSceneVM::Side side) const override;
    bool faulted() const override;
    const char* faultText() const override;
private:
    SceneMotionAdapter& motion_;
    mutable MotionFault bridgeFault_ = MotionFault::NONE;
    mutable bool bridgeFaultValid_ = false;
    static MotionAxis toAxis(IndicatorSceneVM::Side side);
};
