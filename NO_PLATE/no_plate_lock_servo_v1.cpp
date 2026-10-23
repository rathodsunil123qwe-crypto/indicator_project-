#include "no_plate_lock_servo_v1.h"

namespace NoPlateLockServoV1 {

static State st = State::DETACHED;
static uint32_t lockTimerMs = 0;
static uint32_t phaseStartedMs = 0;
static bool attached = false;

static bool attachAndWrite(uint32_t duty) {
    if (!attached) {
        if (!ledcAttach(SERVO_PIN, SERVO_FREQUENCY_HZ, SERVO_RESOLUTION_BITS)) {
            return false;
        }
        attached = true;
    }

    ledcWrite(SERVO_PIN, duty);
    return true;
}

bool begin() {
    if (attached) {
        ledcDetach(SERVO_PIN);
        attached = false;
    }
    st = State::DETACHED;
    lockTimerMs = 0;
    phaseStartedMs = 0;
    return true;
}

bool requestUnlock(uint32_t timerMs) {
    if (st == State::UNLOCKED_WAITING || st == State::LOCKING) {
        return false;
    }

    if (timerMs == 0) {
        return false;
    }

    if (!attachAndWrite(NO_PLATE_UNLOCK_DUTY)) {
        st = State::DETACHED;
        return false;
    }

    lockTimerMs = timerMs;
    phaseStartedMs = millis();
    st = State::UNLOCKED_WAITING;
    return true;
}

void update() {
    const uint32_t now = millis();

    if (st == State::UNLOCKED_WAITING) {
        if ((uint32_t)(now - phaseStartedMs) >= lockTimerMs) {
            if (!attachAndWrite(NO_PLATE_LOCK_DUTY)) {
                detach();
                return;
            }
            phaseStartedMs = now;
            st = State::LOCKING;
        }
        return;
    }

    if (st == State::LOCKING) {
        if ((uint32_t)(now - phaseStartedMs) >= LOCK_HOLD_MS) {
            if (attached) {
                ledcDetach(SERVO_PIN);
                attached = false;
            }
            st = State::LOCKED;
        }
    }
}

void detach() {
    if (attached) {
        ledcDetach(SERVO_PIN);
        attached = false;
    }
    st = State::DETACHED;
    lockTimerMs = 0;
    phaseStartedMs = 0;
}

State state() {
    return st;
}

} // namespace NoPlateLockServoV1
