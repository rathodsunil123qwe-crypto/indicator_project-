#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace IndicatorNoPlate {

// NO-PLATE selector toggle input.
// Wired between GPIO17 and GND; internal pull-up keeps the idle state HIGH.
static constexpr uint8_t NO_PLATE_SIDE_BUTTON_PIN = 17;

enum class SideSelection : uint8_t {
    NORMAL = 0,   // GPIO HIGH
    GRAPHIC = 1   // GPIO LOW
};

class NoPlateButtonV1 {
public:
    bool begin();
    SideSelection selection() const;
    bool normalSide() const;
    bool graphicSide() const;
    bool changed();
    int rawState() const;

private:
    int lastRawState_ = HIGH;
};

} // namespace IndicatorNoPlate
