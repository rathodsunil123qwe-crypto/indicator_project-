#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "../EVENT_MANAGER/event_manager_v1.h"

namespace IndicatorPhysicalController {

class PhysicalControllerV1 {
public:
    static constexpr uint8_t LEFT_PIN   = 14;
    static constexpr uint8_t MIDDLE_PIN = 36; // VP / GPIO36, input-only
    static constexpr uint8_t RIGHT_PIN  = 39; // VN / GPIO39, input-only

    static constexpr uint32_t DEBOUNCE_MS = 30;
    static constexpr uint32_t MIDDLE_CLICK_GAP_MS = 350;
    static constexpr uint8_t MAX_MIDDLE_CLICKS = 32;

    // Configure inputs and arm the controller. This must be called only after
    // the motor system has successfully completed homing.
    void begin();

    // Poll the physical controller. Returns true only when a complete logical
    // event is ready for the existing Event Manager/runtime path.
    bool poll(IndicatorEventManager::Event& event);

    bool active() const { return active_; }

private:
    struct ButtonState {
        uint8_t pin = 0;
        bool stableHigh = true;
        bool lastRawHigh = true;
        uint32_t rawChangedMs = 0;
    };

    ButtonState left_;
    ButtonState middle_;
    ButtonState right_;

    bool active_ = false;
    bool middleClickArmed_ = false;
    uint8_t middleClickCount_ = 0;
    uint32_t middleLastReleaseMs_ = 0;

    static bool readHigh(uint8_t pin);
    static void initializeButton(ButtonState& button, uint8_t pin);
    static bool updateButton(ButtonState& button, uint32_t now);

    bool pollMiddle(uint32_t now, IndicatorEventManager::Event& event);
    static IndicatorEventManager::Event eventForMiddleClicks(uint8_t clicks);
};

} // namespace IndicatorPhysicalController
