#include "motion_engine.h"

#include <stdlib.h>

namespace {
constexpr MotionAxis kAxes[] = {MotionAxis::LEFT, MotionAxis::RIGHT};
}

const char* motionStateName(MotionState state) {
    switch (state) {
        case MotionState::UNHOMED: return "UNHOMED";
        case MotionState::HOMING_SEEK: return "HOMING_SEEK";
        case MotionState::HOMING_RELEASE: return "HOMING_RELEASE";
        case MotionState::IDLE: return "IDLE";
        case MotionState::MOVING: return "MOVING";
        case MotionState::STALL_BACKOFF: return "STALL_BACKOFF";
        case MotionState::STALL_SETTLE: return "STALL_SETTLE";
        case MotionState::AT_TARGET: return "AT_TARGET";
        case MotionState::FAULT: return "FAULT";
    }
    return "UNKNOWN";
}

const char* motionFaultName(MotionFault fault) {
    switch (fault) {
        case MotionFault::NONE: return "NONE";
        case MotionFault::NOT_HOMED: return "NOT_HOMED";
        case MotionFault::INVALID_COMMAND: return "INVALID_COMMAND";
        case MotionFault::COUNTER_RANGE: return "COUNTER_RANGE";
        case MotionFault::HOME_TIMEOUT: return "HOME_TIMEOUT";
        case MotionFault::HOME_RELEASE_TIMEOUT: return "HOME_RELEASE_TIMEOUT";
        case MotionFault::STALL_TIMEOUT: return "STALL_TIMEOUT";
        case MotionFault::MOVE_TIMEOUT: return "MOVE_TIMEOUT";
        case MotionFault::RECOVERY_EXHAUSTED: return "RECOVERY_EXHAUSTED";
        case MotionFault::HOMING_FAILED: return "HOMING_FAILED";
    }
    return "UNKNOWN";
}

MotionFaultClass motionFaultClass(MotionFault fault) {
    switch (fault) {
        case MotionFault::NONE: return MotionFaultClass::NONE;
        case MotionFault::NOT_HOMED:
        case MotionFault::INVALID_COMMAND: return MotionFaultClass::COMMAND;
        case MotionFault::COUNTER_RANGE: return MotionFaultClass::POSITION_INTEGRITY;
        case MotionFault::HOME_TIMEOUT:
        case MotionFault::HOME_RELEASE_TIMEOUT:
        case MotionFault::STALL_TIMEOUT:
        case MotionFault::MOVE_TIMEOUT: return MotionFaultClass::RECOVERABLE;
        case MotionFault::RECOVERY_EXHAUSTED:
        case MotionFault::HOMING_FAILED: return MotionFaultClass::RECOVERY_FAILED;
    }
    return MotionFaultClass::RECOVERY_FAILED;
}

bool motionFaultRecoverable(MotionFault fault) {
    return motionFaultClass(fault) == MotionFaultClass::RECOVERABLE;
}

const char* motionEventName(MotionEventType type) {
    switch (type) {
        case MotionEventType::NONE: return "NONE";
        case MotionEventType::RECOVERY_STARTED: return "RECOVERY_STARTED";
        case MotionEventType::RECOVERY_BACKOFF: return "RECOVERY_BACKOFF";
        case MotionEventType::RECOVERY_RETRY: return "RECOVERY_RETRY";
        case MotionEventType::RECOVERY_SUCCESS: return "RECOVERY_SUCCESS";
        case MotionEventType::MOTOR_FAULT: return "MOTOR_FAULT";
    }
    return "UNKNOWN";
}

MotionEngine::MotionEngine(MotionPlatform& platform,
                           const MotionAxisConfig& leftConfig,
                           const MotionAxisConfig& rightConfig,
                           const MotionRecoveryConfig& leftRecoveryConfig,
                           const MotionRecoveryConfig& rightRecoveryConfig)
    : platform_(platform), configs_{&leftConfig, &rightConfig},
      recoveryConfigs_{&leftRecoveryConfig, &rightRecoveryConfig},
      recovery_(leftRecoveryConfig, rightRecoveryConfig) {}

bool MotionEngine::begin() {
    if (!platform_.begin()) return false;

    const uint32_t now = millis();
    for (MotionAxis axis : kAxes) {
        AxisRuntime& r = axisRuntime(axis);
        r = AxisRuntime{};
        r.activeMaxCounts = config(axis).sceneMaxCounts;
        r.activeMoveTimeoutMs = recoveryConfig(axis).moveTimeoutMs;
        r.lastObservedCounts = platform_.readEncoderCounts(axis);
        r.lastProgressMs = now;
        platform_.stopMotor(axis);
    }
    begun_ = true;
    return true;
}

void MotionEngine::update() {
    if (!begun_) return;
    const uint32_t now = millis();
    for (MotionAxis axis : kAxes) updateAxis(axis, now);
}

bool MotionEngine::startHoming(MotionAxis axes) {
    if (!begun_ || axes == MotionAxis::NONE) return false;

    const uint32_t now = millis();
    for (MotionAxis axis : kAxes) {
        if (!motionHasAxis(axes, axis)) continue;
        AxisRuntime& r = axisRuntime(axis);
        platform_.stopMotor(axis);
        r.state = MotionState::HOMING_SEEK;
        // A new explicit homing operation clears the previous fault state for
        // this axis; a fresh fault can still be latched if this homing cycle
        // ultimately exhausts its bounded recovery attempts.
        r.warnings = MOTION_WARNING_NONE;
        r.targetCounts = config(axis).homeCounts;
        r.activeMaxCounts = config(axis).sceneMaxCounts;
        r.activeMoveTimeoutMs = recoveryConfig(axis).moveTimeoutMs;
        r.speedPercent = 0;
        r.lastError = 0.0f;
        r.lastObservedCounts = platform_.readEncoderCounts(axis);
        r.lastProgressMs = now;
        r.commandStartedMs = now;
        r.phaseStartedMs = now;
        r.homeHitCounts = r.lastObservedCounts;
        recovery_.reset(axis);
        r.recoveryActive = false;
        r.recoveryTrigger = MotionFault::NONE;
        r.recoveryBackoffEventSent = false;
        r.eventHead = 0;
        r.eventTail = 0;
        r.eventCount = 0;
        r.homingAttempt = 1;
        r.fault = MotionFault::NONE;
        r.homed = false;
        r.homeContactRecoveryActive = false;
        r.serviceMoveActive = false;
    }
    return true;
}

bool MotionEngine::moveToPercent(MotionAxis axes, uint8_t positionPercent, uint8_t speedPercent) {
    // A zero-speed MOVE would otherwise leave WAIT TARGET pending forever.
    if (!begun_ || positionPercent > 100 || !validateMoveRequest(axes, speedPercent)) return false;

    const uint32_t now = millis();
    for (MotionAxis axis : kAxes) {
        if (!motionHasAxis(axes, axis)) continue;
        beginMove(axis, percentToCounts(axis, positionPercent), config(axis).sceneMaxCounts,
                  recoveryConfig(axis).moveTimeoutMs, speedPercent, false, now);
    }
    return true;
}

bool MotionEngine::moveToServiceEnd(MotionAxis axes, uint8_t speedPercent) {
    if (!begun_ || !validateMoveRequest(axes, speedPercent)) return false;
    const uint32_t now = millis();
    for (MotionAxis axis : kAxes) {
        if (!motionHasAxis(axes, axis)) continue;
        beginMove(axis, config(axis).serviceMaxCounts, config(axis).serviceMaxCounts,
                  recoveryConfig(axis).serviceMoveTimeoutMs, speedPercent, true, now);
    }
    return true;
}

bool MotionEngine::moveToServiceHome(MotionAxis axes, uint8_t speedPercent) {
    if (!begun_ || !validateMoveRequest(axes, speedPercent)) return false;
    const uint32_t now = millis();
    for (MotionAxis axis : kAxes) {
        if (!motionHasAxis(axes, axis)) continue;
        beginMove(axis, config(axis).homeCounts, config(axis).serviceMaxCounts,
                  recoveryConfig(axis).serviceMoveTimeoutMs, speedPercent, true, now);
    }
    return true;
}

void MotionEngine::stop(MotionAxis axes) {
    if (!begun_) return;
    for (MotionAxis axis : kAxes) {
        if (!motionHasAxis(axes, axis)) continue;
        AxisRuntime& r = axisRuntime(axis);
        platform_.stopMotor(axis);
        if (r.state != MotionState::FAULT) r.state = r.homed ? MotionState::IDLE : MotionState::UNHOMED;
        r.warnings = MOTION_WARNING_NONE;
    }
}

MotionStatus MotionEngine::status(MotionAxis axis) const {
    const AxisRuntime& r = axisRuntime(axis);
    MotionStatus s{};
    s.state = r.state;
    s.fault = r.fault;
    s.recoveryTrigger = r.recoveryTrigger;
    s.faultClass = motionFaultClass(r.fault);
    s.warnings = r.warnings;
    s.encoderCounts = platform_.readEncoderCounts(axis);
    s.targetCounts = r.targetCounts;
    s.activeMaxCounts = r.activeMaxCounts;
    s.positionPercent = countsToPercent(axis, s.encoderCounts);
    s.speedPercent = r.speedPercent;
    s.appliedPwm = platform_.appliedPwm(axis);
    s.recoveryAttempt = recovery_.attempt(axis);
    s.homingAttempt = r.homingAttempt;
    s.homed = r.homed;
    s.homeSwitchActive = platform_.homeSwitchActive(axis);
    s.serviceMoveActive = r.serviceMoveActive;
    return s;
}

bool MotionEngine::targetsReached(MotionAxis axes) const {
    if (axes == MotionAxis::NONE) return false;
    for (MotionAxis axis : kAxes) {
        if (motionHasAxis(axes, axis) && axisRuntime(axis).state != MotionState::AT_TARGET) return false;
    }
    return true;
}

bool MotionEngine::hasFault(MotionAxis axes) const {
    for (MotionAxis axis : kAxes) {
        if (motionHasAxis(axes, axis) && axisRuntime(axis).state == MotionState::FAULT) return true;
    }
    return false;
}

MotionFault MotionEngine::firstFault(MotionAxis axes) const {
    for (MotionAxis axis : kAxes) {
        if (motionHasAxis(axes, axis) && axisRuntime(axis).fault != MotionFault::NONE) {
            return axisRuntime(axis).fault;
        }
    }
    return MotionFault::NONE;
}

bool MotionEngine::pollEvent(MotionAxis axis, MotionEvent& event) {
    if (axis == MotionAxis::NONE) return false;
    AxisRuntime& r = axisRuntime(axis);
    if (r.eventCount == 0) return false;
    event = r.eventQueue[r.eventTail];
    r.eventQueue[r.eventTail] = MotionEvent{};
    r.eventTail = static_cast<uint8_t>((r.eventTail + 1u) % kEventQueueDepth);
    --r.eventCount;
    return true;
}

uint8_t MotionEngine::indexOf(MotionAxis axis) {
    return axis == MotionAxis::RIGHT ? 1 : 0;
}

int32_t MotionEngine::clampCounts(int32_t value, int32_t low, int32_t high) {
    return value < low ? low : (value > high ? high : value);
}

bool MotionEngine::elapsed(uint32_t now, uint32_t since, uint32_t duration) {
    return static_cast<uint32_t>(now - since) >= duration;
}

const MotionAxisConfig& MotionEngine::config(MotionAxis axis) const {
    return *configs_[indexOf(axis)];
}

const MotionRecoveryConfig& MotionEngine::recoveryConfig(MotionAxis axis) const {
    return *recoveryConfigs_[indexOf(axis)];
}

MotionEngine::AxisRuntime& MotionEngine::axisRuntime(MotionAxis axis) {
    return runtime_[indexOf(axis)];
}

const MotionEngine::AxisRuntime& MotionEngine::axisRuntime(MotionAxis axis) const {
    return runtime_[indexOf(axis)];
}

void MotionEngine::updateAxis(MotionAxis axis, uint32_t now) {
    AxisRuntime& r = axisRuntime(axis);
    if (r.state == MotionState::UNHOMED || r.state == MotionState::IDLE ||
        r.state == MotionState::AT_TARGET || r.state == MotionState::FAULT) return;

    const int32_t counts = platform_.readEncoderCounts(axis);
    switch (r.state) {
        case MotionState::HOMING_SEEK: updateHomingSeek(axis, now, counts); break;
        case MotionState::HOMING_RELEASE: updateHomingRelease(axis, now, counts); break;
        case MotionState::MOVING: updateMoving(axis, now, counts); break;
        case MotionState::STALL_BACKOFF: updateStallBackoff(axis, now, counts); break;
        case MotionState::STALL_SETTLE: updateStallSettle(axis, now, counts); break;
        default: break;
    }
}

void MotionEngine::updateHomingSeek(MotionAxis axis, uint32_t now, int32_t counts) {
    AxisRuntime& r = axisRuntime(axis);
    const MotionRecoveryConfig& rcfg = recoveryConfig(axis);
    if (elapsed(now, r.commandStartedMs, rcfg.homeTimeoutMs)) {
        if (beginHomingRecovery(axis, now, counts, MotionFault::HOME_TIMEOUT)) return;
        return;
    }
    if (platform_.homeSwitchActive(axis)) {
        platform_.stopMotor(axis);
        r.state = MotionState::HOMING_RELEASE;
        r.homeHitCounts = counts;
        r.phaseStartedMs = now;
        r.lastObservedCounts = counts;
        r.lastProgressMs = now;
        platform_.driveSignedPwm(axis, rcfg.homeReleaseDirection * rcfg.homingPwm);
        return;
    }
    if (stalled(axis, now, counts)) {
        if (beginHomingRecovery(axis, now, counts, MotionFault::STALL_TIMEOUT)) return;
        return;
    }
    platform_.driveSignedPwm(axis, rcfg.homeSeekDirection * rcfg.homingPwm);
    observeProgress(axis, now, counts);
}

void MotionEngine::updateHomingRelease(MotionAxis axis, uint32_t now, int32_t counts) {
    AxisRuntime& r = axisRuntime(axis);
    const MotionAxisConfig& cfg = config(axis);
    const MotionRecoveryConfig& rcfg = recoveryConfig(axis);
    if (elapsed(now, r.phaseStartedMs, rcfg.homeReleaseTimeoutMs)) {
        if (beginHomingRecovery(axis, now, counts, MotionFault::HOME_RELEASE_TIMEOUT)) return;
        return;
    }
    const int32_t released = abs(counts - r.homeHitCounts);
    if (released >= rcfg.homeReleaseCounts && !platform_.homeSwitchActive(axis)) {
        platform_.stopMotor(axis);
        platform_.zeroEncoderCounts(axis);
        r.homed = true;
        r.fault = MotionFault::NONE;
        r.lastObservedCounts = cfg.homeCounts;
        r.lastProgressMs = now;

        if (r.homeContactRecoveryActive) {
            // A HOME switch hit during normal operation is treated exactly
            // like the seek -> release portion of homing. The encoder is
            // re-zeroed at the released home position, then the original
            // movement either completes (target is HOME) or resumes toward
            // its original target. No new homing command is generated.
            r.homeContactRecoveryActive = false;
            if (atTarget(axis, cfg.homeCounts)) {
                r.targetCounts = cfg.homeCounts;
                r.state = MotionState::AT_TARGET;
            } else {
                r.commandStartedMs = now;
                r.phaseStartedMs = now;
                r.lastError = 0.0f;
                r.state = MotionState::MOVING;
            }
        } else {
            // Normal initial homing completion. Preserve the original
            // homing behavior exactly.
            r.targetCounts = cfg.homeCounts;
            r.state = MotionState::AT_TARGET;
        }

        if (r.recoveryActive) {
            emitEvent(axis, MotionEventType::RECOVERY_SUCCESS);
            r.recoveryActive = false;
        }
        return;
    }
    platform_.driveSignedPwm(axis, rcfg.homeReleaseDirection * rcfg.homingPwm);
    observeProgress(axis, now, counts);
}

void MotionEngine::updateMoving(MotionAxis axis, uint32_t now, int32_t counts) {
    AxisRuntime& r = axisRuntime(axis);
    const MotionAxisConfig& cfg = config(axis);
    const MotionRecoveryConfig& rcfg = recoveryConfig(axis);

    // HOME switches remain active after successful homing. If normal movement
    // reaches HOME again, treat that contact as the same physical HOME event
    // used during homing: stop, release the switch, re-zero the encoder, then
    // finish/resume the original movement. This prevents a valid HOME contact
    // from becoming a stall/recovery fault.
    if (platform_.homeSwitchActive(axis)) {
        platform_.stopMotor(axis);
        r.homeContactRecoveryActive = true;
        r.homeHitCounts = counts;
        r.phaseStartedMs = now;
        r.lastObservedCounts = counts;
        r.lastProgressMs = now;
        r.state = MotionState::HOMING_RELEASE;
        platform_.driveSignedPwm(axis, rcfg.homeReleaseDirection * rcfg.homingPwm);
        return;
    }

    if (!inAllowedRange(axis, counts)) {
        fail(axis, MotionFault::COUNTER_RANGE);
        return;
    }
    // MOVE_TIMEOUT is a recoverable mechanical/runtime trigger, not an
    // immediate terminal fault. It enters the same bounded recovery path as
    // a normal stall so homing and movement share one recovery mechanism.
    if (elapsed(now, r.commandStartedMs, r.activeMoveTimeoutMs)) {
        beginStallRecovery(axis, now, counts, MotionFault::MOVE_TIMEOUT);
        return;
    }
    if (atTarget(axis, counts)) {
        reachTarget(axis, counts);
        return;
    }
    if (stalled(axis, now, counts)) {
        beginStallRecovery(axis, now, counts, MotionFault::STALL_TIMEOUT);
        return;
    }

    const int32_t error = r.targetCounts - counts;
    const float errorFloat = static_cast<float>(error);
    const float errorDelta = errorFloat - r.lastError;
    float output = cfg.kp * errorFloat + cfg.kd * errorDelta; // PD: no I term.
    const int16_t limit = speedLimitPwm(axis, r.speedPercent);
    if (output > limit) output = limit;
    if (output < -limit) output = -limit;
    if (output > 0 && output < cfg.minEffectivePwm) output = cfg.minEffectivePwm;
    if (output < 0 && output > -cfg.minEffectivePwm) output = -cfg.minEffectivePwm;

    platform_.driveSignedPwm(axis, static_cast<int16_t>(output));
    r.lastError = errorFloat;
    observeProgress(axis, now, counts);
}

void MotionEngine::updateStallBackoff(MotionAxis axis, uint32_t now, int32_t counts) {
    AxisRuntime& r = axisRuntime(axis);
    const MotionAxisConfig& cfg = config(axis);
    if (!r.recoveryBackoffEventSent) {
        r.recoveryBackoffEventSent = true;
        emitEvent(axis, MotionEventType::RECOVERY_BACKOFF);
    }
    if (recovery_.backoffComplete(axis, now, counts, cfg.positionToleranceCounts)) {
        platform_.stopMotor(axis);
        r.state = MotionState::STALL_SETTLE;
        recovery_.startSettle(axis, now);
        return;
    }
    platform_.driveSignedPwm(axis, recovery_.direction(axis) * recovery_.recoveryPwm(axis));
}

void MotionEngine::updateStallSettle(MotionAxis axis, uint32_t now, int32_t counts) {
    AxisRuntime& r = axisRuntime(axis);
    if (!recovery_.settleComplete(axis, now)) return;

    if (recovery_.mode(axis) == UniversalRecovery::Mode::HOMING) {
        emitEvent(axis, MotionEventType::RECOVERY_RETRY);
        restartHoming(axis, now, counts);
        return;
    }

    emitEvent(axis, MotionEventType::RECOVERY_RETRY);
    r.state = MotionState::MOVING;
    r.commandStartedMs = now;
    r.lastError = 0.0f;
    r.lastObservedCounts = counts;
    r.lastProgressMs = now;
}

bool MotionEngine::canMove(MotionAxis axis) const {
    const AxisRuntime& r = axisRuntime(axis);
    // A terminal recovery failure stops and indicates the motor fault, but it
    // must not permanently block a fresh movement command. A new movement
    // command is allowed to clear only the recovery-failed latch; all other
    // FAULT states remain command-blocking. Homing states remain blocked.
    const bool recoveryFailedFault =
        r.state == MotionState::FAULT &&
        motionFaultClass(r.fault) == MotionFaultClass::RECOVERY_FAILED &&
        r.fault == MotionFault::RECOVERY_EXHAUSTED;

    return r.homed &&
           (r.state != MotionState::FAULT || recoveryFailedFault) &&
           r.state != MotionState::HOMING_SEEK &&
           r.state != MotionState::HOMING_RELEASE;
}

bool MotionEngine::inAllowedRange(MotionAxis axis, int32_t counts) const {
    const MotionAxisConfig& cfg = config(axis);
    const AxisRuntime& r = axisRuntime(axis);
    return counts >= cfg.homeCounts - cfg.hardRangeMarginCounts &&
           counts <= r.activeMaxCounts + cfg.hardRangeMarginCounts;
}

bool MotionEngine::atTarget(MotionAxis axis, int32_t counts) const {
    return abs(counts - axisRuntime(axis).targetCounts) <= config(axis).positionToleranceCounts;
}

int32_t MotionEngine::percentToCounts(MotionAxis axis, uint8_t percent) const {
    const MotionAxisConfig& cfg = config(axis);
    const int32_t span = cfg.sceneMaxCounts - cfg.homeCounts;
    return cfg.homeCounts + (span * static_cast<int32_t>(percent)) / 100;
}

uint8_t MotionEngine::countsToPercent(MotionAxis axis, int32_t counts) const {
    const MotionAxisConfig& cfg = config(axis);
    const int32_t span = cfg.sceneMaxCounts - cfg.homeCounts;
    if (span <= 0) return 0;
    const int32_t bounded = clampCounts(counts, cfg.homeCounts, cfg.sceneMaxCounts);
    return static_cast<uint8_t>(((bounded - cfg.homeCounts) * 100L) / span);
}

int16_t MotionEngine::speedLimitPwm(MotionAxis axis, uint8_t speedPercent) const {
    const MotionAxisConfig& cfg = config(axis);
    if (speedPercent == 0) return 0;
    int16_t pwm = static_cast<int16_t>((static_cast<int32_t>(cfg.maxPwm) * speedPercent) / 100);
    if (pwm > 0 && pwm < cfg.minEffectivePwm) pwm = cfg.minEffectivePwm;
    return pwm;
}


bool MotionEngine::validateMoveRequest(MotionAxis axes, uint8_t speedPercent) const {
    if (axes == MotionAxis::NONE || speedPercent == 0 || speedPercent > 100) return false;
    // Validate the complete request before changing either motor, so BOTH is atomic.
    for (MotionAxis axis : kAxes) {
        if (motionHasAxis(axes, axis) && !canMove(axis)) return false;
    }
    return true;
}

void MotionEngine::beginMove(MotionAxis axis, int32_t targetCounts, int32_t activeMaxCounts,
                             uint32_t activeMoveTimeoutMs, uint8_t speedPercent,
                             bool serviceMove, uint32_t now) {
    AxisRuntime& r = axisRuntime(axis);
    r.targetCounts = targetCounts;
    r.activeMaxCounts = activeMaxCounts;
    r.activeMoveTimeoutMs = activeMoveTimeoutMs;
    r.speedPercent = speedPercent;
    r.lastError = 0.0f;
    r.lastObservedCounts = platform_.readEncoderCounts(axis);
    r.lastProgressMs = now;
    r.commandStartedMs = now;
    r.phaseStartedMs = now;
    recovery_.reset(axis);
    // A fresh movement command is the explicit recovery-failure reset.
    // Preserve the existing recovery algorithm and fault indication until
    // this new command arrives; then clear only the terminal recovery latch.
    if (r.state == MotionState::FAULT && r.fault == MotionFault::RECOVERY_EXHAUSTED) {
        r.fault = MotionFault::NONE;
    }
    r.recoveryActive = false;
    r.recoveryTrigger = MotionFault::NONE;
    r.recoveryBackoffEventSent = false;
    r.homeContactRecoveryActive = false;
    r.eventHead = 0;
    r.eventTail = 0;
    r.eventCount = 0;
    r.warnings = MOTION_WARNING_NONE;
    r.serviceMoveActive = serviceMove;
    r.state = MotionState::MOVING;
}

void MotionEngine::observeProgress(MotionAxis axis, uint32_t now, int32_t counts) {
    AxisRuntime& r = axisRuntime(axis);
    if (abs(counts - r.lastObservedCounts) >= recoveryConfig(axis).progressDeltaCounts) {
        r.lastObservedCounts = counts;
        r.lastProgressMs = now;
    }
}

bool MotionEngine::stalled(MotionAxis axis, uint32_t now, int32_t counts) const {
    const AxisRuntime& r = axisRuntime(axis);
    const MotionRecoveryConfig& rcfg = recoveryConfig(axis);
    return abs(counts - r.lastObservedCounts) < rcfg.progressDeltaCounts &&
           elapsed(now, r.lastProgressMs, rcfg.stallTimeoutMs);
}

void MotionEngine::beginStallRecovery(MotionAxis axis, uint32_t now, int32_t counts, MotionFault reason) {
    AxisRuntime& r = axisRuntime(axis);
    const MotionAxisConfig& cfg = config(axis);
    if (!recovery_.attemptsAvailable(axis)) {
        // If recovery was disabled before the first attempt, preserve the
        // actual trigger (for example MOVE_TIMEOUT). Otherwise the bounded
        // recovery policy has been exhausted.
        fail(axis, recovery_.attempt(axis) == 0 ? reason : MotionFault::RECOVERY_EXHAUSTED);
        return;
    }

    recovery_.nextAttempt(axis);
    r.warnings |= MOTION_WARNING_RECOVERY_ACTIVE;
    r.recoveryActive = true;
    r.recoveryBackoffEventSent = false;
    r.recoveryTrigger = reason;
    emitEvent(axis, MotionEventType::RECOVERY_STARTED);
    recovery_.beginMotion(axis, now, counts, r.targetCounts, cfg.homeCounts, r.activeMaxCounts);
    if (recovery_.direction(axis) == 0) {
        reachTarget(axis, counts);
        return;
    }
    r.phaseStartedMs = now;
    r.state = MotionState::STALL_BACKOFF;
    platform_.driveSignedPwm(axis, recovery_.direction(axis) * recovery_.recoveryPwm(axis));
}

bool MotionEngine::beginHomingRecovery(MotionAxis axis, uint32_t now, int32_t counts,
                                       MotionFault reason) {
    AxisRuntime& r = axisRuntime(axis);
    const MotionAxisConfig& cfg = config(axis);
    if (!recovery_.attemptsAvailable(axis)) {
        fail(axis, recovery_.attempt(axis) == 0 ? reason : MotionFault::HOMING_FAILED);
        return false;
    }

    recovery_.nextAttempt(axis);
    r.homingAttempt = static_cast<uint8_t>(recovery_.attempt(axis) + 1u);
    r.warnings |= MOTION_WARNING_RECOVERY_ACTIVE;
    r.recoveryActive = true;
    r.recoveryBackoffEventSent = false;
    r.recoveryTrigger = reason;
    emitEvent(axis, MotionEventType::RECOVERY_STARTED);
    recovery_.beginHoming(axis, now, counts, cfg.homeCounts, r.activeMaxCounts);
    r.phaseStartedMs = now;
    r.state = MotionState::STALL_BACKOFF;
    platform_.driveSignedPwm(axis, recovery_.direction(axis) * recovery_.recoveryPwm(axis));
    return true;
}

void MotionEngine::restartHoming(MotionAxis axis, uint32_t now, int32_t counts) {
    AxisRuntime& r = axisRuntime(axis);
    const MotionAxisConfig& cfg = config(axis);
    const MotionRecoveryConfig& rcfg = recoveryConfig(axis);
    platform_.stopMotor(axis);
    r.state = MotionState::HOMING_SEEK;
    r.warnings = MOTION_WARNING_NONE;
    r.targetCounts = cfg.homeCounts;
    r.activeMaxCounts = cfg.sceneMaxCounts;
    r.activeMoveTimeoutMs = rcfg.moveTimeoutMs;
    r.speedPercent = 0;
    r.lastError = 0.0f;
    r.lastObservedCounts = counts;
    r.lastProgressMs = now;
    r.commandStartedMs = now;
    r.phaseStartedMs = now;
    r.homeHitCounts = counts;
    r.homeContactRecoveryActive = false;
    r.serviceMoveActive = false;
    // Recovery attempt count intentionally remains accumulated across retries.
}

void MotionEngine::reachTarget(MotionAxis axis, int32_t counts) {
    AxisRuntime& r = axisRuntime(axis);
    platform_.stopMotor(axis); // Gearbox/lead screw holds mechanically; never active-hold here.
    r.state = MotionState::AT_TARGET;
    r.lastObservedCounts = counts;
    r.lastError = 0.0f;
    r.warnings = MOTION_WARNING_NONE;
    r.serviceMoveActive = false;
    if (r.recoveryActive) {
        emitEvent(axis, MotionEventType::RECOVERY_SUCCESS);
        r.recoveryActive = false;
    }
}

void MotionEngine::fail(MotionAxis axis, MotionFault fault) {
    AxisRuntime& r = axisRuntime(axis);
    platform_.stopMotor(axis);
    r.state = MotionState::FAULT;
    r.fault = fault;
    r.warnings = MOTION_WARNING_NONE;
    r.serviceMoveActive = false;
    r.recoveryActive = false;
    emitEvent(axis, MotionEventType::MOTOR_FAULT);
}

void MotionEngine::emitEvent(MotionAxis axis, MotionEventType type) {
    AxisRuntime& r = axisRuntime(axis);
    if (r.eventCount >= kEventQueueDepth) {
        // Keep the newest event rather than blocking the motion loop. The
        // queue is deliberately small and bounded for embedded use.
        r.eventTail = static_cast<uint8_t>((r.eventTail + 1u) % kEventQueueDepth);
        --r.eventCount;
    }
    MotionEvent& slot = r.eventQueue[r.eventHead];
    slot.type = type;
    slot.axis = axis;
    slot.fault = r.fault;
    slot.recoveryTrigger = r.recoveryTrigger;
    slot.recoveryAttempt = recovery_.attempt(axis);
    slot.timestampMs = millis();
    r.eventHead = static_cast<uint8_t>((r.eventHead + 1u) % kEventQueueDepth);
    ++r.eventCount;
}
