#include "esp32_doit_v1_platform.h"

#include <driver/pcnt.h>

namespace {
constexpr int LEFT_MOTOR_IN1 = 25;
constexpr int LEFT_MOTOR_IN2 = 26;
constexpr int LEFT_ENCODER_A = 34;
constexpr int LEFT_ENCODER_B = 35;
constexpr int LEFT_HOME_PIN = 4;

constexpr int RIGHT_MOTOR_IN1 = 13;
constexpr int RIGHT_MOTOR_IN2 = 27;
constexpr int RIGHT_ENCODER_A = 32;
constexpr int RIGHT_ENCODER_B = 33;
constexpr int RIGHT_HOME_PIN = 5;

constexpr uint8_t LEFT_PCNT_UNIT = PCNT_UNIT_0;
constexpr uint8_t RIGHT_PCNT_UNIT = PCNT_UNIT_1;
constexpr uint32_t PWM_FREQUENCY_HZ = 20000;
constexpr uint8_t PWM_RESOLUTION_BITS = 8;
constexpr uint16_t ENCODER_FILTER = 1000;

int pinFor(MotionAxis axis, bool first) {
    if (axis == MotionAxis::LEFT) return first ? LEFT_MOTOR_IN1 : LEFT_MOTOR_IN2;
    return first ? RIGHT_MOTOR_IN1 : RIGHT_MOTOR_IN2;
}

pcnt_unit_t pcntFor(MotionAxis axis) {
    return axis == MotionAxis::LEFT ? static_cast<pcnt_unit_t>(LEFT_PCNT_UNIT)
                                    : static_cast<pcnt_unit_t>(RIGHT_PCNT_UNIT);
}

int homePinFor(MotionAxis axis) {
    return axis == MotionAxis::LEFT ? LEFT_HOME_PIN : RIGHT_HOME_PIN;
}
}

bool Esp32DoitV1MotionPlatform::begin() {
    // These assignments are the proven V4 hardware map; do not share them
    // with the LED runtime.
    ledcAttach(LEFT_MOTOR_IN1, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
    ledcAttach(LEFT_MOTOR_IN2, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
    ledcAttach(RIGHT_MOTOR_IN1, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
    ledcAttach(RIGHT_MOTOR_IN2, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
    stopMotor(MotionAxis::LEFT);
    stopMotor(MotionAxis::RIGHT);

    configureQuadrature(LEFT_PCNT_UNIT, LEFT_ENCODER_A, LEFT_ENCODER_B);
    configureQuadrature(RIGHT_PCNT_UNIT, RIGHT_ENCODER_A, RIGHT_ENCODER_B);
    pinMode(LEFT_HOME_PIN, INPUT_PULLUP);
    pinMode(RIGHT_HOME_PIN, INPUT_PULLUP);
    return true;
}

int32_t Esp32DoitV1MotionPlatform::readEncoderCounts(MotionAxis axis) {
    int16_t raw = 0;
    pcnt_get_counter_value(pcntFor(axis), &raw);
    // This sign inversion is intentionally preserved from the proven V4
    // PCNT implementation. With its quadrature channel settings, counts grow
    // positive while moving outward from HOME.
    return -static_cast<int32_t>(raw);
}

void Esp32DoitV1MotionPlatform::zeroEncoderCounts(MotionAxis axis) {
    const pcnt_unit_t unit = pcntFor(axis);
    pcnt_counter_pause(unit);
    pcnt_counter_clear(unit);
    pcnt_counter_resume(unit);
}

bool Esp32DoitV1MotionPlatform::homeSwitchActive(MotionAxis axis) {
    return digitalRead(homePinFor(axis)) == LOW;
}

void Esp32DoitV1MotionPlatform::driveSignedPwm(MotionAxis axis, int16_t pwm) {
    pwm = constrain(pwm, -255, 255);
    const int firstPin = pinFor(axis, true);
    const int secondPin = pinFor(axis, false);
    if (pwm >= 0) {
        ledcWrite(firstPin, static_cast<uint32_t>(pwm));
        ledcWrite(secondPin, 0);
    } else {
        ledcWrite(firstPin, 0);
        ledcWrite(secondPin, static_cast<uint32_t>(-pwm));
    }
    appliedPwm_[indexOf(axis)] = pwm;
}

void Esp32DoitV1MotionPlatform::stopMotor(MotionAxis axis) {
    driveSignedPwm(axis, 0);
}

int16_t Esp32DoitV1MotionPlatform::appliedPwm(MotionAxis axis) const {
    return appliedPwm_[indexOf(axis)];
}

uint8_t Esp32DoitV1MotionPlatform::indexOf(MotionAxis axis) {
    return axis == MotionAxis::RIGHT ? 1 : 0;
}

void Esp32DoitV1MotionPlatform::configureQuadrature(uint8_t pcntUnit, int pinA, int pinB) {
    const pcnt_unit_t unit = static_cast<pcnt_unit_t>(pcntUnit);

    pcnt_config_t channelA = {};
    channelA.pulse_gpio_num = pinA;
    channelA.ctrl_gpio_num = pinB;
    channelA.channel = PCNT_CHANNEL_0;
    channelA.unit = unit;
    channelA.pos_mode = PCNT_COUNT_INC;
    channelA.neg_mode = PCNT_COUNT_DEC;
    channelA.lctrl_mode = PCNT_MODE_REVERSE;
    channelA.hctrl_mode = PCNT_MODE_KEEP;
    channelA.counter_h_lim = 32767;
    channelA.counter_l_lim = -32768;
    pcnt_unit_config(&channelA);

    pcnt_config_t channelB = {};
    channelB.pulse_gpio_num = pinB;
    channelB.ctrl_gpio_num = pinA;
    channelB.channel = PCNT_CHANNEL_1;
    channelB.unit = unit;
    channelB.pos_mode = PCNT_COUNT_DEC;
    channelB.neg_mode = PCNT_COUNT_INC;
    channelB.lctrl_mode = PCNT_MODE_REVERSE;
    channelB.hctrl_mode = PCNT_MODE_KEEP;
    channelB.counter_h_lim = 32767;
    channelB.counter_l_lim = -32768;
    pcnt_unit_config(&channelB);

    pcnt_set_filter_value(unit, ENCODER_FILTER);
    pcnt_filter_enable(unit);
    pcnt_counter_pause(unit);
    pcnt_counter_clear(unit);
    pcnt_counter_resume(unit);
}
