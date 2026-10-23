#pragma once

#include "../motion_recovery_config.h"

// ============================================================
// UNIVERSAL MOTOR RECOVERY V1
// ============================================================
// Owns recovery-only state and decisions for both normal movement and homing.
// It deliberately does not own the motor driver, encoder, PD controller, or
// homing PWM. MotionEngine remains responsible for those existing mechanisms.
// This block centralizes the recovery implementation so future recovery
// changes are made here rather than duplicated in test sketches.
class UniversalRecovery {
public:
    enum class Mode : uint8_t { NONE, MOTION, HOMING };

    struct State {
        Mode mode = Mode::NONE;
        uint8_t attempt = 0;
        int8_t direction = 0;
        int8_t returnDirection = 0;
        int32_t targetCounts = 0;
    };

    UniversalRecovery(const MotionRecoveryConfig& leftConfig,
                      const MotionRecoveryConfig& rightConfig);

    void reset(MotionAxis axis);

    bool attemptsAvailable(MotionAxis axis) const;
    uint8_t nextAttempt(MotionAxis axis);
    uint8_t recoveryPwm(MotionAxis axis) const;

    void beginMotion(MotionAxis axis, uint32_t now, int32_t currentCounts, int32_t targetCounts,
                     int32_t low, int32_t high);
    void beginHoming(MotionAxis axis, uint32_t now, int32_t currentCounts,
                     int32_t low, int32_t high);

    bool backoffComplete(MotionAxis axis, uint32_t now, int32_t counts,
                         int32_t positionToleranceCounts) const;
    bool settleComplete(MotionAxis axis, uint32_t now) const;
    void startSettle(MotionAxis axis, uint32_t now);

    Mode mode(MotionAxis axis) const;
    uint8_t attempt(MotionAxis axis) const;
    int8_t direction(MotionAxis axis) const;
    int32_t targetCounts(MotionAxis axis) const;

    const MotionRecoveryConfig& config(MotionAxis axis) const;

private:
    const MotionRecoveryConfig* configs_[2];
    State states_[2];
    uint32_t phaseStartedMs_[2] = {0, 0};

    static uint8_t indexOf(MotionAxis axis);
    static bool elapsed(uint32_t now, uint32_t since, uint32_t duration);
    static int8_t directionTo(int32_t target, int32_t current);
    static int32_t backoffTarget(int32_t currentCounts, int8_t direction,
                                 int32_t backoffCounts, int32_t low, int32_t high);
};
