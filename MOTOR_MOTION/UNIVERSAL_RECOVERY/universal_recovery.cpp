#include "universal_recovery.h"

#include <stdlib.h>

UniversalRecovery::UniversalRecovery(const MotionRecoveryConfig& leftConfig,
                                     const MotionRecoveryConfig& rightConfig)
    : configs_{&leftConfig, &rightConfig} {}

uint8_t UniversalRecovery::indexOf(MotionAxis axis) {
    return axis == MotionAxis::RIGHT ? 1 : 0;
}

bool UniversalRecovery::elapsed(uint32_t now, uint32_t since, uint32_t duration) {
    return static_cast<uint32_t>(now - since) >= duration;
}

int8_t UniversalRecovery::directionTo(int32_t target, int32_t current) {
    return target > current ? 1 : (target < current ? -1 : 0);
}

int32_t UniversalRecovery::backoffTarget(int32_t currentCounts, int8_t direction,
                                         int32_t backoffCounts, int32_t low, int32_t high) {
    const int32_t requested = currentCounts + static_cast<int32_t>(direction) * backoffCounts;
    return requested < low ? low : (requested > high ? high : requested);
}

void UniversalRecovery::reset(MotionAxis axis) {
    states_[indexOf(axis)] = State{};
    phaseStartedMs_[indexOf(axis)] = 0;
}

bool UniversalRecovery::attemptsAvailable(MotionAxis axis) const {
    const State& s = states_[indexOf(axis)];
    const MotionRecoveryConfig& cfg = config(axis);
    return cfg.maxRecoveryAttempts != 0 && s.attempt < cfg.maxRecoveryAttempts;
}

uint8_t UniversalRecovery::nextAttempt(MotionAxis axis) {
    State& s = states_[indexOf(axis)];
    if (s.attempt < 255) ++s.attempt;
    return s.attempt;
}

uint8_t UniversalRecovery::recoveryPwm(MotionAxis axis) const {
    const MotionRecoveryConfig& cfg = config(axis);
    const uint8_t attemptNumber = attempt(axis);
    if (attemptNumber <= 1) return cfg.recoveryPwm1;
    if (attemptNumber == 2) return cfg.recoveryPwm2;
    return cfg.recoveryPwm3;
}

void UniversalRecovery::beginMotion(MotionAxis axis, uint32_t now, int32_t currentCounts,
                                    int32_t target, int32_t low, int32_t high) {
    State& s = states_[indexOf(axis)];
    s.mode = Mode::MOTION;
    s.returnDirection = directionTo(target, currentCounts);
    s.direction = static_cast<int8_t>(-s.returnDirection);
    s.targetCounts = backoffTarget(currentCounts, s.direction,
                                   config(axis).recoveryBackoffCounts, low, high);
    phaseStartedMs_[indexOf(axis)] = now;
}

void UniversalRecovery::beginHoming(MotionAxis axis, uint32_t now, int32_t currentCounts,
                                    int32_t low, int32_t high) {
    State& s = states_[indexOf(axis)];
    s.mode = Mode::HOMING;
    s.returnDirection = config(axis).homeSeekDirection;
    s.direction = static_cast<int8_t>(-config(axis).homeSeekDirection);
    s.targetCounts = backoffTarget(currentCounts, s.direction,
                                   config(axis).recoveryBackoffCounts, low, high);
    phaseStartedMs_[indexOf(axis)] = now;
}

bool UniversalRecovery::backoffComplete(MotionAxis axis, uint32_t now, int32_t counts,
                                        int32_t positionToleranceCounts) const {
    const State& s = states_[indexOf(axis)];
    // Recovery backoff intentionally uses a bounded timer as a second
    // completion condition. The recovery policy is designed around this
    // timeout window: if the mechanism does not reach the bounded backoff
    // target within the window, the attempt still advances to settle/retry.
    // This behavior is deliberate and is preserved for the hardware test.
    return abs(counts - s.targetCounts) <= positionToleranceCounts ||
           elapsed(now, phaseStartedMs_[indexOf(axis)], config(axis).recoveryPhaseTimeoutMs);
}

bool UniversalRecovery::settleComplete(MotionAxis axis, uint32_t now) const {
    return elapsed(now, phaseStartedMs_[indexOf(axis)], config(axis).recoverySettleMs);
}

void UniversalRecovery::startSettle(MotionAxis axis, uint32_t now) {
    phaseStartedMs_[indexOf(axis)] = now;
}

UniversalRecovery::Mode UniversalRecovery::mode(MotionAxis axis) const {
    return states_[indexOf(axis)].mode;
}

uint8_t UniversalRecovery::attempt(MotionAxis axis) const {
    return states_[indexOf(axis)].attempt;
}

int8_t UniversalRecovery::direction(MotionAxis axis) const {
    return states_[indexOf(axis)].direction;
}

int32_t UniversalRecovery::targetCounts(MotionAxis axis) const {
    return states_[indexOf(axis)].targetCounts;
}

const MotionRecoveryConfig& UniversalRecovery::config(MotionAxis axis) const {
    return *configs_[indexOf(axis)];
}
