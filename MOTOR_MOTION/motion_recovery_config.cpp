#include "motion_recovery_config.h"

namespace {
// All recovery values are centralized here. The field order intentionally
// matches MotionRecoveryConfig exactly; comments label every value so tuning
// can be reviewed without decoding positional initializer lists.
constexpr MotionRecoveryConfig kLeft = {
    -1,    // homeSeekDirection: signed motor direction used while seeking the HOME switch. [direction: -1 or +1]
    +1,    // homeReleaseDirection: signed motor direction used to release from the HOME switch. [direction: -1 or +1]
    100,   // homingPwm: PWM magnitude used during homing seek/release. [0..255 PWM]
    50,    // homeReleaseCounts: encoder travel required to leave the HOME switch after detection. [counts]
    15000, // homeTimeoutMs: maximum time allowed for the homing seek phase before recovery/failure. [ms]
    1500,  // homeReleaseTimeoutMs: maximum time allowed for the HOME switch release phase. [ms]

    1,     // progressDeltaCounts: minimum encoder movement that counts as progress. [counts]
    500,   // stallTimeoutMs: time without progress before a commanded motor is treated as stalled. [ms]
    12000, // moveTimeoutMs: maximum normal scene-move duration before MOVE_TIMEOUT. [ms]
    25000, // serviceMoveTimeoutMs: maximum service-move duration before MOVE_TIMEOUT. [ms]

    3,     // maxRecoveryAttempts: number of recovery retries after the initial attempt. [retries]
    70,    // recoveryPwm1: PWM used for recovery attempt 1. [0..255 PWM]
    90,    // recoveryPwm2: PWM used for recovery attempt 2. [0..255 PWM]
    100,   // recoveryPwm3: PWM used for recovery attempt 3 and later. [0..255 PWM]
    1000,  // recoveryBackoffCounts: distance moved away from the failed direction/target before retry. [counts]
    700,   // recoveryPhaseTimeoutMs: maximum duration of the STALL_BACKOFF phase. [ms]
    20,    // recoverySettleMs: pause after backoff before retrying the operation. [ms]

    100,   // safetyLedBrightness: BACKLEDS brightness used by motor safety indication. [0..255]
    255,   // safetyRedR: red-channel value for active homing/fault indication. [0..255]
    0,     // safetyRedG: green-channel value for active homing/fault indication. [0..255]
    0,     // safetyRedB: blue-channel value for active homing/fault indication. [0..255]
    0,     // safetyGreenR: red-channel value for successful-homing indication. [0..255]
    255,   // safetyGreenG: green-channel value for successful-homing indication. [0..255]
    0      // safetyGreenB: blue-channel value for successful-homing indication. [0..255]
};

constexpr MotionRecoveryConfig kRight = {
    -1,    // homeSeekDirection: signed motor direction used while seeking the HOME switch. [direction: -1 or +1]
    +1,    // homeReleaseDirection: signed motor direction used to release from the HOME switch. [direction: -1 or +1]
    100,   // homingPwm: PWM magnitude used during homing seek/release. [0..255 PWM]
    50,    // homeReleaseCounts: encoder travel required to leave the HOME switch after detection. [counts]
    15000, // homeTimeoutMs: maximum time allowed for the homing seek phase before recovery/failure. [ms]
    1500,  // homeReleaseTimeoutMs: maximum time allowed for the HOME switch release phase. [ms]

    1,     // progressDeltaCounts: minimum encoder movement that counts as progress. [counts]
    500,   // stallTimeoutMs: time without progress before a commanded motor is treated as stalled. [ms]
    12000, // moveTimeoutMs: maximum normal scene-move duration before MOVE_TIMEOUT. [ms]
    25000, // serviceMoveTimeoutMs: maximum service-move duration before MOVE_TIMEOUT. [ms]

    3,     // maxRecoveryAttempts: number of recovery retries after the initial attempt. [retries]
    70,    // recoveryPwm1: PWM used for recovery attempt 1. [0..255 PWM]
    90,    // recoveryPwm2: PWM used for recovery attempt 2. [0..255 PWM]
    100,   // recoveryPwm3: PWM used for recovery attempt 3 and later. [0..255 PWM]
    1000,  // recoveryBackoffCounts: distance moved away from the failed direction/target before retry. [counts]
    700,   // recoveryPhaseTimeoutMs: maximum duration of the STALL_BACKOFF phase. [ms]
    20,    // recoverySettleMs: pause after backoff before retrying the operation. [ms]

    100,   // safetyLedBrightness: BACKLEDS brightness used by motor safety indication. [0..255]
    255,   // safetyRedR: red-channel value for active homing/fault indication. [0..255]
    0,     // safetyRedG: green-channel value for active homing/fault indication. [0..255]
    0,     // safetyRedB: blue-channel value for active homing/fault indication. [0..255]
    0,     // safetyGreenR: red-channel value for successful-homing indication. [0..255]
    255,   // safetyGreenG: green-channel value for successful-homing indication. [0..255]
    0      // safetyGreenB: blue-channel value for successful-homing indication. [0..255]
};
}

const MotionRecoveryConfig kLeftRecoveryConfig = kLeft;
const MotionRecoveryConfig kRightRecoveryConfig = kRight;
