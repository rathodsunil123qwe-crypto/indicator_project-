#pragma once

#include <Arduino.h>
#include "motion_types.h"

// ============================================================
// MOTOR FAULT / STALL / HOMING / RECOVERY CONFIGURATION V1.2
// ============================================================
// This is the SINGLE source of truth for all parameters that govern how the
// motor system detects abnormal motion, retries, recovers, times out, and
// reports homing status.
//
// If the 3D-printed mechanism changes, tuning for these behaviors is changed
// here without searching through MotionEngine or MotionAxisConfig.

struct MotionRecoveryConfig {
    // Homing behavior.
    int8_t homeSeekDirection;
    int8_t homeReleaseDirection;
    uint8_t homingPwm;
    int32_t homeReleaseCounts;
    uint32_t homeTimeoutMs;
    uint32_t homeReleaseTimeoutMs;

    // Progress/stall detection for both homing seek and normal movement.
    int32_t progressDeltaCounts;
    uint32_t stallTimeoutMs;
    uint32_t moveTimeoutMs;
    uint32_t serviceMoveTimeoutMs;

    // Unified bounded recovery policy for homing and normal movement.
    // maxRecoveryAttempts=3 means initial attempt + 3 recovery retries = 4 total.
    uint8_t maxRecoveryAttempts;
    uint8_t recoveryPwm1;
    uint8_t recoveryPwm2;
    uint8_t recoveryPwm3;
    int32_t recoveryBackoffCounts;
    uint32_t recoveryPhaseTimeoutMs;
    uint32_t recoverySettleMs;

    // Motor safety BACKLEDS. These values belong to the motor safety indication
    // policy, not SceneVM effects. The current observer uses a universal
    // safety palette; the fields remain here as the single tuning location.
    uint8_t safetyLedBrightness;
    uint8_t safetyRedR;
    uint8_t safetyRedG;
    uint8_t safetyRedB;
    uint8_t safetyGreenR;
    uint8_t safetyGreenG;
    uint8_t safetyGreenB;
};

extern const MotionRecoveryConfig kLeftRecoveryConfig;
extern const MotionRecoveryConfig kRightRecoveryConfig;

