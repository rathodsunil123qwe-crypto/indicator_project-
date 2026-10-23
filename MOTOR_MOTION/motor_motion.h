#pragma once

#include "motion_engine.h"
#include "motion_config.h"
#include "motion_recovery_config.h"
#include "esp32_doit_v1_platform.h"

// ============================================================
// MOTOR MOTION PUBLIC WRAPPER V1
// ============================================================
// Test/application sketches use this wrapper instead of constructing the
// platform, engine, or recovery implementation themselves.
class MotorMotion {
public:
    MotorMotion();

    bool begin();
    void update();

    bool home(MotionAxis axes = MotionAxis::BOTH);
    // Compatibility alias: preserve the existing public API used by test sketches.
    bool startHoming(MotionAxis axes = MotionAxis::BOTH);
    bool move(MotionAxis axes, uint8_t positionPercent, uint8_t speedPercent);
    bool serviceExtend(MotionAxis axes, uint8_t speedPercent);
    bool serviceHome(MotionAxis axes, uint8_t speedPercent);
    void stop(MotionAxis axes = MotionAxis::BOTH);

    MotionStatus status(MotionAxis axis) const;
    bool targetsReached(MotionAxis axes) const;
    bool hasFault(MotionAxis axes) const;
    MotionFault firstFault(MotionAxis axes) const;

    // Poll the next motion/recovery event for one axis. Events are produced by
    // the motion runtime; consumers such as SceneVM or LED status code decide
    // what to do with them.
    bool pollEvent(MotionAxis axis, MotionEvent& event);
    MotionEngine& engine() { return engine_; }
    const MotionEngine& engine() const { return engine_; }

private:
    Esp32DoitV1MotionPlatform platform_;
    MotionEngine engine_;
};
