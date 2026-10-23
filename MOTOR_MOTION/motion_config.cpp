#include "motion_config.h"

namespace {
// Physical travel and PD tuning only. These values are deliberately separated
// from the recovery policy so a stall/homing change cannot accidentally alter
// the proven travel calibration or controller gains.
constexpr MotionAxisConfig kLeftCore = {
    0,      // homeCounts: encoder count assigned to the fully homed mechanical position. [counts]
    6000,   // sceneMaxCounts: maximum commanded travel/counts available to normal scene motion. [counts]
    10000,  // serviceMaxCounts: maximum commanded travel/counts available to service motion. [counts]
    100,    // hardRangeMarginCounts: permitted encoder margin beyond the active travel range before COUNTER_RANGE. [counts]
    10,     // positionToleranceCounts: acceptable absolute position error for AT_TARGET. [counts]
    0.45f,  // kp: proportional gain of the PD position controller. [unitless]
    0.12f,  // kd: derivative gain of the PD position controller. [unitless]
    90,     // minimumEffectivePwm: minimum PWM magnitude used when a non-zero controller output is commanded. [0..255 PWM]
    255     // maximumPwm: absolute PWM ceiling used by the normal PD speed limiter. [0..255 PWM]
};

constexpr MotionAxisConfig kRightCore = {
    0,      // homeCounts: encoder count assigned to the fully homed mechanical position. [counts]
    6000,   // sceneMaxCounts: maximum commanded travel/counts available to normal scene motion. [counts]
    10000,  // serviceMaxCounts: maximum commanded travel/counts available to service motion. [counts]
    100,    // hardRangeMarginCounts: permitted encoder margin beyond the active travel range before COUNTER_RANGE. [counts]
    10,     // positionToleranceCounts: acceptable absolute position error for AT_TARGET. [counts]
    0.45f,  // kp: proportional gain of the PD position controller. [unitless]
    0.12f,  // kd: derivative gain of the PD position controller. [unitless]
    90,     // minimumEffectivePwm: minimum PWM magnitude used when a non-zero controller output is commanded. [0..255 PWM]
    255     // maximumPwm: absolute PWM ceiling used by the normal PD speed limiter. [0..255 PWM]
};
}

const MotionAxisConfig kLeftMotionConfig = kLeftCore;
const MotionAxisConfig kRightMotionConfig = kRightCore;
