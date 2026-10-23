#include "physical_controller_v1.h"

namespace IndicatorPhysicalController {

bool PhysicalControllerV1::readHigh(uint8_t pin) {
    return digitalRead(pin) != LOW;
}

void PhysicalControllerV1::initializeButton(ButtonState& button, uint8_t pin) {
    button.pin = pin;
    // External pull-ups are fitted by hardware. INPUT is deliberately used
    // for every controller pin; none of these pins is ever driven as output.
    pinMode(pin, INPUT);
    const bool high = readHigh(pin);
    button.stableHigh = high;
    button.lastRawHigh = high;
    button.rawChangedMs = millis();
}

bool PhysicalControllerV1::updateButton(ButtonState& button, uint32_t now) {
    const bool rawHigh = readHigh(button.pin);

    if (rawHigh != button.lastRawHigh) {
        button.lastRawHigh = rawHigh;
        button.rawChangedMs = now;
    }

    if (rawHigh != button.stableHigh &&
        (uint32_t)(now - button.rawChangedMs) >= DEBOUNCE_MS) {
        button.stableHigh = rawHigh;
        return true;
    }

    return false;
}

void PhysicalControllerV1::begin() {
    // The controller is intentionally not configured or touched during boot
    // or homing. Calling begin() is the activation boundary.
    initializeButton(left_, LEFT_PIN);
    initializeButton(middle_, MIDDLE_PIN);
    initializeButton(right_, RIGHT_PIN);

    middleClickArmed_ = middle_.stableHigh;
    middleClickCount_ = 0;
    middleLastReleaseMs_ = 0;
    active_ = true;
}

IndicatorEventManager::Event PhysicalControllerV1::eventForMiddleClicks(uint8_t clicks) {
    IndicatorEventManager::Event event{};
    if (clicks < 1 || clicks > MAX_MIDDLE_CLICKS) return event;

    event.type = static_cast<IndicatorEventManager::EventType>(
        static_cast<uint8_t>(IndicatorEventManager::EventType::MIDDLE_1_CLICK) +
        static_cast<uint8_t>(clicks - 1));
    event.middleClickCount = clicks;
    return event;
}

bool PhysicalControllerV1::pollMiddle(uint32_t now,
                                       IndicatorEventManager::Event& event) {
    const bool changed = updateButton(middle_, now);
    if (changed) {
        // A press is a HIGH -> LOW transition. A button that was already held
        // when begin() ran does not generate a synthetic click.
        if (!middle_.stableHigh) {
            if (middleClickCount_ < MAX_MIDDLE_CLICKS) {
                ++middleClickCount_;
            }
            middleClickArmed_ = true;
        } else if (middleClickCount_ > 0) {
            middleLastReleaseMs_ = now;
        }
    }

    if (middleClickCount_ > 0 &&
        middle_.stableHigh &&
        middleLastReleaseMs_ != 0 &&
        (uint32_t)(now - middleLastReleaseMs_) >= MIDDLE_CLICK_GAP_MS) {
        event = eventForMiddleClicks(middleClickCount_);
        middleClickCount_ = 0;
        middleLastReleaseMs_ = 0;
        return event.valid();
    }

    return false;
}

bool PhysicalControllerV1::poll(IndicatorEventManager::Event& event) {
    event = IndicatorEventManager::Event{};
    if (!active_) return false;

    const uint32_t now = millis();

    if (updateButton(left_, now) && !left_.stableHigh) {
        event.type = IndicatorEventManager::EventType::LEFT;
        return true;
    }

    if (updateButton(right_, now) && !right_.stableHigh) {
        event.type = IndicatorEventManager::EventType::RIGHT;
        return true;
    }

    if (pollMiddle(now, event)) {
        return true;
    }

    return false;
}

} // namespace IndicatorPhysicalController
