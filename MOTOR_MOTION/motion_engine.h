#pragma once

#include "motion_platform.h"
#include "UNIVERSAL_RECOVERY/universal_recovery.h"

class MotionEngine {
public:
    static constexpr uint8_t kEventQueueDepth = 8;
    MotionEngine(MotionPlatform& platform,
                 const MotionAxisConfig& leftConfig,
                 const MotionAxisConfig& rightConfig,
                 const MotionRecoveryConfig& leftRecoveryConfig,
                 const MotionRecoveryConfig& rightRecoveryConfig);

    bool begin();
    void update(); // Call every Arduino loop iteration; it never calls delay().

    // A successful HOME operation is the only way to clear a latched fault.
    bool startHoming(MotionAxis axes);
    bool moveToPercent(MotionAxis axes, uint8_t positionPercent, uint8_t speedPercent);
    // Service methods are intentionally separate from scene movement. They
    // use each axis's own serviceMaxCounts and are for the Android service UI.
    bool moveToServiceEnd(MotionAxis axes, uint8_t speedPercent);
    bool moveToServiceHome(MotionAxis axes, uint8_t speedPercent);
    void stop(MotionAxis axes);

    MotionStatus status(MotionAxis axis) const;
    bool targetsReached(MotionAxis axes) const;
    bool hasFault(MotionAxis axes) const;
    MotionFault firstFault(MotionAxis axes) const;
    bool pollEvent(MotionAxis axis, MotionEvent& event);

private:
    struct AxisRuntime {
        MotionState state = MotionState::UNHOMED;
        MotionFault fault = MotionFault::NONE;
        MotionFault recoveryTrigger = MotionFault::NONE;
        uint16_t warnings = MOTION_WARNING_NONE;
        int32_t targetCounts = 0;
        int32_t activeMaxCounts = 0;
        uint32_t activeMoveTimeoutMs = 0;
        uint8_t speedPercent = 0;
        float lastError = 0.0f;
        int32_t lastObservedCounts = 0;
        uint32_t lastProgressMs = 0;
        uint32_t commandStartedMs = 0;
        uint32_t phaseStartedMs = 0;
        int32_t homeHitCounts = 0;
        uint8_t homingAttempt = 0;
        bool homed = false;
        bool serviceMoveActive = false;
        MotionEvent eventQueue[kEventQueueDepth]{};
        uint8_t eventHead = 0;
        uint8_t eventTail = 0;
        uint8_t eventCount = 0;
        bool recoveryActive = false;
        bool recoveryBackoffEventSent = false;
        // True when a HOME switch contact occurs during an already-homed
        // movement. The contact is handled with the same release sequence
        // as initial homing, without treating it as a new homing command.
        bool homeContactRecoveryActive = false;

    };

    MotionPlatform& platform_;
    const MotionAxisConfig* configs_[2];
    const MotionRecoveryConfig* recoveryConfigs_[2];
    UniversalRecovery recovery_;
    AxisRuntime runtime_[2];
    bool begun_ = false;

    static uint8_t indexOf(MotionAxis axis);
    static int32_t clampCounts(int32_t value, int32_t low, int32_t high);
    static bool elapsed(uint32_t now, uint32_t since, uint32_t duration);

    const MotionAxisConfig& config(MotionAxis axis) const;
    const MotionRecoveryConfig& recoveryConfig(MotionAxis axis) const;
    AxisRuntime& axisRuntime(MotionAxis axis);
    const AxisRuntime& axisRuntime(MotionAxis axis) const;

    void updateAxis(MotionAxis axis, uint32_t now);
    void updateHomingSeek(MotionAxis axis, uint32_t now, int32_t counts);
    void updateHomingRelease(MotionAxis axis, uint32_t now, int32_t counts);
    void updateMoving(MotionAxis axis, uint32_t now, int32_t counts);
    void updateStallBackoff(MotionAxis axis, uint32_t now, int32_t counts);
    void updateStallSettle(MotionAxis axis, uint32_t now, int32_t counts);
    bool beginHomingRecovery(MotionAxis axis, uint32_t now, int32_t counts, MotionFault reason);
    void restartHoming(MotionAxis axis, uint32_t now, int32_t counts);

    bool canMove(MotionAxis axis) const;
    bool inAllowedRange(MotionAxis axis, int32_t counts) const;
    bool atTarget(MotionAxis axis, int32_t counts) const;
    int32_t percentToCounts(MotionAxis axis, uint8_t percent) const;
    uint8_t countsToPercent(MotionAxis axis, int32_t counts) const;
    int16_t speedLimitPwm(MotionAxis axis, uint8_t speedPercent) const;

    bool validateMoveRequest(MotionAxis axes, uint8_t speedPercent) const;
    void beginMove(MotionAxis axis, int32_t targetCounts, int32_t activeMaxCounts,
                   uint32_t activeMoveTimeoutMs, uint8_t speedPercent,
                   bool serviceMove, uint32_t now);

    void observeProgress(MotionAxis axis, uint32_t now, int32_t counts);
    bool stalled(MotionAxis axis, uint32_t now, int32_t counts) const;
    void beginStallRecovery(MotionAxis axis, uint32_t now, int32_t counts, MotionFault reason);
    void reachTarget(MotionAxis axis, int32_t counts);
    void fail(MotionAxis axis, MotionFault fault);
    void emitEvent(MotionAxis axis, MotionEventType type);
};
